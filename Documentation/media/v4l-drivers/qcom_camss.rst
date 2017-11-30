.. include:: <isonum.txt>

Qualcomm Camera Subsystem driver
================================

Introduction
------------

This file documents the Qualcomm Camera Subsystem driver located under
drivers/media/platform/qcom/camss-8x16.

The current version of the driver supports the Camera Subsystem found on
Qualcomm MSM8916 and APQ8016 processors.

The driver implements V4L2, Media controller and V4L2 subdev interfaces.
Camera sensor using V4L2 subdev interface in the kernel is supported.

The driver is implemented using as a reference the Qualcomm Camera Subsystem
driver for Android as found in Code Aurora [#f1]_.


Qualcomm Camera Subsystem hardware
----------------------------------

The Camera Subsystem hardware found on 8x16 processors and supported by the
driver consists of:

- 2 CSIPHY modules. They handle the Physical layer of the CSI2 receivers.
  A separate camera sensor can be connected to each of the CSIPHY module;
- 2 CSID (CSI Decoder) modules. They handle the Protocol and Application layer
  of the CSI2 receivers. A CSID can decode data stream from any of the CSIPHY.
  Each CSID also contains a TG (Test Generator) block which can generate
  artificial input data for test purposes;
- ISPIF (ISP Interface) module. Handles the routing of the data streams from
  the CSIDs to the inputs of the VFE;
- VFE (Video Front End) module. Contains a pipeline of image processing hardware
  blocks. The VFE has different input interfaces. The PIX (Pixel) input
  interface feeds the input data to the image processing pipeline. The image
  processing pipeline contains also a scale and crop module at the end. Three
  RDI (Raw Dump Interface) input interfaces bypass the image processing
  pipeline. The VFE also contains the AXI bus interface which writes the output
  data to memory.


Supported functionality
-----------------------

The current version of the driver supports:

- Input from camera sensor via CSIPHY;
- Generation of test input data by the TG in CSID;
- RDI interface of VFE - raw dump of the input data to memory.

  Supported formats:

  - YUYV/UYVY/YVYU/VYUY (packed YUV 4:2:2 - V4L2_PIX_FMT_YUYV /
    V4L2_PIX_FMT_UYVY / V4L2_PIX_FMT_YVYU / V4L2_PIX_FMT_VYUY);
  - MIPI RAW8 (8bit Bayer RAW - V4L2_PIX_FMT_SRGGB8 /
    V4L2_PIX_FMT_SGRBG8 / V4L2_PIX_FMT_SGBRG8 / V4L2_PIX_FMT_SBGGR8);
  - MIPI RAW10 (10bit packed Bayer RAW - V4L2_PIX_FMT_SBGGR10P /
    V4L2_PIX_FMT_SGBRG10P / V4L2_PIX_FMT_SGRBG10P / V4L2_PIX_FMT_SRGGB10P);
  - MIPI RAW12 (12bit packed Bayer RAW - V4L2_PIX_FMT_SRGGB12P /
    V4L2_PIX_FMT_SGBRG12P / V4L2_PIX_FMT_SGRBG12P / V4L2_PIX_FMT_SRGGB12P).

- PIX interface of VFE

  - Format conversion of the input data.

    Supported input formats:

    - YUYV/UYVY/YVYU/VYUY (packed YUV 4:2:2 - V4L2_PIX_FMT_YUYV /
      V4L2_PIX_FMT_UYVY / V4L2_PIX_FMT_YVYU / V4L2_PIX_FMT_VYUY).

    Supported output formats:

    - NV12/NV21 (two plane YUV 4:2:0 - V4L2_PIX_FMT_NV12 / V4L2_PIX_FMT_NV21);
    - NV16/NV61 (two plane YUV 4:2:2 - V4L2_PIX_FMT_NV16 / V4L2_PIX_FMT_NV61).

  - Scaling support. Configuration of the VFE Encoder Scale module
    for downscalling with ratio up to 16x.

  - Cropping support. Configuration of the VFE Encoder Crop module.

- Concurrent and independent usage of two data inputs - could be camera sensors
  and/or TG.


Driver Architecture and Design
------------------------------

The driver implements the V4L2 subdev interface. With the goal to model the
hardware links between the modules and to expose a clean, logical and usable
interface, the driver is split into V4L2 sub-devices as follows:

- 2 CSIPHY sub-devices - each CSIPHY is represented by a single sub-device;
- 2 CSID sub-devices - each CSID is represented by a single sub-device;
- 2 ISPIF sub-devices - ISPIF is represented by a number of sub-devices equal
  to the number of CSID sub-devices;
- 4 VFE sub-devices - VFE is represented by a number of sub-devices equal to
  the number of the input interfaces (3 RDI and 1 PIX).

The considerations to split the driver in this particular way are as follows:

- representing CSIPHY and CSID modules by a separate sub-device for each module
  allows to model the hardware links between these modules;
- representing VFE by a separate sub-devices for each input interface allows
  to use the input interfaces concurently and independently as this is
  supported by the hardware;
- representing ISPIF by a number of sub-devices equal to the number of CSID
  sub-devices allows to create linear media controller pipelines when using two
  cameras simultaneously. This avoids branches in the pipelines which otherwise
  will require a) userspace and b) media framework (e.g. power on/off
  operations) to  make assumptions about the data flow from a sink pad to a
  source pad on a single media entity.

Each VFE sub-device is linked to a separate video device node.

The media controller pipeline graph is as follows (with connected two OV5645
camera sensors):

.. _qcom_camss_graph:

.. kernel-figure:: qcom_camss_graph.dot
    :alt:   qcom_camss_graph.dot
    :align: center

    Media pipeline graph


Implementation
--------------

Runtime configuration of the hardware (updating settings while streaming) is
not required to implement the currently supported functionality. The complete
configuration on each hardware module is applied on STREAMON ioctl based on
the current active media links, formats and controls set.

The output size of the scaler module in the VFE is configured with the actual
compose selection rectangle on the sink pad of the 'msm_vfe0_pix' entity.

The crop output area of the crop module in the VFE is configured with the actual
crop selection rectangle on the source pad of the 'msm_vfe0_pix' entity.


Documentation
-------------

APQ8016 Specification:
https://developer.qualcomm.com/download/sd410/snapdragon-410-processor-device-specification.pdf
Referenced 2016-11-24.


References
----------

.. [#f1] https://source.codeaurora.org/quic/la/kernel/msm-3.10/
