.. -*- coding: utf-8; mode: rst -*-

.. _VIDIOC_G_CROP:

**********************************
ioctl VIDIOC_G_CROP, VIDIOC_S_CROP
**********************************

Name
====

VIDIOC_G_CROP - VIDIOC_S_CROP - Get or set the current cropping rectangle


Synopsis
========

.. c:function:: int ioctl( int fd, VIDIOC_G_CROP, struct v4l2_crop *argp )
    :name: VIDIOC_G_CROP

.. c:function:: int ioctl( int fd, VIDIOC_S_CROP, const struct v4l2_crop *argp )
    :name: VIDIOC_S_CROP


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``argp``


Description
===========

To query the cropping rectangle size and position applications set the
``type`` field of a :ref:`struct v4l2_crop <v4l2-crop>` structure to the
respective buffer (stream) type and call the :ref:`VIDIOC_G_CROP <VIDIOC_G_CROP>` ioctl
with a pointer to this structure. The driver fills the rest of the
structure or returns the ``EINVAL`` error code if cropping is not supported.

To change the cropping rectangle applications initialize the ``type``
and struct :ref:`v4l2_rect <v4l2-rect>` substructure named ``c`` of a
v4l2_crop structure and call the :ref:`VIDIOC_S_CROP <VIDIOC_G_CROP>` ioctl with a pointer
to this structure.

Do not use the multiplanar buffer types. Use
``V4L2_BUF_TYPE_VIDEO_CAPTURE`` instead of
``V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE`` and use
``V4L2_BUF_TYPE_VIDEO_OUTPUT`` instead of
``V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE``.

The driver first adjusts the requested dimensions against hardware
limits, i. e. the bounds given by the capture/output window, and it
rounds to the closest possible values of horizontal and vertical offset,
width and height. In particular the driver must round the vertical
offset of the cropping rectangle to frame lines modulo two, such that
the field order cannot be confused.

Second the driver adjusts the image size (the opposite rectangle of the
scaling process, source or target depending on the data direction) to
the closest size possible while maintaining the current horizontal and
vertical scaling factor.

Finally the driver programs the hardware with the actual cropping and
image parameters. :ref:`VIDIOC_S_CROP <VIDIOC_G_CROP>` is a write-only ioctl, it does not
return the actual parameters. To query them applications must call
:ref:`VIDIOC_G_CROP <VIDIOC_G_CROP>` and :ref:`VIDIOC_G_FMT`. When the
parameters are unsuitable the application may modify the cropping or
image parameters and repeat the cycle until satisfactory parameters have
been negotiated.

When cropping is not supported then no parameters are changed and
:ref:`VIDIOC_S_CROP <VIDIOC_G_CROP>` returns the ``EINVAL`` error code.


.. _v4l2-crop:

.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. flat-table:: struct v4l2_crop
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  __u32

       -  ``type``

       -  Type of the data stream, set by the application. Only these types
	  are valid here: ``V4L2_BUF_TYPE_VIDEO_CAPTURE``,
	  ``V4L2_BUF_TYPE_VIDEO_OUTPUT`` and
	  ``V4L2_BUF_TYPE_VIDEO_OVERLAY``. See :ref:`v4l2-buf-type`.

    -  .. row 2

       -  struct :ref:`v4l2_rect <v4l2-rect>`

       -  ``c``

       -  Cropping rectangle. The same co-ordinate system as for struct
	  :ref:`v4l2_cropcap <v4l2-cropcap>` ``bounds`` is used.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
