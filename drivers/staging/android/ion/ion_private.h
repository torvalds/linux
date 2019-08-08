/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ION Memory Allocator - Internal header
 *
 * Copyright (C) 2019 Google, Inc.
 */

#ifndef _ION_PRIVATE_H
#define _ION_PRIVATE_H

#include <linux/dcache.h>
#include <linux/dma-buf.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/plist.h>
#include <linux/rbtree.h>
#include <linux/rwsem.h>
#include <linux/types.h>

#include "ion.h"

/**
 * struct ion_device - the metadata of the ion device node
 * @dev:		the actual misc device
 * @buffers:		an rb tree of all the existing buffers
 * @buffer_lock:	lock protecting the tree of buffers
 * @lock:		rwsem protecting the tree of heaps and clients
 */
struct ion_device {
	struct miscdevice dev;
	struct rb_root buffers;
	struct mutex buffer_lock;
	struct rw_semaphore lock;
	struct plist_head heaps;
	struct dentry *debug_root;
	int heap_cnt;
};

/* ion_buffer manipulators */
extern struct ion_buffer *ion_buffer_alloc(struct ion_device *dev, size_t len,
					   unsigned int heap_id_mask,
					   unsigned int flags);
extern void ion_buffer_release(struct ion_buffer *buffer);
extern int ion_buffer_destroy(struct ion_device *dev,
			      struct ion_buffer *buffer);
extern void *ion_buffer_kmap_get(struct ion_buffer *buffer);
extern void ion_buffer_kmap_put(struct ion_buffer *buffer);

/* ion dmabuf allocator */
extern struct dma_buf *ion_dmabuf_alloc(struct ion_device *dev, size_t len,
					unsigned int heap_id_mask,
					unsigned int flags);
extern int ion_free(struct ion_buffer *buffer);

#endif /* _ION_PRIVATE_H */
