.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: DTV.audio

.. _AUDIO_SET_STREAMTYPE:

====================
AUDIO_SET_STREAMTYPE
====================

Name
----

AUDIO_SET_STREAMTYPE

.. attention:: This ioctl is deprecated

Synopsis
--------

.. c:macro:: AUDIO_SET_STREAMTYPE

``int ioctl(fd, AUDIO_SET_STREAMTYPE, int type)``

Arguments
---------

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -

       -  int fd

       -  File descriptor returned by a previous call to open().

    -

       -  int type

       -  stream type

Description
-----------

This ioctl tells the driver which kind of audio stream to expect. This
is useful if the stream offers several audio sub-streams like LPCM and
AC3.

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

       -  type is not a valid or supported stream type.
