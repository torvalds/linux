.. SPDX-License-Identifier: GPL-2.0

.. _GPIO_LINEINFO_CHANGED_READ:

**************************
GPIO_LINEINFO_CHANGED_READ
**************************

.. warning::
    This ioctl is part of chardev_v1.rst and is obsoleted by
    gpio-v2-lineinfo-changed-read.rst.

Name
====

GPIO_LINEINFO_CHANGED_READ - Read line info change events for watched lines
from the chip.

Synopsis
========

``int read(int chip_fd, void *buf, size_t count)``

Arguments
=========

``chip_fd``
    The file descriptor of the GPIO character device returned by `open()`.

``buf``
    The buffer to contain the :c:type:`events<gpioline_info_changed>`.

``count``
    The number of bytes available in ``buf``, which must be at least the size
    of a :c:type:`gpioline_info_changed` event.

Description
===========

Read line info change events for watched lines from the chip.

.. note::
    Monitoring line info changes is not generally required, and would typically
    only be performed by a system monitoring component.

    These events relate to changes in a line's request state or configuration,
    not its value. Use gpio-lineevent-data-read.rst to receive events when a
    line changes value.

A line must be watched using gpio-get-lineinfo-watch-ioctl.rst to generate
info changed events.  Subsequently, a request, release, or reconfiguration
of the line will generate an info changed event.

The kernel timestamps events when they occur and stores them in a buffer
from where they can be read by userspace at its convenience using `read()`.

The size of the kernel event buffer is fixed at 32 events per ``chip_fd``.

The buffer may overflow if bursts of events occur quicker than they are read
by userspace. If an overflow occurs then the most recent event is discarded.
Overflow cannot be detected from userspace.

Events read from the buffer are always in the same order that they were
detected by the kernel, including when multiple lines are being monitored by
the one ``chip_fd``.

To minimize the number of calls required to copy events from the kernel to
userspace, `read()` supports copying multiple events. The number of events
copied is the lower of the number available in the kernel buffer and the
number that will fit in the userspace buffer (``buf``).

A `read()` will block if no event is available and the ``chip_fd`` has not
been set **O_NONBLOCK**.

The presence of an event can be tested for by checking that the ``chip_fd`` is
readable using `poll()` or an equivalent.

First added in 5.7.

Return Value
============

On success the number of bytes read, which will be a multiple of the size of
a :c:type:`gpioline_info_changed` event.

On error -1 and the ``errno`` variable is set appropriately.
Common error codes are described in error-codes.rst.
