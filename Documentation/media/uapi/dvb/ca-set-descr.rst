.. -*- coding: utf-8; mode: rst -*-

.. _CA_SET_DESCR:

============
CA_SET_DESCR
============

Name
----

CA_SET_DESCR


Synopsis
--------

.. c:function:: int  ioctl(fd, int request = CA_SET_DESCR, ca_descr_t *)


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

       -  Equals CA_SET_DESCR for this command.

    -  .. row 3

       -  ca_descr_t \*

       -  Undocumented.


Description
-----------

This ioctl is undocumented. Documentation is welcome.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
