.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: DTV.dmx

.. _DMX_QBUF:

*************************
ioctl DMX_QBUF, DMX_DQBUF
*************************

Name
====

DMX_QBUF - DMX_DQBUF - Exchange a buffer with the driver

.. warning:: this API is still experimental

Synopsis
========

.. c:macro:: DMX_QBUF

``int ioctl(int fd, DMX_QBUF, struct dmx_buffer *argp)``

.. c:macro:: DMX_DQBUF

``int ioctl(int fd, DMX_DQBUF, struct dmx_buffer *argp)``

Arguments
=========

``fd``
    File descriptor returned by :c:func:`open()`.

``argp``
    Pointer to struct :c:type:`dmx_buffer`.

Description
===========

Applications call the ``DMX_QBUF`` ioctl to enqueue an empty
(capturing) or filled (output) buffer in the driver's incoming queue.
The semantics depend on the selected I/O method.

To enqueue a buffer applications set the ``index`` field. Valid index
numbers range from zero to the number of buffers allocated with
:ref:`DMX_REQBUFS` (struct :c:type:`dmx_requestbuffers` ``count``) minus
one. The contents of the struct :c:type:`dmx_buffer` returned
by a :ref:`DMX_QUERYBUF` ioctl will do as well.

When ``DMX_QBUF`` is called with a pointer to this structure, it locks the
memory pages of the buffer in physical memory, so they cannot be swapped
out to disk. Buffers remain locked until dequeued, until the
device is closed.

Applications call the ``DMX_DQBUF`` ioctl to dequeue a filled
(capturing) buffer from the driver's outgoing queue.
They just set the ``index`` field with the buffer ID to be queued.
When ``DMX_DQBUF`` is called with a pointer to struct :c:type:`dmx_buffer`,
the driver fills the remaining fields or returns an error code.

By default ``DMX_DQBUF`` blocks when no buffer is in the outgoing
queue. When the ``O_NONBLOCK`` flag was given to the
:c:func:`open()` function, ``DMX_DQBUF`` returns
immediately with an ``EAGAIN`` error code when no buffer is available.

The struct :c:type:`dmx_buffer` structure is specified in
:ref:`buffer`.

Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EAGAIN
    Non-blocking I/O has been selected using ``O_NONBLOCK`` and no
    buffer was in the outgoing queue.

EINVAL
    The ``index`` is out of bounds, or no buffers have been allocated yet.

EIO
    ``DMX_DQBUF`` failed due to an internal error. Can also indicate
    temporary problems like signal loss or CRC errors.
