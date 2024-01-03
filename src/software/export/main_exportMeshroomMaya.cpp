// This file is part of the AliceVision project.
// Copyright (c) 2017 AliceVision contributors.
// This Source Code Form is subject to the terms of the Mozilla Public License,
// v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include <aliceVision/sfmData/SfMData.hpp>
#include <aliceVision/sfmDataIO/sfmDataIO.hpp>
#include <aliceVision/image/all.hpp>
#include <aliceVision/system/main.hpp>
#include <aliceVision/system/ProgressDisplay.hpp>
#include <aliceVision/cmdline/cmdline.hpp>
#include <boost/program_options.hpp>

#include <OpenImageIO/imagebufalgo.h>

#include <filesystem>
#include <fstream>

// These constants define the current software version.
// They must be updated when the command line is changed.
#define ALICEVISION_SOFTWARE_VERSION_MAJOR 1
#define ALICEVISION_SOFTWARE_VERSION_MINOR 0

using namespace aliceVision;

namespace po = boost::program_options;
namespace fs = std::filesystem;
namespace oiio = OIIO;

int aliceVision_main(int argc, char **argv)
{
  // command-line parameters
  std::string sfmDataFilename;
  std::string outputFolder;

  po::options_description requiredParams("Required parameters");
  requiredParams.add_options()
    ("input,i", po::value<std::string>(&sfmDataFilename)->required(),
      "SfMData file.")
    ("output,o", po::value<std::string>(&outputFolder)->required(),
      "Output folder.");

  CmdLine cmdline("AliceVision exportMeshroomMaya");
  cmdline.add(requiredParams);
  if (!cmdline.execute(argc, argv))
  {
      return EXIT_FAILURE;
  }

  // create output folders
  if(!fs::is_directory(outputFolder))
    fs::create_directory(outputFolder);
  if(!fs::is_directory(outputFolder + "/undistort/"))
    fs::create_directory(outputFolder + "/undistort/");
  if(!fs::is_directory(outputFolder + "/undistort/proxy/"))
    fs::create_directory(outputFolder + "/undistort/proxy/");
  if(!fs::is_directory(outputFolder + "/undistort/thumbnail/"))
    fs::create_directory(outputFolder + "/undistort/thumbnail/");

  // read the SfM scene
  sfmData::SfMData sfmData;
  if(!sfmDataIO::load(sfmData, sfmDataFilename, sfmDataIO::ESfMData::ALL))
  {
    ALICEVISION_LOG_ERROR("Error: The input SfMData file '" + sfmDataFilename + "' cannot be read.");
    return EXIT_FAILURE;
  }

  // export the SfM scene to an alembic at the root of the output folder
  ALICEVISION_LOG_INFO("Exporting SfM scene for MeshroomMaya ...");
  sfmDataIO::save(sfmData, outputFolder + "/scene.abc", sfmDataIO::ESfMData::ALL);

  // export undistorted images and thumbnail images
  auto progressDisplay = system::createConsoleProgressDisplay(sfmData.getViews().size(), std::cout,
                                                              "Exporting Images for MeshroomMaya\n");
  for(auto& viewPair : sfmData.getViews())
  {
    const sfmData::View& view = *viewPair.second;
    const std::shared_ptr<camera::IntrinsicBase> intrinsicPtr = sfmData.getIntrinsicsharedPtr(view.getIntrinsicId());

    if(intrinsicPtr == nullptr)
    {
      ALICEVISION_LOG_ERROR("Error: Can't find intrinsic id '" + std::to_string(view.getIntrinsicId()) + "' in SfMData file.");
      return EXIT_FAILURE;
    }

    image::Image<image::RGBColor> image, imageUd;
    image::readImage(view.getImage().getImagePath(), image, image::EImageColorSpace::LINEAR);

    // compute undistorted image
    if(intrinsicPtr->isValid() && intrinsicPtr->hasDistortion())
      camera::UndistortImage(image, intrinsicPtr.get(), imageUd, image::BLACK, true);
    else
      imageUd = image;

    // export images
    oiio::ImageBuf imageBuf;
    image::getBufferFromImage(imageUd, imageBuf);

    image::Image<image::RGBColor> imageProxy(image.Width()/2, image.Height()/2);
    image::Image<image::RGBColor> imageThumbnail(256, image.Height() / (image.Width() / 256.0f)); // width = 256px, keep height ratio

    oiio::ImageBuf proxyBuf;
    oiio::ImageBuf thumbnailBuf;

    image::getBufferFromImage(imageProxy, proxyBuf);
    image::getBufferFromImage(imageThumbnail, thumbnailBuf);

    const oiio::ROI proxyROI(0, imageProxy.Width(), 0, imageProxy.Height(), 0, 1, 0, 3);
    const oiio::ROI thumbnailROI(0, imageThumbnail.Width(), 0, imageThumbnail.Height(), 0, 1, 0, 3);

    oiio::ImageBufAlgo::resample(proxyBuf,     imageBuf, false,     proxyROI); // no interpolation
    oiio::ImageBufAlgo::resample(thumbnailBuf, imageBuf, false, thumbnailROI); // no interpolation

    const std::string basename = fs::path(view.getImage().getImagePath()).stem().string();

    image::writeImage(outputFolder + "/undistort/proxy/" + basename + "-" + std::to_string(view.getViewId()) + "-UOP.jpg",
                      imageProxy, image::ImageWriteOptions());

    image::writeImage(outputFolder + "/undistort/thumbnail/" + basename + "-" + std::to_string(view.getViewId()) + "-UOT.jpg",
                      imageThumbnail, image::ImageWriteOptions());

    ++progressDisplay;
  }

  return EXIT_SUCCESS;
}
