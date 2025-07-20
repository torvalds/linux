// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SPU file system
 *
 * (C) Copyright IBM Deutschland Entwicklung GmbH 2005
 *
 * Author: Arnd Bergmann <arndb@de.ibm.com>
 */

#include <linux/list.h>
#include <linux/slab.h>

#include "spufs.h"

struct spu_gang *alloc_spu_gang(void)
{
	struct spu_gang *gang;

	gang = kzalloc(sizeof *gang, GFP_KERNEL);
	if (!gang)
		goto out;

	kref_init(&gang->kref);
	mutex_init(&gang->mutex);
	mutex_init(&gang->aff_mutex);
	INIT_LIST_HEAD(&gang->list);
	INIT_LIST_HEAD(&gang->aff_list_head);
	gang->alive = 1;

out:
	return gang;
}

static void destroy_spu_gang(struct kref *kref)
{
	struct spu_gang *gang;
	gang = container_of(kref, struct spu_gang, kref);
	WARN_ON(gang->contexts || !list_empty(&gang->list));
	kfree(gang);
}

struct spu_gang *get_spu_gang(struct spu_gang *gang)
{
	kref_get(&gang->kref);
	return gang;
}

int put_spu_gang(struct spu_gang *gang)
{
	return kref_put(&gang->kref, &destroy_spu_gang);
}

void spu_gang_add_ctx(struct spu_gang *gang, struct spu_context *ctx)
{
	mutex_lock(&gang->mutex);
	ctx->gang = get_spu_gang(gang);
	list_add(&ctx->gang_list, &gang->list);
	gang->contexts++;
	mutex_unlock(&gang->mutex);
}

void spu_gang_remove_ctx(struct spu_gang *gang, struct spu_context *ctx)
{
	mutex_lock(&gang->mutex);
	WARN_ON(ctx->gang != gang);
	if (!list_empty(&ctx->aff_list)) {
		list_del_init(&ctx->aff_list);
		gang->aff_flags &= ~AFF_OFFSETS_SET;
	}
	list_del_init(&ctx->gang_list);
	gang->contexts--;
	mutex_unlock(&gang->mutex);

	put_spu_gang(gang);
}
