.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: DTV.dmx

.. _dmx-munmap:

************
DVB munmap()
************

Name
====

dmx-munmap - Unmap device memory

.. warning:: This API is still experimental.

Synopsis
========

.. code-block:: c

    #include <unistd.h>
    #include <sys/mman.h>

.. c:function:: int munmap( void *start, size_t length )

Arguments
=========

``start``
    Address of the mapped buffer as returned by the
    :c:func:`mmap()` function.

``length``
    Length of the mapped buffer. This must be the same value as given to
    :c:func:`mmap()`.

Description
===========

Unmaps a previously with the :c:func:`mmap()` function mapped
buffer and frees it, if possible.

Return Value
============

On success :c:func:`munmap()` returns 0, on failure -1 and the
``errno`` variable is set appropriately:

EINVAL
    The ``start`` or ``length`` is incorrect, or no buffers have been
    mapped yet.
