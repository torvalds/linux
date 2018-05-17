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
    :name: dmx-munmap

Arguments
=========

``start``
    Address of the mapped buffer as returned by the
    :ref:`mmap() <dmx-mmap>` function.

``length``
    Length of the mapped buffer. This must be the same value as given to
    :ref:`mmap() <dmx-mmap>`.


Description
===========

Unmaps a previously with the :ref:`mmap() <dmx-mmap>` function mapped
buffer and frees it, if possible.


Return Value
============

On success :ref:`munmap() <dmx-munmap>` returns 0, on failure -1 and the
``errno`` variable is set appropriately:

EINVAL
    The ``start`` or ``length`` is incorrect, or no buffers have been
    mapped yet.
