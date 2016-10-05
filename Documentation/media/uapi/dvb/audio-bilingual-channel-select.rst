.. -*- coding: utf-8; mode: rst -*-

.. _AUDIO_BILINGUAL_CHANNEL_SELECT:

==============================
AUDIO_BILINGUAL_CHANNEL_SELECT
==============================

Name
----

AUDIO_BILINGUAL_CHANNEL_SELECT


Synopsis
--------

.. cpp:function:: int ioctl(int fd, int request = AUDIO_BILINGUAL_CHANNEL_SELECT, audio_channel_select_t)


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

       -  Equals AUDIO_BILINGUAL_CHANNEL_SELECT for this command.

    -  .. row 3

       -  audio_channel_select_t ch

       -  Select the output format of the audio (mono left/right, stereo).


Description
-----------

This ioctl is obsolete. Do not use in new drivers. It has been replaced
by the V4L2 ``V4L2_CID_MPEG_AUDIO_DEC_MULTILINGUAL_PLAYBACK`` control
for MPEG decoders controlled through V4L2.

This ioctl call asks the Audio Device to select the requested channel
for bilingual streams if possible.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
