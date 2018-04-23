/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010-2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include "ia_css_rmgr.h"

#include <type_support.h>
#include <assert_support.h>
#include <platform_support.h> /* memset */
#include <memory_access.h>    /* mmmgr_malloc, mhmm_free */
#include <ia_css_debug.h>

/*
 * @brief VBUF resource handles
 */
#define NUM_HANDLES 1000
static struct ia_css_rmgr_vbuf_handle handle_table[NUM_HANDLES];

/*
 * @brief VBUF resource pool - refpool
 */
static struct ia_css_rmgr_vbuf_pool refpool = {
	false,			/* copy_on_write */
	false,			/* recycle */
	0,			/* size */
	0,			/* index */
	NULL,			/* handles */
};

/*
 * @brief VBUF resource pool - writepool
 */
static struct ia_css_rmgr_vbuf_pool writepool = {
	true,			/* copy_on_write */
	false,			/* recycle */
	0,			/* size */
	0,			/* index */
	NULL,			/* handles */
};

/*
 * @brief VBUF resource pool - hmmbufferpool
 */
static struct ia_css_rmgr_vbuf_pool hmmbufferpool = {
	true,			/* copy_on_write */
	true,			/* recycle */
	32,			/* size */
	0,			/* index */
	NULL,			/* handles */
};

struct ia_css_rmgr_vbuf_pool *vbuf_ref = &refpool;
struct ia_css_rmgr_vbuf_pool *vbuf_write = &writepool;
struct ia_css_rmgr_vbuf_pool *hmm_buffer_pool = &hmmbufferpool;

/*
 * @brief Initialize the reference count (host, vbuf)
 */
static void rmgr_refcount_init_vbuf(void)
{
	/* initialize the refcount table */
	memset(&handle_table, 0, sizeof(handle_table));
}

/*
 * @brief Retain the reference count for a handle (host, vbuf)
 *
 * @param handle	The pointer to the handle
 */
void ia_css_rmgr_refcount_retain_vbuf(struct ia_css_rmgr_vbuf_handle **handle)
{
	int i;
	struct ia_css_rmgr_vbuf_handle *h;
	if ((handle == NULL) || (*handle == NULL)) {
		IA_CSS_LOG("Invalid inputs");
		return;
	}
	/* new vbuf to count on */
	if ((*handle)->count == 0) {
		h = *handle;
		*handle = NULL;
		for (i = 0; i < NUM_HANDLES; i++) {
			if (handle_table[i].count == 0) {
				*handle = &handle_table[i];
				break;
			}
		}
		/* if the loop dus not break and *handle == NULL
		   this is an error handle and report it.
		 */
		if (*handle == NULL) {
			ia_css_debug_dtrace(IA_CSS_DEBUG_ERROR,
				"ia_css_i_host_refcount_retain_vbuf() failed to find empty slot!\n");
			return;
		}
		(*handle)->vptr = h->vptr;
		(*handle)->size = h->size;
	}
	(*handle)->count++;
}

/*
 * @brief Release the reference count for a handle (host, vbuf)
 *
 * @param handle	The pointer to the handle
 */
void ia_css_rmgr_refcount_release_vbuf(struct ia_css_rmgr_vbuf_handle **handle)
{
	if ((handle == NULL) || ((*handle) == NULL) || (((*handle)->count) == 0)) {
		ia_css_debug_dtrace(IA_CSS_DEBUG_ERROR,
				    "ia_css_rmgr_refcount_release_vbuf() invalid arguments!\n");
		return;
	}
	/* decrease reference count */
	(*handle)->count--;
	/* remove from admin */
	if ((*handle)->count == 0) {
		(*handle)->vptr = 0x0;
		(*handle)->size = 0;
		*handle = NULL;
	}
}

/*
 * @brief Initialize the resource pool (host, vbuf)
 *
 * @param pool	The pointer to the pool
 */
enum ia_css_err ia_css_rmgr_init_vbuf(struct ia_css_rmgr_vbuf_pool *pool)
{
	enum ia_css_err err = IA_CSS_SUCCESS;
	size_t bytes_needed;
	rmgr_refcount_init_vbuf();
	assert(pool != NULL);
	if (pool == NULL)
		return IA_CSS_ERR_INVALID_ARGUMENTS;
	/* initialize the recycle pool if used */
	if (pool->recycle && pool->size) {
		/* allocate memory for storing the handles */
		bytes_needed =
		    sizeof(void *) *
		    pool->size;
		pool->handles = sh_css_malloc(bytes_needed);
		if (pool->handles != NULL)
			memset(pool->handles, 0, bytes_needed);
		else
			err = IA_CSS_ERR_CANNOT_ALLOCATE_MEMORY;
	} else {
		/* just in case, set the size to 0 */
		pool->size = 0;
		pool->handles = NULL;
	}
	return err;
}

/*
 * @brief Uninitialize the resource pool (host, vbuf)
 *
 * @param pool	The pointer to the pool
 */
void ia_css_rmgr_uninit_vbuf(struct ia_css_rmgr_vbuf_pool *pool)
{
	uint32_t i;
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "ia_css_rmgr_uninit_vbuf()\n");
	if (pool == NULL) {
		ia_css_debug_dtrace(IA_CSS_DEBUG_ERROR, "ia_css_rmgr_uninit_vbuf(): NULL argument\n");
		return;
	}
	if (pool->handles != NULL) {
		/* free the hmm buffers */
		for (i = 0; i < pool->size; i++) {
			if (pool->handles[i] != NULL) {
				ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
				      "   freeing/releasing %x (count=%d)\n",
				      pool->handles[i]->vptr,
				      pool->handles[i]->count);
				/* free memory */
				hmm_free(pool->handles[i]->vptr);
				/* remove from refcount admin */
				ia_css_rmgr_refcount_release_vbuf(
					&pool->handles[i]);
			}
		}
		/* now free the pool handles list */
		sh_css_free(pool->handles);
		pool->handles = NULL;
	}
}

/*
 * @brief Push a handle to the pool
 *
 * @param pool		The pointer to the pool
 * @param handle	The pointer to the handle
 */
static
void rmgr_push_handle(struct ia_css_rmgr_vbuf_pool *pool,
		      struct ia_css_rmgr_vbuf_handle **handle)
{
	uint32_t i;
	bool succes = false;
	assert(pool != NULL);
	assert(pool->recycle);
	assert(pool->handles != NULL);
	assert(handle != NULL);
	for (i = 0; i < pool->size; i++) {
		if (pool->handles[i] == NULL) {
			ia_css_rmgr_refcount_retain_vbuf(handle);
			pool->handles[i] = *handle;
			succes = true;
			break;
		}
	}
	assert(succes);
}

/*
 * @brief Pop a handle from the pool
 *
 * @param pool		The pointer to the pool
 * @param handle	The pointer to the handle
 */
static
void rmgr_pop_handle(struct ia_css_rmgr_vbuf_pool *pool,
		     struct ia_css_rmgr_vbuf_handle **handle)
{
	uint32_t i;
	bool succes = false;
	assert(pool != NULL);
	assert(pool->recycle);
	assert(pool->handles != NULL);
	assert(handle != NULL);
	assert(*handle != NULL);
	for (i = 0; i < pool->size; i++) {
		if ((pool->handles[i] != NULL) &&
		    (pool->handles[i]->size == (*handle)->size)) {
			*handle = pool->handles[i];
			pool->handles[i] = NULL;
			/* dont release, we are returning it...
			   ia_css_rmgr_refcount_release_vbuf(handle); */
			succes = true;
			break;
		}
	}
}

/*
 * @brief Acquire a handle from the pool (host, vbuf)
 *
 * @param pool		The pointer to the pool
 * @param handle	The pointer to the handle
 */
void ia_css_rmgr_acq_vbuf(struct ia_css_rmgr_vbuf_pool *pool,
			  struct ia_css_rmgr_vbuf_handle **handle)
{
	struct ia_css_rmgr_vbuf_handle h;

	if ((pool == NULL) || (handle == NULL) || (*handle == NULL)) {
		IA_CSS_LOG("Invalid inputs");
		return;
	}

	if (pool->copy_on_write) {
		/* only one reference, reuse (no new retain) */
		if ((*handle)->count == 1)
			return;
		/* more than one reference, release current buffer */
		if ((*handle)->count > 1) {
			/* store current values */
			h.vptr = 0x0;
			h.size = (*handle)->size;
			/* release ref to current buffer */
			ia_css_rmgr_refcount_release_vbuf(handle);
			*handle = &h;
		}
		/* get new buffer for needed size */
		if ((*handle)->vptr == 0x0) {
			if (pool->recycle) {
				/* try and pop from pool */
				rmgr_pop_handle(pool, handle);
			}
			if ((*handle)->vptr == 0x0) {
				/* we need to allocate */
				(*handle)->vptr = mmgr_malloc((*handle)->size);
			} else {
				/* we popped a buffer */
				return;
			}
		}
	}
	/* Note that handle will change to an internally maintained one */
	ia_css_rmgr_refcount_retain_vbuf(handle);
}

/*
 * @brief Release a handle to the pool (host, vbuf)
 *
 * @param pool		The pointer to the pool
 * @param handle	The pointer to the handle
 */
void ia_css_rmgr_rel_vbuf(struct ia_css_rmgr_vbuf_pool *pool,
			  struct ia_css_rmgr_vbuf_handle **handle)
{
	if ((pool == NULL) || (handle == NULL) || (*handle == NULL)) {
		IA_CSS_LOG("Invalid inputs");
		return;
	}
	/* release the handle */
	if ((*handle)->count == 1) {
		if (!pool->recycle) {
			/* non recycling pool, free mem */
			hmm_free((*handle)->vptr);
		} else {
			/* recycle to pool */
			rmgr_push_handle(pool, handle);
		}
	}
	ia_css_rmgr_refcount_release_vbuf(handle);
	*handle = NULL;
}
