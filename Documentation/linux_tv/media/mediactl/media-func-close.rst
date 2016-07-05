.. -*- coding: utf-8; mode: rst -*-

.. _media-func-close:

*************
media close()
*************

NAME
====

media-close - Close a media device

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

Closes the media device. Resources associated with the file descriptor
are freed. The device configuration remain unchanged.


RETURN VALUE
============

:ref:`close() <func-close>` returns 0 on success. On error, -1 is returned, and
``errno`` is set appropriately. Possible error codes are:

EBADF
    ``fd`` is not a valid open file descriptor.
