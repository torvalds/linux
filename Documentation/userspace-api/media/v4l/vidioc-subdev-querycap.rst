.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: V4L

.. _VIDIOC_SUBDEV_QUERYCAP:

****************************
ioctl VIDIOC_SUBDEV_QUERYCAP
****************************

Name
====

VIDIOC_SUBDEV_QUERYCAP - Query sub-device capabilities

Synopsis
========

.. c:macro:: VIDIOC_SUBDEV_QUERYCAP

``int ioctl(int fd, VIDIOC_SUBDEV_QUERYCAP, struct v4l2_subdev_capability *argp)``

Arguments
=========

``fd``
    File descriptor returned by :c:func:`open()`.

``argp``
    Pointer to struct :c:type:`v4l2_subdev_capability`.

Description
===========

All V4L2 sub-devices support the ``VIDIOC_SUBDEV_QUERYCAP`` ioctl. It is used to
identify kernel devices compatible with this specification and to obtain
information about driver and hardware capabilities. The ioctl takes a pointer to
a struct :c:type:`v4l2_subdev_capability` which is filled by the driver. When
the driver is not compatible with this specification the ioctl returns
``ENOTTY`` error code.

.. tabularcolumns:: |p{1.5cm}|p{2.9cm}|p{12.9cm}|

.. c:type:: v4l2_subdev_capability

.. flat-table:: struct v4l2_subdev_capability
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 4 20

    * - __u32
      - ``version``
      - Version number of the driver.

	The version reported is provided by the V4L2 subsystem following the
	kernel numbering scheme. However, it may not always return the same
	version as the kernel if, for example, a stable or
	distribution-modified kernel uses the V4L2 stack from a newer kernel.

	The version number is formatted using the ``KERNEL_VERSION()``
	macro:
    * - :cspan:`2`

	``#define KERNEL_VERSION(a,b,c) (((a) << 16) + ((b) << 8) + (c))``

	``__u32 version = KERNEL_VERSION(0, 8, 1);``

	``printf ("Version: %u.%u.%u\\n",``

	``(version >> 16) & 0xFF, (version >> 8) & 0xFF, version & 0xFF);``
    * - __u32
      - ``capabilities``
      - Sub-device capabilities of the opened device, see
	:ref:`subdevice-capabilities`.
    * - __u32
      - ``reserved``\ [14]
      - Reserved for future extensions. Set to 0 by the V4L2 core.

.. tabularcolumns:: |p{6.8cm}|p{2.4cm}|p{8.1cm}|

.. _subdevice-capabilities:

.. cssclass:: longtable

.. flat-table:: Sub-Device Capabilities Flags
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4

    * - V4L2_SUBDEV_CAP_RO_SUBDEV
      - 0x00000001
      - The sub-device device node is registered in read-only mode.
	Access to the sub-device ioctls that modify the device state is
	restricted. Refer to each individual subdevice ioctl documentation
	for a description of which restrictions apply to a read-only sub-device.

Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

ENOTTY
    The device node is not a V4L2 sub-device.
