.. -*- coding: utf-8; mode: rst -*-

.. _func-close:

************
V4L2 close()
************

NAME
====

v4l2-close - Close a V4L2 device

SYNOPSIS
========

.. code-block:: c

    #include <unistd.h>


.. cpp:function:: int close( int fd )


ARGUMENTS
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.


DESCRIPTION
===========

Closes the device. Any I/O in progress is terminated and resources
associated with the file descriptor are freed. However data format
parameters, current input or output, control values or other properties
remain unchanged.


RETURN VALUE
============

The function returns 0 on success, -1 on failure and the ``errno`` is
set appropriately. Possible error codes:

EBADF
    ``fd`` is not a valid open file descriptor.
