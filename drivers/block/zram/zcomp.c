/*
 * Copyright (C) 2014 Sergey Senozhatsky.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/sched.h>

#include "zcomp.h"
#include "zcomp_lzo.h"

/*
 * single zcomp_strm backend
 */
struct zcomp_strm_single {
	struct mutex strm_lock;
	struct zcomp_strm *zstrm;
};

static struct zcomp_backend *find_backend(const char *compress)
{
	if (strncmp(compress, "lzo", 3) == 0)
		return &zcomp_lzo;
	return NULL;
}

static void zcomp_strm_free(struct zcomp *comp, struct zcomp_strm *zstrm)
{
	if (zstrm->private)
		comp->backend->destroy(zstrm->private);
	free_pages((unsigned long)zstrm->buffer, 1);
	kfree(zstrm);
}

/*
 * allocate new zcomp_strm structure with ->private initialized by
 * backend, return NULL on error
 */
static struct zcomp_strm *zcomp_strm_alloc(struct zcomp *comp)
{
	struct zcomp_strm *zstrm = kmalloc(sizeof(*zstrm), GFP_KERNEL);
	if (!zstrm)
		return NULL;

	zstrm->private = comp->backend->create();
	/*
	 * allocate 2 pages. 1 for compressed data, plus 1 extra for the
	 * case when compressed size is larger than the original one
	 */
	zstrm->buffer = (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO, 1);
	if (!zstrm->private || !zstrm->buffer) {
		zcomp_strm_free(comp, zstrm);
		zstrm = NULL;
	}
	return zstrm;
}

static struct zcomp_strm *zcomp_strm_single_find(struct zcomp *comp)
{
	struct zcomp_strm_single *zs = comp->stream;
	mutex_lock(&zs->strm_lock);
	return zs->zstrm;
}

static void zcomp_strm_single_release(struct zcomp *comp,
		struct zcomp_strm *zstrm)
{
	struct zcomp_strm_single *zs = comp->stream;
	mutex_unlock(&zs->strm_lock);
}

static void zcomp_strm_single_destroy(struct zcomp *comp)
{
	struct zcomp_strm_single *zs = comp->stream;
	zcomp_strm_free(comp, zs->zstrm);
	kfree(zs);
}

static int zcomp_strm_single_create(struct zcomp *comp)
{
	struct zcomp_strm_single *zs;

	comp->destroy = zcomp_strm_single_destroy;
	comp->strm_find = zcomp_strm_single_find;
	comp->strm_release = zcomp_strm_single_release;
	zs = kmalloc(sizeof(struct zcomp_strm_single), GFP_KERNEL);
	if (!zs)
		return -ENOMEM;

	comp->stream = zs;
	mutex_init(&zs->strm_lock);
	zs->zstrm = zcomp_strm_alloc(comp);
	if (!zs->zstrm) {
		kfree(zs);
		return -ENOMEM;
	}
	return 0;
}

struct zcomp_strm *zcomp_strm_find(struct zcomp *comp)
{
	return comp->strm_find(comp);
}

void zcomp_strm_release(struct zcomp *comp, struct zcomp_strm *zstrm)
{
	comp->strm_release(comp, zstrm);
}

int zcomp_compress(struct zcomp *comp, struct zcomp_strm *zstrm,
		const unsigned char *src, size_t *dst_len)
{
	return comp->backend->compress(src, zstrm->buffer, dst_len,
			zstrm->private);
}

int zcomp_decompress(struct zcomp *comp, const unsigned char *src,
		size_t src_len, unsigned char *dst)
{
	return comp->backend->decompress(src, src_len, dst);
}

void zcomp_destroy(struct zcomp *comp)
{
	comp->destroy(comp);
	kfree(comp);
}

/*
 * search available compressors for requested algorithm.
 * allocate new zcomp and initialize it. return NULL
 * if requested algorithm is not supported or in case
 * of init error
 */
struct zcomp *zcomp_create(const char *compress)
{
	struct zcomp *comp;
	struct zcomp_backend *backend;

	backend = find_backend(compress);
	if (!backend)
		return NULL;

	comp = kzalloc(sizeof(struct zcomp), GFP_KERNEL);
	if (!comp)
		return NULL;

	comp->backend = backend;
	if (zcomp_strm_single_create(comp) != 0) {
		kfree(comp);
		return NULL;
	}
	return comp;
}
