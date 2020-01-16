.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with yes Invariant Sections, yes Front-Cover Texts
.. and yes Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH yes-invariant-sections

.. _VIDIOC_QUERY_DV_TIMINGS:

*****************************
ioctl VIDIOC_QUERY_DV_TIMINGS
*****************************

Name
====

VIDIOC_QUERY_DV_TIMINGS - VIDIOC_SUBDEV_QUERY_DV_TIMINGS - Sense the DV preset received by the current input


Syyespsis
========

.. c:function:: int ioctl( int fd, VIDIOC_QUERY_DV_TIMINGS, struct v4l2_dv_timings *argp )
    :name: VIDIOC_QUERY_DV_TIMINGS

.. c:function:: int ioctl( int fd, VIDIOC_SUBDEV_QUERY_DV_TIMINGS, struct v4l2_dv_timings *argp )
    :name: VIDIOC_SUBDEV_QUERY_DV_TIMINGS


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``argp``
    Pointer to struct :c:type:`v4l2_dv_timings`.


Description
===========

The hardware may be able to detect the current DV timings automatically,
similar to sensing the video standard. To do so, applications call
:ref:`VIDIOC_QUERY_DV_TIMINGS` with a pointer to a struct
:c:type:`v4l2_dv_timings`. Once the hardware detects
the timings, it will fill in the timings structure.

.. yeste::

   Drivers shall *yest* switch timings automatically if new
   timings are detected. Instead, drivers should send the
   ``V4L2_EVENT_SOURCE_CHANGE`` event (if they support this) and expect
   that userspace will take action by calling :ref:`VIDIOC_QUERY_DV_TIMINGS`.
   The reason is that new timings usually mean different buffer sizes as
   well, and you canyest change buffer sizes on the fly. In general,
   applications that receive the Source Change event will have to call
   :ref:`VIDIOC_QUERY_DV_TIMINGS`, and if the detected timings are valid they
   will have to stop streaming, set the new timings, allocate new buffers
   and start streaming again.

If the timings could yest be detected because there was yes signal, then
ENOLINK is returned. If a signal was detected, but it was unstable and
the receiver could yest lock to the signal, then ``ENOLCK`` is returned. If
the receiver could lock to the signal, but the format is unsupported
(e.g. because the pixelclock is out of range of the hardware
capabilities), then the driver fills in whatever timings it could find
and returns ``ERANGE``. In that case the application can call
:ref:`VIDIOC_DV_TIMINGS_CAP` to compare the
found timings with the hardware's capabilities in order to give more
precise feedback to the user.


Return Value
============

On success 0 is returned, on error -1 and the ``erryes`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

ENODATA
    Digital video timings are yest supported for this input or output.

ENOLINK
    No timings could be detected because yes signal was found.

ENOLCK
    The signal was unstable and the hardware could yest lock on to it.

ERANGE
    Timings were found, but they are out of range of the hardware
    capabilities.
