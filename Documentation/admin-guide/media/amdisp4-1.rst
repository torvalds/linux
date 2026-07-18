.. SPDX-License-Identifier: GPL-2.0

.. include:: <isonum.txt>

====================================
AMD Image Signal Processor (amdisp4)
====================================

Introduction
============

This file documents the driver for the AMD ISP4 that is part of
AMD Ryzen AI Max 300 Series.

The driver is located under drivers/media/platform/amd/isp4 and uses
the Media-Controller API.

The driver exposes one video capture device to userspace and provide
web camera like interface. Internally the video device is connected
to the isp4 sub-device responsible for communication with the CCPU FW.

Topology
========

.. _amdisp4_topology_graph:

.. kernel-figure:: amdisp4.dot
     :alt:   Diagram of the media pipeline topology
     :align: center



The driver has 1 sub-device: Representing isp4 image signal processor.
The driver has 1 video device: Capture device for retrieving images.

- ISP4 Image Signal Processing Subdevice Node

---------------------------------------------

The isp4 is represented as a single V4L2 subdev, the sub-device does not
provide interface to the user space. The sub-device is connected to one video node
(isp4_capture) with immutable active link. The sub-device represents ISP with
connected sensor similar to smart cameras (sensors with integrated ISP).
sub-device has only one link to the video device for capturing the frames.
The sub-device communicates with CCPU FW for streaming configuration and
buffer management.


- isp4_capture - Frames Capture Video Node

------------------------------------------

Isp4_capture is a capture device to capture frames to memory.
The entity is connected to isp4 sub-device. The video device
provides web camera like interface to userspace. It supports
mmap and dma buf types of memory.

Capturing Video Frames Example
==============================

.. code-block:: bash

         v4l2-ctl "-d" "/dev/video0" "--set-fmt-video=width=1920,height=1080,pixelformat=NV12" "--stream-mmap" "--stream-count=10"
