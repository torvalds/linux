.. SPDX-License-Identifier: GPL-2.0

.. _GPIOHANDLE_SET_CONFIG_IOCTL:

***************************
GPIOHANDLE_SET_CONFIG_IOCTL
***************************

.. warning::
    This ioctl is part of chardev_v1.rst and is obsoleted by
    gpio-v2-line-set-config-ioctl.rst.

Name
====

GPIOHANDLE_SET_CONFIG_IOCTL - Update the configuration of previously requested lines.

Synopsis
========

.. c:macro:: GPIOHANDLE_SET_CONFIG_IOCTL

``int ioctl(int handle_fd, GPIOHANDLE_SET_CONFIG_IOCTL, struct gpiohandle_config *config)``

Arguments
=========

``handle_fd``
    The file descriptor of the GPIO character device, as returned in the
    :c:type:`request.fd<gpiohandle_request>` by gpio-get-linehandle-ioctl.rst.

``config``
    The new :c:type:`configuration<gpiohandle_config>` to apply to the
    requested lines.

Description
===========

Update the configuration of previously requested lines, without releasing the
line or introducing potential glitches.

The configuration applies to all requested lines.

The same :ref:`gpio-get-linehandle-config-rules` and
:ref:`gpio-get-linehandle-config-support` that apply when requesting the
lines also apply when updating the line configuration, with the additional
restriction that a direction flag must be set. Requesting an invalid
configuration, including without a direction flag set, is an error
(**EINVAL**).

The motivating use case for this command is changing direction of
bi-directional lines between input and output, but it may be used more
generally to move lines seamlessly from one configuration state to another.

To only change the value of output lines, use
gpio-handle-set-line-values-ioctl.rst.

First added in 5.5.

Return Value
============

On success 0.

On error -1 and the ``errno`` variable is set appropriately.
Common error codes are described in error-codes.rst.
