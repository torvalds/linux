// SPDX-License-Identifier: GPL-2.0-only
/*
 * Tegra host1x Interrupt Management
 *
 * Copyright (c) 2010-2013, NVIDIA Corporation.
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/irq.h>

#include <trace/events/host1x.h>
#include "channel.h"
#include "dev.h"
#include "fence.h"
#include "intr.h"

/* Wait list management */

enum waitlist_state {
	WLS_PENDING,
	WLS_REMOVED,
	WLS_CANCELLED,
	WLS_HANDLED
};

static void waiter_release(struct kref *kref)
{
	kfree(container_of(kref, struct host1x_waitlist, refcount));
}

/*
 * add a waiter to a waiter queue, sorted by threshold
 * returns true if it was added at the head of the queue
 */
static bool add_waiter_to_queue(struct host1x_waitlist *waiter,
				struct list_head *queue)
{
	struct host1x_waitlist *pos;
	u32 thresh = waiter->thresh;

	list_for_each_entry_reverse(pos, queue, list)
		if ((s32)(pos->thresh - thresh) <= 0) {
			list_add(&waiter->list, &pos->list);
			return false;
		}

	list_add(&waiter->list, queue);
	return true;
}

/*
 * run through a waiter queue for a single sync point ID
 * and gather all completed waiters into lists by actions
 */
static void remove_completed_waiters(struct list_head *head, u32 sync,
			struct list_head completed[HOST1X_INTR_ACTION_COUNT])
{
	struct list_head *dest;
	struct host1x_waitlist *waiter, *next, *prev;

	list_for_each_entry_safe(waiter, next, head, list) {
		if ((s32)(waiter->thresh - sync) > 0)
			break;

		dest = completed + waiter->action;

		/* consolidate submit cleanups */
		if (waiter->action == HOST1X_INTR_ACTION_SUBMIT_COMPLETE &&
		    !list_empty(dest)) {
			prev = list_entry(dest->prev,
					  struct host1x_waitlist, list);
			if (prev->data == waiter->data) {
				prev->count++;
				dest = NULL;
			}
		}

		/* PENDING->REMOVED or CANCELLED->HANDLED */
		if (atomic_inc_return(&waiter->state) == WLS_HANDLED || !dest) {
			list_del(&waiter->list);
			kref_put(&waiter->refcount, waiter_release);
		} else
			list_move_tail(&waiter->list, dest);
	}
}

static void reset_threshold_interrupt(struct host1x *host,
				      struct list_head *head,
				      unsigned int id)
{
	u32 thresh =
		list_first_entry(head, struct host1x_waitlist, list)->thresh;

	host1x_hw_intr_set_syncpt_threshold(host, id, thresh);
	host1x_hw_intr_enable_syncpt_intr(host, id);
}

static void action_submit_complete(struct host1x_waitlist *waiter)
{
	struct host1x_channel *channel = waiter->data;

	host1x_cdma_update(&channel->cdma);

	/*  Add nr_completed to trace */
	trace_host1x_channel_submit_complete(dev_name(channel->dev),
					     waiter->count, waiter->thresh);
}

static void action_wakeup(struct host1x_waitlist *waiter)
{
	wait_queue_head_t *wq = waiter->data;

	wake_up(wq);
}

static void action_wakeup_interruptible(struct host1x_waitlist *waiter)
{
	wait_queue_head_t *wq = waiter->data;

	wake_up_interruptible(wq);
}

static void action_signal_fence(struct host1x_waitlist *waiter)
{
	struct host1x_syncpt_fence *f = waiter->data;

	host1x_fence_signal(f);
}

typedef void (*action_handler)(struct host1x_waitlist *waiter);

static const action_handler action_handlers[HOST1X_INTR_ACTION_COUNT] = {
	action_submit_complete,
	action_wakeup,
	action_wakeup_interruptible,
	action_signal_fence,
};

static void run_handlers(struct list_head completed[HOST1X_INTR_ACTION_COUNT])
{
	struct list_head *head = completed;
	unsigned int i;

	for (i = 0; i < HOST1X_INTR_ACTION_COUNT; ++i, ++head) {
		action_handler handler = action_handlers[i];
		struct host1x_waitlist *waiter, *next;

		list_for_each_entry_safe(waiter, next, head, list) {
			list_del(&waiter->list);
			handler(waiter);
			WARN_ON(atomic_xchg(&waiter->state, WLS_HANDLED) !=
				WLS_REMOVED);
			kref_put(&waiter->refcount, waiter_release);
		}
	}
}

/*
 * Remove & handle all waiters that have completed for the given syncpt
 */
static int process_wait_list(struct host1x *host,
			     struct host1x_syncpt *syncpt,
			     u32 threshold)
{
	struct list_head completed[HOST1X_INTR_ACTION_COUNT];
	unsigned int i;
	int empty;

	for (i = 0; i < HOST1X_INTR_ACTION_COUNT; ++i)
		INIT_LIST_HEAD(completed + i);

	spin_lock(&syncpt->intr.lock);

	remove_completed_waiters(&syncpt->intr.wait_head, threshold,
				 completed);

	empty = list_empty(&syncpt->intr.wait_head);
	if (empty)
		host1x_hw_intr_disable_syncpt_intr(host, syncpt->id);
	else
		reset_threshold_interrupt(host, &syncpt->intr.wait_head,
					  syncpt->id);

	spin_unlock(&syncpt->intr.lock);

	run_handlers(completed);

	return empty;
}

/*
 * Sync point threshold interrupt service thread function
 * Handles sync point threshold triggers, in thread context
 */

static void syncpt_thresh_work(struct work_struct *work)
{
	struct host1x_syncpt_intr *syncpt_intr =
		container_of(work, struct host1x_syncpt_intr, work);
	struct host1x_syncpt *syncpt =
		container_of(syncpt_intr, struct host1x_syncpt, intr);
	unsigned int id = syncpt->id;
	struct host1x *host = syncpt->host;

	(void)process_wait_list(host, syncpt,
				host1x_syncpt_load(host->syncpt + id));
}

int host1x_intr_add_action(struct host1x *host, struct host1x_syncpt *syncpt,
			   u32 thresh, enum host1x_intr_action action,
			   void *data, struct host1x_waitlist *waiter,
			   void **ref)
{
	int queue_was_empty;

	if (waiter == NULL) {
		pr_warn("%s: NULL waiter\n", __func__);
		return -EINVAL;
	}

	/* initialize a new waiter */
	INIT_LIST_HEAD(&waiter->list);
	kref_init(&waiter->refcount);
	if (ref)
		kref_get(&waiter->refcount);
	waiter->thresh = thresh;
	waiter->action = action;
	atomic_set(&waiter->state, WLS_PENDING);
	waiter->data = data;
	waiter->count = 1;

	spin_lock(&syncpt->intr.lock);

	queue_was_empty = list_empty(&syncpt->intr.wait_head);

	if (add_waiter_to_queue(waiter, &syncpt->intr.wait_head)) {
		/* added at head of list - new threshold value */
		host1x_hw_intr_set_syncpt_threshold(host, syncpt->id, thresh);

		/* added as first waiter - enable interrupt */
		if (queue_was_empty)
			host1x_hw_intr_enable_syncpt_intr(host, syncpt->id);
	}

	if (ref)
		*ref = waiter;

	spin_unlock(&syncpt->intr.lock);

	return 0;
}

void host1x_intr_put_ref(struct host1x *host, unsigned int id, void *ref,
			 bool flush)
{
	struct host1x_waitlist *waiter = ref;
	struct host1x_syncpt *syncpt;

	atomic_cmpxchg(&waiter->state, WLS_PENDING, WLS_CANCELLED);

	syncpt = host->syncpt + id;

	spin_lock(&syncpt->intr.lock);
	if (atomic_cmpxchg(&waiter->state, WLS_CANCELLED, WLS_HANDLED) ==
	    WLS_CANCELLED) {
		list_del(&waiter->list);
		kref_put(&waiter->refcount, waiter_release);
	}
	spin_unlock(&syncpt->intr.lock);

	if (flush) {
		/* Wait until any concurrently executing handler has finished. */
		while (atomic_read(&waiter->state) != WLS_HANDLED)
			schedule();
	}

	kref_put(&waiter->refcount, waiter_release);
}

int host1x_intr_init(struct host1x *host, unsigned int irq_sync)
{
	unsigned int id;
	u32 nb_pts = host1x_syncpt_nb_pts(host);

	mutex_init(&host->intr_mutex);
	host->intr_syncpt_irq = irq_sync;

	for (id = 0; id < nb_pts; ++id) {
		struct host1x_syncpt *syncpt = host->syncpt + id;

		spin_lock_init(&syncpt->intr.lock);
		INIT_LIST_HEAD(&syncpt->intr.wait_head);
		snprintf(syncpt->intr.thresh_irq_name,
			 sizeof(syncpt->intr.thresh_irq_name),
			 "host1x_sp_%02u", id);
	}

	host1x_intr_start(host);

	return 0;
}

void host1x_intr_deinit(struct host1x *host)
{
	host1x_intr_stop(host);
}

void host1x_intr_start(struct host1x *host)
{
	u32 hz = clk_get_rate(host->clk);
	int err;

	mutex_lock(&host->intr_mutex);
	err = host1x_hw_intr_init_host_sync(host, DIV_ROUND_UP(hz, 1000000),
					    syncpt_thresh_work);
	if (err) {
		mutex_unlock(&host->intr_mutex);
		return;
	}
	mutex_unlock(&host->intr_mutex);
}

void host1x_intr_stop(struct host1x *host)
{
	unsigned int id;
	struct host1x_syncpt *syncpt = host->syncpt;
	u32 nb_pts = host1x_syncpt_nb_pts(host);

	mutex_lock(&host->intr_mutex);

	host1x_hw_intr_disable_all_syncpt_intrs(host);

	for (id = 0; id < nb_pts; ++id) {
		struct host1x_waitlist *waiter, *next;

		list_for_each_entry_safe(waiter, next,
			&syncpt[id].intr.wait_head, list) {
			if (atomic_cmpxchg(&waiter->state,
			    WLS_CANCELLED, WLS_HANDLED) == WLS_CANCELLED) {
				list_del(&waiter->list);
				kref_put(&waiter->refcount, waiter_release);
			}
		}

		if (!list_empty(&syncpt[id].intr.wait_head)) {
			/* output diagnostics */
			mutex_unlock(&host->intr_mutex);
			pr_warn("%s cannot stop syncpt intr id=%u\n",
				__func__, id);
			return;
		}
	}

	host1x_hw_intr_free_syncpt_irq(host);

	mutex_unlock(&host->intr_mutex);
}
