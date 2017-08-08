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
  blocks. The VFE has different input interfaces. The PIX input interface feeds
  the input data to the image processing pipeline. Three RDI input interfaces
  bypass the image processing pipeline. The VFE also contains the AXI bus
  interface which writes the output data to memory.


Supported functionality
-----------------------

The current version of the driver supports:

- input from camera sensor via CSIPHY;
- generation of test input data by the TG in CSID;
- raw dump of the input data to memory. RDI interface of VFE is supported.
  PIX interface (ISP processing, statistics engines, resize/crop, format
  conversion) is not supported in the current version;
- concurrent and independent usage of two data inputs - could be camera sensors
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
- 3 VFE sub-devices - VFE is represented by a number of sub-devices equal to
  the number of RDI input interfaces.

The considerations to split the driver in this particular way are as follows:

- representing CSIPHY and CSID modules by a separate sub-device for each module
  allows to model the hardware links between these modules;
- representing VFE by a separate sub-devices for each RDI input interface allows
  to use the three RDI interfaces concurently and independently as this is
  supported by the hardware;
- representing ISPIF by a number of sub-devices equal to the number of CSID
  sub-devices allows to create linear media controller pipelines when using two
  cameras simultaneously. This avoids branches in the pipelines which otherwise
  will require a) userspace and b) media framework (e.g. power on/off
  operations) to  make assumptions about the data flow from a sink pad to a
  source pad on a single media entity.

Each VFE sub-device is linked to a separate video device node.

The complete list of the media entities (V4L2 sub-devices and video device
nodes) is as follows:

- msm_csiphy0
- msm_csiphy1
- msm_csid0
- msm_csid1
- msm_ispif0
- msm_ispif1
- msm_vfe0_rdi0
- msm_vfe0_video0
- msm_vfe0_rdi1
- msm_vfe0_video1
- msm_vfe0_rdi2
- msm_vfe0_video2


Implementation
--------------

Runtime configuration of the hardware (updating settings while streaming) is
not required to implement the currently supported functionality. The complete
configuration on each hardware module is applied on STREAMON ioctl based on
the current active media links, formats and controls set.


Documentation
-------------

APQ8016 Specification:
https://developer.qualcomm.com/download/sd410/snapdragon-410-processor-device-specification.pdf
Referenced 2016-11-24.


References
----------

.. [#f1] https://source.codeaurora.org/quic/la/kernel/msm-3.10/
