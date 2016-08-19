.. -*- coding: utf-8; mode: rst -*-

.. _VIDIOC_CREATE_BUFS:

************************
ioctl VIDIOC_CREATE_BUFS
************************

Name
====

VIDIOC_CREATE_BUFS - Create buffers for Memory Mapped or User Pointer or DMA Buffer I/O


Synopsis
========

.. c:function:: int ioctl( int fd, VIDIOC_CREATE_BUFS, struct v4l2_create_buffers *argp )
    :name: VIDIOC_CREATE_BUFS


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``argp``


Description
===========

This ioctl is used to create buffers for :ref:`memory mapped <mmap>`
or :ref:`user pointer <userp>` or :ref:`DMA buffer <dmabuf>` I/O. It
can be used as an alternative or in addition to the
:ref:`VIDIOC_REQBUFS` ioctl, when a tighter control
over buffers is required. This ioctl can be called multiple times to
create buffers of different sizes.

To allocate the device buffers applications must initialize the relevant
fields of the :ref:`struct v4l2_create_buffers <v4l2-create-buffers>` structure. The
``count`` field must be set to the number of requested buffers, the
``memory`` field specifies the requested I/O method and the ``reserved``
array must be zeroed.

The ``format`` field specifies the image format that the buffers must be
able to handle. The application has to fill in this struct
:ref:`v4l2_format <v4l2-format>`. Usually this will be done using the
:ref:`VIDIOC_TRY_FMT <VIDIOC_G_FMT>` or
:ref:`VIDIOC_G_FMT <VIDIOC_G_FMT>` ioctls to ensure that the
requested format is supported by the driver. Based on the format's
``type`` field the requested buffer size (for single-planar) or plane
sizes (for multi-planar formats) will be used for the allocated buffers.
The driver may return an error if the size(s) are not supported by the
hardware (usually because they are too small).

The buffers created by this ioctl will have as minimum size the size
defined by the ``format.pix.sizeimage`` field (or the corresponding
fields for other format types). Usually if the ``format.pix.sizeimage``
field is less than the minimum required for the given format, then an
error will be returned since drivers will typically not allow this. If
it is larger, then the value will be used as-is. In other words, the
driver may reject the requested size, but if it is accepted the driver
will use it unchanged.

When the ioctl is called with a pointer to this structure the driver
will attempt to allocate up to the requested number of buffers and store
the actual number allocated and the starting index in the ``count`` and
the ``index`` fields respectively. On return ``count`` can be smaller
than the number requested.


.. _v4l2-create-buffers:

.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. flat-table:: struct v4l2_create_buffers
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  __u32

       -  ``index``

       -  The starting buffer index, returned by the driver.

    -  .. row 2

       -  __u32

       -  ``count``

       -  The number of buffers requested or granted. If count == 0, then
	  :ref:`VIDIOC_CREATE_BUFS` will set ``index`` to the current number of
	  created buffers, and it will check the validity of ``memory`` and
	  ``format.type``. If those are invalid -1 is returned and errno is
	  set to ``EINVAL`` error code, otherwise :ref:`VIDIOC_CREATE_BUFS` returns
	  0. It will never set errno to ``EBUSY`` error code in this particular
	  case.

    -  .. row 3

       -  __u32

       -  ``memory``

       -  Applications set this field to ``V4L2_MEMORY_MMAP``,
	  ``V4L2_MEMORY_DMABUF`` or ``V4L2_MEMORY_USERPTR``. See
	  :ref:`v4l2-memory`

    -  .. row 4

       -  struct :ref:`v4l2_format <v4l2-format>`

       -  ``format``

       -  Filled in by the application, preserved by the driver.

    -  .. row 5

       -  __u32

       -  ``reserved``\ [8]

       -  A place holder for future extensions. Drivers and applications
	  must set the array to zero.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

ENOMEM
    No memory to allocate buffers for :ref:`memory mapped <mmap>` I/O.

EINVAL
    The buffer type (``format.type`` field), requested I/O method
    (``memory``) or format (``format`` field) is not valid.
