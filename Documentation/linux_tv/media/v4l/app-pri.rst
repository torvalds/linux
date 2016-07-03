.. -*- coding: utf-8; mode: rst -*-

.. _app-pri:

********************
Application Priority
********************

When multiple applications share a device it may be desirable to assign
them different priorities. Contrary to the traditional "rm -rf /" school
of thought a video recording application could for example block other
applications from changing video controls or switching the current TV
channel. Another objective is to permit low priority applications
working in background, which can be preempted by user controlled
applications and automatically regain control of the device at a later
time.

Since these features cannot be implemented entirely in user space V4L2
defines the :ref:`VIDIOC_G_PRIORITY <VIDIOC_G_PRIORITY>` and
:ref:`VIDIOC_S_PRIORITY <VIDIOC_G_PRIORITY>` ioctls to request and
query the access priority associate with a file descriptor. Opening a
device assigns a medium priority, compatible with earlier versions of
V4L2 and drivers not supporting these ioctls. Applications requiring a
different priority will usually call :ref:`VIDIOC_S_PRIORITY
<VIDIOC_G_PRIORITY>` after verifying the device with the
:ref:`VIDIOC_QUERYCAP` ioctl.

Ioctls changing driver properties, such as
:ref:`VIDIOC_S_INPUT <VIDIOC_G_INPUT>`, return an ``EBUSY`` error code
after another application obtained higher priority.
