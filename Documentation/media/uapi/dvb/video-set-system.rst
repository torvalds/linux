.. -*- coding: utf-8; mode: rst -*-

.. _VIDEO_SET_SYSTEM:

================
VIDEO_SET_SYSTEM
================

Name
----

VIDEO_SET_SYSTEM


Synopsis
--------

.. cpp:function:: int ioctl(fd, int request = VIDEO_SET_SYSTEM , video_system_t system)


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

       -  video_system_t system

       -  video system of TV output.


Description
-----------

This ioctl sets the television output format. The format (see section
??) may vary from the color format of the displayed MPEG stream. If the
hardware is not able to display the requested format the call will
return an error.


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

       -  system is not a valid or supported video system.
