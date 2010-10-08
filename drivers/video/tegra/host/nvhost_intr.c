/*
 * drivers/video/tegra/host/nvhost_intr.c
 *
 * Tegra Graphics Host Interrupt Management
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "nvhost_intr.h"
#include "dev.h"
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/irq.h>

#define intr_to_dev(x) container_of(x, struct nvhost_master, intr)


/*** HW sync point threshold interrupt management ***/

static void set_syncpt_threshold(void __iomem *sync_regs, u32 id, u32 thresh)
{
	thresh &= 0xffff;
	writel(thresh, sync_regs + (HOST1X_SYNC_SYNCPT_INT_THRESH_0 + id * 4));
}

static void enable_syncpt_interrupt(void __iomem *sync_regs, u32 id)
{
	writel(BIT(id),	sync_regs + HOST1X_SYNC_SYNCPT_THRESH_INT_ENABLE_CPU0);
}


/*** Wait list management ***/

struct nvhost_waitlist {
	struct list_head list;
	struct kref refcount;
	u32 thresh;
	enum nvhost_intr_action action;
	atomic_t state;
	void *data;
	int count;
};

enum waitlist_state
{
	WLS_PENDING,
	WLS_REMOVED,
	WLS_CANCELLED,
	WLS_HANDLED
};

static void waiter_release(struct kref *kref)
{
	kfree(container_of(kref, struct nvhost_waitlist, refcount));
}

/*
 * add a waiter to a waiter queue, sorted by threshold
 * returns true if it was added at the head of the queue
 */
static bool add_waiter_to_queue(struct nvhost_waitlist *waiter,
				struct list_head *queue)
{
	struct nvhost_waitlist *pos;
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
			struct list_head completed[NVHOST_INTR_ACTION_COUNT])
{
	struct list_head *dest;
	struct nvhost_waitlist *waiter, *next, *prev;

	list_for_each_entry_safe(waiter, next, head, list) {
		if ((s32)(waiter->thresh - sync) > 0)
			break;

		dest = completed + waiter->action;

		/* consolidate submit cleanups */
		if (waiter->action == NVHOST_INTR_ACTION_SUBMIT_COMPLETE
			&& !list_empty(dest)) {
			prev = list_entry(dest->prev,
					struct nvhost_waitlist, list);
			if (prev->data == waiter->data) {
				prev->count++;
				dest = NULL;
			}
		}

		/* PENDING->REMOVED or CANCELLED->HANDLED */
		if (atomic_inc_return(&waiter->state) == WLS_HANDLED || !dest) {
			list_del(&waiter->list);
			kref_put(&waiter->refcount, waiter_release);
		} else {
			list_move_tail(&waiter->list, dest);
		}
	}
}

static void action_submit_complete(struct nvhost_waitlist *waiter)
{
	struct nvhost_channel *channel = waiter->data;
	int nr_completed = waiter->count;

	nvhost_cdma_update(&channel->cdma);
	nvhost_module_idle_mult(&channel->mod, nr_completed);
}

static void action_ctxsave(struct nvhost_waitlist *waiter)
{
	struct nvhost_hwctx *hwctx = waiter->data;
	struct nvhost_channel *channel = hwctx->channel;

	channel->ctxhandler.save_service(hwctx);
	channel->ctxhandler.put(hwctx);
}

static void action_wakeup(struct nvhost_waitlist *waiter)
{
	wait_queue_head_t *wq = waiter->data;

	wake_up(wq);
}

static void action_wakeup_interruptible(struct nvhost_waitlist *waiter)
{
	wait_queue_head_t *wq = waiter->data;

	wake_up_interruptible(wq);
}

typedef void (*action_handler)(struct nvhost_waitlist *waiter);

static action_handler action_handlers[NVHOST_INTR_ACTION_COUNT] = {
	action_submit_complete,
	action_ctxsave,
	action_wakeup,
	action_wakeup_interruptible,
};

static void run_handlers(struct list_head completed[NVHOST_INTR_ACTION_COUNT])
{
	struct list_head *head = completed;
	int i;

	for (i = 0; i < NVHOST_INTR_ACTION_COUNT; ++i, ++head) {
		action_handler handler = action_handlers[i];
		struct nvhost_waitlist *waiter, *next;

		list_for_each_entry_safe(waiter, next, head, list) {
			list_del(&waiter->list);
			handler(waiter);
			atomic_set(&waiter->state, WLS_HANDLED);
			smp_wmb();
			kref_put(&waiter->refcount, waiter_release);
		}
	}
}


/*** Interrupt service functions ***/

/**
 * Host1x intterrupt service function
 * Handles read / write failures
 */
static irqreturn_t host1x_isr(int irq, void *dev_id)
{
	struct nvhost_intr *intr = dev_id;
	void __iomem *sync_regs = intr_to_dev(intr)->sync_aperture;
	u32 stat;
	u32 ext_stat;
	u32 addr;

	stat = readl(sync_regs + HOST1X_SYNC_HINTSTATUS);
	ext_stat = readl(sync_regs + HOST1X_SYNC_HINTSTATUS_EXT);

	if (nvhost_sync_hintstatus_ext_ip_read_int(ext_stat)) {
		addr = readl(sync_regs + HOST1X_SYNC_IP_READ_TIMEOUT_ADDR);
		pr_err("Host read timeout at address %x\n", addr);
	}

	if (nvhost_sync_hintstatus_ext_ip_write_int(ext_stat)) {
		addr = readl(sync_regs + HOST1X_SYNC_IP_WRITE_TIMEOUT_ADDR);
		pr_err("Host write timeout at address %x\n", addr);
	}

	writel(ext_stat, sync_regs + HOST1X_SYNC_HINTSTATUS_EXT);
	writel(stat, sync_regs + HOST1X_SYNC_HINTSTATUS);

	return IRQ_HANDLED;
}

/**
 * Sync point threshold interrupt service function
 * Handles sync point threshold triggers, in interrupt context
 */
static irqreturn_t syncpt_thresh_isr(int irq, void *dev_id)
{
	struct nvhost_intr_syncpt *syncpt = dev_id;
	unsigned int id = syncpt->id;
	struct nvhost_intr *intr = container_of(syncpt, struct nvhost_intr,
						syncpt[id]);
	void __iomem *sync_regs = intr_to_dev(intr)->sync_aperture;

	writel(BIT(id),
		sync_regs + HOST1X_SYNC_SYNCPT_THRESH_INT_DISABLE);
	writel(BIT(id),
		sync_regs + HOST1X_SYNC_SYNCPT_THRESH_CPU0_INT_STATUS);

	return IRQ_WAKE_THREAD;
}


/**
 * Sync point threshold interrupt service thread function
 * Handles sync point threshold triggers, in thread context
 */
static irqreturn_t syncpt_thresh_fn(int irq, void *dev_id)
{
	struct nvhost_intr_syncpt *syncpt = dev_id;
	unsigned int id = syncpt->id;
	struct nvhost_intr *intr = container_of(syncpt, struct nvhost_intr,
						syncpt[id]);
	struct nvhost_master *dev = intr_to_dev(intr);
	void __iomem *sync_regs = dev->sync_aperture;

	struct list_head completed[NVHOST_INTR_ACTION_COUNT];
	u32 sync;
	unsigned int i;

	for (i = 0; i < NVHOST_INTR_ACTION_COUNT; ++i)
		INIT_LIST_HEAD(completed + i);

	sync = nvhost_syncpt_update_min(&dev->syncpt, id);

	spin_lock(&syncpt->lock);

	remove_completed_waiters(&syncpt->wait_head, sync, completed);

	if (!list_empty(&syncpt->wait_head)) {
		u32 thresh = list_first_entry(&syncpt->wait_head,
					struct nvhost_waitlist, list)->thresh;

		set_syncpt_threshold(sync_regs, id, thresh);
		enable_syncpt_interrupt(sync_regs, id);
	}

	spin_unlock(&syncpt->lock);

	run_handlers(completed);

	return IRQ_HANDLED;
}

/*
 * lazily request a syncpt's irq
 */
static int request_syncpt_irq(struct nvhost_intr_syncpt *syncpt)
{
	static DEFINE_MUTEX(mutex);
	int err;

	mutex_lock(&mutex);
	if (!syncpt->irq_requested) {
		err = request_threaded_irq(syncpt->irq,
					syncpt_thresh_isr, syncpt_thresh_fn,
					0, syncpt->thresh_irq_name, syncpt);
		if (!err)
			syncpt->irq_requested = 1;
	}
	mutex_unlock(&mutex);
	return err;
}


/*** Main API ***/

int nvhost_intr_add_action(struct nvhost_intr *intr, u32 id, u32 thresh,
			enum nvhost_intr_action action, void *data,
			void **ref)
{
	struct nvhost_waitlist *waiter;
	struct nvhost_intr_syncpt *syncpt;
	void __iomem *sync_regs;
	int queue_was_empty;
	int err;

	/* create and initialize a new waiter */
	waiter = kmalloc(sizeof(*waiter), GFP_KERNEL);
	if (!waiter)
		return -ENOMEM;
	INIT_LIST_HEAD(&waiter->list);
	kref_init(&waiter->refcount);
	if (ref)
		kref_get(&waiter->refcount);
	waiter->thresh = thresh;
	waiter->action = action;
	atomic_set(&waiter->state, WLS_PENDING);
	waiter->data = data;
	waiter->count = 1;

	BUG_ON(id >= NV_HOST1X_SYNCPT_NB_PTS);
	syncpt = intr->syncpt + id;
	sync_regs = intr_to_dev(intr)->sync_aperture;

	spin_lock(&syncpt->lock);

	/* lazily request irq for this sync point */
	if (!syncpt->irq_requested) {
		spin_unlock(&syncpt->lock);

		err = request_syncpt_irq(syncpt);
		if (err) {
			kfree(waiter);
			return err;
		}

		spin_lock(&syncpt->lock);
	}

	queue_was_empty = list_empty(&syncpt->wait_head);

	if (add_waiter_to_queue(waiter, &syncpt->wait_head)) {
		/* added at head of list - new threshold value */
		set_syncpt_threshold(sync_regs, id, thresh);

		/* added as first waiter - enable interrupt */
		if (queue_was_empty)
			enable_syncpt_interrupt(sync_regs, id);
	}

	spin_unlock(&syncpt->lock);

	if (ref)
		*ref = waiter;
	return 0;
}

void nvhost_intr_put_ref(struct nvhost_intr *intr, void *ref)
{
	struct nvhost_waitlist *waiter = ref;

	while (atomic_cmpxchg(&waiter->state,
				WLS_PENDING, WLS_CANCELLED) == WLS_REMOVED)
		schedule();

	kref_put(&waiter->refcount, waiter_release);
}


/*** Init & shutdown ***/

int nvhost_intr_init(struct nvhost_intr *intr, u32 irq_gen, u32 irq_sync)
{
	unsigned int id;
	struct nvhost_intr_syncpt *syncpt;
	int err;

	err = request_irq(irq_gen, host1x_isr, 0, "host_status", intr);
	if (err)
		goto fail;
	intr->host1x_irq = irq_gen;
	intr->host1x_isr_started = true;

	for (id = 0, syncpt = intr->syncpt;
	     id < NV_HOST1X_SYNCPT_NB_PTS;
	     ++id, ++syncpt) {
		syncpt->id = id;
		syncpt->irq = irq_sync + id;
		syncpt->irq_requested = 0;
		spin_lock_init(&syncpt->lock);
		INIT_LIST_HEAD(&syncpt->wait_head);
		snprintf(syncpt->thresh_irq_name,
			 sizeof(syncpt->thresh_irq_name),
			 "%s", nvhost_syncpt_name(id));
	}

	return 0;

fail:
	nvhost_intr_deinit(intr);
	return err;
}

void nvhost_intr_deinit(struct nvhost_intr *intr)
{
	unsigned int id;
	struct nvhost_intr_syncpt *syncpt;

	for (id = 0, syncpt = intr->syncpt;
	     id < NV_HOST1X_SYNCPT_NB_PTS;
	     ++id, ++syncpt)
		if (syncpt->irq_requested)
			free_irq(syncpt->irq, syncpt);

	if (intr->host1x_isr_started) {
		free_irq(intr->host1x_irq, intr);
		intr->host1x_isr_started = false;
	}
}

void nvhost_intr_configure (struct nvhost_intr *intr, u32 hz)
{
	void __iomem *sync_regs = intr_to_dev(intr)->sync_aperture;

	// write microsecond clock register
	writel((hz + 1000000 - 1)/1000000, sync_regs + HOST1X_SYNC_USEC_CLK);

	/* disable the ip_busy_timeout. this prevents write drops, etc.
	 * there's no real way to recover from a hung client anyway.
	 */
	writel(0, sync_regs + HOST1X_SYNC_IP_BUSY_TIMEOUT);

	/* increase the auto-ack timout to the maximum value. 2d will hang
	 * otherwise on ap20.
	 */
	writel(0xff, sync_regs + HOST1X_SYNC_CTXSW_TIMEOUT_CFG);

	/* disable interrupts for both cpu's */
	writel(0, sync_regs + HOST1X_SYNC_SYNCPT_THRESH_INT_MASK_0);
	writel(0, sync_regs + HOST1X_SYNC_SYNCPT_THRESH_INT_MASK_1);

	/* masking all of the interrupts actually means "enable" */
	writel(BIT(0), sync_regs + HOST1X_SYNC_INTMASK);

	/* enable HOST_INT_C0MASK */
	writel(BIT(0), sync_regs + HOST1X_SYNC_INTC0MASK);

	/* enable HINTMASK_EXT */
	writel(BIT(31), sync_regs + HOST1X_SYNC_HINTMASK);

	/* enable IP_READ_INT and IP_WRITE_INT */
	writel(BIT(30) | BIT(31), sync_regs + HOST1X_SYNC_HINTMASK_EXT);
}
