/*
 * lttng-context-ppid.c
 *
 * LTTng PPID context.
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
#include <linux/syscalls.h>
#include "lttng-events.h"
#include "wrapper/ringbuffer/frontend_types.h"
#include "wrapper/vmalloc.h"
#include "lttng-tracer.h"

static
size_t ppid_get_size(size_t offset)
{
	size_t size = 0;

	size += lib_ring_buffer_align(offset, lttng_alignof(pid_t));
	size += sizeof(pid_t);
	return size;
}

static
void ppid_record(struct lttng_ctx_field *field,
		 struct lib_ring_buffer_ctx *ctx,
		 struct lttng_channel *chan)
{
	pid_t ppid;


	/*
	 * TODO: when we eventually add RCU subsystem instrumentation,
	 * taking the rcu read lock here will trigger RCU tracing
	 * recursively. We should modify the kernel synchronization so
	 * it synchronizes both for RCU and RCU sched, and rely on
	 * rcu_read_lock_sched_notrace.
	 */

	rcu_read_lock();
	ppid = task_tgid_nr(current->real_parent);
	rcu_read_unlock();
	lib_ring_buffer_align_ctx(ctx, lttng_alignof(ppid));
	chan->ops->event_write(ctx, &ppid, sizeof(ppid));
}

int lttng_add_ppid_to_ctx(struct lttng_ctx **ctx)
{
	struct lttng_ctx_field *field;

	field = lttng_append_context(ctx);
	if (!field)
		return -ENOMEM;
	if (lttng_find_context(*ctx, "ppid")) {
		lttng_remove_context_field(ctx, field);
		return -EEXIST;
	}
	field->event_field.name = "ppid";
	field->event_field.type.atype = atype_integer;
	field->event_field.type.u.basic.integer.size = sizeof(pid_t) * CHAR_BIT;
	field->event_field.type.u.basic.integer.alignment = lttng_alignof(pid_t) * CHAR_BIT;
	field->event_field.type.u.basic.integer.signedness = lttng_is_signed_type(pid_t);
	field->event_field.type.u.basic.integer.reverse_byte_order = 0;
	field->event_field.type.u.basic.integer.base = 10;
	field->event_field.type.u.basic.integer.encoding = lttng_encode_none;
	field->get_size = ppid_get_size;
	field->record = ppid_record;
	wrapper_vmalloc_sync_all();
	return 0;
}
EXPORT_SYMBOL_GPL(lttng_add_ppid_to_ctx);

MODULE_LICENSE("GPL and additional rights");
MODULE_AUTHOR("Mathieu Desnoyers");
MODULE_DESCRIPTION("Linux Trace Toolkit PPID Context");
