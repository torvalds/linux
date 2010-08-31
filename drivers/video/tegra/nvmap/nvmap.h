/*
 * drivers/video/tegra/nvmap/nvmap.h
 *
 * GPU memory management driver for Tegra
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *'
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __VIDEO_TEGRA_NVMAP_NVMAP_H
#define __VIDEO_TEGRA_NVMAP_NVMAP_H

#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/rbtree.h>
#include <linux/sched.h>
#include <linux/wait.h>

#include <asm/atomic.h>

#include <mach/nvmap.h>

#include "nvmap_heap.h"

#define nvmap_err(_client, _fmt, ...)				\
	dev_err(nvmap_client_to_device(_client),		\
		"%s: "_fmt, __func__, ##__VA_ARGS__)

#define nvmap_warn(_client, _fmt, ...)				\
	dev_warn(nvmap_client_to_device(_client),		\
		 "%s: "_fmt, __func__, ##__VA_ARGS__)

#define nvmap_debug(_client, _fmt, ...)				\
	dev_dbg(nvmap_client_to_device(_client),		\
		"%s: "_fmt, __func__, ##__VA_ARGS__)

#define nvmap_ref_to_id(_ref)		((unsigned long)(_ref)->handle)

struct nvmap_device;
struct page;
struct tegra_iovmm_area;

/* handles allocated using shared system memory (either IOVMM- or high-order
 * page allocations */
struct nvmap_pgalloc {
	struct page **pages;
	struct tegra_iovmm_area *area;
	struct list_head mru_list;	/* MRU entry for IOVMM reclamation */
	bool contig;			/* contiguous system memory */
	bool dirty;			/* area is invalid and needs mapping */
};

struct nvmap_handle {
	struct rb_node node;	/* entry on global handle tree */
	atomic_t ref;		/* reference count (i.e., # of duplications) */
	atomic_t pin;		/* pin count */
	unsigned long flags;
	size_t size;		/* padded (as-allocated) size */
	size_t orig_size;	/* original (as-requested) size */
	struct nvmap_client *owner;
	union {
		struct nvmap_pgalloc pgalloc;
		struct nvmap_heap_block *carveout;
	};
	bool global;		/* handle may be duplicated by other clients */
	bool secure;		/* zap IOVMM area on unpin */
	bool heap_pgalloc;	/* handle is page allocated (sysmem / iovmm) */
	bool alloc;		/* handle has memory allocated */
};

struct nvmap_share {
	struct tegra_iovmm_client *iovmm;
	wait_queue_head_t pin_wait;
	struct mutex pin_lock;
#ifdef CONFIG_NVMAP_RECLAIM_UNPINNED_VM
	spinlock_t mru_lock;
	struct list_head *mru_lists;
	int nr_mru;
#endif
};

struct nvmap_client {
	struct nvmap_device *dev;
	struct nvmap_share *share;
	struct rb_root	handle_refs;
	atomic_t	iovm_commit;
	size_t		iovm_limit;
	spinlock_t	ref_lock;
	bool		super;
	atomic_t	count;
};

/* handle_ref objects are client-local references to an nvmap_handle;
 * they are distinct objects so that handles can be unpinned and
 * unreferenced the correct number of times when a client abnormally
 * terminates */
struct nvmap_handle_ref {
	struct nvmap_handle *handle;
	struct rb_node	node;
	atomic_t	dupes;	/* number of times to free on file close */
	atomic_t	pin;	/* number of times to unpin on free */
};

struct nvmap_vma_priv {
	struct nvmap_handle *handle;
	size_t		offs;
	atomic_t	count;	/* number of processes cloning the VMA */
};

static inline void nvmap_ref_lock(struct nvmap_client *priv)
{
	spin_lock(&priv->ref_lock);
}

static inline void nvmap_ref_unlock(struct nvmap_client *priv)
{
	spin_unlock(&priv->ref_lock);
}

struct device *nvmap_client_to_device(struct nvmap_client *client);

pte_t **nvmap_alloc_pte(struct nvmap_device *dev, void **vaddr);

pte_t **nvmap_alloc_pte_irq(struct nvmap_device *dev, void **vaddr);

void nvmap_free_pte(struct nvmap_device *dev, pte_t **pte);

struct nvmap_heap_block *nvmap_carveout_alloc(struct nvmap_client *dev,
					      size_t len, size_t align,
					      unsigned long usage,
					      unsigned int prot);

unsigned long nvmap_carveout_usage(struct nvmap_client *c,
				   struct nvmap_heap_block *b);

struct nvmap_handle *nvmap_validate_get(struct nvmap_client *client,
					unsigned long handle);

struct nvmap_handle_ref *_nvmap_validate_id_locked(struct nvmap_client *priv,
						   unsigned long id);

struct nvmap_handle *nvmap_get_handle_id(struct nvmap_client *client,
					 unsigned long id);

struct nvmap_handle_ref *nvmap_create_handle(struct nvmap_client *client,
					     size_t size);

struct nvmap_handle_ref *nvmap_duplicate_handle_id(struct nvmap_client *client,
						   unsigned long id);


int nvmap_alloc_handle_id(struct nvmap_client *client,
			  unsigned long id, unsigned int heap_mask,
			  size_t align, unsigned int flags);

void nvmap_free_handle_id(struct nvmap_client *c, unsigned long id);

int nvmap_pin_ids(struct nvmap_client *client,
		  unsigned int nr, const unsigned long *ids);

void nvmap_unpin_ids(struct nvmap_client *priv,
		     unsigned int nr, const unsigned long *ids);

void _nvmap_handle_free(struct nvmap_handle *h);

int nvmap_handle_remove(struct nvmap_device *dev, struct nvmap_handle *h);

void nvmap_handle_add(struct nvmap_device *dev, struct nvmap_handle *h);

static inline struct nvmap_handle *nvmap_handle_get(struct nvmap_handle *h)
{
	if (unlikely(atomic_inc_return(&h->ref) <= 1)) {
		pr_err("%s: %s getting a freed handle\n",
			__func__, current->group_leader->comm);
		if (atomic_read(&h->ref) <= 0)
			return NULL;
	}
	return h;
}

static inline void nvmap_handle_put(struct nvmap_handle *h)
{
	int cnt = atomic_dec_return(&h->ref);

	if (WARN_ON(cnt < 0)) {
		pr_err("%s: %s put to negative references\n",
			__func__, current->comm);
	} else if (cnt == 0)
		_nvmap_handle_free(h);
}

static inline pgprot_t nvmap_pgprot(struct nvmap_handle *h, pgprot_t prot)
{
	if (h->flags == NVMAP_HANDLE_UNCACHEABLE)
		return pgprot_dmacoherent(prot);
	else if (h->flags == NVMAP_HANDLE_WRITE_COMBINE)
		return pgprot_writecombine(prot);
	else if (h->flags == NVMAP_HANDLE_INNER_CACHEABLE)
		return pgprot_inner_writeback(prot);
	return prot;
}

int is_nvmap_vma(struct vm_area_struct *vma);

#endif
