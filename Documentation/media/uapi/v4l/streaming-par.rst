.. -*- coding: utf-8; mode: rst -*-

.. _streaming-par:

********************
Streaming Parameters
********************

Streaming parameters are intended to optimize the video capture process
as well as I/O. Presently applications can request a high quality
capture mode with the :ref:`VIDIOC_S_PARM <VIDIOC_G_PARM>` ioctl.

The current video standard determines a nominal number of frames per
second. If less than this number of frames is to be captured or output,
applications can request frame skipping or duplicating on the driver
side. This is especially useful when using the
:ref:`read() <func-read>` or :ref:`write() <func-write>`, which are
not augmented by timestamps or sequence counters, and to avoid
unnecessary data copying.

Finally these ioctls can be used to determine the number of buffers used
internally by a driver in read/write mode. For implications see the
section discussing the :ref:`read() <func-read>` function.

To get and set the streaming parameters applications call the
:ref:`VIDIOC_G_PARM <VIDIOC_G_PARM>` and
:ref:`VIDIOC_S_PARM <VIDIOC_G_PARM>` ioctl, respectively. They take
a pointer to a struct :ref:`v4l2_streamparm <v4l2-streamparm>`, which
contains a union holding separate parameters for input and output
devices.

These ioctls are optional, drivers need not implement them. If so, they
return the ``EINVAL`` error code.
