.. -*- coding: utf-8; mode: rst -*-

.. _AUDIO_SET_AV_SYNC:

=================
AUDIO_SET_AV_SYNC
=================

Name
----

AUDIO_SET_AV_SYNC


Synopsis
--------

.. c:function:: int  ioctl(int fd, int request = AUDIO_SET_AV_SYNC, boolean state)


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

       -  Equals AUDIO_AV_SYNC for this command.

    -  .. row 3

       -  boolean state

       -  Tells the DVB subsystem if A/V synchronization shall be ON or OFF.

    -  .. row 4

       -
       -  TRUE AV-sync ON

    -  .. row 5

       -
       -  FALSE AV-sync OFF


Description
-----------

This ioctl call asks the Audio Device to turn ON or OFF A/V
synchronization.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
