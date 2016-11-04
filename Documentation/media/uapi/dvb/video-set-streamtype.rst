.. -*- coding: utf-8; mode: rst -*-

.. _VIDEO_SET_STREAMTYPE:

====================
VIDEO_SET_STREAMTYPE
====================

Name
----

VIDEO_SET_STREAMTYPE

.. attention:: This ioctl is deprecated.

Synopsis
--------

.. c:function:: int ioctl(fd, VIDEO_SET_STREAMTYPE, int type)
    :name: VIDEO_SET_STREAMTYPE


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

       -  Equals VIDEO_SET_STREAMTYPE for this command.

    -  .. row 3

       -  int type

       -  stream type


Description
-----------

This ioctl tells the driver which kind of stream to expect being written
to it. If this call is not used the default of video PES is used. Some
drivers might not support this call and always expect PES.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
