/*
 * Copyright (C) 2016 Fuzhou Rockchip Electronics Co., Ltd
 * author: Jung Zhao jung.zhao@rock-chips.com
 *         Randy Li, randy.li@rock-chips.com
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
#include <drm/drm_device.h>
#include <linux/dma-iommu.h>
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/iommu.h>
#include <linux/kref.h>
#include <linux/slab.h>

#include "iep_iommu_ops.h"

struct iep_drm_buffer {
	struct list_head list;
	struct dma_buf *dma_buf;
	union {
		unsigned long iova;
		unsigned long phys;
	};
	unsigned long size;
	int index;
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	struct page **pages;
	struct kref ref;
	struct iep_iommu_session_info *session_info;
};

struct iep_iommu_drm_info {
	struct iommu_domain *domain;
	bool attached;
};

static struct iep_drm_buffer *
iep_drm_get_buffer_no_lock(struct iep_iommu_session_info *session_info,
			   int idx)
{
	struct iep_drm_buffer *drm_buffer = NULL, *n;

	list_for_each_entry_safe(drm_buffer, n, &session_info->buffer_list,
				 list) {
		if (drm_buffer->index == idx)
			return drm_buffer;
	}

	return NULL;
}

static struct iep_drm_buffer *
iep_drm_get_buffer_fd_no_lock(struct iep_iommu_session_info *session_info,
			      int fd)
{
	struct iep_drm_buffer *drm_buffer = NULL, *n;
	struct dma_buf *dma_buf = NULL;

	dma_buf = dma_buf_get(fd);

	list_for_each_entry_safe(drm_buffer, n, &session_info->buffer_list,
				 list) {
		if (drm_buffer->dma_buf == dma_buf) {
			dma_buf_put(dma_buf);
			return drm_buffer;
		}
	}

	dma_buf_put(dma_buf);

	return NULL;
}

static void iep_drm_detach(struct iep_iommu_info *iommu_info)
{
	struct iep_iommu_drm_info *drm_info = iommu_info->private;
	struct device *dev = iommu_info->dev;
	struct iommu_domain *domain = drm_info->domain;

	mutex_lock(&iommu_info->iommu_mutex);

	if (!drm_info->attached) {
		mutex_unlock(&iommu_info->iommu_mutex);
		return;
	}

	iommu_detach_device(domain, dev);
	drm_info->attached = false;

	mutex_unlock(&iommu_info->iommu_mutex);
}

static int iep_drm_attach_unlock(struct iep_iommu_info *iommu_info)
{
	struct iep_iommu_drm_info *drm_info = iommu_info->private;
	struct device *dev = iommu_info->dev;
	struct iommu_domain *domain = drm_info->domain;
	int ret = 0;

	ret = dma_set_coherent_mask(dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	dma_set_max_seg_size(dev, DMA_BIT_MASK(32));
	ret = iommu_attach_device(domain, dev);
	if (ret) {
		dev_err(dev, "Failed to attach iommu device\n");
		return ret;
	}

	return ret;
}

static int iep_drm_attach(struct iep_iommu_info *iommu_info)
{
	struct iep_iommu_drm_info *drm_info = iommu_info->private;
	int ret;

	mutex_lock(&iommu_info->iommu_mutex);

	if (drm_info->attached) {
		mutex_unlock(&iommu_info->iommu_mutex);
		return 0;
	}

	ret = iep_drm_attach_unlock(iommu_info);
	if (ret) {
		mutex_unlock(&iommu_info->iommu_mutex);
		return ret;
	}

	drm_info->attached = true;

	mutex_unlock(&iommu_info->iommu_mutex);

	return ret;
}

static void iep_drm_clear_map(struct kref *ref)
{
	struct iep_drm_buffer *drm_buffer =
		container_of(ref, struct iep_drm_buffer, ref);
	struct iep_iommu_session_info *session_info =
		drm_buffer->session_info;
	struct iep_iommu_info *iommu_info = session_info->iommu_info;
	struct iep_iommu_drm_info *drm_info = iommu_info->private;
	struct device *dev = session_info->dev;
	struct iommu_domain *domain = drm_info->domain;

	mutex_lock(&iommu_info->iommu_mutex);
	drm_info = session_info->iommu_info->private;
	if (!drm_info->attached) {
		if (iep_drm_attach_unlock(session_info->iommu_info))
			dev_err(dev, "can't clea map, attach iommu failed.\n");
	}

	if (drm_buffer->attach) {
		dma_buf_unmap_attachment(drm_buffer->attach, drm_buffer->sgt,
					 DMA_BIDIRECTIONAL);
		dma_buf_detach(drm_buffer->dma_buf, drm_buffer->attach);
		dma_buf_put(drm_buffer->dma_buf);
		drm_buffer->attach = NULL;
	}

	if (!drm_info->attached)
		iommu_detach_device(domain, dev);

	mutex_unlock(&iommu_info->iommu_mutex);
}

static void vcdoec_drm_dump_info(struct iep_iommu_session_info *session_info)
{
	struct iep_drm_buffer *drm_buffer = NULL, *n;

	vpu_iommu_debug(session_info->debug_level, DEBUG_IOMMU_OPS_DUMP,
			"still there are below buffers stored in list\n");
	list_for_each_entry_safe(drm_buffer, n, &session_info->buffer_list,
				 list) {
		vpu_iommu_debug(session_info->debug_level, DEBUG_IOMMU_OPS_DUMP,
				"index %d drm_buffer dma_buf %p\n",
				drm_buffer->index,
				drm_buffer->dma_buf);
	}
}

static int iep_drm_free(struct iep_iommu_session_info *session_info,
			int idx)
{
	struct device *dev = session_info->dev;
	/* please double-check all maps have been release */
	struct iep_drm_buffer *drm_buffer;

	mutex_lock(&session_info->list_mutex);
	drm_buffer = iep_drm_get_buffer_no_lock(session_info, idx);

	if (!drm_buffer) {
		dev_err(dev, "can not find %d buffer in list\n", idx);
		mutex_unlock(&session_info->list_mutex);

		return -EINVAL;
	}

	if (kref_read(&drm_buffer->ref) == 0) {
		dma_buf_put(drm_buffer->dma_buf);
		list_del_init(&drm_buffer->list);
		kfree(drm_buffer);
		session_info->buffer_nums--;
		vpu_iommu_debug(session_info->debug_level, DEBUG_IOMMU_NORMAL,
			"buffer nums %d\n", session_info->buffer_nums);
	}
	mutex_unlock(&session_info->list_mutex);

	return 0;
}

static int
iep_drm_unmap_iommu(struct iep_iommu_session_info *session_info,
		    int idx)
{
	struct device *dev = session_info->dev;
	struct iep_drm_buffer *drm_buffer;

	mutex_lock(&session_info->list_mutex);
	drm_buffer = iep_drm_get_buffer_no_lock(session_info, idx);
	mutex_unlock(&session_info->list_mutex);

	if (!drm_buffer) {
		dev_err(dev, "can not find %d buffer in list\n", idx);
		return -EINVAL;
	}

	kref_put(&drm_buffer->ref, iep_drm_clear_map);

	return 0;
}

static int iep_drm_map_iommu(struct iep_iommu_session_info *session_info,
			     int idx,
			     unsigned long *iova,
			     unsigned long *size)
{
	struct device *dev = session_info->dev;
	struct iep_drm_buffer *drm_buffer;

	mutex_lock(&session_info->list_mutex);
	drm_buffer = iep_drm_get_buffer_no_lock(session_info, idx);
	mutex_unlock(&session_info->list_mutex);

	if (!drm_buffer) {
		dev_err(dev, "can not find %d buffer in list\n", idx);
		return -EINVAL;
	}

	kref_get(&drm_buffer->ref);
	if (iova)
		*iova = drm_buffer->iova;
	if (size)
		*size = drm_buffer->size;
	return 0;
}

static int
iep_drm_free_fd(struct iep_iommu_session_info *session_info, int fd)
{
	/* please double-check all maps have been release */
	struct iep_drm_buffer *drm_buffer = NULL;

	mutex_lock(&session_info->list_mutex);
	drm_buffer = iep_drm_get_buffer_fd_no_lock(session_info, fd);

	if (!drm_buffer) {
		vpu_iommu_debug(session_info->debug_level, DEBUG_IOMMU_NORMAL,
				"can not find %d buffer in list\n", fd);
		mutex_unlock(&session_info->list_mutex);

		return -EINVAL;
	}
	mutex_unlock(&session_info->list_mutex);

	iep_drm_unmap_iommu(session_info, drm_buffer->index);

	mutex_lock(&session_info->list_mutex);
	if (kref_read(&drm_buffer->ref) == 0) {
		dma_buf_put(drm_buffer->dma_buf);
		list_del_init(&drm_buffer->list);
		kfree(drm_buffer);
		session_info->buffer_nums--;
		vpu_iommu_debug(session_info->debug_level, DEBUG_IOMMU_NORMAL,
				"buffer nums %d\n", session_info->buffer_nums);
	}
	mutex_unlock(&session_info->list_mutex);

	return 0;
}

static void
iep_drm_clear_session(struct iep_iommu_session_info *session_info)
{
	struct iep_drm_buffer *drm_buffer = NULL, *n;

	list_for_each_entry_safe(drm_buffer, n, &session_info->buffer_list,
				 list) {
		kref_put(&drm_buffer->ref, iep_drm_clear_map);
		iep_drm_free(session_info, drm_buffer->index);
	}
}

static int iep_drm_import(struct iep_iommu_session_info *session_info,
			  int fd)
{
	struct iep_drm_buffer *drm_buffer = NULL, *n;
	struct iep_iommu_info *iommu_info = session_info->iommu_info;
	struct iep_iommu_drm_info *drm_info = iommu_info->private;
	struct iommu_domain *domain = drm_info->domain;
	struct device *dev = session_info->dev;
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	struct dma_buf *dma_buf;
	int ret = 0;

	dma_buf = dma_buf_get(fd);
	if (IS_ERR(dma_buf)) {
		ret = PTR_ERR(dma_buf);
		return ret;
	}

	list_for_each_entry_safe(drm_buffer, n,
				 &session_info->buffer_list, list) {
		if (drm_buffer->dma_buf == dma_buf) {
			dma_buf_put(dma_buf);
			return drm_buffer->index;
		}
	}

	drm_buffer = kzalloc(sizeof(*drm_buffer), GFP_KERNEL);
	if (!drm_buffer) {
		ret = -ENOMEM;
		return ret;
	}

	drm_buffer->dma_buf = dma_buf;
	drm_buffer->session_info = session_info;

	kref_init(&drm_buffer->ref);

	mutex_lock(&iommu_info->iommu_mutex);
	drm_info = session_info->iommu_info->private;
	if (!drm_info->attached) {
		ret = iep_drm_attach_unlock(session_info->iommu_info);
		if (ret)
			goto fail_out;
	}

	attach = dma_buf_attach(drm_buffer->dma_buf, dev);
	if (IS_ERR(attach)) {
		ret = PTR_ERR(attach);
		goto fail_out;
	}

	get_dma_buf(drm_buffer->dma_buf);

	sgt = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(sgt)) {
		ret = PTR_ERR(sgt);
		goto fail_detach;
	}

	drm_buffer->iova = sg_dma_address(sgt->sgl);
	drm_buffer->size = drm_buffer->dma_buf->size;

	drm_buffer->attach = attach;
	drm_buffer->sgt = sgt;

	if (!drm_info->attached)
		iommu_detach_device(domain, dev);

	mutex_unlock(&iommu_info->iommu_mutex);

	INIT_LIST_HEAD(&drm_buffer->list);
	mutex_lock(&session_info->list_mutex);
	session_info->buffer_nums++;
	vpu_iommu_debug(session_info->debug_level, DEBUG_IOMMU_NORMAL,
			"buffer nums %d\n", session_info->buffer_nums);
	drm_buffer->index = session_info->max_idx;
	list_add_tail(&drm_buffer->list, &session_info->buffer_list);
	session_info->max_idx++;
	if ((session_info->max_idx & 0xfffffff) == 0)
		session_info->max_idx = 0;
	mutex_unlock(&session_info->list_mutex);

	return drm_buffer->index;

fail_detach:
	dev_err(dev, "dmabuf map attach failed\n");
	dma_buf_detach(drm_buffer->dma_buf, attach);
	dma_buf_put(drm_buffer->dma_buf);
fail_out:
	kfree(drm_buffer);
	mutex_unlock(&iommu_info->iommu_mutex);

	return ret;
}

static int iep_drm_create(struct iep_iommu_info *iommu_info)
{
	struct iep_iommu_drm_info *drm_info;

	iommu_info->private = kzalloc(sizeof(*drm_info),
				      GFP_KERNEL);
	drm_info = iommu_info->private;
	if (!drm_info)
		return -ENOMEM;

	drm_info->domain = iommu_get_domain_for_dev(iommu_info->dev);
	drm_info->attached = false;
	if (!drm_info->domain) {
		kfree(iommu_info->private);
		return -ENOMEM;
	}

	return 0;
}

static int iep_drm_destroy(struct iep_iommu_info *iommu_info)
{
	struct iep_iommu_drm_info *drm_info = iommu_info->private;

	iep_drm_detach(iommu_info);

	kfree(drm_info);
	iommu_info->private = NULL;

	return 0;
}

static struct iep_iommu_ops drm_ops = {
	.create = iep_drm_create,
	.import = iep_drm_import,
	.free = iep_drm_free,
	.free_fd = iep_drm_free_fd,
	.map_iommu = iep_drm_map_iommu,
	.unmap_iommu = iep_drm_unmap_iommu,
	.destroy = iep_drm_destroy,
	.dump = vcdoec_drm_dump_info,
	.attach = iep_drm_attach,
	.detach = iep_drm_detach,
	.clear = iep_drm_clear_session,
};

void iep_iommu_drm_set_ops(struct iep_iommu_info *iommu_info)
{
	if (!iommu_info)
		return;
	iommu_info->ops = &drm_ops;
}
