/*
 * (C) Copyright	2009-2011 -
 * 		Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * LTTng kprobes integration module.
 *
 * Dual LGPL v2.1/GPL v2 license.
 */

#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/slab.h>
#include "../ltt-events.h"
#include "../wrapper/ringbuffer/frontend_types.h"
#include "../wrapper/vmalloc.h"
#include "../ltt-tracer.h"

static
int lttng_kprobes_handler_pre(struct kprobe *p, struct pt_regs *regs)
{
	struct ltt_event *event =
		container_of(p, struct ltt_event, u.kprobe.kp);
	struct ltt_channel *chan = event->chan;
	struct lib_ring_buffer_ctx ctx;
	int ret;
	unsigned long data = (unsigned long) p->addr;

	if (unlikely(!ACCESS_ONCE(chan->session->active)))
		return 0;
	if (unlikely(!ACCESS_ONCE(chan->enabled)))
		return 0;
	if (unlikely(!ACCESS_ONCE(event->enabled)))
		return 0;

	lib_ring_buffer_ctx_init(&ctx, chan->chan, event, sizeof(data),
				 ltt_alignof(data), -1);
	ret = chan->ops->event_reserve(&ctx, event->id);
	if (ret < 0)
		return 0;
	lib_ring_buffer_align_ctx(&ctx, ltt_alignof(data));
	chan->ops->event_write(&ctx, &data, sizeof(data));
	chan->ops->event_commit(&ctx);
	return 0;
}

/*
 * Create event description
 */
static
int lttng_create_kprobe_event(const char *name, struct ltt_event *event)
{
	struct lttng_event_field *field;
	struct lttng_event_desc *desc;
	int ret;

	desc = kzalloc(sizeof(*event->desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;
	desc->name = kstrdup(name, GFP_KERNEL);
	if (!desc->name) {
		ret = -ENOMEM;
		goto error_str;
	}
	desc->nr_fields = 1;
	desc->fields = field =
		kzalloc(1 * sizeof(struct lttng_event_field), GFP_KERNEL);
	if (!field) {
		ret = -ENOMEM;
		goto error_field;
	}
	field->name = "ip";
	field->type.atype = atype_integer;
	field->type.u.basic.integer.size = sizeof(unsigned long) * CHAR_BIT;
	field->type.u.basic.integer.alignment = ltt_alignof(unsigned long) * CHAR_BIT;
	field->type.u.basic.integer.signedness = is_signed_type(unsigned long);
	field->type.u.basic.integer.reverse_byte_order = 0;
	field->type.u.basic.integer.base = 16;
	field->type.u.basic.integer.encoding = lttng_encode_none;
	desc->owner = THIS_MODULE;
	event->desc = desc;

	return 0;

error_field:
	kfree(desc->name);
error_str:
	kfree(desc);
	return ret;
}

int lttng_kprobes_register(const char *name,
			   const char *symbol_name,
			   uint64_t offset,
			   uint64_t addr,
			   struct ltt_event *event)
{
	int ret;

	/* Kprobes expects a NULL symbol name if unused */
	if (symbol_name[0] == '\0')
		symbol_name = NULL;

	ret = lttng_create_kprobe_event(name, event);
	if (ret)
		goto error;
	memset(&event->u.kprobe.kp, 0, sizeof(event->u.kprobe.kp));
	event->u.kprobe.kp.pre_handler = lttng_kprobes_handler_pre;
	if (symbol_name) {
		event->u.kprobe.symbol_name =
			kzalloc(LTTNG_SYM_NAME_LEN * sizeof(char),
				GFP_KERNEL);
		if (!event->u.kprobe.symbol_name) {
			ret = -ENOMEM;
			goto name_error;
		}
		memcpy(event->u.kprobe.symbol_name, symbol_name,
		       LTTNG_SYM_NAME_LEN * sizeof(char));
		event->u.kprobe.kp.symbol_name =
			event->u.kprobe.symbol_name;
	}
	event->u.kprobe.kp.offset = offset;
	event->u.kprobe.kp.addr = (void *) (unsigned long) addr;

	/*
	 * Ensure the memory we just allocated don't trigger page faults.
	 * Well.. kprobes itself puts the page fault handler on the blacklist,
	 * but we can never be too careful.
	 */
	wrapper_vmalloc_sync_all();

	ret = register_kprobe(&event->u.kprobe.kp);
	if (ret)
		goto register_error;
	return 0;

register_error:
	kfree(event->u.kprobe.symbol_name);
name_error:
	kfree(event->desc->fields);
	kfree(event->desc->name);
	kfree(event->desc);
error:
	return ret;
}
EXPORT_SYMBOL_GPL(lttng_kprobes_register);

void lttng_kprobes_unregister(struct ltt_event *event)
{
	unregister_kprobe(&event->u.kprobe.kp);
}
EXPORT_SYMBOL_GPL(lttng_kprobes_unregister);

void lttng_kprobes_destroy_private(struct ltt_event *event)
{
	kfree(event->u.kprobe.symbol_name);
	kfree(event->desc->fields);
	kfree(event->desc->name);
	kfree(event->desc);
}
EXPORT_SYMBOL_GPL(lttng_kprobes_destroy_private);

MODULE_LICENSE("GPL and additional rights");
MODULE_AUTHOR("Mathieu Desnoyers");
MODULE_DESCRIPTION("Linux Trace Toolkit Kprobes Support");
