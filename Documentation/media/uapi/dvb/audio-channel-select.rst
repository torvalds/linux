.. -*- coding: utf-8; mode: rst -*-

.. _AUDIO_CHANNEL_SELECT:

====================
AUDIO_CHANNEL_SELECT
====================

Name
----

AUDIO_CHANNEL_SELECT

.. attention:: This ioctl is deprecated

Synopsis
--------

.. c:function:: int ioctl(int fd, AUDIO_CHANNEL_SELECT, struct *audio_channel_select)
    :name: AUDIO_CHANNEL_SELECT


Arguments
---------

.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -

       -  int fd

       -  File descriptor returned by a previous call to open().

    -

       -  audio_channel_select_t ch

       -  Select the output format of the audio (mono left/right, stereo).


Description
-----------

This ioctl is for Digital TV devices only. To control a V4L2 decoder use the
V4L2 ``V4L2_CID_MPEG_AUDIO_DEC_PLAYBACK`` control instead.

This ioctl call asks the Audio Device to select the requested channel if
possible.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
