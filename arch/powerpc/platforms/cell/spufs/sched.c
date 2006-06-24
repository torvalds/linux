/* sched.c - SPU scheduler.
 *
 * Copyright (C) IBM 2005
 * Author: Mark Nutter <mnutter@us.ibm.com>
 *
 * SPU scheduler, based on Linux thread priority.  For now use
 * a simple "cooperative" yield model with no preemption.  SPU
 * scheduling will eventually be preemptive: When a thread with
 * a higher static priority gets ready to run, then an active SPU
 * context will be preempted and returned to the waitq.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#undef DEBUG

#include <linux/config.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/completion.h>
#include <linux/vmalloc.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/stddef.h>
#include <linux/unistd.h>

#include <asm/io.h>
#include <asm/mmu_context.h>
#include <asm/spu.h>
#include <asm/spu_csa.h>
#include <asm/spu_priv1.h>
#include "spufs.h"

#define SPU_MIN_TIMESLICE 	(100 * HZ / 1000)

#define SPU_BITMAP_SIZE (((MAX_PRIO+BITS_PER_LONG)/BITS_PER_LONG)+1)
struct spu_prio_array {
	atomic_t nr_blocked;
	unsigned long bitmap[SPU_BITMAP_SIZE];
	wait_queue_head_t waitq[MAX_PRIO];
};

/* spu_runqueue - This is the main runqueue data structure for SPUs. */
struct spu_runqueue {
	struct semaphore sem;
	unsigned long nr_active;
	unsigned long nr_idle;
	unsigned long nr_switches;
	struct list_head active_list;
	struct list_head idle_list;
	struct spu_prio_array prio;
};

static struct spu_runqueue *spu_runqueues = NULL;

static inline struct spu_runqueue *spu_rq(void)
{
	/* Future: make this a per-NODE array,
	 * and use cpu_to_node(smp_processor_id())
	 */
	return spu_runqueues;
}

static inline struct spu *del_idle(struct spu_runqueue *rq)
{
	struct spu *spu;

	BUG_ON(rq->nr_idle <= 0);
	BUG_ON(list_empty(&rq->idle_list));
	/* Future: Move SPU out of low-power SRI state. */
	spu = list_entry(rq->idle_list.next, struct spu, sched_list);
	list_del_init(&spu->sched_list);
	rq->nr_idle--;
	return spu;
}

static inline void del_active(struct spu_runqueue *rq, struct spu *spu)
{
	BUG_ON(rq->nr_active <= 0);
	BUG_ON(list_empty(&rq->active_list));
	list_del_init(&spu->sched_list);
	rq->nr_active--;
}

static inline void add_idle(struct spu_runqueue *rq, struct spu *spu)
{
	/* Future: Put SPU into low-power SRI state. */
	list_add_tail(&spu->sched_list, &rq->idle_list);
	rq->nr_idle++;
}

static inline void add_active(struct spu_runqueue *rq, struct spu *spu)
{
	rq->nr_active++;
	rq->nr_switches++;
	list_add_tail(&spu->sched_list, &rq->active_list);
}

static void prio_wakeup(struct spu_runqueue *rq)
{
	if (atomic_read(&rq->prio.nr_blocked) && rq->nr_idle) {
		int best = sched_find_first_bit(rq->prio.bitmap);
		if (best < MAX_PRIO) {
			wait_queue_head_t *wq = &rq->prio.waitq[best];
			wake_up_interruptible_nr(wq, 1);
		}
	}
}

static void prio_wait(struct spu_runqueue *rq, struct spu_context *ctx,
		      u64 flags)
{
	int prio = current->prio;
	wait_queue_head_t *wq = &rq->prio.waitq[prio];
	DEFINE_WAIT(wait);

	__set_bit(prio, rq->prio.bitmap);
	atomic_inc(&rq->prio.nr_blocked);
	prepare_to_wait_exclusive(wq, &wait, TASK_INTERRUPTIBLE);
	if (!signal_pending(current)) {
		up(&rq->sem);
		up_write(&ctx->state_sema);
		pr_debug("%s: pid=%d prio=%d\n", __FUNCTION__,
			 current->pid, current->prio);
		schedule();
		down_write(&ctx->state_sema);
		down(&rq->sem);
	}
	finish_wait(wq, &wait);
	atomic_dec(&rq->prio.nr_blocked);
	if (!waitqueue_active(wq))
		__clear_bit(prio, rq->prio.bitmap);
}

static inline int is_best_prio(struct spu_runqueue *rq)
{
	int best_prio;

	best_prio = sched_find_first_bit(rq->prio.bitmap);
	return (current->prio < best_prio) ? 1 : 0;
}

static inline void mm_needs_global_tlbie(struct mm_struct *mm)
{
	/* Global TLBIE broadcast required with SPEs. */
#if (NR_CPUS > 1)
	__cpus_setall(&mm->cpu_vm_mask, NR_CPUS);
#else
	__cpus_setall(&mm->cpu_vm_mask, NR_CPUS+1); /* is this ok? */
#endif
}

static inline void bind_context(struct spu *spu, struct spu_context *ctx)
{
	pr_debug("%s: pid=%d SPU=%d\n", __FUNCTION__, current->pid,
		 spu->number);
	spu->ctx = ctx;
	spu->flags = 0;
	ctx->flags = 0;
	ctx->spu = spu;
	ctx->ops = &spu_hw_ops;
	spu->pid = current->pid;
	spu->prio = current->prio;
	spu->mm = ctx->owner;
	mm_needs_global_tlbie(spu->mm);
	spu->ibox_callback = spufs_ibox_callback;
	spu->wbox_callback = spufs_wbox_callback;
	spu->stop_callback = spufs_stop_callback;
	spu->mfc_callback = spufs_mfc_callback;
	mb();
	spu_unmap_mappings(ctx);
	spu_restore(&ctx->csa, spu);
	spu->timestamp = jiffies;
}

static inline void unbind_context(struct spu *spu, struct spu_context *ctx)
{
	pr_debug("%s: unbind pid=%d SPU=%d\n", __FUNCTION__,
		 spu->pid, spu->number);
	spu_unmap_mappings(ctx);
	spu_save(&ctx->csa, spu);
	spu->timestamp = jiffies;
	ctx->state = SPU_STATE_SAVED;
	spu->ibox_callback = NULL;
	spu->wbox_callback = NULL;
	spu->stop_callback = NULL;
	spu->mfc_callback = NULL;
	spu->mm = NULL;
	spu->pid = 0;
	spu->prio = MAX_PRIO;
	ctx->ops = &spu_backing_ops;
	ctx->spu = NULL;
	ctx->flags = 0;
	spu->flags = 0;
	spu->ctx = NULL;
}

static void spu_reaper(void *data)
{
	struct spu_context *ctx = data;
	struct spu *spu;

	down_write(&ctx->state_sema);
	spu = ctx->spu;
	if (spu && test_bit(SPU_CONTEXT_PREEMPT, &ctx->flags)) {
		if (atomic_read(&spu->rq->prio.nr_blocked)) {
			pr_debug("%s: spu=%d\n", __func__, spu->number);
			ctx->ops->runcntl_stop(ctx);
			spu_deactivate(ctx);
			wake_up_all(&ctx->stop_wq);
		} else {
			clear_bit(SPU_CONTEXT_PREEMPT, &ctx->flags);
		}
	}
	up_write(&ctx->state_sema);
	put_spu_context(ctx);
}

static void schedule_spu_reaper(struct spu_runqueue *rq, struct spu *spu)
{
	struct spu_context *ctx = get_spu_context(spu->ctx);
	unsigned long now = jiffies;
	unsigned long expire = spu->timestamp + SPU_MIN_TIMESLICE;

	set_bit(SPU_CONTEXT_PREEMPT, &ctx->flags);
	INIT_WORK(&ctx->reap_work, spu_reaper, ctx);
	if (time_after(now, expire))
		schedule_work(&ctx->reap_work);
	else
		schedule_delayed_work(&ctx->reap_work, expire - now);
}

static void check_preempt_active(struct spu_runqueue *rq)
{
	struct list_head *p;
	struct spu *worst = NULL;

	list_for_each(p, &rq->active_list) {
		struct spu *spu = list_entry(p, struct spu, sched_list);
		struct spu_context *ctx = spu->ctx;
		if (!test_bit(SPU_CONTEXT_PREEMPT, &ctx->flags)) {
			if (!worst || (spu->prio > worst->prio)) {
				worst = spu;
			}
		}
	}
	if (worst && (current->prio < worst->prio))
		schedule_spu_reaper(rq, worst);
}

static struct spu *get_idle_spu(struct spu_context *ctx, u64 flags)
{
	struct spu_runqueue *rq;
	struct spu *spu = NULL;

	rq = spu_rq();
	down(&rq->sem);
	for (;;) {
		if (rq->nr_idle > 0) {
			if (is_best_prio(rq)) {
				/* Fall through. */
				spu = del_idle(rq);
				break;
			} else {
				prio_wakeup(rq);
				up(&rq->sem);
				yield();
				if (signal_pending(current)) {
					return NULL;
				}
				rq = spu_rq();
				down(&rq->sem);
				continue;
			}
		} else {
			check_preempt_active(rq);
			prio_wait(rq, ctx, flags);
			if (signal_pending(current)) {
				prio_wakeup(rq);
				spu = NULL;
				break;
			}
			continue;
		}
	}
	up(&rq->sem);
	return spu;
}

static void put_idle_spu(struct spu *spu)
{
	struct spu_runqueue *rq = spu->rq;

	down(&rq->sem);
	add_idle(rq, spu);
	prio_wakeup(rq);
	up(&rq->sem);
}

static int get_active_spu(struct spu *spu)
{
	struct spu_runqueue *rq = spu->rq;
	struct list_head *p;
	struct spu *tmp;
	int rc = 0;

	down(&rq->sem);
	list_for_each(p, &rq->active_list) {
		tmp = list_entry(p, struct spu, sched_list);
		if (tmp == spu) {
			del_active(rq, spu);
			rc = 1;
			break;
		}
	}
	up(&rq->sem);
	return rc;
}

static void put_active_spu(struct spu *spu)
{
	struct spu_runqueue *rq = spu->rq;

	down(&rq->sem);
	add_active(rq, spu);
	up(&rq->sem);
}

/* Lock order:
 *	spu_activate() & spu_deactivate() require the
 *	caller to have down_write(&ctx->state_sema).
 *
 *	The rq->sem is breifly held (inside or outside a
 *	given ctx lock) for list management, but is never
 *	held during save/restore.
 */

int spu_activate(struct spu_context *ctx, u64 flags)
{
	struct spu *spu;

	if (ctx->spu)
		return 0;
	spu = get_idle_spu(ctx, flags);
	if (!spu)
		return (signal_pending(current)) ? -ERESTARTSYS : -EAGAIN;
	bind_context(spu, ctx);
	/*
	 * We're likely to wait for interrupts on the same
	 * CPU that we are now on, so send them here.
	 */
	spu_cpu_affinity_set(spu, raw_smp_processor_id());
	put_active_spu(spu);
	return 0;
}

void spu_deactivate(struct spu_context *ctx)
{
	struct spu *spu;
	int needs_idle;

	spu = ctx->spu;
	if (!spu)
		return;
	needs_idle = get_active_spu(spu);
	unbind_context(spu, ctx);
	if (needs_idle)
		put_idle_spu(spu);
}

void spu_yield(struct spu_context *ctx)
{
	struct spu *spu;
	int need_yield = 0;

	down_write(&ctx->state_sema);
	spu = ctx->spu;
	if (spu && (sched_find_first_bit(spu->rq->prio.bitmap) < MAX_PRIO)) {
		pr_debug("%s: yielding SPU %d\n", __FUNCTION__, spu->number);
		spu_deactivate(ctx);
		ctx->state = SPU_STATE_SAVED;
		need_yield = 1;
	} else if (spu) {
		spu->prio = MAX_PRIO;
	}
	up_write(&ctx->state_sema);
	if (unlikely(need_yield))
		yield();
}

int __init spu_sched_init(void)
{
	struct spu_runqueue *rq;
	struct spu *spu;
	int i;

	rq = spu_runqueues = kmalloc(sizeof(struct spu_runqueue), GFP_KERNEL);
	if (!rq) {
		printk(KERN_WARNING "%s: Unable to allocate runqueues.\n",
		       __FUNCTION__);
		return 1;
	}
	memset(rq, 0, sizeof(struct spu_runqueue));
	init_MUTEX(&rq->sem);
	INIT_LIST_HEAD(&rq->active_list);
	INIT_LIST_HEAD(&rq->idle_list);
	rq->nr_active = 0;
	rq->nr_idle = 0;
	rq->nr_switches = 0;
	atomic_set(&rq->prio.nr_blocked, 0);
	for (i = 0; i < MAX_PRIO; i++) {
		init_waitqueue_head(&rq->prio.waitq[i]);
		__clear_bit(i, rq->prio.bitmap);
	}
	__set_bit(MAX_PRIO, rq->prio.bitmap);
	for (;;) {
		spu = spu_alloc();
		if (!spu)
			break;
		pr_debug("%s: adding SPU[%d]\n", __FUNCTION__, spu->number);
		add_idle(rq, spu);
		spu->rq = rq;
		spu->timestamp = jiffies;
	}
	if (!rq->nr_idle) {
		printk(KERN_WARNING "%s: No available SPUs.\n", __FUNCTION__);
		kfree(rq);
		return 1;
	}
	return 0;
}

void __exit spu_sched_exit(void)
{
	struct spu_runqueue *rq = spu_rq();
	struct spu *spu;

	if (!rq) {
		printk(KERN_WARNING "%s: no runqueues!\n", __FUNCTION__);
		return;
	}
	while (rq->nr_idle > 0) {
		spu = del_idle(rq);
		if (!spu)
			break;
		spu_free(spu);
	}
	kfree(rq);
}
