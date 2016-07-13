.. -*- coding: utf-8; mode: rst -*-

.. _DMX_GET_PES_PIDS:

================
DMX_GET_PES_PIDS
================

Name
----

DMX_GET_PES_PIDS


Synopsis
--------

.. cpp:function:: int ioctl(fd, int request = DMX_GET_PES_PIDS, __u16[5])


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

       -  Equals DMX_GET_PES_PIDS for this command.

    -  .. row 3

       -  __u16[5]

       -  Undocumented.


Description
-----------

This ioctl is undocumented. Documentation is welcome.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
