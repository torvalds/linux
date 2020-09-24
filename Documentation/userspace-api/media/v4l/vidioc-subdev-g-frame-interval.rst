.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: V4L

.. _VIDIOC_SUBDEV_G_FRAME_INTERVAL:

********************************************************************
ioctl VIDIOC_SUBDEV_G_FRAME_INTERVAL, VIDIOC_SUBDEV_S_FRAME_INTERVAL
********************************************************************

Name
====

VIDIOC_SUBDEV_G_FRAME_INTERVAL - VIDIOC_SUBDEV_S_FRAME_INTERVAL - Get or set the frame interval on a subdev pad

Synopsis
========

.. c:macro:: VIDIOC_SUBDEV_G_FRAME_INTERVAL

``int ioctl(int fd, VIDIOC_SUBDEV_G_FRAME_INTERVAL, struct v4l2_subdev_frame_interval *argp)``

.. c:macro:: VIDIOC_SUBDEV_S_FRAME_INTERVAL

``int ioctl(int fd, VIDIOC_SUBDEV_S_FRAME_INTERVAL, struct v4l2_subdev_frame_interval *argp)``

Arguments
=========

``fd``
    File descriptor returned by :c:func:`open()`.

``argp``
    Pointer to struct :c:type:`v4l2_subdev_frame_interval`.

Description
===========

These ioctls are used to get and set the frame interval at specific
subdev pads in the image pipeline. The frame interval only makes sense
for sub-devices that can control the frame period on their own. This
includes, for instance, image sensors and TV tuners. Sub-devices that
don't support frame intervals must not implement these ioctls.

To retrieve the current frame interval applications set the ``pad``
field of a struct
:c:type:`v4l2_subdev_frame_interval` to
the desired pad number as reported by the media controller API. When
they call the ``VIDIOC_SUBDEV_G_FRAME_INTERVAL`` ioctl with a pointer to
this structure the driver fills the members of the ``interval`` field.

To change the current frame interval applications set both the ``pad``
field and all members of the ``interval`` field. When they call the
``VIDIOC_SUBDEV_S_FRAME_INTERVAL`` ioctl with a pointer to this
structure the driver verifies the requested interval, adjusts it based
on the hardware capabilities and configures the device. Upon return the
struct
:c:type:`v4l2_subdev_frame_interval`
contains the current frame interval as would be returned by a
``VIDIOC_SUBDEV_G_FRAME_INTERVAL`` call.

Calling ``VIDIOC_SUBDEV_S_FRAME_INTERVAL`` on a subdev device node that has been
registered in read-only mode is not allowed. An error is returned and the errno
variable is set to ``-EPERM``.

Drivers must not return an error solely because the requested interval
doesn't match the device capabilities. They must instead modify the
interval to match what the hardware can provide. The modified interval
should be as close as possible to the original request.

Changing the frame interval shall never change the format. Changing the
format, on the other hand, may change the frame interval.

Sub-devices that support the frame interval ioctls should implement them
on a single pad only. Their behaviour when supported on multiple pads of
the same sub-device is not defined.

.. c:type:: v4l2_subdev_frame_interval

.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. flat-table:: struct v4l2_subdev_frame_interval
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u32
      - ``pad``
      - Pad number as reported by the media controller API.
    * - struct :c:type:`v4l2_fract`
      - ``interval``
      - Period, in seconds, between consecutive video frames.
    * - __u32
      - ``reserved``\ [9]
      - Reserved for future extensions. Applications and drivers must set
	the array to zero.

Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EBUSY
    The frame interval can't be changed because the pad is currently
    busy. This can be caused, for instance, by an active video stream on
    the pad. The ioctl must not be retried without performing another
    action to fix the problem first. Only returned by
    ``VIDIOC_SUBDEV_S_FRAME_INTERVAL``

EINVAL
    The struct
    :c:type:`v4l2_subdev_frame_interval`
    ``pad`` references a non-existing pad, or the pad doesn't support
    frame intervals.

EPERM
    The ``VIDIOC_SUBDEV_S_FRAME_INTERVAL`` ioctl has been called on a read-only
    subdevice.
