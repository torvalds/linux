// SPDX-License-Identifier: GPL-2.0-or-later
/* sched.c - SPU scheduler.
 *
 * Copyright (C) IBM 2005
 * Author: Mark Nutter <mnutter@us.ibm.com>
 *
 * 2006-03-31	NUMA domains added.
 */

#undef DEBUG

#include <linux/errno.h>
#include <linux/sched/signal.h>
#include <linux/sched/loadavg.h>
#include <linux/sched/rt.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/completion.h>
#include <linux/vmalloc.h>
#include <linux/smp.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/numa.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/kthread.h>
#include <linux/pid_namespace.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include <asm/io.h>
#include <asm/mmu_context.h>
#include <asm/spu.h>
#include <asm/spu_csa.h>
#include <asm/spu_priv1.h>
#include "spufs.h"
#define CREATE_TRACE_POINTS
#include "sputrace.h"

struct spu_prio_array {
	DECLARE_BITMAP(bitmap, MAX_PRIO);
	struct list_head runq[MAX_PRIO];
	spinlock_t runq_lock;
	int nr_waiting;
};

static unsigned long spu_avenrun[3];
static struct spu_prio_array *spu_prio;
static struct task_struct *spusched_task;
static struct timer_list spusched_timer;
static struct timer_list spuloadavg_timer;

/*
 * Priority of a normal, non-rt, non-niced'd process (aka nice level 0).
 */
#define NORMAL_PRIO		120

/*
 * Frequency of the spu scheduler tick.  By default we do one SPU scheduler
 * tick for every 10 CPU scheduler ticks.
 */
#define SPUSCHED_TICK		(10)

/*
 * These are the 'tuning knobs' of the scheduler:
 *
 * Minimum timeslice is 5 msecs (or 1 spu scheduler tick, whichever is
 * larger), default timeslice is 100 msecs, maximum timeslice is 800 msecs.
 */
#define MIN_SPU_TIMESLICE	max(5 * HZ / (1000 * SPUSCHED_TICK), 1)
#define DEF_SPU_TIMESLICE	(100 * HZ / (1000 * SPUSCHED_TICK))

#define SCALE_PRIO(x, prio) \
	max(x * (MAX_PRIO - prio) / (NICE_WIDTH / 2), MIN_SPU_TIMESLICE)

/*
 * scale user-nice values [ -20 ... 0 ... 19 ] to time slice values:
 * [800ms ... 100ms ... 5ms]
 *
 * The higher a thread's priority, the bigger timeslices
 * it gets during one round of execution. But even the lowest
 * priority thread gets MIN_TIMESLICE worth of execution time.
 */
void spu_set_timeslice(struct spu_context *ctx)
{
	if (ctx->prio < NORMAL_PRIO)
		ctx->time_slice = SCALE_PRIO(DEF_SPU_TIMESLICE * 4, ctx->prio);
	else
		ctx->time_slice = SCALE_PRIO(DEF_SPU_TIMESLICE, ctx->prio);
}

/*
 * Update scheduling information from the owning thread.
 */
void __spu_update_sched_info(struct spu_context *ctx)
{
	/*
	 * assert that the context is not on the runqueue, so it is safe
	 * to change its scheduling parameters.
	 */
	BUG_ON(!list_empty(&ctx->rq));

	/*
	 * 32-Bit assignments are atomic on powerpc, and we don't care about
	 * memory ordering here because retrieving the controlling thread is
	 * per definition racy.
	 */
	ctx->tid = current->pid;

	/*
	 * We do our own priority calculations, so we normally want
	 * ->static_prio to start with. Unfortunately this field
	 * contains junk for threads with a realtime scheduling
	 * policy so we have to look at ->prio in this case.
	 */
	if (rt_prio(current->prio))
		ctx->prio = current->prio;
	else
		ctx->prio = current->static_prio;
	ctx->policy = current->policy;

	/*
	 * TO DO: the context may be loaded, so we may need to activate
	 * it again on a different node. But it shouldn't hurt anything
	 * to update its parameters, because we know that the scheduler
	 * is not actively looking at this field, since it is not on the
	 * runqueue. The context will be rescheduled on the proper node
	 * if it is timesliced or preempted.
	 */
	cpumask_copy(&ctx->cpus_allowed, current->cpus_ptr);

	/* Save the current cpu id for spu interrupt routing. */
	ctx->last_ran = raw_smp_processor_id();
}

void spu_update_sched_info(struct spu_context *ctx)
{
	int node;

	if (ctx->state == SPU_STATE_RUNNABLE) {
		node = ctx->spu->node;

		/*
		 * Take list_mutex to sync with find_victim().
		 */
		mutex_lock(&cbe_spu_info[node].list_mutex);
		__spu_update_sched_info(ctx);
		mutex_unlock(&cbe_spu_info[node].list_mutex);
	} else {
		__spu_update_sched_info(ctx);
	}
}

static int __node_allowed(struct spu_context *ctx, int node)
{
	if (nr_cpus_node(node)) {
		const struct cpumask *mask = cpumask_of_node(node);

		if (cpumask_intersects(mask, &ctx->cpus_allowed))
			return 1;
	}

	return 0;
}

static int node_allowed(struct spu_context *ctx, int node)
{
	int rval;

	spin_lock(&spu_prio->runq_lock);
	rval = __node_allowed(ctx, node);
	spin_unlock(&spu_prio->runq_lock);

	return rval;
}

void do_notify_spus_active(void)
{
	int node;

	/*
	 * Wake up the active spu_contexts.
	 */
	for_each_online_node(node) {
		struct spu *spu;

		mutex_lock(&cbe_spu_info[node].list_mutex);
		list_for_each_entry(spu, &cbe_spu_info[node].spus, cbe_list) {
			if (spu->alloc_state != SPU_FREE) {
				struct spu_context *ctx = spu->ctx;
				set_bit(SPU_SCHED_NOTIFY_ACTIVE,
					&ctx->sched_flags);
				mb();
				wake_up_all(&ctx->stop_wq);
			}
		}
		mutex_unlock(&cbe_spu_info[node].list_mutex);
	}
}

/**
 * spu_bind_context - bind spu context to physical spu
 * @spu:	physical spu to bind to
 * @ctx:	context to bind
 */
static void spu_bind_context(struct spu *spu, struct spu_context *ctx)
{
	spu_context_trace(spu_bind_context__enter, ctx, spu);

	spuctx_switch_state(ctx, SPU_UTIL_SYSTEM);

	if (ctx->flags & SPU_CREATE_NOSCHED)
		atomic_inc(&cbe_spu_info[spu->node].reserved_spus);

	ctx->stats.slb_flt_base = spu->stats.slb_flt;
	ctx->stats.class2_intr_base = spu->stats.class2_intr;

	spu_associate_mm(spu, ctx->owner);

	spin_lock_irq(&spu->register_lock);
	spu->ctx = ctx;
	spu->flags = 0;
	ctx->spu = spu;
	ctx->ops = &spu_hw_ops;
	spu->pid = current->pid;
	spu->tgid = current->tgid;
	spu->ibox_callback = spufs_ibox_callback;
	spu->wbox_callback = spufs_wbox_callback;
	spu->stop_callback = spufs_stop_callback;
	spu->mfc_callback = spufs_mfc_callback;
	spin_unlock_irq(&spu->register_lock);

	spu_unmap_mappings(ctx);

	spu_switch_log_notify(spu, ctx, SWITCH_LOG_START, 0);
	spu_restore(&ctx->csa, spu);
	spu->timestamp = jiffies;
	ctx->state = SPU_STATE_RUNNABLE;

	spuctx_switch_state(ctx, SPU_UTIL_USER);
}

/*
 * Must be used with the list_mutex held.
 */
static inline int sched_spu(struct spu *spu)
{
	BUG_ON(!mutex_is_locked(&cbe_spu_info[spu->node].list_mutex));

	return (!spu->ctx || !(spu->ctx->flags & SPU_CREATE_NOSCHED));
}

static void aff_merge_remaining_ctxs(struct spu_gang *gang)
{
	struct spu_context *ctx;

	list_for_each_entry(ctx, &gang->aff_list_head, aff_list) {
		if (list_empty(&ctx->aff_list))
			list_add(&ctx->aff_list, &gang->aff_list_head);
	}
	gang->aff_flags |= AFF_MERGED;
}

static void aff_set_offsets(struct spu_gang *gang)
{
	struct spu_context *ctx;
	int offset;

	offset = -1;
	list_for_each_entry_reverse(ctx, &gang->aff_ref_ctx->aff_list,
								aff_list) {
		if (&ctx->aff_list == &gang->aff_list_head)
			break;
		ctx->aff_offset = offset--;
	}

	offset = 0;
	list_for_each_entry(ctx, gang->aff_ref_ctx->aff_list.prev, aff_list) {
		if (&ctx->aff_list == &gang->aff_list_head)
			break;
		ctx->aff_offset = offset++;
	}

	gang->aff_flags |= AFF_OFFSETS_SET;
}

static struct spu *aff_ref_location(struct spu_context *ctx, int mem_aff,
		 int group_size, int lowest_offset)
{
	struct spu *spu;
	int node, n;

	/*
	 * TODO: A better algorithm could be used to find a good spu to be
	 *       used as reference location for the ctxs chain.
	 */
	node = cpu_to_node(raw_smp_processor_id());
	for (n = 0; n < MAX_NUMNODES; n++, node++) {
		/*
		 * "available_spus" counts how many spus are not potentially
		 * going to be used by other affinity gangs whose reference
		 * context is already in place. Although this code seeks to
		 * avoid having affinity gangs with a summed amount of
		 * contexts bigger than the amount of spus in the node,
		 * this may happen sporadically. In this case, available_spus
		 * becomes negative, which is harmless.
		 */
		int available_spus;

		node = (node < MAX_NUMNODES) ? node : 0;
		if (!node_allowed(ctx, node))
			continue;

		available_spus = 0;
		mutex_lock(&cbe_spu_info[node].list_mutex);
		list_for_each_entry(spu, &cbe_spu_info[node].spus, cbe_list) {
			if (spu->ctx && spu->ctx->gang && !spu->ctx->aff_offset
					&& spu->ctx->gang->aff_ref_spu)
				available_spus -= spu->ctx->gang->contexts;
			available_spus++;
		}
		if (available_spus < ctx->gang->contexts) {
			mutex_unlock(&cbe_spu_info[node].list_mutex);
			continue;
		}

		list_for_each_entry(spu, &cbe_spu_info[node].spus, cbe_list) {
			if ((!mem_aff || spu->has_mem_affinity) &&
							sched_spu(spu)) {
				mutex_unlock(&cbe_spu_info[node].list_mutex);
				return spu;
			}
		}
		mutex_unlock(&cbe_spu_info[node].list_mutex);
	}
	return NULL;
}

static void aff_set_ref_point_location(struct spu_gang *gang)
{
	int mem_aff, gs, lowest_offset;
	struct spu_context *tmp, *ctx;

	mem_aff = gang->aff_ref_ctx->flags & SPU_CREATE_AFFINITY_MEM;
	lowest_offset = 0;
	gs = 0;

	list_for_each_entry(tmp, &gang->aff_list_head, aff_list)
		gs++;

	list_for_each_entry_reverse(ctx, &gang->aff_ref_ctx->aff_list,
								aff_list) {
		if (&ctx->aff_list == &gang->aff_list_head)
			break;
		lowest_offset = ctx->aff_offset;
	}

	gang->aff_ref_spu = aff_ref_location(gang->aff_ref_ctx, mem_aff, gs,
							lowest_offset);
}

static struct spu *ctx_location(struct spu *ref, int offset, int node)
{
	struct spu *spu;

	spu = NULL;
	if (offset >= 0) {
		list_for_each_entry(spu, ref->aff_list.prev, aff_list) {
			BUG_ON(spu->node != node);
			if (offset == 0)
				break;
			if (sched_spu(spu))
				offset--;
		}
	} else {
		list_for_each_entry_reverse(spu, ref->aff_list.next, aff_list) {
			BUG_ON(spu->node != node);
			if (offset == 0)
				break;
			if (sched_spu(spu))
				offset++;
		}
	}

	return spu;
}

/*
 * affinity_check is called each time a context is going to be scheduled.
 * It returns the spu ptr on which the context must run.
 */
static int has_affinity(struct spu_context *ctx)
{
	struct spu_gang *gang = ctx->gang;

	if (list_empty(&ctx->aff_list))
		return 0;

	if (atomic_read(&ctx->gang->aff_sched_count) == 0)
		ctx->gang->aff_ref_spu = NULL;

	if (!gang->aff_ref_spu) {
		if (!(gang->aff_flags & AFF_MERGED))
			aff_merge_remaining_ctxs(gang);
		if (!(gang->aff_flags & AFF_OFFSETS_SET))
			aff_set_offsets(gang);
		aff_set_ref_point_location(gang);
	}

	return gang->aff_ref_spu != NULL;
}

/**
 * spu_unbind_context - unbind spu context from physical spu
 * @spu:	physical spu to unbind from
 * @ctx:	context to unbind
 */
static void spu_unbind_context(struct spu *spu, struct spu_context *ctx)
{
	u32 status;

	spu_context_trace(spu_unbind_context__enter, ctx, spu);

	spuctx_switch_state(ctx, SPU_UTIL_SYSTEM);

 	if (spu->ctx->flags & SPU_CREATE_NOSCHED)
		atomic_dec(&cbe_spu_info[spu->node].reserved_spus);

	if (ctx->gang)
		/*
		 * If ctx->gang->aff_sched_count is positive, SPU affinity is
		 * being considered in this gang. Using atomic_dec_if_positive
		 * allow us to skip an explicit check for affinity in this gang
		 */
		atomic_dec_if_positive(&ctx->gang->aff_sched_count);

	spu_unmap_mappings(ctx);
	spu_save(&ctx->csa, spu);
	spu_switch_log_notify(spu, ctx, SWITCH_LOG_STOP, 0);

	spin_lock_irq(&spu->register_lock);
	spu->timestamp = jiffies;
	ctx->state = SPU_STATE_SAVED;
	spu->ibox_callback = NULL;
	spu->wbox_callback = NULL;
	spu->stop_callback = NULL;
	spu->mfc_callback = NULL;
	spu->pid = 0;
	spu->tgid = 0;
	ctx->ops = &spu_backing_ops;
	spu->flags = 0;
	spu->ctx = NULL;
	spin_unlock_irq(&spu->register_lock);

	spu_associate_mm(spu, NULL);

	ctx->stats.slb_flt +=
		(spu->stats.slb_flt - ctx->stats.slb_flt_base);
	ctx->stats.class2_intr +=
		(spu->stats.class2_intr - ctx->stats.class2_intr_base);

	/* This maps the underlying spu state to idle */
	spuctx_switch_state(ctx, SPU_UTIL_IDLE_LOADED);
	ctx->spu = NULL;

	if (spu_stopped(ctx, &status))
		wake_up_all(&ctx->stop_wq);
}

/**
 * spu_add_to_rq - add a context to the runqueue
 * @ctx:       context to add
 */
static void __spu_add_to_rq(struct spu_context *ctx)
{
	/*
	 * Unfortunately this code path can be called from multiple threads
	 * on behalf of a single context due to the way the problem state
	 * mmap support works.
	 *
	 * Fortunately we need to wake up all these threads at the same time
	 * and can simply skip the runqueue addition for every but the first
	 * thread getting into this codepath.
	 *
	 * It's still quite hacky, and long-term we should proxy all other
	 * threads through the owner thread so that spu_run is in control
	 * of all the scheduling activity for a given context.
	 */
	if (list_empty(&ctx->rq)) {
		list_add_tail(&ctx->rq, &spu_prio->runq[ctx->prio]);
		set_bit(ctx->prio, spu_prio->bitmap);
		if (!spu_prio->nr_waiting++)
			mod_timer(&spusched_timer, jiffies + SPUSCHED_TICK);
	}
}

static void spu_add_to_rq(struct spu_context *ctx)
{
	spin_lock(&spu_prio->runq_lock);
	__spu_add_to_rq(ctx);
	spin_unlock(&spu_prio->runq_lock);
}

static void __spu_del_from_rq(struct spu_context *ctx)
{
	int prio = ctx->prio;

	if (!list_empty(&ctx->rq)) {
		if (!--spu_prio->nr_waiting)
			del_timer(&spusched_timer);
		list_del_init(&ctx->rq);

		if (list_empty(&spu_prio->runq[prio]))
			clear_bit(prio, spu_prio->bitmap);
	}
}

void spu_del_from_rq(struct spu_context *ctx)
{
	spin_lock(&spu_prio->runq_lock);
	__spu_del_from_rq(ctx);
	spin_unlock(&spu_prio->runq_lock);
}

static void spu_prio_wait(struct spu_context *ctx)
{
	DEFINE_WAIT(wait);

	/*
	 * The caller must explicitly wait for a context to be loaded
	 * if the nosched flag is set.  If NOSCHED is not set, the caller
	 * queues the context and waits for an spu event or error.
	 */
	BUG_ON(!(ctx->flags & SPU_CREATE_NOSCHED));

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

static struct spu *spu_get_idle(struct spu_context *ctx)
{
	struct spu *spu, *aff_ref_spu;
	int node, n;

	spu_context_nospu_trace(spu_get_idle__enter, ctx);

	if (ctx->gang) {
		mutex_lock(&ctx->gang->aff_mutex);
		if (has_affinity(ctx)) {
			aff_ref_spu = ctx->gang->aff_ref_spu;
			atomic_inc(&ctx->gang->aff_sched_count);
			mutex_unlock(&ctx->gang->aff_mutex);
			node = aff_ref_spu->node;

			mutex_lock(&cbe_spu_info[node].list_mutex);
			spu = ctx_location(aff_ref_spu, ctx->aff_offset, node);
			if (spu && spu->alloc_state == SPU_FREE)
				goto found;
			mutex_unlock(&cbe_spu_info[node].list_mutex);

			atomic_dec(&ctx->gang->aff_sched_count);
			goto not_found;
		}
		mutex_unlock(&ctx->gang->aff_mutex);
	}
	node = cpu_to_node(raw_smp_processor_id());
	for (n = 0; n < MAX_NUMNODES; n++, node++) {
		node = (node < MAX_NUMNODES) ? node : 0;
		if (!node_allowed(ctx, node))
			continue;

		mutex_lock(&cbe_spu_info[node].list_mutex);
		list_for_each_entry(spu, &cbe_spu_info[node].spus, cbe_list) {
			if (spu->alloc_state == SPU_FREE)
				goto found;
		}
		mutex_unlock(&cbe_spu_info[node].list_mutex);
	}

 not_found:
	spu_context_nospu_trace(spu_get_idle__not_found, ctx);
	return NULL;

 found:
	spu->alloc_state = SPU_USED;
	mutex_unlock(&cbe_spu_info[node].list_mutex);
	spu_context_trace(spu_get_idle__found, ctx, spu);
	spu_init_channels(spu);
	return spu;
}

/**
 * find_victim - find a lower priority context to preempt
 * @ctx:	candidate context for running
 *
 * Returns the freed physical spu to run the new context on.
 */
static struct spu *find_victim(struct spu_context *ctx)
{
	struct spu_context *victim = NULL;
	struct spu *spu;
	int node, n;

	spu_context_nospu_trace(spu_find_victim__enter, ctx);

	/*
	 * Look for a possible preemption candidate on the local node first.
	 * If there is no candidate look at the other nodes.  This isn't
	 * exactly fair, but so far the whole spu scheduler tries to keep
	 * a strong node affinity.  We might want to fine-tune this in
	 * the future.
	 */
 restart:
	node = cpu_to_node(raw_smp_processor_id());
	for (n = 0; n < MAX_NUMNODES; n++, node++) {
		node = (node < MAX_NUMNODES) ? node : 0;
		if (!node_allowed(ctx, node))
			continue;

		mutex_lock(&cbe_spu_info[node].list_mutex);
		list_for_each_entry(spu, &cbe_spu_info[node].spus, cbe_list) {
			struct spu_context *tmp = spu->ctx;

			if (tmp && tmp->prio > ctx->prio &&
			    !(tmp->flags & SPU_CREATE_NOSCHED) &&
			    (!victim || tmp->prio > victim->prio)) {
				victim = spu->ctx;
			}
		}
		if (victim)
			get_spu_context(victim);
		mutex_unlock(&cbe_spu_info[node].list_mutex);

		if (victim) {
			/*
			 * This nests ctx->state_mutex, but we always lock
			 * higher priority contexts before lower priority
			 * ones, so this is safe until we introduce
			 * priority inheritance schemes.
			 *
			 * XXX if the highest priority context is locked,
			 * this can loop a long time.  Might be better to
			 * look at another context or give up after X retries.
			 */
			if (!mutex_trylock(&victim->state_mutex)) {
				put_spu_context(victim);
				victim = NULL;
				goto restart;
			}

			spu = victim->spu;
			if (!spu || victim->prio <= ctx->prio) {
				/*
				 * This race can happen because we've dropped
				 * the active list mutex.  Not a problem, just
				 * restart the search.
				 */
				mutex_unlock(&victim->state_mutex);
				put_spu_context(victim);
				victim = NULL;
				goto restart;
			}

			spu_context_trace(__spu_deactivate__unload, ctx, spu);

			mutex_lock(&cbe_spu_info[node].list_mutex);
			cbe_spu_info[node].nr_active--;
			spu_unbind_context(spu, victim);
			mutex_unlock(&cbe_spu_info[node].list_mutex);

			victim->stats.invol_ctx_switch++;
			spu->stats.invol_ctx_switch++;
			if (test_bit(SPU_SCHED_SPU_RUN, &victim->sched_flags))
				spu_add_to_rq(victim);

			mutex_unlock(&victim->state_mutex);
			put_spu_context(victim);

			return spu;
		}
	}

	return NULL;
}

static void __spu_schedule(struct spu *spu, struct spu_context *ctx)
{
	int node = spu->node;
	int success = 0;

	spu_set_timeslice(ctx);

	mutex_lock(&cbe_spu_info[node].list_mutex);
	if (spu->ctx == NULL) {
		spu_bind_context(spu, ctx);
		cbe_spu_info[node].nr_active++;
		spu->alloc_state = SPU_USED;
		success = 1;
	}
	mutex_unlock(&cbe_spu_info[node].list_mutex);

	if (success)
		wake_up_all(&ctx->run_wq);
	else
		spu_add_to_rq(ctx);
}

static void spu_schedule(struct spu *spu, struct spu_context *ctx)
{
	/* not a candidate for interruptible because it's called either
	   from the scheduler thread or from spu_deactivate */
	mutex_lock(&ctx->state_mutex);
	if (ctx->state == SPU_STATE_SAVED)
		__spu_schedule(spu, ctx);
	spu_release(ctx);
}

/**
 * spu_unschedule - remove a context from a spu, and possibly release it.
 * @spu:	The SPU to unschedule from
 * @ctx:	The context currently scheduled on the SPU
 * @free_spu	Whether to free the SPU for other contexts
 *
 * Unbinds the context @ctx from the SPU @spu. If @free_spu is non-zero, the
 * SPU is made available for other contexts (ie, may be returned by
 * spu_get_idle). If this is zero, the caller is expected to schedule another
 * context to this spu.
 *
 * Should be called with ctx->state_mutex held.
 */
static void spu_unschedule(struct spu *spu, struct spu_context *ctx,
		int free_spu)
{
	int node = spu->node;

	mutex_lock(&cbe_spu_info[node].list_mutex);
	cbe_spu_info[node].nr_active--;
	if (free_spu)
		spu->alloc_state = SPU_FREE;
	spu_unbind_context(spu, ctx);
	ctx->stats.invol_ctx_switch++;
	spu->stats.invol_ctx_switch++;
	mutex_unlock(&cbe_spu_info[node].list_mutex);
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
	struct spu *spu;

	/*
	 * If there are multiple threads waiting for a single context
	 * only one actually binds the context while the others will
	 * only be able to acquire the state_mutex once the context
	 * already is in runnable state.
	 */
	if (ctx->spu)
		return 0;

spu_activate_top:
	if (signal_pending(current))
		return -ERESTARTSYS;

	spu = spu_get_idle(ctx);
	/*
	 * If this is a realtime thread we try to get it running by
	 * preempting a lower priority thread.
	 */
	if (!spu && rt_prio(ctx->prio))
		spu = find_victim(ctx);
	if (spu) {
		unsigned long runcntl;

		runcntl = ctx->ops->runcntl_read(ctx);
		__spu_schedule(spu, ctx);
		if (runcntl & SPU_RUNCNTL_RUNNABLE)
			spuctx_switch_state(ctx, SPU_UTIL_USER);

		return 0;
	}

	if (ctx->flags & SPU_CREATE_NOSCHED) {
		spu_prio_wait(ctx);
		goto spu_activate_top;
	}

	spu_add_to_rq(ctx);

	return 0;
}

/**
 * grab_runnable_context - try to find a runnable context
 *
 * Remove the highest priority context on the runqueue and return it
 * to the caller.  Returns %NULL if no runnable context was found.
 */
static struct spu_context *grab_runnable_context(int prio, int node)
{
	struct spu_context *ctx;
	int best;

	spin_lock(&spu_prio->runq_lock);
	best = find_first_bit(spu_prio->bitmap, prio);
	while (best < prio) {
		struct list_head *rq = &spu_prio->runq[best];

		list_for_each_entry(ctx, rq, rq) {
			/* XXX(hch): check for affinity here as well */
			if (__node_allowed(ctx, node)) {
				__spu_del_from_rq(ctx);
				goto found;
			}
		}
		best++;
	}
	ctx = NULL;
 found:
	spin_unlock(&spu_prio->runq_lock);
	return ctx;
}

static int __spu_deactivate(struct spu_context *ctx, int force, int max_prio)
{
	struct spu *spu = ctx->spu;
	struct spu_context *new = NULL;

	if (spu) {
		new = grab_runnable_context(max_prio, spu->node);
		if (new || force) {
			spu_unschedule(spu, ctx, new == NULL);
			if (new) {
				if (new->flags & SPU_CREATE_NOSCHED)
					wake_up(&new->stop_wq);
				else {
					spu_release(ctx);
					spu_schedule(spu, new);
					/* this one can't easily be made
					   interruptible */
					mutex_lock(&ctx->state_mutex);
				}
			}
		}
	}

	return new != NULL;
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
	spu_context_nospu_trace(spu_deactivate__enter, ctx);
	__spu_deactivate(ctx, 1, MAX_PRIO);
}

/**
 * spu_yield -	yield a physical spu if others are waiting
 * @ctx:	spu context to yield
 *
 * Check if there is a higher priority context waiting and if yes
 * unbind @ctx from the physical spu and schedule the highest
 * priority context to run on the freed physical spu instead.
 */
void spu_yield(struct spu_context *ctx)
{
	spu_context_nospu_trace(spu_yield__enter, ctx);
	if (!(ctx->flags & SPU_CREATE_NOSCHED)) {
		mutex_lock(&ctx->state_mutex);
		__spu_deactivate(ctx, 0, MAX_PRIO);
		mutex_unlock(&ctx->state_mutex);
	}
}

static noinline void spusched_tick(struct spu_context *ctx)
{
	struct spu_context *new = NULL;
	struct spu *spu = NULL;

	if (spu_acquire(ctx))
		BUG();	/* a kernel thread never has signals pending */

	if (ctx->state != SPU_STATE_RUNNABLE)
		goto out;
	if (ctx->flags & SPU_CREATE_NOSCHED)
		goto out;
	if (ctx->policy == SCHED_FIFO)
		goto out;

	if (--ctx->time_slice && test_bit(SPU_SCHED_SPU_RUN, &ctx->sched_flags))
		goto out;

	spu = ctx->spu;

	spu_context_trace(spusched_tick__preempt, ctx, spu);

	new = grab_runnable_context(ctx->prio + 1, spu->node);
	if (new) {
		spu_unschedule(spu, ctx, 0);
		if (test_bit(SPU_SCHED_SPU_RUN, &ctx->sched_flags))
			spu_add_to_rq(ctx);
	} else {
		spu_context_nospu_trace(spusched_tick__newslice, ctx);
		if (!ctx->time_slice)
			ctx->time_slice++;
	}
out:
	spu_release(ctx);

	if (new)
		spu_schedule(spu, new);
}

/**
 * count_active_contexts - count nr of active tasks
 *
 * Return the number of tasks currently running or waiting to run.
 *
 * Note that we don't take runq_lock / list_mutex here.  Reading
 * a single 32bit value is atomic on powerpc, and we don't care
 * about memory ordering issues here.
 */
static unsigned long count_active_contexts(void)
{
	int nr_active = 0, node;

	for (node = 0; node < MAX_NUMNODES; node++)
		nr_active += cbe_spu_info[node].nr_active;
	nr_active += spu_prio->nr_waiting;

	return nr_active;
}

/**
 * spu_calc_load - update the avenrun load estimates.
 *
 * No locking against reading these values from userspace, as for
 * the CPU loadavg code.
 */
static void spu_calc_load(void)
{
	unsigned long active_tasks; /* fixed-point */

	active_tasks = count_active_contexts() * FIXED_1;
	spu_avenrun[0] = calc_load(spu_avenrun[0], EXP_1, active_tasks);
	spu_avenrun[1] = calc_load(spu_avenrun[1], EXP_5, active_tasks);
	spu_avenrun[2] = calc_load(spu_avenrun[2], EXP_15, active_tasks);
}

static void spusched_wake(struct timer_list *unused)
{
	mod_timer(&spusched_timer, jiffies + SPUSCHED_TICK);
	wake_up_process(spusched_task);
}

static void spuloadavg_wake(struct timer_list *unused)
{
	mod_timer(&spuloadavg_timer, jiffies + LOAD_FREQ);
	spu_calc_load();
}

static int spusched_thread(void *unused)
{
	struct spu *spu;
	int node;

	while (!kthread_should_stop()) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
		for (node = 0; node < MAX_NUMNODES; node++) {
			struct mutex *mtx = &cbe_spu_info[node].list_mutex;

			mutex_lock(mtx);
			list_for_each_entry(spu, &cbe_spu_info[node].spus,
					cbe_list) {
				struct spu_context *ctx = spu->ctx;

				if (ctx) {
					get_spu_context(ctx);
					mutex_unlock(mtx);
					spusched_tick(ctx);
					mutex_lock(mtx);
					put_spu_context(ctx);
				}
			}
			mutex_unlock(mtx);
		}
	}

	return 0;
}

void spuctx_switch_state(struct spu_context *ctx,
		enum spu_utilization_state new_state)
{
	unsigned long long curtime;
	signed long long delta;
	struct spu *spu;
	enum spu_utilization_state old_state;
	int node;

	curtime = ktime_get_ns();
	delta = curtime - ctx->stats.tstamp;

	WARN_ON(!mutex_is_locked(&ctx->state_mutex));
	WARN_ON(delta < 0);

	spu = ctx->spu;
	old_state = ctx->stats.util_state;
	ctx->stats.util_state = new_state;
	ctx->stats.tstamp = curtime;

	/*
	 * Update the physical SPU utilization statistics.
	 */
	if (spu) {
		ctx->stats.times[old_state] += delta;
		spu->stats.times[old_state] += delta;
		spu->stats.util_state = new_state;
		spu->stats.tstamp = curtime;
		node = spu->node;
		if (old_state == SPU_UTIL_USER)
			atomic_dec(&cbe_spu_info[node].busy_spus);
		if (new_state == SPU_UTIL_USER)
			atomic_inc(&cbe_spu_info[node].busy_spus);
	}
}

#ifdef CONFIG_PROC_FS
static int show_spu_loadavg(struct seq_file *s, void *private)
{
	int a, b, c;

	a = spu_avenrun[0] + (FIXED_1/200);
	b = spu_avenrun[1] + (FIXED_1/200);
	c = spu_avenrun[2] + (FIXED_1/200);

	/*
	 * Note that last_pid doesn't really make much sense for the
	 * SPU loadavg (it even seems very odd on the CPU side...),
	 * but we include it here to have a 100% compatible interface.
	 */
	seq_printf(s, "%d.%02d %d.%02d %d.%02d %ld/%d %d\n",
		LOAD_INT(a), LOAD_FRAC(a),
		LOAD_INT(b), LOAD_FRAC(b),
		LOAD_INT(c), LOAD_FRAC(c),
		count_active_contexts(),
		atomic_read(&nr_spu_contexts),
		idr_get_cursor(&task_active_pid_ns(current)->idr) - 1);
	return 0;
}
#endif

int __init spu_sched_init(void)
{
	struct proc_dir_entry *entry;
	int err = -ENOMEM, i;

	spu_prio = kzalloc(sizeof(struct spu_prio_array), GFP_KERNEL);
	if (!spu_prio)
		goto out;

	for (i = 0; i < MAX_PRIO; i++) {
		INIT_LIST_HEAD(&spu_prio->runq[i]);
		__clear_bit(i, spu_prio->bitmap);
	}
	spin_lock_init(&spu_prio->runq_lock);

	timer_setup(&spusched_timer, spusched_wake, 0);
	timer_setup(&spuloadavg_timer, spuloadavg_wake, 0);

	spusched_task = kthread_run(spusched_thread, NULL, "spusched");
	if (IS_ERR(spusched_task)) {
		err = PTR_ERR(spusched_task);
		goto out_free_spu_prio;
	}

	mod_timer(&spuloadavg_timer, 0);

	entry = proc_create_single("spu_loadavg", 0, NULL, show_spu_loadavg);
	if (!entry)
		goto out_stop_kthread;

	pr_debug("spusched: tick: %d, min ticks: %d, default ticks: %d\n",
			SPUSCHED_TICK, MIN_SPU_TIMESLICE, DEF_SPU_TIMESLICE);
	return 0;

 out_stop_kthread:
	kthread_stop(spusched_task);
 out_free_spu_prio:
	kfree(spu_prio);
 out:
	return err;
}

void spu_sched_exit(void)
{
	struct spu *spu;
	int node;

	remove_proc_entry("spu_loadavg", NULL);

	del_timer_sync(&spusched_timer);
	del_timer_sync(&spuloadavg_timer);
	kthread_stop(spusched_task);

	for (node = 0; node < MAX_NUMNODES; node++) {
		mutex_lock(&cbe_spu_info[node].list_mutex);
		list_for_each_entry(spu, &cbe_spu_info[node].spus, cbe_list)
			if (spu->alloc_state != SPU_FREE)
				spu->alloc_state = SPU_FREE;
		mutex_unlock(&cbe_spu_info[node].list_mutex);
	}
	kfree(spu_prio);
}
