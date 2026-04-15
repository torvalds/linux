.. SPDX-License-Identifier: GPL-2.0

===============
Tracing Remotes
===============

:Author: Vincent Donnefort <vdonnefort@google.com>

Overview
========
Firmware and hypervisors are black boxes to the kernel. Having a way to see what
they are doing can be useful to debug both. This is where remote tracing buffers
come in. A remote tracing buffer is a ring buffer executed by the firmware or
hypervisor into memory that is memory mapped to the host kernel. This is similar
to how user space memory maps the kernel ring buffer but in this case the kernel
is acting like user space and the firmware or hypervisor is the "kernel" side.
With a trace remote ring buffer, the firmware and hypervisor can record events
for which the host kernel can see and expose to user space.

Register a remote
=================
A remote must provide a set of callbacks `struct trace_remote_callbacks` whom
description can be found below. Those callbacks allows Tracefs to enable and
disable tracing and events, to load and unload a tracing buffer (a set of
ring-buffers) and to swap a reader page with the head page, which enables
consuming reading.

.. kernel-doc:: include/linux/trace_remote.h

Once registered, an instance will appear for this remote in the Tracefs
directory **remotes/**. Buffers can then be read using the usual Tracefs files
**trace_pipe** and **trace**.

Declare a remote event
======================
Macros are provided to ease the declaration of remote events, in a similar
fashion to in-kernel events. A declaration must provide an ID, a description of
the event arguments and how to print the event:

.. code-block:: c

	REMOTE_EVENT(foo, EVENT_FOO_ID,
		RE_STRUCT(
			re_field(u64, bar)
		),
		RE_PRINTK("bar=%lld", __entry->bar)
	);

Then those events must be declared in a C file with the following:

.. code-block:: c

	#define REMOTE_EVENT_INCLUDE_FILE foo_events.h
	#include <trace/define_remote_events.h>

This will provide a `struct remote_event remote_event_foo` that can be given to
`trace_remote_register`.

Registered events appear in the remote directory under **events/**.

Simple ring-buffer
==================
A simple implementation for a ring-buffer writer can be found in
kernel/trace/simple_ring_buffer.c.

.. kernel-doc:: include/linux/simple_ring_buffer.h
