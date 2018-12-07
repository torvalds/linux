.. include:: <isonum.txt>

===============================================================
Intel Image Processing Unit 3 (IPU3) Imaging Unit (ImgU) driver
===============================================================

Copyright |copy| 2018 Intel Corporation

Introduction
============

This file documents Intel IPU3 (3rd generation Image Processing Unit) Imaging
Unit driver located under drivers/media/pci/intel/ipu3.

The Intel IPU3 found in certain Kaby Lake (as well as certain Sky Lake)
platforms (U/Y processor lines) is made up of two parts namely Imaging Unit
(ImgU) and CIO2 device (MIPI CSI2 receiver).

The CIO2 device receives the raw bayer data from the sensors and outputs the
frames in a format that is specific to IPU3 (for consumption by IPU3 ImgU).
CIO2 driver is available as drivers/media/pci/intel/ipu3/ipu3-cio2* and is
enabled through the CONFIG_VIDEO_IPU3_CIO2 config option.

The Imaging Unit (ImgU) is responsible for processing images captured
through IPU3 CIO2 device. The ImgU driver sources can be found under
drivers/media/pci/intel/ipu3 directory. The driver is enabled through the
CONFIG_VIDEO_IPU3_IMGU config option.

The two driver modules are named ipu3-csi2 and ipu3-imgu, respectively.

The driver has been tested on Kaby Lake platforms (U/Y processor lines).

The driver implements V4L2, Media controller and V4L2 sub-device interfaces.
Camera sensors that have CSI-2 bus, which are connected to the IPU3 CIO2
device are supported. Support for lens and flash drivers depends on the
above sensors.

ImgU device nodes
=================

The ImgU is represented as two V4L2 subdevs, each of which provides a V4L2
subdev interface to the user space.

Each V4L2 subdev represents a pipe, which can support a maximum of 2
streams. A private ioctl can be used to configure the mode (video or still)
of the pipe.

This helps to support advanced camera features like Continuous View Finder
(CVF) and Snapshot During Video(SDV).

CIO2 device
===========

The CIO2 is represented as a single V4L2 subdev, which provides a V4L2 subdev
interface to the user space. There is a video node for each CSI-2 receiver,
with a single media controller interface for the entire device.

Media controller
----------------

The media device interface allows to configure the ImgU links, which defines
the behavior of the IPU3 firmware.

Device operation
----------------

With IPU3, once the input video node ("ipu3-imgu 0/1":0,
in <entity>:<pad-number> format) is queued with buffer (in packed raw bayer
format), IPU3 ISP starts processing the buffer and produces the video output
in YUV format and statistics output on respective output nodes. The driver
is expected to have buffers ready for all of parameter, output and
statistics nodes, when input video node is queued with buffer.

At a minimum, all of input, main output, 3A statistics and viewfinder
video nodes should be enabled for IPU3 to start image processing.

Each ImgU V4L2 subdev has the following set of video nodes.

input, output and viewfinder video nodes
----------------------------------------

The frames (in packed raw bayer format specific to IPU3) received by the
input video node is processed by the IPU3 Imaging Unit and is output to 2
video nodes, with each targeting different purpose (main output and viewfinder
output).

Details on raw bayer format specific to IPU3 can be found as below.
Documentation/media/uapi/v4l/pixfmt-meta-intel-ipu3.rst

The driver supports V4L2 Video Capture Interface as defined at :ref:`devices`.

Only the multi-planar API is supported. More details can be found at
:ref:`planar-apis`.


parameters video node
---------------------

The parameter video node receives the ISP algorithm parameters that are used
to configure how the ISP algorithms process the image.

Details on raw bayer format specific to IPU3 can be found as below.
Documentation/media/uapi/v4l/pixfmt-meta-intel-ipu3.rst

3A statistics video node
------------------------

3A statistics video node is used by the ImgU driver to output the 3A (auto
focus, auto exposure and auto white balance) statistics for the frames that
are being processed by the ISP to user space applications. User space
applications can use this statistics data to arrive at desired algorithm
parameters for ISP.

CIO2 device nodes
=================

CIO2 is represented as a single V4L2 sub-device with a video node for each
CSI-2 receiver. The video node represents the DMA engine.

Configuring the Intel IPU3
==========================

The Intel IPU3 ImgU driver supports V4L2 interface. Using V4L2 ioctl calls,
the ISP can be configured and enabled.

The IPU3 ImgU pipelines can be configured using media controller APIs,
defined at :ref:`media_controller`.

Capturing frames in raw bayer format
------------------------------------

IPU3 MIPI CSI2 receiver is used to capture frames (in packed raw bayer
format) from the raw sensors connected to the CSI2 ports. The captured
frames are used as input to the ImgU driver.

Image processing using IPU3 ImgU requires tools such as v4l2n [#f1]_,
raw2pnm [#f1]_, and yavta [#f2]_ due to the following unique requirements
and / or features specific to IPU3.

-- The IPU3 CSI2 receiver outputs the captured frames from the sensor in
packed raw bayer format that is specific to IPU3

-- Multiple video nodes have to be operated simultaneously

Let us take the example of ov5670 sensor connected to CSI2 port 0, for a
2592x1944 image capture.

Using the media contorller APIs, the ov5670 sensor is configured to send
frames in packed raw bayer format to IPU3 CSI2 receiver.

# This example assumes /dev/media0 as the ImgU media device

export MDEV=/dev/media0

# and that ov5670 sensor is connected to i2c bus 10 with address 0x36

export SDEV="ov5670 10-0036"

# Establish the link for the media devices using media-ctl [#f3]_
media-ctl -d $MDEV -l "ov5670 ":0 -> "ipu3-csi2 0":0[1]

media-ctl -d $MDEV -l "ipu3-csi2 0":1 -> "ipu3-cio2 0":0[1]

# Set the format for the media devices
media-ctl -d $MDEV -V "ov5670 ":0 [fmt:SGRBG10/2592x1944]

media-ctl -d $MDEV -V "ipu3-csi2 0":0 [fmt:SGRBG10/2592x1944]

media-ctl -d $MDEV -V "ipu3-csi2 0":1 [fmt:SGRBG10/2592x1944]

Once the media pipeline is configured, desired sensor specific settings
(such as exposure and gain settings) can be set, using the yavta tool.

e.g

yavta -w 0x009e0903 444 $(media-ctl -d $MDEV -e "$SDEV")

yavta -w 0x009e0913 1024 $(media-ctl -d $MDEV -e "$SDEV")

yavta -w 0x009e0911 2046 $(media-ctl -d $MDEV -e "$SDEV")

Once the desired sensor settings are set, frame captures can be done as below.

e.g

yavta --data-prefix -u -c10 -n5 -I -s2592x1944 --file=/tmp/frame-#.bin
-f IPU3_GRBG10 media-ctl -d $MDEV -e ipu3-cio2 0

With the above command, 10 frames are captured at 2592x1944 resolution, with
sGRBG10 format and output as IPU3_GRBG10 format.

The captured frames are available as /tmp/frame-#.bin files.

Processing the image in raw bayer format
----------------------------------------

Configuring ImgU V4L2 subdev for image processing
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The ImgU V4L2 subdevs have to be configured with media controller APIs to
have all the video nodes setup correctly.

Let us take "ipu3-imgu 0" subdev as an example.

media-ctl -d $MDEV -r

media-ctl -d $MDEV -l "ipu3-imgu 0 input":0 -> "ipu3-imgu 0":0[1]

media-ctl -d $MDEV -l "ipu3-imgu 0":2 -> "output":0[1]

media-ctl -d $MDEV -l "ipu3-imgu 0":3 -> "viewfinder":0[1]

media-ctl -d $MDEV -l "ipu3-imgu 0":4 -> "3a stat":0[1]

Also the pipe mode of the corresponding V4L2 subdev should be set as
desired (e.g 0 for video mode or 1 for still mode) through the
control id 0x009819a1 as below.

e.g

v4l2n -d /dev/v4l-subdev7 --ctrl=0x009819A1=1

RAW bayer frames go through the following ISP pipeline HW blocks to
have the processed image output to the DDR memory.

RAW bayer frame -> Input Feeder -> Bayer Down Scaling (BDS) -> Geometric
Distortion Correction (GDC) -> DDR

The ImgU V4L2 subdev has to be configured with the supported resolutions
in all the above HW blocks, for a given input resolution.

For a given supported resolution for an input frame, the Input Feeder,
Bayer Down Scaling and GDC blocks should be configured with the supported
resolutions. This information can be obtained by looking at the following
IPU3 ISP configuration table.

https://chromium.googlesource.com/chromiumos/overlays/board-overlays/+/master

Under baseboard-poppy/media-libs/arc-camera3-hal-configs-poppy/files/gcss
directory, graph_settings_ov5670.xml can be used as an example.

The following steps prepare the ImgU ISP pipeline for the image processing.

1. The ImgU V4L2 subdev data format should be set by using the
VIDIOC_SUBDEV_S_FMT on pad 0, using the GDC width and height obtained above.

2. The ImgU V4L2 subdev cropping should be set by using the
VIDIOC_SUBDEV_S_SELECTION on pad 0, with V4L2_SEL_TGT_CROP as the target,
using the input feeder height and width.

3. The ImgU V4L2 subdev composing should be set by using the
VIDIOC_SUBDEV_S_SELECTION on pad 0, with V4L2_SEL_TGT_COMPOSE as the target,
using the BDS height and width.

For the ov5670 example, for an input frame with a resolution of 2592x1944
(which is input to the ImgU subdev pad 0), the corresponding resolutions
for input feeder, BDS and GDC are 2592x1944, 2592x1944 and 2560x1920
respectively.

Once this is done, the received raw bayer frames can be input to the ImgU
V4L2 subdev as below, using the open source application v4l2n.

For an image captured with 2592x1944 [#f4]_ resolution, with desired output
resolution as 2560x1920 and viewfinder resolution as 2560x1920, the following
v4l2n command can be used. This helps process the raw bayer frames and
produces the desired results for the main output image and the viewfinder
output, in NV12 format.

v4l2n --pipe=4 --load=/tmp/frame-#.bin --open=/dev/video4
--fmt=type:VIDEO_OUTPUT_MPLANE,width=2592,height=1944,pixelformat=0X47337069
--reqbufs=type:VIDEO_OUTPUT_MPLANE,count:1 --pipe=1 --output=/tmp/frames.out
--open=/dev/video5
--fmt=type:VIDEO_CAPTURE_MPLANE,width=2560,height=1920,pixelformat=NV12
--reqbufs=type:VIDEO_CAPTURE_MPLANE,count:1 --pipe=2 --output=/tmp/frames.vf
--open=/dev/video6
--fmt=type:VIDEO_CAPTURE_MPLANE,width=2560,height=1920,pixelformat=NV12
--reqbufs=type:VIDEO_CAPTURE_MPLANE,count:1 --pipe=3 --open=/dev/video7
--output=/tmp/frames.3A --fmt=type:META_CAPTURE,?
--reqbufs=count:1,type:META_CAPTURE --pipe=1,2,3,4 --stream=5

where /dev/video4, /dev/video5, /dev/video6 and /dev/video7 devices point to
input, output, viewfinder and 3A statistics video nodes respectively.

Converting the raw bayer image into YUV domain
----------------------------------------------

The processed images after the above step, can be converted to YUV domain
as below.

Main output frames
~~~~~~~~~~~~~~~~~~

raw2pnm -x2560 -y1920 -fNV12 /tmp/frames.out /tmp/frames.out.pnm

where 2560x1920 is output resolution, NV12 is the video format, followed
by input frame and output PNM file.

Viewfinder output frames
~~~~~~~~~~~~~~~~~~~~~~~~

raw2pnm -x2560 -y1920 -fNV12 /tmp/frames.vf /tmp/frames.vf.pnm

where 2560x1920 is output resolution, NV12 is the video format, followed
by input frame and output PNM file.

Example user space code for IPU3
================================

User space code that configures and uses IPU3 is available here.

https://chromium.googlesource.com/chromiumos/platform/arc-camera/+/master/

The source can be located under hal/intel directory.

References
==========

include/uapi/linux/intel-ipu3.h

.. [#f1] https://github.com/intel/nvt

.. [#f2] http://git.ideasonboard.org/yavta.git

.. [#f3] http://git.ideasonboard.org/?p=media-ctl.git;a=summary

.. [#f4] ImgU limitation requires an additional 16x16 for all input resolutions
