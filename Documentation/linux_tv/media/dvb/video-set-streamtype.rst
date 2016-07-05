.. -*- coding: utf-8; mode: rst -*-

.. _VIDEO_SET_STREAMTYPE:

====================
VIDEO_SET_STREAMTYPE
====================

NAME
----

VIDEO_SET_STREAMTYPE

SYNOPSIS
--------

.. c:function:: int ioctl(fd, int request = VIDEO_SET_STREAMTYPE, int type)


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

       -  Equals VIDEO_SET_STREAMTYPE for this command.

    -  .. row 3

       -  int type

       -  stream type


DESCRIPTION
-----------

This ioctl tells the driver which kind of stream to expect being written
to it. If this call is not used the default of video PES is used. Some
drivers might not support this call and always expect PES.


RETURN VALUE
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
