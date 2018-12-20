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
    Pointer to struct :c:type:`v4l2_requestbuffers`.

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
struct :c:type:`v4l2_requestbuffers` structure. They set the ``type``
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


.. c:type:: v4l2_requestbuffers

.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. flat-table:: struct v4l2_requestbuffers
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u32
      - ``count``
      - The number of buffers requested or granted.
    * - __u32
      - ``type``
      - Type of the stream or buffers, this is the same as the struct
	:c:type:`v4l2_format` ``type`` field. See
	:c:type:`v4l2_buf_type` for valid values.
    * - __u32
      - ``memory``
      - Applications set this field to ``V4L2_MEMORY_MMAP``,
	``V4L2_MEMORY_DMABUF`` or ``V4L2_MEMORY_USERPTR``. See
	:c:type:`v4l2_memory`.
    * - __u32
      - ``capabilities``
      - Set by the driver. If 0, then the driver doesn't support
        capabilities. In that case all you know is that the driver is
	guaranteed to support ``V4L2_MEMORY_MMAP`` and *might* support
	other :c:type:`v4l2_memory` types. It will not support any others
	capabilities.

	If you want to query the capabilities with a minimum of side-effects,
	then this can be called with ``count`` set to 0, ``memory`` set to
	``V4L2_MEMORY_MMAP`` and ``type`` set to the buffer type. This will
	free any previously allocated buffers, so this is typically something
	that will be done at the start of the application.
    * - __u32
      - ``reserved``\ [1]
      - A place holder for future extensions. Drivers and applications
	must set the array to zero.

.. tabularcolumns:: |p{6.1cm}|p{2.2cm}|p{8.7cm}|

.. _v4l2-buf-capabilities:
.. _V4L2-BUF-CAP-SUPPORTS-MMAP:
.. _V4L2-BUF-CAP-SUPPORTS-USERPTR:
.. _V4L2-BUF-CAP-SUPPORTS-DMABUF:
.. _V4L2-BUF-CAP-SUPPORTS-REQUESTS:

.. cssclass:: longtable

.. flat-table:: V4L2 Buffer Capabilities Flags
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4

    * - ``V4L2_BUF_CAP_SUPPORTS_MMAP``
      - 0x00000001
      - This buffer type supports the ``V4L2_MEMORY_MMAP`` streaming mode.
    * - ``V4L2_BUF_CAP_SUPPORTS_USERPTR``
      - 0x00000002
      - This buffer type supports the ``V4L2_MEMORY_USERPTR`` streaming mode.
    * - ``V4L2_BUF_CAP_SUPPORTS_DMABUF``
      - 0x00000004
      - This buffer type supports the ``V4L2_MEMORY_DMABUF`` streaming mode.
    * - ``V4L2_BUF_CAP_SUPPORTS_REQUESTS``
      - 0x00000008
      - This buffer type supports :ref:`requests <media-request-api>`.

Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EINVAL
    The buffer type (``type`` field) or the requested I/O method
    (``memory``) is not supported.
