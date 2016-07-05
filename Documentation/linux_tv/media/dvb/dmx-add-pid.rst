.. -*- coding: utf-8; mode: rst -*-

.. _DMX_ADD_PID:

===========
DMX_ADD_PID
===========

Name
----

DMX_ADD_PID


Synopsis
--------

.. c:function:: int ioctl(fd, int request = DMX_ADD_PID, __u16 *)


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

       -  Equals DMX_ADD_PID for this command.

    -  .. row 3

       -  __u16 *

       -  PID number to be filtered.


Description
-----------

This ioctl call allows to add multiple PIDs to a transport stream filter
previously set up with DMX_SET_PES_FILTER and output equal to
DMX_OUT_TSDEMUX_TAP.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
