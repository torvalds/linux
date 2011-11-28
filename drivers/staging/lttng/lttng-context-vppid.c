/*
 * (C) Copyright	2009-2011 -
 * 		Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * LTTng vPPID context.
 *
 * Dual LGPL v2.1/GPL v2 license.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/syscalls.h>
#include "ltt-events.h"
#include "wrapper/ringbuffer/frontend_types.h"
#include "wrapper/vmalloc.h"
#include "ltt-tracer.h"

static
size_t vppid_get_size(size_t offset)
{
	size_t size = 0;

	size += lib_ring_buffer_align(offset, ltt_alignof(pid_t));
	size += sizeof(pid_t);
	return size;
}

static
void vppid_record(struct lttng_ctx_field *field,
		  struct lib_ring_buffer_ctx *ctx,
		  struct ltt_channel *chan)
{
	struct task_struct *parent;
	pid_t vppid;

	/*
	 * nsproxy can be NULL when scheduled out of exit.
	 */
	rcu_read_lock();
	parent = rcu_dereference(current->real_parent);
	if (!parent->nsproxy)
		vppid = 0;
	else
		vppid = task_tgid_vnr(parent);
	rcu_read_unlock();
	lib_ring_buffer_align_ctx(ctx, ltt_alignof(vppid));
	chan->ops->event_write(ctx, &vppid, sizeof(vppid));
}

int lttng_add_vppid_to_ctx(struct lttng_ctx **ctx)
{
	struct lttng_ctx_field *field;

	field = lttng_append_context(ctx);
	if (!field)
		return -ENOMEM;
	if (lttng_find_context(*ctx, "vppid")) {
		lttng_remove_context_field(ctx, field);
		return -EEXIST;
	}
	field->event_field.name = "vppid";
	field->event_field.type.atype = atype_integer;
	field->event_field.type.u.basic.integer.size = sizeof(pid_t) * CHAR_BIT;
	field->event_field.type.u.basic.integer.alignment = ltt_alignof(pid_t) * CHAR_BIT;
	field->event_field.type.u.basic.integer.signedness = is_signed_type(pid_t);
	field->event_field.type.u.basic.integer.reverse_byte_order = 0;
	field->event_field.type.u.basic.integer.base = 10;
	field->event_field.type.u.basic.integer.encoding = lttng_encode_none;
	field->get_size = vppid_get_size;
	field->record = vppid_record;
	wrapper_vmalloc_sync_all();
	return 0;
}
EXPORT_SYMBOL_GPL(lttng_add_vppid_to_ctx);

MODULE_LICENSE("GPL and additional rights");
MODULE_AUTHOR("Mathieu Desnoyers");
MODULE_DESCRIPTION("Linux Trace Toolkit vPPID Context");
