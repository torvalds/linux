/*
 * lttng-ring-buffer-client.h
 *
 * LTTng lib ring buffer client template.
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
#include <linux/types.h>
#include "lib/bitfield.h"
#include "wrapper/vmalloc.h"	/* for wrapper_vmalloc_sync_all() */
#include "wrapper/trace-clock.h"
#include "lttng-events.h"
#include "lttng-tracer.h"
#include "wrapper/ringbuffer/frontend_types.h"

#define LTTNG_COMPACT_EVENT_BITS	5
#define LTTNG_COMPACT_TSC_BITS		27

/*
 * Keep the natural field alignment for _each field_ within this structure if
 * you ever add/remove a field from this header. Packed attribute is not used
 * because gcc generates poor code on at least powerpc and mips. Don't ever
 * let gcc add padding between the structure elements.
 *
 * The guarantee we have with timestamps is that all the events in a
 * packet are included (inclusive) within the begin/end timestamps of
 * the packet. Another guarantee we have is that the "timestamp begin",
 * as well as the event timestamps, are monotonically increasing (never
 * decrease) when moving forward in a stream (physically). But this
 * guarantee does not apply to "timestamp end", because it is sampled at
 * commit time, which is not ordered with respect to space reservation.
 */

struct packet_header {
	/* Trace packet header */
	uint32_t magic;			/*
					 * Trace magic number.
					 * contains endianness information.
					 */
	uint8_t uuid[16];
	uint32_t stream_id;

	struct {
		/* Stream packet context */
		uint64_t timestamp_begin;	/* Cycle count at subbuffer start */
		uint64_t timestamp_end;		/* Cycle count at subbuffer end */
		uint64_t content_size;		/* Size of data in subbuffer */
		uint64_t packet_size;		/* Subbuffer size (include padding) */
		unsigned long events_discarded;	/*
						 * Events lost in this subbuffer since
						 * the beginning of the trace.
						 * (may overflow)
						 */
		uint32_t cpu_id;		/* CPU id associated with stream */
		uint8_t header_end;		/* End of header */
	} ctx;
};


static inline notrace u64 lib_ring_buffer_clock_read(struct channel *chan)
{
	return trace_clock_read64();
}

static inline
size_t ctx_get_size(size_t offset, struct lttng_ctx *ctx)
{
	int i;
	size_t orig_offset = offset;

	if (likely(!ctx))
		return 0;
	for (i = 0; i < ctx->nr_fields; i++)
		offset += ctx->fields[i].get_size(offset);
	return offset - orig_offset;
}

static inline
void ctx_record(struct lib_ring_buffer_ctx *bufctx,
		struct lttng_channel *chan,
		struct lttng_ctx *ctx)
{
	int i;

	if (likely(!ctx))
		return;
	for (i = 0; i < ctx->nr_fields; i++)
		ctx->fields[i].record(&ctx->fields[i], bufctx, chan);
}

/*
 * record_header_size - Calculate the header size and padding necessary.
 * @config: ring buffer instance configuration
 * @chan: channel
 * @offset: offset in the write buffer
 * @pre_header_padding: padding to add before the header (output)
 * @ctx: reservation context
 *
 * Returns the event header size (including padding).
 *
 * The payload must itself determine its own alignment from the biggest type it
 * contains.
 */
static __inline__
unsigned char record_header_size(const struct lib_ring_buffer_config *config,
				 struct channel *chan, size_t offset,
				 size_t *pre_header_padding,
				 struct lib_ring_buffer_ctx *ctx)
{
	struct lttng_channel *lttng_chan = channel_get_private(chan);
	struct lttng_event *event = ctx->priv;
	size_t orig_offset = offset;
	size_t padding;

	switch (lttng_chan->header_type) {
	case 1:	/* compact */
		padding = lib_ring_buffer_align(offset, lttng_alignof(uint32_t));
		offset += padding;
		if (!(ctx->rflags & (RING_BUFFER_RFLAG_FULL_TSC | LTTNG_RFLAG_EXTENDED))) {
			offset += sizeof(uint32_t);	/* id and timestamp */
		} else {
			/* Minimum space taken by LTTNG_COMPACT_EVENT_BITS id */
			offset += (LTTNG_COMPACT_EVENT_BITS + CHAR_BIT - 1) / CHAR_BIT;
			/* Align extended struct on largest member */
			offset += lib_ring_buffer_align(offset, lttng_alignof(uint64_t));
			offset += sizeof(uint32_t);	/* id */
			offset += lib_ring_buffer_align(offset, lttng_alignof(uint64_t));
			offset += sizeof(uint64_t);	/* timestamp */
		}
		break;
	case 2:	/* large */
		padding = lib_ring_buffer_align(offset, lttng_alignof(uint16_t));
		offset += padding;
		offset += sizeof(uint16_t);
		if (!(ctx->rflags & (RING_BUFFER_RFLAG_FULL_TSC | LTTNG_RFLAG_EXTENDED))) {
			offset += lib_ring_buffer_align(offset, lttng_alignof(uint32_t));
			offset += sizeof(uint32_t);	/* timestamp */
		} else {
			/* Align extended struct on largest member */
			offset += lib_ring_buffer_align(offset, lttng_alignof(uint64_t));
			offset += sizeof(uint32_t);	/* id */
			offset += lib_ring_buffer_align(offset, lttng_alignof(uint64_t));
			offset += sizeof(uint64_t);	/* timestamp */
		}
		break;
	default:
		padding = 0;
		WARN_ON_ONCE(1);
	}
	offset += ctx_get_size(offset, event->ctx);
	offset += ctx_get_size(offset, lttng_chan->ctx);

	*pre_header_padding = padding;
	return offset - orig_offset;
}

#include "wrapper/ringbuffer/api.h"

static
void lttng_write_event_header_slow(const struct lib_ring_buffer_config *config,
				 struct lib_ring_buffer_ctx *ctx,
				 uint32_t event_id);

/*
 * lttng_write_event_header
 *
 * Writes the event header to the offset (already aligned on 32-bits).
 *
 * @config: ring buffer instance configuration
 * @ctx: reservation context
 * @event_id: event ID
 */
static __inline__
void lttng_write_event_header(const struct lib_ring_buffer_config *config,
			    struct lib_ring_buffer_ctx *ctx,
			    uint32_t event_id)
{
	struct lttng_channel *lttng_chan = channel_get_private(ctx->chan);
	struct lttng_event *event = ctx->priv;

	if (unlikely(ctx->rflags))
		goto slow_path;

	switch (lttng_chan->header_type) {
	case 1:	/* compact */
	{
		uint32_t id_time = 0;

		bt_bitfield_write(&id_time, uint32_t,
				0,
				LTTNG_COMPACT_EVENT_BITS,
				event_id);
		bt_bitfield_write(&id_time, uint32_t,
				LTTNG_COMPACT_EVENT_BITS,
				LTTNG_COMPACT_TSC_BITS,
				ctx->tsc);
		lib_ring_buffer_write(config, ctx, &id_time, sizeof(id_time));
		break;
	}
	case 2:	/* large */
	{
		uint32_t timestamp = (uint32_t) ctx->tsc;
		uint16_t id = event_id;

		lib_ring_buffer_write(config, ctx, &id, sizeof(id));
		lib_ring_buffer_align_ctx(ctx, lttng_alignof(uint32_t));
		lib_ring_buffer_write(config, ctx, &timestamp, sizeof(timestamp));
		break;
	}
	default:
		WARN_ON_ONCE(1);
	}

	ctx_record(ctx, lttng_chan, lttng_chan->ctx);
	ctx_record(ctx, lttng_chan, event->ctx);
	lib_ring_buffer_align_ctx(ctx, ctx->largest_align);

	return;

slow_path:
	lttng_write_event_header_slow(config, ctx, event_id);
}

static
void lttng_write_event_header_slow(const struct lib_ring_buffer_config *config,
				 struct lib_ring_buffer_ctx *ctx,
				 uint32_t event_id)
{
	struct lttng_channel *lttng_chan = channel_get_private(ctx->chan);
	struct lttng_event *event = ctx->priv;

	switch (lttng_chan->header_type) {
	case 1:	/* compact */
		if (!(ctx->rflags & (RING_BUFFER_RFLAG_FULL_TSC | LTTNG_RFLAG_EXTENDED))) {
			uint32_t id_time = 0;

			bt_bitfield_write(&id_time, uint32_t,
					0,
					LTTNG_COMPACT_EVENT_BITS,
					event_id);
			bt_bitfield_write(&id_time, uint32_t,
					LTTNG_COMPACT_EVENT_BITS,
					LTTNG_COMPACT_TSC_BITS, ctx->tsc);
			lib_ring_buffer_write(config, ctx, &id_time, sizeof(id_time));
		} else {
			uint8_t id = 0;
			uint64_t timestamp = ctx->tsc;

			bt_bitfield_write(&id, uint8_t,
					0,
					LTTNG_COMPACT_EVENT_BITS,
					31);
			lib_ring_buffer_write(config, ctx, &id, sizeof(id));
			/* Align extended struct on largest member */
			lib_ring_buffer_align_ctx(ctx, lttng_alignof(uint64_t));
			lib_ring_buffer_write(config, ctx, &event_id, sizeof(event_id));
			lib_ring_buffer_align_ctx(ctx, lttng_alignof(uint64_t));
			lib_ring_buffer_write(config, ctx, &timestamp, sizeof(timestamp));
		}
		break;
	case 2:	/* large */
	{
		if (!(ctx->rflags & (RING_BUFFER_RFLAG_FULL_TSC | LTTNG_RFLAG_EXTENDED))) {
			uint32_t timestamp = (uint32_t) ctx->tsc;
			uint16_t id = event_id;

			lib_ring_buffer_write(config, ctx, &id, sizeof(id));
			lib_ring_buffer_align_ctx(ctx, lttng_alignof(uint32_t));
			lib_ring_buffer_write(config, ctx, &timestamp, sizeof(timestamp));
		} else {
			uint16_t id = 65535;
			uint64_t timestamp = ctx->tsc;

			lib_ring_buffer_write(config, ctx, &id, sizeof(id));
			/* Align extended struct on largest member */
			lib_ring_buffer_align_ctx(ctx, lttng_alignof(uint64_t));
			lib_ring_buffer_write(config, ctx, &event_id, sizeof(event_id));
			lib_ring_buffer_align_ctx(ctx, lttng_alignof(uint64_t));
			lib_ring_buffer_write(config, ctx, &timestamp, sizeof(timestamp));
		}
		break;
	}
	default:
		WARN_ON_ONCE(1);
	}
	ctx_record(ctx, lttng_chan, lttng_chan->ctx);
	ctx_record(ctx, lttng_chan, event->ctx);
	lib_ring_buffer_align_ctx(ctx, ctx->largest_align);
}

static const struct lib_ring_buffer_config client_config;

static u64 client_ring_buffer_clock_read(struct channel *chan)
{
	return lib_ring_buffer_clock_read(chan);
}

static
size_t client_record_header_size(const struct lib_ring_buffer_config *config,
				 struct channel *chan, size_t offset,
				 size_t *pre_header_padding,
				 struct lib_ring_buffer_ctx *ctx)
{
	return record_header_size(config, chan, offset,
				  pre_header_padding, ctx);
}

/**
 * client_packet_header_size - called on buffer-switch to a new sub-buffer
 *
 * Return header size without padding after the structure. Don't use packed
 * structure because gcc generates inefficient code on some architectures
 * (powerpc, mips..)
 */
static size_t client_packet_header_size(void)
{
	return offsetof(struct packet_header, ctx.header_end);
}

static void client_buffer_begin(struct lib_ring_buffer *buf, u64 tsc,
				unsigned int subbuf_idx)
{
	struct channel *chan = buf->backend.chan;
	struct packet_header *header =
		(struct packet_header *)
			lib_ring_buffer_offset_address(&buf->backend,
				subbuf_idx * chan->backend.subbuf_size);
	struct lttng_channel *lttng_chan = channel_get_private(chan);
	struct lttng_session *session = lttng_chan->session;

	header->magic = CTF_MAGIC_NUMBER;
	memcpy(header->uuid, session->uuid.b, sizeof(session->uuid));
	header->stream_id = lttng_chan->id;
	header->ctx.timestamp_begin = tsc;
	header->ctx.timestamp_end = 0;
	header->ctx.content_size = ~0ULL; /* for debugging */
	header->ctx.packet_size = ~0ULL;
	header->ctx.events_discarded = 0;
	header->ctx.cpu_id = buf->backend.cpu;
}

/*
 * offset is assumed to never be 0 here : never deliver a completely empty
 * subbuffer. data_size is between 1 and subbuf_size.
 */
static void client_buffer_end(struct lib_ring_buffer *buf, u64 tsc,
			      unsigned int subbuf_idx, unsigned long data_size)
{
	struct channel *chan = buf->backend.chan;
	struct packet_header *header =
		(struct packet_header *)
			lib_ring_buffer_offset_address(&buf->backend,
				subbuf_idx * chan->backend.subbuf_size);
	unsigned long records_lost = 0;

	header->ctx.timestamp_end = tsc;
	header->ctx.content_size =
		(uint64_t) data_size * CHAR_BIT;		/* in bits */
	header->ctx.packet_size =
		(uint64_t) PAGE_ALIGN(data_size) * CHAR_BIT;	/* in bits */
	records_lost += lib_ring_buffer_get_records_lost_full(&client_config, buf);
	records_lost += lib_ring_buffer_get_records_lost_wrap(&client_config, buf);
	records_lost += lib_ring_buffer_get_records_lost_big(&client_config, buf);
	header->ctx.events_discarded = records_lost;
}

static int client_buffer_create(struct lib_ring_buffer *buf, void *priv,
				int cpu, const char *name)
{
	return 0;
}

static void client_buffer_finalize(struct lib_ring_buffer *buf, void *priv, int cpu)
{
}

static const struct lib_ring_buffer_config client_config = {
	.cb.ring_buffer_clock_read = client_ring_buffer_clock_read,
	.cb.record_header_size = client_record_header_size,
	.cb.subbuffer_header_size = client_packet_header_size,
	.cb.buffer_begin = client_buffer_begin,
	.cb.buffer_end = client_buffer_end,
	.cb.buffer_create = client_buffer_create,
	.cb.buffer_finalize = client_buffer_finalize,

	.tsc_bits = LTTNG_COMPACT_TSC_BITS,
	.alloc = RING_BUFFER_ALLOC_PER_CPU,
	.sync = RING_BUFFER_SYNC_PER_CPU,
	.mode = RING_BUFFER_MODE_TEMPLATE,
	.backend = RING_BUFFER_PAGE,
	.output = RING_BUFFER_OUTPUT_TEMPLATE,
	.oops = RING_BUFFER_OOPS_CONSISTENCY,
	.ipi = RING_BUFFER_IPI_BARRIER,
	.wakeup = RING_BUFFER_WAKEUP_BY_TIMER,
};

static
struct channel *_channel_create(const char *name,
				struct lttng_channel *lttng_chan, void *buf_addr,
				size_t subbuf_size, size_t num_subbuf,
				unsigned int switch_timer_interval,
				unsigned int read_timer_interval)
{
	return channel_create(&client_config, name, lttng_chan, buf_addr,
			      subbuf_size, num_subbuf, switch_timer_interval,
			      read_timer_interval);
}

static
void lttng_channel_destroy(struct channel *chan)
{
	channel_destroy(chan);
}

static
struct lib_ring_buffer *lttng_buffer_read_open(struct channel *chan)
{
	struct lib_ring_buffer *buf;
	int cpu;

	for_each_channel_cpu(cpu, chan) {
		buf = channel_get_ring_buffer(&client_config, chan, cpu);
		if (!lib_ring_buffer_open_read(buf))
			return buf;
	}
	return NULL;
}

static
int lttng_buffer_has_read_closed_stream(struct channel *chan)
{
	struct lib_ring_buffer *buf;
	int cpu;

	for_each_channel_cpu(cpu, chan) {
		buf = channel_get_ring_buffer(&client_config, chan, cpu);
		if (!atomic_long_read(&buf->active_readers))
			return 1;
	}
	return 0;
}

static
void lttng_buffer_read_close(struct lib_ring_buffer *buf)
{
	lib_ring_buffer_release_read(buf);
}

static
int lttng_event_reserve(struct lib_ring_buffer_ctx *ctx,
		      uint32_t event_id)
{
	struct lttng_channel *lttng_chan = channel_get_private(ctx->chan);
	int ret, cpu;

	cpu = lib_ring_buffer_get_cpu(&client_config);
	if (cpu < 0)
		return -EPERM;
	ctx->cpu = cpu;

	switch (lttng_chan->header_type) {
	case 1:	/* compact */
		if (event_id > 30)
			ctx->rflags |= LTTNG_RFLAG_EXTENDED;
		break;
	case 2:	/* large */
		if (event_id > 65534)
			ctx->rflags |= LTTNG_RFLAG_EXTENDED;
		break;
	default:
		WARN_ON_ONCE(1);
	}

	ret = lib_ring_buffer_reserve(&client_config, ctx);
	if (ret)
		goto put;
	lttng_write_event_header(&client_config, ctx, event_id);
	return 0;
put:
	lib_ring_buffer_put_cpu(&client_config);
	return ret;
}

static
void lttng_event_commit(struct lib_ring_buffer_ctx *ctx)
{
	lib_ring_buffer_commit(&client_config, ctx);
	lib_ring_buffer_put_cpu(&client_config);
}

static
void lttng_event_write(struct lib_ring_buffer_ctx *ctx, const void *src,
		     size_t len)
{
	lib_ring_buffer_write(&client_config, ctx, src, len);
}

static
void lttng_event_write_from_user(struct lib_ring_buffer_ctx *ctx,
			       const void __user *src, size_t len)
{
	lib_ring_buffer_copy_from_user_inatomic(&client_config, ctx, src, len);
}

static
void lttng_event_memset(struct lib_ring_buffer_ctx *ctx,
		int c, size_t len)
{
	lib_ring_buffer_memset(&client_config, ctx, c, len);
}

static
wait_queue_head_t *lttng_get_writer_buf_wait_queue(struct channel *chan, int cpu)
{
	struct lib_ring_buffer *buf = channel_get_ring_buffer(&client_config,
					chan, cpu);
	return &buf->write_wait;
}

static
wait_queue_head_t *lttng_get_hp_wait_queue(struct channel *chan)
{
	return &chan->hp_wait;
}

static
int lttng_is_finalized(struct channel *chan)
{
	return lib_ring_buffer_channel_is_finalized(chan);
}

static
int lttng_is_disabled(struct channel *chan)
{
	return lib_ring_buffer_channel_is_disabled(chan);
}

static struct lttng_transport lttng_relay_transport = {
	.name = "relay-" RING_BUFFER_MODE_TEMPLATE_STRING,
	.owner = THIS_MODULE,
	.ops = {
		.channel_create = _channel_create,
		.channel_destroy = lttng_channel_destroy,
		.buffer_read_open = lttng_buffer_read_open,
		.buffer_has_read_closed_stream =
			lttng_buffer_has_read_closed_stream,
		.buffer_read_close = lttng_buffer_read_close,
		.event_reserve = lttng_event_reserve,
		.event_commit = lttng_event_commit,
		.event_write = lttng_event_write,
		.event_write_from_user = lttng_event_write_from_user,
		.event_memset = lttng_event_memset,
		.packet_avail_size = NULL,	/* Would be racy anyway */
		.get_writer_buf_wait_queue = lttng_get_writer_buf_wait_queue,
		.get_hp_wait_queue = lttng_get_hp_wait_queue,
		.is_finalized = lttng_is_finalized,
		.is_disabled = lttng_is_disabled,
	},
};

static int __init lttng_ring_buffer_client_init(void)
{
	/*
	 * This vmalloc sync all also takes care of the lib ring buffer
	 * vmalloc'd module pages when it is built as a module into LTTng.
	 */
	wrapper_vmalloc_sync_all();
	lttng_transport_register(&lttng_relay_transport);
	return 0;
}

module_init(lttng_ring_buffer_client_init);

static void __exit lttng_ring_buffer_client_exit(void)
{
	lttng_transport_unregister(&lttng_relay_transport);
}

module_exit(lttng_ring_buffer_client_exit);

MODULE_LICENSE("GPL and additional rights");
MODULE_AUTHOR("Mathieu Desnoyers");
MODULE_DESCRIPTION("LTTng ring buffer " RING_BUFFER_MODE_TEMPLATE_STRING
		   " client");
