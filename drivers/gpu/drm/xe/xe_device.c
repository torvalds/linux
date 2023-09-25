// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "xe_device.h"

#include <drm/drm_aperture.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_gem_ttm_helper.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_managed.h>
#include <drm/drm_print.h>
#include <drm/xe_drm.h>

#include "regs/xe_regs.h"
#include "xe_bo.h"
#include "xe_debugfs.h"
#include "xe_dma_buf.h"
#include "xe_drm_client.h"
#include "xe_drv.h"
#include "xe_exec_queue.h"
#include "xe_exec.h"
#include "xe_gt.h"
#include "xe_irq.h"
#include "xe_mmio.h"
#include "xe_module.h"
#include "xe_pat.h"
#include "xe_pcode.h"
#include "xe_pm.h"
#include "xe_query.h"
#include "xe_tile.h"
#include "xe_ttm_stolen_mgr.h"
#include "xe_ttm_sys_mgr.h"
#include "xe_vm.h"
#include "xe_vm_madvise.h"
#include "xe_wait_user_fence.h"
#include "xe_hwmon.h"

#ifdef CONFIG_LOCKDEP
struct lockdep_map xe_device_mem_access_lockdep_map = {
	.name = "xe_device_mem_access_lockdep_map"
};
#endif

static int xe_file_open(struct drm_device *dev, struct drm_file *file)
{
	struct xe_device *xe = to_xe_device(dev);
	struct xe_drm_client *client;
	struct xe_file *xef;
	int ret = -ENOMEM;

	xef = kzalloc(sizeof(*xef), GFP_KERNEL);
	if (!xef)
		return ret;

	client = xe_drm_client_alloc();
	if (!client) {
		kfree(xef);
		return ret;
	}

	xef->drm = file;
	xef->client = client;
	xef->xe = xe;

	mutex_init(&xef->vm.lock);
	xa_init_flags(&xef->vm.xa, XA_FLAGS_ALLOC1);

	mutex_init(&xef->exec_queue.lock);
	xa_init_flags(&xef->exec_queue.xa, XA_FLAGS_ALLOC1);

	file->driver_priv = xef;
	return 0;
}

static void device_kill_persistent_exec_queues(struct xe_device *xe,
					       struct xe_file *xef);

static void xe_file_close(struct drm_device *dev, struct drm_file *file)
{
	struct xe_device *xe = to_xe_device(dev);
	struct xe_file *xef = file->driver_priv;
	struct xe_vm *vm;
	struct xe_exec_queue *q;
	unsigned long idx;

	mutex_lock(&xef->exec_queue.lock);
	xa_for_each(&xef->exec_queue.xa, idx, q) {
		xe_exec_queue_kill(q);
		xe_exec_queue_put(q);
	}
	mutex_unlock(&xef->exec_queue.lock);
	xa_destroy(&xef->exec_queue.xa);
	mutex_destroy(&xef->exec_queue.lock);
	device_kill_persistent_exec_queues(xe, xef);

	mutex_lock(&xef->vm.lock);
	xa_for_each(&xef->vm.xa, idx, vm)
		xe_vm_close_and_put(vm);
	mutex_unlock(&xef->vm.lock);
	xa_destroy(&xef->vm.xa);
	mutex_destroy(&xef->vm.lock);

	xe_drm_client_put(xef->client);
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
	DRM_IOCTL_DEF_DRV(XE_EXEC_QUEUE_CREATE, xe_exec_queue_create_ioctl,
			  DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(XE_EXEC_QUEUE_GET_PROPERTY, xe_exec_queue_get_property_ioctl,
			  DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(XE_EXEC_QUEUE_DESTROY, xe_exec_queue_destroy_ioctl,
			  DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(XE_EXEC, xe_exec_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(XE_MMIO, xe_mmio_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(XE_EXEC_QUEUE_SET_PROPERTY, xe_exec_queue_set_property_ioctl,
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
	.compat_ioctl = drm_compat_ioctl,
	.llseek = noop_llseek,
#ifdef CONFIG_PROC_FS
	.show_fdinfo = drm_show_fdinfo,
#endif
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
	    DRIVER_SYNCOBJ_TIMELINE | DRIVER_GEM_GPUVA,
	.open = xe_file_open,
	.postclose = xe_file_close,

	.gem_prime_import = xe_gem_prime_import,

	.dumb_create = xe_bo_dumb_create,
	.dumb_map_offset = drm_gem_ttm_dumb_map_offset,
#ifdef CONFIG_PROC_FS
	.show_fdinfo = xe_drm_client_fdinfo,
#endif
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

	if (xe->ordered_wq)
		destroy_workqueue(xe->ordered_wq);

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

	err = drmm_add_action_or_reset(&xe->drm, xe_device_destroy, NULL);
	if (err)
		goto err_put;

	xe->info.devid = pdev->device;
	xe->info.revid = pdev->revision;
	xe->info.force_execlist = force_execlist;

	spin_lock_init(&xe->irq.lock);

	init_waitqueue_head(&xe->ufence_wq);

	drmm_mutex_init(&xe->drm, &xe->usm.lock);
	xa_init_flags(&xe->usm.asid_to_vm, XA_FLAGS_ALLOC1);

	drmm_mutex_init(&xe->drm, &xe->persistent_engines.lock);
	INIT_LIST_HEAD(&xe->persistent_engines.list);

	spin_lock_init(&xe->pinned.lock);
	INIT_LIST_HEAD(&xe->pinned.kernel_bo_present);
	INIT_LIST_HEAD(&xe->pinned.external_vram);
	INIT_LIST_HEAD(&xe->pinned.evicted);

	xe->ordered_wq = alloc_ordered_workqueue("xe-ordered-wq", 0);
	if (!xe->ordered_wq) {
		drm_err(&xe->drm, "Failed to allocate xe-ordered-wq\n");
		err = -ENOMEM;
		goto err_put;
	}

	drmm_mutex_init(&xe->drm, &xe->sb_lock);
	xe->enabled_irq_mask = ~0;

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
	struct xe_tile *tile;
	struct xe_gt *gt;
	int err;
	u8 id;

	xe_pat_init_early(xe);

	xe->info.mem_region_mask = 1;

	for_each_tile(tile, xe, id) {
		err = xe_tile_alloc(tile);
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

	xe_ttm_sys_mgr_init(xe);

	for_each_tile(tile, xe, id) {
		err = xe_tile_init_noalloc(tile);
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

	xe_pmu_register(&xe->pmu);

	xe_hwmon_register(xe);

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

void xe_device_add_persistent_exec_queues(struct xe_device *xe, struct xe_exec_queue *q)
{
	mutex_lock(&xe->persistent_engines.lock);
	list_add_tail(&q->persistent.link, &xe->persistent_engines.list);
	mutex_unlock(&xe->persistent_engines.lock);
}

void xe_device_remove_persistent_exec_queues(struct xe_device *xe,
					     struct xe_exec_queue *q)
{
	mutex_lock(&xe->persistent_engines.lock);
	if (!list_empty(&q->persistent.link))
		list_del(&q->persistent.link);
	mutex_unlock(&xe->persistent_engines.lock);
}

static void device_kill_persistent_exec_queues(struct xe_device *xe,
					       struct xe_file *xef)
{
	struct xe_exec_queue *q, *next;

	mutex_lock(&xe->persistent_engines.lock);
	list_for_each_entry_safe(q, next, &xe->persistent_engines.list,
				 persistent.link)
		if (q->persistent.xef == xef) {
			xe_exec_queue_kill(q);
			list_del_init(&q->persistent.link);
		}
	mutex_unlock(&xe->persistent_engines.lock);
}

void xe_device_wmb(struct xe_device *xe)
{
	struct xe_gt *gt = xe_root_mmio_gt(xe);

	wmb();
	if (IS_DGFX(xe))
		xe_mmio_write32(gt, SOFTWARE_FLAGS_SPR33, 0);
}

u32 xe_device_ccs_bytes(struct xe_device *xe, u64 size)
{
	return xe_device_has_flat_ccs(xe) ?
		DIV_ROUND_UP(size, NUM_BYTES_PER_CCS_BYTE) : 0;
}

bool xe_device_mem_access_ongoing(struct xe_device *xe)
{
	if (xe_pm_read_callback_task(xe) != NULL)
		return true;

	return atomic_read(&xe->mem_access.ref);
}

void xe_device_assert_mem_access(struct xe_device *xe)
{
	XE_WARN_ON(!xe_device_mem_access_ongoing(xe));
}

bool xe_device_mem_access_get_if_ongoing(struct xe_device *xe)
{
	bool active;

	if (xe_pm_read_callback_task(xe) == current)
		return true;

	active = xe_pm_runtime_get_if_active(xe);
	if (active) {
		int ref = atomic_inc_return(&xe->mem_access.ref);

		xe_assert(xe, ref != S32_MAX);
	}

	return active;
}

void xe_device_mem_access_get(struct xe_device *xe)
{
	int ref;

	/*
	 * This looks racy, but should be fine since the pm_callback_task only
	 * transitions from NULL -> current (and back to NULL again), during the
	 * runtime_resume() or runtime_suspend() callbacks, for which there can
	 * only be a single one running for our device. We only need to prevent
	 * recursively calling the runtime_get or runtime_put from those
	 * callbacks, as well as preventing triggering any access_ongoing
	 * asserts.
	 */
	if (xe_pm_read_callback_task(xe) == current)
		return;

	/*
	 * Since the resume here is synchronous it can be quite easy to deadlock
	 * if we are not careful. Also in practice it might be quite timing
	 * sensitive to ever see the 0 -> 1 transition with the callers locks
	 * held, so deadlocks might exist but are hard for lockdep to ever see.
	 * With this in mind, help lockdep learn about the potentially scary
	 * stuff that can happen inside the runtime_resume callback by acquiring
	 * a dummy lock (it doesn't protect anything and gets compiled out on
	 * non-debug builds).  Lockdep then only needs to see the
	 * mem_access_lockdep_map -> runtime_resume callback once, and then can
	 * hopefully validate all the (callers_locks) -> mem_access_lockdep_map.
	 * For example if the (callers_locks) are ever grabbed in the
	 * runtime_resume callback, lockdep should give us a nice splat.
	 */
	lock_map_acquire(&xe_device_mem_access_lockdep_map);
	lock_map_release(&xe_device_mem_access_lockdep_map);

	xe_pm_runtime_get(xe);
	ref = atomic_inc_return(&xe->mem_access.ref);

	xe_assert(xe, ref != S32_MAX);

}

void xe_device_mem_access_put(struct xe_device *xe)
{
	int ref;

	if (xe_pm_read_callback_task(xe) == current)
		return;

	ref = atomic_dec_return(&xe->mem_access.ref);
	xe_pm_runtime_put(xe);

	xe_assert(xe, ref >= 0);
}
