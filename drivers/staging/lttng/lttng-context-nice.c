/*
 * (C) Copyright	2009-2011 -
 * 		Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * LTTng nice context.
 *
 * Dual LGPL v2.1/GPL v2 license.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include "ltt-events.h"
#include "wrapper/ringbuffer/frontend_types.h"
#include "wrapper/vmalloc.h"
#include "ltt-tracer.h"

static
size_t nice_get_size(size_t offset)
{
	size_t size = 0;

	size += lib_ring_buffer_align(offset, ltt_alignof(int));
	size += sizeof(int);
	return size;
}

static
void nice_record(struct lttng_ctx_field *field,
		struct lib_ring_buffer_ctx *ctx,
		struct ltt_channel *chan)
{
	int nice;

	nice = task_nice(current);
	lib_ring_buffer_align_ctx(ctx, ltt_alignof(nice));
	chan->ops->event_write(ctx, &nice, sizeof(nice));
}

int lttng_add_nice_to_ctx(struct lttng_ctx **ctx)
{
	struct lttng_ctx_field *field;

	field = lttng_append_context(ctx);
	if (!field)
		return -ENOMEM;
	if (lttng_find_context(*ctx, "nice")) {
		lttng_remove_context_field(ctx, field);
		return -EEXIST;
	}
	field->event_field.name = "nice";
	field->event_field.type.atype = atype_integer;
	field->event_field.type.u.basic.integer.size = sizeof(int) * CHAR_BIT;
	field->event_field.type.u.basic.integer.alignment = ltt_alignof(int) * CHAR_BIT;
	field->event_field.type.u.basic.integer.signedness = is_signed_type(int);
	field->event_field.type.u.basic.integer.reverse_byte_order = 0;
	field->event_field.type.u.basic.integer.base = 10;
	field->event_field.type.u.basic.integer.encoding = lttng_encode_none;
	field->get_size = nice_get_size;
	field->record = nice_record;
	wrapper_vmalloc_sync_all();
	return 0;
}
EXPORT_SYMBOL_GPL(lttng_add_nice_to_ctx);

MODULE_LICENSE("GPL and additional rights");
MODULE_AUTHOR("Mathieu Desnoyers");
MODULE_DESCRIPTION("Linux Trace Toolkit Nice Context");
