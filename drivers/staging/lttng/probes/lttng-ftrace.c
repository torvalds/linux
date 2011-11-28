/*
 * (C) Copyright	2009-2011 -
 * 		Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * LTTng function tracer integration module.
 *
 * Dual LGPL v2.1/GPL v2 license.
 */

/*
 * Ftrace function tracer does not seem to provide synchronization between probe
 * teardown and callback execution. Therefore, we make this module permanently
 * loaded (unloadable).
 *
 * TODO: Move to register_ftrace_function() (which is exported for
 * modules) for Linux >= 3.0. It is faster (only enables the selected
 * functions), and will stay there.
 */

#include <linux/module.h>
#include <linux/ftrace.h>
#include <linux/slab.h>
#include "../ltt-events.h"
#include "../wrapper/ringbuffer/frontend_types.h"
#include "../wrapper/ftrace.h"
#include "../wrapper/vmalloc.h"
#include "../ltt-tracer.h"

static
void lttng_ftrace_handler(unsigned long ip, unsigned long parent_ip, void **data)
{
	struct ltt_event *event = *data;
	struct ltt_channel *chan = event->chan;
	struct lib_ring_buffer_ctx ctx;
	struct {
		unsigned long ip;
		unsigned long parent_ip;
	} payload;
	int ret;

	if (unlikely(!ACCESS_ONCE(chan->session->active)))
		return;
	if (unlikely(!ACCESS_ONCE(chan->enabled)))
		return;
	if (unlikely(!ACCESS_ONCE(event->enabled)))
		return;

	lib_ring_buffer_ctx_init(&ctx, chan->chan, event,
				 sizeof(payload), ltt_alignof(payload), -1);
	ret = chan->ops->event_reserve(&ctx, event->id);
	if (ret < 0)
		return;
	payload.ip = ip;
	payload.parent_ip = parent_ip;
	lib_ring_buffer_align_ctx(&ctx, ltt_alignof(payload));
	chan->ops->event_write(&ctx, &payload, sizeof(payload));
	chan->ops->event_commit(&ctx);
	return;
}

/*
 * Create event description
 */
static
int lttng_create_ftrace_event(const char *name, struct ltt_event *event)
{
	struct lttng_event_field *fields;
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
	fields[0].type.u.basic.integer.alignment = ltt_alignof(unsigned long) * CHAR_BIT;
	fields[0].type.u.basic.integer.signedness = is_signed_type(unsigned long);
	fields[0].type.u.basic.integer.reverse_byte_order = 0;
	fields[0].type.u.basic.integer.base = 16;
	fields[0].type.u.basic.integer.encoding = lttng_encode_none;

	fields[1].name = "parent_ip";
	fields[1].type.atype = atype_integer;
	fields[1].type.u.basic.integer.size = sizeof(unsigned long) * CHAR_BIT;
	fields[1].type.u.basic.integer.alignment = ltt_alignof(unsigned long) * CHAR_BIT;
	fields[1].type.u.basic.integer.signedness = is_signed_type(unsigned long);
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

static
struct ftrace_probe_ops lttng_ftrace_ops = {
	.func = lttng_ftrace_handler,
};

int lttng_ftrace_register(const char *name,
			  const char *symbol_name,
			  struct ltt_event *event)
{
	int ret;

	ret = lttng_create_ftrace_event(name, event);
	if (ret)
		goto error;

	event->u.ftrace.symbol_name = kstrdup(symbol_name, GFP_KERNEL);
	if (!event->u.ftrace.symbol_name)
		goto name_error;

	/* Ensure the memory we just allocated don't trigger page faults */
	wrapper_vmalloc_sync_all();

	ret = wrapper_register_ftrace_function_probe(event->u.ftrace.symbol_name,
			&lttng_ftrace_ops, event);
	if (ret < 0)
		goto register_error;
	return 0;

register_error:
	kfree(event->u.ftrace.symbol_name);
name_error:
	kfree(event->desc->name);
	kfree(event->desc);
error:
	return ret;
}
EXPORT_SYMBOL_GPL(lttng_ftrace_register);

void lttng_ftrace_unregister(struct ltt_event *event)
{
	wrapper_unregister_ftrace_function_probe(event->u.ftrace.symbol_name,
			&lttng_ftrace_ops, event);
}
EXPORT_SYMBOL_GPL(lttng_ftrace_unregister);

void lttng_ftrace_destroy_private(struct ltt_event *event)
{
	kfree(event->u.ftrace.symbol_name);
	kfree(event->desc->fields);
	kfree(event->desc->name);
	kfree(event->desc);
}
EXPORT_SYMBOL_GPL(lttng_ftrace_destroy_private);

int lttng_ftrace_init(void)
{
	wrapper_vmalloc_sync_all();
	return 0;
}
module_init(lttng_ftrace_init)

/*
 * Ftrace takes care of waiting for a grace period (RCU sched) at probe
 * unregistration, and disables preemption around probe call.
 */
void lttng_ftrace_exit(void)
{
}
module_exit(lttng_ftrace_exit)

MODULE_LICENSE("GPL and additional rights");
MODULE_AUTHOR("Mathieu Desnoyers");
MODULE_DESCRIPTION("Linux Trace Toolkit Ftrace Support");
