.. -*- coding: utf-8; mode: rst -*-

.. _AUDIO_SET_EXT_ID:

================
AUDIO_SET_EXT_ID
================

Name
----

AUDIO_SET_EXT_ID


Synopsis
--------

.. c:function:: int  ioctl(fd, int request = AUDIO_SET_EXT_ID, int id)


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

       -  Equals AUDIO_SET_EXT_ID for this command.

    -  .. row 3

       -  int id

       -  audio sub_stream_id


Description
-----------

This ioctl can be used to set the extension id for MPEG streams in DVD
playback. Only the first 3 bits are recognized.


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

       -  id is not a valid id.
