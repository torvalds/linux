.. -*- coding: utf-8; mode: rst -*-

.. _AUDIO_SET_BYPASS_MODE:

=====================
AUDIO_SET_BYPASS_MODE
=====================

Name
----

AUDIO_SET_BYPASS_MODE


Synopsis
--------

.. cpp:function:: int ioctl(int fd, int request = AUDIO_SET_BYPASS_MODE, boolean mode)


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

       -  Equals AUDIO_SET_BYPASS_MODE for this command.

    -  .. row 3

       -  boolean mode

       -  Enables or disables the decoding of the current Audio stream in
	  the DVB subsystem.

    -  .. row 4

       -
       -  TRUE Bypass is disabled

    -  .. row 5

       -
       -  FALSE Bypass is enabled


Description
-----------

This ioctl call asks the Audio Device to bypass the Audio decoder and
forward the stream without decoding. This mode shall be used if streams
that can’t be handled by the DVB system shall be decoded. Dolby
DigitalTM streams are automatically forwarded by the DVB subsystem if
the hardware can handle it.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
