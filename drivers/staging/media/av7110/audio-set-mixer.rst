.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: DTV.audio

.. _AUDIO_SET_MIXER:

===============
AUDIO_SET_MIXER
===============

Name
----

AUDIO_SET_MIXER

.. attention:: This ioctl is deprecated

Synopsis
--------

.. c:macro:: AUDIO_SET_MIXER

``int ioctl(int fd, AUDIO_SET_MIXER, struct audio_mixer *mix)``

Arguments
---------

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -

       -  int fd

       -  File descriptor returned by a previous call to open().

    -

       -  audio_mixer_t \*mix

       -  mixer settings.

Description
-----------

This ioctl lets you adjust the mixer settings of the audio decoder.

Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
