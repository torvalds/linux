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
	struct intel_engine_execlists_stats *stats = &engine->stats.execlists;
	unsigned long flags;

	if (stats->active) {
		stats->active++;
		return;
	}

	/* The writer is serialised; but the pmu reader may be from hardirq */
	local_irq_save(flags);
	write_seqcount_begin(&stats->lock);

	stats->start = ktime_get();
	stats->active++;

	write_seqcount_end(&stats->lock);
	local_irq_restore(flags);

	GEM_BUG_ON(!stats->active);
}

static inline void intel_engine_context_out(struct intel_engine_cs *engine)
{
	struct intel_engine_execlists_stats *stats = &engine->stats.execlists;
	unsigned long flags;

	GEM_BUG_ON(!stats->active);
	if (stats->active > 1) {
		stats->active--;
		return;
	}

	local_irq_save(flags);
	write_seqcount_begin(&stats->lock);

	stats->active--;
	stats->total = ktime_add(stats->total,
				 ktime_sub(ktime_get(), stats->start));

	write_seqcount_end(&stats->lock);
	local_irq_restore(flags);
}

#endif /* __INTEL_ENGINE_STATS_H__ */
