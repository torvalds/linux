/*
 * ltt-events.c
 *
 * Copyright 2010 (c) - Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * Holds LTTng per-session event registry.
 *
 * Dual LGPL v2.1/GPL v2 license.
 */

#include <linux/module.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include "wrapper/uuid.h"
#include "wrapper/vmalloc.h"	/* for wrapper_vmalloc_sync_all() */
#include "ltt-events.h"
#include "ltt-tracer.h"

static LIST_HEAD(sessions);
static LIST_HEAD(ltt_transport_list);
static DEFINE_MUTEX(sessions_mutex);
static struct kmem_cache *event_cache;

static void _ltt_event_destroy(struct ltt_event *event);
static void _ltt_channel_destroy(struct ltt_channel *chan);
static int _ltt_event_unregister(struct ltt_event *event);
static
int _ltt_event_metadata_statedump(struct ltt_session *session,
				  struct ltt_channel *chan,
				  struct ltt_event *event);
static
int _ltt_session_metadata_statedump(struct ltt_session *session);

void synchronize_trace(void)
{
	synchronize_sched();
#ifdef CONFIG_PREEMPT_RT
	synchronize_rcu();
#endif
}

struct ltt_session *ltt_session_create(void)
{
	struct ltt_session *session;

	mutex_lock(&sessions_mutex);
	session = kzalloc(sizeof(struct ltt_session), GFP_KERNEL);
	if (!session)
		return NULL;
	INIT_LIST_HEAD(&session->chan);
	INIT_LIST_HEAD(&session->events);
	uuid_le_gen(&session->uuid);
	list_add(&session->list, &sessions);
	mutex_unlock(&sessions_mutex);
	return session;
}

void ltt_session_destroy(struct ltt_session *session)
{
	struct ltt_channel *chan, *tmpchan;
	struct ltt_event *event, *tmpevent;
	int ret;

	mutex_lock(&sessions_mutex);
	ACCESS_ONCE(session->active) = 0;
	list_for_each_entry(chan, &session->chan, list) {
		ret = lttng_syscalls_unregister(chan);
		WARN_ON(ret);
	}
	list_for_each_entry(event, &session->events, list) {
		ret = _ltt_event_unregister(event);
		WARN_ON(ret);
	}
	synchronize_trace();	/* Wait for in-flight events to complete */
	list_for_each_entry_safe(event, tmpevent, &session->events, list)
		_ltt_event_destroy(event);
	list_for_each_entry_safe(chan, tmpchan, &session->chan, list)
		_ltt_channel_destroy(chan);
	list_del(&session->list);
	mutex_unlock(&sessions_mutex);
	kfree(session);
}

int ltt_session_enable(struct ltt_session *session)
{
	int ret = 0;
	struct ltt_channel *chan;

	mutex_lock(&sessions_mutex);
	if (session->active) {
		ret = -EBUSY;
		goto end;
	}

	/*
	 * Snapshot the number of events per channel to know the type of header
	 * we need to use.
	 */
	list_for_each_entry(chan, &session->chan, list) {
		if (chan->header_type)
			continue;		/* don't change it if session stop/restart */
		if (chan->free_event_id < 31)
			chan->header_type = 1;	/* compact */
		else
			chan->header_type = 2;	/* large */
	}

	ACCESS_ONCE(session->active) = 1;
	ACCESS_ONCE(session->been_active) = 1;
	ret = _ltt_session_metadata_statedump(session);
	if (ret)
		ACCESS_ONCE(session->active) = 0;
end:
	mutex_unlock(&sessions_mutex);
	return ret;
}

int ltt_session_disable(struct ltt_session *session)
{
	int ret = 0;

	mutex_lock(&sessions_mutex);
	if (!session->active) {
		ret = -EBUSY;
		goto end;
	}
	ACCESS_ONCE(session->active) = 0;
end:
	mutex_unlock(&sessions_mutex);
	return ret;
}

int ltt_channel_enable(struct ltt_channel *channel)
{
	int old;

	if (channel == channel->session->metadata)
		return -EPERM;
	old = xchg(&channel->enabled, 1);
	if (old)
		return -EEXIST;
	return 0;
}

int ltt_channel_disable(struct ltt_channel *channel)
{
	int old;

	if (channel == channel->session->metadata)
		return -EPERM;
	old = xchg(&channel->enabled, 0);
	if (!old)
		return -EEXIST;
	return 0;
}

int ltt_event_enable(struct ltt_event *event)
{
	int old;

	if (event->chan == event->chan->session->metadata)
		return -EPERM;
	old = xchg(&event->enabled, 1);
	if (old)
		return -EEXIST;
	return 0;
}

int ltt_event_disable(struct ltt_event *event)
{
	int old;

	if (event->chan == event->chan->session->metadata)
		return -EPERM;
	old = xchg(&event->enabled, 0);
	if (!old)
		return -EEXIST;
	return 0;
}

static struct ltt_transport *ltt_transport_find(const char *name)
{
	struct ltt_transport *transport;

	list_for_each_entry(transport, &ltt_transport_list, node) {
		if (!strcmp(transport->name, name))
			return transport;
	}
	return NULL;
}

struct ltt_channel *ltt_channel_create(struct ltt_session *session,
				       const char *transport_name,
				       void *buf_addr,
				       size_t subbuf_size, size_t num_subbuf,
				       unsigned int switch_timer_interval,
				       unsigned int read_timer_interval)
{
	struct ltt_channel *chan;
	struct ltt_transport *transport = NULL;

	mutex_lock(&sessions_mutex);
	if (session->been_active)
		goto active;	/* Refuse to add channel to active session */
	transport = ltt_transport_find(transport_name);
	if (!transport) {
		printk(KERN_WARNING "LTTng transport %s not found\n",
		       transport_name);
		goto notransport;
	}
	if (!try_module_get(transport->owner)) {
		printk(KERN_WARNING "LTT : Can't lock transport module.\n");
		goto notransport;
	}
	chan = kzalloc(sizeof(struct ltt_channel), GFP_KERNEL);
	if (!chan)
		goto nomem;
	chan->session = session;
	chan->id = session->free_chan_id++;
	/*
	 * Note: the channel creation op already writes into the packet
	 * headers. Therefore the "chan" information used as input
	 * should be already accessible.
	 */
	chan->chan = transport->ops.channel_create("[lttng]", chan, buf_addr,
			subbuf_size, num_subbuf, switch_timer_interval,
			read_timer_interval);
	if (!chan->chan)
		goto create_error;
	chan->enabled = 1;
	chan->ops = &transport->ops;
	chan->transport = transport;
	list_add(&chan->list, &session->chan);
	mutex_unlock(&sessions_mutex);
	return chan;

create_error:
	kfree(chan);
nomem:
	if (transport)
		module_put(transport->owner);
notransport:
active:
	mutex_unlock(&sessions_mutex);
	return NULL;
}

/*
 * Only used internally at session destruction.
 */
static
void _ltt_channel_destroy(struct ltt_channel *chan)
{
	chan->ops->channel_destroy(chan->chan);
	module_put(chan->transport->owner);
	list_del(&chan->list);
	lttng_destroy_context(chan->ctx);
	kfree(chan);
}

/*
 * Supports event creation while tracing session is active.
 */
struct ltt_event *ltt_event_create(struct ltt_channel *chan,
				   struct lttng_kernel_event *event_param,
				   void *filter,
				   const struct lttng_event_desc *internal_desc)
{
	struct ltt_event *event;
	int ret;

	mutex_lock(&sessions_mutex);
	if (chan->free_event_id == -1UL)
		goto full;
	/*
	 * This is O(n^2) (for each event, the loop is called at event
	 * creation). Might require a hash if we have lots of events.
	 */
	list_for_each_entry(event, &chan->session->events, list)
		if (!strcmp(event->desc->name, event_param->name))
			goto exist;
	event = kmem_cache_zalloc(event_cache, GFP_KERNEL);
	if (!event)
		goto cache_error;
	event->chan = chan;
	event->filter = filter;
	event->id = chan->free_event_id++;
	event->enabled = 1;
	event->instrumentation = event_param->instrumentation;
	/* Populate ltt_event structure before tracepoint registration. */
	smp_wmb();
	switch (event_param->instrumentation) {
	case LTTNG_KERNEL_TRACEPOINT:
		event->desc = ltt_event_get(event_param->name);
		if (!event->desc)
			goto register_error;
		ret = tracepoint_probe_register(event_param->name,
				event->desc->probe_callback,
				event);
		if (ret)
			goto register_error;
		break;
	case LTTNG_KERNEL_KPROBE:
		ret = lttng_kprobes_register(event_param->name,
				event_param->u.kprobe.symbol_name,
				event_param->u.kprobe.offset,
				event_param->u.kprobe.addr,
				event);
		if (ret)
			goto register_error;
		ret = try_module_get(event->desc->owner);
		WARN_ON_ONCE(!ret);
		break;
	case LTTNG_KERNEL_KRETPROBE:
	{
		struct ltt_event *event_return;

		/* kretprobe defines 2 events */
		event_return =
			kmem_cache_zalloc(event_cache, GFP_KERNEL);
		if (!event_return)
			goto register_error;
		event_return->chan = chan;
		event_return->filter = filter;
		event_return->id = chan->free_event_id++;
		event_return->enabled = 1;
		event_return->instrumentation = event_param->instrumentation;
		/*
		 * Populate ltt_event structure before kretprobe registration.
		 */
		smp_wmb();
		ret = lttng_kretprobes_register(event_param->name,
				event_param->u.kretprobe.symbol_name,
				event_param->u.kretprobe.offset,
				event_param->u.kretprobe.addr,
				event, event_return);
		if (ret) {
			kmem_cache_free(event_cache, event_return);
			goto register_error;
		}
		/* Take 2 refs on the module: one per event. */
		ret = try_module_get(event->desc->owner);
		WARN_ON_ONCE(!ret);
		ret = try_module_get(event->desc->owner);
		WARN_ON_ONCE(!ret);
		ret = _ltt_event_metadata_statedump(chan->session, chan,
						    event_return);
		if (ret) {
			kmem_cache_free(event_cache, event_return);
			module_put(event->desc->owner);
			module_put(event->desc->owner);
			goto statedump_error;
		}
		list_add(&event_return->list, &chan->session->events);
		break;
	}
	case LTTNG_KERNEL_FUNCTION:
		ret = lttng_ftrace_register(event_param->name,
				event_param->u.ftrace.symbol_name,
				event);
		if (ret)
			goto register_error;
		ret = try_module_get(event->desc->owner);
		WARN_ON_ONCE(!ret);
		break;
	case LTTNG_KERNEL_NOOP:
		event->desc = internal_desc;
		if (!event->desc)
			goto register_error;
		break;
	default:
		WARN_ON_ONCE(1);
	}
	ret = _ltt_event_metadata_statedump(chan->session, chan, event);
	if (ret)
		goto statedump_error;
	list_add(&event->list, &chan->session->events);
	mutex_unlock(&sessions_mutex);
	return event;

statedump_error:
	/* If a statedump error occurs, events will not be readable. */
register_error:
	kmem_cache_free(event_cache, event);
cache_error:
exist:
full:
	mutex_unlock(&sessions_mutex);
	return NULL;
}

/*
 * Only used internally at session destruction.
 */
int _ltt_event_unregister(struct ltt_event *event)
{
	int ret = -EINVAL;

	switch (event->instrumentation) {
	case LTTNG_KERNEL_TRACEPOINT:
		ret = tracepoint_probe_unregister(event->desc->name,
						  event->desc->probe_callback,
						  event);
		if (ret)
			return ret;
		break;
	case LTTNG_KERNEL_KPROBE:
		lttng_kprobes_unregister(event);
		ret = 0;
		break;
	case LTTNG_KERNEL_KRETPROBE:
		lttng_kretprobes_unregister(event);
		ret = 0;
		break;
	case LTTNG_KERNEL_FUNCTION:
		lttng_ftrace_unregister(event);
		ret = 0;
		break;
	case LTTNG_KERNEL_NOOP:
		ret = 0;
		break;
	default:
		WARN_ON_ONCE(1);
	}
	return ret;
}

/*
 * Only used internally at session destruction.
 */
static
void _ltt_event_destroy(struct ltt_event *event)
{
	switch (event->instrumentation) {
	case LTTNG_KERNEL_TRACEPOINT:
		ltt_event_put(event->desc);
		break;
	case LTTNG_KERNEL_KPROBE:
		module_put(event->desc->owner);
		lttng_kprobes_destroy_private(event);
		break;
	case LTTNG_KERNEL_KRETPROBE:
		module_put(event->desc->owner);
		lttng_kretprobes_destroy_private(event);
		break;
	case LTTNG_KERNEL_FUNCTION:
		module_put(event->desc->owner);
		lttng_ftrace_destroy_private(event);
		break;
	case LTTNG_KERNEL_NOOP:
		break;
	default:
		WARN_ON_ONCE(1);
	}
	list_del(&event->list);
	lttng_destroy_context(event->ctx);
	kmem_cache_free(event_cache, event);
}

/*
 * We have exclusive access to our metadata buffer (protected by the
 * sessions_mutex), so we can do racy operations such as looking for
 * remaining space left in packet and write, since mutual exclusion
 * protects us from concurrent writes.
 */
int lttng_metadata_printf(struct ltt_session *session,
			  const char *fmt, ...)
{
	struct lib_ring_buffer_ctx ctx;
	struct ltt_channel *chan = session->metadata;
	char *str;
	int ret = 0, waitret;
	size_t len, reserve_len, pos;
	va_list ap;

	WARN_ON_ONCE(!ACCESS_ONCE(session->active));

	va_start(ap, fmt);
	str = kvasprintf(GFP_KERNEL, fmt, ap);
	va_end(ap);
	if (!str)
		return -ENOMEM;

	len = strlen(str);
	pos = 0;

	for (pos = 0; pos < len; pos += reserve_len) {
		reserve_len = min_t(size_t,
				chan->ops->packet_avail_size(chan->chan),
				len - pos);
		lib_ring_buffer_ctx_init(&ctx, chan->chan, NULL, reserve_len,
					 sizeof(char), -1);
		/*
		 * We don't care about metadata buffer's records lost
		 * count, because we always retry here. Report error if
		 * we need to bail out after timeout or being
		 * interrupted.
		 */
		waitret = wait_event_interruptible_timeout(*chan->ops->get_writer_buf_wait_queue(chan->chan, -1),
			({
				ret = chan->ops->event_reserve(&ctx, 0);
				ret != -ENOBUFS || !ret;
			}),
			msecs_to_jiffies(LTTNG_METADATA_TIMEOUT_MSEC));
		if (!waitret || waitret == -ERESTARTSYS || ret) {
			printk(KERN_WARNING "LTTng: Failure to write metadata to buffers (%s)\n",
				waitret == -ERESTARTSYS ? "interrupted" :
					(ret == -ENOBUFS ? "timeout" : "I/O error"));
			if (waitret == -ERESTARTSYS)
				ret = waitret;
			goto end;
		}
		chan->ops->event_write(&ctx, &str[pos], reserve_len);
		chan->ops->event_commit(&ctx);
	}
end:
	kfree(str);
	return ret;
}

static
int _ltt_field_statedump(struct ltt_session *session,
			 const struct lttng_event_field *field)
{
	int ret = 0;

	switch (field->type.atype) {
	case atype_integer:
		ret = lttng_metadata_printf(session,
			"		integer { size = %u; align = %u; signed = %u; encoding = %s; base = %u;%s } _%s;\n",
			field->type.u.basic.integer.size,
			field->type.u.basic.integer.alignment,
			field->type.u.basic.integer.signedness,
			(field->type.u.basic.integer.encoding == lttng_encode_none)
				? "none"
				: (field->type.u.basic.integer.encoding == lttng_encode_UTF8)
					? "UTF8"
					: "ASCII",
			field->type.u.basic.integer.base,
#ifdef __BIG_ENDIAN
			field->type.u.basic.integer.reverse_byte_order ? " byte_order = le;" : "",
#else
			field->type.u.basic.integer.reverse_byte_order ? " byte_order = be;" : "",
#endif
			field->name);
		break;
	case atype_enum:
		ret = lttng_metadata_printf(session,
			"		%s _%s;\n",
			field->type.u.basic.enumeration.name,
			field->name);
		break;
	case atype_array:
	{
		const struct lttng_basic_type *elem_type;

		elem_type = &field->type.u.array.elem_type;
		ret = lttng_metadata_printf(session,
			"		integer { size = %u; align = %u; signed = %u; encoding = %s; base = %u;%s } _%s[%u];\n",
			elem_type->u.basic.integer.size,
			elem_type->u.basic.integer.alignment,
			elem_type->u.basic.integer.signedness,
			(elem_type->u.basic.integer.encoding == lttng_encode_none)
				? "none"
				: (elem_type->u.basic.integer.encoding == lttng_encode_UTF8)
					? "UTF8"
					: "ASCII",
			elem_type->u.basic.integer.base,
#ifdef __BIG_ENDIAN
			elem_type->u.basic.integer.reverse_byte_order ? " byte_order = le;" : "",
#else
			elem_type->u.basic.integer.reverse_byte_order ? " byte_order = be;" : "",
#endif
			field->name, field->type.u.array.length);
		break;
	}
	case atype_sequence:
	{
		const struct lttng_basic_type *elem_type;
		const struct lttng_basic_type *length_type;

		elem_type = &field->type.u.sequence.elem_type;
		length_type = &field->type.u.sequence.length_type;
		ret = lttng_metadata_printf(session,
			"		integer { size = %u; align = %u; signed = %u; encoding = %s; base = %u;%s } __%s_length;\n",
			length_type->u.basic.integer.size,
			(unsigned int) length_type->u.basic.integer.alignment,
			length_type->u.basic.integer.signedness,
			(length_type->u.basic.integer.encoding == lttng_encode_none)
				? "none"
				: ((length_type->u.basic.integer.encoding == lttng_encode_UTF8)
					? "UTF8"
					: "ASCII"),
			length_type->u.basic.integer.base,
#ifdef __BIG_ENDIAN
			length_type->u.basic.integer.reverse_byte_order ? " byte_order = le;" : "",
#else
			length_type->u.basic.integer.reverse_byte_order ? " byte_order = be;" : "",
#endif
			field->name);
		if (ret)
			return ret;

		ret = lttng_metadata_printf(session,
			"		integer { size = %u; align = %u; signed = %u; encoding = %s; base = %u;%s } _%s[ __%s_length ];\n",
			elem_type->u.basic.integer.size,
			(unsigned int) elem_type->u.basic.integer.alignment,
			elem_type->u.basic.integer.signedness,
			(elem_type->u.basic.integer.encoding == lttng_encode_none)
				? "none"
				: ((elem_type->u.basic.integer.encoding == lttng_encode_UTF8)
					? "UTF8"
					: "ASCII"),
			elem_type->u.basic.integer.base,
#ifdef __BIG_ENDIAN
			elem_type->u.basic.integer.reverse_byte_order ? " byte_order = le;" : "",
#else
			elem_type->u.basic.integer.reverse_byte_order ? " byte_order = be;" : "",
#endif
			field->name,
			field->name);
		break;
	}

	case atype_string:
		/* Default encoding is UTF8 */
		ret = lttng_metadata_printf(session,
			"		string%s _%s;\n",
			field->type.u.basic.string.encoding == lttng_encode_ASCII ?
				" { encoding = ASCII; }" : "",
			field->name);
		break;
	default:
		WARN_ON_ONCE(1);
		return -EINVAL;
	}
	return ret;
}

static
int _ltt_context_metadata_statedump(struct ltt_session *session,
				    struct lttng_ctx *ctx)
{
	int ret = 0;
	int i;

	if (!ctx)
		return 0;
	for (i = 0; i < ctx->nr_fields; i++) {
		const struct lttng_ctx_field *field = &ctx->fields[i];

		ret = _ltt_field_statedump(session, &field->event_field);
		if (ret)
			return ret;
	}
	return ret;
}

static
int _ltt_fields_metadata_statedump(struct ltt_session *session,
				   struct ltt_event *event)
{
	const struct lttng_event_desc *desc = event->desc;
	int ret = 0;
	int i;

	for (i = 0; i < desc->nr_fields; i++) {
		const struct lttng_event_field *field = &desc->fields[i];

		ret = _ltt_field_statedump(session, field);
		if (ret)
			return ret;
	}
	return ret;
}

static
int _ltt_event_metadata_statedump(struct ltt_session *session,
				  struct ltt_channel *chan,
				  struct ltt_event *event)
{
	int ret = 0;

	if (event->metadata_dumped || !ACCESS_ONCE(session->active))
		return 0;
	if (chan == session->metadata)
		return 0;

	ret = lttng_metadata_printf(session,
		"event {\n"
		"	name = %s;\n"
		"	id = %u;\n"
		"	stream_id = %u;\n",
		event->desc->name,
		event->id,
		event->chan->id);
	if (ret)
		goto end;

	if (event->ctx) {
		ret = lttng_metadata_printf(session,
			"	context := struct {\n");
		if (ret)
			goto end;
	}
	ret = _ltt_context_metadata_statedump(session, event->ctx);
	if (ret)
		goto end;
	if (event->ctx) {
		ret = lttng_metadata_printf(session,
			"	};\n");
		if (ret)
			goto end;
	}

	ret = lttng_metadata_printf(session,
		"	fields := struct {\n"
		);
	if (ret)
		goto end;

	ret = _ltt_fields_metadata_statedump(session, event);
	if (ret)
		goto end;

	/*
	 * LTTng space reservation can only reserve multiples of the
	 * byte size.
	 */
	ret = lttng_metadata_printf(session,
		"	};\n"
		"};\n\n");
	if (ret)
		goto end;

	event->metadata_dumped = 1;
end:
	return ret;

}

static
int _ltt_channel_metadata_statedump(struct ltt_session *session,
				    struct ltt_channel *chan)
{
	int ret = 0;

	if (chan->metadata_dumped || !ACCESS_ONCE(session->active))
		return 0;
	if (chan == session->metadata)
		return 0;

	WARN_ON_ONCE(!chan->header_type);
	ret = lttng_metadata_printf(session,
		"stream {\n"
		"	id = %u;\n"
		"	event.header := %s;\n"
		"	packet.context := struct packet_context;\n",
		chan->id,
		chan->header_type == 1 ? "struct event_header_compact" :
			"struct event_header_large");
	if (ret)
		goto end;

	if (chan->ctx) {
		ret = lttng_metadata_printf(session,
			"	event.context := struct {\n");
		if (ret)
			goto end;
	}
	ret = _ltt_context_metadata_statedump(session, chan->ctx);
	if (ret)
		goto end;
	if (chan->ctx) {
		ret = lttng_metadata_printf(session,
			"	};\n");
		if (ret)
			goto end;
	}

	ret = lttng_metadata_printf(session,
		"};\n\n");

	chan->metadata_dumped = 1;
end:
	return ret;
}

static
int _ltt_stream_packet_context_declare(struct ltt_session *session)
{
	return lttng_metadata_printf(session,
		"struct packet_context {\n"
		"	uint64_t timestamp_begin;\n"
		"	uint64_t timestamp_end;\n"
		"	uint32_t events_discarded;\n"
		"	uint32_t content_size;\n"
		"	uint32_t packet_size;\n"
		"	uint32_t cpu_id;\n"
		"};\n\n"
		);
}

/*
 * Compact header:
 * id: range: 0 - 30.
 * id 31 is reserved to indicate an extended header.
 *
 * Large header:
 * id: range: 0 - 65534.
 * id 65535 is reserved to indicate an extended header.
 */
static
int _ltt_event_header_declare(struct ltt_session *session)
{
	return lttng_metadata_printf(session,
	"struct event_header_compact {\n"
	"	enum : uint5_t { compact = 0 ... 30, extended = 31 } id;\n"
	"	variant <id> {\n"
	"		struct {\n"
	"			uint27_t timestamp;\n"
	"		} compact;\n"
	"		struct {\n"
	"			uint32_t id;\n"
	"			uint64_t timestamp;\n"
	"		} extended;\n"
	"	} v;\n"
	"} align(%u);\n"
	"\n"
	"struct event_header_large {\n"
	"	enum : uint16_t { compact = 0 ... 65534, extended = 65535 } id;\n"
	"	variant <id> {\n"
	"		struct {\n"
	"			uint32_t timestamp;\n"
	"		} compact;\n"
	"		struct {\n"
	"			uint32_t id;\n"
	"			uint64_t timestamp;\n"
	"		} extended;\n"
	"	} v;\n"
	"} align(%u);\n\n",
	ltt_alignof(uint32_t) * CHAR_BIT,
	ltt_alignof(uint16_t) * CHAR_BIT
	);
}

/*
 * Output metadata into this session's metadata buffers.
 */
static
int _ltt_session_metadata_statedump(struct ltt_session *session)
{
	unsigned char *uuid_c = session->uuid.b;
	unsigned char uuid_s[37];
	struct ltt_channel *chan;
	struct ltt_event *event;
	int ret = 0;

	if (!ACCESS_ONCE(session->active))
		return 0;
	if (session->metadata_dumped)
		goto skip_session;
	if (!session->metadata) {
		printk(KERN_WARNING "LTTng: attempt to start tracing, but metadata channel is not found. Operation abort.\n");
		return -EPERM;
	}

	snprintf(uuid_s, sizeof(uuid_s),
		"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
		uuid_c[0], uuid_c[1], uuid_c[2], uuid_c[3],
		uuid_c[4], uuid_c[5], uuid_c[6], uuid_c[7],
		uuid_c[8], uuid_c[9], uuid_c[10], uuid_c[11],
		uuid_c[12], uuid_c[13], uuid_c[14], uuid_c[15]);

	ret = lttng_metadata_printf(session,
		"typealias integer { size = 8; align = %u; signed = false; } := uint8_t;\n"
		"typealias integer { size = 16; align = %u; signed = false; } := uint16_t;\n"
		"typealias integer { size = 32; align = %u; signed = false; } := uint32_t;\n"
		"typealias integer { size = 64; align = %u; signed = false; } := uint64_t;\n"
		"typealias integer { size = 5; align = 1; signed = false; } := uint5_t;\n"
		"typealias integer { size = 27; align = 1; signed = false; } := uint27_t;\n"
		"\n"
		"trace {\n"
		"	major = %u;\n"
		"	minor = %u;\n"
		"	uuid = \"%s\";\n"
		"	byte_order = %s;\n"
		"	packet.header := struct {\n"
		"		uint32_t magic;\n"
		"		uint8_t  uuid[16];\n"
		"		uint32_t stream_id;\n"
		"	};\n"
		"};\n\n",
		ltt_alignof(uint8_t) * CHAR_BIT,
		ltt_alignof(uint16_t) * CHAR_BIT,
		ltt_alignof(uint32_t) * CHAR_BIT,
		ltt_alignof(uint64_t) * CHAR_BIT,
		CTF_VERSION_MAJOR,
		CTF_VERSION_MINOR,
		uuid_s,
#ifdef __BIG_ENDIAN
		"be"
#else
		"le"
#endif
		);
	if (ret)
		goto end;

	ret = _ltt_stream_packet_context_declare(session);
	if (ret)
		goto end;

	ret = _ltt_event_header_declare(session);
	if (ret)
		goto end;

skip_session:
	list_for_each_entry(chan, &session->chan, list) {
		ret = _ltt_channel_metadata_statedump(session, chan);
		if (ret)
			goto end;
	}

	list_for_each_entry(event, &session->events, list) {
		ret = _ltt_event_metadata_statedump(session, event->chan, event);
		if (ret)
			goto end;
	}
	session->metadata_dumped = 1;
end:
	return ret;
}

/**
 * ltt_transport_register - LTT transport registration
 * @transport: transport structure
 *
 * Registers a transport which can be used as output to extract the data out of
 * LTTng. The module calling this registration function must ensure that no
 * trap-inducing code will be executed by the transport functions. E.g.
 * vmalloc_sync_all() must be called between a vmalloc and the moment the memory
 * is made visible to the transport function. This registration acts as a
 * vmalloc_sync_all. Therefore, only if the module allocates virtual memory
 * after its registration must it synchronize the TLBs.
 */
void ltt_transport_register(struct ltt_transport *transport)
{
	/*
	 * Make sure no page fault can be triggered by the module about to be
	 * registered. We deal with this here so we don't have to call
	 * vmalloc_sync_all() in each module's init.
	 */
	wrapper_vmalloc_sync_all();

	mutex_lock(&sessions_mutex);
	list_add_tail(&transport->node, &ltt_transport_list);
	mutex_unlock(&sessions_mutex);
}
EXPORT_SYMBOL_GPL(ltt_transport_register);

/**
 * ltt_transport_unregister - LTT transport unregistration
 * @transport: transport structure
 */
void ltt_transport_unregister(struct ltt_transport *transport)
{
	mutex_lock(&sessions_mutex);
	list_del(&transport->node);
	mutex_unlock(&sessions_mutex);
}
EXPORT_SYMBOL_GPL(ltt_transport_unregister);

static int __init ltt_events_init(void)
{
	int ret;

	event_cache = KMEM_CACHE(ltt_event, 0);
	if (!event_cache)
		return -ENOMEM;
	ret = ltt_debugfs_abi_init();
	if (ret)
		goto error_abi;
	return 0;
error_abi:
	kmem_cache_destroy(event_cache);
	return ret;
}

module_init(ltt_events_init);

static void __exit ltt_events_exit(void)
{
	struct ltt_session *session, *tmpsession;

	ltt_debugfs_abi_exit();
	list_for_each_entry_safe(session, tmpsession, &sessions, list)
		ltt_session_destroy(session);
	kmem_cache_destroy(event_cache);
}

module_exit(ltt_events_exit);

MODULE_LICENSE("GPL and additional rights");
MODULE_AUTHOR("Mathieu Desnoyers <mathieu.desnoyers@efficios.com>");
MODULE_DESCRIPTION("LTTng Events");
