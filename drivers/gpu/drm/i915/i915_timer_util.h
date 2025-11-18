/* SPDX-License-Identifier: MIT */
/* Copyright © 2025 Intel Corporation */

#ifndef __I915_TIMER_UTIL_H__
#define __I915_TIMER_UTIL_H__

#include <linux/timer.h>
#include <asm/rwonce.h>

void cancel_timer(struct timer_list *t);
void set_timer_ms(struct timer_list *t, unsigned long timeout);

static inline bool timer_active(const struct timer_list *t)
{
	return READ_ONCE(t->expires);
}

static inline bool timer_expired(const struct timer_list *t)
{
	return timer_active(t) && !timer_pending(t);
}

#endif /* __I915_TIMER_UTIL_H__ */
