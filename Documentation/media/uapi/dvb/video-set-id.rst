.. -*- coding: utf-8; mode: rst -*-

.. _VIDEO_SET_ID:

============
VIDEO_SET_ID
============

Name
----

VIDEO_SET_ID


Synopsis
--------

.. c:function:: int ioctl(int fd, int request = VIDEO_SET_ID, int id)


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

       -  Equals VIDEO_SET_ID for this command.

    -  .. row 3

       -  int id

       -  video sub-stream id


Description
-----------

This ioctl selects which sub-stream is to be decoded if a program or
system stream is sent to the video device.


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

       -  Invalid sub-stream id.
