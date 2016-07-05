.. -*- coding: utf-8; mode: rst -*-

.. _CA_RESET:

CA_RESET
========

Description
-----------

This ioctl is undocumented. Documentation is welcome.

Synopsis
--------

.. cpp:function:: int  ioctl(fd, int request = CA_RESET)

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

       -  Equals CA_RESET for this command.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


