.. -*- coding: utf-8; mode: rst -*-

.. _VIDEO_GET_SIZE:

==============
VIDEO_GET_SIZE
==============

Name
----

VIDEO_GET_SIZE


Synopsis
--------

.. c:function:: int ioctl(int fd, int request = VIDEO_GET_SIZE, video_size_t *size)


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

       -  Equals VIDEO_GET_SIZE for this command.

    -  .. row 3

       -  video_size_t \*size

       -  Returns the size and aspect ratio.


Description
-----------

This ioctl returns the size and aspect ratio.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
