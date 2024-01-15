.. SPDX-License-Identifier: GPL-2.0

.. _GPIO_GET_LINEHANDLE_IOCTL:

*************************
GPIO_GET_LINEHANDLE_IOCTL
*************************

.. warning::
    This ioctl is part of chardev_v1.rst and is obsoleted by
    gpio-v2-get-line-ioctl.rst.

Name
====

GPIO_GET_LINEHANDLE_IOCTL - Request a line or lines from the kernel.

Synopsis
========

.. c:macro:: GPIO_GET_LINEHANDLE_IOCTL

``int ioctl(int chip_fd, GPIO_GET_LINEHANDLE_IOCTL, struct gpiohandle_request *request)``

Arguments
=========

``chip_fd``
    The file descriptor of the GPIO character device returned by `open()`.

``request``
    The :c:type:`handle_request<gpiohandle_request>` specifying the lines to
    request and their configuration.

Description
===========

Request a line or lines from the kernel.

While multiple lines may be requested, the same configuration applies to all
lines in the request.

On success, the requesting process is granted exclusive access to the line
value and write access to the line configuration.

The state of a line, including the value of output lines, is guaranteed to
remain as requested until the returned file descriptor is closed. Once the
file descriptor is closed, the state of the line becomes uncontrolled from
the userspace perspective, and may revert to its default state.

Requesting a line already in use is an error (**EBUSY**).

Closing the ``chip_fd`` has no effect on existing line handles.

.. _gpio-get-linehandle-config-rules:

Configuration Rules
-------------------

The following configuration rules apply:

The direction flags, ``GPIOHANDLE_REQUEST_INPUT`` and
``GPIOHANDLE_REQUEST_OUTPUT``, cannot be combined. If neither are set then the
only other flag that may be set is ``GPIOHANDLE_REQUEST_ACTIVE_LOW`` and the
line is requested "as-is" to allow reading of the line value without altering
the electrical configuration.

The drive flags, ``GPIOHANDLE_REQUEST_OPEN_xxx``, require the
``GPIOHANDLE_REQUEST_OUTPUT`` to be set.
Only one drive flag may be set.
If none are set then the line is assumed push-pull.

Only one bias flag, ``GPIOHANDLE_REQUEST_BIAS_xxx``, may be set, and
it requires a direction flag to also be set.
If no bias flags are set then the bias configuration is not changed.

Requesting an invalid configuration is an error (**EINVAL**).

Return Value
============

On success 0 and the :c:type:`request.fd<gpiohandle_request>` contains the
file descriptor for the request.

On error -1 and the ``errno`` variable is set appropriately.
Common error codes are described in error-codes.rst.
