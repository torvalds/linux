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

#define SPU_TIMESLICE	(HZ)

struct spu_prio_array {
	DECLARE_BITMAP(bitmap, MAX_PRIO);
	struct list_head runq[MAX_PRIO];
	spinlock_t runq_lock;
	struct list_head active_list[MAX_NUMNODES];
	struct mutex active_mutex[MAX_NUMNODES];
};

static struct spu_prio_array *spu_prio;
static struct workqueue_struct *spu_sched_wq;

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

void spu_start_tick(struct spu_context *ctx)
{
	if (ctx->policy == SCHED_RR) {
		/*
		 * Make sure the exiting bit is cleared.
		 */
		clear_bit(SPU_SCHED_EXITING, &ctx->sched_flags);
		mb();
		queue_delayed_work(spu_sched_wq, &ctx->sched_work, SPU_TIMESLICE);
	}
}

void spu_stop_tick(struct spu_context *ctx)
{
	if (ctx->policy == SCHED_RR) {
		/*
		 * While the work can be rearming normally setting this flag
		 * makes sure it does not rearm itself anymore.
		 */
		set_bit(SPU_SCHED_EXITING, &ctx->sched_flags);
		mb();
		cancel_delayed_work(&ctx->sched_work);
	}
}

void spu_sched_tick(struct work_struct *work)
{
	struct spu_context *ctx =
		container_of(work, struct spu_context, sched_work.work);
	struct spu *spu;
	int preempted = 0;

	/*
	 * If this context is being stopped avoid rescheduling from the
	 * scheduler tick because we would block on the state_mutex.
	 * The caller will yield the spu later on anyway.
	 */
	if (test_bit(SPU_SCHED_EXITING, &ctx->sched_flags))
		return;

	mutex_lock(&ctx->state_mutex);
	spu = ctx->spu;
	if (spu) {
		int best = sched_find_first_bit(spu_prio->bitmap);
		if (best <= ctx->prio) {
			spu_deactivate(ctx);
			preempted = 1;
		}
	}
	mutex_unlock(&ctx->state_mutex);

	if (preempted) {
		/*
		 * We need to break out of the wait loop in spu_run manually
		 * to ensure this context gets put on the runqueue again
		 * ASAP.
		 */
		wake_up(&ctx->stop_wq);
	} else
		spu_start_tick(ctx);
}

/**
 * spu_add_to_active_list - add spu to active list
 * @spu:	spu to add to the active list
 */
static void spu_add_to_active_list(struct spu *spu)
{
	mutex_lock(&spu_prio->active_mutex[spu->node]);
	list_add_tail(&spu->list, &spu_prio->active_list[spu->node]);
	mutex_unlock(&spu_prio->active_mutex[spu->node]);
}

/**
 * spu_remove_from_active_list - remove spu from active list
 * @spu:       spu to remove from the active list
 */
static void spu_remove_from_active_list(struct spu *spu)
{
	int node = spu->node;

	mutex_lock(&spu_prio->active_mutex[node]);
	list_del_init(&spu->list);
	mutex_unlock(&spu_prio->active_mutex[node]);
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

/**
 * spu_bind_context - bind spu context to physical spu
 * @spu:	physical spu to bind to
 * @ctx:	context to bind
 */
static void spu_bind_context(struct spu *spu, struct spu_context *ctx)
{
	pr_debug("%s: pid=%d SPU=%d NODE=%d\n", __FUNCTION__, current->pid,
		 spu->number, spu->node);
	spu->ctx = ctx;
	spu->flags = 0;
	ctx->spu = spu;
	ctx->ops = &spu_hw_ops;
	spu->pid = current->pid;
	spu_associate_mm(spu, ctx->owner);
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
	spu_add_to_active_list(spu);
	ctx->state = SPU_STATE_RUNNABLE;
}

/**
 * spu_unbind_context - unbind spu context from physical spu
 * @spu:	physical spu to unbind from
 * @ctx:	context to unbind
 */
static void spu_unbind_context(struct spu *spu, struct spu_context *ctx)
{
	pr_debug("%s: unbind pid=%d SPU=%d NODE=%d\n", __FUNCTION__,
		 spu->pid, spu->number, spu->node);

	spu_remove_from_active_list(spu);
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
	spu_associate_mm(spu, NULL);
	spu->pid = 0;
	ctx->ops = &spu_backing_ops;
	ctx->spu = NULL;
	spu->flags = 0;
	spu->ctx = NULL;
}

/**
 * spu_add_to_rq - add a context to the runqueue
 * @ctx:       context to add
 */
static void __spu_add_to_rq(struct spu_context *ctx)
{
	int prio = ctx->prio;

	list_add_tail(&ctx->rq, &spu_prio->runq[prio]);
	set_bit(prio, spu_prio->bitmap);
}

static void __spu_del_from_rq(struct spu_context *ctx)
{
	int prio = ctx->prio;

	if (!list_empty(&ctx->rq))
		list_del_init(&ctx->rq);
	if (list_empty(&spu_prio->runq[prio]))
		clear_bit(prio, spu_prio->bitmap);
}

static void spu_prio_wait(struct spu_context *ctx)
{
	DEFINE_WAIT(wait);

	spin_lock(&spu_prio->runq_lock);
	prepare_to_wait_exclusive(&ctx->stop_wq, &wait, TASK_INTERRUPTIBLE);
	if (!signal_pending(current)) {
		__spu_add_to_rq(ctx);
		spin_unlock(&spu_prio->runq_lock);
		mutex_unlock(&ctx->state_mutex);
		schedule();
		mutex_lock(&ctx->state_mutex);
		spin_lock(&spu_prio->runq_lock);
		__spu_del_from_rq(ctx);
	}
	spin_unlock(&spu_prio->runq_lock);
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&ctx->stop_wq, &wait);
}

/**
 * spu_reschedule - try to find a runnable context for a spu
 * @spu:       spu available
 *
 * This function is called whenever a spu becomes idle.  It looks for the
 * most suitable runnable spu context and schedules it for execution.
 */
static void spu_reschedule(struct spu *spu)
{
	int best;

	spu_free(spu);

	spin_lock(&spu_prio->runq_lock);
	best = sched_find_first_bit(spu_prio->bitmap);
	if (best < MAX_PRIO) {
		struct list_head *rq = &spu_prio->runq[best];
		struct spu_context *ctx;

		BUG_ON(list_empty(rq));

		ctx = list_entry(rq->next, struct spu_context, rq);
		__spu_del_from_rq(ctx);
		wake_up(&ctx->stop_wq);
	}
	spin_unlock(&spu_prio->runq_lock);
}

static struct spu *spu_get_idle(struct spu_context *ctx)
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

/**
 * find_victim - find a lower priority context to preempt
 * @ctx:	canidate context for running
 *
 * Returns the freed physical spu to run the new context on.
 */
static struct spu *find_victim(struct spu_context *ctx)
{
	struct spu_context *victim = NULL;
	struct spu *spu;
	int node, n;

	/*
	 * Look for a possible preemption candidate on the local node first.
	 * If there is no candidate look at the other nodes.  This isn't
	 * exactly fair, but so far the whole spu schedule tries to keep
	 * a strong node affinity.  We might want to fine-tune this in
	 * the future.
	 */
 restart:
	node = cpu_to_node(raw_smp_processor_id());
	for (n = 0; n < MAX_NUMNODES; n++, node++) {
		node = (node < MAX_NUMNODES) ? node : 0;
		if (!node_allowed(node))
			continue;

		mutex_lock(&spu_prio->active_mutex[node]);
		list_for_each_entry(spu, &spu_prio->active_list[node], list) {
			struct spu_context *tmp = spu->ctx;

			if (tmp->rt_priority < ctx->rt_priority &&
			    (!victim || tmp->rt_priority < victim->rt_priority))
				victim = spu->ctx;
		}
		mutex_unlock(&spu_prio->active_mutex[node]);

		if (victim) {
			/*
			 * This nests ctx->state_mutex, but we always lock
			 * higher priority contexts before lower priority
			 * ones, so this is safe until we introduce
			 * priority inheritance schemes.
			 */
			if (!mutex_trylock(&victim->state_mutex)) {
				victim = NULL;
				goto restart;
			}

			spu = victim->spu;
			if (!spu) {
				/*
				 * This race can happen because we've dropped
				 * the active list mutex.  No a problem, just
				 * restart the search.
				 */
				mutex_unlock(&victim->state_mutex);
				victim = NULL;
				goto restart;
			}
			spu_unbind_context(spu, victim);
			mutex_unlock(&victim->state_mutex);
			/*
			 * We need to break out of the wait loop in spu_run
			 * manually to ensure this context gets put on the
			 * runqueue again ASAP.
			 */
			wake_up(&victim->stop_wq);
			return spu;
		}
	}

	return NULL;
}

/**
 * spu_activate - find a free spu for a context and execute it
 * @ctx:	spu context to schedule
 * @flags:	flags (currently ignored)
 *
 * Tries to find a free spu to run @ctx.  If no free spu is available
 * add the context to the runqueue so it gets woken up once an spu
 * is available.
 */
int spu_activate(struct spu_context *ctx, unsigned long flags)
{

	if (ctx->spu)
		return 0;

	do {
		struct spu *spu;

		spu = spu_get_idle(ctx);
		/*
		 * If this is a realtime thread we try to get it running by
		 * preempting a lower priority thread.
		 */
		if (!spu && ctx->rt_priority)
			spu = find_victim(ctx);
		if (spu) {
			spu_bind_context(spu, ctx);
			return 0;
		}

		spu_prio_wait(ctx);
	} while (!signal_pending(current));

	return -ERESTARTSYS;
}

/**
 * spu_deactivate - unbind a context from it's physical spu
 * @ctx:	spu context to unbind
 *
 * Unbind @ctx from the physical spu it is running on and schedule
 * the highest priority context to run on the freed physical spu.
 */
void spu_deactivate(struct spu_context *ctx)
{
	struct spu *spu = ctx->spu;

	if (spu) {
		spu_unbind_context(spu, ctx);
		spu_reschedule(spu);
	}
}

/**
 * spu_yield -  yield a physical spu if others are waiting
 * @ctx:	spu context to yield
 *
 * Check if there is a higher priority context waiting and if yes
 * unbind @ctx from the physical spu and schedule the highest
 * priority context to run on the freed physical spu instead.
 */
void spu_yield(struct spu_context *ctx)
{
	struct spu *spu;

	if (mutex_trylock(&ctx->state_mutex)) {
		if ((spu = ctx->spu) != NULL) {
			int best = sched_find_first_bit(spu_prio->bitmap);
			if (best < MAX_PRIO) {
				pr_debug("%s: yielding SPU %d NODE %d\n",
					 __FUNCTION__, spu->number, spu->node);
				spu_deactivate(ctx);
			}
		}
		mutex_unlock(&ctx->state_mutex);
	}
}

int __init spu_sched_init(void)
{
	int i;

	spu_sched_wq = create_singlethread_workqueue("spusched");
	if (!spu_sched_wq)
		return 1;

	spu_prio = kzalloc(sizeof(struct spu_prio_array), GFP_KERNEL);
	if (!spu_prio) {
		printk(KERN_WARNING "%s: Unable to allocate priority queue.\n",
		       __FUNCTION__);
		       destroy_workqueue(spu_sched_wq);
		return 1;
	}
	for (i = 0; i < MAX_PRIO; i++) {
		INIT_LIST_HEAD(&spu_prio->runq[i]);
		__clear_bit(i, spu_prio->bitmap);
	}
	__set_bit(MAX_PRIO, spu_prio->bitmap);
	for (i = 0; i < MAX_NUMNODES; i++) {
		mutex_init(&spu_prio->active_mutex[i]);
		INIT_LIST_HEAD(&spu_prio->active_list[i]);
	}
	spin_lock_init(&spu_prio->runq_lock);
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
	destroy_workqueue(spu_sched_wq);
}
