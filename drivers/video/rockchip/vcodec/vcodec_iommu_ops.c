/**
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

#include <linux/slab.h>

#include "vcodec_iommu_ops.h"

static
struct vcodec_iommu_session_info *vcodec_iommu_get_session_info
	(struct vcodec_iommu_info *iommu_info, struct vpu_session *session)
{
	struct vcodec_iommu_session_info *session_info = NULL, *n;

	list_for_each_entry_safe(session_info, n, &iommu_info->session_list,
				 head) {
		if (session_info->session == session)
			return session_info;
	}

	return NULL;
}

int vcodec_iommu_create(struct vcodec_iommu_info *iommu_info)
{
	if (!iommu_info || !iommu_info->ops->create)
		return -EINVAL;

	return iommu_info->ops->create(iommu_info);
}

int vcodec_iommu_import(struct vcodec_iommu_info *iommu_info,
			struct vpu_session *session, int fd)
{
	struct vcodec_iommu_session_info *session_info = NULL;

	if (!iommu_info || !iommu_info->ops->import || !session)
		return -EINVAL;

	session_info = vcodec_iommu_get_session_info(iommu_info, session);
	if (!session_info) {
		session_info = kzalloc(sizeof(*session_info), GFP_KERNEL);
		if (!session_info)
			return -ENOMEM;

		INIT_LIST_HEAD(&session_info->head);
		INIT_LIST_HEAD(&session_info->buffer_list);
		mutex_init(&session_info->list_mutex);
		session_info->max_idx = 0;
		session_info->session = session;
		session_info->mmu_dev = iommu_info->mmu_dev;
		session_info->dev = iommu_info->dev;
		session_info->iommu_info = iommu_info;
		session_info->buffer_nums = 0;
		mutex_lock(&iommu_info->list_mutex);
		list_add_tail(&session_info->head, &iommu_info->session_list);
		mutex_unlock(&iommu_info->list_mutex);
	}

	session_info->debug_level = iommu_info->debug_level;

	return iommu_info->ops->import(session_info, fd);
}

int vcodec_iommu_free(struct vcodec_iommu_info *iommu_info,
		      struct vpu_session *session, int idx)
{
	struct vcodec_iommu_session_info *session_info = NULL;

	session_info = vcodec_iommu_get_session_info(iommu_info, session);

	if (!iommu_info || !iommu_info->ops->free || !session_info)
		return -EINVAL;

	return iommu_info->ops->free(session_info, idx);
}

int vcodec_iommu_free_fd(struct vcodec_iommu_info *iommu_info,
			 struct vpu_session *session, int fd)
{
	struct vcodec_iommu_session_info *session_info = NULL;

	session_info = vcodec_iommu_get_session_info(iommu_info, session);

	if (!iommu_info || !iommu_info->ops->free_fd || !session_info)
		return -EINVAL;

	return iommu_info->ops->free_fd(session_info, fd);
}

int vcodec_iommu_map_iommu(struct vcodec_iommu_info *iommu_info,
			   struct vpu_session *session,
			   int idx, dma_addr_t *iova,
			   unsigned long *size)
{
	struct vcodec_iommu_session_info *session_info = NULL;

	session_info = vcodec_iommu_get_session_info(iommu_info, session);

	if (!iommu_info || !iommu_info->ops->map_iommu || !session_info)
		return -EINVAL;

	return iommu_info->ops->map_iommu(session_info, idx, iova, size);
}

int vcodec_iommu_unmap_iommu(struct vcodec_iommu_info *iommu_info,
			     struct vpu_session *session, int idx)
{
	struct vcodec_iommu_session_info *session_info = NULL;

	session_info = vcodec_iommu_get_session_info(iommu_info, session);

	if (!iommu_info || !iommu_info->ops->unmap_iommu || !session_info)
		return -EINVAL;

	return iommu_info->ops->unmap_iommu(session_info, idx);
}

static int vcodec_iommu_destroy(struct vcodec_iommu_info *iommu_info)
{
	if (!iommu_info || !iommu_info->ops->destroy)
		return -EINVAL;

	return iommu_info->ops->destroy(iommu_info);
}

void vcodec_iommu_dump(struct vcodec_iommu_info *iommu_info,
		       struct vpu_session *session)
{
	struct vcodec_iommu_session_info *session_info = NULL;

	session_info = vcodec_iommu_get_session_info(iommu_info, session);

	if (!iommu_info || !iommu_info->ops->dump || !session_info)
		return;

	iommu_info->ops->dump(session_info);
}

void vcodec_iommu_clear(struct vcodec_iommu_info *iommu_info,
			struct vpu_session *session)
{
	struct vcodec_iommu_session_info *session_info = NULL;

	session_info = vcodec_iommu_get_session_info(iommu_info, session);

	if (!iommu_info || !iommu_info->ops->clear || !session_info)
		return;

	iommu_info->ops->clear(session_info);

	mutex_lock(&iommu_info->list_mutex);
	list_del_init(&session_info->head);
	kfree(session_info);
	mutex_unlock(&iommu_info->list_mutex);
}

int vcodec_iommu_attach(struct vcodec_iommu_info *iommu_info)
{
	if (!iommu_info || !iommu_info->ops->attach)
		return 0;

	return iommu_info->ops->attach(iommu_info);
}

void vcodec_iommu_detach(struct vcodec_iommu_info *iommu_info)
{
	if (!iommu_info || !iommu_info->ops->detach)
		return;

	return iommu_info->ops->detach(iommu_info);
}

struct vcodec_iommu_info *
vcodec_iommu_info_create(struct device *dev,
			 struct device *mmu_dev,
			 int alloc_type)
{
	struct vcodec_iommu_info *iommu_info = NULL;

	iommu_info = kzalloc(sizeof(*iommu_info), GFP_KERNEL);
	if (!iommu_info)
		return NULL;

	iommu_info->dev = dev;
	INIT_LIST_HEAD(&iommu_info->session_list);
	mutex_init(&iommu_info->list_mutex);
	mutex_init(&iommu_info->iommu_mutex);
	switch (alloc_type) {
#ifdef CONFIG_DRM
	case ALLOCATOR_USE_DRM:
		vcodec_iommu_drm_set_ops(iommu_info);
		break;
#endif
#ifdef CONFIG_ION
	case ALLOCATOR_USE_ION:
		vcodec_iommu_ion_set_ops(iommu_info);
		break;
#endif
	default:
		iommu_info->ops = NULL;
		break;
	}

	iommu_info->mmu_dev = mmu_dev;

	vcodec_iommu_create(iommu_info);

	return iommu_info;
}

int vcodec_iommu_info_destroy(struct vcodec_iommu_info *iommu_info)
{
	vcodec_iommu_destroy(iommu_info);
	kfree(iommu_info);

	return 0;
}
