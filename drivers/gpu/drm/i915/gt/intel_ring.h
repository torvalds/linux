/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#ifndef INTEL_RING_H
#define INTEL_RING_H

#include "i915_gem.h" /* GEM_BUG_ON */
#include "i915_request.h"
#include "intel_ring_types.h"

struct intel_engine_cs;

struct intel_ring *
intel_engine_create_ring(struct intel_engine_cs *engine, int size);

u32 *intel_ring_begin(struct i915_request *rq, unsigned int num_dwords);
int intel_ring_cacheline_align(struct i915_request *rq);

unsigned int intel_ring_update_space(struct intel_ring *ring);

void __intel_ring_pin(struct intel_ring *ring);
int intel_ring_pin(struct intel_ring *ring, struct i915_gem_ww_ctx *ww);
void intel_ring_unpin(struct intel_ring *ring);
void intel_ring_reset(struct intel_ring *ring, u32 tail);

void intel_ring_free(struct kref *ref);

static inline struct intel_ring *intel_ring_get(struct intel_ring *ring)
{
	kref_get(&ring->ref);
	return ring;
}

static inline void intel_ring_put(struct intel_ring *ring)
{
	kref_put(&ring->ref, intel_ring_free);
}

static inline void intel_ring_advance(struct i915_request *rq, u32 *cs)
{
	/* Dummy function.
	 *
	 * This serves as a placeholder in the code so that the reader
	 * can compare against the preceding intel_ring_begin() and
	 * check that the number of dwords emitted matches the space
	 * reserved for the command packet (i.e. the value passed to
	 * intel_ring_begin()).
	 */
	GEM_BUG_ON((rq->ring->vaddr + rq->ring->emit) != cs);
}

static inline u32 intel_ring_wrap(const struct intel_ring *ring, u32 pos)
{
	return pos & (ring->size - 1);
}

static inline int intel_ring_direction(const struct intel_ring *ring,
				       u32 next, u32 prev)
{
	typecheck(typeof(ring->size), next);
	typecheck(typeof(ring->size), prev);
	return (next - prev) << ring->wrap;
}

static inline bool
intel_ring_offset_valid(const struct intel_ring *ring,
			unsigned int pos)
{
	if (pos & -ring->size) /* must be strictly within the ring */
		return false;

	if (!IS_ALIGNED(pos, 8)) /* must be qword aligned */
		return false;

	return true;
}

static inline u32 intel_ring_offset(const struct i915_request *rq, void *addr)
{
	/* Don't write ring->size (equivalent to 0) as that hangs some GPUs. */
	u32 offset = addr - rq->ring->vaddr;
	GEM_BUG_ON(offset > rq->ring->size);
	return intel_ring_wrap(rq->ring, offset);
}

static inline void
assert_ring_tail_valid(const struct intel_ring *ring, unsigned int tail)
{
	unsigned int head = READ_ONCE(ring->head);

	GEM_BUG_ON(!intel_ring_offset_valid(ring, tail));

	/*
	 * "Ring Buffer Use"
	 *	Gen2 BSpec "1. Programming Environment" / 1.4.4.6
	 *	Gen3 BSpec "1c Memory Interface Functions" / 2.3.4.5
	 *	Gen4+ BSpec "1c Memory Interface and Command Stream" / 5.3.4.5
	 * "If the Ring Buffer Head Pointer and the Tail Pointer are on the
	 * same cacheline, the Head Pointer must not be greater than the Tail
	 * Pointer."
	 *
	 * We use ring->head as the last known location of the actual RING_HEAD,
	 * it may have advanced but in the worst case it is equally the same
	 * as ring->head and so we should never program RING_TAIL to advance
	 * into the same cacheline as ring->head.
	 */
#define cacheline(a) round_down(a, CACHELINE_BYTES)
	GEM_BUG_ON(cacheline(tail) == cacheline(head) && tail < head);
#undef cacheline
}

static inline unsigned int
intel_ring_set_tail(struct intel_ring *ring, unsigned int tail)
{
	/* Whilst writes to the tail are strictly order, there is no
	 * serialisation between readers and the writers. The tail may be
	 * read by i915_request_retire() just as it is being updated
	 * by execlists, as although the breadcrumb is complete, the context
	 * switch hasn't been seen.
	 */
	assert_ring_tail_valid(ring, tail);
	ring->tail = tail;
	return tail;
}

static inline unsigned int
__intel_ring_space(unsigned int head, unsigned int tail, unsigned int size)
{
	/*
	 * "If the Ring Buffer Head Pointer and the Tail Pointer are on the
	 * same cacheline, the Head Pointer must not be greater than the Tail
	 * Pointer."
	 */
	GEM_BUG_ON(!is_power_of_2(size));
	return (head - tail - CACHELINE_BYTES) & (size - 1);
}

#endif /* INTEL_RING_H */
