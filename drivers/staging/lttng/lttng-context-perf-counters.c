/*
 * (C) Copyright	2009-2011 -
 * 		Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * LTTng performance monitoring counters (perf-counters) integration module.
 *
 * Dual LGPL v2.1/GPL v2 license.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/perf_event.h>
#include <linux/list.h>
#include <linux/string.h>
#include "ltt-events.h"
#include "wrapper/ringbuffer/frontend_types.h"
#include "wrapper/vmalloc.h"
#include "wrapper/perf.h"
#include "ltt-tracer.h"

static
size_t perf_counter_get_size(size_t offset)
{
	size_t size = 0;

	size += lib_ring_buffer_align(offset, ltt_alignof(uint64_t));
	size += sizeof(uint64_t);
	return size;
}

static
void perf_counter_record(struct lttng_ctx_field *field,
			 struct lib_ring_buffer_ctx *ctx,
			 struct ltt_channel *chan)
{
	struct perf_event *event;
	uint64_t value;

	event = field->u.perf_counter->e[ctx->cpu];
	if (likely(event)) {
		if (unlikely(event->state == PERF_EVENT_STATE_ERROR)) {
			value = 0;
		} else {
			event->pmu->read(event);
			value = local64_read(&event->count);
		}
	} else {
		/*
		 * Perf chooses not to be clever and not to support enabling a
		 * perf counter before the cpu is brought up. Therefore, we need
		 * to support having events coming (e.g. scheduler events)
		 * before the counter is setup. Write an arbitrary 0 in this
		 * case.
		 */
		value = 0;
	}
	lib_ring_buffer_align_ctx(ctx, ltt_alignof(value));
	chan->ops->event_write(ctx, &value, sizeof(value));
}

#if defined(CONFIG_PERF_EVENTS) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,99))
static
void overflow_callback(struct perf_event *event,
		       struct perf_sample_data *data,
		       struct pt_regs *regs)
{
}
#else
static
void overflow_callback(struct perf_event *event, int nmi,
		       struct perf_sample_data *data,
		       struct pt_regs *regs)
{
}
#endif

static
void lttng_destroy_perf_counter_field(struct lttng_ctx_field *field)
{
	struct perf_event **events = field->u.perf_counter->e;
	int cpu;

	get_online_cpus();
	for_each_online_cpu(cpu)
		perf_event_release_kernel(events[cpu]);
	put_online_cpus();
#ifdef CONFIG_HOTPLUG_CPU
	unregister_cpu_notifier(&field->u.perf_counter->nb);
#endif
	kfree(field->event_field.name);
	kfree(field->u.perf_counter->attr);
	kfree(events);
	kfree(field->u.perf_counter);
}

#ifdef CONFIG_HOTPLUG_CPU

/**
 *	lttng_perf_counter_hp_callback - CPU hotplug callback
 *	@nb: notifier block
 *	@action: hotplug action to take
 *	@hcpu: CPU number
 *
 *	Returns the success/failure of the operation. (%NOTIFY_OK, %NOTIFY_BAD)
 *
 * We can setup perf counters when the cpu is online (up prepare seems to be too
 * soon).
 */
static
int __cpuinit lttng_perf_counter_cpu_hp_callback(struct notifier_block *nb,
						 unsigned long action,
						 void *hcpu)
{
	unsigned int cpu = (unsigned long) hcpu;
	struct lttng_perf_counter_field *perf_field =
		container_of(nb, struct lttng_perf_counter_field, nb);
	struct perf_event **events = perf_field->e;
	struct perf_event_attr *attr = perf_field->attr;
	struct perf_event *pevent;

	if (!perf_field->hp_enable)
		return NOTIFY_OK;

	switch (action) {
	case CPU_ONLINE:
	case CPU_ONLINE_FROZEN:
		pevent = wrapper_perf_event_create_kernel_counter(attr,
				cpu, NULL, overflow_callback);
		if (!pevent || IS_ERR(pevent))
			return NOTIFY_BAD;
		if (pevent->state == PERF_EVENT_STATE_ERROR) {
			perf_event_release_kernel(pevent);
			return NOTIFY_BAD;
		}
		barrier();	/* Create perf counter before setting event */
		events[cpu] = pevent;
		break;
	case CPU_UP_CANCELED:
	case CPU_UP_CANCELED_FROZEN:
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		pevent = events[cpu];
		events[cpu] = NULL;
		barrier();	/* NULLify event before perf counter teardown */
		perf_event_release_kernel(pevent);
		break;
	}
	return NOTIFY_OK;
}

#endif

int lttng_add_perf_counter_to_ctx(uint32_t type,
				  uint64_t config,
				  const char *name,
				  struct lttng_ctx **ctx)
{
	struct lttng_ctx_field *field;
	struct lttng_perf_counter_field *perf_field;
	struct perf_event **events;
	struct perf_event_attr *attr;
	int ret;
	int cpu;
	char *name_alloc;

	events = kzalloc(num_possible_cpus() * sizeof(*events), GFP_KERNEL);
	if (!events)
		return -ENOMEM;

	attr = kzalloc(sizeof(struct perf_event_attr), GFP_KERNEL);
	if (!attr) {
		ret = -ENOMEM;
		goto error_attr;
	}

	attr->type = type;
	attr->config = config;
	attr->size = sizeof(struct perf_event_attr);
	attr->pinned = 1;
	attr->disabled = 0;

	perf_field = kzalloc(sizeof(struct lttng_perf_counter_field), GFP_KERNEL);
	if (!perf_field) {
		ret = -ENOMEM;
		goto error_alloc_perf_field;
	}
	perf_field->e = events;
	perf_field->attr = attr;

	name_alloc = kstrdup(name, GFP_KERNEL);
	if (!name_alloc) {
		ret = -ENOMEM;
		goto name_alloc_error;
	}

	field = lttng_append_context(ctx);
	if (!field) {
		ret = -ENOMEM;
		goto append_context_error;
	}
	if (lttng_find_context(*ctx, name_alloc)) {
		ret = -EEXIST;
		goto find_error;
	}

#ifdef CONFIG_HOTPLUG_CPU
	perf_field->nb.notifier_call =
		lttng_perf_counter_cpu_hp_callback;
	perf_field->nb.priority = 0;
	register_cpu_notifier(&perf_field->nb);
#endif

	get_online_cpus();
	for_each_online_cpu(cpu) {
		events[cpu] = wrapper_perf_event_create_kernel_counter(attr,
					cpu, NULL, overflow_callback);
		if (!events[cpu] || IS_ERR(events[cpu])) {
			ret = -EINVAL;
			goto counter_error;
		}
		if (events[cpu]->state == PERF_EVENT_STATE_ERROR) {
			ret = -EBUSY;
			goto counter_busy;
		}
	}
	put_online_cpus();

	field->destroy = lttng_destroy_perf_counter_field;

	field->event_field.name = name_alloc;
	field->event_field.type.atype = atype_integer;
	field->event_field.type.u.basic.integer.size = sizeof(uint64_t) * CHAR_BIT;
	field->event_field.type.u.basic.integer.alignment = ltt_alignof(uint64_t) * CHAR_BIT;
	field->event_field.type.u.basic.integer.signedness = is_signed_type(uint64_t);
	field->event_field.type.u.basic.integer.reverse_byte_order = 0;
	field->event_field.type.u.basic.integer.base = 10;
	field->event_field.type.u.basic.integer.encoding = lttng_encode_none;
	field->get_size = perf_counter_get_size;
	field->record = perf_counter_record;
	field->u.perf_counter = perf_field;
	perf_field->hp_enable = 1;

	wrapper_vmalloc_sync_all();
	return 0;

counter_busy:
counter_error:
	for_each_online_cpu(cpu) {
		if (events[cpu] && !IS_ERR(events[cpu]))
			perf_event_release_kernel(events[cpu]);
	}
	put_online_cpus();
#ifdef CONFIG_HOTPLUG_CPU
	unregister_cpu_notifier(&perf_field->nb);
#endif
find_error:
	lttng_remove_context_field(ctx, field);
append_context_error:
	kfree(name_alloc);
name_alloc_error:
	kfree(perf_field);
error_alloc_perf_field:
	kfree(attr);
error_attr:
	kfree(events);
	return ret;
}

MODULE_LICENSE("GPL and additional rights");
MODULE_AUTHOR("Mathieu Desnoyers");
MODULE_DESCRIPTION("Linux Trace Toolkit Perf Support");
