/*
 * SPU file system
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
	INIT_LIST_HEAD(&gang->list);

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
	list_del_init(&ctx->gang_list);
	gang->contexts--;
	mutex_unlock(&gang->mutex);

	put_spu_gang(gang);
}
