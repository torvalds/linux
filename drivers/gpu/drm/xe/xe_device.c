// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "xe_device.h"

#include <drm/drm_gem_ttm_helper.h>
#include <drm/drm_aperture.h>
#include <drm/drm_ioctl.h>
#include <drm/xe_drm.h>
#include <drm/drm_managed.h>
#include <drm/drm_atomic_helper.h>

#include "xe_bo.h"
#include "xe_debugfs.h"
#include "xe_dma_buf.h"
#include "xe_drv.h"
#include "xe_engine.h"
#include "xe_exec.h"
#include "xe_gt.h"
#include "xe_irq.h"
#include "xe_module.h"
#include "xe_mmio.h"
#include "xe_pcode.h"
#include "xe_pm.h"
#include "xe_query.h"
#include "xe_ttm_stolen_mgr.h"
#include "xe_vm.h"
#include "xe_vm_madvise.h"
#include "xe_wait_user_fence.h"

static int xe_file_open(struct drm_device *dev, struct drm_file *file)
{
	struct xe_file *xef;

	xef = kzalloc(sizeof(*xef), GFP_KERNEL);
	if (!xef)
		return -ENOMEM;

	xef->drm = file;

	mutex_init(&xef->vm.lock);
	xa_init_flags(&xef->vm.xa, XA_FLAGS_ALLOC1);

	mutex_init(&xef->engine.lock);
	xa_init_flags(&xef->engine.xa, XA_FLAGS_ALLOC1);

	file->driver_priv = xef;
	return 0;
}

static void device_kill_persitent_engines(struct xe_device *xe,
					  struct xe_file *xef);

static void xe_file_close(struct drm_device *dev, struct drm_file *file)
{
	struct xe_device *xe = to_xe_device(dev);
	struct xe_file *xef = file->driver_priv;
	struct xe_vm *vm;
	struct xe_engine *e;
	unsigned long idx;

	mutex_lock(&xef->engine.lock);
	xa_for_each(&xef->engine.xa, idx, e) {
		xe_engine_kill(e);
		xe_engine_put(e);
	}
	mutex_unlock(&xef->engine.lock);
	mutex_destroy(&xef->engine.lock);
	device_kill_persitent_engines(xe, xef);

	mutex_lock(&xef->vm.lock);
	xa_for_each(&xef->vm.xa, idx, vm)
		xe_vm_close_and_put(vm);
	mutex_unlock(&xef->vm.lock);
	mutex_destroy(&xef->vm.lock);

	kfree(xef);
}

static const struct drm_ioctl_desc xe_ioctls[] = {
	DRM_IOCTL_DEF_DRV(XE_DEVICE_QUERY, xe_query_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(XE_GEM_CREATE, xe_gem_create_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(XE_GEM_MMAP_OFFSET, xe_gem_mmap_offset_ioctl,
			  DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(XE_VM_CREATE, xe_vm_create_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(XE_VM_DESTROY, xe_vm_destroy_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(XE_VM_BIND, xe_vm_bind_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(XE_ENGINE_CREATE, xe_engine_create_ioctl,
			  DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(XE_ENGINE_GET_PROPERTY, xe_engine_get_property_ioctl,
			  DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(XE_ENGINE_DESTROY, xe_engine_destroy_ioctl,
			  DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(XE_EXEC, xe_exec_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(XE_MMIO, xe_mmio_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(XE_ENGINE_SET_PROPERTY, xe_engine_set_property_ioctl,
			  DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(XE_WAIT_USER_FENCE, xe_wait_user_fence_ioctl,
			  DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(XE_VM_MADVISE, xe_vm_madvise_ioctl, DRM_RENDER_ALLOW),
};

static const struct file_operations xe_driver_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release_noglobal,
	.unlocked_ioctl = drm_ioctl,
	.mmap = drm_gem_mmap,
	.poll = drm_poll,
	.read = drm_read,
//	.compat_ioctl = i915_ioc32_compat_ioctl,
	.llseek = noop_llseek,
};

static void xe_driver_release(struct drm_device *dev)
{
	struct xe_device *xe = to_xe_device(dev);

	pci_set_drvdata(to_pci_dev(xe->drm.dev), NULL);
}

static struct drm_driver driver = {
	/* Don't use MTRRs here; the Xserver or userspace app should
	 * deal with them for Intel hardware.
	 */
	.driver_features =
	    DRIVER_GEM |
	    DRIVER_RENDER | DRIVER_SYNCOBJ |
	    DRIVER_SYNCOBJ_TIMELINE,
	.open = xe_file_open,
	.postclose = xe_file_close,

	.gem_prime_import = xe_gem_prime_import,

	.dumb_create = xe_bo_dumb_create,
	.dumb_map_offset = drm_gem_ttm_dumb_map_offset,
	.release = &xe_driver_release,

	.ioctls = xe_ioctls,
	.num_ioctls = ARRAY_SIZE(xe_ioctls),
	.fops = &xe_driver_fops,
	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
};

static void xe_device_destroy(struct drm_device *dev, void *dummy)
{
	struct xe_device *xe = to_xe_device(dev);

	destroy_workqueue(xe->ordered_wq);
	mutex_destroy(&xe->persitent_engines.lock);
	ttm_device_fini(&xe->ttm);
}

struct xe_device *xe_device_create(struct pci_dev *pdev,
				   const struct pci_device_id *ent)
{
	struct xe_device *xe;
	int err;

	err = drm_aperture_remove_conflicting_pci_framebuffers(pdev, &driver);
	if (err)
		return ERR_PTR(err);

	xe = devm_drm_dev_alloc(&pdev->dev, &driver, struct xe_device, drm);
	if (IS_ERR(xe))
		return xe;

	err = ttm_device_init(&xe->ttm, &xe_ttm_funcs, xe->drm.dev,
			      xe->drm.anon_inode->i_mapping,
			      xe->drm.vma_offset_manager, false, false);
	if (WARN_ON(err))
		goto err_put;

	xe->info.devid = pdev->device;
	xe->info.revid = pdev->revision;
	xe->info.enable_guc = enable_guc;

	spin_lock_init(&xe->irq.lock);

	init_waitqueue_head(&xe->ufence_wq);

	mutex_init(&xe->usm.lock);
	xa_init_flags(&xe->usm.asid_to_vm, XA_FLAGS_ALLOC1);

	mutex_init(&xe->persitent_engines.lock);
	INIT_LIST_HEAD(&xe->persitent_engines.list);

	spin_lock_init(&xe->pinned.lock);
	INIT_LIST_HEAD(&xe->pinned.kernel_bo_present);
	INIT_LIST_HEAD(&xe->pinned.external_vram);
	INIT_LIST_HEAD(&xe->pinned.evicted);

	xe->ordered_wq = alloc_ordered_workqueue("xe-ordered-wq", 0);

	mutex_init(&xe->sb_lock);
	xe->enabled_irq_mask = ~0;

	err = drmm_add_action_or_reset(&xe->drm, xe_device_destroy, NULL);
	if (err)
		goto err_put;

	mutex_init(&xe->mem_access.lock);
	return xe;

err_put:
	drm_dev_put(&xe->drm);

	return ERR_PTR(err);
}

static void xe_device_sanitize(struct drm_device *drm, void *arg)
{
	struct xe_device *xe = arg;
	struct xe_gt *gt;
	u8 id;

	for_each_gt(gt, xe, id)
		xe_gt_sanitize(gt);
}

int xe_device_probe(struct xe_device *xe)
{
	struct xe_gt *gt;
	int err;
	u8 id;

	xe->info.mem_region_mask = 1;

	for_each_gt(gt, xe, id) {
		err = xe_gt_alloc(xe, gt);
		if (err)
			return err;
	}

	err = xe_mmio_init(xe);
	if (err)
		return err;

	for_each_gt(gt, xe, id) {
		err = xe_pcode_probe(gt);
		if (err)
			return err;
	}

	err = xe_irq_install(xe);
	if (err)
		return err;

	for_each_gt(gt, xe, id) {
		err = xe_gt_init_early(gt);
		if (err)
			goto err_irq_shutdown;
	}

	err = xe_mmio_probe_vram(xe);
	if (err)
		goto err_irq_shutdown;

	for_each_gt(gt, xe, id) {
		err = xe_gt_init_noalloc(gt);
		if (err)
			goto err_irq_shutdown;
	}

	/* Allocate and map stolen after potential VRAM resize */
	xe_ttm_stolen_mgr_init(xe);

	for_each_gt(gt, xe, id) {
		err = xe_gt_init(gt);
		if (err)
			goto err_irq_shutdown;
	}

	err = drm_dev_register(&xe->drm, 0);
	if (err)
		goto err_irq_shutdown;

	xe_debugfs_register(xe);

	err = drmm_add_action_or_reset(&xe->drm, xe_device_sanitize, xe);
	if (err)
		return err;

	return 0;

err_irq_shutdown:
	xe_irq_shutdown(xe);
	return err;
}

void xe_device_remove(struct xe_device *xe)
{
	xe_irq_shutdown(xe);
}

void xe_device_shutdown(struct xe_device *xe)
{
}

void xe_device_add_persitent_engines(struct xe_device *xe, struct xe_engine *e)
{
	mutex_lock(&xe->persitent_engines.lock);
	list_add_tail(&e->persitent.link, &xe->persitent_engines.list);
	mutex_unlock(&xe->persitent_engines.lock);
}

void xe_device_remove_persitent_engines(struct xe_device *xe,
					struct xe_engine *e)
{
	mutex_lock(&xe->persitent_engines.lock);
	if (!list_empty(&e->persitent.link))
		list_del(&e->persitent.link);
	mutex_unlock(&xe->persitent_engines.lock);
}

static void device_kill_persitent_engines(struct xe_device *xe,
					  struct xe_file *xef)
{
	struct xe_engine *e, *next;

	mutex_lock(&xe->persitent_engines.lock);
	list_for_each_entry_safe(e, next, &xe->persitent_engines.list,
				 persitent.link)
		if (e->persitent.xef == xef) {
			xe_engine_kill(e);
			list_del_init(&e->persitent.link);
		}
	mutex_unlock(&xe->persitent_engines.lock);
}

#define SOFTWARE_FLAGS_SPR33         _MMIO(0x4F084)

void xe_device_wmb(struct xe_device *xe)
{
	struct xe_gt *gt = xe_device_get_gt(xe, 0);

	wmb();
	if (IS_DGFX(xe))
		xe_mmio_write32(gt, SOFTWARE_FLAGS_SPR33.reg, 0);
}

u32 xe_device_ccs_bytes(struct xe_device *xe, u64 size)
{
	return xe_device_has_flat_ccs(xe) ?
		DIV_ROUND_UP(size, NUM_BYTES_PER_CCS_BYTE) : 0;
}

void xe_device_mem_access_get(struct xe_device *xe)
{
	bool resumed = xe_pm_runtime_resume_if_suspended(xe);

	mutex_lock(&xe->mem_access.lock);
	if (xe->mem_access.ref++ == 0)
		xe->mem_access.hold_rpm = xe_pm_runtime_get_if_active(xe);
	mutex_unlock(&xe->mem_access.lock);

	/* The usage counter increased if device was immediately resumed */
	if (resumed)
		xe_pm_runtime_put(xe);

	XE_WARN_ON(xe->mem_access.ref == S32_MAX);
}

void xe_device_mem_access_put(struct xe_device *xe)
{
	mutex_lock(&xe->mem_access.lock);
	if (--xe->mem_access.ref == 0 && xe->mem_access.hold_rpm)
		xe_pm_runtime_put(xe);
	mutex_unlock(&xe->mem_access.lock);

	XE_WARN_ON(xe->mem_access.ref < 0);
}
