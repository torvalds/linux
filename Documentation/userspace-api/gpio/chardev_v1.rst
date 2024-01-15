.. SPDX-License-Identifier: GPL-2.0

========================================
GPIO Character Device Userspace API (v1)
========================================

.. warning::
   This API is obsoleted by chardev.rst (v2).

   New developments should use the v2 API, and existing developments are
   encouraged to migrate as soon as possible, as this API will be removed
   in the future. The v2 API is a functional superset of the v1 API so any
   v1 call can be directly translated to a v2 equivalent.

   This interface will continue to be maintained for the migration period,
   but new features will only be added to the new API.

First added in 4.8.

The API is based around three major objects, the :ref:`gpio-v1-chip`, the
:ref:`gpio-v1-line-handle`, and the :ref:`gpio-v1-line-event`.

Where "line event" is used in this document it refers to the request that can
monitor a line for edge events, not the edge events themselves.

.. _gpio-v1-chip:

Chip
====

The Chip represents a single GPIO chip and is exposed to userspace using device
files of the form ``/dev/gpiochipX``.

Each chip supports a number of GPIO lines,
:c:type:`chip.lines<gpiochip_info>`. Lines on the chip are identified by an
``offset`` in the range from 0 to ``chip.lines - 1``, i.e. `[0,chip.lines)`.

Lines are requested from the chip using either gpio-get-linehandle-ioctl.rst
and the resulting line handle is used to access the GPIO chip's lines, or
gpio-get-lineevent-ioctl.rst and the resulting line event is used to monitor
a GPIO line for edge events.

Within this documentation, the file descriptor returned by calling `open()`
on the GPIO device file is referred to as ``chip_fd``.

Operations
----------

The following operations may be performed on the chip:

.. toctree::
   :titlesonly:

   Get Line Handle <gpio-get-linehandle-ioctl>
   Get Line Event <gpio-get-lineevent-ioctl>
   Get Chip Info <gpio-get-chipinfo-ioctl>
   Get Line Info <gpio-get-lineinfo-ioctl>
   Watch Line Info <gpio-get-lineinfo-watch-ioctl>
   Unwatch Line Info <gpio-get-lineinfo-unwatch-ioctl>
   Read Line Info Changed Events <gpio-lineinfo-changed-read>

.. _gpio-v1-line-handle:

Line Handle
===========

Line handles are created by gpio-get-linehandle-ioctl.rst and provide
access to a set of requested lines.  The line handle is exposed to userspace
via the anonymous file descriptor returned  in
:c:type:`request.fd<gpiohandle_request>` by gpio-get-linehandle-ioctl.rst.

Within this documentation, the line handle file descriptor is referred to
as ``handle_fd``.

Operations
----------

The following operations may be performed on the line handle:

.. toctree::
   :titlesonly:

   Get Line Values <gpio-handle-get-line-values-ioctl>
   Set Line Values <gpio-handle-set-line-values-ioctl>
   Reconfigure Lines <gpio-handle-set-config-ioctl>

.. _gpio-v1-line-event:

Line Event
==========

Line events are created by gpio-get-lineevent-ioctl.rst and provide
access to a requested line.  The line event is exposed to userspace
via the anonymous file descriptor returned  in
:c:type:`request.fd<gpioevent_request>` by gpio-get-lineevent-ioctl.rst.

Within this documentation, the line event file descriptor is referred to
as ``event_fd``.

Operations
----------

The following operations may be performed on the line event:

.. toctree::
   :titlesonly:

   Get Line Value <gpio-handle-get-line-values-ioctl>
   Read Line Edge Events <gpio-lineevent-data-read>

Types
=====

This section contains the structs that are referenced by the ABI v1.

The :c:type:`struct gpiochip_info<gpiochip_info>` is common to ABI v1 and v2.

.. kernel-doc:: include/uapi/linux/gpio.h
   :identifiers:
    gpioevent_data
    gpioevent_request
    gpiohandle_config
    gpiohandle_data
    gpiohandle_request
    gpioline_info
    gpioline_info_changed

.. toctree::
   :hidden:

   error-codes
