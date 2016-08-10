.. -*- coding: utf-8; mode: rst -*-

.. _VIDEO_SET_DISPLAY_FORMAT:

========================
VIDEO_SET_DISPLAY_FORMAT
========================

Name
----

VIDEO_SET_DISPLAY_FORMAT


Synopsis
--------

.. cpp:function:: int ioctl(fd, int request = VIDEO_SET_DISPLAY_FORMAT, video_display_format_t format)


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

       -  Equals VIDEO_SET_DISPLAY_FORMAT for this command.

    -  .. row 3

       -  video_display_format_t format

       -  Selects the video format to be used.


Description
-----------

This ioctl call asks the Video Device to select the video format to be
applied by the MPEG chip on the video.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
