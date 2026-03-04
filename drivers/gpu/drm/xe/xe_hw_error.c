// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <linux/fault-inject.h>

#include "regs/xe_gsc_regs.h"
#include "regs/xe_hw_error_regs.h"
#include "regs/xe_irq_regs.h"

#include "xe_device.h"
#include "xe_drm_ras.h"
#include "xe_hw_error.h"
#include "xe_mmio.h"
#include "xe_survivability_mode.h"

#define  HEC_UNCORR_FW_ERR_BITS 4

extern struct fault_attr inject_csc_hw_error;

static const char * const error_severity[] = DRM_XE_RAS_ERROR_SEVERITY_NAMES;

static const char * const hec_uncorrected_fw_errors[] = {
	"Fatal",
	"CSE Disabled",
	"FD Corruption",
	"Data Corruption"
};

static bool fault_inject_csc_hw_error(void)
{
	return IS_ENABLED(CONFIG_DEBUG_FS) && should_fail(&inject_csc_hw_error, 1);
}

static enum drm_xe_ras_error_severity hw_err_to_severity(const enum hardware_error hw_err)
{
	if (hw_err == HARDWARE_ERROR_CORRECTABLE)
		return DRM_XE_RAS_ERR_SEV_CORRECTABLE;

	/* Uncorrectable errors comprise of both fatal and non-fatal errors */
	return DRM_XE_RAS_ERR_SEV_UNCORRECTABLE;
}

static void csc_hw_error_work(struct work_struct *work)
{
	struct xe_tile *tile = container_of(work, typeof(*tile), csc_hw_error_work);
	struct xe_device *xe = tile_to_xe(tile);
	int ret;

	ret = xe_survivability_mode_runtime_enable(xe);
	if (ret)
		drm_err(&xe->drm, "Failed to enable runtime survivability mode\n");
}

static void csc_hw_error_handler(struct xe_tile *tile, const enum hardware_error hw_err)
{
	const enum drm_xe_ras_error_severity severity = hw_err_to_severity(hw_err);
	const char *severity_str = error_severity[severity];
	struct xe_device *xe = tile_to_xe(tile);
	struct xe_mmio *mmio = &tile->mmio;
	u32 base, err_bit, err_src;
	unsigned long fw_err;

	if (xe->info.platform != XE_BATTLEMAGE)
		return;

	base = BMG_GSC_HECI1_BASE;
	lockdep_assert_held(&xe->irq.lock);
	err_src = xe_mmio_read32(mmio, HEC_UNCORR_ERR_STATUS(base));
	if (!err_src) {
		drm_err_ratelimited(&xe->drm, HW_ERR "Tile%d reported %s HEC_ERR_STATUS register blank\n",
				    tile->id, severity_str);
		return;
	}

	if (err_src & UNCORR_FW_REPORTED_ERR) {
		fw_err = xe_mmio_read32(mmio, HEC_UNCORR_FW_ERR_DW0(base));
		for_each_set_bit(err_bit, &fw_err, HEC_UNCORR_FW_ERR_BITS) {
			drm_err_ratelimited(&xe->drm, HW_ERR
					    "HEC FW %s %s reported, bit[%d] is set\n",
					     hec_uncorrected_fw_errors[err_bit], severity_str,
					     err_bit);

			schedule_work(&tile->csc_hw_error_work);
		}
	}

	xe_mmio_write32(mmio, HEC_UNCORR_ERR_STATUS(base), err_src);
}

static void hw_error_source_handler(struct xe_tile *tile, const enum hardware_error hw_err)
{
	const enum drm_xe_ras_error_severity severity = hw_err_to_severity(hw_err);
	const char *severity_str = error_severity[severity];
	struct xe_device *xe = tile_to_xe(tile);
	unsigned long flags;
	u32 err_src;

	if (xe->info.platform != XE_BATTLEMAGE)
		return;

	spin_lock_irqsave(&xe->irq.lock, flags);
	err_src = xe_mmio_read32(&tile->mmio, DEV_ERR_STAT_REG(hw_err));
	if (!err_src) {
		drm_err_ratelimited(&xe->drm, HW_ERR "Tile%d reported %s DEV_ERR_STAT register blank!\n",
				    tile->id, severity_str);
		goto unlock;
	}

	if (err_src & XE_CSC_ERROR)
		csc_hw_error_handler(tile, hw_err);

	xe_mmio_write32(&tile->mmio, DEV_ERR_STAT_REG(hw_err), err_src);

unlock:
	spin_unlock_irqrestore(&xe->irq.lock, flags);
}

/**
 * xe_hw_error_irq_handler - irq handling for hw errors
 * @tile: tile instance
 * @master_ctl: value read from master interrupt register
 *
 * Xe platforms add three error bits to the master interrupt register to support error handling.
 * These three bits are used to convey the class of error FATAL, NONFATAL, or CORRECTABLE.
 * To process the interrupt, determine the source of error by reading the Device Error Source
 * Register that corresponds to the class of error being serviced.
 */
void xe_hw_error_irq_handler(struct xe_tile *tile, const u32 master_ctl)
{
	enum hardware_error hw_err;

	if (fault_inject_csc_hw_error())
		schedule_work(&tile->csc_hw_error_work);

	for (hw_err = 0; hw_err < HARDWARE_ERROR_MAX; hw_err++)
		if (master_ctl & ERROR_IRQ(hw_err))
			hw_error_source_handler(tile, hw_err);
}

static int hw_error_info_init(struct xe_device *xe)
{
	if (xe->info.platform != XE_PVC)
		return 0;

	return xe_drm_ras_init(xe);
}

/*
 * Process hardware errors during boot
 */
static void process_hw_errors(struct xe_device *xe)
{
	struct xe_tile *tile;
	u32 master_ctl;
	u8 id;

	for_each_tile(tile, xe, id) {
		master_ctl = xe_mmio_read32(&tile->mmio, GFX_MSTR_IRQ);
		xe_hw_error_irq_handler(tile, master_ctl);
		xe_mmio_write32(&tile->mmio, GFX_MSTR_IRQ, master_ctl);
	}
}

/**
 * xe_hw_error_init - Initialize hw errors
 * @xe: xe device instance
 *
 * Initialize and check for errors that occurred during boot
 * prior to driver load
 */
void xe_hw_error_init(struct xe_device *xe)
{
	struct xe_tile *tile = xe_device_get_root_tile(xe);
	int ret;

	if (!IS_DGFX(xe) || IS_SRIOV_VF(xe))
		return;

	INIT_WORK(&tile->csc_hw_error_work, csc_hw_error_work);

	ret = hw_error_info_init(xe);
	if (ret)
		drm_err(&xe->drm, "Failed to initialize XE DRM RAS (%pe)\n", ERR_PTR(ret));

	process_hw_errors(xe);
}
