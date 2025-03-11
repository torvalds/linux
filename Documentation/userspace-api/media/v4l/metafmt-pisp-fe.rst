.. SPDX-License-Identifier: GPL-2.0

.. _v4l2-meta-fmt-rpi-fe-cfg:

************************
V4L2_META_FMT_RPI_FE_CFG
************************

Raspberry Pi PiSP Front End configuration format
================================================

The Raspberry Pi PiSP Front End image signal processor is configured by
userspace by providing a buffer of configuration parameters to the
`rp1-cfe-fe-config` output video device node using the
:c:type:`v4l2_meta_format` interface.

The `Raspberry Pi PiSP technical specification
<https://datasheets.raspberrypi.com/camera/raspberry-pi-image-signal-processor-specification.pdf>`_
provide detailed description of the Front End configuration and programming
model.

.. _v4l2-meta-fmt-rpi-fe-stats:

**************************
V4L2_META_FMT_RPI_FE_STATS
**************************

Raspberry Pi PiSP Front End statistics format
=============================================

The Raspberry Pi PiSP Front End image signal processor provides statistics data
by writing to a buffer provided via the `rp1-cfe-fe-stats` capture video device
node using the
:c:type:`v4l2_meta_format` interface.

The `Raspberry Pi PiSP technical specification
<https://datasheets.raspberrypi.com/camera/raspberry-pi-image-signal-processor-specification.pdf>`_
provide detailed description of the Front End configuration and programming
model.
