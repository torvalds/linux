.. SPDX-License-Identifier: GPL-2.0

.. _GPIO_GET_CHIPINFO_IOCTL:

***********************
GPIO_GET_CHIPINFO_IOCTL
***********************

Name
====

GPIO_GET_CHIPINFO_IOCTL - Get the publicly available information for a chip.

Synopsis
========

.. c:macro:: GPIO_GET_CHIPINFO_IOCTL

``int ioctl(int chip_fd, GPIO_GET_CHIPINFO_IOCTL, struct gpiochip_info *info)``

Arguments
=========

``chip_fd``
    The file descriptor of the GPIO character device returned by `open()`.

``info``
    The :c:type:`chip_info<gpiochip_info>` to be populated.

Description
===========

Gets the publicly available information for a particular GPIO chip.

Return Value
============

On success 0 and ``info`` is populated with the chip info.

On error -1 and the ``errno`` variable is set appropriately.
Common error codes are described in error-codes.rst.
