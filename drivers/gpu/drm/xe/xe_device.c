// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "xe_device.h"

#include <linux/aperture.h>
#include <linux/delay.h>
#include <linux/fault-inject.h>
#include <linux/units.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_client.h>
#include <drm/drm_gem_ttm_helper.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_managed.h>
#include <drm/drm_pagemap_util.h>
#include <drm/drm_print.h>
#include <uapi/drm/xe_drm.h>

#include "display/xe_display.h"
#include "instructions/xe_gpu_commands.h"
#include "regs/xe_gt_regs.h"
#include "regs/xe_regs.h"
#include "xe_bo.h"
#include "xe_bo_evict.h"
#include "xe_debugfs.h"
#include "xe_devcoredump.h"
#include "xe_device_sysfs.h"
#include "xe_dma_buf.h"
#include "xe_drm_client.h"
#include "xe_drv.h"
#include "xe_exec.h"
#include "xe_exec_queue.h"
#include "xe_force_wake.h"
#include "xe_ggtt.h"
#include "xe_gt.h"
#include "xe_gt_mcr.h"
#include "xe_gt_printk.h"
#include "xe_gt_sriov_vf.h"
#include "xe_guc.h"
#include "xe_guc_pc.h"
#include "xe_hw_engine_group.h"
#include "xe_hwmon.h"
#include "xe_i2c.h"
#include "xe_irq.h"
#include "xe_late_bind_fw.h"
#include "xe_mmio.h"
#include "xe_module.h"
#include "xe_nvm.h"
#include "xe_oa.h"
#include "xe_observation.h"
#include "xe_pagefault.h"
#include "xe_pat.h"
#include "xe_pcode.h"
#include "xe_pm.h"
#include "xe_pmu.h"
#include "xe_psmi.h"
#include "xe_pxp.h"
#include "xe_query.h"
#include "xe_shrinker.h"
#include "xe_soc_remapper.h"
#include "xe_survivability_mode.h"
#include "xe_sriov.h"
#include "xe_svm.h"
#include "xe_tile.h"
#include "xe_ttm_stolen_mgr.h"
#include "xe_ttm_sys_mgr.h"
#include "xe_vm.h"
#include "xe_vm_madvise.h"
#include "xe_vram.h"
#include "xe_vram_types.h"
#include "xe_vsec.h"
#include "xe_wait_user_fence.h"
#include "xe_wa.h"

#include <generated/xe_device_wa_oob.h>
#include <generated/xe_wa_oob.h>

static int xe_file_open(struct drm_device *dev, struct drm_file *file)
{
	struct xe_device *xe = to_xe_device(dev);
	struct xe_drm_client *client;
	struct xe_file *xef;
	int ret = -ENOMEM;
	struct task_struct *task = NULL;

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
	kref_init(&xef->refcount);

	task = get_pid_task(rcu_access_pointer(file->pid), PIDTYPE_PID);
	if (task) {
		xef->process_name = kstrdup(task->comm, GFP_KERNEL);
		xef->pid = task->pid;
		put_task_struct(task);
	}

	return 0;
}

static void xe_file_destroy(struct kref *ref)
{
	struct xe_file *xef = container_of(ref, struct xe_file, refcount);

	xa_destroy(&xef->exec_queue.xa);
	mutex_destroy(&xef->exec_queue.lock);
	xa_destroy(&xef->vm.xa);
	mutex_destroy(&xef->vm.lock);

	xe_drm_client_put(xef->client);
	kfree(xef->process_name);
	kfree(xef);
}

/**
 * xe_file_get() - Take a reference to the xe file object
 * @xef: Pointer to the xe file
 *
 * Anyone with a pointer to xef must take a reference to the xe file
 * object using this call.
 *
 * Return: xe file pointer
 */
struct xe_file *xe_file_get(struct xe_file *xef)
{
	kref_get(&xef->refcount);
	return xef;
}

/**
 * xe_file_put() - Drop a reference to the xe file object
 * @xef: Pointer to the xe file
 *
 * Used to drop reference to the xef object
 */
void xe_file_put(struct xe_file *xef)
{
	kref_put(&xef->refcount, xe_file_destroy);
}

static void xe_file_close(struct drm_device *dev, struct drm_file *file)
{
	struct xe_device *xe = to_xe_device(dev);
	struct xe_file *xef = file->driver_priv;
	struct xe_vm *vm;
	struct xe_exec_queue *q;
	unsigned long idx;

	guard(xe_pm_runtime)(xe);

	/*
	 * No need for exec_queue.lock here as there is no contention for it
	 * when FD is closing as IOCTLs presumably can't be modifying the
	 * xarray. Taking exec_queue.lock here causes undue dependency on
	 * vm->lock taken during xe_exec_queue_kill().
	 */
	xa_for_each(&xef->exec_queue.xa, idx, q) {
		if (q->vm && q->hwe->hw_engine_group)
			xe_hw_engine_group_del_exec_queue(q->hwe->hw_engine_group, q);
		xe_exec_queue_kill(q);
		xe_exec_queue_put(q);
	}
	xa_for_each(&xef->vm.xa, idx, vm)
		xe_vm_close_and_put(vm);

	xe_file_put(xef);
}

static const struct drm_ioctl_desc xe_ioctls[] = {
	DRM_IOCTL_DEF_DRV(XE_DEVICE_QUERY, xe_query_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(XE_GEM_CREATE, xe_gem_create_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(XE_GEM_MMAP_OFFSET, xe_gem_mmap_offset_ioctl,
			  DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(XE_VM_CREATE, xe_vm_create_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(XE_VM_DESTROY, xe_vm_destroy_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(XE_VM_BIND, xe_vm_bind_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(XE_EXEC, xe_exec_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(XE_EXEC_QUEUE_CREATE, xe_exec_queue_create_ioctl,
			  DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(XE_EXEC_QUEUE_DESTROY, xe_exec_queue_destroy_ioctl,
			  DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(XE_EXEC_QUEUE_GET_PROPERTY, xe_exec_queue_get_property_ioctl,
			  DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(XE_WAIT_USER_FENCE, xe_wait_user_fence_ioctl,
			  DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(XE_OBSERVATION, xe_observation_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(XE_MADVISE, xe_vm_madvise_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(XE_VM_QUERY_MEM_RANGE_ATTRS, xe_vm_query_vmas_attrs_ioctl,
			  DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(XE_EXEC_QUEUE_SET_PROPERTY, xe_exec_queue_set_property_ioctl,
			  DRM_RENDER_ALLOW),
};

static long xe_drm_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct drm_file *file_priv = file->private_data;
	struct xe_device *xe = to_xe_device(file_priv->minor->dev);
	long ret;

	if (xe_device_wedged(xe))
		return -ECANCELED;

	ACQUIRE(xe_pm_runtime_ioctl, pm)(xe);
	ret = ACQUIRE_ERR(xe_pm_runtime_ioctl, &pm);
	if (ret >= 0)
		ret = drm_ioctl(file, cmd, arg);

	return ret;
}

#ifdef CONFIG_COMPAT
static long xe_drm_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct drm_file *file_priv = file->private_data;
	struct xe_device *xe = to_xe_device(file_priv->minor->dev);
	long ret;

	if (xe_device_wedged(xe))
		return -ECANCELED;

	ACQUIRE(xe_pm_runtime_ioctl, pm)(xe);
	ret = ACQUIRE_ERR(xe_pm_runtime_ioctl, &pm);
	if (ret >= 0)
		ret = drm_compat_ioctl(file, cmd, arg);

	return ret;
}
#else
/* similarly to drm_compat_ioctl, let's it be assigned to .compat_ioct unconditionally */
#define xe_drm_compat_ioctl NULL
#endif

static void barrier_open(struct vm_area_struct *vma)
{
	drm_dev_get(vma->vm_private_data);
}

static void barrier_close(struct vm_area_struct *vma)
{
	drm_dev_put(vma->vm_private_data);
}

static void barrier_release_dummy_page(struct drm_device *dev, void *res)
{
	struct page *dummy_page = (struct page *)res;

	__free_page(dummy_page);
}

static vm_fault_t barrier_fault(struct vm_fault *vmf)
{
	struct drm_device *dev = vmf->vma->vm_private_data;
	struct vm_area_struct *vma = vmf->vma;
	vm_fault_t ret = VM_FAULT_NOPAGE;
	pgprot_t prot;
	int idx;

	prot = vm_get_page_prot(vma->vm_flags);

	if (drm_dev_enter(dev, &idx)) {
		unsigned long pfn;

#define LAST_DB_PAGE_OFFSET 0x7ff001
		pfn = PHYS_PFN(pci_resource_start(to_pci_dev(dev->dev), 0) +
				LAST_DB_PAGE_OFFSET);
		ret = vmf_insert_pfn_prot(vma, vma->vm_start, pfn,
					  pgprot_noncached(prot));
		drm_dev_exit(idx);
	} else {
		struct page *page;

		/* Allocate new dummy page to map all the VA range in this VMA to it*/
		page = alloc_page(GFP_KERNEL | __GFP_ZERO);
		if (!page)
			return VM_FAULT_OOM;

		/* Set the page to be freed using drmm release action */
		if (drmm_add_action_or_reset(dev, barrier_release_dummy_page, page))
			return VM_FAULT_OOM;

		ret = vmf_insert_pfn_prot(vma, vma->vm_start, page_to_pfn(page),
					  prot);
	}

	return ret;
}

static const struct vm_operations_struct vm_ops_barrier = {
	.open = barrier_open,
	.close = barrier_close,
	.fault = barrier_fault,
};

static int xe_pci_barrier_mmap(struct file *filp,
			       struct vm_area_struct *vma)
{
	struct drm_file *priv = filp->private_data;
	struct drm_device *dev = priv->minor->dev;
	struct xe_device *xe = to_xe_device(dev);

	if (!IS_DGFX(xe))
		return -EINVAL;

	if (vma->vm_end - vma->vm_start > SZ_4K)
		return -EINVAL;

	if (is_cow_mapping(vma->vm_flags))
		return -EINVAL;

	if (vma->vm_flags & (VM_READ | VM_EXEC))
		return -EINVAL;

	vm_flags_clear(vma, VM_MAYREAD | VM_MAYEXEC);
	vm_flags_set(vma, VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP | VM_IO);
	vma->vm_ops = &vm_ops_barrier;
	vma->vm_private_data = dev;
	drm_dev_get(vma->vm_private_data);

	return 0;
}

static int xe_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_file *priv = filp->private_data;
	struct drm_device *dev = priv->minor->dev;

	if (drm_dev_is_unplugged(dev))
		return -ENODEV;

	switch (vma->vm_pgoff) {
	case XE_PCI_BARRIER_MMAP_OFFSET >> XE_PTE_SHIFT:
		return xe_pci_barrier_mmap(filp, vma);
	}

	return drm_gem_mmap(filp, vma);
}

static const struct file_operations xe_driver_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release_noglobal,
	.unlocked_ioctl = xe_drm_ioctl,
	.mmap = xe_mmap,
	.poll = drm_poll,
	.read = drm_read,
	.compat_ioctl = xe_drm_compat_ioctl,
	.llseek = noop_llseek,
#ifdef CONFIG_PROC_FS
	.show_fdinfo = drm_show_fdinfo,
#endif
	.fop_flags = FOP_UNSIGNED_OFFSET,
};

/**
 * xe_is_xe_file() - Is the file an xe device file?
 * @file: The file.
 *
 * Checks whether the file is opened against
 * an xe device.
 *
 * Return: %true if an xe file, %false if not.
 */
bool xe_is_xe_file(const struct file *file)
{
	return file->f_op == &xe_driver_fops;
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
	.ioctls = xe_ioctls,
	.num_ioctls = ARRAY_SIZE(xe_ioctls),
	.fops = &xe_driver_fops,
	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
};

static void xe_device_destroy(struct drm_device *dev, void *dummy)
{
	struct xe_device *xe = to_xe_device(dev);

	xe_bo_dev_fini(&xe->bo_device);

	if (xe->preempt_fence_wq)
		destroy_workqueue(xe->preempt_fence_wq);

	if (xe->ordered_wq)
		destroy_workqueue(xe->ordered_wq);

	if (xe->unordered_wq)
		destroy_workqueue(xe->unordered_wq);

	if (xe->destroy_wq)
		destroy_workqueue(xe->destroy_wq);

	ttm_device_fini(&xe->ttm);
}

struct xe_device *xe_device_create(struct pci_dev *pdev,
				   const struct pci_device_id *ent)
{
	struct xe_device *xe;
	int err;

	xe_display_driver_set_hooks(&driver);

	err = aperture_remove_conflicting_pci_devices(pdev, driver.name);
	if (err)
		return ERR_PTR(err);

	xe = devm_drm_dev_alloc(&pdev->dev, &driver, struct xe_device, drm);
	if (IS_ERR(xe))
		return xe;

	err = ttm_device_init(&xe->ttm, &xe_ttm_funcs, xe->drm.dev,
			      xe->drm.anon_inode->i_mapping,
			      xe->drm.vma_offset_manager, 0);
	if (WARN_ON(err))
		goto err;

	xe_bo_dev_init(&xe->bo_device);
	err = drmm_add_action_or_reset(&xe->drm, xe_device_destroy, NULL);
	if (err)
		goto err;

	err = xe_shrinker_create(xe);
	if (err)
		goto err;

	xe->info.devid = pdev->device;
	xe->info.revid = pdev->revision;
	xe->info.force_execlist = xe_modparam.force_execlist;
	xe->atomic_svm_timeslice_ms = 5;
	xe->min_run_period_lr_ms = 5;

	err = xe_irq_init(xe);
	if (err)
		goto err;

	xe_validation_device_init(&xe->val);

	init_waitqueue_head(&xe->ufence_wq);

	init_rwsem(&xe->usm.lock);

	err = xe_pagemap_shrinker_create(xe);
	if (err)
		goto err;

	xa_init_flags(&xe->usm.asid_to_vm, XA_FLAGS_ALLOC);

	if (IS_ENABLED(CONFIG_DRM_XE_DEBUG)) {
		/* Trigger a large asid and an early asid wrap. */
		u32 asid;

		BUILD_BUG_ON(XE_MAX_ASID < 2);
		err = xa_alloc_cyclic(&xe->usm.asid_to_vm, &asid, NULL,
				      XA_LIMIT(XE_MAX_ASID - 2, XE_MAX_ASID - 1),
				      &xe->usm.next_asid, GFP_KERNEL);
		drm_WARN_ON(&xe->drm, err);
		if (err >= 0)
			xa_erase(&xe->usm.asid_to_vm, asid);
	}

	err = xe_bo_pinned_init(xe);
	if (err)
		goto err;

	xe->preempt_fence_wq = alloc_ordered_workqueue("xe-preempt-fence-wq",
						       WQ_MEM_RECLAIM);
	xe->ordered_wq = alloc_ordered_workqueue("xe-ordered-wq", 0);
	xe->unordered_wq = alloc_workqueue("xe-unordered-wq", 0, 0);
	xe->destroy_wq = alloc_workqueue("xe-destroy-wq", 0, 0);
	if (!xe->ordered_wq || !xe->unordered_wq ||
	    !xe->preempt_fence_wq || !xe->destroy_wq) {
		/*
		 * Cleanup done in xe_device_destroy via
		 * drmm_add_action_or_reset register above
		 */
		drm_err(&xe->drm, "Failed to allocate xe workqueues\n");
		err = -ENOMEM;
		goto err;
	}

	err = drmm_mutex_init(&xe->drm, &xe->pmt.lock);
	if (err)
		goto err;

	return xe;

err:
	return ERR_PTR(err);
}
ALLOW_ERROR_INJECTION(xe_device_create, ERRNO); /* See xe_pci_probe() */

static bool xe_driver_flr_disabled(struct xe_device *xe)
{
	if (IS_SRIOV_VF(xe))
		return true;

	if (xe_mmio_read32(xe_root_tile_mmio(xe), GU_CNTL_PROTECTED) & DRIVERINT_FLR_DIS) {
		drm_info(&xe->drm, "Driver-FLR disabled by BIOS\n");
		return true;
	}

	return false;
}

/*
 * The driver-initiated FLR is the highest level of reset that we can trigger
 * from within the driver. It is different from the PCI FLR in that it doesn't
 * fully reset the SGUnit and doesn't modify the PCI config space and therefore
 * it doesn't require a re-enumeration of the PCI BARs. However, the
 * driver-initiated FLR does still cause a reset of both GT and display and a
 * memory wipe of local and stolen memory, so recovery would require a full HW
 * re-init and saving/restoring (or re-populating) the wiped memory. Since we
 * perform the FLR as the very last action before releasing access to the HW
 * during the driver release flow, we don't attempt recovery at all, because
 * if/when a new instance of Xe is bound to the device it will do a full
 * re-init anyway.
 */
static void __xe_driver_flr(struct xe_device *xe)
{
	const unsigned int flr_timeout = 3 * USEC_PER_SEC; /* specs recommend a 3s wait */
	struct xe_mmio *mmio = xe_root_tile_mmio(xe);
	int ret;

	drm_dbg(&xe->drm, "Triggering Driver-FLR\n");

	/*
	 * Make sure any pending FLR requests have cleared by waiting for the
	 * FLR trigger bit to go to zero. Also clear GU_DEBUG's DRIVERFLR_STATUS
	 * to make sure it's not still set from a prior attempt (it's a write to
	 * clear bit).
	 * Note that we should never be in a situation where a previous attempt
	 * is still pending (unless the HW is totally dead), but better to be
	 * safe in case something unexpected happens
	 */
	ret = xe_mmio_wait32(mmio, GU_CNTL, DRIVERFLR, 0, flr_timeout, NULL, false);
	if (ret) {
		drm_err(&xe->drm, "Driver-FLR-prepare wait for ready failed! %d\n", ret);
		return;
	}
	xe_mmio_write32(mmio, GU_DEBUG, DRIVERFLR_STATUS);

	/* Trigger the actual Driver-FLR */
	xe_mmio_rmw32(mmio, GU_CNTL, 0, DRIVERFLR);

	/* Wait for hardware teardown to complete */
	ret = xe_mmio_wait32(mmio, GU_CNTL, DRIVERFLR, 0, flr_timeout, NULL, false);
	if (ret) {
		drm_err(&xe->drm, "Driver-FLR-teardown wait completion failed! %d\n", ret);
		return;
	}

	/* Wait for hardware/firmware re-init to complete */
	ret = xe_mmio_wait32(mmio, GU_DEBUG, DRIVERFLR_STATUS, DRIVERFLR_STATUS,
			     flr_timeout, NULL, false);
	if (ret) {
		drm_err(&xe->drm, "Driver-FLR-reinit wait completion failed! %d\n", ret);
		return;
	}

	/* Clear sticky completion status */
	xe_mmio_write32(mmio, GU_DEBUG, DRIVERFLR_STATUS);
}

static void xe_driver_flr(struct xe_device *xe)
{
	if (xe_driver_flr_disabled(xe))
		return;

	__xe_driver_flr(xe);
}

static void xe_driver_flr_fini(void *arg)
{
	struct xe_device *xe = arg;

	if (xe->needs_flr_on_fini)
		xe_driver_flr(xe);
}

static void xe_device_sanitize(void *arg)
{
	struct xe_device *xe = arg;
	struct xe_gt *gt;
	u8 id;

	for_each_gt(gt, xe, id)
		xe_gt_sanitize(gt);
}

static int xe_set_dma_info(struct xe_device *xe)
{
	unsigned int mask_size = xe->info.dma_mask_size;
	int err;

	dma_set_max_seg_size(xe->drm.dev, xe_sg_segment_size(xe->drm.dev));

	err = dma_set_mask(xe->drm.dev, DMA_BIT_MASK(mask_size));
	if (err)
		goto mask_err;

	err = dma_set_coherent_mask(xe->drm.dev, DMA_BIT_MASK(mask_size));
	if (err)
		goto mask_err;

	return 0;

mask_err:
	drm_err(&xe->drm, "Can't set DMA mask/consistent mask (%d)\n", err);
	return err;
}

static void assert_lmem_ready(struct xe_device *xe)
{
	if (!IS_DGFX(xe) || IS_SRIOV_VF(xe))
		return;

	xe_assert(xe, xe_mmio_read32(xe_root_tile_mmio(xe), GU_CNTL) &
		  LMEM_INIT);
}

static void vf_update_device_info(struct xe_device *xe)
{
	xe_assert(xe, IS_SRIOV_VF(xe));
	/* disable features that are not available/applicable to VFs */
	xe->info.probe_display = 0;
	xe->info.has_heci_cscfi = 0;
	xe->info.has_heci_gscfi = 0;
	xe->info.has_late_bind = 0;
	xe->info.skip_guc_pc = 1;
	xe->info.skip_pcode = 1;
}

static int xe_device_vram_alloc(struct xe_device *xe)
{
	struct xe_vram_region *vram;

	if (!IS_DGFX(xe))
		return 0;

	vram = drmm_kzalloc(&xe->drm, sizeof(*vram), GFP_KERNEL);
	if (!vram)
		return -ENOMEM;

	xe->mem.vram = vram;
	return 0;
}

/**
 * xe_device_probe_early: Device early probe
 * @xe: xe device instance
 *
 * Initialize MMIO resources that don't require any
 * knowledge about tile count. Also initialize pcode and
 * check vram initialization on root tile.
 *
 * Return: 0 on success, error code on failure
 */
int xe_device_probe_early(struct xe_device *xe)
{
	int err;

	xe_wa_device_init(xe);
	xe_wa_process_device_oob(xe);

	err = xe_mmio_probe_early(xe);
	if (err)
		return err;

	xe_sriov_probe_early(xe);

	if (IS_SRIOV_VF(xe))
		vf_update_device_info(xe);

	/*
	 * Check for pcode uncore_init status to confirm if the SoC
	 * initialization is complete. Until done, any MMIO or lmem access from
	 * the driver will be blocked
	 */
	err = xe_pcode_probe_early(xe);
	if (err || xe_survivability_mode_is_requested(xe)) {
		int save_err = err;

		/*
		 * Try to leave device in survivability mode if device is
		 * possible, but still return the previous error for error
		 * propagation
		 */
		err = xe_survivability_mode_boot_enable(xe);
		if (err)
			return err;

		return save_err;
	}

	/*
	 * Make sure the lmem is initialized and ready to use. xe_pcode_ready()
	 * is flagged after full initialization is complete. Assert if lmem is
	 * not initialized.
	 */
	assert_lmem_ready(xe);

	xe->wedged.mode = xe_device_validate_wedged_mode(xe, xe_modparam.wedged_mode) ?
			  XE_WEDGED_MODE_DEFAULT : xe_modparam.wedged_mode;
	drm_dbg(&xe->drm, "wedged_mode: setting mode (%u) %s\n",
		xe->wedged.mode, xe_wedged_mode_to_string(xe->wedged.mode));

	err = xe_device_vram_alloc(xe);
	if (err)
		return err;

	return 0;
}
ALLOW_ERROR_INJECTION(xe_device_probe_early, ERRNO); /* See xe_pci_probe() */

static int probe_has_flat_ccs(struct xe_device *xe)
{
	struct xe_gt *gt;
	u32 reg;

	/* Always enabled/disabled, no runtime check to do */
	if (GRAPHICS_VER(xe) < 20 || !xe->info.has_flat_ccs || IS_SRIOV_VF(xe))
		return 0;

	gt = xe_root_mmio_gt(xe);
	if (!gt)
		return 0;

	CLASS(xe_force_wake, fw_ref)(gt_to_fw(gt), XE_FW_GT);
	if (!fw_ref.domains)
		return -ETIMEDOUT;

	reg = xe_gt_mcr_unicast_read_any(gt, XE2_FLAT_CCS_BASE_RANGE_LOWER);
	xe->info.has_flat_ccs = (reg & XE2_FLAT_CCS_ENABLE);

	if (!xe->info.has_flat_ccs)
		drm_dbg(&xe->drm,
			"Flat CCS has been disabled in bios, May lead to performance impact");

	return 0;
}

/*
 * Detect if the driver is being run on pre-production hardware.  We don't
 * keep workarounds for pre-production hardware long term, so print an
 * error and add taint if we're being loaded on a pre-production platform
 * for which the pre-prod workarounds have already been removed.
 *
 * The general policy is that we'll remove any workarounds that only apply to
 * pre-production hardware around the time force_probe restrictions are lifted
 * for a platform of the next major IP generation (for example, Xe2 pre-prod
 * workarounds should be removed around the time the first Xe3 platforms have
 * force_probe lifted).
 */
static void detect_preproduction_hw(struct xe_device *xe)
{
	struct xe_gt *gt;
	int id;

	/*
	 * SR-IOV VFs don't have access to the FUSE2 register, so we can't
	 * check pre-production status there.  But the host OS will notice
	 * and report the pre-production status, which should be enough to
	 * help us catch mistaken use of pre-production hardware.
	 */
	if (IS_SRIOV_VF(xe))
		return;

	/*
	 * The "SW_CAP" fuse contains a bit indicating whether the device is a
	 * production or pre-production device.  This fuse is reflected through
	 * the GT "FUSE2" register, even though the contents of the fuse are
	 * not GT-specific.  Every GT's reflection of this fuse should show the
	 * same value, so we'll just use the first available GT for lookup.
	 */
	for_each_gt(gt, xe, id)
		break;

	if (!gt)
		return;

	CLASS(xe_force_wake, fw_ref)(gt_to_fw(gt), XE_FW_GT);
	if (!xe_force_wake_ref_has_domain(fw_ref.domains, XE_FW_GT)) {
		xe_gt_err(gt, "Forcewake failure; cannot determine production/pre-production hw status.\n");
		return;
	}

	if (xe_mmio_read32(&gt->mmio, FUSE2) & PRODUCTION_HW)
		return;

	xe_info(xe, "Pre-production hardware detected.\n");
	if (!xe->info.has_pre_prod_wa) {
		xe_err(xe, "Pre-production workarounds for this platform have already been removed.\n");
		add_taint(TAINT_MACHINE_CHECK, LOCKDEP_STILL_OK);
	}
}

int xe_device_probe(struct xe_device *xe)
{
	struct xe_tile *tile;
	struct xe_gt *gt;
	int err;
	u8 id;

	xe_pat_init_early(xe);

	err = xe_sriov_init(xe);
	if (err)
		return err;

	xe->info.mem_region_mask = 1;

	err = xe_set_dma_info(xe);
	if (err)
		return err;

	err = xe_mmio_probe_tiles(xe);
	if (err)
		return err;

	for_each_gt(gt, xe, id) {
		err = xe_gt_init_early(gt);
		if (err)
			return err;
	}

	for_each_tile(tile, xe, id) {
		err = xe_ggtt_init_early(tile->mem.ggtt);
		if (err)
			return err;
	}

	/*
	 * From here on, if a step fails, make sure a Driver-FLR is triggereed
	 */
	err = devm_add_action_or_reset(xe->drm.dev, xe_driver_flr_fini, xe);
	if (err)
		return err;

	err = probe_has_flat_ccs(xe);
	if (err)
		return err;

	err = xe_vram_probe(xe);
	if (err)
		return err;

	for_each_tile(tile, xe, id) {
		err = xe_tile_init_noalloc(tile);
		if (err)
			return err;
	}

	/*
	 * Allow allocations only now to ensure xe_display_init_early()
	 * is the first to allocate, always.
	 */
	err = xe_ttm_sys_mgr_init(xe);
	if (err)
		return err;

	/* Allocate and map stolen after potential VRAM resize */
	err = xe_ttm_stolen_mgr_init(xe);
	if (err)
		return err;

	/*
	 * Now that GT is initialized (TTM in particular),
	 * we can try to init display, and inherit the initial fb.
	 * This is the reason the first allocation needs to be done
	 * inside display.
	 */
	err = xe_display_init_early(xe);
	if (err)
		return err;

	for_each_tile(tile, xe, id) {
		err = xe_tile_init(tile);
		if (err)
			return err;
	}

	err = xe_irq_install(xe);
	if (err)
		return err;

	for_each_gt(gt, xe, id) {
		err = xe_gt_init(gt);
		if (err)
			return err;
	}

	err = xe_pagefault_init(xe);
	if (err)
		return err;

	if (xe->tiles->media_gt &&
	    XE_GT_WA(xe->tiles->media_gt, 15015404425_disable))
		XE_DEVICE_WA_DISABLE(xe, 15015404425);

	err = xe_devcoredump_init(xe);
	if (err)
		return err;

	xe_nvm_init(xe);

	err = xe_soc_remapper_init(xe);
	if (err)
		return err;

	err = xe_heci_gsc_init(xe);
	if (err)
		return err;

	err = xe_late_bind_init(&xe->late_bind);
	if (err)
		return err;

	err = xe_oa_init(xe);
	if (err)
		return err;

	err = xe_display_init(xe);
	if (err)
		return err;

	err = xe_pxp_init(xe);
	if (err)
		return err;

	err = xe_psmi_init(xe);
	if (err)
		return err;

	err = drm_dev_register(&xe->drm, 0);
	if (err)
		return err;

	xe_display_register(xe);

	err = xe_oa_register(xe);
	if (err)
		goto err_unregister_display;

	err = xe_pmu_register(&xe->pmu);
	if (err)
		goto err_unregister_display;

	err = xe_device_sysfs_init(xe);
	if (err)
		goto err_unregister_display;

	xe_debugfs_register(xe);

	err = xe_hwmon_register(xe);
	if (err)
		goto err_unregister_display;

	err = xe_i2c_probe(xe);
	if (err)
		goto err_unregister_display;

	for_each_gt(gt, xe, id)
		xe_gt_sanitize_freq(gt);

	xe_vsec_init(xe);

	err = xe_sriov_init_late(xe);
	if (err)
		goto err_unregister_display;

	detect_preproduction_hw(xe);

	return devm_add_action_or_reset(xe->drm.dev, xe_device_sanitize, xe);

err_unregister_display:
	xe_display_unregister(xe);
	drm_dev_unregister(&xe->drm);

	return err;
}

void xe_device_remove(struct xe_device *xe)
{
	xe_display_unregister(xe);

	drm_dev_unplug(&xe->drm);

	xe_bo_pci_dev_remove_all(xe);
}

void xe_device_shutdown(struct xe_device *xe)
{
	struct xe_gt *gt;
	u8 id;

	drm_dbg(&xe->drm, "Shutting down device\n");

	xe_display_pm_shutdown(xe);

	xe_irq_suspend(xe);

	for_each_gt(gt, xe, id)
		xe_gt_shutdown(gt);

	xe_display_pm_shutdown_late(xe);

	if (!xe_driver_flr_disabled(xe)) {
		/* BOOM! */
		__xe_driver_flr(xe);
	}
}

/**
 * xe_device_wmb() - Device specific write memory barrier
 * @xe: the &xe_device
 *
 * While wmb() is sufficient for a barrier if we use system memory, on discrete
 * platforms with device memory we additionally need to issue a register write.
 * Since it doesn't matter which register we write to, use the read-only VF_CAP
 * register that is also marked as accessible by the VFs.
 */
void xe_device_wmb(struct xe_device *xe)
{
	wmb();
	if (IS_DGFX(xe))
		xe_mmio_write32(xe_root_tile_mmio(xe), VF_CAP_REG, 0);
}

/*
 * Issue a TRANSIENT_FLUSH_REQUEST and wait for completion on each gt.
 */
static void tdf_request_sync(struct xe_device *xe)
{
	struct xe_gt *gt;
	u8 id;

	for_each_gt(gt, xe, id) {
		if (xe_gt_is_media_type(gt))
			continue;

		CLASS(xe_force_wake, fw_ref)(gt_to_fw(gt), XE_FW_GT);
		if (!fw_ref.domains)
			return;

		xe_mmio_write32(&gt->mmio, XE2_TDF_CTRL, TRANSIENT_FLUSH_REQUEST);

		/*
		 * FIXME: We can likely do better here with our choice of
		 * timeout. Currently we just assume the worst case, i.e. 150us,
		 * which is believed to be sufficient to cover the worst case
		 * scenario on current platforms if all cache entries are
		 * transient and need to be flushed..
		 */
		if (xe_mmio_wait32(&gt->mmio, XE2_TDF_CTRL, TRANSIENT_FLUSH_REQUEST, 0,
				   300, NULL, false))
			xe_gt_err_once(gt, "TD flush timeout\n");
	}
}

void xe_device_l2_flush(struct xe_device *xe)
{
	struct xe_gt *gt;

	gt = xe_root_mmio_gt(xe);
	if (!gt)
		return;

	if (!XE_GT_WA(gt, 16023588340))
		return;

	CLASS(xe_force_wake, fw_ref)(gt_to_fw(gt), XE_FW_GT);
	if (!fw_ref.domains)
		return;

	spin_lock(&gt->global_invl_lock);

	xe_mmio_write32(&gt->mmio, XE2_GLOBAL_INVAL, 0x1);
	if (xe_mmio_wait32(&gt->mmio, XE2_GLOBAL_INVAL, 0x1, 0x0, 1000, NULL, true))
		xe_gt_err_once(gt, "Global invalidation timeout\n");

	spin_unlock(&gt->global_invl_lock);
}

/**
 * xe_device_td_flush() - Flush transient L3 cache entries
 * @xe: The device
 *
 * Display engine has direct access to memory and is never coherent with L3/L4
 * caches (or CPU caches), however KMD is responsible for specifically flushing
 * transient L3 GPU cache entries prior to the flip sequence to ensure scanout
 * can happen from such a surface without seeing corruption.
 *
 * Display surfaces can be tagged as transient by mapping it using one of the
 * various L3:XD PAT index modes on Xe2.
 *
 * Note: On non-discrete xe2 platforms, like LNL, the entire L3 cache is flushed
 * at the end of each submission via PIPE_CONTROL for compute/render, since SA
 * Media is not coherent with L3 and we want to support render-vs-media
 * usescases. For other engines like copy/blt the HW internally forces uncached
 * behaviour, hence why we can skip the TDF on such platforms.
 */
void xe_device_td_flush(struct xe_device *xe)
{
	struct xe_gt *root_gt;

	if (!IS_DGFX(xe) || GRAPHICS_VER(xe) < 20)
		return;

	root_gt = xe_root_mmio_gt(xe);
	if (!root_gt)
		return;

	if (XE_GT_WA(root_gt, 16023588340)) {
		/* A transient flush is not sufficient: flush the L2 */
		xe_device_l2_flush(xe);
	} else {
		xe_guc_pc_apply_flush_freq_limit(&root_gt->uc.guc.pc);
		tdf_request_sync(xe);
		xe_guc_pc_remove_flush_freq_limit(&root_gt->uc.guc.pc);
	}
}

u32 xe_device_ccs_bytes(struct xe_device *xe, u64 size)
{
	return xe_device_has_flat_ccs(xe) ?
		DIV_ROUND_UP_ULL(size, NUM_BYTES_PER_CCS_BYTE(xe)) : 0;
}

/**
 * xe_device_assert_mem_access - Inspect the current runtime_pm state.
 * @xe: xe device instance
 *
 * To be used before any kind of memory access. It will splat a debug warning
 * if the device is currently sleeping. But it doesn't guarantee in any way
 * that the device is going to remain awake. Xe PM runtime get and put
 * functions might be added to the outer bound of the memory access, while
 * this check is intended for inner usage to splat some warning if the worst
 * case has just happened.
 */
void xe_device_assert_mem_access(struct xe_device *xe)
{
	xe_assert(xe, !xe_pm_runtime_suspended(xe));
}

void xe_device_snapshot_print(struct xe_device *xe, struct drm_printer *p)
{
	struct xe_gt *gt;
	u8 id;

	drm_printf(p, "PCI ID: 0x%04x\n", xe->info.devid);
	drm_printf(p, "PCI revision: 0x%02x\n", xe->info.revid);

	for_each_gt(gt, xe, id) {
		drm_printf(p, "GT id: %u\n", id);
		drm_printf(p, "\tTile: %u\n", gt->tile->id);
		drm_printf(p, "\tType: %s\n",
			   gt->info.type == XE_GT_TYPE_MAIN ? "main" : "media");
		drm_printf(p, "\tIP ver: %u.%u.%u\n",
			   REG_FIELD_GET(GMD_ID_ARCH_MASK, gt->info.gmdid),
			   REG_FIELD_GET(GMD_ID_RELEASE_MASK, gt->info.gmdid),
			   REG_FIELD_GET(GMD_ID_REVID, gt->info.gmdid));
		drm_printf(p, "\tCS reference clock: %u\n", gt->info.reference_clock);
	}
}

u64 xe_device_canonicalize_addr(struct xe_device *xe, u64 address)
{
	return sign_extend64(address, xe->info.va_bits - 1);
}

u64 xe_device_uncanonicalize_addr(struct xe_device *xe, u64 address)
{
	return address & GENMASK_ULL(xe->info.va_bits - 1, 0);
}

static void xe_device_wedged_fini(struct drm_device *drm, void *arg)
{
	struct xe_device *xe = arg;

	xe_pm_runtime_put(xe);
}

/**
 * DOC: Xe Device Wedging
 *
 * Xe driver uses drm device wedged uevent as documented in Documentation/gpu/drm-uapi.rst.
 * When device is in wedged state, every IOCTL will be blocked and GT cannot
 * be used. The conditions under which the driver declares the device wedged
 * depend on the wedged mode configuration (see &enum xe_wedged_mode). The
 * default recovery method for a wedged state is rebind/bus-reset.
 *
 * Another recovery method is vendor-specific. Below are the cases that send
 * ``WEDGED=vendor-specific`` recovery method in drm device wedged uevent.
 *
 * Case: Firmware Flash
 * --------------------
 *
 * Identification Hint
 * +++++++++++++++++++
 *
 * ``WEDGED=vendor-specific`` drm device wedged uevent with
 * :ref:`Runtime Survivability mode <xe-survivability-mode>` is used to notify
 * admin/userspace consumer about the need for a firmware flash.
 *
 * Recovery Procedure
 * ++++++++++++++++++
 *
 * Once ``WEDGED=vendor-specific`` drm device wedged uevent is received, follow
 * the below steps
 *
 * - Check Runtime Survivability mode sysfs.
 *   If enabled, firmware flash is required to recover the device.
 *
 *   /sys/bus/pci/devices/<device>/survivability_mode
 *
 * - Admin/userspace consumer can use firmware flashing tools like fwupd to flash
 *   firmware and restore device to normal operation.
 */

/**
 * xe_device_set_wedged_method - Set wedged recovery method
 * @xe: xe device instance
 * @method: recovery method to set
 *
 * Set wedged recovery method to be sent in drm wedged uevent.
 */
void xe_device_set_wedged_method(struct xe_device *xe, unsigned long method)
{
	xe->wedged.method = method;
}

/**
 * xe_device_declare_wedged - Declare device wedged
 * @xe: xe device instance
 *
 * This is a final state that can only be cleared with the recovery method
 * specified in the drm wedged uevent. The method can be set using
 * xe_device_set_wedged_method before declaring the device as wedged. If no method
 * is set, reprobe (unbind/re-bind) will be sent by default.
 *
 * In this state every IOCTL will be blocked so the GT cannot be used.
 * In general it will be called upon any critical error such as gt reset
 * failure or guc loading failure. Userspace will be notified of this state
 * through device wedged uevent.
 * If xe.wedged module parameter is set to 2, this function will be called
 * on every single execution timeout (a.k.a. GPU hang) right after devcoredump
 * snapshot capture. In this mode, GT reset won't be attempted so the state of
 * the issue is preserved for further debugging.
 */
void xe_device_declare_wedged(struct xe_device *xe)
{
	struct xe_gt *gt;
	u8 id;

	if (xe->wedged.mode == XE_WEDGED_MODE_NEVER) {
		drm_dbg(&xe->drm, "Wedged mode is forcibly disabled\n");
		return;
	}

	xe_pm_runtime_get_noresume(xe);

	if (drmm_add_action_or_reset(&xe->drm, xe_device_wedged_fini, xe)) {
		drm_err(&xe->drm, "Failed to register xe_device_wedged_fini clean-up. Although device is wedged.\n");
		return;
	}

	if (!atomic_xchg(&xe->wedged.flag, 1)) {
		xe->needs_flr_on_fini = true;
		drm_err(&xe->drm,
			"CRITICAL: Xe has declared device %s as wedged.\n"
			"IOCTLs and executions are blocked. Only a rebind may clear the failure\n"
			"Please file a _new_ bug report at https://gitlab.freedesktop.org/drm/xe/kernel/issues/new\n",
			dev_name(xe->drm.dev));
	}

	for_each_gt(gt, xe, id)
		xe_gt_declare_wedged(gt);

	if (xe_device_wedged(xe)) {
		/* If no wedge recovery method is set, use default */
		if (!xe->wedged.method)
			xe_device_set_wedged_method(xe, DRM_WEDGE_RECOVERY_REBIND |
						    DRM_WEDGE_RECOVERY_BUS_RESET);

		/* Notify userspace of wedged device */
		drm_dev_wedged_event(&xe->drm, xe->wedged.method, NULL);
	}
}

/**
 * xe_device_validate_wedged_mode - Check if given mode is supported
 * @xe: the &xe_device
 * @mode: requested mode to validate
 *
 * Check whether the provided wedged mode is supported.
 *
 * Return: 0 if mode is supported, error code otherwise.
 */
int xe_device_validate_wedged_mode(struct xe_device *xe, unsigned int mode)
{
	if (mode > XE_WEDGED_MODE_UPON_ANY_HANG_NO_RESET) {
		drm_dbg(&xe->drm, "wedged_mode: invalid value (%u)\n", mode);
		return -EINVAL;
	} else if (mode == XE_WEDGED_MODE_UPON_ANY_HANG_NO_RESET && (IS_SRIOV_VF(xe) ||
		   (IS_SRIOV_PF(xe) && !IS_ENABLED(CONFIG_DRM_XE_DEBUG)))) {
		drm_dbg(&xe->drm, "wedged_mode: (%u) %s mode is not supported for %s\n",
			mode, xe_wedged_mode_to_string(mode),
			xe_sriov_mode_to_string(xe_device_sriov_mode(xe)));
		return -EPERM;
	}

	return 0;
}

/**
 * xe_wedged_mode_to_string - Convert enum value to string.
 * @mode: the &xe_wedged_mode to convert
 *
 * Returns: wedged mode as a user friendly string.
 */
const char *xe_wedged_mode_to_string(enum xe_wedged_mode mode)
{
	switch (mode) {
	case XE_WEDGED_MODE_NEVER:
		return "never";
	case XE_WEDGED_MODE_UPON_CRITICAL_ERROR:
		return "upon-critical-error";
	case XE_WEDGED_MODE_UPON_ANY_HANG_NO_RESET:
		return "upon-any-hang-no-reset";
	default:
		return "<invalid>";
	}
}
