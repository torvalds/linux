/*
 * lttng-ring-buffer-client-overwrite.c
 *
 * LTTng lib ring buffer client (overwrite mode).
 *
 * Copyright (C) 2010-2012 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
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

#include <linux/module.h>
#include "lttng-tracer.h"

#define RING_BUFFER_MODE_TEMPLATE		RING_BUFFER_OVERWRITE
#define RING_BUFFER_MODE_TEMPLATE_STRING	"overwrite-mmap"
#define RING_BUFFER_OUTPUT_TEMPLATE		RING_BUFFER_MMAP
#include "lttng-ring-buffer-client.h"

MODULE_LICENSE("GPL and additional rights");
MODULE_AUTHOR("Mathieu Desnoyers");
MODULE_DESCRIPTION("LTTng Ring Buffer Client Overwrite Mode");
