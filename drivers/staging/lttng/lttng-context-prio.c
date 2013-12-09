/*
 * lttng-context-prio.c
 *
 * LTTng priority context.
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
#include "wrapper/kallsyms.h"
#include "lttng-tracer.h"

static
int (*wrapper_task_prio_sym)(struct task_struct *t);

int wrapper_task_prio_init(void)
{
	wrapper_task_prio_sym = (void *) kallsyms_lookup_funcptr("task_prio");
	if (!wrapper_task_prio_sym) {
		printk(KERN_WARNING "LTTng: task_prio symbol lookup failed.\n");
		return -EINVAL;
	}
	return 0;
}

static
size_t prio_get_size(size_t offset)
{
	size_t size = 0;

	size += lib_ring_buffer_align(offset, lttng_alignof(int));
	size += sizeof(int);
	return size;
}

static
void prio_record(struct lttng_ctx_field *field,
		struct lib_ring_buffer_ctx *ctx,
		struct lttng_channel *chan)
{
	int prio;

	prio = wrapper_task_prio_sym(current);
	lib_ring_buffer_align_ctx(ctx, lttng_alignof(prio));
	chan->ops->event_write(ctx, &prio, sizeof(prio));
}

int lttng_add_prio_to_ctx(struct lttng_ctx **ctx)
{
	struct lttng_ctx_field *field;
	int ret;

	if (!wrapper_task_prio_sym) {
		ret = wrapper_task_prio_init();
		if (ret)
			return ret;
	}

	field = lttng_append_context(ctx);
	if (!field)
		return -ENOMEM;
	if (lttng_find_context(*ctx, "prio")) {
		lttng_remove_context_field(ctx, field);
		return -EEXIST;
	}
	field->event_field.name = "prio";
	field->event_field.type.atype = atype_integer;
	field->event_field.type.u.basic.integer.size = sizeof(int) * CHAR_BIT;
	field->event_field.type.u.basic.integer.alignment = lttng_alignof(int) * CHAR_BIT;
	field->event_field.type.u.basic.integer.signedness = lttng_is_signed_type(int);
	field->event_field.type.u.basic.integer.reverse_byte_order = 0;
	field->event_field.type.u.basic.integer.base = 10;
	field->event_field.type.u.basic.integer.encoding = lttng_encode_none;
	field->get_size = prio_get_size;
	field->record = prio_record;
	wrapper_vmalloc_sync_all();
	return 0;
}
EXPORT_SYMBOL_GPL(lttng_add_prio_to_ctx);

MODULE_LICENSE("GPL and additional rights");
MODULE_AUTHOR("Mathieu Desnoyers");
MODULE_DESCRIPTION("Linux Trace Toolkit Priority Context");
