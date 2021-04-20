// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *	Alpha Lin, alpha.lin@rock-chips.com
 *	Randy Li, randy.li@rock-chips.com
 *	Ding Wei, leo.ding@rock-chips.com
 *
 */
#include <linux/delay.h>
#include <linux/dma-buf.h>
#include <linux/dma-iommu.h>
#include <linux/iommu.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/kref.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>

#ifdef CONFIG_ARM_DMA_USE_IOMMU
#include <asm/dma-iommu.h>
#endif

#include "mpp_debug.h"
#include "mpp_iommu.h"

static struct mpp_dma_buffer *
mpp_dma_find_buffer_fd(struct mpp_dma_session *dma, int fd)
{
	struct dma_buf *dmabuf;
	struct mpp_dma_buffer *out = NULL;
	struct mpp_dma_buffer *buffer = NULL, *n;

	dmabuf = dma_buf_get(fd);
	if (IS_ERR(dmabuf))
		return NULL;

	mutex_lock(&dma->list_mutex);
	list_for_each_entry_safe(buffer, n,
				 &dma->used_list, link) {
		/*
		 * fd may dup several and point the same dambuf.
		 * thus, here should be distinguish with the dmabuf.
		 */
		if (buffer->dmabuf == dmabuf) {
			out = buffer;
			break;
		}
	}
	mutex_unlock(&dma->list_mutex);
	dma_buf_put(dmabuf);

	return out;
}

/* Release the buffer from the current list */
static void mpp_dma_release_buffer(struct kref *ref)
{
	struct mpp_dma_buffer *buffer =
		container_of(ref, struct mpp_dma_buffer, ref);

	buffer->dma->buffer_count--;
	list_move_tail(&buffer->link, &buffer->dma->unused_list);

	dma_buf_unmap_attachment(buffer->attach, buffer->sgt, buffer->dir);
	dma_buf_detach(buffer->dmabuf, buffer->attach);
	dma_buf_put(buffer->dmabuf);
}

/* Remove the oldest buffer when count more than the setting */
static int
mpp_dma_remove_extra_buffer(struct mpp_dma_session *dma)
{
	struct mpp_dma_buffer *n;
	struct mpp_dma_buffer *oldest = NULL, *buffer = NULL;
	ktime_t oldest_time = ktime_set(0, 0);

	if (dma->buffer_count > dma->max_buffers) {
		mutex_lock(&dma->list_mutex);
		list_for_each_entry_safe(buffer, n,
					 &dma->used_list,
					 link) {
			if (ktime_to_ns(oldest_time) == 0 ||
			    ktime_after(oldest_time, buffer->last_used)) {
				oldest_time = buffer->last_used;
				oldest = buffer;
			}
		}
		if (oldest)
			kref_put(&oldest->ref, mpp_dma_release_buffer);
		mutex_unlock(&dma->list_mutex);
	}

	return 0;
}

int mpp_dma_release(struct mpp_dma_session *dma,
		    struct mpp_dma_buffer *buffer)
{
	mutex_lock(&dma->list_mutex);
	kref_put(&buffer->ref, mpp_dma_release_buffer);
	mutex_unlock(&dma->list_mutex);

	return 0;
}

int mpp_dma_release_fd(struct mpp_dma_session *dma, int fd)
{
	struct device *dev = dma->dev;
	struct mpp_dma_buffer *buffer = NULL;

	buffer = mpp_dma_find_buffer_fd(dma, fd);
	if (IS_ERR_OR_NULL(buffer)) {
		dev_err(dev, "can not find %d buffer in list\n", fd);

		return -EINVAL;
	}

	mutex_lock(&dma->list_mutex);
	kref_put(&buffer->ref, mpp_dma_release_buffer);
	mutex_unlock(&dma->list_mutex);

	return 0;
}

struct mpp_dma_buffer *
mpp_dma_alloc(struct device *dev, size_t size)
{
	size_t align_size;
	dma_addr_t iova;
	struct  mpp_dma_buffer *buffer;

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer)
		return NULL;

	align_size = PAGE_ALIGN(size);
	buffer->vaddr = dma_alloc_coherent(dev, align_size, &iova, GFP_KERNEL);
	if (!buffer->vaddr)
		goto fail_dma_alloc;

	buffer->size = align_size;
	buffer->iova = iova;
	buffer->dev = dev;

	return buffer;
fail_dma_alloc:
	kfree(buffer);
	return NULL;
}

int mpp_dma_free(struct mpp_dma_buffer *buffer)
{
	dma_free_coherent(buffer->dev, buffer->size,
			buffer->vaddr, buffer->iova);
	buffer->vaddr = NULL;
	buffer->iova = 0;
	buffer->size = 0;
	buffer->dev = NULL;
	kfree(buffer);

	return 0;
}

struct mpp_dma_buffer *mpp_dma_import_fd(struct mpp_iommu_info *iommu_info,
					 struct mpp_dma_session *dma,
					 int fd)
{
	int ret = 0;
	struct sg_table *sgt;
	struct dma_buf *dmabuf;
	struct mpp_dma_buffer *buffer;
	struct dma_buf_attachment *attach;

	if (!dma) {
		mpp_err("dma session is null\n");
		return ERR_PTR(-EINVAL);
	}

	/* remove the oldest before add buffer */
	mpp_dma_remove_extra_buffer(dma);

	/* Check whether in dma session */
	buffer = mpp_dma_find_buffer_fd(dma, fd);
	if (!IS_ERR_OR_NULL(buffer)) {
		if (kref_get_unless_zero(&buffer->ref)) {
			buffer->last_used = ktime_get();
			return buffer;
		}
		dev_dbg(dma->dev, "missing the fd %d\n", fd);
	}

	dmabuf = dma_buf_get(fd);
	if (IS_ERR(dmabuf)) {
		mpp_err("dma_buf_get fd %d failed\n", fd);
		return NULL;
	}
	/* A new DMA buffer */
	mutex_lock(&dma->list_mutex);
	buffer = list_first_entry_or_null(&dma->unused_list,
					   struct mpp_dma_buffer,
					   link);
	if (!buffer) {
		ret = -ENOMEM;
		mutex_unlock(&dma->list_mutex);
		goto fail;
	}
	list_del_init(&buffer->link);
	mutex_unlock(&dma->list_mutex);

	buffer->dmabuf = dmabuf;
	buffer->dir = DMA_BIDIRECTIONAL;
	buffer->last_used = ktime_get();

	attach = dma_buf_attach(buffer->dmabuf, dma->dev);
	if (IS_ERR(attach)) {
		mpp_err("dma_buf_attach fd %d failed\n", fd);
		ret = PTR_ERR(attach);
		goto fail_attach;
	}

	sgt = dma_buf_map_attachment(attach, buffer->dir);
	if (IS_ERR(sgt)) {
		mpp_err("dma_buf_map_attachment fd %d failed\n", fd);
		ret = PTR_ERR(sgt);
		goto fail_map;
	}
	buffer->iova = sg_dma_address(sgt->sgl);
	buffer->size = sg_dma_len(sgt->sgl);
	buffer->attach = attach;
	buffer->sgt = sgt;
	buffer->dma = dma;

	kref_init(&buffer->ref);
	/* Increase the reference for used outside the buffer pool */
	kref_get(&buffer->ref);

	mutex_lock(&dma->list_mutex);
	dma->buffer_count++;
	list_add_tail(&buffer->link, &dma->used_list);
	mutex_unlock(&dma->list_mutex);

	return buffer;

fail_map:
	dma_buf_detach(buffer->dmabuf, attach);
fail_attach:
	mutex_lock(&dma->list_mutex);
	list_add_tail(&buffer->link, &dma->unused_list);
	mutex_unlock(&dma->list_mutex);
fail:
	dma_buf_put(dmabuf);
	return ERR_PTR(ret);
}

int mpp_dma_unmap_kernel(struct mpp_dma_session *dma,
			 struct mpp_dma_buffer *buffer)
{
	void *vaddr = buffer->vaddr;
	struct dma_buf *dmabuf = buffer->dmabuf;

	if (IS_ERR_OR_NULL(vaddr) ||
	    IS_ERR_OR_NULL(dmabuf))
		return -EINVAL;

	dma_buf_vunmap(dmabuf, vaddr);
	buffer->vaddr = NULL;

	dma_buf_end_cpu_access(dmabuf, DMA_FROM_DEVICE);

	return 0;
}

int mpp_dma_map_kernel(struct mpp_dma_session *dma,
		       struct mpp_dma_buffer *buffer)
{
	int ret;
	void *vaddr;
	struct dma_buf *dmabuf = buffer->dmabuf;

	if (IS_ERR_OR_NULL(dmabuf))
		return -EINVAL;

	ret = dma_buf_begin_cpu_access(dmabuf, DMA_FROM_DEVICE);
	if (ret) {
		dev_dbg(dma->dev, "can't access the dma buffer\n");
		goto failed_access;
	}

	vaddr = dma_buf_vmap(dmabuf);
	if (!vaddr) {
		dev_dbg(dma->dev, "can't vmap the dma buffer\n");
		ret = -EIO;
		goto failed_vmap;
	}

	buffer->vaddr = vaddr;

	return 0;

failed_vmap:
	dma_buf_end_cpu_access(dmabuf, DMA_FROM_DEVICE);
failed_access:

	return ret;
}

int mpp_dma_session_destroy(struct mpp_dma_session *dma)
{
	struct mpp_dma_buffer *n, *buffer = NULL;

	if (!dma)
		return -EINVAL;

	mutex_lock(&dma->list_mutex);
	list_for_each_entry_safe(buffer, n,
				 &dma->used_list,
				 link) {
		kref_put(&buffer->ref, mpp_dma_release_buffer);
	}
	mutex_unlock(&dma->list_mutex);

	kfree(dma);

	return 0;
}

struct mpp_dma_session *
mpp_dma_session_create(struct device *dev, u32 max_buffers)
{
	int i;
	struct mpp_dma_session *dma = NULL;
	struct mpp_dma_buffer *buffer = NULL;

	dma = kzalloc(sizeof(*dma), GFP_KERNEL);
	if (!dma)
		return NULL;

	mutex_init(&dma->list_mutex);
	INIT_LIST_HEAD(&dma->unused_list);
	INIT_LIST_HEAD(&dma->used_list);

	if (max_buffers > MPP_SESSION_MAX_BUFFERS) {
		mpp_debug(DEBUG_IOCTL, "session_max_buffer %d must less than %d\n",
			  max_buffers, MPP_SESSION_MAX_BUFFERS);
		dma->max_buffers = MPP_SESSION_MAX_BUFFERS;
	} else {
		dma->max_buffers = max_buffers;
	}

	for (i = 0; i < ARRAY_SIZE(dma->dma_bufs); i++) {
		buffer = &dma->dma_bufs[i];
		buffer->dma = dma;
		INIT_LIST_HEAD(&buffer->link);
		list_add_tail(&buffer->link, &dma->unused_list);
	}
	dma->dev = dev;

	return dma;
}

int mpp_iommu_detach(struct mpp_iommu_info *info)
{
	struct iommu_domain *domain = info->domain;
	struct iommu_group *group = info->group;

	iommu_detach_group(domain, group);

	return 0;
}

int mpp_iommu_attach(struct mpp_iommu_info *info)
{
	struct iommu_domain *domain = info->domain;
	struct iommu_group *group = info->group;
	int ret;

	ret = iommu_attach_group(domain, group);
	if (ret)
		return ret;

	return 0;
}

struct mpp_iommu_info *
mpp_iommu_probe(struct device *dev)
{
	int ret = 0;
	struct device_node *np = NULL;
	struct platform_device *pdev = NULL;
	struct mpp_iommu_info *info = NULL;
#ifdef CONFIG_ARM_DMA_USE_IOMMU
	struct dma_iommu_mapping *mapping;
#endif
	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return ERR_PTR(-ENOMEM);

	np = of_parse_phandle(dev->of_node, "iommus", 0);
	if (!np || !of_device_is_available(np)) {
		mpp_err("failed to get device node\n");
		return ERR_PTR(-ENODEV);
	}

	pdev = of_find_device_by_node(np);
	of_node_put(np);
	if (!pdev) {
		mpp_err("failed to get platform device\n");
		return ERR_PTR(-ENODEV);
	}

	info->group = iommu_group_get(dev);
	if (!info->group) {
		ret = -EINVAL;
		goto err_put_pdev;
	}

	/*
	 * On arm32-arch, group->default_domain should be NULL,
	 * domain store in mapping created by arm32-arch.
	 * we re-attach domain here
	 */
#ifdef CONFIG_ARM_DMA_USE_IOMMU
	if (!iommu_group_default_domain(info->group)) {
		mapping = to_dma_iommu_mapping(dev);
		WARN_ON(!mapping);
		info->domain = mapping->domain;
	}
#endif
	if (!info->domain) {
		info->domain = iommu_get_domain_for_dev(dev);
		if (!info->domain) {
			ret = -EINVAL;
			goto err_put_group;
		}
	}

	info->dev = dev;
	info->pdev = pdev;
	init_rwsem(&info->rw_sem);

	return info;

err_put_group:
	iommu_group_put(info->group);
err_put_pdev:
	platform_device_put(pdev);

	return ERR_PTR(ret);
}

int mpp_iommu_remove(struct mpp_iommu_info *info)
{
	iommu_group_put(info->group);
	platform_device_put(info->pdev);

	return 0;
}

#define RK_MMU_DTE_ADDR			0x00 /* Directory table address */
#define RK_MMU_STATUS			0x04
#define RK_MMU_COMMAND			0x08
#define RK_MMU_INT_MASK			0x1C /* IRQ enable */

/* RK_MMU_COMMAND command values */
#define RK_MMU_CMD_ENABLE_PAGING	0 /* Enable memory translation */
#define RK_MMU_CMD_DISABLE_PAGING	1 /* Disable memory translation */
#define RK_MMU_CMD_ENABLE_STALL		2 /* Stall paging to allow other cmds */
#define RK_MMU_CMD_DISABLE_STALL	3 /* Stop stall re-enables paging */
#define RK_MMU_CMD_ZAP_CACHE		4 /* Shoot down entire IOTLB */
#define RK_MMU_CMD_PAGE_FAULT_DONE	5 /* Clear page fault */
#define RK_MMU_CMD_FORCE_RESET		6 /* Reset all registers */

/* RK_MMU_INT_* register fields */
#define RK_MMU_IRQ_MASK			0x03
/* RK_MMU_STATUS fields */
#define RK_MMU_STATUS_PAGING_ENABLED	BIT(0)
#define RK_MMU_STATUS_STALL_ACTIVE	BIT(2)

bool mpp_iommu_is_paged(struct mpp_rk_iommu *iommu)
{
	int i;
	u32 status;
	bool active = true;

	for (i = 0; i < iommu->mmu_num; i++) {
		status = readl(iommu->bases[i] + RK_MMU_STATUS);
		active &= !!(status & RK_MMU_STATUS_PAGING_ENABLED);
	}

	return active;
}

u32 mpp_iommu_get_dte_addr(struct mpp_rk_iommu *iommu)
{
	return readl(iommu->bases[0] + RK_MMU_DTE_ADDR);
}

int mpp_iommu_enable(struct mpp_rk_iommu *iommu)
{
	int i;

	/* check iommu whether is paged */
	iommu->is_paged = mpp_iommu_is_paged(iommu);
	if (iommu->is_paged)
		return 0;

	/* enable stall */
	for (i = 0; i < iommu->mmu_num; i++)
		writel(RK_MMU_CMD_ENABLE_STALL,
		       iommu->bases[i] + RK_MMU_COMMAND);
	udelay(2);
	/* force reset */
	for (i = 0; i < iommu->mmu_num; i++)
		writel(RK_MMU_CMD_FORCE_RESET,
		       iommu->bases[i] + RK_MMU_COMMAND);
	udelay(2);

	for (i = 0; i < iommu->mmu_num; i++) {
		/* restore dte and status */
		writel(iommu->dte_addr,
		       iommu->bases[i] + RK_MMU_DTE_ADDR);
		/* zap cache */
		writel(RK_MMU_CMD_ZAP_CACHE,
		       iommu->bases[i] + RK_MMU_COMMAND);
		/* irq mask */
		writel(RK_MMU_IRQ_MASK,
		       iommu->bases[i] + RK_MMU_INT_MASK);
	}
	udelay(2);
	/* enable paging */
	for (i = 0; i < iommu->mmu_num; i++)
		writel(RK_MMU_CMD_ENABLE_PAGING,
		       iommu->bases[i] + RK_MMU_COMMAND);
	udelay(2);
	/* disable stall */
	for (i = 0; i < iommu->mmu_num; i++)
		writel(RK_MMU_CMD_DISABLE_STALL,
		       iommu->bases[i] + RK_MMU_COMMAND);
	udelay(2);

	/* iommu should be paging enable */
	iommu->is_paged = mpp_iommu_is_paged(iommu);
	if (!iommu->is_paged) {
		mpp_err("iommu->base_addr=%08x enable failed\n",
			iommu->base_addr[0]);
		return -EINVAL;
	}

	return 0;
}

int mpp_iommu_disable(struct mpp_rk_iommu *iommu)
{
	int i;
	u32 dte;

	if (iommu->is_paged) {
		dte = readl(iommu->bases[0] + RK_MMU_DTE_ADDR);
		if (!dte)
			return -EINVAL;
		udelay(2);
		/* enable stall */
		for (i = 0; i < iommu->mmu_num; i++)
			writel(RK_MMU_CMD_ENABLE_STALL,
			       iommu->bases[i] + RK_MMU_COMMAND);
		udelay(2);
		/* disable paging */
		for (i = 0; i < iommu->mmu_num; i++)
			writel(RK_MMU_CMD_DISABLE_PAGING,
			       iommu->bases[i] + RK_MMU_COMMAND);
		udelay(2);
		/* disable stall */
		for (i = 0; i < iommu->mmu_num; i++)
			writel(RK_MMU_CMD_DISABLE_STALL,
			       iommu->bases[i] + RK_MMU_COMMAND);
		udelay(2);
	}

	return 0;
}

int mpp_iommu_refresh(struct mpp_iommu_info *info, struct device *dev)
{
	int i;
	int usage_count;
	struct device_link *link;
	struct device *iommu_dev = &info->pdev->dev;

	rcu_read_lock();

	usage_count = atomic_read(&iommu_dev->power.usage_count);
	list_for_each_entry_rcu(link, &dev->links.suppliers, c_node) {
		for (i = 0; i < usage_count; i++)
			pm_runtime_put_sync(link->supplier);
	}

	list_for_each_entry_rcu(link, &dev->links.suppliers, c_node) {
		for (i = 0; i < usage_count; i++)
			pm_runtime_get_sync(link->supplier);
	}

	rcu_read_unlock();

	return 0;
}
