.. -*- coding: utf-8; mode: rst -*-

.. _AUDIO_STOP:

==========
AUDIO_STOP
==========

Name
----

AUDIO_STOP

.. attention:: This ioctl is deprecated

Synopsis
--------

.. c:function:: int ioctl(int fd, AUDIO_STOP)
    :name: AUDIO_STOP

Arguments
---------

.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().


Description
-----------

This ioctl call asks the Audio Device to stop playing the current
stream.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
