.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

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

.. c:function:: int ioctl( int fd, DMX_QBUF, struct dmx_buffer *argp )
    :name: DMX_QBUF

.. c:function:: int ioctl( int fd, DMX_DQBUF, struct dmx_buffer *argp )
    :name: DMX_DQBUF


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <dmx_fopen>`.

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
the device is closed.

Applications call the ``DMX_DQBUF`` ioctl to dequeue a filled
(capturing) buffer from the driver's outgoing queue.
They just set the ``index`` field withe the buffer ID to be queued.
When ``DMX_DQBUF`` is called with a pointer to struct :c:type:`dmx_buffer`,
the driver fills the remaining fields or returns an error code.

By default ``DMX_DQBUF`` blocks when no buffer is in the outgoing
queue. When the ``O_NONBLOCK`` flag was given to the
:ref:`open() <dmx_fopen>` function, ``DMX_DQBUF`` returns
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
