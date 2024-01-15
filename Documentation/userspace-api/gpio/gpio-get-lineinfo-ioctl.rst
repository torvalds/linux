.. SPDX-License-Identifier: GPL-2.0

.. _GPIO_GET_LINEINFO_IOCTL:

***********************
GPIO_GET_LINEINFO_IOCTL
***********************

.. warning::
    This ioctl is part of chardev_v1.rst and is obsoleted by
    gpio-v2-get-lineinfo-ioctl.rst.

Name
====

GPIO_GET_LINEINFO_IOCTL - Get the publicly available information for a line.

Synopsis
========

.. c:macro:: GPIO_GET_LINEINFO_IOCTL

``int ioctl(int chip_fd, GPIO_GET_LINEINFO_IOCTL, struct gpioline_info *info)``

Arguments
=========

``chip_fd``
    The file descriptor of the GPIO character device returned by `open()`.

``info``
    The :c:type:`line_info<gpioline_info>` to be populated, with the
    ``offset`` field set to indicate the line to be collected.

Description
===========

Get the publicly available information for a line.

This information is available independent of whether the line is in use.

.. note::
    The line info does not include the line value.

    The line must be requested using gpio-get-linehandle-ioctl.rst or
    gpio-get-lineevent-ioctl.rst to access its value.

Return Value
============

On success 0 and ``info`` is populated with the chip info.

On error -1 and the ``errno`` variable is set appropriately.
Common error codes are described in error-codes.rst.
