/*
 * Copyright 2013 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Dave Airlie
 *          Alon Levy
 */

#include <drm/ttm/ttm_bo_api.h>
#include <drm/ttm/ttm_bo_driver.h>
#include <drm/ttm/ttm_placement.h>
#include <drm/ttm/ttm_page_alloc.h>
#include <drm/ttm/ttm_module.h>
#include <drm/drmP.h>
#include <drm/drm.h>
#include <drm/qxl_drm.h>
#include "qxl_drv.h"
#include "qxl_object.h"

#include <linux/delay.h>

static struct qxl_device *qxl_get_qdev(struct ttm_bo_device *bdev)
{
	struct qxl_mman *mman;
	struct qxl_device *qdev;

	mman = container_of(bdev, struct qxl_mman, bdev);
	qdev = container_of(mman, struct qxl_device, mman);
	return qdev;
}

static int qxl_ttm_mem_global_init(struct drm_global_reference *ref)
{
	return ttm_mem_global_init(ref->object);
}

static void qxl_ttm_mem_global_release(struct drm_global_reference *ref)
{
	ttm_mem_global_release(ref->object);
}

static int qxl_ttm_global_init(struct qxl_device *qdev)
{
	struct drm_global_reference *global_ref;
	int r;

	qdev->mman.mem_global_referenced = false;
	global_ref = &qdev->mman.mem_global_ref;
	global_ref->global_type = DRM_GLOBAL_TTM_MEM;
	global_ref->size = sizeof(struct ttm_mem_global);
	global_ref->init = &qxl_ttm_mem_global_init;
	global_ref->release = &qxl_ttm_mem_global_release;

	r = drm_global_item_ref(global_ref);
	if (r != 0) {
		DRM_ERROR("Failed setting up TTM memory accounting "
			  "subsystem.\n");
		return r;
	}

	qdev->mman.bo_global_ref.mem_glob =
		qdev->mman.mem_global_ref.object;
	global_ref = &qdev->mman.bo_global_ref.ref;
	global_ref->global_type = DRM_GLOBAL_TTM_BO;
	global_ref->size = sizeof(struct ttm_bo_global);
	global_ref->init = &ttm_bo_global_init;
	global_ref->release = &ttm_bo_global_release;
	r = drm_global_item_ref(global_ref);
	if (r != 0) {
		DRM_ERROR("Failed setting up TTM BO subsystem.\n");
		drm_global_item_unref(&qdev->mman.mem_global_ref);
		return r;
	}

	qdev->mman.mem_global_referenced = true;
	return 0;
}

static void qxl_ttm_global_fini(struct qxl_device *qdev)
{
	if (qdev->mman.mem_global_referenced) {
		drm_global_item_unref(&qdev->mman.bo_global_ref.ref);
		drm_global_item_unref(&qdev->mman.mem_global_ref);
		qdev->mman.mem_global_referenced = false;
	}
}

static struct vm_operations_struct qxl_ttm_vm_ops;
static const struct vm_operations_struct *ttm_vm_ops;

static int qxl_ttm_fault(struct vm_fault *vmf)
{
	struct ttm_buffer_object *bo;
	int r;

	bo = (struct ttm_buffer_object *)vmf->vma->vm_private_data;
	if (bo == NULL)
		return VM_FAULT_NOPAGE;
	r = ttm_vm_ops->fault(vmf);
	return r;
}

int qxl_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_file *file_priv;
	struct qxl_device *qdev;
	int r;

	if (unlikely(vma->vm_pgoff < DRM_FILE_PAGE_OFFSET))
		return -EINVAL;

	file_priv = filp->private_data;
	qdev = file_priv->minor->dev->dev_private;
	if (qdev == NULL) {
		DRM_ERROR(
		 "filp->private_data->minor->dev->dev_private == NULL\n");
		return -EINVAL;
	}
	QXL_INFO(qdev, "%s: filp->private_data = 0x%p, vma->vm_pgoff = %lx\n",
		 __func__, filp->private_data, vma->vm_pgoff);

	r = ttm_bo_mmap(filp, vma, &qdev->mman.bdev);
	if (unlikely(r != 0))
		return r;
	if (unlikely(ttm_vm_ops == NULL)) {
		ttm_vm_ops = vma->vm_ops;
		qxl_ttm_vm_ops = *ttm_vm_ops;
		qxl_ttm_vm_ops.fault = &qxl_ttm_fault;
	}
	vma->vm_ops = &qxl_ttm_vm_ops;
	return 0;
}

static int qxl_invalidate_caches(struct ttm_bo_device *bdev, uint32_t flags)
{
	return 0;
}

static int qxl_init_mem_type(struct ttm_bo_device *bdev, uint32_t type,
			     struct ttm_mem_type_manager *man)
{
	switch (type) {
	case TTM_PL_SYSTEM:
		/* System memory */
		man->flags = TTM_MEMTYPE_FLAG_MAPPABLE;
		man->available_caching = TTM_PL_MASK_CACHING;
		man->default_caching = TTM_PL_FLAG_CACHED;
		break;
	case TTM_PL_VRAM:
	case TTM_PL_PRIV:
		/* "On-card" video ram */
		man->func = &ttm_bo_manager_func;
		man->gpu_offset = 0;
		man->flags = TTM_MEMTYPE_FLAG_FIXED |
			     TTM_MEMTYPE_FLAG_MAPPABLE;
		man->available_caching = TTM_PL_MASK_CACHING;
		man->default_caching = TTM_PL_FLAG_CACHED;
		break;
	default:
		DRM_ERROR("Unsupported memory type %u\n", (unsigned)type);
		return -EINVAL;
	}
	return 0;
}

static void qxl_evict_flags(struct ttm_buffer_object *bo,
				struct ttm_placement *placement)
{
	struct qxl_bo *qbo;
	static const struct ttm_place placements = {
		.fpfn = 0,
		.lpfn = 0,
		.flags = TTM_PL_MASK_CACHING | TTM_PL_FLAG_SYSTEM
	};

	if (!qxl_ttm_bo_is_qxl_bo(bo)) {
		placement->placement = &placements;
		placement->busy_placement = &placements;
		placement->num_placement = 1;
		placement->num_busy_placement = 1;
		return;
	}
	qbo = to_qxl_bo(bo);
	qxl_ttm_placement_from_domain(qbo, QXL_GEM_DOMAIN_CPU, false);
	*placement = qbo->placement;
}

static int qxl_verify_access(struct ttm_buffer_object *bo, struct file *filp)
{
	struct qxl_bo *qbo = to_qxl_bo(bo);

	return drm_vma_node_verify_access(&qbo->gem_base.vma_node,
					  filp->private_data);
}

static int qxl_ttm_io_mem_reserve(struct ttm_bo_device *bdev,
				  struct ttm_mem_reg *mem)
{
	struct ttm_mem_type_manager *man = &bdev->man[mem->mem_type];
	struct qxl_device *qdev = qxl_get_qdev(bdev);

	mem->bus.addr = NULL;
	mem->bus.offset = 0;
	mem->bus.size = mem->num_pages << PAGE_SHIFT;
	mem->bus.base = 0;
	mem->bus.is_iomem = false;
	if (!(man->flags & TTM_MEMTYPE_FLAG_MAPPABLE))
		return -EINVAL;
	switch (mem->mem_type) {
	case TTM_PL_SYSTEM:
		/* system memory */
		return 0;
	case TTM_PL_VRAM:
		mem->bus.is_iomem = true;
		mem->bus.base = qdev->vram_base;
		mem->bus.offset = mem->start << PAGE_SHIFT;
		break;
	case TTM_PL_PRIV:
		mem->bus.is_iomem = true;
		mem->bus.base = qdev->surfaceram_base;
		mem->bus.offset = mem->start << PAGE_SHIFT;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static void qxl_ttm_io_mem_free(struct ttm_bo_device *bdev,
				struct ttm_mem_reg *mem)
{
}

/*
 * TTM backend functions.
 */
struct qxl_ttm_tt {
	struct ttm_dma_tt		ttm;
	struct qxl_device		*qdev;
	u64				offset;
};

static int qxl_ttm_backend_bind(struct ttm_tt *ttm,
				struct ttm_mem_reg *bo_mem)
{
	struct qxl_ttm_tt *gtt = (void *)ttm;

	gtt->offset = (unsigned long)(bo_mem->start << PAGE_SHIFT);
	if (!ttm->num_pages) {
		WARN(1, "nothing to bind %lu pages for mreg %p back %p!\n",
		     ttm->num_pages, bo_mem, ttm);
	}
	/* Not implemented */
	return -1;
}

static int qxl_ttm_backend_unbind(struct ttm_tt *ttm)
{
	/* Not implemented */
	return -1;
}

static void qxl_ttm_backend_destroy(struct ttm_tt *ttm)
{
	struct qxl_ttm_tt *gtt = (void *)ttm;

	ttm_dma_tt_fini(&gtt->ttm);
	kfree(gtt);
}

static struct ttm_backend_func qxl_backend_func = {
	.bind = &qxl_ttm_backend_bind,
	.unbind = &qxl_ttm_backend_unbind,
	.destroy = &qxl_ttm_backend_destroy,
};

static int qxl_ttm_tt_populate(struct ttm_tt *ttm)
{
	int r;

	if (ttm->state != tt_unpopulated)
		return 0;

	r = ttm_pool_populate(ttm);
	if (r)
		return r;

	return 0;
}

static void qxl_ttm_tt_unpopulate(struct ttm_tt *ttm)
{
	ttm_pool_unpopulate(ttm);
}

static struct ttm_tt *qxl_ttm_tt_create(struct ttm_bo_device *bdev,
					unsigned long size, uint32_t page_flags,
					struct page *dummy_read_page)
{
	struct qxl_device *qdev;
	struct qxl_ttm_tt *gtt;

	qdev = qxl_get_qdev(bdev);
	gtt = kzalloc(sizeof(struct qxl_ttm_tt), GFP_KERNEL);
	if (gtt == NULL)
		return NULL;
	gtt->ttm.ttm.func = &qxl_backend_func;
	gtt->qdev = qdev;
	if (ttm_dma_tt_init(&gtt->ttm, bdev, size, page_flags,
			    dummy_read_page)) {
		kfree(gtt);
		return NULL;
	}
	return &gtt->ttm.ttm;
}

static void qxl_move_null(struct ttm_buffer_object *bo,
			     struct ttm_mem_reg *new_mem)
{
	struct ttm_mem_reg *old_mem = &bo->mem;

	BUG_ON(old_mem->mm_node != NULL);
	*old_mem = *new_mem;
	new_mem->mm_node = NULL;
}

static int qxl_bo_move(struct ttm_buffer_object *bo,
		       bool evict, bool interruptible,
		       bool no_wait_gpu,
		       struct ttm_mem_reg *new_mem)
{
	struct ttm_mem_reg *old_mem = &bo->mem;
	int ret;

	ret = ttm_bo_wait(bo, interruptible, no_wait_gpu);
	if (ret)
		return ret;


	if (old_mem->mem_type == TTM_PL_SYSTEM && bo->ttm == NULL) {
		qxl_move_null(bo, new_mem);
		return 0;
	}
	return ttm_bo_move_memcpy(bo, interruptible, no_wait_gpu,
				  new_mem);
}

static void qxl_bo_move_notify(struct ttm_buffer_object *bo,
			       bool evict,
			       struct ttm_mem_reg *new_mem)
{
	struct qxl_bo *qbo;
	struct qxl_device *qdev;

	if (!qxl_ttm_bo_is_qxl_bo(bo))
		return;
	qbo = to_qxl_bo(bo);
	qdev = qbo->gem_base.dev->dev_private;

	if (bo->mem.mem_type == TTM_PL_PRIV && qbo->surface_id)
		qxl_surface_evict(qdev, qbo, new_mem ? true : false);
}

static struct ttm_bo_driver qxl_bo_driver = {
	.ttm_tt_create = &qxl_ttm_tt_create,
	.ttm_tt_populate = &qxl_ttm_tt_populate,
	.ttm_tt_unpopulate = &qxl_ttm_tt_unpopulate,
	.invalidate_caches = &qxl_invalidate_caches,
	.init_mem_type = &qxl_init_mem_type,
	.eviction_valuable = ttm_bo_eviction_valuable,
	.evict_flags = &qxl_evict_flags,
	.move = &qxl_bo_move,
	.verify_access = &qxl_verify_access,
	.io_mem_reserve = &qxl_ttm_io_mem_reserve,
	.io_mem_free = &qxl_ttm_io_mem_free,
	.io_mem_pfn = ttm_bo_default_io_mem_pfn,
	.move_notify = &qxl_bo_move_notify,
};

int qxl_ttm_init(struct qxl_device *qdev)
{
	int r;
	int num_io_pages; /* != rom->num_io_pages, we include surface0 */

	r = qxl_ttm_global_init(qdev);
	if (r)
		return r;
	/* No others user of address space so set it to 0 */
	r = ttm_bo_device_init(&qdev->mman.bdev,
			       qdev->mman.bo_global_ref.ref.object,
			       &qxl_bo_driver,
			       qdev->ddev.anon_inode->i_mapping,
			       DRM_FILE_PAGE_OFFSET, 0);
	if (r) {
		DRM_ERROR("failed initializing buffer object driver(%d).\n", r);
		return r;
	}
	/* NOTE: this includes the framebuffer (aka surface 0) */
	num_io_pages = qdev->rom->ram_header_offset / PAGE_SIZE;
	r = ttm_bo_init_mm(&qdev->mman.bdev, TTM_PL_VRAM,
			   num_io_pages);
	if (r) {
		DRM_ERROR("Failed initializing VRAM heap.\n");
		return r;
	}
	r = ttm_bo_init_mm(&qdev->mman.bdev, TTM_PL_PRIV,
			   qdev->surfaceram_size / PAGE_SIZE);
	if (r) {
		DRM_ERROR("Failed initializing Surfaces heap.\n");
		return r;
	}
	DRM_INFO("qxl: %uM of VRAM memory size\n",
		 (unsigned)qdev->vram_size / (1024 * 1024));
	DRM_INFO("qxl: %luM of IO pages memory ready (VRAM domain)\n",
		 ((unsigned)num_io_pages * PAGE_SIZE) / (1024 * 1024));
	DRM_INFO("qxl: %uM of Surface memory size\n",
		 (unsigned)qdev->surfaceram_size / (1024 * 1024));
	return 0;
}

void qxl_ttm_fini(struct qxl_device *qdev)
{
	ttm_bo_clean_mm(&qdev->mman.bdev, TTM_PL_VRAM);
	ttm_bo_clean_mm(&qdev->mman.bdev, TTM_PL_PRIV);
	ttm_bo_device_release(&qdev->mman.bdev);
	qxl_ttm_global_fini(qdev);
	DRM_INFO("qxl: ttm finalized\n");
}


#define QXL_DEBUGFS_MEM_TYPES 2

#if defined(CONFIG_DEBUG_FS)
static int qxl_mm_dump_table(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *)m->private;
	struct drm_mm *mm = (struct drm_mm *)node->info_ent->data;
	struct drm_device *dev = node->minor->dev;
	struct qxl_device *rdev = dev->dev_private;
	struct ttm_bo_global *glob = rdev->mman.bdev.glob;
	struct drm_printer p = drm_seq_file_printer(m);

	spin_lock(&glob->lru_lock);
	drm_mm_print(mm, &p);
	spin_unlock(&glob->lru_lock);
	return 0;
}
#endif

int qxl_ttm_debugfs_init(struct qxl_device *qdev)
{
#if defined(CONFIG_DEBUG_FS)
	static struct drm_info_list qxl_mem_types_list[QXL_DEBUGFS_MEM_TYPES];
	static char qxl_mem_types_names[QXL_DEBUGFS_MEM_TYPES][32];
	unsigned i;

	for (i = 0; i < QXL_DEBUGFS_MEM_TYPES; i++) {
		if (i == 0)
			sprintf(qxl_mem_types_names[i], "qxl_mem_mm");
		else
			sprintf(qxl_mem_types_names[i], "qxl_surf_mm");
		qxl_mem_types_list[i].name = qxl_mem_types_names[i];
		qxl_mem_types_list[i].show = &qxl_mm_dump_table;
		qxl_mem_types_list[i].driver_features = 0;
		if (i == 0)
			qxl_mem_types_list[i].data = qdev->mman.bdev.man[TTM_PL_VRAM].priv;
		else
			qxl_mem_types_list[i].data = qdev->mman.bdev.man[TTM_PL_PRIV].priv;

	}
	return qxl_debugfs_add_files(qdev, qxl_mem_types_list, i);
#else
	return 0;
#endif
}
