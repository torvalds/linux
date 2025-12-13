// SPDX-License-Identifier: GPL-2.0-only OR MIT
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#include "pvr_device.h"
#include "pvr_device_info.h"

#include "pvr_fw.h"
#include "pvr_params.h"
#include "pvr_power.h"
#include "pvr_queue.h"
#include "pvr_rogue_cr_defs.h"
#include "pvr_stream.h"
#include "pvr_vm.h"

#include <drm/drm_print.h>

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/compiler_attributes.h>
#include <linux/compiler_types.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/firmware.h>
#include <linux/gfp.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/stddef.h>
#include <linux/types.h>
#include <linux/workqueue.h>

/* Major number for the supported version of the firmware. */
#define PVR_FW_VERSION_MAJOR 1

/**
 * pvr_device_reg_init() - Initialize kernel access to a PowerVR device's
 * control registers.
 * @pvr_dev: Target PowerVR device.
 *
 * Sets struct pvr_device->regs.
 *
 * This method of mapping the device control registers into memory ensures that
 * they are unmapped when the driver is detached (i.e. no explicit cleanup is
 * required).
 *
 * Return:
 *  * 0 on success, or
 *  * Any error returned by devm_platform_ioremap_resource().
 */
static int
pvr_device_reg_init(struct pvr_device *pvr_dev)
{
	struct drm_device *drm_dev = from_pvr_device(pvr_dev);
	struct platform_device *plat_dev = to_platform_device(drm_dev->dev);
	struct resource *regs_resource;
	void __iomem *regs;

	pvr_dev->regs_resource = NULL;
	pvr_dev->regs = NULL;

	regs = devm_platform_get_and_ioremap_resource(plat_dev, 0, &regs_resource);
	if (IS_ERR(regs))
		return dev_err_probe(drm_dev->dev, PTR_ERR(regs),
				     "failed to ioremap gpu registers\n");

	pvr_dev->regs = regs;
	pvr_dev->regs_resource = regs_resource;

	return 0;
}

/**
 * pvr_device_clk_init() - Initialize clocks required by a PowerVR device
 * @pvr_dev: Target PowerVR device.
 *
 * Sets struct pvr_device->core_clk, struct pvr_device->sys_clk and
 * struct pvr_device->mem_clk.
 *
 * Three clocks are required by the PowerVR device: core, sys and mem. On
 * return, this function guarantees that the clocks are in one of the following
 * states:
 *
 *  * All successfully initialized,
 *  * Core errored, sys and mem uninitialized,
 *  * Core deinitialized, sys errored, mem uninitialized, or
 *  * Core and sys deinitialized, mem errored.
 *
 * Return:
 *  * 0 on success,
 *  * Any error returned by devm_clk_get(), or
 *  * Any error returned by devm_clk_get_optional().
 */
static int pvr_device_clk_init(struct pvr_device *pvr_dev)
{
	struct drm_device *drm_dev = from_pvr_device(pvr_dev);
	struct clk *core_clk;
	struct clk *sys_clk;
	struct clk *mem_clk;

	core_clk = devm_clk_get(drm_dev->dev, "core");
	if (IS_ERR(core_clk))
		return dev_err_probe(drm_dev->dev, PTR_ERR(core_clk),
				     "failed to get core clock\n");

	sys_clk = devm_clk_get_optional(drm_dev->dev, "sys");
	if (IS_ERR(sys_clk))
		return dev_err_probe(drm_dev->dev, PTR_ERR(sys_clk),
				     "failed to get sys clock\n");

	mem_clk = devm_clk_get_optional(drm_dev->dev, "mem");
	if (IS_ERR(mem_clk))
		return dev_err_probe(drm_dev->dev, PTR_ERR(mem_clk),
				     "failed to get mem clock\n");

	pvr_dev->core_clk = core_clk;
	pvr_dev->sys_clk = sys_clk;
	pvr_dev->mem_clk = mem_clk;

	return 0;
}

/**
 * pvr_device_process_active_queues() - Process all queue related events.
 * @pvr_dev: PowerVR device to check
 *
 * This is called any time we receive a FW event. It iterates over all
 * active queues and calls pvr_queue_process() on them.
 */
static void pvr_device_process_active_queues(struct pvr_device *pvr_dev)
{
	struct pvr_queue *queue, *tmp_queue;
	LIST_HEAD(active_queues);

	mutex_lock(&pvr_dev->queues.lock);

	/* Move all active queues to a temporary list. Queues that remain
	 * active after we're done processing them are re-inserted to
	 * the queues.active list by pvr_queue_process().
	 */
	list_splice_init(&pvr_dev->queues.active, &active_queues);

	list_for_each_entry_safe(queue, tmp_queue, &active_queues, node)
		pvr_queue_process(queue);

	mutex_unlock(&pvr_dev->queues.lock);
}

static bool pvr_device_safety_irq_pending(struct pvr_device *pvr_dev)
{
	u32 events;

	WARN_ON_ONCE(!pvr_dev->has_safety_events);

	events = pvr_cr_read32(pvr_dev, ROGUE_CR_EVENT_STATUS);

	return (events & ROGUE_CR_EVENT_STATUS_SAFETY_EN) != 0;
}

static void pvr_device_safety_irq_clear(struct pvr_device *pvr_dev)
{
	WARN_ON_ONCE(!pvr_dev->has_safety_events);

	pvr_cr_write32(pvr_dev, ROGUE_CR_EVENT_CLEAR,
		       ROGUE_CR_EVENT_CLEAR_SAFETY_EN);
}

static void pvr_device_handle_safety_events(struct pvr_device *pvr_dev)
{
	struct drm_device *drm_dev = from_pvr_device(pvr_dev);
	u32 events;

	WARN_ON_ONCE(!pvr_dev->has_safety_events);

	events = pvr_cr_read32(pvr_dev, ROGUE_CR_SAFETY_EVENT_STATUS__ROGUEXE);

	/* Handle only these events on the host and leave the rest to the FW. */
	events &= ROGUE_CR_SAFETY_EVENT_STATUS__ROGUEXE__FAULT_FW_EN |
		ROGUE_CR_SAFETY_EVENT_STATUS__ROGUEXE__WATCHDOG_TIMEOUT_EN;

	pvr_cr_write32(pvr_dev, ROGUE_CR_SAFETY_EVENT_CLEAR__ROGUEXE, events);

	if (events & ROGUE_CR_SAFETY_EVENT_STATUS__ROGUEXE__FAULT_FW_EN) {
		u32 fault_fw = pvr_cr_read32(pvr_dev, ROGUE_CR_FAULT_FW_STATUS);

		pvr_cr_write32(pvr_dev, ROGUE_CR_FAULT_FW_CLEAR, fault_fw);

		drm_info(drm_dev, "Safety event: FW fault (mask=0x%08x)\n", fault_fw);
	}

	if (events & ROGUE_CR_SAFETY_EVENT_STATUS__ROGUEXE__WATCHDOG_TIMEOUT_EN) {
		/*
		 * The watchdog timer is disabled by the driver so this event
		 * should never be fired.
		 */
		drm_info(drm_dev, "Safety event: Watchdog timeout\n");
	}
}

static irqreturn_t pvr_device_irq_thread_handler(int irq, void *data)
{
	struct pvr_device *pvr_dev = data;
	struct drm_device *drm_dev = from_pvr_device(pvr_dev);
	irqreturn_t ret = IRQ_NONE;

	/* We are in the threaded handler, we can keep dequeuing events until we
	 * don't see any. This should allow us to reduce the number of interrupts
	 * when the GPU is receiving a massive amount of short jobs.
	 */
	while (pvr_fw_irq_pending(pvr_dev)) {
		pvr_fw_irq_clear(pvr_dev);

		if (pvr_dev->fw_dev.booted) {
			pvr_fwccb_process(pvr_dev);
			pvr_kccb_wake_up_waiters(pvr_dev);
			pvr_device_process_active_queues(pvr_dev);
		}

		pm_runtime_mark_last_busy(drm_dev->dev);

		ret = IRQ_HANDLED;
	}

	if (pvr_dev->has_safety_events) {
		int err;

		/*
		 * Ensure the GPU is powered on since some safety events (such
		 * as ECC faults) can happen outside of job submissions, which
		 * are otherwise the only time a power reference is held.
		 */
		err = pvr_power_get(pvr_dev);
		if (err) {
			drm_err_ratelimited(drm_dev,
					    "%s: could not take power reference (%d)\n",
					    __func__, err);
			return ret;
		}

		while (pvr_device_safety_irq_pending(pvr_dev)) {
			pvr_device_safety_irq_clear(pvr_dev);
			pvr_device_handle_safety_events(pvr_dev);

			ret = IRQ_HANDLED;
		}

		pvr_power_put(pvr_dev);
	}

	return ret;
}

static irqreturn_t pvr_device_irq_handler(int irq, void *data)
{
	struct pvr_device *pvr_dev = data;
	bool safety_irq_pending = false;

	if (pvr_dev->has_safety_events)
		safety_irq_pending = pvr_device_safety_irq_pending(pvr_dev);

	if (!pvr_fw_irq_pending(pvr_dev) && !safety_irq_pending)
		return IRQ_NONE; /* Spurious IRQ - ignore. */

	return IRQ_WAKE_THREAD;
}

static void pvr_device_safety_irq_init(struct pvr_device *pvr_dev)
{
	u32 num_ecc_rams = 0;

	/*
	 * Safety events are an optional feature of the RogueXE platform. They
	 * are only enabled if at least one of ECC memory or the watchdog timer
	 * are present in HW. While safety events can be generated by other
	 * systems, that will never happen if the above mentioned hardware is
	 * not present.
	 */
	if (!PVR_HAS_FEATURE(pvr_dev, roguexe)) {
		pvr_dev->has_safety_events = false;
		return;
	}

	PVR_FEATURE_VALUE(pvr_dev, ecc_rams, &num_ecc_rams);

	pvr_dev->has_safety_events =
		num_ecc_rams > 0 || PVR_HAS_FEATURE(pvr_dev, watchdog_timer);
}

/**
 * pvr_device_irq_init() - Initialise IRQ required by a PowerVR device
 * @pvr_dev: Target PowerVR device.
 *
 * Returns:
 *  * 0 on success,
 *  * Any error returned by platform_get_irq_byname(), or
 *  * Any error returned by request_irq().
 */
static int
pvr_device_irq_init(struct pvr_device *pvr_dev)
{
	struct drm_device *drm_dev = from_pvr_device(pvr_dev);
	struct platform_device *plat_dev = to_platform_device(drm_dev->dev);

	init_waitqueue_head(&pvr_dev->kccb.rtn_q);

	pvr_device_safety_irq_init(pvr_dev);

	pvr_dev->irq = platform_get_irq(plat_dev, 0);
	if (pvr_dev->irq < 0)
		return pvr_dev->irq;

	/* Clear any pending events before requesting the IRQ line. */
	pvr_fw_irq_clear(pvr_dev);

	if (pvr_dev->has_safety_events)
		pvr_device_safety_irq_clear(pvr_dev);

	/*
	 * The ONESHOT flag ensures IRQs are masked while the thread handler is
	 * running.
	 */
	return request_threaded_irq(pvr_dev->irq, pvr_device_irq_handler,
				    pvr_device_irq_thread_handler,
				    IRQF_SHARED | IRQF_ONESHOT, "gpu", pvr_dev);
}

/**
 * pvr_device_irq_fini() - Deinitialise IRQ required by a PowerVR device
 * @pvr_dev: Target PowerVR device.
 */
static void
pvr_device_irq_fini(struct pvr_device *pvr_dev)
{
	free_irq(pvr_dev->irq, pvr_dev);
}

/**
 * pvr_build_firmware_filename() - Construct a PowerVR firmware filename
 * @pvr_dev: Target PowerVR device.
 * @base: First part of the filename.
 * @major: Major version number.
 *
 * A PowerVR firmware filename consists of three parts separated by underscores
 * (``'_'``) along with a '.fw' file suffix. The first part is the exact value
 * of @base, the second part is the hardware version string derived from @pvr_fw
 * and the final part is the firmware version number constructed from @major with
 * a 'v' prefix, e.g. powervr/rogue_4.40.2.51_v1.fw.
 *
 * The returned string will have been slab allocated and must be freed with
 * kfree().
 *
 * Return:
 *  * The constructed filename on success, or
 *  * Any error returned by kasprintf().
 */
static char *
pvr_build_firmware_filename(struct pvr_device *pvr_dev, const char *base,
			    u8 major)
{
	struct pvr_gpu_id *gpu_id = &pvr_dev->gpu_id;

	return kasprintf(GFP_KERNEL, "%s_%d.%d.%d.%d_v%d.fw", base, gpu_id->b,
			 gpu_id->v, gpu_id->n, gpu_id->c, major);
}

static void
pvr_release_firmware(void *data)
{
	struct pvr_device *pvr_dev = data;

	release_firmware(pvr_dev->fw_dev.firmware);
}

/**
 * pvr_request_firmware() - Load firmware for a PowerVR device
 * @pvr_dev: Target PowerVR device.
 *
 * See pvr_build_firmware_filename() for details on firmware file naming.
 *
 * Return:
 *  * 0 on success,
 *  * Any error returned by pvr_build_firmware_filename(), or
 *  * Any error returned by request_firmware().
 */
static int
pvr_request_firmware(struct pvr_device *pvr_dev)
{
	struct drm_device *drm_dev = &pvr_dev->base;
	char *filename;
	const struct firmware *fw;
	int err;

	filename = pvr_build_firmware_filename(pvr_dev, "powervr/rogue",
					       PVR_FW_VERSION_MAJOR);
	if (!filename)
		return -ENOMEM;

	/*
	 * This function takes a copy of &filename, meaning we can free our
	 * instance before returning.
	 */
	err = request_firmware(&fw, filename, pvr_dev->base.dev);
	if (err) {
		drm_err(drm_dev, "failed to load firmware %s (err=%d)\n",
			filename, err);
		goto err_free_filename;
	}

	drm_info(drm_dev, "loaded firmware %s\n", filename);
	kfree(filename);

	pvr_dev->fw_dev.firmware = fw;

	return devm_add_action_or_reset(drm_dev->dev, pvr_release_firmware, pvr_dev);

err_free_filename:
	kfree(filename);

	return err;
}

/**
 * pvr_load_gpu_id() - Load a PowerVR device's GPU ID (BVNC) from control registers.
 *
 * Sets struct pvr_dev.gpu_id.
 *
 * @pvr_dev: Target PowerVR device.
 */
static void
pvr_load_gpu_id(struct pvr_device *pvr_dev)
{
	struct pvr_gpu_id *gpu_id = &pvr_dev->gpu_id;
	u64 bvnc;

	/*
	 * Try reading the BVNC using the newer (cleaner) method first. If the
	 * B value is zero, fall back to the older method.
	 */
	bvnc = pvr_cr_read64(pvr_dev, ROGUE_CR_CORE_ID__PBVNC);

	gpu_id->b = PVR_CR_FIELD_GET(bvnc, CORE_ID__PBVNC__BRANCH_ID);
	if (gpu_id->b != 0) {
		gpu_id->v = PVR_CR_FIELD_GET(bvnc, CORE_ID__PBVNC__VERSION_ID);
		gpu_id->n = PVR_CR_FIELD_GET(bvnc, CORE_ID__PBVNC__NUMBER_OF_SCALABLE_UNITS);
		gpu_id->c = PVR_CR_FIELD_GET(bvnc, CORE_ID__PBVNC__CONFIG_ID);
	} else {
		u32 core_rev = pvr_cr_read32(pvr_dev, ROGUE_CR_CORE_REVISION);
		u32 core_id = pvr_cr_read32(pvr_dev, ROGUE_CR_CORE_ID);
		u16 core_id_config = PVR_CR_FIELD_GET(core_id, CORE_ID_CONFIG);

		gpu_id->b = PVR_CR_FIELD_GET(core_rev, CORE_REVISION_MAJOR);
		gpu_id->v = PVR_CR_FIELD_GET(core_rev, CORE_REVISION_MINOR);
		gpu_id->n = FIELD_GET(0xFF00, core_id_config);
		gpu_id->c = FIELD_GET(0x00FF, core_id_config);
	}
}

/**
 * pvr_set_dma_info() - Set PowerVR device DMA information
 * @pvr_dev: Target PowerVR device.
 *
 * Sets the DMA mask and max segment size for the PowerVR device.
 *
 * Return:
 *  * 0 on success,
 *  * Any error returned by PVR_FEATURE_VALUE(), or
 *  * Any error returned by dma_set_mask().
 */

static int
pvr_set_dma_info(struct pvr_device *pvr_dev)
{
	struct drm_device *drm_dev = from_pvr_device(pvr_dev);
	u16 phys_bus_width;
	int err;

	err = PVR_FEATURE_VALUE(pvr_dev, phys_bus_width, &phys_bus_width);
	if (err) {
		drm_err(drm_dev, "Failed to get device physical bus width\n");
		return err;
	}

	err = dma_set_mask(drm_dev->dev, DMA_BIT_MASK(phys_bus_width));
	if (err) {
		drm_err(drm_dev, "Failed to set DMA mask (err=%d)\n", err);
		return err;
	}

	dma_set_max_seg_size(drm_dev->dev, UINT_MAX);

	return 0;
}

/**
 * pvr_device_gpu_init() - GPU-specific initialization for a PowerVR device
 * @pvr_dev: Target PowerVR device.
 *
 * The following steps are taken to ensure the device is ready:
 *
 *  1. Read the hardware version information from control registers,
 *  2. Initialise the hardware feature information,
 *  3. Setup the device DMA information,
 *  4. Setup the device-scoped memory context, and
 *  5. Load firmware into the device.
 *
 * Return:
 *  * 0 on success,
 *  * -%ENODEV if the GPU is not supported,
 *  * Any error returned by pvr_set_dma_info(),
 *  * Any error returned by pvr_memory_context_init(), or
 *  * Any error returned by pvr_request_firmware().
 */
static int
pvr_device_gpu_init(struct pvr_device *pvr_dev)
{
	int err;

	pvr_load_gpu_id(pvr_dev);

	err = pvr_request_firmware(pvr_dev);
	if (err)
		return err;

	err = pvr_fw_validate_init_device_info(pvr_dev);
	if (err)
		return err;

	if (PVR_HAS_FEATURE(pvr_dev, meta))
		pvr_dev->fw_dev.processor_type = PVR_FW_PROCESSOR_TYPE_META;
	else if (PVR_HAS_FEATURE(pvr_dev, mips))
		pvr_dev->fw_dev.processor_type = PVR_FW_PROCESSOR_TYPE_MIPS;
	else if (PVR_HAS_FEATURE(pvr_dev, riscv_fw_processor))
		pvr_dev->fw_dev.processor_type = PVR_FW_PROCESSOR_TYPE_RISCV;
	else
		return -EINVAL;

	pvr_stream_create_musthave_masks(pvr_dev);

	err = pvr_set_dma_info(pvr_dev);
	if (err)
		return err;

	if (pvr_dev->fw_dev.processor_type != PVR_FW_PROCESSOR_TYPE_MIPS) {
		pvr_dev->kernel_vm_ctx = pvr_vm_create_context(pvr_dev, false);
		if (IS_ERR(pvr_dev->kernel_vm_ctx))
			return PTR_ERR(pvr_dev->kernel_vm_ctx);
	}

	err = pvr_fw_init(pvr_dev);
	if (err)
		goto err_vm_ctx_put;

	return 0;

err_vm_ctx_put:
	if (pvr_dev->fw_dev.processor_type != PVR_FW_PROCESSOR_TYPE_MIPS) {
		pvr_vm_context_put(pvr_dev->kernel_vm_ctx);
		pvr_dev->kernel_vm_ctx = NULL;
	}

	return err;
}

/**
 * pvr_device_gpu_fini() - GPU-specific deinitialization for a PowerVR device
 * @pvr_dev: Target PowerVR device.
 */
static void
pvr_device_gpu_fini(struct pvr_device *pvr_dev)
{
	pvr_fw_fini(pvr_dev);

	if (pvr_dev->fw_dev.processor_type != PVR_FW_PROCESSOR_TYPE_MIPS) {
		WARN_ON(!pvr_vm_context_put(pvr_dev->kernel_vm_ctx));
		pvr_dev->kernel_vm_ctx = NULL;
	}
}

/**
 * pvr_device_init() - Initialize a PowerVR device
 * @pvr_dev: Target PowerVR device.
 *
 * If this function returns successfully, the device will have been fully
 * initialized. Otherwise, any parts of the device initialized before an error
 * occurs will be de-initialized before returning.
 *
 * NOTE: The initialization steps currently taken are the bare minimum required
 *       to read from the control registers. The device is unlikely to function
 *       until further initialization steps are added. [This note should be
 *       removed when that happens.]
 *
 * Return:
 *  * 0 on success,
 *  * Any error returned by pvr_device_reg_init(),
 *  * Any error returned by pvr_device_clk_init(), or
 *  * Any error returned by pvr_device_gpu_init().
 */
int
pvr_device_init(struct pvr_device *pvr_dev)
{
	struct drm_device *drm_dev = from_pvr_device(pvr_dev);
	struct device *dev = drm_dev->dev;
	int err;

	/* Get the platform-specific data based on the compatible string. */
	pvr_dev->device_data = of_device_get_match_data(dev);

	/*
	 * Setup device parameters. We do this first in case other steps
	 * depend on them.
	 */
	err = pvr_device_params_init(&pvr_dev->params);
	if (err)
		return err;

	/* Enable and initialize clocks required for the device to operate. */
	err = pvr_device_clk_init(pvr_dev);
	if (err)
		return err;

	err = pvr_dev->device_data->pwr_ops->init(pvr_dev);
	if (err)
		return err;

	/* Explicitly power the GPU so we can access control registers before the FW is booted. */
	err = pm_runtime_resume_and_get(dev);
	if (err)
		return err;

	/* Map the control registers into memory. */
	err = pvr_device_reg_init(pvr_dev);
	if (err)
		goto err_pm_runtime_put;

	/* Perform GPU-specific initialization steps. */
	err = pvr_device_gpu_init(pvr_dev);
	if (err)
		goto err_pm_runtime_put;

	err = pvr_device_irq_init(pvr_dev);
	if (err)
		goto err_device_gpu_fini;

	pm_runtime_put(dev);

	return 0;

err_device_gpu_fini:
	pvr_device_gpu_fini(pvr_dev);

err_pm_runtime_put:
	pm_runtime_put_sync_suspend(dev);

	return err;
}

/**
 * pvr_device_fini() - Deinitialize a PowerVR device
 * @pvr_dev: Target PowerVR device.
 */
void
pvr_device_fini(struct pvr_device *pvr_dev)
{
	/*
	 * Deinitialization stages are performed in reverse order compared to
	 * the initialization stages in pvr_device_init().
	 */
	pvr_device_irq_fini(pvr_dev);
	pvr_device_gpu_fini(pvr_dev);
}

bool
pvr_device_has_uapi_quirk(struct pvr_device *pvr_dev, u32 quirk)
{
	switch (quirk) {
	case 47217:
		return PVR_HAS_QUIRK(pvr_dev, 47217);
	case 48545:
		return PVR_HAS_QUIRK(pvr_dev, 48545);
	case 49927:
		return PVR_HAS_QUIRK(pvr_dev, 49927);
	case 51764:
		return PVR_HAS_QUIRK(pvr_dev, 51764);
	case 62269:
		return PVR_HAS_QUIRK(pvr_dev, 62269);
	default:
		return false;
	};
}

bool
pvr_device_has_uapi_enhancement(struct pvr_device *pvr_dev, u32 enhancement)
{
	switch (enhancement) {
	case 35421:
		return PVR_HAS_ENHANCEMENT(pvr_dev, 35421);
	case 42064:
		return PVR_HAS_ENHANCEMENT(pvr_dev, 42064);
	default:
		return false;
	};
}

/**
 * pvr_device_has_feature() - Look up device feature based on feature definition
 * @pvr_dev: Device pointer.
 * @feature: Feature to look up. Should be one of %PVR_FEATURE_*.
 *
 * Returns:
 *  * %true if feature is present on device, or
 *  * %false if feature is not present on device.
 */
bool
pvr_device_has_feature(struct pvr_device *pvr_dev, u32 feature)
{
	switch (feature) {
	case PVR_FEATURE_CLUSTER_GROUPING:
		return PVR_HAS_FEATURE(pvr_dev, cluster_grouping);

	case PVR_FEATURE_COMPUTE_MORTON_CAPABLE:
		return PVR_HAS_FEATURE(pvr_dev, compute_morton_capable);

	case PVR_FEATURE_FB_CDC_V4:
		return PVR_HAS_FEATURE(pvr_dev, fb_cdc_v4);

	case PVR_FEATURE_GPU_MULTICORE_SUPPORT:
		return PVR_HAS_FEATURE(pvr_dev, gpu_multicore_support);

	case PVR_FEATURE_ISP_ZLS_D24_S8_PACKING_OGL_MODE:
		return PVR_HAS_FEATURE(pvr_dev, isp_zls_d24_s8_packing_ogl_mode);

	case PVR_FEATURE_S7_TOP_INFRASTRUCTURE:
		return PVR_HAS_FEATURE(pvr_dev, s7_top_infrastructure);

	case PVR_FEATURE_TESSELLATION:
		return PVR_HAS_FEATURE(pvr_dev, tessellation);

	case PVR_FEATURE_TPU_DM_GLOBAL_REGISTERS:
		return PVR_HAS_FEATURE(pvr_dev, tpu_dm_global_registers);

	case PVR_FEATURE_VDM_DRAWINDIRECT:
		return PVR_HAS_FEATURE(pvr_dev, vdm_drawindirect);

	case PVR_FEATURE_VDM_OBJECT_LEVEL_LLS:
		return PVR_HAS_FEATURE(pvr_dev, vdm_object_level_lls);

	case PVR_FEATURE_ZLS_SUBTILE:
		return PVR_HAS_FEATURE(pvr_dev, zls_subtile);

	/* Derived features. */
	case PVR_FEATURE_CDM_USER_MODE_QUEUE: {
		u8 cdm_control_stream_format = 0;

		PVR_FEATURE_VALUE(pvr_dev, cdm_control_stream_format, &cdm_control_stream_format);
		return (cdm_control_stream_format >= 2 && cdm_control_stream_format <= 4);
	}

	case PVR_FEATURE_REQUIRES_FB_CDC_ZLS_SETUP:
		if (PVR_HAS_FEATURE(pvr_dev, fbcdc_algorithm)) {
			u8 fbcdc_algorithm = 0;

			PVR_FEATURE_VALUE(pvr_dev, fbcdc_algorithm, &fbcdc_algorithm);
			return (fbcdc_algorithm < 3 || PVR_HAS_FEATURE(pvr_dev, fb_cdc_v4));
		}
		return false;

	default:
		WARN(true, "Looking up undefined feature %u\n", feature);
		return false;
	}
}
