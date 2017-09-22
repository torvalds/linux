/**
 * Copyright (C) 2016 Fuzhou Rockchip Electronics Co., Ltd
 * author: Jung Zhao jung.zhao@rock-chips.com
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

#ifndef __VCODEC_IOMMU_OPS_H__
#define __VCODEC_IOMMU_OPS_H__

#include <linux/platform_device.h>
#include "vcodec_service.h"

#define BUFFER_LIST_MAX_NUMS	30

#define ALLOCATOR_USE_ION		0x00000000
#define ALLOCATOR_USE_DRM		0x00000001

#define DEBUG_IOMMU_OPS_DUMP	0x00020000
#define DEBUG_IOMMU_NORMAL	0x00040000

#define vpu_iommu_debug_func(debug_level, type, fmt, args...)	\
	do {							\
		if (unlikely(debug_level & type)) {		\
			pr_info("%s:%d: " fmt,			\
				 __func__, __LINE__, ##args);	\
		}						\
	} while (0)
#define vpu_iommu_debug(debug_level, type, fmt, args...)	\
	do {							\
		if (unlikely(debug_level & type)) {		\
			pr_info(fmt, ##args);			\
		}						\
	} while (0)

struct vcodec_iommu_info;
struct vcodec_iommu_session_info;

struct vcodec_iommu_ops {
	int (*create)(struct vcodec_iommu_info *iommu_info);
	int (*import)(struct vcodec_iommu_session_info *session_info, int fd);
	int (*free)(struct vcodec_iommu_session_info *session_info, int idx);
	int (*free_fd)(struct vcodec_iommu_session_info *session_info, int fd);
	int (*map_iommu)(struct vcodec_iommu_session_info *session_info,
			 int idx,
			 dma_addr_t *iova, unsigned long *size);
	int (*unmap_iommu)(struct vcodec_iommu_session_info *session_info,
			   int idx);
	int (*destroy)(struct vcodec_iommu_info *iommu_info);
	void (*dump)(struct vcodec_iommu_session_info *session_info);
	int (*attach)(struct vcodec_iommu_info *iommu_info);
	void (*detach)(struct vcodec_iommu_info *iommu_info);
	void (*clear)(struct vcodec_iommu_session_info *session_info);
};

struct vcodec_iommu_session_info {
	struct list_head head;
	struct vpu_session *session;
	int buffer_nums;
	struct list_head buffer_list;
	struct mutex list_mutex;
	int max_idx;
	struct device *dev;
	struct device *mmu_dev;
	struct vcodec_iommu_info *iommu_info;
	int debug_level;
};

struct vcodec_iommu_info {
	struct list_head session_list;
	struct mutex list_mutex;
	struct mutex iommu_mutex;
	struct device *dev;
	struct device *mmu_dev;
	struct vcodec_iommu_ops *ops;
	int debug_level;
	void *private;
};

#ifdef CONFIG_DRM
void vcodec_iommu_drm_set_ops(struct vcodec_iommu_info *iommu_info);
#endif
#ifdef CONFIG_ION
void vcodec_iommu_ion_set_ops(struct vcodec_iommu_info *iommu_info);
#endif

struct vcodec_iommu_info *vcodec_iommu_info_create(struct device *dev,
						   struct device *mmu_dev,
						   int alloc_type);
int vcodec_iommu_info_destroy(struct vcodec_iommu_info *iommu_info);

int vcodec_iommu_create(struct vcodec_iommu_info *iommu_info);
int vcodec_iommu_import(struct vcodec_iommu_info *iommu_info,
			struct vpu_session *session, int fd);
int vcodec_iommu_free(struct vcodec_iommu_info *iommu_info,
		      struct vpu_session *session, int idx);
int vcodec_iommu_free_fd(struct vcodec_iommu_info *iommu_info,
			 struct vpu_session *session, int fd);
int vcodec_iommu_map_iommu(struct vcodec_iommu_info *iommu_info,
			   struct vpu_session *session,
			   int idx,
			   dma_addr_t *iova,
			   unsigned long *size);
int vcodec_iommu_unmap_iommu(struct vcodec_iommu_info *iommu_info,
			     struct vpu_session *session,
			     int idx);
void vcodec_iommu_dump(struct vcodec_iommu_info *iommu_info,
		       struct vpu_session *session);
void vcodec_iommu_clear(struct vcodec_iommu_info *iommu_info,
			struct vpu_session *session);

int vcodec_iommu_attach(struct vcodec_iommu_info *iommu_info);
void vcodec_iommu_detach(struct vcodec_iommu_info *iommu_info);

#endif
