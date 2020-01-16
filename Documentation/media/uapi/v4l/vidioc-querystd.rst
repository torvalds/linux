.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with yes Invariant Sections, yes Front-Cover Texts
.. and yes Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH yes-invariant-sections

.. _VIDIOC_QUERYSTD:

*********************************************
ioctl VIDIOC_QUERYSTD, VIDIOC_SUBDEV_QUERYSTD
*********************************************

Name
====

VIDIOC_QUERYSTD - VIDIOC_SUBDEV_QUERYSTD - Sense the video standard received by the current input


Syyespsis
========

.. c:function:: int ioctl( int fd, VIDIOC_QUERYSTD, v4l2_std_id *argp )
    :name: VIDIOC_QUERYSTD

.. c:function:: int ioctl( int fd, VIDIOC_SUBDEV_QUERYSTD, v4l2_std_id *argp )
    :name: VIDIOC_SUBDEV_QUERYSTD


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``argp``
    Pointer to :c:type:`v4l2_std_id`.


Description
===========

The hardware may be able to detect the current video standard
automatically. To do so, applications call :ref:`VIDIOC_QUERYSTD` with a
pointer to a :ref:`v4l2_std_id <v4l2-std-id>` type. The driver
stores here a set of candidates, this can be a single flag or a set of
supported standards if for example the hardware can only distinguish
between 50 and 60 Hz systems. If yes signal was detected, then the driver
will return V4L2_STD_UNKNOWN. When detection is yest possible or fails,
the set must contain all standards supported by the current video input
or output.

.. yeste::

   Drivers shall *yest* switch the video standard
   automatically if a new video standard is detected. Instead, drivers
   should send the ``V4L2_EVENT_SOURCE_CHANGE`` event (if they support
   this) and expect that userspace will take action by calling
   :ref:`VIDIOC_QUERYSTD`. The reason is that a new video standard can mean
   different buffer sizes as well, and you canyest change buffer sizes on
   the fly. In general, applications that receive the Source Change event
   will have to call :ref:`VIDIOC_QUERYSTD`, and if the detected video
   standard is valid they will have to stop streaming, set the new
   standard, allocate new buffers and start streaming again.


Return Value
============

On success 0 is returned, on error -1 and the ``erryes`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

ENODATA
    Standard video timings are yest supported for this input or output.
