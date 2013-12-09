#ifndef _LIB_RING_BUFFER_FRONTEND_TYPES_H
#define _LIB_RING_BUFFER_FRONTEND_TYPES_H

/*
 * lib/ringbuffer/frontend_types.h
 *
 * Ring Buffer Library Synchronization Header (types).
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
 *
 * Author:
 *	Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * See ring_buffer_frontend.c for more information on wait-free algorithms.
 */

#include <linux/kref.h>
#include "../../wrapper/ringbuffer/config.h"
#include "../../wrapper/ringbuffer/backend_types.h"
#include "../../wrapper/spinlock.h"
#include "../../lib/prio_heap/lttng_prio_heap.h"	/* For per-CPU read-side iterator */

/*
 * A switch is done during tracing or as a final flush after tracing (so it
 * won't write in the new sub-buffer).
 */
enum switch_mode { SWITCH_ACTIVE, SWITCH_FLUSH };

/* channel-level read-side iterator */
struct channel_iter {
	/* Prio heap of buffers. Lowest timestamps at the top. */
	struct lttng_ptr_heap heap;	/* Heap of struct lib_ring_buffer ptrs */
	struct list_head empty_head;	/* Empty buffers linked-list head */
	int read_open;			/* Opened for reading ? */
	u64 last_qs;			/* Last quiescent state timestamp */
	u64 last_timestamp;		/* Last timestamp (for WARN_ON) */
	int last_cpu;			/* Last timestamp cpu */
	/*
	 * read() file operation state.
	 */
	unsigned long len_left;
};

/* channel: collection of per-cpu ring buffers. */
struct channel {
	atomic_t record_disabled;
	unsigned long commit_count_mask;	/*
						 * Commit count mask, removing
						 * the MSBs corresponding to
						 * bits used to represent the
						 * subbuffer index.
						 */

	struct channel_backend backend;		/* Associated backend */

	unsigned long switch_timer_interval;	/* Buffer flush (jiffies) */
	unsigned long read_timer_interval;	/* Reader wakeup (jiffies) */
	struct notifier_block cpu_hp_notifier;	/* CPU hotplug notifier */
	struct notifier_block tick_nohz_notifier; /* CPU nohz notifier */
	struct notifier_block hp_iter_notifier;	/* hotplug iterator notifier */
	unsigned int cpu_hp_enable:1;		/* Enable CPU hotplug notif. */
	unsigned int hp_iter_enable:1;		/* Enable hp iter notif. */
	wait_queue_head_t read_wait;		/* reader wait queue */
	wait_queue_head_t hp_wait;		/* CPU hotplug wait queue */
	int finalized;				/* Has channel been finalized */
	struct channel_iter iter;		/* Channel read-side iterator */
	struct kref ref;			/* Reference count */
};

/* Per-subbuffer commit counters used on the hot path */
struct commit_counters_hot {
	union v_atomic cc;		/* Commit counter */
	union v_atomic seq;		/* Consecutive commits */
};

/* Per-subbuffer commit counters used only on cold paths */
struct commit_counters_cold {
	union v_atomic cc_sb;		/* Incremented _once_ at sb switch */
};

/* Per-buffer read iterator */
struct lib_ring_buffer_iter {
	u64 timestamp;			/* Current record timestamp */
	size_t header_len;		/* Current record header length */
	size_t payload_len;		/* Current record payload length */

	struct list_head empty_node;	/* Linked list of empty buffers */
	unsigned long consumed, read_offset, data_size;
	enum {
		ITER_GET_SUBBUF = 0,
		ITER_TEST_RECORD,
		ITER_NEXT_RECORD,
		ITER_PUT_SUBBUF,
	} state;
	unsigned int allocated:1;
	unsigned int read_open:1;	/* Opened for reading ? */
};

/* ring buffer state */
struct lib_ring_buffer {
	/* First 32 bytes cache-hot cacheline */
	union v_atomic offset;		/* Current offset in the buffer */
	struct commit_counters_hot *commit_hot;
					/* Commit count per sub-buffer */
	atomic_long_t consumed;		/*
					 * Current offset in the buffer
					 * standard atomic access (shared)
					 */
	atomic_t record_disabled;
	/* End of first 32 bytes cacheline */
	union v_atomic last_tsc;	/*
					 * Last timestamp written in the buffer.
					 */

	struct lib_ring_buffer_backend backend;	/* Associated backend */

	struct commit_counters_cold *commit_cold;
					/* Commit count per sub-buffer */
	atomic_long_t active_readers;	/*
					 * Active readers count
					 * standard atomic access (shared)
					 */
					/* Dropped records */
	union v_atomic records_lost_full;	/* Buffer full */
	union v_atomic records_lost_wrap;	/* Nested wrap-around */
	union v_atomic records_lost_big;	/* Events too big */
	union v_atomic records_count;	/* Number of records written */
	union v_atomic records_overrun;	/* Number of overwritten records */
	wait_queue_head_t read_wait;	/* reader buffer-level wait queue */
	wait_queue_head_t write_wait;	/* writer buffer-level wait queue (for metadata only) */
	int finalized;			/* buffer has been finalized */
	struct timer_list switch_timer;	/* timer for periodical switch */
	struct timer_list read_timer;	/* timer for read poll */
	raw_spinlock_t raw_tick_nohz_spinlock;	/* nohz entry lock/trylock */
	struct lib_ring_buffer_iter iter;	/* read-side iterator */
	unsigned long get_subbuf_consumed;	/* Read-side consumed */
	unsigned long prod_snapshot;	/* Producer count snapshot */
	unsigned long cons_snapshot;	/* Consumer count snapshot */
	unsigned int get_subbuf:1,	/* Sub-buffer being held by reader */
		switch_timer_enabled:1,	/* Protected by ring_buffer_nohz_lock */
		read_timer_enabled:1;	/* Protected by ring_buffer_nohz_lock */
};

static inline
void *channel_get_private(struct channel *chan)
{
	return chan->backend.priv;
}

/*
 * Issue warnings and disable channels upon internal error.
 * Can receive struct lib_ring_buffer or struct lib_ring_buffer_backend
 * parameters.
 */
#define CHAN_WARN_ON(c, cond)						\
	({								\
		struct channel *__chan;					\
		int _____ret = unlikely(cond);				\
		if (_____ret) {						\
			if (__same_type(*(c), struct channel_backend))	\
				__chan = container_of((void *) (c),	\
							struct channel, \
							backend);	\
			else if (__same_type(*(c), struct channel))	\
				__chan = (void *) (c);			\
			else						\
				BUG_ON(1);				\
			atomic_inc(&__chan->record_disabled);		\
			WARN_ON(1);					\
		}							\
		_____ret;						\
	})

#endif /* _LIB_RING_BUFFER_FRONTEND_TYPES_H */
