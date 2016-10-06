.. -*- coding: utf-8; mode: rst -*-

.. _media-func-ioctl:

*************
media ioctl()
*************

Name
====

media-ioctl - Control a media device


Synopsis
========

.. code-block:: c

    #include <sys/ioctl.h>


.. cpp:function:: int ioctl( int fd, int request, void *argp )


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``request``
    Media ioctl request code as defined in the media.h header file, for
    example MEDIA_IOC_SETUP_LINK.

``argp``
    Pointer to a request-specific structure.


Description
===========

The :ref:`ioctl() <media-func-ioctl>` function manipulates media device
parameters. The argument ``fd`` must be an open file descriptor.

The ioctl ``request`` code specifies the media function to be called. It
has encoded in it whether the argument is an input, output or read/write
parameter, and the size of the argument ``argp`` in bytes.

Macros and structures definitions specifying media ioctl requests and
their parameters are located in the media.h header file. All media ioctl
requests, their respective function and parameters are specified in
:ref:`media-user-func`.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

Request-specific error codes are listed in the individual requests
descriptions.

When an ioctl that takes an output or read/write parameter fails, the
parameter remains unmodified.
