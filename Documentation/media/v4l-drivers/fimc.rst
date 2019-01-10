.. SPDX-License-Identifier: GPL-2.0

.. include:: <isonum.txt>

The Samsung S5P/EXYNOS4 FIMC driver
===================================

Copyright |copy| 2012 - 2013 Samsung Electronics Co., Ltd.

The FIMC (Fully Interactive Mobile Camera) device available in Samsung
SoC Application Processors is an integrated camera host interface, color
space converter, image resizer and rotator.  It's also capable of capturing
data from LCD controller (FIMD) through the SoC internal writeback data
path.  There are multiple FIMC instances in the SoCs (up to 4), having
slightly different capabilities, like pixel alignment constraints, rotator
availability, LCD writeback support, etc. The driver is located at
drivers/media/platform/exynos4-is directory.

Supported SoCs
--------------

S5PC100 (mem-to-mem only), S5PV210, EXYNOS4210

Supported features
------------------

- camera parallel interface capture (ITU-R.BT601/565);
- camera serial interface capture (MIPI-CSI2);
- memory-to-memory processing (color space conversion, scaling, mirror
  and rotation);
- dynamic pipeline re-configuration at runtime (re-attachment of any FIMC
  instance to any parallel video input or any MIPI-CSI front-end);
- runtime PM and system wide suspend/resume

Not currently supported
-----------------------

- LCD writeback input
- per frame clock gating (mem-to-mem)

Files partitioning
------------------

- media device driver
  drivers/media/platform/exynos4-is/media-dev.[ch]

- camera capture video device driver
  drivers/media/platform/exynos4-is/fimc-capture.c

- MIPI-CSI2 receiver subdev
  drivers/media/platform/exynos4-is/mipi-csis.[ch]

- video post-processor (mem-to-mem)
  drivers/media/platform/exynos4-is/fimc-core.c

- common files
  drivers/media/platform/exynos4-is/fimc-core.h
  drivers/media/platform/exynos4-is/fimc-reg.h
  drivers/media/platform/exynos4-is/regs-fimc.h

User space interfaces
---------------------

Media device interface
~~~~~~~~~~~~~~~~~~~~~~

The driver supports Media Controller API as defined at :ref:`media_controller`.
The media device driver name is "SAMSUNG S5P FIMC".

The purpose of this interface is to allow changing assignment of FIMC instances
to the SoC peripheral camera input at runtime and optionally to control internal
connections of the MIPI-CSIS device(s) to the FIMC entities.

The media device interface allows to configure the SoC for capturing image
data from the sensor through more than one FIMC instance (e.g. for simultaneous
viewfinder and still capture setup).
Reconfiguration is done by enabling/disabling media links created by the driver
during initialization. The internal device topology can be easily discovered
through media entity and links enumeration.

Memory-to-memory video node
~~~~~~~~~~~~~~~~~~~~~~~~~~~

V4L2 memory-to-memory interface at /dev/video? device node.  This is standalone
video device, it has no media pads. However please note the mem-to-mem and
capture video node operation on same FIMC instance is not allowed.  The driver
detects such cases but the applications should prevent them to avoid an
undefined behaviour.

Capture video node
~~~~~~~~~~~~~~~~~~

The driver supports V4L2 Video Capture Interface as defined at
:ref:`devices`.

At the capture and mem-to-mem video nodes only the multi-planar API is
supported. For more details see: :ref:`planar-apis`.

Camera capture subdevs
~~~~~~~~~~~~~~~~~~~~~~

Each FIMC instance exports a sub-device node (/dev/v4l-subdev?), a sub-device
node is also created per each available and enabled at the platform level
MIPI-CSI receiver device (currently up to two).

sysfs
~~~~~

In order to enable more precise camera pipeline control through the sub-device
API the driver creates a sysfs entry associated with "s5p-fimc-md" platform
device. The entry path is: /sys/platform/devices/s5p-fimc-md/subdev_conf_mode.

In typical use case there could be a following capture pipeline configuration:
sensor subdev -> mipi-csi subdev -> fimc subdev -> video node

When we configure these devices through sub-device API at user space, the
configuration flow must be from left to right, and the video node is
configured as last one.
When we don't use sub-device user space API the whole configuration of all
devices belonging to the pipeline is done at the video node driver.
The sysfs entry allows to instruct the capture node driver not to configure
the sub-devices (format, crop), to avoid resetting the subdevs' configuration
when the last configuration steps at the video node is performed.

For full sub-device control support (subdevs configured at user space before
starting streaming):

.. code-block:: none

	# echo "sub-dev" > /sys/platform/devices/s5p-fimc-md/subdev_conf_mode

For V4L2 video node control only (subdevs configured internally by the host
driver):

.. code-block:: none

	# echo "vid-dev" > /sys/platform/devices/s5p-fimc-md/subdev_conf_mode

This is a default option.

5. Device mapping to video and subdev device nodes
--------------------------------------------------

There are associated two video device nodes with each device instance in
hardware - video capture and mem-to-mem and additionally a subdev node for
more precise FIMC capture subsystem control. In addition a separate v4l2
sub-device node is created per each MIPI-CSIS device.

How to find out which /dev/video? or /dev/v4l-subdev? is assigned to which
device?

You can either grep through the kernel log to find relevant information, i.e.

.. code-block:: none

	# dmesg | grep -i fimc

(note that udev, if present, might still have rearranged the video nodes),

or retrieve the information from /dev/media? with help of the media-ctl tool:

.. code-block:: none

	# media-ctl -p

7. Build
--------

If the driver is built as a loadable kernel module (CONFIG_VIDEO_SAMSUNG_S5P_FIMC=m)
two modules are created (in addition to the core v4l2 modules): s5p-fimc.ko and
optional s5p-csis.ko (MIPI-CSI receiver subdev).
