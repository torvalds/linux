/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *	Alpha Lin, alpha.lin@rock-chips.com
 *	Randy Li, randy.li@rock-chips.com
 *	Ding Wei, leo.ding@rock-chips.com
 *
 */
#ifndef __ROCKCHIP_MPP_IOMMU_H__
#define __ROCKCHIP_MPP_IOMMU_H__

#include <linux/iommu.h>
#include <linux/dma-mapping.h>

struct mpp_dma_buffer {
	/* link to dma session buffer list */
	struct list_head link;

	/* dma session belong */
	struct mpp_dma_session *dma;
	/* DMABUF information */
	struct dma_buf *dmabuf;
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	struct sg_table *copy_sgt;
	enum dma_data_direction dir;

	dma_addr_t iova;
	unsigned long size;
	void *vaddr;

	struct kref ref;
	ktime_t last_used;
	/* alloc by device */
	struct device *dev;
};

#define MPP_SESSION_MAX_BUFFERS		60

struct mpp_dma_session {
	/* the buffer used in session */
	struct list_head unused_list;
	struct list_head used_list;
	struct mpp_dma_buffer dma_bufs[MPP_SESSION_MAX_BUFFERS];
	/* the mutex for the above buffer list */
	struct mutex list_mutex;
	/* the max buffer num for the buffer list */
	u32 max_buffers;
	/* the count for the buffer list */
	int buffer_count;

	struct device *dev;
};

struct mpp_rk_iommu {
	struct list_head link;
	u32 grf_val;
	int mmu_num;
	u32 base_addr[2];
	void __iomem *bases[2];
	u32 dte_addr;
	u32 is_paged;
};

struct mpp_dev;

struct mpp_iommu_info {
	struct rw_semaphore rw_sem;

	struct device *dev;
	struct platform_device *pdev;
	struct iommu_domain *domain;
	struct iommu_group *group;
	struct mpp_rk_iommu *iommu;
	iommu_fault_handler_t hdl;

	spinlock_t dev_lock;
	struct mpp_dev *dev_active;

	u32 av1d_iommu;
	int irq;
	int got_irq;
};

struct mpp_dma_session *
mpp_dma_session_create(struct device *dev, u32 max_buffers);
int mpp_dma_session_destroy(struct mpp_dma_session *dma);

struct mpp_dma_buffer *
mpp_dma_alloc(struct device *dev, size_t size);
int mpp_dma_free(struct mpp_dma_buffer *buffer);

struct mpp_dma_buffer *
mpp_dma_import_fd(struct mpp_iommu_info *iommu_info,
		  struct mpp_dma_session *dma, int fd);
int mpp_dma_release(struct mpp_dma_session *dma,
		    struct mpp_dma_buffer *buffer);
int mpp_dma_release_fd(struct mpp_dma_session *dma, int fd);

int mpp_dma_unmap_kernel(struct mpp_dma_session *dma,
			 struct mpp_dma_buffer *buffer);
int mpp_dma_map_kernel(struct mpp_dma_session *dma,
		       struct mpp_dma_buffer *buffer);

struct mpp_iommu_info *
mpp_iommu_probe(struct device *dev);
int mpp_iommu_remove(struct mpp_iommu_info *info);

int mpp_iommu_attach(struct mpp_iommu_info *info);
int mpp_iommu_detach(struct mpp_iommu_info *info);

int mpp_iommu_refresh(struct mpp_iommu_info *info, struct device *dev);
int mpp_iommu_flush_tlb(struct mpp_iommu_info *info);
int mpp_av1_iommu_disable(struct device *dev);
int mpp_av1_iommu_enable(struct device *dev);

int mpp_iommu_dev_activate(struct mpp_iommu_info *info, struct mpp_dev *dev);
int mpp_iommu_dev_deactivate(struct mpp_iommu_info *info, struct mpp_dev *dev);

static inline int mpp_iommu_down_read(struct mpp_iommu_info *info)
{
	if (info)
		down_read(&info->rw_sem);

	return 0;
}

static inline int mpp_iommu_up_read(struct mpp_iommu_info *info)
{
	if (info)
		up_read(&info->rw_sem);

	return 0;
}

static inline int mpp_iommu_down_write(struct mpp_iommu_info *info)
{
	if (info)
		down_write(&info->rw_sem);

	return 0;
}

static inline int mpp_iommu_up_write(struct mpp_iommu_info *info)
{
	if (info)
		up_write(&info->rw_sem);

	return 0;
}

#endif
