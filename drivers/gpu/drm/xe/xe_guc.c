// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_guc.h"

#include "regs/xe_gt_regs.h"
#include "regs/xe_guc_regs.h"
#include "xe_bo.h"
#include "xe_device.h"
#include "xe_force_wake.h"
#include "xe_gt.h"
#include "xe_guc_ads.h"
#include "xe_guc_ct.h"
#include "xe_guc_hwconfig.h"
#include "xe_guc_log.h"
#include "xe_guc_pc.h"
#include "xe_guc_submit.h"
#include "xe_mmio.h"
#include "xe_platform_types.h"
#include "xe_uc_fw.h"
#include "xe_wopcm.h"

static struct xe_gt *
guc_to_gt(struct xe_guc *guc)
{
	return container_of(guc, struct xe_gt, uc.guc);
}

static struct xe_device *
guc_to_xe(struct xe_guc *guc)
{
	return gt_to_xe(guc_to_gt(guc));
}

/* GuC addresses above GUC_GGTT_TOP also don't map through the GTT */
#define GUC_GGTT_TOP    0xFEE00000
static u32 guc_bo_ggtt_addr(struct xe_guc *guc,
			    struct xe_bo *bo)
{
	u32 addr = xe_bo_ggtt_addr(bo);

	XE_BUG_ON(addr < xe_wopcm_size(guc_to_xe(guc)));
	XE_BUG_ON(addr >= GUC_GGTT_TOP);
	XE_BUG_ON(bo->size > GUC_GGTT_TOP - addr);

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
	return GUC_CTL_ENABLE_SLPC;
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

	/* Wa_22012773006:gen11,gen12 < XeHP */
	if (GRAPHICS_VER(xe) >= 11 &&
	    GRAPHICS_VERx100(xe) < 1250)
		flags |= GUC_WA_POLLCS;

	/* Wa_16011759253 */
	/* Wa_22011383443 */
	if (IS_SUBPLATFORM_STEP(xe, XE_DG2, XE_SUBPLATFORM_DG2_G10, STEP_A0, STEP_B0) ||
	    IS_PLATFORM_STEP(xe, XE_PVC, STEP_A0, STEP_B0))
		flags |= GUC_WA_GAM_CREDITS;

	/* Wa_14014475959 */
	if (IS_PLATFORM_STEP(xe, XE_METEORLAKE, STEP_A0, STEP_B0) ||
	    xe->info.platform == XE_DG2)
		flags |= GUC_WA_HOLD_CCS_SWITCHOUT;

	/*
	 * Wa_14012197797
	 * Wa_22011391025
	 *
	 * The same WA bit is used for both and 22011391025 is applicable to
	 * all DG2.
	 */
	if (xe->info.platform == XE_DG2)
		flags |= GUC_WA_DUAL_QUEUE;

	/*
	 * Wa_2201180203
	 * GUC_WA_PRE_PARSER causes media workload hang for PVC A0 and PCIe
	 * errors. Disable this for PVC A0 steppings.
	 */
	if (GRAPHICS_VER(xe) <= 12 &&
	    !IS_PLATFORM_STEP(xe, XE_PVC, STEP_A0, STEP_B0))
		flags |= GUC_WA_PRE_PARSER;

	/* Wa_16011777198 */
	if (IS_SUBPLATFORM_STEP(xe, XE_DG2, XE_SUBPLATFORM_DG2_G10, STEP_A0, STEP_C0) ||
	    IS_SUBPLATFORM_STEP(xe, XE_DG2, XE_SUBPLATFORM_DG2_G11, STEP_A0,
				STEP_B0))
		flags |= GUC_WA_RCS_RESET_BEFORE_RC6;

	/*
	 * Wa_22012727170
	 * Wa_22012727685
	 *
	 * This WA is applicable to PVC CT A0, but causes media regressions. 
	 * Drop the WA for PVC.
	 */
	if (IS_SUBPLATFORM_STEP(xe, XE_DG2, XE_SUBPLATFORM_DG2_G10, STEP_A0, STEP_C0) ||
	    IS_SUBPLATFORM_STEP(xe, XE_DG2, XE_SUBPLATFORM_DG2_G11, STEP_A0,
				STEP_FOREVER))
		flags |= GUC_WA_CONTEXT_ISOLATION;

	/* Wa_16015675438, Wa_18020744125 */
	if (!xe_hw_engine_mask_per_class(gt, XE_ENGINE_CLASS_RENDER))
		flags |= GUC_WA_RCS_REGS_IN_CCS_REGS_LIST;

	/* Wa_1509372804 */
	if (IS_PLATFORM_STEP(xe, XE_PVC, STEP_A0, STEP_C0))
		flags |= GUC_WA_RENDER_RST_RC6_EXIT;


	return flags;
}

static u32 guc_ctl_devid(struct xe_guc *guc)
{
	struct xe_device *xe = guc_to_xe(guc);

	return (((u32)xe->info.devid) << 16) | xe->info.revid;
}

static void guc_init_params(struct xe_guc *guc)
{
	struct xe_device *xe = guc_to_xe(guc);
	u32 *params = guc->params;
	int i;

	BUILD_BUG_ON(sizeof(guc->params) != GUC_CTL_MAX_DWORDS * sizeof(u32));
	BUILD_BUG_ON(SOFT_SCRATCH_COUNT != GUC_CTL_MAX_DWORDS + 2);

	params[GUC_CTL_LOG_PARAMS] = guc_ctl_log_params_flags(guc);
	params[GUC_CTL_FEATURE] = guc_ctl_feature_flags(guc);
	params[GUC_CTL_DEBUG] = guc_ctl_debug_flags(guc);
	params[GUC_CTL_ADS] = guc_ctl_ads_flags(guc);
	params[GUC_CTL_WA] = guc_ctl_wa_flags(guc);
	params[GUC_CTL_DEVID] = guc_ctl_devid(guc);

	for (i = 0; i < GUC_CTL_MAX_DWORDS; i++)
		drm_dbg(&xe->drm, "GuC param[%2d] = 0x%08x\n", i, params[i]);
}

/*
 * Initialise the GuC parameter block before starting the firmware
 * transfer. These parameters are read by the firmware on startup
 * and cannot be changed thereafter.
 */
static void guc_write_params(struct xe_guc *guc)
{
	struct xe_gt *gt = guc_to_gt(guc);
	int i;

	xe_force_wake_assert_held(gt_to_fw(gt), XE_FW_GT);

	xe_mmio_write32(gt, SOFT_SCRATCH(0).reg, 0);

	for (i = 0; i < GUC_CTL_MAX_DWORDS; i++)
		xe_mmio_write32(gt, SOFT_SCRATCH(1 + i).reg, guc->params[i]);
}

#define MEDIA_GUC_HOST_INTERRUPT        _MMIO(0x190304)

int xe_guc_init(struct xe_guc *guc)
{
	struct xe_device *xe = guc_to_xe(guc);
	struct xe_gt *gt = guc_to_gt(guc);
	int ret;

	guc->fw.type = XE_UC_FW_TYPE_GUC;
	ret = xe_uc_fw_init(&guc->fw);
	if (ret)
		goto out;

	ret = xe_guc_log_init(&guc->log);
	if (ret)
		goto out;

	ret = xe_guc_ads_init(&guc->ads);
	if (ret)
		goto out;

	ret = xe_guc_ct_init(&guc->ct);
	if (ret)
		goto out;

	ret = xe_guc_pc_init(&guc->pc);
	if (ret)
		goto out;

	guc_init_params(guc);

	if (xe_gt_is_media_type(gt))
		guc->notify_reg = MEDIA_GUC_HOST_INTERRUPT.reg;
	else
		guc->notify_reg = GEN11_GUC_HOST_INTERRUPT.reg;

	xe_uc_fw_change_status(&guc->fw, XE_UC_FIRMWARE_LOADABLE);

	return 0;

out:
	drm_err(&xe->drm, "GuC init failed with %d", ret);
	return ret;
}

/**
 * xe_guc_init_post_hwconfig - initialize GuC post hwconfig load
 * @guc: The GuC object
 *
 * Return: 0 on success, negative error code on error.
 */
int xe_guc_init_post_hwconfig(struct xe_guc *guc)
{
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
	struct xe_device *xe = guc_to_xe(guc);
	struct xe_gt *gt = guc_to_gt(guc);
	u32 guc_status, gdrst;
	int ret;

	xe_force_wake_assert_held(gt_to_fw(gt), XE_FW_GT);

	xe_mmio_write32(gt, GEN6_GDRST.reg, GEN11_GRDOM_GUC);

	ret = xe_mmio_wait32(gt, GEN6_GDRST.reg, 0, GEN11_GRDOM_GUC, 5000,
			     &gdrst, false);
	if (ret) {
		drm_err(&xe->drm, "GuC reset timed out, GEN6_GDRST=0x%8x\n",
			gdrst);
		goto err_out;
	}

	guc_status = xe_mmio_read32(gt, GUC_STATUS.reg);
	if (!(guc_status & GS_MIA_IN_RESET)) {
		drm_err(&xe->drm,
			"GuC status: 0x%x, MIA core expected to be in reset\n",
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
	struct xe_device *xe =  guc_to_xe(guc);
	u32 shim_flags = GUC_ENABLE_READ_CACHE_LOGIC |
		GUC_ENABLE_READ_CACHE_FOR_SRAM_DATA |
		GUC_ENABLE_READ_CACHE_FOR_WOPCM_DATA |
		GUC_ENABLE_MIA_CLOCK_GATING;

	if (GRAPHICS_VERx100(xe) < 1250)
		shim_flags |= GUC_DISABLE_SRAM_INIT_TO_ZEROES |
				GUC_ENABLE_MIA_CACHING;

	if (xe->info.platform == XE_PVC)
		shim_flags |= PVC_GUC_MOCS_INDEX(PVC_MOCS_UC_INDEX);

	/* Must program this register before loading the ucode with DMA */
	xe_mmio_write32(gt, GUC_SHIM_CONTROL.reg, shim_flags);

	xe_mmio_write32(gt, GEN9_GT_PM_CONFIG.reg, GT_DOORBELL_ENABLE);
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
		xe_mmio_write32(gt, UOS_RSA_SCRATCH(0).reg, rsa_ggtt_addr);
		return 0;
	}

	copied = xe_uc_fw_copy_rsa(&guc->fw, rsa, sizeof(rsa));
	if (copied < sizeof(rsa))
		return -ENOMEM;

	for (i = 0; i < UOS_RSA_SCRATCH_COUNT; i++)
		xe_mmio_write32(gt, UOS_RSA_SCRATCH(i).reg, rsa[i]);

	return 0;
}

static int guc_wait_ucode(struct xe_guc *guc)
{
	struct xe_device *xe = guc_to_xe(guc);
	u32 status;
	int ret;

	/*
	 * Wait for the GuC to start up.
	 * NB: Docs recommend not using the interrupt for completion.
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
	 */
	ret = xe_mmio_wait32(guc_to_gt(guc), GUC_STATUS.reg,
			     FIELD_PREP(GS_UKERNEL_MASK,
					XE_GUC_LOAD_STATUS_READY),
			     GS_UKERNEL_MASK, 200000, &status, false);

	if (ret) {
		struct drm_device *drm = &xe->drm;
		struct drm_printer p = drm_info_printer(drm->dev);

		drm_info(drm, "GuC load failed: status = 0x%08X\n", status);
		drm_info(drm, "GuC load failed: status: Reset = %d, "
			"BootROM = 0x%02X, UKernel = 0x%02X, "
			"MIA = 0x%02X, Auth = 0x%02X\n",
			REG_FIELD_GET(GS_MIA_IN_RESET, status),
			REG_FIELD_GET(GS_BOOTROM_MASK, status),
			REG_FIELD_GET(GS_UKERNEL_MASK, status),
			REG_FIELD_GET(GS_MIA_MASK, status),
			REG_FIELD_GET(GS_AUTH_STATUS_MASK, status));

		if ((status & GS_BOOTROM_MASK) == GS_BOOTROM_RSA_FAILED) {
			drm_info(drm, "GuC firmware signature verification failed\n");
			ret = -ENOEXEC;
		}

		if (REG_FIELD_GET(GS_UKERNEL_MASK, status) ==
		    XE_GUC_LOAD_STATUS_EXCEPTION) {
			drm_info(drm, "GuC firmware exception. EIP: %#x\n",
				 xe_mmio_read32(guc_to_gt(guc),
						SOFT_SCRATCH(13).reg));
			ret = -ENXIO;
		}

		xe_guc_log_print(&guc->log, &p);
	} else {
		drm_dbg(&xe->drm, "GuC successfully loaded");
	}

	return ret;
}

static int __xe_guc_upload(struct xe_guc *guc)
{
	int ret;

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
	ret = guc_wait_ucode(guc);
	if (ret)
		goto out;

	xe_uc_fw_change_status(&guc->fw, XE_UC_FIRMWARE_RUNNING);
	return 0;

out:
	xe_uc_fw_change_status(&guc->fw, XE_UC_FIRMWARE_LOAD_FAIL);
	return 0	/* FIXME: ret, don't want to stop load currently */;
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

	xe_guc_ads_populate_minimal(&guc->ads);

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

	xe_force_wake_assert_held(gt_to_fw(gt), XE_FW_GT);

	msg = xe_mmio_read32(gt, SOFT_SCRATCH(15).reg);
	msg &= XE_GUC_RECV_MSG_EXCEPTION |
		XE_GUC_RECV_MSG_CRASH_DUMP_POSTED;
	xe_mmio_write32(gt, SOFT_SCRATCH(15).reg, 0);

	if (msg & XE_GUC_RECV_MSG_CRASH_DUMP_POSTED)
		drm_err(&guc_to_xe(guc)->drm,
			"Received early GuC crash dump notification!\n");

	if (msg & XE_GUC_RECV_MSG_EXCEPTION)
		drm_err(&guc_to_xe(guc)->drm,
			"Received early GuC exception notification!\n");
}

static void guc_enable_irq(struct xe_guc *guc)
{
	struct xe_gt *gt = guc_to_gt(guc);
	u32 events = xe_gt_is_media_type(gt) ?
		REG_FIELD_PREP(ENGINE0_MASK, GUC_INTR_GUC2HOST)  :
		REG_FIELD_PREP(ENGINE1_MASK, GUC_INTR_GUC2HOST);

	xe_mmio_write32(gt, GUC_SG_INTR_ENABLE.reg,
			REG_FIELD_PREP(ENGINE1_MASK, GUC_INTR_GUC2HOST));
	if (xe_gt_is_media_type(gt))
		xe_mmio_rmw32(gt, GUC_SG_INTR_MASK.reg, events, 0);
	else
		xe_mmio_write32(gt, GUC_SG_INTR_MASK.reg, ~events);
}

int xe_guc_enable_communication(struct xe_guc *guc)
{
	int err;

	guc_enable_irq(guc);

	xe_mmio_rmw32(guc_to_gt(guc), GEN6_PMINTRMSK.reg,
		      ARAT_EXPIRED_INTRMSK, 0);

	err = xe_guc_ct_enable(&guc->ct);
	if (err)
		return err;

	guc_handle_mmio_msg(guc);

	return 0;
}

int xe_guc_suspend(struct xe_guc *guc)
{
	int ret;
	u32 action[] = {
		XE_GUC_ACTION_CLIENT_SOFT_RESET,
	};

	ret = xe_guc_mmio_send(guc, action, ARRAY_SIZE(action));
	if (ret) {
		drm_err(&guc_to_xe(guc)->drm,
			"GuC suspend: CLIENT_SOFT_RESET fail: %d!\n", ret);
		return ret;
	}

	xe_guc_sanitize(guc);
	return 0;
}

void xe_guc_notify(struct xe_guc *guc)
{
	struct xe_gt *gt = guc_to_gt(guc);

	xe_mmio_write32(gt, guc->notify_reg, GUC_SEND_TRIGGER);
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
	u32 header, reply;
	u32 reply_reg = xe_gt_is_media_type(gt) ?
		MED_VF_SW_FLAG(0).reg : VF_SW_FLAG(0).reg;
	const u32 LAST_INDEX = VF_SW_FLAG_COUNT;
	int ret;
	int i;

	BUILD_BUG_ON(VF_SW_FLAG_COUNT != MED_VF_SW_FLAG_COUNT);

	XE_BUG_ON(guc->ct.enabled);
	XE_BUG_ON(!len);
	XE_BUG_ON(len > VF_SW_FLAG_COUNT);
	XE_BUG_ON(len > MED_VF_SW_FLAG_COUNT);
	XE_BUG_ON(FIELD_GET(GUC_HXG_MSG_0_ORIGIN, request[0]) !=
		  GUC_HXG_ORIGIN_HOST);
	XE_BUG_ON(FIELD_GET(GUC_HXG_MSG_0_TYPE, request[0]) !=
		  GUC_HXG_TYPE_REQUEST);

retry:
	/* Not in critical data-path, just do if else for GT type */
	if (xe_gt_is_media_type(gt)) {
		for (i = 0; i < len; ++i)
			xe_mmio_write32(gt, MED_VF_SW_FLAG(i).reg,
					request[i]);
		xe_mmio_read32(gt, MED_VF_SW_FLAG(LAST_INDEX).reg);
	} else {
		for (i = 0; i < len; ++i)
			xe_mmio_write32(gt, VF_SW_FLAG(i).reg,
					request[i]);
		xe_mmio_read32(gt, VF_SW_FLAG(LAST_INDEX).reg);
	}

	xe_guc_notify(guc);

	ret = xe_mmio_wait32(gt, reply_reg,
			     FIELD_PREP(GUC_HXG_MSG_0_ORIGIN,
					GUC_HXG_ORIGIN_GUC),
			     GUC_HXG_MSG_0_ORIGIN, 50000, &reply, false);
	if (ret) {
timeout:
		drm_err(&xe->drm, "mmio request 0x%08x: no reply 0x%08x\n",
			request[0], reply);
		return ret;
	}

	header = xe_mmio_read32(gt, reply_reg);
	if (FIELD_GET(GUC_HXG_MSG_0_TYPE, header) ==
	    GUC_HXG_TYPE_NO_RESPONSE_BUSY) {

		ret = xe_mmio_wait32(gt, reply_reg,
				     FIELD_PREP(GUC_HXG_MSG_0_TYPE,
						GUC_HXG_TYPE_RESPONSE_SUCCESS),
				     GUC_HXG_MSG_0_TYPE, 1000000, &header,
				     false);

		if (unlikely(FIELD_GET(GUC_HXG_MSG_0_ORIGIN, header) !=
			     GUC_HXG_ORIGIN_GUC))
			goto proto;
		if (unlikely(ret))
			goto timeout;
	}

	if (FIELD_GET(GUC_HXG_MSG_0_TYPE, header) ==
	    GUC_HXG_TYPE_NO_RESPONSE_RETRY) {
		u32 reason = FIELD_GET(GUC_HXG_RETRY_MSG_0_REASON, header);

		drm_dbg(&xe->drm, "mmio request %#x: retrying, reason %u\n",
			request[0], reason);
		goto retry;
	}

	if (FIELD_GET(GUC_HXG_MSG_0_TYPE, header) ==
	    GUC_HXG_TYPE_RESPONSE_FAILURE) {
		u32 hint = FIELD_GET(GUC_HXG_FAILURE_MSG_0_HINT, header);
		u32 error = FIELD_GET(GUC_HXG_FAILURE_MSG_0_ERROR, header);

		drm_err(&xe->drm, "mmio request %#x: failure %x/%u\n",
			request[0], error, hint);
		return -ENXIO;
	}

	if (FIELD_GET(GUC_HXG_MSG_0_TYPE, header) !=
	    GUC_HXG_TYPE_RESPONSE_SUCCESS) {
proto:
		drm_err(&xe->drm, "mmio request %#x: unexpected reply %#x\n",
			request[0], header);
		return -EPROTO;
	}

	/* Just copy entire possible message response */
	if (response_buf) {
		response_buf[0] = header;

		for (i = 1; i < VF_SW_FLAG_COUNT; i++)
			response_buf[i] =
				xe_mmio_read32(gt, reply_reg + i * sizeof(u32));
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

	XE_BUG_ON(len > 2);
	XE_BUG_ON(len == 1 && upper_32_bits(val));

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

void xe_guc_irq_handler(struct xe_guc *guc, const u16 iir)
{
	if (iir & GUC_INTR_GUC2HOST)
		xe_guc_ct_irq_handler(&guc->ct);
}

void xe_guc_sanitize(struct xe_guc *guc)
{
	xe_uc_fw_change_status(&guc->fw, XE_UC_FIRMWARE_LOADABLE);
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
	XE_WARN_ON(xe_guc_pc_stop(&guc->pc));
}

int xe_guc_stop(struct xe_guc *guc)
{
	int ret;

	xe_guc_ct_disable(&guc->ct);

	ret = xe_guc_submit_stop(guc);
	if (ret)
		return ret;

	return 0;
}

int xe_guc_start(struct xe_guc *guc)
{
	int ret;

	ret = xe_guc_submit_start(guc);
	if (ret)
		return ret;

	ret = xe_guc_pc_start(&guc->pc);
	XE_WARN_ON(ret);

	return 0;
}

void xe_guc_print_info(struct xe_guc *guc, struct drm_printer *p)
{
	struct xe_gt *gt = guc_to_gt(guc);
	u32 status;
	int err;
	int i;

	xe_uc_fw_print(&guc->fw, p);

	err = xe_force_wake_get(gt_to_fw(gt), XE_FW_GT);
	if (err)
		return;

	status = xe_mmio_read32(gt, GUC_STATUS.reg);

	drm_printf(p, "\nGuC status 0x%08x:\n", status);
	drm_printf(p, "\tBootrom status = 0x%x\n",
		   (status & GS_BOOTROM_MASK) >> GS_BOOTROM_SHIFT);
	drm_printf(p, "\tuKernel status = 0x%x\n",
		   (status & GS_UKERNEL_MASK) >> GS_UKERNEL_SHIFT);
	drm_printf(p, "\tMIA Core status = 0x%x\n",
		   (status & GS_MIA_MASK) >> GS_MIA_SHIFT);
	drm_printf(p, "\tLog level = %d\n",
		   xe_guc_log_get_level(&guc->log));

	drm_puts(p, "\nScratch registers:\n");
	for (i = 0; i < SOFT_SCRATCH_COUNT; i++) {
		drm_printf(p, "\t%2d: \t0x%x\n",
			   i, xe_mmio_read32(gt, SOFT_SCRATCH(i).reg));
	}

	xe_force_wake_put(gt_to_fw(gt), XE_FW_GT);

	xe_guc_ct_print(&guc->ct, p);
	xe_guc_submit_print(guc, p);
}
