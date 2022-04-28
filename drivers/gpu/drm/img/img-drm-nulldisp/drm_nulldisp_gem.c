/*
 * @File
 * @Codingstyle LinuxKernel
 * @Copyright   Copyright (c) Imagination Technologies Ltd. All Rights Reserved
 * @License     Dual MIT/GPLv2
 *
 * The contents of this file are subject to the MIT license as set out below.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * Alternatively, the contents of this file may be used under the terms of
 * the GNU General Public License Version 2 ("GPL") in which case the provisions
 * of GPL are applicable instead of those above.
 *
 * If you wish to allow use of your version of this file only under the terms of
 * GPL, and not to allow others to use your version of this file under the terms
 * of the MIT license, indicate your decision by deleting the provisions above
 * and replace them with the notice and other provisions required by GPL as set
 * out in the file called "GPL-COPYING" included in this distribution. If you do
 * not delete the provisions above, a recipient may use your version of this file
 * under the terms of either the MIT license or GPL.
 *
 * This License is also included in this distribution in the file called
 * "MIT-COPYING".
 *
 * EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
 * PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 * PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/atomic.h>
#include <linux/mm_types.h>
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/mutex.h>
#include <linux/capability.h>

#include "pvr_dma_resv.h"
#include "drm_nulldisp_gem.h"
#include "nulldisp_drm.h"
#include "kernel_compatibility.h"
#include "riscv_vmap.h"

struct nulldisp_gem_object {
	struct drm_gem_object base;

	atomic_t pg_refcnt;
	struct page **pages;
	dma_addr_t *addrs;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 2, 0))
	struct dma_resv _resv;
#endif
	struct dma_resv *resv;

	bool cpu_prep;
	struct sg_table *import_sgt;
};

#define to_nulldisp_obj(obj) \
	container_of(obj, struct nulldisp_gem_object, base)

int nulldisp_gem_object_get_pages(struct drm_gem_object *obj)
{
	struct drm_device *dev = obj->dev;
	struct nulldisp_gem_object *nulldisp_obj = to_nulldisp_obj(obj);
	struct page **pages;
	int err;

	if (WARN_ON(obj->import_attach))
		return -EEXIST;

	WARN_ON(!mutex_is_locked(&dev->struct_mutex));

	if (atomic_inc_return(&nulldisp_obj->pg_refcnt) == 1) {
		unsigned int npages = obj->size >> PAGE_SHIFT;
		dma_addr_t *addrs;
		unsigned int i;

		pages = drm_gem_get_pages(obj);
		if (IS_ERR(pages)) {
			err = PTR_ERR(pages);
			goto dec_refcnt;
		}

		addrs = kmalloc_array(npages, sizeof(*addrs), GFP_KERNEL);
		if (!addrs) {
			err = -ENOMEM;
			goto free_pages;
		}

		for (i = 0; i < npages; i++) {
			addrs[i] = dma_map_page(dev->dev, pages[i],
						0, PAGE_SIZE,
						DMA_BIDIRECTIONAL);
		}

		nulldisp_obj->pages = pages;
		nulldisp_obj->addrs = addrs;
	}

	return 0;

free_pages:
	drm_gem_put_pages(obj, pages, false, false);
dec_refcnt:
	atomic_dec(&nulldisp_obj->pg_refcnt);
	return err;
}

static void nulldisp_gem_object_put_pages(struct drm_gem_object *obj)
{
	struct drm_device *dev = obj->dev;
	struct nulldisp_gem_object *nulldisp_obj = to_nulldisp_obj(obj);

	WARN_ON(!mutex_is_locked(&dev->struct_mutex));

	if (WARN_ON(atomic_read(&nulldisp_obj->pg_refcnt) == 0))
		return;

	if (atomic_dec_and_test(&nulldisp_obj->pg_refcnt)) {
		unsigned int npages = obj->size >> PAGE_SHIFT;
		unsigned int i;

		for (i = 0; i < npages; i++) {
			dma_unmap_page(dev->dev, nulldisp_obj->addrs[i],
				       PAGE_SIZE, DMA_BIDIRECTIONAL);
		}

		kfree(nulldisp_obj->addrs);
		nulldisp_obj->addrs = NULL;

		drm_gem_put_pages(obj, nulldisp_obj->pages, true, true);
		nulldisp_obj->pages = NULL;
	}
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0))
vm_fault_t nulldisp_gem_object_vm_fault(struct vm_fault *vmf)
#else
int nulldisp_gem_object_vm_fault(struct vm_area_struct *vma,
				 struct vm_fault *vmf)
#endif
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0))
	struct vm_area_struct *vma = vmf->vma;
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0))
	unsigned long addr = vmf->address;
#else
	unsigned long addr = (unsigned long)vmf->virtual_address;
#endif
	struct drm_gem_object *obj = vma->vm_private_data;
	struct nulldisp_gem_object *nulldisp_obj = to_nulldisp_obj(obj);
	unsigned long pg_off;
	struct page *page;

	/*
	 * nulldisp_gem_object_get_pages should have been called in
	 * nulldisp_gem_mmap so there's no need to do it here.
	 */
	if (WARN_ON(atomic_read(&nulldisp_obj->pg_refcnt) == 0))
		return VM_FAULT_SIGBUS;

	pg_off = (addr - vma->vm_start) >> PAGE_SHIFT;
	page = nulldisp_obj->pages[pg_off];

	get_page(page);
	vmf->page = page;

	return 0;
}

void nulldisp_gem_vm_open(struct vm_area_struct *vma)
{
	struct drm_gem_object *obj = vma->vm_private_data;

	drm_gem_vm_open(vma);

	if (!obj->import_attach) {
		struct drm_device *dev = obj->dev;

		mutex_lock(&dev->struct_mutex);
		(void) nulldisp_gem_object_get_pages(obj);
		mutex_unlock(&dev->struct_mutex);
	}
}

void nulldisp_gem_vm_close(struct vm_area_struct *vma)
{
	struct drm_gem_object *obj = vma->vm_private_data;

	if (!obj->import_attach) {
		struct drm_device *dev = obj->dev;

		mutex_lock(&dev->struct_mutex);
		(void) nulldisp_gem_object_put_pages(obj);
		mutex_unlock(&dev->struct_mutex);
	}

	drm_gem_vm_close(vma);
}

void nulldisp_gem_object_free(struct drm_gem_object *obj)
{
	struct nulldisp_gem_object *nulldisp_obj = to_nulldisp_obj(obj);

	WARN_ON(atomic_read(&nulldisp_obj->pg_refcnt) != 0);

	if (obj->import_attach) {
		kfree(nulldisp_obj->pages);
		kfree(nulldisp_obj->addrs);
		drm_gem_free_mmap_offset(obj);
		drm_prime_gem_destroy(obj, nulldisp_obj->import_sgt);
	} else {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 2, 0))
		dma_resv_fini(&nulldisp_obj->_resv);
#endif
		drm_gem_object_release(obj);
	}

	kfree(nulldisp_obj);
}

int nulldisp_gem_prime_pin(struct drm_gem_object *obj)
{
	struct drm_device *dev = obj->dev;
	int err;

	mutex_lock(&dev->struct_mutex);
	err = nulldisp_gem_object_get_pages(obj);
	mutex_unlock(&dev->struct_mutex);

	return err;
}

void nulldisp_gem_prime_unpin(struct drm_gem_object *obj)
{
	struct drm_device *dev = obj->dev;

	mutex_lock(&dev->struct_mutex);
	nulldisp_gem_object_put_pages(obj);
	mutex_unlock(&dev->struct_mutex);
}

struct sg_table *
nulldisp_gem_prime_get_sg_table(struct drm_gem_object *obj)
{
	struct nulldisp_gem_object *nulldisp_obj = to_nulldisp_obj(obj);
	int nr_pages = obj->size >> PAGE_SHIFT;

	/*
	 * nulldisp_gem_prime_pin should have been called in which case we don't
	 * need to call nulldisp_gem_object_get_pages.
	 */
	if (WARN_ON(atomic_read(&nulldisp_obj->pg_refcnt) == 0))
		return NULL;

	return drm_prime_pages_to_sg(obj->dev, nulldisp_obj->pages, nr_pages);
}

struct drm_gem_object *
nulldisp_gem_prime_import_sg_table(struct drm_device *dev,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0))
				   struct dma_buf_attachment *attach,
#else
				   size_t size,
#endif
				   struct sg_table *sgt)
{
	struct nulldisp_gem_object *nulldisp_obj;
	struct drm_gem_object *obj;
	struct page **pages;
	dma_addr_t *addrs;
	unsigned int npages;

	nulldisp_obj = kzalloc(sizeof(*nulldisp_obj), GFP_KERNEL);
	if (!nulldisp_obj)
		return NULL;

	nulldisp_obj->resv = attach->dmabuf->resv;

	obj = &nulldisp_obj->base;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 2, 0))
	obj->resv = nulldisp_obj->resv;
#endif

	drm_gem_private_object_init(dev, obj, attach->dmabuf->size);

	npages = obj->size >> PAGE_SHIFT;

	pages = kmalloc_array(npages, sizeof(*pages), GFP_KERNEL);
	addrs = kmalloc_array(npages, sizeof(*addrs), GFP_KERNEL);
	if (!pages || !addrs)
		goto exit_free_arrays;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 2, 0) && LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 0))
	if (drm_prime_sg_to_page_addr_arrays(sgt, pages, addrs, npages))
		goto exit_free_arrays;
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
	if (drm_prime_sg_to_page_array(sgt, pages, npages))
		goto exit_free_arrays;
	if (drm_prime_sg_to_dma_addr_array(sgt, addrs, npages))
		goto exit_free_arrays;
#endif

	nulldisp_obj->import_sgt = sgt;
	nulldisp_obj->pages = pages;
	nulldisp_obj->addrs = addrs;

	return obj;

exit_free_arrays:
	kfree(pages);
	kfree(addrs);
	drm_prime_gem_destroy(obj, sgt);
	kfree(nulldisp_obj);
	return NULL;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
struct dma_buf *nulldisp_gem_prime_export(
					  struct drm_device *dev,
					  struct drm_gem_object *obj,
					  int flags)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0))
	/* Read/write access required */
	flags |= O_RDWR;
#endif
	return drm_gem_prime_export(dev, obj, flags);
}
#endif

static void *nulldisp_gem_vmap(struct drm_gem_object *obj)
{
	struct nulldisp_gem_object *nulldisp_obj = to_nulldisp_obj(obj);
	int nr_pages = obj->size >> PAGE_SHIFT;

	/*
	 * nulldisp_gem_prime_pin should have been called in which case we don't
	 * need to call nulldisp_gem_object_get_pages.
	 */
	if (WARN_ON(atomic_read(&nulldisp_obj->pg_refcnt) == 0))
		return NULL;
	printk("nulldisp_gem_vmap called\n\n\n\n");
	dump_stack();
	return riscv_vmap(nulldisp_obj->pages, nr_pages, 0, PAGE_KERNEL);
	//return vmap(nulldisp_obj->pages, nr_pages, 0, PAGE_KERNEL);
}

static void nulldisp_gem_vunmap(struct drm_gem_object *obj, void *vaddr)
{
	vunmap(vaddr);
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 11, 0))
void *nulldisp_gem_prime_vmap(struct drm_gem_object *obj)
{
	return nulldisp_gem_vmap(obj);
}

void nulldisp_gem_prime_vunmap(struct drm_gem_object *obj, void *vaddr)
{
	nulldisp_gem_vunmap(obj, vaddr);
}
#else
int nulldisp_gem_prime_vmap(struct drm_gem_object *obj, struct dma_buf_map *map)
{
	void *vaddr = nulldisp_gem_vmap(obj);
	
	dma_buf_map_set_vaddr(map, vaddr);
	return (vaddr == NULL) ? -ENOMEM : 0;
}

void nulldisp_gem_prime_vunmap(struct drm_gem_object *obj, struct dma_buf_map *map)
{
	nulldisp_gem_vunmap(obj, map->vaddr);
	dma_buf_map_clear(map);
}
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(5, 11, 0) */

int nulldisp_gem_prime_mmap(struct drm_gem_object *obj,
			    struct vm_area_struct *vma)
{
	int err;

	mutex_lock(&obj->dev->struct_mutex);
	err = nulldisp_gem_object_get_pages(obj);
	if (!err)
		err = drm_gem_mmap_obj(obj, obj->size, vma);
	mutex_unlock(&obj->dev->struct_mutex);

	return err;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
struct dma_resv *
nulldisp_gem_prime_res_obj(struct drm_gem_object *obj)
{
	struct nulldisp_gem_object *nulldisp_obj = to_nulldisp_obj(obj);

	return nulldisp_obj->resv;
}
#endif

int nulldisp_gem_object_mmap_ioctl(struct drm_device *dev, void *data,
				   struct drm_file *file)
{
	struct drm_nulldisp_gem_mmap *args =
		(struct drm_nulldisp_gem_mmap *)data;

	if (args->pad) {
		DRM_ERROR("invalid pad (this should always be 0)\n");
		return -EINVAL;
	}

	if (args->offset) {
		DRM_ERROR("invalid offset (this should always be 0)\n");
		return -EINVAL;
	}

	return nulldisp_gem_dumb_map_offset(file, dev, args->handle,
					    &args->offset);
}

int nulldisp_gem_object_cpu_prep_ioctl(struct drm_device *dev, void *data,
				       struct drm_file *file)
{
	struct drm_nulldisp_gem_cpu_prep *args =
		(struct drm_nulldisp_gem_cpu_prep *)data;

	struct drm_gem_object *obj;
	struct nulldisp_gem_object *nulldisp_obj;
	bool write = !!(args->flags & NULLDISP_GEM_CPU_PREP_WRITE);
	bool wait = !(args->flags & NULLDISP_GEM_CPU_PREP_NOWAIT);
	int err;

	if (args->flags & ~(NULLDISP_GEM_CPU_PREP_READ |
			    NULLDISP_GEM_CPU_PREP_WRITE |
			    NULLDISP_GEM_CPU_PREP_NOWAIT)) {
		DRM_ERROR("invalid flags: %#08x\n", args->flags);
		return -EINVAL;
	}

	mutex_lock(&dev->struct_mutex);

	obj = drm_gem_object_lookup(file, args->handle);
	if (!obj) {
		err = -ENOENT;
		goto exit_unlock;
	}

	nulldisp_obj = to_nulldisp_obj(obj);

	if (nulldisp_obj->cpu_prep) {
		err = -EBUSY;
		goto exit_unref;
	}

	if (wait) {
		long lerr;

		lerr = dma_resv_wait_timeout(nulldisp_obj->resv,
						 write,
						 true,
						 30 * HZ);

		/* Remap return value (0 indicates busy state, > 0 success) */
		if (lerr > 0)
			err = 0;
		else if (!lerr)
			err = -EBUSY;
		else
			err = lerr;
	} else {
		/*
		 * Remap return value (false indicates busy state,
		 * true success).
		 */
		if (!dma_resv_test_signaled(nulldisp_obj->resv,
						write))
			err = -EBUSY;
		else
			err = 0;
	}

	if (!err)
		nulldisp_obj->cpu_prep = true;
exit_unref:
	drm_gem_object_put(obj);
exit_unlock:
	mutex_unlock(&dev->struct_mutex);
	return err;
}

int nulldisp_gem_object_cpu_fini_ioctl(struct drm_device *dev, void *data,
				       struct drm_file *file)
{
	struct drm_nulldisp_gem_cpu_fini *args =
		(struct drm_nulldisp_gem_cpu_fini *)data;

	struct drm_gem_object *obj;
	struct nulldisp_gem_object *nulldisp_obj;
	int err;

	if (args->pad) {
		DRM_ERROR("invalid pad (this should always be 0)\n");
		return -EINVAL;
	}

	mutex_lock(&dev->struct_mutex);

	obj = drm_gem_object_lookup(file, args->handle);
	if (!obj) {
		err = -ENOENT;
		goto exit_unlock;
	}

	nulldisp_obj = to_nulldisp_obj(obj);

	if (!nulldisp_obj->cpu_prep) {
		err = -EINVAL;
		goto exit_unref;
	}

	nulldisp_obj->cpu_prep = false;
	err = 0;
exit_unref:
	drm_gem_object_put(obj);
exit_unlock:
	mutex_unlock(&dev->struct_mutex);
	return err;
}

static int nulldisp_gem_object_create_priv(struct drm_file *file,
					   struct drm_device *dev,
					   u64 size,
					   u32 *handle)
{
	struct nulldisp_gem_object *nulldisp_obj;
	struct drm_gem_object *obj;
	struct address_space *mapping;
	int err;

	nulldisp_obj = kzalloc(sizeof(*nulldisp_obj), GFP_KERNEL);
	if (!nulldisp_obj)
		return -ENOMEM;

	obj = &nulldisp_obj->base;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0))
	obj->funcs = &nulldisp_gem_funcs;
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0) */

	err = drm_gem_object_init(dev, obj, size);
	if (err) {
		kfree(nulldisp_obj);
		return err;
	}

	mapping = file_inode(obj->filp)->i_mapping;
	mapping_set_gfp_mask(mapping, GFP_USER | __GFP_DMA32 | __GFP_NORETRY);

	err = drm_gem_handle_create(file, obj, handle);
	if (err)
		goto exit;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 2, 0))
	dma_resv_init(&nulldisp_obj->_resv);
	nulldisp_obj->resv = &nulldisp_obj->_resv;
#else
	nulldisp_obj->resv = nulldisp_obj->base.resv;
#endif

exit:
	drm_gem_object_put(obj);
	return err;
}

int nulldisp_gem_object_create_ioctl(struct drm_device *dev,
				     void *data,
				     struct drm_file *file)
{
	struct drm_nulldisp_gem_create *args = data;
	u32 handle;
	int err;
	u64 aligned_size;

	if (args->flags) {
		DRM_ERROR("invalid flags: %#08x\n", args->flags);
		return -EINVAL;
	}

	if (args->handle) {
		DRM_ERROR("invalid handle (this should always be 0)\n");
		return -EINVAL;
	}

	aligned_size = PAGE_ALIGN(args->size);

	err = nulldisp_gem_object_create_priv(file, dev, aligned_size, &handle);
	if (!err)
		args->handle = handle;

	return err;
}

int nulldisp_gem_dumb_create(struct drm_file *file,
			     struct drm_device *dev,
			     struct drm_mode_create_dumb *args)
{
	u32 handle;
	u32 pitch;
	size_t size;
	int err;

	pitch = args->width * (ALIGN(args->bpp, 8) >> 3);
	size = PAGE_ALIGN(pitch * args->height);

	err = nulldisp_gem_object_create_priv(file, dev, size, &handle);
	if (!err) {
		args->handle = handle;
		args->pitch = pitch;
		args->size = size;
	}

	return err;
}

int nulldisp_gem_dumb_map_offset(struct drm_file *file,
				 struct drm_device *dev,
				 uint32_t handle,
				 uint64_t *offset)
{
	struct drm_gem_object *obj;
	int err;

	mutex_lock(&dev->struct_mutex);

	obj = drm_gem_object_lookup(file, handle);
	if (!obj) {
		err = -ENOENT;
		goto exit_unlock;
	}

	err = drm_gem_create_mmap_offset(obj);
	if (err)
		goto exit_obj_unref;

	*offset = drm_vma_node_offset_addr(&obj->vma_node);

exit_obj_unref:
	drm_gem_object_put(obj);
exit_unlock:
	mutex_unlock(&dev->struct_mutex);
	return err;
}

struct dma_resv *nulldisp_gem_get_resv(struct drm_gem_object *obj)
{
	return (to_nulldisp_obj(obj)->resv);
}
