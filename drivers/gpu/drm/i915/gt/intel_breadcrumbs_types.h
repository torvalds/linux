/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_BREADCRUMBS_TYPES__
#define __INTEL_BREADCRUMBS_TYPES__

#include <linux/irq_work.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/types.h>

/*
 * Rather than have every client wait upon all user interrupts,
 * with the herd waking after every interrupt and each doing the
 * heavyweight seqno dance, we delegate the task (of being the
 * bottom-half of the user interrupt) to the first client. After
 * every interrupt, we wake up one client, who does the heavyweight
 * coherent seqno read and either goes back to sleep (if incomplete),
 * or wakes up all the completed clients in parallel, before then
 * transferring the bottom-half status to the next client in the queue.
 *
 * Compared to walking the entire list of waiters in a single dedicated
 * bottom-half, we reduce the latency of the first waiter by avoiding
 * a context switch, but incur additional coherent seqno reads when
 * following the chain of request breadcrumbs. Since it is most likely
 * that we have a single client waiting on each seqno, then reducing
 * the overhead of waking that client is much preferred.
 */
struct intel_breadcrumbs {
	spinlock_t irq_lock; /* protects the lists used in hardirq context */

	/* Not all breadcrumbs are attached to physical HW */
	struct intel_engine_cs *irq_engine;

	struct list_head signalers;
	struct llist_head signaled_requests;

	struct irq_work irq_work; /* for use from inside irq_lock */

	unsigned int irq_enabled;

	bool irq_armed;
};

#endif /* __INTEL_BREADCRUMBS_TYPES__ */
