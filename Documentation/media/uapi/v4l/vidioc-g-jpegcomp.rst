.. -*- coding: utf-8; mode: rst -*-

.. _VIDIOC_G_JPEGCOMP:

******************************************
ioctl VIDIOC_G_JPEGCOMP, VIDIOC_S_JPEGCOMP
******************************************

Name
====

VIDIOC_G_JPEGCOMP - VIDIOC_S_JPEGCOMP


Synopsis
========

.. cpp:function:: int ioctl( int fd, int request, v4l2_jpegcompression *argp )

.. cpp:function:: int ioctl( int fd, int request, const v4l2_jpegcompression *argp )


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``request``
    VIDIOC_G_JPEGCOMP, VIDIOC_S_JPEGCOMP

``argp``


Description
===========

These ioctls are **deprecated**. New drivers and applications should use
:ref:`JPEG class controls <jpeg-controls>` for image quality and JPEG
markers control.

[to do]

Ronald Bultje elaborates:

APP is some application-specific information. The application can set it
itself, and it'll be stored in the JPEG-encoded fields (eg; interlacing
information for in an AVI or so). COM is the same, but it's comments,
like 'encoded by me' or so.

jpeg_markers describes whether the huffman tables, quantization tables
and the restart interval information (all JPEG-specific stuff) should be
stored in the JPEG-encoded fields. These define how the JPEG field is
encoded. If you omit them, applications assume you've used standard
encoding. You usually do want to add them.


.. _v4l2-jpegcompression:

.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. flat-table:: struct v4l2_jpegcompression
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  int

       -  ``quality``

       -  Deprecated. If
	  :ref:`V4L2_CID_JPEG_COMPRESSION_QUALITY <jpeg-quality-control>`
	  control is exposed by a driver applications should use it instead
	  and ignore this field.

    -  .. row 2

       -  int

       -  ``APPn``

       -

    -  .. row 3

       -  int

       -  ``APP_len``

       -

    -  .. row 4

       -  char

       -  ``APP_data``\ [60]

       -

    -  .. row 5

       -  int

       -  ``COM_len``

       -

    -  .. row 6

       -  char

       -  ``COM_data``\ [60]

       -

    -  .. row 7

       -  __u32

       -  ``jpeg_markers``

       -  See :ref:`jpeg-markers`. Deprecated. If
	  :ref:`V4L2_CID_JPEG_ACTIVE_MARKER <jpeg-active-marker-control>`
	  control is exposed by a driver applications should use it instead
	  and ignore this field.



.. _jpeg-markers:

.. tabularcolumns:: |p{6.6cm}|p{2.2cm}|p{8.7cm}|

.. flat-table:: JPEG Markers Flags
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4


    -  .. row 1

       -  ``V4L2_JPEG_MARKER_DHT``

       -  (1<<3)

       -  Define Huffman Tables

    -  .. row 2

       -  ``V4L2_JPEG_MARKER_DQT``

       -  (1<<4)

       -  Define Quantization Tables

    -  .. row 3

       -  ``V4L2_JPEG_MARKER_DRI``

       -  (1<<5)

       -  Define Restart Interval

    -  .. row 4

       -  ``V4L2_JPEG_MARKER_COM``

       -  (1<<6)

       -  Comment segment

    -  .. row 5

       -  ``V4L2_JPEG_MARKER_APP``

       -  (1<<7)

       -  App segment, driver will always use APP0


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
