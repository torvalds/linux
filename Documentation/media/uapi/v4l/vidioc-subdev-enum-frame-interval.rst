.. -*- coding: utf-8; mode: rst -*-

.. _VIDIOC_SUBDEV_ENUM_FRAME_INTERVAL:

***************************************
ioctl VIDIOC_SUBDEV_ENUM_FRAME_INTERVAL
***************************************

Name
====

VIDIOC_SUBDEV_ENUM_FRAME_INTERVAL - Enumerate frame intervals


Synopsis
========

.. cpp:function:: int ioctl( int fd, int request, struct v4l2_subdev_frame_interval_enum * argp )


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``request``
    VIDIOC_SUBDEV_ENUM_FRAME_INTERVAL

``argp``


Description
===========

This ioctl lets applications enumerate available frame intervals on a
given sub-device pad. Frame intervals only makes sense for sub-devices
that can control the frame period on their own. This includes, for
instance, image sensors and TV tuners.

For the common use case of image sensors, the frame intervals available
on the sub-device output pad depend on the frame format and size on the
same pad. Applications must thus specify the desired format and size
when enumerating frame intervals.

To enumerate frame intervals applications initialize the ``index``,
``pad``, ``which``, ``code``, ``width`` and ``height`` fields of struct
:ref:`v4l2_subdev_frame_interval_enum <v4l2-subdev-frame-interval-enum>`
and call the :ref:`VIDIOC_SUBDEV_ENUM_FRAME_INTERVAL` ioctl with a pointer
to this structure. Drivers fill the rest of the structure or return an
EINVAL error code if one of the input fields is invalid. All frame
intervals are enumerable by beginning at index zero and incrementing by
one until ``EINVAL`` is returned.

Available frame intervals may depend on the current 'try' formats at
other pads of the sub-device, as well as on the current active links.
See :ref:`VIDIOC_SUBDEV_G_FMT` for more
information about the try formats.

Sub-devices that support the frame interval enumeration ioctl should
implemented it on a single pad only. Its behaviour when supported on
multiple pads of the same sub-device is not defined.


.. _v4l2-subdev-frame-interval-enum:

.. flat-table:: struct v4l2_subdev_frame_interval_enum
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  __u32

       -  ``index``

       -  Number of the format in the enumeration, set by the application.

    -  .. row 2

       -  __u32

       -  ``pad``

       -  Pad number as reported by the media controller API.

    -  .. row 3

       -  __u32

       -  ``code``

       -  The media bus format code, as defined in
	  :ref:`v4l2-mbus-format`.

    -  .. row 4

       -  __u32

       -  ``width``

       -  Frame width, in pixels.

    -  .. row 5

       -  __u32

       -  ``height``

       -  Frame height, in pixels.

    -  .. row 6

       -  struct :ref:`v4l2_fract <v4l2-fract>`

       -  ``interval``

       -  Period, in seconds, between consecutive video frames.

    -  .. row 7

       -  __u32

       -  ``which``

       -  Frame intervals to be enumerated, from enum
	  :ref:`v4l2_subdev_format_whence <v4l2-subdev-format-whence>`.

    -  .. row 8

       -  __u32

       -  ``reserved``\ [8]

       -  Reserved for future extensions. Applications and drivers must set
	  the array to zero.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EINVAL
    The struct
    :ref:`v4l2_subdev_frame_interval_enum <v4l2-subdev-frame-interval-enum>`
    ``pad`` references a non-existing pad, one of the ``code``,
    ``width`` or ``height`` fields are invalid for the given pad or the
    ``index`` field is out of bounds.
