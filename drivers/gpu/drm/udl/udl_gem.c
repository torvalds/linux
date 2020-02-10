// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Red Hat
 */

#include <linux/dma-buf.h>
#include <linux/vmalloc.h>

#include <drm/drm_drv.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_mode.h>
#include <drm/drm_prime.h>

#include "udl_drv.h"

/*
 * GEM object funcs
 */

static int udl_gem_object_mmap(struct drm_gem_object *obj,
			       struct vm_area_struct *vma)
{
	int ret;

	ret = drm_gem_shmem_mmap(obj, vma);
	if (ret)
		return ret;

	vma->vm_page_prot = vm_get_page_prot(vma->vm_flags);
	if (obj->import_attach)
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	vma->vm_page_prot = pgprot_decrypted(vma->vm_page_prot);

	return 0;
}

static void *udl_gem_object_vmap(struct drm_gem_object *obj)
{
	struct drm_gem_shmem_object *shmem = to_drm_gem_shmem_obj(obj);
	int ret;

	ret = mutex_lock_interruptible(&shmem->vmap_lock);
	if (ret)
		return ERR_PTR(ret);

	if (shmem->vmap_use_count++ > 0)
		goto out;

	ret = drm_gem_shmem_get_pages(shmem);
	if (ret)
		goto err_zero_use;

	if (obj->import_attach)
		shmem->vaddr = dma_buf_vmap(obj->import_attach->dmabuf);
	else
		shmem->vaddr = vmap(shmem->pages, obj->size >> PAGE_SHIFT,
				    VM_MAP, PAGE_KERNEL);

	if (!shmem->vaddr) {
		DRM_DEBUG_KMS("Failed to vmap pages\n");
		ret = -ENOMEM;
		goto err_put_pages;
	}

out:
	mutex_unlock(&shmem->vmap_lock);
	return shmem->vaddr;

err_put_pages:
	drm_gem_shmem_put_pages(shmem);
err_zero_use:
	shmem->vmap_use_count = 0;
	mutex_unlock(&shmem->vmap_lock);
	return ERR_PTR(ret);
}

static const struct drm_gem_object_funcs udl_gem_object_funcs = {
	.free = drm_gem_shmem_free_object,
	.print_info = drm_gem_shmem_print_info,
	.pin = drm_gem_shmem_pin,
	.unpin = drm_gem_shmem_unpin,
	.get_sg_table = drm_gem_shmem_get_sg_table,
	.vmap = udl_gem_object_vmap,
	.vunmap = drm_gem_shmem_vunmap,
	.mmap = udl_gem_object_mmap,
};

/*
 * Helpers for struct drm_driver
 */

struct drm_gem_object *udl_driver_gem_create_object(struct drm_device *dev,
						    size_t size)
{
	struct drm_gem_shmem_object *shmem;
	struct drm_gem_object *obj;

	shmem = kzalloc(sizeof(*shmem), GFP_KERNEL);
	if (!shmem)
		return NULL;

	obj = &shmem->base;
	obj->funcs = &udl_gem_object_funcs;

	return obj;
}
