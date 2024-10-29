// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt)	"[drm-dp] %s: " fmt, __func__

#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/platform_device.h>
#include <linux/rational.h>
#include <drm/display/drm_dp_helper.h>
#include <drm/drm_print.h>

#include "dp_catalog.h"
#include "dp_reg.h"

#define POLLING_SLEEP_US			1000
#define POLLING_TIMEOUT_US			10000

#define SCRAMBLER_RESET_COUNT_VALUE		0xFC

#define DP_INTERRUPT_STATUS_ACK_SHIFT	1
#define DP_INTERRUPT_STATUS_MASK_SHIFT	2

#define DP_INTF_CONFIG_DATABUS_WIDEN     BIT(4)

#define DP_INTERRUPT_STATUS1 \
	(DP_INTR_AUX_XFER_DONE| \
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

#define DP_INTERRUPT_STATUS4 \
	(PSR_UPDATE_INT | PSR_CAPTURE_INT | PSR_EXIT_INT | \
	PSR_UPDATE_ERROR_INT | PSR_WAKE_ERROR_INT)

#define DP_INTERRUPT_MASK4 \
	(PSR_UPDATE_MASK | PSR_CAPTURE_MASK | PSR_EXIT_MASK | \
	PSR_UPDATE_ERROR_MASK | PSR_WAKE_ERROR_MASK)

#define DP_DEFAULT_AHB_OFFSET	0x0000
#define DP_DEFAULT_AHB_SIZE	0x0200
#define DP_DEFAULT_AUX_OFFSET	0x0200
#define DP_DEFAULT_AUX_SIZE	0x0200
#define DP_DEFAULT_LINK_OFFSET	0x0400
#define DP_DEFAULT_LINK_SIZE	0x0C00
#define DP_DEFAULT_P0_OFFSET	0x1000
#define DP_DEFAULT_P0_SIZE	0x0400

struct dss_io_region {
	size_t len;
	void __iomem *base;
};

struct dss_io_data {
	struct dss_io_region ahb;
	struct dss_io_region aux;
	struct dss_io_region link;
	struct dss_io_region p0;
};

struct msm_dp_catalog_private {
	struct device *dev;
	struct drm_device *drm_dev;
	struct dss_io_data io;
	u32 (*audio_map)[DP_AUDIO_SDP_HEADER_MAX];
	struct msm_dp_catalog msm_dp_catalog;
};

void msm_dp_catalog_snapshot(struct msm_dp_catalog *msm_dp_catalog, struct msm_disp_state *disp_state)
{
	struct msm_dp_catalog_private *catalog = container_of(msm_dp_catalog,
			struct msm_dp_catalog_private, msm_dp_catalog);
	struct dss_io_data *dss = &catalog->io;

	msm_disp_snapshot_add_block(disp_state, dss->ahb.len, dss->ahb.base, "dp_ahb");
	msm_disp_snapshot_add_block(disp_state, dss->aux.len, dss->aux.base, "dp_aux");
	msm_disp_snapshot_add_block(disp_state, dss->link.len, dss->link.base, "dp_link");
	msm_disp_snapshot_add_block(disp_state, dss->p0.len, dss->p0.base, "dp_p0");
}

static inline u32 msm_dp_read_aux(struct msm_dp_catalog_private *catalog, u32 offset)
{
	return readl_relaxed(catalog->io.aux.base + offset);
}

static inline void msm_dp_write_aux(struct msm_dp_catalog_private *catalog,
			       u32 offset, u32 data)
{
	/*
	 * To make sure aux reg writes happens before any other operation,
	 * this function uses writel() instread of writel_relaxed()
	 */
	writel(data, catalog->io.aux.base + offset);
}

static inline u32 msm_dp_read_ahb(const struct msm_dp_catalog_private *catalog, u32 offset)
{
	return readl_relaxed(catalog->io.ahb.base + offset);
}

static inline void msm_dp_write_ahb(struct msm_dp_catalog_private *catalog,
			       u32 offset, u32 data)
{
	/*
	 * To make sure phy reg writes happens before any other operation,
	 * this function uses writel() instread of writel_relaxed()
	 */
	writel(data, catalog->io.ahb.base + offset);
}

static inline void msm_dp_write_p0(struct msm_dp_catalog_private *catalog,
			       u32 offset, u32 data)
{
	/*
	 * To make sure interface reg writes happens before any other operation,
	 * this function uses writel() instread of writel_relaxed()
	 */
	writel(data, catalog->io.p0.base + offset);
}

static inline u32 msm_dp_read_p0(struct msm_dp_catalog_private *catalog,
			       u32 offset)
{
	/*
	 * To make sure interface reg writes happens before any other operation,
	 * this function uses writel() instread of writel_relaxed()
	 */
	return readl_relaxed(catalog->io.p0.base + offset);
}

static inline u32 msm_dp_read_link(struct msm_dp_catalog_private *catalog, u32 offset)
{
	return readl_relaxed(catalog->io.link.base + offset);
}

static inline void msm_dp_write_link(struct msm_dp_catalog_private *catalog,
			       u32 offset, u32 data)
{
	/*
	 * To make sure link reg writes happens before any other operation,
	 * this function uses writel() instread of writel_relaxed()
	 */
	writel(data, catalog->io.link.base + offset);
}

/* aux related catalog functions */
u32 msm_dp_catalog_aux_read_data(struct msm_dp_catalog *msm_dp_catalog)
{
	struct msm_dp_catalog_private *catalog = container_of(msm_dp_catalog,
				struct msm_dp_catalog_private, msm_dp_catalog);

	return msm_dp_read_aux(catalog, REG_DP_AUX_DATA);
}

int msm_dp_catalog_aux_write_data(struct msm_dp_catalog *msm_dp_catalog, u32 data)
{
	struct msm_dp_catalog_private *catalog = container_of(msm_dp_catalog,
				struct msm_dp_catalog_private, msm_dp_catalog);

	msm_dp_write_aux(catalog, REG_DP_AUX_DATA, data);
	return 0;
}

int msm_dp_catalog_aux_write_trans(struct msm_dp_catalog *msm_dp_catalog, u32 data)
{
	struct msm_dp_catalog_private *catalog = container_of(msm_dp_catalog,
				struct msm_dp_catalog_private, msm_dp_catalog);

	msm_dp_write_aux(catalog, REG_DP_AUX_TRANS_CTRL, data);
	return 0;
}

int msm_dp_catalog_aux_clear_trans(struct msm_dp_catalog *msm_dp_catalog, bool read)
{
	u32 data;
	struct msm_dp_catalog_private *catalog = container_of(msm_dp_catalog,
				struct msm_dp_catalog_private, msm_dp_catalog);

	if (read) {
		data = msm_dp_read_aux(catalog, REG_DP_AUX_TRANS_CTRL);
		data &= ~DP_AUX_TRANS_CTRL_GO;
		msm_dp_write_aux(catalog, REG_DP_AUX_TRANS_CTRL, data);
	} else {
		msm_dp_write_aux(catalog, REG_DP_AUX_TRANS_CTRL, 0);
	}
	return 0;
}

int msm_dp_catalog_aux_clear_hw_interrupts(struct msm_dp_catalog *msm_dp_catalog)
{
	struct msm_dp_catalog_private *catalog = container_of(msm_dp_catalog,
				struct msm_dp_catalog_private, msm_dp_catalog);

	msm_dp_read_aux(catalog, REG_DP_PHY_AUX_INTERRUPT_STATUS);
	msm_dp_write_aux(catalog, REG_DP_PHY_AUX_INTERRUPT_CLEAR, 0x1f);
	msm_dp_write_aux(catalog, REG_DP_PHY_AUX_INTERRUPT_CLEAR, 0x9f);
	msm_dp_write_aux(catalog, REG_DP_PHY_AUX_INTERRUPT_CLEAR, 0);
	return 0;
}

/**
 * msm_dp_catalog_aux_reset() - reset AUX controller
 *
 * @msm_dp_catalog: DP catalog structure
 *
 * return: void
 *
 * This function reset AUX controller
 *
 * NOTE: reset AUX controller will also clear any pending HPD related interrupts
 * 
 */
void msm_dp_catalog_aux_reset(struct msm_dp_catalog *msm_dp_catalog)
{
	u32 aux_ctrl;
	struct msm_dp_catalog_private *catalog = container_of(msm_dp_catalog,
				struct msm_dp_catalog_private, msm_dp_catalog);

	aux_ctrl = msm_dp_read_aux(catalog, REG_DP_AUX_CTRL);

	aux_ctrl |= DP_AUX_CTRL_RESET;
	msm_dp_write_aux(catalog, REG_DP_AUX_CTRL, aux_ctrl);
	usleep_range(1000, 1100); /* h/w recommended delay */

	aux_ctrl &= ~DP_AUX_CTRL_RESET;
	msm_dp_write_aux(catalog, REG_DP_AUX_CTRL, aux_ctrl);
}

void msm_dp_catalog_aux_enable(struct msm_dp_catalog *msm_dp_catalog, bool enable)
{
	u32 aux_ctrl;
	struct msm_dp_catalog_private *catalog = container_of(msm_dp_catalog,
				struct msm_dp_catalog_private, msm_dp_catalog);

	aux_ctrl = msm_dp_read_aux(catalog, REG_DP_AUX_CTRL);

	if (enable) {
		msm_dp_write_aux(catalog, REG_DP_TIMEOUT_COUNT, 0xffff);
		msm_dp_write_aux(catalog, REG_DP_AUX_LIMITS, 0xffff);
		aux_ctrl |= DP_AUX_CTRL_ENABLE;
	} else {
		aux_ctrl &= ~DP_AUX_CTRL_ENABLE;
	}

	msm_dp_write_aux(catalog, REG_DP_AUX_CTRL, aux_ctrl);
}

int msm_dp_catalog_aux_wait_for_hpd_connect_state(struct msm_dp_catalog *msm_dp_catalog,
					      unsigned long wait_us)
{
	u32 state;
	struct msm_dp_catalog_private *catalog = container_of(msm_dp_catalog,
				struct msm_dp_catalog_private, msm_dp_catalog);

	/* poll for hpd connected status every 2ms and timeout after wait_us */
	return readl_poll_timeout(catalog->io.aux.base +
				REG_DP_DP_HPD_INT_STATUS,
				state, state & DP_DP_HPD_STATE_STATUS_CONNECTED,
				min(wait_us, 2000), wait_us);
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

void msm_dp_catalog_dump_regs(struct msm_dp_catalog *msm_dp_catalog)
{
	struct msm_dp_catalog_private *catalog = container_of(msm_dp_catalog,
		struct msm_dp_catalog_private, msm_dp_catalog);
	struct dss_io_data *io = &catalog->io;

	pr_info("AHB regs\n");
	dump_regs(io->ahb.base, io->ahb.len);

	pr_info("AUXCLK regs\n");
	dump_regs(io->aux.base, io->aux.len);

	pr_info("LCLK regs\n");
	dump_regs(io->link.base, io->link.len);

	pr_info("P0CLK regs\n");
	dump_regs(io->p0.base, io->p0.len);
}

u32 msm_dp_catalog_aux_get_irq(struct msm_dp_catalog *msm_dp_catalog)
{
	struct msm_dp_catalog_private *catalog = container_of(msm_dp_catalog,
				struct msm_dp_catalog_private, msm_dp_catalog);
	u32 intr, intr_ack;

	intr = msm_dp_read_ahb(catalog, REG_DP_INTR_STATUS);
	intr &= ~DP_INTERRUPT_STATUS1_MASK;
	intr_ack = (intr & DP_INTERRUPT_STATUS1)
			<< DP_INTERRUPT_STATUS_ACK_SHIFT;
	msm_dp_write_ahb(catalog, REG_DP_INTR_STATUS, intr_ack |
			DP_INTERRUPT_STATUS1_MASK);

	return intr;

}

/* controller related catalog functions */
void msm_dp_catalog_ctrl_update_transfer_unit(struct msm_dp_catalog *msm_dp_catalog,
				u32 msm_dp_tu, u32 valid_boundary,
				u32 valid_boundary2)
{
	struct msm_dp_catalog_private *catalog = container_of(msm_dp_catalog,
				struct msm_dp_catalog_private, msm_dp_catalog);

	msm_dp_write_link(catalog, REG_DP_VALID_BOUNDARY, valid_boundary);
	msm_dp_write_link(catalog, REG_DP_TU, msm_dp_tu);
	msm_dp_write_link(catalog, REG_DP_VALID_BOUNDARY_2, valid_boundary2);
}

void msm_dp_catalog_ctrl_state_ctrl(struct msm_dp_catalog *msm_dp_catalog, u32 state)
{
	struct msm_dp_catalog_private *catalog = container_of(msm_dp_catalog,
				struct msm_dp_catalog_private, msm_dp_catalog);

	msm_dp_write_link(catalog, REG_DP_STATE_CTRL, state);
}

void msm_dp_catalog_ctrl_config_ctrl(struct msm_dp_catalog *msm_dp_catalog, u32 cfg)
{
	struct msm_dp_catalog_private *catalog = container_of(msm_dp_catalog,
				struct msm_dp_catalog_private, msm_dp_catalog);

	drm_dbg_dp(catalog->drm_dev, "DP_CONFIGURATION_CTRL=0x%x\n", cfg);

	msm_dp_write_link(catalog, REG_DP_CONFIGURATION_CTRL, cfg);
}

void msm_dp_catalog_ctrl_lane_mapping(struct msm_dp_catalog *msm_dp_catalog)
{
	struct msm_dp_catalog_private *catalog = container_of(msm_dp_catalog,
				struct msm_dp_catalog_private, msm_dp_catalog);
	u32 ln_0 = 0, ln_1 = 1, ln_2 = 2, ln_3 = 3; /* One-to-One mapping */
	u32 ln_mapping;

	ln_mapping = ln_0 << LANE0_MAPPING_SHIFT;
	ln_mapping |= ln_1 << LANE1_MAPPING_SHIFT;
	ln_mapping |= ln_2 << LANE2_MAPPING_SHIFT;
	ln_mapping |= ln_3 << LANE3_MAPPING_SHIFT;

	msm_dp_write_link(catalog, REG_DP_LOGICAL2PHYSICAL_LANE_MAPPING,
			ln_mapping);
}

void msm_dp_catalog_ctrl_psr_mainlink_enable(struct msm_dp_catalog *msm_dp_catalog,
						bool enable)
{
	u32 val;
	struct msm_dp_catalog_private *catalog = container_of(msm_dp_catalog,
				struct msm_dp_catalog_private, msm_dp_catalog);

	val = msm_dp_read_link(catalog, REG_DP_MAINLINK_CTRL);

	if (enable)
		val |= DP_MAINLINK_CTRL_ENABLE;
	else
		val &= ~DP_MAINLINK_CTRL_ENABLE;

	msm_dp_write_link(catalog, REG_DP_MAINLINK_CTRL, val);
}

void msm_dp_catalog_ctrl_mainlink_ctrl(struct msm_dp_catalog *msm_dp_catalog,
						bool enable)
{
	u32 mainlink_ctrl;
	struct msm_dp_catalog_private *catalog = container_of(msm_dp_catalog,
				struct msm_dp_catalog_private, msm_dp_catalog);

	drm_dbg_dp(catalog->drm_dev, "enable=%d\n", enable);
	if (enable) {
		/*
		 * To make sure link reg writes happens before other operation,
		 * msm_dp_write_link() function uses writel()
		 */
		mainlink_ctrl = msm_dp_read_link(catalog, REG_DP_MAINLINK_CTRL);

		mainlink_ctrl &= ~(DP_MAINLINK_CTRL_RESET |
						DP_MAINLINK_CTRL_ENABLE);
		msm_dp_write_link(catalog, REG_DP_MAINLINK_CTRL, mainlink_ctrl);

		mainlink_ctrl |= DP_MAINLINK_CTRL_RESET;
		msm_dp_write_link(catalog, REG_DP_MAINLINK_CTRL, mainlink_ctrl);

		mainlink_ctrl &= ~DP_MAINLINK_CTRL_RESET;
		msm_dp_write_link(catalog, REG_DP_MAINLINK_CTRL, mainlink_ctrl);

		mainlink_ctrl |= (DP_MAINLINK_CTRL_ENABLE |
					DP_MAINLINK_FB_BOUNDARY_SEL);
		msm_dp_write_link(catalog, REG_DP_MAINLINK_CTRL, mainlink_ctrl);
	} else {
		mainlink_ctrl = msm_dp_read_link(catalog, REG_DP_MAINLINK_CTRL);
		mainlink_ctrl &= ~DP_MAINLINK_CTRL_ENABLE;
		msm_dp_write_link(catalog, REG_DP_MAINLINK_CTRL, mainlink_ctrl);
	}
}

void msm_dp_catalog_ctrl_config_misc(struct msm_dp_catalog *msm_dp_catalog,
					u32 colorimetry_cfg,
					u32 test_bits_depth)
{
	u32 misc_val;
	struct msm_dp_catalog_private *catalog = container_of(msm_dp_catalog,
				struct msm_dp_catalog_private, msm_dp_catalog);

	misc_val = msm_dp_read_link(catalog, REG_DP_MISC1_MISC0);

	/* clear bpp bits */
	misc_val &= ~(0x07 << DP_MISC0_TEST_BITS_DEPTH_SHIFT);
	misc_val |= colorimetry_cfg << DP_MISC0_COLORIMETRY_CFG_SHIFT;
	misc_val |= test_bits_depth << DP_MISC0_TEST_BITS_DEPTH_SHIFT;
	/* Configure clock to synchronous mode */
	misc_val |= DP_MISC0_SYNCHRONOUS_CLK;

	drm_dbg_dp(catalog->drm_dev, "misc settings = 0x%x\n", misc_val);
	msm_dp_write_link(catalog, REG_DP_MISC1_MISC0, misc_val);
}

void msm_dp_catalog_setup_peripheral_flush(struct msm_dp_catalog *msm_dp_catalog)
{
	u32 mainlink_ctrl, hw_revision;
	struct msm_dp_catalog_private *catalog = container_of(msm_dp_catalog,
				struct msm_dp_catalog_private, msm_dp_catalog);

	mainlink_ctrl = msm_dp_read_link(catalog, REG_DP_MAINLINK_CTRL);

	hw_revision = msm_dp_catalog_hw_revision(msm_dp_catalog);
	if (hw_revision >= DP_HW_VERSION_1_2)
		mainlink_ctrl |= DP_MAINLINK_FLUSH_MODE_SDE_PERIPH_UPDATE;
	else
		mainlink_ctrl |= DP_MAINLINK_FLUSH_MODE_UPDATE_SDP;

	msm_dp_write_link(catalog, REG_DP_MAINLINK_CTRL, mainlink_ctrl);
}

void msm_dp_catalog_ctrl_config_msa(struct msm_dp_catalog *msm_dp_catalog,
					u32 rate, u32 stream_rate_khz,
					bool is_ycbcr_420)
{
	u32 pixel_m, pixel_n;
	u32 mvid, nvid, pixel_div = 0, dispcc_input_rate;
	u32 const nvid_fixed = DP_LINK_CONSTANT_N_VALUE;
	u32 const link_rate_hbr2 = 540000;
	u32 const link_rate_hbr3 = 810000;
	unsigned long den, num;

	struct msm_dp_catalog_private *catalog = container_of(msm_dp_catalog,
				struct msm_dp_catalog_private, msm_dp_catalog);

	if (rate == link_rate_hbr3)
		pixel_div = 6;
	else if (rate == 162000 || rate == 270000)
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

	if (is_ycbcr_420)
		mvid /= 2;

	if (link_rate_hbr2 == rate)
		nvid *= 2;

	if (link_rate_hbr3 == rate)
		nvid *= 3;

	drm_dbg_dp(catalog->drm_dev, "mvid=0x%x, nvid=0x%x\n", mvid, nvid);
	msm_dp_write_link(catalog, REG_DP_SOFTWARE_MVID, mvid);
	msm_dp_write_link(catalog, REG_DP_SOFTWARE_NVID, nvid);
	msm_dp_write_p0(catalog, MMSS_DP_DSC_DTO, 0x0);
}

int msm_dp_catalog_ctrl_set_pattern_state_bit(struct msm_dp_catalog *msm_dp_catalog,
					u32 state_bit)
{
	int bit, ret;
	u32 data;
	struct msm_dp_catalog_private *catalog = container_of(msm_dp_catalog,
				struct msm_dp_catalog_private, msm_dp_catalog);

	bit = BIT(state_bit - 1);
	drm_dbg_dp(catalog->drm_dev, "hw: bit=%d train=%d\n", bit, state_bit);
	msm_dp_catalog_ctrl_state_ctrl(msm_dp_catalog, bit);

	bit = BIT(state_bit - 1) << DP_MAINLINK_READY_LINK_TRAINING_SHIFT;

	/* Poll for mainlink ready status */
	ret = readx_poll_timeout(readl, catalog->io.link.base +
					REG_DP_MAINLINK_READY,
					data, data & bit,
					POLLING_SLEEP_US, POLLING_TIMEOUT_US);
	if (ret < 0) {
		DRM_ERROR("set state_bit for link_train=%d failed\n", state_bit);
		return ret;
	}
	return 0;
}

/**
 * msm_dp_catalog_hw_revision() - retrieve DP hw revision
 *
 * @msm_dp_catalog: DP catalog structure
 *
 * Return: DP controller hw revision
 *
 */
u32 msm_dp_catalog_hw_revision(const struct msm_dp_catalog *msm_dp_catalog)
{
	const struct msm_dp_catalog_private *catalog = container_of(msm_dp_catalog,
				struct msm_dp_catalog_private, msm_dp_catalog);

	return msm_dp_read_ahb(catalog, REG_DP_HW_VERSION);
}

/**
 * msm_dp_catalog_ctrl_reset() - reset DP controller
 *
 * @msm_dp_catalog: DP catalog structure
 *
 * return: void
 *
 * This function reset the DP controller
 *
 * NOTE: reset DP controller will also clear any pending HPD related interrupts
 * 
 */
void msm_dp_catalog_ctrl_reset(struct msm_dp_catalog *msm_dp_catalog)
{
	u32 sw_reset;
	struct msm_dp_catalog_private *catalog = container_of(msm_dp_catalog,
				struct msm_dp_catalog_private, msm_dp_catalog);

	sw_reset = msm_dp_read_ahb(catalog, REG_DP_SW_RESET);

	sw_reset |= DP_SW_RESET;
	msm_dp_write_ahb(catalog, REG_DP_SW_RESET, sw_reset);
	usleep_range(1000, 1100); /* h/w recommended delay */

	sw_reset &= ~DP_SW_RESET;
	msm_dp_write_ahb(catalog, REG_DP_SW_RESET, sw_reset);
}

bool msm_dp_catalog_ctrl_mainlink_ready(struct msm_dp_catalog *msm_dp_catalog)
{
	u32 data;
	int ret;
	struct msm_dp_catalog_private *catalog = container_of(msm_dp_catalog,
				struct msm_dp_catalog_private, msm_dp_catalog);

	/* Poll for mainlink ready status */
	ret = readl_poll_timeout(catalog->io.link.base +
				REG_DP_MAINLINK_READY,
				data, data & DP_MAINLINK_READY_FOR_VIDEO,
				POLLING_SLEEP_US, POLLING_TIMEOUT_US);
	if (ret < 0) {
		DRM_ERROR("mainlink not ready\n");
		return false;
	}

	return true;
}

void msm_dp_catalog_ctrl_enable_irq(struct msm_dp_catalog *msm_dp_catalog,
						bool enable)
{
	struct msm_dp_catalog_private *catalog = container_of(msm_dp_catalog,
				struct msm_dp_catalog_private, msm_dp_catalog);

	if (enable) {
		msm_dp_write_ahb(catalog, REG_DP_INTR_STATUS,
				DP_INTERRUPT_STATUS1_MASK);
		msm_dp_write_ahb(catalog, REG_DP_INTR_STATUS2,
				DP_INTERRUPT_STATUS2_MASK);
	} else {
		msm_dp_write_ahb(catalog, REG_DP_INTR_STATUS, 0x00);
		msm_dp_write_ahb(catalog, REG_DP_INTR_STATUS2, 0x00);
	}
}

void msm_dp_catalog_hpd_config_intr(struct msm_dp_catalog *msm_dp_catalog,
			u32 intr_mask, bool en)
{
	struct msm_dp_catalog_private *catalog = container_of(msm_dp_catalog,
				struct msm_dp_catalog_private, msm_dp_catalog);

	u32 config = msm_dp_read_aux(catalog, REG_DP_DP_HPD_INT_MASK);

	config = (en ? config | intr_mask : config & ~intr_mask);

	drm_dbg_dp(catalog->drm_dev, "intr_mask=%#x config=%#x\n",
					intr_mask, config);
	msm_dp_write_aux(catalog, REG_DP_DP_HPD_INT_MASK,
				config & DP_DP_HPD_INT_MASK);
}

void msm_dp_catalog_ctrl_hpd_enable(struct msm_dp_catalog *msm_dp_catalog)
{
	struct msm_dp_catalog_private *catalog = container_of(msm_dp_catalog,
				struct msm_dp_catalog_private, msm_dp_catalog);

	u32 reftimer = msm_dp_read_aux(catalog, REG_DP_DP_HPD_REFTIMER);

	/* Configure REFTIMER and enable it */
	reftimer |= DP_DP_HPD_REFTIMER_ENABLE;
	msm_dp_write_aux(catalog, REG_DP_DP_HPD_REFTIMER, reftimer);

	/* Enable HPD */
	msm_dp_write_aux(catalog, REG_DP_DP_HPD_CTRL, DP_DP_HPD_CTRL_HPD_EN);
}

void msm_dp_catalog_ctrl_hpd_disable(struct msm_dp_catalog *msm_dp_catalog)
{
	struct msm_dp_catalog_private *catalog = container_of(msm_dp_catalog,
				struct msm_dp_catalog_private, msm_dp_catalog);

	u32 reftimer = msm_dp_read_aux(catalog, REG_DP_DP_HPD_REFTIMER);

	reftimer &= ~DP_DP_HPD_REFTIMER_ENABLE;
	msm_dp_write_aux(catalog, REG_DP_DP_HPD_REFTIMER, reftimer);

	msm_dp_write_aux(catalog, REG_DP_DP_HPD_CTRL, 0);
}

static void msm_dp_catalog_enable_sdp(struct msm_dp_catalog_private *catalog)
{
	/* trigger sdp */
	msm_dp_write_link(catalog, MMSS_DP_SDP_CFG3, UPDATE_SDP);
	msm_dp_write_link(catalog, MMSS_DP_SDP_CFG3, 0x0);
}

void msm_dp_catalog_ctrl_config_psr(struct msm_dp_catalog *msm_dp_catalog)
{
	struct msm_dp_catalog_private *catalog = container_of(msm_dp_catalog,
				struct msm_dp_catalog_private, msm_dp_catalog);
	u32 config;

	/* enable PSR1 function */
	config = msm_dp_read_link(catalog, REG_PSR_CONFIG);
	config |= PSR1_SUPPORTED;
	msm_dp_write_link(catalog, REG_PSR_CONFIG, config);

	msm_dp_write_ahb(catalog, REG_DP_INTR_MASK4, DP_INTERRUPT_MASK4);
	msm_dp_catalog_enable_sdp(catalog);
}

void msm_dp_catalog_ctrl_set_psr(struct msm_dp_catalog *msm_dp_catalog, bool enter)
{
	struct msm_dp_catalog_private *catalog = container_of(msm_dp_catalog,
			struct msm_dp_catalog_private, msm_dp_catalog);
	u32 cmd;

	cmd = msm_dp_read_link(catalog, REG_PSR_CMD);

	cmd &= ~(PSR_ENTER | PSR_EXIT);

	if (enter)
		cmd |= PSR_ENTER;
	else
		cmd |= PSR_EXIT;

	msm_dp_catalog_enable_sdp(catalog);
	msm_dp_write_link(catalog, REG_PSR_CMD, cmd);
}

u32 msm_dp_catalog_link_is_connected(struct msm_dp_catalog *msm_dp_catalog)
{
	struct msm_dp_catalog_private *catalog = container_of(msm_dp_catalog,
				struct msm_dp_catalog_private, msm_dp_catalog);
	u32 status;

	status = msm_dp_read_aux(catalog, REG_DP_DP_HPD_INT_STATUS);
	drm_dbg_dp(catalog->drm_dev, "aux status: %#x\n", status);
	status >>= DP_DP_HPD_STATE_STATUS_BITS_SHIFT;
	status &= DP_DP_HPD_STATE_STATUS_BITS_MASK;

	return status;
}

u32 msm_dp_catalog_hpd_get_intr_status(struct msm_dp_catalog *msm_dp_catalog)
{
	struct msm_dp_catalog_private *catalog = container_of(msm_dp_catalog,
				struct msm_dp_catalog_private, msm_dp_catalog);
	int isr, mask;

	isr = msm_dp_read_aux(catalog, REG_DP_DP_HPD_INT_STATUS);
	msm_dp_write_aux(catalog, REG_DP_DP_HPD_INT_ACK,
				 (isr & DP_DP_HPD_INT_MASK));
	mask = msm_dp_read_aux(catalog, REG_DP_DP_HPD_INT_MASK);

	/*
	 * We only want to return interrupts that are unmasked to the caller.
	 * However, the interrupt status field also contains other
	 * informational bits about the HPD state status, so we only mask
	 * out the part of the register that tells us about which interrupts
	 * are pending.
	 */
	return isr & (mask | ~DP_DP_HPD_INT_MASK);
}

u32 msm_dp_catalog_ctrl_read_psr_interrupt_status(struct msm_dp_catalog *msm_dp_catalog)
{
	struct msm_dp_catalog_private *catalog = container_of(msm_dp_catalog,
				struct msm_dp_catalog_private, msm_dp_catalog);
	u32 intr, intr_ack;

	intr = msm_dp_read_ahb(catalog, REG_DP_INTR_STATUS4);
	intr_ack = (intr & DP_INTERRUPT_STATUS4)
			<< DP_INTERRUPT_STATUS_ACK_SHIFT;
	msm_dp_write_ahb(catalog, REG_DP_INTR_STATUS4, intr_ack);

	return intr;
}

int msm_dp_catalog_ctrl_get_interrupt(struct msm_dp_catalog *msm_dp_catalog)
{
	struct msm_dp_catalog_private *catalog = container_of(msm_dp_catalog,
				struct msm_dp_catalog_private, msm_dp_catalog);
	u32 intr, intr_ack;

	intr = msm_dp_read_ahb(catalog, REG_DP_INTR_STATUS2);
	intr &= ~DP_INTERRUPT_STATUS2_MASK;
	intr_ack = (intr & DP_INTERRUPT_STATUS2)
			<< DP_INTERRUPT_STATUS_ACK_SHIFT;
	msm_dp_write_ahb(catalog, REG_DP_INTR_STATUS2,
			intr_ack | DP_INTERRUPT_STATUS2_MASK);

	return intr;
}

void msm_dp_catalog_ctrl_phy_reset(struct msm_dp_catalog *msm_dp_catalog)
{
	struct msm_dp_catalog_private *catalog = container_of(msm_dp_catalog,
				struct msm_dp_catalog_private, msm_dp_catalog);

	msm_dp_write_ahb(catalog, REG_DP_PHY_CTRL,
			DP_PHY_CTRL_SW_RESET | DP_PHY_CTRL_SW_RESET_PLL);
	usleep_range(1000, 1100); /* h/w recommended delay */
	msm_dp_write_ahb(catalog, REG_DP_PHY_CTRL, 0x0);
}

void msm_dp_catalog_ctrl_send_phy_pattern(struct msm_dp_catalog *msm_dp_catalog,
			u32 pattern)
{
	struct msm_dp_catalog_private *catalog = container_of(msm_dp_catalog,
				struct msm_dp_catalog_private, msm_dp_catalog);
	u32 value = 0x0;

	/* Make sure to clear the current pattern before starting a new one */
	msm_dp_write_link(catalog, REG_DP_STATE_CTRL, 0x0);

	drm_dbg_dp(catalog->drm_dev, "pattern: %#x\n", pattern);
	switch (pattern) {
	case DP_PHY_TEST_PATTERN_D10_2:
		msm_dp_write_link(catalog, REG_DP_STATE_CTRL,
				DP_STATE_CTRL_LINK_TRAINING_PATTERN1);
		break;
	case DP_PHY_TEST_PATTERN_ERROR_COUNT:
		value &= ~(1 << 16);
		msm_dp_write_link(catalog, REG_DP_HBR2_COMPLIANCE_SCRAMBLER_RESET,
					value);
		value |= SCRAMBLER_RESET_COUNT_VALUE;
		msm_dp_write_link(catalog, REG_DP_HBR2_COMPLIANCE_SCRAMBLER_RESET,
					value);
		msm_dp_write_link(catalog, REG_DP_MAINLINK_LEVELS,
					DP_MAINLINK_SAFE_TO_EXIT_LEVEL_2);
		msm_dp_write_link(catalog, REG_DP_STATE_CTRL,
					DP_STATE_CTRL_LINK_SYMBOL_ERR_MEASURE);
		break;
	case DP_PHY_TEST_PATTERN_PRBS7:
		msm_dp_write_link(catalog, REG_DP_STATE_CTRL,
				DP_STATE_CTRL_LINK_PRBS7);
		break;
	case DP_PHY_TEST_PATTERN_80BIT_CUSTOM:
		msm_dp_write_link(catalog, REG_DP_STATE_CTRL,
				DP_STATE_CTRL_LINK_TEST_CUSTOM_PATTERN);
		/* 00111110000011111000001111100000 */
		msm_dp_write_link(catalog, REG_DP_TEST_80BIT_CUSTOM_PATTERN_REG0,
				0x3E0F83E0);
		/* 00001111100000111110000011111000 */
		msm_dp_write_link(catalog, REG_DP_TEST_80BIT_CUSTOM_PATTERN_REG1,
				0x0F83E0F8);
		/* 1111100000111110 */
		msm_dp_write_link(catalog, REG_DP_TEST_80BIT_CUSTOM_PATTERN_REG2,
				0x0000F83E);
		break;
	case DP_PHY_TEST_PATTERN_CP2520:
		value = msm_dp_read_link(catalog, REG_DP_MAINLINK_CTRL);
		value &= ~DP_MAINLINK_CTRL_SW_BYPASS_SCRAMBLER;
		msm_dp_write_link(catalog, REG_DP_MAINLINK_CTRL, value);

		value = DP_HBR2_ERM_PATTERN;
		msm_dp_write_link(catalog, REG_DP_HBR2_COMPLIANCE_SCRAMBLER_RESET,
				value);
		value |= SCRAMBLER_RESET_COUNT_VALUE;
		msm_dp_write_link(catalog, REG_DP_HBR2_COMPLIANCE_SCRAMBLER_RESET,
					value);
		msm_dp_write_link(catalog, REG_DP_MAINLINK_LEVELS,
					DP_MAINLINK_SAFE_TO_EXIT_LEVEL_2);
		msm_dp_write_link(catalog, REG_DP_STATE_CTRL,
					DP_STATE_CTRL_LINK_SYMBOL_ERR_MEASURE);
		value = msm_dp_read_link(catalog, REG_DP_MAINLINK_CTRL);
		value |= DP_MAINLINK_CTRL_ENABLE;
		msm_dp_write_link(catalog, REG_DP_MAINLINK_CTRL, value);
		break;
	case DP_PHY_TEST_PATTERN_SEL_MASK:
		msm_dp_write_link(catalog, REG_DP_MAINLINK_CTRL,
				DP_MAINLINK_CTRL_ENABLE);
		msm_dp_write_link(catalog, REG_DP_STATE_CTRL,
				DP_STATE_CTRL_LINK_TRAINING_PATTERN4);
		break;
	default:
		drm_dbg_dp(catalog->drm_dev,
				"No valid test pattern requested: %#x\n", pattern);
		break;
	}
}

u32 msm_dp_catalog_ctrl_read_phy_pattern(struct msm_dp_catalog *msm_dp_catalog)
{
	struct msm_dp_catalog_private *catalog = container_of(msm_dp_catalog,
				struct msm_dp_catalog_private, msm_dp_catalog);

	return msm_dp_read_link(catalog, REG_DP_MAINLINK_READY);
}

/* panel related catalog functions */
int msm_dp_catalog_panel_timing_cfg(struct msm_dp_catalog *msm_dp_catalog, u32 total,
				u32 sync_start, u32 width_blanking, u32 msm_dp_active)
{
	struct msm_dp_catalog_private *catalog = container_of(msm_dp_catalog,
				struct msm_dp_catalog_private, msm_dp_catalog);
	u32 reg;

	msm_dp_write_link(catalog, REG_DP_TOTAL_HOR_VER, total);
	msm_dp_write_link(catalog, REG_DP_START_HOR_VER_FROM_SYNC, sync_start);
	msm_dp_write_link(catalog, REG_DP_HSYNC_VSYNC_WIDTH_POLARITY, width_blanking);
	msm_dp_write_link(catalog, REG_DP_ACTIVE_HOR_VER, msm_dp_active);

	reg = msm_dp_read_p0(catalog, MMSS_DP_INTF_CONFIG);

	if (msm_dp_catalog->wide_bus_en)
		reg |= DP_INTF_CONFIG_DATABUS_WIDEN;
	else
		reg &= ~DP_INTF_CONFIG_DATABUS_WIDEN;


	DRM_DEBUG_DP("wide_bus_en=%d reg=%#x\n", msm_dp_catalog->wide_bus_en, reg);

	msm_dp_write_p0(catalog, MMSS_DP_INTF_CONFIG, reg);
	return 0;
}

static void msm_dp_catalog_panel_send_vsc_sdp(struct msm_dp_catalog *msm_dp_catalog, struct dp_sdp *vsc_sdp)
{
	struct msm_dp_catalog_private *catalog;
	u32 header[2];
	u32 val;
	int i;

	catalog = container_of(msm_dp_catalog, struct msm_dp_catalog_private, msm_dp_catalog);

	msm_dp_utils_pack_sdp_header(&vsc_sdp->sdp_header, header);

	msm_dp_write_link(catalog, MMSS_DP_GENERIC0_0, header[0]);
	msm_dp_write_link(catalog, MMSS_DP_GENERIC0_1, header[1]);

	for (i = 0; i < sizeof(vsc_sdp->db); i += 4) {
		val = ((vsc_sdp->db[i]) | (vsc_sdp->db[i + 1] << 8) | (vsc_sdp->db[i + 2] << 16) |
		       (vsc_sdp->db[i + 3] << 24));
		msm_dp_write_link(catalog, MMSS_DP_GENERIC0_2 + i, val);
	}
}

static void msm_dp_catalog_panel_update_sdp(struct msm_dp_catalog *msm_dp_catalog)
{
	struct msm_dp_catalog_private *catalog;
	u32 hw_revision;

	catalog = container_of(msm_dp_catalog, struct msm_dp_catalog_private, msm_dp_catalog);

	hw_revision = msm_dp_catalog_hw_revision(msm_dp_catalog);
	if (hw_revision < DP_HW_VERSION_1_2 && hw_revision >= DP_HW_VERSION_1_0) {
		msm_dp_write_link(catalog, MMSS_DP_SDP_CFG3, 0x01);
		msm_dp_write_link(catalog, MMSS_DP_SDP_CFG3, 0x00);
	}
}

void msm_dp_catalog_panel_enable_vsc_sdp(struct msm_dp_catalog *msm_dp_catalog, struct dp_sdp *vsc_sdp)
{
	struct msm_dp_catalog_private *catalog;
	u32 cfg, cfg2, misc;

	catalog = container_of(msm_dp_catalog, struct msm_dp_catalog_private, msm_dp_catalog);

	cfg = msm_dp_read_link(catalog, MMSS_DP_SDP_CFG);
	cfg2 = msm_dp_read_link(catalog, MMSS_DP_SDP_CFG2);
	misc = msm_dp_read_link(catalog, REG_DP_MISC1_MISC0);

	cfg |= GEN0_SDP_EN;
	msm_dp_write_link(catalog, MMSS_DP_SDP_CFG, cfg);

	cfg2 |= GENERIC0_SDPSIZE_VALID;
	msm_dp_write_link(catalog, MMSS_DP_SDP_CFG2, cfg2);

	msm_dp_catalog_panel_send_vsc_sdp(msm_dp_catalog, vsc_sdp);

	/* indicates presence of VSC (BIT(6) of MISC1) */
	misc |= DP_MISC1_VSC_SDP;

	drm_dbg_dp(catalog->drm_dev, "vsc sdp enable=1\n");

	pr_debug("misc settings = 0x%x\n", misc);
	msm_dp_write_link(catalog, REG_DP_MISC1_MISC0, misc);

	msm_dp_catalog_panel_update_sdp(msm_dp_catalog);
}

void msm_dp_catalog_panel_disable_vsc_sdp(struct msm_dp_catalog *msm_dp_catalog)
{
	struct msm_dp_catalog_private *catalog;
	u32 cfg, cfg2, misc;

	catalog = container_of(msm_dp_catalog, struct msm_dp_catalog_private, msm_dp_catalog);

	cfg = msm_dp_read_link(catalog, MMSS_DP_SDP_CFG);
	cfg2 = msm_dp_read_link(catalog, MMSS_DP_SDP_CFG2);
	misc = msm_dp_read_link(catalog, REG_DP_MISC1_MISC0);

	cfg &= ~GEN0_SDP_EN;
	msm_dp_write_link(catalog, MMSS_DP_SDP_CFG, cfg);

	cfg2 &= ~GENERIC0_SDPSIZE_VALID;
	msm_dp_write_link(catalog, MMSS_DP_SDP_CFG2, cfg2);

	/* switch back to MSA */
	misc &= ~DP_MISC1_VSC_SDP;

	drm_dbg_dp(catalog->drm_dev, "vsc sdp enable=0\n");

	pr_debug("misc settings = 0x%x\n", misc);
	msm_dp_write_link(catalog, REG_DP_MISC1_MISC0, misc);

	msm_dp_catalog_panel_update_sdp(msm_dp_catalog);
}

void msm_dp_catalog_panel_tpg_enable(struct msm_dp_catalog *msm_dp_catalog,
				struct drm_display_mode *drm_mode)
{
	struct msm_dp_catalog_private *catalog = container_of(msm_dp_catalog,
				struct msm_dp_catalog_private, msm_dp_catalog);
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


	msm_dp_write_p0(catalog, MMSS_DP_INTF_CONFIG, 0x0);
	msm_dp_write_p0(catalog, MMSS_DP_INTF_HSYNC_CTL, hsync_ctl);
	msm_dp_write_p0(catalog, MMSS_DP_INTF_VSYNC_PERIOD_F0, vsync_period *
			hsync_period);
	msm_dp_write_p0(catalog, MMSS_DP_INTF_VSYNC_PULSE_WIDTH_F0, v_sync_width *
			hsync_period);
	msm_dp_write_p0(catalog, MMSS_DP_INTF_VSYNC_PERIOD_F1, 0);
	msm_dp_write_p0(catalog, MMSS_DP_INTF_VSYNC_PULSE_WIDTH_F1, 0);
	msm_dp_write_p0(catalog, MMSS_DP_INTF_DISPLAY_HCTL, display_hctl);
	msm_dp_write_p0(catalog, MMSS_DP_INTF_ACTIVE_HCTL, 0);
	msm_dp_write_p0(catalog, MMSS_INTF_DISPLAY_V_START_F0, display_v_start);
	msm_dp_write_p0(catalog, MMSS_DP_INTF_DISPLAY_V_END_F0, display_v_end);
	msm_dp_write_p0(catalog, MMSS_INTF_DISPLAY_V_START_F1, 0);
	msm_dp_write_p0(catalog, MMSS_DP_INTF_DISPLAY_V_END_F1, 0);
	msm_dp_write_p0(catalog, MMSS_DP_INTF_ACTIVE_V_START_F0, 0);
	msm_dp_write_p0(catalog, MMSS_DP_INTF_ACTIVE_V_END_F0, 0);
	msm_dp_write_p0(catalog, MMSS_DP_INTF_ACTIVE_V_START_F1, 0);
	msm_dp_write_p0(catalog, MMSS_DP_INTF_ACTIVE_V_END_F1, 0);
	msm_dp_write_p0(catalog, MMSS_DP_INTF_POLARITY_CTL, 0);

	msm_dp_write_p0(catalog, MMSS_DP_TPG_MAIN_CONTROL,
				DP_TPG_CHECKERED_RECT_PATTERN);
	msm_dp_write_p0(catalog, MMSS_DP_TPG_VIDEO_CONFIG,
				DP_TPG_VIDEO_CONFIG_BPP_8BIT |
				DP_TPG_VIDEO_CONFIG_RGB);
	msm_dp_write_p0(catalog, MMSS_DP_BIST_ENABLE,
				DP_BIST_ENABLE_DPBIST_EN);
	msm_dp_write_p0(catalog, MMSS_DP_TIMING_ENGINE_EN,
				DP_TIMING_ENGINE_EN_EN);
	drm_dbg_dp(catalog->drm_dev, "%s: enabled tpg\n", __func__);
}

void msm_dp_catalog_panel_tpg_disable(struct msm_dp_catalog *msm_dp_catalog)
{
	struct msm_dp_catalog_private *catalog = container_of(msm_dp_catalog,
				struct msm_dp_catalog_private, msm_dp_catalog);

	msm_dp_write_p0(catalog, MMSS_DP_TPG_MAIN_CONTROL, 0x0);
	msm_dp_write_p0(catalog, MMSS_DP_BIST_ENABLE, 0x0);
	msm_dp_write_p0(catalog, MMSS_DP_TIMING_ENGINE_EN, 0x0);
}

static void __iomem *msm_dp_ioremap(struct platform_device *pdev, int idx, size_t *len)
{
	struct resource *res;
	void __iomem *base;

	base = devm_platform_get_and_ioremap_resource(pdev, idx, &res);
	if (!IS_ERR(base))
		*len = resource_size(res);

	return base;
}

static int msm_dp_catalog_get_io(struct msm_dp_catalog_private *catalog)
{
	struct platform_device *pdev = to_platform_device(catalog->dev);
	struct dss_io_data *dss = &catalog->io;

	dss->ahb.base = msm_dp_ioremap(pdev, 0, &dss->ahb.len);
	if (IS_ERR(dss->ahb.base))
		return PTR_ERR(dss->ahb.base);

	dss->aux.base = msm_dp_ioremap(pdev, 1, &dss->aux.len);
	if (IS_ERR(dss->aux.base)) {
		/*
		 * The initial binding had a single reg, but in order to
		 * support variation in the sub-region sizes this was split.
		 * msm_dp_ioremap() will fail with -EINVAL here if only a single
		 * reg is specified, so fill in the sub-region offsets and
		 * lengths based on this single region.
		 */
		if (PTR_ERR(dss->aux.base) == -EINVAL) {
			if (dss->ahb.len < DP_DEFAULT_P0_OFFSET + DP_DEFAULT_P0_SIZE) {
				DRM_ERROR("legacy memory region not large enough\n");
				return -EINVAL;
			}

			dss->ahb.len = DP_DEFAULT_AHB_SIZE;
			dss->aux.base = dss->ahb.base + DP_DEFAULT_AUX_OFFSET;
			dss->aux.len = DP_DEFAULT_AUX_SIZE;
			dss->link.base = dss->ahb.base + DP_DEFAULT_LINK_OFFSET;
			dss->link.len = DP_DEFAULT_LINK_SIZE;
			dss->p0.base = dss->ahb.base + DP_DEFAULT_P0_OFFSET;
			dss->p0.len = DP_DEFAULT_P0_SIZE;
		} else {
			DRM_ERROR("unable to remap aux region: %pe\n", dss->aux.base);
			return PTR_ERR(dss->aux.base);
		}
	} else {
		dss->link.base = msm_dp_ioremap(pdev, 2, &dss->link.len);
		if (IS_ERR(dss->link.base)) {
			DRM_ERROR("unable to remap link region: %pe\n", dss->link.base);
			return PTR_ERR(dss->link.base);
		}

		dss->p0.base = msm_dp_ioremap(pdev, 3, &dss->p0.len);
		if (IS_ERR(dss->p0.base)) {
			DRM_ERROR("unable to remap p0 region: %pe\n", dss->p0.base);
			return PTR_ERR(dss->p0.base);
		}
	}

	return 0;
}

struct msm_dp_catalog *msm_dp_catalog_get(struct device *dev)
{
	struct msm_dp_catalog_private *catalog;
	int ret;

	catalog  = devm_kzalloc(dev, sizeof(*catalog), GFP_KERNEL);
	if (!catalog)
		return ERR_PTR(-ENOMEM);

	catalog->dev = dev;

	ret = msm_dp_catalog_get_io(catalog);
	if (ret)
		return ERR_PTR(ret);

	return &catalog->msm_dp_catalog;
}

u32 msm_dp_catalog_audio_get_header(struct msm_dp_catalog *msm_dp_catalog,
				enum msm_dp_catalog_audio_sdp_type sdp,
				enum msm_dp_catalog_audio_header_type header)
{
	struct msm_dp_catalog_private *catalog;
	u32 (*sdp_map)[DP_AUDIO_SDP_HEADER_MAX];

	catalog = container_of(msm_dp_catalog,
		struct msm_dp_catalog_private, msm_dp_catalog);

	sdp_map = catalog->audio_map;

	return msm_dp_read_link(catalog, sdp_map[sdp][header]);
}

void msm_dp_catalog_audio_set_header(struct msm_dp_catalog *msm_dp_catalog,
				 enum msm_dp_catalog_audio_sdp_type sdp,
				 enum msm_dp_catalog_audio_header_type header,
				 u32 data)
{
	struct msm_dp_catalog_private *catalog;
	u32 (*sdp_map)[DP_AUDIO_SDP_HEADER_MAX];

	if (!msm_dp_catalog)
		return;

	catalog = container_of(msm_dp_catalog,
		struct msm_dp_catalog_private, msm_dp_catalog);

	sdp_map = catalog->audio_map;

	msm_dp_write_link(catalog, sdp_map[sdp][header], data);
}

void msm_dp_catalog_audio_config_acr(struct msm_dp_catalog *msm_dp_catalog, u32 select)
{
	struct msm_dp_catalog_private *catalog;
	u32 acr_ctrl;

	if (!msm_dp_catalog)
		return;

	catalog = container_of(msm_dp_catalog,
		struct msm_dp_catalog_private, msm_dp_catalog);

	acr_ctrl = select << 4 | BIT(31) | BIT(8) | BIT(14);

	drm_dbg_dp(catalog->drm_dev, "select: %#x, acr_ctrl: %#x\n",
					select, acr_ctrl);

	msm_dp_write_link(catalog, MMSS_DP_AUDIO_ACR_CTRL, acr_ctrl);
}

void msm_dp_catalog_audio_enable(struct msm_dp_catalog *msm_dp_catalog, bool enable)
{
	struct msm_dp_catalog_private *catalog;
	u32 audio_ctrl;

	if (!msm_dp_catalog)
		return;

	catalog = container_of(msm_dp_catalog,
		struct msm_dp_catalog_private, msm_dp_catalog);

	audio_ctrl = msm_dp_read_link(catalog, MMSS_DP_AUDIO_CFG);

	if (enable)
		audio_ctrl |= BIT(0);
	else
		audio_ctrl &= ~BIT(0);

	drm_dbg_dp(catalog->drm_dev, "dp_audio_cfg = 0x%x\n", audio_ctrl);

	msm_dp_write_link(catalog, MMSS_DP_AUDIO_CFG, audio_ctrl);
	/* make sure audio engine is disabled */
	wmb();
}

void msm_dp_catalog_audio_config_sdp(struct msm_dp_catalog *msm_dp_catalog)
{
	struct msm_dp_catalog_private *catalog;
	u32 sdp_cfg = 0;
	u32 sdp_cfg2 = 0;

	if (!msm_dp_catalog)
		return;

	catalog = container_of(msm_dp_catalog,
		struct msm_dp_catalog_private, msm_dp_catalog);

	sdp_cfg = msm_dp_read_link(catalog, MMSS_DP_SDP_CFG);
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

	drm_dbg_dp(catalog->drm_dev, "sdp_cfg = 0x%x\n", sdp_cfg);

	msm_dp_write_link(catalog, MMSS_DP_SDP_CFG, sdp_cfg);

	sdp_cfg2 = msm_dp_read_link(catalog, MMSS_DP_SDP_CFG2);
	/* IFRM_REGSRC -> Do not use reg values */
	sdp_cfg2 &= ~BIT(0);
	/* AUDIO_STREAM_HB3_REGSRC-> Do not use reg values */
	sdp_cfg2 &= ~BIT(1);

	drm_dbg_dp(catalog->drm_dev, "sdp_cfg2 = 0x%x\n", sdp_cfg2);

	msm_dp_write_link(catalog, MMSS_DP_SDP_CFG2, sdp_cfg2);
}

void msm_dp_catalog_audio_init(struct msm_dp_catalog *msm_dp_catalog)
{
	struct msm_dp_catalog_private *catalog;

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

	if (!msm_dp_catalog)
		return;

	catalog = container_of(msm_dp_catalog,
		struct msm_dp_catalog_private, msm_dp_catalog);

	catalog->audio_map = sdp_map;
}

void msm_dp_catalog_audio_sfe_level(struct msm_dp_catalog *msm_dp_catalog, u32 safe_to_exit_level)
{
	struct msm_dp_catalog_private *catalog;
	u32 mainlink_levels;

	if (!msm_dp_catalog)
		return;

	catalog = container_of(msm_dp_catalog,
		struct msm_dp_catalog_private, msm_dp_catalog);

	mainlink_levels = msm_dp_read_link(catalog, REG_DP_MAINLINK_LEVELS);
	mainlink_levels &= 0xFE0;
	mainlink_levels |= safe_to_exit_level;

	drm_dbg_dp(catalog->drm_dev,
			"mainlink_level = 0x%x, safe_to_exit_level = 0x%x\n",
			 mainlink_levels, safe_to_exit_level);

	msm_dp_write_link(catalog, REG_DP_MAINLINK_LEVELS, mainlink_levels);
}
