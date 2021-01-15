/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef __INTEL_ENGINE_STATS_H__
#define __INTEL_ENGINE_STATS_H__

#include <linux/atomic.h>
#include <linux/ktime.h>
#include <linux/seqlock.h>

#include "i915_gem.h" /* GEM_BUG_ON */
#include "intel_engine.h"

static inline void intel_engine_context_in(struct intel_engine_cs *engine)
{
	unsigned long flags;

	if (atomic_add_unless(&engine->stats.active, 1, 0))
		return;

	write_seqlock_irqsave(&engine->stats.lock, flags);
	if (!atomic_add_unless(&engine->stats.active, 1, 0)) {
		engine->stats.start = ktime_get();
		atomic_inc(&engine->stats.active);
	}
	write_sequnlock_irqrestore(&engine->stats.lock, flags);
}

static inline void intel_engine_context_out(struct intel_engine_cs *engine)
{
	unsigned long flags;

	GEM_BUG_ON(!atomic_read(&engine->stats.active));

	if (atomic_add_unless(&engine->stats.active, -1, 1))
		return;

	write_seqlock_irqsave(&engine->stats.lock, flags);
	if (atomic_dec_and_test(&engine->stats.active)) {
		engine->stats.total =
			ktime_add(engine->stats.total,
				  ktime_sub(ktime_get(), engine->stats.start));
	}
	write_sequnlock_irqrestore(&engine->stats.lock, flags);
}

#endif /* __INTEL_ENGINE_STATS_H__ */
