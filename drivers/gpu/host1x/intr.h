/*
 * Tegra host1x Interrupt Management
 *
 * Copyright (c) 2010-2013, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __HOST1X_INTR_H
#define __HOST1X_INTR_H

#include <linux/interrupt.h>
#include <linux/workqueue.h>

struct host1x;

enum host1x_intr_action {
	/*
	 * Perform cleanup after a submit has completed.
	 * 'data' points to a channel
	 */
	HOST1X_INTR_ACTION_SUBMIT_COMPLETE = 0,

	/*
	 * Wake up a  task.
	 * 'data' points to a wait_queue_head_t
	 */
	HOST1X_INTR_ACTION_WAKEUP,

	/*
	 * Wake up a interruptible task.
	 * 'data' points to a wait_queue_head_t
	 */
	HOST1X_INTR_ACTION_WAKEUP_INTERRUPTIBLE,

	HOST1X_INTR_ACTION_COUNT
};

struct host1x_syncpt_intr {
	spinlock_t lock;
	struct list_head wait_head;
	char thresh_irq_name[12];
	struct work_struct work;
};

struct host1x_waitlist {
	struct list_head list;
	struct kref refcount;
	u32 thresh;
	enum host1x_intr_action action;
	atomic_t state;
	void *data;
	int count;
};

/*
 * Schedule an action to be taken when a sync point reaches the given threshold.
 *
 * @id the sync point
 * @thresh the threshold
 * @action the action to take
 * @data a pointer to extra data depending on action, see above
 * @waiter waiter structure - assumes ownership
 * @ref must be passed if cancellation is possible, else NULL
 *
 * This is a non-blocking api.
 */
int host1x_intr_add_action(struct host1x *host, u32 id, u32 thresh,
	enum host1x_intr_action action, void *data,
	struct host1x_waitlist *waiter, void **ref);

/*
 * Unreference an action submitted to host1x_intr_add_action().
 * You must call this if you passed non-NULL as ref.
 * @ref the ref returned from host1x_intr_add_action()
 */
void host1x_intr_put_ref(struct host1x *host, u32 id, void *ref);

/* Initialize host1x sync point interrupt */
int host1x_intr_init(struct host1x *host, unsigned int irq_sync);

/* Deinitialize host1x sync point interrupt */
void host1x_intr_deinit(struct host1x *host);

/* Enable host1x sync point interrupt */
void host1x_intr_start(struct host1x *host);

/* Disable host1x sync point interrupt */
void host1x_intr_stop(struct host1x *host);

irqreturn_t host1x_syncpt_thresh_fn(void *dev_id);
#endif
