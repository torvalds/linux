/*
 * Copyright (C) 2012 Red Hat
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License v2. See the file COPYING in the main directory of this archive for
 * more details.
 */

#include "drmP.h"
#include "udl_drv.h"
#include <linux/shmem_fs.h>
#include <linux/dma-buf.h>

struct udl_gem_object *udl_gem_alloc_object(struct drm_device *dev,
					    size_t size)
{
	struct udl_gem_object *obj;

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (obj == NULL)
		return NULL;

	if (drm_gem_object_init(dev, &obj->base, size) != 0) {
		kfree(obj);
		return NULL;
	}

	return obj;
}

static int
udl_gem_create(struct drm_file *file,
	       struct drm_device *dev,
	       uint64_t size,
	       uint32_t *handle_p)
{
	struct udl_gem_object *obj;
	int ret;
	u32 handle;

	size = roundup(size, PAGE_SIZE);

	obj = udl_gem_alloc_object(dev, size);
	if (obj == NULL)
		return -ENOMEM;

	ret = drm_gem_handle_create(file, &obj->base, &handle);
	if (ret) {
		drm_gem_object_release(&obj->base);
		kfree(obj);
		return ret;
	}

	drm_gem_object_unreference(&obj->base);
	*handle_p = handle;
	return 0;
}

int udl_dumb_create(struct drm_file *file,
		    struct drm_device *dev,
		    struct drm_mode_create_dumb *args)
{
	args->pitch = args->width * ((args->bpp + 1) / 8);
	args->size = args->pitch * args->height;
	return udl_gem_create(file, dev,
			      args->size, &args->handle);
}

int udl_dumb_destroy(struct drm_file *file, struct drm_device *dev,
		     uint32_t handle)
{
	return drm_gem_handle_delete(file, handle);
}

int udl_drm_gem_mmap(struct file *filp, struct vm_area_struct *vma)
{
	int ret;

	ret = drm_gem_mmap(filp, vma);
	if (ret)
		return ret;

	vma->vm_flags &= ~VM_PFNMAP;
	vma->vm_flags |= VM_MIXEDMAP;

	return ret;
}

int udl_gem_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct udl_gem_object *obj = to_udl_bo(vma->vm_private_data);
	struct page *page;
	unsigned int page_offset;
	int ret = 0;

	page_offset = ((unsigned long)vmf->virtual_address - vma->vm_start) >>
		PAGE_SHIFT;

	if (!obj->pages)
		return VM_FAULT_SIGBUS;

	page = obj->pages[page_offset];
	ret = vm_insert_page(vma, (unsigned long)vmf->virtual_address, page);
	switch (ret) {
	case -EAGAIN:
		set_need_resched();
	case 0:
	case -ERESTARTSYS:
		return VM_FAULT_NOPAGE;
	case -ENOMEM:
		return VM_FAULT_OOM;
	default:
		return VM_FAULT_SIGBUS;
	}
}

int udl_gem_init_object(struct drm_gem_object *obj)
{
	BUG();

	return 0;
}

static int udl_gem_get_pages(struct udl_gem_object *obj, gfp_t gfpmask)
{
	int page_count, i;
	struct page *page;
	struct inode *inode;
	struct address_space *mapping;

	if (obj->pages)
		return 0;

	page_count = obj->base.size / PAGE_SIZE;
	BUG_ON(obj->pages != NULL);
	obj->pages = drm_malloc_ab(page_count, sizeof(struct page *));
	if (obj->pages == NULL)
		return -ENOMEM;

	inode = obj->base.filp->f_path.dentry->d_inode;
	mapping = inode->i_mapping;
	gfpmask |= mapping_gfp_mask(mapping);

	for (i = 0; i < page_count; i++) {
		page = shmem_read_mapping_page_gfp(mapping, i, gfpmask);
		if (IS_ERR(page))
			goto err_pages;
		obj->pages[i] = page;
	}

	return 0;
err_pages:
	while (i--)
		page_cache_release(obj->pages[i]);
	drm_free_large(obj->pages);
	obj->pages = NULL;
	return PTR_ERR(page);
}

static void udl_gem_put_pages(struct udl_gem_object *obj)
{
	int page_count = obj->base.size / PAGE_SIZE;
	int i;

	if (obj->base.import_attach) {
		drm_free_large(obj->pages);
		obj->pages = NULL;
		return;
	}

	for (i = 0; i < page_count; i++)
		page_cache_release(obj->pages[i]);

	drm_free_large(obj->pages);
	obj->pages = NULL;
}

int udl_gem_vmap(struct udl_gem_object *obj)
{
	int page_count = obj->base.size / PAGE_SIZE;
	int ret;

	ret = udl_gem_get_pages(obj, GFP_KERNEL);
	if (ret)
		return ret;

	obj->vmapping = vmap(obj->pages, page_count, 0, PAGE_KERNEL);
	if (!obj->vmapping)
		return -ENOMEM;
	return 0;
}

void udl_gem_vunmap(struct udl_gem_object *obj)
{
	if (obj->vmapping)
		vunmap(obj->vmapping);

	udl_gem_put_pages(obj);
}

void udl_gem_free_object(struct drm_gem_object *gem_obj)
{
	struct udl_gem_object *obj = to_udl_bo(gem_obj);

	if (gem_obj->import_attach)
		drm_prime_gem_destroy(gem_obj, obj->sg);

	if (obj->vmapping)
		udl_gem_vunmap(obj);

	if (obj->pages)
		udl_gem_put_pages(obj);

	if (gem_obj->map_list.map)
		drm_gem_free_mmap_offset(gem_obj);
}

/* the dumb interface doesn't work with the GEM straight MMAP
   interface, it expects to do MMAP on the drm fd, like normal */
int udl_gem_mmap(struct drm_file *file, struct drm_device *dev,
		 uint32_t handle, uint64_t *offset)
{
	struct udl_gem_object *gobj;
	struct drm_gem_object *obj;
	int ret = 0;

	mutex_lock(&dev->struct_mutex);
	obj = drm_gem_object_lookup(dev, file, handle);
	if (obj == NULL) {
		ret = -ENOENT;
		goto unlock;
	}
	gobj = to_udl_bo(obj);

	ret = udl_gem_get_pages(gobj, GFP_KERNEL);
	if (ret)
		return ret;
	if (!gobj->base.map_list.map) {
		ret = drm_gem_create_mmap_offset(obj);
		if (ret)
			goto out;
	}

	*offset = (u64)gobj->base.map_list.hash.key << PAGE_SHIFT;

out:
	drm_gem_object_unreference(&gobj->base);
unlock:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

static int udl_prime_create(struct drm_device *dev,
			    size_t size,
			    struct sg_table *sg,
			    struct udl_gem_object **obj_p)
{
	struct udl_gem_object *obj;
	int npages;
	int i;
	struct scatterlist *iter;

	npages = size / PAGE_SIZE;

	*obj_p = NULL;
	obj = udl_gem_alloc_object(dev, npages * PAGE_SIZE);
	if (!obj)
		return -ENOMEM;

	obj->sg = sg;
	obj->pages = drm_malloc_ab(npages, sizeof(struct page *));
	if (obj->pages == NULL) {
		DRM_ERROR("obj pages is NULL %d\n", npages);
		return -ENOMEM;
	}

	drm_prime_sg_to_page_addr_arrays(sg, obj->pages, NULL, npages);

	*obj_p = obj;
	return 0;
}

struct drm_gem_object *udl_gem_prime_import(struct drm_device *dev,
				struct dma_buf *dma_buf)
{
	struct dma_buf_attachment *attach;
	struct sg_table *sg;
	struct udl_gem_object *uobj;
	int ret;

	/* need to attach */
	attach = dma_buf_attach(dma_buf, dev->dev);
	if (IS_ERR(attach))
		return ERR_PTR(PTR_ERR(attach));

	sg = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(sg)) {
		ret = PTR_ERR(sg);
		goto fail_detach;
	}

	ret = udl_prime_create(dev, dma_buf->size, sg, &uobj);
	if (ret) {
		goto fail_unmap;
	}

	uobj->base.import_attach = attach;

	return &uobj->base;

fail_unmap:
	dma_buf_unmap_attachment(attach, sg, DMA_BIDIRECTIONAL);
fail_detach:
	dma_buf_detach(dma_buf, attach);
	return ERR_PTR(ret);
}
