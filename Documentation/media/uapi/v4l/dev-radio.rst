.. -*- coding: utf-8; mode: rst -*-

.. _radio:

***************
Radio Interface
***************

This interface is intended for AM and FM (analog) radio receivers and
transmitters.

Conventionally V4L2 radio devices are accessed through character device
special files named ``/dev/radio`` and ``/dev/radio0`` to
``/dev/radio63`` with major number 81 and minor numbers 64 to 127.


Querying Capabilities
=====================

Devices supporting the radio interface set the ``V4L2_CAP_RADIO`` and
``V4L2_CAP_TUNER`` or ``V4L2_CAP_MODULATOR`` flag in the
``capabilities`` field of struct
:c:type:`v4l2_capability` returned by the
:ref:`VIDIOC_QUERYCAP` ioctl. Other combinations of
capability flags are reserved for future extensions.


Supplemental Functions
======================

Radio devices can support :ref:`controls <control>`, and must support
the :ref:`tuner or modulator <tuner>` ioctls.

They do not support the video input or output, audio input or output,
video standard, cropping and scaling, compression and streaming
parameter, or overlay ioctls. All other ioctls and I/O methods are
reserved for future extensions.


Programming
===========

Radio devices may have a couple audio controls (as discussed in
:ref:`control`) such as a volume control, possibly custom controls.
Further all radio devices have one tuner or modulator (these are
discussed in :ref:`tuner`) with index number zero to select the radio
frequency and to determine if a monaural or FM stereo program is
received/emitted. Drivers switch automatically between AM and FM
depending on the selected frequency. The
:ref:`VIDIOC_G_TUNER <VIDIOC_G_TUNER>` or
:ref:`VIDIOC_G_MODULATOR <VIDIOC_G_MODULATOR>` ioctl reports the
supported frequency range.
