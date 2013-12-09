#ifndef _LIB_RING_BUFFER_FRONTEND_INTERNAL_H
#define _LIB_RING_BUFFER_FRONTEND_INTERNAL_H

/*
 * linux/ringbuffer/frontend_internal.h
 *
 * Ring Buffer Library Synchronization Header (internal helpers).
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

#include "../../wrapper/ringbuffer/config.h"
#include "../../wrapper/ringbuffer/backend_types.h"
#include "../../wrapper/ringbuffer/frontend_types.h"
#include "../../lib/prio_heap/lttng_prio_heap.h"	/* For per-CPU read-side iterator */

/* Buffer offset macros */

/* buf_trunc mask selects only the buffer number. */
static inline
unsigned long buf_trunc(unsigned long offset, struct channel *chan)
{
	return offset & ~(chan->backend.buf_size - 1);

}

/* Select the buffer number value (counter). */
static inline
unsigned long buf_trunc_val(unsigned long offset, struct channel *chan)
{
	return buf_trunc(offset, chan) >> chan->backend.buf_size_order;
}

/* buf_offset mask selects only the offset within the current buffer. */
static inline
unsigned long buf_offset(unsigned long offset, struct channel *chan)
{
	return offset & (chan->backend.buf_size - 1);
}

/* subbuf_offset mask selects the offset within the current subbuffer. */
static inline
unsigned long subbuf_offset(unsigned long offset, struct channel *chan)
{
	return offset & (chan->backend.subbuf_size - 1);
}

/* subbuf_trunc mask selects the subbuffer number. */
static inline
unsigned long subbuf_trunc(unsigned long offset, struct channel *chan)
{
	return offset & ~(chan->backend.subbuf_size - 1);
}

/* subbuf_align aligns the offset to the next subbuffer. */
static inline
unsigned long subbuf_align(unsigned long offset, struct channel *chan)
{
	return (offset + chan->backend.subbuf_size)
	       & ~(chan->backend.subbuf_size - 1);
}

/* subbuf_index returns the index of the current subbuffer within the buffer. */
static inline
unsigned long subbuf_index(unsigned long offset, struct channel *chan)
{
	return buf_offset(offset, chan) >> chan->backend.subbuf_size_order;
}

/*
 * Last TSC comparison functions. Check if the current TSC overflows tsc_bits
 * bits from the last TSC read. When overflows are detected, the full 64-bit
 * timestamp counter should be written in the record header. Reads and writes
 * last_tsc atomically.
 */

#if (BITS_PER_LONG == 32)
static inline
void save_last_tsc(const struct lib_ring_buffer_config *config,
		   struct lib_ring_buffer *buf, u64 tsc)
{
	if (config->tsc_bits == 0 || config->tsc_bits == 64)
		return;

	/*
	 * Ensure the compiler performs this update in a single instruction.
	 */
	v_set(config, &buf->last_tsc, (unsigned long)(tsc >> config->tsc_bits));
}

static inline
int last_tsc_overflow(const struct lib_ring_buffer_config *config,
		      struct lib_ring_buffer *buf, u64 tsc)
{
	unsigned long tsc_shifted;

	if (config->tsc_bits == 0 || config->tsc_bits == 64)
		return 0;

	tsc_shifted = (unsigned long)(tsc >> config->tsc_bits);
	if (unlikely(tsc_shifted
		     - (unsigned long)v_read(config, &buf->last_tsc)))
		return 1;
	else
		return 0;
}
#else
static inline
void save_last_tsc(const struct lib_ring_buffer_config *config,
		   struct lib_ring_buffer *buf, u64 tsc)
{
	if (config->tsc_bits == 0 || config->tsc_bits == 64)
		return;

	v_set(config, &buf->last_tsc, (unsigned long)tsc);
}

static inline
int last_tsc_overflow(const struct lib_ring_buffer_config *config,
		      struct lib_ring_buffer *buf, u64 tsc)
{
	if (config->tsc_bits == 0 || config->tsc_bits == 64)
		return 0;

	if (unlikely((tsc - v_read(config, &buf->last_tsc))
		     >> config->tsc_bits))
		return 1;
	else
		return 0;
}
#endif

extern
int lib_ring_buffer_reserve_slow(struct lib_ring_buffer_ctx *ctx);

extern
void lib_ring_buffer_switch_slow(struct lib_ring_buffer *buf,
				 enum switch_mode mode);

extern
void lib_ring_buffer_switch_remote(struct lib_ring_buffer *buf);

/* Buffer write helpers */

static inline
void lib_ring_buffer_reserve_push_reader(struct lib_ring_buffer *buf,
					 struct channel *chan,
					 unsigned long offset)
{
	unsigned long consumed_old, consumed_new;

	do {
		consumed_old = atomic_long_read(&buf->consumed);
		/*
		 * If buffer is in overwrite mode, push the reader consumed
		 * count if the write position has reached it and we are not
		 * at the first iteration (don't push the reader farther than
		 * the writer). This operation can be done concurrently by many
		 * writers in the same buffer, the writer being at the farthest
		 * write position sub-buffer index in the buffer being the one
		 * which will win this loop.
		 */
		if (unlikely(subbuf_trunc(offset, chan)
			      - subbuf_trunc(consumed_old, chan)
			     >= chan->backend.buf_size))
			consumed_new = subbuf_align(consumed_old, chan);
		else
			return;
	} while (unlikely(atomic_long_cmpxchg(&buf->consumed, consumed_old,
					      consumed_new) != consumed_old));
}

static inline
void lib_ring_buffer_vmcore_check_deliver(const struct lib_ring_buffer_config *config,
					  struct lib_ring_buffer *buf,
				          unsigned long commit_count,
				          unsigned long idx)
{
	if (config->oops == RING_BUFFER_OOPS_CONSISTENCY)
		v_set(config, &buf->commit_hot[idx].seq, commit_count);
}

static inline
int lib_ring_buffer_poll_deliver(const struct lib_ring_buffer_config *config,
				 struct lib_ring_buffer *buf,
			         struct channel *chan)
{
	unsigned long consumed_old, consumed_idx, commit_count, write_offset;

	consumed_old = atomic_long_read(&buf->consumed);
	consumed_idx = subbuf_index(consumed_old, chan);
	commit_count = v_read(config, &buf->commit_cold[consumed_idx].cc_sb);
	/*
	 * No memory barrier here, since we are only interested
	 * in a statistically correct polling result. The next poll will
	 * get the data is we are racing. The mb() that ensures correct
	 * memory order is in get_subbuf.
	 */
	write_offset = v_read(config, &buf->offset);

	/*
	 * Check that the subbuffer we are trying to consume has been
	 * already fully committed.
	 */

	if (((commit_count - chan->backend.subbuf_size)
	     & chan->commit_count_mask)
	    - (buf_trunc(consumed_old, chan)
	       >> chan->backend.num_subbuf_order)
	    != 0)
		return 0;

	/*
	 * Check that we are not about to read the same subbuffer in
	 * which the writer head is.
	 */
	if (subbuf_trunc(write_offset, chan) - subbuf_trunc(consumed_old, chan)
	    == 0)
		return 0;

	return 1;

}

static inline
int lib_ring_buffer_pending_data(const struct lib_ring_buffer_config *config,
				 struct lib_ring_buffer *buf,
				 struct channel *chan)
{
	return !!subbuf_offset(v_read(config, &buf->offset), chan);
}

static inline
unsigned long lib_ring_buffer_get_data_size(const struct lib_ring_buffer_config *config,
					    struct lib_ring_buffer *buf,
					    unsigned long idx)
{
	return subbuffer_get_data_size(config, &buf->backend, idx);
}

/*
 * Check if all space reservation in a buffer have been committed. This helps
 * knowing if an execution context is nested (for per-cpu buffers only).
 * This is a very specific ftrace use-case, so we keep this as "internal" API.
 */
static inline
int lib_ring_buffer_reserve_committed(const struct lib_ring_buffer_config *config,
				      struct lib_ring_buffer *buf,
				      struct channel *chan)
{
	unsigned long offset, idx, commit_count;

	CHAN_WARN_ON(chan, config->alloc != RING_BUFFER_ALLOC_PER_CPU);
	CHAN_WARN_ON(chan, config->sync != RING_BUFFER_SYNC_PER_CPU);

	/*
	 * Read offset and commit count in a loop so they are both read
	 * atomically wrt interrupts. By deal with interrupt concurrency by
	 * restarting both reads if the offset has been pushed. Note that given
	 * we only have to deal with interrupt concurrency here, an interrupt
	 * modifying the commit count will also modify "offset", so it is safe
	 * to only check for offset modifications.
	 */
	do {
		offset = v_read(config, &buf->offset);
		idx = subbuf_index(offset, chan);
		commit_count = v_read(config, &buf->commit_hot[idx].cc);
	} while (offset != v_read(config, &buf->offset));

	return ((buf_trunc(offset, chan) >> chan->backend.num_subbuf_order)
		     - (commit_count & chan->commit_count_mask) == 0);
}

/*
 * Receive end of subbuffer TSC as parameter. It has been read in the
 * space reservation loop of either reserve or switch, which ensures it
 * progresses monotonically with event records in the buffer. Therefore,
 * it ensures that the end timestamp of a subbuffer is <= begin
 * timestamp of the following subbuffers.
 */
static inline
void lib_ring_buffer_check_deliver(const struct lib_ring_buffer_config *config,
				   struct lib_ring_buffer *buf,
			           struct channel *chan,
			           unsigned long offset,
				   unsigned long commit_count,
			           unsigned long idx,
				   u64 tsc)
{
	unsigned long old_commit_count = commit_count
					 - chan->backend.subbuf_size;

	/* Check if all commits have been done */
	if (unlikely((buf_trunc(offset, chan) >> chan->backend.num_subbuf_order)
		     - (old_commit_count & chan->commit_count_mask) == 0)) {
		/*
		 * If we succeeded at updating cc_sb below, we are the subbuffer
		 * writer delivering the subbuffer. Deals with concurrent
		 * updates of the "cc" value without adding a add_return atomic
		 * operation to the fast path.
		 *
		 * We are doing the delivery in two steps:
		 * - First, we cmpxchg() cc_sb to the new value
		 *   old_commit_count + 1. This ensures that we are the only
		 *   subbuffer user successfully filling the subbuffer, but we
		 *   do _not_ set the cc_sb value to "commit_count" yet.
		 *   Therefore, other writers that would wrap around the ring
		 *   buffer and try to start writing to our subbuffer would
		 *   have to drop records, because it would appear as
		 *   non-filled.
		 *   We therefore have exclusive access to the subbuffer control
		 *   structures.  This mutual exclusion with other writers is
		 *   crucially important to perform record overruns count in
		 *   flight recorder mode locklessly.
		 * - When we are ready to release the subbuffer (either for
		 *   reading or for overrun by other writers), we simply set the
		 *   cc_sb value to "commit_count" and perform delivery.
		 *
		 * The subbuffer size is least 2 bytes (minimum size: 1 page).
		 * This guarantees that old_commit_count + 1 != commit_count.
		 */

		/*
		 * Order prior updates to reserve count prior to the
		 * commit_cold cc_sb update.
		 */
		smp_wmb();
		if (likely(v_cmpxchg(config, &buf->commit_cold[idx].cc_sb,
					 old_commit_count, old_commit_count + 1)
			   == old_commit_count)) {
			/*
			 * Start of exclusive subbuffer access. We are
			 * guaranteed to be the last writer in this subbuffer
			 * and any other writer trying to access this subbuffer
			 * in this state is required to drop records.
			 */
			v_add(config,
			      subbuffer_get_records_count(config,
							  &buf->backend, idx),
			      &buf->records_count);
			v_add(config,
			      subbuffer_count_records_overrun(config,
							      &buf->backend,
							      idx),
			      &buf->records_overrun);
			config->cb.buffer_end(buf, tsc, idx,
					      lib_ring_buffer_get_data_size(config,
									buf,
									idx));

			/*
			 * Set noref flag and offset for this subbuffer id.
			 * Contains a memory barrier that ensures counter stores
			 * are ordered before set noref and offset.
			 */
			lib_ring_buffer_set_noref_offset(config, &buf->backend, idx,
							 buf_trunc_val(offset, chan));

			/*
			 * Order set_noref and record counter updates before the
			 * end of subbuffer exclusive access. Orders with
			 * respect to writers coming into the subbuffer after
			 * wrap around, and also order wrt concurrent readers.
			 */
			smp_mb();
			/* End of exclusive subbuffer access */
			v_set(config, &buf->commit_cold[idx].cc_sb,
			      commit_count);
			/*
			 * Order later updates to reserve count after
			 * the commit_cold cc_sb update.
			 */
			smp_wmb();
			lib_ring_buffer_vmcore_check_deliver(config, buf,
							 commit_count, idx);

			/*
			 * RING_BUFFER_WAKEUP_BY_WRITER wakeup is not lock-free.
			 */
			if (config->wakeup == RING_BUFFER_WAKEUP_BY_WRITER
			    && atomic_long_read(&buf->active_readers)
			    && lib_ring_buffer_poll_deliver(config, buf, chan)) {
				wake_up_interruptible(&buf->read_wait);
				wake_up_interruptible(&chan->read_wait);
			}

		}
	}
}

/*
 * lib_ring_buffer_write_commit_counter
 *
 * For flight recording. must be called after commit.
 * This function increments the subbuffer's commit_seq counter each time the
 * commit count reaches back the reserve offset (modulo subbuffer size). It is
 * useful for crash dump.
 */
static inline
void lib_ring_buffer_write_commit_counter(const struct lib_ring_buffer_config *config,
					  struct lib_ring_buffer *buf,
				          struct channel *chan,
				          unsigned long idx,
				          unsigned long buf_offset,
				          unsigned long commit_count,
				          size_t slot_size)
{
	unsigned long offset, commit_seq_old;

	if (config->oops != RING_BUFFER_OOPS_CONSISTENCY)
		return;

	offset = buf_offset + slot_size;

	/*
	 * subbuf_offset includes commit_count_mask. We can simply
	 * compare the offsets within the subbuffer without caring about
	 * buffer full/empty mismatch because offset is never zero here
	 * (subbuffer header and record headers have non-zero length).
	 */
	if (unlikely(subbuf_offset(offset - commit_count, chan)))
		return;

	commit_seq_old = v_read(config, &buf->commit_hot[idx].seq);
	while ((long) (commit_seq_old - commit_count) < 0)
		commit_seq_old = v_cmpxchg(config, &buf->commit_hot[idx].seq,
					   commit_seq_old, commit_count);
}

extern int lib_ring_buffer_create(struct lib_ring_buffer *buf,
				  struct channel_backend *chanb, int cpu);
extern void lib_ring_buffer_free(struct lib_ring_buffer *buf);

/* Keep track of trap nesting inside ring buffer code */
DECLARE_PER_CPU(unsigned int, lib_ring_buffer_nesting);

#endif /* _LIB_RING_BUFFER_FRONTEND_INTERNAL_H */
