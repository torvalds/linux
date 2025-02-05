.. SPDX-License-Identifier: GPL-2.0

.. _GPIO_V2_LINE_EVENT_READ:

***********************
GPIO_V2_LINE_EVENT_READ
***********************

Name
====

GPIO_V2_LINE_EVENT_READ - Read edge detection events for lines from a request.

Synopsis
========

``int read(int req_fd, void *buf, size_t count)``

Arguments
=========

``req_fd``
    The file descriptor of the GPIO character device, as returned in the
    :c:type:`request.fd<gpio_v2_line_request>` by gpio-v2-get-line-ioctl.rst.

``buf``
    The buffer to contain the :c:type:`events<gpio_v2_line_event>`.

``count``
    The number of bytes available in ``buf``, which must be at
    least the size of a :c:type:`gpio_v2_line_event`.

Description
===========

Read edge detection events for lines from a request.

Edge detection must be enabled for the input line using either
``GPIO_V2_LINE_FLAG_EDGE_RISING`` or ``GPIO_V2_LINE_FLAG_EDGE_FALLING``, or
both. Edge events are then generated whenever edge interrupts are detected on
the input line.

Edges are defined in terms of changes to the logical line value, so an inactive
to active transition is a rising edge.  If ``GPIO_V2_LINE_FLAG_ACTIVE_LOW`` is
set then logical polarity is the opposite of physical polarity, and
``GPIO_V2_LINE_FLAG_EDGE_RISING`` then corresponds to a falling physical edge.

The kernel captures and timestamps edge events as close as possible to their
occurrence and stores them in a buffer from where they can be read by
userspace at its convenience using `read()`.

Events read from the buffer are always in the same order that they were
detected by the kernel, including when multiple lines are being monitored by
the one request.

The size of the kernel event buffer is fixed at the time of line request
creation, and can be influenced by the
:c:type:`request.event_buffer_size<gpio_v2_line_request>`.
The default size is 16 times the number of lines requested.

The buffer may overflow if bursts of events occur quicker than they are read
by userspace. If an overflow occurs then the oldest buffered event is
discarded. Overflow can be detected from userspace by monitoring the event
sequence numbers.

To minimize the number of calls required to copy events from the kernel to
userspace, `read()` supports copying multiple events. The number of events
copied is the lower of the number available in the kernel buffer and the
number that will fit in the userspace buffer (``buf``).

Changing the edge detection flags using gpio-v2-line-set-config-ioctl.rst
does not remove or modify the events already contained in the kernel event
buffer.

The `read()` will block if no event is available and the ``req_fd`` has not
been set **O_NONBLOCK**.

The presence of an event can be tested for by checking that the ``req_fd`` is
readable using `poll()` or an equivalent.

Return Value
============

On success the number of bytes read, which will be a multiple of the size of a
:c:type:`gpio_v2_line_event` event.

On error -1 and the ``errno`` variable is set appropriately.
Common error codes are described in error-codes.rst.
