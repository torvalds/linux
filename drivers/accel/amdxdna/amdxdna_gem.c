// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024, Advanced Micro Devices, Inc.
 */

#include <drm/amdxdna_accel.h>
#include <drm/drm_cache.h>
#include <drm/drm_device.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/gpu_scheduler.h>
#include <linux/dma-buf.h>
#include <linux/dma-direct.h>
#include <linux/iosys-map.h>
#include <linux/pagemap.h>
#include <linux/vmalloc.h>

#include "amdxdna_ctx.h"
#include "amdxdna_gem.h"
#include "amdxdna_pci_drv.h"
#include "amdxdna_ubuf.h"

#define XDNA_MAX_CMD_BO_SIZE	SZ_32K

MODULE_IMPORT_NS("DMA_BUF");

static int
amdxdna_gem_heap_alloc(struct amdxdna_gem_obj *abo)
{
	struct amdxdna_client *client = abo->client;
	struct amdxdna_dev *xdna = client->xdna;
	struct amdxdna_mem *mem = &abo->mem;
	struct amdxdna_gem_obj *heap;
	u64 offset;
	u32 align;
	int ret;

	mutex_lock(&client->mm_lock);

	heap = client->dev_heap;
	if (!heap) {
		ret = -EINVAL;
		goto unlock_out;
	}

	if (heap->mem.userptr == AMDXDNA_INVALID_ADDR) {
		XDNA_ERR(xdna, "Invalid dev heap userptr");
		ret = -EINVAL;
		goto unlock_out;
	}

	if (mem->size == 0 || mem->size > heap->mem.size) {
		XDNA_ERR(xdna, "Invalid dev bo size 0x%lx, limit 0x%lx",
			 mem->size, heap->mem.size);
		ret = -EINVAL;
		goto unlock_out;
	}

	align = 1 << max(PAGE_SHIFT, xdna->dev_info->dev_mem_buf_shift);
	ret = drm_mm_insert_node_generic(&heap->mm, &abo->mm_node,
					 mem->size, align,
					 0, DRM_MM_INSERT_BEST);
	if (ret) {
		XDNA_ERR(xdna, "Failed to alloc dev bo memory, ret %d", ret);
		goto unlock_out;
	}

	mem->dev_addr = abo->mm_node.start;
	offset = mem->dev_addr - heap->mem.dev_addr;
	mem->userptr = heap->mem.userptr + offset;
	mem->kva = heap->mem.kva + offset;

	drm_gem_object_get(to_gobj(heap));

unlock_out:
	mutex_unlock(&client->mm_lock);

	return ret;
}

static void
amdxdna_gem_destroy_obj(struct amdxdna_gem_obj *abo)
{
	mutex_destroy(&abo->lock);
	kfree(abo);
}

static void
amdxdna_gem_heap_free(struct amdxdna_gem_obj *abo)
{
	struct amdxdna_gem_obj *heap;

	mutex_lock(&abo->client->mm_lock);

	drm_mm_remove_node(&abo->mm_node);

	heap = abo->client->dev_heap;
	drm_gem_object_put(to_gobj(heap));

	mutex_unlock(&abo->client->mm_lock);
}

static bool amdxdna_hmm_invalidate(struct mmu_interval_notifier *mni,
				   const struct mmu_notifier_range *range,
				   unsigned long cur_seq)
{
	struct amdxdna_umap *mapp = container_of(mni, struct amdxdna_umap, notifier);
	struct amdxdna_gem_obj *abo = mapp->abo;
	struct amdxdna_dev *xdna;

	xdna = to_xdna_dev(to_gobj(abo)->dev);
	XDNA_DBG(xdna, "Invalidating range 0x%lx, 0x%lx, type %d",
		 mapp->vma->vm_start, mapp->vma->vm_end, abo->type);

	if (!mmu_notifier_range_blockable(range))
		return false;

	down_write(&xdna->notifier_lock);
	abo->mem.map_invalid = true;
	mapp->invalid = true;
	mmu_interval_set_seq(&mapp->notifier, cur_seq);
	up_write(&xdna->notifier_lock);

	xdna->dev_info->ops->hmm_invalidate(abo, cur_seq);

	if (range->event == MMU_NOTIFY_UNMAP) {
		down_write(&xdna->notifier_lock);
		if (!mapp->unmapped) {
			queue_work(xdna->notifier_wq, &mapp->hmm_unreg_work);
			mapp->unmapped = true;
		}
		up_write(&xdna->notifier_lock);
	}

	return true;
}

static const struct mmu_interval_notifier_ops amdxdna_hmm_ops = {
	.invalidate = amdxdna_hmm_invalidate,
};

static void amdxdna_hmm_unregister(struct amdxdna_gem_obj *abo,
				   struct vm_area_struct *vma)
{
	struct amdxdna_dev *xdna = to_xdna_dev(to_gobj(abo)->dev);
	struct amdxdna_umap *mapp;

	down_read(&xdna->notifier_lock);
	list_for_each_entry(mapp, &abo->mem.umap_list, node) {
		if (!vma || mapp->vma == vma) {
			if (!mapp->unmapped) {
				queue_work(xdna->notifier_wq, &mapp->hmm_unreg_work);
				mapp->unmapped = true;
			}
			if (vma)
				break;
		}
	}
	up_read(&xdna->notifier_lock);
}

static void amdxdna_umap_release(struct kref *ref)
{
	struct amdxdna_umap *mapp = container_of(ref, struct amdxdna_umap, refcnt);
	struct vm_area_struct *vma = mapp->vma;
	struct amdxdna_dev *xdna;

	mmu_interval_notifier_remove(&mapp->notifier);
	if (is_import_bo(mapp->abo) && vma->vm_file && vma->vm_file->f_mapping)
		mapping_clear_unevictable(vma->vm_file->f_mapping);

	xdna = to_xdna_dev(to_gobj(mapp->abo)->dev);
	down_write(&xdna->notifier_lock);
	list_del(&mapp->node);
	up_write(&xdna->notifier_lock);

	kvfree(mapp->range.hmm_pfns);
	kfree(mapp);
}

void amdxdna_umap_put(struct amdxdna_umap *mapp)
{
	kref_put(&mapp->refcnt, amdxdna_umap_release);
}

static void amdxdna_hmm_unreg_work(struct work_struct *work)
{
	struct amdxdna_umap *mapp = container_of(work, struct amdxdna_umap,
						 hmm_unreg_work);

	amdxdna_umap_put(mapp);
}

static int amdxdna_hmm_register(struct amdxdna_gem_obj *abo,
				struct vm_area_struct *vma)
{
	struct amdxdna_dev *xdna = to_xdna_dev(to_gobj(abo)->dev);
	unsigned long len = vma->vm_end - vma->vm_start;
	unsigned long addr = vma->vm_start;
	struct amdxdna_umap *mapp;
	u32 nr_pages;
	int ret;

	if (!xdna->dev_info->ops->hmm_invalidate)
		return 0;

	mapp = kzalloc(sizeof(*mapp), GFP_KERNEL);
	if (!mapp)
		return -ENOMEM;

	nr_pages = (PAGE_ALIGN(addr + len) - (addr & PAGE_MASK)) >> PAGE_SHIFT;
	mapp->range.hmm_pfns = kvcalloc(nr_pages, sizeof(*mapp->range.hmm_pfns),
					GFP_KERNEL);
	if (!mapp->range.hmm_pfns) {
		ret = -ENOMEM;
		goto free_map;
	}

	ret = mmu_interval_notifier_insert_locked(&mapp->notifier,
						  current->mm,
						  addr,
						  len,
						  &amdxdna_hmm_ops);
	if (ret) {
		XDNA_ERR(xdna, "Insert mmu notifier failed, ret %d", ret);
		goto free_pfns;
	}

	mapp->range.notifier = &mapp->notifier;
	mapp->range.start = vma->vm_start;
	mapp->range.end = vma->vm_end;
	mapp->range.default_flags = HMM_PFN_REQ_FAULT;
	mapp->vma = vma;
	mapp->abo = abo;
	kref_init(&mapp->refcnt);

	if (abo->mem.userptr == AMDXDNA_INVALID_ADDR)
		abo->mem.userptr = addr;
	INIT_WORK(&mapp->hmm_unreg_work, amdxdna_hmm_unreg_work);
	if (is_import_bo(abo) && vma->vm_file && vma->vm_file->f_mapping)
		mapping_set_unevictable(vma->vm_file->f_mapping);

	down_write(&xdna->notifier_lock);
	list_add_tail(&mapp->node, &abo->mem.umap_list);
	up_write(&xdna->notifier_lock);

	return 0;

free_pfns:
	kvfree(mapp->range.hmm_pfns);
free_map:
	kfree(mapp);
	return ret;
}

static void amdxdna_gem_dev_obj_free(struct drm_gem_object *gobj)
{
	struct amdxdna_dev *xdna = to_xdna_dev(gobj->dev);
	struct amdxdna_gem_obj *abo = to_xdna_obj(gobj);

	XDNA_DBG(xdna, "BO type %d xdna_addr 0x%llx", abo->type, abo->mem.dev_addr);
	if (abo->pinned)
		amdxdna_gem_unpin(abo);

	amdxdna_gem_heap_free(abo);
	drm_gem_object_release(gobj);
	amdxdna_gem_destroy_obj(abo);
}

static int amdxdna_insert_pages(struct amdxdna_gem_obj *abo,
				struct vm_area_struct *vma)
{
	struct amdxdna_dev *xdna = to_xdna_dev(to_gobj(abo)->dev);
	unsigned long num_pages = vma_pages(vma);
	unsigned long offset = 0;
	int ret;

	if (!is_import_bo(abo)) {
		ret = drm_gem_shmem_mmap(&abo->base, vma);
		if (ret) {
			XDNA_ERR(xdna, "Failed shmem mmap %d", ret);
			return ret;
		}

		/* The buffer is based on memory pages. Fix the flag. */
		vm_flags_mod(vma, VM_MIXEDMAP, VM_PFNMAP);
		ret = vm_insert_pages(vma, vma->vm_start, abo->base.pages,
				      &num_pages);
		if (ret) {
			XDNA_ERR(xdna, "Failed insert pages %d", ret);
			vma->vm_ops->close(vma);
			return ret;
		}

		return 0;
	}

	vma->vm_private_data = NULL;
	vma->vm_ops = NULL;
	ret = dma_buf_mmap(abo->dma_buf, vma, 0);
	if (ret) {
		XDNA_ERR(xdna, "Failed to mmap dma buf %d", ret);
		return ret;
	}

	do {
		vm_fault_t fault_ret;

		fault_ret = handle_mm_fault(vma, vma->vm_start + offset,
					    FAULT_FLAG_WRITE, NULL);
		if (fault_ret & VM_FAULT_ERROR) {
			vma->vm_ops->close(vma);
			XDNA_ERR(xdna, "Fault in page failed");
			return -EFAULT;
		}

		offset += PAGE_SIZE;
	} while (--num_pages);

	/* Drop the reference drm_gem_mmap_obj() acquired.*/
	drm_gem_object_put(to_gobj(abo));

	return 0;
}

static int amdxdna_gem_obj_mmap(struct drm_gem_object *gobj,
				struct vm_area_struct *vma)
{
	struct amdxdna_dev *xdna = to_xdna_dev(gobj->dev);
	struct amdxdna_gem_obj *abo = to_xdna_obj(gobj);
	int ret;

	ret = amdxdna_hmm_register(abo, vma);
	if (ret)
		return ret;

	ret = amdxdna_insert_pages(abo, vma);
	if (ret) {
		XDNA_ERR(xdna, "Failed insert pages, ret %d", ret);
		goto hmm_unreg;
	}

	XDNA_DBG(xdna, "BO map_offset 0x%llx type %d userptr 0x%lx size 0x%lx",
		 drm_vma_node_offset_addr(&gobj->vma_node), abo->type,
		 vma->vm_start, gobj->size);
	return 0;

hmm_unreg:
	amdxdna_hmm_unregister(abo, vma);
	return ret;
}

static int amdxdna_gem_dmabuf_mmap(struct dma_buf *dma_buf, struct vm_area_struct *vma)
{
	struct drm_gem_object *gobj = dma_buf->priv;
	struct amdxdna_gem_obj *abo = to_xdna_obj(gobj);
	unsigned long num_pages = vma_pages(vma);
	int ret;

	vma->vm_ops = &drm_gem_shmem_vm_ops;
	vma->vm_private_data = gobj;

	drm_gem_object_get(gobj);
	ret = drm_gem_shmem_mmap(&abo->base, vma);
	if (ret)
		goto put_obj;

	/* The buffer is based on memory pages. Fix the flag. */
	vm_flags_mod(vma, VM_MIXEDMAP, VM_PFNMAP);
	ret = vm_insert_pages(vma, vma->vm_start, abo->base.pages,
			      &num_pages);
	if (ret)
		goto close_vma;

	return 0;

close_vma:
	vma->vm_ops->close(vma);
put_obj:
	drm_gem_object_put(gobj);
	return ret;
}

static const struct dma_buf_ops amdxdna_dmabuf_ops = {
	.attach = drm_gem_map_attach,
	.detach = drm_gem_map_detach,
	.map_dma_buf = drm_gem_map_dma_buf,
	.unmap_dma_buf = drm_gem_unmap_dma_buf,
	.release = drm_gem_dmabuf_release,
	.mmap = amdxdna_gem_dmabuf_mmap,
	.vmap = drm_gem_dmabuf_vmap,
	.vunmap = drm_gem_dmabuf_vunmap,
};

static int amdxdna_gem_obj_vmap(struct drm_gem_object *obj, struct iosys_map *map)
{
	struct amdxdna_gem_obj *abo = to_xdna_obj(obj);

	iosys_map_clear(map);

	dma_resv_assert_held(obj->resv);

	if (is_import_bo(abo))
		dma_buf_vmap(abo->dma_buf, map);
	else
		drm_gem_shmem_object_vmap(obj, map);

	if (!map->vaddr)
		return -ENOMEM;

	return 0;
}

static void amdxdna_gem_obj_vunmap(struct drm_gem_object *obj, struct iosys_map *map)
{
	struct amdxdna_gem_obj *abo = to_xdna_obj(obj);

	dma_resv_assert_held(obj->resv);

	if (is_import_bo(abo))
		dma_buf_vunmap(abo->dma_buf, map);
	else
		drm_gem_shmem_object_vunmap(obj, map);
}

static struct dma_buf *amdxdna_gem_prime_export(struct drm_gem_object *gobj, int flags)
{
	struct amdxdna_gem_obj *abo = to_xdna_obj(gobj);
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);

	if (abo->dma_buf) {
		get_dma_buf(abo->dma_buf);
		return abo->dma_buf;
	}

	exp_info.ops = &amdxdna_dmabuf_ops;
	exp_info.size = gobj->size;
	exp_info.flags = flags;
	exp_info.priv = gobj;
	exp_info.resv = gobj->resv;

	return drm_gem_dmabuf_export(gobj->dev, &exp_info);
}

static void amdxdna_imported_obj_free(struct amdxdna_gem_obj *abo)
{
	dma_buf_unmap_attachment_unlocked(abo->attach, abo->base.sgt, DMA_BIDIRECTIONAL);
	dma_buf_detach(abo->dma_buf, abo->attach);
	dma_buf_put(abo->dma_buf);
	drm_gem_object_release(to_gobj(abo));
	kfree(abo);
}

static void amdxdna_gem_obj_free(struct drm_gem_object *gobj)
{
	struct amdxdna_dev *xdna = to_xdna_dev(gobj->dev);
	struct amdxdna_gem_obj *abo = to_xdna_obj(gobj);
	struct iosys_map map = IOSYS_MAP_INIT_VADDR(abo->mem.kva);

	XDNA_DBG(xdna, "BO type %d xdna_addr 0x%llx", abo->type, abo->mem.dev_addr);

	amdxdna_hmm_unregister(abo, NULL);
	flush_workqueue(xdna->notifier_wq);

	if (abo->pinned)
		amdxdna_gem_unpin(abo);

	if (abo->type == AMDXDNA_BO_DEV_HEAP)
		drm_mm_takedown(&abo->mm);

	drm_gem_vunmap(gobj, &map);
	mutex_destroy(&abo->lock);

	if (is_import_bo(abo)) {
		amdxdna_imported_obj_free(abo);
		return;
	}

	drm_gem_shmem_free(&abo->base);
}

static const struct drm_gem_object_funcs amdxdna_gem_dev_obj_funcs = {
	.free = amdxdna_gem_dev_obj_free,
};

static const struct drm_gem_object_funcs amdxdna_gem_shmem_funcs = {
	.free = amdxdna_gem_obj_free,
	.print_info = drm_gem_shmem_object_print_info,
	.pin = drm_gem_shmem_object_pin,
	.unpin = drm_gem_shmem_object_unpin,
	.get_sg_table = drm_gem_shmem_object_get_sg_table,
	.vmap = amdxdna_gem_obj_vmap,
	.vunmap = amdxdna_gem_obj_vunmap,
	.mmap = amdxdna_gem_obj_mmap,
	.vm_ops = &drm_gem_shmem_vm_ops,
	.export = amdxdna_gem_prime_export,
};

static struct amdxdna_gem_obj *
amdxdna_gem_create_obj(struct drm_device *dev, size_t size)
{
	struct amdxdna_gem_obj *abo;

	abo = kzalloc(sizeof(*abo), GFP_KERNEL);
	if (!abo)
		return ERR_PTR(-ENOMEM);

	abo->pinned = false;
	abo->assigned_hwctx = AMDXDNA_INVALID_CTX_HANDLE;
	mutex_init(&abo->lock);

	abo->mem.userptr = AMDXDNA_INVALID_ADDR;
	abo->mem.dev_addr = AMDXDNA_INVALID_ADDR;
	abo->mem.size = size;
	INIT_LIST_HEAD(&abo->mem.umap_list);

	return abo;
}

/* For drm_driver->gem_create_object callback */
struct drm_gem_object *
amdxdna_gem_create_object_cb(struct drm_device *dev, size_t size)
{
	struct amdxdna_gem_obj *abo;

	abo = amdxdna_gem_create_obj(dev, size);
	if (IS_ERR(abo))
		return ERR_CAST(abo);

	to_gobj(abo)->funcs = &amdxdna_gem_shmem_funcs;

	return to_gobj(abo);
}

static struct amdxdna_gem_obj *
amdxdna_gem_create_shmem_object(struct drm_device *dev, size_t size)
{
	struct drm_gem_shmem_object *shmem = drm_gem_shmem_create(dev, size);

	if (IS_ERR(shmem))
		return ERR_CAST(shmem);

	shmem->map_wc = false;
	return to_xdna_obj(&shmem->base);
}

static struct amdxdna_gem_obj *
amdxdna_gem_create_ubuf_object(struct drm_device *dev, struct amdxdna_drm_create_bo *args)
{
	struct amdxdna_dev *xdna = to_xdna_dev(dev);
	enum amdxdna_ubuf_flag flags = 0;
	struct amdxdna_drm_va_tbl va_tbl;
	struct drm_gem_object *gobj;
	struct dma_buf *dma_buf;

	if (copy_from_user(&va_tbl, u64_to_user_ptr(args->vaddr), sizeof(va_tbl))) {
		XDNA_DBG(xdna, "Access va table failed");
		return ERR_PTR(-EINVAL);
	}

	if (va_tbl.num_entries) {
		if (args->type == AMDXDNA_BO_CMD)
			flags |= AMDXDNA_UBUF_FLAG_MAP_DMA;

		dma_buf = amdxdna_get_ubuf(dev, flags, va_tbl.num_entries,
					   u64_to_user_ptr(args->vaddr + sizeof(va_tbl)));
	} else {
		dma_buf = dma_buf_get(va_tbl.dmabuf_fd);
	}

	if (IS_ERR(dma_buf))
		return ERR_CAST(dma_buf);

	gobj = amdxdna_gem_prime_import(dev, dma_buf);
	if (IS_ERR(gobj)) {
		dma_buf_put(dma_buf);
		return ERR_CAST(gobj);
	}

	dma_buf_put(dma_buf);

	return to_xdna_obj(gobj);
}

static struct amdxdna_gem_obj *
amdxdna_gem_create_object(struct drm_device *dev,
			  struct amdxdna_drm_create_bo *args)
{
	size_t aligned_sz = PAGE_ALIGN(args->size);

	if (args->vaddr)
		return amdxdna_gem_create_ubuf_object(dev, args);

	return amdxdna_gem_create_shmem_object(dev, aligned_sz);
}

struct drm_gem_object *
amdxdna_gem_prime_import(struct drm_device *dev, struct dma_buf *dma_buf)
{
	struct dma_buf_attachment *attach;
	struct amdxdna_gem_obj *abo;
	struct drm_gem_object *gobj;
	struct sg_table *sgt;
	int ret;

	get_dma_buf(dma_buf);

	attach = dma_buf_attach(dma_buf, dev->dev);
	if (IS_ERR(attach)) {
		ret = PTR_ERR(attach);
		goto put_buf;
	}

	sgt = dma_buf_map_attachment_unlocked(attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(sgt)) {
		ret = PTR_ERR(sgt);
		goto fail_detach;
	}

	gobj = drm_gem_shmem_prime_import_sg_table(dev, attach, sgt);
	if (IS_ERR(gobj)) {
		ret = PTR_ERR(gobj);
		goto fail_unmap;
	}

	abo = to_xdna_obj(gobj);
	abo->attach = attach;
	abo->dma_buf = dma_buf;

	return gobj;

fail_unmap:
	dma_buf_unmap_attachment_unlocked(attach, sgt, DMA_BIDIRECTIONAL);
fail_detach:
	dma_buf_detach(dma_buf, attach);
put_buf:
	dma_buf_put(dma_buf);

	return ERR_PTR(ret);
}

static struct amdxdna_gem_obj *
amdxdna_drm_alloc_shmem(struct drm_device *dev,
			struct amdxdna_drm_create_bo *args,
			struct drm_file *filp)
{
	struct amdxdna_client *client = filp->driver_priv;
	struct amdxdna_gem_obj *abo;

	abo = amdxdna_gem_create_object(dev, args);
	if (IS_ERR(abo))
		return ERR_CAST(abo);

	abo->client = client;
	abo->type = AMDXDNA_BO_SHMEM;

	return abo;
}

static struct amdxdna_gem_obj *
amdxdna_drm_create_dev_heap(struct drm_device *dev,
			    struct amdxdna_drm_create_bo *args,
			    struct drm_file *filp)
{
	struct amdxdna_client *client = filp->driver_priv;
	struct iosys_map map = IOSYS_MAP_INIT_VADDR(NULL);
	struct amdxdna_dev *xdna = to_xdna_dev(dev);
	struct amdxdna_gem_obj *abo;
	int ret;

	if (args->size > xdna->dev_info->dev_mem_size) {
		XDNA_DBG(xdna, "Invalid dev heap size 0x%llx, limit 0x%lx",
			 args->size, xdna->dev_info->dev_mem_size);
		return ERR_PTR(-EINVAL);
	}

	mutex_lock(&client->mm_lock);
	if (client->dev_heap) {
		XDNA_DBG(client->xdna, "dev heap is already created");
		ret = -EBUSY;
		goto mm_unlock;
	}

	abo = amdxdna_gem_create_object(dev, args);
	if (IS_ERR(abo)) {
		ret = PTR_ERR(abo);
		goto mm_unlock;
	}

	abo->type = AMDXDNA_BO_DEV_HEAP;
	abo->client = client;
	abo->mem.dev_addr = client->xdna->dev_info->dev_mem_base;
	drm_mm_init(&abo->mm, abo->mem.dev_addr, abo->mem.size);

	ret = drm_gem_vmap(to_gobj(abo), &map);
	if (ret) {
		XDNA_ERR(xdna, "Vmap heap bo failed, ret %d", ret);
		goto release_obj;
	}
	abo->mem.kva = map.vaddr;

	client->dev_heap = abo;
	drm_gem_object_get(to_gobj(abo));
	mutex_unlock(&client->mm_lock);

	return abo;

release_obj:
	drm_gem_object_put(to_gobj(abo));
mm_unlock:
	mutex_unlock(&client->mm_lock);
	return ERR_PTR(ret);
}

struct amdxdna_gem_obj *
amdxdna_drm_alloc_dev_bo(struct drm_device *dev,
			 struct amdxdna_drm_create_bo *args,
			 struct drm_file *filp)
{
	struct amdxdna_client *client = filp->driver_priv;
	struct amdxdna_dev *xdna = to_xdna_dev(dev);
	size_t aligned_sz = PAGE_ALIGN(args->size);
	struct amdxdna_gem_obj *abo;
	int ret;

	abo = amdxdna_gem_create_obj(&xdna->ddev, aligned_sz);
	if (IS_ERR(abo))
		return abo;

	to_gobj(abo)->funcs = &amdxdna_gem_dev_obj_funcs;
	abo->type = AMDXDNA_BO_DEV;
	abo->client = client;

	ret = amdxdna_gem_heap_alloc(abo);
	if (ret) {
		XDNA_ERR(xdna, "Failed to alloc dev bo memory, ret %d", ret);
		amdxdna_gem_destroy_obj(abo);
		return ERR_PTR(ret);
	}

	drm_gem_private_object_init(&xdna->ddev, to_gobj(abo), aligned_sz);

	return abo;
}

static struct amdxdna_gem_obj *
amdxdna_drm_create_cmd_bo(struct drm_device *dev,
			  struct amdxdna_drm_create_bo *args,
			  struct drm_file *filp)
{
	struct iosys_map map = IOSYS_MAP_INIT_VADDR(NULL);
	struct amdxdna_dev *xdna = to_xdna_dev(dev);
	struct amdxdna_gem_obj *abo;
	int ret;

	if (args->size > XDNA_MAX_CMD_BO_SIZE) {
		XDNA_ERR(xdna, "Command bo size 0x%llx too large", args->size);
		return ERR_PTR(-EINVAL);
	}

	if (args->size < sizeof(struct amdxdna_cmd)) {
		XDNA_DBG(xdna, "Command BO size 0x%llx too small", args->size);
		return ERR_PTR(-EINVAL);
	}

	abo = amdxdna_gem_create_object(dev, args);
	if (IS_ERR(abo))
		return ERR_CAST(abo);

	abo->type = AMDXDNA_BO_CMD;
	abo->client = filp->driver_priv;

	ret = drm_gem_vmap(to_gobj(abo), &map);
	if (ret) {
		XDNA_ERR(xdna, "Vmap cmd bo failed, ret %d", ret);
		goto release_obj;
	}
	abo->mem.kva = map.vaddr;

	return abo;

release_obj:
	drm_gem_object_put(to_gobj(abo));
	return ERR_PTR(ret);
}

int amdxdna_drm_create_bo_ioctl(struct drm_device *dev, void *data, struct drm_file *filp)
{
	struct amdxdna_dev *xdna = to_xdna_dev(dev);
	struct amdxdna_drm_create_bo *args = data;
	struct amdxdna_gem_obj *abo;
	int ret;

	if (args->flags)
		return -EINVAL;

	XDNA_DBG(xdna, "BO arg type %d vaddr 0x%llx size 0x%llx flags 0x%llx",
		 args->type, args->vaddr, args->size, args->flags);
	switch (args->type) {
	case AMDXDNA_BO_SHMEM:
		abo = amdxdna_drm_alloc_shmem(dev, args, filp);
		break;
	case AMDXDNA_BO_DEV_HEAP:
		abo = amdxdna_drm_create_dev_heap(dev, args, filp);
		break;
	case AMDXDNA_BO_DEV:
		abo = amdxdna_drm_alloc_dev_bo(dev, args, filp);
		break;
	case AMDXDNA_BO_CMD:
		abo = amdxdna_drm_create_cmd_bo(dev, args, filp);
		break;
	default:
		return -EINVAL;
	}
	if (IS_ERR(abo))
		return PTR_ERR(abo);

	/* ready to publish object to userspace */
	ret = drm_gem_handle_create(filp, to_gobj(abo), &args->handle);
	if (ret) {
		XDNA_ERR(xdna, "Create handle failed");
		goto put_obj;
	}

	XDNA_DBG(xdna, "BO hdl %d type %d userptr 0x%llx xdna_addr 0x%llx size 0x%lx",
		 args->handle, args->type, abo->mem.userptr,
		 abo->mem.dev_addr, abo->mem.size);
put_obj:
	/* Dereference object reference. Handle holds it now. */
	drm_gem_object_put(to_gobj(abo));
	return ret;
}

int amdxdna_gem_pin_nolock(struct amdxdna_gem_obj *abo)
{
	struct amdxdna_dev *xdna = to_xdna_dev(to_gobj(abo)->dev);
	int ret;

	if (abo->type == AMDXDNA_BO_DEV)
		abo = abo->client->dev_heap;

	if (is_import_bo(abo))
		return 0;

	ret = drm_gem_shmem_pin(&abo->base);

	XDNA_DBG(xdna, "BO type %d ret %d", abo->type, ret);
	return ret;
}

int amdxdna_gem_pin(struct amdxdna_gem_obj *abo)
{
	int ret;

	mutex_lock(&abo->lock);
	ret = amdxdna_gem_pin_nolock(abo);
	mutex_unlock(&abo->lock);

	return ret;
}

void amdxdna_gem_unpin(struct amdxdna_gem_obj *abo)
{
	if (abo->type == AMDXDNA_BO_DEV)
		abo = abo->client->dev_heap;

	if (is_import_bo(abo))
		return;

	mutex_lock(&abo->lock);
	drm_gem_shmem_unpin(&abo->base);
	mutex_unlock(&abo->lock);
}

struct amdxdna_gem_obj *amdxdna_gem_get_obj(struct amdxdna_client *client,
					    u32 bo_hdl, u8 bo_type)
{
	struct amdxdna_dev *xdna = client->xdna;
	struct amdxdna_gem_obj *abo;
	struct drm_gem_object *gobj;

	gobj = drm_gem_object_lookup(client->filp, bo_hdl);
	if (!gobj) {
		XDNA_DBG(xdna, "Can not find bo %d", bo_hdl);
		return NULL;
	}

	abo = to_xdna_obj(gobj);
	if (bo_type == AMDXDNA_BO_INVALID || abo->type == bo_type)
		return abo;

	drm_gem_object_put(gobj);
	return NULL;
}

int amdxdna_drm_get_bo_info_ioctl(struct drm_device *dev, void *data, struct drm_file *filp)
{
	struct amdxdna_drm_get_bo_info *args = data;
	struct amdxdna_dev *xdna = to_xdna_dev(dev);
	struct amdxdna_gem_obj *abo;
	struct drm_gem_object *gobj;
	int ret = 0;

	if (args->ext || args->ext_flags || args->pad)
		return -EINVAL;

	gobj = drm_gem_object_lookup(filp, args->handle);
	if (!gobj) {
		XDNA_DBG(xdna, "Lookup GEM object %d failed", args->handle);
		return -ENOENT;
	}

	abo = to_xdna_obj(gobj);
	args->vaddr = abo->mem.userptr;
	args->xdna_addr = abo->mem.dev_addr;

	if (abo->type != AMDXDNA_BO_DEV)
		args->map_offset = drm_vma_node_offset_addr(&gobj->vma_node);
	else
		args->map_offset = AMDXDNA_INVALID_ADDR;

	XDNA_DBG(xdna, "BO hdl %d map_offset 0x%llx vaddr 0x%llx xdna_addr 0x%llx",
		 args->handle, args->map_offset, args->vaddr, args->xdna_addr);

	drm_gem_object_put(gobj);
	return ret;
}

/*
 * The sync bo ioctl is to make sure the CPU cache is in sync with memory.
 * This is required because NPU is not cache coherent device. CPU cache
 * flushing/invalidation is expensive so it is best to handle this outside
 * of the command submission path. This ioctl allows explicit cache
 * flushing/invalidation outside of the critical path.
 */
int amdxdna_drm_sync_bo_ioctl(struct drm_device *dev,
			      void *data, struct drm_file *filp)
{
	struct amdxdna_dev *xdna = to_xdna_dev(dev);
	struct amdxdna_drm_sync_bo *args = data;
	struct amdxdna_gem_obj *abo;
	struct drm_gem_object *gobj;
	int ret;

	gobj = drm_gem_object_lookup(filp, args->handle);
	if (!gobj) {
		XDNA_ERR(xdna, "Lookup GEM object failed");
		return -ENOENT;
	}
	abo = to_xdna_obj(gobj);

	ret = amdxdna_gem_pin(abo);
	if (ret) {
		XDNA_ERR(xdna, "Pin BO %d failed, ret %d", args->handle, ret);
		goto put_obj;
	}

	if (is_import_bo(abo))
		drm_clflush_sg(abo->base.sgt);
	else if (abo->mem.kva)
		drm_clflush_virt_range(abo->mem.kva + args->offset, args->size);
	else if (abo->base.pages)
		drm_clflush_pages(abo->base.pages, gobj->size >> PAGE_SHIFT);
	else
		drm_WARN(&xdna->ddev, 1, "Can not get flush memory");

	amdxdna_gem_unpin(abo);

	XDNA_DBG(xdna, "Sync bo %d offset 0x%llx, size 0x%llx\n",
		 args->handle, args->offset, args->size);

put_obj:
	drm_gem_object_put(gobj);
	return ret;
}
