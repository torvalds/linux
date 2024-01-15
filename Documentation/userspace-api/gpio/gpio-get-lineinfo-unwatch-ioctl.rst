.. SPDX-License-Identifier: GPL-2.0

.. _GPIO_GET_LINEINFO_UNWATCH_IOCTL:

*******************************
GPIO_GET_LINEINFO_UNWATCH_IOCTL
*******************************

Name
====

GPIO_GET_LINEINFO_UNWATCH_IOCTL - Disable watching a line for changes to its
requested state and configuration information.

Synopsis
========

.. c:macro:: GPIO_GET_LINEINFO_UNWATCH_IOCTL

``int ioctl(int chip_fd, GPIO_GET_LINEINFO_UNWATCH_IOCTL, u32 *offset)``

Arguments
=========

``chip_fd``
    The file descriptor of the GPIO character device returned by `open()`.

``offset``
    The offset of the line to no longer watch.

Description
===========

Remove the line from the list of lines being watched on this ``chip_fd``.

This is the reverse of gpio-v2-get-lineinfo-watch-ioctl.rst (v2) and
gpio-get-lineinfo-watch-ioctl.rst (v1).

Unwatching a line that is not watched is an error (**EBUSY**).

First added in 5.7.

Return Value
============

On success 0.

On error -1 and the ``errno`` variable is set appropriately.
Common error codes are described in error-codes.rst.
