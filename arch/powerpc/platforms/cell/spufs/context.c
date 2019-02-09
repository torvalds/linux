/*
 * SPU file system -- SPU context management
 *
 * (C) Copyright IBM Deutschland Entwicklung GmbH 2005
 *
 * Author: Arnd Bergmann <arndb@de.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/atomic.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>

#include <asm/spu.h>
#include <asm/spu_csa.h>
#include "spufs.h"
#include "sputrace.h"


atomic_t nr_spu_contexts = ATOMIC_INIT(0);

struct spu_context *alloc_spu_context(struct spu_gang *gang)
{
	struct spu_context *ctx;

	ctx = kzalloc(sizeof *ctx, GFP_KERNEL);
	if (!ctx)
		goto out;
	/* Binding to physical processor deferred
	 * until spu_activate().
	 */
	if (spu_init_csa(&ctx->csa))
		goto out_free;
	spin_lock_init(&ctx->mmio_lock);
	mutex_init(&ctx->mapping_lock);
	kref_init(&ctx->kref);
	mutex_init(&ctx->state_mutex);
	mutex_init(&ctx->run_mutex);
	init_waitqueue_head(&ctx->ibox_wq);
	init_waitqueue_head(&ctx->wbox_wq);
	init_waitqueue_head(&ctx->stop_wq);
	init_waitqueue_head(&ctx->mfc_wq);
	init_waitqueue_head(&ctx->run_wq);
	ctx->state = SPU_STATE_SAVED;
	ctx->ops = &spu_backing_ops;
	ctx->owner = get_task_mm(current);
	INIT_LIST_HEAD(&ctx->rq);
	INIT_LIST_HEAD(&ctx->aff_list);
	if (gang)
		spu_gang_add_ctx(gang, ctx);

	__spu_update_sched_info(ctx);
	spu_set_timeslice(ctx);
	ctx->stats.util_state = SPU_UTIL_IDLE_LOADED;
	ctx->stats.tstamp = ktime_get_ns();

	atomic_inc(&nr_spu_contexts);
	goto out;
out_free:
	kfree(ctx);
	ctx = NULL;
out:
	return ctx;
}

void destroy_spu_context(struct kref *kref)
{
	struct spu_context *ctx;
	ctx = container_of(kref, struct spu_context, kref);
	spu_context_nospu_trace(destroy_spu_context__enter, ctx);
	mutex_lock(&ctx->state_mutex);
	spu_deactivate(ctx);
	mutex_unlock(&ctx->state_mutex);
	spu_fini_csa(&ctx->csa);
	if (ctx->gang)
		spu_gang_remove_ctx(ctx->gang, ctx);
	if (ctx->prof_priv_kref)
		kref_put(ctx->prof_priv_kref, ctx->prof_priv_release);
	BUG_ON(!list_empty(&ctx->rq));
	atomic_dec(&nr_spu_contexts);
	kfree(ctx->switch_log);
	kfree(ctx);
}

struct spu_context * get_spu_context(struct spu_context *ctx)
{
	kref_get(&ctx->kref);
	return ctx;
}

int put_spu_context(struct spu_context *ctx)
{
	return kref_put(&ctx->kref, &destroy_spu_context);
}

/* give up the mm reference when the context is about to be destroyed */
void spu_forget(struct spu_context *ctx)
{
	struct mm_struct *mm;

	/*
	 * This is basically an open-coded spu_acquire_saved, except that
	 * we don't acquire the state mutex interruptible, and we don't
	 * want this context to be rescheduled on release.
	 */
	mutex_lock(&ctx->state_mutex);
	if (ctx->state != SPU_STATE_SAVED)
		spu_deactivate(ctx);

	mm = ctx->owner;
	ctx->owner = NULL;
	mmput(mm);
	spu_release(ctx);
}

void spu_unmap_mappings(struct spu_context *ctx)
{
	mutex_lock(&ctx->mapping_lock);
	if (ctx->local_store)
		unmap_mapping_range(ctx->local_store, 0, LS_SIZE, 1);
	if (ctx->mfc)
		unmap_mapping_range(ctx->mfc, 0, SPUFS_MFC_MAP_SIZE, 1);
	if (ctx->cntl)
		unmap_mapping_range(ctx->cntl, 0, SPUFS_CNTL_MAP_SIZE, 1);
	if (ctx->signal1)
		unmap_mapping_range(ctx->signal1, 0, SPUFS_SIGNAL_MAP_SIZE, 1);
	if (ctx->signal2)
		unmap_mapping_range(ctx->signal2, 0, SPUFS_SIGNAL_MAP_SIZE, 1);
	if (ctx->mss)
		unmap_mapping_range(ctx->mss, 0, SPUFS_MSS_MAP_SIZE, 1);
	if (ctx->psmap)
		unmap_mapping_range(ctx->psmap, 0, SPUFS_PS_MAP_SIZE, 1);
	mutex_unlock(&ctx->mapping_lock);
}

/**
 * spu_acquire_saved - lock spu contex and make sure it is in saved state
 * @ctx:	spu contex to lock
 */
int spu_acquire_saved(struct spu_context *ctx)
{
	int ret;

	spu_context_nospu_trace(spu_acquire_saved__enter, ctx);

	ret = spu_acquire(ctx);
	if (ret)
		return ret;

	if (ctx->state != SPU_STATE_SAVED) {
		set_bit(SPU_SCHED_WAS_ACTIVE, &ctx->sched_flags);
		spu_deactivate(ctx);
	}

	return 0;
}

/**
 * spu_release_saved - unlock spu context and return it to the runqueue
 * @ctx:	context to unlock
 */
void spu_release_saved(struct spu_context *ctx)
{
	BUG_ON(ctx->state != SPU_STATE_SAVED);

	if (test_and_clear_bit(SPU_SCHED_WAS_ACTIVE, &ctx->sched_flags) &&
			test_bit(SPU_SCHED_SPU_RUN, &ctx->sched_flags))
		spu_activate(ctx, 0);

	spu_release(ctx);
}

