.. -*- coding: utf-8; mode: rst -*-

.. _touch:

*************
Touch Devices
*************

Touch devices are accessed through character device special files named
``/dev/v4l-touch0`` to ``/dev/v4l-touch255`` with major number 81 and
dynamically allocated minor numbers 0 to 255.

Overview
========

Sensors may be Optical, or Projected Capacitive touch (PCT).

Processing is required to analyse the raw data and produce input events. In
some systems, this may be performed on the ASIC and the raw data is purely a
side-channel for diagnostics or tuning. In other systems, the ASIC is a simple
analogue front end device which delivers touch data at high rate, and any touch
processing must be done on the host.

For capacitive touch sensing, the touchscreen is composed of an array of
horizontal and vertical conductors (alternatively called rows/columns, X/Y
lines, or tx/rx). Mutual Capacitance measured is at the nodes where the
conductors cross. Alternatively, Self Capacitance measures the signal from each
column and row independently.

A touch input may be determined by comparing the raw capacitance measurement to
a no-touch reference (or "baseline") measurement:

Delta = Raw - Reference

The reference measurement takes account of variations in the capacitance across
the touch sensor matrix, for example manufacturing irregularities,
environmental or edge effects.

Querying Capabilities
=====================

Devices supporting the touch interface set the ``V4L2_CAP_VIDEO_CAPTURE`` flag
and the ``V4L2_CAP_TOUCH`` flag in the ``capabilities`` field of
:c:type:`v4l2_capability` returned by the
:ref:`VIDIOC_QUERYCAP` ioctl.

At least one of the read/write or streaming I/O methods must be
supported.

The formats supported by touch devices are documented in
:ref:`Touch Formats <tch-formats>`.

Data Format Negotiation
=======================

A touch device may support any I/O method.
