#ifndef _LIB_RING_BUFFER_FRONTEND_H
#define _LIB_RING_BUFFER_FRONTEND_H

/*
 * lib/ringbuffer/frontend.h
 *
 * Ring Buffer Library Synchronization Header (API).
 *
 * Copyright (C) 2005-2012 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
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
 *
 * Author:
 *	Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * See ring_buffer_frontend.c for more information on wait-free algorithms.
 */

#include <linux/pipe_fs_i.h>
#include <linux/rcupdate.h>
#include <linux/cpumask.h>
#include <linux/module.h>
#include <linux/bitops.h>
#include <linux/splice.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/sched.h>
#include <linux/cache.h>
#include <linux/time.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/stat.h>
#include <linux/cpu.h>
#include <linux/fs.h>

#include <asm/atomic.h>
#include <asm/local.h>

/* Internal helpers */
#include "../../wrapper/ringbuffer/frontend_internal.h"

/* Buffer creation/removal and setup operations */

/*
 * switch_timer_interval is the time interval (in us) to fill sub-buffers with
 * padding to let readers get those sub-buffers.  Used for live streaming.
 *
 * read_timer_interval is the time interval (in us) to wake up pending readers.
 *
 * buf_addr is a pointer the the beginning of the preallocated buffer contiguous
 * address mapping. It is used only by RING_BUFFER_STATIC configuration. It can
 * be set to NULL for other backends.
 */

extern
struct channel *channel_create(const struct lib_ring_buffer_config *config,
			       const char *name, void *priv,
			       void *buf_addr,
			       size_t subbuf_size, size_t num_subbuf,
			       unsigned int switch_timer_interval,
			       unsigned int read_timer_interval);

/*
 * channel_destroy returns the private data pointer. It finalizes all channel's
 * buffers, waits for readers to release all references, and destroys the
 * channel.
 */
extern
void *channel_destroy(struct channel *chan);


/* Buffer read operations */

/*
 * Iteration on channel cpumask needs to issue a read barrier to match the write
 * barrier in cpu hotplug. It orders the cpumask read before read of per-cpu
 * buffer data. The per-cpu buffer is never removed by cpu hotplug; teardown is
 * only performed at channel destruction.
 */
#define for_each_channel_cpu(cpu, chan)					\
	for ((cpu) = -1;						\
		({ (cpu) = cpumask_next(cpu, (chan)->backend.cpumask);	\
		   smp_read_barrier_depends(); (cpu) < nr_cpu_ids; });)

extern struct lib_ring_buffer *channel_get_ring_buffer(
				const struct lib_ring_buffer_config *config,
				struct channel *chan, int cpu);
extern int lib_ring_buffer_open_read(struct lib_ring_buffer *buf);
extern void lib_ring_buffer_release_read(struct lib_ring_buffer *buf);

/*
 * Read sequence: snapshot, many get_subbuf/put_subbuf, move_consumer.
 */
extern int lib_ring_buffer_snapshot(struct lib_ring_buffer *buf,
				    unsigned long *consumed,
				    unsigned long *produced);
extern void lib_ring_buffer_move_consumer(struct lib_ring_buffer *buf,
					  unsigned long consumed_new);

extern int lib_ring_buffer_get_subbuf(struct lib_ring_buffer *buf,
				      unsigned long consumed);
extern void lib_ring_buffer_put_subbuf(struct lib_ring_buffer *buf);

/*
 * lib_ring_buffer_get_next_subbuf/lib_ring_buffer_put_next_subbuf are helpers
 * to read sub-buffers sequentially.
 */
static inline int lib_ring_buffer_get_next_subbuf(struct lib_ring_buffer *buf)
{
	int ret;

	ret = lib_ring_buffer_snapshot(buf, &buf->cons_snapshot,
				       &buf->prod_snapshot);
	if (ret)
		return ret;
	ret = lib_ring_buffer_get_subbuf(buf, buf->cons_snapshot);
	return ret;
}

static inline void lib_ring_buffer_put_next_subbuf(struct lib_ring_buffer *buf)
{
	lib_ring_buffer_put_subbuf(buf);
	lib_ring_buffer_move_consumer(buf, subbuf_align(buf->cons_snapshot,
						    buf->backend.chan));
}

extern void channel_reset(struct channel *chan);
extern void lib_ring_buffer_reset(struct lib_ring_buffer *buf);

static inline
unsigned long lib_ring_buffer_get_offset(const struct lib_ring_buffer_config *config,
					 struct lib_ring_buffer *buf)
{
	return v_read(config, &buf->offset);
}

static inline
unsigned long lib_ring_buffer_get_consumed(const struct lib_ring_buffer_config *config,
					   struct lib_ring_buffer *buf)
{
	return atomic_long_read(&buf->consumed);
}

/*
 * Must call lib_ring_buffer_is_finalized before reading counters (memory
 * ordering enforced with respect to trace teardown).
 */
static inline
int lib_ring_buffer_is_finalized(const struct lib_ring_buffer_config *config,
				 struct lib_ring_buffer *buf)
{
	int finalized = ACCESS_ONCE(buf->finalized);
	/*
	 * Read finalized before counters.
	 */
	smp_rmb();
	return finalized;
}

static inline
int lib_ring_buffer_channel_is_finalized(const struct channel *chan)
{
	return chan->finalized;
}

static inline
int lib_ring_buffer_channel_is_disabled(const struct channel *chan)
{
	return atomic_read(&chan->record_disabled);
}

static inline
unsigned long lib_ring_buffer_get_read_data_size(
				const struct lib_ring_buffer_config *config,
				struct lib_ring_buffer *buf)
{
	return subbuffer_get_read_data_size(config, &buf->backend);
}

static inline
unsigned long lib_ring_buffer_get_records_count(
				const struct lib_ring_buffer_config *config,
				struct lib_ring_buffer *buf)
{
	return v_read(config, &buf->records_count);
}

static inline
unsigned long lib_ring_buffer_get_records_overrun(
				const struct lib_ring_buffer_config *config,
				struct lib_ring_buffer *buf)
{
	return v_read(config, &buf->records_overrun);
}

static inline
unsigned long lib_ring_buffer_get_records_lost_full(
				const struct lib_ring_buffer_config *config,
				struct lib_ring_buffer *buf)
{
	return v_read(config, &buf->records_lost_full);
}

static inline
unsigned long lib_ring_buffer_get_records_lost_wrap(
				const struct lib_ring_buffer_config *config,
				struct lib_ring_buffer *buf)
{
	return v_read(config, &buf->records_lost_wrap);
}

static inline
unsigned long lib_ring_buffer_get_records_lost_big(
				const struct lib_ring_buffer_config *config,
				struct lib_ring_buffer *buf)
{
	return v_read(config, &buf->records_lost_big);
}

static inline
unsigned long lib_ring_buffer_get_records_read(
				const struct lib_ring_buffer_config *config,
				struct lib_ring_buffer *buf)
{
	return v_read(config, &buf->backend.records_read);
}

#endif /* _LIB_RING_BUFFER_FRONTEND_H */
