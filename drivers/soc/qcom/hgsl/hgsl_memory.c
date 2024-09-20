// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "hgsl_memory.h"
#include <linux/dma-buf.h>
#include <linux/highmem.h>
#include <linux/fs.h>
#include <soc/qcom/secure_buffer.h>

#ifndef pgprot_writebackcache
#define pgprot_writebackcache(_prot)	(_prot)
#endif

#ifndef pgprot_writethroughcache
#define pgprot_writethroughcache(_prot)	(_prot)
#endif

static DEFINE_MUTEX(hgsl_map_global_lock);

static struct sg_table *hgsl_get_sgt_internal(struct hgsl_mem_node *mem_node)
{
	struct sg_table *sgt;
	int ret = 0;

	if (!mem_node) {
		sgt = ERR_PTR(-EINVAL);
		goto out;
	}

	mutex_lock(&hgsl_map_global_lock);
	if (!IS_ERR_OR_NULL(mem_node->sgt)) {
		sgt = mem_node->sgt;
		mem_node->sgt_refcount++;
	} else {
		sgt = hgsl_zalloc(sizeof(struct sg_table));
		if (!sgt) {
			sgt = ERR_PTR(-ENOMEM);
		} else {
			ret = sg_alloc_table_from_pages(sgt, mem_node->pages,
				mem_node->page_count, 0,
				mem_node->page_count << PAGE_SHIFT, GFP_KERNEL);
			if (ret) {
				hgsl_free(sgt);
				sgt = ERR_PTR(ret);
			} else {
				mem_node->sgt = sgt;
				mem_node->sgt_refcount = 1;
				if (mem_node->dma_buf)
					get_dma_buf(mem_node->dma_buf);
			}
		}
	}
	mutex_unlock(&hgsl_map_global_lock);

out:
	return sgt;
}


static void hgsl_put_sgt_internal(struct hgsl_mem_node *mem_node)
{
	if (!mem_node)
		return;

	mutex_lock(&hgsl_map_global_lock);
	if (!mem_node->sgt_refcount || !mem_node->sgt)
		goto out;

	if (--mem_node->sgt_refcount == 0) {
		sg_free_table(mem_node->sgt);
		hgsl_free(mem_node->sgt);
		mem_node->sgt = NULL;
		if (mem_node->dma_buf)
			dma_buf_put(mem_node->dma_buf);
	}
out:
	mutex_unlock(&hgsl_map_global_lock);
}

static struct sg_table *hgsl_mem_map_dma_buf(
	struct dma_buf_attachment *attachment,
	enum dma_data_direction direction)
{
	struct dma_buf *dmabuf = attachment->dmabuf;
	struct hgsl_mem_node *mem_node = dmabuf->priv;

	return hgsl_get_sgt_internal(mem_node);
}


static void hgsl_mem_unmap_dma_buf(struct dma_buf_attachment *attachment,
	struct sg_table *sgt,
	enum dma_data_direction direction)
{
	struct dma_buf *dmabuf = attachment->dmabuf;
	struct hgsl_mem_node *mem_node = dmabuf->priv;

	if (!mem_node)
		return;

	if (sgt != mem_node->sgt) {
		LOGE("invalid sgt");
		return;
	}

	hgsl_put_sgt_internal(mem_node);
}

static int hgsl_mem_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct hgsl_mem_node *mem_node = dmabuf->priv;
	unsigned long page_count;
	unsigned long addr;
	uint32_t i;
	uint32_t cache_mode;
	int ret;

	if ((vma == NULL) ||
	    (mem_node->flags & GSL_MEMFLAGS_PROTECTED))
		return -EINVAL;

	page_count = vma_pages(vma);
	addr = vma->vm_start;

	/* Check for valid size. */
	if ((mem_node->page_count < vma->vm_pgoff) ||
		(mem_node->page_count < page_count + vma->vm_pgoff))
		return -EINVAL;

	vm_flags_set(vma, VM_DONTDUMP | VM_DONTEXPAND | VM_DONTCOPY);
	vma->vm_private_data = mem_node;
	if (!mem_node->default_iocoherency) {
		cache_mode = mem_node->flags & GSL_MEMFLAGS_CACHEMODE_MASK;
		switch (cache_mode) {
		case GSL_MEMFLAGS_WRITECOMBINE:
		case GSL_MEMFLAGS_UNCACHED:
			vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
			break;
		case GSL_MEMFLAGS_WRITETHROUGH:
			vma->vm_page_prot = pgprot_writethroughcache(vma->vm_page_prot);
			break;
		case GSL_MEMFLAGS_WRITEBACK:
			vma->vm_page_prot = pgprot_writebackcache(vma->vm_page_prot);
			break;
		default:
			LOGE("invalid cache mode");
			return -EINVAL;
		}
	}

	for (i = 0; i < page_count; i++) {
		struct page *page = mem_node->pages[i + vma->vm_pgoff];

		ret = vm_insert_page(vma, addr, page);
		if (ret)
			return ret;
		addr += PAGE_SIZE;
	}

	return 0;
}

static void hgsl_free_pages(struct hgsl_mem_node *mem_node)
{
	uint32_t pcount = mem_node->page_count;
	unsigned int page_order;
	uint32_t i;

	for (i = 0; i < pcount;) {
		struct page *p = mem_node->pages[i];

		page_order = compound_order(p);

		mod_node_page_state(page_pgdat(p), NR_KERNEL_MISC_RECLAIMABLE,
							-(1 << page_order));

		__free_pages(p, page_order);
		i += 1 << page_order;
	}

	mem_node->page_count = 0;
}

static int hgsl_lock_pages(struct hgsl_mem_node *mem_node)
{
	struct sg_table *sgt = hgsl_get_sgt_internal(mem_node);
	struct scatterlist *sg;
	int src_vmid = VMID_HLOS;
	int dest_vmid = VMID_CP_PIXEL;
	int dest_perms = PERM_READ | PERM_WRITE;
	int ret;
	int i;

	if (IS_ERR(sgt))
		return PTR_ERR(sgt);

	ret = hyp_assign_table(sgt, &src_vmid, 1, &dest_vmid, &dest_perms, 1);
	if (ret) {
		LOGE("Failed to assign sgt %d", ret);
		hgsl_put_sgt_internal(mem_node);
		return ret;
	}

	for_each_sg(sgt->sgl, sg, sgt->nents, i)
		SetPagePrivate(sg_page(sg));

	return 0;
}

static int hgsl_unlock_pages(struct hgsl_mem_node *mem_node)
{
	struct sg_table *sgt = mem_node->sgt;
	struct scatterlist *sg;
	int src_vmid = VMID_CP_PIXEL;
	int dest_vmid = VMID_HLOS;
	int dest_perms = PERM_READ | PERM_WRITE | PERM_EXEC;
	int ret;
	int i;

	if (!sgt)
		return -EINVAL;

	ret = hyp_assign_table(sgt, &src_vmid, 1, &dest_vmid, &dest_perms, 1);
	if (ret)
		goto out;

	for_each_sg(sgt->sgl, sg, sgt->nents, i)
		ClearPagePrivate(sg_page(sg));

out:
	mem_node->dma_buf = NULL;
	hgsl_put_sgt_internal(mem_node);
	return ret;
}

static void hgsl_mem_free_actual(struct hgsl_mem_node *mem_node)
{
	if (mem_node->flags & GSL_MEMFLAGS_PROTECTED)
		hgsl_unlock_pages(mem_node);

	hgsl_free_pages(mem_node);
	hgsl_free(mem_node->pages);
	hgsl_free(mem_node);
}

static void hgsl_mem_dma_buf_release(struct dma_buf *dmabuf)
{
	struct hgsl_mem_node *mem_node = dmabuf->priv;

	hgsl_mem_free_actual(mem_node);
}

static int hgsl_mem_dma_buf_vmap(struct dma_buf *dmabuf, struct iosys_map *map)
{
	struct hgsl_mem_node *mem_node = dmabuf->priv;
	pgprot_t prot = PAGE_KERNEL;

	if (mem_node->flags & GSL_MEMFLAGS_PROTECTED)
		return -EINVAL;

	mutex_lock(&hgsl_map_global_lock);
	if (!mem_node->default_iocoherency)
		prot = pgprot_writecombine(prot);

	if (IS_ERR_OR_NULL(mem_node->vmapping))
		mem_node->vmapping = vmap(mem_node->pages,
			    mem_node->page_count,
			    VM_IOREMAP,
			    prot);

	if (!IS_ERR_OR_NULL(mem_node->vmapping))
		mem_node->vmap_count++;
	mutex_unlock(&hgsl_map_global_lock);

	if (!mem_node->vmapping)
		return -ENOMEM;

	iosys_map_set_vaddr(map, mem_node->vmapping);
	return 0;
}

static void hgsl_mem_dma_buf_vunmap(struct dma_buf *dmabuf, struct iosys_map *map)
{
	struct hgsl_mem_node *mem_node = dmabuf->priv;

	if (mem_node->flags & GSL_MEMFLAGS_PROTECTED)
		return;

	mutex_lock(&hgsl_map_global_lock);
	if (!mem_node->vmap_count)
		goto out;

	if (--mem_node->vmap_count == 0) {
		if (!IS_ERR_OR_NULL(mem_node->vmapping)) {
			vunmap(mem_node->vmapping);
			mem_node->vmapping = NULL;
		} else {
			pr_err("HGSL %s vmapping is NULL\n", __func__);
		}
	}
out:
	mutex_unlock(&hgsl_map_global_lock);
}

static struct dma_buf_ops dma_buf_ops = {
	.map_dma_buf = hgsl_mem_map_dma_buf,
	.unmap_dma_buf = hgsl_mem_unmap_dma_buf,
	.mmap = hgsl_mem_mmap,
	.release = hgsl_mem_dma_buf_release,
	.vmap = hgsl_mem_dma_buf_vmap,
	.vunmap = hgsl_mem_dma_buf_vunmap,
};


static inline gfp_t hgsl_gfp_mask(unsigned int page_order)
{
	unsigned int gfp_mask = __GFP_HIGHMEM | __GFP_ZERO;

	if (page_order > 0) {
		gfp_mask |= __GFP_COMP | __GFP_NORETRY | __GFP_NOWARN;
		gfp_mask &= ~__GFP_RECLAIM;
	} else {
		gfp_mask |= GFP_KERNEL;
	}

	return gfp_mask;
}

static void _dma_cache_op(struct device *dev, struct page *page,
		uint32_t page_count, unsigned int op)
{
	struct scatterlist sgl;
	size_t size_bytes = page_count << PAGE_SHIFT;

	if (!dev || !page || !page_count)
		return;

	sg_init_table(&sgl, 1);
	sg_set_page(&sgl, page, size_bytes, 0);
	sg_dma_address(&sgl) = page_to_phys(page);

	/*
	 * APIs for Cache Maintenance Operations are updated in kernel
	 * version 6.1. Prior to 6.1, dma_sync_sg_for_device() with
	 * DMA_FROM_DEVICE as direction triggers cache invalidate and
	 * clean whereas in kernel version 6.1, it triggers only cache
	 * clean. Hence use dma_sync_sg_for_cpu() for cache invalidate
	 * for kernel version 6.1 and above.
	 */

	switch (op) {
	case GSL_CACHEFLAGS_FLUSH:
		/*This change is for kernel version 6.1*/
		dma_sync_sg_for_device(dev, &sgl, 1, DMA_TO_DEVICE);
		dma_sync_sg_for_cpu(dev, &sgl, 1, DMA_FROM_DEVICE);
		break;
	case GSL_CACHEFLAGS_CLEAN:
		dma_sync_sg_for_device(dev, &sgl, 1, DMA_TO_DEVICE);
		break;
	case GSL_CACHEFLAGS_INVALIDATE:
		/*This change is for kernel version 6.1*/
		dma_sync_sg_for_cpu(dev, &sgl, 1, DMA_FROM_DEVICE);
		break;
	default:
		LOGE("invalid cache operation");
		break;
	}
}

static void hgsl_get_sgt(struct device *dev, struct hgsl_mem_node *mem_node,
	bool internal)
{
	if (!IS_ERR_OR_NULL(mem_node->sgt_ext))
		return;

	if (internal) {
		mem_node->sgt_ext = hgsl_get_sgt_internal(mem_node);
		return;
	}

	if (IS_ERR_OR_NULL(mem_node->attach)) {
		if (IS_ERR_OR_NULL(mem_node->dma_buf)) {
			mem_node->dma_buf = dma_buf_get(mem_node->fd);
			if (IS_ERR_OR_NULL(mem_node->dma_buf))
				return;
		}

		mem_node->attach = dma_buf_attach(mem_node->dma_buf, dev);
		if (IS_ERR_OR_NULL(mem_node->attach))
			return;
	}

	mem_node->sgt_ext = dma_buf_map_attachment(mem_node->attach, DMA_BIDIRECTIONAL);
}

void hgsl_put_sgt(struct hgsl_mem_node *mem_node, bool internal)
{
	if (IS_ERR_OR_NULL(mem_node->sgt_ext))
		return;

	if (internal) {
		hgsl_put_sgt_internal(mem_node);
		mem_node->sgt_ext = NULL;
		return;
	}

	if (!IS_ERR_OR_NULL(mem_node->attach)) {
		dma_buf_unmap_attachment(mem_node->attach,
								mem_node->sgt_ext,
								DMA_BIDIRECTIONAL);
		mem_node->sgt_ext = NULL;
	} else {
		LOGE("invalid attach");
	}

	if (!IS_ERR_OR_NULL(mem_node->attach)) {
		if (!IS_ERR_OR_NULL(mem_node->dma_buf)) {
			dma_buf_detach(mem_node->dma_buf, mem_node->attach);
			mem_node->attach = NULL;
		} else {
			LOGE("invalid dma_buf");
		}
	}

	if (!IS_ERR_OR_NULL(mem_node->dma_buf)) {
		dma_buf_put(mem_node->dma_buf);
		mem_node->dma_buf = NULL;
	}

}

int hgsl_mem_cache_op(struct device *dev, struct hgsl_mem_node *mem_node,
	bool internal, uint64_t offsetbytes, uint64_t sizebytes, uint32_t op)
{
	int ret = 0;
	uint32_t cache_mode = 0;
	uint32_t pg_offset = 0;
	uint32_t pg_size = 0;
	uint32_t sg_size = 0;
	uint32_t i = 0;
	struct sg_table *sgt = NULL;
	struct scatterlist *s = NULL;

	if (!dev || !mem_node)
		return -EINVAL;

	if (mem_node->flags & GSL_MEMFLAGS_PROTECTED)
		return -EINVAL;

	cache_mode = mem_node->flags & GSL_MEMFLAGS_CACHEMODE_MASK;
	switch (cache_mode) {
	case GSL_MEMFLAGS_WRITETHROUGH:
	case GSL_MEMFLAGS_WRITEBACK:
		break;
	default:
		return 0;
	}

	if (sizebytes == 0 || sizebytes > UINT_MAX || offsetbytes > UINT_MAX)
		return -ERANGE;

	/* Check that offset+length does not exceed memdesc->size */
	if ((sizebytes + offsetbytes) > mem_node->memdesc.size)
		return -ERANGE;

	sizebytes += offsetbytes & ~PAGE_MASK;
	pg_size = PAGE_ALIGN(sizebytes) >> PAGE_SHIFT;
	pg_offset = offsetbytes >> PAGE_SHIFT;

	if (IS_ERR_OR_NULL(mem_node->sgt_ext)) {
		hgsl_get_sgt(dev, mem_node, internal);
		if (IS_ERR_OR_NULL(mem_node->sgt_ext))
			return -EINVAL;
	}
	sgt = mem_node->sgt_ext;

	for_each_sg(sgt->sgl, s, sgt->nents, i) {
		sg_size = s->length >> PAGE_SHIFT;
		if (sg_size > pg_offset) {
			struct page *page = nth_page(sg_page(s), pg_offset);

			sg_size -= pg_offset;
			pg_offset = 0;
			sg_size = min(sg_size, pg_size);
			_dma_cache_op(dev, page, sg_size, op);
			pg_size -= sg_size;
			if (!pg_size)
				break;
		} else {
			pg_offset -= sg_size;
		}
	}

	return ret;
}

static int hgsl_alloc_pages(struct device *dev, uint32_t requested_pcount,
				struct page **pages)
{
	struct page *page = NULL;
	unsigned int order = get_order(requested_pcount << PAGE_SHIFT);
	uint32_t pcount = 0;
	uint32_t i;

	while (1) {
		gfp_t gfp_mask = hgsl_gfp_mask(order);

		page = alloc_pages(gfp_mask, order);
		if ((page == NULL) && (order > 0)) {
			order--;
			continue;
		}
		break;
	}

	if (page) {
		pcount = (1 << order);
		if (requested_pcount < pcount)
			pcount = requested_pcount;
		for (i = 0; i < pcount; i++)
			pages[i] = nth_page(page, i);
		_dma_cache_op(dev, page, pcount, GSL_CACHEFLAGS_FLUSH);
		mod_node_page_state(page_pgdat(page), NR_KERNEL_MISC_RECLAIMABLE,
						(1 << order));
	}

	return pcount;
}

static int hgsl_export_dma_buf(struct hgsl_mem_node *mem_node)
{
	struct dma_buf *dma_buf = NULL;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);

	exp_info.ops = &dma_buf_ops;
	exp_info.size = mem_node->page_count << PAGE_SHIFT;
	exp_info.flags = O_RDWR;
	exp_info.priv = mem_node;
	dma_buf = dma_buf_export(&exp_info);

	if (IS_ERR(dma_buf)) {
		LOGE("dma_buf_export failed");
		return -ENOMEM;
	}
	mem_node->dma_buf = dma_buf;

	return 0;
}

int hgsl_sharedmem_alloc(struct device *dev, uint32_t sizebytes,
	uint32_t flags, struct hgsl_mem_node *mem_node)
{
	uint32_t requested_size = PAGE_ALIGN(sizebytes);
	uint32_t requested_pcount = requested_size >> PAGE_SHIFT;
	uint32_t allocated_pcount = 0;
	uint32_t nents = 0;
	int ret = 0;

	mem_node->pages =
		hgsl_malloc(requested_pcount * sizeof(struct page *));
	if (mem_node->pages == NULL)
		return -ENOMEM;

	while (requested_pcount > 0) {
		uint32_t pcount;

		pcount = hgsl_alloc_pages(dev, requested_pcount,
					mem_node->pages + allocated_pcount);

		if (pcount == 0) {
			LOGE("Out of memory requested 0x%x, allocated 0x%x",
				sizebytes, allocated_pcount << PAGE_SHIFT);
			break;
		}
		allocated_pcount += pcount;
		requested_pcount -= pcount;
		nents++;
	}

	mem_node->page_count = allocated_pcount;
	mem_node->sg_nents = nents;
	mem_node->memtype = GSL_USER_MEM_TYPE_ASHMEM;
	mem_node->memdesc.size = requested_size;
	mem_node->fd = -1;

	if (requested_pcount != 0)
		return -ENOMEM;

	if (flags & GSL_MEMFLAGS_PROTECTED) {
		ret = hgsl_lock_pages(mem_node);
		if (ret)
			return ret;
	}

	return hgsl_export_dma_buf(mem_node);
}

void hgsl_sharedmem_free(struct hgsl_mem_node *mem_node)
{
	if (!mem_node)
		return;

	hgsl_put_sgt(mem_node, true);

	if (mem_node->dma_buf)
		dma_buf_put(mem_node->dma_buf);
	else
		hgsl_mem_free_actual(mem_node);

}

void *hgsl_mem_node_zalloc(bool iocoherency)
{
	struct hgsl_mem_node *mem_node = NULL;

	mem_node = hgsl_zalloc(sizeof(*mem_node));
	if (mem_node == NULL)
		goto out;

	mem_node->default_iocoherency = iocoherency;

out:
	return mem_node;
}

int hgsl_mem_add_node(struct rb_root *rb_root,
		struct hgsl_mem_node *mem_node)
{
	struct rb_node **cur;
	struct rb_node *parent = NULL;
	struct hgsl_mem_node *node = NULL;
	int ret = 0;

	cur = &rb_root->rb_node;
	while (*cur) {
		parent = *cur;
		node = rb_entry(parent, struct hgsl_mem_node, mem_rb_node);
		if (mem_node->memdesc.gpuaddr > node->memdesc.gpuaddr)
			cur = &parent->rb_right;
		else if (mem_node->memdesc.gpuaddr < node->memdesc.gpuaddr)
			cur = &parent->rb_left;
		else {
			LOGE("Duplicate gpuaddr: 0x%llx",
				mem_node->memdesc.gpuaddr);
			ret = -EEXIST;
			goto out;
		}
	}

	rb_link_node(&mem_node->mem_rb_node, parent, cur);
	rb_insert_color(&mem_node->mem_rb_node, rb_root);
out:
	return ret;
}

struct hgsl_mem_node *hgsl_mem_find_node_locked(
		struct rb_root *rb_root, uint64_t gpuaddr,
		uint64_t size, bool accurate)
{
	struct rb_node *cur = NULL;
	struct hgsl_mem_node *node_found = NULL;

	cur = rb_root->rb_node;
	while (cur) {
		node_found = rb_entry(cur, struct hgsl_mem_node, mem_rb_node);
		if (hgsl_mem_range_inspect(
				node_found->memdesc.gpuaddr, gpuaddr,
				node_found->memdesc.size64, size,
				accurate)) {
			return node_found;
		} else if (node_found->memdesc.gpuaddr < gpuaddr)
			cur = cur->rb_right;
		else if (node_found->memdesc.gpuaddr > gpuaddr)
			cur = cur->rb_left;
		else {
			LOGE("Invalid addr: 0x%llx size: [0x%llx 0x%llx]",
				gpuaddr, size, node_found->memdesc.size64);
			goto out;
		}
	}

out:
	return NULL;
}

MODULE_IMPORT_NS(DMA_BUF);
