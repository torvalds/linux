.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: DTV.video

.. _VIDEO_SLOWMOTION:

================
VIDEO_SLOWMOTION
================

Name
----

VIDEO_SLOWMOTION

.. attention:: This ioctl is deprecated.

Synopsis
--------

.. c:macro:: VIDEO_SLOWMOTION

``int ioctl(fd, VIDEO_SLOWMOTION, int nFrames)``

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

       -  Equals VIDEO_SLOWMOTION for this command.

    -  .. row 3

       -  int nFrames

       -  The number of times to repeat each frame.

Description
-----------

This ioctl call asks the video device to repeat decoding frames N number
of times. This call can only be used if VIDEO_SOURCE_MEMORY is
selected.

Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  .. row 1

       -  ``EPERM``

       -  Mode VIDEO_SOURCE_MEMORY not selected.
