.. -*- coding: utf-8; mode: rst -*-

.. _capture:

***********************
Video Capture Interface
***********************

Video capture devices sample an analog video signal and store the
digitized images in memory. Today nearly all devices can capture at full
25 or 30 frames/second. With this interface applications can control the
capture process and move images from the driver into user space.

Conventionally V4L2 video capture devices are accessed through character
device special files named ``/dev/video`` and ``/dev/video0`` to
``/dev/video63`` with major number 81 and minor numbers 0 to 63.
``/dev/video`` is typically a symbolic link to the preferred video
device.

.. note:: The same device file names are used for video output devices.


Querying Capabilities
=====================

Devices supporting the video capture interface set the
``V4L2_CAP_VIDEO_CAPTURE`` or ``V4L2_CAP_VIDEO_CAPTURE_MPLANE`` flag in
the ``capabilities`` field of struct
:c:type:`v4l2_capability` returned by the
:ref:`VIDIOC_QUERYCAP` ioctl. As secondary device
functions they may also support the :ref:`video overlay <overlay>`
(``V4L2_CAP_VIDEO_OVERLAY``) and the :ref:`raw VBI capture <raw-vbi>`
(``V4L2_CAP_VBI_CAPTURE``) interface. At least one of the read/write or
streaming I/O methods must be supported. Tuners and audio inputs are
optional.


Supplemental Functions
======================

Video capture devices shall support :ref:`audio input <audio>`,
:ref:`tuner`, :ref:`controls <control>`,
:ref:`cropping and scaling <crop>` and
:ref:`streaming parameter <streaming-par>` ioctls as needed. The
:ref:`video input <video>` ioctls must be supported by all video
capture devices.


Image Format Negotiation
========================

The result of a capture operation is determined by cropping and image
format parameters. The former select an area of the video picture to
capture, the latter how images are stored in memory, i. e. in RGB or YUV
format, the number of bits per pixel or width and height. Together they
also define how images are scaled in the process.

As usual these parameters are *not* reset at :ref:`open() <func-open>`
time to permit Unix tool chains, programming a device and then reading
from it as if it was a plain file. Well written V4L2 applications ensure
they really get what they want, including cropping and scaling.

Cropping initialization at minimum requires to reset the parameters to
defaults. An example is given in :ref:`crop`.

To query the current image format applications set the ``type`` field of
a struct :c:type:`v4l2_format` to
``V4L2_BUF_TYPE_VIDEO_CAPTURE`` or
``V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE`` and call the
:ref:`VIDIOC_G_FMT <VIDIOC_G_FMT>` ioctl with a pointer to this
structure. Drivers fill the struct
:c:type:`v4l2_pix_format` ``pix`` or the struct
:c:type:`v4l2_pix_format_mplane` ``pix_mp``
member of the ``fmt`` union.

To request different parameters applications set the ``type`` field of a
struct :c:type:`v4l2_format` as above and initialize all
fields of the struct :c:type:`v4l2_pix_format`
``vbi`` member of the ``fmt`` union, or better just modify the results
of :ref:`VIDIOC_G_FMT <VIDIOC_G_FMT>`, and call the :ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>`
ioctl with a pointer to this structure. Drivers may adjust the
parameters and finally return the actual parameters as :ref:`VIDIOC_G_FMT <VIDIOC_G_FMT>`
does.

Like :ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>` the :ref:`VIDIOC_TRY_FMT <VIDIOC_G_FMT>` ioctl
can be used to learn about hardware limitations without disabling I/O or
possibly time consuming hardware preparations.

The contents of struct :c:type:`v4l2_pix_format` and
struct :c:type:`v4l2_pix_format_mplane` are
discussed in :ref:`pixfmt`. See also the specification of the
:ref:`VIDIOC_G_FMT <VIDIOC_G_FMT>`, :ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>` and :ref:`VIDIOC_TRY_FMT <VIDIOC_G_FMT>` ioctls for
details. Video capture devices must implement both the :ref:`VIDIOC_G_FMT <VIDIOC_G_FMT>`
and :ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>` ioctl, even if :ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>` ignores all
requests and always returns default parameters as :ref:`VIDIOC_G_FMT <VIDIOC_G_FMT>` does.
:ref:`VIDIOC_TRY_FMT <VIDIOC_G_FMT>` is optional.


Reading Images
==============

A video capture device may support the ::ref:`read() function <func-read>`
and/or streaming (:ref:`memory mapping <func-mmap>` or
:ref:`user pointer <userp>`) I/O. See :ref:`io` for details.
