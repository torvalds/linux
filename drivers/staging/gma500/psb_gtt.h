/**************************************************************************
 * Copyright (c) 2007-2008, Intel Corporation.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 **************************************************************************/

#ifndef _PSB_GTT_H_
#define _PSB_GTT_H_

#include <drm/drmP.h>

/*#include "img_types.h"*/

struct psb_gtt {
	struct drm_device *dev;
	int initialized;
	uint32_t gatt_start;
	uint32_t mmu_gatt_start;
	uint32_t ci_start;
	uint32_t rar_start;
	uint32_t gtt_start;
	uint32_t gtt_phys_start;
	unsigned gtt_pages;
	unsigned gatt_pages;
	uint32_t stolen_base;
	void *vram_addr;
	uint32_t pge_ctl;
	u16 gmch_ctrl;
	unsigned long stolen_size;
	unsigned long vram_stolen_size;
	unsigned long ci_stolen_size;
	unsigned long rar_stolen_size;
	uint32_t *gtt_map;
	struct rw_semaphore sem;
};

struct psb_gtt_mm {
	struct drm_mm base;
	struct drm_open_hash hash;
	uint32_t count;
	spinlock_t lock;
};

struct psb_gtt_hash_entry {
	struct drm_open_hash ht;
	uint32_t count;
	struct drm_hash_item item;
};

struct psb_gtt_mem_mapping {
	struct drm_mm_node *node;
	struct drm_hash_item item;
};

/*Exported functions*/
extern int psb_gtt_init(struct psb_gtt *pg, int resume);
extern int psb_gtt_insert_pages(struct psb_gtt *pg, struct page **pages,
				unsigned offset_pages, unsigned num_pages,
				unsigned desired_tile_stride,
				unsigned hw_tile_stride, int type);
extern int psb_gtt_remove_pages(struct psb_gtt *pg, unsigned offset_pages,
				unsigned num_pages,
				unsigned desired_tile_stride,
				unsigned hw_tile_stride,
				int rc_prot);

extern struct psb_gtt *psb_gtt_alloc(struct drm_device *dev);
extern void psb_gtt_takedown(struct psb_gtt *pg, int free);
extern int psb_gtt_map_meminfo(struct drm_device *dev,
				void * hKernelMemInfo,
				uint32_t *offset);
extern int psb_gtt_unmap_meminfo(struct drm_device *dev,
				 void * hKernelMemInfo);
extern int psb_gtt_map_meminfo_ioctl(struct drm_device *dev, void *data,
				     struct drm_file *file_priv);
extern int psb_gtt_unmap_meminfo_ioctl(struct drm_device *dev, void *data,
				       struct drm_file *file_priv);
extern int psb_gtt_mm_init(struct psb_gtt *pg);
extern void psb_gtt_mm_takedown(void);

extern int psb_gtt_map_pvr_memory(struct drm_device *dev,
                                                                  unsigned int hHandle,
                                                                  unsigned int ui32TaskId,
                                                                  dma_addr_t *pPages,
                                                                  unsigned int ui32PagesNum,
                                                                  unsigned int *ui32Offset);

extern int psb_gtt_unmap_pvr_memory(struct drm_device *dev,
                                                                        unsigned int hHandle,
                                                                        unsigned int ui32TaskId);

#endif
