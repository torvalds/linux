.. -*- coding: utf-8; mode: rst -*-

.. _VIDEO_GET_FRAME_COUNT:

=====================
VIDEO_GET_FRAME_COUNT
=====================

Name
----

VIDEO_GET_FRAME_COUNT

.. attention:: This ioctl is deprecated.

Synopsis
--------

.. c:function:: int ioctl(int fd, VIDEO_GET_FRAME_COUNT, __u64 *pts)
    :name: VIDEO_GET_FRAME_COUNT


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

       -  Equals VIDEO_GET_FRAME_COUNT for this command.

    -  .. row 3

       -  __u64 \*pts

       -  Returns the number of frames displayed since the decoder was
	  started.


Description
-----------

This ioctl is obsolete. Do not use in new drivers. For V4L2 decoders
this ioctl has been replaced by the ``V4L2_CID_MPEG_VIDEO_DEC_FRAME``
control.

This ioctl call asks the Video Device to return the number of displayed
frames since the decoder was started.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
