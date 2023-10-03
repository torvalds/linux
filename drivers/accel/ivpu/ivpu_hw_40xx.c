// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2023 Intel Corporation
 */

#include "ivpu_drv.h"
#include "ivpu_fw.h"
#include "ivpu_hw.h"
#include "ivpu_hw_40xx_reg.h"
#include "ivpu_hw_reg_io.h"
#include "ivpu_ipc.h"
#include "ivpu_mmu.h"
#include "ivpu_pm.h"

#include <linux/dmi.h>

#define TILE_MAX_NUM                 6
#define TILE_MAX_MASK                0x3f

#define LNL_HW_ID                    0x4040

#define SKU_TILE_SHIFT               0u
#define SKU_TILE_MASK                0x0000ffffu
#define SKU_HW_ID_SHIFT              16u
#define SKU_HW_ID_MASK               0xffff0000u

#define PLL_CONFIG_DEFAULT           0x1
#define PLL_CDYN_DEFAULT             0x80
#define PLL_EPP_DEFAULT              0x80
#define PLL_REF_CLK_FREQ	     (50 * 1000000)
#define PLL_RATIO_TO_FREQ(x)	     ((x) * PLL_REF_CLK_FREQ)

#define PLL_PROFILING_FREQ_DEFAULT   38400000
#define PLL_PROFILING_FREQ_HIGH      400000000

#define TIM_SAFE_ENABLE		     0xf1d0dead
#define TIM_WATCHDOG_RESET_VALUE     0xffffffff

#define TIMEOUT_US		     (150 * USEC_PER_MSEC)
#define PWR_ISLAND_STATUS_TIMEOUT_US (5 * USEC_PER_MSEC)
#define PLL_TIMEOUT_US		     (1500 * USEC_PER_MSEC)

#define WEIGHTS_DEFAULT              0xf711f711u
#define WEIGHTS_ATS_DEFAULT          0x0000f711u

#define ICB_0_IRQ_MASK ((REG_FLD(VPU_40XX_HOST_SS_ICB_STATUS_0, HOST_IPC_FIFO_INT)) | \
			(REG_FLD(VPU_40XX_HOST_SS_ICB_STATUS_0, MMU_IRQ_0_INT)) | \
			(REG_FLD(VPU_40XX_HOST_SS_ICB_STATUS_0, MMU_IRQ_1_INT)) | \
			(REG_FLD(VPU_40XX_HOST_SS_ICB_STATUS_0, MMU_IRQ_2_INT)) | \
			(REG_FLD(VPU_40XX_HOST_SS_ICB_STATUS_0, NOC_FIREWALL_INT)) | \
			(REG_FLD(VPU_40XX_HOST_SS_ICB_STATUS_0, CPU_INT_REDIRECT_0_INT)) | \
			(REG_FLD(VPU_40XX_HOST_SS_ICB_STATUS_0, CPU_INT_REDIRECT_1_INT)))

#define ICB_1_IRQ_MASK ((REG_FLD(VPU_40XX_HOST_SS_ICB_STATUS_1, CPU_INT_REDIRECT_2_INT)) | \
			(REG_FLD(VPU_40XX_HOST_SS_ICB_STATUS_1, CPU_INT_REDIRECT_3_INT)) | \
			(REG_FLD(VPU_40XX_HOST_SS_ICB_STATUS_1, CPU_INT_REDIRECT_4_INT)))

#define ICB_0_1_IRQ_MASK ((((u64)ICB_1_IRQ_MASK) << 32) | ICB_0_IRQ_MASK)

#define BUTTRESS_IRQ_MASK ((REG_FLD(VPU_40XX_BUTTRESS_INTERRUPT_STAT, ATS_ERR)) | \
			   (REG_FLD(VPU_40XX_BUTTRESS_INTERRUPT_STAT, CFI0_ERR)) | \
			   (REG_FLD(VPU_40XX_BUTTRESS_INTERRUPT_STAT, CFI1_ERR)) | \
			   (REG_FLD(VPU_40XX_BUTTRESS_INTERRUPT_STAT, IMR0_ERR)) | \
			   (REG_FLD(VPU_40XX_BUTTRESS_INTERRUPT_STAT, IMR1_ERR)) | \
			   (REG_FLD(VPU_40XX_BUTTRESS_INTERRUPT_STAT, SURV_ERR)))

#define BUTTRESS_IRQ_ENABLE_MASK ((u32)~BUTTRESS_IRQ_MASK)
#define BUTTRESS_IRQ_DISABLE_MASK ((u32)-1)

#define ITF_FIREWALL_VIOLATION_MASK ((REG_FLD(VPU_40XX_HOST_SS_FW_SOC_IRQ_EN, CSS_ROM_CMX)) | \
				     (REG_FLD(VPU_40XX_HOST_SS_FW_SOC_IRQ_EN, CSS_DBG)) | \
				     (REG_FLD(VPU_40XX_HOST_SS_FW_SOC_IRQ_EN, CSS_CTRL)) | \
				     (REG_FLD(VPU_40XX_HOST_SS_FW_SOC_IRQ_EN, DEC400)) | \
				     (REG_FLD(VPU_40XX_HOST_SS_FW_SOC_IRQ_EN, MSS_NCE)) | \
				     (REG_FLD(VPU_40XX_HOST_SS_FW_SOC_IRQ_EN, MSS_MBI)) | \
				     (REG_FLD(VPU_40XX_HOST_SS_FW_SOC_IRQ_EN, MSS_MBI_CMX)))

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

static const struct dmi_system_id ivpu_dmi_platform_simulation[] = {
	{
		.ident = "Intel Simics",
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "lnlrvp"),
			DMI_MATCH(DMI_BOARD_VERSION, "1.0"),
			DMI_MATCH(DMI_BOARD_SERIAL, "123456789"),
		},
	},
	{
		.ident = "Intel Simics",
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "Simics"),
		},
	},
	{ }
};

static void ivpu_hw_read_platform(struct ivpu_device *vdev)
{
	if (dmi_check_system(ivpu_dmi_platform_simulation))
		vdev->platform = IVPU_PLATFORM_SIMICS;
	else
		vdev->platform = IVPU_PLATFORM_SILICON;

	ivpu_dbg(vdev, MISC, "Platform type: %s (%d)\n",
		 ivpu_platform_to_str(vdev->platform), vdev->platform);
}

static void ivpu_hw_wa_init(struct ivpu_device *vdev)
{
	vdev->wa.punit_disabled = ivpu_is_fpga(vdev);
	vdev->wa.clear_runtime_mem = false;

	if (ivpu_hw_gen(vdev) == IVPU_HW_40XX)
		vdev->wa.disable_clock_relinquish = true;
}

static void ivpu_hw_timeouts_init(struct ivpu_device *vdev)
{
	if (ivpu_is_fpga(vdev)) {
		vdev->timeout.boot = 100000;
		vdev->timeout.jsm = 50000;
		vdev->timeout.tdr = 2000000;
		vdev->timeout.reschedule_suspend = 1000;
	} else if (ivpu_is_simics(vdev)) {
		vdev->timeout.boot = 50;
		vdev->timeout.jsm = 500;
		vdev->timeout.tdr = 10000;
		vdev->timeout.reschedule_suspend = 10;
	} else {
		vdev->timeout.boot = 1000;
		vdev->timeout.jsm = 500;
		vdev->timeout.tdr = 2000;
		vdev->timeout.reschedule_suspend = 10;
	}
}

static int ivpu_pll_wait_for_cmd_send(struct ivpu_device *vdev)
{
	return REGB_POLL_FLD(VPU_40XX_BUTTRESS_WP_REQ_CMD, SEND, 0, PLL_TIMEOUT_US);
}

static int ivpu_pll_cmd_send(struct ivpu_device *vdev, u16 min_ratio, u16 max_ratio,
			     u16 target_ratio, u16 epp, u16 config, u16 cdyn)
{
	int ret;
	u32 val;

	ret = ivpu_pll_wait_for_cmd_send(vdev);
	if (ret) {
		ivpu_err(vdev, "Failed to sync before WP request: %d\n", ret);
		return ret;
	}

	val = REGB_RD32(VPU_40XX_BUTTRESS_WP_REQ_PAYLOAD0);
	val = REG_SET_FLD_NUM(VPU_40XX_BUTTRESS_WP_REQ_PAYLOAD0, MIN_RATIO, min_ratio, val);
	val = REG_SET_FLD_NUM(VPU_40XX_BUTTRESS_WP_REQ_PAYLOAD0, MAX_RATIO, max_ratio, val);
	REGB_WR32(VPU_40XX_BUTTRESS_WP_REQ_PAYLOAD0, val);

	val = REGB_RD32(VPU_40XX_BUTTRESS_WP_REQ_PAYLOAD1);
	val = REG_SET_FLD_NUM(VPU_40XX_BUTTRESS_WP_REQ_PAYLOAD1, TARGET_RATIO, target_ratio, val);
	val = REG_SET_FLD_NUM(VPU_40XX_BUTTRESS_WP_REQ_PAYLOAD1, EPP, epp, val);
	REGB_WR32(VPU_40XX_BUTTRESS_WP_REQ_PAYLOAD1, val);

	val = REGB_RD32(VPU_40XX_BUTTRESS_WP_REQ_PAYLOAD2);
	val = REG_SET_FLD_NUM(VPU_40XX_BUTTRESS_WP_REQ_PAYLOAD2, CONFIG, config, val);
	val = REG_SET_FLD_NUM(VPU_40XX_BUTTRESS_WP_REQ_PAYLOAD2, CDYN, cdyn, val);
	REGB_WR32(VPU_40XX_BUTTRESS_WP_REQ_PAYLOAD2, val);

	val = REGB_RD32(VPU_40XX_BUTTRESS_WP_REQ_CMD);
	val = REG_SET_FLD(VPU_40XX_BUTTRESS_WP_REQ_CMD, SEND, val);
	REGB_WR32(VPU_40XX_BUTTRESS_WP_REQ_CMD, val);

	ret = ivpu_pll_wait_for_cmd_send(vdev);
	if (ret)
		ivpu_err(vdev, "Failed to sync after WP request: %d\n", ret);

	return ret;
}

static int ivpu_pll_wait_for_status_ready(struct ivpu_device *vdev)
{
	return REGB_POLL_FLD(VPU_40XX_BUTTRESS_VPU_STATUS, READY, 1, PLL_TIMEOUT_US);
}

static int ivpu_wait_for_clock_own_resource_ack(struct ivpu_device *vdev)
{
	if (ivpu_is_simics(vdev))
		return 0;

	return REGB_POLL_FLD(VPU_40XX_BUTTRESS_VPU_STATUS, CLOCK_RESOURCE_OWN_ACK, 1, TIMEOUT_US);
}

static void ivpu_pll_init_frequency_ratios(struct ivpu_device *vdev)
{
	struct ivpu_hw_info *hw = vdev->hw;
	u8 fuse_min_ratio, fuse_pn_ratio, fuse_max_ratio;
	u32 fmin_fuse, fmax_fuse;

	fmin_fuse = REGB_RD32(VPU_40XX_BUTTRESS_FMIN_FUSE);
	fuse_min_ratio = REG_GET_FLD(VPU_40XX_BUTTRESS_FMIN_FUSE, MIN_RATIO, fmin_fuse);
	fuse_pn_ratio = REG_GET_FLD(VPU_40XX_BUTTRESS_FMIN_FUSE, PN_RATIO, fmin_fuse);

	fmax_fuse = REGB_RD32(VPU_40XX_BUTTRESS_FMAX_FUSE);
	fuse_max_ratio = REG_GET_FLD(VPU_40XX_BUTTRESS_FMAX_FUSE, MAX_RATIO, fmax_fuse);

	hw->pll.min_ratio = clamp_t(u8, ivpu_pll_min_ratio, fuse_min_ratio, fuse_max_ratio);
	hw->pll.max_ratio = clamp_t(u8, ivpu_pll_max_ratio, hw->pll.min_ratio, fuse_max_ratio);
	hw->pll.pn_ratio = clamp_t(u8, fuse_pn_ratio, hw->pll.min_ratio, hw->pll.max_ratio);
}

static int ivpu_pll_drive(struct ivpu_device *vdev, bool enable)
{
	u16 config = enable ? PLL_CONFIG_DEFAULT : 0;
	u16 cdyn = enable ? PLL_CDYN_DEFAULT : 0;
	u16 epp = enable ? PLL_EPP_DEFAULT : 0;
	struct ivpu_hw_info *hw = vdev->hw;
	u16 target_ratio = hw->pll.pn_ratio;
	int ret;

	ivpu_dbg(vdev, PM, "PLL workpoint request: %u Hz, epp: 0x%x, config: 0x%x, cdyn: 0x%x\n",
		 PLL_RATIO_TO_FREQ(target_ratio), epp, config, cdyn);

	ret = ivpu_pll_cmd_send(vdev, hw->pll.min_ratio, hw->pll.max_ratio,
				target_ratio, epp, config, cdyn);
	if (ret) {
		ivpu_err(vdev, "Failed to send PLL workpoint request: %d\n", ret);
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

static void ivpu_boot_host_ss_rst_drive(struct ivpu_device *vdev, bool enable)
{
	u32 val = REGV_RD32(VPU_40XX_HOST_SS_CPR_RST_EN);

	if (enable) {
		val = REG_SET_FLD(VPU_40XX_HOST_SS_CPR_RST_EN, TOP_NOC, val);
		val = REG_SET_FLD(VPU_40XX_HOST_SS_CPR_RST_EN, DSS_MAS, val);
		val = REG_SET_FLD(VPU_40XX_HOST_SS_CPR_RST_EN, CSS_MAS, val);
	} else {
		val = REG_CLR_FLD(VPU_40XX_HOST_SS_CPR_RST_EN, TOP_NOC, val);
		val = REG_CLR_FLD(VPU_40XX_HOST_SS_CPR_RST_EN, DSS_MAS, val);
		val = REG_CLR_FLD(VPU_40XX_HOST_SS_CPR_RST_EN, CSS_MAS, val);
	}

	REGV_WR32(VPU_40XX_HOST_SS_CPR_RST_EN, val);
}

static void ivpu_boot_host_ss_clk_drive(struct ivpu_device *vdev, bool enable)
{
	u32 val = REGV_RD32(VPU_40XX_HOST_SS_CPR_CLK_EN);

	if (enable) {
		val = REG_SET_FLD(VPU_40XX_HOST_SS_CPR_CLK_EN, TOP_NOC, val);
		val = REG_SET_FLD(VPU_40XX_HOST_SS_CPR_CLK_EN, DSS_MAS, val);
		val = REG_SET_FLD(VPU_40XX_HOST_SS_CPR_CLK_EN, CSS_MAS, val);
	} else {
		val = REG_CLR_FLD(VPU_40XX_HOST_SS_CPR_CLK_EN, TOP_NOC, val);
		val = REG_CLR_FLD(VPU_40XX_HOST_SS_CPR_CLK_EN, DSS_MAS, val);
		val = REG_CLR_FLD(VPU_40XX_HOST_SS_CPR_CLK_EN, CSS_MAS, val);
	}

	REGV_WR32(VPU_40XX_HOST_SS_CPR_CLK_EN, val);
}

static int ivpu_boot_noc_qreqn_check(struct ivpu_device *vdev, u32 exp_val)
{
	u32 val = REGV_RD32(VPU_40XX_HOST_SS_NOC_QREQN);

	if (!REG_TEST_FLD_NUM(VPU_40XX_HOST_SS_NOC_QREQN, TOP_SOCMMIO, exp_val, val))
		return -EIO;

	return 0;
}

static int ivpu_boot_noc_qacceptn_check(struct ivpu_device *vdev, u32 exp_val)
{
	u32 val = REGV_RD32(VPU_40XX_HOST_SS_NOC_QACCEPTN);

	if (!REG_TEST_FLD_NUM(VPU_40XX_HOST_SS_NOC_QACCEPTN, TOP_SOCMMIO, exp_val, val))
		return -EIO;

	return 0;
}

static int ivpu_boot_noc_qdeny_check(struct ivpu_device *vdev, u32 exp_val)
{
	u32 val = REGV_RD32(VPU_40XX_HOST_SS_NOC_QDENY);

	if (!REG_TEST_FLD_NUM(VPU_40XX_HOST_SS_NOC_QDENY, TOP_SOCMMIO, exp_val, val))
		return -EIO;

	return 0;
}

static int ivpu_boot_top_noc_qrenqn_check(struct ivpu_device *vdev, u32 exp_val)
{
	u32 val = REGV_RD32(VPU_40XX_TOP_NOC_QREQN);

	if (!REG_TEST_FLD_NUM(VPU_40XX_TOP_NOC_QREQN, CPU_CTRL, exp_val, val) ||
	    !REG_TEST_FLD_NUM(VPU_40XX_TOP_NOC_QREQN, HOSTIF_L2CACHE, exp_val, val))
		return -EIO;

	return 0;
}

static int ivpu_boot_top_noc_qacceptn_check(struct ivpu_device *vdev, u32 exp_val)
{
	u32 val = REGV_RD32(VPU_40XX_TOP_NOC_QACCEPTN);

	if (!REG_TEST_FLD_NUM(VPU_40XX_TOP_NOC_QACCEPTN, CPU_CTRL, exp_val, val) ||
	    !REG_TEST_FLD_NUM(VPU_40XX_TOP_NOC_QACCEPTN, HOSTIF_L2CACHE, exp_val, val))
		return -EIO;

	return 0;
}

static int ivpu_boot_top_noc_qdeny_check(struct ivpu_device *vdev, u32 exp_val)
{
	u32 val = REGV_RD32(VPU_40XX_TOP_NOC_QDENY);

	if (!REG_TEST_FLD_NUM(VPU_40XX_TOP_NOC_QDENY, CPU_CTRL, exp_val, val) ||
	    !REG_TEST_FLD_NUM(VPU_40XX_TOP_NOC_QDENY, HOSTIF_L2CACHE, exp_val, val))
		return -EIO;

	return 0;
}

static void ivpu_boot_idle_gen_drive(struct ivpu_device *vdev, bool enable)
{
	u32 val = REGV_RD32(VPU_40XX_HOST_SS_AON_IDLE_GEN);

	if (enable)
		val = REG_SET_FLD(VPU_40XX_HOST_SS_AON_IDLE_GEN, EN, val);
	else
		val = REG_CLR_FLD(VPU_40XX_HOST_SS_AON_IDLE_GEN, EN, val);

	REGV_WR32(VPU_40XX_HOST_SS_AON_IDLE_GEN, val);
}

static int ivpu_boot_host_ss_check(struct ivpu_device *vdev)
{
	int ret;

	ret = ivpu_boot_noc_qreqn_check(vdev, 0x0);
	if (ret) {
		ivpu_err(vdev, "Failed qreqn check: %d\n", ret);
		return ret;
	}

	ret = ivpu_boot_noc_qacceptn_check(vdev, 0x0);
	if (ret) {
		ivpu_err(vdev, "Failed qacceptn check: %d\n", ret);
		return ret;
	}

	ret = ivpu_boot_noc_qdeny_check(vdev, 0x0);
	if (ret)
		ivpu_err(vdev, "Failed qdeny check %d\n", ret);

	return ret;
}

static int ivpu_boot_host_ss_axi_drive(struct ivpu_device *vdev, bool enable)
{
	int ret;
	u32 val;

	val = REGV_RD32(VPU_40XX_HOST_SS_NOC_QREQN);
	if (enable)
		val = REG_SET_FLD(VPU_40XX_HOST_SS_NOC_QREQN, TOP_SOCMMIO, val);
	else
		val = REG_CLR_FLD(VPU_40XX_HOST_SS_NOC_QREQN, TOP_SOCMMIO, val);
	REGV_WR32(VPU_40XX_HOST_SS_NOC_QREQN, val);

	ret = ivpu_boot_noc_qacceptn_check(vdev, enable ? 0x1 : 0x0);
	if (ret) {
		ivpu_err(vdev, "Failed qacceptn check: %d\n", ret);
		return ret;
	}

	ret = ivpu_boot_noc_qdeny_check(vdev, 0x0);
	if (ret) {
		ivpu_err(vdev, "Failed qdeny check: %d\n", ret);
		return ret;
	}

	if (enable) {
		REGB_WR32(VPU_40XX_BUTTRESS_PORT_ARBITRATION_WEIGHTS, WEIGHTS_DEFAULT);
		REGB_WR32(VPU_40XX_BUTTRESS_PORT_ARBITRATION_WEIGHTS_ATS, WEIGHTS_ATS_DEFAULT);
	}

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

	val = REGV_RD32(VPU_40XX_TOP_NOC_QREQN);
	if (enable) {
		val = REG_SET_FLD(VPU_40XX_TOP_NOC_QREQN, CPU_CTRL, val);
		val = REG_SET_FLD(VPU_40XX_TOP_NOC_QREQN, HOSTIF_L2CACHE, val);
	} else {
		val = REG_CLR_FLD(VPU_40XX_TOP_NOC_QREQN, CPU_CTRL, val);
		val = REG_CLR_FLD(VPU_40XX_TOP_NOC_QREQN, HOSTIF_L2CACHE, val);
	}
	REGV_WR32(VPU_40XX_TOP_NOC_QREQN, val);

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
	u32 val = REGV_RD32(VPU_40XX_HOST_SS_AON_PWR_ISLAND_TRICKLE_EN0);

	if (enable)
		val = REG_SET_FLD(VPU_40XX_HOST_SS_AON_PWR_ISLAND_TRICKLE_EN0, CSS_CPU, val);
	else
		val = REG_CLR_FLD(VPU_40XX_HOST_SS_AON_PWR_ISLAND_TRICKLE_EN0, CSS_CPU, val);

	REGV_WR32(VPU_40XX_HOST_SS_AON_PWR_ISLAND_TRICKLE_EN0, val);

	if (enable)
		ndelay(500);
}

static void ivpu_boot_pwr_island_drive(struct ivpu_device *vdev, bool enable)
{
	u32 val = REGV_RD32(VPU_40XX_HOST_SS_AON_PWR_ISLAND_EN0);

	if (enable)
		val = REG_SET_FLD(VPU_40XX_HOST_SS_AON_PWR_ISLAND_EN0, CSS_CPU, val);
	else
		val = REG_CLR_FLD(VPU_40XX_HOST_SS_AON_PWR_ISLAND_EN0, CSS_CPU, val);

	REGV_WR32(VPU_40XX_HOST_SS_AON_PWR_ISLAND_EN0, val);

	if (!enable)
		ndelay(500);
}

static int ivpu_boot_wait_for_pwr_island_status(struct ivpu_device *vdev, u32 exp_val)
{
	if (ivpu_is_fpga(vdev))
		return 0;

	return REGV_POLL_FLD(VPU_40XX_HOST_SS_AON_PWR_ISLAND_STATUS0, CSS_CPU,
			     exp_val, PWR_ISLAND_STATUS_TIMEOUT_US);
}

static void ivpu_boot_pwr_island_isolation_drive(struct ivpu_device *vdev, bool enable)
{
	u32 val = REGV_RD32(VPU_40XX_HOST_SS_AON_PWR_ISO_EN0);

	if (enable)
		val = REG_SET_FLD(VPU_40XX_HOST_SS_AON_PWR_ISO_EN0, CSS_CPU, val);
	else
		val = REG_CLR_FLD(VPU_40XX_HOST_SS_AON_PWR_ISO_EN0, CSS_CPU, val);

	REGV_WR32(VPU_40XX_HOST_SS_AON_PWR_ISO_EN0, val);
}

static void ivpu_boot_no_snoop_enable(struct ivpu_device *vdev)
{
	u32 val = REGV_RD32(VPU_40XX_HOST_IF_TCU_PTW_OVERRIDES);

	val = REG_SET_FLD(VPU_40XX_HOST_IF_TCU_PTW_OVERRIDES, SNOOP_OVERRIDE_EN, val);
	val = REG_CLR_FLD(VPU_40XX_HOST_IF_TCU_PTW_OVERRIDES, AW_SNOOP_OVERRIDE, val);
	val = REG_CLR_FLD(VPU_40XX_HOST_IF_TCU_PTW_OVERRIDES, AR_SNOOP_OVERRIDE, val);

	REGV_WR32(VPU_40XX_HOST_IF_TCU_PTW_OVERRIDES, val);
}

static void ivpu_boot_tbu_mmu_enable(struct ivpu_device *vdev)
{
	u32 val = REGV_RD32(VPU_40XX_HOST_IF_TBU_MMUSSIDV);

	val = REG_SET_FLD(VPU_40XX_HOST_IF_TBU_MMUSSIDV, TBU0_AWMMUSSIDV, val);
	val = REG_SET_FLD(VPU_40XX_HOST_IF_TBU_MMUSSIDV, TBU0_ARMMUSSIDV, val);
	val = REG_SET_FLD(VPU_40XX_HOST_IF_TBU_MMUSSIDV, TBU1_AWMMUSSIDV, val);
	val = REG_SET_FLD(VPU_40XX_HOST_IF_TBU_MMUSSIDV, TBU1_ARMMUSSIDV, val);
	val = REG_SET_FLD(VPU_40XX_HOST_IF_TBU_MMUSSIDV, TBU2_AWMMUSSIDV, val);
	val = REG_SET_FLD(VPU_40XX_HOST_IF_TBU_MMUSSIDV, TBU2_ARMMUSSIDV, val);

	REGV_WR32(VPU_40XX_HOST_IF_TBU_MMUSSIDV, val);
}

static int ivpu_boot_cpu_noc_qacceptn_check(struct ivpu_device *vdev, u32 exp_val)
{
	u32 val = REGV_RD32(VPU_40XX_CPU_SS_CPR_NOC_QACCEPTN);

	if (!REG_TEST_FLD_NUM(VPU_40XX_CPU_SS_CPR_NOC_QACCEPTN, TOP_MMIO, exp_val, val))
		return -EIO;

	return 0;
}

static int ivpu_boot_cpu_noc_qdeny_check(struct ivpu_device *vdev, u32 exp_val)
{
	u32 val = REGV_RD32(VPU_40XX_CPU_SS_CPR_NOC_QDENY);

	if (!REG_TEST_FLD_NUM(VPU_40XX_CPU_SS_CPR_NOC_QDENY, TOP_MMIO, exp_val, val))
		return -EIO;

	return 0;
}

static int ivpu_boot_pwr_domain_enable(struct ivpu_device *vdev)
{
	int ret;

	ret = ivpu_wait_for_clock_own_resource_ack(vdev);
	if (ret) {
		ivpu_err(vdev, "Timed out waiting for clock own resource ACK\n");
		return ret;
	}

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
	ivpu_boot_host_ss_rst_drive(vdev, true);
	ivpu_boot_pwr_island_isolation_drive(vdev, false);

	return ret;
}

static int ivpu_boot_soc_cpu_drive(struct ivpu_device *vdev, bool enable)
{
	int ret;
	u32 val;

	val = REGV_RD32(VPU_40XX_CPU_SS_CPR_NOC_QREQN);
	if (enable)
		val = REG_SET_FLD(VPU_40XX_CPU_SS_CPR_NOC_QREQN, TOP_MMIO, val);
	else
		val = REG_CLR_FLD(VPU_40XX_CPU_SS_CPR_NOC_QREQN, TOP_MMIO, val);
	REGV_WR32(VPU_40XX_CPU_SS_CPR_NOC_QREQN, val);

	ret = ivpu_boot_cpu_noc_qacceptn_check(vdev, enable ? 0x1 : 0x0);
	if (ret) {
		ivpu_err(vdev, "Failed qacceptn check: %d\n", ret);
		return ret;
	}

	ret = ivpu_boot_cpu_noc_qdeny_check(vdev, 0x0);
	if (ret)
		ivpu_err(vdev, "Failed qdeny check: %d\n", ret);

	return ret;
}

static int ivpu_boot_soc_cpu_enable(struct ivpu_device *vdev)
{
	return ivpu_boot_soc_cpu_drive(vdev, true);
}

static int ivpu_boot_soc_cpu_boot(struct ivpu_device *vdev)
{
	int ret;
	u32 val;
	u64 val64;

	ret = ivpu_boot_soc_cpu_enable(vdev);
	if (ret) {
		ivpu_err(vdev, "Failed to enable SOC CPU: %d\n", ret);
		return ret;
	}

	val64 = vdev->fw->entry_point;
	val64 <<= ffs(VPU_40XX_HOST_SS_VERIFICATION_ADDRESS_LO_IMAGE_LOCATION_MASK) - 1;
	REGV_WR64(VPU_40XX_HOST_SS_VERIFICATION_ADDRESS_LO, val64);

	val = REGV_RD32(VPU_40XX_HOST_SS_VERIFICATION_ADDRESS_LO);
	val = REG_SET_FLD(VPU_40XX_HOST_SS_VERIFICATION_ADDRESS_LO, DONE, val);
	REGV_WR32(VPU_40XX_HOST_SS_VERIFICATION_ADDRESS_LO, val);

	ivpu_dbg(vdev, PM, "Booting firmware, mode: %s\n",
		 ivpu_fw_is_cold_boot(vdev) ? "cold boot" : "resume");

	return 0;
}

static int ivpu_boot_d0i3_drive(struct ivpu_device *vdev, bool enable)
{
	int ret;
	u32 val;

	ret = REGB_POLL_FLD(VPU_40XX_BUTTRESS_D0I3_CONTROL, INPROGRESS, 0, TIMEOUT_US);
	if (ret) {
		ivpu_err(vdev, "Failed to sync before D0i3 transition: %d\n", ret);
		return ret;
	}

	val = REGB_RD32(VPU_40XX_BUTTRESS_D0I3_CONTROL);
	if (enable)
		val = REG_SET_FLD(VPU_40XX_BUTTRESS_D0I3_CONTROL, I3, val);
	else
		val = REG_CLR_FLD(VPU_40XX_BUTTRESS_D0I3_CONTROL, I3, val);
	REGB_WR32(VPU_40XX_BUTTRESS_D0I3_CONTROL, val);

	ret = REGB_POLL_FLD(VPU_40XX_BUTTRESS_D0I3_CONTROL, INPROGRESS, 0, TIMEOUT_US);
	if (ret) {
		ivpu_err(vdev, "Failed to sync after D0i3 transition: %d\n", ret);
		return ret;
	}

	return 0;
}

static bool ivpu_tile_disable_check(u32 config)
{
	/* Allowed values: 0 or one bit from range 0-5 (6 tiles) */
	if (config == 0)
		return true;

	if (config > BIT(TILE_MAX_NUM - 1))
		return false;

	if ((config & (config - 1)) == 0)
		return true;

	return false;
}

static int ivpu_hw_40xx_info_init(struct ivpu_device *vdev)
{
	struct ivpu_hw_info *hw = vdev->hw;
	u32 tile_disable;
	u32 tile_enable;
	u32 fuse;

	fuse = REGB_RD32(VPU_40XX_BUTTRESS_TILE_FUSE);
	if (!REG_TEST_FLD(VPU_40XX_BUTTRESS_TILE_FUSE, VALID, fuse)) {
		ivpu_err(vdev, "Fuse: invalid (0x%x)\n", fuse);
		return -EIO;
	}

	tile_disable = REG_GET_FLD(VPU_40XX_BUTTRESS_TILE_FUSE, CONFIG, fuse);
	if (!ivpu_tile_disable_check(tile_disable)) {
		ivpu_err(vdev, "Fuse: Invalid tile disable config (0x%x)\n", tile_disable);
		return -EIO;
	}

	if (tile_disable)
		ivpu_dbg(vdev, MISC, "Fuse: %d tiles enabled. Tile number %d disabled\n",
			 TILE_MAX_NUM - 1, ffs(tile_disable) - 1);
	else
		ivpu_dbg(vdev, MISC, "Fuse: All %d tiles enabled\n", TILE_MAX_NUM);

	tile_enable = (~tile_disable) & TILE_MAX_MASK;

	hw->sku = REG_SET_FLD_NUM(SKU, HW_ID, LNL_HW_ID, hw->sku);
	hw->sku = REG_SET_FLD_NUM(SKU, TILE, tile_enable, hw->sku);
	hw->tile_fuse = tile_disable;
	hw->pll.profiling_freq = PLL_PROFILING_FREQ_DEFAULT;

	ivpu_pll_init_frequency_ratios(vdev);

	ivpu_hw_init_range(&vdev->hw->ranges.global, 0x80000000, SZ_512M);
	ivpu_hw_init_range(&vdev->hw->ranges.user,   0x80000000, SZ_256M);
	ivpu_hw_init_range(&vdev->hw->ranges.shave,  0x80000000 + SZ_256M, SZ_2G - SZ_256M);
	ivpu_hw_init_range(&vdev->hw->ranges.dma,   0x200000000, SZ_8G);

	return 0;
}

static int ivpu_hw_40xx_reset(struct ivpu_device *vdev)
{
	int ret;
	u32 val;

	ret = REGB_POLL_FLD(VPU_40XX_BUTTRESS_IP_RESET, TRIGGER, 0, TIMEOUT_US);
	if (ret) {
		ivpu_err(vdev, "Wait for *_TRIGGER timed out\n");
		return ret;
	}

	val = REGB_RD32(VPU_40XX_BUTTRESS_IP_RESET);
	val = REG_SET_FLD(VPU_40XX_BUTTRESS_IP_RESET, TRIGGER, val);
	REGB_WR32(VPU_40XX_BUTTRESS_IP_RESET, val);

	ret = REGB_POLL_FLD(VPU_40XX_BUTTRESS_IP_RESET, TRIGGER, 0, TIMEOUT_US);
	if (ret)
		ivpu_err(vdev, "Timed out waiting for RESET completion\n");

	return ret;
}

static int ivpu_hw_40xx_d0i3_enable(struct ivpu_device *vdev)
{
	int ret;

	if (IVPU_WA(punit_disabled))
		return 0;

	ret = ivpu_boot_d0i3_drive(vdev, true);
	if (ret)
		ivpu_err(vdev, "Failed to enable D0i3: %d\n", ret);

	udelay(5); /* VPU requires 5 us to complete the transition */

	return ret;
}

static int ivpu_hw_40xx_d0i3_disable(struct ivpu_device *vdev)
{
	int ret;

	if (IVPU_WA(punit_disabled))
		return 0;

	ret = ivpu_boot_d0i3_drive(vdev, false);
	if (ret)
		ivpu_err(vdev, "Failed to disable D0i3: %d\n", ret);

	return ret;
}

static void ivpu_hw_40xx_profiling_freq_reg_set(struct ivpu_device *vdev)
{
	u32 val = REGB_RD32(VPU_40XX_BUTTRESS_VPU_STATUS);

	if (vdev->hw->pll.profiling_freq == PLL_PROFILING_FREQ_DEFAULT)
		val = REG_CLR_FLD(VPU_40XX_BUTTRESS_VPU_STATUS, PERF_CLK, val);
	else
		val = REG_SET_FLD(VPU_40XX_BUTTRESS_VPU_STATUS, PERF_CLK, val);

	REGB_WR32(VPU_40XX_BUTTRESS_VPU_STATUS, val);
}

static void ivpu_hw_40xx_ats_print(struct ivpu_device *vdev)
{
	ivpu_dbg(vdev, MISC, "Buttress ATS: %s\n",
		 REGB_RD32(VPU_40XX_BUTTRESS_HM_ATS) ? "Enable" : "Disable");
}

static void ivpu_hw_40xx_clock_relinquish_disable(struct ivpu_device *vdev)
{
	u32 val = REGB_RD32(VPU_40XX_BUTTRESS_VPU_STATUS);

	val = REG_SET_FLD(VPU_40XX_BUTTRESS_VPU_STATUS, DISABLE_CLK_RELINQUISH, val);
	REGB_WR32(VPU_40XX_BUTTRESS_VPU_STATUS, val);
}

static int ivpu_hw_40xx_power_up(struct ivpu_device *vdev)
{
	int ret;

	ret = ivpu_hw_40xx_reset(vdev);
	if (ret) {
		ivpu_err(vdev, "Failed to reset HW: %d\n", ret);
		return ret;
	}

	ivpu_hw_read_platform(vdev);
	ivpu_hw_wa_init(vdev);
	ivpu_hw_timeouts_init(vdev);

	ret = ivpu_hw_40xx_d0i3_disable(vdev);
	if (ret)
		ivpu_warn(vdev, "Failed to disable D0I3: %d\n", ret);

	ret = ivpu_pll_enable(vdev);
	if (ret) {
		ivpu_err(vdev, "Failed to enable PLL: %d\n", ret);
		return ret;
	}

	if (IVPU_WA(disable_clock_relinquish))
		ivpu_hw_40xx_clock_relinquish_disable(vdev);
	ivpu_hw_40xx_profiling_freq_reg_set(vdev);
	ivpu_hw_40xx_ats_print(vdev);

	ret = ivpu_boot_host_ss_check(vdev);
	if (ret) {
		ivpu_err(vdev, "Failed to configure host SS: %d\n", ret);
		return ret;
	}

	ivpu_boot_idle_gen_drive(vdev, false);

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

static int ivpu_hw_40xx_boot_fw(struct ivpu_device *vdev)
{
	int ret;

	ivpu_boot_no_snoop_enable(vdev);
	ivpu_boot_tbu_mmu_enable(vdev);

	ret = ivpu_boot_soc_cpu_boot(vdev);
	if (ret)
		ivpu_err(vdev, "Failed to boot SOC CPU: %d\n", ret);

	return ret;
}

static bool ivpu_hw_40xx_is_idle(struct ivpu_device *vdev)
{
	u32 val;

	if (IVPU_WA(punit_disabled))
		return true;

	val = REGB_RD32(VPU_40XX_BUTTRESS_VPU_STATUS);
	return REG_TEST_FLD(VPU_40XX_BUTTRESS_VPU_STATUS, READY, val) &&
	       REG_TEST_FLD(VPU_40XX_BUTTRESS_VPU_STATUS, IDLE, val);
}

static int ivpu_hw_40xx_power_down(struct ivpu_device *vdev)
{
	int ret = 0;

	if (!ivpu_hw_40xx_is_idle(vdev) && ivpu_hw_40xx_reset(vdev))
		ivpu_warn(vdev, "Failed to reset the VPU\n");

	if (ivpu_pll_disable(vdev)) {
		ivpu_err(vdev, "Failed to disable PLL\n");
		ret = -EIO;
	}

	if (ivpu_hw_40xx_d0i3_enable(vdev)) {
		ivpu_err(vdev, "Failed to enter D0I3\n");
		ret = -EIO;
	}

	return ret;
}

static void ivpu_hw_40xx_wdt_disable(struct ivpu_device *vdev)
{
	u32 val;

	REGV_WR32(VPU_40XX_CPU_SS_TIM_SAFE, TIM_SAFE_ENABLE);
	REGV_WR32(VPU_40XX_CPU_SS_TIM_WATCHDOG, TIM_WATCHDOG_RESET_VALUE);

	REGV_WR32(VPU_40XX_CPU_SS_TIM_SAFE, TIM_SAFE_ENABLE);
	REGV_WR32(VPU_40XX_CPU_SS_TIM_WDOG_EN, 0);

	val = REGV_RD32(VPU_40XX_CPU_SS_TIM_GEN_CONFIG);
	val = REG_CLR_FLD(VPU_40XX_CPU_SS_TIM_GEN_CONFIG, WDOG_TO_INT_CLR, val);
	REGV_WR32(VPU_40XX_CPU_SS_TIM_GEN_CONFIG, val);
}

/* Register indirect accesses */
static u32 ivpu_hw_40xx_reg_pll_freq_get(struct ivpu_device *vdev)
{
	u32 pll_curr_ratio;

	pll_curr_ratio = REGB_RD32(VPU_40XX_BUTTRESS_PLL_FREQ);
	pll_curr_ratio &= VPU_40XX_BUTTRESS_PLL_FREQ_RATIO_MASK;

	return PLL_RATIO_TO_FREQ(pll_curr_ratio);
}

static u32 ivpu_hw_40xx_reg_telemetry_offset_get(struct ivpu_device *vdev)
{
	return REGB_RD32(VPU_40XX_BUTTRESS_VPU_TELEMETRY_OFFSET);
}

static u32 ivpu_hw_40xx_reg_telemetry_size_get(struct ivpu_device *vdev)
{
	return REGB_RD32(VPU_40XX_BUTTRESS_VPU_TELEMETRY_SIZE);
}

static u32 ivpu_hw_40xx_reg_telemetry_enable_get(struct ivpu_device *vdev)
{
	return REGB_RD32(VPU_40XX_BUTTRESS_VPU_TELEMETRY_ENABLE);
}

static void ivpu_hw_40xx_reg_db_set(struct ivpu_device *vdev, u32 db_id)
{
	u32 reg_stride = VPU_40XX_CPU_SS_DOORBELL_1 - VPU_40XX_CPU_SS_DOORBELL_0;
	u32 val = REG_FLD(VPU_40XX_CPU_SS_DOORBELL_0, SET);

	REGV_WR32I(VPU_40XX_CPU_SS_DOORBELL_0, reg_stride, db_id, val);
}

static u32 ivpu_hw_40xx_reg_ipc_rx_addr_get(struct ivpu_device *vdev)
{
	return REGV_RD32(VPU_40XX_HOST_SS_TIM_IPC_FIFO_ATM);
}

static u32 ivpu_hw_40xx_reg_ipc_rx_count_get(struct ivpu_device *vdev)
{
	u32 count = REGV_RD32_SILENT(VPU_40XX_HOST_SS_TIM_IPC_FIFO_STAT);

	return REG_GET_FLD(VPU_40XX_HOST_SS_TIM_IPC_FIFO_STAT, FILL_LEVEL, count);
}

static void ivpu_hw_40xx_reg_ipc_tx_set(struct ivpu_device *vdev, u32 vpu_addr)
{
	REGV_WR32(VPU_40XX_CPU_SS_TIM_IPC_FIFO, vpu_addr);
}

static void ivpu_hw_40xx_irq_clear(struct ivpu_device *vdev)
{
	REGV_WR64(VPU_40XX_HOST_SS_ICB_CLEAR_0, ICB_0_1_IRQ_MASK);
}

static void ivpu_hw_40xx_irq_enable(struct ivpu_device *vdev)
{
	REGV_WR32(VPU_40XX_HOST_SS_FW_SOC_IRQ_EN, ITF_FIREWALL_VIOLATION_MASK);
	REGV_WR64(VPU_40XX_HOST_SS_ICB_ENABLE_0, ICB_0_1_IRQ_MASK);
	REGB_WR32(VPU_40XX_BUTTRESS_LOCAL_INT_MASK, BUTTRESS_IRQ_ENABLE_MASK);
	REGB_WR32(VPU_40XX_BUTTRESS_GLOBAL_INT_MASK, 0x0);
}

static void ivpu_hw_40xx_irq_disable(struct ivpu_device *vdev)
{
	REGB_WR32(VPU_40XX_BUTTRESS_GLOBAL_INT_MASK, 0x1);
	REGB_WR32(VPU_40XX_BUTTRESS_LOCAL_INT_MASK, BUTTRESS_IRQ_DISABLE_MASK);
	REGV_WR64(VPU_40XX_HOST_SS_ICB_ENABLE_0, 0x0ull);
	REGV_WR32(VPU_40XX_HOST_SS_FW_SOC_IRQ_EN, 0x0ul);
}

static void ivpu_hw_40xx_irq_wdt_nce_handler(struct ivpu_device *vdev)
{
	/* TODO: For LNN hang consider engine reset instead of full recovery */
	ivpu_pm_schedule_recovery(vdev);
}

static void ivpu_hw_40xx_irq_wdt_mss_handler(struct ivpu_device *vdev)
{
	ivpu_hw_wdt_disable(vdev);
	ivpu_pm_schedule_recovery(vdev);
}

static void ivpu_hw_40xx_irq_noc_firewall_handler(struct ivpu_device *vdev)
{
	ivpu_pm_schedule_recovery(vdev);
}

/* Handler for IRQs from VPU core (irqV) */
static irqreturn_t ivpu_hw_40xx_irqv_handler(struct ivpu_device *vdev, int irq)
{
	u32 status = REGV_RD32(VPU_40XX_HOST_SS_ICB_STATUS_0) & ICB_0_IRQ_MASK;
	irqreturn_t ret = IRQ_NONE;

	if (!status)
		return IRQ_NONE;

	REGV_WR32(VPU_40XX_HOST_SS_ICB_CLEAR_0, status);

	if (REG_TEST_FLD(VPU_40XX_HOST_SS_ICB_STATUS_0, MMU_IRQ_0_INT, status))
		ivpu_mmu_irq_evtq_handler(vdev);

	if (REG_TEST_FLD(VPU_40XX_HOST_SS_ICB_STATUS_0, HOST_IPC_FIFO_INT, status))
		ret |= ivpu_ipc_irq_handler(vdev);

	if (REG_TEST_FLD(VPU_40XX_HOST_SS_ICB_STATUS_0, MMU_IRQ_1_INT, status))
		ivpu_dbg(vdev, IRQ, "MMU sync complete\n");

	if (REG_TEST_FLD(VPU_40XX_HOST_SS_ICB_STATUS_0, MMU_IRQ_2_INT, status))
		ivpu_mmu_irq_gerr_handler(vdev);

	if (REG_TEST_FLD(VPU_40XX_HOST_SS_ICB_STATUS_0, CPU_INT_REDIRECT_0_INT, status))
		ivpu_hw_40xx_irq_wdt_mss_handler(vdev);

	if (REG_TEST_FLD(VPU_40XX_HOST_SS_ICB_STATUS_0, CPU_INT_REDIRECT_1_INT, status))
		ivpu_hw_40xx_irq_wdt_nce_handler(vdev);

	if (REG_TEST_FLD(VPU_40XX_HOST_SS_ICB_STATUS_0, NOC_FIREWALL_INT, status))
		ivpu_hw_40xx_irq_noc_firewall_handler(vdev);

	return ret;
}

/* Handler for IRQs from Buttress core (irqB) */
static irqreturn_t ivpu_hw_40xx_irqb_handler(struct ivpu_device *vdev, int irq)
{
	bool schedule_recovery = false;
	u32 status = REGB_RD32(VPU_40XX_BUTTRESS_INTERRUPT_STAT) & BUTTRESS_IRQ_MASK;

	if (status == 0)
		return IRQ_NONE;

	if (REG_TEST_FLD(VPU_40XX_BUTTRESS_INTERRUPT_STAT, FREQ_CHANGE, status))
		ivpu_dbg(vdev, IRQ, "FREQ_CHANGE");

	if (REG_TEST_FLD(VPU_40XX_BUTTRESS_INTERRUPT_STAT, ATS_ERR, status)) {
		ivpu_err(vdev, "ATS_ERR LOG1 0x%08x ATS_ERR_LOG2 0x%08x\n",
			 REGB_RD32(VPU_40XX_BUTTRESS_ATS_ERR_LOG1),
			 REGB_RD32(VPU_40XX_BUTTRESS_ATS_ERR_LOG2));
		REGB_WR32(VPU_40XX_BUTTRESS_ATS_ERR_CLEAR, 0x1);
		schedule_recovery = true;
	}

	if (REG_TEST_FLD(VPU_40XX_BUTTRESS_INTERRUPT_STAT, CFI0_ERR, status)) {
		ivpu_err(vdev, "CFI0_ERR 0x%08x", REGB_RD32(VPU_40XX_BUTTRESS_CFI0_ERR_LOG));
		REGB_WR32(VPU_40XX_BUTTRESS_CFI0_ERR_CLEAR, 0x1);
		schedule_recovery = true;
	}

	if (REG_TEST_FLD(VPU_40XX_BUTTRESS_INTERRUPT_STAT, CFI1_ERR, status)) {
		ivpu_err(vdev, "CFI1_ERR 0x%08x", REGB_RD32(VPU_40XX_BUTTRESS_CFI1_ERR_LOG));
		REGB_WR32(VPU_40XX_BUTTRESS_CFI1_ERR_CLEAR, 0x1);
		schedule_recovery = true;
	}

	if (REG_TEST_FLD(VPU_40XX_BUTTRESS_INTERRUPT_STAT, IMR0_ERR, status)) {
		ivpu_err(vdev, "IMR_ERR_CFI0 LOW: 0x%08x HIGH: 0x%08x",
			 REGB_RD32(VPU_40XX_BUTTRESS_IMR_ERR_CFI0_LOW),
			 REGB_RD32(VPU_40XX_BUTTRESS_IMR_ERR_CFI0_HIGH));
		REGB_WR32(VPU_40XX_BUTTRESS_IMR_ERR_CFI0_CLEAR, 0x1);
		schedule_recovery = true;
	}

	if (REG_TEST_FLD(VPU_40XX_BUTTRESS_INTERRUPT_STAT, IMR1_ERR, status)) {
		ivpu_err(vdev, "IMR_ERR_CFI1 LOW: 0x%08x HIGH: 0x%08x",
			 REGB_RD32(VPU_40XX_BUTTRESS_IMR_ERR_CFI1_LOW),
			 REGB_RD32(VPU_40XX_BUTTRESS_IMR_ERR_CFI1_HIGH));
		REGB_WR32(VPU_40XX_BUTTRESS_IMR_ERR_CFI1_CLEAR, 0x1);
		schedule_recovery = true;
	}

	if (REG_TEST_FLD(VPU_40XX_BUTTRESS_INTERRUPT_STAT, SURV_ERR, status)) {
		ivpu_err(vdev, "Survivability error detected\n");
		schedule_recovery = true;
	}

	/* This must be done after interrupts are cleared at the source. */
	REGB_WR32(VPU_40XX_BUTTRESS_INTERRUPT_STAT, status);

	if (schedule_recovery)
		ivpu_pm_schedule_recovery(vdev);

	return IRQ_HANDLED;
}

static irqreturn_t ivpu_hw_40xx_irq_handler(int irq, void *ptr)
{
	struct ivpu_device *vdev = ptr;
	irqreturn_t ret = IRQ_NONE;

	REGB_WR32(VPU_40XX_BUTTRESS_GLOBAL_INT_MASK, 0x1);

	ret |= ivpu_hw_40xx_irqv_handler(vdev, irq);
	ret |= ivpu_hw_40xx_irqb_handler(vdev, irq);

	/* Re-enable global interrupts to re-trigger MSI for pending interrupts */
	REGB_WR32(VPU_40XX_BUTTRESS_GLOBAL_INT_MASK, 0x0);

	if (ret & IRQ_WAKE_THREAD)
		return IRQ_WAKE_THREAD;

	return ret;
}

static void ivpu_hw_40xx_diagnose_failure(struct ivpu_device *vdev)
{
	u32 irqv = REGV_RD32(VPU_40XX_HOST_SS_ICB_STATUS_0) & ICB_0_IRQ_MASK;
	u32 irqb = REGB_RD32(VPU_40XX_BUTTRESS_INTERRUPT_STAT) & BUTTRESS_IRQ_MASK;

	if (ivpu_hw_40xx_reg_ipc_rx_count_get(vdev))
		ivpu_err(vdev, "IPC FIFO queue not empty, missed IPC IRQ");

	if (REG_TEST_FLD(VPU_40XX_HOST_SS_ICB_STATUS_0, CPU_INT_REDIRECT_0_INT, irqv))
		ivpu_err(vdev, "WDT MSS timeout detected\n");

	if (REG_TEST_FLD(VPU_40XX_HOST_SS_ICB_STATUS_0, CPU_INT_REDIRECT_1_INT, irqv))
		ivpu_err(vdev, "WDT NCE timeout detected\n");

	if (REG_TEST_FLD(VPU_40XX_HOST_SS_ICB_STATUS_0, NOC_FIREWALL_INT, irqv))
		ivpu_err(vdev, "NOC Firewall irq detected\n");

	if (REG_TEST_FLD(VPU_40XX_BUTTRESS_INTERRUPT_STAT, ATS_ERR, irqb)) {
		ivpu_err(vdev, "ATS_ERR_LOG1 0x%08x ATS_ERR_LOG2 0x%08x\n",
			 REGB_RD32(VPU_40XX_BUTTRESS_ATS_ERR_LOG1),
			 REGB_RD32(VPU_40XX_BUTTRESS_ATS_ERR_LOG2));
	}

	if (REG_TEST_FLD(VPU_40XX_BUTTRESS_INTERRUPT_STAT, CFI0_ERR, irqb))
		ivpu_err(vdev, "CFI0_ERR_LOG 0x%08x\n", REGB_RD32(VPU_40XX_BUTTRESS_CFI0_ERR_LOG));

	if (REG_TEST_FLD(VPU_40XX_BUTTRESS_INTERRUPT_STAT, CFI1_ERR, irqb))
		ivpu_err(vdev, "CFI1_ERR_LOG 0x%08x\n", REGB_RD32(VPU_40XX_BUTTRESS_CFI1_ERR_LOG));

	if (REG_TEST_FLD(VPU_40XX_BUTTRESS_INTERRUPT_STAT, IMR0_ERR, irqb))
		ivpu_err(vdev, "IMR_ERR_CFI0 LOW: 0x%08x HIGH: 0x%08x\n",
			 REGB_RD32(VPU_40XX_BUTTRESS_IMR_ERR_CFI0_LOW),
			 REGB_RD32(VPU_40XX_BUTTRESS_IMR_ERR_CFI0_HIGH));

	if (REG_TEST_FLD(VPU_40XX_BUTTRESS_INTERRUPT_STAT, IMR1_ERR, irqb))
		ivpu_err(vdev, "IMR_ERR_CFI1 LOW: 0x%08x HIGH: 0x%08x\n",
			 REGB_RD32(VPU_40XX_BUTTRESS_IMR_ERR_CFI1_LOW),
			 REGB_RD32(VPU_40XX_BUTTRESS_IMR_ERR_CFI1_HIGH));

	if (REG_TEST_FLD(VPU_40XX_BUTTRESS_INTERRUPT_STAT, SURV_ERR, irqb))
		ivpu_err(vdev, "Survivability error detected\n");
}

const struct ivpu_hw_ops ivpu_hw_40xx_ops = {
	.info_init = ivpu_hw_40xx_info_init,
	.power_up = ivpu_hw_40xx_power_up,
	.is_idle = ivpu_hw_40xx_is_idle,
	.power_down = ivpu_hw_40xx_power_down,
	.boot_fw = ivpu_hw_40xx_boot_fw,
	.wdt_disable = ivpu_hw_40xx_wdt_disable,
	.diagnose_failure = ivpu_hw_40xx_diagnose_failure,
	.reg_pll_freq_get = ivpu_hw_40xx_reg_pll_freq_get,
	.reg_telemetry_offset_get = ivpu_hw_40xx_reg_telemetry_offset_get,
	.reg_telemetry_size_get = ivpu_hw_40xx_reg_telemetry_size_get,
	.reg_telemetry_enable_get = ivpu_hw_40xx_reg_telemetry_enable_get,
	.reg_db_set = ivpu_hw_40xx_reg_db_set,
	.reg_ipc_rx_addr_get = ivpu_hw_40xx_reg_ipc_rx_addr_get,
	.reg_ipc_rx_count_get = ivpu_hw_40xx_reg_ipc_rx_count_get,
	.reg_ipc_tx_set = ivpu_hw_40xx_reg_ipc_tx_set,
	.irq_clear = ivpu_hw_40xx_irq_clear,
	.irq_enable = ivpu_hw_40xx_irq_enable,
	.irq_disable = ivpu_hw_40xx_irq_disable,
	.irq_handler = ivpu_hw_40xx_irq_handler,
};
