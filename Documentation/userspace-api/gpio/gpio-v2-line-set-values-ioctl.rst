.. SPDX-License-Identifier: GPL-2.0

.. _GPIO_V2_LINE_SET_VALUES_IOCTL:

*****************************
GPIO_V2_LINE_SET_VALUES_IOCTL
*****************************

Name
====

GPIO_V2_LINE_SET_VALUES_IOCTL - Set the values of requested output lines.

Synopsis
========

.. c:macro:: GPIO_V2_LINE_SET_VALUES_IOCTL

``int ioctl(int req_fd, GPIO_V2_LINE_SET_VALUES_IOCTL, struct gpio_v2_line_values *values)``

Arguments
=========

``req_fd``
    The file descriptor of the GPIO character device, as returned in the
    :c:type:`request.fd<gpio_v2_line_request>` by gpio-v2-get-line-ioctl.rst.

``values``
    The :c:type:`line_values<gpio_v2_line_values>` to set with the ``mask`` set
    to indicate the subset of requested lines to set and ``bits`` set to
    indicate the new value.

Description
===========

Set the values of requested output lines.

The values set are logical, indicating if the line is to be active or inactive.
The ``GPIO_V2_LINE_FLAG_ACTIVE_LOW`` flag controls the mapping between logical
values (active/inactive) and physical values (high/low).
If ``GPIO_V2_LINE_FLAG_ACTIVE_LOW`` is not set then active is high and inactive
is low.  If ``GPIO_V2_LINE_FLAG_ACTIVE_LOW`` is set then active is low and
inactive is high.

Only the values of output lines may be set.
Attempting to set the value of an input line is an error (**EPERM**).

Return Value
============

On success 0.

On error -1 and the ``errno`` variable is set appropriately.
Common error codes are described in error-codes.rst.
