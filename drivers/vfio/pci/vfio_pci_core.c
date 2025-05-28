// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Red Hat, Inc.  All rights reserved.
 *     Author: Alex Williamson <alex.williamson@redhat.com>
 *
 * Derived from original vfio:
 * Copyright 2010 Cisco Systems, Inc.  All rights reserved.
 * Author: Tom Lyon, pugs@cisco.com
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/aperture.h>
#include <linux/device.h>
#include <linux/eventfd.h>
#include <linux/file.h>
#include <linux/interrupt.h>
#include <linux/iommu.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/pci.h>
#include <linux/pfn_t.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/vgaarb.h>
#include <linux/nospec.h>
#include <linux/sched/mm.h>
#include <linux/iommufd.h>
#if IS_ENABLED(CONFIG_EEH)
#include <asm/eeh.h>
#endif

#include "vfio_pci_priv.h"

#define DRIVER_AUTHOR   "Alex Williamson <alex.williamson@redhat.com>"
#define DRIVER_DESC "core driver for VFIO based PCI devices"

static bool nointxmask;
static bool disable_vga;
static bool disable_idle_d3;

/* List of PF's that vfio_pci_core_sriov_configure() has been called on */
static DEFINE_MUTEX(vfio_pci_sriov_pfs_mutex);
static LIST_HEAD(vfio_pci_sriov_pfs);

struct vfio_pci_dummy_resource {
	struct resource		resource;
	int			index;
	struct list_head	res_next;
};

struct vfio_pci_vf_token {
	struct mutex		lock;
	uuid_t			uuid;
	int			users;
};

static inline bool vfio_vga_disabled(void)
{
#ifdef CONFIG_VFIO_PCI_VGA
	return disable_vga;
#else
	return true;
#endif
}

/*
 * Our VGA arbiter participation is limited since we don't know anything
 * about the device itself.  However, if the device is the only VGA device
 * downstream of a bridge and VFIO VGA support is disabled, then we can
 * safely return legacy VGA IO and memory as not decoded since the user
 * has no way to get to it and routing can be disabled externally at the
 * bridge.
 */
static unsigned int vfio_pci_set_decode(struct pci_dev *pdev, bool single_vga)
{
	struct pci_dev *tmp = NULL;
	unsigned char max_busnr;
	unsigned int decodes;

	if (single_vga || !vfio_vga_disabled() || pci_is_root_bus(pdev->bus))
		return VGA_RSRC_NORMAL_IO | VGA_RSRC_NORMAL_MEM |
		       VGA_RSRC_LEGACY_IO | VGA_RSRC_LEGACY_MEM;

	max_busnr = pci_bus_max_busnr(pdev->bus);
	decodes = VGA_RSRC_NORMAL_IO | VGA_RSRC_NORMAL_MEM;

	while ((tmp = pci_get_class(PCI_CLASS_DISPLAY_VGA << 8, tmp)) != NULL) {
		if (tmp == pdev ||
		    pci_domain_nr(tmp->bus) != pci_domain_nr(pdev->bus) ||
		    pci_is_root_bus(tmp->bus))
			continue;

		if (tmp->bus->number >= pdev->bus->number &&
		    tmp->bus->number <= max_busnr) {
			pci_dev_put(tmp);
			decodes |= VGA_RSRC_LEGACY_IO | VGA_RSRC_LEGACY_MEM;
			break;
		}
	}

	return decodes;
}

static void vfio_pci_probe_mmaps(struct vfio_pci_core_device *vdev)
{
	struct resource *res;
	int i;
	struct vfio_pci_dummy_resource *dummy_res;

	for (i = 0; i < PCI_STD_NUM_BARS; i++) {
		int bar = i + PCI_STD_RESOURCES;

		res = &vdev->pdev->resource[bar];

		if (vdev->pdev->non_mappable_bars)
			goto no_mmap;

		if (!(res->flags & IORESOURCE_MEM))
			goto no_mmap;

		/*
		 * The PCI core shouldn't set up a resource with a
		 * type but zero size. But there may be bugs that
		 * cause us to do that.
		 */
		if (!resource_size(res))
			goto no_mmap;

		if (resource_size(res) >= PAGE_SIZE) {
			vdev->bar_mmap_supported[bar] = true;
			continue;
		}

		if (!(res->start & ~PAGE_MASK)) {
			/*
			 * Add a dummy resource to reserve the remainder
			 * of the exclusive page in case that hot-add
			 * device's bar is assigned into it.
			 */
			dummy_res =
				kzalloc(sizeof(*dummy_res), GFP_KERNEL_ACCOUNT);
			if (dummy_res == NULL)
				goto no_mmap;

			dummy_res->resource.name = "vfio sub-page reserved";
			dummy_res->resource.start = res->end + 1;
			dummy_res->resource.end = res->start + PAGE_SIZE - 1;
			dummy_res->resource.flags = res->flags;
			if (request_resource(res->parent,
						&dummy_res->resource)) {
				kfree(dummy_res);
				goto no_mmap;
			}
			dummy_res->index = bar;
			list_add(&dummy_res->res_next,
					&vdev->dummy_resources_list);
			vdev->bar_mmap_supported[bar] = true;
			continue;
		}
		/*
		 * Here we don't handle the case when the BAR is not page
		 * aligned because we can't expect the BAR will be
		 * assigned into the same location in a page in guest
		 * when we passthrough the BAR. And it's hard to access
		 * this BAR in userspace because we have no way to get
		 * the BAR's location in a page.
		 */
no_mmap:
		vdev->bar_mmap_supported[bar] = false;
	}
}

struct vfio_pci_group_info;
static void vfio_pci_dev_set_try_reset(struct vfio_device_set *dev_set);
static int vfio_pci_dev_set_hot_reset(struct vfio_device_set *dev_set,
				      struct vfio_pci_group_info *groups,
				      struct iommufd_ctx *iommufd_ctx);

/*
 * INTx masking requires the ability to disable INTx signaling via PCI_COMMAND
 * _and_ the ability detect when the device is asserting INTx via PCI_STATUS.
 * If a device implements the former but not the latter we would typically
 * expect broken_intx_masking be set and require an exclusive interrupt.
 * However since we do have control of the device's ability to assert INTx,
 * we can instead pretend that the device does not implement INTx, virtualizing
 * the pin register to report zero and maintaining DisINTx set on the host.
 */
static bool vfio_pci_nointx(struct pci_dev *pdev)
{
	switch (pdev->vendor) {
	case PCI_VENDOR_ID_INTEL:
		switch (pdev->device) {
		/* All i40e (XL710/X710/XXV710) 10/20/25/40GbE NICs */
		case 0x1572:
		case 0x1574:
		case 0x1580 ... 0x1581:
		case 0x1583 ... 0x158b:
		case 0x37d0 ... 0x37d2:
		/* X550 */
		case 0x1563:
			return true;
		default:
			return false;
		}
	}

	return false;
}

static void vfio_pci_probe_power_state(struct vfio_pci_core_device *vdev)
{
	struct pci_dev *pdev = vdev->pdev;
	u16 pmcsr;

	if (!pdev->pm_cap)
		return;

	pci_read_config_word(pdev, pdev->pm_cap + PCI_PM_CTRL, &pmcsr);

	vdev->needs_pm_restore = !(pmcsr & PCI_PM_CTRL_NO_SOFT_RESET);
}

/*
 * pci_set_power_state() wrapper handling devices which perform a soft reset on
 * D3->D0 transition.  Save state prior to D0/1/2->D3, stash it on the vdev,
 * restore when returned to D0.  Saved separately from pci_saved_state for use
 * by PM capability emulation and separately from pci_dev internal saved state
 * to avoid it being overwritten and consumed around other resets.
 */
int vfio_pci_set_power_state(struct vfio_pci_core_device *vdev, pci_power_t state)
{
	struct pci_dev *pdev = vdev->pdev;
	bool needs_restore = false, needs_save = false;
	int ret;

	/* Prevent changing power state for PFs with VFs enabled */
	if (pci_num_vf(pdev) && state > PCI_D0)
		return -EBUSY;

	if (vdev->needs_pm_restore) {
		if (pdev->current_state < PCI_D3hot && state >= PCI_D3hot) {
			pci_save_state(pdev);
			needs_save = true;
		}

		if (pdev->current_state >= PCI_D3hot && state <= PCI_D0)
			needs_restore = true;
	}

	ret = pci_set_power_state(pdev, state);

	if (!ret) {
		/* D3 might be unsupported via quirk, skip unless in D3 */
		if (needs_save && pdev->current_state >= PCI_D3hot) {
			/*
			 * The current PCI state will be saved locally in
			 * 'pm_save' during the D3hot transition. When the
			 * device state is changed to D0 again with the current
			 * function, then pci_store_saved_state() will restore
			 * the state and will free the memory pointed by
			 * 'pm_save'. There are few cases where the PCI power
			 * state can be changed to D0 without the involvement
			 * of the driver. For these cases, free the earlier
			 * allocated memory first before overwriting 'pm_save'
			 * to prevent the memory leak.
			 */
			kfree(vdev->pm_save);
			vdev->pm_save = pci_store_saved_state(pdev);
		} else if (needs_restore) {
			pci_load_and_free_saved_state(pdev, &vdev->pm_save);
			pci_restore_state(pdev);
		}
	}

	return ret;
}

static int vfio_pci_runtime_pm_entry(struct vfio_pci_core_device *vdev,
				     struct eventfd_ctx *efdctx)
{
	/*
	 * The vdev power related flags are protected with 'memory_lock'
	 * semaphore.
	 */
	vfio_pci_zap_and_down_write_memory_lock(vdev);
	if (vdev->pm_runtime_engaged) {
		up_write(&vdev->memory_lock);
		return -EINVAL;
	}

	vdev->pm_runtime_engaged = true;
	vdev->pm_wake_eventfd_ctx = efdctx;
	pm_runtime_put_noidle(&vdev->pdev->dev);
	up_write(&vdev->memory_lock);

	return 0;
}

static int vfio_pci_core_pm_entry(struct vfio_device *device, u32 flags,
				  void __user *arg, size_t argsz)
{
	struct vfio_pci_core_device *vdev =
		container_of(device, struct vfio_pci_core_device, vdev);
	int ret;

	ret = vfio_check_feature(flags, argsz, VFIO_DEVICE_FEATURE_SET, 0);
	if (ret != 1)
		return ret;

	/*
	 * Inside vfio_pci_runtime_pm_entry(), only the runtime PM usage count
	 * will be decremented. The pm_runtime_put() will be invoked again
	 * while returning from the ioctl and then the device can go into
	 * runtime suspended state.
	 */
	return vfio_pci_runtime_pm_entry(vdev, NULL);
}

static int vfio_pci_core_pm_entry_with_wakeup(
	struct vfio_device *device, u32 flags,
	struct vfio_device_low_power_entry_with_wakeup __user *arg,
	size_t argsz)
{
	struct vfio_pci_core_device *vdev =
		container_of(device, struct vfio_pci_core_device, vdev);
	struct vfio_device_low_power_entry_with_wakeup entry;
	struct eventfd_ctx *efdctx;
	int ret;

	ret = vfio_check_feature(flags, argsz, VFIO_DEVICE_FEATURE_SET,
				 sizeof(entry));
	if (ret != 1)
		return ret;

	if (copy_from_user(&entry, arg, sizeof(entry)))
		return -EFAULT;

	if (entry.wakeup_eventfd < 0)
		return -EINVAL;

	efdctx = eventfd_ctx_fdget(entry.wakeup_eventfd);
	if (IS_ERR(efdctx))
		return PTR_ERR(efdctx);

	ret = vfio_pci_runtime_pm_entry(vdev, efdctx);
	if (ret)
		eventfd_ctx_put(efdctx);

	return ret;
}

static void __vfio_pci_runtime_pm_exit(struct vfio_pci_core_device *vdev)
{
	if (vdev->pm_runtime_engaged) {
		vdev->pm_runtime_engaged = false;
		pm_runtime_get_noresume(&vdev->pdev->dev);

		if (vdev->pm_wake_eventfd_ctx) {
			eventfd_ctx_put(vdev->pm_wake_eventfd_ctx);
			vdev->pm_wake_eventfd_ctx = NULL;
		}
	}
}

static void vfio_pci_runtime_pm_exit(struct vfio_pci_core_device *vdev)
{
	/*
	 * The vdev power related flags are protected with 'memory_lock'
	 * semaphore.
	 */
	down_write(&vdev->memory_lock);
	__vfio_pci_runtime_pm_exit(vdev);
	up_write(&vdev->memory_lock);
}

static int vfio_pci_core_pm_exit(struct vfio_device *device, u32 flags,
				 void __user *arg, size_t argsz)
{
	struct vfio_pci_core_device *vdev =
		container_of(device, struct vfio_pci_core_device, vdev);
	int ret;

	ret = vfio_check_feature(flags, argsz, VFIO_DEVICE_FEATURE_SET, 0);
	if (ret != 1)
		return ret;

	/*
	 * The device is always in the active state here due to pm wrappers
	 * around ioctls. If the device had entered a low power state and
	 * pm_wake_eventfd_ctx is valid, vfio_pci_core_runtime_resume() has
	 * already signaled the eventfd and exited low power mode itself.
	 * pm_runtime_engaged protects the redundant call here.
	 */
	vfio_pci_runtime_pm_exit(vdev);
	return 0;
}

#ifdef CONFIG_PM
static int vfio_pci_core_runtime_suspend(struct device *dev)
{
	struct vfio_pci_core_device *vdev = dev_get_drvdata(dev);

	down_write(&vdev->memory_lock);
	/*
	 * The user can move the device into D3hot state before invoking
	 * power management IOCTL. Move the device into D0 state here and then
	 * the pci-driver core runtime PM suspend function will move the device
	 * into the low power state. Also, for the devices which have
	 * NoSoftRst-, it will help in restoring the original state
	 * (saved locally in 'vdev->pm_save').
	 */
	vfio_pci_set_power_state(vdev, PCI_D0);
	up_write(&vdev->memory_lock);

	/*
	 * If INTx is enabled, then mask INTx before going into the runtime
	 * suspended state and unmask the same in the runtime resume.
	 * If INTx has already been masked by the user, then
	 * vfio_pci_intx_mask() will return false and in that case, INTx
	 * should not be unmasked in the runtime resume.
	 */
	vdev->pm_intx_masked = ((vdev->irq_type == VFIO_PCI_INTX_IRQ_INDEX) &&
				vfio_pci_intx_mask(vdev));

	return 0;
}

static int vfio_pci_core_runtime_resume(struct device *dev)
{
	struct vfio_pci_core_device *vdev = dev_get_drvdata(dev);

	/*
	 * Resume with a pm_wake_eventfd_ctx signals the eventfd and exit
	 * low power mode.
	 */
	down_write(&vdev->memory_lock);
	if (vdev->pm_wake_eventfd_ctx) {
		eventfd_signal(vdev->pm_wake_eventfd_ctx);
		__vfio_pci_runtime_pm_exit(vdev);
	}
	up_write(&vdev->memory_lock);

	if (vdev->pm_intx_masked)
		vfio_pci_intx_unmask(vdev);

	return 0;
}
#endif /* CONFIG_PM */

/*
 * The pci-driver core runtime PM routines always save the device state
 * before going into suspended state. If the device is going into low power
 * state with only with runtime PM ops, then no explicit handling is needed
 * for the devices which have NoSoftRst-.
 */
static const struct dev_pm_ops vfio_pci_core_pm_ops = {
	SET_RUNTIME_PM_OPS(vfio_pci_core_runtime_suspend,
			   vfio_pci_core_runtime_resume,
			   NULL)
};

int vfio_pci_core_enable(struct vfio_pci_core_device *vdev)
{
	struct pci_dev *pdev = vdev->pdev;
	int ret;
	u16 cmd;
	u8 msix_pos;

	if (!disable_idle_d3) {
		ret = pm_runtime_resume_and_get(&pdev->dev);
		if (ret < 0)
			return ret;
	}

	/* Don't allow our initial saved state to include busmaster */
	pci_clear_master(pdev);

	ret = pci_enable_device(pdev);
	if (ret)
		goto out_power;

	/* If reset fails because of the device lock, fail this path entirely */
	ret = pci_try_reset_function(pdev);
	if (ret == -EAGAIN)
		goto out_disable_device;

	vdev->reset_works = !ret;
	pci_save_state(pdev);
	vdev->pci_saved_state = pci_store_saved_state(pdev);
	if (!vdev->pci_saved_state)
		pci_dbg(pdev, "%s: Couldn't store saved state\n", __func__);

	if (likely(!nointxmask)) {
		if (vfio_pci_nointx(pdev)) {
			pci_info(pdev, "Masking broken INTx support\n");
			vdev->nointx = true;
			pci_intx(pdev, 0);
		} else
			vdev->pci_2_3 = pci_intx_mask_supported(pdev);
	}

	pci_read_config_word(pdev, PCI_COMMAND, &cmd);
	if (vdev->pci_2_3 && (cmd & PCI_COMMAND_INTX_DISABLE)) {
		cmd &= ~PCI_COMMAND_INTX_DISABLE;
		pci_write_config_word(pdev, PCI_COMMAND, cmd);
	}

	ret = vfio_pci_zdev_open_device(vdev);
	if (ret)
		goto out_free_state;

	ret = vfio_config_init(vdev);
	if (ret)
		goto out_free_zdev;

	msix_pos = pdev->msix_cap;
	if (msix_pos) {
		u16 flags;
		u32 table;

		pci_read_config_word(pdev, msix_pos + PCI_MSIX_FLAGS, &flags);
		pci_read_config_dword(pdev, msix_pos + PCI_MSIX_TABLE, &table);

		vdev->msix_bar = table & PCI_MSIX_TABLE_BIR;
		vdev->msix_offset = table & PCI_MSIX_TABLE_OFFSET;
		vdev->msix_size = ((flags & PCI_MSIX_FLAGS_QSIZE) + 1) * 16;
		vdev->has_dyn_msix = pci_msix_can_alloc_dyn(pdev);
	} else {
		vdev->msix_bar = 0xFF;
		vdev->has_dyn_msix = false;
	}

	if (!vfio_vga_disabled() && vfio_pci_is_vga(pdev))
		vdev->has_vga = true;


	return 0;

out_free_zdev:
	vfio_pci_zdev_close_device(vdev);
out_free_state:
	kfree(vdev->pci_saved_state);
	vdev->pci_saved_state = NULL;
out_disable_device:
	pci_disable_device(pdev);
out_power:
	if (!disable_idle_d3)
		pm_runtime_put(&pdev->dev);
	return ret;
}
EXPORT_SYMBOL_GPL(vfio_pci_core_enable);

void vfio_pci_core_disable(struct vfio_pci_core_device *vdev)
{
	struct pci_dev *pdev = vdev->pdev;
	struct vfio_pci_dummy_resource *dummy_res, *tmp;
	struct vfio_pci_ioeventfd *ioeventfd, *ioeventfd_tmp;
	int i, bar;

	/* For needs_reset */
	lockdep_assert_held(&vdev->vdev.dev_set->lock);

	/*
	 * This function can be invoked while the power state is non-D0.
	 * This non-D0 power state can be with or without runtime PM.
	 * vfio_pci_runtime_pm_exit() will internally increment the usage
	 * count corresponding to pm_runtime_put() called during low power
	 * feature entry and then pm_runtime_resume() will wake up the device,
	 * if the device has already gone into the suspended state. Otherwise,
	 * the vfio_pci_set_power_state() will change the device power state
	 * to D0.
	 */
	vfio_pci_runtime_pm_exit(vdev);
	pm_runtime_resume(&pdev->dev);

	/*
	 * This function calls __pci_reset_function_locked() which internally
	 * can use pci_pm_reset() for the function reset. pci_pm_reset() will
	 * fail if the power state is non-D0. Also, for the devices which
	 * have NoSoftRst-, the reset function can cause the PCI config space
	 * reset without restoring the original state (saved locally in
	 * 'vdev->pm_save').
	 */
	vfio_pci_set_power_state(vdev, PCI_D0);

	/* Stop the device from further DMA */
	pci_clear_master(pdev);

	vfio_pci_set_irqs_ioctl(vdev, VFIO_IRQ_SET_DATA_NONE |
				VFIO_IRQ_SET_ACTION_TRIGGER,
				vdev->irq_type, 0, 0, NULL);

	/* Device closed, don't need mutex here */
	list_for_each_entry_safe(ioeventfd, ioeventfd_tmp,
				 &vdev->ioeventfds_list, next) {
		vfio_virqfd_disable(&ioeventfd->virqfd);
		list_del(&ioeventfd->next);
		kfree(ioeventfd);
	}
	vdev->ioeventfds_nr = 0;

	vdev->virq_disabled = false;

	for (i = 0; i < vdev->num_regions; i++)
		vdev->region[i].ops->release(vdev, &vdev->region[i]);

	vdev->num_regions = 0;
	kfree(vdev->region);
	vdev->region = NULL; /* don't krealloc a freed pointer */

	vfio_config_free(vdev);

	for (i = 0; i < PCI_STD_NUM_BARS; i++) {
		bar = i + PCI_STD_RESOURCES;
		if (!vdev->barmap[bar])
			continue;
		pci_iounmap(pdev, vdev->barmap[bar]);
		pci_release_selected_regions(pdev, 1 << bar);
		vdev->barmap[bar] = NULL;
	}

	list_for_each_entry_safe(dummy_res, tmp,
				 &vdev->dummy_resources_list, res_next) {
		list_del(&dummy_res->res_next);
		release_resource(&dummy_res->resource);
		kfree(dummy_res);
	}

	vdev->needs_reset = true;

	vfio_pci_zdev_close_device(vdev);

	/*
	 * If we have saved state, restore it.  If we can reset the device,
	 * even better.  Resetting with current state seems better than
	 * nothing, but saving and restoring current state without reset
	 * is just busy work.
	 */
	if (pci_load_and_free_saved_state(pdev, &vdev->pci_saved_state)) {
		pci_info(pdev, "%s: Couldn't reload saved state\n", __func__);

		if (!vdev->reset_works)
			goto out;

		pci_save_state(pdev);
	}

	/*
	 * Disable INTx and MSI, presumably to avoid spurious interrupts
	 * during reset.  Stolen from pci_reset_function()
	 */
	pci_write_config_word(pdev, PCI_COMMAND, PCI_COMMAND_INTX_DISABLE);

	/*
	 * Try to get the locks ourselves to prevent a deadlock. The
	 * success of this is dependent on being able to lock the device,
	 * which is not always possible.
	 * We can not use the "try" reset interface here, which will
	 * overwrite the previously restored configuration information.
	 */
	if (vdev->reset_works && pci_dev_trylock(pdev)) {
		if (!__pci_reset_function_locked(pdev))
			vdev->needs_reset = false;
		pci_dev_unlock(pdev);
	}

	pci_restore_state(pdev);
out:
	pci_disable_device(pdev);

	vfio_pci_dev_set_try_reset(vdev->vdev.dev_set);

	/* Put the pm-runtime usage counter acquired during enable */
	if (!disable_idle_d3)
		pm_runtime_put(&pdev->dev);
}
EXPORT_SYMBOL_GPL(vfio_pci_core_disable);

void vfio_pci_core_close_device(struct vfio_device *core_vdev)
{
	struct vfio_pci_core_device *vdev =
		container_of(core_vdev, struct vfio_pci_core_device, vdev);

	if (vdev->sriov_pf_core_dev) {
		mutex_lock(&vdev->sriov_pf_core_dev->vf_token->lock);
		WARN_ON(!vdev->sriov_pf_core_dev->vf_token->users);
		vdev->sriov_pf_core_dev->vf_token->users--;
		mutex_unlock(&vdev->sriov_pf_core_dev->vf_token->lock);
	}
#if IS_ENABLED(CONFIG_EEH)
	eeh_dev_release(vdev->pdev);
#endif
	vfio_pci_core_disable(vdev);

	mutex_lock(&vdev->igate);
	if (vdev->err_trigger) {
		eventfd_ctx_put(vdev->err_trigger);
		vdev->err_trigger = NULL;
	}
	if (vdev->req_trigger) {
		eventfd_ctx_put(vdev->req_trigger);
		vdev->req_trigger = NULL;
	}
	mutex_unlock(&vdev->igate);
}
EXPORT_SYMBOL_GPL(vfio_pci_core_close_device);

void vfio_pci_core_finish_enable(struct vfio_pci_core_device *vdev)
{
	vfio_pci_probe_mmaps(vdev);
#if IS_ENABLED(CONFIG_EEH)
	eeh_dev_open(vdev->pdev);
#endif

	if (vdev->sriov_pf_core_dev) {
		mutex_lock(&vdev->sriov_pf_core_dev->vf_token->lock);
		vdev->sriov_pf_core_dev->vf_token->users++;
		mutex_unlock(&vdev->sriov_pf_core_dev->vf_token->lock);
	}
}
EXPORT_SYMBOL_GPL(vfio_pci_core_finish_enable);

static int vfio_pci_get_irq_count(struct vfio_pci_core_device *vdev, int irq_type)
{
	if (irq_type == VFIO_PCI_INTX_IRQ_INDEX) {
		return vdev->vconfig[PCI_INTERRUPT_PIN] ? 1 : 0;
	} else if (irq_type == VFIO_PCI_MSI_IRQ_INDEX) {
		u8 pos;
		u16 flags;

		pos = vdev->pdev->msi_cap;
		if (pos) {
			pci_read_config_word(vdev->pdev,
					     pos + PCI_MSI_FLAGS, &flags);
			return 1 << ((flags & PCI_MSI_FLAGS_QMASK) >> 1);
		}
	} else if (irq_type == VFIO_PCI_MSIX_IRQ_INDEX) {
		u8 pos;
		u16 flags;

		pos = vdev->pdev->msix_cap;
		if (pos) {
			pci_read_config_word(vdev->pdev,
					     pos + PCI_MSIX_FLAGS, &flags);

			return (flags & PCI_MSIX_FLAGS_QSIZE) + 1;
		}
	} else if (irq_type == VFIO_PCI_ERR_IRQ_INDEX) {
		if (pci_is_pcie(vdev->pdev))
			return 1;
	} else if (irq_type == VFIO_PCI_REQ_IRQ_INDEX) {
		return 1;
	}

	return 0;
}

static int vfio_pci_count_devs(struct pci_dev *pdev, void *data)
{
	(*(int *)data)++;
	return 0;
}

struct vfio_pci_fill_info {
	struct vfio_device *vdev;
	struct vfio_pci_dependent_device *devices;
	int nr_devices;
	u32 count;
	u32 flags;
};

static int vfio_pci_fill_devs(struct pci_dev *pdev, void *data)
{
	struct vfio_pci_dependent_device *info;
	struct vfio_pci_fill_info *fill = data;

	/* The topology changed since we counted devices */
	if (fill->count >= fill->nr_devices)
		return -EAGAIN;

	info = &fill->devices[fill->count++];
	info->segment = pci_domain_nr(pdev->bus);
	info->bus = pdev->bus->number;
	info->devfn = pdev->devfn;

	if (fill->flags & VFIO_PCI_HOT_RESET_FLAG_DEV_ID) {
		struct iommufd_ctx *iommufd = vfio_iommufd_device_ictx(fill->vdev);
		struct vfio_device_set *dev_set = fill->vdev->dev_set;
		struct vfio_device *vdev;

		/*
		 * hot-reset requires all affected devices be represented in
		 * the dev_set.
		 */
		vdev = vfio_find_device_in_devset(dev_set, &pdev->dev);
		if (!vdev) {
			info->devid = VFIO_PCI_DEVID_NOT_OWNED;
		} else {
			int id = vfio_iommufd_get_dev_id(vdev, iommufd);

			if (id > 0)
				info->devid = id;
			else if (id == -ENOENT)
				info->devid = VFIO_PCI_DEVID_OWNED;
			else
				info->devid = VFIO_PCI_DEVID_NOT_OWNED;
		}
		/* If devid is VFIO_PCI_DEVID_NOT_OWNED, clear owned flag. */
		if (info->devid == VFIO_PCI_DEVID_NOT_OWNED)
			fill->flags &= ~VFIO_PCI_HOT_RESET_FLAG_DEV_ID_OWNED;
	} else {
		struct iommu_group *iommu_group;

		iommu_group = iommu_group_get(&pdev->dev);
		if (!iommu_group)
			return -EPERM; /* Cannot reset non-isolated devices */

		info->group_id = iommu_group_id(iommu_group);
		iommu_group_put(iommu_group);
	}

	return 0;
}

struct vfio_pci_group_info {
	int count;
	struct file **files;
};

static bool vfio_pci_dev_below_slot(struct pci_dev *pdev, struct pci_slot *slot)
{
	for (; pdev; pdev = pdev->bus->self)
		if (pdev->bus == slot->bus)
			return (pdev->slot == slot);
	return false;
}

struct vfio_pci_walk_info {
	int (*fn)(struct pci_dev *pdev, void *data);
	void *data;
	struct pci_dev *pdev;
	bool slot;
	int ret;
};

static int vfio_pci_walk_wrapper(struct pci_dev *pdev, void *data)
{
	struct vfio_pci_walk_info *walk = data;

	if (!walk->slot || vfio_pci_dev_below_slot(pdev, walk->pdev->slot))
		walk->ret = walk->fn(pdev, walk->data);

	return walk->ret;
}

static int vfio_pci_for_each_slot_or_bus(struct pci_dev *pdev,
					 int (*fn)(struct pci_dev *,
						   void *data), void *data,
					 bool slot)
{
	struct vfio_pci_walk_info walk = {
		.fn = fn, .data = data, .pdev = pdev, .slot = slot, .ret = 0,
	};

	pci_walk_bus(pdev->bus, vfio_pci_walk_wrapper, &walk);

	return walk.ret;
}

static int msix_mmappable_cap(struct vfio_pci_core_device *vdev,
			      struct vfio_info_cap *caps)
{
	struct vfio_info_cap_header header = {
		.id = VFIO_REGION_INFO_CAP_MSIX_MAPPABLE,
		.version = 1
	};

	return vfio_info_add_capability(caps, &header, sizeof(header));
}

int vfio_pci_core_register_dev_region(struct vfio_pci_core_device *vdev,
				      unsigned int type, unsigned int subtype,
				      const struct vfio_pci_regops *ops,
				      size_t size, u32 flags, void *data)
{
	struct vfio_pci_region *region;

	region = krealloc(vdev->region,
			  (vdev->num_regions + 1) * sizeof(*region),
			  GFP_KERNEL_ACCOUNT);
	if (!region)
		return -ENOMEM;

	vdev->region = region;
	vdev->region[vdev->num_regions].type = type;
	vdev->region[vdev->num_regions].subtype = subtype;
	vdev->region[vdev->num_regions].ops = ops;
	vdev->region[vdev->num_regions].size = size;
	vdev->region[vdev->num_regions].flags = flags;
	vdev->region[vdev->num_regions].data = data;

	vdev->num_regions++;

	return 0;
}
EXPORT_SYMBOL_GPL(vfio_pci_core_register_dev_region);

static int vfio_pci_info_atomic_cap(struct vfio_pci_core_device *vdev,
				    struct vfio_info_cap *caps)
{
	struct vfio_device_info_cap_pci_atomic_comp cap = {
		.header.id = VFIO_DEVICE_INFO_CAP_PCI_ATOMIC_COMP,
		.header.version = 1
	};
	struct pci_dev *pdev = pci_physfn(vdev->pdev);
	u32 devcap2;

	pcie_capability_read_dword(pdev, PCI_EXP_DEVCAP2, &devcap2);

	if ((devcap2 & PCI_EXP_DEVCAP2_ATOMIC_COMP32) &&
	    !pci_enable_atomic_ops_to_root(pdev, PCI_EXP_DEVCAP2_ATOMIC_COMP32))
		cap.flags |= VFIO_PCI_ATOMIC_COMP32;

	if ((devcap2 & PCI_EXP_DEVCAP2_ATOMIC_COMP64) &&
	    !pci_enable_atomic_ops_to_root(pdev, PCI_EXP_DEVCAP2_ATOMIC_COMP64))
		cap.flags |= VFIO_PCI_ATOMIC_COMP64;

	if ((devcap2 & PCI_EXP_DEVCAP2_ATOMIC_COMP128) &&
	    !pci_enable_atomic_ops_to_root(pdev,
					   PCI_EXP_DEVCAP2_ATOMIC_COMP128))
		cap.flags |= VFIO_PCI_ATOMIC_COMP128;

	if (!cap.flags)
		return -ENODEV;

	return vfio_info_add_capability(caps, &cap.header, sizeof(cap));
}

static int vfio_pci_ioctl_get_info(struct vfio_pci_core_device *vdev,
				   struct vfio_device_info __user *arg)
{
	unsigned long minsz = offsetofend(struct vfio_device_info, num_irqs);
	struct vfio_device_info info = {};
	struct vfio_info_cap caps = { .buf = NULL, .size = 0 };
	int ret;

	if (copy_from_user(&info, arg, minsz))
		return -EFAULT;

	if (info.argsz < minsz)
		return -EINVAL;

	minsz = min_t(size_t, info.argsz, sizeof(info));

	info.flags = VFIO_DEVICE_FLAGS_PCI;

	if (vdev->reset_works)
		info.flags |= VFIO_DEVICE_FLAGS_RESET;

	info.num_regions = VFIO_PCI_NUM_REGIONS + vdev->num_regions;
	info.num_irqs = VFIO_PCI_NUM_IRQS;

	ret = vfio_pci_info_zdev_add_caps(vdev, &caps);
	if (ret && ret != -ENODEV) {
		pci_warn(vdev->pdev,
			 "Failed to setup zPCI info capabilities\n");
		return ret;
	}

	ret = vfio_pci_info_atomic_cap(vdev, &caps);
	if (ret && ret != -ENODEV) {
		pci_warn(vdev->pdev,
			 "Failed to setup AtomicOps info capability\n");
		return ret;
	}

	if (caps.size) {
		info.flags |= VFIO_DEVICE_FLAGS_CAPS;
		if (info.argsz < sizeof(info) + caps.size) {
			info.argsz = sizeof(info) + caps.size;
		} else {
			vfio_info_cap_shift(&caps, sizeof(info));
			if (copy_to_user(arg + 1, caps.buf, caps.size)) {
				kfree(caps.buf);
				return -EFAULT;
			}
			info.cap_offset = sizeof(*arg);
		}

		kfree(caps.buf);
	}

	return copy_to_user(arg, &info, minsz) ? -EFAULT : 0;
}

static int vfio_pci_ioctl_get_region_info(struct vfio_pci_core_device *vdev,
					  struct vfio_region_info __user *arg)
{
	unsigned long minsz = offsetofend(struct vfio_region_info, offset);
	struct pci_dev *pdev = vdev->pdev;
	struct vfio_region_info info;
	struct vfio_info_cap caps = { .buf = NULL, .size = 0 };
	int i, ret;

	if (copy_from_user(&info, arg, minsz))
		return -EFAULT;

	if (info.argsz < minsz)
		return -EINVAL;

	switch (info.index) {
	case VFIO_PCI_CONFIG_REGION_INDEX:
		info.offset = VFIO_PCI_INDEX_TO_OFFSET(info.index);
		info.size = pdev->cfg_size;
		info.flags = VFIO_REGION_INFO_FLAG_READ |
			     VFIO_REGION_INFO_FLAG_WRITE;
		break;
	case VFIO_PCI_BAR0_REGION_INDEX ... VFIO_PCI_BAR5_REGION_INDEX:
		info.offset = VFIO_PCI_INDEX_TO_OFFSET(info.index);
		info.size = pci_resource_len(pdev, info.index);
		if (!info.size) {
			info.flags = 0;
			break;
		}

		info.flags = VFIO_REGION_INFO_FLAG_READ |
			     VFIO_REGION_INFO_FLAG_WRITE;
		if (vdev->bar_mmap_supported[info.index]) {
			info.flags |= VFIO_REGION_INFO_FLAG_MMAP;
			if (info.index == vdev->msix_bar) {
				ret = msix_mmappable_cap(vdev, &caps);
				if (ret)
					return ret;
			}
		}

		break;
	case VFIO_PCI_ROM_REGION_INDEX: {
		void __iomem *io;
		size_t size;
		u16 cmd;

		info.offset = VFIO_PCI_INDEX_TO_OFFSET(info.index);
		info.flags = 0;
		info.size = 0;

		if (pci_resource_start(pdev, PCI_ROM_RESOURCE)) {
			/*
			 * Check ROM content is valid. Need to enable memory
			 * decode for ROM access in pci_map_rom().
			 */
			cmd = vfio_pci_memory_lock_and_enable(vdev);
			io = pci_map_rom(pdev, &size);
			if (io) {
				info.flags = VFIO_REGION_INFO_FLAG_READ;
				/* Report the BAR size, not the ROM size. */
				info.size = pci_resource_len(pdev, PCI_ROM_RESOURCE);
				pci_unmap_rom(pdev, io);
			}
			vfio_pci_memory_unlock_and_restore(vdev, cmd);
		} else if (pdev->rom && pdev->romlen) {
			info.flags = VFIO_REGION_INFO_FLAG_READ;
			/* Report BAR size as power of two. */
			info.size = roundup_pow_of_two(pdev->romlen);
		}

		break;
	}
	case VFIO_PCI_VGA_REGION_INDEX:
		if (!vdev->has_vga)
			return -EINVAL;

		info.offset = VFIO_PCI_INDEX_TO_OFFSET(info.index);
		info.size = 0xc0000;
		info.flags = VFIO_REGION_INFO_FLAG_READ |
			     VFIO_REGION_INFO_FLAG_WRITE;

		break;
	default: {
		struct vfio_region_info_cap_type cap_type = {
			.header.id = VFIO_REGION_INFO_CAP_TYPE,
			.header.version = 1
		};

		if (info.index >= VFIO_PCI_NUM_REGIONS + vdev->num_regions)
			return -EINVAL;
		info.index = array_index_nospec(
			info.index, VFIO_PCI_NUM_REGIONS + vdev->num_regions);

		i = info.index - VFIO_PCI_NUM_REGIONS;

		info.offset = VFIO_PCI_INDEX_TO_OFFSET(info.index);
		info.size = vdev->region[i].size;
		info.flags = vdev->region[i].flags;

		cap_type.type = vdev->region[i].type;
		cap_type.subtype = vdev->region[i].subtype;

		ret = vfio_info_add_capability(&caps, &cap_type.header,
					       sizeof(cap_type));
		if (ret)
			return ret;

		if (vdev->region[i].ops->add_capability) {
			ret = vdev->region[i].ops->add_capability(
				vdev, &vdev->region[i], &caps);
			if (ret)
				return ret;
		}
	}
	}

	if (caps.size) {
		info.flags |= VFIO_REGION_INFO_FLAG_CAPS;
		if (info.argsz < sizeof(info) + caps.size) {
			info.argsz = sizeof(info) + caps.size;
			info.cap_offset = 0;
		} else {
			vfio_info_cap_shift(&caps, sizeof(info));
			if (copy_to_user(arg + 1, caps.buf, caps.size)) {
				kfree(caps.buf);
				return -EFAULT;
			}
			info.cap_offset = sizeof(*arg);
		}

		kfree(caps.buf);
	}

	return copy_to_user(arg, &info, minsz) ? -EFAULT : 0;
}

static int vfio_pci_ioctl_get_irq_info(struct vfio_pci_core_device *vdev,
				       struct vfio_irq_info __user *arg)
{
	unsigned long minsz = offsetofend(struct vfio_irq_info, count);
	struct vfio_irq_info info;

	if (copy_from_user(&info, arg, minsz))
		return -EFAULT;

	if (info.argsz < minsz || info.index >= VFIO_PCI_NUM_IRQS)
		return -EINVAL;

	switch (info.index) {
	case VFIO_PCI_INTX_IRQ_INDEX ... VFIO_PCI_MSIX_IRQ_INDEX:
	case VFIO_PCI_REQ_IRQ_INDEX:
		break;
	case VFIO_PCI_ERR_IRQ_INDEX:
		if (pci_is_pcie(vdev->pdev))
			break;
		fallthrough;
	default:
		return -EINVAL;
	}

	info.flags = VFIO_IRQ_INFO_EVENTFD;

	info.count = vfio_pci_get_irq_count(vdev, info.index);

	if (info.index == VFIO_PCI_INTX_IRQ_INDEX)
		info.flags |=
			(VFIO_IRQ_INFO_MASKABLE | VFIO_IRQ_INFO_AUTOMASKED);
	else if (info.index != VFIO_PCI_MSIX_IRQ_INDEX || !vdev->has_dyn_msix)
		info.flags |= VFIO_IRQ_INFO_NORESIZE;

	return copy_to_user(arg, &info, minsz) ? -EFAULT : 0;
}

static int vfio_pci_ioctl_set_irqs(struct vfio_pci_core_device *vdev,
				   struct vfio_irq_set __user *arg)
{
	unsigned long minsz = offsetofend(struct vfio_irq_set, count);
	struct vfio_irq_set hdr;
	u8 *data = NULL;
	int max, ret = 0;
	size_t data_size = 0;

	if (copy_from_user(&hdr, arg, minsz))
		return -EFAULT;

	max = vfio_pci_get_irq_count(vdev, hdr.index);

	ret = vfio_set_irqs_validate_and_prepare(&hdr, max, VFIO_PCI_NUM_IRQS,
						 &data_size);
	if (ret)
		return ret;

	if (data_size) {
		data = memdup_user(&arg->data, data_size);
		if (IS_ERR(data))
			return PTR_ERR(data);
	}

	mutex_lock(&vdev->igate);

	ret = vfio_pci_set_irqs_ioctl(vdev, hdr.flags, hdr.index, hdr.start,
				      hdr.count, data);

	mutex_unlock(&vdev->igate);
	kfree(data);

	return ret;
}

static int vfio_pci_ioctl_reset(struct vfio_pci_core_device *vdev,
				void __user *arg)
{
	int ret;

	if (!vdev->reset_works)
		return -EINVAL;

	vfio_pci_zap_and_down_write_memory_lock(vdev);

	/*
	 * This function can be invoked while the power state is non-D0. If
	 * pci_try_reset_function() has been called while the power state is
	 * non-D0, then pci_try_reset_function() will internally set the power
	 * state to D0 without vfio driver involvement. For the devices which
	 * have NoSoftRst-, the reset function can cause the PCI config space
	 * reset without restoring the original state (saved locally in
	 * 'vdev->pm_save').
	 */
	vfio_pci_set_power_state(vdev, PCI_D0);

	ret = pci_try_reset_function(vdev->pdev);
	up_write(&vdev->memory_lock);

	return ret;
}

static int vfio_pci_ioctl_get_pci_hot_reset_info(
	struct vfio_pci_core_device *vdev,
	struct vfio_pci_hot_reset_info __user *arg)
{
	unsigned long minsz =
		offsetofend(struct vfio_pci_hot_reset_info, count);
	struct vfio_pci_dependent_device *devices = NULL;
	struct vfio_pci_hot_reset_info hdr;
	struct vfio_pci_fill_info fill = {};
	bool slot = false;
	int ret, count = 0;

	if (copy_from_user(&hdr, arg, minsz))
		return -EFAULT;

	if (hdr.argsz < minsz)
		return -EINVAL;

	hdr.flags = 0;

	/* Can we do a slot or bus reset or neither? */
	if (!pci_probe_reset_slot(vdev->pdev->slot))
		slot = true;
	else if (pci_probe_reset_bus(vdev->pdev->bus))
		return -ENODEV;

	ret = vfio_pci_for_each_slot_or_bus(vdev->pdev, vfio_pci_count_devs,
					    &count, slot);
	if (ret)
		return ret;

	if (WARN_ON(!count)) /* Should always be at least one */
		return -ERANGE;

	if (count > (hdr.argsz - sizeof(hdr)) / sizeof(*devices)) {
		hdr.count = count;
		ret = -ENOSPC;
		goto header;
	}

	devices = kcalloc(count, sizeof(*devices), GFP_KERNEL);
	if (!devices)
		return -ENOMEM;

	fill.devices = devices;
	fill.nr_devices = count;
	fill.vdev = &vdev->vdev;

	if (vfio_device_cdev_opened(&vdev->vdev))
		fill.flags |= VFIO_PCI_HOT_RESET_FLAG_DEV_ID |
			     VFIO_PCI_HOT_RESET_FLAG_DEV_ID_OWNED;

	mutex_lock(&vdev->vdev.dev_set->lock);
	ret = vfio_pci_for_each_slot_or_bus(vdev->pdev, vfio_pci_fill_devs,
					    &fill, slot);
	mutex_unlock(&vdev->vdev.dev_set->lock);
	if (ret)
		goto out;

	if (copy_to_user(arg->devices, devices,
			 sizeof(*devices) * fill.count)) {
		ret = -EFAULT;
		goto out;
	}

	hdr.count = fill.count;
	hdr.flags = fill.flags;

header:
	if (copy_to_user(arg, &hdr, minsz))
		ret = -EFAULT;
out:
	kfree(devices);
	return ret;
}

static int
vfio_pci_ioctl_pci_hot_reset_groups(struct vfio_pci_core_device *vdev,
				    u32 array_count, bool slot,
				    struct vfio_pci_hot_reset __user *arg)
{
	int32_t *group_fds;
	struct file **files;
	struct vfio_pci_group_info info;
	int file_idx, count = 0, ret = 0;

	/*
	 * We can't let userspace give us an arbitrarily large buffer to copy,
	 * so verify how many we think there could be.  Note groups can have
	 * multiple devices so one group per device is the max.
	 */
	ret = vfio_pci_for_each_slot_or_bus(vdev->pdev, vfio_pci_count_devs,
					    &count, slot);
	if (ret)
		return ret;

	if (array_count > count)
		return -EINVAL;

	group_fds = kcalloc(array_count, sizeof(*group_fds), GFP_KERNEL);
	files = kcalloc(array_count, sizeof(*files), GFP_KERNEL);
	if (!group_fds || !files) {
		kfree(group_fds);
		kfree(files);
		return -ENOMEM;
	}

	if (copy_from_user(group_fds, arg->group_fds,
			   array_count * sizeof(*group_fds))) {
		kfree(group_fds);
		kfree(files);
		return -EFAULT;
	}

	/*
	 * Get the group file for each fd to ensure the group is held across
	 * the reset
	 */
	for (file_idx = 0; file_idx < array_count; file_idx++) {
		struct file *file = fget(group_fds[file_idx]);

		if (!file) {
			ret = -EBADF;
			break;
		}

		/* Ensure the FD is a vfio group FD.*/
		if (!vfio_file_is_group(file)) {
			fput(file);
			ret = -EINVAL;
			break;
		}

		files[file_idx] = file;
	}

	kfree(group_fds);

	/* release reference to groups on error */
	if (ret)
		goto hot_reset_release;

	info.count = array_count;
	info.files = files;

	ret = vfio_pci_dev_set_hot_reset(vdev->vdev.dev_set, &info, NULL);

hot_reset_release:
	for (file_idx--; file_idx >= 0; file_idx--)
		fput(files[file_idx]);

	kfree(files);
	return ret;
}

static int vfio_pci_ioctl_pci_hot_reset(struct vfio_pci_core_device *vdev,
					struct vfio_pci_hot_reset __user *arg)
{
	unsigned long minsz = offsetofend(struct vfio_pci_hot_reset, count);
	struct vfio_pci_hot_reset hdr;
	bool slot = false;

	if (copy_from_user(&hdr, arg, minsz))
		return -EFAULT;

	if (hdr.argsz < minsz || hdr.flags)
		return -EINVAL;

	/* zero-length array is only for cdev opened devices */
	if (!!hdr.count == vfio_device_cdev_opened(&vdev->vdev))
		return -EINVAL;

	/* Can we do a slot or bus reset or neither? */
	if (!pci_probe_reset_slot(vdev->pdev->slot))
		slot = true;
	else if (pci_probe_reset_bus(vdev->pdev->bus))
		return -ENODEV;

	if (hdr.count)
		return vfio_pci_ioctl_pci_hot_reset_groups(vdev, hdr.count, slot, arg);

	return vfio_pci_dev_set_hot_reset(vdev->vdev.dev_set, NULL,
					  vfio_iommufd_device_ictx(&vdev->vdev));
}

static int vfio_pci_ioctl_ioeventfd(struct vfio_pci_core_device *vdev,
				    struct vfio_device_ioeventfd __user *arg)
{
	unsigned long minsz = offsetofend(struct vfio_device_ioeventfd, fd);
	struct vfio_device_ioeventfd ioeventfd;
	int count;

	if (copy_from_user(&ioeventfd, arg, minsz))
		return -EFAULT;

	if (ioeventfd.argsz < minsz)
		return -EINVAL;

	if (ioeventfd.flags & ~VFIO_DEVICE_IOEVENTFD_SIZE_MASK)
		return -EINVAL;

	count = ioeventfd.flags & VFIO_DEVICE_IOEVENTFD_SIZE_MASK;

	if (hweight8(count) != 1 || ioeventfd.fd < -1)
		return -EINVAL;

	return vfio_pci_ioeventfd(vdev, ioeventfd.offset, ioeventfd.data, count,
				  ioeventfd.fd);
}

long vfio_pci_core_ioctl(struct vfio_device *core_vdev, unsigned int cmd,
			 unsigned long arg)
{
	struct vfio_pci_core_device *vdev =
		container_of(core_vdev, struct vfio_pci_core_device, vdev);
	void __user *uarg = (void __user *)arg;

	switch (cmd) {
	case VFIO_DEVICE_GET_INFO:
		return vfio_pci_ioctl_get_info(vdev, uarg);
	case VFIO_DEVICE_GET_IRQ_INFO:
		return vfio_pci_ioctl_get_irq_info(vdev, uarg);
	case VFIO_DEVICE_GET_PCI_HOT_RESET_INFO:
		return vfio_pci_ioctl_get_pci_hot_reset_info(vdev, uarg);
	case VFIO_DEVICE_GET_REGION_INFO:
		return vfio_pci_ioctl_get_region_info(vdev, uarg);
	case VFIO_DEVICE_IOEVENTFD:
		return vfio_pci_ioctl_ioeventfd(vdev, uarg);
	case VFIO_DEVICE_PCI_HOT_RESET:
		return vfio_pci_ioctl_pci_hot_reset(vdev, uarg);
	case VFIO_DEVICE_RESET:
		return vfio_pci_ioctl_reset(vdev, uarg);
	case VFIO_DEVICE_SET_IRQS:
		return vfio_pci_ioctl_set_irqs(vdev, uarg);
	default:
		return -ENOTTY;
	}
}
EXPORT_SYMBOL_GPL(vfio_pci_core_ioctl);

static int vfio_pci_core_feature_token(struct vfio_device *device, u32 flags,
				       uuid_t __user *arg, size_t argsz)
{
	struct vfio_pci_core_device *vdev =
		container_of(device, struct vfio_pci_core_device, vdev);
	uuid_t uuid;
	int ret;

	if (!vdev->vf_token)
		return -ENOTTY;
	/*
	 * We do not support GET of the VF Token UUID as this could
	 * expose the token of the previous device user.
	 */
	ret = vfio_check_feature(flags, argsz, VFIO_DEVICE_FEATURE_SET,
				 sizeof(uuid));
	if (ret != 1)
		return ret;

	if (copy_from_user(&uuid, arg, sizeof(uuid)))
		return -EFAULT;

	mutex_lock(&vdev->vf_token->lock);
	uuid_copy(&vdev->vf_token->uuid, &uuid);
	mutex_unlock(&vdev->vf_token->lock);
	return 0;
}

int vfio_pci_core_ioctl_feature(struct vfio_device *device, u32 flags,
				void __user *arg, size_t argsz)
{
	switch (flags & VFIO_DEVICE_FEATURE_MASK) {
	case VFIO_DEVICE_FEATURE_LOW_POWER_ENTRY:
		return vfio_pci_core_pm_entry(device, flags, arg, argsz);
	case VFIO_DEVICE_FEATURE_LOW_POWER_ENTRY_WITH_WAKEUP:
		return vfio_pci_core_pm_entry_with_wakeup(device, flags,
							  arg, argsz);
	case VFIO_DEVICE_FEATURE_LOW_POWER_EXIT:
		return vfio_pci_core_pm_exit(device, flags, arg, argsz);
	case VFIO_DEVICE_FEATURE_PCI_VF_TOKEN:
		return vfio_pci_core_feature_token(device, flags, arg, argsz);
	default:
		return -ENOTTY;
	}
}
EXPORT_SYMBOL_GPL(vfio_pci_core_ioctl_feature);

static ssize_t vfio_pci_rw(struct vfio_pci_core_device *vdev, char __user *buf,
			   size_t count, loff_t *ppos, bool iswrite)
{
	unsigned int index = VFIO_PCI_OFFSET_TO_INDEX(*ppos);
	int ret;

	if (index >= VFIO_PCI_NUM_REGIONS + vdev->num_regions)
		return -EINVAL;

	ret = pm_runtime_resume_and_get(&vdev->pdev->dev);
	if (ret) {
		pci_info_ratelimited(vdev->pdev, "runtime resume failed %d\n",
				     ret);
		return -EIO;
	}

	switch (index) {
	case VFIO_PCI_CONFIG_REGION_INDEX:
		ret = vfio_pci_config_rw(vdev, buf, count, ppos, iswrite);
		break;

	case VFIO_PCI_ROM_REGION_INDEX:
		if (iswrite)
			ret = -EINVAL;
		else
			ret = vfio_pci_bar_rw(vdev, buf, count, ppos, false);
		break;

	case VFIO_PCI_BAR0_REGION_INDEX ... VFIO_PCI_BAR5_REGION_INDEX:
		ret = vfio_pci_bar_rw(vdev, buf, count, ppos, iswrite);
		break;

	case VFIO_PCI_VGA_REGION_INDEX:
		ret = vfio_pci_vga_rw(vdev, buf, count, ppos, iswrite);
		break;

	default:
		index -= VFIO_PCI_NUM_REGIONS;
		ret = vdev->region[index].ops->rw(vdev, buf,
						   count, ppos, iswrite);
		break;
	}

	pm_runtime_put(&vdev->pdev->dev);
	return ret;
}

ssize_t vfio_pci_core_read(struct vfio_device *core_vdev, char __user *buf,
		size_t count, loff_t *ppos)
{
	struct vfio_pci_core_device *vdev =
		container_of(core_vdev, struct vfio_pci_core_device, vdev);

	if (!count)
		return 0;

	return vfio_pci_rw(vdev, buf, count, ppos, false);
}
EXPORT_SYMBOL_GPL(vfio_pci_core_read);

ssize_t vfio_pci_core_write(struct vfio_device *core_vdev, const char __user *buf,
		size_t count, loff_t *ppos)
{
	struct vfio_pci_core_device *vdev =
		container_of(core_vdev, struct vfio_pci_core_device, vdev);

	if (!count)
		return 0;

	return vfio_pci_rw(vdev, (char __user *)buf, count, ppos, true);
}
EXPORT_SYMBOL_GPL(vfio_pci_core_write);

static void vfio_pci_zap_bars(struct vfio_pci_core_device *vdev)
{
	struct vfio_device *core_vdev = &vdev->vdev;
	loff_t start = VFIO_PCI_INDEX_TO_OFFSET(VFIO_PCI_BAR0_REGION_INDEX);
	loff_t end = VFIO_PCI_INDEX_TO_OFFSET(VFIO_PCI_ROM_REGION_INDEX);
	loff_t len = end - start;

	unmap_mapping_range(core_vdev->inode->i_mapping, start, len, true);
}

void vfio_pci_zap_and_down_write_memory_lock(struct vfio_pci_core_device *vdev)
{
	down_write(&vdev->memory_lock);
	vfio_pci_zap_bars(vdev);
}

u16 vfio_pci_memory_lock_and_enable(struct vfio_pci_core_device *vdev)
{
	u16 cmd;

	down_write(&vdev->memory_lock);
	pci_read_config_word(vdev->pdev, PCI_COMMAND, &cmd);
	if (!(cmd & PCI_COMMAND_MEMORY))
		pci_write_config_word(vdev->pdev, PCI_COMMAND,
				      cmd | PCI_COMMAND_MEMORY);

	return cmd;
}

void vfio_pci_memory_unlock_and_restore(struct vfio_pci_core_device *vdev, u16 cmd)
{
	pci_write_config_word(vdev->pdev, PCI_COMMAND, cmd);
	up_write(&vdev->memory_lock);
}

static unsigned long vma_to_pfn(struct vm_area_struct *vma)
{
	struct vfio_pci_core_device *vdev = vma->vm_private_data;
	int index = vma->vm_pgoff >> (VFIO_PCI_OFFSET_SHIFT - PAGE_SHIFT);
	u64 pgoff;

	pgoff = vma->vm_pgoff &
		((1U << (VFIO_PCI_OFFSET_SHIFT - PAGE_SHIFT)) - 1);

	return (pci_resource_start(vdev->pdev, index) >> PAGE_SHIFT) + pgoff;
}

static vm_fault_t vfio_pci_mmap_huge_fault(struct vm_fault *vmf,
					   unsigned int order)
{
	struct vm_area_struct *vma = vmf->vma;
	struct vfio_pci_core_device *vdev = vma->vm_private_data;
	unsigned long pfn, pgoff = vmf->pgoff - vma->vm_pgoff;
	vm_fault_t ret = VM_FAULT_SIGBUS;

	pfn = vma_to_pfn(vma) + pgoff;

	if (order && (pfn & ((1 << order) - 1) ||
		      vmf->address & ((PAGE_SIZE << order) - 1) ||
		      vmf->address + (PAGE_SIZE << order) > vma->vm_end)) {
		ret = VM_FAULT_FALLBACK;
		goto out;
	}

	down_read(&vdev->memory_lock);

	if (vdev->pm_runtime_engaged || !__vfio_pci_memory_enabled(vdev))
		goto out_unlock;

	switch (order) {
	case 0:
		ret = vmf_insert_pfn(vma, vmf->address, pfn);
		break;
#ifdef CONFIG_ARCH_SUPPORTS_PMD_PFNMAP
	case PMD_ORDER:
		ret = vmf_insert_pfn_pmd(vmf,
					 __pfn_to_pfn_t(pfn, PFN_DEV), false);
		break;
#endif
#ifdef CONFIG_ARCH_SUPPORTS_PUD_PFNMAP
	case PUD_ORDER:
		ret = vmf_insert_pfn_pud(vmf,
					 __pfn_to_pfn_t(pfn, PFN_DEV), false);
		break;
#endif
	default:
		ret = VM_FAULT_FALLBACK;
	}

out_unlock:
	up_read(&vdev->memory_lock);
out:
	dev_dbg_ratelimited(&vdev->pdev->dev,
			   "%s(,order = %d) BAR %ld page offset 0x%lx: 0x%x\n",
			    __func__, order,
			    vma->vm_pgoff >>
				(VFIO_PCI_OFFSET_SHIFT - PAGE_SHIFT),
			    pgoff, (unsigned int)ret);

	return ret;
}

static vm_fault_t vfio_pci_mmap_page_fault(struct vm_fault *vmf)
{
	return vfio_pci_mmap_huge_fault(vmf, 0);
}

static const struct vm_operations_struct vfio_pci_mmap_ops = {
	.fault = vfio_pci_mmap_page_fault,
#ifdef CONFIG_ARCH_SUPPORTS_HUGE_PFNMAP
	.huge_fault = vfio_pci_mmap_huge_fault,
#endif
};

int vfio_pci_core_mmap(struct vfio_device *core_vdev, struct vm_area_struct *vma)
{
	struct vfio_pci_core_device *vdev =
		container_of(core_vdev, struct vfio_pci_core_device, vdev);
	struct pci_dev *pdev = vdev->pdev;
	unsigned int index;
	u64 phys_len, req_len, pgoff, req_start;
	int ret;

	index = vma->vm_pgoff >> (VFIO_PCI_OFFSET_SHIFT - PAGE_SHIFT);

	if (index >= VFIO_PCI_NUM_REGIONS + vdev->num_regions)
		return -EINVAL;
	if (vma->vm_end < vma->vm_start)
		return -EINVAL;
	if ((vma->vm_flags & VM_SHARED) == 0)
		return -EINVAL;
	if (index >= VFIO_PCI_NUM_REGIONS) {
		int regnum = index - VFIO_PCI_NUM_REGIONS;
		struct vfio_pci_region *region = vdev->region + regnum;

		if (region->ops && region->ops->mmap &&
		    (region->flags & VFIO_REGION_INFO_FLAG_MMAP))
			return region->ops->mmap(vdev, region, vma);
		return -EINVAL;
	}
	if (index >= VFIO_PCI_ROM_REGION_INDEX)
		return -EINVAL;
	if (!vdev->bar_mmap_supported[index])
		return -EINVAL;

	phys_len = PAGE_ALIGN(pci_resource_len(pdev, index));
	req_len = vma->vm_end - vma->vm_start;
	pgoff = vma->vm_pgoff &
		((1U << (VFIO_PCI_OFFSET_SHIFT - PAGE_SHIFT)) - 1);
	req_start = pgoff << PAGE_SHIFT;

	if (req_start + req_len > phys_len)
		return -EINVAL;

	/*
	 * Even though we don't make use of the barmap for the mmap,
	 * we need to request the region and the barmap tracks that.
	 */
	if (!vdev->barmap[index]) {
		ret = pci_request_selected_regions(pdev,
						   1 << index, "vfio-pci");
		if (ret)
			return ret;

		vdev->barmap[index] = pci_iomap(pdev, index, 0);
		if (!vdev->barmap[index]) {
			pci_release_selected_regions(pdev, 1 << index);
			return -ENOMEM;
		}
	}

	vma->vm_private_data = vdev;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	vma->vm_page_prot = pgprot_decrypted(vma->vm_page_prot);

	/*
	 * Set vm_flags now, they should not be changed in the fault handler.
	 * We want the same flags and page protection (decrypted above) as
	 * io_remap_pfn_range() would set.
	 *
	 * VM_ALLOW_ANY_UNCACHED: The VMA flag is implemented for ARM64,
	 * allowing KVM stage 2 device mapping attributes to use Normal-NC
	 * rather than DEVICE_nGnRE, which allows guest mappings
	 * supporting write-combining attributes (WC). ARM does not
	 * architecturally guarantee this is safe, and indeed some MMIO
	 * regions like the GICv2 VCPU interface can trigger uncontained
	 * faults if Normal-NC is used.
	 *
	 * To safely use VFIO in KVM the platform must guarantee full
	 * safety in the guest where no action taken against a MMIO
	 * mapping can trigger an uncontained failure. The assumption is
	 * that most VFIO PCI platforms support this for both mapping types,
	 * at least in common flows, based on some expectations of how
	 * PCI IP is integrated. Hence VM_ALLOW_ANY_UNCACHED is set in
	 * the VMA flags.
	 */
	vm_flags_set(vma, VM_ALLOW_ANY_UNCACHED | VM_IO | VM_PFNMAP |
			VM_DONTEXPAND | VM_DONTDUMP);
	vma->vm_ops = &vfio_pci_mmap_ops;

	return 0;
}
EXPORT_SYMBOL_GPL(vfio_pci_core_mmap);

void vfio_pci_core_request(struct vfio_device *core_vdev, unsigned int count)
{
	struct vfio_pci_core_device *vdev =
		container_of(core_vdev, struct vfio_pci_core_device, vdev);
	struct pci_dev *pdev = vdev->pdev;

	mutex_lock(&vdev->igate);

	if (vdev->req_trigger) {
		if (!(count % 10))
			pci_notice_ratelimited(pdev,
				"Relaying device request to user (#%u)\n",
				count);
		eventfd_signal(vdev->req_trigger);
	} else if (count == 0) {
		pci_warn(pdev,
			"No device request channel registered, blocked until released by user\n");
	}

	mutex_unlock(&vdev->igate);
}
EXPORT_SYMBOL_GPL(vfio_pci_core_request);

static int vfio_pci_validate_vf_token(struct vfio_pci_core_device *vdev,
				      bool vf_token, uuid_t *uuid)
{
	/*
	 * There's always some degree of trust or collaboration between SR-IOV
	 * PF and VFs, even if just that the PF hosts the SR-IOV capability and
	 * can disrupt VFs with a reset, but often the PF has more explicit
	 * access to deny service to the VF or access data passed through the
	 * VF.  We therefore require an opt-in via a shared VF token (UUID) to
	 * represent this trust.  This both prevents that a VF driver might
	 * assume the PF driver is a trusted, in-kernel driver, and also that
	 * a PF driver might be replaced with a rogue driver, unknown to in-use
	 * VF drivers.
	 *
	 * Therefore when presented with a VF, if the PF is a vfio device and
	 * it is bound to the vfio-pci driver, the user needs to provide a VF
	 * token to access the device, in the form of appending a vf_token to
	 * the device name, for example:
	 *
	 * "0000:04:10.0 vf_token=bd8d9d2b-5a5f-4f5a-a211-f591514ba1f3"
	 *
	 * When presented with a PF which has VFs in use, the user must also
	 * provide the current VF token to prove collaboration with existing
	 * VF users.  If VFs are not in use, the VF token provided for the PF
	 * device will act to set the VF token.
	 *
	 * If the VF token is provided but unused, an error is generated.
	 */
	if (vdev->pdev->is_virtfn) {
		struct vfio_pci_core_device *pf_vdev = vdev->sriov_pf_core_dev;
		bool match;

		if (!pf_vdev) {
			if (!vf_token)
				return 0; /* PF is not vfio-pci, no VF token */

			pci_info_ratelimited(vdev->pdev,
				"VF token incorrectly provided, PF not bound to vfio-pci\n");
			return -EINVAL;
		}

		if (!vf_token) {
			pci_info_ratelimited(vdev->pdev,
				"VF token required to access device\n");
			return -EACCES;
		}

		mutex_lock(&pf_vdev->vf_token->lock);
		match = uuid_equal(uuid, &pf_vdev->vf_token->uuid);
		mutex_unlock(&pf_vdev->vf_token->lock);

		if (!match) {
			pci_info_ratelimited(vdev->pdev,
				"Incorrect VF token provided for device\n");
			return -EACCES;
		}
	} else if (vdev->vf_token) {
		mutex_lock(&vdev->vf_token->lock);
		if (vdev->vf_token->users) {
			if (!vf_token) {
				mutex_unlock(&vdev->vf_token->lock);
				pci_info_ratelimited(vdev->pdev,
					"VF token required to access device\n");
				return -EACCES;
			}

			if (!uuid_equal(uuid, &vdev->vf_token->uuid)) {
				mutex_unlock(&vdev->vf_token->lock);
				pci_info_ratelimited(vdev->pdev,
					"Incorrect VF token provided for device\n");
				return -EACCES;
			}
		} else if (vf_token) {
			uuid_copy(&vdev->vf_token->uuid, uuid);
		}

		mutex_unlock(&vdev->vf_token->lock);
	} else if (vf_token) {
		pci_info_ratelimited(vdev->pdev,
			"VF token incorrectly provided, not a PF or VF\n");
		return -EINVAL;
	}

	return 0;
}

#define VF_TOKEN_ARG "vf_token="

int vfio_pci_core_match(struct vfio_device *core_vdev, char *buf)
{
	struct vfio_pci_core_device *vdev =
		container_of(core_vdev, struct vfio_pci_core_device, vdev);
	bool vf_token = false;
	uuid_t uuid;
	int ret;

	if (strncmp(pci_name(vdev->pdev), buf, strlen(pci_name(vdev->pdev))))
		return 0; /* No match */

	if (strlen(buf) > strlen(pci_name(vdev->pdev))) {
		buf += strlen(pci_name(vdev->pdev));

		if (*buf != ' ')
			return 0; /* No match: non-whitespace after name */

		while (*buf) {
			if (*buf == ' ') {
				buf++;
				continue;
			}

			if (!vf_token && !strncmp(buf, VF_TOKEN_ARG,
						  strlen(VF_TOKEN_ARG))) {
				buf += strlen(VF_TOKEN_ARG);

				if (strlen(buf) < UUID_STRING_LEN)
					return -EINVAL;

				ret = uuid_parse(buf, &uuid);
				if (ret)
					return ret;

				vf_token = true;
				buf += UUID_STRING_LEN;
			} else {
				/* Unknown/duplicate option */
				return -EINVAL;
			}
		}
	}

	ret = vfio_pci_validate_vf_token(vdev, vf_token, &uuid);
	if (ret)
		return ret;

	return 1; /* Match */
}
EXPORT_SYMBOL_GPL(vfio_pci_core_match);

static int vfio_pci_bus_notifier(struct notifier_block *nb,
				 unsigned long action, void *data)
{
	struct vfio_pci_core_device *vdev = container_of(nb,
						    struct vfio_pci_core_device, nb);
	struct device *dev = data;
	struct pci_dev *pdev = to_pci_dev(dev);
	struct pci_dev *physfn = pci_physfn(pdev);

	if (action == BUS_NOTIFY_ADD_DEVICE &&
	    pdev->is_virtfn && physfn == vdev->pdev) {
		pci_info(vdev->pdev, "Captured SR-IOV VF %s driver_override\n",
			 pci_name(pdev));
		pdev->driver_override = kasprintf(GFP_KERNEL, "%s",
						  vdev->vdev.ops->name);
		WARN_ON(!pdev->driver_override);
	} else if (action == BUS_NOTIFY_BOUND_DRIVER &&
		   pdev->is_virtfn && physfn == vdev->pdev) {
		struct pci_driver *drv = pci_dev_driver(pdev);

		if (drv && drv != pci_dev_driver(vdev->pdev))
			pci_warn(vdev->pdev,
				 "VF %s bound to driver %s while PF bound to driver %s\n",
				 pci_name(pdev), drv->name,
				 pci_dev_driver(vdev->pdev)->name);
	}

	return 0;
}

static int vfio_pci_vf_init(struct vfio_pci_core_device *vdev)
{
	struct pci_dev *pdev = vdev->pdev;
	struct vfio_pci_core_device *cur;
	struct pci_dev *physfn;
	int ret;

	if (pdev->is_virtfn) {
		/*
		 * If this VF was created by our vfio_pci_core_sriov_configure()
		 * then we can find the PF vfio_pci_core_device now, and due to
		 * the locking in pci_disable_sriov() it cannot change until
		 * this VF device driver is removed.
		 */
		physfn = pci_physfn(vdev->pdev);
		mutex_lock(&vfio_pci_sriov_pfs_mutex);
		list_for_each_entry(cur, &vfio_pci_sriov_pfs, sriov_pfs_item) {
			if (cur->pdev == physfn) {
				vdev->sriov_pf_core_dev = cur;
				break;
			}
		}
		mutex_unlock(&vfio_pci_sriov_pfs_mutex);
		return 0;
	}

	/* Not a SRIOV PF */
	if (!pdev->is_physfn)
		return 0;

	vdev->vf_token = kzalloc(sizeof(*vdev->vf_token), GFP_KERNEL);
	if (!vdev->vf_token)
		return -ENOMEM;

	mutex_init(&vdev->vf_token->lock);
	uuid_gen(&vdev->vf_token->uuid);

	vdev->nb.notifier_call = vfio_pci_bus_notifier;
	ret = bus_register_notifier(&pci_bus_type, &vdev->nb);
	if (ret) {
		kfree(vdev->vf_token);
		return ret;
	}
	return 0;
}

static void vfio_pci_vf_uninit(struct vfio_pci_core_device *vdev)
{
	if (!vdev->vf_token)
		return;

	bus_unregister_notifier(&pci_bus_type, &vdev->nb);
	WARN_ON(vdev->vf_token->users);
	mutex_destroy(&vdev->vf_token->lock);
	kfree(vdev->vf_token);
}

static int vfio_pci_vga_init(struct vfio_pci_core_device *vdev)
{
	struct pci_dev *pdev = vdev->pdev;
	int ret;

	if (!vfio_pci_is_vga(pdev))
		return 0;

	ret = aperture_remove_conflicting_pci_devices(pdev, vdev->vdev.ops->name);
	if (ret)
		return ret;

	ret = vga_client_register(pdev, vfio_pci_set_decode);
	if (ret)
		return ret;
	vga_set_legacy_decoding(pdev, vfio_pci_set_decode(pdev, false));
	return 0;
}

static void vfio_pci_vga_uninit(struct vfio_pci_core_device *vdev)
{
	struct pci_dev *pdev = vdev->pdev;

	if (!vfio_pci_is_vga(pdev))
		return;
	vga_client_unregister(pdev);
	vga_set_legacy_decoding(pdev, VGA_RSRC_NORMAL_IO | VGA_RSRC_NORMAL_MEM |
					      VGA_RSRC_LEGACY_IO |
					      VGA_RSRC_LEGACY_MEM);
}

int vfio_pci_core_init_dev(struct vfio_device *core_vdev)
{
	struct vfio_pci_core_device *vdev =
		container_of(core_vdev, struct vfio_pci_core_device, vdev);

	vdev->pdev = to_pci_dev(core_vdev->dev);
	vdev->irq_type = VFIO_PCI_NUM_IRQS;
	mutex_init(&vdev->igate);
	spin_lock_init(&vdev->irqlock);
	mutex_init(&vdev->ioeventfds_lock);
	INIT_LIST_HEAD(&vdev->dummy_resources_list);
	INIT_LIST_HEAD(&vdev->ioeventfds_list);
	INIT_LIST_HEAD(&vdev->sriov_pfs_item);
	init_rwsem(&vdev->memory_lock);
	xa_init(&vdev->ctx);

	return 0;
}
EXPORT_SYMBOL_GPL(vfio_pci_core_init_dev);

void vfio_pci_core_release_dev(struct vfio_device *core_vdev)
{
	struct vfio_pci_core_device *vdev =
		container_of(core_vdev, struct vfio_pci_core_device, vdev);

	mutex_destroy(&vdev->igate);
	mutex_destroy(&vdev->ioeventfds_lock);
	kfree(vdev->region);
	kfree(vdev->pm_save);
}
EXPORT_SYMBOL_GPL(vfio_pci_core_release_dev);

int vfio_pci_core_register_device(struct vfio_pci_core_device *vdev)
{
	struct pci_dev *pdev = vdev->pdev;
	struct device *dev = &pdev->dev;
	int ret;

	/* Drivers must set the vfio_pci_core_device to their drvdata */
	if (WARN_ON(vdev != dev_get_drvdata(dev)))
		return -EINVAL;

	if (pdev->hdr_type != PCI_HEADER_TYPE_NORMAL)
		return -EINVAL;

	if (vdev->vdev.mig_ops) {
		if (!(vdev->vdev.mig_ops->migration_get_state &&
		      vdev->vdev.mig_ops->migration_set_state &&
		      vdev->vdev.mig_ops->migration_get_data_size) ||
		    !(vdev->vdev.migration_flags & VFIO_MIGRATION_STOP_COPY))
			return -EINVAL;
	}

	if (vdev->vdev.log_ops && !(vdev->vdev.log_ops->log_start &&
	    vdev->vdev.log_ops->log_stop &&
	    vdev->vdev.log_ops->log_read_and_clear))
		return -EINVAL;

	/*
	 * Prevent binding to PFs with VFs enabled, the VFs might be in use
	 * by the host or other users.  We cannot capture the VFs if they
	 * already exist, nor can we track VF users.  Disabling SR-IOV here
	 * would initiate removing the VFs, which would unbind the driver,
	 * which is prone to blocking if that VF is also in use by vfio-pci.
	 * Just reject these PFs and let the user sort it out.
	 */
	if (pci_num_vf(pdev)) {
		pci_warn(pdev, "Cannot bind to PF with SR-IOV enabled\n");
		return -EBUSY;
	}

	if (pci_is_root_bus(pdev->bus)) {
		ret = vfio_assign_device_set(&vdev->vdev, vdev);
	} else if (!pci_probe_reset_slot(pdev->slot)) {
		ret = vfio_assign_device_set(&vdev->vdev, pdev->slot);
	} else {
		/*
		 * If there is no slot reset support for this device, the whole
		 * bus needs to be grouped together to support bus-wide resets.
		 */
		ret = vfio_assign_device_set(&vdev->vdev, pdev->bus);
	}

	if (ret)
		return ret;
	ret = vfio_pci_vf_init(vdev);
	if (ret)
		return ret;
	ret = vfio_pci_vga_init(vdev);
	if (ret)
		goto out_vf;

	vfio_pci_probe_power_state(vdev);

	/*
	 * pci-core sets the device power state to an unknown value at
	 * bootup and after being removed from a driver.  The only
	 * transition it allows from this unknown state is to D0, which
	 * typically happens when a driver calls pci_enable_device().
	 * We're not ready to enable the device yet, but we do want to
	 * be able to get to D3.  Therefore first do a D0 transition
	 * before enabling runtime PM.
	 */
	vfio_pci_set_power_state(vdev, PCI_D0);

	dev->driver->pm = &vfio_pci_core_pm_ops;
	pm_runtime_allow(dev);
	if (!disable_idle_d3)
		pm_runtime_put(dev);

	ret = vfio_register_group_dev(&vdev->vdev);
	if (ret)
		goto out_power;
	return 0;

out_power:
	if (!disable_idle_d3)
		pm_runtime_get_noresume(dev);

	pm_runtime_forbid(dev);
out_vf:
	vfio_pci_vf_uninit(vdev);
	return ret;
}
EXPORT_SYMBOL_GPL(vfio_pci_core_register_device);

void vfio_pci_core_unregister_device(struct vfio_pci_core_device *vdev)
{
	vfio_pci_core_sriov_configure(vdev, 0);

	vfio_unregister_group_dev(&vdev->vdev);

	vfio_pci_vf_uninit(vdev);
	vfio_pci_vga_uninit(vdev);

	if (!disable_idle_d3)
		pm_runtime_get_noresume(&vdev->pdev->dev);

	pm_runtime_forbid(&vdev->pdev->dev);
}
EXPORT_SYMBOL_GPL(vfio_pci_core_unregister_device);

pci_ers_result_t vfio_pci_core_aer_err_detected(struct pci_dev *pdev,
						pci_channel_state_t state)
{
	struct vfio_pci_core_device *vdev = dev_get_drvdata(&pdev->dev);

	mutex_lock(&vdev->igate);

	if (vdev->err_trigger)
		eventfd_signal(vdev->err_trigger);

	mutex_unlock(&vdev->igate);

	return PCI_ERS_RESULT_CAN_RECOVER;
}
EXPORT_SYMBOL_GPL(vfio_pci_core_aer_err_detected);

int vfio_pci_core_sriov_configure(struct vfio_pci_core_device *vdev,
				  int nr_virtfn)
{
	struct pci_dev *pdev = vdev->pdev;
	int ret = 0;

	device_lock_assert(&pdev->dev);

	if (nr_virtfn) {
		mutex_lock(&vfio_pci_sriov_pfs_mutex);
		/*
		 * The thread that adds the vdev to the list is the only thread
		 * that gets to call pci_enable_sriov() and we will only allow
		 * it to be called once without going through
		 * pci_disable_sriov()
		 */
		if (!list_empty(&vdev->sriov_pfs_item)) {
			ret = -EINVAL;
			goto out_unlock;
		}
		list_add_tail(&vdev->sriov_pfs_item, &vfio_pci_sriov_pfs);
		mutex_unlock(&vfio_pci_sriov_pfs_mutex);

		/*
		 * The PF power state should always be higher than the VF power
		 * state. The PF can be in low power state either with runtime
		 * power management (when there is no user) or PCI_PM_CTRL
		 * register write by the user. If PF is in the low power state,
		 * then change the power state to D0 first before enabling
		 * SR-IOV. Also, this function can be called at any time, and
		 * userspace PCI_PM_CTRL write can race against this code path,
		 * so protect the same with 'memory_lock'.
		 */
		ret = pm_runtime_resume_and_get(&pdev->dev);
		if (ret)
			goto out_del;

		down_write(&vdev->memory_lock);
		vfio_pci_set_power_state(vdev, PCI_D0);
		ret = pci_enable_sriov(pdev, nr_virtfn);
		up_write(&vdev->memory_lock);
		if (ret) {
			pm_runtime_put(&pdev->dev);
			goto out_del;
		}
		return nr_virtfn;
	}

	if (pci_num_vf(pdev)) {
		pci_disable_sriov(pdev);
		pm_runtime_put(&pdev->dev);
	}

out_del:
	mutex_lock(&vfio_pci_sriov_pfs_mutex);
	list_del_init(&vdev->sriov_pfs_item);
out_unlock:
	mutex_unlock(&vfio_pci_sriov_pfs_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(vfio_pci_core_sriov_configure);

const struct pci_error_handlers vfio_pci_core_err_handlers = {
	.error_detected = vfio_pci_core_aer_err_detected,
};
EXPORT_SYMBOL_GPL(vfio_pci_core_err_handlers);

static bool vfio_dev_in_groups(struct vfio_device *vdev,
			       struct vfio_pci_group_info *groups)
{
	unsigned int i;

	if (!groups)
		return false;

	for (i = 0; i < groups->count; i++)
		if (vfio_file_has_dev(groups->files[i], vdev))
			return true;
	return false;
}

static int vfio_pci_is_device_in_set(struct pci_dev *pdev, void *data)
{
	struct vfio_device_set *dev_set = data;

	return vfio_find_device_in_devset(dev_set, &pdev->dev) ? 0 : -ENODEV;
}

/*
 * vfio-core considers a group to be viable and will create a vfio_device even
 * if some devices are bound to drivers like pci-stub or pcieport. Here we
 * require all PCI devices to be inside our dev_set since that ensures they stay
 * put and that every driver controlling the device can co-ordinate with the
 * device reset.
 *
 * Returns the pci_dev to pass to pci_reset_bus() if every PCI device to be
 * reset is inside the dev_set, and pci_reset_bus() can succeed. NULL otherwise.
 */
static struct pci_dev *
vfio_pci_dev_set_resettable(struct vfio_device_set *dev_set)
{
	struct pci_dev *pdev;

	lockdep_assert_held(&dev_set->lock);

	/*
	 * By definition all PCI devices in the dev_set share the same PCI
	 * reset, so any pci_dev will have the same outcomes for
	 * pci_probe_reset_*() and pci_reset_bus().
	 */
	pdev = list_first_entry(&dev_set->device_list,
				struct vfio_pci_core_device,
				vdev.dev_set_list)->pdev;

	/* pci_reset_bus() is supported */
	if (pci_probe_reset_slot(pdev->slot) && pci_probe_reset_bus(pdev->bus))
		return NULL;

	if (vfio_pci_for_each_slot_or_bus(pdev, vfio_pci_is_device_in_set,
					  dev_set,
					  !pci_probe_reset_slot(pdev->slot)))
		return NULL;
	return pdev;
}

static int vfio_pci_dev_set_pm_runtime_get(struct vfio_device_set *dev_set)
{
	struct vfio_pci_core_device *cur;
	int ret;

	list_for_each_entry(cur, &dev_set->device_list, vdev.dev_set_list) {
		ret = pm_runtime_resume_and_get(&cur->pdev->dev);
		if (ret)
			goto unwind;
	}

	return 0;

unwind:
	list_for_each_entry_continue_reverse(cur, &dev_set->device_list,
					     vdev.dev_set_list)
		pm_runtime_put(&cur->pdev->dev);

	return ret;
}

static int vfio_pci_dev_set_hot_reset(struct vfio_device_set *dev_set,
				      struct vfio_pci_group_info *groups,
				      struct iommufd_ctx *iommufd_ctx)
{
	struct vfio_pci_core_device *vdev;
	struct pci_dev *pdev;
	int ret;

	mutex_lock(&dev_set->lock);

	pdev = vfio_pci_dev_set_resettable(dev_set);
	if (!pdev) {
		ret = -EINVAL;
		goto err_unlock;
	}

	/*
	 * Some of the devices in the dev_set can be in the runtime suspended
	 * state. Increment the usage count for all the devices in the dev_set
	 * before reset and decrement the same after reset.
	 */
	ret = vfio_pci_dev_set_pm_runtime_get(dev_set);
	if (ret)
		goto err_unlock;

	list_for_each_entry(vdev, &dev_set->device_list, vdev.dev_set_list) {
		bool owned;

		/*
		 * Test whether all the affected devices can be reset by the
		 * user.
		 *
		 * If called from a group opened device and the user provides
		 * a set of groups, all the devices in the dev_set should be
		 * contained by the set of groups provided by the user.
		 *
		 * If called from a cdev opened device and the user provides
		 * a zero-length array, all the devices in the dev_set must
		 * be bound to the same iommufd_ctx as the input iommufd_ctx.
		 * If there is any device that has not been bound to any
		 * iommufd_ctx yet, check if its iommu_group has any device
		 * bound to the input iommufd_ctx.  Such devices can be
		 * considered owned by the input iommufd_ctx as the device
		 * cannot be owned by another iommufd_ctx when its iommu_group
		 * is owned.
		 *
		 * Otherwise, reset is not allowed.
		 */
		if (iommufd_ctx) {
			int devid = vfio_iommufd_get_dev_id(&vdev->vdev,
							    iommufd_ctx);

			owned = (devid > 0 || devid == -ENOENT);
		} else {
			owned = vfio_dev_in_groups(&vdev->vdev, groups);
		}

		if (!owned) {
			ret = -EINVAL;
			break;
		}

		/*
		 * Take the memory write lock for each device and zap BAR
		 * mappings to prevent the user accessing the device while in
		 * reset.  Locking multiple devices is prone to deadlock,
		 * runaway and unwind if we hit contention.
		 */
		if (!down_write_trylock(&vdev->memory_lock)) {
			ret = -EBUSY;
			break;
		}

		vfio_pci_zap_bars(vdev);
	}

	if (!list_entry_is_head(vdev,
				&dev_set->device_list, vdev.dev_set_list)) {
		vdev = list_prev_entry(vdev, vdev.dev_set_list);
		goto err_undo;
	}

	/*
	 * The pci_reset_bus() will reset all the devices in the bus.
	 * The power state can be non-D0 for some of the devices in the bus.
	 * For these devices, the pci_reset_bus() will internally set
	 * the power state to D0 without vfio driver involvement.
	 * For the devices which have NoSoftRst-, the reset function can
	 * cause the PCI config space reset without restoring the original
	 * state (saved locally in 'vdev->pm_save').
	 */
	list_for_each_entry(vdev, &dev_set->device_list, vdev.dev_set_list)
		vfio_pci_set_power_state(vdev, PCI_D0);

	ret = pci_reset_bus(pdev);

	vdev = list_last_entry(&dev_set->device_list,
			       struct vfio_pci_core_device, vdev.dev_set_list);

err_undo:
	list_for_each_entry_from_reverse(vdev, &dev_set->device_list,
					 vdev.dev_set_list)
		up_write(&vdev->memory_lock);

	list_for_each_entry(vdev, &dev_set->device_list, vdev.dev_set_list)
		pm_runtime_put(&vdev->pdev->dev);

err_unlock:
	mutex_unlock(&dev_set->lock);
	return ret;
}

static bool vfio_pci_dev_set_needs_reset(struct vfio_device_set *dev_set)
{
	struct vfio_pci_core_device *cur;
	bool needs_reset = false;

	/* No other VFIO device in the set can be open. */
	if (vfio_device_set_open_count(dev_set) > 1)
		return false;

	list_for_each_entry(cur, &dev_set->device_list, vdev.dev_set_list)
		needs_reset |= cur->needs_reset;
	return needs_reset;
}

/*
 * If a bus or slot reset is available for the provided dev_set and:
 *  - All of the devices affected by that bus or slot reset are unused
 *  - At least one of the affected devices is marked dirty via
 *    needs_reset (such as by lack of FLR support)
 * Then attempt to perform that bus or slot reset.
 */
static void vfio_pci_dev_set_try_reset(struct vfio_device_set *dev_set)
{
	struct vfio_pci_core_device *cur;
	struct pci_dev *pdev;
	bool reset_done = false;

	if (!vfio_pci_dev_set_needs_reset(dev_set))
		return;

	pdev = vfio_pci_dev_set_resettable(dev_set);
	if (!pdev)
		return;

	/*
	 * Some of the devices in the bus can be in the runtime suspended
	 * state. Increment the usage count for all the devices in the dev_set
	 * before reset and decrement the same after reset.
	 */
	if (!disable_idle_d3 && vfio_pci_dev_set_pm_runtime_get(dev_set))
		return;

	if (!pci_reset_bus(pdev))
		reset_done = true;

	list_for_each_entry(cur, &dev_set->device_list, vdev.dev_set_list) {
		if (reset_done)
			cur->needs_reset = false;

		if (!disable_idle_d3)
			pm_runtime_put(&cur->pdev->dev);
	}
}

void vfio_pci_core_set_params(bool is_nointxmask, bool is_disable_vga,
			      bool is_disable_idle_d3)
{
	nointxmask = is_nointxmask;
	disable_vga = is_disable_vga;
	disable_idle_d3 = is_disable_idle_d3;
}
EXPORT_SYMBOL_GPL(vfio_pci_core_set_params);

static void vfio_pci_core_cleanup(void)
{
	vfio_pci_uninit_perm_bits();
}

static int __init vfio_pci_core_init(void)
{
	/* Allocate shared config space permission data used by all devices */
	return vfio_pci_init_perm_bits();
}

module_init(vfio_pci_core_init);
module_exit(vfio_pci_core_cleanup);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
