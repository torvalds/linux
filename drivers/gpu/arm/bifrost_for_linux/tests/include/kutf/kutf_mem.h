/*
 *
 * (C) COPYRIGHT 2014, 2017 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */



#ifndef _KERNEL_UTF_MEM_H_
#define _KERNEL_UTF_MEM_H_

/* kutf_mem.h
 * Functions for management of memory pools in the kernel.
 *
 * This module implements a memory pool allocator, allowing a test
 * implementation to allocate linked allocations which can then be freed by a
 * single free which releases all of the resources held by the entire pool.
 *
 * Note that it is not possible to free single resources within the pool once
 * allocated.
 */

#include <linux/list.h>
#include <linux/mutex.h>

/**
 * struct kutf_mempool - the memory pool context management structure
 * @head:	list head on which the allocations in this context are added to
 * @lock:	mutex for concurrent allocation from multiple threads
 *
 */
struct kutf_mempool {
	struct list_head head;
	struct mutex lock;
};

/**
 * kutf_mempool_init() - Initialize a memory pool.
 * @pool:	Memory pool structure to initialize, provided by the user
 *
 * Return:	zero on success
 */
int kutf_mempool_init(struct kutf_mempool *pool);

/**
 * kutf_mempool_alloc() - Allocate memory from a pool
 * @pool:	Memory pool to allocate from
 * @size:	Size of memory wanted in number of bytes
 *
 * Return:	Pointer to memory on success, NULL on failure.
 */
void *kutf_mempool_alloc(struct kutf_mempool *pool, size_t size);

/**
 * kutf_mempool_destroy() - Destroy a memory pool, freeing all memory within it.
 * @pool:	The memory pool to free
 */
void kutf_mempool_destroy(struct kutf_mempool *pool);
#endif	/* _KERNEL_UTF_MEM_H_ */
