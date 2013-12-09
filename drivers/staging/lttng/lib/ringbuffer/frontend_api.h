#ifndef _LIB_RING_BUFFER_FRONTEND_API_H
#define _LIB_RING_BUFFER_FRONTEND_API_H

/*
 * lib/ringbuffer/frontend_api.h
 *
 * Ring Buffer Library Synchronization Header (buffer write API).
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
 * See linux/ringbuffer/frontend.h for channel allocation and read-side API.
 */

#include "../../wrapper/ringbuffer/frontend.h"
#include <linux/errno.h>
#include <linux/prefetch.h>

/**
 * lib_ring_buffer_get_cpu - Precedes ring buffer reserve/commit.
 *
 * Disables preemption (acts as a RCU read-side critical section) and keeps a
 * ring buffer nesting count as supplementary safety net to ensure tracer client
 * code will never trigger an endless recursion. Returns the processor ID on
 * success, -EPERM on failure (nesting count too high).
 *
 * asm volatile and "memory" clobber prevent the compiler from moving
 * instructions out of the ring buffer nesting count. This is required to ensure
 * that probe side-effects which can cause recursion (e.g. unforeseen traps,
 * divisions by 0, ...) are triggered within the incremented nesting count
 * section.
 */
static inline
int lib_ring_buffer_get_cpu(const struct lib_ring_buffer_config *config)
{
	int cpu, nesting;

	rcu_read_lock_sched_notrace();
	cpu = smp_processor_id();
	nesting = ++per_cpu(lib_ring_buffer_nesting, cpu);
	barrier();

	if (unlikely(nesting > 4)) {
		WARN_ON_ONCE(1);
		per_cpu(lib_ring_buffer_nesting, cpu)--;
		rcu_read_unlock_sched_notrace();
		return -EPERM;
	} else
		return cpu;
}

/**
 * lib_ring_buffer_put_cpu - Follows ring buffer reserve/commit.
 */
static inline
void lib_ring_buffer_put_cpu(const struct lib_ring_buffer_config *config)
{
	barrier();
	__get_cpu_var(lib_ring_buffer_nesting)--;
	rcu_read_unlock_sched_notrace();
}

/*
 * lib_ring_buffer_try_reserve is called by lib_ring_buffer_reserve(). It is not
 * part of the API per se.
 *
 * returns 0 if reserve ok, or 1 if the slow path must be taken.
 */
static inline
int lib_ring_buffer_try_reserve(const struct lib_ring_buffer_config *config,
				struct lib_ring_buffer_ctx *ctx,
				unsigned long *o_begin, unsigned long *o_end,
				unsigned long *o_old, size_t *before_hdr_pad)
{
	struct channel *chan = ctx->chan;
	struct lib_ring_buffer *buf = ctx->buf;
	*o_begin = v_read(config, &buf->offset);
	*o_old = *o_begin;

	ctx->tsc = lib_ring_buffer_clock_read(chan);
	if ((int64_t) ctx->tsc == -EIO)
		return 1;

	/*
	 * Prefetch cacheline for read because we have to read the previous
	 * commit counter to increment it and commit seq value to compare it to
	 * the commit counter.
	 */
	prefetch(&buf->commit_hot[subbuf_index(*o_begin, chan)]);

	if (last_tsc_overflow(config, buf, ctx->tsc))
		ctx->rflags |= RING_BUFFER_RFLAG_FULL_TSC;

	if (unlikely(subbuf_offset(*o_begin, chan) == 0))
		return 1;

	ctx->slot_size = record_header_size(config, chan, *o_begin,
					    before_hdr_pad, ctx);
	ctx->slot_size +=
		lib_ring_buffer_align(*o_begin + ctx->slot_size,
				      ctx->largest_align) + ctx->data_size;
	if (unlikely((subbuf_offset(*o_begin, chan) + ctx->slot_size)
		     > chan->backend.subbuf_size))
		return 1;

	/*
	 * Record fits in the current buffer and we are not on a switch
	 * boundary. It's safe to write.
	 */
	*o_end = *o_begin + ctx->slot_size;

	if (unlikely((subbuf_offset(*o_end, chan)) == 0))
		/*
		 * The offset_end will fall at the very beginning of the next
		 * subbuffer.
		 */
		return 1;

	return 0;
}

/**
 * lib_ring_buffer_reserve - Reserve space in a ring buffer.
 * @config: ring buffer instance configuration.
 * @ctx: ring buffer context. (input and output) Must be already initialized.
 *
 * Atomic wait-free slot reservation. The reserved space starts at the context
 * "pre_offset". Its length is "slot_size". The associated time-stamp is "tsc".
 *
 * Return :
 *  0 on success.
 * -EAGAIN if channel is disabled.
 * -ENOSPC if event size is too large for packet.
 * -ENOBUFS if there is currently not enough space in buffer for the event.
 * -EIO if data cannot be written into the buffer for any other reason.
 */

static inline
int lib_ring_buffer_reserve(const struct lib_ring_buffer_config *config,
			    struct lib_ring_buffer_ctx *ctx)
{
	struct channel *chan = ctx->chan;
	struct lib_ring_buffer *buf;
	unsigned long o_begin, o_end, o_old;
	size_t before_hdr_pad = 0;

	if (atomic_read(&chan->record_disabled))
		return -EAGAIN;

	if (config->alloc == RING_BUFFER_ALLOC_PER_CPU)
		buf = per_cpu_ptr(chan->backend.buf, ctx->cpu);
	else
		buf = chan->backend.buf;
	if (atomic_read(&buf->record_disabled))
		return -EAGAIN;
	ctx->buf = buf;

	/*
	 * Perform retryable operations.
	 */
	if (unlikely(lib_ring_buffer_try_reserve(config, ctx, &o_begin,
						 &o_end, &o_old, &before_hdr_pad)))
		goto slow_path;

	if (unlikely(v_cmpxchg(config, &ctx->buf->offset, o_old, o_end)
		     != o_old))
		goto slow_path;

	/*
	 * Atomically update last_tsc. This update races against concurrent
	 * atomic updates, but the race will always cause supplementary full TSC
	 * record headers, never the opposite (missing a full TSC record header
	 * when it would be needed).
	 */
	save_last_tsc(config, ctx->buf, ctx->tsc);

	/*
	 * Push the reader if necessary
	 */
	lib_ring_buffer_reserve_push_reader(ctx->buf, chan, o_end - 1);

	/*
	 * Clear noref flag for this subbuffer.
	 */
	lib_ring_buffer_clear_noref(config, &ctx->buf->backend,
				subbuf_index(o_end - 1, chan));

	ctx->pre_offset = o_begin;
	ctx->buf_offset = o_begin + before_hdr_pad;
	return 0;
slow_path:
	return lib_ring_buffer_reserve_slow(ctx);
}

/**
 * lib_ring_buffer_switch - Perform a sub-buffer switch for a per-cpu buffer.
 * @config: ring buffer instance configuration.
 * @buf: buffer
 * @mode: buffer switch mode (SWITCH_ACTIVE or SWITCH_FLUSH)
 *
 * This operation is completely reentrant : can be called while tracing is
 * active with absolutely no lock held.
 *
 * Note, however, that as a v_cmpxchg is used for some atomic operations and
 * requires to be executed locally for per-CPU buffers, this function must be
 * called from the CPU which owns the buffer for a ACTIVE flush, with preemption
 * disabled, for RING_BUFFER_SYNC_PER_CPU configuration.
 */
static inline
void lib_ring_buffer_switch(const struct lib_ring_buffer_config *config,
			    struct lib_ring_buffer *buf, enum switch_mode mode)
{
	lib_ring_buffer_switch_slow(buf, mode);
}

/* See ring_buffer_frontend_api.h for lib_ring_buffer_reserve(). */

/**
 * lib_ring_buffer_commit - Commit an record.
 * @config: ring buffer instance configuration.
 * @ctx: ring buffer context. (input arguments only)
 *
 * Atomic unordered slot commit. Increments the commit count in the
 * specified sub-buffer, and delivers it if necessary.
 */
static inline
void lib_ring_buffer_commit(const struct lib_ring_buffer_config *config,
			    const struct lib_ring_buffer_ctx *ctx)
{
	struct channel *chan = ctx->chan;
	struct lib_ring_buffer *buf = ctx->buf;
	unsigned long offset_end = ctx->buf_offset;
	unsigned long endidx = subbuf_index(offset_end - 1, chan);
	unsigned long commit_count;

	/*
	 * Must count record before incrementing the commit count.
	 */
	subbuffer_count_record(config, &buf->backend, endidx);

	/*
	 * Order all writes to buffer before the commit count update that will
	 * determine that the subbuffer is full.
	 */
	if (config->ipi == RING_BUFFER_IPI_BARRIER) {
		/*
		 * Must write slot data before incrementing commit count.  This
		 * compiler barrier is upgraded into a smp_mb() by the IPI sent
		 * by get_subbuf().
		 */
		barrier();
	} else
		smp_wmb();

	v_add(config, ctx->slot_size, &buf->commit_hot[endidx].cc);

	/*
	 * commit count read can race with concurrent OOO commit count updates.
	 * This is only needed for lib_ring_buffer_check_deliver (for
	 * non-polling delivery only) and for
	 * lib_ring_buffer_write_commit_counter.  The race can only cause the
	 * counter to be read with the same value more than once, which could
	 * cause :
	 * - Multiple delivery for the same sub-buffer (which is handled
	 *   gracefully by the reader code) if the value is for a full
	 *   sub-buffer. It's important that we can never miss a sub-buffer
	 *   delivery. Re-reading the value after the v_add ensures this.
	 * - Reading a commit_count with a higher value that what was actually
	 *   added to it for the lib_ring_buffer_write_commit_counter call
	 *   (again caused by a concurrent committer). It does not matter,
	 *   because this function is interested in the fact that the commit
	 *   count reaches back the reserve offset for a specific sub-buffer,
	 *   which is completely independent of the order.
	 */
	commit_count = v_read(config, &buf->commit_hot[endidx].cc);

	lib_ring_buffer_check_deliver(config, buf, chan, offset_end - 1,
				      commit_count, endidx, ctx->tsc);
	/*
	 * Update used size at each commit. It's needed only for extracting
	 * ring_buffer buffers from vmcore, after crash.
	 */
	lib_ring_buffer_write_commit_counter(config, buf, chan, endidx,
					     ctx->buf_offset, commit_count,
					 ctx->slot_size);
}

/**
 * lib_ring_buffer_try_discard_reserve - Try discarding a record.
 * @config: ring buffer instance configuration.
 * @ctx: ring buffer context. (input arguments only)
 *
 * Only succeeds if no other record has been written after the record to
 * discard. If discard fails, the record must be committed to the buffer.
 *
 * Returns 0 upon success, -EPERM if the record cannot be discarded.
 */
static inline
int lib_ring_buffer_try_discard_reserve(const struct lib_ring_buffer_config *config,
					const struct lib_ring_buffer_ctx *ctx)
{
	struct lib_ring_buffer *buf = ctx->buf;
	unsigned long end_offset = ctx->pre_offset + ctx->slot_size;

	/*
	 * We need to ensure that if the cmpxchg succeeds and discards the
	 * record, the next record will record a full TSC, because it cannot
	 * rely on the last_tsc associated with the discarded record to detect
	 * overflows. The only way to ensure this is to set the last_tsc to 0
	 * (assuming no 64-bit TSC overflow), which forces to write a 64-bit
	 * timestamp in the next record.
	 *
	 * Note: if discard fails, we must leave the TSC in the record header.
	 * It is needed to keep track of TSC overflows for the following
	 * records.
	 */
	save_last_tsc(config, buf, 0ULL);

	if (likely(v_cmpxchg(config, &buf->offset, end_offset, ctx->pre_offset)
		   != end_offset))
		return -EPERM;
	else
		return 0;
}

static inline
void channel_record_disable(const struct lib_ring_buffer_config *config,
			    struct channel *chan)
{
	atomic_inc(&chan->record_disabled);
}

static inline
void channel_record_enable(const struct lib_ring_buffer_config *config,
			   struct channel *chan)
{
	atomic_dec(&chan->record_disabled);
}

static inline
void lib_ring_buffer_record_disable(const struct lib_ring_buffer_config *config,
				    struct lib_ring_buffer *buf)
{
	atomic_inc(&buf->record_disabled);
}

static inline
void lib_ring_buffer_record_enable(const struct lib_ring_buffer_config *config,
				   struct lib_ring_buffer *buf)
{
	atomic_dec(&buf->record_disabled);
}

#endif /* _LIB_RING_BUFFER_FRONTEND_API_H */
