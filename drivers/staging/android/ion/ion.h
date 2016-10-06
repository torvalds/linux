/*
 * drivers/staging/android/ion/ion.h
 *
 * Copyright (C) 2011 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _LINUX_ION_H
#define _LINUX_ION_H

#include <linux/types.h>

#include "../uapi/ion.h"

struct ion_handle;
struct ion_device;
struct ion_heap;
struct ion_mapper;
struct ion_client;
struct ion_buffer;

/*
 * This should be removed some day when phys_addr_t's are fully
 * plumbed in the kernel, and all instances of ion_phys_addr_t should
 * be converted to phys_addr_t.  For the time being many kernel interfaces
 * do not accept phys_addr_t's that would have to
 */
#define ion_phys_addr_t unsigned long

/**
 * struct ion_platform_heap - defines a heap in the given platform
 * @type:	type of the heap from ion_heap_type enum
 * @id:		unique identifier for heap.  When allocating higher numbers
 *		will be allocated from first.  At allocation these are passed
 *		as a bit mask and therefore can not exceed ION_NUM_HEAP_IDS.
 * @name:	used for debug purposes
 * @base:	base address of heap in physical memory if applicable
 * @size:	size of the heap in bytes if applicable
 * @align:	required alignment in physical memory if applicable
 * @priv:	private info passed from the board file
 *
 * Provided by the board file.
 */
struct ion_platform_heap {
	enum ion_heap_type type;
	unsigned int id;
	const char *name;
	ion_phys_addr_t base;
	size_t size;
	ion_phys_addr_t align;
	void *priv;
};

/**
 * struct ion_platform_data - array of platform heaps passed from board file
 * @nr:		number of structures in the array
 * @heaps:	array of platform_heap structions
 *
 * Provided by the board file in the form of platform data to a platform device.
 */
struct ion_platform_data {
	int nr;
	struct ion_platform_heap *heaps;
};

/**
 * ion_client_create() -  allocate a client and returns it
 * @dev:		the global ion device
 * @name:		used for debugging
 */
struct ion_client *ion_client_create(struct ion_device *dev,
				     const char *name);

/**
 * ion_client_destroy() -  free's a client and all it's handles
 * @client:	the client
 *
 * Free the provided client and all it's resources including
 * any handles it is holding.
 */
void ion_client_destroy(struct ion_client *client);

/**
 * ion_alloc - allocate ion memory
 * @client:		the client
 * @len:		size of the allocation
 * @align:		requested allocation alignment, lots of hardware blocks
 *			have alignment requirements of some kind
 * @heap_id_mask:	mask of heaps to allocate from, if multiple bits are set
 *			heaps will be tried in order from highest to lowest
 *			id
 * @flags:		heap flags, the low 16 bits are consumed by ion, the
 *			high 16 bits are passed on to the respective heap and
 *			can be heap custom
 *
 * Allocate memory in one of the heaps provided in heap mask and return
 * an opaque handle to it.
 */
struct ion_handle *ion_alloc(struct ion_client *client, size_t len,
			     size_t align, unsigned int heap_id_mask,
			     unsigned int flags);

/**
 * ion_free - free a handle
 * @client:	the client
 * @handle:	the handle to free
 *
 * Free the provided handle.
 */
void ion_free(struct ion_client *client, struct ion_handle *handle);

/**
 * ion_map_kernel - create mapping for the given handle
 * @client:	the client
 * @handle:	handle to map
 *
 * Map the given handle into the kernel and return a kernel address that
 * can be used to access this address.
 */
void *ion_map_kernel(struct ion_client *client, struct ion_handle *handle);

/**
 * ion_unmap_kernel() - destroy a kernel mapping for a handle
 * @client:	the client
 * @handle:	handle to unmap
 */
void ion_unmap_kernel(struct ion_client *client, struct ion_handle *handle);

/**
 * ion_share_dma_buf() - share buffer as dma-buf
 * @client:	the client
 * @handle:	the handle
 */
struct dma_buf *ion_share_dma_buf(struct ion_client *client,
						struct ion_handle *handle);

/**
 * ion_share_dma_buf_fd() - given an ion client, create a dma-buf fd
 * @client:	the client
 * @handle:	the handle
 */
int ion_share_dma_buf_fd(struct ion_client *client, struct ion_handle *handle);

/**
 * ion_import_dma_buf() - get ion_handle from dma-buf
 * @client:	the client
 * @dmabuf:	the dma-buf
 *
 * Get the ion_buffer associated with the dma-buf and return the ion_handle.
 * If no ion_handle exists for this buffer, return newly created ion_handle.
 * If dma-buf from another exporter is passed, return ERR_PTR(-EINVAL)
 */
struct ion_handle *ion_import_dma_buf(struct ion_client *client,
				      struct dma_buf *dmabuf);

/**
 * ion_import_dma_buf_fd() - given a dma-buf fd from the ion exporter get handle
 * @client:	the client
 * @fd:		the dma-buf fd
 *
 * Given an dma-buf fd that was allocated through ion via ion_share_dma_buf_fd,
 * import that fd and return a handle representing it. If a dma-buf from
 * another exporter is passed in this function will return ERR_PTR(-EINVAL)
 */
struct ion_handle *ion_import_dma_buf_fd(struct ion_client *client, int fd);

#endif /* _LINUX_ION_H */
