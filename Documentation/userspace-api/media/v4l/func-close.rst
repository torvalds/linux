.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: V4L

.. _func-close:

************
V4L2 close()
************

Name
====

v4l2-close - Close a V4L2 device

Synopsis
========

.. code-block:: c

    #include <unistd.h>

.. c:function:: int close( int fd )

Arguments
=========

``fd``
    File descriptor returned by :c:func:`open()`.

Description
===========

Closes the device. Any I/O in progress is terminated and resources
associated with the file descriptor are freed. However data format
parameters, current input or output, control values or other properties
remain unchanged.

Return Value
============

The function returns 0 on success, -1 on failure and the ``errno`` is
set appropriately. Possible error codes:

EBADF
    ``fd`` is not a valid open file descriptor.
