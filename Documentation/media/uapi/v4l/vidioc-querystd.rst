.. -*- coding: utf-8; mode: rst -*-

.. _VIDIOC_QUERYSTD:

*********************
ioctl VIDIOC_QUERYSTD
*********************

Name
====

VIDIOC_QUERYSTD - Sense the video standard received by the current input


Synopsis
========

.. c:function:: int ioctl( int fd, VIDIOC_QUERYSTD, v4l2_std_id *argp )
    :name: VIDIOC_QUERYSTD


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``argp``


Description
===========

The hardware may be able to detect the current video standard
automatically. To do so, applications call :ref:`VIDIOC_QUERYSTD` with a
pointer to a :ref:`v4l2_std_id <v4l2-std-id>` type. The driver
stores here a set of candidates, this can be a single flag or a set of
supported standards if for example the hardware can only distinguish
between 50 and 60 Hz systems. If no signal was detected, then the driver
will return V4L2_STD_UNKNOWN. When detection is not possible or fails,
the set must contain all standards supported by the current video input
or output.

.. note::

   Drivers shall *not* switch the video standard
   automatically if a new video standard is detected. Instead, drivers
   should send the ``V4L2_EVENT_SOURCE_CHANGE`` event (if they support
   this) and expect that userspace will take action by calling
   :ref:`VIDIOC_QUERYSTD`. The reason is that a new video standard can mean
   different buffer sizes as well, and you cannot change buffer sizes on
   the fly. In general, applications that receive the Source Change event
   will have to call :ref:`VIDIOC_QUERYSTD`, and if the detected video
   standard is valid they will have to stop streaming, set the new
   standard, allocate new buffers and start streaming again.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

ENODATA
    Standard video timings are not supported for this input or output.
