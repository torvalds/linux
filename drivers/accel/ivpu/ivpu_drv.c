// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2024 Intel Corporation
 */

#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <generated/utsrelease.h>

#include <drm/drm_accel.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_prime.h>

#include "ivpu_coredump.h"
#include "ivpu_debugfs.h"
#include "ivpu_drv.h"
#include "ivpu_fw.h"
#include "ivpu_fw_log.h"
#include "ivpu_gem.h"
#include "ivpu_hw.h"
#include "ivpu_ipc.h"
#include "ivpu_job.h"
#include "ivpu_jsm_msg.h"
#include "ivpu_mmu.h"
#include "ivpu_mmu_context.h"
#include "ivpu_ms.h"
#include "ivpu_pm.h"
#include "ivpu_sysfs.h"
#include "vpu_boot_api.h"

#ifndef DRIVER_VERSION_STR
#define DRIVER_VERSION_STR "1.0.0 " UTS_RELEASE
#endif

static struct lock_class_key submitted_jobs_xa_lock_class_key;

int ivpu_dbg_mask;
module_param_named(dbg_mask, ivpu_dbg_mask, int, 0644);
MODULE_PARM_DESC(dbg_mask, "Driver debug mask. See IVPU_DBG_* macros.");

int ivpu_test_mode;
#if IS_ENABLED(CONFIG_DRM_ACCEL_IVPU_DEBUG)
module_param_named_unsafe(test_mode, ivpu_test_mode, int, 0644);
MODULE_PARM_DESC(test_mode, "Test mode mask. See IVPU_TEST_MODE_* macros.");
#endif

u8 ivpu_pll_min_ratio;
module_param_named(pll_min_ratio, ivpu_pll_min_ratio, byte, 0644);
MODULE_PARM_DESC(pll_min_ratio, "Minimum PLL ratio used to set NPU frequency");

u8 ivpu_pll_max_ratio = U8_MAX;
module_param_named(pll_max_ratio, ivpu_pll_max_ratio, byte, 0644);
MODULE_PARM_DESC(pll_max_ratio, "Maximum PLL ratio used to set NPU frequency");

int ivpu_sched_mode = IVPU_SCHED_MODE_AUTO;
module_param_named(sched_mode, ivpu_sched_mode, int, 0444);
MODULE_PARM_DESC(sched_mode, "Scheduler mode: -1 - Use default scheduler, 0 - Use OS scheduler, 1 - Use HW scheduler");

bool ivpu_disable_mmu_cont_pages;
module_param_named(disable_mmu_cont_pages, ivpu_disable_mmu_cont_pages, bool, 0444);
MODULE_PARM_DESC(disable_mmu_cont_pages, "Disable MMU contiguous pages optimization");

bool ivpu_force_snoop;
module_param_named(force_snoop, ivpu_force_snoop, bool, 0444);
MODULE_PARM_DESC(force_snoop, "Force snooping for NPU host memory access");

struct ivpu_file_priv *ivpu_file_priv_get(struct ivpu_file_priv *file_priv)
{
	struct ivpu_device *vdev = file_priv->vdev;

	kref_get(&file_priv->ref);

	ivpu_dbg(vdev, KREF, "file_priv get: ctx %u refcount %u\n",
		 file_priv->ctx.id, kref_read(&file_priv->ref));

	return file_priv;
}

static void file_priv_unbind(struct ivpu_device *vdev, struct ivpu_file_priv *file_priv)
{
	mutex_lock(&file_priv->lock);
	if (file_priv->bound) {
		ivpu_dbg(vdev, FILE, "file_priv unbind: ctx %u\n", file_priv->ctx.id);

		ivpu_cmdq_release_all_locked(file_priv);
		ivpu_bo_unbind_all_bos_from_context(vdev, &file_priv->ctx);
		ivpu_mmu_context_fini(vdev, &file_priv->ctx);
		file_priv->bound = false;
		drm_WARN_ON(&vdev->drm, !xa_erase_irq(&vdev->context_xa, file_priv->ctx.id));
	}
	mutex_unlock(&file_priv->lock);
}

static void file_priv_release(struct kref *ref)
{
	struct ivpu_file_priv *file_priv = container_of(ref, struct ivpu_file_priv, ref);
	struct ivpu_device *vdev = file_priv->vdev;

	ivpu_dbg(vdev, FILE, "file_priv release: ctx %u bound %d\n",
		 file_priv->ctx.id, (bool)file_priv->bound);

	pm_runtime_get_sync(vdev->drm.dev);
	mutex_lock(&vdev->context_list_lock);
	file_priv_unbind(vdev, file_priv);
	drm_WARN_ON(&vdev->drm, !xa_empty(&file_priv->cmdq_xa));
	xa_destroy(&file_priv->cmdq_xa);
	mutex_unlock(&vdev->context_list_lock);
	pm_runtime_put_autosuspend(vdev->drm.dev);

	mutex_destroy(&file_priv->ms_lock);
	mutex_destroy(&file_priv->lock);
	kfree(file_priv);
}

void ivpu_file_priv_put(struct ivpu_file_priv **link)
{
	struct ivpu_file_priv *file_priv = *link;
	struct ivpu_device *vdev = file_priv->vdev;

	ivpu_dbg(vdev, KREF, "file_priv put: ctx %u refcount %u\n",
		 file_priv->ctx.id, kref_read(&file_priv->ref));

	*link = NULL;
	kref_put(&file_priv->ref, file_priv_release);
}

static int ivpu_get_capabilities(struct ivpu_device *vdev, struct drm_ivpu_param *args)
{
	switch (args->index) {
	case DRM_IVPU_CAP_METRIC_STREAMER:
		args->value = 1;
		break;
	case DRM_IVPU_CAP_DMA_MEMORY_RANGE:
		args->value = 1;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int ivpu_get_param_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct ivpu_file_priv *file_priv = file->driver_priv;
	struct ivpu_device *vdev = file_priv->vdev;
	struct pci_dev *pdev = to_pci_dev(vdev->drm.dev);
	struct drm_ivpu_param *args = data;
	int ret = 0;
	int idx;

	if (!drm_dev_enter(dev, &idx))
		return -ENODEV;

	switch (args->param) {
	case DRM_IVPU_PARAM_DEVICE_ID:
		args->value = pdev->device;
		break;
	case DRM_IVPU_PARAM_DEVICE_REVISION:
		args->value = pdev->revision;
		break;
	case DRM_IVPU_PARAM_PLATFORM_TYPE:
		args->value = vdev->platform;
		break;
	case DRM_IVPU_PARAM_CORE_CLOCK_RATE:
		args->value = ivpu_hw_ratio_to_freq(vdev, vdev->hw->pll.max_ratio);
		break;
	case DRM_IVPU_PARAM_NUM_CONTEXTS:
		args->value = ivpu_get_context_count(vdev);
		break;
	case DRM_IVPU_PARAM_CONTEXT_BASE_ADDRESS:
		args->value = vdev->hw->ranges.user.start;
		break;
	case DRM_IVPU_PARAM_CONTEXT_ID:
		args->value = file_priv->ctx.id;
		break;
	case DRM_IVPU_PARAM_FW_API_VERSION:
		if (args->index < VPU_FW_API_VER_NUM) {
			struct vpu_firmware_header *fw_hdr;

			fw_hdr = (struct vpu_firmware_header *)vdev->fw->file->data;
			args->value = fw_hdr->api_version[args->index];
		} else {
			ret = -EINVAL;
		}
		break;
	case DRM_IVPU_PARAM_ENGINE_HEARTBEAT:
		ret = ivpu_jsm_get_heartbeat(vdev, args->index, &args->value);
		break;
	case DRM_IVPU_PARAM_UNIQUE_INFERENCE_ID:
		args->value = (u64)atomic64_inc_return(&vdev->unique_id_counter);
		break;
	case DRM_IVPU_PARAM_TILE_CONFIG:
		args->value = vdev->hw->tile_fuse;
		break;
	case DRM_IVPU_PARAM_SKU:
		args->value = vdev->hw->sku;
		break;
	case DRM_IVPU_PARAM_CAPABILITIES:
		ret = ivpu_get_capabilities(vdev, args);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	drm_dev_exit(idx);
	return ret;
}

static int ivpu_set_param_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_ivpu_param *args = data;
	int ret = 0;

	switch (args->param) {
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int ivpu_open(struct drm_device *dev, struct drm_file *file)
{
	struct ivpu_device *vdev = to_ivpu_device(dev);
	struct ivpu_file_priv *file_priv;
	u32 ctx_id;
	int idx, ret;

	if (!drm_dev_enter(dev, &idx))
		return -ENODEV;

	file_priv = kzalloc(sizeof(*file_priv), GFP_KERNEL);
	if (!file_priv) {
		ret = -ENOMEM;
		goto err_dev_exit;
	}

	INIT_LIST_HEAD(&file_priv->ms_instance_list);

	file_priv->vdev = vdev;
	file_priv->bound = true;
	kref_init(&file_priv->ref);
	mutex_init(&file_priv->lock);
	mutex_init(&file_priv->ms_lock);

	mutex_lock(&vdev->context_list_lock);

	ret = xa_alloc_irq(&vdev->context_xa, &ctx_id, file_priv,
			   vdev->context_xa_limit, GFP_KERNEL);
	if (ret) {
		ivpu_err(vdev, "Failed to allocate context id: %d\n", ret);
		goto err_unlock;
	}

	ivpu_mmu_context_init(vdev, &file_priv->ctx, ctx_id);

	file_priv->job_limit.min = FIELD_PREP(IVPU_JOB_ID_CONTEXT_MASK, (file_priv->ctx.id - 1));
	file_priv->job_limit.max = file_priv->job_limit.min | IVPU_JOB_ID_JOB_MASK;

	xa_init_flags(&file_priv->cmdq_xa, XA_FLAGS_ALLOC1);
	file_priv->cmdq_limit.min = IVPU_CMDQ_MIN_ID;
	file_priv->cmdq_limit.max = IVPU_CMDQ_MAX_ID;

	mutex_unlock(&vdev->context_list_lock);
	drm_dev_exit(idx);

	file->driver_priv = file_priv;

	ivpu_dbg(vdev, FILE, "file_priv create: ctx %u process %s pid %d\n",
		 ctx_id, current->comm, task_pid_nr(current));

	return 0;

err_unlock:
	mutex_unlock(&vdev->context_list_lock);
	mutex_destroy(&file_priv->ms_lock);
	mutex_destroy(&file_priv->lock);
	kfree(file_priv);
err_dev_exit:
	drm_dev_exit(idx);
	return ret;
}

static void ivpu_postclose(struct drm_device *dev, struct drm_file *file)
{
	struct ivpu_file_priv *file_priv = file->driver_priv;
	struct ivpu_device *vdev = to_ivpu_device(dev);

	ivpu_dbg(vdev, FILE, "file_priv close: ctx %u process %s pid %d\n",
		 file_priv->ctx.id, current->comm, task_pid_nr(current));

	ivpu_ms_cleanup(file_priv);
	ivpu_file_priv_put(&file_priv);
}

static const struct drm_ioctl_desc ivpu_drm_ioctls[] = {
	DRM_IOCTL_DEF_DRV(IVPU_GET_PARAM, ivpu_get_param_ioctl, 0),
	DRM_IOCTL_DEF_DRV(IVPU_SET_PARAM, ivpu_set_param_ioctl, 0),
	DRM_IOCTL_DEF_DRV(IVPU_BO_CREATE, ivpu_bo_create_ioctl, 0),
	DRM_IOCTL_DEF_DRV(IVPU_BO_INFO, ivpu_bo_info_ioctl, 0),
	DRM_IOCTL_DEF_DRV(IVPU_SUBMIT, ivpu_submit_ioctl, 0),
	DRM_IOCTL_DEF_DRV(IVPU_BO_WAIT, ivpu_bo_wait_ioctl, 0),
	DRM_IOCTL_DEF_DRV(IVPU_METRIC_STREAMER_START, ivpu_ms_start_ioctl, 0),
	DRM_IOCTL_DEF_DRV(IVPU_METRIC_STREAMER_GET_DATA, ivpu_ms_get_data_ioctl, 0),
	DRM_IOCTL_DEF_DRV(IVPU_METRIC_STREAMER_STOP, ivpu_ms_stop_ioctl, 0),
	DRM_IOCTL_DEF_DRV(IVPU_METRIC_STREAMER_GET_INFO, ivpu_ms_get_info_ioctl, 0),
};

static int ivpu_wait_for_ready(struct ivpu_device *vdev)
{
	struct ivpu_ipc_consumer cons;
	struct ivpu_ipc_hdr ipc_hdr;
	unsigned long timeout;
	int ret;

	if (ivpu_test_mode & IVPU_TEST_MODE_FW_TEST)
		return 0;

	ivpu_ipc_consumer_add(vdev, &cons, IVPU_IPC_CHAN_BOOT_MSG, NULL);

	timeout = jiffies + msecs_to_jiffies(vdev->timeout.boot);
	while (1) {
		ivpu_ipc_irq_handler(vdev);
		ret = ivpu_ipc_receive(vdev, &cons, &ipc_hdr, NULL, 0);
		if (ret != -ETIMEDOUT || time_after_eq(jiffies, timeout))
			break;

		cond_resched();
	}

	ivpu_ipc_consumer_del(vdev, &cons);

	if (!ret && ipc_hdr.data_addr != IVPU_IPC_BOOT_MSG_DATA_ADDR) {
		ivpu_err(vdev, "Invalid NPU ready message: 0x%x\n",
			 ipc_hdr.data_addr);
		return -EIO;
	}

	if (!ret)
		ivpu_dbg(vdev, PM, "NPU ready message received successfully\n");

	return ret;
}

static int ivpu_hw_sched_init(struct ivpu_device *vdev)
{
	int ret = 0;

	if (vdev->fw->sched_mode == VPU_SCHEDULING_MODE_HW) {
		ret = ivpu_jsm_hws_setup_priority_bands(vdev);
		if (ret) {
			ivpu_err(vdev, "Failed to enable hw scheduler: %d", ret);
			return ret;
		}
	}

	return ret;
}

/**
 * ivpu_boot() - Start VPU firmware
 * @vdev: VPU device
 *
 * This function is paired with ivpu_shutdown() but it doesn't power up the
 * VPU because power up has to be called very early in ivpu_probe().
 */
int ivpu_boot(struct ivpu_device *vdev)
{
	int ret;

	/* Update boot params located at first 4KB of FW memory */
	ivpu_fw_boot_params_setup(vdev, ivpu_bo_vaddr(vdev->fw->mem));

	ret = ivpu_hw_boot_fw(vdev);
	if (ret) {
		ivpu_err(vdev, "Failed to start the firmware: %d\n", ret);
		return ret;
	}

	ret = ivpu_wait_for_ready(vdev);
	if (ret) {
		ivpu_err(vdev, "Failed to boot the firmware: %d\n", ret);
		goto err_diagnose_failure;
	}

	ivpu_hw_irq_clear(vdev);
	enable_irq(vdev->irq);
	ivpu_hw_irq_enable(vdev);
	ivpu_ipc_enable(vdev);

	if (ivpu_fw_is_cold_boot(vdev)) {
		ret = ivpu_pm_dct_init(vdev);
		if (ret)
			goto err_diagnose_failure;

		ret = ivpu_hw_sched_init(vdev);
		if (ret)
			goto err_diagnose_failure;
	}

	return 0;

err_diagnose_failure:
	ivpu_hw_diagnose_failure(vdev);
	ivpu_mmu_evtq_dump(vdev);
	ivpu_dev_coredump(vdev);
	return ret;
}

void ivpu_prepare_for_reset(struct ivpu_device *vdev)
{
	ivpu_hw_irq_disable(vdev);
	disable_irq(vdev->irq);
	ivpu_ipc_disable(vdev);
	ivpu_mmu_disable(vdev);
}

int ivpu_shutdown(struct ivpu_device *vdev)
{
	int ret;

	/* Save PCI state before powering down as it sometimes gets corrupted if NPU hangs */
	pci_save_state(to_pci_dev(vdev->drm.dev));

	ret = ivpu_hw_power_down(vdev);
	if (ret)
		ivpu_warn(vdev, "Failed to power down HW: %d\n", ret);

	pci_set_power_state(to_pci_dev(vdev->drm.dev), PCI_D3hot);

	return ret;
}

static const struct file_operations ivpu_fops = {
	.owner		= THIS_MODULE,
	DRM_ACCEL_FOPS,
};

static const struct drm_driver driver = {
	.driver_features = DRIVER_GEM | DRIVER_COMPUTE_ACCEL,

	.open = ivpu_open,
	.postclose = ivpu_postclose,

	.gem_create_object = ivpu_gem_create_object,
	.gem_prime_import_sg_table = drm_gem_shmem_prime_import_sg_table,

	.ioctls = ivpu_drm_ioctls,
	.num_ioctls = ARRAY_SIZE(ivpu_drm_ioctls),
	.fops = &ivpu_fops,

	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,

#ifdef DRIVER_DATE
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
#else
	.date = UTS_RELEASE,
	.major = 1,
#endif
};

static void ivpu_context_abort_invalid(struct ivpu_device *vdev)
{
	struct ivpu_file_priv *file_priv;
	unsigned long ctx_id;

	mutex_lock(&vdev->context_list_lock);

	xa_for_each(&vdev->context_xa, ctx_id, file_priv) {
		if (!file_priv->has_mmu_faults || file_priv->aborted)
			continue;

		mutex_lock(&file_priv->lock);
		ivpu_context_abort_locked(file_priv);
		file_priv->aborted = true;
		mutex_unlock(&file_priv->lock);
	}

	mutex_unlock(&vdev->context_list_lock);
}

static irqreturn_t ivpu_irq_thread_handler(int irq, void *arg)
{
	struct ivpu_device *vdev = arg;
	u8 irq_src;

	if (kfifo_is_empty(&vdev->hw->irq.fifo))
		return IRQ_NONE;

	while (kfifo_get(&vdev->hw->irq.fifo, &irq_src)) {
		switch (irq_src) {
		case IVPU_HW_IRQ_SRC_IPC:
			ivpu_ipc_irq_thread_handler(vdev);
			break;
		case IVPU_HW_IRQ_SRC_MMU_EVTQ:
			ivpu_context_abort_invalid(vdev);
			break;
		case IVPU_HW_IRQ_SRC_DCT:
			ivpu_pm_dct_irq_thread_handler(vdev);
			break;
		default:
			ivpu_err_ratelimited(vdev, "Unknown IRQ source: %u\n", irq_src);
			break;
		}
	}

	return IRQ_HANDLED;
}

static int ivpu_irq_init(struct ivpu_device *vdev)
{
	struct pci_dev *pdev = to_pci_dev(vdev->drm.dev);
	int ret;

	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSI | PCI_IRQ_MSIX);
	if (ret < 0) {
		ivpu_err(vdev, "Failed to allocate a MSI IRQ: %d\n", ret);
		return ret;
	}

	ivpu_irq_handlers_init(vdev);

	vdev->irq = pci_irq_vector(pdev, 0);

	ret = devm_request_threaded_irq(vdev->drm.dev, vdev->irq, ivpu_hw_irq_handler,
					ivpu_irq_thread_handler, IRQF_NO_AUTOEN, DRIVER_NAME, vdev);
	if (ret)
		ivpu_err(vdev, "Failed to request an IRQ %d\n", ret);

	return ret;
}

static int ivpu_pci_init(struct ivpu_device *vdev)
{
	struct pci_dev *pdev = to_pci_dev(vdev->drm.dev);
	struct resource *bar0 = &pdev->resource[0];
	struct resource *bar4 = &pdev->resource[4];
	int ret;

	ivpu_dbg(vdev, MISC, "Mapping BAR0 (RegV) %pR\n", bar0);
	vdev->regv = devm_ioremap_resource(vdev->drm.dev, bar0);
	if (IS_ERR(vdev->regv)) {
		ivpu_err(vdev, "Failed to map bar 0: %pe\n", vdev->regv);
		return PTR_ERR(vdev->regv);
	}

	ivpu_dbg(vdev, MISC, "Mapping BAR4 (RegB) %pR\n", bar4);
	vdev->regb = devm_ioremap_resource(vdev->drm.dev, bar4);
	if (IS_ERR(vdev->regb)) {
		ivpu_err(vdev, "Failed to map bar 4: %pe\n", vdev->regb);
		return PTR_ERR(vdev->regb);
	}

	ret = dma_set_mask_and_coherent(vdev->drm.dev, DMA_BIT_MASK(vdev->hw->dma_bits));
	if (ret) {
		ivpu_err(vdev, "Failed to set DMA mask: %d\n", ret);
		return ret;
	}
	dma_set_max_seg_size(vdev->drm.dev, UINT_MAX);

	/* Clear any pending errors */
	pcie_capability_clear_word(pdev, PCI_EXP_DEVSTA, 0x3f);

	/* NPU does not require 10m D3hot delay */
	pdev->d3hot_delay = 0;

	ret = pcim_enable_device(pdev);
	if (ret) {
		ivpu_err(vdev, "Failed to enable PCI device: %d\n", ret);
		return ret;
	}

	pci_set_master(pdev);

	return 0;
}

static int ivpu_dev_init(struct ivpu_device *vdev)
{
	int ret;

	vdev->hw = drmm_kzalloc(&vdev->drm, sizeof(*vdev->hw), GFP_KERNEL);
	if (!vdev->hw)
		return -ENOMEM;

	vdev->mmu = drmm_kzalloc(&vdev->drm, sizeof(*vdev->mmu), GFP_KERNEL);
	if (!vdev->mmu)
		return -ENOMEM;

	vdev->fw = drmm_kzalloc(&vdev->drm, sizeof(*vdev->fw), GFP_KERNEL);
	if (!vdev->fw)
		return -ENOMEM;

	vdev->ipc = drmm_kzalloc(&vdev->drm, sizeof(*vdev->ipc), GFP_KERNEL);
	if (!vdev->ipc)
		return -ENOMEM;

	vdev->pm = drmm_kzalloc(&vdev->drm, sizeof(*vdev->pm), GFP_KERNEL);
	if (!vdev->pm)
		return -ENOMEM;

	if (ivpu_hw_ip_gen(vdev) >= IVPU_HW_IP_40XX)
		vdev->hw->dma_bits = 48;
	else
		vdev->hw->dma_bits = 38;

	vdev->platform = IVPU_PLATFORM_INVALID;
	vdev->context_xa_limit.min = IVPU_USER_CONTEXT_MIN_SSID;
	vdev->context_xa_limit.max = IVPU_USER_CONTEXT_MAX_SSID;
	atomic64_set(&vdev->unique_id_counter, 0);
	xa_init_flags(&vdev->context_xa, XA_FLAGS_ALLOC | XA_FLAGS_LOCK_IRQ);
	xa_init_flags(&vdev->submitted_jobs_xa, XA_FLAGS_ALLOC1);
	xa_init_flags(&vdev->db_xa, XA_FLAGS_ALLOC1);
	lockdep_set_class(&vdev->submitted_jobs_xa.xa_lock, &submitted_jobs_xa_lock_class_key);
	INIT_LIST_HEAD(&vdev->bo_list);

	vdev->db_limit.min = IVPU_MIN_DB;
	vdev->db_limit.max = IVPU_MAX_DB;

	ret = drmm_mutex_init(&vdev->drm, &vdev->context_list_lock);
	if (ret)
		goto err_xa_destroy;

	ret = drmm_mutex_init(&vdev->drm, &vdev->bo_list_lock);
	if (ret)
		goto err_xa_destroy;

	ret = ivpu_pci_init(vdev);
	if (ret)
		goto err_xa_destroy;

	ret = ivpu_irq_init(vdev);
	if (ret)
		goto err_xa_destroy;

	/* Init basic HW info based on buttress registers which are accessible before power up */
	ret = ivpu_hw_init(vdev);
	if (ret)
		goto err_xa_destroy;

	/* Power up early so the rest of init code can access VPU registers */
	ret = ivpu_hw_power_up(vdev);
	if (ret)
		goto err_shutdown;

	ivpu_mmu_global_context_init(vdev);

	ret = ivpu_mmu_init(vdev);
	if (ret)
		goto err_mmu_gctx_fini;

	ret = ivpu_mmu_reserved_context_init(vdev);
	if (ret)
		goto err_mmu_gctx_fini;

	ret = ivpu_fw_init(vdev);
	if (ret)
		goto err_mmu_rctx_fini;

	ret = ivpu_ipc_init(vdev);
	if (ret)
		goto err_fw_fini;

	ivpu_pm_init(vdev);

	ret = ivpu_boot(vdev);
	if (ret)
		goto err_ipc_fini;

	ivpu_job_done_consumer_init(vdev);
	ivpu_pm_enable(vdev);

	return 0;

err_ipc_fini:
	ivpu_ipc_fini(vdev);
err_fw_fini:
	ivpu_fw_fini(vdev);
err_mmu_rctx_fini:
	ivpu_mmu_reserved_context_fini(vdev);
err_mmu_gctx_fini:
	ivpu_mmu_global_context_fini(vdev);
err_shutdown:
	ivpu_shutdown(vdev);
err_xa_destroy:
	xa_destroy(&vdev->db_xa);
	xa_destroy(&vdev->submitted_jobs_xa);
	xa_destroy(&vdev->context_xa);
	return ret;
}

static void ivpu_bo_unbind_all_user_contexts(struct ivpu_device *vdev)
{
	struct ivpu_file_priv *file_priv;
	unsigned long ctx_id;

	mutex_lock(&vdev->context_list_lock);

	xa_for_each(&vdev->context_xa, ctx_id, file_priv)
		file_priv_unbind(vdev, file_priv);

	mutex_unlock(&vdev->context_list_lock);
}

static void ivpu_dev_fini(struct ivpu_device *vdev)
{
	ivpu_jobs_abort_all(vdev);
	ivpu_pm_cancel_recovery(vdev);
	ivpu_pm_disable(vdev);
	ivpu_prepare_for_reset(vdev);
	ivpu_shutdown(vdev);

	ivpu_ms_cleanup_all(vdev);
	ivpu_job_done_consumer_fini(vdev);
	ivpu_bo_unbind_all_user_contexts(vdev);

	ivpu_ipc_fini(vdev);
	ivpu_fw_fini(vdev);
	ivpu_mmu_reserved_context_fini(vdev);
	ivpu_mmu_global_context_fini(vdev);

	drm_WARN_ON(&vdev->drm, !xa_empty(&vdev->db_xa));
	xa_destroy(&vdev->db_xa);
	drm_WARN_ON(&vdev->drm, !xa_empty(&vdev->submitted_jobs_xa));
	xa_destroy(&vdev->submitted_jobs_xa);
	drm_WARN_ON(&vdev->drm, !xa_empty(&vdev->context_xa));
	xa_destroy(&vdev->context_xa);
}

static struct pci_device_id ivpu_pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_MTL) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_ARL) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_LNL) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_PTL_P) },
	{ }
};
MODULE_DEVICE_TABLE(pci, ivpu_pci_ids);

static int ivpu_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct ivpu_device *vdev;
	int ret;

	vdev = devm_drm_dev_alloc(&pdev->dev, &driver, struct ivpu_device, drm);
	if (IS_ERR(vdev))
		return PTR_ERR(vdev);

	pci_set_drvdata(pdev, vdev);

	ret = ivpu_dev_init(vdev);
	if (ret)
		return ret;

	ivpu_debugfs_init(vdev);
	ivpu_sysfs_init(vdev);

	ret = drm_dev_register(&vdev->drm, 0);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register DRM device: %d\n", ret);
		ivpu_dev_fini(vdev);
	}

	return ret;
}

static void ivpu_remove(struct pci_dev *pdev)
{
	struct ivpu_device *vdev = pci_get_drvdata(pdev);

	drm_dev_unplug(&vdev->drm);
	ivpu_dev_fini(vdev);
}

static const struct dev_pm_ops ivpu_drv_pci_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(ivpu_pm_suspend_cb, ivpu_pm_resume_cb)
	SET_RUNTIME_PM_OPS(ivpu_pm_runtime_suspend_cb, ivpu_pm_runtime_resume_cb, NULL)
};

static const struct pci_error_handlers ivpu_drv_pci_err = {
	.reset_prepare = ivpu_pm_reset_prepare_cb,
	.reset_done = ivpu_pm_reset_done_cb,
};

static struct pci_driver ivpu_pci_driver = {
	.name = KBUILD_MODNAME,
	.id_table = ivpu_pci_ids,
	.probe = ivpu_probe,
	.remove = ivpu_remove,
	.driver = {
		.pm = &ivpu_drv_pci_pm,
	},
	.err_handler = &ivpu_drv_pci_err,
};

module_pci_driver(ivpu_pci_driver);

MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL and additional rights");
MODULE_VERSION(DRIVER_VERSION_STR);
