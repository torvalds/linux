.. -*- coding: utf-8; mode: rst -*-

.. _VIDEO_FREEZE:

============
VIDEO_FREEZE
============

Name
----

VIDEO_FREEZE


Synopsis
--------

.. c:function:: int ioctl(fd, int request = VIDEO_FREEZE)


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

       -  Equals VIDEO_FREEZE for this command.


Description
-----------

This ioctl is for DVB devices only. To control a V4L2 decoder use the
V4L2 :ref:`VIDIOC_DECODER_CMD` instead.

This ioctl call suspends the live video stream being played. Decoding
and playing are frozen. It is then possible to restart the decoding and
playing process of the video stream using the VIDEO_CONTINUE command.
If VIDEO_SOURCE_MEMORY is selected in the ioctl call
VIDEO_SELECT_SOURCE, the DVB subsystem will not decode any more data
until the ioctl call VIDEO_CONTINUE or VIDEO_PLAY is performed.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
