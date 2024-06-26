.. SPDX-License-Identifier: GPL-2.0

.. _GPIO_V2_LINE_SET_CONFIG_IOCTL:

*****************************
GPIO_V2_LINE_SET_CONFIG_IOCTL
*****************************

Name
====

GPIO_V2_LINE_SET_CONFIG_IOCTL - Update the configuration of previously requested lines.

Synopsis
========

.. c:macro:: GPIO_V2_LINE_SET_CONFIG_IOCTL

``int ioctl(int req_fd, GPIO_V2_LINE_SET_CONFIG_IOCTL, struct gpio_v2_line_config *config)``

Arguments
=========

``req_fd``
    The file descriptor of the GPIO character device, as returned in the
    :c:type:`request.fd<gpio_v2_line_request>` by gpio-v2-get-line-ioctl.rst.

``config``
    The new :c:type:`configuration<gpio_v2_line_config>` to apply to the
    requested lines.

Description
===========

Update the configuration of previously requested lines, without releasing the
line or introducing potential glitches.

The new configuration must specify a configuration for all requested lines.

The same :ref:`gpio-v2-get-line-config-rules` and
:ref:`gpio-v2-get-line-config-support` that apply when requesting the lines
also apply when updating the line configuration, with the additional
restriction that a direction flag must be set to enable reconfiguration.
If no direction flag is set in the configuration for a given line then the
configuration for that line is left unchanged.

The motivating use case for this command is changing direction of
bi-directional lines between input and output, but it may also be used to
dynamically control edge detection, or more generally move lines seamlessly
from one configuration state to another.

To only change the value of output lines, use
gpio-v2-line-set-values-ioctl.rst.

Return Value
============

On success 0.

On error -1 and the ``errno`` variable is set appropriately.
Common error codes are described in error-codes.rst.
