.. -*- coding: utf-8; mode: rst -*-

.. _VIDEO_GET_FRAME_RATE:

====================
VIDEO_GET_FRAME_RATE
====================

Name
----

VIDEO_GET_FRAME_RATE


Synopsis
--------

.. cpp:function:: int ioctl(int fd, int request = VIDEO_GET_FRAME_RATE, unsigned int *rate)


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

       -  Equals VIDEO_GET_FRAME_RATE for this command.

    -  .. row 3

       -  unsigned int \*rate

       -  Returns the framerate in number of frames per 1000 seconds.


Description
-----------

This ioctl call asks the Video Device to return the current framerate.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
