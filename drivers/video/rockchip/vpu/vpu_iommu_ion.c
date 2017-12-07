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

#include "vpu_iommu_ops.h"

#if defined(CONFIG_ION_ROCKCHIP)
#include <linux/rockchip_ion.h>
#include <linux/rockchip-iovmm.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <linux/memblock.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_graph.h>
#include <linux/component.h>
#include <linux/fence.h>
#include <linux/console.h>
#include <linux/kref.h>
#include <linux/fdtable.h>

struct vpu_ion_buffer {
	struct list_head list;
	struct ion_handle *handle;
	int index;
};

struct vpu_iommu_ion_info {
	struct ion_client *ion_client;
	bool attached;
};

static struct vpu_ion_buffer*
vpu_ion_get_buffer_no_lock(struct vpu_iommu_session_info *session_info,
			   int idx)
{
	struct vpu_ion_buffer *ion_buffer = NULL, *n;

	list_for_each_entry_safe(ion_buffer, n,
				 &session_info->buffer_list, list) {
		if (ion_buffer->index == idx)
			return ion_buffer;
	}

	return NULL;
}

static void
vpu_ion_clear_session(struct vpu_iommu_session_info *session_info)
{
	/* do nothing */
}

static int vpu_ion_attach(struct vpu_iommu_info *iommu_info)
{
	struct vpu_iommu_ion_info *ion_info = iommu_info->private;
					      int ret;

	mutex_lock(&iommu_info->iommu_mutex);

	if (ion_info->attached) {
		mutex_unlock(&iommu_info->iommu_mutex);
		return 0;
	}

	rockchip_iovmm_activate(iommu_info->dev);

	ion_info->attached = true;

	mutex_unlock(&iommu_info->iommu_mutex);

	return ret;
}

static void vpu_ion_detach(struct vpu_iommu_info *iommu_info)
{
	struct vpu_iommu_ion_info *ion_info = iommu_info->private;

	mutex_lock(&iommu_info->iommu_mutex);

	if (!ion_info->attached) {
		mutex_unlock(&iommu_info->iommu_mutex);
		return;
	}

	rockchip_iovmm_deactivate(iommu_info->dev);
	ion_info->attached = false;

	mutex_unlock(&iommu_info->iommu_mutex);
}

static int vpu_ion_destroy(struct vpu_iommu_info *iommu_info)
{
	struct vpu_iommu_ion_info *ion_info = iommu_info->private;

	vpu_ion_detach(iommu_info);
	kfree(ion_info);
	iommu_info->private = NULL;

	return 0;
}

static int
vpu_ion_free(struct vpu_iommu_session_info *session_info, int idx)
{
	struct vpu_ion_buffer *ion_buffer;

	mutex_lock(&session_info->list_mutex);
	ion_buffer = vpu_ion_get_buffer_no_lock(session_info, idx);

	if (!ion_buffer) {
		mutex_unlock(&session_info->list_mutex);
		pr_err("%s can not find %d buffer in list\n", __func__, idx);

		return -EINVAL;
	}

	list_del_init(&ion_buffer->list);
	mutex_unlock(&session_info->list_mutex);
	kfree(ion_buffer);

	return 0;
}

static int
vpu_ion_unmap_iommu(struct vpu_iommu_session_info *session_info, int idx)
{
	struct vpu_ion_buffer *ion_buffer;
	struct vpu_iommu_info *iommu_info = session_info->iommu_info;
	struct vpu_iommu_ion_info *ion_info = iommu_info->private;

	mutex_lock(&session_info->list_mutex);
	ion_buffer = vpu_ion_get_buffer_no_lock(session_info, idx);
	mutex_unlock(&session_info->list_mutex);

	if (!ion_buffer) {
		pr_err("%s can not find %d buffer in list\n", __func__, idx);

		return -EINVAL;
	}

	ion_free(ion_info->ion_client, ion_buffer->handle);

	return 0;
}

static int
vpu_ion_map_iommu(struct vpu_iommu_session_info *session_info, int idx,
		  unsigned long *iova, unsigned long *size)
{
	struct vpu_ion_buffer *ion_buffer;
	struct device *dev = session_info->dev;
	struct vpu_iommu_info *iommu_info = session_info->iommu_info;
	struct vpu_iommu_ion_info *ion_info = iommu_info->private;
					      int ret = 0;

	/* Force to flush iommu table */
	rockchip_iovmm_invalidate_tlb(session_info->dev);

	mutex_lock(&session_info->list_mutex);
	ion_buffer = vpu_ion_get_buffer_no_lock(session_info, idx);
	mutex_unlock(&session_info->list_mutex);

	if (!ion_buffer) {
		pr_err("%s can not find %d buffer in list\n", __func__, idx);

		return -EINVAL;
	}

	if (session_info->mmu_dev)
		ret = ion_map_iommu(dev, ion_info->ion_client,
				    ion_buffer->handle, iova, size);
	else
		ret = ion_phys(ion_info->ion_client, ion_buffer->handle,
			       iova, (size_t *)size);

	return ret;
}

static int
vpu_ion_unmap_kernel(struct vpu_iommu_session_info *session_info,
		     int idx)
{
	struct vpu_ion_buffer *ion_buffer;

	mutex_lock(&session_info->list_mutex);
	ion_buffer = vpu_ion_get_buffer_no_lock(session_info, idx);
	mutex_unlock(&session_info->list_mutex);

	if (!ion_buffer) {
		pr_err("%s can not find %d buffer in list\n", __func__, idx);

		return -EINVAL;
	}

	return 0;
}

static void*
vpu_ion_map_kernel(struct vpu_iommu_session_info *session_info, int idx)
{
	struct vpu_ion_buffer *ion_buffer;
	struct vpu_iommu_info *iommu_info = session_info->iommu_info;
	struct vpu_iommu_ion_info *ion_info = iommu_info->private;

	rockchip_iovmm_invalidate_tlb(session_info->dev);

	mutex_lock(&session_info->list_mutex);
	ion_buffer = vpu_ion_get_buffer_no_lock(session_info, idx);
	mutex_unlock(&session_info->list_mutex);

	if (!ion_buffer) {
		pr_err("%s can not find %d buffer in list\n", __func__, idx);

		return NULL;
	}

	return ion_map_kernel(ion_info->ion_client, ion_buffer->handle);
}

static int vpu_ion_alloc(struct vpu_iommu_session_info *session_info,
			 unsigned long size,
			 unsigned long align)
{
	struct vpu_ion_buffer *ion_buffer = NULL;
	struct vpu_iommu_info *iommu_info = session_info->iommu_info;
	struct vpu_iommu_ion_info *ion_info = iommu_info->private;
	unsigned int heap_id_mask;

	if (iommu_info->mmu_dev)
		heap_id_mask = ION_HEAP_TYPE_SYSTEM;
	else
		heap_id_mask = ION_HEAP_TYPE_DMA;

	ion_buffer = kzalloc(sizeof(*ion_buffer), GFP_KERNEL);
	if (!ion_buffer)
		return -ENOMEM;

	ion_buffer->handle = ion_alloc(ion_info->ion_client, size,
				       align, heap_id_mask, 0);

	INIT_LIST_HEAD(&ion_buffer->list);
	mutex_lock(&session_info->list_mutex);
	ion_buffer->index = session_info->max_idx;
	list_add_tail(&ion_buffer->list, &session_info->buffer_list);
	session_info->max_idx++;
	if ((session_info->max_idx & 0xfffffff) == 0)
		session_info->max_idx = 0;
	mutex_unlock(&session_info->list_mutex);

	return ion_buffer->index;
}

static int
vpu_ion_import(struct vpu_iommu_session_info *session_info, int fd)
{
	struct vpu_ion_buffer *ion_buffer = NULL;
	struct vpu_iommu_info *iommu_info = session_info->iommu_info;
	struct vpu_iommu_ion_info *ion_info = iommu_info->private;

	ion_buffer = kzalloc(sizeof(*ion_buffer), GFP_KERNEL);
	if (!ion_buffer)
		return -ENOMEM;

	ion_buffer->handle = ion_import_dma_buf(ion_info->ion_client, fd);

	INIT_LIST_HEAD(&ion_buffer->list);
	mutex_lock(&session_info->list_mutex);
	ion_buffer->index = session_info->max_idx;
	list_add_tail(&ion_buffer->list, &session_info->buffer_list);
	session_info->max_idx++;
	if ((session_info->max_idx & 0xfffffff) == 0)
		session_info->max_idx = 0;
	mutex_unlock(&session_info->list_mutex);

	return ion_buffer->index;
}

static int vpu_ion_create(struct vpu_iommu_info *iommu_info)
{
	struct vpu_iommu_ion_info *ion_info;

	iommu_info->private = kmalloc(sizeof(*ion_info), GFP_KERNEL);

	ion_info = iommu_info->private;
	if (!ion_info)
		return -ENOMEM;

	ion_info->ion_client = rockchip_ion_client_create("vpu");
	ion_info->attached = false;

	vpu_ion_attach(iommu_info);

	return IS_ERR(ion_info->ion_client) ? -1 : 0;
}

#else

static void
vpu_ion_clear_session(struct vpu_iommu_session_info *session_info)
{
	/* do nothing */
}

static int vpu_ion_attach(struct vpu_iommu_info *iommu_info)
{
	return 0;
}

static void vpu_ion_detach(struct vpu_iommu_info *iommu_info)
{
}

static int vpu_ion_destroy(struct vpu_iommu_info *iommu_info)
{
	return 0;
}

static int
vpu_ion_free(struct vpu_iommu_session_info *session_info, int idx)
{
	return 0;
}

static int
vpu_ion_unmap_iommu(struct vpu_iommu_session_info *session_info, int idx)
{
	return 0;
}

static int
vpu_ion_map_iommu(struct vpu_iommu_session_info *session_info, int idx,
		  unsigned long *iova, unsigned long *size)
{
	return 0;
}

static int
vpu_ion_unmap_kernel(struct vpu_iommu_session_info *session_info,
		     int idx)
{
	return 0;
}

static void*
vpu_ion_map_kernel(struct vpu_iommu_session_info *session_info, int idx)
{
	return NULL;
}

static int vpu_ion_alloc(struct vpu_iommu_session_info *session_info,
			 unsigned long size,
			 unsigned long align)
{
	return 0;
}

static int
vpu_ion_import(struct vpu_iommu_session_info *session_info, int fd)
{
	return 0;
}

static int vpu_ion_create(struct vpu_iommu_info *iommu_info)
{
	return -1;
}
#endif

static struct vpu_iommu_ops ion_ops = {
	.create = vpu_ion_create,
	.destroy = vpu_ion_destroy,
	.alloc = vpu_ion_alloc,
	.import = vpu_ion_import,
	.free = vpu_ion_free,
	.free_fd = NULL,
	.map_kernel = vpu_ion_map_kernel,
	.unmap_kernel = vpu_ion_unmap_kernel,
	.map_iommu = vpu_ion_map_iommu,
	.unmap_iommu = vpu_ion_unmap_iommu,
	.dump = NULL,
	.attach = vpu_ion_attach,
	.detach = vpu_ion_detach,
	.clear = vpu_ion_clear_session,
};

/*
 * we do not manage the ref number ourselves,
 * since ion will help us to do that. what we
 * need to do is just map/unmap and import/free
 * every time
 */
void vpu_iommu_ion_set_ops(struct vpu_iommu_info *iommu_info)
{
	if (!iommu_info)
		return;
	iommu_info->ops = &ion_ops;
}
