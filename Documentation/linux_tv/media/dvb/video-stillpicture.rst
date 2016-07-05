.. -*- coding: utf-8; mode: rst -*-

.. _VIDEO_STILLPICTURE:

==================
VIDEO_STILLPICTURE
==================

Name
----

VIDEO_STILLPICTURE


Synopsis
--------

.. cpp:function:: int ioctl(fd, int request = VIDEO_STILLPICTURE, struct video_still_picture *sp)


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

       -  Equals VIDEO_STILLPICTURE for this command.

    -  .. row 3

       -  struct video_still_picture \*sp

       -  Pointer to a location where an I-frame and size is stored.


Description
-----------

This ioctl call asks the Video Device to display a still picture
(I-frame). The input data shall contain an I-frame. If the pointer is
NULL, then the current displayed still picture is blanked.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
