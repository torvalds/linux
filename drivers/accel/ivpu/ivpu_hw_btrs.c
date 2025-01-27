// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2024 Intel Corporation
 */

#include "ivpu_drv.h"
#include "ivpu_hw.h"
#include "ivpu_hw_btrs.h"
#include "ivpu_hw_btrs_lnl_reg.h"
#include "ivpu_hw_btrs_mtl_reg.h"
#include "ivpu_hw_reg_io.h"
#include "ivpu_pm.h"

#define BTRS_MTL_IRQ_MASK ((REG_FLD(VPU_HW_BTRS_MTL_INTERRUPT_STAT, ATS_ERR)) | \
			   (REG_FLD(VPU_HW_BTRS_MTL_INTERRUPT_STAT, UFI_ERR)))

#define BTRS_LNL_IRQ_MASK ((REG_FLD(VPU_HW_BTRS_LNL_INTERRUPT_STAT, ATS_ERR)) | \
			   (REG_FLD(VPU_HW_BTRS_LNL_INTERRUPT_STAT, CFI0_ERR)) | \
			   (REG_FLD(VPU_HW_BTRS_LNL_INTERRUPT_STAT, CFI1_ERR)) | \
			   (REG_FLD(VPU_HW_BTRS_LNL_INTERRUPT_STAT, IMR0_ERR)) | \
			   (REG_FLD(VPU_HW_BTRS_LNL_INTERRUPT_STAT, IMR1_ERR)) | \
			   (REG_FLD(VPU_HW_BTRS_LNL_INTERRUPT_STAT, SURV_ERR)))

#define BTRS_MTL_ALL_IRQ_MASK (BTRS_MTL_IRQ_MASK | (REG_FLD(VPU_HW_BTRS_MTL_INTERRUPT_STAT, \
			       FREQ_CHANGE)))

#define BTRS_IRQ_DISABLE_MASK ((u32)-1)

#define BTRS_LNL_ALL_IRQ_MASK ((u32)-1)

#define BTRS_MTL_WP_CONFIG_1_TILE_5_3_RATIO WP_CONFIG(MTL_CONFIG_1_TILE, MTL_PLL_RATIO_5_3)
#define BTRS_MTL_WP_CONFIG_1_TILE_4_3_RATIO WP_CONFIG(MTL_CONFIG_1_TILE, MTL_PLL_RATIO_4_3)
#define BTRS_MTL_WP_CONFIG_2_TILE_5_3_RATIO WP_CONFIG(MTL_CONFIG_2_TILE, MTL_PLL_RATIO_5_3)
#define BTRS_MTL_WP_CONFIG_2_TILE_4_3_RATIO WP_CONFIG(MTL_CONFIG_2_TILE, MTL_PLL_RATIO_4_3)
#define BTRS_MTL_WP_CONFIG_0_TILE_PLL_OFF   WP_CONFIG(0, 0)

#define PLL_CDYN_DEFAULT               0x80
#define PLL_EPP_DEFAULT                0x80
#define PLL_CONFIG_DEFAULT             0x0
#define PLL_SIMULATION_FREQ            10000000
#define PLL_REF_CLK_FREQ               50000000
#define PLL_TIMEOUT_US		       (1500 * USEC_PER_MSEC)
#define IDLE_TIMEOUT_US		       (5 * USEC_PER_MSEC)
#define TIMEOUT_US                     (150 * USEC_PER_MSEC)

/* Work point configuration values */
#define WP_CONFIG(tile, ratio)         (((tile) << 8) | (ratio))
#define MTL_CONFIG_1_TILE              0x01
#define MTL_CONFIG_2_TILE              0x02
#define MTL_PLL_RATIO_5_3              0x01
#define MTL_PLL_RATIO_4_3              0x02
#define BTRS_MTL_TILE_FUSE_ENABLE_BOTH 0x0
#define BTRS_MTL_TILE_SKU_BOTH         0x3630

#define BTRS_LNL_TILE_MAX_NUM          6
#define BTRS_LNL_TILE_MAX_MASK         0x3f

#define WEIGHTS_DEFAULT                0xf711f711u
#define WEIGHTS_ATS_DEFAULT            0x0000f711u

#define DCT_REQ                        0x2
#define DCT_ENABLE                     0x1
#define DCT_DISABLE                    0x0

int ivpu_hw_btrs_irqs_clear_with_0_mtl(struct ivpu_device *vdev)
{
	REGB_WR32(VPU_HW_BTRS_MTL_INTERRUPT_STAT, BTRS_MTL_ALL_IRQ_MASK);
	if (REGB_RD32(VPU_HW_BTRS_MTL_INTERRUPT_STAT) == BTRS_MTL_ALL_IRQ_MASK) {
		/* Writing 1s does not clear the interrupt status register */
		REGB_WR32(VPU_HW_BTRS_MTL_INTERRUPT_STAT, 0x0);
		return true;
	}

	return false;
}

static void freq_ratios_init_mtl(struct ivpu_device *vdev)
{
	struct ivpu_hw_info *hw = vdev->hw;
	u32 fmin_fuse, fmax_fuse;

	fmin_fuse = REGB_RD32(VPU_HW_BTRS_MTL_FMIN_FUSE);
	hw->pll.min_ratio = REG_GET_FLD(VPU_HW_BTRS_MTL_FMIN_FUSE, MIN_RATIO, fmin_fuse);
	hw->pll.pn_ratio = REG_GET_FLD(VPU_HW_BTRS_MTL_FMIN_FUSE, PN_RATIO, fmin_fuse);

	fmax_fuse = REGB_RD32(VPU_HW_BTRS_MTL_FMAX_FUSE);
	hw->pll.max_ratio = REG_GET_FLD(VPU_HW_BTRS_MTL_FMAX_FUSE, MAX_RATIO, fmax_fuse);
}

static void freq_ratios_init_lnl(struct ivpu_device *vdev)
{
	struct ivpu_hw_info *hw = vdev->hw;
	u32 fmin_fuse, fmax_fuse;

	fmin_fuse = REGB_RD32(VPU_HW_BTRS_LNL_FMIN_FUSE);
	hw->pll.min_ratio = REG_GET_FLD(VPU_HW_BTRS_LNL_FMIN_FUSE, MIN_RATIO, fmin_fuse);
	hw->pll.pn_ratio = REG_GET_FLD(VPU_HW_BTRS_LNL_FMIN_FUSE, PN_RATIO, fmin_fuse);

	fmax_fuse = REGB_RD32(VPU_HW_BTRS_LNL_FMAX_FUSE);
	hw->pll.max_ratio = REG_GET_FLD(VPU_HW_BTRS_LNL_FMAX_FUSE, MAX_RATIO, fmax_fuse);
}

void ivpu_hw_btrs_freq_ratios_init(struct ivpu_device *vdev)
{
	struct ivpu_hw_info *hw = vdev->hw;

	if (ivpu_hw_btrs_gen(vdev) == IVPU_HW_BTRS_MTL)
		freq_ratios_init_mtl(vdev);
	else
		freq_ratios_init_lnl(vdev);

	hw->pll.min_ratio = clamp_t(u8, ivpu_pll_min_ratio, hw->pll.min_ratio, hw->pll.max_ratio);
	hw->pll.max_ratio = clamp_t(u8, ivpu_pll_max_ratio, hw->pll.min_ratio, hw->pll.max_ratio);
	hw->pll.pn_ratio = clamp_t(u8, hw->pll.pn_ratio, hw->pll.min_ratio, hw->pll.max_ratio);
}

static bool tile_disable_check(u32 config)
{
	/* Allowed values: 0 or one bit from range 0-5 (6 tiles) */
	if (config == 0)
		return true;

	if (config > BIT(BTRS_LNL_TILE_MAX_NUM - 1))
		return false;

	if ((config & (config - 1)) == 0)
		return true;

	return false;
}

static int read_tile_config_fuse(struct ivpu_device *vdev, u32 *tile_fuse_config)
{
	u32 fuse;
	u32 config;

	fuse = REGB_RD32(VPU_HW_BTRS_LNL_TILE_FUSE);
	if (!REG_TEST_FLD(VPU_HW_BTRS_LNL_TILE_FUSE, VALID, fuse)) {
		ivpu_err(vdev, "Fuse: invalid (0x%x)\n", fuse);
		return -EIO;
	}

	config = REG_GET_FLD(VPU_HW_BTRS_LNL_TILE_FUSE, CONFIG, fuse);
	if (!tile_disable_check(config))
		ivpu_warn(vdev, "More than 1 tile disabled, tile fuse config mask: 0x%x\n", config);

	ivpu_dbg(vdev, MISC, "Tile disable config mask: 0x%x\n", config);

	*tile_fuse_config = config;
	return 0;
}

static int info_init_mtl(struct ivpu_device *vdev)
{
	struct ivpu_hw_info *hw = vdev->hw;

	hw->tile_fuse = BTRS_MTL_TILE_FUSE_ENABLE_BOTH;
	hw->sku = BTRS_MTL_TILE_SKU_BOTH;
	hw->config = BTRS_MTL_WP_CONFIG_2_TILE_4_3_RATIO;

	return 0;
}

static int info_init_lnl(struct ivpu_device *vdev)
{
	struct ivpu_hw_info *hw = vdev->hw;
	u32 tile_fuse_config;
	int ret;

	ret = read_tile_config_fuse(vdev, &tile_fuse_config);
	if (ret)
		return ret;

	hw->tile_fuse = tile_fuse_config;
	hw->pll.profiling_freq = PLL_PROFILING_FREQ_DEFAULT;

	return 0;
}

int ivpu_hw_btrs_info_init(struct ivpu_device *vdev)
{
	if (ivpu_hw_btrs_gen(vdev) == IVPU_HW_BTRS_MTL)
		return info_init_mtl(vdev);
	else
		return info_init_lnl(vdev);
}

static int wp_request_sync(struct ivpu_device *vdev)
{
	if (ivpu_hw_btrs_gen(vdev) == IVPU_HW_BTRS_MTL)
		return REGB_POLL_FLD(VPU_HW_BTRS_MTL_WP_REQ_CMD, SEND, 0, PLL_TIMEOUT_US);
	else
		return REGB_POLL_FLD(VPU_HW_BTRS_LNL_WP_REQ_CMD, SEND, 0, PLL_TIMEOUT_US);
}

static int wait_for_status_ready(struct ivpu_device *vdev, bool enable)
{
	u32 exp_val = enable ? 0x1 : 0x0;

	if (IVPU_WA(punit_disabled))
		return 0;

	if (ivpu_hw_btrs_gen(vdev) == IVPU_HW_BTRS_MTL)
		return REGB_POLL_FLD(VPU_HW_BTRS_MTL_VPU_STATUS, READY, exp_val, PLL_TIMEOUT_US);
	else
		return REGB_POLL_FLD(VPU_HW_BTRS_LNL_VPU_STATUS, READY, exp_val, PLL_TIMEOUT_US);
}

struct wp_request {
	u16 min;
	u16 max;
	u16 target;
	u16 cfg;
	u16 epp;
	u16 cdyn;
};

static void wp_request_mtl(struct ivpu_device *vdev, struct wp_request *wp)
{
	u32 val;

	val = REGB_RD32(VPU_HW_BTRS_MTL_WP_REQ_PAYLOAD0);
	val = REG_SET_FLD_NUM(VPU_HW_BTRS_MTL_WP_REQ_PAYLOAD0, MIN_RATIO, wp->min, val);
	val = REG_SET_FLD_NUM(VPU_HW_BTRS_MTL_WP_REQ_PAYLOAD0, MAX_RATIO, wp->max, val);
	REGB_WR32(VPU_HW_BTRS_MTL_WP_REQ_PAYLOAD0, val);

	val = REGB_RD32(VPU_HW_BTRS_MTL_WP_REQ_PAYLOAD1);
	val = REG_SET_FLD_NUM(VPU_HW_BTRS_MTL_WP_REQ_PAYLOAD1, TARGET_RATIO, wp->target, val);
	val = REG_SET_FLD_NUM(VPU_HW_BTRS_MTL_WP_REQ_PAYLOAD1, EPP, PLL_EPP_DEFAULT, val);
	REGB_WR32(VPU_HW_BTRS_MTL_WP_REQ_PAYLOAD1, val);

	val = REGB_RD32(VPU_HW_BTRS_MTL_WP_REQ_PAYLOAD2);
	val = REG_SET_FLD_NUM(VPU_HW_BTRS_MTL_WP_REQ_PAYLOAD2, CONFIG, wp->cfg, val);
	REGB_WR32(VPU_HW_BTRS_MTL_WP_REQ_PAYLOAD2, val);

	val = REGB_RD32(VPU_HW_BTRS_MTL_WP_REQ_CMD);
	val = REG_SET_FLD(VPU_HW_BTRS_MTL_WP_REQ_CMD, SEND, val);
	REGB_WR32(VPU_HW_BTRS_MTL_WP_REQ_CMD, val);
}

static void wp_request_lnl(struct ivpu_device *vdev, struct wp_request *wp)
{
	u32 val;

	val = REGB_RD32(VPU_HW_BTRS_LNL_WP_REQ_PAYLOAD0);
	val = REG_SET_FLD_NUM(VPU_HW_BTRS_LNL_WP_REQ_PAYLOAD0, MIN_RATIO, wp->min, val);
	val = REG_SET_FLD_NUM(VPU_HW_BTRS_LNL_WP_REQ_PAYLOAD0, MAX_RATIO, wp->max, val);
	REGB_WR32(VPU_HW_BTRS_LNL_WP_REQ_PAYLOAD0, val);

	val = REGB_RD32(VPU_HW_BTRS_LNL_WP_REQ_PAYLOAD1);
	val = REG_SET_FLD_NUM(VPU_HW_BTRS_LNL_WP_REQ_PAYLOAD1, TARGET_RATIO, wp->target, val);
	val = REG_SET_FLD_NUM(VPU_HW_BTRS_LNL_WP_REQ_PAYLOAD1, EPP, wp->epp, val);
	REGB_WR32(VPU_HW_BTRS_LNL_WP_REQ_PAYLOAD1, val);

	val = REGB_RD32(VPU_HW_BTRS_LNL_WP_REQ_PAYLOAD2);
	val = REG_SET_FLD_NUM(VPU_HW_BTRS_LNL_WP_REQ_PAYLOAD2, CONFIG, wp->cfg, val);
	val = REG_SET_FLD_NUM(VPU_HW_BTRS_LNL_WP_REQ_PAYLOAD2, CDYN, wp->cdyn, val);
	REGB_WR32(VPU_HW_BTRS_LNL_WP_REQ_PAYLOAD2, val);

	val = REGB_RD32(VPU_HW_BTRS_LNL_WP_REQ_CMD);
	val = REG_SET_FLD(VPU_HW_BTRS_LNL_WP_REQ_CMD, SEND, val);
	REGB_WR32(VPU_HW_BTRS_LNL_WP_REQ_CMD, val);
}

static void wp_request(struct ivpu_device *vdev, struct wp_request *wp)
{
	if (ivpu_hw_btrs_gen(vdev) == IVPU_HW_BTRS_MTL)
		wp_request_mtl(vdev, wp);
	else
		wp_request_lnl(vdev, wp);
}

static int wp_request_send(struct ivpu_device *vdev, struct wp_request *wp)
{
	int ret;

	ret = wp_request_sync(vdev);
	if (ret) {
		ivpu_err(vdev, "Failed to sync before workpoint request: %d\n", ret);
		return ret;
	}

	wp_request(vdev, wp);

	ret = wp_request_sync(vdev);
	if (ret)
		ivpu_err(vdev, "Failed to sync after workpoint request: %d\n", ret);

	return ret;
}

static void prepare_wp_request(struct ivpu_device *vdev, struct wp_request *wp, bool enable)
{
	struct ivpu_hw_info *hw = vdev->hw;

	wp->min = hw->pll.min_ratio;
	wp->max = hw->pll.max_ratio;

	if (ivpu_hw_btrs_gen(vdev) == IVPU_HW_BTRS_MTL) {
		wp->target = enable ? hw->pll.pn_ratio : 0;
		wp->cfg = enable ? hw->config : 0;
		wp->cdyn = 0;
		wp->epp = 0;
	} else {
		wp->target = hw->pll.pn_ratio;
		wp->cfg = enable ? PLL_CONFIG_DEFAULT : 0;
		wp->cdyn = enable ? PLL_CDYN_DEFAULT : 0;
		wp->epp = enable ? PLL_EPP_DEFAULT : 0;
	}
}

static int wait_for_pll_lock(struct ivpu_device *vdev, bool enable)
{
	u32 exp_val = enable ? 0x1 : 0x0;

	if (ivpu_hw_btrs_gen(vdev) != IVPU_HW_BTRS_MTL)
		return 0;

	if (IVPU_WA(punit_disabled))
		return 0;

	return REGB_POLL_FLD(VPU_HW_BTRS_MTL_PLL_STATUS, LOCK, exp_val, PLL_TIMEOUT_US);
}

int ivpu_hw_btrs_wp_drive(struct ivpu_device *vdev, bool enable)
{
	struct wp_request wp;
	int ret;

	if (IVPU_WA(punit_disabled)) {
		ivpu_dbg(vdev, PM, "Skipping workpoint request\n");
		return 0;
	}

	prepare_wp_request(vdev, &wp, enable);

	ivpu_dbg(vdev, PM, "PLL workpoint request: %u Hz, config: 0x%x, epp: 0x%x, cdyn: 0x%x\n",
		 PLL_RATIO_TO_FREQ(wp.target), wp.cfg, wp.epp, wp.cdyn);

	ret = wp_request_send(vdev, &wp);
	if (ret) {
		ivpu_err(vdev, "Failed to send workpoint request: %d\n", ret);
		return ret;
	}

	ret = wait_for_pll_lock(vdev, enable);
	if (ret) {
		ivpu_err(vdev, "Timed out waiting for PLL lock\n");
		return ret;
	}

	ret = wait_for_status_ready(vdev, enable);
	if (ret) {
		ivpu_err(vdev, "Timed out waiting for NPU ready status\n");
		return ret;
	}

	return 0;
}

static int d0i3_drive_mtl(struct ivpu_device *vdev, bool enable)
{
	int ret;
	u32 val;

	ret = REGB_POLL_FLD(VPU_HW_BTRS_MTL_VPU_D0I3_CONTROL, INPROGRESS, 0, TIMEOUT_US);
	if (ret) {
		ivpu_err(vdev, "Failed to sync before D0i3 transition: %d\n", ret);
		return ret;
	}

	val = REGB_RD32(VPU_HW_BTRS_MTL_VPU_D0I3_CONTROL);
	if (enable)
		val = REG_SET_FLD(VPU_HW_BTRS_MTL_VPU_D0I3_CONTROL, I3, val);
	else
		val = REG_CLR_FLD(VPU_HW_BTRS_MTL_VPU_D0I3_CONTROL, I3, val);
	REGB_WR32(VPU_HW_BTRS_MTL_VPU_D0I3_CONTROL, val);

	ret = REGB_POLL_FLD(VPU_HW_BTRS_MTL_VPU_D0I3_CONTROL, INPROGRESS, 0, TIMEOUT_US);
	if (ret)
		ivpu_err(vdev, "Failed to sync after D0i3 transition: %d\n", ret);

	return ret;
}

static int d0i3_drive_lnl(struct ivpu_device *vdev, bool enable)
{
	int ret;
	u32 val;

	ret = REGB_POLL_FLD(VPU_HW_BTRS_LNL_D0I3_CONTROL, INPROGRESS, 0, TIMEOUT_US);
	if (ret) {
		ivpu_err(vdev, "Failed to sync before D0i3 transition: %d\n", ret);
		return ret;
	}

	val = REGB_RD32(VPU_HW_BTRS_LNL_D0I3_CONTROL);
	if (enable)
		val = REG_SET_FLD(VPU_HW_BTRS_LNL_D0I3_CONTROL, I3, val);
	else
		val = REG_CLR_FLD(VPU_HW_BTRS_LNL_D0I3_CONTROL, I3, val);
	REGB_WR32(VPU_HW_BTRS_LNL_D0I3_CONTROL, val);

	ret = REGB_POLL_FLD(VPU_HW_BTRS_LNL_D0I3_CONTROL, INPROGRESS, 0, TIMEOUT_US);
	if (ret) {
		ivpu_err(vdev, "Failed to sync after D0i3 transition: %d\n", ret);
		return ret;
	}

	return 0;
}

static int d0i3_drive(struct ivpu_device *vdev, bool enable)
{
	if (ivpu_hw_btrs_gen(vdev) == IVPU_HW_BTRS_MTL)
		return d0i3_drive_mtl(vdev, enable);
	else
		return d0i3_drive_lnl(vdev, enable);
}

int ivpu_hw_btrs_d0i3_enable(struct ivpu_device *vdev)
{
	int ret;

	if (IVPU_WA(punit_disabled))
		return 0;

	ret = d0i3_drive(vdev, true);
	if (ret)
		ivpu_err(vdev, "Failed to enable D0i3: %d\n", ret);

	udelay(5); /* VPU requires 5 us to complete the transition */

	return ret;
}

int ivpu_hw_btrs_d0i3_disable(struct ivpu_device *vdev)
{
	int ret;

	if (IVPU_WA(punit_disabled))
		return 0;

	ret = d0i3_drive(vdev, false);
	if (ret)
		ivpu_err(vdev, "Failed to disable D0i3: %d\n", ret);

	return ret;
}

int ivpu_hw_btrs_wait_for_clock_res_own_ack(struct ivpu_device *vdev)
{
	if (ivpu_hw_btrs_gen(vdev) == IVPU_HW_BTRS_MTL)
		return 0;

	return REGB_POLL_FLD(VPU_HW_BTRS_LNL_VPU_STATUS, CLOCK_RESOURCE_OWN_ACK, 1, TIMEOUT_US);
}

void ivpu_hw_btrs_set_port_arbitration_weights_lnl(struct ivpu_device *vdev)
{
	REGB_WR32(VPU_HW_BTRS_LNL_PORT_ARBITRATION_WEIGHTS, WEIGHTS_DEFAULT);
	REGB_WR32(VPU_HW_BTRS_LNL_PORT_ARBITRATION_WEIGHTS_ATS, WEIGHTS_ATS_DEFAULT);
}

static int ip_reset_mtl(struct ivpu_device *vdev)
{
	int ret;
	u32 val;

	ret = REGB_POLL_FLD(VPU_HW_BTRS_MTL_VPU_IP_RESET, TRIGGER, 0, TIMEOUT_US);
	if (ret) {
		ivpu_err(vdev, "Timed out waiting for TRIGGER bit\n");
		return ret;
	}

	val = REGB_RD32(VPU_HW_BTRS_MTL_VPU_IP_RESET);
	val = REG_SET_FLD(VPU_HW_BTRS_MTL_VPU_IP_RESET, TRIGGER, val);
	REGB_WR32(VPU_HW_BTRS_MTL_VPU_IP_RESET, val);

	ret = REGB_POLL_FLD(VPU_HW_BTRS_MTL_VPU_IP_RESET, TRIGGER, 0, TIMEOUT_US);
	if (ret)
		ivpu_err(vdev, "Timed out waiting for RESET completion\n");

	return ret;
}

static int ip_reset_lnl(struct ivpu_device *vdev)
{
	int ret;
	u32 val;

	ivpu_hw_btrs_clock_relinquish_disable_lnl(vdev);

	ret = REGB_POLL_FLD(VPU_HW_BTRS_LNL_IP_RESET, TRIGGER, 0, TIMEOUT_US);
	if (ret) {
		ivpu_err(vdev, "Wait for *_TRIGGER timed out\n");
		return ret;
	}

	val = REGB_RD32(VPU_HW_BTRS_LNL_IP_RESET);
	val = REG_SET_FLD(VPU_HW_BTRS_LNL_IP_RESET, TRIGGER, val);
	REGB_WR32(VPU_HW_BTRS_LNL_IP_RESET, val);

	ret = REGB_POLL_FLD(VPU_HW_BTRS_LNL_IP_RESET, TRIGGER, 0, TIMEOUT_US);
	if (ret)
		ivpu_err(vdev, "Timed out waiting for RESET completion\n");

	return ret;
}

int ivpu_hw_btrs_ip_reset(struct ivpu_device *vdev)
{
	if (IVPU_WA(punit_disabled))
		return 0;

	if (ivpu_hw_btrs_gen(vdev) == IVPU_HW_BTRS_MTL)
		return ip_reset_mtl(vdev);
	else
		return ip_reset_lnl(vdev);
}

void ivpu_hw_btrs_profiling_freq_reg_set_lnl(struct ivpu_device *vdev)
{
	u32 val = REGB_RD32(VPU_HW_BTRS_LNL_VPU_STATUS);

	if (vdev->hw->pll.profiling_freq == PLL_PROFILING_FREQ_DEFAULT)
		val = REG_CLR_FLD(VPU_HW_BTRS_LNL_VPU_STATUS, PERF_CLK, val);
	else
		val = REG_SET_FLD(VPU_HW_BTRS_LNL_VPU_STATUS, PERF_CLK, val);

	REGB_WR32(VPU_HW_BTRS_LNL_VPU_STATUS, val);
}

void ivpu_hw_btrs_ats_print_lnl(struct ivpu_device *vdev)
{
	ivpu_dbg(vdev, MISC, "Buttress ATS: %s\n",
		 REGB_RD32(VPU_HW_BTRS_LNL_HM_ATS) ? "Enable" : "Disable");
}

void ivpu_hw_btrs_clock_relinquish_disable_lnl(struct ivpu_device *vdev)
{
	u32 val = REGB_RD32(VPU_HW_BTRS_LNL_VPU_STATUS);

	val = REG_SET_FLD(VPU_HW_BTRS_LNL_VPU_STATUS, DISABLE_CLK_RELINQUISH, val);
	REGB_WR32(VPU_HW_BTRS_LNL_VPU_STATUS, val);
}

bool ivpu_hw_btrs_is_idle(struct ivpu_device *vdev)
{
	u32 val;

	if (IVPU_WA(punit_disabled))
		return true;

	if (ivpu_hw_btrs_gen(vdev) == IVPU_HW_BTRS_MTL) {
		val = REGB_RD32(VPU_HW_BTRS_MTL_VPU_STATUS);

		return REG_TEST_FLD(VPU_HW_BTRS_MTL_VPU_STATUS, READY, val) &&
		       REG_TEST_FLD(VPU_HW_BTRS_MTL_VPU_STATUS, IDLE, val);
	} else {
		val = REGB_RD32(VPU_HW_BTRS_LNL_VPU_STATUS);

		return REG_TEST_FLD(VPU_HW_BTRS_LNL_VPU_STATUS, READY, val) &&
		       REG_TEST_FLD(VPU_HW_BTRS_LNL_VPU_STATUS, IDLE, val);
	}
}

int ivpu_hw_btrs_wait_for_idle(struct ivpu_device *vdev)
{
	if (ivpu_hw_btrs_gen(vdev) == IVPU_HW_BTRS_MTL)
		return REGB_POLL_FLD(VPU_HW_BTRS_MTL_VPU_STATUS, IDLE, 0x1, IDLE_TIMEOUT_US);
	else
		return REGB_POLL_FLD(VPU_HW_BTRS_LNL_VPU_STATUS, IDLE, 0x1, IDLE_TIMEOUT_US);
}

/* Handler for IRQs from Buttress core (irqB) */
bool ivpu_hw_btrs_irq_handler_mtl(struct ivpu_device *vdev, int irq)
{
	u32 status = REGB_RD32(VPU_HW_BTRS_MTL_INTERRUPT_STAT) & BTRS_MTL_IRQ_MASK;
	bool schedule_recovery = false;

	if (!status)
		return false;

	if (REG_TEST_FLD(VPU_HW_BTRS_MTL_INTERRUPT_STAT, FREQ_CHANGE, status))
		ivpu_dbg(vdev, IRQ, "FREQ_CHANGE irq: %08x",
			 REGB_RD32(VPU_HW_BTRS_MTL_CURRENT_PLL));

	if (REG_TEST_FLD(VPU_HW_BTRS_MTL_INTERRUPT_STAT, ATS_ERR, status)) {
		ivpu_err(vdev, "ATS_ERR irq 0x%016llx", REGB_RD64(VPU_HW_BTRS_MTL_ATS_ERR_LOG_0));
		REGB_WR32(VPU_HW_BTRS_MTL_ATS_ERR_CLEAR, 0x1);
		schedule_recovery = true;
	}

	if (REG_TEST_FLD(VPU_HW_BTRS_MTL_INTERRUPT_STAT, UFI_ERR, status)) {
		u32 ufi_log = REGB_RD32(VPU_HW_BTRS_MTL_UFI_ERR_LOG);

		ivpu_err(vdev, "UFI_ERR irq (0x%08x) opcode: 0x%02lx axi_id: 0x%02lx cq_id: 0x%03lx",
			 ufi_log, REG_GET_FLD(VPU_HW_BTRS_MTL_UFI_ERR_LOG, OPCODE, ufi_log),
			 REG_GET_FLD(VPU_HW_BTRS_MTL_UFI_ERR_LOG, AXI_ID, ufi_log),
			 REG_GET_FLD(VPU_HW_BTRS_MTL_UFI_ERR_LOG, CQ_ID, ufi_log));
		REGB_WR32(VPU_HW_BTRS_MTL_UFI_ERR_CLEAR, 0x1);
		schedule_recovery = true;
	}

	/* This must be done after interrupts are cleared at the source. */
	if (IVPU_WA(interrupt_clear_with_0))
		/*
		 * Writing 1 triggers an interrupt, so we can't perform read update write.
		 * Clear local interrupt status by writing 0 to all bits.
		 */
		REGB_WR32(VPU_HW_BTRS_MTL_INTERRUPT_STAT, 0x0);
	else
		REGB_WR32(VPU_HW_BTRS_MTL_INTERRUPT_STAT, status);

	if (schedule_recovery)
		ivpu_pm_trigger_recovery(vdev, "Buttress IRQ");

	return true;
}

/* Handler for IRQs from Buttress core (irqB) */
bool ivpu_hw_btrs_irq_handler_lnl(struct ivpu_device *vdev, int irq)
{
	u32 status = REGB_RD32(VPU_HW_BTRS_LNL_INTERRUPT_STAT) & BTRS_LNL_IRQ_MASK;
	bool schedule_recovery = false;

	if (!status)
		return false;

	if (REG_TEST_FLD(VPU_HW_BTRS_LNL_INTERRUPT_STAT, SURV_ERR, status)) {
		ivpu_dbg(vdev, IRQ, "Survivability IRQ\n");
		if (!kfifo_put(&vdev->hw->irq.fifo, IVPU_HW_IRQ_SRC_DCT))
			ivpu_err_ratelimited(vdev, "IRQ FIFO full\n");
	}

	if (REG_TEST_FLD(VPU_HW_BTRS_LNL_INTERRUPT_STAT, FREQ_CHANGE, status))
		ivpu_dbg(vdev, IRQ, "FREQ_CHANGE irq: %08x", REGB_RD32(VPU_HW_BTRS_LNL_PLL_FREQ));

	if (REG_TEST_FLD(VPU_HW_BTRS_LNL_INTERRUPT_STAT, ATS_ERR, status)) {
		ivpu_err(vdev, "ATS_ERR LOG1 0x%08x ATS_ERR_LOG2 0x%08x\n",
			 REGB_RD32(VPU_HW_BTRS_LNL_ATS_ERR_LOG1),
			 REGB_RD32(VPU_HW_BTRS_LNL_ATS_ERR_LOG2));
		REGB_WR32(VPU_HW_BTRS_LNL_ATS_ERR_CLEAR, 0x1);
		schedule_recovery = true;
	}

	if (REG_TEST_FLD(VPU_HW_BTRS_LNL_INTERRUPT_STAT, CFI0_ERR, status)) {
		ivpu_err(vdev, "CFI0_ERR 0x%08x", REGB_RD32(VPU_HW_BTRS_LNL_CFI0_ERR_LOG));
		REGB_WR32(VPU_HW_BTRS_LNL_CFI0_ERR_CLEAR, 0x1);
		schedule_recovery = true;
	}

	if (REG_TEST_FLD(VPU_HW_BTRS_LNL_INTERRUPT_STAT, CFI1_ERR, status)) {
		ivpu_err(vdev, "CFI1_ERR 0x%08x", REGB_RD32(VPU_HW_BTRS_LNL_CFI1_ERR_LOG));
		REGB_WR32(VPU_HW_BTRS_LNL_CFI1_ERR_CLEAR, 0x1);
		schedule_recovery = true;
	}

	if (REG_TEST_FLD(VPU_HW_BTRS_LNL_INTERRUPT_STAT, IMR0_ERR, status)) {
		ivpu_err(vdev, "IMR_ERR_CFI0 LOW: 0x%08x HIGH: 0x%08x",
			 REGB_RD32(VPU_HW_BTRS_LNL_IMR_ERR_CFI0_LOW),
			 REGB_RD32(VPU_HW_BTRS_LNL_IMR_ERR_CFI0_HIGH));
		REGB_WR32(VPU_HW_BTRS_LNL_IMR_ERR_CFI0_CLEAR, 0x1);
		schedule_recovery = true;
	}

	if (REG_TEST_FLD(VPU_HW_BTRS_LNL_INTERRUPT_STAT, IMR1_ERR, status)) {
		ivpu_err(vdev, "IMR_ERR_CFI1 LOW: 0x%08x HIGH: 0x%08x",
			 REGB_RD32(VPU_HW_BTRS_LNL_IMR_ERR_CFI1_LOW),
			 REGB_RD32(VPU_HW_BTRS_LNL_IMR_ERR_CFI1_HIGH));
		REGB_WR32(VPU_HW_BTRS_LNL_IMR_ERR_CFI1_CLEAR, 0x1);
		schedule_recovery = true;
	}

	/* This must be done after interrupts are cleared at the source. */
	REGB_WR32(VPU_HW_BTRS_LNL_INTERRUPT_STAT, status);

	if (schedule_recovery)
		ivpu_pm_trigger_recovery(vdev, "Buttress IRQ");

	return true;
}

int ivpu_hw_btrs_dct_get_request(struct ivpu_device *vdev, bool *enable)
{
	u32 val = REGB_RD32(VPU_HW_BTRS_LNL_PCODE_MAILBOX_SHADOW);
	u32 cmd = REG_GET_FLD(VPU_HW_BTRS_LNL_PCODE_MAILBOX_SHADOW, CMD, val);
	u32 param1 = REG_GET_FLD(VPU_HW_BTRS_LNL_PCODE_MAILBOX_SHADOW, PARAM1, val);

	if (cmd != DCT_REQ) {
		ivpu_err_ratelimited(vdev, "Unsupported PCODE command: 0x%x\n", cmd);
		return -EBADR;
	}

	switch (param1) {
	case DCT_ENABLE:
		*enable = true;
		return 0;
	case DCT_DISABLE:
		*enable = false;
		return 0;
	default:
		ivpu_err_ratelimited(vdev, "Invalid PARAM1 value: %u\n", param1);
		return -EINVAL;
	}
}

void ivpu_hw_btrs_dct_set_status(struct ivpu_device *vdev, bool enable, u32 active_percent)
{
	u32 val = 0;
	u32 cmd = enable ? DCT_ENABLE : DCT_DISABLE;

	val = REG_SET_FLD_NUM(VPU_HW_BTRS_LNL_PCODE_MAILBOX_STATUS, CMD, DCT_REQ, val);
	val = REG_SET_FLD_NUM(VPU_HW_BTRS_LNL_PCODE_MAILBOX_STATUS, PARAM1, cmd, val);
	val = REG_SET_FLD_NUM(VPU_HW_BTRS_LNL_PCODE_MAILBOX_STATUS, PARAM2, active_percent, val);

	REGB_WR32(VPU_HW_BTRS_LNL_PCODE_MAILBOX_STATUS, val);
}

static u32 pll_ratio_to_freq_mtl(u32 ratio, u32 config)
{
	u32 pll_clock = PLL_REF_CLK_FREQ * ratio;
	u32 cpu_clock;

	if ((config & 0xff) == MTL_PLL_RATIO_4_3)
		cpu_clock = pll_clock * 2 / 4;
	else
		cpu_clock = pll_clock * 2 / 5;

	return cpu_clock;
}

u32 ivpu_hw_btrs_ratio_to_freq(struct ivpu_device *vdev, u32 ratio)
{
	struct ivpu_hw_info *hw = vdev->hw;

	if (ivpu_hw_btrs_gen(vdev) == IVPU_HW_BTRS_MTL)
		return pll_ratio_to_freq_mtl(ratio, hw->config);
	else
		return PLL_RATIO_TO_FREQ(ratio);
}

static u32 pll_freq_get_mtl(struct ivpu_device *vdev)
{
	u32 pll_curr_ratio;

	pll_curr_ratio = REGB_RD32(VPU_HW_BTRS_MTL_CURRENT_PLL);
	pll_curr_ratio &= VPU_HW_BTRS_MTL_CURRENT_PLL_RATIO_MASK;

	if (!ivpu_is_silicon(vdev))
		return PLL_SIMULATION_FREQ;

	return pll_ratio_to_freq_mtl(pll_curr_ratio, vdev->hw->config);
}

static u32 pll_freq_get_lnl(struct ivpu_device *vdev)
{
	u32 pll_curr_ratio;

	pll_curr_ratio = REGB_RD32(VPU_HW_BTRS_LNL_PLL_FREQ);
	pll_curr_ratio &= VPU_HW_BTRS_LNL_PLL_FREQ_RATIO_MASK;

	return PLL_RATIO_TO_FREQ(pll_curr_ratio);
}

u32 ivpu_hw_btrs_pll_freq_get(struct ivpu_device *vdev)
{
	if (ivpu_hw_btrs_gen(vdev) == IVPU_HW_BTRS_MTL)
		return pll_freq_get_mtl(vdev);
	else
		return pll_freq_get_lnl(vdev);
}

u32 ivpu_hw_btrs_telemetry_offset_get(struct ivpu_device *vdev)
{
	if (ivpu_hw_btrs_gen(vdev) == IVPU_HW_BTRS_MTL)
		return REGB_RD32(VPU_HW_BTRS_MTL_VPU_TELEMETRY_OFFSET);
	else
		return REGB_RD32(VPU_HW_BTRS_LNL_VPU_TELEMETRY_OFFSET);
}

u32 ivpu_hw_btrs_telemetry_size_get(struct ivpu_device *vdev)
{
	if (ivpu_hw_btrs_gen(vdev) == IVPU_HW_BTRS_MTL)
		return REGB_RD32(VPU_HW_BTRS_MTL_VPU_TELEMETRY_SIZE);
	else
		return REGB_RD32(VPU_HW_BTRS_LNL_VPU_TELEMETRY_SIZE);
}

u32 ivpu_hw_btrs_telemetry_enable_get(struct ivpu_device *vdev)
{
	if (ivpu_hw_btrs_gen(vdev) == IVPU_HW_BTRS_MTL)
		return REGB_RD32(VPU_HW_BTRS_MTL_VPU_TELEMETRY_ENABLE);
	else
		return REGB_RD32(VPU_HW_BTRS_LNL_VPU_TELEMETRY_ENABLE);
}

void ivpu_hw_btrs_global_int_disable(struct ivpu_device *vdev)
{
	if (ivpu_hw_btrs_gen(vdev) == IVPU_HW_BTRS_MTL)
		REGB_WR32(VPU_HW_BTRS_MTL_GLOBAL_INT_MASK, 0x1);
	else
		REGB_WR32(VPU_HW_BTRS_LNL_GLOBAL_INT_MASK, 0x1);
}

void ivpu_hw_btrs_global_int_enable(struct ivpu_device *vdev)
{
	if (ivpu_hw_btrs_gen(vdev) == IVPU_HW_BTRS_MTL)
		REGB_WR32(VPU_HW_BTRS_MTL_GLOBAL_INT_MASK, 0x0);
	else
		REGB_WR32(VPU_HW_BTRS_LNL_GLOBAL_INT_MASK, 0x0);
}

void ivpu_hw_btrs_irq_enable(struct ivpu_device *vdev)
{
	if (ivpu_hw_btrs_gen(vdev) == IVPU_HW_BTRS_MTL) {
		REGB_WR32(VPU_HW_BTRS_MTL_LOCAL_INT_MASK, (u32)(~BTRS_MTL_IRQ_MASK));
		REGB_WR32(VPU_HW_BTRS_MTL_GLOBAL_INT_MASK, 0x0);
	} else {
		REGB_WR32(VPU_HW_BTRS_LNL_LOCAL_INT_MASK, (u32)(~BTRS_LNL_IRQ_MASK));
		REGB_WR32(VPU_HW_BTRS_LNL_GLOBAL_INT_MASK, 0x0);
	}
}

void ivpu_hw_btrs_irq_disable(struct ivpu_device *vdev)
{
	if (ivpu_hw_btrs_gen(vdev) == IVPU_HW_BTRS_MTL) {
		REGB_WR32(VPU_HW_BTRS_MTL_GLOBAL_INT_MASK, 0x1);
		REGB_WR32(VPU_HW_BTRS_MTL_LOCAL_INT_MASK, BTRS_IRQ_DISABLE_MASK);
	} else {
		REGB_WR32(VPU_HW_BTRS_LNL_GLOBAL_INT_MASK, 0x1);
		REGB_WR32(VPU_HW_BTRS_LNL_LOCAL_INT_MASK, BTRS_IRQ_DISABLE_MASK);
	}
}

static void diagnose_failure_mtl(struct ivpu_device *vdev)
{
	u32 reg = REGB_RD32(VPU_HW_BTRS_MTL_INTERRUPT_STAT) & BTRS_MTL_IRQ_MASK;

	if (REG_TEST_FLD(VPU_HW_BTRS_MTL_INTERRUPT_STAT, ATS_ERR, reg))
		ivpu_err(vdev, "ATS_ERR irq 0x%016llx", REGB_RD64(VPU_HW_BTRS_MTL_ATS_ERR_LOG_0));

	if (REG_TEST_FLD(VPU_HW_BTRS_MTL_INTERRUPT_STAT, UFI_ERR, reg)) {
		u32 log = REGB_RD32(VPU_HW_BTRS_MTL_UFI_ERR_LOG);

		ivpu_err(vdev, "UFI_ERR irq (0x%08x) opcode: 0x%02lx axi_id: 0x%02lx cq_id: 0x%03lx",
			 log, REG_GET_FLD(VPU_HW_BTRS_MTL_UFI_ERR_LOG, OPCODE, log),
			 REG_GET_FLD(VPU_HW_BTRS_MTL_UFI_ERR_LOG, AXI_ID, log),
			 REG_GET_FLD(VPU_HW_BTRS_MTL_UFI_ERR_LOG, CQ_ID, log));
	}
}

static void diagnose_failure_lnl(struct ivpu_device *vdev)
{
	u32 reg = REGB_RD32(VPU_HW_BTRS_MTL_INTERRUPT_STAT) & BTRS_LNL_IRQ_MASK;

	if (REG_TEST_FLD(VPU_HW_BTRS_LNL_INTERRUPT_STAT, ATS_ERR, reg)) {
		ivpu_err(vdev, "ATS_ERR_LOG1 0x%08x ATS_ERR_LOG2 0x%08x\n",
			 REGB_RD32(VPU_HW_BTRS_LNL_ATS_ERR_LOG1),
			 REGB_RD32(VPU_HW_BTRS_LNL_ATS_ERR_LOG2));
	}

	if (REG_TEST_FLD(VPU_HW_BTRS_LNL_INTERRUPT_STAT, CFI0_ERR, reg))
		ivpu_err(vdev, "CFI0_ERR_LOG 0x%08x\n", REGB_RD32(VPU_HW_BTRS_LNL_CFI0_ERR_LOG));

	if (REG_TEST_FLD(VPU_HW_BTRS_LNL_INTERRUPT_STAT, CFI1_ERR, reg))
		ivpu_err(vdev, "CFI1_ERR_LOG 0x%08x\n", REGB_RD32(VPU_HW_BTRS_LNL_CFI1_ERR_LOG));

	if (REG_TEST_FLD(VPU_HW_BTRS_LNL_INTERRUPT_STAT, IMR0_ERR, reg))
		ivpu_err(vdev, "IMR_ERR_CFI0 LOW: 0x%08x HIGH: 0x%08x\n",
			 REGB_RD32(VPU_HW_BTRS_LNL_IMR_ERR_CFI0_LOW),
			 REGB_RD32(VPU_HW_BTRS_LNL_IMR_ERR_CFI0_HIGH));

	if (REG_TEST_FLD(VPU_HW_BTRS_LNL_INTERRUPT_STAT, IMR1_ERR, reg))
		ivpu_err(vdev, "IMR_ERR_CFI1 LOW: 0x%08x HIGH: 0x%08x\n",
			 REGB_RD32(VPU_HW_BTRS_LNL_IMR_ERR_CFI1_LOW),
			 REGB_RD32(VPU_HW_BTRS_LNL_IMR_ERR_CFI1_HIGH));

	if (REG_TEST_FLD(VPU_HW_BTRS_LNL_INTERRUPT_STAT, SURV_ERR, reg))
		ivpu_err(vdev, "Survivability IRQ\n");
}

void ivpu_hw_btrs_diagnose_failure(struct ivpu_device *vdev)
{
	if (ivpu_hw_btrs_gen(vdev) == IVPU_HW_BTRS_MTL)
		return diagnose_failure_mtl(vdev);
	else
		return diagnose_failure_lnl(vdev);
}
