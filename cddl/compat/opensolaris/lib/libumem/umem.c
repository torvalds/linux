/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 *
 * $FreeBSD$
 */
/*
 * Copyright 2006 Ricardo Correia.  All rights reserved.
 * Use is subject to license terms.
 */

#include <umem.h>
#include <stdlib.h>
#include <assert.h>

static umem_nofail_callback_t *nofail_cb = NULL;

struct umem_cache {
	umem_constructor_t *constructor;
	umem_destructor_t *destructor;
	void *callback_data;
	size_t bufsize;
};

/*
 * Simple stub for umem_alloc(). The callback isn't expected to return.
 */
void *umem_alloc(size_t size, int flags)
{
	assert(flags == UMEM_DEFAULT || flags == UMEM_NOFAIL);

	if(size == 0)
		return NULL;

	void *ret = malloc(size);
	if(ret == NULL) {
		if(!(flags & UMEM_NOFAIL))
			return NULL;

		if(nofail_cb != NULL)
			nofail_cb();
		abort();
	}

	return ret;
}

/*
 * Simple stub for umem_zalloc().
 */
void *umem_zalloc(size_t size, int flags)
{
	assert(flags == UMEM_DEFAULT || flags == UMEM_NOFAIL);

	if(size == 0)
		return NULL;

	void *ret = calloc(1, size);
	if(ret == NULL) {
		if(!(flags & UMEM_NOFAIL))
			return NULL;

		if(nofail_cb != NULL)
			nofail_cb();
		abort();
	}

	return ret;
}

/*
 * Simple stub for umem_free().
 */
void umem_free(void *buf, size_t size)
{
	free(buf);
}

/*
 * Simple stub for umem_nofail_callback().
 */
void umem_nofail_callback(umem_nofail_callback_t *callback)
{
	nofail_cb = callback;
}

/*
 * Simple stub for umem_cache_create().
 */
umem_cache_t *umem_cache_create(char *debug_name, size_t bufsize, size_t align, umem_constructor_t *constructor, umem_destructor_t *destructor, umem_reclaim_t *reclaim, void *callback_data, void *source, int cflags)
{
	assert(source == NULL);

	umem_cache_t *cache = malloc(sizeof(umem_cache_t));
	if(cache == NULL)
		return NULL;

	cache->constructor = constructor;
	cache->destructor = destructor;
	cache->callback_data = callback_data;
	cache->bufsize = bufsize;

	return cache;
}

/*
 * Simple stub for umem_cache_alloc(). The nofail callback isn't expected to return.
 */
void *umem_cache_alloc(umem_cache_t *cache, int flags)
{
	void *buf = malloc(cache->bufsize);
	if(buf == NULL) {
		if(!(flags & UMEM_NOFAIL))
			return NULL;

		if(nofail_cb != NULL)
			nofail_cb();
		abort();
	}

	if(cache->constructor != NULL) {
		if(cache->constructor(buf, cache->callback_data, flags) != 0) {
			free(buf);
			if(!(flags & UMEM_NOFAIL))
				return NULL;

			if(nofail_cb != NULL)
				nofail_cb();
			abort();
		}
	}

	return buf;
}

/*
 * Simple stub for umem_cache_free().
 */
void umem_cache_free(umem_cache_t *cache, void *buffer)
{
	if(cache->destructor != NULL)
		cache->destructor(buffer, cache->callback_data);

	free(buffer);
}

/*
 * Simple stub for umem_cache_destroy().
 */
void umem_cache_destroy(umem_cache_t *cache)
{
	free(cache);
}
