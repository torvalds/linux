// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Russell King
 */

#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/mman.h>
#include <linux/shmem_fs.h>

#include <drm/armada_drm.h>
#include <drm/drm_prime.h>

#include "armada_drm.h"
#include "armada_gem.h"
#include "armada_ioctlP.h"

static vm_fault_t armada_gem_vm_fault(struct vm_fault *vmf)
{
	struct drm_gem_object *gobj = vmf->vma->vm_private_data;
	struct armada_gem_object *obj = drm_to_armada_gem(gobj);
	unsigned long pfn = obj->phys_addr >> PAGE_SHIFT;

	pfn += (vmf->address - vmf->vma->vm_start) >> PAGE_SHIFT;
	return vmf_insert_pfn(vmf->vma, vmf->address, pfn);
}

const struct vm_operations_struct armada_gem_vm_ops = {
	.fault	= armada_gem_vm_fault,
	.open	= drm_gem_vm_open,
	.close	= drm_gem_vm_close,
};

static size_t roundup_gem_size(size_t size)
{
	return roundup(size, PAGE_SIZE);
}

void armada_gem_free_object(struct drm_gem_object *obj)
{
	struct armada_gem_object *dobj = drm_to_armada_gem(obj);
	struct armada_private *priv = drm_to_armada_dev(obj->dev);

	DRM_DEBUG_DRIVER("release obj %p\n", dobj);

	drm_gem_free_mmap_offset(&dobj->obj);

	might_lock(&priv->linear_lock);

	if (dobj->page) {
		/* page backed memory */
		unsigned int order = get_order(dobj->obj.size);
		__free_pages(dobj->page, order);
	} else if (dobj->linear) {
		/* linear backed memory */
		mutex_lock(&priv->linear_lock);
		drm_mm_remove_node(dobj->linear);
		mutex_unlock(&priv->linear_lock);
		kfree(dobj->linear);
		if (dobj->addr)
			iounmap(dobj->addr);
	}

	if (dobj->obj.import_attach) {
		/* We only ever display imported data */
		if (dobj->sgt)
			dma_buf_unmap_attachment(dobj->obj.import_attach,
						 dobj->sgt, DMA_TO_DEVICE);
		drm_prime_gem_destroy(&dobj->obj, NULL);
	}

	drm_gem_object_release(&dobj->obj);

	kfree(dobj);
}

int
armada_gem_linear_back(struct drm_device *dev, struct armada_gem_object *obj)
{
	struct armada_private *priv = drm_to_armada_dev(dev);
	size_t size = obj->obj.size;

	if (obj->page || obj->linear)
		return 0;

	/*
	 * If it is a small allocation (typically cursor, which will
	 * be 32x64 or 64x32 ARGB pixels) try to get it from the system.
	 * Framebuffers will never be this small (our minimum size for
	 * framebuffers is larger than this anyway.)  Such objects are
	 * only accessed by the CPU so we don't need any special handing
	 * here.
	 */
	if (size <= 8192) {
		unsigned int order = get_order(size);
		struct page *p = alloc_pages(GFP_KERNEL, order);

		if (p) {
			obj->addr = page_address(p);
			obj->phys_addr = page_to_phys(p);
			obj->page = p;

			memset(obj->addr, 0, PAGE_ALIGN(size));
		}
	}

	/*
	 * We could grab something from CMA if it's enabled, but that
	 * involves building in a problem:
	 *
	 * CMA's interface uses dma_alloc_coherent(), which provides us
	 * with an CPU virtual address and a device address.
	 *
	 * The CPU virtual address may be either an address in the kernel
	 * direct mapped region (for example, as it would be on x86) or
	 * it may be remapped into another part of kernel memory space
	 * (eg, as it would be on ARM.)  This means virt_to_phys() on the
	 * returned virtual address is invalid depending on the architecture
	 * implementation.
	 *
	 * The device address may also not be a physical address; it may
	 * be that there is some kind of remapping between the device and
	 * system RAM, which makes the use of the device address also
	 * unsafe to re-use as a physical address.
	 *
	 * This makes DRM usage of dma_alloc_coherent() in a generic way
	 * at best very questionable and unsafe.
	 */

	/* Otherwise, grab it from our linear allocation */
	if (!obj->page) {
		struct drm_mm_node *node;
		unsigned align = min_t(unsigned, size, SZ_2M);
		void __iomem *ptr;
		int ret;

		node = kzalloc(sizeof(*node), GFP_KERNEL);
		if (!node)
			return -ENOSPC;

		mutex_lock(&priv->linear_lock);
		ret = drm_mm_insert_node_generic(&priv->linear, node,
						 size, align, 0, 0);
		mutex_unlock(&priv->linear_lock);
		if (ret) {
			kfree(node);
			return ret;
		}

		obj->linear = node;

		/* Ensure that the memory we're returning is cleared. */
		ptr = ioremap_wc(obj->linear->start, size);
		if (!ptr) {
			mutex_lock(&priv->linear_lock);
			drm_mm_remove_node(obj->linear);
			mutex_unlock(&priv->linear_lock);
			kfree(obj->linear);
			obj->linear = NULL;
			return -ENOMEM;
		}

		memset_io(ptr, 0, size);
		iounmap(ptr);

		obj->phys_addr = obj->linear->start;
		obj->dev_addr = obj->linear->start;
		obj->mapped = true;
	}

	DRM_DEBUG_DRIVER("obj %p phys %#llx dev %#llx\n", obj,
			 (unsigned long long)obj->phys_addr,
			 (unsigned long long)obj->dev_addr);

	return 0;
}

void *
armada_gem_map_object(struct drm_device *dev, struct armada_gem_object *dobj)
{
	/* only linear objects need to be ioremap'd */
	if (!dobj->addr && dobj->linear)
		dobj->addr = ioremap_wc(dobj->phys_addr, dobj->obj.size);
	return dobj->addr;
}

struct armada_gem_object *
armada_gem_alloc_private_object(struct drm_device *dev, size_t size)
{
	struct armada_gem_object *obj;

	size = roundup_gem_size(size);

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (!obj)
		return NULL;

	drm_gem_private_object_init(dev, &obj->obj, size);

	DRM_DEBUG_DRIVER("alloc private obj %p size %zu\n", obj, size);

	return obj;
}

static struct armada_gem_object *armada_gem_alloc_object(struct drm_device *dev,
	size_t size)
{
	struct armada_gem_object *obj;
	struct address_space *mapping;

	size = roundup_gem_size(size);

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (!obj)
		return NULL;

	if (drm_gem_object_init(dev, &obj->obj, size)) {
		kfree(obj);
		return NULL;
	}

	mapping = obj->obj.filp->f_mapping;
	mapping_set_gfp_mask(mapping, GFP_HIGHUSER | __GFP_RECLAIMABLE);

	DRM_DEBUG_DRIVER("alloc obj %p size %zu\n", obj, size);

	return obj;
}

/* Dumb alloc support */
int armada_gem_dumb_create(struct drm_file *file, struct drm_device *dev,
	struct drm_mode_create_dumb *args)
{
	struct armada_gem_object *dobj;
	u32 handle;
	size_t size;
	int ret;

	args->pitch = armada_pitch(args->width, args->bpp);
	args->size = size = args->pitch * args->height;

	dobj = armada_gem_alloc_private_object(dev, size);
	if (dobj == NULL)
		return -ENOMEM;

	ret = armada_gem_linear_back(dev, dobj);
	if (ret)
		goto err;

	ret = drm_gem_handle_create(file, &dobj->obj, &handle);
	if (ret)
		goto err;

	args->handle = handle;

	/* drop reference from allocate - handle holds it now */
	DRM_DEBUG_DRIVER("obj %p size %zu handle %#x\n", dobj, size, handle);
 err:
	drm_gem_object_put(&dobj->obj);
	return ret;
}

/* Private driver gem ioctls */
int armada_gem_create_ioctl(struct drm_device *dev, void *data,
	struct drm_file *file)
{
	struct drm_armada_gem_create *args = data;
	struct armada_gem_object *dobj;
	size_t size;
	u32 handle;
	int ret;

	if (args->size == 0)
		return -ENOMEM;

	size = args->size;

	dobj = armada_gem_alloc_object(dev, size);
	if (dobj == NULL)
		return -ENOMEM;

	ret = drm_gem_handle_create(file, &dobj->obj, &handle);
	if (ret)
		goto err;

	args->handle = handle;

	/* drop reference from allocate - handle holds it now */
	DRM_DEBUG_DRIVER("obj %p size %zu handle %#x\n", dobj, size, handle);
 err:
	drm_gem_object_put(&dobj->obj);
	return ret;
}

/* Map a shmem-backed object into process memory space */
int armada_gem_mmap_ioctl(struct drm_device *dev, void *data,
	struct drm_file *file)
{
	struct drm_armada_gem_mmap *args = data;
	struct armada_gem_object *dobj;
	unsigned long addr;

	dobj = armada_gem_object_lookup(file, args->handle);
	if (dobj == NULL)
		return -ENOENT;

	if (!dobj->obj.filp) {
		drm_gem_object_put(&dobj->obj);
		return -EINVAL;
	}

	addr = vm_mmap(dobj->obj.filp, 0, args->size, PROT_READ | PROT_WRITE,
		       MAP_SHARED, args->offset);
	drm_gem_object_put(&dobj->obj);
	if (IS_ERR_VALUE(addr))
		return addr;

	args->addr = addr;

	return 0;
}

int armada_gem_pwrite_ioctl(struct drm_device *dev, void *data,
	struct drm_file *file)
{
	struct drm_armada_gem_pwrite *args = data;
	struct armada_gem_object *dobj;
	char __user *ptr;
	int ret;

	DRM_DEBUG_DRIVER("handle %u off %u size %u ptr 0x%llx\n",
		args->handle, args->offset, args->size, args->ptr);

	if (args->size == 0)
		return 0;

	ptr = (char __user *)(uintptr_t)args->ptr;

	if (!access_ok(ptr, args->size))
		return -EFAULT;

	ret = fault_in_pages_readable(ptr, args->size);
	if (ret)
		return ret;

	dobj = armada_gem_object_lookup(file, args->handle);
	if (dobj == NULL)
		return -ENOENT;

	/* Must be a kernel-mapped object */
	if (!dobj->addr)
		return -EINVAL;

	if (args->offset > dobj->obj.size ||
	    args->size > dobj->obj.size - args->offset) {
		DRM_ERROR("invalid size: object size %u\n", dobj->obj.size);
		ret = -EINVAL;
		goto unref;
	}

	if (copy_from_user(dobj->addr + args->offset, ptr, args->size)) {
		ret = -EFAULT;
	} else if (dobj->update) {
		dobj->update(dobj->update_data);
		ret = 0;
	}

 unref:
	drm_gem_object_put(&dobj->obj);
	return ret;
}

/* Prime support */
static struct sg_table *
armada_gem_prime_map_dma_buf(struct dma_buf_attachment *attach,
	enum dma_data_direction dir)
{
	struct drm_gem_object *obj = attach->dmabuf->priv;
	struct armada_gem_object *dobj = drm_to_armada_gem(obj);
	struct scatterlist *sg;
	struct sg_table *sgt;
	int i;

	sgt = kmalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt)
		return NULL;

	if (dobj->obj.filp) {
		struct address_space *mapping;
		int count;

		count = dobj->obj.size / PAGE_SIZE;
		if (sg_alloc_table(sgt, count, GFP_KERNEL))
			goto free_sgt;

		mapping = dobj->obj.filp->f_mapping;

		for_each_sgtable_sg(sgt, sg, i) {
			struct page *page;

			page = shmem_read_mapping_page(mapping, i);
			if (IS_ERR(page))
				goto release;

			sg_set_page(sg, page, PAGE_SIZE, 0);
		}

		if (dma_map_sgtable(attach->dev, sgt, dir, 0))
			goto release;
	} else if (dobj->page) {
		/* Single contiguous page */
		if (sg_alloc_table(sgt, 1, GFP_KERNEL))
			goto free_sgt;

		sg_set_page(sgt->sgl, dobj->page, dobj->obj.size, 0);

		if (dma_map_sgtable(attach->dev, sgt, dir, 0))
			goto free_table;
	} else if (dobj->linear) {
		/* Single contiguous physical region - no struct page */
		if (sg_alloc_table(sgt, 1, GFP_KERNEL))
			goto free_sgt;
		sg_dma_address(sgt->sgl) = dobj->dev_addr;
		sg_dma_len(sgt->sgl) = dobj->obj.size;
	} else {
		goto free_sgt;
	}
	return sgt;

 release:
	for_each_sgtable_sg(sgt, sg, i)
		if (sg_page(sg))
			put_page(sg_page(sg));
 free_table:
	sg_free_table(sgt);
 free_sgt:
	kfree(sgt);
	return NULL;
}

static void armada_gem_prime_unmap_dma_buf(struct dma_buf_attachment *attach,
	struct sg_table *sgt, enum dma_data_direction dir)
{
	struct drm_gem_object *obj = attach->dmabuf->priv;
	struct armada_gem_object *dobj = drm_to_armada_gem(obj);
	int i;

	if (!dobj->linear)
		dma_unmap_sgtable(attach->dev, sgt, dir, 0);

	if (dobj->obj.filp) {
		struct scatterlist *sg;

		for_each_sgtable_sg(sgt, sg, i)
			put_page(sg_page(sg));
	}

	sg_free_table(sgt);
	kfree(sgt);
}

static int
armada_gem_dmabuf_mmap(struct dma_buf *buf, struct vm_area_struct *vma)
{
	return -EINVAL;
}

static const struct dma_buf_ops armada_gem_prime_dmabuf_ops = {
	.map_dma_buf	= armada_gem_prime_map_dma_buf,
	.unmap_dma_buf	= armada_gem_prime_unmap_dma_buf,
	.release	= drm_gem_dmabuf_release,
	.mmap		= armada_gem_dmabuf_mmap,
};

struct dma_buf *
armada_gem_prime_export(struct drm_gem_object *obj, int flags)
{
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);

	exp_info.ops = &armada_gem_prime_dmabuf_ops;
	exp_info.size = obj->size;
	exp_info.flags = O_RDWR;
	exp_info.priv = obj;

	return drm_gem_dmabuf_export(obj->dev, &exp_info);
}

struct drm_gem_object *
armada_gem_prime_import(struct drm_device *dev, struct dma_buf *buf)
{
	struct dma_buf_attachment *attach;
	struct armada_gem_object *dobj;

	if (buf->ops == &armada_gem_prime_dmabuf_ops) {
		struct drm_gem_object *obj = buf->priv;
		if (obj->dev == dev) {
			/*
			 * Importing our own dmabuf(s) increases the
			 * refcount on the gem object itself.
			 */
			drm_gem_object_get(obj);
			return obj;
		}
	}

	attach = dma_buf_attach(buf, dev->dev);
	if (IS_ERR(attach))
		return ERR_CAST(attach);

	dobj = armada_gem_alloc_private_object(dev, buf->size);
	if (!dobj) {
		dma_buf_detach(buf, attach);
		return ERR_PTR(-ENOMEM);
	}

	dobj->obj.import_attach = attach;
	get_dma_buf(buf);

	/*
	 * Don't call dma_buf_map_attachment() here - it maps the
	 * scatterlist immediately for DMA, and this is not always
	 * an appropriate thing to do.
	 */
	return &dobj->obj;
}

int armada_gem_map_import(struct armada_gem_object *dobj)
{
	int ret;

	dobj->sgt = dma_buf_map_attachment(dobj->obj.import_attach,
					   DMA_TO_DEVICE);
	if (IS_ERR(dobj->sgt)) {
		ret = PTR_ERR(dobj->sgt);
		dobj->sgt = NULL;
		DRM_ERROR("dma_buf_map_attachment() error: %d\n", ret);
		return ret;
	}
	if (dobj->sgt->nents > 1) {
		DRM_ERROR("dma_buf_map_attachment() returned an (unsupported) scattered list\n");
		return -EINVAL;
	}
	if (sg_dma_len(dobj->sgt->sgl) < dobj->obj.size) {
		DRM_ERROR("dma_buf_map_attachment() returned a small buffer\n");
		return -EINVAL;
	}
	dobj->dev_addr = sg_dma_address(dobj->sgt->sgl);
	dobj->mapped = true;
	return 0;
}
