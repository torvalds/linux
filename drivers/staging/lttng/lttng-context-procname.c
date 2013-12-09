/*
 * lttng-context-procname.c
 *
 * LTTng procname context.
 *
 * Copyright (C) 2009-2012 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
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
#include <linux/slab.h>
#include <linux/sched.h>
#include "lttng-events.h"
#include "wrapper/ringbuffer/frontend_types.h"
#include "wrapper/vmalloc.h"
#include "lttng-tracer.h"

static
size_t procname_get_size(size_t offset)
{
	size_t size = 0;

	size += sizeof(current->comm);
	return size;
}

/*
 * Racy read of procname. We simply copy its whole array size.
 * Races with /proc/<task>/procname write only.
 * Otherwise having to take a mutex for each event is cumbersome and
 * could lead to crash in IRQ context and deadlock of the lockdep tracer.
 */
static
void procname_record(struct lttng_ctx_field *field,
		 struct lib_ring_buffer_ctx *ctx,
		 struct lttng_channel *chan)
{
	chan->ops->event_write(ctx, current->comm, sizeof(current->comm));
}

int lttng_add_procname_to_ctx(struct lttng_ctx **ctx)
{
	struct lttng_ctx_field *field;

	field = lttng_append_context(ctx);
	if (!field)
		return -ENOMEM;
	if (lttng_find_context(*ctx, "procname")) {
		lttng_remove_context_field(ctx, field);
		return -EEXIST;
	}
	field->event_field.name = "procname";
	field->event_field.type.atype = atype_array;
	field->event_field.type.u.array.elem_type.atype = atype_integer;
	field->event_field.type.u.array.elem_type.u.basic.integer.size = sizeof(char) * CHAR_BIT;
	field->event_field.type.u.array.elem_type.u.basic.integer.alignment = lttng_alignof(char) * CHAR_BIT;
	field->event_field.type.u.array.elem_type.u.basic.integer.signedness = lttng_is_signed_type(char);
	field->event_field.type.u.array.elem_type.u.basic.integer.reverse_byte_order = 0;
	field->event_field.type.u.array.elem_type.u.basic.integer.base = 10;
	field->event_field.type.u.array.elem_type.u.basic.integer.encoding = lttng_encode_UTF8;
	field->event_field.type.u.array.length = sizeof(current->comm);

	field->get_size = procname_get_size;
	field->record = procname_record;
	wrapper_vmalloc_sync_all();
	return 0;
}
EXPORT_SYMBOL_GPL(lttng_add_procname_to_ctx);

MODULE_LICENSE("GPL and additional rights");
MODULE_AUTHOR("Mathieu Desnoyers");
MODULE_DESCRIPTION("Linux Trace Toolkit Perf Support");
