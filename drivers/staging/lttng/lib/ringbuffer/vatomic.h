#ifndef _LINUX_RING_BUFFER_VATOMIC_H
#define _LINUX_RING_BUFFER_VATOMIC_H

/*
 * linux/ringbuffer/vatomic.h
 *
 * Copyright (C) 2010 - Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * Dual LGPL v2.1/GPL v2 license.
 */

#include <asm/atomic.h>
#include <asm/local.h>

/*
 * Same data type (long) accessed differently depending on configuration.
 * v field is for non-atomic access (protected by mutual exclusion).
 * In the fast-path, the ring_buffer_config structure is constant, so the
 * compiler can statically select the appropriate branch.
 * local_t is used for per-cpu and per-thread buffers.
 * atomic_long_t is used for globally shared buffers.
 */
union v_atomic {
	local_t l;
	atomic_long_t a;
	long v;
};

static inline
long v_read(const struct lib_ring_buffer_config *config, union v_atomic *v_a)
{
	if (config->sync == RING_BUFFER_SYNC_PER_CPU)
		return local_read(&v_a->l);
	else
		return atomic_long_read(&v_a->a);
}

static inline
void v_set(const struct lib_ring_buffer_config *config, union v_atomic *v_a,
	   long v)
{
	if (config->sync == RING_BUFFER_SYNC_PER_CPU)
		local_set(&v_a->l, v);
	else
		atomic_long_set(&v_a->a, v);
}

static inline
void v_add(const struct lib_ring_buffer_config *config, long v, union v_atomic *v_a)
{
	if (config->sync == RING_BUFFER_SYNC_PER_CPU)
		local_add(v, &v_a->l);
	else
		atomic_long_add(v, &v_a->a);
}

static inline
void v_inc(const struct lib_ring_buffer_config *config, union v_atomic *v_a)
{
	if (config->sync == RING_BUFFER_SYNC_PER_CPU)
		local_inc(&v_a->l);
	else
		atomic_long_inc(&v_a->a);
}

/*
 * Non-atomic decrement. Only used by reader, apply to reader-owned subbuffer.
 */
static inline
void _v_dec(const struct lib_ring_buffer_config *config, union v_atomic *v_a)
{
	--v_a->v;
}

static inline
long v_cmpxchg(const struct lib_ring_buffer_config *config, union v_atomic *v_a,
	       long old, long _new)
{
	if (config->sync == RING_BUFFER_SYNC_PER_CPU)
		return local_cmpxchg(&v_a->l, old, _new);
	else
		return atomic_long_cmpxchg(&v_a->a, old, _new);
}

#endif /* _LINUX_RING_BUFFER_VATOMIC_H */
