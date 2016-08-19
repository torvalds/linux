.. -*- coding: utf-8; mode: rst -*-

.. _func-write:

************
V4L2 write()
************

Name
====

v4l2-write - Write to a V4L2 device


Synopsis
========

.. code-block:: c

    #include <unistd.h>


.. c:function:: ssize_t write( int fd, void *buf, size_t count )


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``buf``
``count``


Description
===========

:ref:`write() <func-write>` writes up to ``count`` bytes to the device
referenced by the file descriptor ``fd`` from the buffer starting at
``buf``. When the hardware outputs are not active yet, this function
enables them. When ``count`` is zero, :ref:`write() <func-write>` returns 0
without any other effect.

When the application does not provide more data in time, the previous
video frame, raw VBI image, sliced VPS or WSS data is displayed again.
Sliced Teletext or Closed Caption data is not repeated, the driver
inserts a blank line instead.


Return Value
============

On success, the number of bytes written are returned. Zero indicates
nothing was written. On error, -1 is returned, and the ``errno``
variable is set appropriately. In this case the next write will start at
the beginning of a new frame. Possible error codes are:

EAGAIN
    Non-blocking I/O has been selected using the
    :ref:`O_NONBLOCK <func-open>` flag and no buffer space was
    available to write the data immediately.

EBADF
    ``fd`` is not a valid file descriptor or is not open for writing.

EBUSY
    The driver does not support multiple write streams and the device is
    already in use.

EFAULT
    ``buf`` references an inaccessible memory area.

EINTR
    The call was interrupted by a signal before any data was written.

EIO
    I/O error. This indicates some hardware problem.

EINVAL
    The :ref:`write() <func-write>` function is not supported by this driver,
    not on this device, or generally not on this type of device.
