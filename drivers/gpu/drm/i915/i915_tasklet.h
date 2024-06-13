/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __I915_TASKLET_H__
#define __I915_TASKLET_H__

#include <linux/interrupt.h>

static inline void tasklet_lock(struct tasklet_struct *t)
{
	while (!tasklet_trylock(t))
		cpu_relax();
}

static inline bool tasklet_is_locked(const struct tasklet_struct *t)
{
	return test_bit(TASKLET_STATE_RUN, &t->state);
}

static inline void __tasklet_disable_sync_once(struct tasklet_struct *t)
{
	if (!atomic_fetch_inc(&t->count))
		tasklet_unlock_spin_wait(t);
}

static inline bool __tasklet_is_enabled(const struct tasklet_struct *t)
{
	return !atomic_read(&t->count);
}

static inline bool __tasklet_enable(struct tasklet_struct *t)
{
	return atomic_dec_and_test(&t->count);
}

static inline bool __tasklet_is_scheduled(struct tasklet_struct *t)
{
	return test_bit(TASKLET_STATE_SCHED, &t->state);
}

#endif /* __I915_TASKLET_H__ */
