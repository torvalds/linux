.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: DTV.audio

.. _AUDIO_SET_AV_SYNC:

=================
AUDIO_SET_AV_SYNC
=================

Name
----

AUDIO_SET_AV_SYNC

.. attention:: This ioctl is deprecated

Synopsis
--------

.. c:macro:: AUDIO_SET_AV_SYNC

``int ioctl(int fd, AUDIO_SET_AV_SYNC, boolean state)``

Arguments
---------

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -

       -  int fd

       -  File descriptor returned by a previous call to open().

    -

       -  boolean state

       -  Tells the Digital TV subsystem if A/V synchronization shall be ON or OFF.

          TRUE: AV-sync ON

          FALSE: AV-sync OFF

Description
-----------

This ioctl call asks the Audio Device to turn ON or OFF A/V
synchronization.

Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
