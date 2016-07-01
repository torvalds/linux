.. -*- coding: utf-8; mode: rst -*-

.. _VIDIOC_LOG_STATUS:

***********************
ioctl VIDIOC_LOG_STATUS
***********************

*man VIDIOC_LOG_STATUS(2)*

Log driver status information


Synopsis
========

.. c:function:: int ioctl( int fd, int request )

Description
===========

As the video/audio devices become more complicated it becomes harder to
debug problems. When this ioctl is called the driver will output the
current device status to the kernel log. This is particular useful when
dealing with problems like no sound, no video and incorrectly tuned
channels. Also many modern devices autodetect video and audio standards
and this ioctl will report what the device thinks what the standard is.
Mismatches may give an indication where the problem is.

This ioctl is optional and not all drivers support it. It was introduced
in Linux 2.6.15.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. ------------------------------------------------------------------------------
.. This file was automatically converted from DocBook-XML with the dbxml
.. library (https://github.com/return42/sphkerneldoc). The origin XML comes
.. from the linux kernel, refer to:
..
.. * https://github.com/torvalds/linux/tree/master/Documentation/DocBook
.. ------------------------------------------------------------------------------
