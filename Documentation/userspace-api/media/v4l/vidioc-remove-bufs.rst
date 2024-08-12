.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: V4L

.. _VIDIOC_REMOVE_BUFS:

************************
ioctl VIDIOC_REMOVE_BUFS
************************

Name
====

VIDIOC_REMOVE_BUFS - Removes buffers from a queue

Synopsis
========

.. c:macro:: VIDIOC_REMOVE_BUFS

``int ioctl(int fd, VIDIOC_REMOVE_BUFS, struct v4l2_remove_buffers *argp)``

Arguments
=========

``fd``
    File descriptor returned by :c:func:`open()`.

``argp``
    Pointer to struct :c:type:`v4l2_remove_buffers`.

Description
===========

Applications can optionally call the :ref:`VIDIOC_REMOVE_BUFS` ioctl to
remove buffers from a queue.
:ref:`VIDIOC_CREATE_BUFS` ioctl support is mandatory to enable :ref:`VIDIOC_REMOVE_BUFS`.
This ioctl is available if the ``V4L2_BUF_CAP_SUPPORTS_REMOVE_BUFS`` capability
is set on the queue when :c:func:`VIDIOC_REQBUFS` or :c:func:`VIDIOC_CREATE_BUFS`
are invoked.

.. c:type:: v4l2_remove_buffers

.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.5cm}|

.. flat-table:: struct v4l2_remove_buffers
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u32
      - ``index``
      - The starting buffer index to remove. This field is ignored if count == 0.
    * - __u32
      - ``count``
      - The number of buffers to be removed with indices 'index' until 'index + count - 1'.
        All buffers in this range must be valid and in DEQUEUED state.
        :ref:`VIDIOC_REMOVE_BUFS` will always check the validity of ``type`, if it is
        invalid it returns ``EINVAL`` error code.
        If count is set to 0 :ref:`VIDIOC_REMOVE_BUFS` will do nothing and return 0.
    * - __u32
      - ``type``
      - Type of the stream or buffers, this is the same as the struct
	:c:type:`v4l2_format` ``type`` field. See
	:c:type:`v4l2_buf_type` for valid values.
    * - __u32
      - ``reserved``\ [13]
      - A place holder for future extensions. Drivers and applications
	must set the array to zero.

Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter. If an error occurs, no
buffers will be freed and one of the error codes below will be returned:

EBUSY
    File I/O is in progress.
    One or more of the buffers in the range ``index`` to ``index + count - 1`` are not
    in DEQUEUED state.

EINVAL
    One or more of the buffers in the range ``index`` to ``index + count - 1`` do not
    exist in the queue.
    The buffer type (``type`` field) is not valid.
