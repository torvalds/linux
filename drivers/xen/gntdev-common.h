/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Common functionality of grant device.
 *
 * Copyright (c) 2006-2007, D G Murray.
 *           (c) 2009 Gerd Hoffmann <kraxel@redhat.com>
 *           (c) 2018 Oleksandr Andrushchenko, EPAM Systems Inc.
 */

#ifndef _GNTDEV_COMMON_H
#define _GNTDEV_COMMON_H

#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/mmu_notifier.h>
#include <linux/types.h>
#include <xen/interface/event_channel.h>

struct gntdev_dmabuf_priv;

struct gntdev_priv {
	/* Maps with visible offsets in the file descriptor. */
	struct list_head maps;
	/* lock protects maps and freeable_maps. */
	struct mutex lock;

#ifdef CONFIG_XEN_GRANT_DMA_ALLOC
	/* Device for which DMA memory is allocated. */
	struct device *dma_dev;
#endif

#ifdef CONFIG_XEN_GNTDEV_DMABUF
	struct gntdev_dmabuf_priv *dmabuf_priv;
#endif
};

struct gntdev_unmap_notify {
	int flags;
	/* Address relative to the start of the gntdev_grant_map. */
	int addr;
	evtchn_port_t event;
};

struct gntdev_grant_map {
	struct mmu_interval_notifier notifier;
	struct list_head next;
	struct vm_area_struct *vma;
	int index;
	int count;
	int flags;
	refcount_t users;
	struct gntdev_unmap_notify notify;
	struct ioctl_gntdev_grant_ref *grants;
	struct gnttab_map_grant_ref   *map_ops;
	struct gnttab_unmap_grant_ref *unmap_ops;
	struct gnttab_map_grant_ref   *kmap_ops;
	struct gnttab_unmap_grant_ref *kunmap_ops;
	struct page **pages;
	unsigned long pages_vm_start;

#ifdef CONFIG_XEN_GRANT_DMA_ALLOC
	/*
	 * If dmabuf_vaddr is not NULL then this mapping is backed by DMA
	 * capable memory.
	 */

	struct device *dma_dev;
	/* Flags used to create this DMA buffer: GNTDEV_DMA_FLAG_XXX. */
	int dma_flags;
	void *dma_vaddr;
	dma_addr_t dma_bus_addr;
	/* Needed to avoid allocation in gnttab_dma_free_pages(). */
	xen_pfn_t *frames;
#endif
};

struct gntdev_grant_map *gntdev_alloc_map(struct gntdev_priv *priv, int count,
					  int dma_flags);

void gntdev_add_map(struct gntdev_priv *priv, struct gntdev_grant_map *add);

void gntdev_put_map(struct gntdev_priv *priv, struct gntdev_grant_map *map);

bool gntdev_test_page_count(unsigned int count);

int gntdev_map_grant_pages(struct gntdev_grant_map *map);

#endif
