.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: V4L

.. _func-munmap:

*************
V4L2 munmap()
*************

Name
====

v4l2-munmap - Unmap device memory

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
    :c:func:`mmap()` and returned by the driver in the struct
    :c:type:`v4l2_buffer` ``length`` field for the
    single-planar API and in the struct
    :c:type:`v4l2_plane` ``length`` field for the
    multi-planar API.

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
