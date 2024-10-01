.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: V4L

.. _libv4l-introduction:

************
Introduction
************

libv4l is a collection of libraries which adds a thin abstraction layer
on top of video4linux2 devices. The purpose of this (thin) layer is to
make it easy for application writers to support a wide variety of
devices without having to write separate code for different devices in
the same class.

An example of using libv4l is provided by
:ref:`v4l2grab <v4l2grab-example>`.

libv4l consists of 3 different libraries:

libv4lconvert
=============

libv4lconvert is a library that converts several different pixelformats
found in V4L2 drivers into a few common RGB and YUY formats.

It currently accepts the following V4L2 driver formats:
:ref:`V4L2_PIX_FMT_BGR24 <V4L2-PIX-FMT-BGR24>`,
:ref:`V4L2_PIX_FMT_NV12_16L16 <V4L2-PIX-FMT-NV12-16L16>`,
:ref:`V4L2_PIX_FMT_JPEG <V4L2-PIX-FMT-JPEG>`,
:ref:`V4L2_PIX_FMT_MJPEG <V4L2-PIX-FMT-MJPEG>`,
:ref:`V4L2_PIX_FMT_MR97310A <V4L2-PIX-FMT-MR97310A>`,
:ref:`V4L2_PIX_FMT_OV511 <V4L2-PIX-FMT-OV511>`,
:ref:`V4L2_PIX_FMT_OV518 <V4L2-PIX-FMT-OV518>`,
:ref:`V4L2_PIX_FMT_PAC207 <V4L2-PIX-FMT-PAC207>`,
:ref:`V4L2_PIX_FMT_PJPG <V4L2-PIX-FMT-PJPG>`,
:ref:`V4L2_PIX_FMT_RGB24 <V4L2-PIX-FMT-RGB24>`,
:ref:`V4L2_PIX_FMT_SBGGR8 <V4L2-PIX-FMT-SBGGR8>`,
:ref:`V4L2_PIX_FMT_SGBRG8 <V4L2-PIX-FMT-SGBRG8>`,
:ref:`V4L2_PIX_FMT_SGRBG8 <V4L2-PIX-FMT-SGRBG8>`,
:ref:`V4L2_PIX_FMT_SN9C10X <V4L2-PIX-FMT-SN9C10X>`,
:ref:`V4L2_PIX_FMT_SN9C20X_I420 <V4L2-PIX-FMT-SN9C20X-I420>`,
:ref:`V4L2_PIX_FMT_SPCA501 <V4L2-PIX-FMT-SPCA501>`,
:ref:`V4L2_PIX_FMT_SPCA505 <V4L2-PIX-FMT-SPCA505>`,
:ref:`V4L2_PIX_FMT_SPCA508 <V4L2-PIX-FMT-SPCA508>`,
:ref:`V4L2_PIX_FMT_SPCA561 <V4L2-PIX-FMT-SPCA561>`,
:ref:`V4L2_PIX_FMT_SQ905C <V4L2-PIX-FMT-SQ905C>`,
:ref:`V4L2_PIX_FMT_SRGGB8 <V4L2-PIX-FMT-SRGGB8>`,
:ref:`V4L2_PIX_FMT_UYVY <V4L2-PIX-FMT-UYVY>`,
:ref:`V4L2_PIX_FMT_YUV420 <V4L2-PIX-FMT-YUV420>`,
:ref:`V4L2_PIX_FMT_YUYV <V4L2-PIX-FMT-YUYV>`,
:ref:`V4L2_PIX_FMT_YVU420 <V4L2-PIX-FMT-YVU420>`, and
:ref:`V4L2_PIX_FMT_YVYU <V4L2-PIX-FMT-YVYU>`.

Later on libv4lconvert was expanded to also be able to do various video
processing functions to improve webcam video quality. The video
processing is split in to 2 parts: libv4lconvert/control and
libv4lconvert/processing.

The control part is used to offer video controls which can be used to
control the video processing functions made available by
libv4lconvert/processing. These controls are stored application wide
(until reboot) by using a persistent shared memory object.

libv4lconvert/processing offers the actual video processing
functionality.

libv4l1
=======

This library offers functions that can be used to quickly make v4l1
applications work with v4l2 devices. These functions work exactly like
the normal open/close/etc, except that libv4l1 does full emulation of
the v4l1 api on top of v4l2 drivers, in case of v4l1 drivers it will
just pass calls through.

Since those functions are emulations of the old V4L1 API, it shouldn't
be used for new applications.

libv4l2
=======

This library should be used for all modern V4L2 applications.

It provides handles to call V4L2 open/ioctl/close/poll methods. Instead
of just providing the raw output of the device, it enhances the calls in
the sense that it will use libv4lconvert to provide more video formats
and to enhance the image quality.

In most cases, libv4l2 just passes the calls directly through to the
v4l2 driver, intercepting the calls to
:ref:`VIDIOC_TRY_FMT <VIDIOC_G_FMT>`,
:ref:`VIDIOC_G_FMT <VIDIOC_G_FMT>`,
:ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>`,
:ref:`VIDIOC_ENUM_FRAMESIZES <VIDIOC_ENUM_FRAMESIZES>` and
:ref:`VIDIOC_ENUM_FRAMEINTERVALS <VIDIOC_ENUM_FRAMEINTERVALS>` in
order to emulate the formats
:ref:`V4L2_PIX_FMT_BGR24 <V4L2-PIX-FMT-BGR24>`,
:ref:`V4L2_PIX_FMT_RGB24 <V4L2-PIX-FMT-RGB24>`,
:ref:`V4L2_PIX_FMT_YUV420 <V4L2-PIX-FMT-YUV420>`, and
:ref:`V4L2_PIX_FMT_YVU420 <V4L2-PIX-FMT-YVU420>`, if they aren't
available in the driver. :ref:`VIDIOC_ENUM_FMT <VIDIOC_ENUM_FMT>`
keeps enumerating the hardware supported formats, plus the emulated
formats offered by libv4l at the end.

.. _libv4l-ops:

Libv4l device control functions
-------------------------------

The common file operation methods are provided by libv4l.

Those functions operate just like the gcc function ``dup()`` and
V4L2 functions
:c:func:`open()`, :c:func:`close()`,
:c:func:`ioctl()`, :c:func:`read()`,
:c:func:`mmap()` and :c:func:`munmap()`:

.. c:function:: int v4l2_open(const char *file, int oflag, ...)

   operates like the :c:func:`open()` function.

.. c:function:: int v4l2_close(int fd)

   operates like the :c:func:`close()` function.

.. c:function:: int v4l2_dup(int fd)

   operates like the libc ``dup()`` function, duplicating a file handler.

.. c:function:: int v4l2_ioctl (int fd, unsigned long int request, ...)

   operates like the :c:func:`ioctl()` function.

.. c:function:: int v4l2_read (int fd, void* buffer, size_t n)

   operates like the :c:func:`read()` function.

.. c:function:: void *v4l2_mmap(void *start, size_t length, int prot, int flags, int fd, int64_t offset);

   operates like the :c:func:`mmap()` function.

.. c:function:: int v4l2_munmap(void *_start, size_t length);

   operates like the :c:func:`munmap()` function.

Those functions provide additional control:

.. c:function:: int v4l2_fd_open(int fd, int v4l2_flags)

   opens an already opened fd for further use through v4l2lib and possibly
   modify libv4l2's default behavior through the ``v4l2_flags`` argument.
   Currently, ``v4l2_flags`` can be ``V4L2_DISABLE_CONVERSION``, to disable
   format conversion.

.. c:function:: int v4l2_set_control(int fd, int cid, int value)

   This function takes a value of 0 - 65535, and then scales that range to the
   actual range of the given v4l control id, and then if the cid exists and is
   not locked sets the cid to the scaled value.

.. c:function:: int v4l2_get_control(int fd, int cid)

   This function returns a value of 0 - 65535, scaled to from the actual range
   of the given v4l control id. when the cid does not exist, could not be
   accessed for some reason, or some error occurred 0 is returned.

v4l1compat.so wrapper library
=============================

This library intercepts calls to
:c:func:`open()`, :c:func:`close()`,
:c:func:`ioctl()`, :c:func:`mmap()` and
:c:func:`munmap()`
operations and redirects them to the libv4l counterparts, by using
``LD_PRELOAD=/usr/lib/v4l1compat.so``. It also emulates V4L1 calls via V4L2
API.

It allows usage of binary legacy applications that still don't use
libv4l.
