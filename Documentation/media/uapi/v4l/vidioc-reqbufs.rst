.. -*- coding: utf-8; mode: rst -*-

.. _VIDIOC_REQBUFS:

********************
ioctl VIDIOC_REQBUFS
********************

Name
====

VIDIOC_REQBUFS - Initiate Memory Mapping, User Pointer I/O or DMA buffer I/O


Synopsis
========

.. c:function:: int ioctl( int fd, VIDIOC_REQBUFS, struct v4l2_requestbuffers *argp )
    :name: VIDIOC_REQBUFS


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``argp``


Description
===========

This ioctl is used to initiate :ref:`memory mapped <mmap>`,
:ref:`user pointer <userp>` or :ref:`DMABUF <dmabuf>` based I/O.
Memory mapped buffers are located in device memory and must be allocated
with this ioctl before they can be mapped into the application's address
space. User buffers are allocated by applications themselves, and this
ioctl is merely used to switch the driver into user pointer I/O mode and
to setup some internal structures. Similarly, DMABUF buffers are
allocated by applications through a device driver, and this ioctl only
configures the driver into DMABUF I/O mode without performing any direct
allocation.

To allocate device buffers applications initialize all fields of the
:ref:`struct v4l2_requestbuffers <v4l2-requestbuffers>` structure. They set the ``type``
field to the respective stream or buffer type, the ``count`` field to
the desired number of buffers, ``memory`` must be set to the requested
I/O method and the ``reserved`` array must be zeroed. When the ioctl is
called with a pointer to this structure the driver will attempt to
allocate the requested number of buffers and it stores the actual number
allocated in the ``count`` field. It can be smaller than the number
requested, even zero, when the driver runs out of free memory. A larger
number is also possible when the driver requires more buffers to
function correctly. For example video output requires at least two
buffers, one displayed and one filled by the application.

When the I/O method is not supported the ioctl returns an ``EINVAL`` error
code.

Applications can call :ref:`VIDIOC_REQBUFS` again to change the number of
buffers, however this cannot succeed when any buffers are still mapped.
A ``count`` value of zero frees all buffers, after aborting or finishing
any DMA in progress, an implicit
:ref:`VIDIOC_STREAMOFF <VIDIOC_STREAMON>`.


.. _v4l2-requestbuffers:

.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. flat-table:: struct v4l2_requestbuffers
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  __u32

       -  ``count``

       -  The number of buffers requested or granted.

    -  .. row 2

       -  __u32

       -  ``type``

       -  Type of the stream or buffers, this is the same as the struct
	  :ref:`v4l2_format <v4l2-format>` ``type`` field. See
	  :ref:`v4l2-buf-type` for valid values.

    -  .. row 3

       -  __u32

       -  ``memory``

       -  Applications set this field to ``V4L2_MEMORY_MMAP``,
	  ``V4L2_MEMORY_DMABUF`` or ``V4L2_MEMORY_USERPTR``. See
	  :ref:`v4l2-memory`.

    -  .. row 4

       -  __u32

       -  ``reserved``\ [2]

       -  A place holder for future extensions. Drivers and applications
	  must set the array to zero.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EINVAL
    The buffer type (``type`` field) or the requested I/O method
    (``memory``) is not supported.
