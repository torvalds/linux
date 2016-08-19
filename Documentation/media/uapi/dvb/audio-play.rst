.. -*- coding: utf-8; mode: rst -*-

.. _AUDIO_PLAY:

==========
AUDIO_PLAY
==========

Name
----

AUDIO_PLAY

.. attention:: This ioctl is deprecated

Synopsis
--------

.. c:function:: int  ioctl(int fd, AUDIO_PLAY)
    :name: AUDIO_PLAY


Arguments
---------

.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

Description
-----------

This ioctl call asks the Audio Device to start playing an audio stream
from the selected source.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
