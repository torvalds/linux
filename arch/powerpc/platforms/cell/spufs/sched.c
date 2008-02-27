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
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/numa.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/kthread.h>
#include <linux/pid_namespace.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/marker.h>

#include <asm/io.h>
#include <asm/mmu_context.h>
#include <asm/spu.h>
#include <asm/spu_csa.h>
#include <asm/spu_priv1.h>
#include "spufs.h"

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

#define MAX_USER_PRIO		(MAX_PRIO - MAX_RT_PRIO)
#define SCALE_PRIO(x, prio) \
	max(x * (MAX_PRIO - prio) / (MAX_USER_PRIO / 2), MIN_SPU_TIMESLICE)

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
	ctx->cpus_allowed = current->cpus_allowed;
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
		cpumask_t mask = node_to_cpumask(node);

		if (cpus_intersects(mask, ctx->cpus_allowed))
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
	 *
	 * When the awakened processes see their "notify_active" flag is set,
	 * they will call spu_switch_notify().
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

	spu->ctx = ctx;
	spu->flags = 0;
	ctx->spu = spu;
	ctx->ops = &spu_hw_ops;
	spu->pid = current->pid;
	spu->tgid = current->tgid;
	spu_associate_mm(spu, ctx->owner);
	spu->ibox_callback = spufs_ibox_callback;
	spu->wbox_callback = spufs_wbox_callback;
	spu->stop_callback = spufs_stop_callback;
	spu->mfc_callback = spufs_mfc_callback;
	mb();
	spu_unmap_mappings(ctx);
	spu_restore(&ctx->csa, spu);
	spu->timestamp = jiffies;
	spu_cpu_affinity_set(spu, raw_smp_processor_id());
	spu_switch_notify(spu, ctx);
	ctx->state = SPU_STATE_RUNNABLE;

	spuctx_switch_state(ctx, SPU_UTIL_IDLE_LOADED);
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
		node = (node < MAX_NUMNODES) ? node : 0;
		if (!node_allowed(ctx, node))
			continue;
		mutex_lock(&cbe_spu_info[node].list_mutex);
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
	struct spu_context *ctx;
	struct spu *tmp;

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
	spu_context_trace(spu_unbind_context__enter, ctx, spu);

	spuctx_switch_state(ctx, SPU_UTIL_SYSTEM);

 	if (spu->ctx->flags & SPU_CREATE_NOSCHED)
		atomic_dec(&cbe_spu_info[spu->node].reserved_spus);

	if (ctx->gang){
		mutex_lock(&ctx->gang->aff_mutex);
		if (has_affinity(ctx)) {
			if (atomic_dec_and_test(&ctx->gang->aff_sched_count))
				ctx->gang->aff_ref_spu = NULL;
		}
		mutex_unlock(&ctx->gang->aff_mutex);
	}

	spu_switch_notify(spu, NULL);
	spu_unmap_mappings(ctx);
	spu_save(&ctx->csa, spu);
	spu->timestamp = jiffies;
	ctx->state = SPU_STATE_SAVED;
	spu->ibox_callback = NULL;
	spu->wbox_callback = NULL;
	spu->stop_callback = NULL;
	spu->mfc_callback = NULL;
	spu_associate_mm(spu, NULL);
	spu->pid = 0;
	spu->tgid = 0;
	ctx->ops = &spu_backing_ops;
	spu->flags = 0;
	spu->ctx = NULL;

	ctx->stats.slb_flt +=
		(spu->stats.slb_flt - ctx->stats.slb_flt_base);
	ctx->stats.class2_intr +=
		(spu->stats.class2_intr - ctx->stats.class2_intr_base);

	/* This maps the underlying spu state to idle */
	spuctx_switch_state(ctx, SPU_UTIL_IDLE_LOADED);
	ctx->spu = NULL;
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
			__mod_timer(&spusched_timer, jiffies + SPUSCHED_TICK);
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

			mutex_lock(&ctx->gang->aff_mutex);
			if (atomic_dec_and_test(&ctx->gang->aff_sched_count))
				ctx->gang->aff_ref_spu = NULL;
			mutex_unlock(&ctx->gang->aff_mutex);
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
 * @ctx:	canidate context for running
 *
 * Returns the freed physical spu to run the new context on.
 */
static struct spu *find_victim(struct spu_context *ctx)
{
	struct spu_context *victim = NULL;
	struct spu *spu;
	int node, n;

	spu_context_nospu_trace(spu_find_vitim__enter, ctx);

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
			    (!victim || tmp->prio > victim->prio))
				victim = spu->ctx;
		}
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
			spu_add_to_rq(victim);

			mutex_unlock(&victim->state_mutex);

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
	__spu_schedule(spu, ctx);
	spu_release(ctx);
}

static void spu_unschedule(struct spu *spu, struct spu_context *ctx)
{
	int node = spu->node;

	mutex_lock(&cbe_spu_info[node].list_mutex);
	cbe_spu_info[node].nr_active--;
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
			/* XXX(hch): check for affinity here aswell */
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
			spu_unschedule(spu, ctx);
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

	if (--ctx->time_slice && ctx->policy != SCHED_IDLE)
		goto out;

	spu = ctx->spu;

	spu_context_trace(spusched_tick__preempt, ctx, spu);

	new = grab_runnable_context(ctx->prio + 1, spu->node);
	if (new) {
		spu_unschedule(spu, ctx);
		if (ctx->policy != SCHED_IDLE)
			spu_add_to_rq(ctx);
	} else {
		spu_context_nospu_trace(spusched_tick__newslice, ctx);
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
	CALC_LOAD(spu_avenrun[0], EXP_1, active_tasks);
	CALC_LOAD(spu_avenrun[1], EXP_5, active_tasks);
	CALC_LOAD(spu_avenrun[2], EXP_15, active_tasks);
}

static void spusched_wake(unsigned long data)
{
	mod_timer(&spusched_timer, jiffies + SPUSCHED_TICK);
	wake_up_process(spusched_task);
}

static void spuloadavg_wake(unsigned long data)
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
					mutex_unlock(mtx);
					spusched_tick(ctx);
					mutex_lock(mtx);
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
	struct timespec ts;
	struct spu *spu;
	enum spu_utilization_state old_state;

	ktime_get_ts(&ts);
	curtime = timespec_to_ns(&ts);
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
	}
}

#define LOAD_INT(x) ((x) >> FSHIFT)
#define LOAD_FRAC(x) LOAD_INT(((x) & (FIXED_1-1)) * 100)

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
		current->nsproxy->pid_ns->last_pid);
	return 0;
}

static int spu_loadavg_open(struct inode *inode, struct file *file)
{
	return single_open(file, show_spu_loadavg, NULL);
}

static const struct file_operations spu_loadavg_fops = {
	.open		= spu_loadavg_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

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

	setup_timer(&spusched_timer, spusched_wake, 0);
	setup_timer(&spuloadavg_timer, spuloadavg_wake, 0);

	spusched_task = kthread_run(spusched_thread, NULL, "spusched");
	if (IS_ERR(spusched_task)) {
		err = PTR_ERR(spusched_task);
		goto out_free_spu_prio;
	}

	mod_timer(&spuloadavg_timer, 0);

	entry = create_proc_entry("spu_loadavg", 0, NULL);
	if (!entry)
		goto out_stop_kthread;
	entry->proc_fops = &spu_loadavg_fops;

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
