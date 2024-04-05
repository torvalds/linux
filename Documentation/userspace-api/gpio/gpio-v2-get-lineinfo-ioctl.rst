.. SPDX-License-Identifier: GPL-2.0

.. _GPIO_V2_GET_LINEINFO_IOCTL:

**************************
GPIO_V2_GET_LINEINFO_IOCTL
**************************

Name
====

GPIO_V2_GET_LINEINFO_IOCTL - Get the publicly available information for a line.

Synopsis
========

.. c:macro:: GPIO_V2_GET_LINEINFO_IOCTL

``int ioctl(int chip_fd, GPIO_V2_GET_LINEINFO_IOCTL, struct gpio_v2_line_info *info)``

Arguments
=========

``chip_fd``
    The file descriptor of the GPIO character device returned by `open()`.

``info``
    The :c:type:`line_info<gpio_v2_line_info>` to be populated, with the
    ``offset`` field set to indicate the line to be collected.

Description
===========

Get the publicly available information for a line.

This information is available independent of whether the line is in use.

.. note::
    The line info does not include the line value.

    The line must be requested using gpio-v2-get-line-ioctl.rst to access its
    value.

Return Value
============

On success 0 and ``info`` is populated with the chip info.

On error -1 and the ``errno`` variable is set appropriately.
Common error codes are described in error-codes.rst.
