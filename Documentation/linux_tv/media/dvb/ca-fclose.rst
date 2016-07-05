.. -*- coding: utf-8; mode: rst -*-

.. _ca_fclose:

==============
DVB CA close()
==============

NAME
----

DVB CA close()

SYNOPSIS
--------

.. cpp:function:: int  close(int fd)


ARGUMENTS
---------

.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().


DESCRIPTION
-----------

This system call closes a previously opened audio device.


RETURN VALUE
------------

.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  ``EBADF``

       -  fd is not a valid open file descriptor.
