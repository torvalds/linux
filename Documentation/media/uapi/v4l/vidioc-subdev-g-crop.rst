.. -*- coding: utf-8; mode: rst -*-

.. _VIDIOC_SUBDEV_G_CROP:

************************************************
ioctl VIDIOC_SUBDEV_G_CROP, VIDIOC_SUBDEV_S_CROP
************************************************

Name
====

VIDIOC_SUBDEV_G_CROP - VIDIOC_SUBDEV_S_CROP - Get or set the crop rectangle on a subdev pad


Synopsis
========

.. cpp:function:: int ioctl( int fd, int request, struct v4l2_subdev_crop *argp )

.. cpp:function:: int ioctl( int fd, int request, const struct v4l2_subdev_crop *argp )


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``request``
    VIDIOC_SUBDEV_G_CROP, VIDIOC_SUBDEV_S_CROP

``argp``


Description
===========

.. note::

    This is an :ref:`obsolete` interface and may be removed
    in the future. It is superseded by
    :ref:`the selection API <VIDIOC_SUBDEV_G_SELECTION>`.

To retrieve the current crop rectangle applications set the ``pad``
field of a struct :ref:`v4l2_subdev_crop <v4l2-subdev-crop>` to the
desired pad number as reported by the media API and the ``which`` field
to ``V4L2_SUBDEV_FORMAT_ACTIVE``. They then call the
``VIDIOC_SUBDEV_G_CROP`` ioctl with a pointer to this structure. The
driver fills the members of the ``rect`` field or returns ``EINVAL`` error
code if the input arguments are invalid, or if cropping is not supported
on the given pad.

To change the current crop rectangle applications set both the ``pad``
and ``which`` fields and all members of the ``rect`` field. They then
call the ``VIDIOC_SUBDEV_S_CROP`` ioctl with a pointer to this
structure. The driver verifies the requested crop rectangle, adjusts it
based on the hardware capabilities and configures the device. Upon
return the struct :ref:`v4l2_subdev_crop <v4l2-subdev-crop>`
contains the current format as would be returned by a
``VIDIOC_SUBDEV_G_CROP`` call.

Applications can query the device capabilities by setting the ``which``
to ``V4L2_SUBDEV_FORMAT_TRY``. When set, 'try' crop rectangles are not
applied to the device by the driver, but are mangled exactly as active
crop rectangles and stored in the sub-device file handle. Two
applications querying the same sub-device would thus not interact with
each other.

Drivers must not return an error solely because the requested crop
rectangle doesn't match the device capabilities. They must instead
modify the rectangle to match what the hardware can provide. The
modified format should be as close as possible to the original request.


.. _v4l2-subdev-crop:

.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. flat-table:: struct v4l2_subdev_crop
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  __u32

       -  ``pad``

       -  Pad number as reported by the media framework.

    -  .. row 2

       -  __u32

       -  ``which``

       -  Crop rectangle to get or set, from enum
	  :ref:`v4l2_subdev_format_whence <v4l2-subdev-format-whence>`.

    -  .. row 3

       -  struct :ref:`v4l2_rect <v4l2-rect>`

       -  ``rect``

       -  Crop rectangle boundaries, in pixels.

    -  .. row 4

       -  __u32

       -  ``reserved``\ [8]

       -  Reserved for future extensions. Applications and drivers must set
	  the array to zero.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EBUSY
    The crop rectangle can't be changed because the pad is currently
    busy. This can be caused, for instance, by an active video stream on
    the pad. The ioctl must not be retried without performing another
    action to fix the problem first. Only returned by
    ``VIDIOC_SUBDEV_S_CROP``

EINVAL
    The struct :ref:`v4l2_subdev_crop <v4l2-subdev-crop>` ``pad``
    references a non-existing pad, the ``which`` field references a
    non-existing format, or cropping is not supported on the given
    subdev pad.
