/* linux/drivers/media/video/videobuf2-ion.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Implementation of Android ION memory allocator for videobuf2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/file.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>
#include <linux/ion.h>
#include <linux/syscalls.h>

#include <asm/cacheflush.h>

#include <media/videobuf2-ion.h>
#include <plat/iovmm.h>
#include <plat/cpu.h>

static int vb2_ion_debug;
module_param(vb2_ion_debug, int, 0644);
#define dbg(level, fmt, arg...)						\
	do {								\
		if (vb2_ion_debug >= level)				\
			printk(KERN_DEBUG "vb2_ion: " fmt, ## arg);	\
	} while (0)

#define SIZE_THRESHOLD SZ_1M

struct vb2_ion_conf {
	struct device		*dev;
	const char		*name;

	struct ion_client	*client;

	unsigned long		align;
	bool			contig;
	bool			sharable;
	bool			cacheable;
	bool			use_mmu;
	atomic_t		mmu_enable;

	spinlock_t		slock;
};

struct vb2_ion_buf {
	struct vm_area_struct		**vma;
	int				vma_count;
	struct vb2_ion_conf		*conf;
	struct vb2_vmarea_handler	handler;

	struct ion_handle		*handle;	/* Kernel space */

	dma_addr_t			kva;
	dma_addr_t			dva;
	unsigned long			size;

	struct scatterlist		*sg;
	int				nents;

	atomic_t			ref;

	bool				cacheable;
};

static void vb2_ion_put(void *buf_priv);

static struct ion_client *vb2_ion_init_ion(struct vb2_ion *ion,
					   struct vb2_drv *drv)
{
	struct ion_client *client;
	int ret;
	int mask = ION_HEAP_EXYNOS_MASK | ION_HEAP_EXYNOS_CONTIG_MASK |
						ION_HEAP_EXYNOS_USER_MASK;

	client = ion_client_create(ion_exynos, mask, ion->name);
	if (IS_ERR(client)) {
		pr_err("ion_client_create: ion_name(%s)\n", ion->name);
		return ERR_PTR(-EINVAL);
	}

	if (!drv->use_mmu)
		return client;

	ret = iovmm_setup(ion->dev);
	if (ret) {
		pr_err("iovmm_setup: ion_name(%s)\n", ion->name);
		ion_client_destroy(client);
		return ERR_PTR(-EINVAL);
	}

	return client;
}

static void vb2_ion_init_conf(struct vb2_ion_conf *conf,
			      struct ion_client *client,
			      struct vb2_ion *ion,
			      struct vb2_drv *drv)
{
	conf->dev		= ion->dev;
	conf->name		= ion->name;
	conf->client		= client;
	conf->contig		= ion->contig;
	conf->cacheable		= ion->cacheable;
	conf->align		= ion->align;
	conf->use_mmu		= drv->use_mmu;

	spin_lock_init(&conf->slock);
}

void *vb2_ion_init(struct vb2_ion *ion,
		   struct vb2_drv *drv)
{
	struct ion_client *client;
	struct vb2_ion_conf *conf;

	conf = kzalloc(sizeof *conf, GFP_KERNEL);
	if (!conf)
		return ERR_PTR(-ENOMEM);

	client = vb2_ion_init_ion(ion, drv);
	if (IS_ERR(client)) {
		kfree(conf);
		return ERR_PTR(-EINVAL);
	}

	vb2_ion_init_conf(conf, client, ion, drv);

	return conf;
}
EXPORT_SYMBOL_GPL(vb2_ion_init);

void vb2_ion_cleanup(void *alloc_ctx)
{
	struct vb2_ion_conf *conf = alloc_ctx;

	BUG_ON(!conf);

	if (conf->use_mmu) {
		if (atomic_read(&conf->mmu_enable)) {
			pr_warning("mmu_enable(%d)\n", atomic_read(&conf->mmu_enable));
			iovmm_deactivate(conf->dev);
		}

		iovmm_cleanup(conf->dev);
	}

	ion_client_destroy(conf->client);

	kfree(alloc_ctx);
}
EXPORT_SYMBOL_GPL(vb2_ion_cleanup);

void **vb2_ion_init_multi(unsigned int num_planes,
			  struct vb2_ion *ion,
			  struct vb2_drv *drv)
{
	struct ion_client *client;
	struct vb2_ion_conf *conf;
	void **alloc_ctxes;
	int i;

	/* allocate structure of alloc_ctxes */
	alloc_ctxes = kzalloc((sizeof *alloc_ctxes + sizeof *conf) * num_planes,
			      GFP_KERNEL);

	if (!alloc_ctxes)
		return ERR_PTR(-ENOMEM);

	client = vb2_ion_init_ion(ion, drv);
	if (IS_ERR(client)) {
		kfree(alloc_ctxes);
		return ERR_PTR(-EINVAL);
	}

	conf = (void *)(alloc_ctxes + num_planes);
	for (i = 0; i < num_planes; ++i, ++conf) {
		alloc_ctxes[i] = conf;
		vb2_ion_init_conf(conf, client, ion, drv);
	}

	return alloc_ctxes;
}
EXPORT_SYMBOL_GPL(vb2_ion_init_multi);

void vb2_ion_cleanup_multi(void **alloc_ctxes)
{
	struct vb2_ion_conf *conf = alloc_ctxes[0];

	BUG_ON(!conf);

	if (conf->use_mmu) {
		if (atomic_read(&conf->mmu_enable)) {
			pr_warning("mmu_enable(%d)\n", atomic_read(&conf->mmu_enable));
			iovmm_deactivate(conf->dev);
		}

		iovmm_cleanup(conf->dev);
	}

	ion_client_destroy(conf->client);

	kfree(alloc_ctxes);
}
EXPORT_SYMBOL_GPL(vb2_ion_cleanup_multi);

static void *vb2_ion_alloc(void *alloc_ctx, unsigned long size)
{
	struct vb2_ion_conf	*conf = alloc_ctx;
	struct vb2_ion_buf	*buf;
	struct scatterlist	*sg;
	size_t	len;
	u32 heap = 0;
	int ret = 0;

	buf = kzalloc(sizeof *buf, GFP_KERNEL);
	if (!buf) {
		pr_err("no memory for vb2_ion_conf\n");
		return ERR_PTR(-ENOMEM);
	}

	/* Set vb2_ion_buf */
	buf->conf = conf;
	buf->size = size;
	buf->cacheable = conf->cacheable;

	/* Allocate: physical memory */
	if (conf->contig)
		heap = ION_HEAP_EXYNOS_CONTIG_MASK;
	else
		heap = ION_HEAP_EXYNOS_MASK;

	buf->handle = ion_alloc(conf->client, size, conf->align, heap);
	if (IS_ERR(buf->handle)) {
		pr_err("ion_alloc of size %ld\n", size);
		ret = -ENOMEM;
		goto err_alloc;
	}

	/* Getting scatterlist */
	buf->sg = ion_map_dma(conf->client, buf->handle);
	if (IS_ERR(buf->sg)) {
		pr_err("ion_map_dma conf->name(%s)\n", conf->name);
		ret = -ENOMEM;
		goto err_map_dma;
	}
	dbg(6, "PA(0x%x), SIZE(%x)\n", buf->sg->dma_address, buf->sg->length);

	sg = buf->sg;
	do {
		buf->nents++;
	} while ((sg = sg_next(sg)));
	dbg(6, "buf->nents(0x%x)\n", buf->nents);

	/* Map DVA */
	if (conf->use_mmu) {
		buf->dva = iovmm_map(conf->dev, buf->sg, 0, size);
		if (!buf->dva) {
			pr_err("iovmm_map: conf->name(%s)\n", conf->name);
			goto err_ion_map_dva;
		}
		dbg(6, "DVA(0x%x)\n", buf->dva);
	} else {
		ret = ion_phys(conf->client, buf->handle,
			       (unsigned long *)&buf->dva, &len);
		if (ret) {
			pr_err("ion_phys: conf->name(%s)\n", conf->name);
			goto err_ion_map_dva;
		}
	}

	/* Set struct vb2_vmarea_handler */
	buf->handler.refcount = &buf->ref;
	buf->handler.put = vb2_ion_put;
	buf->handler.arg = buf;

	atomic_inc(&buf->ref);

	return buf;

err_ion_map_dva:
	ion_unmap_dma(conf->client, buf->handle);

err_map_dma:
	ion_free(conf->client, buf->handle);

err_alloc:
	kfree(buf);

	return ERR_PTR(ret);
}

static void vb2_ion_put(void *buf_priv)
{
	struct vb2_ion_buf *buf = buf_priv;
	struct vb2_ion_conf *conf = buf->conf;

	dbg(6, "released: buf_refcnt(%d)\n", atomic_read(&buf->ref) - 1);

	if (atomic_dec_and_test(&buf->ref)) {
		if (conf->use_mmu)
			iovmm_unmap(conf->dev, buf->dva);

		ion_unmap_dma(conf->client, buf->handle);

		if (buf->kva)
			ion_unmap_kernel(conf->client, buf->handle);

		ion_free(conf->client, buf->handle);

		kfree(buf);
	}
}

/**
 * _vb2_ion_get_vma() - lock userspace mapped memory
 * @vaddr:	starting virtual address of the area to be verified
 * @size:	size of the area
 * @res_vma:	will return locked copy of struct vm_area for the given area
 *
 * This function will go through memory area of size @size mapped at @vaddr
 * If they are contiguous the virtual memory area is locked and a @res_vma is
 * filled with the copy and @res_pa set to the physical address of the buffer.
 *
 * Returns 0 on success.
 */
static struct vm_area_struct **_vb2_ion_get_vma(unsigned long vaddr,
					unsigned long size, int *vma_num)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma, *vma0;
	struct vm_area_struct **vmas;
	unsigned long prev_end = 0;
	unsigned long end;
	int i;

	end = vaddr + size;

	down_read(&mm->mmap_sem);
	vma0 = find_vma(mm, vaddr);
	if (!vma0) {
		vmas = ERR_PTR(-EINVAL);
		goto done;
	}

	for (*vma_num = 1, vma = vma0->vm_next, prev_end = vma0->vm_end;
		vma && (end > vma->vm_start) && (prev_end == vma->vm_start);
				prev_end = vma->vm_end, vma = vma->vm_next) {
		*vma_num += 1;
	}

	if (prev_end < end) {
		vmas = ERR_PTR(-EINVAL);
		goto done;
	}

	vmas = kmalloc(sizeof(*vmas) * *vma_num, GFP_KERNEL);
	if (!vmas) {
		vmas = ERR_PTR(-ENOMEM);
		goto done;
	}

	for (i = 0; i < *vma_num; i++, vma0 = vma0->vm_next) {
		vmas[i] = vb2_get_vma(vma0);
		if (!vmas[i])
			break;
	}

	if (i < *vma_num) {
		while (i-- > 0)
			vb2_put_vma(vmas[i]);

		kfree(vmas);
		vmas = ERR_PTR(-ENOMEM);
	}

done:
	up_read(&mm->mmap_sem);
	return vmas;
}

static void *vb2_ion_get_userptr(void *alloc_ctx, unsigned long vaddr,
				 unsigned long size, int write)
{
	struct vb2_ion_conf *conf = alloc_ctx;
	struct vb2_ion_buf *buf = NULL;
	size_t len;
	int ret = 0;
	bool malloced = false;
	struct scatterlist *sg;
	off_t offset;

	/* Create vb2_ion_buf */
	buf = kzalloc(sizeof *buf, GFP_KERNEL);
	if (!buf) {
		pr_err("kzalloc failed\n");
		return ERR_PTR(-ENOMEM);
	}

	/* Getting handle, client from DVA */
	buf->handle = ion_import_uva(conf->client, vaddr, &offset);
	if (IS_ERR(buf->handle)) {
		if ((PTR_ERR(buf->handle) == -ENXIO) && conf->use_mmu) {
			int flags = ION_HEAP_EXYNOS_USER_MASK;

			if (write)
				flags |= ION_EXYNOS_WRITE_MASK;

			buf->handle = ion_exynos_get_user_pages(conf->client,
							vaddr, size, flags);
			if (IS_ERR(buf->handle))
				ret = PTR_ERR(buf->handle);
		} else {
			ret = -EINVAL;
		}

		if (ret) {
			pr_err("%s: Failed to retrieving non-ion user buffer @ "
				"0x%lx (size:0x%lx, dev:%s, errno %ld)\n",
				__func__, vaddr, size, dev_name(conf->dev),
					PTR_ERR(buf->handle));
			goto err_import_uva;
		}

		malloced = true;
		offset = 0;
	}

	/* TODO: Need to check whether already DVA is created or not */

	buf->sg = ion_map_dma(conf->client, buf->handle);
	if (IS_ERR(buf->sg)) {
		ret = -ENOMEM;
		goto err_map_dma;
	}
	dbg(6, "PA(0x%x) size(%x)\n", buf->sg->dma_address, buf->sg->length);

	sg = buf->sg;
	do {
		buf->nents++;
	} while ((sg = sg_next(sg)));

	/* Map DVA */
	if (conf->use_mmu) {
		buf->dva = iovmm_map(conf->dev, buf->sg, offset, size);
		if (!buf->dva) {
			pr_err("iovmm_map: conf->name(%s)\n", conf->name);
			goto err_ion_map_dva;
		}
		dbg(6, "DVA(0x%x)\n", buf->dva);
	} else {
		ret = ion_phys(conf->client, buf->handle,
			       (unsigned long *)&buf->dva, &len);
		if (ret) {
			pr_err("ion_phys: conf->name(%s)\n", conf->name);
			goto err_ion_map_dva;
		}

		buf->dva += offset;
	}

	/* Set vb2_ion_buf */
	buf->vma = _vb2_ion_get_vma(vaddr, size, &buf->vma_count);
	if (IS_ERR(buf->vma)) {
		pr_err("Failed acquiring VMA 0x%08lx\n", vaddr);

		if (conf->use_mmu)
			iovmm_unmap(conf->dev, buf->dva);

		goto err_get_vma;
	}

	buf->conf = conf;
	buf->size = size;
	buf->cacheable = conf->cacheable;

	return buf;

err_get_vma:	/* fall through */
err_ion_map_dva:
	ion_unmap_dma(conf->client, buf->handle);

err_map_dma:
	ion_free(conf->client, buf->handle);

err_import_uva:
	kfree(buf);

	return ERR_PTR(ret);
}

static void vb2_ion_put_userptr(void *mem_priv)
{
	struct vb2_ion_buf *buf = mem_priv;
	struct vb2_ion_conf *conf = buf->conf;
	int i;

	if (!buf) {
		pr_err("No buffer to put\n");
		return;
	}

	/* Unmap DVA, KVA */
	if (conf->use_mmu)
		iovmm_unmap(conf->dev, buf->dva);

	ion_unmap_dma(conf->client, buf->handle);
	if (buf->kva)
		ion_unmap_kernel(conf->client, buf->handle);

	ion_free(conf->client, buf->handle);

	for (i = 0; i < buf->vma_count; i++)
		vb2_put_vma(buf->vma[i]);
	kfree(buf->vma);

	kfree(buf);
}

static void *vb2_ion_cookie(void *buf_priv)
{
	struct vb2_ion_buf *buf = buf_priv;

	if (!buf) {
		pr_err("failed to get buffer\n");
		return NULL;
	}

	return (void *)buf->dva;
}

static void *vb2_ion_vaddr(void *buf_priv)
{
	struct vb2_ion_buf *buf = buf_priv;
	struct vb2_ion_conf *conf = buf->conf;

	if (!buf) {
		pr_err("failed to get buffer\n");
		return NULL;
	}

	if (!buf->kva) {
		buf->kva = (dma_addr_t)ion_map_kernel(conf->client, buf->handle);
		if (IS_ERR(ERR_PTR(buf->kva))) {
			pr_err("ion_map_kernel handle(%x)\n",
				(u32)buf->handle);
			return NULL;
		}
	}

	return (void *)buf->kva;
}

static unsigned int vb2_ion_num_users(void *buf_priv)
{
	struct vb2_ion_buf *buf = buf_priv;

	return atomic_read(&buf->ref);
}

/**
 * _vb2_ion_mmap_pfn_range() - map physical pages(vcm) to userspace
 * @vma:	virtual memory region for the mapping
 * @sg:		scatterlist to be mapped
 * @nents:	number of scatterlist to be mapped
 * @size:	size of the memory to be mapped
 * @vm_ops:	vm operations to be assigned to the created area
 * @priv:	private data to be associated with the area
 *
 * Returns 0 on success.
 */
static int _vb2_ion_mmap_pfn_range(struct vm_area_struct *vma,
				   struct scatterlist *sg,
				   int nents,
				   unsigned long size,
				   const struct vm_operations_struct *vm_ops,
				   void *priv)
{
	struct scatterlist *s;
	dma_addr_t addr;
	size_t len;
	unsigned long org_vm_start = vma->vm_start;
	int vma_size = vma->vm_end - vma->vm_start;
	resource_size_t remap_size;
	int mapped_size = 0;
	int remap_break = 0;
	int ret, i = 0;

	for_each_sg(sg, s, nents, i) {
		addr = sg_phys(s);
		len = sg_dma_len(s);
		if ((mapped_size + len) > vma_size) {
			remap_size = vma_size - mapped_size;
			remap_break = 1;
		} else {
			remap_size = len;
		}

		ret = remap_pfn_range(vma, vma->vm_start, addr >> PAGE_SHIFT,
				      remap_size, vma->vm_page_prot);
		if (ret) {
			pr_err("Remapping failed, error: %d\n", ret);
			return ret;
		}

		dbg(6, "%dth page vaddr(0x%08x), paddr(0x%08x),	size(0x%08x)\n",
			i++, (u32)vma->vm_start, addr, len);

		mapped_size += remap_size;
		vma->vm_start += len;

		if (remap_break)
			break;
	}

	WARN_ON(size > mapped_size);

	/* re-assign initial start address */
	vma->vm_start		= org_vm_start;
	vma->vm_flags		|= VM_DONTEXPAND | VM_RESERVED;
	vma->vm_private_data	= priv;
	vma->vm_ops		= vm_ops;

	vma->vm_ops->open(vma);

	return 0;
}

static int vb2_ion_mmap(void *buf_priv, struct vm_area_struct *vma)
{
	struct vb2_ion_buf *buf = buf_priv;

	if (!buf) {
		pr_err("No buffer to map\n");
		return -EINVAL;
	}

	if (!buf->cacheable)
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

	return _vb2_ion_mmap_pfn_range(vma, buf->sg, buf->nents, buf->size,
				&vb2_common_vm_ops, &buf->handler);
}

const struct vb2_mem_ops vb2_ion_memops = {
	.alloc		= vb2_ion_alloc,
	.put		= vb2_ion_put,
	.cookie		= vb2_ion_cookie,
	.vaddr		= vb2_ion_vaddr,
	.mmap		= vb2_ion_mmap,
	.get_userptr	= vb2_ion_get_userptr,
	.put_userptr	= vb2_ion_put_userptr,
	.num_users	= vb2_ion_num_users,
};
EXPORT_SYMBOL_GPL(vb2_ion_memops);


void vb2_ion_set_sharable(void *alloc_ctx, bool sharable)
{
	((struct vb2_ion_conf *)alloc_ctx)->sharable = sharable;
}

void vb2_ion_set_cacheable(void *alloc_ctx, bool cacheable)
{
	((struct vb2_ion_conf *)alloc_ctx)->cacheable = cacheable;
}

bool vb2_ion_get_cacheable(void *alloc_ctx)
{
	return ((struct vb2_ion_conf *)alloc_ctx)->cacheable;
}

#if 0
int vb2_ion_cache_flush(struct vb2_buffer *vb, u32 num_planes)
{
	struct vb2_ion_conf *conf;
	struct vb2_ion_buf *buf;
	int i, ret;

	for (i = 0; i < num_planes; i++) {
		buf = vb->planes[i].mem_priv;
		conf = buf->conf;

		if (!buf->cacheable) {
			pr_warning("This is non-cacheable buffer allocator\n");
			return -EINVAL;
		}

		ret = dma_map_sg(conf->dev, buf->sg, buf->nents, DMA_TO_DEVICE);
		if (ret) {
			pr_err("flush sg cnt(%d)\n", ret);
			return -EINVAL;
		}
	}

	return 0;
}
#else
static void _vb2_ion_cache_flush_all(void)
{
	flush_cache_all();	/* L1 */
	smp_call_function((void (*)(void *))__cpuc_flush_kern_all, NULL, 1);
	outer_flush_all();	/* L2 */
}

static void _vb2_ion_cache_flush_range(struct vb2_ion_buf *buf,
				       unsigned long size)
{
	struct scatterlist *s;
	phys_addr_t start, end;
	int i;

	/* sequentially traversal phys */
	if (size > SZ_64K ) {
		flush_cache_all();	/* L1 */
		smp_call_function((void (*)(void *))__cpuc_flush_kern_all, NULL, 1);

		for_each_sg(buf->sg, s, buf->nents, i) {
			start = sg_phys(s);
			end = start + sg_dma_len(s) - 1;

			outer_flush_range(start, end);	/* L2 */
		}
	} else {
		dma_sync_sg_for_device(buf->conf->dev, buf->sg, buf->nents,
							DMA_BIDIRECTIONAL);
		dma_sync_sg_for_cpu(buf->conf->dev, buf->sg, buf->nents,
							DMA_BIDIRECTIONAL);
	}
}


int vb2_ion_cache_flush(struct vb2_buffer *vb, u32 num_planes)
{
	struct vb2_ion_conf *conf;
	struct vb2_ion_buf *buf;
	unsigned long size = 0;
	int i;

	for (i = 0; i < num_planes; i++) {
		buf = vb->planes[i].mem_priv;
		conf = buf->conf;

		if (!buf->cacheable) {
			pr_warning("This is non-cacheable buffer allocator\n");
			return -EINVAL;
		}

		size += buf->size;
	}

	if (size > (unsigned long)SIZE_THRESHOLD) {
		_vb2_ion_cache_flush_all();
	} else {
		for (i = 0; i < num_planes; i++) {
			buf = vb->planes[i].mem_priv;
			_vb2_ion_cache_flush_range(buf, size);
		}
	}

	return 0;
}
#endif

int vb2_ion_cache_inv(struct vb2_buffer *vb, u32 num_planes)
{
	struct vb2_ion_conf *conf;
	struct vb2_ion_buf *buf;
	int i;

	for (i = 0; i < num_planes; i++) {
		buf = vb->planes[i].mem_priv;
		conf = buf->conf;
		if (!buf->cacheable) {
			pr_warning("This is non-cacheable buffer allocator\n");
			return -EINVAL;
		}

		dma_unmap_sg(conf->dev, buf->sg, buf->nents, DMA_FROM_DEVICE);
	}

	return 0;
}

void vb2_ion_suspend(void *alloc_ctx)
{
	struct vb2_ion_conf *conf = alloc_ctx;
	unsigned long flags;

	if (!conf->use_mmu)
		return;

	spin_lock_irqsave(&conf->slock, flags);
	if (!atomic_read(&conf->mmu_enable)) {
		pr_warning("Already suspend: device(%x)\n", (u32)conf->dev);
		return;
	}

	atomic_dec(&conf->mmu_enable);
	iovmm_deactivate(conf->dev);
	spin_unlock_irqrestore(&conf->slock, flags);

}

void vb2_ion_resume(void *alloc_ctx)
{
	struct vb2_ion_conf *conf = alloc_ctx;
	int ret;
	unsigned long flags;

	if (!conf->use_mmu)
		return;

	spin_lock_irqsave(&conf->slock, flags);
	if (atomic_read(&conf->mmu_enable)) {
		pr_warning("Already resume: device(%x)\n", (u32)conf->dev);
		return;
	}

	atomic_inc(&conf->mmu_enable);
	ret = iovmm_activate(conf->dev);
	if (ret) {
		pr_err("iovmm_activate: dev(%x)\n", (u32)conf->dev);
		atomic_dec(&conf->mmu_enable);
	}
	spin_unlock_irqrestore(&conf->slock, flags);
}

MODULE_AUTHOR("Jonghun,	Han <jonghun.han@samsung.com>");
MODULE_DESCRIPTION("Android ION allocator handling routines for videobuf2");
MODULE_LICENSE("GPL");
