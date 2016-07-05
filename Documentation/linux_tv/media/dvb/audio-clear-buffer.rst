.. -*- coding: utf-8; mode: rst -*-

.. _AUDIO_CLEAR_BUFFER:

==================
AUDIO_CLEAR_BUFFER
==================

Name
----

AUDIO_CLEAR_BUFFER


Synopsis
--------

.. c:function:: int  ioctl(int fd, int request = AUDIO_CLEAR_BUFFER)


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

       -  Equals AUDIO_CLEAR_BUFFER for this command.


Description
-----------

This ioctl call asks the Audio Device to clear all software and hardware
buffers of the audio decoder device.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
