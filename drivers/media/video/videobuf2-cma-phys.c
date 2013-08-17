/* linux/drivers/media/video/videobuf2-cma-phys.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *              http://www.samsung.com/
 *
 * CMA-phys memory allocator for videobuf2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/cma.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/file.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>

#include <media/videobuf2-core.h>
#include <media/videobuf2-memops.h>

#include <asm/cacheflush.h>

#define SIZE_THRESHOLD SZ_1M

struct vb2_cma_phys_conf {
	struct device		*dev;
	const char		*type;
	unsigned long		alignment;
	bool			cacheable;
};

struct vb2_cma_phys_buf {
	struct vb2_cma_phys_conf		*conf;
	dma_addr_t			paddr;
	unsigned long			size;
	struct vm_area_struct		*vma;
	atomic_t			refcount;
	struct vb2_vmarea_handler	handler;
	bool				cacheable;
};

static void vb2_cma_phys_put(void *buf_priv);

static void *vb2_cma_phys_alloc(void *alloc_ctx, unsigned long size)
{
	struct vb2_cma_phys_conf *conf = alloc_ctx;
	struct vb2_cma_phys_buf *buf;

	buf = kzalloc(sizeof *buf, GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	buf->paddr = cma_alloc(conf->dev, conf->type, size, conf->alignment);
	if (IS_ERR((void *)buf->paddr)) {
		printk(KERN_ERR "cma_alloc of size %ld failed\n", size);
		kfree(buf);
		return ERR_PTR(-ENOMEM);
	}

	buf->conf = conf;
	buf->size = size;
	buf->cacheable = conf->cacheable;

	buf->handler.refcount = &buf->refcount;
	buf->handler.put = vb2_cma_phys_put;
	buf->handler.arg = buf;

	atomic_inc(&buf->refcount);

	return buf;
}

static void vb2_cma_phys_put(void *buf_priv)
{
	struct vb2_cma_phys_buf *buf = buf_priv;

	if (atomic_dec_and_test(&buf->refcount)) {
		cma_free(buf->paddr);
		kfree(buf);
	}
}

static void *vb2_cma_phys_cookie(void *buf_priv)
{
	struct vb2_cma_phys_buf *buf = buf_priv;

	return (void *)buf->paddr;
}

static unsigned int vb2_cma_phys_num_users(void *buf_priv)
{
	struct vb2_cma_phys_buf *buf = buf_priv;

	return atomic_read(&buf->refcount);
}

/**
 * vb2_cma_mmap_pfn_range() - map physical pages to userspace
 * @vma:	virtual memory region for the mapping
 * @paddr:	starting physical address of the memory to be mapped
 * @size:	size of the memory to be mapped
 * @vm_ops:	vm operations to be assigned to the created area
 * @priv:	private data to be associated with the area
 *
 * Returns 0 on success.
 */
int vb2_cma_phys_mmap_pfn_range(struct vm_area_struct *vma, unsigned long paddr,
				 unsigned long size,
				 const struct vm_operations_struct *vm_ops,
				 void *priv)
{
	int ret;

	size = min_t(unsigned long, vma->vm_end - vma->vm_start, size);

	ret = remap_pfn_range(vma, vma->vm_start, paddr >> PAGE_SHIFT,
				size, vma->vm_page_prot);
	if (ret) {
		printk(KERN_ERR "Remapping memory failed, error: %d\n", ret);
		return ret;
	}

	vma->vm_flags		|= VM_DONTEXPAND | VM_RESERVED;
	vma->vm_private_data	= priv;
	vma->vm_ops		= vm_ops;

	vma->vm_ops->open(vma);

	printk(KERN_DEBUG "%s: mapped paddr 0x%08lx at 0x%08lx, size %ld\n",
			__func__, paddr, vma->vm_start, size);

	return 0;
}

static int vb2_cma_phys_mmap(void *buf_priv, struct vm_area_struct *vma)
{
	struct vb2_cma_phys_buf *buf = buf_priv;

	if (!buf) {
		printk(KERN_ERR "No buffer to map\n");
		return -EINVAL;
	}

	if (!buf->cacheable)
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

	return vb2_cma_phys_mmap_pfn_range(vma, buf->paddr, buf->size,
					   &vb2_common_vm_ops, &buf->handler);
}

static void *vb2_cma_phys_get_userptr(void *alloc_ctx, unsigned long vaddr,
				 unsigned long size, int write)
{
	struct vb2_cma_phys_buf *buf;

	buf = kzalloc(sizeof *buf, GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	printk(KERN_DEBUG "[%s] paddr(0x%08lx)\n", __func__, vaddr);
	buf->size = size;
	buf->paddr = vaddr;	/* drv directly gets phys. addr. from user. */

	return buf;
}

static void vb2_cma_phys_put_userptr(void *mem_priv)
{
	struct vb2_cma_phys_buf *buf = mem_priv;

	if (!buf)
		return;

	kfree(buf);
}

static void *vb2_cma_phys_vaddr(void *mem_priv)
{
	struct vb2_cma_phys_buf *buf = mem_priv;
	if (!buf)
		return 0;

	return phys_to_virt(buf->paddr);
}

const struct vb2_mem_ops vb2_cma_phys_memops = {
	.alloc		= vb2_cma_phys_alloc,
	.put		= vb2_cma_phys_put,
	.cookie		= vb2_cma_phys_cookie,
	.mmap		= vb2_cma_phys_mmap,
	.get_userptr	= vb2_cma_phys_get_userptr,
	.put_userptr	= vb2_cma_phys_put_userptr,
	.num_users	= vb2_cma_phys_num_users,
	.vaddr		= vb2_cma_phys_vaddr,
};
EXPORT_SYMBOL_GPL(vb2_cma_phys_memops);

void *vb2_cma_phys_init(struct device *dev, const char *type,
			unsigned long alignment, bool cacheable)
{
	struct vb2_cma_phys_conf *conf;

	conf = kzalloc(sizeof *conf, GFP_KERNEL);
	if (!conf)
		return ERR_PTR(-ENOMEM);

	conf->dev = dev;
	conf->type = type;
	conf->alignment = alignment;
	conf->cacheable = cacheable;

	return conf;
}
EXPORT_SYMBOL_GPL(vb2_cma_phys_init);

void vb2_cma_phys_cleanup(void *conf)
{
	kfree(conf);
}
EXPORT_SYMBOL_GPL(vb2_cma_phys_cleanup);

void **vb2_cma_phys_init_multi(struct device *dev,
			  unsigned int num_planes,
			  const char *types[],
			  unsigned long alignments[],
			  bool cacheable)
{
	struct vb2_cma_phys_conf *cma_conf;
	void **alloc_ctxes;
	unsigned int i;

	alloc_ctxes = kzalloc((sizeof *alloc_ctxes + sizeof *cma_conf)
				* num_planes, GFP_KERNEL);
	if (!alloc_ctxes)
		return ERR_PTR(-ENOMEM);

	cma_conf = (void *)(alloc_ctxes + num_planes);

	for (i = 0; i < num_planes; ++i, ++cma_conf) {
		alloc_ctxes[i] = cma_conf;
		cma_conf->dev = dev;
		cma_conf->type = types[i];
		cma_conf->alignment = alignments[i];
		cma_conf->cacheable = cacheable;
	}

	return alloc_ctxes;
}
EXPORT_SYMBOL_GPL(vb2_cma_phys_init_multi);

void vb2_cma_phys_cleanup_multi(void **alloc_ctxes)
{
	kfree(alloc_ctxes);
}
EXPORT_SYMBOL_GPL(vb2_cma_phys_cleanup_multi);

void vb2_cma_phys_set_cacheable(void *alloc_ctx, bool cacheable)
{
	((struct vb2_cma_phys_conf *)alloc_ctx)->cacheable = cacheable;
}

bool vb2_cma_phys_get_cacheable(void *alloc_ctx)
{
	return ((struct vb2_cma_phys_conf *)alloc_ctx)->cacheable;
}

static void _vb2_cma_phys_cache_flush_all(void)
{
	flush_cache_all();	/* L1 */
	smp_call_function((smp_call_func_t)__cpuc_flush_kern_all, NULL, 1);
	outer_flush_all();	/* L2 */
}

static void _vb2_cma_phys_cache_flush_range(struct vb2_cma_phys_buf *buf,
					    unsigned long size)
{
	phys_addr_t start = buf->paddr;
	phys_addr_t end = start + size - 1;

	if (size > SZ_64K) {
		flush_cache_all();	/* L1 */
		smp_call_function((smp_call_func_t)__cpuc_flush_kern_all, NULL, 1);
	} else {
		dmac_flush_range(phys_to_virt(start), phys_to_virt(end));
	}

	outer_flush_range(start, end);	/* L2 */
}

int vb2_cma_phys_cache_flush(struct vb2_buffer *vb, u32 num_planes)
{
	struct vb2_cma_phys_buf *buf;
	unsigned long size = 0;
	int i;

	for (i = 0; i < num_planes; i++) {
		buf = vb->planes[i].mem_priv;
		if (!buf->cacheable) {
			pr_warning("This is non-cacheable buffer allocator\n");
			return -EINVAL;
		}

		size += buf->size;
	}

	if (size > (unsigned long)SIZE_THRESHOLD) {
		_vb2_cma_phys_cache_flush_all();
	} else {
		for (i = 0; i < num_planes; i++) {
			buf = vb->planes[i].mem_priv;
			_vb2_cma_phys_cache_flush_range(buf, size);
		}
	}

	return 0;
}

int vb2_cma_phys_cache_inv(struct vb2_buffer *vb, u32 num_planes)
{
	struct vb2_cma_phys_buf *buf;
	phys_addr_t start;
	size_t size;
	int i;

	for (i = 0; i < num_planes; i++) {
		buf = vb->planes[i].mem_priv;
		start = buf->paddr;
		size = buf->size;

		if (!buf->cacheable) {
			pr_warning("This is non-cacheable buffer allocator\n");
			return -EINVAL;
		}

		dmac_unmap_area(phys_to_virt(start), size, DMA_FROM_DEVICE);
		outer_inv_range(start, start + size);	/* L2 */
	}

	return 0;
}

int vb2_cma_phys_cache_clean(struct vb2_buffer *vb, u32 num_planes)
{
	struct vb2_cma_phys_buf *buf;
	phys_addr_t start;
	size_t size;
	int i;

	for (i = 0; i < num_planes; i++) {
		buf = vb->planes[i].mem_priv;
		start = buf->paddr;
		size = buf->size;

		if (!buf->cacheable) {
			pr_warning("This is non-cacheable buffer allocator\n");
			return -EINVAL;
		}

		dmac_unmap_area(phys_to_virt(start), size, DMA_TO_DEVICE);
		outer_clean_range(start, start + size - 1);	/* L2 */
	}

	return 0;
}

/* FIXME: l2 cache clean all should be implemented */
int vb2_cma_phys_cache_clean2(struct vb2_buffer *vb, u32 num_planes)
{
	struct vb2_cma_phys_buf *buf;
	unsigned long t_size = 0;
	phys_addr_t start;
	size_t size;
	int i;

	for (i = 0; i < num_planes; i++) {
		buf = vb->planes[i].mem_priv;
		if (!buf->cacheable) {
			pr_warning("This is non-cacheable buffer allocator\n");
			return -EINVAL;
		}

		t_size += buf->size;
	}

	if (t_size > (unsigned long)SIZE_THRESHOLD) {
		for (i = 0; i < num_planes; i++) {
			buf = vb->planes[i].mem_priv;
			start = buf->paddr;
			size = buf->size;

			dmac_unmap_area(phys_to_virt(start), size, DMA_TO_DEVICE);
		}
	} else {
		for (i = 0; i < num_planes; i++) {
			buf = vb->planes[i].mem_priv;
			start = buf->paddr;
			size = buf->size;

			dmac_unmap_area(phys_to_virt(start), size, DMA_TO_DEVICE);
			outer_clean_range(start, start + size - 1);	/* L2 */
		}
	}

	return 0;
}

MODULE_AUTHOR("Jonghun, Han <jonghun.han@samsung.com>");
MODULE_DESCRIPTION("CMA-phys allocator handling routines for videobuf2");
MODULE_LICENSE("GPL");
