.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: DTV.video

.. _VIDEO_CONTINUE:

==============
VIDEO_CONTINUE
==============

Name
----

VIDEO_CONTINUE

.. attention:: This ioctl is deprecated.

Synopsis
--------

.. c:macro:: VIDEO_CONTINUE

``int ioctl(fd, VIDEO_CONTINUE)``

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

       -  Equals VIDEO_CONTINUE for this command.

Description
-----------

This ioctl is for Digital TV devices only. To control a V4L2 decoder use the
V4L2 :ref:`VIDIOC_DECODER_CMD` instead.

This ioctl call restarts decoding and playing processes of the video
stream which was played before a call to VIDEO_FREEZE was made.

Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
