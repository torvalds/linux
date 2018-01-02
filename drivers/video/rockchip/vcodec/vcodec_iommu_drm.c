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

#include <linux/dma-iommu.h>
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/fence.h>
#include <linux/iova.h>
#include <linux/kref.h>
#include <linux/ktime.h>
#include <linux/memblock.h>

#include "vcodec_iommu_ops.h"

/*
 * there two type drm buffer, one is through import interface and
 * the other is through alloc. there are some differents between
 * import and alloc buffer when free them. this flag is used to
 * inform driver which type of this buffer.
 */
#define VCODEC_DRM_BUFFER_ALLOC	0x00000001

struct vcodec_drm_buffer {
	struct list_head list;
	struct dma_buf *dma_buf;
	union {
		dma_addr_t iova;
		unsigned long phys;
	};
	/* this is used for page fault */
	unsigned long iova_out;
	void *cpu_addr;
	unsigned long size;
	int index;
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	struct sg_table *copy_sgt;
	struct page **pages;
	struct kref ref;
	struct vcodec_iommu_session_info *session_info;
	ktime_t last_used;
	int flags;
};

struct vcodec_iommu_drm_info {
	struct iommu_domain *domain;
	bool attached;
};

static struct vcodec_drm_buffer *
vcodec_drm_get_buffer_no_lock(struct vcodec_iommu_session_info *session_info,
			      int idx)
{
	struct vcodec_drm_buffer *drm_buffer = NULL, *n;

	list_for_each_entry_safe(drm_buffer, n, &session_info->buffer_list,
				 list) {
		if (drm_buffer->index == idx) {
			drm_buffer->last_used = ktime_get();
			return drm_buffer;
		}
	}

	return NULL;
}

static struct vcodec_drm_buffer *
vcodec_drm_get_buffer_fd_no_lock(struct vcodec_iommu_session_info *session_info,
				 int fd)
{
	struct vcodec_drm_buffer *drm_buffer = NULL, *n;
	struct dma_buf *dma_buf = NULL;

	dma_buf = dma_buf_get(fd);

	list_for_each_entry_safe(drm_buffer, n, &session_info->buffer_list,
				 list) {
		if (drm_buffer->dma_buf == dma_buf) {
			drm_buffer->last_used = ktime_get();
			dma_buf_put(dma_buf);
			return drm_buffer;
		}
	}

	dma_buf_put(dma_buf);

	return NULL;
}

static void *vcodec_drm_sgt_map_kernel(struct vcodec_drm_buffer *drm_buffer)
{
	struct vcodec_iommu_session_info *session_info =
						      drm_buffer->session_info;
	struct device *dev = session_info->dev;
	struct scatterlist *sgl, *sg;
	int nr_pages = PAGE_ALIGN(drm_buffer->size) >> PAGE_SHIFT;
	int i = 0, j = 0, k = 0;
	struct page *page;

	drm_buffer->pages = kmalloc_array(nr_pages, sizeof(*drm_buffer->pages),
					  GFP_KERNEL);
	if (!(drm_buffer->pages)) {
		dev_err(dev, "drm map alloc %d pages failed!\n", nr_pages);

		return NULL;
	}

	sgl = drm_buffer->copy_sgt->sgl;

	for_each_sg(sgl, sg, drm_buffer->copy_sgt->nents, i) {
		page = sg_page(sg);
		for (j = 0; j < sg->length / PAGE_SIZE; j++)
			drm_buffer->pages[k++] = page++;
	}

	return vmap(drm_buffer->pages, nr_pages, VM_MAP,
		    pgprot_noncached(PAGE_KERNEL));
}

static void vcodec_drm_sgt_unmap_kernel(struct vcodec_drm_buffer *drm_buffer)
{
	vunmap(drm_buffer->cpu_addr);
	kfree(drm_buffer->pages);
}

static int vcodec_finalise_sg(struct scatterlist *sg,
			      int nents,
			      dma_addr_t dma_addr)
{
	struct scatterlist *s, *cur = sg;
	unsigned long seg_mask = DMA_BIT_MASK(32);
	unsigned int cur_len = 0, max_len = DMA_BIT_MASK(32);
	int i, count = 0;

	for_each_sg(sg, s, nents, i) {
		/* Restore this segment's original unaligned fields first */
		unsigned int s_iova_off = sg_dma_address(s);
		unsigned int s_length = sg_dma_len(s);
		unsigned int s_iova_len = s->length;

		s->offset += s_iova_off;
		s->length = s_length;
		sg_dma_address(s) = DMA_ERROR_CODE;
		sg_dma_len(s) = 0;

		/*
		 * Now fill in the real DMA data. If...
		 * - there is a valid output segment to append to
		 * - and this segment starts on an IOVA page boundary
		 * - but doesn't fall at a segment boundary
		 * - and wouldn't make the resulting output segment too long
		 */
		if (cur_len && !s_iova_off && (dma_addr & seg_mask) &&
		    (cur_len + s_length <= max_len)) {
			/* ...then concatenate it with the previous one */
			cur_len += s_length;
		} else {
			/* Otherwise start the next output segment */
			if (i > 0)
				cur = sg_next(cur);
			cur_len = s_length;
			count++;

			sg_dma_address(cur) = dma_addr + s_iova_off;
		}

		sg_dma_len(cur) = cur_len;
		dma_addr += s_iova_len;

		if (s_length + s_iova_off < s_iova_len)
			cur_len = 0;
	}
	return count;
}

static void vcodec_invalidate_sg(struct scatterlist *sg, int nents)
{
	struct scatterlist *s;
	int i;

	for_each_sg(sg, s, nents, i) {
		if (sg_dma_address(s) != DMA_ERROR_CODE)
			s->offset += sg_dma_address(s);
		if (sg_dma_len(s))
			s->length = sg_dma_len(s);
		sg_dma_address(s) = DMA_ERROR_CODE;
		sg_dma_len(s) = 0;
	}
}

static void vcodec_dma_unmap_sg(struct iommu_domain *domain,
				dma_addr_t dma_addr)
{
	struct iova_domain *iovad = domain->iova_cookie;
	unsigned long shift = iova_shift(iovad);
	unsigned long pfn = dma_addr >> shift;
	struct iova *iova = find_iova(iovad, pfn);
	size_t size;

	if (WARN_ON(!iova))
		return;

	size = iova_size(iova) << shift;
	size -= iommu_unmap(domain, pfn << shift, size);
	/* ...and if we can't, then something is horribly, horribly wrong */
	WARN_ON(size > 0);
	__free_iova(iovad, iova);
}

static void vcodec_drm_clear_map(struct kref *ref)
{
	struct vcodec_drm_buffer *drm_buffer =
		container_of(ref, struct vcodec_drm_buffer, ref);
	struct vcodec_iommu_session_info *session_info =
		drm_buffer->session_info;
	struct vcodec_iommu_info *iommu_info = session_info->iommu_info;
	struct vcodec_iommu_drm_info *drm_info = iommu_info->private;
					      struct sg_table *table;
					      struct scatterlist *sg;
					      struct page *page;
					      int i;

	mutex_lock(&iommu_info->iommu_mutex);
	drm_info = session_info->iommu_info->private;

	if (drm_buffer->cpu_addr) {
		vcodec_drm_sgt_unmap_kernel(drm_buffer);
		drm_buffer->cpu_addr = NULL;
	}

	vcodec_dma_unmap_sg(drm_info->domain, drm_buffer->iova);
	if (drm_buffer->flags & VCODEC_DRM_BUFFER_ALLOC) {
		table = drm_buffer->copy_sgt;
		for_each_sg(table->sgl, sg, table->nents, i) {
			page = sg_page(sg);
			__free_pages(page, compound_order(page));
		}
	}
	sg_free_table(drm_buffer->copy_sgt);
	kfree(drm_buffer->copy_sgt);

	if (drm_buffer->attach) {
		dma_buf_unmap_attachment(drm_buffer->attach, drm_buffer->sgt,
					 DMA_BIDIRECTIONAL);
		dma_buf_detach(drm_buffer->dma_buf, drm_buffer->attach);
		dma_buf_put(drm_buffer->dma_buf);
		drm_buffer->attach = NULL;
	}

	mutex_unlock(&iommu_info->iommu_mutex);
}

static int
vcodec_drm_unmap_iommu_with_iova(struct vcodec_iommu_session_info *session_info,
				 int idx)
{
	struct device *dev = session_info->dev;
	struct vcodec_drm_buffer *drm_buffer;
	struct vcodec_iommu_info *iommu_info = session_info->iommu_info;
	struct vcodec_iommu_drm_info *drm_info = iommu_info->private;
	struct iommu_domain *domain = drm_info->domain;

	mutex_lock(&session_info->list_mutex);
	drm_buffer = vcodec_drm_get_buffer_no_lock(session_info, idx);
	mutex_unlock(&session_info->list_mutex);

	if (!drm_buffer) {
		dev_err(dev, "can not find %d buffer in list\n", idx);
		return -EINVAL;
	}

	iommu_unmap(domain, drm_buffer->iova_out, PAGE_SIZE);

	return 0;
}

static int
vcodec_drm_map_iommu_with_iova(struct vcodec_iommu_session_info *session_info,
			       int idx,
			       unsigned long iova,
			       unsigned long size)
{
	struct device *dev = session_info->dev;
	struct vcodec_drm_buffer *drm_buffer;
	struct vcodec_iommu_info *iommu_info = session_info->iommu_info;
	struct vcodec_iommu_drm_info *drm_info = iommu_info->private;

	mutex_lock(&session_info->list_mutex);
	drm_buffer = vcodec_drm_get_buffer_no_lock(session_info, idx);
	mutex_unlock(&session_info->list_mutex);

	if (!drm_buffer) {
		dev_err(dev, "can not find %d buffer in list\n", idx);
		return -EINVAL;
	}

	drm_buffer->iova_out = iova;

	return iommu_map(drm_info->domain,
			 iova,
			 sg_phys(drm_buffer->copy_sgt->sgl),
			 size, IOMMU_READ | IOMMU_WRITE);
}

static void*
vcodec_drm_map_kernel(struct vcodec_iommu_session_info *session_info, int idx)
{
	struct device *dev = session_info->dev;
	struct vcodec_drm_buffer *drm_buffer;

	mutex_lock(&session_info->list_mutex);
	drm_buffer = vcodec_drm_get_buffer_no_lock(session_info, idx);
	mutex_unlock(&session_info->list_mutex);

	if (!drm_buffer) {
		dev_err(dev, "can not find %d buffer in list\n", idx);
		return NULL;
	}

	if (!drm_buffer->cpu_addr)
		drm_buffer->cpu_addr =
				       vcodec_drm_sgt_map_kernel(drm_buffer);

	kref_get(&drm_buffer->ref);

	return drm_buffer->cpu_addr;
}

static int
vcodec_drm_unmap_kernel(struct vcodec_iommu_session_info *session_info, int idx)
{
	struct device *dev = session_info->dev;
	struct vcodec_drm_buffer *drm_buffer;

	mutex_lock(&session_info->list_mutex);
	drm_buffer = vcodec_drm_get_buffer_no_lock(session_info, idx);
	mutex_unlock(&session_info->list_mutex);

	if (!drm_buffer) {
		dev_err(dev, "can not find %d buffer in list\n", idx);

		return -EINVAL;
	}

	if (drm_buffer->cpu_addr) {
		vcodec_drm_sgt_unmap_kernel(drm_buffer);
		drm_buffer->cpu_addr = NULL;
	}

	kref_put(&drm_buffer->ref, vcodec_drm_clear_map);
	return 0;
}

static void
vcodec_drm_remove_extra_buffer_no_lock(struct vcodec_iommu_session_info
				       *session_info)
{
	struct vcodec_drm_buffer *oldest_buffer = NULL, *loop_buffer = NULL;
	struct vcodec_drm_buffer *n;
	ktime_t oldest_time = ktime_set(0, 0);

	if (session_info->buffer_nums > BUFFER_LIST_MAX_NUMS) {
		list_for_each_entry_safe(loop_buffer, n,
					 &session_info->buffer_list, list) {
			if (loop_buffer->flags & VCODEC_DRM_BUFFER_ALLOC)
				continue;

			if (ktime_to_ns(oldest_time) == 0 ||
			    ktime_after(oldest_time,
					loop_buffer->last_used)) {
				oldest_time = loop_buffer->last_used;
				oldest_buffer = loop_buffer;
			}
		}
		kref_put(&oldest_buffer->ref, vcodec_drm_clear_map);
		dma_buf_put(oldest_buffer->dma_buf);
		list_del_init(&oldest_buffer->list);
		kfree(oldest_buffer);
		session_info->buffer_nums--;
	}
}

static void vcodec_drm_detach(struct vcodec_iommu_info *iommu_info)
{
	struct vcodec_iommu_drm_info *drm_info = iommu_info->private;
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

static int vcodec_drm_attach_unlock(struct vcodec_iommu_info *iommu_info)
{
	struct vcodec_iommu_drm_info *drm_info = iommu_info->private;
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

static int vcodec_drm_attach(struct vcodec_iommu_info *iommu_info)
{
	struct vcodec_iommu_drm_info *drm_info = iommu_info->private;
	int ret;

	mutex_lock(&iommu_info->iommu_mutex);

	if (drm_info->attached) {
		mutex_unlock(&iommu_info->iommu_mutex);
		return 0;
	}

	ret = vcodec_drm_attach_unlock(iommu_info);
	if (ret) {
		mutex_unlock(&iommu_info->iommu_mutex);
		return ret;
	}

	drm_info->attached = true;

	mutex_unlock(&iommu_info->iommu_mutex);

	return ret;
}

static dma_addr_t vcodec_dma_map_sg(struct iommu_domain *domain,
				    struct scatterlist *sg,
				    int nents, int prot)
{
	struct iova_domain *iovad = domain->iova_cookie;
	struct iova *iova;
	struct scatterlist *s, *prev = NULL;
	dma_addr_t dma_addr;
	size_t iova_len = 0;
	unsigned long mask = DMA_BIT_MASK(32);
	unsigned long shift = iova_shift(iovad);
	int i;

	/*
	 * Work out how much IOVA space we need, and align the segments to
	 * IOVA granules for the IOMMU driver to handle. With some clever
	 * trickery we can modify the list in-place, but reversibly, by
	 * stashing the unaligned parts in the as-yet-unused DMA fields.
	 */
	for_each_sg(sg, s, nents, i) {
		size_t s_iova_off = iova_offset(iovad, s->offset);
		size_t s_length = s->length;
		size_t pad_len = (mask - iova_len + 1) & mask;

		sg_dma_address(s) = s_iova_off;
		sg_dma_len(s) = s_length;
		s->offset -= s_iova_off;
		s_length = iova_align(iovad, s_length + s_iova_off);
		s->length = s_length;

		/*
		 * Due to the alignment of our single IOVA allocation, we can
		 * depend on these assumptions about the segment boundary mask:
		 * - If mask size >= IOVA size, then the IOVA range cannot
		 *   possibly fall across a boundary, so we don't care.
		 * - If mask size < IOVA size, then the IOVA range must start
		 *   exactly on a boundary, therefore we can lay things out
		 *   based purely on segment lengths without needing to know
		 *   the actual addresses beforehand.
		 * - The mask must be a power of 2, so pad_len == 0 if
		 *   iova_len == 0, thus we cannot dereference prev the first
		 *   time through here (i.e. before it has a meaningful value).
		 */
		if (pad_len && pad_len < s_length - 1) {
			prev->length += pad_len;
			iova_len += pad_len;
		}

		iova_len += s_length;
		prev = s;
	}

	iova = alloc_iova(iovad, iova_align(iovad, iova_len) >> shift,
			  mask >> shift, true);
	if (!iova)
		goto out_restore_sg;

	/*
	 * We'll leave any physical concatenation to the IOMMU driver's
	 * implementation - it knows better than we do.
	 */
	dma_addr = iova_dma_addr(iovad, iova);
	if (iommu_map_sg(domain, dma_addr, sg, nents, prot) < iova_len)
		goto out_free_iova;

	return vcodec_finalise_sg(sg, nents, dma_addr);

out_free_iova:
	__free_iova(iovad, iova);
out_restore_sg:
	vcodec_invalidate_sg(sg, nents);
	return 0;
}

static void vcdoec_drm_dump_info(struct vcodec_iommu_session_info *session_info)
{
	struct vcodec_drm_buffer *drm_buffer = NULL, *n;

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

static int vcodec_drm_free(struct vcodec_iommu_session_info *session_info,
			   int idx)
{
	struct device *dev = session_info->dev;
	/* please double-check all maps have been release */
	struct vcodec_drm_buffer *drm_buffer;

	mutex_lock(&session_info->list_mutex);
	drm_buffer = vcodec_drm_get_buffer_no_lock(session_info, idx);

	if (!drm_buffer) {
		dev_err(dev, "can not find %d buffer in list\n", idx);
		mutex_unlock(&session_info->list_mutex);

		return -EINVAL;
	}

	if (atomic_read(&drm_buffer->ref.refcount) == 0) {
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
vcodec_drm_unmap_iommu(struct vcodec_iommu_session_info *session_info,
		       int idx)
{
	struct device *dev = session_info->dev;
	struct vcodec_drm_buffer *drm_buffer;

	mutex_lock(&session_info->list_mutex);
	drm_buffer = vcodec_drm_get_buffer_no_lock(session_info, idx);
	mutex_unlock(&session_info->list_mutex);

	if (!drm_buffer) {
		dev_err(dev, "can not find %d buffer in list\n", idx);
		return -EINVAL;
	}

	kref_put(&drm_buffer->ref, vcodec_drm_clear_map);

	return 0;
}

static int vcodec_drm_map_iommu(struct vcodec_iommu_session_info *session_info,
				int idx,
				dma_addr_t *iova,
				unsigned long *size)
{
	struct device *dev = session_info->dev;
	struct vcodec_drm_buffer *drm_buffer;

	mutex_lock(&session_info->list_mutex);
	drm_buffer = vcodec_drm_get_buffer_no_lock(session_info, idx);
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
vcodec_drm_free_fd(struct vcodec_iommu_session_info *session_info, int fd)
{
	struct device *dev = session_info->dev;
	/* please double-check all maps have been release */
	struct vcodec_drm_buffer *drm_buffer = NULL;

	mutex_lock(&session_info->list_mutex);
	drm_buffer = vcodec_drm_get_buffer_fd_no_lock(session_info, fd);

	if (!drm_buffer) {
		dev_err(dev, "can not find %d buffer in list\n", fd);
		mutex_unlock(&session_info->list_mutex);

		return -EINVAL;
	}
	mutex_unlock(&session_info->list_mutex);

	vcodec_drm_unmap_iommu(session_info, drm_buffer->index);

	mutex_lock(&session_info->list_mutex);
	if (atomic_read(&drm_buffer->ref.refcount) == 0) {
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
vcodec_drm_clear_session(struct vcodec_iommu_session_info *session_info)
{
	struct vcodec_drm_buffer *drm_buffer = NULL, *n;

	list_for_each_entry_safe(drm_buffer, n, &session_info->buffer_list,
				 list) {
		kref_put(&drm_buffer->ref, vcodec_drm_clear_map);
		vcodec_drm_free(session_info, drm_buffer->index);
	}
}

static int vcodec_drm_import(struct vcodec_iommu_session_info *session_info,
			     int fd)
{
	struct vcodec_drm_buffer *drm_buffer = NULL, *n;
	struct vcodec_iommu_info *iommu_info = session_info->iommu_info;
	struct vcodec_iommu_drm_info *drm_info = iommu_info->private;
	struct device *dev = session_info->dev;
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	struct dma_buf *dma_buf;
	struct scatterlist *sg, *s;
	int i;
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
			drm_buffer->last_used = ktime_get();
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
	drm_buffer->last_used = ktime_get();

	kref_init(&drm_buffer->ref);

	mutex_lock(&iommu_info->iommu_mutex);
	drm_info = session_info->iommu_info->private;

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

	/*
	 * Since we call dma_buf_map_attachment outside attach/detach, this
	 * will cause incorrectly map. we have to re-build map table native
	 * and for avoiding destroy their origin map table, we need use a
	 * copy one sg_table.
	 */
	drm_buffer->copy_sgt = kmalloc(sizeof(*drm_buffer->copy_sgt),
				       GFP_KERNEL);
	if (!drm_buffer->copy_sgt) {
		ret = -ENOMEM;
		goto fail_detach;
	}

	ret = sg_alloc_table(drm_buffer->copy_sgt, sgt->nents, GFP_KERNEL);
	s = drm_buffer->copy_sgt->sgl;
	for_each_sg(sgt->sgl, sg, sgt->nents, i) {
		sg_set_page(s, sg_page(sg),
			    PAGE_SIZE << compound_order(sg_page(sg)), 0);
		sg_dma_address(s) = page_to_phys(sg_page(sg));
		s->offset = sg->offset;
		s->length = sg->length;
		s = sg_next(s);
	}

	ret = vcodec_dma_map_sg(drm_info->domain, drm_buffer->copy_sgt->sgl,
				drm_buffer->copy_sgt->nents,
				IOMMU_READ | IOMMU_WRITE);
	if (!ret) {
		ret = -ENOMEM;
		goto fail_alloc;
	}
	drm_buffer->iova = sg_dma_address(drm_buffer->copy_sgt->sgl);
	drm_buffer->size = drm_buffer->dma_buf->size;

	drm_buffer->attach = attach;
	drm_buffer->sgt = sgt;

	mutex_unlock(&iommu_info->iommu_mutex);

	INIT_LIST_HEAD(&drm_buffer->list);
	mutex_lock(&session_info->list_mutex);
	session_info->buffer_nums++;
	vpu_iommu_debug(session_info->debug_level, DEBUG_IOMMU_NORMAL,
			"buffer nums %d\n", session_info->buffer_nums);
	vcodec_drm_remove_extra_buffer_no_lock(session_info);
	drm_buffer->index = session_info->max_idx;
	list_add_tail(&drm_buffer->list, &session_info->buffer_list);
	session_info->max_idx++;
	if ((session_info->max_idx & 0xfffffff) == 0)
		session_info->max_idx = 0;
	mutex_unlock(&session_info->list_mutex);

	return drm_buffer->index;

fail_alloc:
	sg_free_table(drm_buffer->copy_sgt);
	kfree(drm_buffer->copy_sgt);
	dma_buf_unmap_attachment(attach, sgt,
				 DMA_BIDIRECTIONAL);
fail_detach:
	dma_buf_detach(drm_buffer->dma_buf, attach);
	dma_buf_put(drm_buffer->dma_buf);
fail_out:
	kfree(drm_buffer);
	mutex_unlock(&iommu_info->iommu_mutex);

	return ret;
}

static int vcodec_drm_alloc(struct vcodec_iommu_session_info *session_info,
			    unsigned long size,
			    unsigned long align)
{
	struct sg_table *table;
	struct scatterlist *sg;
	struct list_head pages;
	struct page *page, *tmp_page;
	long size_remaining = PAGE_ALIGN(size);
	struct vcodec_drm_buffer *drm_buffer;
	struct vcodec_iommu_info *iommu_info = session_info->iommu_info;
	struct vcodec_iommu_drm_info *drm_info = iommu_info->private;
	int i;

	if (align > PAGE_SIZE)
		return -EINVAL;

	if (size / PAGE_SIZE > totalram_pages / 2)
		return -ENOMEM;

	drm_buffer = kzalloc(sizeof(*drm_buffer), GFP_KERNEL);
	if (!drm_buffer)
		return -ENOMEM;

	drm_buffer->session_info = session_info;
	drm_buffer->last_used = ktime_set(0, 0);

	kref_init(&drm_buffer->ref);

	INIT_LIST_HEAD(&pages);

	i = 0;
	while (size_remaining > 0) {
		gfp_t gfp_flags = (GFP_HIGHUSER | __GFP_ZERO | __GFP_NOWARN |
				   __GFP_NORETRY) & ~__GFP_DIRECT_RECLAIM;
		page = alloc_pages(gfp_flags | __GFP_COMP, 1);
		if (!page)
			goto free_pages;

		size_remaining -= PAGE_SIZE;
		list_add_tail(&page->lru, &pages);
		i++;
	}

	table = kmalloc(sizeof(*table), GFP_KERNEL);
	if (!table)
		goto free_pages;

	if (sg_alloc_table(table, i, GFP_KERNEL))
		goto free_table;

	sg = table->sgl;
	list_for_each_entry_safe(page, tmp_page, &pages, lru) {
		sg_set_page(sg, page, PAGE_SIZE, 0);
		sg = sg_next(sg);
		list_del(&page->lru);
	}

	mutex_lock(&iommu_info->iommu_mutex);
	drm_info = session_info->iommu_info->private;
	drm_buffer->copy_sgt = table;

	vcodec_dma_map_sg(drm_info->domain, drm_buffer->copy_sgt->sgl,
			  drm_buffer->copy_sgt->nents,
			  IOMMU_READ | IOMMU_WRITE);
	drm_buffer->iova = sg_dma_address(drm_buffer->copy_sgt->sgl);
	drm_buffer->size = size;
	drm_buffer->flags = VCODEC_DRM_BUFFER_ALLOC;

	mutex_unlock(&iommu_info->iommu_mutex);

	INIT_LIST_HEAD(&drm_buffer->list);
	mutex_lock(&session_info->list_mutex);
	session_info->buffer_nums++;
	vpu_iommu_debug(session_info->debug_level, DEBUG_IOMMU_NORMAL,
			"buffer nums %d\n", session_info->buffer_nums);
	vcodec_drm_remove_extra_buffer_no_lock(session_info);
	drm_buffer->index = session_info->max_idx;
	list_add_tail(&drm_buffer->list, &session_info->buffer_list);
	session_info->max_idx++;
	if ((session_info->max_idx & 0xfffffff) == 0)
		session_info->max_idx = 0;
	mutex_unlock(&session_info->list_mutex);

	return drm_buffer->index;

free_table:
	kfree(table);
free_pages:
	list_for_each_entry_safe(page, tmp_page, &pages, lru)
		__free_pages(page, 8);

	kfree(drm_buffer);

	return -ENOMEM;
}

static int vcodec_drm_create(struct vcodec_iommu_info *iommu_info)
{
	struct vcodec_iommu_drm_info *drm_info;
	struct iommu_group *group;
	int ret;

	iommu_info->private = kzalloc(sizeof(*drm_info),
				      GFP_KERNEL);
	drm_info = iommu_info->private;
	if (!drm_info)
		return -ENOMEM;

	drm_info->domain = iommu_domain_alloc(&platform_bus_type);
	drm_info->attached = false;
	if (!drm_info->domain)
		return -ENOMEM;

	ret = iommu_get_dma_cookie(drm_info->domain);
	if (ret)
		goto err_free_domain;

	group = iommu_group_get(iommu_info->dev);
	if (!group) {
		group = iommu_group_alloc();
		if (IS_ERR(group)) {
			dev_err(iommu_info->dev,
				"Failed to allocate IOMMU group\n");
			goto err_put_cookie;
		}
		ret = iommu_group_add_device(group, iommu_info->dev);
		if (ret) {
			dev_err(iommu_info->dev,
				"failed to add device to IOMMU group\n");
			goto err_put_cookie;
		}
	}
	iommu_dma_init_domain(drm_info->domain, 0x10000000, SZ_2G);
	iommu_group_put(group);

	return 0;

err_put_cookie:
	iommu_put_dma_cookie(drm_info->domain);
err_free_domain:
	iommu_domain_free(drm_info->domain);

	return ret;
}

static int vcodec_drm_destroy(struct vcodec_iommu_info *iommu_info)
{
	struct vcodec_iommu_drm_info *drm_info = iommu_info->private;

	iommu_put_dma_cookie(drm_info->domain);
	iommu_domain_free(drm_info->domain);

	kfree(drm_info);
	iommu_info->private = NULL;

	return 0;
}

static struct vcodec_iommu_ops drm_ops = {
	.create = vcodec_drm_create,
	.alloc = vcodec_drm_alloc,
	.import = vcodec_drm_import,
	.free = vcodec_drm_free,
	.free_fd = vcodec_drm_free_fd,
	.map_kernel = vcodec_drm_map_kernel,
	.unmap_kernel = vcodec_drm_unmap_kernel,
	.map_iommu = vcodec_drm_map_iommu,
	.unmap_iommu = vcodec_drm_unmap_iommu,
	.map_iommu_with_iova = vcodec_drm_map_iommu_with_iova,
	.unmap_iommu_with_iova = vcodec_drm_unmap_iommu_with_iova,
	.destroy = vcodec_drm_destroy,
	.dump = vcdoec_drm_dump_info,
	.attach = vcodec_drm_attach,
	.detach = vcodec_drm_detach,
	.clear = vcodec_drm_clear_session,
};

void vcodec_iommu_drm_set_ops(struct vcodec_iommu_info *iommu_info)
{
	if (!iommu_info)
		return;
	iommu_info->ops = &drm_ops;
}
