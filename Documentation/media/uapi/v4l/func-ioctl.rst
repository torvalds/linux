.. -*- coding: utf-8; mode: rst -*-

.. _func-ioctl:

************
V4L2 ioctl()
************

Name
====

v4l2-ioctl - Program a V4L2 device


Synopsis
========

.. code-block:: c

    #include <sys/ioctl.h>


.. c:function:: int ioctl( int fd, int request, void *argp )
    :name: v4l2-ioctl

Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``request``
    V4L2 ioctl request code as defined in the ``videodev2.h`` header
    file, for example VIDIOC_QUERYCAP.

``argp``
    Pointer to a function parameter, usually a structure.


Description
===========

The :ref:`ioctl() <func-ioctl>` function is used to program V4L2 devices. The
argument ``fd`` must be an open file descriptor. An ioctl ``request``
has encoded in it whether the argument is an input, output or read/write
parameter, and the size of the argument ``argp`` in bytes. Macros and
defines specifying V4L2 ioctl requests are located in the
``videodev2.h`` header file. Applications should use their own copy, not
include the version in the kernel sources on the system they compile on.
All V4L2 ioctl requests, their respective function and parameters are
specified in :ref:`user-func`.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

When an ioctl that takes an output or read/write parameter fails, the
parameter remains unmodified.
