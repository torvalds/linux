.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: DTV.dmx

.. _DMX_QUERYBUF:

******************
ioctl DMX_QUERYBUF
******************

Name
====

DMX_QUERYBUF - Query the status of a buffer

.. warning:: this API is still experimental

Synopsis
========

.. c:macro:: DMX_QUERYBUF

``int ioctl(int fd, DMX_QUERYBUF, struct dvb_buffer *argp)``

Arguments
=========

``fd``
    File descriptor returned by :c:func:`open()`.

``argp``
    Pointer to struct :c:type:`dvb_buffer`.

Description
===========

This ioctl is part of the mmap streaming I/O method. It can
be used to query the status of a buffer at any time after buffers have
been allocated with the :ref:`DMX_REQBUFS` ioctl.

Applications set the ``index`` field. Valid index numbers range from zero
to the number of buffers allocated with :ref:`DMX_REQBUFS`
(struct :c:type:`dvb_requestbuffers` ``count``) minus one.

After calling :ref:`DMX_QUERYBUF` with a pointer to this structure,
drivers return an error code or fill the rest of the structure.

On success, the ``offset`` will contain the offset of the buffer from the
start of the device memory, the ``length`` field its size, and the
``bytesused`` the number of bytes occupied by data in the buffer (payload).

Return Value
============

On success 0 is returned, the ``offset`` will contain the offset of the
buffer from the start of the device memory, the ``length`` field its size,
and the ``bytesused`` the number of bytes occupied by data in the buffer
(payload).

On error it returns -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EINVAL
    The ``index`` is out of bounds.
