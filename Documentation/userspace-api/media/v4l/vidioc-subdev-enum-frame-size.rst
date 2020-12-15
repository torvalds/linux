.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _VIDIOC_SUBDEV_ENUM_FRAME_SIZE:

***********************************
ioctl VIDIOC_SUBDEV_ENUM_FRAME_SIZE
***********************************

Name
====

VIDIOC_SUBDEV_ENUM_FRAME_SIZE - Enumerate media bus frame sizes


Synopsis
========

.. c:function:: int ioctl( int fd, VIDIOC_SUBDEV_ENUM_FRAME_SIZE, struct v4l2_subdev_frame_size_enum * argp )
    :name: VIDIOC_SUBDEV_ENUM_FRAME_SIZE


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``argp``
    Pointer to struct :c:type:`v4l2_subdev_frame_size_enum`.


Description
===========

This ioctl allows applications to enumerate all frame sizes supported by
a sub-device on the given pad for the given media bus format. Supported
formats can be retrieved with the
:ref:`VIDIOC_SUBDEV_ENUM_MBUS_CODE`
ioctl.

To enumerate frame sizes applications initialize the ``pad``, ``which``
, ``code`` and ``index`` fields of the struct
:c:type:`v4l2_subdev_mbus_code_enum` and
call the :ref:`VIDIOC_SUBDEV_ENUM_FRAME_SIZE` ioctl with a pointer to the
structure. Drivers fill the minimum and maximum frame sizes or return an
EINVAL error code if one of the input parameters is invalid.

Sub-devices that only support discrete frame sizes (such as most
sensors) will return one or more frame sizes with identical minimum and
maximum values.

Not all possible sizes in given [minimum, maximum] ranges need to be
supported. For instance, a scaler that uses a fixed-point scaling ratio
might not be able to produce every frame size between the minimum and
maximum values. Applications must use the
:ref:`VIDIOC_SUBDEV_S_FMT <VIDIOC_SUBDEV_G_FMT>` ioctl to try the
sub-device for an exact supported frame size.

Available frame sizes may depend on the current 'try' formats at other
pads of the sub-device, as well as on the current active links and the
current values of V4L2 controls. See
:ref:`VIDIOC_SUBDEV_G_FMT` for more
information about try formats.


.. c:type:: v4l2_subdev_frame_size_enum

.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. flat-table:: struct v4l2_subdev_frame_size_enum
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u32
      - ``index``
      - Number of the format in the enumeration, set by the application.
    * - __u32
      - ``pad``
      - Pad number as reported by the media controller API.
    * - __u32
      - ``code``
      - The media bus format code, as defined in
	:ref:`v4l2-mbus-format`.
    * - __u32
      - ``min_width``
      - Minimum frame width, in pixels.
    * - __u32
      - ``max_width``
      - Maximum frame width, in pixels.
    * - __u32
      - ``min_height``
      - Minimum frame height, in pixels.
    * - __u32
      - ``max_height``
      - Maximum frame height, in pixels.
    * - __u32
      - ``which``
      - Frame sizes to be enumerated, from enum
	:ref:`v4l2_subdev_format_whence <v4l2-subdev-format-whence>`.
    * - __u32
      - ``reserved``\ [8]
      - Reserved for future extensions. Applications and drivers must set
	the array to zero.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EINVAL
    The struct
    :c:type:`v4l2_subdev_frame_size_enum`
    ``pad`` references a non-existing pad, the ``code`` is invalid for
    the given pad or the ``index`` field is out of bounds.
