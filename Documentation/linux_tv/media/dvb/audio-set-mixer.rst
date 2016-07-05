.. -*- coding: utf-8; mode: rst -*-

.. _AUDIO_SET_MIXER:

===============
AUDIO_SET_MIXER
===============

NAME
----

AUDIO_SET_MIXER

SYNOPSIS
--------

.. c:function:: int ioctl(int fd, int request = AUDIO_SET_MIXER, audio_mixer_t *mix)


ARGUMENTS
---------

.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals AUDIO_SET_ID for this command.

    -  .. row 3

       -  audio_mixer_t \*mix

       -  mixer settings.


DESCRIPTION
-----------

This ioctl lets you adjust the mixer settings of the audio decoder.


RETURN VALUE
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
