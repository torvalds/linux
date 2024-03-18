.. SPDX-License-Identifier: GPL-2.0

===================================
GPIO Character Device Userspace API
===================================

This is latest version (v2) of the character device API, as defined in
``include/uapi/linux/gpio.h.``

First added in 5.10.

.. note::
   Do NOT abuse userspace APIs to control hardware that has proper kernel
   drivers. There may already be a driver for your use case, and an existing
   kernel driver is sure to provide a superior solution to bitbashing
   from userspace.

   Read Documentation/driver-api/gpio/drivers-on-gpio.rst to avoid reinventing
   kernel wheels in userspace.

   Similarly, for multi-function lines there may be other subsystems, such as
   Documentation/spi/index.rst, Documentation/i2c/index.rst,
   Documentation/driver-api/pwm.rst, Documentation/w1/index.rst etc, that
   provide suitable drivers and APIs for your hardware.

Basic examples using the character device API can be found in ``tools/gpio/*``.

The API is based around two major objects, the :ref:`gpio-v2-chip` and the
:ref:`gpio-v2-line-request`.

.. _gpio-v2-chip:

Chip
====

The Chip represents a single GPIO chip and is exposed to userspace using device
files of the form ``/dev/gpiochipX``.

Each chip supports a number of GPIO lines,
:c:type:`chip.lines<gpiochip_info>`. Lines on the chip are identified by an
``offset`` in the range from 0 to ``chip.lines - 1``, i.e. `[0,chip.lines)`.

Lines are requested from the chip using gpio-v2-get-line-ioctl.rst
and the resulting line request is used to access the GPIO chip's lines or
monitor the lines for edge events.

Within this documentation, the file descriptor returned by calling `open()`
on the GPIO device file is referred to as ``chip_fd``.

Operations
----------

The following operations may be performed on the chip:

.. toctree::
   :titlesonly:

   Get Line <gpio-v2-get-line-ioctl>
   Get Chip Info <gpio-get-chipinfo-ioctl>
   Get Line Info <gpio-v2-get-lineinfo-ioctl>
   Watch Line Info <gpio-v2-get-lineinfo-watch-ioctl>
   Unwatch Line Info <gpio-get-lineinfo-unwatch-ioctl>
   Read Line Info Changed Events <gpio-v2-lineinfo-changed-read>

.. _gpio-v2-line-request:

Line Request
============

Line requests are created by gpio-v2-get-line-ioctl.rst and provide
access to a set of requested lines.  The line request is exposed to userspace
via the anonymous file descriptor returned in
:c:type:`request.fd<gpio_v2_line_request>` by gpio-v2-get-line-ioctl.rst.

Within this documentation, the line request file descriptor is referred to
as ``req_fd``.

Operations
----------

The following operations may be performed on the line request:

.. toctree::
   :titlesonly:

   Get Line Values <gpio-v2-line-get-values-ioctl>
   Set Line Values <gpio-v2-line-set-values-ioctl>
   Read Line Edge Events <gpio-v2-line-event-read>
   Reconfigure Lines <gpio-v2-line-set-config-ioctl>

Types
=====

This section contains the structs and enums that are referenced by the API v2,
as defined in ``include/uapi/linux/gpio.h``.

.. kernel-doc:: include/uapi/linux/gpio.h
   :identifiers:
    gpio_v2_line_attr_id
    gpio_v2_line_attribute
    gpio_v2_line_changed_type
    gpio_v2_line_config
    gpio_v2_line_config_attribute
    gpio_v2_line_event
    gpio_v2_line_event_id
    gpio_v2_line_flag
    gpio_v2_line_info
    gpio_v2_line_info_changed
    gpio_v2_line_request
    gpio_v2_line_values
    gpiochip_info

.. toctree::
   :hidden:

   error-codes
