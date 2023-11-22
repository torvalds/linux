// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#define pr_fmt(fmt)			"habanalabs: " fmt

#include <uapi/drm/habanalabs_accel.h>
#include "habanalabs.h"

#include <linux/pci.h>
#include <linux/hwmon.h>
#include <linux/vmalloc.h>

#include <drm/drm_accel.h>
#include <drm/drm_drv.h>

#include <trace/events/habanalabs.h>

#define HL_RESET_DELAY_USEC			10000	/* 10ms */

#define HL_DEVICE_RELEASE_WATCHDOG_TIMEOUT_SEC	30

enum dma_alloc_type {
	DMA_ALLOC_COHERENT,
	DMA_ALLOC_POOL,
};

#define MEM_SCRUB_DEFAULT_VAL 0x1122334455667788

/*
 * hl_set_dram_bar- sets the bar to allow later access to address
 *
 * @hdev: pointer to habanalabs device structure.
 * @addr: the address the caller wants to access.
 * @region: the PCI region.
 * @new_bar_region_base: the new BAR region base address.
 *
 * @return: the old BAR base address on success, U64_MAX for failure.
 *	    The caller should set it back to the old address after use.
 *
 * In case the bar space does not cover the whole address space,
 * the bar base address should be set to allow access to a given address.
 * This function can be called also if the bar doesn't need to be set,
 * in that case it just won't change the base.
 */
static u64 hl_set_dram_bar(struct hl_device *hdev, u64 addr, struct pci_mem_region *region,
				u64 *new_bar_region_base)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u64 bar_base_addr, old_base;

	if (is_power_of_2(prop->dram_pci_bar_size))
		bar_base_addr = addr & ~(prop->dram_pci_bar_size - 0x1ull);
	else
		bar_base_addr = DIV_ROUND_DOWN_ULL(addr, prop->dram_pci_bar_size) *
				prop->dram_pci_bar_size;

	old_base = hdev->asic_funcs->set_dram_bar_base(hdev, bar_base_addr);

	/* in case of success we need to update the new BAR base */
	if ((old_base != U64_MAX) && new_bar_region_base)
		*new_bar_region_base = bar_base_addr;

	return old_base;
}

int hl_access_sram_dram_region(struct hl_device *hdev, u64 addr, u64 *val,
	enum debugfs_access_type acc_type, enum pci_region region_type, bool set_dram_bar)
{
	struct pci_mem_region *region = &hdev->pci_mem_region[region_type];
	u64 old_base = 0, rc, bar_region_base = region->region_base;
	void __iomem *acc_addr;

	if (set_dram_bar) {
		old_base = hl_set_dram_bar(hdev, addr, region, &bar_region_base);
		if (old_base == U64_MAX)
			return -EIO;
	}

	acc_addr = hdev->pcie_bar[region->bar_id] + region->offset_in_bar +
			(addr - bar_region_base);

	switch (acc_type) {
	case DEBUGFS_READ8:
		*val = readb(acc_addr);
		break;
	case DEBUGFS_WRITE8:
		writeb(*val, acc_addr);
		break;
	case DEBUGFS_READ32:
		*val = readl(acc_addr);
		break;
	case DEBUGFS_WRITE32:
		writel(*val, acc_addr);
		break;
	case DEBUGFS_READ64:
		*val = readq(acc_addr);
		break;
	case DEBUGFS_WRITE64:
		writeq(*val, acc_addr);
		break;
	}

	if (set_dram_bar) {
		rc = hl_set_dram_bar(hdev, old_base, region, NULL);
		if (rc == U64_MAX)
			return -EIO;
	}

	return 0;
}

static void *hl_dma_alloc_common(struct hl_device *hdev, size_t size, dma_addr_t *dma_handle,
					gfp_t flag, enum dma_alloc_type alloc_type,
					const char *caller)
{
	void *ptr = NULL;

	switch (alloc_type) {
	case DMA_ALLOC_COHERENT:
		ptr = hdev->asic_funcs->asic_dma_alloc_coherent(hdev, size, dma_handle, flag);
		break;
	case DMA_ALLOC_POOL:
		ptr = hdev->asic_funcs->asic_dma_pool_zalloc(hdev, size, flag, dma_handle);
		break;
	}

	if (trace_habanalabs_dma_alloc_enabled() && !ZERO_OR_NULL_PTR(ptr))
		trace_habanalabs_dma_alloc(hdev->dev, (u64) (uintptr_t) ptr, *dma_handle, size,
						caller);

	return ptr;
}

static void hl_asic_dma_free_common(struct hl_device *hdev, size_t size, void *cpu_addr,
					dma_addr_t dma_handle, enum dma_alloc_type alloc_type,
					const char *caller)
{
	/* this is needed to avoid warning on using freed pointer */
	u64 store_cpu_addr = (u64) (uintptr_t) cpu_addr;

	switch (alloc_type) {
	case DMA_ALLOC_COHERENT:
		hdev->asic_funcs->asic_dma_free_coherent(hdev, size, cpu_addr, dma_handle);
		break;
	case DMA_ALLOC_POOL:
		hdev->asic_funcs->asic_dma_pool_free(hdev, cpu_addr, dma_handle);
		break;
	}

	trace_habanalabs_dma_free(hdev->dev, store_cpu_addr, dma_handle, size, caller);
}

void *hl_asic_dma_alloc_coherent_caller(struct hl_device *hdev, size_t size, dma_addr_t *dma_handle,
					gfp_t flag, const char *caller)
{
	return hl_dma_alloc_common(hdev, size, dma_handle, flag, DMA_ALLOC_COHERENT, caller);
}

void hl_asic_dma_free_coherent_caller(struct hl_device *hdev, size_t size, void *cpu_addr,
					dma_addr_t dma_handle, const char *caller)
{
	hl_asic_dma_free_common(hdev, size, cpu_addr, dma_handle, DMA_ALLOC_COHERENT, caller);
}

void *hl_asic_dma_pool_zalloc_caller(struct hl_device *hdev, size_t size, gfp_t mem_flags,
					dma_addr_t *dma_handle, const char *caller)
{
	return hl_dma_alloc_common(hdev, size, dma_handle, mem_flags, DMA_ALLOC_POOL, caller);
}

void hl_asic_dma_pool_free_caller(struct hl_device *hdev, void *vaddr, dma_addr_t dma_addr,
					const char *caller)
{
	hl_asic_dma_free_common(hdev, 0, vaddr, dma_addr, DMA_ALLOC_POOL, caller);
}

void *hl_cpu_accessible_dma_pool_alloc(struct hl_device *hdev, size_t size, dma_addr_t *dma_handle)
{
	return hdev->asic_funcs->cpu_accessible_dma_pool_alloc(hdev, size, dma_handle);
}

void hl_cpu_accessible_dma_pool_free(struct hl_device *hdev, size_t size, void *vaddr)
{
	hdev->asic_funcs->cpu_accessible_dma_pool_free(hdev, size, vaddr);
}

int hl_dma_map_sgtable_caller(struct hl_device *hdev, struct sg_table *sgt,
				enum dma_data_direction dir, const char *caller)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct scatterlist *sg;
	int rc, i;

	rc = hdev->asic_funcs->dma_map_sgtable(hdev, sgt, dir);
	if (rc)
		return rc;

	if (!trace_habanalabs_dma_map_page_enabled())
		return 0;

	for_each_sgtable_dma_sg(sgt, sg, i)
		trace_habanalabs_dma_map_page(hdev->dev,
				page_to_phys(sg_page(sg)),
				sg->dma_address - prop->device_dma_offset_for_host_access,
#ifdef CONFIG_NEED_SG_DMA_LENGTH
				sg->dma_length,
#else
				sg->length,
#endif
				dir, caller);

	return 0;
}

int hl_asic_dma_map_sgtable(struct hl_device *hdev, struct sg_table *sgt,
				enum dma_data_direction dir)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct scatterlist *sg;
	int rc, i;

	rc = dma_map_sgtable(&hdev->pdev->dev, sgt, dir, 0);
	if (rc)
		return rc;

	/* Shift to the device's base physical address of host memory if necessary */
	if (prop->device_dma_offset_for_host_access)
		for_each_sgtable_dma_sg(sgt, sg, i)
			sg->dma_address += prop->device_dma_offset_for_host_access;

	return 0;
}

void hl_dma_unmap_sgtable_caller(struct hl_device *hdev, struct sg_table *sgt,
					enum dma_data_direction dir, const char *caller)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct scatterlist *sg;
	int i;

	hdev->asic_funcs->dma_unmap_sgtable(hdev, sgt, dir);

	if (trace_habanalabs_dma_unmap_page_enabled()) {
		for_each_sgtable_dma_sg(sgt, sg, i)
			trace_habanalabs_dma_unmap_page(hdev->dev, page_to_phys(sg_page(sg)),
					sg->dma_address - prop->device_dma_offset_for_host_access,
#ifdef CONFIG_NEED_SG_DMA_LENGTH
					sg->dma_length,
#else
					sg->length,
#endif
					dir, caller);
	}
}

void hl_asic_dma_unmap_sgtable(struct hl_device *hdev, struct sg_table *sgt,
				enum dma_data_direction dir)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct scatterlist *sg;
	int i;

	/* Cancel the device's base physical address of host memory if necessary */
	if (prop->device_dma_offset_for_host_access)
		for_each_sgtable_dma_sg(sgt, sg, i)
			sg->dma_address -= prop->device_dma_offset_for_host_access;

	dma_unmap_sgtable(&hdev->pdev->dev, sgt, dir, 0);
}

/*
 * hl_access_cfg_region - access the config region
 *
 * @hdev: pointer to habanalabs device structure
 * @addr: the address to access
 * @val: the value to write from or read to
 * @acc_type: the type of access (read/write 64/32)
 */
int hl_access_cfg_region(struct hl_device *hdev, u64 addr, u64 *val,
	enum debugfs_access_type acc_type)
{
	struct pci_mem_region *cfg_region = &hdev->pci_mem_region[PCI_REGION_CFG];
	u32 val_h, val_l;

	if (!IS_ALIGNED(addr, sizeof(u32))) {
		dev_err(hdev->dev, "address %#llx not a multiple of %zu\n", addr, sizeof(u32));
		return -EINVAL;
	}

	switch (acc_type) {
	case DEBUGFS_READ32:
		*val = RREG32(addr - cfg_region->region_base);
		break;
	case DEBUGFS_WRITE32:
		WREG32(addr - cfg_region->region_base, *val);
		break;
	case DEBUGFS_READ64:
		val_l = RREG32(addr - cfg_region->region_base);
		val_h = RREG32(addr + sizeof(u32) - cfg_region->region_base);

		*val = (((u64) val_h) << 32) | val_l;
		break;
	case DEBUGFS_WRITE64:
		WREG32(addr - cfg_region->region_base, lower_32_bits(*val));
		WREG32(addr + sizeof(u32) - cfg_region->region_base, upper_32_bits(*val));
		break;
	default:
		dev_err(hdev->dev, "access type %d is not supported\n", acc_type);
		return -EOPNOTSUPP;
	}

	return 0;
}

/*
 * hl_access_dev_mem - access device memory
 *
 * @hdev: pointer to habanalabs device structure
 * @region_type: the type of the region the address belongs to
 * @addr: the address to access
 * @val: the value to write from or read to
 * @acc_type: the type of access (r/w, 32/64)
 */
int hl_access_dev_mem(struct hl_device *hdev, enum pci_region region_type,
			u64 addr, u64 *val, enum debugfs_access_type acc_type)
{
	switch (region_type) {
	case PCI_REGION_CFG:
		return hl_access_cfg_region(hdev, addr, val, acc_type);
	case PCI_REGION_SRAM:
	case PCI_REGION_DRAM:
		return hl_access_sram_dram_region(hdev, addr, val, acc_type,
				region_type, (region_type == PCI_REGION_DRAM));
	default:
		return -EFAULT;
	}

	return 0;
}

void hl_engine_data_sprintf(struct engines_data *e, const char *fmt, ...)
{
	va_list args;
	int str_size;

	va_start(args, fmt);
	/* Calculate formatted string length. Assuming each string is null terminated, hence
	 * increment result by 1
	 */
	str_size = vsnprintf(NULL, 0, fmt, args) + 1;
	va_end(args);

	if ((e->actual_size + str_size) < e->allocated_buf_size) {
		va_start(args, fmt);
		vsnprintf(e->buf + e->actual_size, str_size, fmt, args);
		va_end(args);
	}

	/* Need to update the size even when not updating destination buffer to get the exact size
	 * of all input strings
	 */
	e->actual_size += str_size;
}

enum hl_device_status hl_device_status(struct hl_device *hdev)
{
	enum hl_device_status status;

	if (hdev->device_fini_pending) {
		status = HL_DEVICE_STATUS_MALFUNCTION;
	} else if (hdev->reset_info.in_reset) {
		if (hdev->reset_info.in_compute_reset)
			status = HL_DEVICE_STATUS_IN_RESET_AFTER_DEVICE_RELEASE;
		else
			status = HL_DEVICE_STATUS_IN_RESET;
	} else if (hdev->reset_info.needs_reset) {
		status = HL_DEVICE_STATUS_NEEDS_RESET;
	} else if (hdev->disabled) {
		status = HL_DEVICE_STATUS_MALFUNCTION;
	} else if (!hdev->init_done) {
		status = HL_DEVICE_STATUS_IN_DEVICE_CREATION;
	} else {
		status = HL_DEVICE_STATUS_OPERATIONAL;
	}

	return status;
}

bool hl_device_operational(struct hl_device *hdev,
		enum hl_device_status *status)
{
	enum hl_device_status current_status;

	current_status = hl_device_status(hdev);
	if (status)
		*status = current_status;

	switch (current_status) {
	case HL_DEVICE_STATUS_MALFUNCTION:
	case HL_DEVICE_STATUS_IN_RESET:
	case HL_DEVICE_STATUS_IN_RESET_AFTER_DEVICE_RELEASE:
	case HL_DEVICE_STATUS_NEEDS_RESET:
		return false;
	case HL_DEVICE_STATUS_OPERATIONAL:
	case HL_DEVICE_STATUS_IN_DEVICE_CREATION:
	default:
		return true;
	}
}

bool hl_ctrl_device_operational(struct hl_device *hdev,
		enum hl_device_status *status)
{
	enum hl_device_status current_status;

	current_status = hl_device_status(hdev);
	if (status)
		*status = current_status;

	switch (current_status) {
	case HL_DEVICE_STATUS_MALFUNCTION:
		return false;
	case HL_DEVICE_STATUS_IN_RESET:
	case HL_DEVICE_STATUS_IN_RESET_AFTER_DEVICE_RELEASE:
	case HL_DEVICE_STATUS_NEEDS_RESET:
	case HL_DEVICE_STATUS_OPERATIONAL:
	case HL_DEVICE_STATUS_IN_DEVICE_CREATION:
	default:
		return true;
	}
}

static void print_idle_status_mask(struct hl_device *hdev, const char *message,
					u64 idle_mask[HL_BUSY_ENGINES_MASK_EXT_SIZE])
{
	if (idle_mask[3])
		dev_err(hdev->dev, "%s (mask %#llx_%016llx_%016llx_%016llx)\n",
			message, idle_mask[3], idle_mask[2], idle_mask[1], idle_mask[0]);
	else if (idle_mask[2])
		dev_err(hdev->dev, "%s (mask %#llx_%016llx_%016llx)\n",
			message, idle_mask[2], idle_mask[1], idle_mask[0]);
	else if (idle_mask[1])
		dev_err(hdev->dev, "%s (mask %#llx_%016llx)\n",
			message, idle_mask[1], idle_mask[0]);
	else
		dev_err(hdev->dev, "%s (mask %#llx)\n", message, idle_mask[0]);
}

static void hpriv_release(struct kref *ref)
{
	u64 idle_mask[HL_BUSY_ENGINES_MASK_EXT_SIZE] = {0};
	bool reset_device, device_is_idle = true;
	struct hl_fpriv *hpriv;
	struct hl_device *hdev;

	hpriv = container_of(ref, struct hl_fpriv, refcount);

	hdev = hpriv->hdev;

	hdev->asic_funcs->send_device_activity(hdev, false);

	hl_debugfs_remove_file(hpriv);

	mutex_destroy(&hpriv->ctx_lock);
	mutex_destroy(&hpriv->restore_phase_mutex);

	/* There should be no memory buffers at this point and handles IDR can be destroyed */
	hl_mem_mgr_idr_destroy(&hpriv->mem_mgr);

	/* Device should be reset if reset-upon-device-release is enabled, or if there is a pending
	 * reset that waits for device release.
	 */
	reset_device = hdev->reset_upon_device_release || hdev->reset_info.watchdog_active;

	/* Check the device idle status and reset if not idle.
	 * Skip it if already in reset, or if device is going to be reset in any case.
	 */
	if (!hdev->reset_info.in_reset && !reset_device && !hdev->pldm)
		device_is_idle = hdev->asic_funcs->is_device_idle(hdev, idle_mask,
							HL_BUSY_ENGINES_MASK_EXT_SIZE, NULL);
	if (!device_is_idle) {
		print_idle_status_mask(hdev, "device is not idle after user context is closed",
					idle_mask);
		reset_device = true;
	}

	/* We need to remove the user from the list to make sure the reset process won't
	 * try to kill the user process. Because, if we got here, it means there are no
	 * more driver/device resources that the user process is occupying so there is
	 * no need to kill it
	 *
	 * However, we can't set the compute_ctx to NULL at this stage. This is to prevent
	 * a race between the release and opening the device again. We don't want to let
	 * a user open the device while there a reset is about to happen.
	 */
	mutex_lock(&hdev->fpriv_list_lock);
	list_del(&hpriv->dev_node);
	mutex_unlock(&hdev->fpriv_list_lock);

	put_pid(hpriv->taskpid);

	if (reset_device) {
		hl_device_reset(hdev, HL_DRV_RESET_DEV_RELEASE);
	} else {
		/* Scrubbing is handled within hl_device_reset(), so here need to do it directly */
		int rc = hdev->asic_funcs->scrub_device_mem(hdev);

		if (rc) {
			dev_err(hdev->dev, "failed to scrub memory from hpriv release (%d)\n", rc);
			hl_device_reset(hdev, HL_DRV_RESET_HARD);
		}
	}

	/* Now we can mark the compute_ctx as not active. Even if a reset is running in a different
	 * thread, we don't care because the in_reset is marked so if a user will try to open
	 * the device it will fail on that, even if compute_ctx is false.
	 */
	mutex_lock(&hdev->fpriv_list_lock);
	hdev->is_compute_ctx_active = false;
	mutex_unlock(&hdev->fpriv_list_lock);

	hdev->compute_ctx_in_release = 0;

	/* release the eventfd */
	if (hpriv->notifier_event.eventfd)
		eventfd_ctx_put(hpriv->notifier_event.eventfd);

	mutex_destroy(&hpriv->notifier_event.lock);

	kfree(hpriv);
}

void hl_hpriv_get(struct hl_fpriv *hpriv)
{
	kref_get(&hpriv->refcount);
}

int hl_hpriv_put(struct hl_fpriv *hpriv)
{
	return kref_put(&hpriv->refcount, hpriv_release);
}

static void print_device_in_use_info(struct hl_device *hdev, const char *message)
{
	u32 active_cs_num, dmabuf_export_cnt;
	bool unknown_reason = true;
	char buf[128];
	size_t size;
	int offset;

	size = sizeof(buf);
	offset = 0;

	active_cs_num = hl_get_active_cs_num(hdev);
	if (active_cs_num) {
		unknown_reason = false;
		offset += scnprintf(buf + offset, size - offset, " [%u active CS]", active_cs_num);
	}

	dmabuf_export_cnt = atomic_read(&hdev->dmabuf_export_cnt);
	if (dmabuf_export_cnt) {
		unknown_reason = false;
		offset += scnprintf(buf + offset, size - offset, " [%u exported dma-buf]",
					dmabuf_export_cnt);
	}

	if (unknown_reason)
		scnprintf(buf + offset, size - offset, " [unknown reason]");

	dev_notice(hdev->dev, "%s%s\n", message, buf);
}

/*
 * hl_device_release() - release function for habanalabs device.
 * @ddev: pointer to DRM device structure.
 * @file: pointer to DRM file private data structure.
 *
 * Called when process closes an habanalabs device
 */
void hl_device_release(struct drm_device *ddev, struct drm_file *file_priv)
{
	struct hl_fpriv *hpriv = file_priv->driver_priv;
	struct hl_device *hdev = to_hl_device(ddev);

	if (!hdev) {
		pr_crit("Closing FD after device was removed. Memory leak will occur and it is advised to reboot.\n");
		put_pid(hpriv->taskpid);
	}

	hl_ctx_mgr_fini(hdev, &hpriv->ctx_mgr);

	/* Memory buffers might be still in use at this point and thus the handles IDR destruction
	 * is postponed to hpriv_release().
	 */
	hl_mem_mgr_fini(&hpriv->mem_mgr);

	hdev->compute_ctx_in_release = 1;

	if (!hl_hpriv_put(hpriv)) {
		print_device_in_use_info(hdev, "User process closed FD but device still in use");
		hl_device_reset(hdev, HL_DRV_RESET_HARD);
	}

	hdev->last_open_session_duration_jif = jiffies - hdev->last_successful_open_jif;
}

static int hl_device_release_ctrl(struct inode *inode, struct file *filp)
{
	struct hl_fpriv *hpriv = filp->private_data;
	struct hl_device *hdev = hpriv->hdev;

	filp->private_data = NULL;

	if (!hdev) {
		pr_err("Closing FD after device was removed\n");
		goto out;
	}

	mutex_lock(&hdev->fpriv_ctrl_list_lock);
	list_del(&hpriv->dev_node);
	mutex_unlock(&hdev->fpriv_ctrl_list_lock);
out:
	put_pid(hpriv->taskpid);

	kfree(hpriv);

	return 0;
}

static int __hl_mmap(struct hl_fpriv *hpriv, struct vm_area_struct *vma)
{
	struct hl_device *hdev = hpriv->hdev;
	unsigned long vm_pgoff;

	if (!hdev) {
		pr_err_ratelimited("Trying to mmap after device was removed! Please close FD\n");
		return -ENODEV;
	}

	vm_pgoff = vma->vm_pgoff;

	switch (vm_pgoff & HL_MMAP_TYPE_MASK) {
	case HL_MMAP_TYPE_BLOCK:
		vma->vm_pgoff = HL_MMAP_OFFSET_VALUE_GET(vm_pgoff);
		return hl_hw_block_mmap(hpriv, vma);

	case HL_MMAP_TYPE_CB:
	case HL_MMAP_TYPE_TS_BUFF:
		return hl_mem_mgr_mmap(&hpriv->mem_mgr, vma, NULL);
	}
	return -EINVAL;
}

/*
 * hl_mmap - mmap function for habanalabs device
 *
 * @*filp: pointer to file structure
 * @*vma: pointer to vm_area_struct of the process
 *
 * Called when process does an mmap on habanalabs device. Call the relevant mmap
 * function at the end of the common code.
 */
int hl_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_file *file_priv = filp->private_data;
	struct hl_fpriv *hpriv = file_priv->driver_priv;

	return __hl_mmap(hpriv, vma);
}

static const struct file_operations hl_ctrl_ops = {
	.owner = THIS_MODULE,
	.open = hl_device_open_ctrl,
	.release = hl_device_release_ctrl,
	.unlocked_ioctl = hl_ioctl_control,
	.compat_ioctl = hl_ioctl_control
};

static void device_release_func(struct device *dev)
{
	kfree(dev);
}

/*
 * device_init_cdev - Initialize cdev and device for habanalabs device
 *
 * @hdev: pointer to habanalabs device structure
 * @class: pointer to the class object of the device
 * @minor: minor number of the specific device
 * @fops: file operations to install for this device
 * @name: name of the device as it will appear in the filesystem
 * @cdev: pointer to the char device object that will be initialized
 * @dev: pointer to the device object that will be initialized
 *
 * Initialize a cdev and a Linux device for habanalabs's device.
 */
static int device_init_cdev(struct hl_device *hdev, const struct class *class,
				int minor, const struct file_operations *fops,
				char *name, struct cdev *cdev,
				struct device **dev)
{
	cdev_init(cdev, fops);
	cdev->owner = THIS_MODULE;

	*dev = kzalloc(sizeof(**dev), GFP_KERNEL);
	if (!*dev)
		return -ENOMEM;

	device_initialize(*dev);
	(*dev)->devt = MKDEV(hdev->major, minor);
	(*dev)->class = class;
	(*dev)->release = device_release_func;
	dev_set_drvdata(*dev, hdev);
	dev_set_name(*dev, "%s", name);

	return 0;
}

static int cdev_sysfs_debugfs_add(struct hl_device *hdev)
{
	const struct class *accel_class = hdev->drm.accel->kdev->class;
	char name[32];
	int rc;

	hdev->cdev_idx = hdev->drm.accel->index;

	/* Initialize cdev and device structures for the control device */
	snprintf(name, sizeof(name), "accel_controlD%d", hdev->cdev_idx);
	rc = device_init_cdev(hdev, accel_class, hdev->cdev_idx, &hl_ctrl_ops, name,
				&hdev->cdev_ctrl, &hdev->dev_ctrl);
	if (rc)
		return rc;

	rc = cdev_device_add(&hdev->cdev_ctrl, hdev->dev_ctrl);
	if (rc) {
		dev_err(hdev->dev_ctrl,
			"failed to add an accel control char device to the system\n");
		goto free_ctrl_device;
	}

	rc = hl_sysfs_init(hdev);
	if (rc) {
		dev_err(hdev->dev, "failed to initialize sysfs\n");
		goto delete_ctrl_cdev_device;
	}

	hl_debugfs_add_device(hdev);

	hdev->cdev_sysfs_debugfs_created = true;

	return 0;

delete_ctrl_cdev_device:
	cdev_device_del(&hdev->cdev_ctrl, hdev->dev_ctrl);
free_ctrl_device:
	put_device(hdev->dev_ctrl);
	return rc;
}

static void cdev_sysfs_debugfs_remove(struct hl_device *hdev)
{
	if (!hdev->cdev_sysfs_debugfs_created)
		return;

	hl_sysfs_fini(hdev);

	cdev_device_del(&hdev->cdev_ctrl, hdev->dev_ctrl);
	put_device(hdev->dev_ctrl);
}

static void device_hard_reset_pending(struct work_struct *work)
{
	struct hl_device_reset_work *device_reset_work =
		container_of(work, struct hl_device_reset_work, reset_work.work);
	struct hl_device *hdev = device_reset_work->hdev;
	u32 flags;
	int rc;

	flags = device_reset_work->flags | HL_DRV_RESET_FROM_RESET_THR;

	rc = hl_device_reset(hdev, flags);

	if ((rc == -EBUSY) && !hdev->device_fini_pending) {
		struct hl_ctx *ctx = hl_get_compute_ctx(hdev);

		if (ctx) {
			/* The read refcount value should subtracted by one, because the read is
			 * protected with hl_get_compute_ctx().
			 */
			dev_info(hdev->dev,
				"Could not reset device (compute_ctx refcount %u). will try again in %u seconds",
				kref_read(&ctx->refcount) - 1, HL_PENDING_RESET_PER_SEC);
			hl_ctx_put(ctx);
		} else {
			dev_info(hdev->dev, "Could not reset device. will try again in %u seconds",
				HL_PENDING_RESET_PER_SEC);
		}

		queue_delayed_work(hdev->reset_wq, &device_reset_work->reset_work,
					msecs_to_jiffies(HL_PENDING_RESET_PER_SEC * 1000));
	}
}

static void device_release_watchdog_func(struct work_struct *work)
{
	struct hl_device_reset_work *watchdog_work =
			container_of(work, struct hl_device_reset_work, reset_work.work);
	struct hl_device *hdev = watchdog_work->hdev;
	u32 flags;

	dev_dbg(hdev->dev, "Device wasn't released in time. Initiate hard-reset.\n");

	flags = watchdog_work->flags | HL_DRV_RESET_HARD | HL_DRV_RESET_FROM_WD_THR;

	hl_device_reset(hdev, flags);
}

/*
 * device_early_init - do some early initialization for the habanalabs device
 *
 * @hdev: pointer to habanalabs device structure
 *
 * Install the relevant function pointers and call the early_init function,
 * if such a function exists
 */
static int device_early_init(struct hl_device *hdev)
{
	int i, rc;
	char workq_name[32];

	switch (hdev->asic_type) {
	case ASIC_GOYA:
		goya_set_asic_funcs(hdev);
		strscpy(hdev->asic_name, "GOYA", sizeof(hdev->asic_name));
		break;
	case ASIC_GAUDI:
		gaudi_set_asic_funcs(hdev);
		strscpy(hdev->asic_name, "GAUDI", sizeof(hdev->asic_name));
		break;
	case ASIC_GAUDI_SEC:
		gaudi_set_asic_funcs(hdev);
		strscpy(hdev->asic_name, "GAUDI SEC", sizeof(hdev->asic_name));
		break;
	case ASIC_GAUDI2:
		gaudi2_set_asic_funcs(hdev);
		strscpy(hdev->asic_name, "GAUDI2", sizeof(hdev->asic_name));
		break;
	case ASIC_GAUDI2B:
		gaudi2_set_asic_funcs(hdev);
		strscpy(hdev->asic_name, "GAUDI2B", sizeof(hdev->asic_name));
		break;
		break;
	default:
		dev_err(hdev->dev, "Unrecognized ASIC type %d\n",
			hdev->asic_type);
		return -EINVAL;
	}

	rc = hdev->asic_funcs->early_init(hdev);
	if (rc)
		return rc;

	rc = hl_asid_init(hdev);
	if (rc)
		goto early_fini;

	if (hdev->asic_prop.completion_queues_count) {
		hdev->cq_wq = kcalloc(hdev->asic_prop.completion_queues_count,
				sizeof(struct workqueue_struct *),
				GFP_KERNEL);
		if (!hdev->cq_wq) {
			rc = -ENOMEM;
			goto asid_fini;
		}
	}

	for (i = 0 ; i < hdev->asic_prop.completion_queues_count ; i++) {
		snprintf(workq_name, 32, "hl%u-free-jobs-%u", hdev->cdev_idx, (u32) i);
		hdev->cq_wq[i] = create_singlethread_workqueue(workq_name);
		if (hdev->cq_wq[i] == NULL) {
			dev_err(hdev->dev, "Failed to allocate CQ workqueue\n");
			rc = -ENOMEM;
			goto free_cq_wq;
		}
	}

	snprintf(workq_name, 32, "hl%u-events", hdev->cdev_idx);
	hdev->eq_wq = create_singlethread_workqueue(workq_name);
	if (hdev->eq_wq == NULL) {
		dev_err(hdev->dev, "Failed to allocate EQ workqueue\n");
		rc = -ENOMEM;
		goto free_cq_wq;
	}

	snprintf(workq_name, 32, "hl%u-cs-completions", hdev->cdev_idx);
	hdev->cs_cmplt_wq = alloc_workqueue(workq_name, WQ_UNBOUND, 0);
	if (!hdev->cs_cmplt_wq) {
		dev_err(hdev->dev,
			"Failed to allocate CS completions workqueue\n");
		rc = -ENOMEM;
		goto free_eq_wq;
	}

	snprintf(workq_name, 32, "hl%u-ts-free-obj", hdev->cdev_idx);
	hdev->ts_free_obj_wq = alloc_workqueue(workq_name, WQ_UNBOUND, 0);
	if (!hdev->ts_free_obj_wq) {
		dev_err(hdev->dev,
			"Failed to allocate Timestamp registration free workqueue\n");
		rc = -ENOMEM;
		goto free_cs_cmplt_wq;
	}

	snprintf(workq_name, 32, "hl%u-prefetch", hdev->cdev_idx);
	hdev->prefetch_wq = alloc_workqueue(workq_name, WQ_UNBOUND, 0);
	if (!hdev->prefetch_wq) {
		dev_err(hdev->dev, "Failed to allocate MMU prefetch workqueue\n");
		rc = -ENOMEM;
		goto free_ts_free_wq;
	}

	hdev->hl_chip_info = kzalloc(sizeof(struct hwmon_chip_info), GFP_KERNEL);
	if (!hdev->hl_chip_info) {
		rc = -ENOMEM;
		goto free_prefetch_wq;
	}

	rc = hl_mmu_if_set_funcs(hdev);
	if (rc)
		goto free_chip_info;

	hl_mem_mgr_init(hdev->dev, &hdev->kernel_mem_mgr);

	snprintf(workq_name, 32, "hl%u_device_reset", hdev->cdev_idx);
	hdev->reset_wq = create_singlethread_workqueue(workq_name);
	if (!hdev->reset_wq) {
		rc = -ENOMEM;
		dev_err(hdev->dev, "Failed to create device reset WQ\n");
		goto free_cb_mgr;
	}

	INIT_DELAYED_WORK(&hdev->device_reset_work.reset_work, device_hard_reset_pending);
	hdev->device_reset_work.hdev = hdev;
	hdev->device_fini_pending = 0;

	INIT_DELAYED_WORK(&hdev->device_release_watchdog_work.reset_work,
				device_release_watchdog_func);
	hdev->device_release_watchdog_work.hdev = hdev;

	mutex_init(&hdev->send_cpu_message_lock);
	mutex_init(&hdev->debug_lock);
	INIT_LIST_HEAD(&hdev->cs_mirror_list);
	spin_lock_init(&hdev->cs_mirror_lock);
	spin_lock_init(&hdev->reset_info.lock);
	INIT_LIST_HEAD(&hdev->fpriv_list);
	INIT_LIST_HEAD(&hdev->fpriv_ctrl_list);
	mutex_init(&hdev->fpriv_list_lock);
	mutex_init(&hdev->fpriv_ctrl_list_lock);
	mutex_init(&hdev->clk_throttling.lock);

	return 0;

free_cb_mgr:
	hl_mem_mgr_fini(&hdev->kernel_mem_mgr);
	hl_mem_mgr_idr_destroy(&hdev->kernel_mem_mgr);
free_chip_info:
	kfree(hdev->hl_chip_info);
free_prefetch_wq:
	destroy_workqueue(hdev->prefetch_wq);
free_ts_free_wq:
	destroy_workqueue(hdev->ts_free_obj_wq);
free_cs_cmplt_wq:
	destroy_workqueue(hdev->cs_cmplt_wq);
free_eq_wq:
	destroy_workqueue(hdev->eq_wq);
free_cq_wq:
	for (i = 0 ; i < hdev->asic_prop.completion_queues_count ; i++)
		if (hdev->cq_wq[i])
			destroy_workqueue(hdev->cq_wq[i]);
	kfree(hdev->cq_wq);
asid_fini:
	hl_asid_fini(hdev);
early_fini:
	if (hdev->asic_funcs->early_fini)
		hdev->asic_funcs->early_fini(hdev);

	return rc;
}

/*
 * device_early_fini - finalize all that was done in device_early_init
 *
 * @hdev: pointer to habanalabs device structure
 *
 */
static void device_early_fini(struct hl_device *hdev)
{
	int i;

	mutex_destroy(&hdev->debug_lock);
	mutex_destroy(&hdev->send_cpu_message_lock);

	mutex_destroy(&hdev->fpriv_list_lock);
	mutex_destroy(&hdev->fpriv_ctrl_list_lock);

	mutex_destroy(&hdev->clk_throttling.lock);

	hl_mem_mgr_fini(&hdev->kernel_mem_mgr);
	hl_mem_mgr_idr_destroy(&hdev->kernel_mem_mgr);

	kfree(hdev->hl_chip_info);

	destroy_workqueue(hdev->prefetch_wq);
	destroy_workqueue(hdev->ts_free_obj_wq);
	destroy_workqueue(hdev->cs_cmplt_wq);
	destroy_workqueue(hdev->eq_wq);
	destroy_workqueue(hdev->reset_wq);

	for (i = 0 ; i < hdev->asic_prop.completion_queues_count ; i++)
		destroy_workqueue(hdev->cq_wq[i]);
	kfree(hdev->cq_wq);

	hl_asid_fini(hdev);

	if (hdev->asic_funcs->early_fini)
		hdev->asic_funcs->early_fini(hdev);
}

static bool is_pci_link_healthy(struct hl_device *hdev)
{
	u16 vendor_id;

	if (!hdev->pdev)
		return false;

	pci_read_config_word(hdev->pdev, PCI_VENDOR_ID, &vendor_id);

	return (vendor_id == PCI_VENDOR_ID_HABANALABS);
}

static void hl_device_eq_heartbeat(struct hl_device *hdev)
{
	u64 event_mask = HL_NOTIFIER_EVENT_DEVICE_RESET | HL_NOTIFIER_EVENT_DEVICE_UNAVAILABLE;
	struct asic_fixed_properties *prop = &hdev->asic_prop;

	if (!prop->cpucp_info.eq_health_check_supported)
		return;

	if (hdev->eq_heartbeat_received)
		hdev->eq_heartbeat_received = false;
	else
		hl_device_cond_reset(hdev, HL_DRV_RESET_HARD, event_mask);
}

static void hl_device_heartbeat(struct work_struct *work)
{
	struct hl_device *hdev = container_of(work, struct hl_device,
						work_heartbeat.work);
	struct hl_info_fw_err_info info = {0};
	u64 event_mask = HL_NOTIFIER_EVENT_DEVICE_RESET | HL_NOTIFIER_EVENT_DEVICE_UNAVAILABLE;

	/* Start heartbeat checks only after driver has enabled events from FW */
	if (!hl_device_operational(hdev, NULL) || !hdev->init_done)
		goto reschedule;

	/*
	 * For EQ health check need to check if driver received the heartbeat eq event
	 * in order to validate the eq is working.
	 */
	hl_device_eq_heartbeat(hdev);

	if (!hdev->asic_funcs->send_heartbeat(hdev))
		goto reschedule;

	if (hl_device_operational(hdev, NULL))
		dev_err(hdev->dev, "Device heartbeat failed! PCI link is %s\n",
			is_pci_link_healthy(hdev) ? "healthy" : "broken");

	info.err_type = HL_INFO_FW_HEARTBEAT_ERR;
	info.event_mask = &event_mask;
	hl_handle_fw_err(hdev, &info);
	hl_device_cond_reset(hdev, HL_DRV_RESET_HARD | HL_DRV_RESET_HEARTBEAT, event_mask);

	return;

reschedule:
	/*
	 * prev_reset_trigger tracks consecutive fatal h/w errors until first
	 * heartbeat immediately post reset.
	 * If control reached here, then at least one heartbeat work has been
	 * scheduled since last reset/init cycle.
	 * So if the device is not already in reset cycle, reset the flag
	 * prev_reset_trigger as no reset occurred with HL_DRV_RESET_FW_FATAL_ERR
	 * status for at least one heartbeat. From this point driver restarts
	 * tracking future consecutive fatal errors.
	 */
	if (!hdev->reset_info.in_reset)
		hdev->reset_info.prev_reset_trigger = HL_RESET_TRIGGER_DEFAULT;

	schedule_delayed_work(&hdev->work_heartbeat,
			usecs_to_jiffies(HL_HEARTBEAT_PER_USEC));
}

/*
 * device_late_init - do late stuff initialization for the habanalabs device
 *
 * @hdev: pointer to habanalabs device structure
 *
 * Do stuff that either needs the device H/W queues to be active or needs
 * to happen after all the rest of the initialization is finished
 */
static int device_late_init(struct hl_device *hdev)
{
	int rc;

	if (hdev->asic_funcs->late_init) {
		rc = hdev->asic_funcs->late_init(hdev);
		if (rc) {
			dev_err(hdev->dev,
				"failed late initialization for the H/W\n");
			return rc;
		}
	}

	hdev->high_pll = hdev->asic_prop.high_pll;

	if (hdev->heartbeat) {
		/*
		 * Before scheduling the heartbeat driver will check if eq event has received.
		 * for the first schedule we need to set the indication as true then for the next
		 * one this indication will be true only if eq event was sent by FW.
		 */
		hdev->eq_heartbeat_received = true;

		INIT_DELAYED_WORK(&hdev->work_heartbeat, hl_device_heartbeat);

		schedule_delayed_work(&hdev->work_heartbeat,
				usecs_to_jiffies(HL_HEARTBEAT_PER_USEC));
	}

	hdev->late_init_done = true;

	return 0;
}

/*
 * device_late_fini - finalize all that was done in device_late_init
 *
 * @hdev: pointer to habanalabs device structure
 *
 */
static void device_late_fini(struct hl_device *hdev)
{
	if (!hdev->late_init_done)
		return;

	if (hdev->heartbeat)
		cancel_delayed_work_sync(&hdev->work_heartbeat);

	if (hdev->asic_funcs->late_fini)
		hdev->asic_funcs->late_fini(hdev);

	hdev->late_init_done = false;
}

int hl_device_utilization(struct hl_device *hdev, u32 *utilization)
{
	u64 max_power, curr_power, dc_power, dividend, divisor;
	int rc;

	max_power = hdev->max_power;
	dc_power = hdev->asic_prop.dc_power_default;
	divisor = max_power - dc_power;
	if (!divisor) {
		dev_warn(hdev->dev, "device utilization is not supported\n");
		return -EOPNOTSUPP;
	}
	rc = hl_fw_cpucp_power_get(hdev, &curr_power);

	if (rc)
		return rc;

	curr_power = clamp(curr_power, dc_power, max_power);

	dividend = (curr_power - dc_power) * 100;
	*utilization = (u32) div_u64(dividend, divisor);

	return 0;
}

int hl_device_set_debug_mode(struct hl_device *hdev, struct hl_ctx *ctx, bool enable)
{
	int rc = 0;

	mutex_lock(&hdev->debug_lock);

	if (!enable) {
		if (!hdev->in_debug) {
			dev_err(hdev->dev,
				"Failed to disable debug mode because device was not in debug mode\n");
			rc = -EFAULT;
			goto out;
		}

		if (!hdev->reset_info.hard_reset_pending)
			hdev->asic_funcs->halt_coresight(hdev, ctx);

		hdev->in_debug = 0;

		goto out;
	}

	if (hdev->in_debug) {
		dev_err(hdev->dev,
			"Failed to enable debug mode because device is already in debug mode\n");
		rc = -EFAULT;
		goto out;
	}

	hdev->in_debug = 1;

out:
	mutex_unlock(&hdev->debug_lock);

	return rc;
}

static void take_release_locks(struct hl_device *hdev)
{
	/* Flush anyone that is inside the critical section of enqueue
	 * jobs to the H/W
	 */
	hdev->asic_funcs->hw_queues_lock(hdev);
	hdev->asic_funcs->hw_queues_unlock(hdev);

	/* Flush processes that are sending message to CPU */
	mutex_lock(&hdev->send_cpu_message_lock);
	mutex_unlock(&hdev->send_cpu_message_lock);

	/* Flush anyone that is inside device open */
	mutex_lock(&hdev->fpriv_list_lock);
	mutex_unlock(&hdev->fpriv_list_lock);
	mutex_lock(&hdev->fpriv_ctrl_list_lock);
	mutex_unlock(&hdev->fpriv_ctrl_list_lock);
}

static void hl_abort_waiting_for_completions(struct hl_device *hdev)
{
	hl_abort_waiting_for_cs_completions(hdev);

	/* Release all pending user interrupts, each pending user interrupt
	 * holds a reference to a user context.
	 */
	hl_release_pending_user_interrupts(hdev);
}

static void cleanup_resources(struct hl_device *hdev, bool hard_reset, bool fw_reset,
				bool skip_wq_flush)
{
	if (hard_reset)
		device_late_fini(hdev);

	/*
	 * Halt the engines and disable interrupts so we won't get any more
	 * completions from H/W and we won't have any accesses from the
	 * H/W to the host machine
	 */
	hdev->asic_funcs->halt_engines(hdev, hard_reset, fw_reset);

	/* Go over all the queues, release all CS and their jobs */
	hl_cs_rollback_all(hdev, skip_wq_flush);

	/* flush the MMU prefetch workqueue */
	flush_workqueue(hdev->prefetch_wq);

	hl_abort_waiting_for_completions(hdev);
}

/*
 * hl_device_suspend - initiate device suspend
 *
 * @hdev: pointer to habanalabs device structure
 *
 * Puts the hw in the suspend state (all asics).
 * Returns 0 for success or an error on failure.
 * Called at driver suspend.
 */
int hl_device_suspend(struct hl_device *hdev)
{
	int rc;

	pci_save_state(hdev->pdev);

	/* Block future CS/VM/JOB completion operations */
	spin_lock(&hdev->reset_info.lock);
	if (hdev->reset_info.in_reset) {
		spin_unlock(&hdev->reset_info.lock);
		dev_err(hdev->dev, "Can't suspend while in reset\n");
		return -EIO;
	}
	hdev->reset_info.in_reset = 1;
	spin_unlock(&hdev->reset_info.lock);

	/* This blocks all other stuff that is not blocked by in_reset */
	hdev->disabled = true;

	take_release_locks(hdev);

	rc = hdev->asic_funcs->suspend(hdev);
	if (rc)
		dev_err(hdev->dev,
			"Failed to disable PCI access of device CPU\n");

	/* Shut down the device */
	pci_disable_device(hdev->pdev);
	pci_set_power_state(hdev->pdev, PCI_D3hot);

	return 0;
}

/*
 * hl_device_resume - initiate device resume
 *
 * @hdev: pointer to habanalabs device structure
 *
 * Bring the hw back to operating state (all asics).
 * Returns 0 for success or an error on failure.
 * Called at driver resume.
 */
int hl_device_resume(struct hl_device *hdev)
{
	int rc;

	pci_set_power_state(hdev->pdev, PCI_D0);
	pci_restore_state(hdev->pdev);
	rc = pci_enable_device_mem(hdev->pdev);
	if (rc) {
		dev_err(hdev->dev,
			"Failed to enable PCI device in resume\n");
		return rc;
	}

	pci_set_master(hdev->pdev);

	rc = hdev->asic_funcs->resume(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to resume device after suspend\n");
		goto disable_device;
	}


	/* 'in_reset' was set to true during suspend, now we must clear it in order
	 * for hard reset to be performed
	 */
	spin_lock(&hdev->reset_info.lock);
	hdev->reset_info.in_reset = 0;
	spin_unlock(&hdev->reset_info.lock);

	rc = hl_device_reset(hdev, HL_DRV_RESET_HARD);
	if (rc) {
		dev_err(hdev->dev, "Failed to reset device during resume\n");
		goto disable_device;
	}

	return 0;

disable_device:
	pci_disable_device(hdev->pdev);

	return rc;
}

static int device_kill_open_processes(struct hl_device *hdev, u32 timeout, bool control_dev)
{
	struct task_struct *task = NULL;
	struct list_head *hpriv_list;
	struct hl_fpriv *hpriv;
	struct mutex *hpriv_lock;
	u32 pending_cnt;

	hpriv_lock = control_dev ? &hdev->fpriv_ctrl_list_lock : &hdev->fpriv_list_lock;
	hpriv_list = control_dev ? &hdev->fpriv_ctrl_list : &hdev->fpriv_list;

	/* Giving time for user to close FD, and for processes that are inside
	 * hl_device_open to finish
	 */
	if (!list_empty(hpriv_list))
		ssleep(1);

	if (timeout) {
		pending_cnt = timeout;
	} else {
		if (hdev->process_kill_trial_cnt) {
			/* Processes have been already killed */
			pending_cnt = 1;
			goto wait_for_processes;
		} else {
			/* Wait a small period after process kill */
			pending_cnt = HL_PENDING_RESET_PER_SEC;
		}
	}

	mutex_lock(hpriv_lock);

	/* This section must be protected because we are dereferencing
	 * pointers that are freed if the process exits
	 */
	list_for_each_entry(hpriv, hpriv_list, dev_node) {
		task = get_pid_task(hpriv->taskpid, PIDTYPE_PID);
		if (task) {
			dev_info(hdev->dev, "Killing user process pid=%d\n",
				task_pid_nr(task));
			send_sig(SIGKILL, task, 1);
			usleep_range(1000, 10000);

			put_task_struct(task);
		} else {
			dev_dbg(hdev->dev,
				"Can't get task struct for user process %d, process was killed from outside the driver\n",
				pid_nr(hpriv->taskpid));
		}
	}

	mutex_unlock(hpriv_lock);

	/*
	 * We killed the open users, but that doesn't mean they are closed.
	 * It could be that they are running a long cleanup phase in the driver
	 * e.g. MMU unmappings, or running other long teardown flow even before
	 * our cleanup.
	 * Therefore we need to wait again to make sure they are closed before
	 * continuing with the reset.
	 */

wait_for_processes:
	while ((!list_empty(hpriv_list)) && (pending_cnt)) {
		dev_dbg(hdev->dev,
			"Waiting for all unmap operations to finish before hard reset\n");

		pending_cnt--;

		ssleep(1);
	}

	/* All processes exited successfully */
	if (list_empty(hpriv_list))
		return 0;

	/* Give up waiting for processes to exit */
	if (hdev->process_kill_trial_cnt == HL_PENDING_RESET_MAX_TRIALS)
		return -ETIME;

	hdev->process_kill_trial_cnt++;

	return -EBUSY;
}

static void device_disable_open_processes(struct hl_device *hdev, bool control_dev)
{
	struct list_head *hpriv_list;
	struct hl_fpriv *hpriv;
	struct mutex *hpriv_lock;

	hpriv_lock = control_dev ? &hdev->fpriv_ctrl_list_lock : &hdev->fpriv_list_lock;
	hpriv_list = control_dev ? &hdev->fpriv_ctrl_list : &hdev->fpriv_list;

	mutex_lock(hpriv_lock);
	list_for_each_entry(hpriv, hpriv_list, dev_node)
		hpriv->hdev = NULL;
	mutex_unlock(hpriv_lock);
}

static void send_disable_pci_access(struct hl_device *hdev, u32 flags)
{
	/* If reset is due to heartbeat, device CPU is no responsive in
	 * which case no point sending PCI disable message to it.
	 */
	if ((flags & HL_DRV_RESET_HARD) &&
			!(flags & (HL_DRV_RESET_HEARTBEAT | HL_DRV_RESET_BYPASS_REQ_TO_FW))) {
		/* Disable PCI access from device F/W so he won't send
		 * us additional interrupts. We disable MSI/MSI-X at
		 * the halt_engines function and we can't have the F/W
		 * sending us interrupts after that. We need to disable
		 * the access here because if the device is marked
		 * disable, the message won't be send. Also, in case
		 * of heartbeat, the device CPU is marked as disable
		 * so this message won't be sent
		 */
		if (hl_fw_send_pci_access_msg(hdev, CPUCP_PACKET_DISABLE_PCI_ACCESS, 0x0)) {
			dev_warn(hdev->dev, "Failed to disable FW's PCI access\n");
			return;
		}

		/* verify that last EQs are handled before disabled is set */
		if (hdev->cpu_queues_enable)
			synchronize_irq(pci_irq_vector(hdev->pdev,
					hdev->asic_prop.eq_interrupt_id));
	}
}

static void handle_reset_trigger(struct hl_device *hdev, u32 flags)
{
	u32 cur_reset_trigger = HL_RESET_TRIGGER_DEFAULT;

	/* No consecutive mechanism when user context exists */
	if (hdev->is_compute_ctx_active)
		return;

	/*
	 * 'reset cause' is being updated here, because getting here
	 * means that it's the 1st time and the last time we're here
	 * ('in_reset' makes sure of it). This makes sure that
	 * 'reset_cause' will continue holding its 1st recorded reason!
	 */
	if (flags & HL_DRV_RESET_HEARTBEAT) {
		hdev->reset_info.curr_reset_cause = HL_RESET_CAUSE_HEARTBEAT;
		cur_reset_trigger = HL_DRV_RESET_HEARTBEAT;
	} else if (flags & HL_DRV_RESET_TDR) {
		hdev->reset_info.curr_reset_cause = HL_RESET_CAUSE_TDR;
		cur_reset_trigger = HL_DRV_RESET_TDR;
	} else if (flags & HL_DRV_RESET_FW_FATAL_ERR) {
		hdev->reset_info.curr_reset_cause = HL_RESET_CAUSE_UNKNOWN;
		cur_reset_trigger = HL_DRV_RESET_FW_FATAL_ERR;
	} else {
		hdev->reset_info.curr_reset_cause = HL_RESET_CAUSE_UNKNOWN;
	}

	/*
	 * If reset cause is same twice, then reset_trigger_repeated
	 * is set and if this reset is due to a fatal FW error
	 * device is set to an unstable state.
	 */
	if (hdev->reset_info.prev_reset_trigger != cur_reset_trigger) {
		hdev->reset_info.prev_reset_trigger = cur_reset_trigger;
		hdev->reset_info.reset_trigger_repeated = 0;
	} else {
		hdev->reset_info.reset_trigger_repeated = 1;
	}
}

/*
 * hl_device_reset - reset the device
 *
 * @hdev: pointer to habanalabs device structure
 * @flags: reset flags.
 *
 * Block future CS and wait for pending CS to be enqueued
 * Call ASIC H/W fini
 * Flush all completions
 * Re-initialize all internal data structures
 * Call ASIC H/W init, late_init
 * Test queues
 * Enable device
 *
 * Returns 0 for success or an error on failure.
 */
int hl_device_reset(struct hl_device *hdev, u32 flags)
{
	bool hard_reset, from_hard_reset_thread, fw_reset, reset_upon_device_release,
		schedule_hard_reset = false, delay_reset, from_dev_release, from_watchdog_thread;
	u64 idle_mask[HL_BUSY_ENGINES_MASK_EXT_SIZE] = {0};
	struct hl_ctx *ctx;
	int i, rc, hw_fini_rc;

	if (!hdev->init_done) {
		dev_err(hdev->dev, "Can't reset before initialization is done\n");
		return 0;
	}

	hard_reset = !!(flags & HL_DRV_RESET_HARD);
	from_hard_reset_thread = !!(flags & HL_DRV_RESET_FROM_RESET_THR);
	fw_reset = !!(flags & HL_DRV_RESET_BYPASS_REQ_TO_FW);
	from_dev_release = !!(flags & HL_DRV_RESET_DEV_RELEASE);
	delay_reset = !!(flags & HL_DRV_RESET_DELAY);
	from_watchdog_thread = !!(flags & HL_DRV_RESET_FROM_WD_THR);
	reset_upon_device_release = hdev->reset_upon_device_release && from_dev_release;

	if (!hard_reset && (hl_device_status(hdev) == HL_DEVICE_STATUS_MALFUNCTION)) {
		dev_dbg(hdev->dev, "soft-reset isn't supported on a malfunctioning device\n");
		return 0;
	}

	if (!hard_reset && !hdev->asic_prop.supports_compute_reset) {
		dev_dbg(hdev->dev, "asic doesn't support compute reset - do hard-reset instead\n");
		hard_reset = true;
	}

	if (reset_upon_device_release) {
		if (hard_reset) {
			dev_crit(hdev->dev,
				"Aborting reset because hard-reset is mutually exclusive with reset-on-device-release\n");
			return -EINVAL;
		}

		goto do_reset;
	}

	if (!hard_reset && !hdev->asic_prop.allow_inference_soft_reset) {
		dev_dbg(hdev->dev,
			"asic doesn't allow inference soft reset - do hard-reset instead\n");
		hard_reset = true;
	}

do_reset:
	/* Re-entry of reset thread */
	if (from_hard_reset_thread && hdev->process_kill_trial_cnt)
		goto kill_processes;

	/*
	 * Prevent concurrency in this function - only one reset should be
	 * done at any given time. We need to perform this only if we didn't
	 * get here from a dedicated hard reset thread.
	 */
	if (!from_hard_reset_thread) {
		/* Block future CS/VM/JOB completion operations */
		spin_lock(&hdev->reset_info.lock);
		if (hdev->reset_info.in_reset) {
			/* We allow scheduling of a hard reset only during a compute reset */
			if (hard_reset && hdev->reset_info.in_compute_reset)
				hdev->reset_info.hard_reset_schedule_flags = flags;
			spin_unlock(&hdev->reset_info.lock);
			return 0;
		}

		/* This still allows the completion of some KDMA ops
		 * Update this before in_reset because in_compute_reset implies we are in reset
		 */
		hdev->reset_info.in_compute_reset = !hard_reset;

		hdev->reset_info.in_reset = 1;

		spin_unlock(&hdev->reset_info.lock);

		/* Cancel the device release watchdog work if required.
		 * In case of reset-upon-device-release while the release watchdog work is
		 * scheduled due to a hard-reset, do hard-reset instead of compute-reset.
		 */
		if ((hard_reset || from_dev_release) && hdev->reset_info.watchdog_active) {
			struct hl_device_reset_work *watchdog_work =
					&hdev->device_release_watchdog_work;

			hdev->reset_info.watchdog_active = 0;
			if (!from_watchdog_thread)
				cancel_delayed_work_sync(&watchdog_work->reset_work);

			if (from_dev_release && (watchdog_work->flags & HL_DRV_RESET_HARD)) {
				hdev->reset_info.in_compute_reset = 0;
				flags |= HL_DRV_RESET_HARD;
				flags &= ~HL_DRV_RESET_DEV_RELEASE;
				hard_reset = true;
			}
		}

		if (delay_reset)
			usleep_range(HL_RESET_DELAY_USEC, HL_RESET_DELAY_USEC << 1);

escalate_reset_flow:
		handle_reset_trigger(hdev, flags);
		send_disable_pci_access(hdev, flags);

		/* This also blocks future CS/VM/JOB completion operations */
		hdev->disabled = true;

		take_release_locks(hdev);

		if (hard_reset)
			dev_info(hdev->dev, "Going to reset device\n");
		else if (reset_upon_device_release)
			dev_dbg(hdev->dev, "Going to reset device after release by user\n");
		else
			dev_dbg(hdev->dev, "Going to reset engines of inference device\n");
	}

	if ((hard_reset) && (!from_hard_reset_thread)) {
		hdev->reset_info.hard_reset_pending = true;

		hdev->process_kill_trial_cnt = 0;

		hdev->device_reset_work.flags = flags;

		/*
		 * Because the reset function can't run from heartbeat work,
		 * we need to call the reset function from a dedicated work.
		 */
		queue_delayed_work(hdev->reset_wq, &hdev->device_reset_work.reset_work, 0);

		return 0;
	}

	cleanup_resources(hdev, hard_reset, fw_reset, from_dev_release);

kill_processes:
	if (hard_reset) {
		/* Kill processes here after CS rollback. This is because the
		 * process can't really exit until all its CSs are done, which
		 * is what we do in cs rollback
		 */
		rc = device_kill_open_processes(hdev, 0, false);

		if (rc == -EBUSY) {
			if (hdev->device_fini_pending) {
				dev_crit(hdev->dev,
					"%s Failed to kill all open processes, stopping hard reset\n",
					dev_name(&(hdev)->pdev->dev));
				goto out_err;
			}

			/* signal reset thread to reschedule */
			return rc;
		}

		if (rc) {
			dev_crit(hdev->dev,
				"%s Failed to kill all open processes, stopping hard reset\n",
				dev_name(&(hdev)->pdev->dev));
			goto out_err;
		}

		/* Flush the Event queue workers to make sure no other thread is
		 * reading or writing to registers during the reset
		 */
		flush_workqueue(hdev->eq_wq);
	}

	/* Reset the H/W. It will be in idle state after this returns */
	hw_fini_rc = hdev->asic_funcs->hw_fini(hdev, hard_reset, fw_reset);

	if (hard_reset) {
		hdev->fw_loader.fw_comp_loaded = FW_TYPE_NONE;

		/* Release kernel context */
		if (hdev->kernel_ctx && hl_ctx_put(hdev->kernel_ctx) == 1)
			hdev->kernel_ctx = NULL;

		hl_vm_fini(hdev);
		hl_mmu_fini(hdev);
		hl_eq_reset(hdev, &hdev->event_queue);
	}

	/* Re-initialize PI,CI to 0 in all queues (hw queue, cq) */
	hl_hw_queue_reset(hdev, hard_reset);
	for (i = 0 ; i < hdev->asic_prop.completion_queues_count ; i++)
		hl_cq_reset(hdev, &hdev->completion_queue[i]);

	/* Make sure the context switch phase will run again */
	ctx = hl_get_compute_ctx(hdev);
	if (ctx) {
		atomic_set(&ctx->thread_ctx_switch_token, 1);
		ctx->thread_ctx_switch_wait_token = 0;
		hl_ctx_put(ctx);
	}

	if (hw_fini_rc) {
		rc = hw_fini_rc;
		goto out_err;
	}
	/* Finished tear-down, starting to re-initialize */

	if (hard_reset) {
		hdev->device_cpu_disabled = false;
		hdev->reset_info.hard_reset_pending = false;

		if (hdev->reset_info.reset_trigger_repeated &&
				(hdev->reset_info.prev_reset_trigger ==
						HL_DRV_RESET_FW_FATAL_ERR)) {
			/* if there 2 back to back resets from FW,
			 * ensure driver puts the driver in a unusable state
			 */
			dev_crit(hdev->dev,
				"%s Consecutive FW fatal errors received, stopping hard reset\n",
				dev_name(&(hdev)->pdev->dev));
			rc = -EIO;
			goto out_err;
		}

		if (hdev->kernel_ctx) {
			dev_crit(hdev->dev,
				"%s kernel ctx was alive during hard reset, something is terribly wrong\n",
				dev_name(&(hdev)->pdev->dev));
			rc = -EBUSY;
			goto out_err;
		}

		rc = hl_mmu_init(hdev);
		if (rc) {
			dev_err(hdev->dev,
				"Failed to initialize MMU S/W after hard reset\n");
			goto out_err;
		}

		/* Allocate the kernel context */
		hdev->kernel_ctx = kzalloc(sizeof(*hdev->kernel_ctx),
						GFP_KERNEL);
		if (!hdev->kernel_ctx) {
			rc = -ENOMEM;
			hl_mmu_fini(hdev);
			goto out_err;
		}

		hdev->is_compute_ctx_active = false;

		rc = hl_ctx_init(hdev, hdev->kernel_ctx, true);
		if (rc) {
			dev_err(hdev->dev,
				"failed to init kernel ctx in hard reset\n");
			kfree(hdev->kernel_ctx);
			hdev->kernel_ctx = NULL;
			hl_mmu_fini(hdev);
			goto out_err;
		}
	}

	/* Device is now enabled as part of the initialization requires
	 * communication with the device firmware to get information that
	 * is required for the initialization itself
	 */
	hdev->disabled = false;

	/* F/W security enabled indication might be updated after hard-reset */
	if (hard_reset) {
		rc = hl_fw_read_preboot_status(hdev);
		if (rc)
			goto out_err;
	}

	rc = hdev->asic_funcs->hw_init(hdev);
	if (rc) {
		dev_err(hdev->dev, "failed to initialize the H/W after reset\n");
		goto out_err;
	}

	/* If device is not idle fail the reset process */
	if (!hdev->asic_funcs->is_device_idle(hdev, idle_mask,
						HL_BUSY_ENGINES_MASK_EXT_SIZE, NULL)) {
		print_idle_status_mask(hdev, "device is not idle after reset", idle_mask);
		rc = -EIO;
		goto out_err;
	}

	/* Check that the communication with the device is working */
	rc = hdev->asic_funcs->test_queues(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to detect if device is alive after reset\n");
		goto out_err;
	}

	if (hard_reset) {
		rc = device_late_init(hdev);
		if (rc) {
			dev_err(hdev->dev, "Failed late init after hard reset\n");
			goto out_err;
		}

		rc = hl_vm_init(hdev);
		if (rc) {
			dev_err(hdev->dev, "Failed to init memory module after hard reset\n");
			goto out_err;
		}

		if (!hdev->asic_prop.fw_security_enabled)
			hl_fw_set_max_power(hdev);
	} else {
		rc = hdev->asic_funcs->compute_reset_late_init(hdev);
		if (rc) {
			if (reset_upon_device_release)
				dev_err(hdev->dev,
					"Failed late init in reset after device release\n");
			else
				dev_err(hdev->dev, "Failed late init after compute reset\n");
			goto out_err;
		}
	}

	rc = hdev->asic_funcs->scrub_device_mem(hdev);
	if (rc) {
		dev_err(hdev->dev, "scrub mem failed from device reset (%d)\n", rc);
		goto out_err;
	}

	spin_lock(&hdev->reset_info.lock);
	hdev->reset_info.in_compute_reset = 0;

	/* Schedule hard reset only if requested and if not already in hard reset.
	 * We keep 'in_reset' enabled, so no other reset can go in during the hard
	 * reset schedule
	 */
	if (!hard_reset && hdev->reset_info.hard_reset_schedule_flags)
		schedule_hard_reset = true;
	else
		hdev->reset_info.in_reset = 0;

	spin_unlock(&hdev->reset_info.lock);

	hdev->reset_info.needs_reset = false;

	if (hard_reset)
		dev_info(hdev->dev,
			 "Successfully finished resetting the %s device\n",
			 dev_name(&(hdev)->pdev->dev));
	else
		dev_dbg(hdev->dev,
			"Successfully finished resetting the %s device\n",
			dev_name(&(hdev)->pdev->dev));

	if (hard_reset) {
		hdev->reset_info.hard_reset_cnt++;

		/* After reset is done, we are ready to receive events from
		 * the F/W. We can't do it before because we will ignore events
		 * and if those events are fatal, we won't know about it and
		 * the device will be operational although it shouldn't be
		 */
		hdev->asic_funcs->enable_events_from_fw(hdev);
	} else {
		if (!reset_upon_device_release)
			hdev->reset_info.compute_reset_cnt++;

		if (schedule_hard_reset) {
			dev_info(hdev->dev, "Performing hard reset scheduled during compute reset\n");
			flags = hdev->reset_info.hard_reset_schedule_flags;
			hdev->reset_info.hard_reset_schedule_flags = 0;
			hard_reset = true;
			goto escalate_reset_flow;
		}
	}

	return 0;

out_err:
	hdev->disabled = true;

	spin_lock(&hdev->reset_info.lock);
	hdev->reset_info.in_compute_reset = 0;

	if (hard_reset) {
		dev_err(hdev->dev,
			"%s Failed to reset! Device is NOT usable\n",
			dev_name(&(hdev)->pdev->dev));
		hdev->reset_info.hard_reset_cnt++;
	} else {
		if (reset_upon_device_release) {
			dev_err(hdev->dev, "Failed to reset device after user release\n");
			flags &= ~HL_DRV_RESET_DEV_RELEASE;
		} else {
			dev_err(hdev->dev, "Failed to do compute reset\n");
			hdev->reset_info.compute_reset_cnt++;
		}

		spin_unlock(&hdev->reset_info.lock);
		flags |= HL_DRV_RESET_HARD;
		hard_reset = true;
		goto escalate_reset_flow;
	}

	hdev->reset_info.in_reset = 0;

	spin_unlock(&hdev->reset_info.lock);

	return rc;
}

/*
 * hl_device_cond_reset() - conditionally reset the device.
 * @hdev: pointer to habanalabs device structure.
 * @reset_flags: reset flags.
 * @event_mask: events to notify user about.
 *
 * Conditionally reset the device, or alternatively schedule a watchdog work to reset the device
 * unless another reset precedes it.
 */
int hl_device_cond_reset(struct hl_device *hdev, u32 flags, u64 event_mask)
{
	struct hl_ctx *ctx = NULL;

	/* F/W reset cannot be postponed */
	if (flags & HL_DRV_RESET_BYPASS_REQ_TO_FW)
		goto device_reset;

	/* Device release watchdog is relevant only if user exists and gets a reset notification */
	if (!(event_mask & HL_NOTIFIER_EVENT_DEVICE_RESET)) {
		dev_err(hdev->dev, "Resetting device without a reset indication to user\n");
		goto device_reset;
	}

	ctx = hl_get_compute_ctx(hdev);
	if (!ctx)
		goto device_reset;

	/*
	 * There is no point in postponing the reset if user is not registered for events.
	 * However if no eventfd_ctx exists but the device release watchdog is already scheduled, it
	 * just implies that user has unregistered as part of handling a previous event. In this
	 * case an immediate reset is not required.
	 */
	if (!ctx->hpriv->notifier_event.eventfd && !hdev->reset_info.watchdog_active)
		goto device_reset;

	/* Schedule the device release watchdog work unless reset is already in progress or if the
	 * work is already scheduled.
	 */
	spin_lock(&hdev->reset_info.lock);
	if (hdev->reset_info.in_reset) {
		spin_unlock(&hdev->reset_info.lock);
		goto device_reset;
	}

	if (hdev->reset_info.watchdog_active) {
		hdev->device_release_watchdog_work.flags |= flags;
		goto out;
	}

	hdev->device_release_watchdog_work.flags = flags;
	dev_dbg(hdev->dev, "Device is going to be hard-reset in %u sec unless being released\n",
		hdev->device_release_watchdog_timeout_sec);
	schedule_delayed_work(&hdev->device_release_watchdog_work.reset_work,
				msecs_to_jiffies(hdev->device_release_watchdog_timeout_sec * 1000));
	hdev->reset_info.watchdog_active = 1;
out:
	spin_unlock(&hdev->reset_info.lock);

	hl_notifier_event_send_all(hdev, event_mask);

	hl_ctx_put(ctx);

	hl_abort_waiting_for_completions(hdev);

	return 0;

device_reset:
	if (event_mask)
		hl_notifier_event_send_all(hdev, event_mask);
	if (ctx)
		hl_ctx_put(ctx);

	return hl_device_reset(hdev, flags);
}

static void hl_notifier_event_send(struct hl_notifier_event *notifier_event, u64 event_mask)
{
	mutex_lock(&notifier_event->lock);
	notifier_event->events_mask |= event_mask;

	if (notifier_event->eventfd)
		eventfd_signal(notifier_event->eventfd);

	mutex_unlock(&notifier_event->lock);
}

/*
 * hl_notifier_event_send_all - notify all user processes via eventfd
 *
 * @hdev: pointer to habanalabs device structure
 * @event_mask: the occurred event/s
 * Returns 0 for success or an error on failure.
 */
void hl_notifier_event_send_all(struct hl_device *hdev, u64 event_mask)
{
	struct hl_fpriv	*hpriv;

	if (!event_mask) {
		dev_warn(hdev->dev, "Skip sending zero event");
		return;
	}

	mutex_lock(&hdev->fpriv_list_lock);

	list_for_each_entry(hpriv, &hdev->fpriv_list, dev_node)
		hl_notifier_event_send(&hpriv->notifier_event, event_mask);

	mutex_unlock(&hdev->fpriv_list_lock);
}

/*
 * hl_device_init - main initialization function for habanalabs device
 *
 * @hdev: pointer to habanalabs device structure
 *
 * Allocate an id for the device, do early initialization and then call the
 * ASIC specific initialization functions. Finally, create the cdev and the
 * Linux device to expose it to the user
 */
int hl_device_init(struct hl_device *hdev)
{
	int i, rc, cq_cnt, user_interrupt_cnt, cq_ready_cnt;
	struct hl_ts_free_jobs *free_jobs_data;
	bool expose_interfaces_on_err = false;
	void *p;

	/* Initialize ASIC function pointers and perform early init */
	rc = device_early_init(hdev);
	if (rc)
		goto out_disabled;

	user_interrupt_cnt = hdev->asic_prop.user_dec_intr_count +
				hdev->asic_prop.user_interrupt_count;

	if (user_interrupt_cnt) {
		hdev->user_interrupt = kcalloc(user_interrupt_cnt, sizeof(*hdev->user_interrupt),
						GFP_KERNEL);
		if (!hdev->user_interrupt) {
			rc = -ENOMEM;
			goto early_fini;
		}

		/* Timestamp records supported only if CQ supported in device */
		if (hdev->asic_prop.first_available_cq[0] != USHRT_MAX) {
			for (i = 0 ; i < user_interrupt_cnt ; i++) {
				p = vzalloc(TIMESTAMP_FREE_NODES_NUM *
						sizeof(struct timestamp_reg_free_node));
				if (!p) {
					rc = -ENOMEM;
					goto free_usr_intr_mem;
				}
				free_jobs_data = &hdev->user_interrupt[i].ts_free_jobs_data;
				free_jobs_data->free_nodes_pool = p;
				free_jobs_data->free_nodes_length = TIMESTAMP_FREE_NODES_NUM;
				free_jobs_data->next_avail_free_node_idx = 0;
			}
		}
	}

	free_jobs_data = &hdev->common_user_cq_interrupt.ts_free_jobs_data;
	p = vzalloc(TIMESTAMP_FREE_NODES_NUM *
				sizeof(struct timestamp_reg_free_node));
	if (!p) {
		rc = -ENOMEM;
		goto free_usr_intr_mem;
	}

	free_jobs_data->free_nodes_pool = p;
	free_jobs_data->free_nodes_length = TIMESTAMP_FREE_NODES_NUM;
	free_jobs_data->next_avail_free_node_idx = 0;

	/*
	 * Start calling ASIC initialization. First S/W then H/W and finally
	 * late init
	 */
	rc = hdev->asic_funcs->sw_init(hdev);
	if (rc)
		goto free_common_usr_intr_mem;


	/* initialize completion structure for multi CS wait */
	hl_multi_cs_completion_init(hdev);

	/*
	 * Initialize the H/W queues. Must be done before hw_init, because
	 * there the addresses of the kernel queue are being written to the
	 * registers of the device
	 */
	rc = hl_hw_queues_create(hdev);
	if (rc) {
		dev_err(hdev->dev, "failed to initialize kernel queues\n");
		goto sw_fini;
	}

	cq_cnt = hdev->asic_prop.completion_queues_count;

	/*
	 * Initialize the completion queues. Must be done before hw_init,
	 * because there the addresses of the completion queues are being
	 * passed as arguments to request_irq
	 */
	if (cq_cnt) {
		hdev->completion_queue = kcalloc(cq_cnt,
				sizeof(*hdev->completion_queue),
				GFP_KERNEL);

		if (!hdev->completion_queue) {
			dev_err(hdev->dev,
				"failed to allocate completion queues\n");
			rc = -ENOMEM;
			goto hw_queues_destroy;
		}
	}

	for (i = 0, cq_ready_cnt = 0 ; i < cq_cnt ; i++, cq_ready_cnt++) {
		rc = hl_cq_init(hdev, &hdev->completion_queue[i],
				hdev->asic_funcs->get_queue_id_for_cq(hdev, i));
		if (rc) {
			dev_err(hdev->dev,
				"failed to initialize completion queue\n");
			goto cq_fini;
		}
		hdev->completion_queue[i].cq_idx = i;
	}

	hdev->shadow_cs_queue = kcalloc(hdev->asic_prop.max_pending_cs,
					sizeof(struct hl_cs *), GFP_KERNEL);
	if (!hdev->shadow_cs_queue) {
		rc = -ENOMEM;
		goto cq_fini;
	}

	/*
	 * Initialize the event queue. Must be done before hw_init,
	 * because there the address of the event queue is being
	 * passed as argument to request_irq
	 */
	rc = hl_eq_init(hdev, &hdev->event_queue);
	if (rc) {
		dev_err(hdev->dev, "failed to initialize event queue\n");
		goto free_shadow_cs_queue;
	}

	/* MMU S/W must be initialized before kernel context is created */
	rc = hl_mmu_init(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to initialize MMU S/W structures\n");
		goto eq_fini;
	}

	/* Allocate the kernel context */
	hdev->kernel_ctx = kzalloc(sizeof(*hdev->kernel_ctx), GFP_KERNEL);
	if (!hdev->kernel_ctx) {
		rc = -ENOMEM;
		goto mmu_fini;
	}

	hdev->is_compute_ctx_active = false;

	hdev->asic_funcs->state_dump_init(hdev);

	hdev->device_release_watchdog_timeout_sec = HL_DEVICE_RELEASE_WATCHDOG_TIMEOUT_SEC;

	hdev->memory_scrub_val = MEM_SCRUB_DEFAULT_VAL;

	rc = hl_debugfs_device_init(hdev);
	if (rc) {
		dev_err(hdev->dev, "failed to initialize debugfs entry structure\n");
		kfree(hdev->kernel_ctx);
		goto mmu_fini;
	}

	/* The debugfs entry structure is accessed in hl_ctx_init(), so it must be called after
	 * hl_debugfs_device_init().
	 */
	rc = hl_ctx_init(hdev, hdev->kernel_ctx, true);
	if (rc) {
		dev_err(hdev->dev, "failed to initialize kernel context\n");
		kfree(hdev->kernel_ctx);
		goto debugfs_device_fini;
	}

	rc = hl_cb_pool_init(hdev);
	if (rc) {
		dev_err(hdev->dev, "failed to initialize CB pool\n");
		goto release_ctx;
	}

	rc = hl_dec_init(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to initialize the decoder module\n");
		goto cb_pool_fini;
	}

	/*
	 * From this point, override rc (=0) in case of an error to allow debugging
	 * (by adding char devices and creating sysfs/debugfs files as part of the error flow).
	 */
	expose_interfaces_on_err = true;

	/* Device is now enabled as part of the initialization requires
	 * communication with the device firmware to get information that
	 * is required for the initialization itself
	 */
	hdev->disabled = false;

	rc = hdev->asic_funcs->hw_init(hdev);
	if (rc) {
		dev_err(hdev->dev, "failed to initialize the H/W\n");
		rc = 0;
		goto out_disabled;
	}

	/* Check that the communication with the device is working */
	rc = hdev->asic_funcs->test_queues(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to detect if device is alive\n");
		rc = 0;
		goto out_disabled;
	}

	rc = device_late_init(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed late initialization\n");
		rc = 0;
		goto out_disabled;
	}

	dev_info(hdev->dev, "Found %s device with %lluGB DRAM\n",
		hdev->asic_name,
		hdev->asic_prop.dram_size / SZ_1G);

	rc = hl_vm_init(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to initialize memory module\n");
		rc = 0;
		goto out_disabled;
	}

	/*
	 * Expose devices and sysfs/debugfs files to user.
	 * From here there is no need to expose them in case of an error.
	 */
	expose_interfaces_on_err = false;

	rc = drm_dev_register(&hdev->drm, 0);
	if (rc) {
		dev_err(hdev->dev, "Failed to register DRM device, rc %d\n", rc);
		rc = 0;
		goto out_disabled;
	}

	rc = cdev_sysfs_debugfs_add(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to add char devices and sysfs/debugfs files\n");
		rc = 0;
		goto out_disabled;
	}

	/* Need to call this again because the max power might change,
	 * depending on card type for certain ASICs
	 */
	if (hdev->asic_prop.set_max_power_on_device_init &&
			!hdev->asic_prop.fw_security_enabled)
		hl_fw_set_max_power(hdev);

	/*
	 * hl_hwmon_init() must be called after device_late_init(), because only
	 * there we get the information from the device about which
	 * hwmon-related sensors the device supports.
	 * Furthermore, it must be done after adding the device to the system.
	 */
	rc = hl_hwmon_init(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to initialize hwmon\n");
		rc = 0;
		goto out_disabled;
	}

	dev_notice(hdev->dev,
		"Successfully added device %s to habanalabs driver\n",
		dev_name(&(hdev)->pdev->dev));

	/* After initialization is done, we are ready to receive events from
	 * the F/W. We can't do it before because we will ignore events and if
	 * those events are fatal, we won't know about it and the device will
	 * be operational although it shouldn't be
	 */
	hdev->asic_funcs->enable_events_from_fw(hdev);

	hdev->init_done = true;

	return 0;

cb_pool_fini:
	hl_cb_pool_fini(hdev);
release_ctx:
	if (hl_ctx_put(hdev->kernel_ctx) != 1)
		dev_err(hdev->dev,
			"kernel ctx is still alive on initialization failure\n");
debugfs_device_fini:
	hl_debugfs_device_fini(hdev);
mmu_fini:
	hl_mmu_fini(hdev);
eq_fini:
	hl_eq_fini(hdev, &hdev->event_queue);
free_shadow_cs_queue:
	kfree(hdev->shadow_cs_queue);
cq_fini:
	for (i = 0 ; i < cq_ready_cnt ; i++)
		hl_cq_fini(hdev, &hdev->completion_queue[i]);
	kfree(hdev->completion_queue);
hw_queues_destroy:
	hl_hw_queues_destroy(hdev);
sw_fini:
	hdev->asic_funcs->sw_fini(hdev);
free_common_usr_intr_mem:
	vfree(hdev->common_user_cq_interrupt.ts_free_jobs_data.free_nodes_pool);
free_usr_intr_mem:
	if (user_interrupt_cnt) {
		for (i = 0 ; i < user_interrupt_cnt ; i++) {
			if (!hdev->user_interrupt[i].ts_free_jobs_data.free_nodes_pool)
				break;
			vfree(hdev->user_interrupt[i].ts_free_jobs_data.free_nodes_pool);
		}
		kfree(hdev->user_interrupt);
	}
early_fini:
	device_early_fini(hdev);
out_disabled:
	hdev->disabled = true;
	if (expose_interfaces_on_err) {
		drm_dev_register(&hdev->drm, 0);
		cdev_sysfs_debugfs_add(hdev);
	}

	pr_err("Failed to initialize accel%d. Device %s is NOT usable!\n",
		hdev->cdev_idx, dev_name(&hdev->pdev->dev));

	return rc;
}

/*
 * hl_device_fini - main tear-down function for habanalabs device
 *
 * @hdev: pointer to habanalabs device structure
 *
 * Destroy the device, call ASIC fini functions and release the id
 */
void hl_device_fini(struct hl_device *hdev)
{
	u32 user_interrupt_cnt;
	bool device_in_reset;
	ktime_t timeout;
	u64 reset_sec;
	int i, rc;

	dev_info(hdev->dev, "Removing device %s\n", dev_name(&(hdev)->pdev->dev));

	hdev->device_fini_pending = 1;
	flush_delayed_work(&hdev->device_reset_work.reset_work);

	if (hdev->pldm)
		reset_sec = HL_PLDM_HARD_RESET_MAX_TIMEOUT;
	else
		reset_sec = HL_HARD_RESET_MAX_TIMEOUT;

	/*
	 * This function is competing with the reset function, so try to
	 * take the reset atomic and if we are already in middle of reset,
	 * wait until reset function is finished. Reset function is designed
	 * to always finish. However, in Gaudi, because of all the network
	 * ports, the hard reset could take between 10-30 seconds
	 */

	timeout = ktime_add_us(ktime_get(), reset_sec * 1000 * 1000);

	spin_lock(&hdev->reset_info.lock);
	device_in_reset = !!hdev->reset_info.in_reset;
	if (!device_in_reset)
		hdev->reset_info.in_reset = 1;
	spin_unlock(&hdev->reset_info.lock);

	while (device_in_reset) {
		usleep_range(50, 200);

		spin_lock(&hdev->reset_info.lock);
		device_in_reset = !!hdev->reset_info.in_reset;
		if (!device_in_reset)
			hdev->reset_info.in_reset = 1;
		spin_unlock(&hdev->reset_info.lock);

		if (ktime_compare(ktime_get(), timeout) > 0) {
			dev_crit(hdev->dev,
				"%s Failed to remove device because reset function did not finish\n",
				dev_name(&(hdev)->pdev->dev));
			return;
		}
	}

	cancel_delayed_work_sync(&hdev->device_release_watchdog_work.reset_work);

	/* Disable PCI access from device F/W so it won't send us additional
	 * interrupts. We disable MSI/MSI-X at the halt_engines function and we
	 * can't have the F/W sending us interrupts after that. We need to
	 * disable the access here because if the device is marked disable, the
	 * message won't be send. Also, in case of heartbeat, the device CPU is
	 * marked as disable so this message won't be sent
	 */
	hl_fw_send_pci_access_msg(hdev,	CPUCP_PACKET_DISABLE_PCI_ACCESS, 0x0);

	/* Mark device as disabled */
	hdev->disabled = true;

	take_release_locks(hdev);

	hdev->reset_info.hard_reset_pending = true;

	hl_hwmon_fini(hdev);

	cleanup_resources(hdev, true, false, false);

	/* Kill processes here after CS rollback. This is because the process
	 * can't really exit until all its CSs are done, which is what we
	 * do in cs rollback
	 */
	dev_info(hdev->dev,
		"Waiting for all processes to exit (timeout of %u seconds)",
		HL_WAIT_PROCESS_KILL_ON_DEVICE_FINI);

	hdev->process_kill_trial_cnt = 0;
	rc = device_kill_open_processes(hdev, HL_WAIT_PROCESS_KILL_ON_DEVICE_FINI, false);
	if (rc) {
		dev_crit(hdev->dev, "Failed to kill all open processes (%d)\n", rc);
		device_disable_open_processes(hdev, false);
	}

	hdev->process_kill_trial_cnt = 0;
	rc = device_kill_open_processes(hdev, 0, true);
	if (rc) {
		dev_crit(hdev->dev, "Failed to kill all control device open processes (%d)\n", rc);
		device_disable_open_processes(hdev, true);
	}

	hl_cb_pool_fini(hdev);

	/* Reset the H/W. It will be in idle state after this returns */
	rc = hdev->asic_funcs->hw_fini(hdev, true, false);
	if (rc)
		dev_err(hdev->dev, "hw_fini failed in device fini while removing device %d\n", rc);

	hdev->fw_loader.fw_comp_loaded = FW_TYPE_NONE;

	/* Release kernel context */
	if ((hdev->kernel_ctx) && (hl_ctx_put(hdev->kernel_ctx) != 1))
		dev_err(hdev->dev, "kernel ctx is still alive\n");

	hl_dec_fini(hdev);

	hl_vm_fini(hdev);

	hl_mmu_fini(hdev);

	vfree(hdev->captured_err_info.page_fault_info.user_mappings);

	hl_eq_fini(hdev, &hdev->event_queue);

	kfree(hdev->shadow_cs_queue);

	for (i = 0 ; i < hdev->asic_prop.completion_queues_count ; i++)
		hl_cq_fini(hdev, &hdev->completion_queue[i]);
	kfree(hdev->completion_queue);

	user_interrupt_cnt = hdev->asic_prop.user_dec_intr_count +
					hdev->asic_prop.user_interrupt_count;

	if (user_interrupt_cnt) {
		if (hdev->asic_prop.first_available_cq[0] != USHRT_MAX) {
			for (i = 0 ; i < user_interrupt_cnt ; i++)
				vfree(hdev->user_interrupt[i].ts_free_jobs_data.free_nodes_pool);
		}

		kfree(hdev->user_interrupt);
	}

	vfree(hdev->common_user_cq_interrupt.ts_free_jobs_data.free_nodes_pool);

	hl_hw_queues_destroy(hdev);

	/* Call ASIC S/W finalize function */
	hdev->asic_funcs->sw_fini(hdev);

	device_early_fini(hdev);

	/* Hide devices and sysfs/debugfs files from user */
	cdev_sysfs_debugfs_remove(hdev);
	drm_dev_unregister(&hdev->drm);

	hl_debugfs_device_fini(hdev);

	pr_info("removed device successfully\n");
}

/*
 * MMIO register access helper functions.
 */

/*
 * hl_rreg - Read an MMIO register
 *
 * @hdev: pointer to habanalabs device structure
 * @reg: MMIO register offset (in bytes)
 *
 * Returns the value of the MMIO register we are asked to read
 *
 */
inline u32 hl_rreg(struct hl_device *hdev, u32 reg)
{
	u32 val = readl(hdev->rmmio + reg);

	if (unlikely(trace_habanalabs_rreg32_enabled()))
		trace_habanalabs_rreg32(hdev->dev, reg, val);

	return val;
}

/*
 * hl_wreg - Write to an MMIO register
 *
 * @hdev: pointer to habanalabs device structure
 * @reg: MMIO register offset (in bytes)
 * @val: 32-bit value
 *
 * Writes the 32-bit value into the MMIO register
 *
 */
inline void hl_wreg(struct hl_device *hdev, u32 reg, u32 val)
{
	if (unlikely(trace_habanalabs_wreg32_enabled()))
		trace_habanalabs_wreg32(hdev->dev, reg, val);

	writel(val, hdev->rmmio + reg);
}

void hl_capture_razwi(struct hl_device *hdev, u64 addr, u16 *engine_id, u16 num_of_engines,
			u8 flags)
{
	struct razwi_info *razwi_info = &hdev->captured_err_info.razwi_info;

	if (num_of_engines > HL_RAZWI_MAX_NUM_OF_ENGINES_PER_RTR) {
		dev_err(hdev->dev,
				"Number of possible razwi initiators (%u) exceeded limit (%u)\n",
				num_of_engines, HL_RAZWI_MAX_NUM_OF_ENGINES_PER_RTR);
		return;
	}

	/* In case it's the first razwi since the device was opened, capture its parameters */
	if (atomic_cmpxchg(&hdev->captured_err_info.razwi_info.razwi_detected, 0, 1))
		return;

	razwi_info->razwi.timestamp = ktime_to_ns(ktime_get());
	razwi_info->razwi.addr = addr;
	razwi_info->razwi.num_of_possible_engines = num_of_engines;
	memcpy(&razwi_info->razwi.engine_id[0], &engine_id[0],
			num_of_engines * sizeof(u16));
	razwi_info->razwi.flags = flags;

	razwi_info->razwi_info_available = true;
}

void hl_handle_razwi(struct hl_device *hdev, u64 addr, u16 *engine_id, u16 num_of_engines,
			u8 flags, u64 *event_mask)
{
	hl_capture_razwi(hdev, addr, engine_id, num_of_engines, flags);

	if (event_mask)
		*event_mask |= HL_NOTIFIER_EVENT_RAZWI;
}

static void hl_capture_user_mappings(struct hl_device *hdev, bool is_pmmu)
{
	struct page_fault_info *pgf_info = &hdev->captured_err_info.page_fault_info;
	struct hl_vm_phys_pg_pack *phys_pg_pack = NULL;
	struct hl_vm_hash_node *hnode;
	struct hl_userptr *userptr;
	enum vm_type *vm_type;
	struct hl_ctx *ctx;
	u32 map_idx = 0;
	int i;

	/* Reset previous session count*/
	pgf_info->num_of_user_mappings = 0;

	ctx = hl_get_compute_ctx(hdev);
	if (!ctx) {
		dev_err(hdev->dev, "Can't get user context for user mappings\n");
		return;
	}

	mutex_lock(&ctx->mem_hash_lock);
	hash_for_each(ctx->mem_hash, i, hnode, node) {
		vm_type = hnode->ptr;
		if (((*vm_type == VM_TYPE_USERPTR) && is_pmmu) ||
				((*vm_type == VM_TYPE_PHYS_PACK) && !is_pmmu))
			pgf_info->num_of_user_mappings++;

	}

	if (!pgf_info->num_of_user_mappings)
		goto finish;

	/* In case we already allocated in previous session, need to release it before
	 * allocating new buffer.
	 */
	vfree(pgf_info->user_mappings);
	pgf_info->user_mappings =
			vzalloc(pgf_info->num_of_user_mappings * sizeof(struct hl_user_mapping));
	if (!pgf_info->user_mappings) {
		pgf_info->num_of_user_mappings = 0;
		goto finish;
	}

	hash_for_each(ctx->mem_hash, i, hnode, node) {
		vm_type = hnode->ptr;
		if ((*vm_type == VM_TYPE_USERPTR) && (is_pmmu)) {
			userptr = hnode->ptr;
			pgf_info->user_mappings[map_idx].dev_va = hnode->vaddr;
			pgf_info->user_mappings[map_idx].size = userptr->size;
			map_idx++;
		} else if ((*vm_type == VM_TYPE_PHYS_PACK) && (!is_pmmu)) {
			phys_pg_pack = hnode->ptr;
			pgf_info->user_mappings[map_idx].dev_va = hnode->vaddr;
			pgf_info->user_mappings[map_idx].size = phys_pg_pack->total_size;
			map_idx++;
		}
	}
finish:
	mutex_unlock(&ctx->mem_hash_lock);
	hl_ctx_put(ctx);
}

void hl_capture_page_fault(struct hl_device *hdev, u64 addr, u16 eng_id, bool is_pmmu)
{
	struct page_fault_info *pgf_info = &hdev->captured_err_info.page_fault_info;

	/* Capture only the first page fault */
	if (atomic_cmpxchg(&pgf_info->page_fault_detected, 0, 1))
		return;

	pgf_info->page_fault.timestamp = ktime_to_ns(ktime_get());
	pgf_info->page_fault.addr = addr;
	pgf_info->page_fault.engine_id = eng_id;
	hl_capture_user_mappings(hdev, is_pmmu);

	pgf_info->page_fault_info_available = true;
}

void hl_handle_page_fault(struct hl_device *hdev, u64 addr, u16 eng_id, bool is_pmmu,
				u64 *event_mask)
{
	hl_capture_page_fault(hdev, addr, eng_id, is_pmmu);

	if (event_mask)
		*event_mask |=  HL_NOTIFIER_EVENT_PAGE_FAULT;
}

static void hl_capture_hw_err(struct hl_device *hdev, u16 event_id)
{
	struct hw_err_info *info = &hdev->captured_err_info.hw_err;

	/* Capture only the first HW err */
	if (atomic_cmpxchg(&info->event_detected, 0, 1))
		return;

	info->event.timestamp = ktime_to_ns(ktime_get());
	info->event.event_id = event_id;

	info->event_info_available = true;
}

void hl_handle_critical_hw_err(struct hl_device *hdev, u16 event_id, u64 *event_mask)
{
	hl_capture_hw_err(hdev, event_id);

	if (event_mask)
		*event_mask |= HL_NOTIFIER_EVENT_CRITICL_HW_ERR;
}

static void hl_capture_fw_err(struct hl_device *hdev, struct hl_info_fw_err_info *fw_info)
{
	struct fw_err_info *info = &hdev->captured_err_info.fw_err;

	/* Capture only the first FW error */
	if (atomic_cmpxchg(&info->event_detected, 0, 1))
		return;

	info->event.timestamp = ktime_to_ns(ktime_get());
	info->event.err_type = fw_info->err_type;
	if (fw_info->err_type == HL_INFO_FW_REPORTED_ERR)
		info->event.event_id = fw_info->event_id;

	info->event_info_available = true;
}

void hl_handle_fw_err(struct hl_device *hdev, struct hl_info_fw_err_info *info)
{
	hl_capture_fw_err(hdev, info);

	if (info->event_mask)
		*info->event_mask |= HL_NOTIFIER_EVENT_CRITICL_FW_ERR;
}

void hl_capture_engine_err(struct hl_device *hdev, u16 engine_id, u16 error_count)
{
	struct engine_err_info *info = &hdev->captured_err_info.engine_err;

	/* Capture only the first engine error */
	if (atomic_cmpxchg(&info->event_detected, 0, 1))
		return;

	info->event.timestamp = ktime_to_ns(ktime_get());
	info->event.engine_id = engine_id;
	info->event.error_count = error_count;
	info->event_info_available = true;
}

void hl_enable_err_info_capture(struct hl_error_info *captured_err_info)
{
	vfree(captured_err_info->page_fault_info.user_mappings);
	memset(captured_err_info, 0, sizeof(struct hl_error_info));
	atomic_set(&captured_err_info->cs_timeout.write_enable, 1);
	captured_err_info->undef_opcode.write_enable = true;
}
