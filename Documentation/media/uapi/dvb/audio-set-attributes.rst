.. -*- coding: utf-8; mode: rst -*-

.. _AUDIO_SET_ATTRIBUTES:

====================
AUDIO_SET_ATTRIBUTES
====================

Name
----

AUDIO_SET_ATTRIBUTES

.. attention:: This ioctl is deprecated


Synopsis
--------

.. c:function:: int ioctl(fd, AUDIO_SET_ATTRIBUTES, struct audio_attributes *attr )
    :name: AUDIO_SET_ATTRIBUTES

Arguments
---------

.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -

       -  int fd

       -  File descriptor returned by a previous call to open().

    -

       -  audio_attributes_t attr

       -  audio attributes according to section ??


Description
-----------

This ioctl is intended for DVD playback and allows you to set certain
information about the audio stream.


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

       -  attr is not a valid or supported attribute setting.
