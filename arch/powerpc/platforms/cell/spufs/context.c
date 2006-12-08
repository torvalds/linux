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
	spu_init_csa(&ctx->csa);
	if (!ctx->csa.lscsa) {
		goto out_free;
	}
	spin_lock_init(&ctx->mmio_lock);
	kref_init(&ctx->kref);
	init_rwsem(&ctx->state_sema);
	init_MUTEX(&ctx->run_sema);
	init_waitqueue_head(&ctx->ibox_wq);
	init_waitqueue_head(&ctx->wbox_wq);
	init_waitqueue_head(&ctx->stop_wq);
	init_waitqueue_head(&ctx->mfc_wq);
	ctx->state = SPU_STATE_SAVED;
	ctx->ops = &spu_backing_ops;
	ctx->owner = get_task_mm(current);
	if (gang)
		spu_gang_add_ctx(gang, ctx);
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
	down_write(&ctx->state_sema);
	spu_deactivate(ctx);
	up_write(&ctx->state_sema);
	spu_fini_csa(&ctx->csa);
	if (ctx->gang)
		spu_gang_remove_ctx(ctx->gang, ctx);
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

void spu_acquire(struct spu_context *ctx)
{
	down_read(&ctx->state_sema);
}

void spu_release(struct spu_context *ctx)
{
	up_read(&ctx->state_sema);
}

void spu_unmap_mappings(struct spu_context *ctx)
{
	if (ctx->local_store)
		unmap_mapping_range(ctx->local_store, 0, LS_SIZE, 1);
	if (ctx->mfc)
		unmap_mapping_range(ctx->mfc, 0, 0x4000, 1);
	if (ctx->cntl)
		unmap_mapping_range(ctx->cntl, 0, 0x4000, 1);
	if (ctx->signal1)
		unmap_mapping_range(ctx->signal1, 0, 0x4000, 1);
	if (ctx->signal2)
		unmap_mapping_range(ctx->signal2, 0, 0x4000, 1);
}

int spu_acquire_exclusive(struct spu_context *ctx)
{
	int ret = 0;

	down_write(&ctx->state_sema);
	/* ctx is about to be freed, can't acquire any more */
	if (!ctx->owner) {
		ret = -EINVAL;
		goto out;
	}

	if (ctx->state == SPU_STATE_SAVED) {
		ret = spu_activate(ctx, 0);
		if (ret)
			goto out;
		ctx->state = SPU_STATE_RUNNABLE;
	} else {
		/* We need to exclude userspace access to the context. */
		spu_unmap_mappings(ctx);
	}

out:
	if (ret)
		up_write(&ctx->state_sema);
	return ret;
}

int spu_acquire_runnable(struct spu_context *ctx)
{
	int ret = 0;

	down_read(&ctx->state_sema);
	if (ctx->state == SPU_STATE_RUNNABLE) {
		ctx->spu->prio = current->prio;
		return 0;
	}
	up_read(&ctx->state_sema);

	down_write(&ctx->state_sema);
	/* ctx is about to be freed, can't acquire any more */
	if (!ctx->owner) {
		ret = -EINVAL;
		goto out;
	}

	if (ctx->state == SPU_STATE_SAVED) {
		ret = spu_activate(ctx, 0);
		if (ret)
			goto out;
		ctx->state = SPU_STATE_RUNNABLE;
	}

	downgrade_write(&ctx->state_sema);
	/* On success, we return holding the lock */

	return ret;
out:
	/* Release here, to simplify calling code. */
	up_write(&ctx->state_sema);

	return ret;
}

void spu_acquire_saved(struct spu_context *ctx)
{
	down_read(&ctx->state_sema);

	if (ctx->state == SPU_STATE_SAVED)
		return;

	up_read(&ctx->state_sema);
	down_write(&ctx->state_sema);

	if (ctx->state == SPU_STATE_RUNNABLE) {
		spu_deactivate(ctx);
		ctx->state = SPU_STATE_SAVED;
	}

	downgrade_write(&ctx->state_sema);
}
