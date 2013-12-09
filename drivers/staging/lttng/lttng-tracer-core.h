#ifndef LTTNG_TRACER_CORE_H
#define LTTNG_TRACER_CORE_H

/*
 * lttng-tracer-core.h
 *
 * This contains the core definitions for the Linux Trace Toolkit Next
 * Generation tracer.
 *
 * Copyright (C) 2005-2012 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; only
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <linux/list.h>
#include <linux/percpu.h>

#ifndef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
/* Align data on its natural alignment */
#define RING_BUFFER_ALIGN
#endif

#include "wrapper/ringbuffer/config.h"

struct lttng_session;
struct lttng_channel;
struct lttng_event;

#endif /* LTTNG_TRACER_CORE_H */
