.. -*- coding: utf-8; mode: rst -*-

.. _VIDIOC_QUERY_DV_TIMINGS:

*****************************
ioctl VIDIOC_QUERY_DV_TIMINGS
*****************************

Name
====

VIDIOC_QUERY_DV_TIMINGS - VIDIOC_SUBDEV_QUERY_DV_TIMINGS - Sense the DV preset received by the current input


Synopsis
========

.. cpp:function:: int ioctl( int fd, int request, struct v4l2_dv_timings *argp )


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``request``
    VIDIOC_QUERY_DV_TIMINGS, VIDIOC_SUBDEV_QUERY_DV_TIMINGS

``argp``


Description
===========

The hardware may be able to detect the current DV timings automatically,
similar to sensing the video standard. To do so, applications call
:ref:`VIDIOC_QUERY_DV_TIMINGS` with a pointer to a struct
:ref:`v4l2_dv_timings <v4l2-dv-timings>`. Once the hardware detects
the timings, it will fill in the timings structure.

.. note:: Drivers shall *not* switch timings automatically if new
   timings are detected. Instead, drivers should send the
   ``V4L2_EVENT_SOURCE_CHANGE`` event (if they support this) and expect
   that userspace will take action by calling :ref:`VIDIOC_QUERY_DV_TIMINGS`.
   The reason is that new timings usually mean different buffer sizes as
   well, and you cannot change buffer sizes on the fly. In general,
   applications that receive the Source Change event will have to call
   :ref:`VIDIOC_QUERY_DV_TIMINGS`, and if the detected timings are valid they
   will have to stop streaming, set the new timings, allocate new buffers
   and start streaming again.

If the timings could not be detected because there was no signal, then
ENOLINK is returned. If a signal was detected, but it was unstable and
the receiver could not lock to the signal, then ``ENOLCK`` is returned. If
the receiver could lock to the signal, but the format is unsupported
(e.g. because the pixelclock is out of range of the hardware
capabilities), then the driver fills in whatever timings it could find
and returns ``ERANGE``. In that case the application can call
:ref:`VIDIOC_DV_TIMINGS_CAP` to compare the
found timings with the hardware's capabilities in order to give more
precise feedback to the user.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

ENODATA
    Digital video timings are not supported for this input or output.

ENOLINK
    No timings could be detected because no signal was found.

ENOLCK
    The signal was unstable and the hardware could not lock on to it.

ERANGE
    Timings were found, but they are out of range of the hardware
    capabilities.
