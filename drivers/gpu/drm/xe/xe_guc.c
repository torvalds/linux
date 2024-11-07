// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_guc.h"

#include <drm/drm_managed.h>

#include <generated/xe_wa_oob.h>

#include "abi/guc_actions_abi.h"
#include "abi/guc_errors_abi.h"
#include "regs/xe_gt_regs.h"
#include "regs/xe_gtt_defs.h"
#include "regs/xe_guc_regs.h"
#include "regs/xe_irq_regs.h"
#include "xe_bo.h"
#include "xe_device.h"
#include "xe_force_wake.h"
#include "xe_gt.h"
#include "xe_gt_printk.h"
#include "xe_gt_sriov_vf.h"
#include "xe_gt_throttle.h"
#include "xe_guc_ads.h"
#include "xe_guc_capture.h"
#include "xe_guc_ct.h"
#include "xe_guc_db_mgr.h"
#include "xe_guc_hwconfig.h"
#include "xe_guc_log.h"
#include "xe_guc_pc.h"
#include "xe_guc_relay.h"
#include "xe_guc_submit.h"
#include "xe_memirq.h"
#include "xe_mmio.h"
#include "xe_platform_types.h"
#include "xe_sriov.h"
#include "xe_uc.h"
#include "xe_uc_fw.h"
#include "xe_wa.h"
#include "xe_wopcm.h"

static u32 guc_bo_ggtt_addr(struct xe_guc *guc,
			    struct xe_bo *bo)
{
	struct xe_device *xe = guc_to_xe(guc);
	u32 addr = xe_bo_ggtt_addr(bo);

	/* GuC addresses above GUC_GGTT_TOP don't map through the GTT */
	xe_assert(xe, addr >= xe_wopcm_size(guc_to_xe(guc)));
	xe_assert(xe, addr < GUC_GGTT_TOP);
	xe_assert(xe, bo->size <= GUC_GGTT_TOP - addr);

	return addr;
}

static u32 guc_ctl_debug_flags(struct xe_guc *guc)
{
	u32 level = xe_guc_log_get_level(&guc->log);
	u32 flags = 0;

	if (!GUC_LOG_LEVEL_IS_VERBOSE(level))
		flags |= GUC_LOG_DISABLED;
	else
		flags |= GUC_LOG_LEVEL_TO_VERBOSITY(level) <<
			 GUC_LOG_VERBOSITY_SHIFT;

	return flags;
}

static u32 guc_ctl_feature_flags(struct xe_guc *guc)
{
	u32 flags = GUC_CTL_ENABLE_LITE_RESTORE;

	if (!guc_to_xe(guc)->info.skip_guc_pc)
		flags |= GUC_CTL_ENABLE_SLPC;

	return flags;
}

static u32 guc_ctl_log_params_flags(struct xe_guc *guc)
{
	u32 offset = guc_bo_ggtt_addr(guc, guc->log.bo) >> PAGE_SHIFT;
	u32 flags;

	#if (((CRASH_BUFFER_SIZE) % SZ_1M) == 0)
	#define LOG_UNIT SZ_1M
	#define LOG_FLAG GUC_LOG_LOG_ALLOC_UNITS
	#else
	#define LOG_UNIT SZ_4K
	#define LOG_FLAG 0
	#endif

	#if (((CAPTURE_BUFFER_SIZE) % SZ_1M) == 0)
	#define CAPTURE_UNIT SZ_1M
	#define CAPTURE_FLAG GUC_LOG_CAPTURE_ALLOC_UNITS
	#else
	#define CAPTURE_UNIT SZ_4K
	#define CAPTURE_FLAG 0
	#endif

	BUILD_BUG_ON(!CRASH_BUFFER_SIZE);
	BUILD_BUG_ON(!IS_ALIGNED(CRASH_BUFFER_SIZE, LOG_UNIT));
	BUILD_BUG_ON(!DEBUG_BUFFER_SIZE);
	BUILD_BUG_ON(!IS_ALIGNED(DEBUG_BUFFER_SIZE, LOG_UNIT));
	BUILD_BUG_ON(!CAPTURE_BUFFER_SIZE);
	BUILD_BUG_ON(!IS_ALIGNED(CAPTURE_BUFFER_SIZE, CAPTURE_UNIT));

	BUILD_BUG_ON((CRASH_BUFFER_SIZE / LOG_UNIT - 1) >
			(GUC_LOG_CRASH_MASK >> GUC_LOG_CRASH_SHIFT));
	BUILD_BUG_ON((DEBUG_BUFFER_SIZE / LOG_UNIT - 1) >
			(GUC_LOG_DEBUG_MASK >> GUC_LOG_DEBUG_SHIFT));
	BUILD_BUG_ON((CAPTURE_BUFFER_SIZE / CAPTURE_UNIT - 1) >
			(GUC_LOG_CAPTURE_MASK >> GUC_LOG_CAPTURE_SHIFT));

	flags = GUC_LOG_VALID |
		GUC_LOG_NOTIFY_ON_HALF_FULL |
		CAPTURE_FLAG |
		LOG_FLAG |
		((CRASH_BUFFER_SIZE / LOG_UNIT - 1) << GUC_LOG_CRASH_SHIFT) |
		((DEBUG_BUFFER_SIZE / LOG_UNIT - 1) << GUC_LOG_DEBUG_SHIFT) |
		((CAPTURE_BUFFER_SIZE / CAPTURE_UNIT - 1) <<
		 GUC_LOG_CAPTURE_SHIFT) |
		(offset << GUC_LOG_BUF_ADDR_SHIFT);

	#undef LOG_UNIT
	#undef LOG_FLAG
	#undef CAPTURE_UNIT
	#undef CAPTURE_FLAG

	return flags;
}

static u32 guc_ctl_ads_flags(struct xe_guc *guc)
{
	u32 ads = guc_bo_ggtt_addr(guc, guc->ads.bo) >> PAGE_SHIFT;
	u32 flags = ads << GUC_ADS_ADDR_SHIFT;

	return flags;
}

static u32 guc_ctl_wa_flags(struct xe_guc *guc)
{
	struct xe_device *xe = guc_to_xe(guc);
	struct xe_gt *gt = guc_to_gt(guc);
	u32 flags = 0;

	if (XE_WA(gt, 22012773006))
		flags |= GUC_WA_POLLCS;

	if (XE_WA(gt, 14014475959))
		flags |= GUC_WA_HOLD_CCS_SWITCHOUT;

	if (XE_WA(gt, 22011391025))
		flags |= GUC_WA_DUAL_QUEUE;

	/*
	 * Wa_22011802037: FIXME - there's more to be done than simply setting
	 * this flag: make sure each CS is stopped when preparing for GT reset
	 * and wait for pending MI_FW.
	 */
	if (GRAPHICS_VERx100(xe) < 1270)
		flags |= GUC_WA_PRE_PARSER;

	if (XE_WA(gt, 22012727170) || XE_WA(gt, 22012727685))
		flags |= GUC_WA_CONTEXT_ISOLATION;

	if (XE_WA(gt, 18020744125) &&
	    !xe_hw_engine_mask_per_class(gt, XE_ENGINE_CLASS_RENDER))
		flags |= GUC_WA_RCS_REGS_IN_CCS_REGS_LIST;

	if (XE_WA(gt, 1509372804))
		flags |= GUC_WA_RENDER_RST_RC6_EXIT;

	if (XE_WA(gt, 14018913170))
		flags |= GUC_WA_ENABLE_TSC_CHECK_ON_RC6;

	return flags;
}

static u32 guc_ctl_devid(struct xe_guc *guc)
{
	struct xe_device *xe = guc_to_xe(guc);

	return (((u32)xe->info.devid) << 16) | xe->info.revid;
}

static void guc_print_params(struct xe_guc *guc)
{
	struct xe_gt *gt = guc_to_gt(guc);
	u32 *params = guc->params;
	int i;

	BUILD_BUG_ON(sizeof(guc->params) != GUC_CTL_MAX_DWORDS * sizeof(u32));
	BUILD_BUG_ON(GUC_CTL_MAX_DWORDS + 2 != SOFT_SCRATCH_COUNT);

	for (i = 0; i < GUC_CTL_MAX_DWORDS; i++)
		xe_gt_dbg(gt, "GuC param[%2d] = 0x%08x\n", i, params[i]);
}

static void guc_init_params(struct xe_guc *guc)
{
	u32 *params = guc->params;

	params[GUC_CTL_LOG_PARAMS] = guc_ctl_log_params_flags(guc);
	params[GUC_CTL_FEATURE] = 0;
	params[GUC_CTL_DEBUG] = guc_ctl_debug_flags(guc);
	params[GUC_CTL_ADS] = guc_ctl_ads_flags(guc);
	params[GUC_CTL_WA] = 0;
	params[GUC_CTL_DEVID] = guc_ctl_devid(guc);

	guc_print_params(guc);
}

static void guc_init_params_post_hwconfig(struct xe_guc *guc)
{
	u32 *params = guc->params;

	params[GUC_CTL_LOG_PARAMS] = guc_ctl_log_params_flags(guc);
	params[GUC_CTL_FEATURE] = guc_ctl_feature_flags(guc);
	params[GUC_CTL_DEBUG] = guc_ctl_debug_flags(guc);
	params[GUC_CTL_ADS] = guc_ctl_ads_flags(guc);
	params[GUC_CTL_WA] = guc_ctl_wa_flags(guc);
	params[GUC_CTL_DEVID] = guc_ctl_devid(guc);

	guc_print_params(guc);
}

/*
 * Initialize the GuC parameter block before starting the firmware
 * transfer. These parameters are read by the firmware on startup
 * and cannot be changed thereafter.
 */
static void guc_write_params(struct xe_guc *guc)
{
	struct xe_gt *gt = guc_to_gt(guc);
	int i;

	xe_force_wake_assert_held(gt_to_fw(gt), XE_FW_GT);

	xe_mmio_write32(&gt->mmio, SOFT_SCRATCH(0), 0);

	for (i = 0; i < GUC_CTL_MAX_DWORDS; i++)
		xe_mmio_write32(&gt->mmio, SOFT_SCRATCH(1 + i), guc->params[i]);
}

static void guc_fini_hw(void *arg)
{
	struct xe_guc *guc = arg;
	struct xe_gt *gt = guc_to_gt(guc);
	unsigned int fw_ref;

	fw_ref = xe_force_wake_get(gt_to_fw(gt), XE_FORCEWAKE_ALL);
	xe_uc_fini_hw(&guc_to_gt(guc)->uc);
	xe_force_wake_put(gt_to_fw(gt), fw_ref);
}

/**
 * xe_guc_comm_init_early - early initialization of GuC communication
 * @guc: the &xe_guc to initialize
 *
 * Must be called prior to first MMIO communication with GuC firmware.
 */
void xe_guc_comm_init_early(struct xe_guc *guc)
{
	struct xe_gt *gt = guc_to_gt(guc);

	if (xe_gt_is_media_type(gt))
		guc->notify_reg = MED_GUC_HOST_INTERRUPT;
	else
		guc->notify_reg = GUC_HOST_INTERRUPT;
}

static int xe_guc_realloc_post_hwconfig(struct xe_guc *guc)
{
	struct xe_tile *tile = gt_to_tile(guc_to_gt(guc));
	struct xe_device *xe = guc_to_xe(guc);
	int ret;

	if (!IS_DGFX(guc_to_xe(guc)))
		return 0;

	ret = xe_managed_bo_reinit_in_vram(xe, tile, &guc->fw.bo);
	if (ret)
		return ret;

	ret = xe_managed_bo_reinit_in_vram(xe, tile, &guc->log.bo);
	if (ret)
		return ret;

	ret = xe_managed_bo_reinit_in_vram(xe, tile, &guc->ads.bo);
	if (ret)
		return ret;

	ret = xe_managed_bo_reinit_in_vram(xe, tile, &guc->ct.bo);
	if (ret)
		return ret;

	return 0;
}

static int vf_guc_init(struct xe_guc *guc)
{
	int err;

	xe_guc_comm_init_early(guc);

	err = xe_guc_ct_init(&guc->ct);
	if (err)
		return err;

	err = xe_guc_relay_init(&guc->relay);
	if (err)
		return err;

	return 0;
}

int xe_guc_init(struct xe_guc *guc)
{
	struct xe_device *xe = guc_to_xe(guc);
	struct xe_gt *gt = guc_to_gt(guc);
	int ret;

	guc->fw.type = XE_UC_FW_TYPE_GUC;
	ret = xe_uc_fw_init(&guc->fw);
	if (ret)
		goto out;

	if (!xe_uc_fw_is_enabled(&guc->fw))
		return 0;

	if (IS_SRIOV_VF(xe)) {
		ret = vf_guc_init(guc);
		if (ret)
			goto out;
		return 0;
	}

	ret = xe_guc_log_init(&guc->log);
	if (ret)
		goto out;

	ret = xe_guc_capture_init(guc);
	if (ret)
		goto out;

	ret = xe_guc_ads_init(&guc->ads);
	if (ret)
		goto out;

	ret = xe_guc_ct_init(&guc->ct);
	if (ret)
		goto out;

	ret = xe_guc_relay_init(&guc->relay);
	if (ret)
		goto out;

	xe_uc_fw_change_status(&guc->fw, XE_UC_FIRMWARE_LOADABLE);

	ret = devm_add_action_or_reset(xe->drm.dev, guc_fini_hw, guc);
	if (ret)
		goto out;

	guc_init_params(guc);

	xe_guc_comm_init_early(guc);

	return 0;

out:
	xe_gt_err(gt, "GuC init failed with %pe\n", ERR_PTR(ret));
	return ret;
}

static int vf_guc_init_post_hwconfig(struct xe_guc *guc)
{
	int err;

	err = xe_guc_submit_init(guc, xe_gt_sriov_vf_guc_ids(guc_to_gt(guc)));
	if (err)
		return err;

	/* XXX xe_guc_db_mgr_init not needed for now */

	return 0;
}

/**
 * xe_guc_init_post_hwconfig - initialize GuC post hwconfig load
 * @guc: The GuC object
 *
 * Return: 0 on success, negative error code on error.
 */
int xe_guc_init_post_hwconfig(struct xe_guc *guc)
{
	int ret;

	if (IS_SRIOV_VF(guc_to_xe(guc)))
		return vf_guc_init_post_hwconfig(guc);

	ret = xe_guc_realloc_post_hwconfig(guc);
	if (ret)
		return ret;

	guc_init_params_post_hwconfig(guc);

	ret = xe_guc_submit_init(guc, ~0);
	if (ret)
		return ret;

	ret = xe_guc_db_mgr_init(&guc->dbm, ~0);
	if (ret)
		return ret;

	ret = xe_guc_pc_init(&guc->pc);
	if (ret)
		return ret;

	return xe_guc_ads_init_post_hwconfig(&guc->ads);
}

int xe_guc_post_load_init(struct xe_guc *guc)
{
	xe_guc_ads_populate_post_load(&guc->ads);
	guc->submission_state.enabled = true;

	return 0;
}

int xe_guc_reset(struct xe_guc *guc)
{
	struct xe_gt *gt = guc_to_gt(guc);
	struct xe_mmio *mmio = &gt->mmio;
	u32 guc_status, gdrst;
	int ret;

	xe_force_wake_assert_held(gt_to_fw(gt), XE_FW_GT);

	if (IS_SRIOV_VF(gt_to_xe(gt)))
		return xe_gt_sriov_vf_bootstrap(gt);

	xe_mmio_write32(mmio, GDRST, GRDOM_GUC);

	ret = xe_mmio_wait32(mmio, GDRST, GRDOM_GUC, 0, 5000, &gdrst, false);
	if (ret) {
		xe_gt_err(gt, "GuC reset timed out, GDRST=%#x\n", gdrst);
		goto err_out;
	}

	guc_status = xe_mmio_read32(mmio, GUC_STATUS);
	if (!(guc_status & GS_MIA_IN_RESET)) {
		xe_gt_err(gt, "GuC status: %#x, MIA core expected to be in reset\n",
			  guc_status);
		ret = -EIO;
		goto err_out;
	}

	return 0;

err_out:

	return ret;
}

static void guc_prepare_xfer(struct xe_guc *guc)
{
	struct xe_gt *gt = guc_to_gt(guc);
	struct xe_mmio *mmio = &gt->mmio;
	struct xe_device *xe =  guc_to_xe(guc);
	u32 shim_flags = GUC_ENABLE_READ_CACHE_LOGIC |
		GUC_ENABLE_READ_CACHE_FOR_SRAM_DATA |
		GUC_ENABLE_READ_CACHE_FOR_WOPCM_DATA |
		GUC_ENABLE_MIA_CLOCK_GATING;

	if (GRAPHICS_VERx100(xe) < 1250)
		shim_flags |= GUC_DISABLE_SRAM_INIT_TO_ZEROES |
				GUC_ENABLE_MIA_CACHING;

	if (GRAPHICS_VER(xe) >= 20 || xe->info.platform == XE_PVC)
		shim_flags |= REG_FIELD_PREP(GUC_MOCS_INDEX_MASK, gt->mocs.uc_index);

	/* Must program this register before loading the ucode with DMA */
	xe_mmio_write32(mmio, GUC_SHIM_CONTROL, shim_flags);

	xe_mmio_write32(mmio, GT_PM_CONFIG, GT_DOORBELL_ENABLE);

	/* Make sure GuC receives ARAT interrupts */
	xe_mmio_rmw32(mmio, PMINTRMSK, ARAT_EXPIRED_INTRMSK, 0);
}

/*
 * Supporting MMIO & in memory RSA
 */
static int guc_xfer_rsa(struct xe_guc *guc)
{
	struct xe_gt *gt = guc_to_gt(guc);
	u32 rsa[UOS_RSA_SCRATCH_COUNT];
	size_t copied;
	int i;

	if (guc->fw.rsa_size > 256) {
		u32 rsa_ggtt_addr = xe_bo_ggtt_addr(guc->fw.bo) +
				    xe_uc_fw_rsa_offset(&guc->fw);
		xe_mmio_write32(&gt->mmio, UOS_RSA_SCRATCH(0), rsa_ggtt_addr);
		return 0;
	}

	copied = xe_uc_fw_copy_rsa(&guc->fw, rsa, sizeof(rsa));
	if (copied < sizeof(rsa))
		return -ENOMEM;

	for (i = 0; i < UOS_RSA_SCRATCH_COUNT; i++)
		xe_mmio_write32(&gt->mmio, UOS_RSA_SCRATCH(i), rsa[i]);

	return 0;
}

/*
 * Check a previously read GuC status register (GUC_STATUS) looking for
 * known terminal states (either completion or failure) of either the
 * microkernel status field or the boot ROM status field. Returns +1 for
 * successful completion, -1 for failure and 0 for any intermediate state.
 */
static int guc_load_done(u32 status)
{
	u32 uk_val = REG_FIELD_GET(GS_UKERNEL_MASK, status);
	u32 br_val = REG_FIELD_GET(GS_BOOTROM_MASK, status);

	switch (uk_val) {
	case XE_GUC_LOAD_STATUS_READY:
		return 1;

	case XE_GUC_LOAD_STATUS_ERROR_DEVID_BUILD_MISMATCH:
	case XE_GUC_LOAD_STATUS_GUC_PREPROD_BUILD_MISMATCH:
	case XE_GUC_LOAD_STATUS_ERROR_DEVID_INVALID_GUCTYPE:
	case XE_GUC_LOAD_STATUS_HWCONFIG_ERROR:
	case XE_GUC_LOAD_STATUS_DPC_ERROR:
	case XE_GUC_LOAD_STATUS_EXCEPTION:
	case XE_GUC_LOAD_STATUS_INIT_DATA_INVALID:
	case XE_GUC_LOAD_STATUS_MPU_DATA_INVALID:
	case XE_GUC_LOAD_STATUS_INIT_MMIO_SAVE_RESTORE_INVALID:
		return -1;
	}

	switch (br_val) {
	case XE_BOOTROM_STATUS_NO_KEY_FOUND:
	case XE_BOOTROM_STATUS_RSA_FAILED:
	case XE_BOOTROM_STATUS_PAVPC_FAILED:
	case XE_BOOTROM_STATUS_WOPCM_FAILED:
	case XE_BOOTROM_STATUS_LOADLOC_FAILED:
	case XE_BOOTROM_STATUS_JUMP_FAILED:
	case XE_BOOTROM_STATUS_RC6CTXCONFIG_FAILED:
	case XE_BOOTROM_STATUS_MPUMAP_INCORRECT:
	case XE_BOOTROM_STATUS_EXCEPTION:
	case XE_BOOTROM_STATUS_PROD_KEY_CHECK_FAILURE:
		return -1;
	}

	return 0;
}

static s32 guc_pc_get_cur_freq(struct xe_guc_pc *guc_pc)
{
	u32 freq;
	int ret = xe_guc_pc_get_cur_freq(guc_pc, &freq);

	return ret ? ret : freq;
}

/*
 * Wait for the GuC to start up.
 *
 * Measurements indicate this should take no more than 20ms (assuming the GT
 * clock is at maximum frequency). However, thermal throttling and other issues
 * can prevent the clock hitting max and thus making the load take significantly
 * longer. Allow up to 200ms as a safety margin for real world worst case situations.
 *
 * However, bugs anywhere from KMD to GuC to PCODE to fan failure in a CI farm can
 * lead to even longer times. E.g. if the GT is clamped to minimum frequency then
 * the load times can be in the seconds range. So the timeout is increased for debug
 * builds to ensure that problems can be correctly analysed. For release builds, the
 * timeout is kept short so that users don't wait forever to find out that there is a
 * problem. In either case, if the load took longer than is reasonable even with some
 * 'sensible' throttling, then flag a warning because something is not right.
 *
 * Note that there is a limit on how long an individual usleep_range() can wait for,
 * hence longer waits require wrapping a shorter wait in a loop.
 *
 * Note that the only reason an end user should hit the shorter timeout is in case of
 * extreme thermal throttling. And a system that is that hot during boot is probably
 * dead anyway!
 */
#if IS_ENABLED(CONFIG_DRM_XE_DEBUG)
#define GUC_LOAD_RETRY_LIMIT	20
#else
#define GUC_LOAD_RETRY_LIMIT	3
#endif
#define GUC_LOAD_TIME_WARN_MS      200

static void guc_wait_ucode(struct xe_guc *guc)
{
	struct xe_gt *gt = guc_to_gt(guc);
	struct xe_mmio *mmio = &gt->mmio;
	struct xe_guc_pc *guc_pc = &gt->uc.guc.pc;
	ktime_t before, after, delta;
	int load_done;
	u32 status = 0;
	int count = 0;
	u64 delta_ms;
	u32 before_freq;

	before_freq = xe_guc_pc_get_act_freq(guc_pc);
	before = ktime_get();
	/*
	 * Note, can't use any kind of timing information from the call to xe_mmio_wait.
	 * It could return a thousand intermediate stages at random times. Instead, must
	 * manually track the total time taken and locally implement the timeout.
	 */
	do {
		u32 last_status = status & (GS_UKERNEL_MASK | GS_BOOTROM_MASK);
		int ret;

		/*
		 * Wait for any change (intermediate or terminal) in the status register.
		 * Note, the return value is a don't care. The only failure code is timeout
		 * but the timeouts need to be accumulated over all the intermediate partial
		 * timeouts rather than allowing a huge timeout each time. So basically, need
		 * to treat a timeout no different to a value change.
		 */
		ret = xe_mmio_wait32_not(mmio, GUC_STATUS, GS_UKERNEL_MASK | GS_BOOTROM_MASK,
					 last_status, 1000 * 1000, &status, false);
		if (ret < 0)
			count++;
		after = ktime_get();
		delta = ktime_sub(after, before);
		delta_ms = ktime_to_ms(delta);

		load_done = guc_load_done(status);
		if (load_done != 0)
			break;

		if (delta_ms >= (GUC_LOAD_RETRY_LIMIT * 1000))
			break;

		xe_gt_dbg(gt, "load still in progress, timeouts = %d, freq = %dMHz (req %dMHz), status = 0x%08X [0x%02X/%02X]\n",
			  count, xe_guc_pc_get_act_freq(guc_pc),
			  guc_pc_get_cur_freq(guc_pc), status,
			  REG_FIELD_GET(GS_BOOTROM_MASK, status),
			  REG_FIELD_GET(GS_UKERNEL_MASK, status));
	} while (1);

	if (load_done != 1) {
		u32 ukernel = REG_FIELD_GET(GS_UKERNEL_MASK, status);
		u32 bootrom = REG_FIELD_GET(GS_BOOTROM_MASK, status);

		xe_gt_err(gt, "load failed: status = 0x%08X, time = %lldms, freq = %dMHz (req %dMHz), done = %d\n",
			  status, delta_ms, xe_guc_pc_get_act_freq(guc_pc),
			  guc_pc_get_cur_freq(guc_pc), load_done);
		xe_gt_err(gt, "load failed: status: Reset = %d, BootROM = 0x%02X, UKernel = 0x%02X, MIA = 0x%02X, Auth = 0x%02X\n",
			  REG_FIELD_GET(GS_MIA_IN_RESET, status),
			  bootrom, ukernel,
			  REG_FIELD_GET(GS_MIA_MASK, status),
			  REG_FIELD_GET(GS_AUTH_STATUS_MASK, status));

		switch (bootrom) {
		case XE_BOOTROM_STATUS_NO_KEY_FOUND:
			xe_gt_err(gt, "invalid key requested, header = 0x%08X\n",
				  xe_mmio_read32(mmio, GUC_HEADER_INFO));
			break;

		case XE_BOOTROM_STATUS_RSA_FAILED:
			xe_gt_err(gt, "firmware signature verification failed\n");
			break;

		case XE_BOOTROM_STATUS_PROD_KEY_CHECK_FAILURE:
			xe_gt_err(gt, "firmware production part check failure\n");
			break;
		}

		switch (ukernel) {
		case XE_GUC_LOAD_STATUS_EXCEPTION:
			xe_gt_err(gt, "firmware exception. EIP: %#x\n",
				  xe_mmio_read32(mmio, SOFT_SCRATCH(13)));
			break;

		case XE_GUC_LOAD_STATUS_INIT_MMIO_SAVE_RESTORE_INVALID:
			xe_gt_err(gt, "illegal register in save/restore workaround list\n");
			break;

		case XE_GUC_LOAD_STATUS_HWCONFIG_START:
			xe_gt_err(gt, "still extracting hwconfig table.\n");
			break;
		}

		xe_device_declare_wedged(gt_to_xe(gt));
	} else if (delta_ms > GUC_LOAD_TIME_WARN_MS) {
		xe_gt_warn(gt, "excessive init time: %lldms! [status = 0x%08X, timeouts = %d]\n",
			   delta_ms, status, count);
		xe_gt_warn(gt, "excessive init time: [freq = %dMHz (req = %dMHz), before = %dMHz, perf_limit_reasons = 0x%08X]\n",
			   xe_guc_pc_get_act_freq(guc_pc), guc_pc_get_cur_freq(guc_pc),
			   before_freq, xe_gt_throttle_get_limit_reasons(gt));
	} else {
		xe_gt_dbg(gt, "init took %lldms, freq = %dMHz (req = %dMHz), before = %dMHz, status = 0x%08X, timeouts = %d\n",
			  delta_ms, xe_guc_pc_get_act_freq(guc_pc), guc_pc_get_cur_freq(guc_pc),
			  before_freq, status, count);
	}
}

static int __xe_guc_upload(struct xe_guc *guc)
{
	int ret;

	/* Raise GT freq to speed up HuC/GuC load */
	xe_guc_pc_raise_unslice(&guc->pc);

	guc_write_params(guc);
	guc_prepare_xfer(guc);

	/*
	 * Note that GuC needs the CSS header plus uKernel code to be copied
	 * by the DMA engine in one operation, whereas the RSA signature is
	 * loaded separately, either by copying it to the UOS_RSA_SCRATCH
	 * register (if key size <= 256) or through a ggtt-pinned vma (if key
	 * size > 256). The RSA size and therefore the way we provide it to the
	 * HW is fixed for each platform and hard-coded in the bootrom.
	 */
	ret = guc_xfer_rsa(guc);
	if (ret)
		goto out;
	/*
	 * Current uCode expects the code to be loaded at 8k; locations below
	 * this are used for the stack.
	 */
	ret = xe_uc_fw_upload(&guc->fw, 0x2000, UOS_MOVE);
	if (ret)
		goto out;

	/* Wait for authentication */
	guc_wait_ucode(guc);

	xe_uc_fw_change_status(&guc->fw, XE_UC_FIRMWARE_RUNNING);
	return 0;

out:
	xe_uc_fw_change_status(&guc->fw, XE_UC_FIRMWARE_LOAD_FAIL);
	return 0	/* FIXME: ret, don't want to stop load currently */;
}

static int vf_guc_min_load_for_hwconfig(struct xe_guc *guc)
{
	struct xe_gt *gt = guc_to_gt(guc);
	int ret;

	ret = xe_gt_sriov_vf_bootstrap(gt);
	if (ret)
		return ret;

	ret = xe_gt_sriov_vf_query_config(gt);
	if (ret)
		return ret;

	ret = xe_guc_hwconfig_init(guc);
	if (ret)
		return ret;

	ret = xe_guc_enable_communication(guc);
	if (ret)
		return ret;

	ret = xe_gt_sriov_vf_connect(gt);
	if (ret)
		return ret;

	ret = xe_gt_sriov_vf_query_runtime(gt);
	if (ret)
		return ret;

	return 0;
}

/**
 * xe_guc_min_load_for_hwconfig - load minimal GuC and read hwconfig table
 * @guc: The GuC object
 *
 * This function uploads a minimal GuC that does not support submissions but
 * in a state where the hwconfig table can be read. Next, it reads and parses
 * the hwconfig table so it can be used for subsequent steps in the driver load.
 * Lastly, it enables CT communication (XXX: this is needed for PFs/VFs only).
 *
 * Return: 0 on success, negative error code on error.
 */
int xe_guc_min_load_for_hwconfig(struct xe_guc *guc)
{
	int ret;

	if (IS_SRIOV_VF(guc_to_xe(guc)))
		return vf_guc_min_load_for_hwconfig(guc);

	xe_guc_ads_populate_minimal(&guc->ads);

	xe_guc_pc_init_early(&guc->pc);

	ret = __xe_guc_upload(guc);
	if (ret)
		return ret;

	ret = xe_guc_hwconfig_init(guc);
	if (ret)
		return ret;

	ret = xe_guc_enable_communication(guc);
	if (ret)
		return ret;

	return 0;
}

int xe_guc_upload(struct xe_guc *guc)
{
	xe_guc_ads_populate(&guc->ads);

	return __xe_guc_upload(guc);
}

static void guc_handle_mmio_msg(struct xe_guc *guc)
{
	struct xe_gt *gt = guc_to_gt(guc);
	u32 msg;

	if (IS_SRIOV_VF(guc_to_xe(guc)))
		return;

	xe_force_wake_assert_held(gt_to_fw(gt), XE_FW_GT);

	msg = xe_mmio_read32(&gt->mmio, SOFT_SCRATCH(15));
	msg &= XE_GUC_RECV_MSG_EXCEPTION |
		XE_GUC_RECV_MSG_CRASH_DUMP_POSTED;
	xe_mmio_write32(&gt->mmio, SOFT_SCRATCH(15), 0);

	if (msg & XE_GUC_RECV_MSG_CRASH_DUMP_POSTED)
		xe_gt_err(gt, "Received early GuC crash dump notification!\n");

	if (msg & XE_GUC_RECV_MSG_EXCEPTION)
		xe_gt_err(gt, "Received early GuC exception notification!\n");
}

static void guc_enable_irq(struct xe_guc *guc)
{
	struct xe_gt *gt = guc_to_gt(guc);
	u32 events = xe_gt_is_media_type(gt) ?
		REG_FIELD_PREP(ENGINE0_MASK, GUC_INTR_GUC2HOST)  :
		REG_FIELD_PREP(ENGINE1_MASK, GUC_INTR_GUC2HOST);

	/* Primary GuC and media GuC share a single enable bit */
	xe_mmio_write32(&gt->mmio, GUC_SG_INTR_ENABLE,
			REG_FIELD_PREP(ENGINE1_MASK, GUC_INTR_GUC2HOST));

	/*
	 * There are separate mask bits for primary and media GuCs, so use
	 * a RMW operation to avoid clobbering the other GuC's setting.
	 */
	xe_mmio_rmw32(&gt->mmio, GUC_SG_INTR_MASK, events, 0);
}

int xe_guc_enable_communication(struct xe_guc *guc)
{
	struct xe_device *xe = guc_to_xe(guc);
	int err;

	if (IS_SRIOV_VF(xe) && xe_device_has_memirq(xe)) {
		struct xe_gt *gt = guc_to_gt(guc);
		struct xe_tile *tile = gt_to_tile(gt);

		err = xe_memirq_init_guc(&tile->memirq, guc);
		if (err)
			return err;
	} else {
		guc_enable_irq(guc);
	}

	err = xe_guc_ct_enable(&guc->ct);
	if (err)
		return err;

	guc_handle_mmio_msg(guc);

	return 0;
}

int xe_guc_suspend(struct xe_guc *guc)
{
	struct xe_gt *gt = guc_to_gt(guc);
	u32 action[] = {
		XE_GUC_ACTION_CLIENT_SOFT_RESET,
	};
	int ret;

	ret = xe_guc_mmio_send(guc, action, ARRAY_SIZE(action));
	if (ret) {
		xe_gt_err(gt, "GuC suspend failed: %pe\n", ERR_PTR(ret));
		return ret;
	}

	xe_guc_sanitize(guc);
	return 0;
}

void xe_guc_notify(struct xe_guc *guc)
{
	struct xe_gt *gt = guc_to_gt(guc);
	const u32 default_notify_data = 0;

	/*
	 * Both GUC_HOST_INTERRUPT and MED_GUC_HOST_INTERRUPT can pass
	 * additional payload data to the GuC but this capability is not
	 * used by the firmware yet. Use default value in the meantime.
	 */
	xe_mmio_write32(&gt->mmio, guc->notify_reg, default_notify_data);
}

int xe_guc_auth_huc(struct xe_guc *guc, u32 rsa_addr)
{
	u32 action[] = {
		XE_GUC_ACTION_AUTHENTICATE_HUC,
		rsa_addr
	};

	return xe_guc_ct_send_block(&guc->ct, action, ARRAY_SIZE(action));
}

int xe_guc_mmio_send_recv(struct xe_guc *guc, const u32 *request,
			  u32 len, u32 *response_buf)
{
	struct xe_device *xe = guc_to_xe(guc);
	struct xe_gt *gt = guc_to_gt(guc);
	struct xe_mmio *mmio = &gt->mmio;
	u32 header, reply;
	struct xe_reg reply_reg = xe_gt_is_media_type(gt) ?
		MED_VF_SW_FLAG(0) : VF_SW_FLAG(0);
	const u32 LAST_INDEX = VF_SW_FLAG_COUNT - 1;
	int ret;
	int i;

	BUILD_BUG_ON(VF_SW_FLAG_COUNT != MED_VF_SW_FLAG_COUNT);

	xe_assert(xe, len);
	xe_assert(xe, len <= VF_SW_FLAG_COUNT);
	xe_assert(xe, len <= MED_VF_SW_FLAG_COUNT);
	xe_assert(xe, FIELD_GET(GUC_HXG_MSG_0_ORIGIN, request[0]) ==
		  GUC_HXG_ORIGIN_HOST);
	xe_assert(xe, FIELD_GET(GUC_HXG_MSG_0_TYPE, request[0]) ==
		  GUC_HXG_TYPE_REQUEST);

retry:
	/* Not in critical data-path, just do if else for GT type */
	if (xe_gt_is_media_type(gt)) {
		for (i = 0; i < len; ++i)
			xe_mmio_write32(mmio, MED_VF_SW_FLAG(i),
					request[i]);
		xe_mmio_read32(mmio, MED_VF_SW_FLAG(LAST_INDEX));
	} else {
		for (i = 0; i < len; ++i)
			xe_mmio_write32(mmio, VF_SW_FLAG(i),
					request[i]);
		xe_mmio_read32(mmio, VF_SW_FLAG(LAST_INDEX));
	}

	xe_guc_notify(guc);

	ret = xe_mmio_wait32(mmio, reply_reg, GUC_HXG_MSG_0_ORIGIN,
			     FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_GUC),
			     50000, &reply, false);
	if (ret) {
timeout:
		xe_gt_err(gt, "GuC mmio request %#x: no reply %#x\n",
			  request[0], reply);
		return ret;
	}

	header = xe_mmio_read32(mmio, reply_reg);
	if (FIELD_GET(GUC_HXG_MSG_0_TYPE, header) ==
	    GUC_HXG_TYPE_NO_RESPONSE_BUSY) {
		/*
		 * Once we got a BUSY reply we must wait again for the final
		 * response but this time we can't use ORIGIN mask anymore.
		 * To spot a right change in the reply, we take advantage that
		 * response SUCCESS and FAILURE differ only by the single bit
		 * and all other bits are set and can be used as a new mask.
		 */
		u32 resp_bits = GUC_HXG_TYPE_RESPONSE_SUCCESS & GUC_HXG_TYPE_RESPONSE_FAILURE;
		u32 resp_mask = FIELD_PREP(GUC_HXG_MSG_0_TYPE, resp_bits);

		BUILD_BUG_ON(FIELD_MAX(GUC_HXG_MSG_0_TYPE) != GUC_HXG_TYPE_RESPONSE_SUCCESS);
		BUILD_BUG_ON((GUC_HXG_TYPE_RESPONSE_SUCCESS ^ GUC_HXG_TYPE_RESPONSE_FAILURE) != 1);

		ret = xe_mmio_wait32(mmio, reply_reg, resp_mask, resp_mask,
				     1000000, &header, false);

		if (unlikely(FIELD_GET(GUC_HXG_MSG_0_ORIGIN, header) !=
			     GUC_HXG_ORIGIN_GUC))
			goto proto;
		if (unlikely(ret)) {
			if (FIELD_GET(GUC_HXG_MSG_0_TYPE, header) !=
			    GUC_HXG_TYPE_NO_RESPONSE_BUSY)
				goto proto;
			goto timeout;
		}
	}

	if (FIELD_GET(GUC_HXG_MSG_0_TYPE, header) ==
	    GUC_HXG_TYPE_NO_RESPONSE_RETRY) {
		u32 reason = FIELD_GET(GUC_HXG_RETRY_MSG_0_REASON, header);

		xe_gt_dbg(gt, "GuC mmio request %#x: retrying, reason %#x\n",
			  request[0], reason);
		goto retry;
	}

	if (FIELD_GET(GUC_HXG_MSG_0_TYPE, header) ==
	    GUC_HXG_TYPE_RESPONSE_FAILURE) {
		u32 hint = FIELD_GET(GUC_HXG_FAILURE_MSG_0_HINT, header);
		u32 error = FIELD_GET(GUC_HXG_FAILURE_MSG_0_ERROR, header);

		xe_gt_err(gt, "GuC mmio request %#x: failure %#x hint %#x\n",
			  request[0], error, hint);
		return -ENXIO;
	}

	if (FIELD_GET(GUC_HXG_MSG_0_TYPE, header) !=
	    GUC_HXG_TYPE_RESPONSE_SUCCESS) {
proto:
		xe_gt_err(gt, "GuC mmio request %#x: unexpected reply %#x\n",
			  request[0], header);
		return -EPROTO;
	}

	/* Just copy entire possible message response */
	if (response_buf) {
		response_buf[0] = header;

		for (i = 1; i < VF_SW_FLAG_COUNT; i++) {
			reply_reg.addr += sizeof(u32);
			response_buf[i] = xe_mmio_read32(mmio, reply_reg);
		}
	}

	/* Use data from the GuC response as our return value */
	return FIELD_GET(GUC_HXG_RESPONSE_MSG_0_DATA0, header);
}

int xe_guc_mmio_send(struct xe_guc *guc, const u32 *request, u32 len)
{
	return xe_guc_mmio_send_recv(guc, request, len, NULL);
}

static int guc_self_cfg(struct xe_guc *guc, u16 key, u16 len, u64 val)
{
	struct xe_device *xe = guc_to_xe(guc);
	u32 request[HOST2GUC_SELF_CFG_REQUEST_MSG_LEN] = {
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
		FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION,
			   GUC_ACTION_HOST2GUC_SELF_CFG),
		FIELD_PREP(HOST2GUC_SELF_CFG_REQUEST_MSG_1_KLV_KEY, key) |
		FIELD_PREP(HOST2GUC_SELF_CFG_REQUEST_MSG_1_KLV_LEN, len),
		FIELD_PREP(HOST2GUC_SELF_CFG_REQUEST_MSG_2_VALUE32,
			   lower_32_bits(val)),
		FIELD_PREP(HOST2GUC_SELF_CFG_REQUEST_MSG_3_VALUE64,
			   upper_32_bits(val)),
	};
	int ret;

	xe_assert(xe, len <= 2);
	xe_assert(xe, len != 1 || !upper_32_bits(val));

	/* Self config must go over MMIO */
	ret = xe_guc_mmio_send(guc, request, ARRAY_SIZE(request));

	if (unlikely(ret < 0))
		return ret;
	if (unlikely(ret > 1))
		return -EPROTO;
	if (unlikely(!ret))
		return -ENOKEY;

	return 0;
}

int xe_guc_self_cfg32(struct xe_guc *guc, u16 key, u32 val)
{
	return guc_self_cfg(guc, key, 1, val);
}

int xe_guc_self_cfg64(struct xe_guc *guc, u16 key, u64 val)
{
	return guc_self_cfg(guc, key, 2, val);
}

static void xe_guc_sw_0_irq_handler(struct xe_guc *guc)
{
	struct xe_gt *gt = guc_to_gt(guc);

	if (IS_SRIOV_VF(gt_to_xe(gt)))
		xe_gt_sriov_vf_migrated_event_handler(gt);
}

void xe_guc_irq_handler(struct xe_guc *guc, const u16 iir)
{
	if (iir & GUC_INTR_GUC2HOST)
		xe_guc_ct_irq_handler(&guc->ct);

	if (iir & GUC_INTR_SW_INT_0)
		xe_guc_sw_0_irq_handler(guc);
}

void xe_guc_sanitize(struct xe_guc *guc)
{
	xe_uc_fw_sanitize(&guc->fw);
	xe_guc_ct_disable(&guc->ct);
	guc->submission_state.enabled = false;
}

int xe_guc_reset_prepare(struct xe_guc *guc)
{
	return xe_guc_submit_reset_prepare(guc);
}

void xe_guc_reset_wait(struct xe_guc *guc)
{
	xe_guc_submit_reset_wait(guc);
}

void xe_guc_stop_prepare(struct xe_guc *guc)
{
	if (!IS_SRIOV_VF(guc_to_xe(guc))) {
		int err;

		err = xe_guc_pc_stop(&guc->pc);
		xe_gt_WARN(guc_to_gt(guc), err, "Failed to stop GuC PC: %pe\n",
			   ERR_PTR(err));
	}
}

void xe_guc_stop(struct xe_guc *guc)
{
	xe_guc_ct_stop(&guc->ct);

	xe_guc_submit_stop(guc);
}

int xe_guc_start(struct xe_guc *guc)
{
	if (!IS_SRIOV_VF(guc_to_xe(guc))) {
		int err;

		err = xe_guc_pc_start(&guc->pc);
		xe_gt_WARN(guc_to_gt(guc), err, "Failed to start GuC PC: %pe\n",
			   ERR_PTR(err));
	}

	return xe_guc_submit_start(guc);
}

void xe_guc_print_info(struct xe_guc *guc, struct drm_printer *p)
{
	struct xe_gt *gt = guc_to_gt(guc);
	unsigned int fw_ref;
	u32 status;
	int i;

	xe_uc_fw_print(&guc->fw, p);

	fw_ref = xe_force_wake_get(gt_to_fw(gt), XE_FW_GT);
	if (!fw_ref)
		return;

	status = xe_mmio_read32(&gt->mmio, GUC_STATUS);

	drm_printf(p, "\nGuC status 0x%08x:\n", status);
	drm_printf(p, "\tBootrom status = 0x%x\n",
		   REG_FIELD_GET(GS_BOOTROM_MASK, status));
	drm_printf(p, "\tuKernel status = 0x%x\n",
		   REG_FIELD_GET(GS_UKERNEL_MASK, status));
	drm_printf(p, "\tMIA Core status = 0x%x\n",
		   REG_FIELD_GET(GS_MIA_MASK, status));
	drm_printf(p, "\tLog level = %d\n",
		   xe_guc_log_get_level(&guc->log));

	drm_puts(p, "\nScratch registers:\n");
	for (i = 0; i < SOFT_SCRATCH_COUNT; i++) {
		drm_printf(p, "\t%2d: \t0x%x\n",
			   i, xe_mmio_read32(&gt->mmio, SOFT_SCRATCH(i)));
	}

	xe_force_wake_put(gt_to_fw(gt), fw_ref);

	drm_puts(p, "\n");
	xe_guc_ct_print(&guc->ct, p, false);

	drm_puts(p, "\n");
	xe_guc_submit_print(guc, p);
}

/**
 * xe_guc_declare_wedged() - Declare GuC wedged
 * @guc: the GuC object
 *
 * Wedge the GuC which stops all submission, saves desired debug state, and
 * cleans up anything which could timeout.
 */
void xe_guc_declare_wedged(struct xe_guc *guc)
{
	xe_gt_assert(guc_to_gt(guc), guc_to_xe(guc)->wedged.mode);

	xe_guc_reset_prepare(guc);
	xe_guc_ct_stop(&guc->ct);
	xe_guc_submit_wedge(guc);
}
