.. -*- coding: utf-8; mode: rst -*-

.. _DMX_REMOVE_PID:

==============
DMX_REMOVE_PID
==============

NAME
----

DMX_REMOVE_PID

SYNOPSIS
--------

.. c:function:: int ioctl(fd, int request = DMX_REMOVE_PID, __u16 *)


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

       -  Equals DMX_REMOVE_PID for this command.

    -  .. row 3

       -  __u16 *

       -  PID of the PES filter to be removed.


DESCRIPTION
-----------

This ioctl call allows to remove a PID when multiple PIDs are set on a
transport stream filter, e. g. a filter previously set up with output
equal to DMX_OUT_TSDEMUX_TAP, created via either
DMX_SET_PES_FILTER or DMX_ADD_PID.


RETURN VALUE
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
