// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2023 Intel Corporation
 */

#include "ivpu_drv.h"
#include "ivpu_fw.h"
#include "ivpu_hw_mtl_reg.h"
#include "ivpu_hw_reg_io.h"
#include "ivpu_hw.h"
#include "ivpu_ipc.h"
#include "ivpu_mmu.h"
#include "ivpu_pm.h"

#define TILE_FUSE_ENABLE_BOTH        0x0
#define TILE_SKU_BOTH_MTL            0x3630

/* Work point configuration values */
#define CONFIG_1_TILE                0x01
#define CONFIG_2_TILE                0x02
#define PLL_RATIO_5_3                0x01
#define PLL_RATIO_4_3                0x02
#define WP_CONFIG(tile, ratio)       (((tile) << 8) | (ratio))
#define WP_CONFIG_1_TILE_5_3_RATIO   WP_CONFIG(CONFIG_1_TILE, PLL_RATIO_5_3)
#define WP_CONFIG_1_TILE_4_3_RATIO   WP_CONFIG(CONFIG_1_TILE, PLL_RATIO_4_3)
#define WP_CONFIG_2_TILE_5_3_RATIO   WP_CONFIG(CONFIG_2_TILE, PLL_RATIO_5_3)
#define WP_CONFIG_2_TILE_4_3_RATIO   WP_CONFIG(CONFIG_2_TILE, PLL_RATIO_4_3)
#define WP_CONFIG_0_TILE_PLL_OFF     WP_CONFIG(0, 0)

#define PLL_REF_CLK_FREQ	     (50 * 1000000)
#define PLL_SIMULATION_FREQ	     (10 * 1000000)
#define PLL_DEFAULT_EPP_VALUE	     0x80

#define TIM_SAFE_ENABLE		     0xf1d0dead
#define TIM_WATCHDOG_RESET_VALUE     0xffffffff

#define TIMEOUT_US		     (150 * USEC_PER_MSEC)
#define PWR_ISLAND_STATUS_TIMEOUT_US (5 * USEC_PER_MSEC)
#define PLL_TIMEOUT_US		     (1500 * USEC_PER_MSEC)
#define IDLE_TIMEOUT_US		     (500 * USEC_PER_MSEC)

#define ICB_0_IRQ_MASK ((REG_FLD(MTL_VPU_HOST_SS_ICB_STATUS_0, HOST_IPC_FIFO_INT)) | \
			(REG_FLD(MTL_VPU_HOST_SS_ICB_STATUS_0, MMU_IRQ_0_INT)) | \
			(REG_FLD(MTL_VPU_HOST_SS_ICB_STATUS_0, MMU_IRQ_1_INT)) | \
			(REG_FLD(MTL_VPU_HOST_SS_ICB_STATUS_0, MMU_IRQ_2_INT)) | \
			(REG_FLD(MTL_VPU_HOST_SS_ICB_STATUS_0, NOC_FIREWALL_INT)) | \
			(REG_FLD(MTL_VPU_HOST_SS_ICB_STATUS_0, CPU_INT_REDIRECT_0_INT)) | \
			(REG_FLD(MTL_VPU_HOST_SS_ICB_STATUS_0, CPU_INT_REDIRECT_1_INT)))

#define ICB_1_IRQ_MASK ((REG_FLD(MTL_VPU_HOST_SS_ICB_STATUS_1, CPU_INT_REDIRECT_2_INT)) | \
			(REG_FLD(MTL_VPU_HOST_SS_ICB_STATUS_1, CPU_INT_REDIRECT_3_INT)) | \
			(REG_FLD(MTL_VPU_HOST_SS_ICB_STATUS_1, CPU_INT_REDIRECT_4_INT)))

#define ICB_0_1_IRQ_MASK ((((u64)ICB_1_IRQ_MASK) << 32) | ICB_0_IRQ_MASK)

#define BUTTRESS_IRQ_MASK ((REG_FLD(MTL_BUTTRESS_INTERRUPT_STAT, FREQ_CHANGE)) | \
			   (REG_FLD(MTL_BUTTRESS_INTERRUPT_STAT, ATS_ERR)) | \
			   (REG_FLD(MTL_BUTTRESS_INTERRUPT_STAT, UFI_ERR)))

#define BUTTRESS_IRQ_ENABLE_MASK ((u32)~BUTTRESS_IRQ_MASK)
#define BUTTRESS_IRQ_DISABLE_MASK ((u32)-1)

#define ITF_FIREWALL_VIOLATION_MASK ((REG_FLD(MTL_VPU_HOST_SS_FW_SOC_IRQ_EN, CSS_ROM_CMX)) | \
				     (REG_FLD(MTL_VPU_HOST_SS_FW_SOC_IRQ_EN, CSS_DBG)) | \
				     (REG_FLD(MTL_VPU_HOST_SS_FW_SOC_IRQ_EN, CSS_CTRL)) | \
				     (REG_FLD(MTL_VPU_HOST_SS_FW_SOC_IRQ_EN, DEC400)) | \
				     (REG_FLD(MTL_VPU_HOST_SS_FW_SOC_IRQ_EN, MSS_NCE)) | \
				     (REG_FLD(MTL_VPU_HOST_SS_FW_SOC_IRQ_EN, MSS_MBI)) | \
				     (REG_FLD(MTL_VPU_HOST_SS_FW_SOC_IRQ_EN, MSS_MBI_CMX)))

static char *ivpu_platform_to_str(u32 platform)
{
	switch (platform) {
	case IVPU_PLATFORM_SILICON:
		return "IVPU_PLATFORM_SILICON";
	case IVPU_PLATFORM_SIMICS:
		return "IVPU_PLATFORM_SIMICS";
	case IVPU_PLATFORM_FPGA:
		return "IVPU_PLATFORM_FPGA";
	default:
		return "Invalid platform";
	}
}

static void ivpu_hw_read_platform(struct ivpu_device *vdev)
{
	u32 gen_ctrl = REGV_RD32(MTL_VPU_HOST_SS_GEN_CTRL);
	u32 platform = REG_GET_FLD(MTL_VPU_HOST_SS_GEN_CTRL, PS, gen_ctrl);

	if  (platform == IVPU_PLATFORM_SIMICS || platform == IVPU_PLATFORM_FPGA)
		vdev->platform = platform;
	else
		vdev->platform = IVPU_PLATFORM_SILICON;

	ivpu_dbg(vdev, MISC, "Platform type: %s (%d)\n",
		 ivpu_platform_to_str(vdev->platform), vdev->platform);
}

static void ivpu_hw_wa_init(struct ivpu_device *vdev)
{
	vdev->wa.punit_disabled = ivpu_is_fpga(vdev);
	vdev->wa.clear_runtime_mem = false;
	vdev->wa.d3hot_after_power_off = true;
}

static void ivpu_hw_timeouts_init(struct ivpu_device *vdev)
{
	if (ivpu_is_simics(vdev) || ivpu_is_fpga(vdev)) {
		vdev->timeout.boot = 100000;
		vdev->timeout.jsm = 50000;
		vdev->timeout.tdr = 2000000;
		vdev->timeout.reschedule_suspend = 1000;
	} else {
		vdev->timeout.boot = 1000;
		vdev->timeout.jsm = 500;
		vdev->timeout.tdr = 2000;
		vdev->timeout.reschedule_suspend = 10;
	}
}

static int ivpu_pll_wait_for_cmd_send(struct ivpu_device *vdev)
{
	return REGB_POLL_FLD(MTL_BUTTRESS_WP_REQ_CMD, SEND, 0, PLL_TIMEOUT_US);
}

/* Send KMD initiated workpoint change */
static int ivpu_pll_cmd_send(struct ivpu_device *vdev, u16 min_ratio, u16 max_ratio,
			     u16 target_ratio, u16 config)
{
	int ret;
	u32 val;

	ret = ivpu_pll_wait_for_cmd_send(vdev);
	if (ret) {
		ivpu_err(vdev, "Failed to sync before WP request: %d\n", ret);
		return ret;
	}

	val = REGB_RD32(MTL_BUTTRESS_WP_REQ_PAYLOAD0);
	val = REG_SET_FLD_NUM(MTL_BUTTRESS_WP_REQ_PAYLOAD0, MIN_RATIO, min_ratio, val);
	val = REG_SET_FLD_NUM(MTL_BUTTRESS_WP_REQ_PAYLOAD0, MAX_RATIO, max_ratio, val);
	REGB_WR32(MTL_BUTTRESS_WP_REQ_PAYLOAD0, val);

	val = REGB_RD32(MTL_BUTTRESS_WP_REQ_PAYLOAD1);
	val = REG_SET_FLD_NUM(MTL_BUTTRESS_WP_REQ_PAYLOAD1, TARGET_RATIO, target_ratio, val);
	val = REG_SET_FLD_NUM(MTL_BUTTRESS_WP_REQ_PAYLOAD1, EPP, PLL_DEFAULT_EPP_VALUE, val);
	REGB_WR32(MTL_BUTTRESS_WP_REQ_PAYLOAD1, val);

	val = REGB_RD32(MTL_BUTTRESS_WP_REQ_PAYLOAD2);
	val = REG_SET_FLD_NUM(MTL_BUTTRESS_WP_REQ_PAYLOAD2, CONFIG, config, val);
	REGB_WR32(MTL_BUTTRESS_WP_REQ_PAYLOAD2, val);

	val = REGB_RD32(MTL_BUTTRESS_WP_REQ_CMD);
	val = REG_SET_FLD(MTL_BUTTRESS_WP_REQ_CMD, SEND, val);
	REGB_WR32(MTL_BUTTRESS_WP_REQ_CMD, val);

	ret = ivpu_pll_wait_for_cmd_send(vdev);
	if (ret)
		ivpu_err(vdev, "Failed to sync after WP request: %d\n", ret);

	return ret;
}

static int ivpu_pll_wait_for_lock(struct ivpu_device *vdev, bool enable)
{
	u32 exp_val = enable ? 0x1 : 0x0;

	if (IVPU_WA(punit_disabled))
		return 0;

	return REGB_POLL_FLD(MTL_BUTTRESS_PLL_STATUS, LOCK, exp_val, PLL_TIMEOUT_US);
}

static int ivpu_pll_wait_for_status_ready(struct ivpu_device *vdev)
{
	if (IVPU_WA(punit_disabled))
		return 0;

	return REGB_POLL_FLD(MTL_BUTTRESS_VPU_STATUS, READY, 1, PLL_TIMEOUT_US);
}

static void ivpu_pll_init_frequency_ratios(struct ivpu_device *vdev)
{
	struct ivpu_hw_info *hw = vdev->hw;
	u8 fuse_min_ratio, fuse_max_ratio, fuse_pn_ratio;
	u32 fmin_fuse, fmax_fuse;

	fmin_fuse = REGB_RD32(MTL_BUTTRESS_FMIN_FUSE);
	fuse_min_ratio = REG_GET_FLD(MTL_BUTTRESS_FMIN_FUSE, MIN_RATIO, fmin_fuse);
	fuse_pn_ratio = REG_GET_FLD(MTL_BUTTRESS_FMIN_FUSE, PN_RATIO, fmin_fuse);

	fmax_fuse = REGB_RD32(MTL_BUTTRESS_FMAX_FUSE);
	fuse_max_ratio = REG_GET_FLD(MTL_BUTTRESS_FMAX_FUSE, MAX_RATIO, fmax_fuse);

	hw->pll.min_ratio = clamp_t(u8, ivpu_pll_min_ratio, fuse_min_ratio, fuse_max_ratio);
	hw->pll.max_ratio = clamp_t(u8, ivpu_pll_max_ratio, hw->pll.min_ratio, fuse_max_ratio);
	hw->pll.pn_ratio = clamp_t(u8, fuse_pn_ratio, hw->pll.min_ratio, hw->pll.max_ratio);
}

static int ivpu_pll_drive(struct ivpu_device *vdev, bool enable)
{
	struct ivpu_hw_info *hw = vdev->hw;
	u16 target_ratio;
	u16 config;
	int ret;

	if (IVPU_WA(punit_disabled)) {
		ivpu_dbg(vdev, PM, "Skipping PLL request on %s\n",
			 ivpu_platform_to_str(vdev->platform));
		return 0;
	}

	if (enable) {
		target_ratio = hw->pll.pn_ratio;
		config = hw->config;
	} else {
		target_ratio = 0;
		config = 0;
	}

	ivpu_dbg(vdev, PM, "PLL workpoint request: config 0x%04x pll ratio 0x%x\n",
		 config, target_ratio);

	ret = ivpu_pll_cmd_send(vdev, hw->pll.min_ratio, hw->pll.max_ratio, target_ratio, config);
	if (ret) {
		ivpu_err(vdev, "Failed to send PLL workpoint request: %d\n", ret);
		return ret;
	}

	ret = ivpu_pll_wait_for_lock(vdev, enable);
	if (ret) {
		ivpu_err(vdev, "Timed out waiting for PLL lock\n");
		return ret;
	}

	if (enable) {
		ret = ivpu_pll_wait_for_status_ready(vdev);
		if (ret) {
			ivpu_err(vdev, "Timed out waiting for PLL ready status\n");
			return ret;
		}
	}

	return 0;
}

static int ivpu_pll_enable(struct ivpu_device *vdev)
{
	return ivpu_pll_drive(vdev, true);
}

static int ivpu_pll_disable(struct ivpu_device *vdev)
{
	return ivpu_pll_drive(vdev, false);
}

static void ivpu_boot_host_ss_rst_clr_assert(struct ivpu_device *vdev)
{
	u32 val = REGV_RD32(MTL_VPU_HOST_SS_CPR_RST_CLR);

	val = REG_SET_FLD(MTL_VPU_HOST_SS_CPR_RST_CLR, TOP_NOC, val);
	val = REG_SET_FLD(MTL_VPU_HOST_SS_CPR_RST_CLR, DSS_MAS, val);
	val = REG_SET_FLD(MTL_VPU_HOST_SS_CPR_RST_CLR, MSS_MAS, val);

	REGV_WR32(MTL_VPU_HOST_SS_CPR_RST_CLR, val);
}

static void ivpu_boot_host_ss_rst_drive(struct ivpu_device *vdev, bool enable)
{
	u32 val = REGV_RD32(MTL_VPU_HOST_SS_CPR_RST_SET);

	if (enable) {
		val = REG_SET_FLD(MTL_VPU_HOST_SS_CPR_RST_SET, TOP_NOC, val);
		val = REG_SET_FLD(MTL_VPU_HOST_SS_CPR_RST_SET, DSS_MAS, val);
		val = REG_SET_FLD(MTL_VPU_HOST_SS_CPR_RST_SET, MSS_MAS, val);
	} else {
		val = REG_CLR_FLD(MTL_VPU_HOST_SS_CPR_RST_SET, TOP_NOC, val);
		val = REG_CLR_FLD(MTL_VPU_HOST_SS_CPR_RST_SET, DSS_MAS, val);
		val = REG_CLR_FLD(MTL_VPU_HOST_SS_CPR_RST_SET, MSS_MAS, val);
	}

	REGV_WR32(MTL_VPU_HOST_SS_CPR_RST_SET, val);
}

static void ivpu_boot_host_ss_clk_drive(struct ivpu_device *vdev, bool enable)
{
	u32 val = REGV_RD32(MTL_VPU_HOST_SS_CPR_CLK_SET);

	if (enable) {
		val = REG_SET_FLD(MTL_VPU_HOST_SS_CPR_CLK_SET, TOP_NOC, val);
		val = REG_SET_FLD(MTL_VPU_HOST_SS_CPR_CLK_SET, DSS_MAS, val);
		val = REG_SET_FLD(MTL_VPU_HOST_SS_CPR_CLK_SET, MSS_MAS, val);
	} else {
		val = REG_CLR_FLD(MTL_VPU_HOST_SS_CPR_CLK_SET, TOP_NOC, val);
		val = REG_CLR_FLD(MTL_VPU_HOST_SS_CPR_CLK_SET, DSS_MAS, val);
		val = REG_CLR_FLD(MTL_VPU_HOST_SS_CPR_CLK_SET, MSS_MAS, val);
	}

	REGV_WR32(MTL_VPU_HOST_SS_CPR_CLK_SET, val);
}

static int ivpu_boot_noc_qreqn_check(struct ivpu_device *vdev, u32 exp_val)
{
	u32 val = REGV_RD32(MTL_VPU_HOST_SS_NOC_QREQN);

	if (!REG_TEST_FLD_NUM(MTL_VPU_HOST_SS_NOC_QREQN, TOP_SOCMMIO, exp_val, val))
		return -EIO;

	return 0;
}

static int ivpu_boot_noc_qacceptn_check(struct ivpu_device *vdev, u32 exp_val)
{
	u32 val = REGV_RD32(MTL_VPU_HOST_SS_NOC_QACCEPTN);

	if (!REG_TEST_FLD_NUM(MTL_VPU_HOST_SS_NOC_QACCEPTN, TOP_SOCMMIO, exp_val, val))
		return -EIO;

	return 0;
}

static int ivpu_boot_noc_qdeny_check(struct ivpu_device *vdev, u32 exp_val)
{
	u32 val = REGV_RD32(MTL_VPU_HOST_SS_NOC_QDENY);

	if (!REG_TEST_FLD_NUM(MTL_VPU_HOST_SS_NOC_QDENY, TOP_SOCMMIO, exp_val, val))
		return -EIO;

	return 0;
}

static int ivpu_boot_top_noc_qrenqn_check(struct ivpu_device *vdev, u32 exp_val)
{
	u32 val = REGV_RD32(MTL_VPU_TOP_NOC_QREQN);

	if (!REG_TEST_FLD_NUM(MTL_VPU_TOP_NOC_QREQN, CPU_CTRL, exp_val, val) ||
	    !REG_TEST_FLD_NUM(MTL_VPU_TOP_NOC_QREQN, HOSTIF_L2CACHE, exp_val, val))
		return -EIO;

	return 0;
}

static int ivpu_boot_top_noc_qacceptn_check(struct ivpu_device *vdev, u32 exp_val)
{
	u32 val = REGV_RD32(MTL_VPU_TOP_NOC_QACCEPTN);

	if (!REG_TEST_FLD_NUM(MTL_VPU_TOP_NOC_QACCEPTN, CPU_CTRL, exp_val, val) ||
	    !REG_TEST_FLD_NUM(MTL_VPU_TOP_NOC_QACCEPTN, HOSTIF_L2CACHE, exp_val, val))
		return -EIO;

	return 0;
}

static int ivpu_boot_top_noc_qdeny_check(struct ivpu_device *vdev, u32 exp_val)
{
	u32 val = REGV_RD32(MTL_VPU_TOP_NOC_QDENY);

	if (!REG_TEST_FLD_NUM(MTL_VPU_TOP_NOC_QDENY, CPU_CTRL, exp_val, val) ||
	    !REG_TEST_FLD_NUM(MTL_VPU_TOP_NOC_QDENY, HOSTIF_L2CACHE, exp_val, val))
		return -EIO;

	return 0;
}

static int ivpu_boot_host_ss_configure(struct ivpu_device *vdev)
{
	ivpu_boot_host_ss_rst_clr_assert(vdev);

	return ivpu_boot_noc_qreqn_check(vdev, 0x0);
}

static void ivpu_boot_vpu_idle_gen_disable(struct ivpu_device *vdev)
{
	REGV_WR32(MTL_VPU_HOST_SS_AON_VPU_IDLE_GEN, 0x0);
}

static int ivpu_boot_host_ss_axi_drive(struct ivpu_device *vdev, bool enable)
{
	int ret;
	u32 val;

	val = REGV_RD32(MTL_VPU_HOST_SS_NOC_QREQN);
	if (enable)
		val = REG_SET_FLD(MTL_VPU_HOST_SS_NOC_QREQN, TOP_SOCMMIO, val);
	else
		val = REG_CLR_FLD(MTL_VPU_HOST_SS_NOC_QREQN, TOP_SOCMMIO, val);
	REGV_WR32(MTL_VPU_HOST_SS_NOC_QREQN, val);

	ret = ivpu_boot_noc_qacceptn_check(vdev, enable ? 0x1 : 0x0);
	if (ret) {
		ivpu_err(vdev, "Failed qacceptn check: %d\n", ret);
		return ret;
	}

	ret = ivpu_boot_noc_qdeny_check(vdev, 0x0);
	if (ret)
		ivpu_err(vdev, "Failed qdeny check: %d\n", ret);

	return ret;
}

static int ivpu_boot_host_ss_axi_enable(struct ivpu_device *vdev)
{
	return ivpu_boot_host_ss_axi_drive(vdev, true);
}

static int ivpu_boot_host_ss_top_noc_drive(struct ivpu_device *vdev, bool enable)
{
	int ret;
	u32 val;

	val = REGV_RD32(MTL_VPU_TOP_NOC_QREQN);
	if (enable) {
		val = REG_SET_FLD(MTL_VPU_TOP_NOC_QREQN, CPU_CTRL, val);
		val = REG_SET_FLD(MTL_VPU_TOP_NOC_QREQN, HOSTIF_L2CACHE, val);
	} else {
		val = REG_CLR_FLD(MTL_VPU_TOP_NOC_QREQN, CPU_CTRL, val);
		val = REG_CLR_FLD(MTL_VPU_TOP_NOC_QREQN, HOSTIF_L2CACHE, val);
	}
	REGV_WR32(MTL_VPU_TOP_NOC_QREQN, val);

	ret = ivpu_boot_top_noc_qacceptn_check(vdev, enable ? 0x1 : 0x0);
	if (ret) {
		ivpu_err(vdev, "Failed qacceptn check: %d\n", ret);
		return ret;
	}

	ret = ivpu_boot_top_noc_qdeny_check(vdev, 0x0);
	if (ret)
		ivpu_err(vdev, "Failed qdeny check: %d\n", ret);

	return ret;
}

static int ivpu_boot_host_ss_top_noc_enable(struct ivpu_device *vdev)
{
	return ivpu_boot_host_ss_top_noc_drive(vdev, true);
}

static void ivpu_boot_pwr_island_trickle_drive(struct ivpu_device *vdev, bool enable)
{
	u32 val = REGV_RD32(MTL_VPU_HOST_SS_AON_PWR_ISLAND_TRICKLE_EN0);

	if (enable)
		val = REG_SET_FLD(MTL_VPU_HOST_SS_AON_PWR_ISLAND_TRICKLE_EN0, MSS_CPU, val);
	else
		val = REG_CLR_FLD(MTL_VPU_HOST_SS_AON_PWR_ISLAND_TRICKLE_EN0, MSS_CPU, val);

	REGV_WR32(MTL_VPU_HOST_SS_AON_PWR_ISLAND_TRICKLE_EN0, val);
}

static void ivpu_boot_pwr_island_drive(struct ivpu_device *vdev, bool enable)
{
	u32 val = REGV_RD32(MTL_VPU_HOST_SS_AON_PWR_ISLAND_EN0);

	if (enable)
		val = REG_SET_FLD(MTL_VPU_HOST_SS_AON_PWR_ISLAND_EN0, MSS_CPU, val);
	else
		val = REG_CLR_FLD(MTL_VPU_HOST_SS_AON_PWR_ISLAND_EN0, MSS_CPU, val);

	REGV_WR32(MTL_VPU_HOST_SS_AON_PWR_ISLAND_EN0, val);
}

static int ivpu_boot_wait_for_pwr_island_status(struct ivpu_device *vdev, u32 exp_val)
{
	/* FPGA model (UPF) is not power aware, skipped Power Island polling */
	if (ivpu_is_fpga(vdev))
		return 0;

	return REGV_POLL_FLD(MTL_VPU_HOST_SS_AON_PWR_ISLAND_STATUS0, MSS_CPU,
			     exp_val, PWR_ISLAND_STATUS_TIMEOUT_US);
}

static void ivpu_boot_pwr_island_isolation_drive(struct ivpu_device *vdev, bool enable)
{
	u32 val = REGV_RD32(MTL_VPU_HOST_SS_AON_PWR_ISO_EN0);

	if (enable)
		val = REG_SET_FLD(MTL_VPU_HOST_SS_AON_PWR_ISO_EN0, MSS_CPU, val);
	else
		val = REG_CLR_FLD(MTL_VPU_HOST_SS_AON_PWR_ISO_EN0, MSS_CPU, val);

	REGV_WR32(MTL_VPU_HOST_SS_AON_PWR_ISO_EN0, val);
}

static void ivpu_boot_dpu_active_drive(struct ivpu_device *vdev, bool enable)
{
	u32 val = REGV_RD32(MTL_VPU_HOST_SS_AON_DPU_ACTIVE);

	if (enable)
		val = REG_SET_FLD(MTL_VPU_HOST_SS_AON_DPU_ACTIVE, DPU_ACTIVE, val);
	else
		val = REG_CLR_FLD(MTL_VPU_HOST_SS_AON_DPU_ACTIVE, DPU_ACTIVE, val);

	REGV_WR32(MTL_VPU_HOST_SS_AON_DPU_ACTIVE, val);
}

static int ivpu_boot_pwr_domain_enable(struct ivpu_device *vdev)
{
	int ret;

	ivpu_boot_pwr_island_trickle_drive(vdev, true);
	ivpu_boot_pwr_island_drive(vdev, true);

	ret = ivpu_boot_wait_for_pwr_island_status(vdev, 0x1);
	if (ret) {
		ivpu_err(vdev, "Timed out waiting for power island status\n");
		return ret;
	}

	ret = ivpu_boot_top_noc_qrenqn_check(vdev, 0x0);
	if (ret) {
		ivpu_err(vdev, "Failed qrenqn check %d\n", ret);
		return ret;
	}

	ivpu_boot_host_ss_clk_drive(vdev, true);
	ivpu_boot_pwr_island_isolation_drive(vdev, false);
	ivpu_boot_host_ss_rst_drive(vdev, true);
	ivpu_boot_dpu_active_drive(vdev, true);

	return ret;
}

static void ivpu_boot_no_snoop_enable(struct ivpu_device *vdev)
{
	u32 val = REGV_RD32(MTL_VPU_HOST_IF_TCU_PTW_OVERRIDES);

	val = REG_SET_FLD(MTL_VPU_HOST_IF_TCU_PTW_OVERRIDES, NOSNOOP_OVERRIDE_EN, val);
	val = REG_SET_FLD(MTL_VPU_HOST_IF_TCU_PTW_OVERRIDES, AW_NOSNOOP_OVERRIDE, val);
	val = REG_SET_FLD(MTL_VPU_HOST_IF_TCU_PTW_OVERRIDES, AR_NOSNOOP_OVERRIDE, val);

	REGV_WR32(MTL_VPU_HOST_IF_TCU_PTW_OVERRIDES, val);
}

static void ivpu_boot_tbu_mmu_enable(struct ivpu_device *vdev)
{
	u32 val = REGV_RD32(MTL_VPU_HOST_IF_TBU_MMUSSIDV);

	if (ivpu_is_fpga(vdev)) {
		val = REG_SET_FLD(MTL_VPU_HOST_IF_TBU_MMUSSIDV, TBU0_AWMMUSSIDV, val);
		val = REG_SET_FLD(MTL_VPU_HOST_IF_TBU_MMUSSIDV, TBU0_ARMMUSSIDV, val);
		val = REG_SET_FLD(MTL_VPU_HOST_IF_TBU_MMUSSIDV, TBU2_AWMMUSSIDV, val);
		val = REG_SET_FLD(MTL_VPU_HOST_IF_TBU_MMUSSIDV, TBU2_ARMMUSSIDV, val);
	} else {
		val = REG_SET_FLD(MTL_VPU_HOST_IF_TBU_MMUSSIDV, TBU0_AWMMUSSIDV, val);
		val = REG_SET_FLD(MTL_VPU_HOST_IF_TBU_MMUSSIDV, TBU0_ARMMUSSIDV, val);
		val = REG_SET_FLD(MTL_VPU_HOST_IF_TBU_MMUSSIDV, TBU1_AWMMUSSIDV, val);
		val = REG_SET_FLD(MTL_VPU_HOST_IF_TBU_MMUSSIDV, TBU1_ARMMUSSIDV, val);
		val = REG_SET_FLD(MTL_VPU_HOST_IF_TBU_MMUSSIDV, TBU2_AWMMUSSIDV, val);
		val = REG_SET_FLD(MTL_VPU_HOST_IF_TBU_MMUSSIDV, TBU2_ARMMUSSIDV, val);
		val = REG_SET_FLD(MTL_VPU_HOST_IF_TBU_MMUSSIDV, TBU3_AWMMUSSIDV, val);
		val = REG_SET_FLD(MTL_VPU_HOST_IF_TBU_MMUSSIDV, TBU3_ARMMUSSIDV, val);
	}

	REGV_WR32(MTL_VPU_HOST_IF_TBU_MMUSSIDV, val);
}

static void ivpu_boot_soc_cpu_boot(struct ivpu_device *vdev)
{
	u32 val;

	val = REGV_RD32(MTL_VPU_CPU_SS_MSSCPU_CPR_LEON_RT_VEC);
	val = REG_SET_FLD(MTL_VPU_CPU_SS_MSSCPU_CPR_LEON_RT_VEC, IRQI_RSTRUN0, val);

	val = REG_CLR_FLD(MTL_VPU_CPU_SS_MSSCPU_CPR_LEON_RT_VEC, IRQI_RSTVEC, val);
	REGV_WR32(MTL_VPU_CPU_SS_MSSCPU_CPR_LEON_RT_VEC, val);

	val = REG_SET_FLD(MTL_VPU_CPU_SS_MSSCPU_CPR_LEON_RT_VEC, IRQI_RESUME0, val);
	REGV_WR32(MTL_VPU_CPU_SS_MSSCPU_CPR_LEON_RT_VEC, val);

	val = REG_CLR_FLD(MTL_VPU_CPU_SS_MSSCPU_CPR_LEON_RT_VEC, IRQI_RESUME0, val);
	REGV_WR32(MTL_VPU_CPU_SS_MSSCPU_CPR_LEON_RT_VEC, val);

	val = vdev->fw->entry_point >> 9;
	REGV_WR32(MTL_VPU_HOST_SS_LOADING_ADDRESS_LO, val);

	val = REG_SET_FLD(MTL_VPU_HOST_SS_LOADING_ADDRESS_LO, DONE, val);
	REGV_WR32(MTL_VPU_HOST_SS_LOADING_ADDRESS_LO, val);

	ivpu_dbg(vdev, PM, "Booting firmware, mode: %s\n",
		 vdev->fw->entry_point == vdev->fw->cold_boot_entry_point ? "cold boot" : "resume");
}

static int ivpu_boot_d0i3_drive(struct ivpu_device *vdev, bool enable)
{
	int ret;
	u32 val;

	ret = REGB_POLL_FLD(MTL_BUTTRESS_VPU_D0I3_CONTROL, INPROGRESS, 0, TIMEOUT_US);
	if (ret) {
		ivpu_err(vdev, "Failed to sync before D0i3 transition: %d\n", ret);
		return ret;
	}

	val = REGB_RD32(MTL_BUTTRESS_VPU_D0I3_CONTROL);
	if (enable)
		val = REG_SET_FLD(MTL_BUTTRESS_VPU_D0I3_CONTROL, I3, val);
	else
		val = REG_CLR_FLD(MTL_BUTTRESS_VPU_D0I3_CONTROL, I3, val);
	REGB_WR32(MTL_BUTTRESS_VPU_D0I3_CONTROL, val);

	ret = REGB_POLL_FLD(MTL_BUTTRESS_VPU_D0I3_CONTROL, INPROGRESS, 0, TIMEOUT_US);
	if (ret)
		ivpu_err(vdev, "Failed to sync after D0i3 transition: %d\n", ret);

	return ret;
}

static int ivpu_hw_mtl_info_init(struct ivpu_device *vdev)
{
	struct ivpu_hw_info *hw = vdev->hw;

	hw->tile_fuse = TILE_FUSE_ENABLE_BOTH;
	hw->sku = TILE_SKU_BOTH_MTL;
	hw->config = WP_CONFIG_2_TILE_4_3_RATIO;

	ivpu_pll_init_frequency_ratios(vdev);

	ivpu_hw_init_range(&hw->ranges.global_low, 0x80000000, SZ_512M);
	ivpu_hw_init_range(&hw->ranges.global_high, 0x180000000, SZ_2M);
	ivpu_hw_init_range(&hw->ranges.user_low, 0xc0000000, 255 * SZ_1M);
	ivpu_hw_init_range(&hw->ranges.user_high, 0x180000000, SZ_2G);
	hw->ranges.global_aliased_pio = hw->ranges.user_low;

	return 0;
}

static int ivpu_hw_mtl_reset(struct ivpu_device *vdev)
{
	int ret;
	u32 val;

	if (IVPU_WA(punit_disabled))
		return 0;

	ret = REGB_POLL_FLD(MTL_BUTTRESS_VPU_IP_RESET, TRIGGER, 0, TIMEOUT_US);
	if (ret) {
		ivpu_err(vdev, "Timed out waiting for TRIGGER bit\n");
		return ret;
	}

	val = REGB_RD32(MTL_BUTTRESS_VPU_IP_RESET);
	val = REG_SET_FLD(MTL_BUTTRESS_VPU_IP_RESET, TRIGGER, val);
	REGB_WR32(MTL_BUTTRESS_VPU_IP_RESET, val);

	ret = REGB_POLL_FLD(MTL_BUTTRESS_VPU_IP_RESET, TRIGGER, 0, TIMEOUT_US);
	if (ret)
		ivpu_err(vdev, "Timed out waiting for RESET completion\n");

	return ret;
}

static int ivpu_hw_mtl_d0i3_enable(struct ivpu_device *vdev)
{
	int ret;

	ret = ivpu_boot_d0i3_drive(vdev, true);
	if (ret)
		ivpu_err(vdev, "Failed to enable D0i3: %d\n", ret);

	udelay(5); /* VPU requires 5 us to complete the transition */

	return ret;
}

static int ivpu_hw_mtl_d0i3_disable(struct ivpu_device *vdev)
{
	int ret;

	ret = ivpu_boot_d0i3_drive(vdev, false);
	if (ret)
		ivpu_err(vdev, "Failed to disable D0i3: %d\n", ret);

	return ret;
}

static int ivpu_hw_mtl_power_up(struct ivpu_device *vdev)
{
	int ret;

	ivpu_hw_read_platform(vdev);
	ivpu_hw_wa_init(vdev);
	ivpu_hw_timeouts_init(vdev);

	ret = ivpu_hw_mtl_reset(vdev);
	if (ret)
		ivpu_warn(vdev, "Failed to reset HW: %d\n", ret);

	ret = ivpu_hw_mtl_d0i3_disable(vdev);
	if (ret)
		ivpu_warn(vdev, "Failed to disable D0I3: %d\n", ret);

	ret = ivpu_pll_enable(vdev);
	if (ret) {
		ivpu_err(vdev, "Failed to enable PLL: %d\n", ret);
		return ret;
	}

	ret = ivpu_boot_host_ss_configure(vdev);
	if (ret) {
		ivpu_err(vdev, "Failed to configure host SS: %d\n", ret);
		return ret;
	}

	/*
	 * The control circuitry for vpu_idle indication logic powers up active.
	 * To ensure unnecessary low power mode signal from LRT during bring up,
	 * KMD disables the circuitry prior to bringing up the Main Power island.
	 */
	ivpu_boot_vpu_idle_gen_disable(vdev);

	ret = ivpu_boot_pwr_domain_enable(vdev);
	if (ret) {
		ivpu_err(vdev, "Failed to enable power domain: %d\n", ret);
		return ret;
	}

	ret = ivpu_boot_host_ss_axi_enable(vdev);
	if (ret) {
		ivpu_err(vdev, "Failed to enable AXI: %d\n", ret);
		return ret;
	}

	ret = ivpu_boot_host_ss_top_noc_enable(vdev);
	if (ret)
		ivpu_err(vdev, "Failed to enable TOP NOC: %d\n", ret);

	return ret;
}

static int ivpu_hw_mtl_boot_fw(struct ivpu_device *vdev)
{
	ivpu_boot_no_snoop_enable(vdev);
	ivpu_boot_tbu_mmu_enable(vdev);
	ivpu_boot_soc_cpu_boot(vdev);

	return 0;
}

static bool ivpu_hw_mtl_is_idle(struct ivpu_device *vdev)
{
	u32 val;

	if (IVPU_WA(punit_disabled))
		return true;

	val = REGB_RD32(MTL_BUTTRESS_VPU_STATUS);
	return REG_TEST_FLD(MTL_BUTTRESS_VPU_STATUS, READY, val) &&
	       REG_TEST_FLD(MTL_BUTTRESS_VPU_STATUS, IDLE, val);
}

static int ivpu_hw_mtl_power_down(struct ivpu_device *vdev)
{
	int ret = 0;

	if (ivpu_hw_mtl_reset(vdev)) {
		ivpu_err(vdev, "Failed to reset the VPU\n");
		ret = -EIO;
	}

	if (ivpu_pll_disable(vdev)) {
		ivpu_err(vdev, "Failed to disable PLL\n");
		ret = -EIO;
	}

	if (ivpu_hw_mtl_d0i3_enable(vdev))
		ivpu_warn(vdev, "Failed to enable D0I3\n");

	return ret;
}

static void ivpu_hw_mtl_wdt_disable(struct ivpu_device *vdev)
{
	u32 val;

	/* Enable writing and set non-zero WDT value */
	REGV_WR32(MTL_VPU_CPU_SS_TIM_SAFE, TIM_SAFE_ENABLE);
	REGV_WR32(MTL_VPU_CPU_SS_TIM_WATCHDOG, TIM_WATCHDOG_RESET_VALUE);

	/* Enable writing and disable watchdog timer */
	REGV_WR32(MTL_VPU_CPU_SS_TIM_SAFE, TIM_SAFE_ENABLE);
	REGV_WR32(MTL_VPU_CPU_SS_TIM_WDOG_EN, 0);

	/* Now clear the timeout interrupt */
	val = REGV_RD32(MTL_VPU_CPU_SS_TIM_GEN_CONFIG);
	val = REG_CLR_FLD(MTL_VPU_CPU_SS_TIM_GEN_CONFIG, WDOG_TO_INT_CLR, val);
	REGV_WR32(MTL_VPU_CPU_SS_TIM_GEN_CONFIG, val);
}

static u32 ivpu_hw_mtl_pll_to_freq(u32 ratio, u32 config)
{
	u32 pll_clock = PLL_REF_CLK_FREQ * ratio;
	u32 cpu_clock;

	if ((config & 0xff) == PLL_RATIO_4_3)
		cpu_clock = pll_clock * 2 / 4;
	else
		cpu_clock = pll_clock * 2 / 5;

	return cpu_clock;
}

/* Register indirect accesses */
static u32 ivpu_hw_mtl_reg_pll_freq_get(struct ivpu_device *vdev)
{
	u32 pll_curr_ratio;

	pll_curr_ratio = REGB_RD32(MTL_BUTTRESS_CURRENT_PLL);
	pll_curr_ratio &= MTL_BUTTRESS_CURRENT_PLL_RATIO_MASK;

	if (!ivpu_is_silicon(vdev))
		return PLL_SIMULATION_FREQ;

	return ivpu_hw_mtl_pll_to_freq(pll_curr_ratio, vdev->hw->config);
}

static u32 ivpu_hw_mtl_reg_telemetry_offset_get(struct ivpu_device *vdev)
{
	return REGB_RD32(MTL_BUTTRESS_VPU_TELEMETRY_OFFSET);
}

static u32 ivpu_hw_mtl_reg_telemetry_size_get(struct ivpu_device *vdev)
{
	return REGB_RD32(MTL_BUTTRESS_VPU_TELEMETRY_SIZE);
}

static u32 ivpu_hw_mtl_reg_telemetry_enable_get(struct ivpu_device *vdev)
{
	return REGB_RD32(MTL_BUTTRESS_VPU_TELEMETRY_ENABLE);
}

static void ivpu_hw_mtl_reg_db_set(struct ivpu_device *vdev, u32 db_id)
{
	u32 reg_stride = MTL_VPU_CPU_SS_DOORBELL_1 - MTL_VPU_CPU_SS_DOORBELL_0;
	u32 val = REG_FLD(MTL_VPU_CPU_SS_DOORBELL_0, SET);

	REGV_WR32I(MTL_VPU_CPU_SS_DOORBELL_0, reg_stride, db_id, val);
}

static u32 ivpu_hw_mtl_reg_ipc_rx_addr_get(struct ivpu_device *vdev)
{
	return REGV_RD32(MTL_VPU_HOST_SS_TIM_IPC_FIFO_ATM);
}

static u32 ivpu_hw_mtl_reg_ipc_rx_count_get(struct ivpu_device *vdev)
{
	u32 count = REGV_RD32_SILENT(MTL_VPU_HOST_SS_TIM_IPC_FIFO_STAT);

	return REG_GET_FLD(MTL_VPU_HOST_SS_TIM_IPC_FIFO_STAT, FILL_LEVEL, count);
}

static void ivpu_hw_mtl_reg_ipc_tx_set(struct ivpu_device *vdev, u32 vpu_addr)
{
	REGV_WR32(MTL_VPU_CPU_SS_TIM_IPC_FIFO, vpu_addr);
}

static void ivpu_hw_mtl_irq_clear(struct ivpu_device *vdev)
{
	REGV_WR64(MTL_VPU_HOST_SS_ICB_CLEAR_0, ICB_0_1_IRQ_MASK);
}

static void ivpu_hw_mtl_irq_enable(struct ivpu_device *vdev)
{
	REGV_WR32(MTL_VPU_HOST_SS_FW_SOC_IRQ_EN, ITF_FIREWALL_VIOLATION_MASK);
	REGV_WR64(MTL_VPU_HOST_SS_ICB_ENABLE_0, ICB_0_1_IRQ_MASK);
	REGB_WR32(MTL_BUTTRESS_LOCAL_INT_MASK, BUTTRESS_IRQ_ENABLE_MASK);
	REGB_WR32(MTL_BUTTRESS_GLOBAL_INT_MASK, 0x0);
}

static void ivpu_hw_mtl_irq_disable(struct ivpu_device *vdev)
{
	REGB_WR32(MTL_BUTTRESS_GLOBAL_INT_MASK, 0x1);
	REGB_WR32(MTL_BUTTRESS_LOCAL_INT_MASK, BUTTRESS_IRQ_DISABLE_MASK);
	REGV_WR64(MTL_VPU_HOST_SS_ICB_ENABLE_0, 0x0ull);
	REGB_WR32(MTL_VPU_HOST_SS_FW_SOC_IRQ_EN, 0x0);
}

static void ivpu_hw_mtl_irq_wdt_nce_handler(struct ivpu_device *vdev)
{
	ivpu_err_ratelimited(vdev, "WDT NCE irq\n");

	ivpu_pm_schedule_recovery(vdev);
}

static void ivpu_hw_mtl_irq_wdt_mss_handler(struct ivpu_device *vdev)
{
	ivpu_err_ratelimited(vdev, "WDT MSS irq\n");

	ivpu_hw_wdt_disable(vdev);
	ivpu_pm_schedule_recovery(vdev);
}

static void ivpu_hw_mtl_irq_noc_firewall_handler(struct ivpu_device *vdev)
{
	ivpu_err_ratelimited(vdev, "NOC Firewall irq\n");

	ivpu_pm_schedule_recovery(vdev);
}

/* Handler for IRQs from VPU core (irqV) */
static u32 ivpu_hw_mtl_irqv_handler(struct ivpu_device *vdev, int irq)
{
	u32 status = REGV_RD32(MTL_VPU_HOST_SS_ICB_STATUS_0) & ICB_0_IRQ_MASK;

	REGV_WR32(MTL_VPU_HOST_SS_ICB_CLEAR_0, status);

	if (REG_TEST_FLD(MTL_VPU_HOST_SS_ICB_STATUS_0, MMU_IRQ_0_INT, status))
		ivpu_mmu_irq_evtq_handler(vdev);

	if (REG_TEST_FLD(MTL_VPU_HOST_SS_ICB_STATUS_0, HOST_IPC_FIFO_INT, status))
		ivpu_ipc_irq_handler(vdev);

	if (REG_TEST_FLD(MTL_VPU_HOST_SS_ICB_STATUS_0, MMU_IRQ_1_INT, status))
		ivpu_dbg(vdev, IRQ, "MMU sync complete\n");

	if (REG_TEST_FLD(MTL_VPU_HOST_SS_ICB_STATUS_0, MMU_IRQ_2_INT, status))
		ivpu_mmu_irq_gerr_handler(vdev);

	if (REG_TEST_FLD(MTL_VPU_HOST_SS_ICB_STATUS_0, CPU_INT_REDIRECT_0_INT, status))
		ivpu_hw_mtl_irq_wdt_mss_handler(vdev);

	if (REG_TEST_FLD(MTL_VPU_HOST_SS_ICB_STATUS_0, CPU_INT_REDIRECT_1_INT, status))
		ivpu_hw_mtl_irq_wdt_nce_handler(vdev);

	if (REG_TEST_FLD(MTL_VPU_HOST_SS_ICB_STATUS_0, NOC_FIREWALL_INT, status))
		ivpu_hw_mtl_irq_noc_firewall_handler(vdev);

	return status;
}

/* Handler for IRQs from Buttress core (irqB) */
static u32 ivpu_hw_mtl_irqb_handler(struct ivpu_device *vdev, int irq)
{
	u32 status = REGB_RD32(MTL_BUTTRESS_INTERRUPT_STAT) & BUTTRESS_IRQ_MASK;
	bool schedule_recovery = false;

	if (status == 0)
		return 0;

	/* Disable global interrupt before handling local buttress interrupts */
	REGB_WR32(MTL_BUTTRESS_GLOBAL_INT_MASK, 0x1);

	if (REG_TEST_FLD(MTL_BUTTRESS_INTERRUPT_STAT, FREQ_CHANGE, status))
		ivpu_dbg(vdev, IRQ, "FREQ_CHANGE irq: %08x", REGB_RD32(MTL_BUTTRESS_CURRENT_PLL));

	if (REG_TEST_FLD(MTL_BUTTRESS_INTERRUPT_STAT, ATS_ERR, status)) {
		ivpu_err(vdev, "ATS_ERR irq 0x%016llx", REGB_RD64(MTL_BUTTRESS_ATS_ERR_LOG_0));
		REGB_WR32(MTL_BUTTRESS_ATS_ERR_CLEAR, 0x1);
		schedule_recovery = true;
	}

	if (REG_TEST_FLD(MTL_BUTTRESS_INTERRUPT_STAT, UFI_ERR, status)) {
		u32 ufi_log = REGB_RD32(MTL_BUTTRESS_UFI_ERR_LOG);

		ivpu_err(vdev, "UFI_ERR irq (0x%08x) opcode: 0x%02lx axi_id: 0x%02lx cq_id: 0x%03lx",
			 ufi_log, REG_GET_FLD(MTL_BUTTRESS_UFI_ERR_LOG, OPCODE, ufi_log),
			 REG_GET_FLD(MTL_BUTTRESS_UFI_ERR_LOG, AXI_ID, ufi_log),
			 REG_GET_FLD(MTL_BUTTRESS_UFI_ERR_LOG, CQ_ID, ufi_log));
		REGB_WR32(MTL_BUTTRESS_UFI_ERR_CLEAR, 0x1);
		schedule_recovery = true;
	}

	/*
	 * Clear local interrupt status by writing 0 to all bits.
	 * This must be done after interrupts are cleared at the source.
	 * Writing 1 triggers an interrupt, so we can't perform read update write.
	 */
	REGB_WR32(MTL_BUTTRESS_INTERRUPT_STAT, 0x0);

	/* Re-enable global interrupt */
	REGB_WR32(MTL_BUTTRESS_GLOBAL_INT_MASK, 0x0);

	if (schedule_recovery)
		ivpu_pm_schedule_recovery(vdev);

	return status;
}

static irqreturn_t ivpu_hw_mtl_irq_handler(int irq, void *ptr)
{
	struct ivpu_device *vdev = ptr;
	u32 ret_irqv, ret_irqb;

	ret_irqv = ivpu_hw_mtl_irqv_handler(vdev, irq);
	ret_irqb = ivpu_hw_mtl_irqb_handler(vdev, irq);

	return IRQ_RETVAL(ret_irqb | ret_irqv);
}

static void ivpu_hw_mtl_diagnose_failure(struct ivpu_device *vdev)
{
	u32 irqv = REGV_RD32(MTL_VPU_HOST_SS_ICB_STATUS_0) & ICB_0_IRQ_MASK;
	u32 irqb = REGB_RD32(MTL_BUTTRESS_INTERRUPT_STAT) & BUTTRESS_IRQ_MASK;

	if (ivpu_hw_mtl_reg_ipc_rx_count_get(vdev))
		ivpu_err(vdev, "IPC FIFO queue not empty, missed IPC IRQ");

	if (REG_TEST_FLD(MTL_VPU_HOST_SS_ICB_STATUS_0, CPU_INT_REDIRECT_0_INT, irqv))
		ivpu_err(vdev, "WDT MSS timeout detected\n");

	if (REG_TEST_FLD(MTL_VPU_HOST_SS_ICB_STATUS_0, CPU_INT_REDIRECT_1_INT, irqv))
		ivpu_err(vdev, "WDT NCE timeout detected\n");

	if (REG_TEST_FLD(MTL_VPU_HOST_SS_ICB_STATUS_0, NOC_FIREWALL_INT, irqv))
		ivpu_err(vdev, "NOC Firewall irq detected\n");

	if (REG_TEST_FLD(MTL_BUTTRESS_INTERRUPT_STAT, ATS_ERR, irqb))
		ivpu_err(vdev, "ATS_ERR irq 0x%016llx", REGB_RD64(MTL_BUTTRESS_ATS_ERR_LOG_0));

	if (REG_TEST_FLD(MTL_BUTTRESS_INTERRUPT_STAT, UFI_ERR, irqb)) {
		u32 ufi_log = REGB_RD32(MTL_BUTTRESS_UFI_ERR_LOG);

		ivpu_err(vdev, "UFI_ERR irq (0x%08x) opcode: 0x%02lx axi_id: 0x%02lx cq_id: 0x%03lx",
			 ufi_log, REG_GET_FLD(MTL_BUTTRESS_UFI_ERR_LOG, OPCODE, ufi_log),
			 REG_GET_FLD(MTL_BUTTRESS_UFI_ERR_LOG, AXI_ID, ufi_log),
			 REG_GET_FLD(MTL_BUTTRESS_UFI_ERR_LOG, CQ_ID, ufi_log));
	}
}

const struct ivpu_hw_ops ivpu_hw_mtl_ops = {
	.info_init = ivpu_hw_mtl_info_init,
	.power_up = ivpu_hw_mtl_power_up,
	.is_idle = ivpu_hw_mtl_is_idle,
	.power_down = ivpu_hw_mtl_power_down,
	.boot_fw = ivpu_hw_mtl_boot_fw,
	.wdt_disable = ivpu_hw_mtl_wdt_disable,
	.diagnose_failure = ivpu_hw_mtl_diagnose_failure,
	.reg_pll_freq_get = ivpu_hw_mtl_reg_pll_freq_get,
	.reg_telemetry_offset_get = ivpu_hw_mtl_reg_telemetry_offset_get,
	.reg_telemetry_size_get = ivpu_hw_mtl_reg_telemetry_size_get,
	.reg_telemetry_enable_get = ivpu_hw_mtl_reg_telemetry_enable_get,
	.reg_db_set = ivpu_hw_mtl_reg_db_set,
	.reg_ipc_rx_addr_get = ivpu_hw_mtl_reg_ipc_rx_addr_get,
	.reg_ipc_rx_count_get = ivpu_hw_mtl_reg_ipc_rx_count_get,
	.reg_ipc_tx_set = ivpu_hw_mtl_reg_ipc_tx_set,
	.irq_clear = ivpu_hw_mtl_irq_clear,
	.irq_enable = ivpu_hw_mtl_irq_enable,
	.irq_disable = ivpu_hw_mtl_irq_disable,
	.irq_handler = ivpu_hw_mtl_irq_handler,
};
