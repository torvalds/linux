.. -*- coding: utf-8; mode: rst -*-

.. _dmx_fclose:

DVB demux close()
=================

Description
-----------

This system call deactivates and deallocates a filter that was
previously allocated via the open() call.

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



