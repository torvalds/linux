.. -*- coding: utf-8; mode: rst -*-

.. _VIDEO_SET_FORMAT:

================
VIDEO_SET_FORMAT
================

Name
----

VIDEO_SET_FORMAT

.. attention:: This ioctl is deprecated.

Synopsis
--------

.. c:function:: int ioctl(fd, VIDEO_SET_FORMAT, video_format_t format)
    :name: VIDEO_SET_FORMAT


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

       -  Equals VIDEO_SET_FORMAT for this command.

    -  .. row 3

       -  video_format_t format

       -  video format of TV as defined in section ??.


Description
-----------

This ioctl sets the screen format (aspect ratio) of the connected output
device (TV) so that the output of the decoder can be adjusted
accordingly.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  ``EINVAL``

       -  format is not a valid video format.
