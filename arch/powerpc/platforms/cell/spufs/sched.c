/* sched.c - SPU scheduler.
 *
 * Copyright (C) IBM 2005
 * Author: Mark Nutter <mnutter@us.ibm.com>
 *
 * 2006-03-31	NUMA domains added.
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
#include <linux/numa.h>
#include <linux/mutex.h>
#include <linux/notifier.h>

#include <asm/io.h>
#include <asm/mmu_context.h>
#include <asm/spu.h>
#include <asm/spu_csa.h>
#include <asm/spu_priv1.h>
#include "spufs.h"

#define SPU_MIN_TIMESLICE 	(100 * HZ / 1000)

#define SPU_BITMAP_SIZE (((MAX_PRIO+BITS_PER_LONG)/BITS_PER_LONG)+1)
struct spu_prio_array {
	unsigned long bitmap[SPU_BITMAP_SIZE];
	wait_queue_head_t waitq[MAX_PRIO];
	struct list_head active_list[MAX_NUMNODES];
	struct mutex active_mutex[MAX_NUMNODES];
};

static struct spu_prio_array *spu_prio;

static inline int node_allowed(int node)
{
	cpumask_t mask;

	if (!nr_cpus_node(node))
		return 0;
	mask = node_to_cpumask(node);
	if (!cpus_intersects(mask, current->cpus_allowed))
		return 0;
	return 1;
}

static inline void mm_needs_global_tlbie(struct mm_struct *mm)
{
	int nr = (NR_CPUS > 1) ? NR_CPUS : NR_CPUS + 1;

	/* Global TLBIE broadcast required with SPEs. */
	__cpus_setall(&mm->cpu_vm_mask, nr);
}

static BLOCKING_NOTIFIER_HEAD(spu_switch_notifier);

static void spu_switch_notify(struct spu *spu, struct spu_context *ctx)
{
	blocking_notifier_call_chain(&spu_switch_notifier,
			    ctx ? ctx->object_id : 0, spu);
}

int spu_switch_event_register(struct notifier_block * n)
{
	return blocking_notifier_chain_register(&spu_switch_notifier, n);
}

int spu_switch_event_unregister(struct notifier_block * n)
{
	return blocking_notifier_chain_unregister(&spu_switch_notifier, n);
}


static inline void bind_context(struct spu *spu, struct spu_context *ctx)
{
	pr_debug("%s: pid=%d SPU=%d NODE=%d\n", __FUNCTION__, current->pid,
		 spu->number, spu->node);
	spu->ctx = ctx;
	spu->flags = 0;
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
	spu->dma_callback = spufs_dma_callback;
	mb();
	spu_unmap_mappings(ctx);
	spu_restore(&ctx->csa, spu);
	spu->timestamp = jiffies;
	spu_cpu_affinity_set(spu, raw_smp_processor_id());
	spu_switch_notify(spu, ctx);
}

static inline void unbind_context(struct spu *spu, struct spu_context *ctx)
{
	pr_debug("%s: unbind pid=%d SPU=%d NODE=%d\n", __FUNCTION__,
		 spu->pid, spu->number, spu->node);
	spu_switch_notify(spu, NULL);
	spu_unmap_mappings(ctx);
	spu_save(&ctx->csa, spu);
	spu->timestamp = jiffies;
	ctx->state = SPU_STATE_SAVED;
	spu->ibox_callback = NULL;
	spu->wbox_callback = NULL;
	spu->stop_callback = NULL;
	spu->mfc_callback = NULL;
	spu->dma_callback = NULL;
	spu->mm = NULL;
	spu->pid = 0;
	spu->prio = MAX_PRIO;
	ctx->ops = &spu_backing_ops;
	ctx->spu = NULL;
	spu->flags = 0;
	spu->ctx = NULL;
}

static inline void spu_add_wq(wait_queue_head_t * wq, wait_queue_t * wait,
			      int prio)
{
	prepare_to_wait_exclusive(wq, wait, TASK_INTERRUPTIBLE);
	set_bit(prio, spu_prio->bitmap);
}

static inline void spu_del_wq(wait_queue_head_t * wq, wait_queue_t * wait,
			      int prio)
{
	u64 flags;

	__set_current_state(TASK_RUNNING);

	spin_lock_irqsave(&wq->lock, flags);

	remove_wait_queue_locked(wq, wait);
	if (list_empty(&wq->task_list))
		clear_bit(prio, spu_prio->bitmap);

	spin_unlock_irqrestore(&wq->lock, flags);
}

static void spu_prio_wait(struct spu_context *ctx, u64 flags)
{
	int prio = current->prio;
	wait_queue_head_t *wq = &spu_prio->waitq[prio];
	DEFINE_WAIT(wait);

	if (ctx->spu)
		return;

	spu_add_wq(wq, &wait, prio);

	if (!signal_pending(current)) {
		up_write(&ctx->state_sema);
		pr_debug("%s: pid=%d prio=%d\n", __FUNCTION__,
			 current->pid, current->prio);
		schedule();
		down_write(&ctx->state_sema);
	}

	spu_del_wq(wq, &wait, prio);
}

static void spu_prio_wakeup(void)
{
	int best = sched_find_first_bit(spu_prio->bitmap);
	if (best < MAX_PRIO) {
		wait_queue_head_t *wq = &spu_prio->waitq[best];
		wake_up_interruptible_nr(wq, 1);
	}
}

static int get_active_spu(struct spu *spu)
{
	int node = spu->node;
	struct spu *tmp;
	int rc = 0;

	mutex_lock(&spu_prio->active_mutex[node]);
	list_for_each_entry(tmp, &spu_prio->active_list[node], list) {
		if (tmp == spu) {
			list_del_init(&spu->list);
			rc = 1;
			break;
		}
	}
	mutex_unlock(&spu_prio->active_mutex[node]);
	return rc;
}

static void put_active_spu(struct spu *spu)
{
	int node = spu->node;

	mutex_lock(&spu_prio->active_mutex[node]);
	list_add_tail(&spu->list, &spu_prio->active_list[node]);
	mutex_unlock(&spu_prio->active_mutex[node]);
}

static struct spu *spu_get_idle(struct spu_context *ctx, u64 flags)
{
	struct spu *spu = NULL;
	int node = cpu_to_node(raw_smp_processor_id());
	int n;

	for (n = 0; n < MAX_NUMNODES; n++, node++) {
		node = (node < MAX_NUMNODES) ? node : 0;
		if (!node_allowed(node))
			continue;
		spu = spu_alloc_node(node);
		if (spu)
			break;
	}
	return spu;
}

static inline struct spu *spu_get(struct spu_context *ctx, u64 flags)
{
	/* Future: spu_get_idle() if possible,
	 * otherwise try to preempt an active
	 * context.
	 */
	return spu_get_idle(ctx, flags);
}

/* The three externally callable interfaces
 * for the scheduler begin here.
 *
 *	spu_activate	- bind a context to SPU, waiting as needed.
 *	spu_deactivate	- unbind a context from its SPU.
 *	spu_yield	- yield an SPU if others are waiting.
 */

int spu_activate(struct spu_context *ctx, u64 flags)
{
	struct spu *spu;
	int ret = 0;

	for (;;) {
		if (ctx->spu)
			return 0;
		spu = spu_get(ctx, flags);
		if (spu != NULL) {
			if (ctx->spu != NULL) {
				spu_free(spu);
				spu_prio_wakeup();
				break;
			}
			bind_context(spu, ctx);
			put_active_spu(spu);
			break;
		}
		spu_prio_wait(ctx, flags);
		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			spu_prio_wakeup();
			break;
		}
	}
	return ret;
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
	if (needs_idle) {
		spu_free(spu);
		spu_prio_wakeup();
	}
}

void spu_yield(struct spu_context *ctx)
{
	struct spu *spu;
	int need_yield = 0;

	if (down_write_trylock(&ctx->state_sema)) {
		if ((spu = ctx->spu) != NULL) {
			int best = sched_find_first_bit(spu_prio->bitmap);
			if (best < MAX_PRIO) {
				pr_debug("%s: yielding SPU %d NODE %d\n",
					 __FUNCTION__, spu->number, spu->node);
				spu_deactivate(ctx);
				ctx->state = SPU_STATE_SAVED;
				need_yield = 1;
			} else {
				spu->prio = MAX_PRIO;
			}
		}
		up_write(&ctx->state_sema);
	}
	if (unlikely(need_yield))
		yield();
}

int __init spu_sched_init(void)
{
	int i;

	spu_prio = kzalloc(sizeof(struct spu_prio_array), GFP_KERNEL);
	if (!spu_prio) {
		printk(KERN_WARNING "%s: Unable to allocate priority queue.\n",
		       __FUNCTION__);
		return 1;
	}
	for (i = 0; i < MAX_PRIO; i++) {
		init_waitqueue_head(&spu_prio->waitq[i]);
		__clear_bit(i, spu_prio->bitmap);
	}
	__set_bit(MAX_PRIO, spu_prio->bitmap);
	for (i = 0; i < MAX_NUMNODES; i++) {
		mutex_init(&spu_prio->active_mutex[i]);
		INIT_LIST_HEAD(&spu_prio->active_list[i]);
	}
	return 0;
}

void __exit spu_sched_exit(void)
{
	struct spu *spu, *tmp;
	int node;

	for (node = 0; node < MAX_NUMNODES; node++) {
		mutex_lock(&spu_prio->active_mutex[node]);
		list_for_each_entry_safe(spu, tmp, &spu_prio->active_list[node],
					 list) {
			list_del_init(&spu->list);
			spu_free(spu);
		}
		mutex_unlock(&spu_prio->active_mutex[node]);
	}
	kfree(spu_prio);
}
