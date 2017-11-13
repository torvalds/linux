.. -*- coding: utf-8; mode: rst -*-

.. _VIDIOC_ENUM_FMT:

*********************
ioctl VIDIOC_ENUM_FMT
*********************

Name
====

VIDIOC_ENUM_FMT - Enumerate image formats


Synopsis
========

.. c:function:: int ioctl( int fd, VIDIOC_ENUM_FMT, struct v4l2_fmtdesc *argp )
    :name: VIDIOC_ENUM_FMT


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``argp``
    Pointer to struct :c:type:`v4l2_fmtdesc`.


Description
===========

To enumerate image formats applications initialize the ``type`` and
``index`` field of struct :c:type:`v4l2_fmtdesc` and call
the :ref:`VIDIOC_ENUM_FMT` ioctl with a pointer to this structure. Drivers
fill the rest of the structure or return an ``EINVAL`` error code. All
formats are enumerable by beginning at index zero and incrementing by
one until ``EINVAL`` is returned.

.. note::

   After switching input or output the list of enumerated image
   formats may be different.


.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. c:type:: v4l2_fmtdesc

.. flat-table:: struct v4l2_fmtdesc
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u32
      - ``index``
      - Number of the format in the enumeration, set by the application.
	This is in no way related to the ``pixelformat`` field.
    * - __u32
      - ``type``
      - Type of the data stream, set by the application. Only these types
	are valid here: ``V4L2_BUF_TYPE_VIDEO_CAPTURE``,
	``V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE``,
	``V4L2_BUF_TYPE_VIDEO_OUTPUT``,
	``V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE`` and
	``V4L2_BUF_TYPE_VIDEO_OVERLAY``. See :c:type:`v4l2_buf_type`.
    * - __u32
      - ``flags``
      - See :ref:`fmtdesc-flags`
    * - __u8
      - ``description``\ [32]
      - Description of the format, a NUL-terminated ASCII string. This
	information is intended for the user, for example: "YUV 4:2:2".
    * - __u32
      - ``pixelformat``
      - The image format identifier. This is a four character code as
	computed by the v4l2_fourcc() macro:
    * - :cspan:`2`

	.. _v4l2-fourcc:

	``#define v4l2_fourcc(a,b,c,d)``

	``(((__u32)(a)<<0)|((__u32)(b)<<8)|((__u32)(c)<<16)|((__u32)(d)<<24))``

	Several image formats are already defined by this specification in
	:ref:`pixfmt`.

	.. attention::

	   These codes are not the same as those used
	   in the Windows world.
    * - __u32
      - ``reserved``\ [4]
      - Reserved for future extensions. Drivers must set the array to
	zero.



.. tabularcolumns:: |p{6.6cm}|p{2.2cm}|p{8.7cm}|

.. _fmtdesc-flags:

.. flat-table:: Image Format Description Flags
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4

    * - ``V4L2_FMT_FLAG_COMPRESSED``
      - 0x0001
      - This is a compressed format.
    * - ``V4L2_FMT_FLAG_EMULATED``
      - 0x0002
      - This format is not native to the device but emulated through
	software (usually libv4l2), where possible try to use a native
	format instead for better performance.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EINVAL
    The struct :c:type:`v4l2_fmtdesc` ``type`` is not
    supported or the ``index`` is out of bounds.
