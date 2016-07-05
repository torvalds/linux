.. -*- coding: utf-8; mode: rst -*-

.. _VIDEO_GET_STATUS:

================
VIDEO_GET_STATUS
================

Name
----

VIDEO_GET_STATUS


Synopsis
--------

.. c:function:: int ioctl(fd, int request = VIDEO_GET_STATUS, struct video_status *status)


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

       -  Equals VIDEO_GET_STATUS for this command.

    -  .. row 3

       -  struct video_status \*status

       -  Returns the current status of the Video Device.


Description
-----------

This ioctl call asks the Video Device to return the current status of
the device.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
