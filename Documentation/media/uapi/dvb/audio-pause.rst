.. -*- coding: utf-8; mode: rst -*-

.. _AUDIO_PAUSE:

===========
AUDIO_PAUSE
===========

Name
----

AUDIO_PAUSE


Synopsis
--------

.. cpp:function:: int  ioctl(int fd, int request = AUDIO_PAUSE)


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

       -  Equals AUDIO_PAUSE for this command.


Description
-----------

This ioctl call suspends the audio stream being played. Decoding and
playing are paused. It is then possible to restart again decoding and
playing process of the audio stream using AUDIO_CONTINUE command.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
