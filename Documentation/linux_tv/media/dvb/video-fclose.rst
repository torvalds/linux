.. -*- coding: utf-8; mode: rst -*-

.. _video_fclose:

dvb video close()
=================

Description
-----------

This system call closes a previously opened video device.

Synopsis
--------

.. c:function:: int close(int fd)

Arguments
----------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().


Return Value
------------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  ``EBADF``

       -  fd is not a valid open file descriptor.



