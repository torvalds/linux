.. -*- coding: utf-8; mode: rst -*-

.. _VIDEO_STOP:

==========
VIDEO_STOP
==========

Name
----

VIDEO_STOP

.. attention:: This ioctl is deprecated.

Synopsis
--------

.. c:function:: int ioctl(fd, VIDEO_STOP, boolean mode)
    :name: VIDEO_STOP


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

       -  Equals VIDEO_STOP for this command.

    -  .. row 3

       -  Boolean mode

       -  Indicates how the screen shall be handled.

    -  .. row 4

       -
       -  TRUE: Blank screen when stop.

    -  .. row 5

       -
       -  FALSE: Show last decoded frame.


Description
-----------

This ioctl is for Digital TV devices only. To control a V4L2 decoder use the
V4L2 :ref:`VIDIOC_DECODER_CMD` instead.

This ioctl call asks the Video Device to stop playing the current
stream. Depending on the input parameter, the screen can be blanked out
or displaying the last decoded frame.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
