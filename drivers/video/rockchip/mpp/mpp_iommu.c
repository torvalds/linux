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
#ifdef CONFIG_ARM_DMA_USE_IOMMU
#include <asm/dma-iommu.h>
#endif
#include <linux/dma-buf.h>
#include <linux/dma-iommu.h>
#include <linux/iommu.h>
#include <linux/kref.h>
#include <linux/slab.h>

#include "mpp_iommu.h"

static struct mpp_dma_buffer *
mpp_dma_find_buffer_fd(struct mpp_dma_session *session, int fd)
{
	struct dma_buf *dmabuf;
	struct mpp_dma_buffer *out = NULL;
	struct mpp_dma_buffer *buffer = NULL, *n;

	dmabuf = dma_buf_get(fd);
	if (IS_ERR(dmabuf))
		return NULL;

	list_for_each_entry_safe(buffer, n,
				 &session->buffer_list, list) {
		/*
		 * As long as the last reference is hold by the buffer pool,
		 * the same fd won't be assigned to the other application.
		 */
		if (buffer->fd == fd &&
		    buffer->dmabuf == dmabuf) {
			out = buffer;
			break;
		}
	}
	dma_buf_put(dmabuf);

	return out;
}

/* Release the buffer from the current list */
static void mpp_dma_release_buffer(struct kref *ref)
{
	struct mpp_dma_buffer *buffer =
		container_of(ref, struct mpp_dma_buffer, ref);

	mutex_lock(&buffer->session->list_mutex);
	buffer->session->buffer_count--;
	list_del_init(&buffer->list);
	mutex_unlock(&buffer->session->list_mutex);

	dma_buf_unmap_attachment(buffer->attach, buffer->sgt, buffer->dir);
	dma_buf_detach(buffer->dmabuf, buffer->attach);
	dma_buf_put(buffer->dmabuf);
	kfree(buffer);
}

/* Remove the oldest buffer when count more than the setting */
static int
mpp_dma_remove_extra_buffer(struct mpp_dma_session *session)
{
	struct mpp_dma_buffer *n;
	struct mpp_dma_buffer *oldest = NULL, *buffer = NULL;
	ktime_t oldest_time = ktime_set(0, 0);

	if (session->buffer_count > session->max_buffers) {
		list_for_each_entry_safe(buffer, n,
					 &session->buffer_list,
					 list) {
			if (ktime_to_ns(oldest_time) == 0 ||
			    ktime_after(oldest_time, buffer->last_used)) {
				oldest_time = buffer->last_used;
				oldest = buffer;
			}
		}
		kref_put(&oldest->ref, mpp_dma_release_buffer);
	}

	return 0;
}

int mpp_dma_release(struct mpp_dma_session *session,
		    struct mpp_dma_buffer *buffer)
{
	if (IS_ERR_OR_NULL(buffer))
		return -EINVAL;

	kref_put(&buffer->ref, mpp_dma_release_buffer);

	return 0;
}

int mpp_dma_release_fd(struct mpp_dma_session *session, int fd)
{
	struct device *dev = session->dev;
	struct mpp_dma_buffer *buffer = NULL;

	buffer = mpp_dma_find_buffer_fd(session, fd);
	if (IS_ERR_OR_NULL(buffer)) {
		dev_err(dev, "can not find %d buffer in list\n", fd);

		return -EINVAL;
	}

	kref_put(&buffer->ref, mpp_dma_release_buffer);

	return 0;
}

struct mpp_dma_buffer *
mpp_dma_alloc(struct mpp_dma_session *session, size_t size)
{
	size_t align_size;
	dma_addr_t iova;
	struct  mpp_dma_buffer *buffer;

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer)
		return NULL;

	align_size = PAGE_ALIGN(size);
	buffer->vaddr = dma_alloc_coherent(session->dev,
					   align_size,
					   &iova,
					   GFP_KERNEL);
	if (!buffer->vaddr)
		goto fail_dma_alloc;

	buffer->size = PAGE_ALIGN(size);
	buffer->iova = iova;

	return buffer;
fail_dma_alloc:
	kfree(buffer);
	return NULL;
}

int mpp_dma_free(struct mpp_dma_session *session,
		 struct mpp_dma_buffer *buffer)
{
	dma_free_coherent(session->dev, buffer->size,
			  buffer->vaddr, buffer->iova);
	buffer->vaddr = NULL;
	buffer->iova = 0;
	buffer->size = 0;

	return 0;
}

struct mpp_dma_buffer *mpp_dma_import_fd(struct mpp_iommu_info *iommu_info,
					 struct mpp_dma_session *session,
					 int fd)
{
	int ret = 0;
	struct sg_table *sgt;
	struct dma_buf *dmabuf;
	struct mpp_dma_buffer *buffer;
	struct dma_buf_attachment *attach;

	if (!session)
		return ERR_PTR(-EINVAL);

	/* remove the oldest before add buffer */
	mpp_dma_remove_extra_buffer(session);

	dmabuf = dma_buf_get(fd);
	if (IS_ERR(dmabuf))
		return NULL;

	/* Check whether in session */
	buffer = mpp_dma_find_buffer_fd(session, fd);
	if (!IS_ERR_OR_NULL(buffer)) {
		if (buffer->dmabuf == dmabuf) {
			if (kref_get_unless_zero(&buffer->ref)) {
				buffer->last_used = ktime_get();
				dma_buf_put(dmabuf);
				return buffer;
			}
		}
		dev_dbg(session->dev, "missing the fd %d\n", fd);
		kref_put(&buffer->ref, mpp_dma_release_buffer);
	}

	/* A new DMA buffer */
	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer) {
		ret = -ENOMEM;
		goto fail;
	}

	buffer->dmabuf = dmabuf;
	buffer->fd = fd;
	buffer->dir = DMA_BIDIRECTIONAL;
	buffer->last_used = ktime_get();

	attach = dma_buf_attach(buffer->dmabuf, session->dev);
	if (IS_ERR(attach)) {
		ret = PTR_ERR(attach);
		goto fail_attach;
	}

	sgt = dma_buf_map_attachment(attach, buffer->dir);
	if (IS_ERR(sgt)) {
		ret = PTR_ERR(sgt);
		goto fail_map;
	}
	buffer->iova = sg_dma_address(sgt->sgl);
	buffer->size = sg_dma_len(sgt->sgl);
	buffer->attach = attach;
	buffer->sgt = sgt;
	buffer->session = session;

	kref_init(&buffer->ref);
	/* Increase the reference for used outside the buffer pool */
	kref_get(&buffer->ref);
	INIT_LIST_HEAD(&buffer->list);

	mutex_lock(&session->list_mutex);
	session->buffer_count++;
	list_add_tail(&buffer->list, &session->buffer_list);
	mutex_unlock(&session->list_mutex);

	return buffer;

fail_map:
	dma_buf_detach(buffer->dmabuf, attach);
fail_attach:
	kfree(buffer);
fail:
	dma_buf_put(dmabuf);
	return ERR_PTR(ret);
}

int mpp_dma_unmap_kernel(struct mpp_dma_session *session,
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

int mpp_dma_map_kernel(struct mpp_dma_session *session,
		       struct mpp_dma_buffer *buffer)
{
	int ret;
	void *vaddr;
	struct dma_buf *dmabuf = buffer->dmabuf;

	if (IS_ERR_OR_NULL(dmabuf))
		return -EINVAL;

	ret = dma_buf_begin_cpu_access(dmabuf, DMA_FROM_DEVICE);
	if (ret) {
		dev_dbg(session->dev, "can't access the dma buffer\n");
		goto failed_access;
	}

	vaddr = dma_buf_vmap(dmabuf);
	if (!vaddr) {
		dev_dbg(session->dev, "can't vmap the dma buffer\n");
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

int mpp_dma_session_destroy(struct mpp_dma_session *session)
{
	struct mpp_dma_buffer *n, *buffer = NULL;

	if (!session)
		return -EINVAL;

	list_for_each_entry_safe(buffer, n,
				 &session->buffer_list,
				 list) {
		kref_put(&buffer->ref, mpp_dma_release_buffer);
	}

	kfree(session);

	return 0;
}

struct mpp_dma_session *
mpp_dma_session_create(struct device *dev)
{
	struct mpp_dma_session *session = NULL;

	session = kzalloc(sizeof(*session), GFP_KERNEL);
	if (!session)
		return session;

	INIT_LIST_HEAD(&session->buffer_list);
	mutex_init(&session->list_mutex);

	session->dev = dev;

	return session;
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
	struct mpp_iommu_info *info = NULL;
#ifdef CONFIG_ARM_DMA_USE_IOMMU
	struct dma_iommu_mapping *mapping;
#endif

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		ret = -ENOMEM;
		goto err;
	}
	info->dev = dev;

	info->group = iommu_group_get(dev);
	if (!info->group) {
		ret = -EINVAL;
		goto err_free_info;
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

	return info;

err_put_group:
	iommu_group_put(info->group);
err_free_info:
	kfree(info);
err:
	return ERR_PTR(ret);
}

int mpp_iommu_remove(struct mpp_iommu_info *info)
{
#ifdef CONFIG_ARM_DMA_USE_IOMMU
	struct dma_iommu_mapping *mapping;

	mapping = to_dma_iommu_mapping(info->dev);
	arm_iommu_release_mapping(mapping);
#endif
	iommu_group_put(info->group);
	kfree(info);

	return 0;
}
