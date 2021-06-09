/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef INTEL_ENGINE_PM_H
#define INTEL_ENGINE_PM_H

#include "i915_request.h"
#include "intel_engine_types.h"
#include "intel_wakeref.h"

static inline bool
intel_engine_pm_is_awake(const struct intel_engine_cs *engine)
{
	return intel_wakeref_is_active(&engine->wakeref);
}

static inline void intel_engine_pm_get(struct intel_engine_cs *engine)
{
	intel_wakeref_get(&engine->wakeref);
}

static inline bool intel_engine_pm_get_if_awake(struct intel_engine_cs *engine)
{
	return intel_wakeref_get_if_active(&engine->wakeref);
}

static inline void intel_engine_pm_put(struct intel_engine_cs *engine)
{
	intel_wakeref_put(&engine->wakeref);
}

static inline void intel_engine_pm_put_async(struct intel_engine_cs *engine)
{
	intel_wakeref_put_async(&engine->wakeref);
}

static inline void intel_engine_pm_put_delay(struct intel_engine_cs *engine,
					     unsigned long delay)
{
	intel_wakeref_put_delay(&engine->wakeref, delay);
}

static inline void intel_engine_pm_flush(struct intel_engine_cs *engine)
{
	intel_wakeref_unlock_wait(&engine->wakeref);
}

static inline struct i915_request *
intel_engine_create_kernel_request(struct intel_engine_cs *engine)
{
	struct i915_request *rq;

	/*
	 * The engine->kernel_context is special as it is used inside
	 * the engine-pm barrier (see __engine_park()), circumventing
	 * the usual mutexes and relying on the engine-pm barrier
	 * instead. So whenever we use the engine->kernel_context
	 * outside of the barrier, we must manually handle the
	 * engine wakeref to serialise with the use inside.
	 */
	intel_engine_pm_get(engine);
	rq = i915_request_create(engine->kernel_context);
	intel_engine_pm_put(engine);

	return rq;
}

void intel_engine_init__pm(struct intel_engine_cs *engine);

#endif /* INTEL_ENGINE_PM_H */
