.. -*- coding: utf-8; mode: rst -*-

.. _DMX_STOP:

DMX_STOP
========

Description
-----------

This ioctl call is used to stop the actual filtering operation defined
via the ioctl calls DMX_SET_FILTER or DMX_SET_PES_FILTER and
started via the DMX_START command.

Synopsis
--------

.. c:function:: int ioctl( int fd, int request = DMX_STOP)

Arguments
----------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals DMX_STOP for this command.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


