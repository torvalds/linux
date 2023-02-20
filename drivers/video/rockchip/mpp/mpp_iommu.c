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
#include <linux/dma-buf-cache.h>
#include <linux/dma-iommu.h>
#include <linux/dma-mapping.h>
#include <linux/iommu.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/kref.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>

#ifdef CONFIG_ARM_DMA_USE_IOMMU
#include <asm/dma-iommu.h>
#endif
#include <soc/rockchip/rockchip_iommu.h>

#include "mpp_debug.h"
#include "mpp_iommu.h"
#include "mpp_common.h"

struct mpp_dma_buffer *
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
	buffer->dma = NULL;
	buffer->dmabuf = NULL;
	buffer->attach = NULL;
	buffer->sgt = NULL;
	buffer->copy_sgt = NULL;
	buffer->iova = 0;
	buffer->size = 0;
	buffer->vaddr = NULL;
	buffer->last_used = 0;
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
		if (oldest && kref_read(&oldest->ref) == 1)
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
	if (!IS_ENABLED(CONFIG_DMABUF_CACHE))
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
		ret = PTR_ERR(dmabuf);
		mpp_err("dma_buf_get fd %d failed(%d)\n", fd, ret);
		return ERR_PTR(ret);
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
		ret = PTR_ERR(attach);
		mpp_err("dma_buf_attach fd %d failed(%d)\n", fd, ret);
		goto fail_attach;
	}

	sgt = dma_buf_map_attachment(attach, buffer->dir);
	if (IS_ERR(sgt)) {
		ret = PTR_ERR(sgt);
		mpp_err("dma_buf_map_attachment fd %d failed(%d)\n", fd, ret);
		goto fail_map;
	}
	buffer->iova = sg_dma_address(sgt->sgl);
	buffer->size = sg_dma_len(sgt->sgl);
	buffer->attach = attach;
	buffer->sgt = sgt;
	buffer->dma = dma;

	kref_init(&buffer->ref);

	if (!IS_ENABLED(CONFIG_DMABUF_CACHE))
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

/*
 * begin cpu access => for_cpu = true
 * end cpu access => for_cpu = false
 */
void mpp_dma_buf_sync(struct mpp_dma_buffer *buffer, u32 offset, u32 length,
		      enum dma_data_direction dir, bool for_cpu)
{
	struct device *dev = buffer->dma->dev;
	struct sg_table *sgt = buffer->sgt;
	struct scatterlist *sg = sgt->sgl;
	dma_addr_t sg_dma_addr = sg_dma_address(sg);
	unsigned int len = 0;
	int i;

	for_each_sgtable_sg(sgt, sg, i) {
		unsigned int sg_offset, sg_left, size = 0;

		len += sg->length;
		if (len <= offset) {
			sg_dma_addr += sg->length;
			continue;
		}

		sg_left = len - offset;
		sg_offset = sg->length - sg_left;

		size = (length < sg_left) ? length : sg_left;

		if (for_cpu)
			dma_sync_single_range_for_cpu(dev, sg_dma_addr,
						      sg_offset, size, dir);
		else
			dma_sync_single_range_for_device(dev, sg_dma_addr,
							 sg_offset, size, dir);

		offset += size;
		length -= size;
		sg_dma_addr += sg->length;

		if (length == 0)
			break;
	}
}

int mpp_iommu_detach(struct mpp_iommu_info *info)
{
	if (!info)
		return 0;

	iommu_detach_group(info->domain, info->group);
	return 0;
}

int mpp_iommu_attach(struct mpp_iommu_info *info)
{
	if (!info)
		return 0;

	if (info->domain == iommu_get_domain_for_dev(info->dev))
		return 0;

	return iommu_attach_group(info->domain, info->group);
}

static int mpp_iommu_handle(struct iommu_domain *iommu,
			    struct device *iommu_dev,
			    unsigned long iova,
			    int status, void *arg)
{
	struct mpp_dev *mpp = (struct mpp_dev *)arg;

	dev_err(iommu_dev, "fault addr 0x%08lx status %x arg %p\n",
		iova, status, arg);

	if (!mpp) {
		dev_err(iommu_dev, "pagefault without device to handle\n");
		return 0;
	}

	if (mpp->dev_ops && mpp->dev_ops->dump_dev)
		mpp->dev_ops->dump_dev(mpp);
	else
		mpp_task_dump_hw_reg(mpp);

	return 0;
}

struct mpp_iommu_info *
mpp_iommu_probe(struct device *dev)
{
	int ret = 0;
	struct device_node *np = NULL;
	struct platform_device *pdev = NULL;
	struct mpp_iommu_info *info = NULL;
	struct iommu_domain *domain = NULL;
	struct iommu_group *group = NULL;
#ifdef CONFIG_ARM_DMA_USE_IOMMU
	struct dma_iommu_mapping *mapping;
#endif
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

	group = iommu_group_get(dev);
	if (!group) {
		ret = -EINVAL;
		goto err_put_pdev;
	}

	/*
	 * On arm32-arch, group->default_domain should be NULL,
	 * domain store in mapping created by arm32-arch.
	 * we re-attach domain here
	 */
#ifdef CONFIG_ARM_DMA_USE_IOMMU
	if (!iommu_group_default_domain(group)) {
		mapping = to_dma_iommu_mapping(dev);
		WARN_ON(!mapping);
		domain = mapping->domain;
	}
#endif
	if (!domain) {
		domain = iommu_get_domain_for_dev(dev);
		if (!domain) {
			ret = -EINVAL;
			goto err_put_group;
		}
	}

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info) {
		ret = -ENOMEM;
		goto err_put_group;
	}

	init_rwsem(&info->rw_sem);
	spin_lock_init(&info->dev_lock);
	info->dev = dev;
	info->pdev = pdev;
	info->group = group;
	info->domain = domain;
	info->dev_active = NULL;
	info->irq = platform_get_irq(pdev, 0);
	info->got_irq = (info->irq < 0) ? false : true;

	return info;

err_put_group:
	if (group)
		iommu_group_put(group);
err_put_pdev:
	if (pdev)
		platform_device_put(pdev);

	return ERR_PTR(ret);
}

int mpp_iommu_remove(struct mpp_iommu_info *info)
{
	if (!info)
		return 0;

	iommu_group_put(info->group);
	platform_device_put(info->pdev);

	return 0;
}

int mpp_iommu_refresh(struct mpp_iommu_info *info, struct device *dev)
{
	int ret;

	if (!info)
		return 0;
	/* call av1 iommu ops */
	if (IS_ENABLED(CONFIG_ROCKCHIP_MPP_AV1DEC) && info->av1d_iommu) {
		ret = mpp_av1_iommu_disable(dev);
		if (ret)
			return ret;
		return mpp_av1_iommu_enable(dev);
	}
	/* disable iommu */
	ret = rockchip_iommu_disable(dev);
	if (ret)
		return ret;
	/* re-enable iommu */
	return rockchip_iommu_enable(dev);
}

int mpp_iommu_flush_tlb(struct mpp_iommu_info *info)
{
	if (!info)
		return 0;

	if (info->domain && info->domain->ops)
		iommu_flush_iotlb_all(info->domain);

	return 0;
}

int mpp_iommu_dev_activate(struct mpp_iommu_info *info, struct mpp_dev *dev)
{
	unsigned long flags;
	int ret = 0;

	if (!info)
		return 0;

	spin_lock_irqsave(&info->dev_lock, flags);

	if (info->dev_active || !dev) {
		dev_err(info->dev, "can not activate %s -> %s\n",
			info->dev_active ? dev_name(info->dev_active->dev) : NULL,
			dev ? dev_name(dev->dev) : NULL);
		ret = -EINVAL;
	} else {
		info->dev_active = dev;
		/* switch domain pagefault handler and arg depending on device */
		iommu_set_fault_handler(info->domain, dev->fault_handler ?
					dev->fault_handler : mpp_iommu_handle, dev);

		dev_dbg(info->dev, "activate -> %p %s\n", dev, dev_name(dev->dev));
	}

	spin_unlock_irqrestore(&info->dev_lock, flags);

	return ret;
}

int mpp_iommu_dev_deactivate(struct mpp_iommu_info *info, struct mpp_dev *dev)
{
	unsigned long flags;

	if (!info)
		return 0;

	spin_lock_irqsave(&info->dev_lock, flags);

	if (info->dev_active != dev)
		dev_err(info->dev, "can not deactivate %s when %s activated\n",
			dev_name(dev->dev),
			info->dev_active ? dev_name(info->dev_active->dev) : NULL);

	dev_dbg(info->dev, "deactivate %p\n", info->dev_active);
	info->dev_active = NULL;
	spin_unlock_irqrestore(&info->dev_lock, flags);

	return 0;
}
