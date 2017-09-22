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

#include <linux/fence.h>
#include <linux/kref.h>
#include <linux/memblock.h>
#include <linux/module.h>
#include <linux/rockchip_ion.h>
#include <linux/rockchip-iovmm.h>
#include <linux/slab.h>

#include "vcodec_iommu_ops.h"

struct vcodec_ion_buffer {
	struct list_head list;
	struct ion_handle *handle;
	int index;
};

struct vcodec_iommu_ion_info {
	struct ion_client *ion_client;
	bool attached;
};

static struct vcodec_ion_buffer *
vcodec_ion_get_buffer_no_lock(struct vcodec_iommu_session_info *session_info,
			      int idx)
{
	struct vcodec_ion_buffer *ion_buffer = NULL, *n;

	list_for_each_entry_safe(ion_buffer, n,
				 &session_info->buffer_list, list) {
		if (ion_buffer->index == idx)
			return ion_buffer;
	}

	return NULL;
}

static void
vcodec_ion_clear_session(struct vcodec_iommu_session_info *session_info)
{
	/* do nothing */
}

static int vcodec_ion_attach(struct vcodec_iommu_info *iommu_info)
{
	struct vcodec_iommu_ion_info *ion_info = iommu_info->private;
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

static void vcodec_ion_detach(struct vcodec_iommu_info *iommu_info)
{
	struct vcodec_iommu_ion_info *ion_info = iommu_info->private;

	mutex_lock(&iommu_info->iommu_mutex);

	if (!ion_info->attached) {
		mutex_unlock(&iommu_info->iommu_mutex);
		return;
	}

	rockchip_iovmm_deactivate(iommu_info->dev);
	ion_info->attached = false;

	mutex_unlock(&iommu_info->iommu_mutex);
}

static int vcodec_ion_destroy(struct vcodec_iommu_info *iommu_info)
{
	struct vcodec_iommu_ion_info *ion_info = iommu_info->private;

	vcodec_ion_detach(iommu_info);
	kfree(ion_info);
	iommu_info->private = NULL;

	return 0;
}

static int
vcodec_ion_free(struct vcodec_iommu_session_info *session_info, int idx)
{
	struct vcodec_ion_buffer *ion_buffer;

	mutex_lock(&session_info->list_mutex);
	ion_buffer = vcodec_ion_get_buffer_no_lock(session_info, idx);

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
vcodec_ion_unmap_iommu(struct vcodec_iommu_session_info *session_info, int idx)
{
	struct vcodec_ion_buffer *ion_buffer;
	struct vcodec_iommu_info *iommu_info = session_info->iommu_info;
	struct vcodec_iommu_ion_info *ion_info = iommu_info->private;

	mutex_lock(&session_info->list_mutex);
	ion_buffer = vcodec_ion_get_buffer_no_lock(session_info, idx);
	mutex_unlock(&session_info->list_mutex);

	if (!ion_buffer) {
		pr_err("%s can not find %d buffer in list\n", __func__, idx);

		return -EINVAL;
	}

	ion_free(ion_info->ion_client, ion_buffer->handle);

	return 0;
}

static int
vcodec_ion_map_iommu(struct vcodec_iommu_session_info *session_info, int idx,
		     dma_addr_t *iova, unsigned long *size)
{
	struct vcodec_ion_buffer *ion_buffer;
	struct device *dev = session_info->dev;
	struct vcodec_iommu_info *iommu_info = session_info->iommu_info;
	struct vcodec_iommu_ion_info *ion_info = iommu_info->private;
	int ret = 0;

	/* Force to flush iommu table */
	rockchip_iovmm_invalidate_tlb(session_info->dev);

	mutex_lock(&session_info->list_mutex);
	ion_buffer = vcodec_ion_get_buffer_no_lock(session_info, idx);
	mutex_unlock(&session_info->list_mutex);

	if (!ion_buffer) {
		pr_err("%s can not find %d buffer in list\n", __func__, idx);

		return -EINVAL;
	}

	if (session_info->mmu_dev)
		ret = ion_map_iommu(dev, ion_info->ion_client,
				    ion_buffer->handle, (unsigned long *)iova,
				    size);
	else
		ret = ion_phys(ion_info->ion_client, ion_buffer->handle,
			       (ion_phys_addr_t *)iova, (size_t *)size);

	return ret;
}

static int
vcodec_ion_import(struct vcodec_iommu_session_info *session_info, int fd)
{
	struct vcodec_ion_buffer *ion_buffer = NULL;
	struct vcodec_iommu_info *iommu_info = session_info->iommu_info;
	struct vcodec_iommu_ion_info *ion_info = iommu_info->private;

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

static int vcodec_ion_create(struct vcodec_iommu_info *iommu_info)
{
	struct vcodec_iommu_ion_info *ion_info;

	iommu_info->private = kmalloc(sizeof(*ion_info), GFP_KERNEL);

	ion_info = iommu_info->private;
	if (!ion_info)
		return -ENOMEM;

	ion_info->ion_client = rockchip_ion_client_create("vpu");
	ion_info->attached = false;

	vcodec_ion_attach(iommu_info);

	return IS_ERR(ion_info->ion_client) ? -1 : 0;
}

static struct vcodec_iommu_ops ion_ops = {
	.create = vcodec_ion_create,
	.destroy = vcodec_ion_destroy,
	.import = vcodec_ion_import,
	.free = vcodec_ion_free,
	.free_fd = NULL,
	.map_iommu = vcodec_ion_map_iommu,
	.unmap_iommu = vcodec_ion_unmap_iommu,
	.dump = NULL,
	.attach = vcodec_ion_attach,
	.detach = vcodec_ion_detach,
	.clear = vcodec_ion_clear_session,
};

/*
 * we do not manage the ref number ourselves,
 * since ion will help us to do that. what we
 * need to do is just map/unmap and import/free
 * every time
 */
void vcodec_iommu_ion_set_ops(struct vcodec_iommu_info *iommu_info)
{
	if (!iommu_info)
		return;
	iommu_info->ops = &ion_ops;
}
