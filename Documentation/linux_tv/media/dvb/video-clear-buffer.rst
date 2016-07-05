.. -*- coding: utf-8; mode: rst -*-

.. _VIDEO_CLEAR_BUFFER:

==================
VIDEO_CLEAR_BUFFER
==================

Name
----

VIDEO_CLEAR_BUFFER


Synopsis
--------

.. c:function:: int ioctl(fd, int request = VIDEO_CLEAR_BUFFER)


Arguments
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


Description
-----------

This ioctl call clears all video buffers in the driver and in the
decoder hardware.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
