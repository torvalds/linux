/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * MMU-based software IOTLB.
 *
 * Copyright (C) 2020-2021 Bytedance Inc. and/or its affiliates. All rights reserved.
 *
 * Author: Xie Yongji <xieyongji@bytedance.com>
 *
 */

#ifndef _VDUSE_IOVA_DOMAIN_H
#define _VDUSE_IOVA_DOMAIN_H

#include <linux/iova.h>
#include <linux/dma-mapping.h>
#include <linux/vhost_iotlb.h>

#define IOVA_START_PFN 1

#define INVALID_PHYS_ADDR (~(phys_addr_t)0)

struct vduse_bounce_map {
	struct page *bounce_page;
	u64 orig_phys;
};

struct vduse_iova_domain {
	struct iova_domain stream_iovad;
	struct iova_domain consistent_iovad;
	struct vduse_bounce_map *bounce_maps;
	size_t bounce_size;
	unsigned long iova_limit;
	int bounce_map;
	struct vhost_iotlb *iotlb;
	spinlock_t iotlb_lock;
	struct file *file;
	bool user_bounce_pages;
	rwlock_t bounce_lock;
};

int vduse_domain_set_map(struct vduse_iova_domain *domain,
			 struct vhost_iotlb *iotlb);

void vduse_domain_clear_map(struct vduse_iova_domain *domain,
			    struct vhost_iotlb *iotlb);

void vduse_domain_sync_single_for_device(struct vduse_iova_domain *domain,
				      dma_addr_t dma_addr, size_t size,
				      enum dma_data_direction dir);

void vduse_domain_sync_single_for_cpu(struct vduse_iova_domain *domain,
				      dma_addr_t dma_addr, size_t size,
				      enum dma_data_direction dir);

dma_addr_t vduse_domain_map_page(struct vduse_iova_domain *domain,
				 struct page *page, unsigned long offset,
				 size_t size, enum dma_data_direction dir,
				 unsigned long attrs);

void vduse_domain_unmap_page(struct vduse_iova_domain *domain,
			     dma_addr_t dma_addr, size_t size,
			     enum dma_data_direction dir, unsigned long attrs);

void *vduse_domain_alloc_coherent(struct vduse_iova_domain *domain,
				  size_t size, dma_addr_t *dma_addr,
				  gfp_t flag, unsigned long attrs);

void vduse_domain_free_coherent(struct vduse_iova_domain *domain, size_t size,
				void *vaddr, dma_addr_t dma_addr,
				unsigned long attrs);

void vduse_domain_reset_bounce_map(struct vduse_iova_domain *domain);

int vduse_domain_add_user_bounce_pages(struct vduse_iova_domain *domain,
				       struct page **pages, int count);

void vduse_domain_remove_user_bounce_pages(struct vduse_iova_domain *domain);

void vduse_domain_destroy(struct vduse_iova_domain *domain);

struct vduse_iova_domain *vduse_domain_create(unsigned long iova_limit,
					      size_t bounce_size);

int vduse_domain_init(void);

void vduse_domain_exit(void);

#endif /* _VDUSE_IOVA_DOMAIN_H */
