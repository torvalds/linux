.. -*- coding: utf-8; mode: rst -*-

.. _AUDIO_SET_KARAOKE:

=================
AUDIO_SET_KARAOKE
=================

Name
----

AUDIO_SET_KARAOKE

.. attention:: This ioctl is deprecated

Synopsis
--------

.. c:function:: int ioctl(fd, AUDIO_SET_KARAOKE, audio_karaoke_t *karaoke)
    :name: AUDIO_SET_KARAOKE


Arguments
---------

.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -

       -  int fd

       -  File descriptor returned by a previous call to open().

    -

       -  audio_karaoke_t \*karaoke

       -  karaoke settings according to section ??.


Description
-----------

This ioctl allows one to set the mixer settings for a karaoke DVD.


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

       -  karaoke is not a valid or supported karaoke setting.
