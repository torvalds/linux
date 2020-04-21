.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _VIDIOC_G_FMT:

************************************************
ioctl VIDIOC_G_FMT, VIDIOC_S_FMT, VIDIOC_TRY_FMT
************************************************

Name
====

VIDIOC_G_FMT - VIDIOC_S_FMT - VIDIOC_TRY_FMT - Get or set the data format, try a format


Synopsis
========

.. c:function:: int ioctl( int fd, VIDIOC_G_FMT, struct v4l2_format *argp )
    :name: VIDIOC_G_FMT

.. c:function:: int ioctl( int fd, VIDIOC_S_FMT, struct v4l2_format *argp )
    :name: VIDIOC_S_FMT

.. c:function:: int ioctl( int fd, VIDIOC_TRY_FMT, struct v4l2_format *argp )
    :name: VIDIOC_TRY_FMT

Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``argp``
    Pointer to struct :c:type:`v4l2_format`.


Description
===========

These ioctls are used to negotiate the format of data (typically image
format) exchanged between driver and application.

To query the current parameters applications set the ``type`` field of a
struct :c:type:`v4l2_format` to the respective buffer (stream)
type. For example video capture devices use
``V4L2_BUF_TYPE_VIDEO_CAPTURE`` or
``V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE``. When the application calls the
:ref:`VIDIOC_G_FMT <VIDIOC_G_FMT>` ioctl with a pointer to this structure the driver fills
the respective member of the ``fmt`` union. In case of video capture
devices that is either the struct
:c:type:`v4l2_pix_format` ``pix`` or the struct
:c:type:`v4l2_pix_format_mplane` ``pix_mp``
member. When the requested buffer type is not supported drivers return
an ``EINVAL`` error code.

To change the current format parameters applications initialize the
``type`` field and all fields of the respective ``fmt`` union member.
For details see the documentation of the various devices types in
:ref:`devices`. Good practice is to query the current parameters
first, and to modify only those parameters not suitable for the
application. When the application calls the :ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>` ioctl with
a pointer to a struct :c:type:`v4l2_format` structure the driver
checks and adjusts the parameters against hardware abilities. Drivers
should not return an error code unless the ``type`` field is invalid,
this is a mechanism to fathom device capabilities and to approach
parameters acceptable for both the application and driver. On success
the driver may program the hardware, allocate resources and generally
prepare for data exchange. Finally the :ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>` ioctl returns
the current format parameters as :ref:`VIDIOC_G_FMT <VIDIOC_G_FMT>` does. Very simple,
inflexible devices may even ignore all input and always return the
default parameters. However all V4L2 devices exchanging data with the
application must implement the :ref:`VIDIOC_G_FMT <VIDIOC_G_FMT>` and :ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>`
ioctl. When the requested buffer type is not supported drivers return an
EINVAL error code on a :ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>` attempt. When I/O is already in
progress or the resource is not available for other reasons drivers
return the ``EBUSY`` error code.

The :ref:`VIDIOC_TRY_FMT <VIDIOC_G_FMT>` ioctl is equivalent to :ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>` with one
exception: it does not change driver state. It can also be called at any
time, never returning ``EBUSY``. This function is provided to negotiate
parameters, to learn about hardware limitations, without disabling I/O
or possibly time consuming hardware preparations. Although strongly
recommended drivers are not required to implement this ioctl.

The format as returned by :ref:`VIDIOC_TRY_FMT <VIDIOC_G_FMT>` must be identical to what
:ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>` returns for the same input or output.


.. c:type:: v4l2_format

.. tabularcolumns::  |p{1.2cm}|p{4.6cm}|p{3.0cm}|p{8.6cm}|

.. flat-table:: struct v4l2_format
    :header-rows:  0
    :stub-columns: 0

    * - __u32
      - ``type``
      - Type of the data stream, see :c:type:`v4l2_buf_type`.
    * - union {
      - ``fmt``
    * - struct :c:type:`v4l2_pix_format`
      - ``pix``
      - Definition of an image format, see :ref:`pixfmt`, used by video
	capture and output devices.
    * - struct :c:type:`v4l2_pix_format_mplane`
      - ``pix_mp``
      - Definition of an image format, see :ref:`pixfmt`, used by video
	capture and output devices that support the
	:ref:`multi-planar version of the API <planar-apis>`.
    * - struct :c:type:`v4l2_window`
      - ``win``
      - Definition of an overlaid image, see :ref:`overlay`, used by
	video overlay devices.
    * - struct :c:type:`v4l2_vbi_format`
      - ``vbi``
      - Raw VBI capture or output parameters. This is discussed in more
	detail in :ref:`raw-vbi`. Used by raw VBI capture and output
	devices.
    * - struct :c:type:`v4l2_sliced_vbi_format`
      - ``sliced``
      - Sliced VBI capture or output parameters. See :ref:`sliced` for
	details. Used by sliced VBI capture and output devices.
    * - struct :c:type:`v4l2_sdr_format`
      - ``sdr``
      - Definition of a data format, see :ref:`pixfmt`, used by SDR
	capture and output devices.
    * - struct :c:type:`v4l2_meta_format`
      - ``meta``
      - Definition of a metadata format, see :ref:`meta-formats`, used by
	metadata capture devices.
    * - __u8
      - ``raw_data``\ [200]
      - Place holder for future extensions.
    * - }
      -


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EINVAL
    The struct :c:type:`v4l2_format` ``type`` field is
    invalid or the requested buffer type not supported.

EBUSY
    The device is busy and cannot change the format. This could be
    because or the device is streaming or buffers are allocated or
    queued to the driver. Relevant for :ref:`VIDIOC_S_FMT
    <VIDIOC_G_FMT>` only.
