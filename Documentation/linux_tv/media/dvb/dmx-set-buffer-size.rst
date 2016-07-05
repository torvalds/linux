.. -*- coding: utf-8; mode: rst -*-

.. _DMX_SET_BUFFER_SIZE:

===================
DMX_SET_BUFFER_SIZE
===================

Name
----

DMX_SET_BUFFER_SIZE


Synopsis
--------

.. cpp:function:: int ioctl( int fd, int request = DMX_SET_BUFFER_SIZE, unsigned long size)


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

       -  Equals DMX_SET_BUFFER_SIZE for this command.

    -  .. row 3

       -  unsigned long size

       -  Size of circular buffer.


Description
-----------

This ioctl call is used to set the size of the circular buffer used for
filtered data. The default size is two maximum sized sections, i.e. if
this function is not called a buffer size of 2 \* 4096 bytes will be
used.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
