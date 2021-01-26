// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt)	"[drm-dp] %s: " fmt, __func__

#include <linux/rational.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/phy/phy.h>
#include <linux/phy/phy-dp.h>
#include <linux/rational.h>
#include <drm/drm_dp_helper.h>
#include <drm/drm_print.h>

#include "dp_catalog.h"
#include "dp_reg.h"

#define POLLING_SLEEP_US			1000
#define POLLING_TIMEOUT_US			10000

#define SCRAMBLER_RESET_COUNT_VALUE		0xFC

#define DP_INTERRUPT_STATUS_ACK_SHIFT	1
#define DP_INTERRUPT_STATUS_MASK_SHIFT	2

#define MSM_DP_CONTROLLER_AHB_OFFSET	0x0000
#define MSM_DP_CONTROLLER_AHB_SIZE	0x0200
#define MSM_DP_CONTROLLER_AUX_OFFSET	0x0200
#define MSM_DP_CONTROLLER_AUX_SIZE	0x0200
#define MSM_DP_CONTROLLER_LINK_OFFSET	0x0400
#define MSM_DP_CONTROLLER_LINK_SIZE	0x0C00
#define MSM_DP_CONTROLLER_P0_OFFSET	0x1000
#define MSM_DP_CONTROLLER_P0_SIZE	0x0400

#define DP_INTERRUPT_STATUS1 \
	(DP_INTR_AUX_I2C_DONE| \
	DP_INTR_WRONG_ADDR | DP_INTR_TIMEOUT | \
	DP_INTR_NACK_DEFER | DP_INTR_WRONG_DATA_CNT | \
	DP_INTR_I2C_NACK | DP_INTR_I2C_DEFER | \
	DP_INTR_PLL_UNLOCKED | DP_INTR_AUX_ERROR)

#define DP_INTERRUPT_STATUS1_ACK \
	(DP_INTERRUPT_STATUS1 << DP_INTERRUPT_STATUS_ACK_SHIFT)
#define DP_INTERRUPT_STATUS1_MASK \
	(DP_INTERRUPT_STATUS1 << DP_INTERRUPT_STATUS_MASK_SHIFT)

#define DP_INTERRUPT_STATUS2 \
	(DP_INTR_READY_FOR_VIDEO | DP_INTR_IDLE_PATTERN_SENT | \
	DP_INTR_FRAME_END | DP_INTR_CRC_UPDATED)

#define DP_INTERRUPT_STATUS2_ACK \
	(DP_INTERRUPT_STATUS2 << DP_INTERRUPT_STATUS_ACK_SHIFT)
#define DP_INTERRUPT_STATUS2_MASK \
	(DP_INTERRUPT_STATUS2 << DP_INTERRUPT_STATUS_MASK_SHIFT)

struct dp_catalog_private {
	struct device *dev;
	struct dp_io *io;
	u32 (*audio_map)[DP_AUDIO_SDP_HEADER_MAX];
	struct dp_catalog dp_catalog;
	u8 aux_lut_cfg_index[PHY_AUX_CFG_MAX];
};

static inline u32 dp_read_aux(struct dp_catalog_private *catalog, u32 offset)
{
	offset += MSM_DP_CONTROLLER_AUX_OFFSET;
	return readl_relaxed(catalog->io->dp_controller.base + offset);
}

static inline void dp_write_aux(struct dp_catalog_private *catalog,
			       u32 offset, u32 data)
{
	offset += MSM_DP_CONTROLLER_AUX_OFFSET;
	/*
	 * To make sure aux reg writes happens before any other operation,
	 * this function uses writel() instread of writel_relaxed()
	 */
	writel(data, catalog->io->dp_controller.base + offset);
}

static inline u32 dp_read_ahb(struct dp_catalog_private *catalog, u32 offset)
{
	offset += MSM_DP_CONTROLLER_AHB_OFFSET;
	return readl_relaxed(catalog->io->dp_controller.base + offset);
}

static inline void dp_write_ahb(struct dp_catalog_private *catalog,
			       u32 offset, u32 data)
{
	offset += MSM_DP_CONTROLLER_AHB_OFFSET;
	/*
	 * To make sure phy reg writes happens before any other operation,
	 * this function uses writel() instread of writel_relaxed()
	 */
	writel(data, catalog->io->dp_controller.base + offset);
}

static inline void dp_write_p0(struct dp_catalog_private *catalog,
			       u32 offset, u32 data)
{
	offset += MSM_DP_CONTROLLER_P0_OFFSET;
	/*
	 * To make sure interface reg writes happens before any other operation,
	 * this function uses writel() instread of writel_relaxed()
	 */
	writel(data, catalog->io->dp_controller.base + offset);
}

static inline u32 dp_read_p0(struct dp_catalog_private *catalog,
			       u32 offset)
{
	offset += MSM_DP_CONTROLLER_P0_OFFSET;
	/*
	 * To make sure interface reg writes happens before any other operation,
	 * this function uses writel() instread of writel_relaxed()
	 */
	return readl_relaxed(catalog->io->dp_controller.base + offset);
}

static inline u32 dp_read_link(struct dp_catalog_private *catalog, u32 offset)
{
	offset += MSM_DP_CONTROLLER_LINK_OFFSET;
	return readl_relaxed(catalog->io->dp_controller.base + offset);
}

static inline void dp_write_link(struct dp_catalog_private *catalog,
			       u32 offset, u32 data)
{
	offset += MSM_DP_CONTROLLER_LINK_OFFSET;
	/*
	 * To make sure link reg writes happens before any other operation,
	 * this function uses writel() instread of writel_relaxed()
	 */
	writel(data, catalog->io->dp_controller.base + offset);
}

/* aux related catalog functions */
u32 dp_catalog_aux_read_data(struct dp_catalog *dp_catalog)
{
	struct dp_catalog_private *catalog = container_of(dp_catalog,
				struct dp_catalog_private, dp_catalog);

	return dp_read_aux(catalog, REG_DP_AUX_DATA);
}

int dp_catalog_aux_write_data(struct dp_catalog *dp_catalog)
{
	struct dp_catalog_private *catalog = container_of(dp_catalog,
				struct dp_catalog_private, dp_catalog);

	dp_write_aux(catalog, REG_DP_AUX_DATA, dp_catalog->aux_data);
	return 0;
}

int dp_catalog_aux_write_trans(struct dp_catalog *dp_catalog)
{
	struct dp_catalog_private *catalog = container_of(dp_catalog,
				struct dp_catalog_private, dp_catalog);

	dp_write_aux(catalog, REG_DP_AUX_TRANS_CTRL, dp_catalog->aux_data);
	return 0;
}

int dp_catalog_aux_clear_trans(struct dp_catalog *dp_catalog, bool read)
{
	u32 data;
	struct dp_catalog_private *catalog = container_of(dp_catalog,
				struct dp_catalog_private, dp_catalog);

	if (read) {
		data = dp_read_aux(catalog, REG_DP_AUX_TRANS_CTRL);
		data &= ~DP_AUX_TRANS_CTRL_GO;
		dp_write_aux(catalog, REG_DP_AUX_TRANS_CTRL, data);
	} else {
		dp_write_aux(catalog, REG_DP_AUX_TRANS_CTRL, 0);
	}
	return 0;
}

int dp_catalog_aux_clear_hw_interrupts(struct dp_catalog *dp_catalog)
{
	struct dp_catalog_private *catalog = container_of(dp_catalog,
				struct dp_catalog_private, dp_catalog);

	dp_read_aux(catalog, REG_DP_PHY_AUX_INTERRUPT_STATUS);
	dp_write_aux(catalog, REG_DP_PHY_AUX_INTERRUPT_CLEAR, 0x1f);
	dp_write_aux(catalog, REG_DP_PHY_AUX_INTERRUPT_CLEAR, 0x9f);
	dp_write_aux(catalog, REG_DP_PHY_AUX_INTERRUPT_CLEAR, 0);
	return 0;
}

void dp_catalog_aux_reset(struct dp_catalog *dp_catalog)
{
	u32 aux_ctrl;
	struct dp_catalog_private *catalog = container_of(dp_catalog,
				struct dp_catalog_private, dp_catalog);

	aux_ctrl = dp_read_aux(catalog, REG_DP_AUX_CTRL);

	aux_ctrl |= DP_AUX_CTRL_RESET;
	dp_write_aux(catalog, REG_DP_AUX_CTRL, aux_ctrl);
	usleep_range(1000, 1100); /* h/w recommended delay */

	aux_ctrl &= ~DP_AUX_CTRL_RESET;
	dp_write_aux(catalog, REG_DP_AUX_CTRL, aux_ctrl);
}

void dp_catalog_aux_enable(struct dp_catalog *dp_catalog, bool enable)
{
	u32 aux_ctrl;
	struct dp_catalog_private *catalog = container_of(dp_catalog,
				struct dp_catalog_private, dp_catalog);

	aux_ctrl = dp_read_aux(catalog, REG_DP_AUX_CTRL);

	if (enable) {
		dp_write_aux(catalog, REG_DP_TIMEOUT_COUNT, 0xffff);
		dp_write_aux(catalog, REG_DP_AUX_LIMITS, 0xffff);
		aux_ctrl |= DP_AUX_CTRL_ENABLE;
	} else {
		aux_ctrl &= ~DP_AUX_CTRL_ENABLE;
	}

	dp_write_aux(catalog, REG_DP_AUX_CTRL, aux_ctrl);
}

void dp_catalog_aux_update_cfg(struct dp_catalog *dp_catalog)
{
	struct dp_catalog_private *catalog = container_of(dp_catalog,
				struct dp_catalog_private, dp_catalog);
	struct dp_io *dp_io = catalog->io;
	struct phy *phy = dp_io->phy;

	phy_calibrate(phy);
}

static void dump_regs(void __iomem *base, int len)
{
	int i;
	u32 x0, x4, x8, xc;
	u32 addr_off = 0;

	len = DIV_ROUND_UP(len, 16);
	for (i = 0; i < len; i++) {
		x0 = readl_relaxed(base + addr_off);
		x4 = readl_relaxed(base + addr_off + 0x04);
		x8 = readl_relaxed(base + addr_off + 0x08);
		xc = readl_relaxed(base + addr_off + 0x0c);

		pr_info("%08x: %08x %08x %08x %08x", addr_off, x0, x4, x8, xc);
		addr_off += 16;
	}
}

void dp_catalog_dump_regs(struct dp_catalog *dp_catalog)
{
	u32 offset, len;
	struct dp_catalog_private *catalog = container_of(dp_catalog,
		struct dp_catalog_private, dp_catalog);

	pr_info("AHB regs\n");
	offset = MSM_DP_CONTROLLER_AHB_OFFSET;
	len = MSM_DP_CONTROLLER_AHB_SIZE;
	dump_regs(catalog->io->dp_controller.base + offset, len);

	pr_info("AUXCLK regs\n");
	offset = MSM_DP_CONTROLLER_AUX_OFFSET;
	len = MSM_DP_CONTROLLER_AUX_SIZE;
	dump_regs(catalog->io->dp_controller.base + offset, len);

	pr_info("LCLK regs\n");
	offset = MSM_DP_CONTROLLER_LINK_OFFSET;
	len = MSM_DP_CONTROLLER_LINK_SIZE;
	dump_regs(catalog->io->dp_controller.base + offset, len);

	pr_info("P0CLK regs\n");
	offset = MSM_DP_CONTROLLER_P0_OFFSET;
	len = MSM_DP_CONTROLLER_P0_SIZE;
	dump_regs(catalog->io->dp_controller.base + offset, len);
}

int dp_catalog_aux_get_irq(struct dp_catalog *dp_catalog)
{
	struct dp_catalog_private *catalog = container_of(dp_catalog,
				struct dp_catalog_private, dp_catalog);
	u32 intr, intr_ack;

	intr = dp_read_ahb(catalog, REG_DP_INTR_STATUS);
	intr &= ~DP_INTERRUPT_STATUS1_MASK;
	intr_ack = (intr & DP_INTERRUPT_STATUS1)
			<< DP_INTERRUPT_STATUS_ACK_SHIFT;
	dp_write_ahb(catalog, REG_DP_INTR_STATUS, intr_ack |
			DP_INTERRUPT_STATUS1_MASK);

	return intr;

}

/* controller related catalog functions */
void dp_catalog_ctrl_update_transfer_unit(struct dp_catalog *dp_catalog,
				u32 dp_tu, u32 valid_boundary,
				u32 valid_boundary2)
{
	struct dp_catalog_private *catalog = container_of(dp_catalog,
				struct dp_catalog_private, dp_catalog);

	dp_write_link(catalog, REG_DP_VALID_BOUNDARY, valid_boundary);
	dp_write_link(catalog, REG_DP_TU, dp_tu);
	dp_write_link(catalog, REG_DP_VALID_BOUNDARY_2, valid_boundary2);
}

void dp_catalog_ctrl_state_ctrl(struct dp_catalog *dp_catalog, u32 state)
{
	struct dp_catalog_private *catalog = container_of(dp_catalog,
				struct dp_catalog_private, dp_catalog);

	dp_write_link(catalog, REG_DP_STATE_CTRL, state);
}

void dp_catalog_ctrl_config_ctrl(struct dp_catalog *dp_catalog, u32 cfg)
{
	struct dp_catalog_private *catalog = container_of(dp_catalog,
				struct dp_catalog_private, dp_catalog);

	DRM_DEBUG_DP("DP_CONFIGURATION_CTRL=0x%x\n", cfg);

	dp_write_link(catalog, REG_DP_CONFIGURATION_CTRL, cfg);
}

void dp_catalog_ctrl_lane_mapping(struct dp_catalog *dp_catalog)
{
	struct dp_catalog_private *catalog = container_of(dp_catalog,
				struct dp_catalog_private, dp_catalog);
	u32 ln_0 = 0, ln_1 = 1, ln_2 = 2, ln_3 = 3; /* One-to-One mapping */
	u32 ln_mapping;

	ln_mapping = ln_0 << LANE0_MAPPING_SHIFT;
	ln_mapping |= ln_1 << LANE1_MAPPING_SHIFT;
	ln_mapping |= ln_2 << LANE2_MAPPING_SHIFT;
	ln_mapping |= ln_3 << LANE3_MAPPING_SHIFT;

	dp_write_link(catalog, REG_DP_LOGICAL2PHYSICAL_LANE_MAPPING,
			ln_mapping);
}

void dp_catalog_ctrl_mainlink_ctrl(struct dp_catalog *dp_catalog,
						bool enable)
{
	u32 mainlink_ctrl;
	struct dp_catalog_private *catalog = container_of(dp_catalog,
				struct dp_catalog_private, dp_catalog);

	if (enable) {
		/*
		 * To make sure link reg writes happens before other operation,
		 * dp_write_link() function uses writel()
		 */
		mainlink_ctrl = dp_read_link(catalog, REG_DP_MAINLINK_CTRL);

		mainlink_ctrl &= ~(DP_MAINLINK_CTRL_RESET |
						DP_MAINLINK_CTRL_ENABLE);
		dp_write_link(catalog, REG_DP_MAINLINK_CTRL, mainlink_ctrl);

		mainlink_ctrl |= DP_MAINLINK_CTRL_RESET;
		dp_write_link(catalog, REG_DP_MAINLINK_CTRL, mainlink_ctrl);

		mainlink_ctrl &= ~DP_MAINLINK_CTRL_RESET;
		dp_write_link(catalog, REG_DP_MAINLINK_CTRL, mainlink_ctrl);

		mainlink_ctrl |= (DP_MAINLINK_CTRL_ENABLE |
					DP_MAINLINK_FB_BOUNDARY_SEL);
		dp_write_link(catalog, REG_DP_MAINLINK_CTRL, mainlink_ctrl);
	} else {
		mainlink_ctrl = dp_read_link(catalog, REG_DP_MAINLINK_CTRL);
		mainlink_ctrl &= ~DP_MAINLINK_CTRL_ENABLE;
		dp_write_link(catalog, REG_DP_MAINLINK_CTRL, mainlink_ctrl);
	}
}

void dp_catalog_ctrl_config_misc(struct dp_catalog *dp_catalog,
					u32 colorimetry_cfg,
					u32 test_bits_depth)
{
	u32 misc_val;
	struct dp_catalog_private *catalog = container_of(dp_catalog,
				struct dp_catalog_private, dp_catalog);

	misc_val = dp_read_link(catalog, REG_DP_MISC1_MISC0);

	/* clear bpp bits */
	misc_val &= ~(0x07 << DP_MISC0_TEST_BITS_DEPTH_SHIFT);
	misc_val |= colorimetry_cfg << DP_MISC0_COLORIMETRY_CFG_SHIFT;
	misc_val |= test_bits_depth << DP_MISC0_TEST_BITS_DEPTH_SHIFT;
	/* Configure clock to synchronous mode */
	misc_val |= DP_MISC0_SYNCHRONOUS_CLK;

	DRM_DEBUG_DP("misc settings = 0x%x\n", misc_val);
	dp_write_link(catalog, REG_DP_MISC1_MISC0, misc_val);
}

void dp_catalog_ctrl_config_msa(struct dp_catalog *dp_catalog,
					u32 rate, u32 stream_rate_khz,
					bool fixed_nvid)
{
	u32 pixel_m, pixel_n;
	u32 mvid, nvid, pixel_div = 0, dispcc_input_rate;
	u32 const nvid_fixed = DP_LINK_CONSTANT_N_VALUE;
	u32 const link_rate_hbr2 = 540000;
	u32 const link_rate_hbr3 = 810000;
	unsigned long den, num;

	struct dp_catalog_private *catalog = container_of(dp_catalog,
				struct dp_catalog_private, dp_catalog);

	if (rate == link_rate_hbr3)
		pixel_div = 6;
	else if (rate == 1620000 || rate == 270000)
		pixel_div = 2;
	else if (rate == link_rate_hbr2)
		pixel_div = 4;
	else
		DRM_ERROR("Invalid pixel mux divider\n");

	dispcc_input_rate = (rate * 10) / pixel_div;

	rational_best_approximation(dispcc_input_rate, stream_rate_khz,
			(unsigned long)(1 << 16) - 1,
			(unsigned long)(1 << 16) - 1, &den, &num);

	den = ~(den - num);
	den = den & 0xFFFF;
	pixel_m = num;
	pixel_n = den;

	mvid = (pixel_m & 0xFFFF) * 5;
	nvid = (0xFFFF & (~pixel_n)) + (pixel_m & 0xFFFF);

	if (nvid < nvid_fixed) {
		u32 temp;

		temp = (nvid_fixed / nvid) * nvid;
		mvid = (nvid_fixed / nvid) * mvid;
		nvid = temp;
	}

	if (link_rate_hbr2 == rate)
		nvid *= 2;

	if (link_rate_hbr3 == rate)
		nvid *= 3;

	DRM_DEBUG_DP("mvid=0x%x, nvid=0x%x\n", mvid, nvid);
	dp_write_link(catalog, REG_DP_SOFTWARE_MVID, mvid);
	dp_write_link(catalog, REG_DP_SOFTWARE_NVID, nvid);
	dp_write_p0(catalog, MMSS_DP_DSC_DTO, 0x0);
}

int dp_catalog_ctrl_set_pattern(struct dp_catalog *dp_catalog,
					u32 pattern)
{
	int bit, ret;
	u32 data;
	struct dp_catalog_private *catalog = container_of(dp_catalog,
				struct dp_catalog_private, dp_catalog);

	bit = BIT(pattern - 1);
	DRM_DEBUG_DP("hw: bit=%d train=%d\n", bit, pattern);
	dp_catalog_ctrl_state_ctrl(dp_catalog, bit);

	bit = BIT(pattern - 1) << DP_MAINLINK_READY_LINK_TRAINING_SHIFT;

	/* Poll for mainlink ready status */
	ret = readx_poll_timeout(readl, catalog->io->dp_controller.base +
					MSM_DP_CONTROLLER_LINK_OFFSET +
					REG_DP_MAINLINK_READY,
					data, data & bit,
					POLLING_SLEEP_US, POLLING_TIMEOUT_US);
	if (ret < 0) {
		DRM_ERROR("set pattern for link_train=%d failed\n", pattern);
		return ret;
	}
	return 0;
}

void dp_catalog_ctrl_reset(struct dp_catalog *dp_catalog)
{
	u32 sw_reset;
	struct dp_catalog_private *catalog = container_of(dp_catalog,
				struct dp_catalog_private, dp_catalog);

	sw_reset = dp_read_ahb(catalog, REG_DP_SW_RESET);

	sw_reset |= DP_SW_RESET;
	dp_write_ahb(catalog, REG_DP_SW_RESET, sw_reset);
	usleep_range(1000, 1100); /* h/w recommended delay */

	sw_reset &= ~DP_SW_RESET;
	dp_write_ahb(catalog, REG_DP_SW_RESET, sw_reset);
}

bool dp_catalog_ctrl_mainlink_ready(struct dp_catalog *dp_catalog)
{
	u32 data;
	int ret;
	struct dp_catalog_private *catalog = container_of(dp_catalog,
				struct dp_catalog_private, dp_catalog);

	/* Poll for mainlink ready status */
	ret = readl_poll_timeout(catalog->io->dp_controller.base +
				MSM_DP_CONTROLLER_LINK_OFFSET +
				REG_DP_MAINLINK_READY,
				data, data & DP_MAINLINK_READY_FOR_VIDEO,
				POLLING_SLEEP_US, POLLING_TIMEOUT_US);
	if (ret < 0) {
		DRM_ERROR("mainlink not ready\n");
		return false;
	}

	return true;
}

void dp_catalog_ctrl_enable_irq(struct dp_catalog *dp_catalog,
						bool enable)
{
	struct dp_catalog_private *catalog = container_of(dp_catalog,
				struct dp_catalog_private, dp_catalog);

	if (enable) {
		dp_write_ahb(catalog, REG_DP_INTR_STATUS,
				DP_INTERRUPT_STATUS1_MASK);
		dp_write_ahb(catalog, REG_DP_INTR_STATUS2,
				DP_INTERRUPT_STATUS2_MASK);
	} else {
		dp_write_ahb(catalog, REG_DP_INTR_STATUS, 0x00);
		dp_write_ahb(catalog, REG_DP_INTR_STATUS2, 0x00);
	}
}

void dp_catalog_hpd_config_intr(struct dp_catalog *dp_catalog,
			u32 intr_mask, bool en)
{
	struct dp_catalog_private *catalog = container_of(dp_catalog,
				struct dp_catalog_private, dp_catalog);

	u32 config = dp_read_aux(catalog, REG_DP_DP_HPD_INT_MASK);

	config = (en ? config | intr_mask : config & ~intr_mask);

	dp_write_aux(catalog, REG_DP_DP_HPD_INT_MASK,
				config & DP_DP_HPD_INT_MASK);
}

void dp_catalog_ctrl_hpd_config(struct dp_catalog *dp_catalog)
{
	struct dp_catalog_private *catalog = container_of(dp_catalog,
				struct dp_catalog_private, dp_catalog);

	u32 reftimer = dp_read_aux(catalog, REG_DP_DP_HPD_REFTIMER);

	/* enable HPD interrupts */
	dp_catalog_hpd_config_intr(dp_catalog,
		DP_DP_HPD_PLUG_INT_MASK | DP_DP_IRQ_HPD_INT_MASK
		| DP_DP_HPD_UNPLUG_INT_MASK | DP_DP_HPD_REPLUG_INT_MASK, true);

	/* Configure REFTIMER and enable it */
	reftimer |= DP_DP_HPD_REFTIMER_ENABLE;
	dp_write_aux(catalog, REG_DP_DP_HPD_REFTIMER, reftimer);

	/* Enable HPD */
	dp_write_aux(catalog, REG_DP_DP_HPD_CTRL, DP_DP_HPD_CTRL_HPD_EN);
}

u32 dp_catalog_hpd_get_intr_status(struct dp_catalog *dp_catalog)
{
	struct dp_catalog_private *catalog = container_of(dp_catalog,
				struct dp_catalog_private, dp_catalog);
	int isr = 0;

	isr = dp_read_aux(catalog, REG_DP_DP_HPD_INT_STATUS);
	dp_write_aux(catalog, REG_DP_DP_HPD_INT_ACK,
				 (isr & DP_DP_HPD_INT_MASK));

	return isr;
}

int dp_catalog_ctrl_get_interrupt(struct dp_catalog *dp_catalog)
{
	struct dp_catalog_private *catalog = container_of(dp_catalog,
				struct dp_catalog_private, dp_catalog);
	u32 intr, intr_ack;

	intr = dp_read_ahb(catalog, REG_DP_INTR_STATUS2);
	intr &= ~DP_INTERRUPT_STATUS2_MASK;
	intr_ack = (intr & DP_INTERRUPT_STATUS2)
			<< DP_INTERRUPT_STATUS_ACK_SHIFT;
	dp_write_ahb(catalog, REG_DP_INTR_STATUS2,
			intr_ack | DP_INTERRUPT_STATUS2_MASK);

	return intr;
}

void dp_catalog_ctrl_phy_reset(struct dp_catalog *dp_catalog)
{
	struct dp_catalog_private *catalog = container_of(dp_catalog,
				struct dp_catalog_private, dp_catalog);

	dp_write_ahb(catalog, REG_DP_PHY_CTRL,
			DP_PHY_CTRL_SW_RESET | DP_PHY_CTRL_SW_RESET_PLL);
	usleep_range(1000, 1100); /* h/w recommended delay */
	dp_write_ahb(catalog, REG_DP_PHY_CTRL, 0x0);
}

int dp_catalog_ctrl_update_vx_px(struct dp_catalog *dp_catalog,
		u8 v_level, u8 p_level)
{
	struct dp_catalog_private *catalog = container_of(dp_catalog,
				struct dp_catalog_private, dp_catalog);
	struct dp_io *dp_io = catalog->io;
	struct phy *phy = dp_io->phy;
	struct phy_configure_opts_dp *opts_dp = &dp_io->phy_opts.dp;

	/* TODO: Update for all lanes instead of just first one */
	opts_dp->voltage[0] = v_level;
	opts_dp->pre[0] = p_level;
	opts_dp->set_voltages = 1;
	phy_configure(phy, &dp_io->phy_opts);
	opts_dp->set_voltages = 0;

	return 0;
}

void dp_catalog_ctrl_send_phy_pattern(struct dp_catalog *dp_catalog,
			u32 pattern)
{
	struct dp_catalog_private *catalog = container_of(dp_catalog,
				struct dp_catalog_private, dp_catalog);
	u32 value = 0x0;

	/* Make sure to clear the current pattern before starting a new one */
	dp_write_link(catalog, REG_DP_STATE_CTRL, 0x0);

	switch (pattern) {
	case DP_PHY_TEST_PATTERN_D10_2:
		dp_write_link(catalog, REG_DP_STATE_CTRL,
				DP_STATE_CTRL_LINK_TRAINING_PATTERN1);
		break;
	case DP_PHY_TEST_PATTERN_ERROR_COUNT:
		value &= ~(1 << 16);
		dp_write_link(catalog, REG_DP_HBR2_COMPLIANCE_SCRAMBLER_RESET,
					value);
		value |= SCRAMBLER_RESET_COUNT_VALUE;
		dp_write_link(catalog, REG_DP_HBR2_COMPLIANCE_SCRAMBLER_RESET,
					value);
		dp_write_link(catalog, REG_DP_MAINLINK_LEVELS,
					DP_MAINLINK_SAFE_TO_EXIT_LEVEL_2);
		dp_write_link(catalog, REG_DP_STATE_CTRL,
					DP_STATE_CTRL_LINK_SYMBOL_ERR_MEASURE);
		break;
	case DP_PHY_TEST_PATTERN_PRBS7:
		dp_write_link(catalog, REG_DP_STATE_CTRL,
				DP_STATE_CTRL_LINK_PRBS7);
		break;
	case DP_PHY_TEST_PATTERN_80BIT_CUSTOM:
		dp_write_link(catalog, REG_DP_STATE_CTRL,
				DP_STATE_CTRL_LINK_TEST_CUSTOM_PATTERN);
		/* 00111110000011111000001111100000 */
		dp_write_link(catalog, REG_DP_TEST_80BIT_CUSTOM_PATTERN_REG0,
				0x3E0F83E0);
		/* 00001111100000111110000011111000 */
		dp_write_link(catalog, REG_DP_TEST_80BIT_CUSTOM_PATTERN_REG1,
				0x0F83E0F8);
		/* 1111100000111110 */
		dp_write_link(catalog, REG_DP_TEST_80BIT_CUSTOM_PATTERN_REG2,
				0x0000F83E);
		break;
	case DP_PHY_TEST_PATTERN_CP2520:
		value = dp_read_link(catalog, REG_DP_MAINLINK_CTRL);
		value &= ~DP_MAINLINK_CTRL_SW_BYPASS_SCRAMBLER;
		dp_write_link(catalog, REG_DP_MAINLINK_CTRL, value);

		value = DP_HBR2_ERM_PATTERN;
		dp_write_link(catalog, REG_DP_HBR2_COMPLIANCE_SCRAMBLER_RESET,
				value);
		value |= SCRAMBLER_RESET_COUNT_VALUE;
		dp_write_link(catalog, REG_DP_HBR2_COMPLIANCE_SCRAMBLER_RESET,
					value);
		dp_write_link(catalog, REG_DP_MAINLINK_LEVELS,
					DP_MAINLINK_SAFE_TO_EXIT_LEVEL_2);
		dp_write_link(catalog, REG_DP_STATE_CTRL,
					DP_STATE_CTRL_LINK_SYMBOL_ERR_MEASURE);
		value = dp_read_link(catalog, REG_DP_MAINLINK_CTRL);
		value |= DP_MAINLINK_CTRL_ENABLE;
		dp_write_link(catalog, REG_DP_MAINLINK_CTRL, value);
		break;
	case DP_PHY_TEST_PATTERN_SEL_MASK:
		dp_write_link(catalog, REG_DP_MAINLINK_CTRL,
				DP_MAINLINK_CTRL_ENABLE);
		dp_write_link(catalog, REG_DP_STATE_CTRL,
				DP_STATE_CTRL_LINK_TRAINING_PATTERN4);
		break;
	default:
		DRM_DEBUG_DP("No valid test pattern requested:0x%x\n", pattern);
		break;
	}
}

u32 dp_catalog_ctrl_read_phy_pattern(struct dp_catalog *dp_catalog)
{
	struct dp_catalog_private *catalog = container_of(dp_catalog,
				struct dp_catalog_private, dp_catalog);

	return dp_read_link(catalog, REG_DP_MAINLINK_READY);
}

/* panel related catalog functions */
int dp_catalog_panel_timing_cfg(struct dp_catalog *dp_catalog)
{
	struct dp_catalog_private *catalog = container_of(dp_catalog,
				struct dp_catalog_private, dp_catalog);

	dp_write_link(catalog, REG_DP_TOTAL_HOR_VER,
				dp_catalog->total);
	dp_write_link(catalog, REG_DP_START_HOR_VER_FROM_SYNC,
				dp_catalog->sync_start);
	dp_write_link(catalog, REG_DP_HSYNC_VSYNC_WIDTH_POLARITY,
				dp_catalog->width_blanking);
	dp_write_link(catalog, REG_DP_ACTIVE_HOR_VER, dp_catalog->dp_active);
	return 0;
}

void dp_catalog_panel_tpg_enable(struct dp_catalog *dp_catalog,
				struct drm_display_mode *drm_mode)
{
	struct dp_catalog_private *catalog = container_of(dp_catalog,
				struct dp_catalog_private, dp_catalog);
	u32 hsync_period, vsync_period;
	u32 display_v_start, display_v_end;
	u32 hsync_start_x, hsync_end_x;
	u32 v_sync_width;
	u32 hsync_ctl;
	u32 display_hctl;

	/* TPG config parameters*/
	hsync_period = drm_mode->htotal;
	vsync_period = drm_mode->vtotal;

	display_v_start = ((drm_mode->vtotal - drm_mode->vsync_start) *
					hsync_period);
	display_v_end = ((vsync_period - (drm_mode->vsync_start -
					drm_mode->vdisplay))
					* hsync_period) - 1;

	display_v_start += drm_mode->htotal - drm_mode->hsync_start;
	display_v_end -= (drm_mode->hsync_start - drm_mode->hdisplay);

	hsync_start_x = drm_mode->htotal - drm_mode->hsync_start;
	hsync_end_x = hsync_period - (drm_mode->hsync_start -
					drm_mode->hdisplay) - 1;

	v_sync_width = drm_mode->vsync_end - drm_mode->vsync_start;

	hsync_ctl = (hsync_period << 16) |
			(drm_mode->hsync_end - drm_mode->hsync_start);
	display_hctl = (hsync_end_x << 16) | hsync_start_x;


	dp_write_p0(catalog, MMSS_DP_INTF_CONFIG, 0x0);
	dp_write_p0(catalog, MMSS_DP_INTF_HSYNC_CTL, hsync_ctl);
	dp_write_p0(catalog, MMSS_DP_INTF_VSYNC_PERIOD_F0, vsync_period *
			hsync_period);
	dp_write_p0(catalog, MMSS_DP_INTF_VSYNC_PULSE_WIDTH_F0, v_sync_width *
			hsync_period);
	dp_write_p0(catalog, MMSS_DP_INTF_VSYNC_PERIOD_F1, 0);
	dp_write_p0(catalog, MMSS_DP_INTF_VSYNC_PULSE_WIDTH_F1, 0);
	dp_write_p0(catalog, MMSS_DP_INTF_DISPLAY_HCTL, display_hctl);
	dp_write_p0(catalog, MMSS_DP_INTF_ACTIVE_HCTL, 0);
	dp_write_p0(catalog, MMSS_INTF_DISPLAY_V_START_F0, display_v_start);
	dp_write_p0(catalog, MMSS_DP_INTF_DISPLAY_V_END_F0, display_v_end);
	dp_write_p0(catalog, MMSS_INTF_DISPLAY_V_START_F1, 0);
	dp_write_p0(catalog, MMSS_DP_INTF_DISPLAY_V_END_F1, 0);
	dp_write_p0(catalog, MMSS_DP_INTF_ACTIVE_V_START_F0, 0);
	dp_write_p0(catalog, MMSS_DP_INTF_ACTIVE_V_END_F0, 0);
	dp_write_p0(catalog, MMSS_DP_INTF_ACTIVE_V_START_F1, 0);
	dp_write_p0(catalog, MMSS_DP_INTF_ACTIVE_V_END_F1, 0);
	dp_write_p0(catalog, MMSS_DP_INTF_POLARITY_CTL, 0);

	dp_write_p0(catalog, MMSS_DP_TPG_MAIN_CONTROL,
				DP_TPG_CHECKERED_RECT_PATTERN);
	dp_write_p0(catalog, MMSS_DP_TPG_VIDEO_CONFIG,
				DP_TPG_VIDEO_CONFIG_BPP_8BIT |
				DP_TPG_VIDEO_CONFIG_RGB);
	dp_write_p0(catalog, MMSS_DP_BIST_ENABLE,
				DP_BIST_ENABLE_DPBIST_EN);
	dp_write_p0(catalog, MMSS_DP_TIMING_ENGINE_EN,
				DP_TIMING_ENGINE_EN_EN);
	DRM_DEBUG_DP("%s: enabled tpg\n", __func__);
}

void dp_catalog_panel_tpg_disable(struct dp_catalog *dp_catalog)
{
	struct dp_catalog_private *catalog = container_of(dp_catalog,
				struct dp_catalog_private, dp_catalog);

	dp_write_p0(catalog, MMSS_DP_TPG_MAIN_CONTROL, 0x0);
	dp_write_p0(catalog, MMSS_DP_BIST_ENABLE, 0x0);
	dp_write_p0(catalog, MMSS_DP_TIMING_ENGINE_EN, 0x0);
}

struct dp_catalog *dp_catalog_get(struct device *dev, struct dp_io *io)
{
	struct dp_catalog_private *catalog;

	if (!io) {
		DRM_ERROR("invalid input\n");
		return ERR_PTR(-EINVAL);
	}

	catalog  = devm_kzalloc(dev, sizeof(*catalog), GFP_KERNEL);
	if (!catalog)
		return ERR_PTR(-ENOMEM);

	catalog->dev = dev;
	catalog->io = io;

	return &catalog->dp_catalog;
}

void dp_catalog_audio_get_header(struct dp_catalog *dp_catalog)
{
	struct dp_catalog_private *catalog;
	u32 (*sdp_map)[DP_AUDIO_SDP_HEADER_MAX];
	enum dp_catalog_audio_sdp_type sdp;
	enum dp_catalog_audio_header_type header;

	if (!dp_catalog)
		return;

	catalog = container_of(dp_catalog,
		struct dp_catalog_private, dp_catalog);

	sdp_map = catalog->audio_map;
	sdp     = dp_catalog->sdp_type;
	header  = dp_catalog->sdp_header;

	dp_catalog->audio_data = dp_read_link(catalog,
			sdp_map[sdp][header]);
}

void dp_catalog_audio_set_header(struct dp_catalog *dp_catalog)
{
	struct dp_catalog_private *catalog;
	u32 (*sdp_map)[DP_AUDIO_SDP_HEADER_MAX];
	enum dp_catalog_audio_sdp_type sdp;
	enum dp_catalog_audio_header_type header;
	u32 data;

	if (!dp_catalog)
		return;

	catalog = container_of(dp_catalog,
		struct dp_catalog_private, dp_catalog);

	sdp_map = catalog->audio_map;
	sdp     = dp_catalog->sdp_type;
	header  = dp_catalog->sdp_header;
	data    = dp_catalog->audio_data;

	dp_write_link(catalog, sdp_map[sdp][header], data);
}

void dp_catalog_audio_config_acr(struct dp_catalog *dp_catalog)
{
	struct dp_catalog_private *catalog;
	u32 acr_ctrl, select;

	if (!dp_catalog)
		return;

	catalog = container_of(dp_catalog,
		struct dp_catalog_private, dp_catalog);

	select = dp_catalog->audio_data;
	acr_ctrl = select << 4 | BIT(31) | BIT(8) | BIT(14);

	DRM_DEBUG_DP("select = 0x%x, acr_ctrl = 0x%x\n", select, acr_ctrl);

	dp_write_link(catalog, MMSS_DP_AUDIO_ACR_CTRL, acr_ctrl);
}

void dp_catalog_audio_enable(struct dp_catalog *dp_catalog)
{
	struct dp_catalog_private *catalog;
	bool enable;
	u32 audio_ctrl;

	if (!dp_catalog)
		return;

	catalog = container_of(dp_catalog,
		struct dp_catalog_private, dp_catalog);

	enable = !!dp_catalog->audio_data;
	audio_ctrl = dp_read_link(catalog, MMSS_DP_AUDIO_CFG);

	if (enable)
		audio_ctrl |= BIT(0);
	else
		audio_ctrl &= ~BIT(0);

	DRM_DEBUG_DP("dp_audio_cfg = 0x%x\n", audio_ctrl);

	dp_write_link(catalog, MMSS_DP_AUDIO_CFG, audio_ctrl);
	/* make sure audio engine is disabled */
	wmb();
}

void dp_catalog_audio_config_sdp(struct dp_catalog *dp_catalog)
{
	struct dp_catalog_private *catalog;
	u32 sdp_cfg = 0;
	u32 sdp_cfg2 = 0;

	if (!dp_catalog)
		return;

	catalog = container_of(dp_catalog,
		struct dp_catalog_private, dp_catalog);

	sdp_cfg = dp_read_link(catalog, MMSS_DP_SDP_CFG);
	/* AUDIO_TIMESTAMP_SDP_EN */
	sdp_cfg |= BIT(1);
	/* AUDIO_STREAM_SDP_EN */
	sdp_cfg |= BIT(2);
	/* AUDIO_COPY_MANAGEMENT_SDP_EN */
	sdp_cfg |= BIT(5);
	/* AUDIO_ISRC_SDP_EN  */
	sdp_cfg |= BIT(6);
	/* AUDIO_INFOFRAME_SDP_EN  */
	sdp_cfg |= BIT(20);

	DRM_DEBUG_DP("sdp_cfg = 0x%x\n", sdp_cfg);

	dp_write_link(catalog, MMSS_DP_SDP_CFG, sdp_cfg);

	sdp_cfg2 = dp_read_link(catalog, MMSS_DP_SDP_CFG2);
	/* IFRM_REGSRC -> Do not use reg values */
	sdp_cfg2 &= ~BIT(0);
	/* AUDIO_STREAM_HB3_REGSRC-> Do not use reg values */
	sdp_cfg2 &= ~BIT(1);

	DRM_DEBUG_DP("sdp_cfg2 = 0x%x\n", sdp_cfg2);

	dp_write_link(catalog, MMSS_DP_SDP_CFG2, sdp_cfg2);
}

void dp_catalog_audio_init(struct dp_catalog *dp_catalog)
{
	struct dp_catalog_private *catalog;

	static u32 sdp_map[][DP_AUDIO_SDP_HEADER_MAX] = {
		{
			MMSS_DP_AUDIO_STREAM_0,
			MMSS_DP_AUDIO_STREAM_1,
			MMSS_DP_AUDIO_STREAM_1,
		},
		{
			MMSS_DP_AUDIO_TIMESTAMP_0,
			MMSS_DP_AUDIO_TIMESTAMP_1,
			MMSS_DP_AUDIO_TIMESTAMP_1,
		},
		{
			MMSS_DP_AUDIO_INFOFRAME_0,
			MMSS_DP_AUDIO_INFOFRAME_1,
			MMSS_DP_AUDIO_INFOFRAME_1,
		},
		{
			MMSS_DP_AUDIO_COPYMANAGEMENT_0,
			MMSS_DP_AUDIO_COPYMANAGEMENT_1,
			MMSS_DP_AUDIO_COPYMANAGEMENT_1,
		},
		{
			MMSS_DP_AUDIO_ISRC_0,
			MMSS_DP_AUDIO_ISRC_1,
			MMSS_DP_AUDIO_ISRC_1,
		},
	};

	if (!dp_catalog)
		return;

	catalog = container_of(dp_catalog,
		struct dp_catalog_private, dp_catalog);

	catalog->audio_map = sdp_map;
}

void dp_catalog_audio_sfe_level(struct dp_catalog *dp_catalog)
{
	struct dp_catalog_private *catalog;
	u32 mainlink_levels, safe_to_exit_level;

	if (!dp_catalog)
		return;

	catalog = container_of(dp_catalog,
		struct dp_catalog_private, dp_catalog);

	safe_to_exit_level = dp_catalog->audio_data;
	mainlink_levels = dp_read_link(catalog, REG_DP_MAINLINK_LEVELS);
	mainlink_levels &= 0xFE0;
	mainlink_levels |= safe_to_exit_level;

	DRM_DEBUG_DP("mainlink_level = 0x%x, safe_to_exit_level = 0x%x\n",
			 mainlink_levels, safe_to_exit_level);

	dp_write_link(catalog, REG_DP_MAINLINK_LEVELS, mainlink_levels);
}
