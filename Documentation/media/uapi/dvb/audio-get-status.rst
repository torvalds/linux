.. -*- coding: utf-8; mode: rst -*-

.. _AUDIO_GET_STATUS:

================
AUDIO_GET_STATUS
================

Name
----

AUDIO_GET_STATUS


Synopsis
--------

.. cpp:function:: int ioctl(int fd, int request = AUDIO_GET_STATUS, struct audio_status *status)


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

       -  Equals AUDIO_GET_STATUS for this command.

    -  .. row 3

       -  struct audio_status \*status

       -  Returns the current state of Audio Device.


Description
-----------

This ioctl call asks the Audio Device to return the current state of the
Audio Device.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
