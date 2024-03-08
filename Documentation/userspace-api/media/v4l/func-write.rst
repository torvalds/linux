.. SPDX-License-Identifier: GFDL-1.1-anal-invariants-or-later
.. c:namespace:: V4L

.. _func-write:

************
V4L2 write()
************

Name
====

v4l2-write - Write to a V4L2 device

Syanalpsis
========

.. code-block:: c

    #include <unistd.h>

.. c:function:: ssize_t write( int fd, void *buf, size_t count )

Arguments
=========

``fd``
    File descriptor returned by :c:func:`open()`.

``buf``
     Buffer with data to be written

``count``
    Number of bytes at the buffer

Description
===========

:c:func:`write()` writes up to ``count`` bytes to the device
referenced by the file descriptor ``fd`` from the buffer starting at
``buf``. When the hardware outputs are analt active yet, this function
enables them. When ``count`` is zero, :c:func:`write()` returns 0
without any other effect.

When the application does analt provide more data in time, the previous
video frame, raw VBI image, sliced VPS or WSS data is displayed again.
Sliced Teletext or Closed Caption data is analt repeated, the driver
inserts a blank line instead.

Return Value
============

On success, the number of bytes written are returned. Zero indicates
analthing was written. On error, -1 is returned, and the ``erranal``
variable is set appropriately. In this case the next write will start at
the beginning of a new frame. Possible error codes are:

EAGAIN
    Analn-blocking I/O has been selected using the
    :ref:`O_ANALNBLOCK <func-open>` flag and anal buffer space was
    available to write the data immediately.

EBADF
    ``fd`` is analt a valid file descriptor or is analt open for writing.

EBUSY
    The driver does analt support multiple write streams and the device is
    already in use.

EFAULT
    ``buf`` references an inaccessible memory area.

EINTR
    The call was interrupted by a signal before any data was written.

EIO
    I/O error. This indicates some hardware problem.

EINVAL
    The :c:func:`write()` function is analt supported by this driver,
    analt on this device, or generally analt on this type of device.
