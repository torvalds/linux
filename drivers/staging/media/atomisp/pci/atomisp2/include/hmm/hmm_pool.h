/*
 * Support for Medifield PNW Camera Imaging ISP subsystem.
 *
 * Copyright (c) 2010 Intel Corporation. All Rights Reserved.
 *
 * Copyright (c) 2010 Silicon Hive www.siliconhive.com.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 */
#ifndef __HMM_POOL_H__
#define __HMM_POOL_H__

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/kref.h>
#include "hmm_common.h"
#include "hmm/hmm_bo.h"

#define ALLOC_PAGE_FAIL_NUM		5

enum hmm_pool_type {
	HMM_POOL_TYPE_RESERVED,
	HMM_POOL_TYPE_DYNAMIC,
};

/**
 * struct hmm_pool_ops  -  memory pool callbacks.
 *
 * @pool_init:		   initialize the memory pool.
 * @pool_exit:		   uninitialize the memory pool.
 * @pool_alloc_pages:	   allocate pages from memory pool.
 * @pool_free_pages:	   free pages to memory pool.
 * @pool_inited:	   check whether memory pool is initialized.
 */
struct hmm_pool_ops {
	int (*pool_init)(void **pool, unsigned int pool_size);
	void (*pool_exit)(void **pool);
	unsigned int (*pool_alloc_pages)(void *pool,
					struct hmm_page_object *page_obj,
					unsigned int size, bool cached);
	void (*pool_free_pages)(void *pool,
				struct hmm_page_object *page_obj);
	int (*pool_inited)(void *pool);
};

struct hmm_pool {
	struct hmm_pool_ops	*pops;

	void			*pool_info;
};

/**
 * struct hmm_reserved_pool_info  - represents reserved pool private data.
 * @pages:			    a array that store physical pages.
 *				    The array is as reserved memory pool.
 * @index:			    to indicate the first blank page number
 *				    in reserved memory pool(pages array).
 * @pgnr:			    the valid page amount in reserved memory
 *				    pool.
 * @list_lock:			    list lock is used to protect the operation
 *				    to reserved memory pool.
 * @flag:			    reserved memory pool state flag.
 */
struct hmm_reserved_pool_info {
	struct page		**pages;

	unsigned int		index;
	unsigned int		pgnr;
	spinlock_t		list_lock;
	bool			initialized;
};

/**
 * struct hmm_dynamic_pool_info  -  represents dynamic pool private data.
 * @pages_list:			    a list that store physical pages.
 *				    The pages list is as dynamic memory pool.
 * @list_lock:			    list lock is used to protect the operation
 *				    to dynamic memory pool.
 * @flag:			    dynamic memory pool state flag.
 * @pgptr_cache:		    struct kmem_cache, manages a cache.
 */
struct hmm_dynamic_pool_info {
	struct list_head	pages_list;

	/* list lock is used to protect the free pages block lists */
	spinlock_t		list_lock;

	struct kmem_cache	*pgptr_cache;
	bool			initialized;

	unsigned int		pool_size;
	unsigned int		pgnr;
};

struct hmm_page {
	struct page		*page;
	struct list_head	list;
};

extern struct hmm_pool_ops	reserved_pops;
extern struct hmm_pool_ops	dynamic_pops;

#endif
