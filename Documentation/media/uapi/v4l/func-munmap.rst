.. -*- coding: utf-8; mode: rst -*-

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
    :ref:`mmap() <func-mmap>` function.

``length``
    Length of the mapped buffer. This must be the same value as given to
    :ref:`mmap() <func-mmap>` and returned by the driver in the struct
    :ref:`v4l2_buffer <v4l2-buffer>` ``length`` field for the
    single-planar API and in the struct
    :ref:`v4l2_plane <v4l2-plane>` ``length`` field for the
    multi-planar API.


Description
===========

Unmaps a previously with the :ref:`mmap() <func-mmap>` function mapped
buffer and frees it, if possible.


Return Value
============

On success :ref:`munmap() <func-munmap>` returns 0, on failure -1 and the
``errno`` variable is set appropriately:

EINVAL
    The ``start`` or ``length`` is incorrect, or no buffers have been
    mapped yet.
