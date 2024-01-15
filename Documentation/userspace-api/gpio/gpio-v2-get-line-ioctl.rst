.. SPDX-License-Identifier: GPL-2.0

.. _GPIO_V2_GET_LINE_IOCTL:

**********************
GPIO_V2_GET_LINE_IOCTL
**********************

Name
====

GPIO_V2_GET_LINE_IOCTL - Request a line or lines from the kernel.

Synopsis
========

.. c:macro:: GPIO_V2_GET_LINE_IOCTL

``int ioctl(int chip_fd, GPIO_V2_GET_LINE_IOCTL, struct gpio_v2_line_request *request)``

Arguments
=========

``chip_fd``
    The file descriptor of the GPIO character device returned by `open()`.

``request``
    The :c:type:`line_request<gpio_v2_line_request>` specifying the lines
    to request and their configuration.

Description
===========

On success, the requesting process is granted exclusive access to the line
value, write access to the line configuration, and may receive events when
edges are detected on the line, all of which are described in more detail in
:ref:`gpio-v2-line-request`.

A number of lines may be requested in the one line request, and request
operations are performed on the requested lines by the kernel as atomically
as possible. e.g. gpio-v2-line-get-values-ioctl.rst will read all the
requested lines at once.

The state of a line, including the value of output lines, is guaranteed to
remain as requested until the returned file descriptor is closed. Once the
file descriptor is closed, the state of the line becomes uncontrolled from
the userspace perspective, and may revert to its default state.

Requesting a line already in use is an error (**EBUSY**).

Closing the ``chip_fd`` has no effect on existing line requests.

.. _gpio-v2-get-line-config-rules:

Configuration Rules
-------------------

For any given requested line, the following configuration rules apply:

The direction flags, ``GPIO_V2_LINE_FLAG_INPUT`` and
``GPIO_V2_LINE_FLAG_OUTPUT``, cannot be combined. If neither are set then
the only other flag that may be set is ``GPIO_V2_LINE_FLAG_ACTIVE_LOW``
and the line is requested "as-is" to allow reading of the line value
without altering the electrical configuration.

The drive flags, ``GPIO_V2_LINE_FLAG_OPEN_xxx``, require the
``GPIO_V2_LINE_FLAG_OUTPUT`` to be set.
Only one drive flag may be set.
If none are set then the line is assumed push-pull.

Only one bias flag, ``GPIO_V2_LINE_FLAG_BIAS_xxx``, may be set, and it
requires a direction flag to also be set.
If no bias flags are set then the bias configuration is not changed.

The edge flags, ``GPIO_V2_LINE_FLAG_EDGE_xxx``, require
``GPIO_V2_LINE_FLAG_INPUT`` to be set and may be combined to detect both rising
and falling edges.

Only one event clock flag, ``GPIO_V2_LINE_FLAG_EVENT_CLOCK_xxx``, may be set.
If none are set then the event clock defaults to ``CLOCK_MONOTONIC``.
The ``GPIO_V2_LINE_FLAG_EVENT_CLOCK_HTE`` flag requires supporting hardware
and a kernel with ``CONFIG_HTE`` set.  Requesting HTE from a device that
doesn't support it is an error (**EOPNOTSUP**).

The :c:type:`debounce_period_us<gpio_v2_line_attribute>` attribute may only
be applied to lines with ``GPIO_V2_LINE_FLAG_INPUT`` set. When set, debounce
applies to both the values returned by gpio-v2-line-get-values-ioctl.rst and
the edges returned by gpio-v2-line-event-read.rst.  If not
supported directly by hardware, the debouncing is performed in software by the
kernel.

Requesting an invalid configuration is an error (**EINVAL**).

Return Value
============

On success 0 and the :c:type:`request.fd<gpio_v2_line_request>` contains the
file descriptor for the request.

On error -1 and the ``errno`` variable is set appropriately.
Common error codes are described in error-codes.rst.
