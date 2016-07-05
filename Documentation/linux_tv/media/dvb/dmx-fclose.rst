.. -*- coding: utf-8; mode: rst -*-

.. _dmx_fclose:

=================
DVB demux close()
=================

NAME
----

DVB demux close()

SYNOPSIS
--------

.. c:function:: int close(int fd)


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

This system call deactivates and deallocates a filter that was
previously allocated via the open() call.


RETURN VALUE
------------

.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  ``EBADF``

       -  fd is not a valid open file descriptor.
