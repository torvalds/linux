// SPDX-License-Identifier: GPL-2.0
/*
 * R-Car MIPI DSI Encoder
 *
 * Copyright (C) 2020 Renesas Electronics Corporation
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/slab.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

#include "rcar_mipi_dsi_regs.h"

struct rcar_mipi_dsi {
	struct device *dev;
	const struct rcar_mipi_dsi_device_info *info;
	struct reset_control *rstc;

	struct mipi_dsi_host host;
	struct drm_bridge bridge;
	struct drm_bridge *next_bridge;
	struct drm_connector connector;

	void __iomem *mmio;
	struct {
		struct clk *mod;
		struct clk *pll;
		struct clk *dsi;
	} clocks;

	enum mipi_dsi_pixel_format format;
	unsigned int num_data_lanes;
	unsigned int lanes;
};

static inline struct rcar_mipi_dsi *
bridge_to_rcar_mipi_dsi(struct drm_bridge *bridge)
{
	return container_of(bridge, struct rcar_mipi_dsi, bridge);
}

static inline struct rcar_mipi_dsi *
host_to_rcar_mipi_dsi(struct mipi_dsi_host *host)
{
	return container_of(host, struct rcar_mipi_dsi, host);
}

static const u32 phtw[] = {
	0x01020114, 0x01600115, /* General testing */
	0x01030116, 0x0102011d, /* General testing */
	0x011101a4, 0x018601a4, /* 1Gbps testing */
	0x014201a0, 0x010001a3, /* 1Gbps testing */
	0x0101011f,		/* 1Gbps testing */
};

static const u32 phtw2[] = {
	0x010c0130, 0x010c0140, /* General testing */
	0x010c0150, 0x010c0180, /* General testing */
	0x010c0190,
	0x010a0160, 0x010a0170,
	0x01800164, 0x01800174,	/* 1Gbps testing */
};

static const u32 hsfreqrange_table[][2] = {
	{ 80000000U,   0x00 }, { 90000000U,   0x10 }, { 100000000U,  0x20 },
	{ 110000000U,  0x30 }, { 120000000U,  0x01 }, { 130000000U,  0x11 },
	{ 140000000U,  0x21 }, { 150000000U,  0x31 }, { 160000000U,  0x02 },
	{ 170000000U,  0x12 }, { 180000000U,  0x22 }, { 190000000U,  0x32 },
	{ 205000000U,  0x03 }, { 220000000U,  0x13 }, { 235000000U,  0x23 },
	{ 250000000U,  0x33 }, { 275000000U,  0x04 }, { 300000000U,  0x14 },
	{ 325000000U,  0x25 }, { 350000000U,  0x35 }, { 400000000U,  0x05 },
	{ 450000000U,  0x16 }, { 500000000U,  0x26 }, { 550000000U,  0x37 },
	{ 600000000U,  0x07 }, { 650000000U,  0x18 }, { 700000000U,  0x28 },
	{ 750000000U,  0x39 }, { 800000000U,  0x09 }, { 850000000U,  0x19 },
	{ 900000000U,  0x29 }, { 950000000U,  0x3a }, { 1000000000U, 0x0a },
	{ 1050000000U, 0x1a }, { 1100000000U, 0x2a }, { 1150000000U, 0x3b },
	{ 1200000000U, 0x0b }, { 1250000000U, 0x1b }, { 1300000000U, 0x2b },
	{ 1350000000U, 0x3c }, { 1400000000U, 0x0c }, { 1450000000U, 0x1c },
	{ 1500000000U, 0x2c }, { 1550000000U, 0x3d }, { 1600000000U, 0x0d },
	{ 1650000000U, 0x1d }, { 1700000000U, 0x2e }, { 1750000000U, 0x3e },
	{ 1800000000U, 0x0e }, { 1850000000U, 0x1e }, { 1900000000U, 0x2f },
	{ 1950000000U, 0x3f }, { 2000000000U, 0x0f }, { 2050000000U, 0x40 },
	{ 2100000000U, 0x41 }, { 2150000000U, 0x42 }, { 2200000000U, 0x43 },
	{ 2250000000U, 0x44 }, { 2300000000U, 0x45 }, { 2350000000U, 0x46 },
	{ 2400000000U, 0x47 }, { 2450000000U, 0x48 }, { 2500000000U, 0x49 },
	{ /* sentinel */ },
};

struct vco_cntrl_value {
	u32 min_freq;
	u32 max_freq;
	u16 value;
};

static const struct vco_cntrl_value vco_cntrl_table[] = {
	{ .min_freq = 40000000U,   .max_freq = 55000000U,   .value = 0x3f },
	{ .min_freq = 52500000U,   .max_freq = 80000000U,   .value = 0x39 },
	{ .min_freq = 80000000U,   .max_freq = 110000000U,  .value = 0x2f },
	{ .min_freq = 105000000U,  .max_freq = 160000000U,  .value = 0x29 },
	{ .min_freq = 160000000U,  .max_freq = 220000000U,  .value = 0x1f },
	{ .min_freq = 210000000U,  .max_freq = 320000000U,  .value = 0x19 },
	{ .min_freq = 320000000U,  .max_freq = 440000000U,  .value = 0x0f },
	{ .min_freq = 420000000U,  .max_freq = 660000000U,  .value = 0x09 },
	{ .min_freq = 630000000U,  .max_freq = 1149000000U, .value = 0x03 },
	{ .min_freq = 1100000000U, .max_freq = 1152000000U, .value = 0x01 },
	{ .min_freq = 1150000000U, .max_freq = 1250000000U, .value = 0x01 },
	{ /* sentinel */ },
};

static void rcar_mipi_dsi_write(struct rcar_mipi_dsi *dsi, u32 reg, u32 data)
{
	iowrite32(data, dsi->mmio + reg);
}

static u32 rcar_mipi_dsi_read(struct rcar_mipi_dsi *dsi, u32 reg)
{
	return ioread32(dsi->mmio + reg);
}

static void rcar_mipi_dsi_clr(struct rcar_mipi_dsi *dsi, u32 reg, u32 clr)
{
	rcar_mipi_dsi_write(dsi, reg, rcar_mipi_dsi_read(dsi, reg) & ~clr);
}

static void rcar_mipi_dsi_set(struct rcar_mipi_dsi *dsi, u32 reg, u32 set)
{
	rcar_mipi_dsi_write(dsi, reg, rcar_mipi_dsi_read(dsi, reg) | set);
}

static int rcar_mipi_dsi_phtw_test(struct rcar_mipi_dsi *dsi, u32 phtw)
{
	u32 status;
	int ret;

	rcar_mipi_dsi_write(dsi, PHTW, phtw);

	ret = read_poll_timeout(rcar_mipi_dsi_read, status,
				!(status & (PHTW_DWEN | PHTW_CWEN)),
				2000, 10000, false, dsi, PHTW);
	if (ret < 0) {
		dev_err(dsi->dev, "PHY test interface write timeout (0x%08x)\n",
			phtw);
		return ret;
	}

	return ret;
}

/* -----------------------------------------------------------------------------
 * Hardware Setup
 */

struct dsi_setup_info {
	unsigned long fout;
	u16 vco_cntrl;
	u16 prop_cntrl;
	u16 hsfreqrange;
	u16 div;
	unsigned int m;
	unsigned int n;
};

static void rcar_mipi_dsi_parameters_calc(struct rcar_mipi_dsi *dsi,
					  struct clk *clk, unsigned long target,
					  struct dsi_setup_info *setup_info)
{

	const struct vco_cntrl_value *vco_cntrl;
	unsigned long fout_target;
	unsigned long fin, fout;
	unsigned long hsfreq;
	unsigned int best_err = -1;
	unsigned int divider;
	unsigned int n;
	unsigned int i;
	unsigned int err;

	/*
	 * Calculate Fout = dot clock * ColorDepth / (2 * Lane Count)
	 * The range out Fout is [40 - 1250] Mhz
	 */
	fout_target = target * mipi_dsi_pixel_format_to_bpp(dsi->format)
		    / (2 * dsi->lanes);
	if (fout_target < 40000000 || fout_target > 1250000000)
		return;

	/* Find vco_cntrl */
	for (vco_cntrl = vco_cntrl_table; vco_cntrl->min_freq != 0; vco_cntrl++) {
		if (fout_target > vco_cntrl->min_freq &&
		    fout_target <= vco_cntrl->max_freq) {
			setup_info->vco_cntrl = vco_cntrl->value;
			if (fout_target >= 1150000000)
				setup_info->prop_cntrl = 0x0c;
			else
				setup_info->prop_cntrl = 0x0b;
			break;
		}
	}

	/* Add divider */
	setup_info->div = (setup_info->vco_cntrl & 0x30) >> 4;

	/* Find hsfreqrange */
	hsfreq = fout_target * 2;
	for (i = 0; i < ARRAY_SIZE(hsfreqrange_table); i++) {
		if (hsfreqrange_table[i][0] >= hsfreq) {
			setup_info->hsfreqrange = hsfreqrange_table[i][1];
			break;
		}
	}

	/*
	 * Calculate n and m for PLL clock
	 * Following the HW manual the ranges of n and m are
	 * n = [3-8] and m = [64-625]
	 */
	fin = clk_get_rate(clk);
	divider = 1 << setup_info->div;
	for (n = 3; n < 9; n++) {
		unsigned long fpfd;
		unsigned int m;

		fpfd = fin / n;

		for (m = 64; m < 626; m++) {
			fout = fpfd * m / divider;
			err = abs((long)(fout - fout_target) * 10000 /
				  (long)fout_target);
			if (err < best_err) {
				setup_info->m = m - 2;
				setup_info->n = n - 1;
				setup_info->fout = fout;
				best_err = err;
				if (err == 0)
					goto done;
			}
		}
	}

done:
	dev_dbg(dsi->dev,
		"%pC %lu Hz -> Fout %lu Hz (target %lu Hz, error %d.%02u%%), PLL M/N/DIV %u/%u/%u\n",
		clk, fin, setup_info->fout, fout_target, best_err / 100,
		best_err % 100, setup_info->m, setup_info->n, setup_info->div);
	dev_dbg(dsi->dev,
		"vco_cntrl = 0x%x\tprop_cntrl = 0x%x\thsfreqrange = 0x%x\n",
		setup_info->vco_cntrl, setup_info->prop_cntrl,
		setup_info->hsfreqrange);
}

static void rcar_mipi_dsi_set_display_timing(struct rcar_mipi_dsi *dsi,
					     const struct drm_display_mode *mode)
{
	u32 setr;
	u32 vprmset0r;
	u32 vprmset1r;
	u32 vprmset2r;
	u32 vprmset3r;
	u32 vprmset4r;

	/* Configuration for Pixel Stream and Packet Header */
	if (mipi_dsi_pixel_format_to_bpp(dsi->format) == 24)
		rcar_mipi_dsi_write(dsi, TXVMPSPHSETR, TXVMPSPHSETR_DT_RGB24);
	else if (mipi_dsi_pixel_format_to_bpp(dsi->format) == 18)
		rcar_mipi_dsi_write(dsi, TXVMPSPHSETR, TXVMPSPHSETR_DT_RGB18);
	else if (mipi_dsi_pixel_format_to_bpp(dsi->format) == 16)
		rcar_mipi_dsi_write(dsi, TXVMPSPHSETR, TXVMPSPHSETR_DT_RGB16);
	else {
		dev_warn(dsi->dev, "unsupported format");
		return;
	}

	/* Configuration for Blanking sequence and Input Pixel */
	setr = TXVMSETR_HSABPEN_EN | TXVMSETR_HBPBPEN_EN
	     | TXVMSETR_HFPBPEN_EN | TXVMSETR_SYNSEQ_PULSES
	     | TXVMSETR_PIXWDTH | TXVMSETR_VSTPM;
	rcar_mipi_dsi_write(dsi, TXVMSETR, setr);

	/* Configuration for Video Parameters */
	vprmset0r = (mode->flags & DRM_MODE_FLAG_PVSYNC ?
		     TXVMVPRMSET0R_VSPOL_HIG : TXVMVPRMSET0R_VSPOL_LOW)
		  | (mode->flags & DRM_MODE_FLAG_PHSYNC ?
		     TXVMVPRMSET0R_HSPOL_HIG : TXVMVPRMSET0R_HSPOL_LOW)
		  | TXVMVPRMSET0R_CSPC_RGB | TXVMVPRMSET0R_BPP_24;

	vprmset1r = TXVMVPRMSET1R_VACTIVE(mode->vdisplay)
		  | TXVMVPRMSET1R_VSA(mode->vsync_end - mode->vsync_start);

	vprmset2r = TXVMVPRMSET2R_VFP(mode->vsync_start - mode->vdisplay)
		  | TXVMVPRMSET2R_VBP(mode->vtotal - mode->vsync_end);

	vprmset3r = TXVMVPRMSET3R_HACTIVE(mode->hdisplay)
		  | TXVMVPRMSET3R_HSA(mode->hsync_end - mode->hsync_start);

	vprmset4r = TXVMVPRMSET4R_HFP(mode->hsync_start - mode->hdisplay)
		  | TXVMVPRMSET4R_HBP(mode->htotal - mode->hsync_end);

	rcar_mipi_dsi_write(dsi, TXVMVPRMSET0R, vprmset0r);
	rcar_mipi_dsi_write(dsi, TXVMVPRMSET1R, vprmset1r);
	rcar_mipi_dsi_write(dsi, TXVMVPRMSET2R, vprmset2r);
	rcar_mipi_dsi_write(dsi, TXVMVPRMSET3R, vprmset3r);
	rcar_mipi_dsi_write(dsi, TXVMVPRMSET4R, vprmset4r);
}

static int rcar_mipi_dsi_startup(struct rcar_mipi_dsi *dsi,
				 const struct drm_display_mode *mode)
{
	struct dsi_setup_info setup_info = {};
	unsigned int timeout;
	int ret, i;
	int dsi_format;
	u32 phy_setup;
	u32 clockset2, clockset3;
	u32 ppisetr;
	u32 vclkset;

	/* Checking valid format */
	dsi_format = mipi_dsi_pixel_format_to_bpp(dsi->format);
	if (dsi_format < 0) {
		dev_warn(dsi->dev, "invalid format");
		return -EINVAL;
	}

	/* Parameters Calculation */
	rcar_mipi_dsi_parameters_calc(dsi, dsi->clocks.pll,
				      mode->clock * 1000, &setup_info);

	/* LPCLK enable */
	rcar_mipi_dsi_set(dsi, LPCLKSET, LPCLKSET_CKEN);

	/* CFGCLK enabled */
	rcar_mipi_dsi_set(dsi, CFGCLKSET, CFGCLKSET_CKEN);

	rcar_mipi_dsi_clr(dsi, PHYSETUP, PHYSETUP_RSTZ);
	rcar_mipi_dsi_clr(dsi, PHYSETUP, PHYSETUP_SHUTDOWNZ);

	rcar_mipi_dsi_set(dsi, PHTC, PHTC_TESTCLR);
	rcar_mipi_dsi_clr(dsi, PHTC, PHTC_TESTCLR);

	/* PHY setting */
	phy_setup = rcar_mipi_dsi_read(dsi, PHYSETUP);
	phy_setup &= ~PHYSETUP_HSFREQRANGE_MASK;
	phy_setup |= PHYSETUP_HSFREQRANGE(setup_info.hsfreqrange);
	rcar_mipi_dsi_write(dsi, PHYSETUP, phy_setup);

	for (i = 0; i < ARRAY_SIZE(phtw); i++) {
		ret = rcar_mipi_dsi_phtw_test(dsi, phtw[i]);
		if (ret < 0)
			return ret;
	}

	/* PLL Clock Setting */
	rcar_mipi_dsi_clr(dsi, CLOCKSET1, CLOCKSET1_SHADOW_CLEAR);
	rcar_mipi_dsi_set(dsi, CLOCKSET1, CLOCKSET1_SHADOW_CLEAR);
	rcar_mipi_dsi_clr(dsi, CLOCKSET1, CLOCKSET1_SHADOW_CLEAR);

	clockset2 = CLOCKSET2_M(setup_info.m) | CLOCKSET2_N(setup_info.n)
		  | CLOCKSET2_VCO_CNTRL(setup_info.vco_cntrl);
	clockset3 = CLOCKSET3_PROP_CNTRL(setup_info.prop_cntrl)
		  | CLOCKSET3_INT_CNTRL(0)
		  | CLOCKSET3_CPBIAS_CNTRL(0x10)
		  | CLOCKSET3_GMP_CNTRL(1);
	rcar_mipi_dsi_write(dsi, CLOCKSET2, clockset2);
	rcar_mipi_dsi_write(dsi, CLOCKSET3, clockset3);

	rcar_mipi_dsi_clr(dsi, CLOCKSET1, CLOCKSET1_UPDATEPLL);
	rcar_mipi_dsi_set(dsi, CLOCKSET1, CLOCKSET1_UPDATEPLL);
	udelay(10);
	rcar_mipi_dsi_clr(dsi, CLOCKSET1, CLOCKSET1_UPDATEPLL);

	ppisetr = PPISETR_DLEN_3 | PPISETR_CLEN;
	rcar_mipi_dsi_write(dsi, PPISETR, ppisetr);

	rcar_mipi_dsi_set(dsi, PHYSETUP, PHYSETUP_SHUTDOWNZ);
	rcar_mipi_dsi_set(dsi, PHYSETUP, PHYSETUP_RSTZ);
	usleep_range(400, 500);

	/* Checking PPI clock status register */
	for (timeout = 10; timeout > 0; --timeout) {
		if ((rcar_mipi_dsi_read(dsi, PPICLSR) & PPICLSR_STPST) &&
		    (rcar_mipi_dsi_read(dsi, PPIDLSR) & PPIDLSR_STPST) &&
		    (rcar_mipi_dsi_read(dsi, CLOCKSET1) & CLOCKSET1_LOCK))
			break;

		usleep_range(1000, 2000);
	}

	if (!timeout) {
		dev_err(dsi->dev, "failed to enable PPI clock\n");
		return -ETIMEDOUT;
	}

	for (i = 0; i < ARRAY_SIZE(phtw2); i++) {
		ret = rcar_mipi_dsi_phtw_test(dsi, phtw2[i]);
		if (ret < 0)
			return ret;
	}

	/* Enable DOT clock */
	vclkset = VCLKSET_CKEN;
	rcar_mipi_dsi_set(dsi, VCLKSET, vclkset);

	if (dsi_format == 24)
		vclkset |= VCLKSET_BPP_24;
	else if (dsi_format == 18)
		vclkset |= VCLKSET_BPP_18;
	else if (dsi_format == 16)
		vclkset |= VCLKSET_BPP_16;
	else {
		dev_warn(dsi->dev, "unsupported format");
		return -EINVAL;
	}
	vclkset |= VCLKSET_COLOR_RGB | VCLKSET_DIV(setup_info.div)
		|  VCLKSET_LANE(dsi->lanes - 1);

	rcar_mipi_dsi_set(dsi, VCLKSET, vclkset);

	/* After setting VCLKSET register, enable VCLKEN */
	rcar_mipi_dsi_set(dsi, VCLKEN, VCLKEN_CKEN);

	dev_dbg(dsi->dev, "DSI device is started\n");

	return 0;
}

static void rcar_mipi_dsi_shutdown(struct rcar_mipi_dsi *dsi)
{
	rcar_mipi_dsi_clr(dsi, PHYSETUP, PHYSETUP_RSTZ);
	rcar_mipi_dsi_clr(dsi, PHYSETUP, PHYSETUP_SHUTDOWNZ);

	dev_dbg(dsi->dev, "DSI device is shutdown\n");
}

static int rcar_mipi_dsi_clk_enable(struct rcar_mipi_dsi *dsi)
{
	int ret;

	reset_control_deassert(dsi->rstc);

	ret = clk_prepare_enable(dsi->clocks.mod);
	if (ret < 0)
		goto err_reset;

	ret = clk_prepare_enable(dsi->clocks.dsi);
	if (ret < 0)
		goto err_clock;

	return 0;

err_clock:
	clk_disable_unprepare(dsi->clocks.mod);
err_reset:
	reset_control_assert(dsi->rstc);
	return ret;
}

static void rcar_mipi_dsi_clk_disable(struct rcar_mipi_dsi *dsi)
{
	clk_disable_unprepare(dsi->clocks.dsi);
	clk_disable_unprepare(dsi->clocks.mod);

	reset_control_assert(dsi->rstc);
}

static int rcar_mipi_dsi_start_hs_clock(struct rcar_mipi_dsi *dsi)
{
	/*
	 * In HW manual, we need to check TxDDRClkHS-Q Stable? but it dont
	 * write how to check. So we skip this check in this patch
	 */
	u32 status;
	int ret;

	/* Start HS clock. */
	rcar_mipi_dsi_set(dsi, PPICLCR, PPICLCR_TXREQHS);

	ret = read_poll_timeout(rcar_mipi_dsi_read, status,
				status & PPICLSR_TOHS,
				2000, 10000, false, dsi, PPICLSR);
	if (ret < 0) {
		dev_err(dsi->dev, "failed to enable HS clock\n");
		return ret;
	}

	rcar_mipi_dsi_set(dsi, PPICLSCR, PPICLSCR_TOHS);

	return 0;
}

static int rcar_mipi_dsi_start_video(struct rcar_mipi_dsi *dsi)
{
	u32 status;
	int ret;

	/* Wait for the link to be ready. */
	ret = read_poll_timeout(rcar_mipi_dsi_read, status,
				!(status & (LINKSR_LPBUSY | LINKSR_HSBUSY)),
				2000, 10000, false, dsi, LINKSR);
	if (ret < 0) {
		dev_err(dsi->dev, "Link failed to become ready\n");
		return ret;
	}

	/* De-assert video FIFO clear. */
	rcar_mipi_dsi_clr(dsi, TXVMCR, TXVMCR_VFCLR);

	ret = read_poll_timeout(rcar_mipi_dsi_read, status,
				status & TXVMSR_VFRDY,
				2000, 10000, false, dsi, TXVMSR);
	if (ret < 0) {
		dev_err(dsi->dev, "Failed to de-assert video FIFO clear\n");
		return ret;
	}

	/* Enable transmission in video mode. */
	rcar_mipi_dsi_set(dsi, TXVMCR, TXVMCR_EN_VIDEO);

	ret = read_poll_timeout(rcar_mipi_dsi_read, status,
				status & TXVMSR_RDY,
				2000, 10000, false, dsi, TXVMSR);
	if (ret < 0) {
		dev_err(dsi->dev, "Failed to enable video transmission\n");
		return ret;
	}

	return 0;
}

/* -----------------------------------------------------------------------------
 * Bridge
 */

static int rcar_mipi_dsi_attach(struct drm_bridge *bridge,
				enum drm_bridge_attach_flags flags)
{
	struct rcar_mipi_dsi *dsi = bridge_to_rcar_mipi_dsi(bridge);

	return drm_bridge_attach(bridge->encoder, dsi->next_bridge, bridge,
				 flags);
}

static void rcar_mipi_dsi_atomic_enable(struct drm_bridge *bridge,
					struct drm_bridge_state *old_bridge_state)
{
	struct drm_atomic_state *state = old_bridge_state->base.state;
	struct rcar_mipi_dsi *dsi = bridge_to_rcar_mipi_dsi(bridge);
	const struct drm_display_mode *mode;
	struct drm_connector *connector;
	struct drm_crtc *crtc;
	int ret;

	connector = drm_atomic_get_new_connector_for_encoder(state,
							     bridge->encoder);
	crtc = drm_atomic_get_new_connector_state(state, connector)->crtc;
	mode = &drm_atomic_get_new_crtc_state(state, crtc)->adjusted_mode;

	ret = rcar_mipi_dsi_clk_enable(dsi);
	if (ret < 0) {
		dev_err(dsi->dev, "failed to enable DSI clocks\n");
		return;
	}

	ret = rcar_mipi_dsi_startup(dsi, mode);
	if (ret < 0)
		goto err_dsi_startup;

	rcar_mipi_dsi_set_display_timing(dsi, mode);

	ret = rcar_mipi_dsi_start_hs_clock(dsi);
	if (ret < 0)
		goto err_dsi_start_hs;

	rcar_mipi_dsi_start_video(dsi);

	return;

err_dsi_start_hs:
	rcar_mipi_dsi_shutdown(dsi);
err_dsi_startup:
	rcar_mipi_dsi_clk_disable(dsi);
}

static void rcar_mipi_dsi_atomic_disable(struct drm_bridge *bridge,
					 struct drm_bridge_state *old_bridge_state)
{
	struct rcar_mipi_dsi *dsi = bridge_to_rcar_mipi_dsi(bridge);

	rcar_mipi_dsi_shutdown(dsi);
	rcar_mipi_dsi_clk_disable(dsi);
}

static enum drm_mode_status
rcar_mipi_dsi_bridge_mode_valid(struct drm_bridge *bridge,
				const struct drm_display_info *info,
				const struct drm_display_mode *mode)
{
	if (mode->clock > 297000)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static const struct drm_bridge_funcs rcar_mipi_dsi_bridge_ops = {
	.attach = rcar_mipi_dsi_attach,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset = drm_atomic_helper_bridge_reset,
	.atomic_enable = rcar_mipi_dsi_atomic_enable,
	.atomic_disable = rcar_mipi_dsi_atomic_disable,
	.mode_valid = rcar_mipi_dsi_bridge_mode_valid,
};

/* -----------------------------------------------------------------------------
 * Host setting
 */

static int rcar_mipi_dsi_host_attach(struct mipi_dsi_host *host,
				     struct mipi_dsi_device *device)
{
	struct rcar_mipi_dsi *dsi = host_to_rcar_mipi_dsi(host);
	int ret;

	if (device->lanes > dsi->num_data_lanes)
		return -EINVAL;

	dsi->lanes = device->lanes;
	dsi->format = device->format;

	dsi->next_bridge = devm_drm_of_get_bridge(dsi->dev, dsi->dev->of_node,
						  1, 0);
	if (IS_ERR(dsi->next_bridge)) {
		ret = PTR_ERR(dsi->next_bridge);
		dev_err(dsi->dev, "failed to get next bridge: %d\n", ret);
		return ret;
	}

	/* Initialize the DRM bridge. */
	dsi->bridge.funcs = &rcar_mipi_dsi_bridge_ops;
	dsi->bridge.of_node = dsi->dev->of_node;
	drm_bridge_add(&dsi->bridge);

	return 0;
}

static int rcar_mipi_dsi_host_detach(struct mipi_dsi_host *host,
					struct mipi_dsi_device *device)
{
	struct rcar_mipi_dsi *dsi = host_to_rcar_mipi_dsi(host);

	drm_bridge_remove(&dsi->bridge);

	return 0;
}

static const struct mipi_dsi_host_ops rcar_mipi_dsi_host_ops = {
	.attach = rcar_mipi_dsi_host_attach,
	.detach = rcar_mipi_dsi_host_detach,
};

/* -----------------------------------------------------------------------------
 * Probe & Remove
 */

static int rcar_mipi_dsi_parse_dt(struct rcar_mipi_dsi *dsi)
{
	int ret;

	ret = drm_of_get_data_lanes_count_ep(dsi->dev->of_node, 1, 0, 1, 4);
	if (ret < 0) {
		dev_err(dsi->dev, "missing or invalid data-lanes property\n");
		return ret;
	}

	dsi->num_data_lanes = ret;
	return 0;
}

static struct clk *rcar_mipi_dsi_get_clock(struct rcar_mipi_dsi *dsi,
					   const char *name,
					   bool optional)
{
	struct clk *clk;

	clk = devm_clk_get(dsi->dev, name);
	if (!IS_ERR(clk))
		return clk;

	if (PTR_ERR(clk) == -ENOENT && optional)
		return NULL;

	dev_err_probe(dsi->dev, PTR_ERR(clk), "failed to get %s clock\n",
		      name ? name : "module");

	return clk;
}

static int rcar_mipi_dsi_get_clocks(struct rcar_mipi_dsi *dsi)
{
	dsi->clocks.mod = rcar_mipi_dsi_get_clock(dsi, NULL, false);
	if (IS_ERR(dsi->clocks.mod))
		return PTR_ERR(dsi->clocks.mod);

	dsi->clocks.pll = rcar_mipi_dsi_get_clock(dsi, "pll", true);
	if (IS_ERR(dsi->clocks.pll))
		return PTR_ERR(dsi->clocks.pll);

	dsi->clocks.dsi = rcar_mipi_dsi_get_clock(dsi, "dsi", true);
	if (IS_ERR(dsi->clocks.dsi))
		return PTR_ERR(dsi->clocks.dsi);

	if (!dsi->clocks.pll && !dsi->clocks.dsi) {
		dev_err(dsi->dev, "no input clock (pll, dsi)\n");
		return -EINVAL;
	}

	return 0;
}

static int rcar_mipi_dsi_probe(struct platform_device *pdev)
{
	struct rcar_mipi_dsi *dsi;
	struct resource *mem;
	int ret;

	dsi = devm_kzalloc(&pdev->dev, sizeof(*dsi), GFP_KERNEL);
	if (dsi == NULL)
		return -ENOMEM;

	platform_set_drvdata(pdev, dsi);

	dsi->dev = &pdev->dev;
	dsi->info = of_device_get_match_data(&pdev->dev);

	ret = rcar_mipi_dsi_parse_dt(dsi);
	if (ret < 0)
		return ret;

	/* Acquire resources. */
	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dsi->mmio = devm_ioremap_resource(dsi->dev, mem);
	if (IS_ERR(dsi->mmio))
		return PTR_ERR(dsi->mmio);

	ret = rcar_mipi_dsi_get_clocks(dsi);
	if (ret < 0)
		return ret;

	dsi->rstc = devm_reset_control_get(dsi->dev, NULL);
	if (IS_ERR(dsi->rstc)) {
		dev_err(dsi->dev, "failed to get cpg reset\n");
		return PTR_ERR(dsi->rstc);
	}

	/* Initialize the DSI host. */
	dsi->host.dev = dsi->dev;
	dsi->host.ops = &rcar_mipi_dsi_host_ops;
	ret = mipi_dsi_host_register(&dsi->host);
	if (ret < 0)
		return ret;

	return 0;
}

static int rcar_mipi_dsi_remove(struct platform_device *pdev)
{
	struct rcar_mipi_dsi *dsi = platform_get_drvdata(pdev);

	mipi_dsi_host_unregister(&dsi->host);

	return 0;
}

static const struct of_device_id rcar_mipi_dsi_of_table[] = {
	{ .compatible = "renesas,r8a779a0-dsi-csi2-tx" },
	{ }
};

MODULE_DEVICE_TABLE(of, rcar_mipi_dsi_of_table);

static struct platform_driver rcar_mipi_dsi_platform_driver = {
	.probe          = rcar_mipi_dsi_probe,
	.remove         = rcar_mipi_dsi_remove,
	.driver         = {
		.name   = "rcar-mipi-dsi",
		.of_match_table = rcar_mipi_dsi_of_table,
	},
};

module_platform_driver(rcar_mipi_dsi_platform_driver);

MODULE_DESCRIPTION("Renesas R-Car MIPI DSI Encoder Driver");
MODULE_LICENSE("GPL");
