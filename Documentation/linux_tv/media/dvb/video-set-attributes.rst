.. -*- coding: utf-8; mode: rst -*-

.. _VIDEO_SET_ATTRIBUTES:

====================
VIDEO_SET_ATTRIBUTES
====================

Name
----

VIDEO_SET_ATTRIBUTES


Synopsis
--------

.. c:function:: int ioctl(fd, int request = VIDEO_SET_ATTRIBUTE ,video_attributes_t vattr)


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

       -  Equals VIDEO_SET_ATTRIBUTE for this command.

    -  .. row 3

       -  video_attributes_t vattr

       -  video attributes according to section ??.


Description
-----------

This ioctl is intended for DVD playback and allows you to set certain
information about the stream. Some hardware may not need this
information, but the call also tells the hardware to prepare for DVD
playback.


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

       -  input is not a valid attribute setting.
