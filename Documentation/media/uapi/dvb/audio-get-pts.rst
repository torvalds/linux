.. -*- coding: utf-8; mode: rst -*-

.. _AUDIO_GET_PTS:

=============
AUDIO_GET_PTS
=============

Name
----

AUDIO_GET_PTS


Synopsis
--------

.. cpp:function:: int ioctl(int fd, int request = AUDIO_GET_PTS, __u64 *pts)


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

       -  Equals AUDIO_GET_PTS for this command.

    -  .. row 3

       -  __u64 \*pts

       -  Returns the 33-bit timestamp as defined in ITU T-REC-H.222.0 /
	  ISO/IEC 13818-1.

	  The PTS should belong to the currently played frame if possible,
	  but may also be a value close to it like the PTS of the last
	  decoded frame or the last PTS extracted by the PES parser.


Description
-----------

This ioctl is obsolete. Do not use in new drivers. If you need this
functionality, then please contact the linux-media mailing list
(`https://linuxtv.org/lists.php <https://linuxtv.org/lists.php>`__).

This ioctl call asks the Audio Device to return the current PTS
timestamp.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
