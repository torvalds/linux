/*
 * probes/lttng-kretprobes.c
 *
 * LTTng kretprobes integration module.
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
#include <linux/kprobes.h>
#include <linux/slab.h>
#include <linux/kref.h>
#include "../lttng-events.h"
#include "../wrapper/ringbuffer/frontend_types.h"
#include "../wrapper/vmalloc.h"
#include "../lttng-tracer.h"

enum lttng_kretprobe_type {
	EVENT_ENTRY = 0,
	EVENT_RETURN = 1,
};

struct lttng_krp {
	struct kretprobe krp;
	struct lttng_event *event[2];	/* ENTRY and RETURN */
	struct kref kref_register;
	struct kref kref_alloc;
};

static
int _lttng_kretprobes_handler(struct kretprobe_instance *krpi,
			      struct pt_regs *regs,
			      enum lttng_kretprobe_type type)
{
	struct lttng_krp *lttng_krp =
		container_of(krpi->rp, struct lttng_krp, krp);
	struct lttng_event *event =
		lttng_krp->event[type];
	struct lttng_channel *chan = event->chan;
	struct lib_ring_buffer_ctx ctx;
	int ret;
	struct {
		unsigned long ip;
		unsigned long parent_ip;
	} payload;

	if (unlikely(!ACCESS_ONCE(chan->session->active)))
		return 0;
	if (unlikely(!ACCESS_ONCE(chan->enabled)))
		return 0;
	if (unlikely(!ACCESS_ONCE(event->enabled)))
		return 0;

	payload.ip = (unsigned long) krpi->rp->kp.addr;
	payload.parent_ip = (unsigned long) krpi->ret_addr;

	lib_ring_buffer_ctx_init(&ctx, chan->chan, event, sizeof(payload),
				 lttng_alignof(payload), -1);
	ret = chan->ops->event_reserve(&ctx, event->id);
	if (ret < 0)
		return 0;
	lib_ring_buffer_align_ctx(&ctx, lttng_alignof(payload));
	chan->ops->event_write(&ctx, &payload, sizeof(payload));
	chan->ops->event_commit(&ctx);
	return 0;
}

static
int lttng_kretprobes_handler_entry(struct kretprobe_instance *krpi,
				   struct pt_regs *regs)
{
	return _lttng_kretprobes_handler(krpi, regs, EVENT_ENTRY);
}

static
int lttng_kretprobes_handler_return(struct kretprobe_instance *krpi,
				    struct pt_regs *regs)
{
	return _lttng_kretprobes_handler(krpi, regs, EVENT_RETURN);
}

/*
 * Create event description
 */
static
int lttng_create_kprobe_event(const char *name, struct lttng_event *event,
			      enum lttng_kretprobe_type type)
{
	struct lttng_event_field *fields;
	struct lttng_event_desc *desc;
	int ret;
	char *alloc_name;
	size_t name_len;
	const char *suffix = NULL;

	desc = kzalloc(sizeof(*event->desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;
	name_len = strlen(name);
	switch (type) {
	case EVENT_ENTRY:
		suffix = "_entry";
		break;
	case EVENT_RETURN:
		suffix = "_return";
		break;
	}
	name_len += strlen(suffix);
	alloc_name = kmalloc(name_len + 1, GFP_KERNEL);
	if (!alloc_name) {
		ret = -ENOMEM;
		goto error_str;
	}
	strcpy(alloc_name, name);
	strcat(alloc_name, suffix);
	desc->name = alloc_name;
	desc->nr_fields = 2;
	desc->fields = fields =
		kzalloc(2 * sizeof(struct lttng_event_field), GFP_KERNEL);
	if (!desc->fields) {
		ret = -ENOMEM;
		goto error_fields;
	}
	fields[0].name = "ip";
	fields[0].type.atype = atype_integer;
	fields[0].type.u.basic.integer.size = sizeof(unsigned long) * CHAR_BIT;
	fields[0].type.u.basic.integer.alignment = lttng_alignof(unsigned long) * CHAR_BIT;
	fields[0].type.u.basic.integer.signedness = lttng_is_signed_type(unsigned long);
	fields[0].type.u.basic.integer.reverse_byte_order = 0;
	fields[0].type.u.basic.integer.base = 16;
	fields[0].type.u.basic.integer.encoding = lttng_encode_none;

	fields[1].name = "parent_ip";
	fields[1].type.atype = atype_integer;
	fields[1].type.u.basic.integer.size = sizeof(unsigned long) * CHAR_BIT;
	fields[1].type.u.basic.integer.alignment = lttng_alignof(unsigned long) * CHAR_BIT;
	fields[1].type.u.basic.integer.signedness = lttng_is_signed_type(unsigned long);
	fields[1].type.u.basic.integer.reverse_byte_order = 0;
	fields[1].type.u.basic.integer.base = 16;
	fields[1].type.u.basic.integer.encoding = lttng_encode_none;

	desc->owner = THIS_MODULE;
	event->desc = desc;

	return 0;

error_fields:
	kfree(desc->name);
error_str:
	kfree(desc);
	return ret;
}

int lttng_kretprobes_register(const char *name,
			   const char *symbol_name,
			   uint64_t offset,
			   uint64_t addr,
			   struct lttng_event *event_entry,
			   struct lttng_event *event_return)
{
	int ret;
	struct lttng_krp *lttng_krp;

	/* Kprobes expects a NULL symbol name if unused */
	if (symbol_name[0] == '\0')
		symbol_name = NULL;

	ret = lttng_create_kprobe_event(name, event_entry, EVENT_ENTRY);
	if (ret)
		goto error;
	ret = lttng_create_kprobe_event(name, event_return, EVENT_RETURN);
	if (ret)
		goto event_return_error;
	lttng_krp = kzalloc(sizeof(*lttng_krp), GFP_KERNEL);
	if (!lttng_krp)
		goto krp_error;
	lttng_krp->krp.entry_handler = lttng_kretprobes_handler_entry;
	lttng_krp->krp.handler = lttng_kretprobes_handler_return;
	if (symbol_name) {
		char *alloc_symbol;

		alloc_symbol = kstrdup(symbol_name, GFP_KERNEL);
		if (!alloc_symbol) {
			ret = -ENOMEM;
			goto name_error;
		}
		lttng_krp->krp.kp.symbol_name =
			alloc_symbol;
		event_entry->u.kretprobe.symbol_name =
			alloc_symbol;
		event_return->u.kretprobe.symbol_name =
			alloc_symbol;
	}
	lttng_krp->krp.kp.offset = offset;
	lttng_krp->krp.kp.addr = (void *) (unsigned long) addr;

	/* Allow probe handler to find event structures */
	lttng_krp->event[EVENT_ENTRY] = event_entry;
	lttng_krp->event[EVENT_RETURN] = event_return;
	event_entry->u.kretprobe.lttng_krp = lttng_krp;
	event_return->u.kretprobe.lttng_krp = lttng_krp;

	/*
	 * Both events must be unregistered before the kretprobe is
	 * unregistered. Same for memory allocation.
	 */
	kref_init(&lttng_krp->kref_alloc);
	kref_get(&lttng_krp->kref_alloc);	/* inc refcount to 2 */
	kref_init(&lttng_krp->kref_register);
	kref_get(&lttng_krp->kref_register);	/* inc refcount to 2 */

	/*
	 * Ensure the memory we just allocated don't trigger page faults.
	 * Well.. kprobes itself puts the page fault handler on the blacklist,
	 * but we can never be too careful.
	 */
	wrapper_vmalloc_sync_all();

	ret = register_kretprobe(&lttng_krp->krp);
	if (ret)
		goto register_error;
	return 0;

register_error:
	kfree(lttng_krp->krp.kp.symbol_name);
name_error:
	kfree(lttng_krp);
krp_error:
	kfree(event_return->desc->fields);
	kfree(event_return->desc->name);
	kfree(event_return->desc);
event_return_error:
	kfree(event_entry->desc->fields);
	kfree(event_entry->desc->name);
	kfree(event_entry->desc);
error:
	return ret;
}
EXPORT_SYMBOL_GPL(lttng_kretprobes_register);

static
void _lttng_kretprobes_unregister_release(struct kref *kref)
{
	struct lttng_krp *lttng_krp =
		container_of(kref, struct lttng_krp, kref_register);
	unregister_kretprobe(&lttng_krp->krp);
}

void lttng_kretprobes_unregister(struct lttng_event *event)
{
	kref_put(&event->u.kretprobe.lttng_krp->kref_register,
		_lttng_kretprobes_unregister_release);
}
EXPORT_SYMBOL_GPL(lttng_kretprobes_unregister);

static
void _lttng_kretprobes_release(struct kref *kref)
{
	struct lttng_krp *lttng_krp =
		container_of(kref, struct lttng_krp, kref_alloc);
	kfree(lttng_krp->krp.kp.symbol_name);
}

void lttng_kretprobes_destroy_private(struct lttng_event *event)
{
	kfree(event->desc->fields);
	kfree(event->desc->name);
	kfree(event->desc);
	kref_put(&event->u.kretprobe.lttng_krp->kref_alloc,
		_lttng_kretprobes_release);
}
EXPORT_SYMBOL_GPL(lttng_kretprobes_destroy_private);

MODULE_LICENSE("GPL and additional rights");
MODULE_AUTHOR("Mathieu Desnoyers");
MODULE_DESCRIPTION("Linux Trace Toolkit Kretprobes Support");
