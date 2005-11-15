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

#include <linux/slab.h>
#include <asm/spu.h>
#include "spufs.h"

struct spu_context *alloc_spu_context(void)
{
	struct spu_context *ctx;
	ctx = kmalloc(sizeof *ctx, GFP_KERNEL);
	if (!ctx)
		goto out;
	ctx->spu = spu_alloc();
	if (!ctx->spu)
		goto out_free;
	init_rwsem(&ctx->backing_sema);
	spin_lock_init(&ctx->mmio_lock);
	kref_init(&ctx->kref);
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
	if (ctx->spu)
		spu_free(ctx->spu);
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


