.. SPDX-License-Identifier: GPL-2.0

.. _v4l2-meta-fmt-rpi-be-cfg:

************************
V4L2_META_FMT_RPI_BE_CFG
************************

Raspberry Pi PiSP Back End configuration format
===============================================

The Raspberry Pi PiSP Back End memory-to-memory image signal processor is
configured by userspace by providing a buffer of configuration parameters
to the `pispbe-config` output video device node using the
:c:type:`v4l2_meta_format` interface.

The PiSP Back End processes images in tiles, and its configuration requires
specifying two different sets of parameters by populating the members of
:c:type:`pisp_be_tiles_config` defined in the ``pisp_be_config.h`` header file.

The `Raspberry Pi PiSP technical specification
<https://datasheets.raspberrypi.com/camera/raspberry-pi-image-signal-processor-specification.pdf>`_
provide detailed description of the ISP back end configuration and programming
model.

Global configuration data
-------------------------

The global configuration data describe how the pixels in a particular image are
to be processed and is therefore shared across all the tiles of the image. So
for example, LSC (Lens Shading Correction) or Denoise parameters would be common
across all tiles from the same frame.

Global configuration data are passed to the ISP by populating the member of
:c:type:`pisp_be_config`.

Tile parameters
---------------

As the ISP processes images in tiles, each set of tiles parameters describe how
a single tile in an image is going to be processed. A single set of tile
parameters consist of 160 bytes of data and to process a batch of tiles several
sets of tiles parameters are required.

Tiles parameters are passed to the ISP by populating the member of
``pisp_tile`` and the ``num_tiles`` fields of :c:type:`pisp_be_tiles_config`.

Raspberry Pi PiSP Back End uAPI data types
==========================================

This section describes the data types exposed to userspace by the Raspberry Pi
PiSP Back End. The section is informative only, for a detailed description of
each field refer to the `Raspberry Pi PiSP technical specification
<https://datasheets.raspberrypi.com/camera/raspberry-pi-image-signal-processor-specification.pdf>`_.

.. kernel-doc:: include/uapi/linux/media/raspberrypi/pisp_be_config.h
