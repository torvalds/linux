.. -*- coding: utf-8; mode: rst -*-

.. _VIDEO_GET_NAVI:

==============
VIDEO_GET_NAVI
==============

Name
----

VIDEO_GET_NAVI


Synopsis
--------

.. cpp:function:: int ioctl(fd, int request = VIDEO_GET_NAVI , video_navi_pack_t *navipack)


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

       -  Equals VIDEO_GET_NAVI for this command.

    -  .. row 3

       -  video_navi_pack_t \*navipack

       -  PCI or DSI pack (private stream 2) according to section ??.


Description
-----------

This ioctl returns navigational information from the DVD stream. This is
especially needed if an encoded stream has to be decoded by the
hardware.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  ``EFAULT``

       -  driver is not able to return navigational information
