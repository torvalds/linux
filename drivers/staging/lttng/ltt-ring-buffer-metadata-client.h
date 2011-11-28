/*
 * ltt-ring-buffer-client.h
 *
 * Copyright (C) 2010 - Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * LTTng lib ring buffer client template.
 *
 * Dual LGPL v2.1/GPL v2 license.
 */

#include <linux/module.h>
#include <linux/types.h>
#include "wrapper/vmalloc.h"	/* for wrapper_vmalloc_sync_all() */
#include "ltt-events.h"
#include "ltt-tracer.h"

struct metadata_packet_header {
	uint32_t magic;			/* 0x75D11D57 */
	uint8_t  uuid[16];		/* Unique Universal Identifier */
	uint32_t checksum;		/* 0 if unused */
	uint32_t content_size;		/* in bits */
	uint32_t packet_size;		/* in bits */
	uint8_t  compression_scheme;	/* 0 if unused */
	uint8_t  encryption_scheme;	/* 0 if unused */
	uint8_t  checksum_scheme;	/* 0 if unused */
	uint8_t  major;			/* CTF spec major version number */
	uint8_t  minor;			/* CTF spec minor version number */
	uint8_t  header_end[0];
};

struct metadata_record_header {
	uint8_t header_end[0];		/* End of header */
};

static const struct lib_ring_buffer_config client_config;

static inline
u64 lib_ring_buffer_clock_read(struct channel *chan)
{
	return 0;
}

static inline
unsigned char record_header_size(const struct lib_ring_buffer_config *config,
				 struct channel *chan, size_t offset,
				 size_t *pre_header_padding,
				 struct lib_ring_buffer_ctx *ctx)
{
	return 0;
}

#include "wrapper/ringbuffer/api.h"

static u64 client_ring_buffer_clock_read(struct channel *chan)
{
	return 0;
}

static
size_t client_record_header_size(const struct lib_ring_buffer_config *config,
				 struct channel *chan, size_t offset,
				 size_t *pre_header_padding,
				 struct lib_ring_buffer_ctx *ctx)
{
	return 0;
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
	return offsetof(struct metadata_packet_header, header_end);
}

static void client_buffer_begin(struct lib_ring_buffer *buf, u64 tsc,
				unsigned int subbuf_idx)
{
	struct channel *chan = buf->backend.chan;
	struct metadata_packet_header *header =
		(struct metadata_packet_header *)
			lib_ring_buffer_offset_address(&buf->backend,
				subbuf_idx * chan->backend.subbuf_size);
	struct ltt_channel *ltt_chan = channel_get_private(chan);
	struct ltt_session *session = ltt_chan->session;

	header->magic = TSDL_MAGIC_NUMBER;
	memcpy(header->uuid, session->uuid.b, sizeof(session->uuid));
	header->checksum = 0;		/* 0 if unused */
	header->content_size = 0xFFFFFFFF; /* in bits, for debugging */
	header->packet_size = 0xFFFFFFFF;  /* in bits, for debugging */
	header->compression_scheme = 0;	/* 0 if unused */
	header->encryption_scheme = 0;	/* 0 if unused */
	header->checksum_scheme = 0;	/* 0 if unused */
	header->major = CTF_SPEC_MAJOR;
	header->minor = CTF_SPEC_MINOR;
}

/*
 * offset is assumed to never be 0 here : never deliver a completely empty
 * subbuffer. data_size is between 1 and subbuf_size.
 */
static void client_buffer_end(struct lib_ring_buffer *buf, u64 tsc,
			      unsigned int subbuf_idx, unsigned long data_size)
{
	struct channel *chan = buf->backend.chan;
	struct metadata_packet_header *header =
		(struct metadata_packet_header *)
			lib_ring_buffer_offset_address(&buf->backend,
				subbuf_idx * chan->backend.subbuf_size);
	unsigned long records_lost = 0;

	header->content_size = data_size * CHAR_BIT;		/* in bits */
	header->packet_size = PAGE_ALIGN(data_size) * CHAR_BIT; /* in bits */
	/*
	 * We do not care about the records lost count, because the metadata
	 * channel waits and retry.
	 */
	(void) lib_ring_buffer_get_records_lost_full(&client_config, buf);
	records_lost += lib_ring_buffer_get_records_lost_wrap(&client_config, buf);
	records_lost += lib_ring_buffer_get_records_lost_big(&client_config, buf);
	WARN_ON_ONCE(records_lost != 0);
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

	.tsc_bits = 0,
	.alloc = RING_BUFFER_ALLOC_GLOBAL,
	.sync = RING_BUFFER_SYNC_GLOBAL,
	.mode = RING_BUFFER_MODE_TEMPLATE,
	.backend = RING_BUFFER_PAGE,
	.output = RING_BUFFER_OUTPUT_TEMPLATE,
	.oops = RING_BUFFER_OOPS_CONSISTENCY,
	.ipi = RING_BUFFER_IPI_BARRIER,
	.wakeup = RING_BUFFER_WAKEUP_BY_TIMER,
};

static
struct channel *_channel_create(const char *name,
				struct ltt_channel *ltt_chan, void *buf_addr,
				size_t subbuf_size, size_t num_subbuf,
				unsigned int switch_timer_interval,
				unsigned int read_timer_interval)
{
	return channel_create(&client_config, name, ltt_chan, buf_addr,
			      subbuf_size, num_subbuf, switch_timer_interval,
			      read_timer_interval);
}

static
void ltt_channel_destroy(struct channel *chan)
{
	channel_destroy(chan);
}

static
struct lib_ring_buffer *ltt_buffer_read_open(struct channel *chan)
{
	struct lib_ring_buffer *buf;

	buf = channel_get_ring_buffer(&client_config, chan, 0);
	if (!lib_ring_buffer_open_read(buf))
		return buf;
	return NULL;
}

static
int ltt_buffer_has_read_closed_stream(struct channel *chan)
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
void ltt_buffer_read_close(struct lib_ring_buffer *buf)
{
	lib_ring_buffer_release_read(buf);
}

static
int ltt_event_reserve(struct lib_ring_buffer_ctx *ctx, uint32_t event_id)
{
	return lib_ring_buffer_reserve(&client_config, ctx);
}

static
void ltt_event_commit(struct lib_ring_buffer_ctx *ctx)
{
	lib_ring_buffer_commit(&client_config, ctx);
}

static
void ltt_event_write(struct lib_ring_buffer_ctx *ctx, const void *src,
		     size_t len)
{
	lib_ring_buffer_write(&client_config, ctx, src, len);
}

static
void ltt_event_write_from_user(struct lib_ring_buffer_ctx *ctx,
			       const void __user *src, size_t len)
{
	lib_ring_buffer_copy_from_user(&client_config, ctx, src, len);
}

static
void ltt_event_memset(struct lib_ring_buffer_ctx *ctx,
		int c, size_t len)
{
	lib_ring_buffer_memset(&client_config, ctx, c, len);
}

static
size_t ltt_packet_avail_size(struct channel *chan)
			     
{
	unsigned long o_begin;
	struct lib_ring_buffer *buf;

	buf = chan->backend.buf;	/* Only for global buffer ! */
	o_begin = v_read(&client_config, &buf->offset);
	if (subbuf_offset(o_begin, chan) != 0) {
		return chan->backend.subbuf_size - subbuf_offset(o_begin, chan);
	} else {
		return chan->backend.subbuf_size - subbuf_offset(o_begin, chan)
			- sizeof(struct metadata_packet_header);
	}
}

static
wait_queue_head_t *ltt_get_writer_buf_wait_queue(struct channel *chan, int cpu)
{
	struct lib_ring_buffer *buf = channel_get_ring_buffer(&client_config,
					chan, cpu);
	return &buf->write_wait;
}

static
wait_queue_head_t *ltt_get_hp_wait_queue(struct channel *chan)
{
	return &chan->hp_wait;
}

static
int ltt_is_finalized(struct channel *chan)
{
	return lib_ring_buffer_channel_is_finalized(chan);
}

static
int ltt_is_disabled(struct channel *chan)
{
	return lib_ring_buffer_channel_is_disabled(chan);
}

static struct ltt_transport ltt_relay_transport = {
	.name = "relay-" RING_BUFFER_MODE_TEMPLATE_STRING,
	.owner = THIS_MODULE,
	.ops = {
		.channel_create = _channel_create,
		.channel_destroy = ltt_channel_destroy,
		.buffer_read_open = ltt_buffer_read_open,
		.buffer_has_read_closed_stream =
			ltt_buffer_has_read_closed_stream,
		.buffer_read_close = ltt_buffer_read_close,
		.event_reserve = ltt_event_reserve,
		.event_commit = ltt_event_commit,
		.event_write_from_user = ltt_event_write_from_user,
		.event_memset = ltt_event_memset,
		.event_write = ltt_event_write,
		.packet_avail_size = ltt_packet_avail_size,
		.get_writer_buf_wait_queue = ltt_get_writer_buf_wait_queue,
		.get_hp_wait_queue = ltt_get_hp_wait_queue,
		.is_finalized = ltt_is_finalized,
		.is_disabled = ltt_is_disabled,
	},
};

static int __init ltt_ring_buffer_client_init(void)
{
	/*
	 * This vmalloc sync all also takes care of the lib ring buffer
	 * vmalloc'd module pages when it is built as a module into LTTng.
	 */
	wrapper_vmalloc_sync_all();
	ltt_transport_register(&ltt_relay_transport);
	return 0;
}

module_init(ltt_ring_buffer_client_init);

static void __exit ltt_ring_buffer_client_exit(void)
{
	ltt_transport_unregister(&ltt_relay_transport);
}

module_exit(ltt_ring_buffer_client_exit);

MODULE_LICENSE("GPL and additional rights");
MODULE_AUTHOR("Mathieu Desnoyers");
MODULE_DESCRIPTION("LTTng ring buffer " RING_BUFFER_MODE_TEMPLATE_STRING
		   " client");
