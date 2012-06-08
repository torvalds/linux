/*
 * zsmalloc memory allocator
 *
 * Copyright (C) 2011  Nitin Gupta
 *
 * This code is released using a dual license strategy: BSD/GPL
 * You can choose the license that better fits your requirements.
 *
 * Released under the terms of 3-clause BSD License
 * Released under the terms of GNU General Public License Version 2.0
 */

#ifndef _ZS_MALLOC_H_
#define _ZS_MALLOC_H_

#include <linux/types.h>

struct zs_pool;

struct zs_pool *zs_create_pool(const char *name, gfp_t flags);
void zs_destroy_pool(struct zs_pool *pool);

unsigned long zs_malloc(struct zs_pool *pool, size_t size);
void zs_free(struct zs_pool *pool, unsigned long obj);

void *zs_map_object(struct zs_pool *pool, unsigned long handle);
void zs_unmap_object(struct zs_pool *pool, unsigned long handle);

u64 zs_get_total_size_bytes(struct zs_pool *pool);

#endif
