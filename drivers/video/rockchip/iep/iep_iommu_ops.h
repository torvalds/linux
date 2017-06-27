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

#ifndef __IEP_IOMMU_OPS_H__
#define __IEP_IOMMU_OPS_H__

#include <linux/platform_device.h>
#include <linux/rockchip-iovmm.h>
#include "iep_drv.h"

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

struct iep_iommu_info;
struct iep_iommu_session_info;

struct iep_iommu_ops {
	int (*create)(struct iep_iommu_info *iommu_info);
	int (*import)(struct iep_iommu_session_info *session_info, int fd);
	int (*free)(struct iep_iommu_session_info *session_info, int idx);
	int (*free_fd)(struct iep_iommu_session_info *session_info, int fd);
	int (*map_iommu)(struct iep_iommu_session_info *session_info,
			 int idx,
			 unsigned long *iova, unsigned long *size);
	int (*unmap_iommu)(struct iep_iommu_session_info *session_info,
			   int idx);
	int (*destroy)(struct iep_iommu_info *iommu_info);
	void (*dump)(struct iep_iommu_session_info *session_info);
	int (*attach)(struct iep_iommu_info *iommu_info);
	void (*detach)(struct iep_iommu_info *iommu_info);
	void (*clear)(struct iep_iommu_session_info *session_info);
};

struct iep_iommu_session_info {
	struct list_head head;
	struct iep_session *session;
	int buffer_nums;
	struct list_head buffer_list;
	struct mutex list_mutex;
	int max_idx;
	struct device *dev;
	struct device *mmu_dev;
	struct iep_iommu_info *iommu_info;
	int debug_level;
};

struct iep_iommu_info {
	struct list_head session_list;
	struct mutex list_mutex;
	struct mutex iommu_mutex;
	struct device *dev;
	struct device *mmu_dev;
	struct iep_iommu_ops *ops;
	int debug_level;
	void *private;
};

#ifdef CONFIG_DRM
void iep_iommu_drm_set_ops(struct iep_iommu_info *iommu_info);
#endif
#ifdef CONFIG_ION
void iep_iommu_ion_set_ops(struct iep_iommu_info *iommu_info);
#endif

struct iep_iommu_info *iep_iommu_info_create(struct device *dev,
					     struct device *mmu_dev,
					     int alloc_type);
int iep_iommu_info_destroy(struct iep_iommu_info *iommu_info);

int iep_iommu_create(struct iep_iommu_info *iommu_info);
int iep_iommu_import(struct iep_iommu_info *iommu_info,
		     struct iep_session *session, int fd);
int iep_iommu_free(struct iep_iommu_info *iommu_info,
		   struct iep_session *session, int idx);
int iep_iommu_free_fd(struct iep_iommu_info *iommu_info,
		      struct iep_session *session, int fd);
int iep_iommu_map_iommu(struct iep_iommu_info *iommu_info,
			struct iep_session *session,
			int idx,
			unsigned long *iova,
			unsigned long *size);
int iep_iommu_unmap_iommu(struct iep_iommu_info *iommu_info,
			  struct iep_session *session,
			  int idx);
int iep_iommu_destroy(struct iep_iommu_info *iommu_info);
void iep_iommu_dump(struct iep_iommu_info *iommu_info,
		    struct iep_session *session);
void iep_iommu_clear(struct iep_iommu_info *iommu_info,
		     struct iep_session *session);

int iep_iommu_attach(struct iep_iommu_info *iommu_info);
void iep_iommu_detach(struct iep_iommu_info *iommu_info);

#endif
