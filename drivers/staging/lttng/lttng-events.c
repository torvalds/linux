/*
 * lttng-events.c
 *
 * Holds LTTng per-session event registry.
 *
 * Copyright (C) 2010-2012 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
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
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/utsname.h>
#include "wrapper/uuid.h"
#include "wrapper/vmalloc.h"	/* for wrapper_vmalloc_sync_all() */
#include "wrapper/random.h"
#include "wrapper/tracepoint.h"
#include "lttng-kernel-version.h"
#include "lttng-events.h"
#include "lttng-tracer.h"
#include "lttng-abi-old.h"

#define METADATA_CACHE_DEFAULT_SIZE 4096

static LIST_HEAD(sessions);
static LIST_HEAD(lttng_transport_list);
/*
 * Protect the sessions and metadata caches.
 */
static DEFINE_MUTEX(sessions_mutex);
static struct kmem_cache *event_cache;

static void _lttng_event_destroy(struct lttng_event *event);
static void _lttng_channel_destroy(struct lttng_channel *chan);
static int _lttng_event_unregister(struct lttng_event *event);
static
int _lttng_event_metadata_statedump(struct lttng_session *session,
				  struct lttng_channel *chan,
				  struct lttng_event *event);
static
int _lttng_session_metadata_statedump(struct lttng_session *session);
static
void _lttng_metadata_channel_hangup(struct lttng_metadata_stream *stream);

void synchronize_trace(void)
{
	synchronize_sched();
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,0))
#ifdef CONFIG_PREEMPT_RT_FULL
	synchronize_rcu();
#endif
#else /* (LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,0)) */
#ifdef CONFIG_PREEMPT_RT
	synchronize_rcu();
#endif
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,0)) */
}

struct lttng_session *lttng_session_create(void)
{
	struct lttng_session *session;
	struct lttng_metadata_cache *metadata_cache;

	mutex_lock(&sessions_mutex);
	session = kzalloc(sizeof(struct lttng_session), GFP_KERNEL);
	if (!session)
		goto err;
	INIT_LIST_HEAD(&session->chan);
	INIT_LIST_HEAD(&session->events);
	uuid_le_gen(&session->uuid);

	metadata_cache = kzalloc(sizeof(struct lttng_metadata_cache),
			GFP_KERNEL);
	if (!metadata_cache)
		goto err_free_session;
	metadata_cache->data = kzalloc(METADATA_CACHE_DEFAULT_SIZE,
			GFP_KERNEL);
	if (!metadata_cache->data)
		goto err_free_cache;
	metadata_cache->cache_alloc = METADATA_CACHE_DEFAULT_SIZE;
	kref_init(&metadata_cache->refcount);
	session->metadata_cache = metadata_cache;
	INIT_LIST_HEAD(&metadata_cache->metadata_stream);
	list_add(&session->list, &sessions);
	mutex_unlock(&sessions_mutex);
	return session;

err_free_cache:
	kfree(metadata_cache);
err_free_session:
	kfree(session);
err:
	mutex_unlock(&sessions_mutex);
	return NULL;
}

void metadata_cache_destroy(struct kref *kref)
{
	struct lttng_metadata_cache *cache =
		container_of(kref, struct lttng_metadata_cache, refcount);
	kfree(cache->data);
	kfree(cache);
}

void lttng_session_destroy(struct lttng_session *session)
{
	struct lttng_channel *chan, *tmpchan;
	struct lttng_event *event, *tmpevent;
	struct lttng_metadata_stream *metadata_stream;
	int ret;

	mutex_lock(&sessions_mutex);
	ACCESS_ONCE(session->active) = 0;
	list_for_each_entry(chan, &session->chan, list) {
		ret = lttng_syscalls_unregister(chan);
		WARN_ON(ret);
	}
	list_for_each_entry(event, &session->events, list) {
		ret = _lttng_event_unregister(event);
		WARN_ON(ret);
	}
	synchronize_trace();	/* Wait for in-flight events to complete */
	list_for_each_entry_safe(event, tmpevent, &session->events, list)
		_lttng_event_destroy(event);
	list_for_each_entry_safe(chan, tmpchan, &session->chan, list) {
		BUG_ON(chan->channel_type == METADATA_CHANNEL);
		_lttng_channel_destroy(chan);
	}
	list_for_each_entry(metadata_stream, &session->metadata_cache->metadata_stream, list)
		_lttng_metadata_channel_hangup(metadata_stream);
	kref_put(&session->metadata_cache->refcount, metadata_cache_destroy);
	list_del(&session->list);
	mutex_unlock(&sessions_mutex);
	kfree(session);
}

int lttng_session_enable(struct lttng_session *session)
{
	int ret = 0;
	struct lttng_channel *chan;

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
	ret = _lttng_session_metadata_statedump(session);
	if (ret) {
		ACCESS_ONCE(session->active) = 0;
		goto end;
	}
	ret = lttng_statedump_start(session);
	if (ret)
		ACCESS_ONCE(session->active) = 0;
end:
	mutex_unlock(&sessions_mutex);
	return ret;
}

int lttng_session_disable(struct lttng_session *session)
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

int lttng_channel_enable(struct lttng_channel *channel)
{
	int old;

	if (channel->channel_type == METADATA_CHANNEL)
		return -EPERM;
	old = xchg(&channel->enabled, 1);
	if (old)
		return -EEXIST;
	return 0;
}

int lttng_channel_disable(struct lttng_channel *channel)
{
	int old;

	if (channel->channel_type == METADATA_CHANNEL)
		return -EPERM;
	old = xchg(&channel->enabled, 0);
	if (!old)
		return -EEXIST;
	return 0;
}

int lttng_event_enable(struct lttng_event *event)
{
	int old;

	if (event->chan->channel_type == METADATA_CHANNEL)
		return -EPERM;
	old = xchg(&event->enabled, 1);
	if (old)
		return -EEXIST;
	return 0;
}

int lttng_event_disable(struct lttng_event *event)
{
	int old;

	if (event->chan->channel_type == METADATA_CHANNEL)
		return -EPERM;
	old = xchg(&event->enabled, 0);
	if (!old)
		return -EEXIST;
	return 0;
}

static struct lttng_transport *lttng_transport_find(const char *name)
{
	struct lttng_transport *transport;

	list_for_each_entry(transport, &lttng_transport_list, node) {
		if (!strcmp(transport->name, name))
			return transport;
	}
	return NULL;
}

struct lttng_channel *lttng_channel_create(struct lttng_session *session,
				       const char *transport_name,
				       void *buf_addr,
				       size_t subbuf_size, size_t num_subbuf,
				       unsigned int switch_timer_interval,
				       unsigned int read_timer_interval,
				       enum channel_type channel_type)
{
	struct lttng_channel *chan;
	struct lttng_transport *transport = NULL;

	mutex_lock(&sessions_mutex);
	if (session->been_active && channel_type != METADATA_CHANNEL)
		goto active;	/* Refuse to add channel to active session */
	transport = lttng_transport_find(transport_name);
	if (!transport) {
		printk(KERN_WARNING "LTTng transport %s not found\n",
		       transport_name);
		goto notransport;
	}
	if (!try_module_get(transport->owner)) {
		printk(KERN_WARNING "LTT : Can't lock transport module.\n");
		goto notransport;
	}
	chan = kzalloc(sizeof(struct lttng_channel), GFP_KERNEL);
	if (!chan)
		goto nomem;
	chan->session = session;
	chan->id = session->free_chan_id++;
	/*
	 * Note: the channel creation op already writes into the packet
	 * headers. Therefore the "chan" information used as input
	 * should be already accessible.
	 */
	chan->chan = transport->ops.channel_create(transport_name,
			chan, buf_addr, subbuf_size, num_subbuf,
			switch_timer_interval, read_timer_interval);
	if (!chan->chan)
		goto create_error;
	chan->enabled = 1;
	chan->ops = &transport->ops;
	chan->transport = transport;
	chan->channel_type = channel_type;
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
 * Only used internally at session destruction for per-cpu channels, and
 * when metadata channel is released.
 * Needs to be called with sessions mutex held.
 */
static
void _lttng_channel_destroy(struct lttng_channel *chan)
{
	chan->ops->channel_destroy(chan->chan);
	module_put(chan->transport->owner);
	list_del(&chan->list);
	lttng_destroy_context(chan->ctx);
	kfree(chan);
}

void lttng_metadata_channel_destroy(struct lttng_channel *chan)
{
	BUG_ON(chan->channel_type != METADATA_CHANNEL);

	/* Protect the metadata cache with the sessions_mutex. */
	mutex_lock(&sessions_mutex);
	_lttng_channel_destroy(chan);
	mutex_unlock(&sessions_mutex);
}
EXPORT_SYMBOL_GPL(lttng_metadata_channel_destroy);

static
void _lttng_metadata_channel_hangup(struct lttng_metadata_stream *stream)
{
	stream->finalized = 1;
	wake_up_interruptible(&stream->read_wait);
}

/*
 * Supports event creation while tracing session is active.
 */
struct lttng_event *lttng_event_create(struct lttng_channel *chan,
				   struct lttng_kernel_event *event_param,
				   void *filter,
				   const struct lttng_event_desc *internal_desc)
{
	struct lttng_event *event;
	int ret;

	mutex_lock(&sessions_mutex);
	if (chan->free_event_id == -1U)
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
	/* Populate lttng_event structure before tracepoint registration. */
	smp_wmb();
	switch (event_param->instrumentation) {
	case LTTNG_KERNEL_TRACEPOINT:
		event->desc = lttng_event_get(event_param->name);
		if (!event->desc)
			goto register_error;
		ret = kabi_2635_tracepoint_probe_register(event_param->name,
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
		struct lttng_event *event_return;

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
		 * Populate lttng_event structure before kretprobe registration.
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
		ret = _lttng_event_metadata_statedump(chan->session, chan,
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
		goto register_error;
	}
	ret = _lttng_event_metadata_statedump(chan->session, chan, event);
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
int _lttng_event_unregister(struct lttng_event *event)
{
	int ret = -EINVAL;

	switch (event->instrumentation) {
	case LTTNG_KERNEL_TRACEPOINT:
		ret = kabi_2635_tracepoint_probe_unregister(event->desc->name,
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
void _lttng_event_destroy(struct lttng_event *event)
{
	switch (event->instrumentation) {
	case LTTNG_KERNEL_TRACEPOINT:
		lttng_event_put(event->desc);
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
 * Serialize at most one packet worth of metadata into a metadata
 * channel.
 * We have exclusive access to our metadata buffer (protected by the
 * sessions_mutex), so we can do racy operations such as looking for
 * remaining space left in packet and write, since mutual exclusion
 * protects us from concurrent writes.
 */
int lttng_metadata_output_channel(struct lttng_metadata_stream *stream,
		struct channel *chan)
{
	struct lib_ring_buffer_ctx ctx;
	int ret = 0;
	size_t len, reserve_len;

	/*
	 * Ensure we support mutiple get_next / put sequences followed
	 * by put_next.
	 */
	WARN_ON(stream->metadata_in < stream->metadata_out);
	if (stream->metadata_in != stream->metadata_out)
		return 0;

	len = stream->metadata_cache->metadata_written -
		stream->metadata_in;
	if (!len)
		return 0;
	reserve_len = min_t(size_t,
			stream->transport->ops.packet_avail_size(chan),
			len);
	lib_ring_buffer_ctx_init(&ctx, chan, NULL, reserve_len,
			sizeof(char), -1);
	/*
	 * If reservation failed, return an error to the caller.
	 */
	ret = stream->transport->ops.event_reserve(&ctx, 0);
	if (ret != 0) {
		printk(KERN_WARNING "LTTng: Metadata event reservation failed\n");
		goto end;
	}
	stream->transport->ops.event_write(&ctx,
			stream->metadata_cache->data + stream->metadata_in,
			reserve_len);
	stream->transport->ops.event_commit(&ctx);
	stream->metadata_in += reserve_len;
	ret = reserve_len;

end:
	return ret;
}

/*
 * Write the metadata to the metadata cache.
 * Must be called with sessions_mutex held.
 */
int lttng_metadata_printf(struct lttng_session *session,
			  const char *fmt, ...)
{
	char *str;
	size_t len;
	va_list ap;
	struct lttng_metadata_stream *stream;

	WARN_ON_ONCE(!ACCESS_ONCE(session->active));

	va_start(ap, fmt);
	str = kvasprintf(GFP_KERNEL, fmt, ap);
	va_end(ap);
	if (!str)
		return -ENOMEM;

	len = strlen(str);
	if (session->metadata_cache->metadata_written + len >
			session->metadata_cache->cache_alloc) {
		char *tmp_cache_realloc;
		unsigned int tmp_cache_alloc_size;

		tmp_cache_alloc_size = max_t(unsigned int,
				session->metadata_cache->cache_alloc + len,
				session->metadata_cache->cache_alloc << 1);
		tmp_cache_realloc = krealloc(session->metadata_cache->data,
				tmp_cache_alloc_size, GFP_KERNEL);
		if (!tmp_cache_realloc)
			goto err;
		session->metadata_cache->cache_alloc = tmp_cache_alloc_size;
		session->metadata_cache->data = tmp_cache_realloc;
	}
	memcpy(session->metadata_cache->data +
			session->metadata_cache->metadata_written,
			str, len);
	session->metadata_cache->metadata_written += len;
	kfree(str);

	list_for_each_entry(stream, &session->metadata_cache->metadata_stream, list)
		wake_up_interruptible(&stream->read_wait);

	return 0;

err:
	kfree(str);
	return -ENOMEM;
}

/*
 * Must be called with sessions_mutex held.
 */
static
int _lttng_field_statedump(struct lttng_session *session,
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
int _lttng_context_metadata_statedump(struct lttng_session *session,
				    struct lttng_ctx *ctx)
{
	int ret = 0;
	int i;

	if (!ctx)
		return 0;
	for (i = 0; i < ctx->nr_fields; i++) {
		const struct lttng_ctx_field *field = &ctx->fields[i];

		ret = _lttng_field_statedump(session, &field->event_field);
		if (ret)
			return ret;
	}
	return ret;
}

static
int _lttng_fields_metadata_statedump(struct lttng_session *session,
				   struct lttng_event *event)
{
	const struct lttng_event_desc *desc = event->desc;
	int ret = 0;
	int i;

	for (i = 0; i < desc->nr_fields; i++) {
		const struct lttng_event_field *field = &desc->fields[i];

		ret = _lttng_field_statedump(session, field);
		if (ret)
			return ret;
	}
	return ret;
}

/*
 * Must be called with sessions_mutex held.
 */
static
int _lttng_event_metadata_statedump(struct lttng_session *session,
				  struct lttng_channel *chan,
				  struct lttng_event *event)
{
	int ret = 0;

	if (event->metadata_dumped || !ACCESS_ONCE(session->active))
		return 0;
	if (chan->channel_type == METADATA_CHANNEL)
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
	ret = _lttng_context_metadata_statedump(session, event->ctx);
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

	ret = _lttng_fields_metadata_statedump(session, event);
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

/*
 * Must be called with sessions_mutex held.
 */
static
int _lttng_channel_metadata_statedump(struct lttng_session *session,
				    struct lttng_channel *chan)
{
	int ret = 0;

	if (chan->metadata_dumped || !ACCESS_ONCE(session->active))
		return 0;

	if (chan->channel_type == METADATA_CHANNEL)
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
	ret = _lttng_context_metadata_statedump(session, chan->ctx);
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

/*
 * Must be called with sessions_mutex held.
 */
static
int _lttng_stream_packet_context_declare(struct lttng_session *session)
{
	return lttng_metadata_printf(session,
		"struct packet_context {\n"
		"	uint64_clock_monotonic_t timestamp_begin;\n"
		"	uint64_clock_monotonic_t timestamp_end;\n"
		"	uint64_t content_size;\n"
		"	uint64_t packet_size;\n"
		"	unsigned long events_discarded;\n"
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
 *
 * Must be called with sessions_mutex held.
 */
static
int _lttng_event_header_declare(struct lttng_session *session)
{
	return lttng_metadata_printf(session,
	"struct event_header_compact {\n"
	"	enum : uint5_t { compact = 0 ... 30, extended = 31 } id;\n"
	"	variant <id> {\n"
	"		struct {\n"
	"			uint27_clock_monotonic_t timestamp;\n"
	"		} compact;\n"
	"		struct {\n"
	"			uint32_t id;\n"
	"			uint64_clock_monotonic_t timestamp;\n"
	"		} extended;\n"
	"	} v;\n"
	"} align(%u);\n"
	"\n"
	"struct event_header_large {\n"
	"	enum : uint16_t { compact = 0 ... 65534, extended = 65535 } id;\n"
	"	variant <id> {\n"
	"		struct {\n"
	"			uint32_clock_monotonic_t timestamp;\n"
	"		} compact;\n"
	"		struct {\n"
	"			uint32_t id;\n"
	"			uint64_clock_monotonic_t timestamp;\n"
	"		} extended;\n"
	"	} v;\n"
	"} align(%u);\n\n",
	lttng_alignof(uint32_t) * CHAR_BIT,
	lttng_alignof(uint16_t) * CHAR_BIT
	);
}

 /*
 * Approximation of NTP time of day to clock monotonic correlation,
 * taken at start of trace.
 * Yes, this is only an approximation. Yes, we can (and will) do better
 * in future versions.
 */
static
uint64_t measure_clock_offset(void)
{
	uint64_t offset, monotonic[2], realtime;
	struct timespec rts = { 0, 0 };
	unsigned long flags;

	/* Disable interrupts to increase correlation precision. */
	local_irq_save(flags);
	monotonic[0] = trace_clock_read64();
	getnstimeofday(&rts);
	monotonic[1] = trace_clock_read64();
	local_irq_restore(flags);

	offset = (monotonic[0] + monotonic[1]) >> 1;
	realtime = (uint64_t) rts.tv_sec * NSEC_PER_SEC;
	realtime += rts.tv_nsec;
	offset = realtime - offset;
	return offset;
}

/*
 * Output metadata into this session's metadata buffers.
 * Must be called with sessions_mutex held.
 */
static
int _lttng_session_metadata_statedump(struct lttng_session *session)
{
	unsigned char *uuid_c = session->uuid.b;
	unsigned char uuid_s[37], clock_uuid_s[BOOT_ID_LEN];
	struct lttng_channel *chan;
	struct lttng_event *event;
	int ret = 0;

	if (!ACCESS_ONCE(session->active))
		return 0;
	if (session->metadata_dumped)
		goto skip_session;

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
		"typealias integer { size = %u; align = %u; signed = false; } := unsigned long;\n"
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
		lttng_alignof(uint8_t) * CHAR_BIT,
		lttng_alignof(uint16_t) * CHAR_BIT,
		lttng_alignof(uint32_t) * CHAR_BIT,
		lttng_alignof(uint64_t) * CHAR_BIT,
		sizeof(unsigned long) * CHAR_BIT,
		lttng_alignof(unsigned long) * CHAR_BIT,
		CTF_SPEC_MAJOR,
		CTF_SPEC_MINOR,
		uuid_s,
#ifdef __BIG_ENDIAN
		"be"
#else
		"le"
#endif
		);
	if (ret)
		goto end;

	ret = lttng_metadata_printf(session,
		"env {\n"
		"	hostname = \"%s\";\n"
		"	domain = \"kernel\";\n"
		"	sysname = \"%s\";\n"
		"	kernel_release = \"%s\";\n"
		"	kernel_version = \"%s\";\n"
		"	tracer_name = \"lttng-modules\";\n"
		"	tracer_major = %d;\n"
		"	tracer_minor = %d;\n"
		"	tracer_patchlevel = %d;\n"
		"};\n\n",
		current->nsproxy->uts_ns->name.nodename,
		utsname()->sysname,
		utsname()->release,
		utsname()->version,
		LTTNG_MODULES_MAJOR_VERSION,
		LTTNG_MODULES_MINOR_VERSION,
		LTTNG_MODULES_PATCHLEVEL_VERSION
		);
	if (ret)
		goto end;

	ret = lttng_metadata_printf(session,
		"clock {\n"
		"	name = %s;\n",
		"monotonic"
		);
	if (ret)
		goto end;

	if (!trace_clock_uuid(clock_uuid_s)) {
		ret = lttng_metadata_printf(session,
			"	uuid = \"%s\";\n",
			clock_uuid_s
			);
		if (ret)
			goto end;
	}

	ret = lttng_metadata_printf(session,
		"	description = \"Monotonic Clock\";\n"
		"	freq = %llu; /* Frequency, in Hz */\n"
		"	/* clock value offset from Epoch is: offset * (1/freq) */\n"
		"	offset = %llu;\n"
		"};\n\n",
		(unsigned long long) trace_clock_freq(),
		(unsigned long long) measure_clock_offset()
		);
	if (ret)
		goto end;

	ret = lttng_metadata_printf(session,
		"typealias integer {\n"
		"	size = 27; align = 1; signed = false;\n"
		"	map = clock.monotonic.value;\n"
		"} := uint27_clock_monotonic_t;\n"
		"\n"
		"typealias integer {\n"
		"	size = 32; align = %u; signed = false;\n"
		"	map = clock.monotonic.value;\n"
		"} := uint32_clock_monotonic_t;\n"
		"\n"
		"typealias integer {\n"
		"	size = 64; align = %u; signed = false;\n"
		"	map = clock.monotonic.value;\n"
		"} := uint64_clock_monotonic_t;\n\n",
		lttng_alignof(uint32_t) * CHAR_BIT,
		lttng_alignof(uint64_t) * CHAR_BIT
		);
	if (ret)
		goto end;

	ret = _lttng_stream_packet_context_declare(session);
	if (ret)
		goto end;

	ret = _lttng_event_header_declare(session);
	if (ret)
		goto end;

skip_session:
	list_for_each_entry(chan, &session->chan, list) {
		ret = _lttng_channel_metadata_statedump(session, chan);
		if (ret)
			goto end;
	}

	list_for_each_entry(event, &session->events, list) {
		ret = _lttng_event_metadata_statedump(session, event->chan, event);
		if (ret)
			goto end;
	}
	session->metadata_dumped = 1;
end:
	return ret;
}

/**
 * lttng_transport_register - LTT transport registration
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
void lttng_transport_register(struct lttng_transport *transport)
{
	/*
	 * Make sure no page fault can be triggered by the module about to be
	 * registered. We deal with this here so we don't have to call
	 * vmalloc_sync_all() in each module's init.
	 */
	wrapper_vmalloc_sync_all();

	mutex_lock(&sessions_mutex);
	list_add_tail(&transport->node, &lttng_transport_list);
	mutex_unlock(&sessions_mutex);
}
EXPORT_SYMBOL_GPL(lttng_transport_register);

/**
 * lttng_transport_unregister - LTT transport unregistration
 * @transport: transport structure
 */
void lttng_transport_unregister(struct lttng_transport *transport)
{
	mutex_lock(&sessions_mutex);
	list_del(&transport->node);
	mutex_unlock(&sessions_mutex);
}
EXPORT_SYMBOL_GPL(lttng_transport_unregister);

static int __init lttng_events_init(void)
{
	int ret;

	event_cache = KMEM_CACHE(lttng_event, 0);
	if (!event_cache)
		return -ENOMEM;
	ret = lttng_abi_init();
	if (ret)
		goto error_abi;
	return 0;
error_abi:
	kmem_cache_destroy(event_cache);
	return ret;
}

module_init(lttng_events_init);

static void __exit lttng_events_exit(void)
{
	struct lttng_session *session, *tmpsession;

	lttng_abi_exit();
	list_for_each_entry_safe(session, tmpsession, &sessions, list)
		lttng_session_destroy(session);
	kmem_cache_destroy(event_cache);
}

module_exit(lttng_events_exit);

MODULE_LICENSE("GPL and additional rights");
MODULE_AUTHOR("Mathieu Desnoyers <mathieu.desnoyers@efficios.com>");
MODULE_DESCRIPTION("LTTng Events");
