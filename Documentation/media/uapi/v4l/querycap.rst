.. -*- coding: utf-8; mode: rst -*-

.. _querycap:

*********************
Querying Capabilities
*********************

Because V4L2 covers a wide variety of devices not all aspects of the API
are equally applicable to all types of devices. Furthermore devices of
the same type have different capabilities and this specification permits
the omission of a few complicated and less important parts of the API.

The :ref:`VIDIOC_QUERYCAP` ioctl is available to
check if the kernel device is compatible with this specification, and to
query the :ref:`functions <devices>` and :ref:`I/O methods <io>`
supported by the device.

Starting with kernel version 3.1, :ref:`VIDIOC_QUERYCAP`
will return the V4L2 API version used by the driver, with generally
matches the Kernel version. There's no need of using
:ref:`VIDIOC_QUERYCAP` to check if a specific ioctl
is supported, the V4L2 core now returns ``ENOTTY`` if a driver doesn't
provide support for an ioctl.

Other features can be queried by calling the respective ioctl, for
example :ref:`VIDIOC_ENUMINPUT` to learn about the
number, types and names of video connectors on the device. Although
abstraction is a major objective of this API, the
:ref:`VIDIOC_QUERYCAP` ioctl also allows driver
specific applications to reliably identify the driver.

All V4L2 drivers must support :ref:`VIDIOC_QUERYCAP`.
Applications should always call this ioctl after opening the device.
