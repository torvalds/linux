.. -*- coding: utf-8; mode: rst -*-

.. _VIDEO_SET_HIGHLIGHT:

===================
VIDEO_SET_HIGHLIGHT
===================

Name
----

VIDEO_SET_HIGHLIGHT


Synopsis
--------

.. c:function:: int ioctl(fd, int request = VIDEO_SET_HIGHLIGHT ,video_highlight_t *vhilite)


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

       -  Equals VIDEO_SET_HIGHLIGHT for this command.

    -  .. row 3

       -  video_highlight_t \*vhilite

       -  SPU Highlight information according to section ??.


Description
-----------

This ioctl sets the SPU highlight information for the menu access of a
DVD.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
