// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt)	"[drm-dp] %s: " fmt, __func__

#include <linux/rational.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/rational.h>
#include <drm/drm_dp_helper.h>

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

static u8 const vm_pre_emphasis_hbr_rbr[4][4] = {
	{0x00, 0x0C, 0x14, 0x19},
	{0x00, 0x0B, 0x12, 0xFF},
	{0x00, 0x0B, 0xFF, 0xFF},
	{0x04, 0xFF, 0xFF, 0xFF}
};

static u8 const vm_voltage_swing_hbr_rbr[4][4] = {
	{0x08, 0x0F, 0x16, 0x1F},
	{0x11, 0x1E, 0x1F, 0xFF},
	{0x19, 0x1F, 0xFF, 0xFF},
	{0x1F, 0xFF, 0xFF, 0xFF}
};

/* AUX look-up-table configurations
 * Pair of offset and config values for each LUT
 */
static u8 const aux_lut_offset[] = {
	0x20, 0x24, 0x28, 0x2C, 0x30, 0x34, 0x38, 0x3C, 0x40, 0x44
};

static u8 const
aux_lut_value[PHY_AUX_CFG_MAX][DP_AUX_CFG_MAX_VALUE_CNT] = {
	{ 0x00, 0x00, 0x00, },
	{ 0x13, 0x23, 0x1d, },
	{ 0x24, 0x00, 0x00, },
	{ 0x00, 0x00, 0x00, },
	{ 0x0A, 0x00, 0x00, },
	{ 0x26, 0x00, 0x00, },
	{ 0x0A, 0x00, 0x00, },
	{ 0x03, 0x00, 0x00, },
	{ 0xBB, 0x00, 0x00, },
	{ 0x03, 0x00, 0x00, }
};

struct dp_catalog_private {
	struct device *dev;
	struct dp_io *io;
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

static inline void dp_write_phy(struct dp_catalog_private *catalog,
			       u32 offset, u32 data)
{
	offset += DP_PHY_REG_OFFSET;
	/*
	 * To make sure phy reg writes happens before any other operation,
	 * this function uses writel() instread of writel_relaxed()
	 */
	writel(data, catalog->io->phy_reg.base + offset);
}

static inline u32 dp_read_phy(struct dp_catalog_private *catalog,
			       u32 offset)
{
	offset += DP_PHY_REG_OFFSET;
	/*
	 * To make sure phy reg writes happens before any other operation,
	 * this function uses writel() instread of writel_relaxed()
	 */
	return readl_relaxed(catalog->io->phy_reg.base + offset);
}

static inline void dp_write_pll(struct dp_catalog_private *catalog,
			       u32 offset, u32 data)
{
	offset += DP_PHY_PLL_OFFSET;
	writel_relaxed(data, catalog->io->phy_reg.base + offset);
}

static inline void dp_write_ln_tx0(struct dp_catalog_private *catalog,
			       u32 offset, u32 data)
{
	offset += DP_PHY_LN_TX0_OFFSET;
	writel_relaxed(data, catalog->io->phy_reg.base + offset);
}

static inline void dp_write_ln_tx1(struct dp_catalog_private *catalog,
			       u32 offset, u32 data)
{
	offset += DP_PHY_LN_TX1_OFFSET;
	writel_relaxed(data, catalog->io->phy_reg.base + offset);
}

static inline u32 dp_read_ln_tx0(struct dp_catalog_private *catalog,
			       u32 offset)
{
	offset += DP_PHY_LN_TX0_OFFSET;
	return readl_relaxed(catalog->io->phy_reg.base + offset);
}

static inline u32 dp_read_ln_tx1(struct dp_catalog_private *catalog,
			       u32 offset)
{
	offset += DP_PHY_LN_TX1_OFFSET;
	return readl_relaxed(catalog->io->phy_reg.base + offset);
}

static inline void dp_write_usb_cm(struct dp_catalog_private *catalog,
			       u32 offset, u32 data)
{
	/*
	 * To make sure usb reg writes happens before any other operation,
	 * this function uses writel() instread of writel_relaxed()
	 */
	writel(data, catalog->io->usb3_dp_com.base + offset);
}

static inline u32 dp_read_usb_cm(struct dp_catalog_private *catalog,
			       u32 offset)
{
	/*
	 * To make sure usb reg writes happens before any other operation,
	 * this function uses writel() instread of writel_relaxed()
	 */
	return readl_relaxed(catalog->io->usb3_dp_com.base + offset);
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

void dp_catalog_aux_update_cfg(struct dp_catalog *dp_catalog,
		enum dp_phy_aux_config_type type)
{
	struct dp_catalog_private *catalog = container_of(dp_catalog,
				struct dp_catalog_private, dp_catalog);
	u32 new_index = 0, current_index = 0;

	if (type >= PHY_AUX_CFG_MAX) {
		DRM_ERROR("invalid input\n");
		return;
	}

	current_index = catalog->aux_lut_cfg_index[type];
	new_index = (current_index + 1) % DP_AUX_CFG_MAX_VALUE_CNT;
	DRM_DEBUG_DP("Updating PHY_AUX_CFG%d from 0x%08x to 0x%08x\n",
			type, aux_lut_value[type][current_index],
			aux_lut_value[type][new_index]);

	dp_write_phy(catalog, aux_lut_offset[type],
			aux_lut_value[type][new_index]);
	catalog->aux_lut_cfg_index[type] = new_index;
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

	pr_info("USB3 DP COM regs\n");
	dump_regs(catalog->io->usb3_dp_com.base, catalog->io->usb3_dp_com.len);

	pr_info("LN TX0 regs\n");
	dump_regs(catalog->io->phy_reg.base + DP_PHY_LN_TX0_OFFSET,
						DP_PHY_LN_TX0_SIZE);

	pr_info("LN TX1 regs\n");
	dump_regs(catalog->io->phy_reg.base + DP_PHY_LN_TX1_OFFSET,
						DP_PHY_LN_TX1_SIZE);

	pr_info("DP PHY regs\n");
	dump_regs(catalog->io->phy_reg.base + DP_PHY_REG_OFFSET,
						DP_PHY_REG_SIZE);
}

void dp_catalog_aux_setup(struct dp_catalog *dp_catalog)
{
	struct dp_catalog_private *catalog = container_of(dp_catalog,
				struct dp_catalog_private, dp_catalog);
	int i = 0;

	dp_write_phy(catalog, REG_DP_PHY_PD_CTL, DP_PHY_PD_CTL_PWRDN |
		DP_PHY_PD_CTL_AUX_PWRDN | DP_PHY_PD_CTL_PLL_PWRDN |
		DP_PHY_PD_CTL_DP_CLAMP_EN);

	/* Turn on BIAS current for PHY/PLL */
	dp_write_pll(catalog,
		QSERDES_COM_BIAS_EN_CLKBUFLR_EN, QSERDES_COM_BIAS_EN |
		QSERDES_COM_BIAS_EN_MUX | QSERDES_COM_CLKBUF_L_EN |
		QSERDES_COM_EN_SYSCLK_TX_SEL);

	dp_write_phy(catalog, REG_DP_PHY_PD_CTL, DP_PHY_PD_CTL_PSR_PWRDN);

	dp_write_phy(catalog, REG_DP_PHY_PD_CTL, DP_PHY_PD_CTL_PWRDN |
		DP_PHY_PD_CTL_AUX_PWRDN | DP_PHY_PD_CTL_LANE_0_1_PWRDN
		| DP_PHY_PD_CTL_LANE_2_3_PWRDN | DP_PHY_PD_CTL_PLL_PWRDN
		| DP_PHY_PD_CTL_DP_CLAMP_EN);

	dp_write_pll(catalog,
		QSERDES_COM_BIAS_EN_CLKBUFLR_EN, QSERDES_COM_BIAS_EN |
		QSERDES_COM_BIAS_EN_MUX | QSERDES_COM_CLKBUF_R_EN |
		QSERDES_COM_CLKBUF_L_EN | QSERDES_COM_EN_SYSCLK_TX_SEL |
		QSERDES_COM_CLKBUF_RX_DRIVE_L);

	/* DP AUX CFG register programming */
	for (i = 0; i < PHY_AUX_CFG_MAX; i++) {
		DRM_DEBUG_DP("PHY_AUX_CFG%ds: offset=0x%08x, value=0x%08x\n",
			i, aux_lut_offset[i], aux_lut_value[i][0]);
		dp_write_phy(catalog, aux_lut_offset[i],
				     aux_lut_value[i][0]);
	}

	dp_write_phy(catalog, REG_DP_PHY_AUX_INTERRUPT_MASK,
			PHY_AUX_STOP_ERR_MASK |	PHY_AUX_DEC_ERR_MASK |
			PHY_AUX_SYNC_ERR_MASK |	PHY_AUX_ALIGN_ERR_MASK |
			PHY_AUX_REQ_ERR_MASK);
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
	u32 mvid, nvid, div, pixel_div = 0, dispcc_input_rate;
	u32 const nvid_fixed = DP_LINK_CONSTANT_N_VALUE;
	u32 const link_rate_hbr2 = 540000;
	u32 const link_rate_hbr3 = 810000;
	unsigned long den, num;

	struct dp_catalog_private *catalog = container_of(dp_catalog,
				struct dp_catalog_private, dp_catalog);

	div = dp_read_phy(catalog, REG_DP_PHY_VCO_DIV);
	div &= 0x03;

	if (div == 0)
		pixel_div = 6;
	else if (div == 1)
		pixel_div = 2;
	else if (div == 2)
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

void dp_catalog_ctrl_usb_reset(struct dp_catalog *dp_catalog, bool flip)
{
	struct dp_catalog_private *catalog = container_of(dp_catalog,
				struct dp_catalog_private, dp_catalog);
	u32 typec_ctrl;

	dp_write_usb_cm(catalog, REG_USB3_DP_COM_RESET_OVRD_CTRL,
			USB3_DP_COM_OVRD_CTRL_SW_DPPHY_RESET_MUX |
			USB3_DP_COM_OVRD_CTRL_SW_USB3PHY_RESET_MUX);
	dp_write_usb_cm(catalog, REG_USB3_DP_COM_PHY_MODE_CTRL,
						USB3_DP_COM_PHY_MODE_DP);
	dp_write_usb_cm(catalog, REG_USB3_DP_COM_SW_RESET,
						USB3_DP_COM_SW_RESET_SET);

	/* Default configuration i.e CC1 */
	typec_ctrl = USB3_DP_COM_TYPEC_CTRL_PORTSEL_MUX;
	if (flip)
		typec_ctrl |= USB3_DP_COM_TYPEC_CTRL_PORTSEL;

	dp_write_usb_cm(catalog, REG_USB3_DP_COM_TYPEC_CTRL, typec_ctrl);

	dp_write_usb_cm(catalog, REG_USB3_DP_COM_SWI_CTRL, 0x00);
	dp_write_usb_cm(catalog, REG_USB3_DP_COM_SW_RESET, 0x00);

	dp_write_usb_cm(catalog, REG_USB3_DP_COM_POWER_DOWN_CTRL,
					USB3_DP_COM_POWER_DOWN_CTRL_SW_PWRDN);
	dp_write_usb_cm(catalog, REG_USB3_DP_COM_RESET_OVRD_CTRL, 0x00);

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
			DP_PHY_CTRL_SW_RESET_PLL | DP_PHY_CTRL_SW_RESET);
	usleep_range(1000, 1100); /* h/w recommended delay */
	dp_write_ahb(catalog, REG_DP_PHY_CTRL, 0x0);
}

void dp_catalog_ctrl_phy_lane_cfg(struct dp_catalog *dp_catalog,
		bool flipped, u8 ln_cnt)
{
	u32 info;
	struct dp_catalog_private *catalog = container_of(dp_catalog,
				struct dp_catalog_private, dp_catalog);
	u8 orientation = BIT(!!flipped);

	info = ln_cnt & DP_PHY_SPARE0_MASK;
	info |= (orientation & DP_PHY_SPARE0_MASK)
			<< DP_PHY_SPARE0_ORIENTATION_INFO_SHIFT;
	DRM_DEBUG_DP("Shared Info = 0x%x\n", info);

	dp_write_phy(catalog, REG_DP_PHY_SPARE0, info);
}

int dp_catalog_ctrl_update_vx_px(struct dp_catalog *dp_catalog,
		u8 v_level, u8 p_level)
{
	struct dp_catalog_private *catalog = container_of(dp_catalog,
				struct dp_catalog_private, dp_catalog);
	u8 voltage_swing_cfg, pre_emphasis_cfg;

	DRM_DEBUG_DP("hw: v=%d p=%d\n", v_level, p_level);

	voltage_swing_cfg = vm_voltage_swing_hbr_rbr[v_level][p_level];
	pre_emphasis_cfg = vm_pre_emphasis_hbr_rbr[v_level][p_level];

	if (voltage_swing_cfg == 0xFF && pre_emphasis_cfg == 0xFF) {
		DRM_ERROR("invalid vx (0x%x=0x%x), px (0x%x=0x%x\n",
			v_level, voltage_swing_cfg, p_level, pre_emphasis_cfg);
		return -EINVAL;
	}

	/* Enable MUX to use Cursor values from these registers */
	voltage_swing_cfg |= DP_PHY_TXn_TX_DRV_LVL_MUX_EN;
	pre_emphasis_cfg |= DP_PHY_TXn_TX_EMP_POST1_LVL_MUX_EN;

	/* Configure host and panel only if both values are allowed */
	dp_write_ln_tx0(catalog, REG_DP_PHY_TXn_TX_DRV_LVL, voltage_swing_cfg);
	dp_write_ln_tx1(catalog, REG_DP_PHY_TXn_TX_DRV_LVL, voltage_swing_cfg);
	dp_write_ln_tx0(catalog, REG_DP_PHY_TXn_TX_EMP_POST1_LVL,
					pre_emphasis_cfg);
	dp_write_ln_tx1(catalog, REG_DP_PHY_TXn_TX_EMP_POST1_LVL,
					pre_emphasis_cfg);
	DRM_DEBUG_DP("hw: vx_value=0x%x px_value=0x%x\n",
			voltage_swing_cfg, pre_emphasis_cfg);

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
