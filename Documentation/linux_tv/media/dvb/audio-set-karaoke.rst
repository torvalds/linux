.. -*- coding: utf-8; mode: rst -*-

.. _AUDIO_SET_KARAOKE:

=================
AUDIO_SET_KARAOKE
=================

NAME
----

AUDIO_SET_KARAOKE

SYNOPSIS
--------

.. c:function:: int ioctl(fd, int request = AUDIO_SET_KARAOKE, audio_karaoke_t *karaoke)


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

       -  Equals AUDIO_SET_KARAOKE for this command.

    -  .. row 3

       -  audio_karaoke_t \*karaoke

       -  karaoke settings according to section ??.


DESCRIPTION
-----------

This ioctl allows one to set the mixer settings for a karaoke DVD.


RETURN VALUE
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
