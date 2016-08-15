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

.. cpp:function:: int ioctl( int fd, int request, struct v4l2_fmtdesc *argp )


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``request``
    VIDIOC_ENUM_FMT

``argp``


Description
===========

To enumerate image formats applications initialize the ``type`` and
``index`` field of struct :ref:`v4l2_fmtdesc <v4l2-fmtdesc>` and call
the :ref:`VIDIOC_ENUM_FMT` ioctl with a pointer to this structure. Drivers
fill the rest of the structure or return an ``EINVAL`` error code. All
formats are enumerable by beginning at index zero and incrementing by
one until ``EINVAL`` is returned.

.. note:: After switching input or output the list of enumerated image
   formats may be different.


.. _v4l2-fmtdesc:

.. flat-table:: struct v4l2_fmtdesc
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  __u32

       -  ``index``

       -  Number of the format in the enumeration, set by the application.
	  This is in no way related to the ``pixelformat`` field.

    -  .. row 2

       -  __u32

       -  ``type``

       -  Type of the data stream, set by the application. Only these types
	  are valid here: ``V4L2_BUF_TYPE_VIDEO_CAPTURE``,
	  ``V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE``,
	  ``V4L2_BUF_TYPE_VIDEO_OUTPUT``,
	  ``V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE`` and
	  ``V4L2_BUF_TYPE_VIDEO_OVERLAY``. See :ref:`v4l2-buf-type`.

    -  .. row 3

       -  __u32

       -  ``flags``

       -  See :ref:`fmtdesc-flags`

    -  .. row 4

       -  __u8

       -  ``description``\ [32]

       -  Description of the format, a NUL-terminated ASCII string. This
	  information is intended for the user, for example: "YUV 4:2:2".

    -  .. row 5

       -  __u32

       -  ``pixelformat``

       -  The image format identifier. This is a four character code as
	  computed by the v4l2_fourcc() macro:

    -  .. row 6

       -  :cspan:`2`


	  .. _v4l2-fourcc:
	  .. code-block:: c

	      #define v4l2_fourcc(a,b,c,d) (((__u32)(a)<<0)|((__u32)(b)<<8)|((__u32)(c)<<16)|((__u32)(d)<<24))

	  Several image formats are already defined by this specification in
	  :ref:`pixfmt`.

	  .. attention:: These codes are not the same as those used
	     in the Windows world.

    -  .. row 7

       -  __u32

       -  ``reserved``\ [4]

       -  Reserved for future extensions. Drivers must set the array to
	  zero.



.. _fmtdesc-flags:

.. flat-table:: Image Format Description Flags
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4


    -  .. row 1

       -  ``V4L2_FMT_FLAG_COMPRESSED``

       -  0x0001

       -  This is a compressed format.

    -  .. row 2

       -  ``V4L2_FMT_FLAG_EMULATED``

       -  0x0002

       -  This format is not native to the device but emulated through
	  software (usually libv4l2), where possible try to use a native
	  format instead for better performance.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EINVAL
    The struct :ref:`v4l2_fmtdesc <v4l2-fmtdesc>` ``type`` is not
    supported or the ``index`` is out of bounds.
