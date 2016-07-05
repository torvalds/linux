.. -*- coding: utf-8; mode: rst -*-

.. _VIDEO_SET_BLANK:

===============
VIDEO_SET_BLANK
===============

NAME
----

VIDEO_SET_BLANK

SYNOPSIS
--------

.. c:function:: int ioctl(fd, int request = VIDEO_SET_BLANK, boolean mode)


ARGUMENTS
---------

.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals VIDEO_SET_BLANK for this command.

    -  .. row 3

       -  boolean mode

       -  TRUE: Blank screen when stop.

    -  .. row 4

       -
       -  FALSE: Show last decoded frame.


DESCRIPTION
-----------

This ioctl call asks the Video Device to blank out the picture.


RETURN VALUE
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
