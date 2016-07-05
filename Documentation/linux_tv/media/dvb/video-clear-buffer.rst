.. -*- coding: utf-8; mode: rst -*-

.. _VIDEO_CLEAR_BUFFER:

==================
VIDEO_CLEAR_BUFFER
==================

NAME
----

VIDEO_CLEAR_BUFFER

SYNOPSIS
--------

.. c:function:: int ioctl(fd, int request = VIDEO_CLEAR_BUFFER)


ARGUMENTS
---------

.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals VIDEO_CLEAR_BUFFER for this command.


DESCRIPTION
-----------

This ioctl call clears all video buffers in the driver and in the
decoder hardware.


RETURN VALUE
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
