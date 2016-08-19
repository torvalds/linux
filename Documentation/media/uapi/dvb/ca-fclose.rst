.. -*- coding: utf-8; mode: rst -*-

.. _ca_fclose:

==============
DVB CA close()
==============

Name
----

DVB CA close()


Synopsis
--------

.. c:function:: int  close(int fd)


Arguments
---------

.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().


Description
-----------

This system call closes a previously opened audio device.


Return Value
------------

.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  ``EBADF``

       -  fd is not a valid open file descriptor.
