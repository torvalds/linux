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
#include <asm/spu.h>
#include <asm/spu_csa.h>
#include "spufs.h"

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
	ctx->state = SPU_STATE_SAVED;
	ctx->ops = &spu_backing_ops;
	ctx->owner = get_task_mm(current);
	INIT_LIST_HEAD(&ctx->rq);
	if (gang)
		spu_gang_add_ctx(gang, ctx);
	ctx->rt_priority = current->rt_priority;
	ctx->policy = current->policy;
	ctx->prio = current->prio;
	INIT_DELAYED_WORK(&ctx->sched_work, spu_sched_tick);
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
	mutex_lock(&ctx->state_mutex);
	spu_deactivate(ctx);
	mutex_unlock(&ctx->state_mutex);
	spu_fini_csa(&ctx->csa);
	if (ctx->gang)
		spu_gang_remove_ctx(ctx->gang, ctx);
	BUG_ON(!list_empty(&ctx->rq));
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
	spu_acquire_saved(ctx);
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
		unmap_mapping_range(ctx->mfc, 0, 0x1000, 1);
	if (ctx->cntl)
		unmap_mapping_range(ctx->cntl, 0, 0x1000, 1);
	if (ctx->signal1)
		unmap_mapping_range(ctx->signal1, 0, PAGE_SIZE, 1);
	if (ctx->signal2)
		unmap_mapping_range(ctx->signal2, 0, PAGE_SIZE, 1);
	if (ctx->mss)
		unmap_mapping_range(ctx->mss, 0, 0x1000, 1);
	if (ctx->psmap)
		unmap_mapping_range(ctx->psmap, 0, 0x20000, 1);
	mutex_unlock(&ctx->mapping_lock);
}

/**
 * spu_acquire_runnable - lock spu contex and make sure it is in runnable state
 * @ctx:	spu contex to lock
 *
 * Note:
 *	Returns 0 and with the context locked on success
 *	Returns negative error and with the context _unlocked_ on failure.
 */
int spu_acquire_runnable(struct spu_context *ctx, unsigned long flags)
{
	int ret = -EINVAL;

	spu_acquire(ctx);
	if (ctx->state == SPU_STATE_SAVED) {
		/*
		 * Context is about to be freed, so we can't acquire it anymore.
		 */
		if (!ctx->owner)
			goto out_unlock;
		ret = spu_activate(ctx, flags);
		if (ret)
			goto out_unlock;
	}

	return 0;

 out_unlock:
	spu_release(ctx);
	return ret;
}

/**
 * spu_acquire_saved - lock spu contex and make sure it is in saved state
 * @ctx:	spu contex to lock
 */
void spu_acquire_saved(struct spu_context *ctx)
{
	spu_acquire(ctx);
	if (ctx->state != SPU_STATE_SAVED)
		spu_deactivate(ctx);
}
