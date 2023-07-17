.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: V4L

.. _VIDIOC_SUBDEV_ENUM_FRAME_SIZE:

***********************************
ioctl VIDIOC_SUBDEV_ENUM_FRAME_SIZE
***********************************

Name
====

VIDIOC_SUBDEV_ENUM_FRAME_SIZE - Enumerate media bus frame sizes

Synopsis
========

.. c:macro:: VIDIOC_SUBDEV_ENUM_FRAME_SIZE

``int ioctl(int fd, VIDIOC_SUBDEV_ENUM_FRAME_SIZE, struct v4l2_subdev_frame_size_enum * argp)``

Arguments
=========

``fd``
    File descriptor returned by :c:func:`open()`.

``argp``
    Pointer to struct :c:type:`v4l2_subdev_frame_size_enum`.

Description
===========

This ioctl allows applications to access the enumeration of frame sizes
supported by a sub-device on the specified pad
for the specified media bus format.
Supported formats can be retrieved with the
:ref:`VIDIOC_SUBDEV_ENUM_MBUS_CODE`
ioctl.

The enumerations are defined by the driver, and indexed using the ``index`` field
of the struct :c:type:`v4l2_subdev_frame_size_enum`.
Each pair of ``pad`` and ``code`` correspond to a separate enumeration.
Each enumeration starts with the ``index`` of 0, and
the lowest invalid index marks the end of the enumeration.

Therefore, to enumerate frame sizes allowed on the specified pad
and using the specified mbus format, initialize the
``pad``, ``which``, and ``code`` fields to desired values,
and set ``index`` to 0.
Then call the :ref:`VIDIOC_SUBDEV_ENUM_FRAME_SIZE` ioctl with a pointer to the
structure.

A successful call will return with minimum and maximum frame sizes filled in.
Repeat with increasing ``index`` until ``EINVAL`` is received.
``EINVAL`` means that either no more entries are available in the enumeration,
or that an input parameter was invalid.

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

.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.5cm}|

.. flat-table:: struct v4l2_subdev_frame_size_enum
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u32
      - ``index``
      - Index of the frame size in the enumeration belonging to the given pad
	and format. Filled in by the application.
    * - __u32
      - ``pad``
      - Pad number as reported by the media controller API.
	Filled in by the application.
    * - __u32
      - ``code``
      - The media bus format code, as defined in
	:ref:`v4l2-mbus-format`. Filled in by the application.
    * - __u32
      - ``min_width``
      - Minimum frame width, in pixels. Filled in by the driver.
    * - __u32
      - ``max_width``
      - Maximum frame width, in pixels. Filled in by the driver.
    * - __u32
      - ``min_height``
      - Minimum frame height, in pixels. Filled in by the driver.
    * - __u32
      - ``max_height``
      - Maximum frame height, in pixels. Filled in by the driver.
    * - __u32
      - ``which``
      - Frame sizes to be enumerated, from enum
	:ref:`v4l2_subdev_format_whence <v4l2-subdev-format-whence>`.
    * - __u32
      - ``stream``
      - Stream identifier.
    * - __u32
      - ``reserved``\ [7]
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
