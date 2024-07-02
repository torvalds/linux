// SPDX-License-Identifier: MIT
/*
 * Copyright © 2014-2019 Intel Corporation
 *
 * Authors:
 *    Vinit Azad <vinit.azad@intel.com>
 *    Ben Widawsky <ben@bwidawsk.net>
 *    Dave Gordon <david.s.gordon@intel.com>
 *    Alex Dai <yu.dai@intel.com>
 */

#include "gt/intel_gt.h"
#include "gt/intel_gt_mcr.h"
#include "gt/intel_gt_regs.h"
#include "gt/intel_rps.h"
#include "intel_guc_fw.h"
#include "intel_guc_print.h"
#include "i915_drv.h"

static void guc_prepare_xfer(struct intel_gt *gt)
{
	struct intel_uncore *uncore = gt->uncore;

	u32 shim_flags = GUC_ENABLE_READ_CACHE_LOGIC |
			 GUC_ENABLE_READ_CACHE_FOR_SRAM_DATA |
			 GUC_ENABLE_READ_CACHE_FOR_WOPCM_DATA |
			 GUC_ENABLE_MIA_CLOCK_GATING;

	if (GRAPHICS_VER_FULL(uncore->i915) < IP_VER(12, 55))
		shim_flags |= GUC_DISABLE_SRAM_INIT_TO_ZEROES |
			      GUC_ENABLE_MIA_CACHING;

	/* Must program this register before loading the ucode with DMA */
	intel_uncore_write(uncore, GUC_SHIM_CONTROL, shim_flags);

	if (IS_GEN9_LP(uncore->i915))
		intel_uncore_write(uncore, GEN9LP_GT_PM_CONFIG, GT_DOORBELL_ENABLE);
	else
		intel_uncore_write(uncore, GEN9_GT_PM_CONFIG, GT_DOORBELL_ENABLE);

	if (GRAPHICS_VER(uncore->i915) == 9) {
		/* DOP Clock Gating Enable for GuC clocks */
		intel_uncore_rmw(uncore, GEN7_MISCCPCTL, 0,
				 GEN8_DOP_CLOCK_GATE_GUC_ENABLE);

		/* allows for 5us (in 10ns units) before GT can go to RC6 */
		intel_uncore_write(uncore, GUC_ARAT_C6DIS, 0x1FF);
	}
}

static int guc_xfer_rsa_mmio(struct intel_uc_fw *guc_fw,
			     struct intel_uncore *uncore)
{
	u32 rsa[UOS_RSA_SCRATCH_COUNT];
	size_t copied;
	int i;

	copied = intel_uc_fw_copy_rsa(guc_fw, rsa, sizeof(rsa));
	if (copied < sizeof(rsa))
		return -ENOMEM;

	for (i = 0; i < UOS_RSA_SCRATCH_COUNT; i++)
		intel_uncore_write(uncore, UOS_RSA_SCRATCH(i), rsa[i]);

	return 0;
}

static int guc_xfer_rsa_vma(struct intel_uc_fw *guc_fw,
			    struct intel_uncore *uncore)
{
	struct intel_guc *guc = container_of(guc_fw, struct intel_guc, fw);

	intel_uncore_write(uncore, UOS_RSA_SCRATCH(0),
			   intel_guc_ggtt_offset(guc, guc_fw->rsa_data));

	return 0;
}

/* Copy RSA signature from the fw image to HW for verification */
static int guc_xfer_rsa(struct intel_uc_fw *guc_fw,
			struct intel_uncore *uncore)
{
	if (guc_fw->rsa_data)
		return guc_xfer_rsa_vma(guc_fw, uncore);
	else
		return guc_xfer_rsa_mmio(guc_fw, uncore);
}

/*
 * Read the GuC status register (GUC_STATUS) and store it in the
 * specified location; then return a boolean indicating whether
 * the value matches either completion or a known failure code.
 *
 * This is used for polling the GuC status in a wait_for()
 * loop below.
 */
static inline bool guc_load_done(struct intel_uncore *uncore, u32 *status, bool *success)
{
	u32 val = intel_uncore_read(uncore, GUC_STATUS);
	u32 uk_val = REG_FIELD_GET(GS_UKERNEL_MASK, val);
	u32 br_val = REG_FIELD_GET(GS_BOOTROM_MASK, val);

	*status = val;
	switch (uk_val) {
	case INTEL_GUC_LOAD_STATUS_READY:
		*success = true;
		return true;

	case INTEL_GUC_LOAD_STATUS_ERROR_DEVID_BUILD_MISMATCH:
	case INTEL_GUC_LOAD_STATUS_GUC_PREPROD_BUILD_MISMATCH:
	case INTEL_GUC_LOAD_STATUS_ERROR_DEVID_INVALID_GUCTYPE:
	case INTEL_GUC_LOAD_STATUS_HWCONFIG_ERROR:
	case INTEL_GUC_LOAD_STATUS_DPC_ERROR:
	case INTEL_GUC_LOAD_STATUS_EXCEPTION:
	case INTEL_GUC_LOAD_STATUS_INIT_DATA_INVALID:
	case INTEL_GUC_LOAD_STATUS_MPU_DATA_INVALID:
	case INTEL_GUC_LOAD_STATUS_INIT_MMIO_SAVE_RESTORE_INVALID:
	case INTEL_GUC_LOAD_STATUS_KLV_WORKAROUND_INIT_ERROR:
		*success = false;
		return true;
	}

	switch (br_val) {
	case INTEL_BOOTROM_STATUS_NO_KEY_FOUND:
	case INTEL_BOOTROM_STATUS_RSA_FAILED:
	case INTEL_BOOTROM_STATUS_PAVPC_FAILED:
	case INTEL_BOOTROM_STATUS_WOPCM_FAILED:
	case INTEL_BOOTROM_STATUS_LOADLOC_FAILED:
	case INTEL_BOOTROM_STATUS_JUMP_FAILED:
	case INTEL_BOOTROM_STATUS_RC6CTXCONFIG_FAILED:
	case INTEL_BOOTROM_STATUS_MPUMAP_INCORRECT:
	case INTEL_BOOTROM_STATUS_EXCEPTION:
	case INTEL_BOOTROM_STATUS_PROD_KEY_CHECK_FAILURE:
		*success = false;
		return true;
	}

	return false;
}

/*
 * Use a longer timeout for debug builds so that problems can be detected
 * and analysed. But a shorter timeout for releases so that user's don't
 * wait forever to find out there is a problem. Note that the only reason
 * an end user should hit the timeout is in case of extreme thermal throttling.
 * And a system that is that hot during boot is probably dead anyway!
 */
#if defined(CONFIG_DRM_I915_DEBUG_GEM)
#define GUC_LOAD_RETRY_LIMIT	20
#else
#define GUC_LOAD_RETRY_LIMIT	3
#endif

static int guc_wait_ucode(struct intel_guc *guc)
{
	struct intel_gt *gt = guc_to_gt(guc);
	struct intel_uncore *uncore = gt->uncore;
	ktime_t before, after, delta;
	bool success;
	u32 status;
	int ret, count;
	u64 delta_ms;
	u32 before_freq;

	/*
	 * Wait for the GuC to start up.
	 *
	 * Measurements indicate this should take no more than 20ms
	 * (assuming the GT clock is at maximum frequency). So, a
	 * timeout here indicates that the GuC has failed and is unusable.
	 * (Higher levels of the driver may decide to reset the GuC and
	 * attempt the ucode load again if this happens.)
	 *
	 * FIXME: There is a known (but exceedingly unlikely) race condition
	 * where the asynchronous frequency management code could reduce
	 * the GT clock while a GuC reload is in progress (during a full
	 * GT reset). A fix is in progress but there are complex locking
	 * issues to be resolved. In the meantime bump the timeout to
	 * 200ms. Even at slowest clock, this should be sufficient. And
	 * in the working case, a larger timeout makes no difference.
	 *
	 * IFWI updates have also been seen to cause sporadic failures due to
	 * the requested frequency not being granted and thus the firmware
	 * load is attempted at minimum frequency. That can lead to load times
	 * in the seconds range. However, there is a limit on how long an
	 * individual wait_for() can wait. So wrap it in a loop.
	 */
	before_freq = intel_rps_read_actual_frequency(&gt->rps);
	before = ktime_get();
	for (count = 0; count < GUC_LOAD_RETRY_LIMIT; count++) {
		ret = wait_for(guc_load_done(uncore, &status, &success), 1000);
		if (!ret || !success)
			break;

		guc_dbg(guc, "load still in progress, count = %d, freq = %dMHz, status = 0x%08X [0x%02X/%02X]\n",
			count, intel_rps_read_actual_frequency(&gt->rps), status,
			REG_FIELD_GET(GS_BOOTROM_MASK, status),
			REG_FIELD_GET(GS_UKERNEL_MASK, status));
	}
	after = ktime_get();
	delta = ktime_sub(after, before);
	delta_ms = ktime_to_ms(delta);
	if (ret || !success) {
		u32 ukernel = REG_FIELD_GET(GS_UKERNEL_MASK, status);
		u32 bootrom = REG_FIELD_GET(GS_BOOTROM_MASK, status);

		guc_info(guc, "load failed: status = 0x%08X, time = %lldms, freq = %dMHz, ret = %d\n",
			 status, delta_ms, intel_rps_read_actual_frequency(&gt->rps), ret);
		guc_info(guc, "load failed: status: Reset = %d, BootROM = 0x%02X, UKernel = 0x%02X, MIA = 0x%02X, Auth = 0x%02X\n",
			 REG_FIELD_GET(GS_MIA_IN_RESET, status),
			 bootrom, ukernel,
			 REG_FIELD_GET(GS_MIA_MASK, status),
			 REG_FIELD_GET(GS_AUTH_STATUS_MASK, status));

		switch (bootrom) {
		case INTEL_BOOTROM_STATUS_NO_KEY_FOUND:
			guc_info(guc, "invalid key requested, header = 0x%08X\n",
				 intel_uncore_read(uncore, GUC_HEADER_INFO));
			ret = -ENOEXEC;
			break;

		case INTEL_BOOTROM_STATUS_RSA_FAILED:
			guc_info(guc, "firmware signature verification failed\n");
			ret = -ENOEXEC;
			break;

		case INTEL_BOOTROM_STATUS_PROD_KEY_CHECK_FAILURE:
			guc_info(guc, "firmware production part check failure\n");
			ret = -ENOEXEC;
			break;
		}

		switch (ukernel) {
		case INTEL_GUC_LOAD_STATUS_EXCEPTION:
			guc_info(guc, "firmware exception. EIP: %#x\n",
				 intel_uncore_read(uncore, SOFT_SCRATCH(13)));
			ret = -ENXIO;
			break;

		case INTEL_GUC_LOAD_STATUS_INIT_MMIO_SAVE_RESTORE_INVALID:
			guc_info(guc, "illegal register in save/restore workaround list\n");
			ret = -EPERM;
			break;

		case INTEL_GUC_LOAD_STATUS_KLV_WORKAROUND_INIT_ERROR:
			guc_info(guc, "invalid w/a KLV entry\n");
			ret = -EINVAL;
			break;

		case INTEL_GUC_LOAD_STATUS_HWCONFIG_START:
			guc_info(guc, "still extracting hwconfig table.\n");
			ret = -ETIMEDOUT;
			break;
		}

		/* Uncommon/unexpected error, see earlier status code print for details */
		if (ret == 0)
			ret = -ENXIO;
	} else if (delta_ms > 200) {
		guc_warn(guc, "excessive init time: %lldms! [status = 0x%08X, count = %d, ret = %d]\n",
			 delta_ms, status, count, ret);
		guc_warn(guc, "excessive init time: [freq = %dMHz, before = %dMHz, perf_limit_reasons = 0x%08X]\n",
			 intel_rps_read_actual_frequency(&gt->rps), before_freq,
			 intel_uncore_read(uncore, intel_gt_perf_limit_reasons_reg(gt)));
	} else {
		guc_dbg(guc, "init took %lldms, freq = %dMHz, before = %dMHz, status = 0x%08X, count = %d, ret = %d\n",
			delta_ms, intel_rps_read_actual_frequency(&gt->rps),
			before_freq, status, count, ret);
	}

	return ret;
}

/**
 * intel_guc_fw_upload() - load GuC uCode to device
 * @guc: intel_guc structure
 *
 * Called from intel_uc_init_hw() during driver load, resume from sleep and
 * after a GPU reset.
 *
 * The firmware image should have already been fetched into memory, so only
 * check that fetch succeeded, and then transfer the image to the h/w.
 *
 * Return:	non-zero code on error
 */
int intel_guc_fw_upload(struct intel_guc *guc)
{
	struct intel_gt *gt = guc_to_gt(guc);
	struct intel_uncore *uncore = gt->uncore;
	int ret;

	guc_prepare_xfer(gt);

	/*
	 * Note that GuC needs the CSS header plus uKernel code to be copied
	 * by the DMA engine in one operation, whereas the RSA signature is
	 * loaded separately, either by copying it to the UOS_RSA_SCRATCH
	 * register (if key size <= 256) or through a ggtt-pinned vma (if key
	 * size > 256). The RSA size and therefore the way we provide it to the
	 * HW is fixed for each platform and hard-coded in the bootrom.
	 */
	ret = guc_xfer_rsa(&guc->fw, uncore);
	if (ret)
		goto out;

	/*
	 * Current uCode expects the code to be loaded at 8k; locations below
	 * this are used for the stack.
	 */
	ret = intel_uc_fw_upload(&guc->fw, 0x2000, UOS_MOVE);
	if (ret)
		goto out;

	ret = guc_wait_ucode(guc);
	if (ret)
		goto out;

	intel_uc_fw_change_status(&guc->fw, INTEL_UC_FIRMWARE_RUNNING);
	return 0;

out:
	intel_uc_fw_change_status(&guc->fw, INTEL_UC_FIRMWARE_LOAD_FAIL);
	return ret;
}
