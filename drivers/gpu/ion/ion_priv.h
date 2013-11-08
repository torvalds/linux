/*
 * drivers/gpu/ion/ion_priv.h
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

#ifndef _ION_PRIV_H
#define _ION_PRIV_H

#include <linux/kref.h>
#include <linux/mm_types.h>
#include <linux/mutex.h>
#include <linux/rbtree.h>
#include <linux/ion.h>

#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/debugfs.h>
#include <linux/list.h>

struct ion_mapping;

struct ion_dma_mapping {
	struct kref ref;
	struct scatterlist *sglist;
};

struct ion_kernel_mapping {
	struct kref ref;
	void *vaddr;
};

struct ion_user_map_addr {
	unsigned long vaddr;
	unsigned long size;
	struct list_head list;
};
struct ion_buffer *ion_handle_buffer(struct ion_handle *handle);

/**
 * struct ion_buffer - metadata for a particular buffer
 * @ref:		refernce count
 * @node:		node in the ion_device buffers tree
 * @dev:		back pointer to the ion_device
 * @heap:		back pointer to the heap the buffer came from
 * @flags:		buffer specific flags
 * @size:		size of the buffer
 * @priv_virt:		private data to the buffer representable as
 *			a void *
 * @priv_phys:		private data to the buffer representable as
 *			an ion_phys_addr_t (and someday a phys_addr_t)
 * @lock:		protects the buffers cnt fields
 * @kmap_cnt:		number of times the buffer is mapped to the kernel
 * @vaddr:		the kenrel mapping if kmap_cnt is not zero
 * @dmap_cnt:		number of times the buffer is mapped for dma
 * @sglist:		the scatterlist for the buffer is dmap_cnt is not zero
*/
struct ion_buffer {
	struct kref ref;
	struct rb_node node;
	struct ion_device *dev;
	struct ion_heap *heap;
	unsigned long flags;
	size_t size;
	union {
		void *priv_virt;
		ion_phys_addr_t priv_phys;
	};
	struct mutex lock;
	int kmap_cnt;
	void *vaddr;
	int dmap_cnt;
	struct scatterlist *sglist;
        struct list_head map_addr;
	pid_t pid;
	int marked;
};

/**
 * struct ion_heap_ops - ops to operate on a given heap
 * @allocate:		allocate memory
 * @free:		free memory
 * @phys		get physical address of a buffer (only define on
 *			physically contiguous heaps)
 * @map_dma		map the memory for dma to a scatterlist
 * @unmap_dma		unmap the memory for dma
 * @map_kernel		map memory to the kernel
 * @unmap_kernel	unmap memory to the kernel
 * @map_user		map memory to userspace
 */
struct ion_heap_ops {
	int (*allocate) (struct ion_heap *heap,
			 struct ion_buffer *buffer, unsigned long len,
			 unsigned long align, unsigned long flags);
	void (*free) (struct ion_buffer *buffer);
	int (*phys) (struct ion_heap *heap, struct ion_buffer *buffer,
		     ion_phys_addr_t *addr, size_t *len);
	struct scatterlist *(*map_dma) (struct ion_heap *heap,
					struct ion_buffer *buffer);
	void (*unmap_dma) (struct ion_heap *heap, struct ion_buffer *buffer);
	void * (*map_kernel) (struct ion_heap *heap, struct ion_buffer *buffer);
	void (*unmap_kernel) (struct ion_heap *heap, struct ion_buffer *buffer);
	int (*map_user) (struct ion_heap *mapper, struct ion_buffer *buffer,
			 struct vm_area_struct *vma);
	int (*cache_op)(struct ion_heap *heap, struct ion_buffer *buffer,
			void *virt, unsigned int type);
	int (*print_debug)(struct ion_heap *heap, struct seq_file *s);
};

/**
 * struct ion_heap - represents a heap in the system
 * @node:		rb node to put the heap on the device's tree of heaps
 * @dev:		back pointer to the ion_device
 * @type:		type of heap
 * @ops:		ops struct as above
 * @id:			id of heap, also indicates priority of this heap when
 *			allocating.  These are specified by platform data and
 *			MUST be unique
 * @name:		used for debugging
 *
 * Represents a pool of memory from which buffers can be made.  In some
 * systems the only heap is regular system memory allocated via vmalloc.
 * On others, some blocks might require large physically contiguous buffers
 * that are allocated from a specially reserved heap.
 */
struct ion_heap {
	struct rb_node node;
	struct ion_device *dev;
	enum ion_heap_type type;
	struct ion_heap_ops *ops;
	int id;
	const char *name;

	unsigned long allocated_size;
	unsigned long max_allocated;
	unsigned long total_size;
};

/**
 * ion_device_create - allocates and returns an ion device
 * @custom_ioctl:	arch specific ioctl function if applicable
 *
 * returns a valid device or -PTR_ERR
 */
struct ion_device *ion_device_create(long (*custom_ioctl)
				     (struct ion_client *client,
				      unsigned int cmd,
				      unsigned long arg));

/**
 * ion_device_destroy - free and device and it's resource
 * @dev:		the device
 */
void ion_device_destroy(struct ion_device *dev);

/**
 * ion_device_add_heap - adds a heap to the ion device
 * @dev:		the device
 * @heap:		the heap to add
 */
void ion_device_add_heap(struct ion_device *dev, struct ion_heap *heap);

/**
 * functions for creating and destroying the built in ion heaps.
 * architectures can add their own custom architecture specific
 * heaps as appropriate.
 */

struct ion_heap *ion_heap_create(struct ion_platform_heap *);
void ion_heap_destroy(struct ion_heap *);

struct ion_heap *ion_system_heap_create(struct ion_platform_heap *);
void ion_system_heap_destroy(struct ion_heap *);

struct ion_heap *ion_system_contig_heap_create(struct ion_platform_heap *);
void ion_system_contig_heap_destroy(struct ion_heap *);

struct ion_heap *ion_carveout_heap_create(struct ion_platform_heap *);
void ion_carveout_heap_destroy(struct ion_heap *);
/**
 * kernel api to allocate/free from carveout -- used when carveout is
 * used to back an architecture specific custom heap
 */
ion_phys_addr_t ion_carveout_allocate(struct ion_heap *heap, unsigned long size,
				      unsigned long align);
void ion_carveout_free(struct ion_heap *heap, ion_phys_addr_t addr,
		       unsigned long size);
/**
 * The carveout heap returns physical addresses, since 0 may be a valid
 * physical address, this is used to indicate allocation failed
 */
#define ION_CARVEOUT_ALLOCATE_FAIL -1

#endif /* _ION_PRIV_H */
