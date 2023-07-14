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
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/of.h>
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

#include "rcar_mipi_dsi.h"
#include "rcar_mipi_dsi_regs.h"

#define MHZ(v) ((u32)((v) * 1000000U))

enum rcar_mipi_dsi_hw_model {
	RCAR_DSI_V3U,
	RCAR_DSI_V4H,
};

struct rcar_mipi_dsi_device_info {
	enum rcar_mipi_dsi_hw_model model;

	const struct dsi_clk_config *clk_cfg;

	u8 clockset2_m_offset;

	u8 n_min;
	u8 n_max;
	u8 n_mul;
	unsigned long fpfd_min;
	unsigned long fpfd_max;
	u16 m_min;
	u16 m_max;
	unsigned long fout_min;
	unsigned long fout_max;
};

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

struct dsi_setup_info {
	unsigned long hsfreq;
	u16 hsfreqrange;

	unsigned long fout;
	u16 m;
	u16 n;
	u16 vclk_divider;
	const struct dsi_clk_config *clkset;
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

static const u32 hsfreqrange_table[][2] = {
	{   MHZ(80), 0x00 }, {   MHZ(90), 0x10 }, {  MHZ(100), 0x20 },
	{  MHZ(110), 0x30 }, {  MHZ(120), 0x01 }, {  MHZ(130), 0x11 },
	{  MHZ(140), 0x21 }, {  MHZ(150), 0x31 }, {  MHZ(160), 0x02 },
	{  MHZ(170), 0x12 }, {  MHZ(180), 0x22 }, {  MHZ(190), 0x32 },
	{  MHZ(205), 0x03 }, {  MHZ(220), 0x13 }, {  MHZ(235), 0x23 },
	{  MHZ(250), 0x33 }, {  MHZ(275), 0x04 }, {  MHZ(300), 0x14 },
	{  MHZ(325), 0x25 }, {  MHZ(350), 0x35 }, {  MHZ(400), 0x05 },
	{  MHZ(450), 0x16 }, {  MHZ(500), 0x26 }, {  MHZ(550), 0x37 },
	{  MHZ(600), 0x07 }, {  MHZ(650), 0x18 }, {  MHZ(700), 0x28 },
	{  MHZ(750), 0x39 }, {  MHZ(800), 0x09 }, {  MHZ(850), 0x19 },
	{  MHZ(900), 0x29 }, {  MHZ(950), 0x3a }, { MHZ(1000), 0x0a },
	{ MHZ(1050), 0x1a }, { MHZ(1100), 0x2a }, { MHZ(1150), 0x3b },
	{ MHZ(1200), 0x0b }, { MHZ(1250), 0x1b }, { MHZ(1300), 0x2b },
	{ MHZ(1350), 0x3c }, { MHZ(1400), 0x0c }, { MHZ(1450), 0x1c },
	{ MHZ(1500), 0x2c }, { MHZ(1550), 0x3d }, { MHZ(1600), 0x0d },
	{ MHZ(1650), 0x1d }, { MHZ(1700), 0x2e }, { MHZ(1750), 0x3e },
	{ MHZ(1800), 0x0e }, { MHZ(1850), 0x1e }, { MHZ(1900), 0x2f },
	{ MHZ(1950), 0x3f }, { MHZ(2000), 0x0f }, { MHZ(2050), 0x40 },
	{ MHZ(2100), 0x41 }, { MHZ(2150), 0x42 }, { MHZ(2200), 0x43 },
	{ MHZ(2250), 0x44 }, { MHZ(2300), 0x45 }, { MHZ(2350), 0x46 },
	{ MHZ(2400), 0x47 }, { MHZ(2450), 0x48 }, { MHZ(2500), 0x49 },
	{ /* sentinel */ },
};

struct dsi_clk_config {
	u32 min_freq;
	u32 max_freq;
	u8 vco_cntrl;
	u8 cpbias_cntrl;
	u8 gmp_cntrl;
	u8 int_cntrl;
	u8 prop_cntrl;
};

static const struct dsi_clk_config dsi_clk_cfg_v3u[] = {
	{   MHZ(40),    MHZ(55), 0x3f, 0x10, 0x01, 0x00, 0x0b },
	{   MHZ(52.5),  MHZ(80), 0x39, 0x10, 0x01, 0x00, 0x0b },
	{   MHZ(80),   MHZ(110), 0x2f, 0x10, 0x01, 0x00, 0x0b },
	{  MHZ(105),   MHZ(160), 0x29, 0x10, 0x01, 0x00, 0x0b },
	{  MHZ(160),   MHZ(220), 0x1f, 0x10, 0x01, 0x00, 0x0b },
	{  MHZ(210),   MHZ(320), 0x19, 0x10, 0x01, 0x00, 0x0b },
	{  MHZ(320),   MHZ(440), 0x0f, 0x10, 0x01, 0x00, 0x0b },
	{  MHZ(420),   MHZ(660), 0x09, 0x10, 0x01, 0x00, 0x0b },
	{  MHZ(630),  MHZ(1149), 0x03, 0x10, 0x01, 0x00, 0x0b },
	{ MHZ(1100),  MHZ(1152), 0x01, 0x10, 0x01, 0x00, 0x0b },
	{ MHZ(1150),  MHZ(1250), 0x01, 0x10, 0x01, 0x00, 0x0c },
	{ /* sentinel */ },
};

static const struct dsi_clk_config dsi_clk_cfg_v4h[] = {
	{   MHZ(40),    MHZ(45.31),  0x2b, 0x00, 0x00, 0x08, 0x0a },
	{   MHZ(45.31), MHZ(54.66),  0x28, 0x00, 0x00, 0x08, 0x0a },
	{   MHZ(54.66), MHZ(62.5),   0x28, 0x00, 0x00, 0x08, 0x0a },
	{   MHZ(62.5),  MHZ(75),     0x27, 0x00, 0x00, 0x08, 0x0a },
	{   MHZ(75),    MHZ(90.63),  0x23, 0x00, 0x00, 0x08, 0x0a },
	{   MHZ(90.63), MHZ(109.37), 0x20, 0x00, 0x00, 0x08, 0x0a },
	{  MHZ(109.37), MHZ(125),    0x20, 0x00, 0x00, 0x08, 0x0a },
	{  MHZ(125),    MHZ(150),    0x1f, 0x00, 0x00, 0x08, 0x0a },
	{  MHZ(150),    MHZ(181.25), 0x1b, 0x00, 0x00, 0x08, 0x0a },
	{  MHZ(181.25), MHZ(218.75), 0x18, 0x00, 0x00, 0x08, 0x0a },
	{  MHZ(218.75), MHZ(250),    0x18, 0x00, 0x00, 0x08, 0x0a },
	{  MHZ(250),    MHZ(300),    0x17, 0x00, 0x00, 0x08, 0x0a },
	{  MHZ(300),    MHZ(362.5),  0x13, 0x00, 0x00, 0x08, 0x0a },
	{  MHZ(362.5),  MHZ(455.48), 0x10, 0x00, 0x00, 0x08, 0x0a },
	{  MHZ(455.48), MHZ(500),    0x10, 0x00, 0x00, 0x08, 0x0a },
	{  MHZ(500),    MHZ(600),    0x0f, 0x00, 0x00, 0x08, 0x0a },
	{  MHZ(600),    MHZ(725),    0x0b, 0x00, 0x00, 0x08, 0x0a },
	{  MHZ(725),    MHZ(875),    0x08, 0x00, 0x00, 0x08, 0x0a },
	{  MHZ(875),   MHZ(1000),    0x08, 0x00, 0x00, 0x08, 0x0a },
	{ MHZ(1000),   MHZ(1200),    0x07, 0x00, 0x00, 0x08, 0x0a },
	{ MHZ(1200),   MHZ(1250),    0x03, 0x00, 0x00, 0x08, 0x0a },
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

static int rcar_mipi_dsi_write_phtw(struct rcar_mipi_dsi *dsi, u32 phtw)
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

static int rcar_mipi_dsi_write_phtw_arr(struct rcar_mipi_dsi *dsi,
					const u32 *phtw, unsigned int size)
{
	for (unsigned int i = 0; i < size; i++) {
		int ret = rcar_mipi_dsi_write_phtw(dsi, phtw[i]);

		if (ret < 0)
			return ret;
	}

	return 0;
}

#define WRITE_PHTW(...)                                               \
	({                                                            \
		static const u32 phtw[] = { __VA_ARGS__ };            \
		int ret;                                              \
		ret = rcar_mipi_dsi_write_phtw_arr(dsi, phtw,         \
						   ARRAY_SIZE(phtw)); \
		ret;                                                  \
	})

static int rcar_mipi_dsi_init_phtw_v3u(struct rcar_mipi_dsi *dsi)
{
	return WRITE_PHTW(0x01020114, 0x01600115, 0x01030116, 0x0102011d,
			  0x011101a4, 0x018601a4, 0x014201a0, 0x010001a3,
			  0x0101011f);
}

static int rcar_mipi_dsi_post_init_phtw_v3u(struct rcar_mipi_dsi *dsi)
{
	return WRITE_PHTW(0x010c0130, 0x010c0140, 0x010c0150, 0x010c0180,
			  0x010c0190, 0x010a0160, 0x010a0170, 0x01800164,
			  0x01800174);
}

static int rcar_mipi_dsi_init_phtw_v4h(struct rcar_mipi_dsi *dsi,
				       const struct dsi_setup_info *setup_info)
{
	int ret;

	if (setup_info->hsfreq < MHZ(450)) {
		ret = WRITE_PHTW(0x01010100, 0x011b01ac);
		if (ret)
			return ret;
	}

	ret = WRITE_PHTW(0x01010100, 0x01030173, 0x01000174, 0x01500175,
			 0x01030176, 0x01040166, 0x010201ad);
	if (ret)
		return ret;

	if (setup_info->hsfreq <= MHZ(1000))
		ret = WRITE_PHTW(0x01020100, 0x01910170, 0x01020171,
				 0x01110172);
	else if (setup_info->hsfreq <= MHZ(1500))
		ret = WRITE_PHTW(0x01020100, 0x01980170, 0x01030171,
				 0x01100172);
	else if (setup_info->hsfreq <= MHZ(2500))
		ret = WRITE_PHTW(0x01020100, 0x0144016b, 0x01000172);
	else
		return -EINVAL;

	if (ret)
		return ret;

	if (dsi->lanes <= 1) {
		ret = WRITE_PHTW(0x01070100, 0x010e010b);
		if (ret)
			return ret;
	}

	if (dsi->lanes <= 2) {
		ret = WRITE_PHTW(0x01090100, 0x010e010b);
		if (ret)
			return ret;
	}

	if (dsi->lanes <= 3) {
		ret = WRITE_PHTW(0x010b0100, 0x010e010b);
		if (ret)
			return ret;
	}

	if (setup_info->hsfreq <= MHZ(1500)) {
		ret = WRITE_PHTW(0x01010100, 0x01c0016e);
		if (ret)
			return ret;
	}

	return 0;
}

static int
rcar_mipi_dsi_post_init_phtw_v4h(struct rcar_mipi_dsi *dsi,
				 const struct dsi_setup_info *setup_info)
{
	u32 status;
	int ret;

	if (setup_info->hsfreq <= MHZ(1500)) {
		WRITE_PHTW(0x01020100, 0x00000180);

		ret = read_poll_timeout(rcar_mipi_dsi_read, status,
					status & PHTR_TEST, 2000, 10000, false,
					dsi, PHTR);
		if (ret < 0) {
			dev_err(dsi->dev, "failed to test PHTR\n");
			return ret;
		}

		WRITE_PHTW(0x01010100, 0x0100016e);
	}

	return 0;
}

/* -----------------------------------------------------------------------------
 * Hardware Setup
 */

static void rcar_mipi_dsi_pll_calc(struct rcar_mipi_dsi *dsi,
				   unsigned long fin_rate,
				   unsigned long fout_target,
				   struct dsi_setup_info *setup_info)
{
	unsigned int best_err = -1;
	const struct rcar_mipi_dsi_device_info *info = dsi->info;

	for (unsigned int n = info->n_min; n <= info->n_max; n++) {
		unsigned long fpfd;

		fpfd = fin_rate / n;

		if (fpfd < info->fpfd_min || fpfd > info->fpfd_max)
			continue;

		for (unsigned int m = info->m_min; m <= info->m_max; m++) {
			unsigned int err;
			u64 fout;

			fout = div64_u64((u64)fpfd * m, dsi->info->n_mul);

			if (fout < info->fout_min || fout > info->fout_max)
				continue;

			fout = div64_u64(fout, setup_info->vclk_divider);

			if (fout < setup_info->clkset->min_freq ||
			    fout > setup_info->clkset->max_freq)
				continue;

			err = abs((long)(fout - fout_target) * 10000 /
				  (long)fout_target);
			if (err < best_err) {
				setup_info->m = m;
				setup_info->n = n;
				setup_info->fout = (unsigned long)fout;
				best_err = err;

				if (err == 0)
					return;
			}
		}
	}
}

static void rcar_mipi_dsi_parameters_calc(struct rcar_mipi_dsi *dsi,
					  struct clk *clk, unsigned long target,
					  struct dsi_setup_info *setup_info)
{

	const struct dsi_clk_config *clk_cfg;
	unsigned long fout_target;
	unsigned long fin_rate;
	unsigned int i;
	unsigned int err;

	/*
	 * Calculate Fout = dot clock * ColorDepth / (2 * Lane Count)
	 * The range out Fout is [40 - 1250] Mhz
	 */
	fout_target = target * mipi_dsi_pixel_format_to_bpp(dsi->format)
		    / (2 * dsi->lanes);
	if (fout_target < MHZ(40) || fout_target > MHZ(1250))
		return;

	/* Find PLL settings */
	for (clk_cfg = dsi->info->clk_cfg; clk_cfg->min_freq != 0; clk_cfg++) {
		if (fout_target > clk_cfg->min_freq &&
		    fout_target <= clk_cfg->max_freq) {
			setup_info->clkset = clk_cfg;
			break;
		}
	}

	fin_rate = clk_get_rate(clk);

	switch (dsi->info->model) {
	case RCAR_DSI_V3U:
	default:
		setup_info->vclk_divider = 1 << ((clk_cfg->vco_cntrl >> 4) & 0x3);
		break;

	case RCAR_DSI_V4H:
		setup_info->vclk_divider = 1 << (((clk_cfg->vco_cntrl >> 3) & 0x7) + 1);
		break;
	}

	rcar_mipi_dsi_pll_calc(dsi, fin_rate, fout_target, setup_info);

	/* Find hsfreqrange */
	setup_info->hsfreq = setup_info->fout * 2;
	for (i = 0; i < ARRAY_SIZE(hsfreqrange_table); i++) {
		if (hsfreqrange_table[i][0] >= setup_info->hsfreq) {
			setup_info->hsfreqrange = hsfreqrange_table[i][1];
			break;
		}
	}

	err = abs((long)(setup_info->fout - fout_target) * 10000 / (long)fout_target);

	dev_dbg(dsi->dev,
		"Fout = %u * %lu / (%u * %u * %u) = %lu (target %lu Hz, error %d.%02u%%)\n",
		setup_info->m, fin_rate, dsi->info->n_mul, setup_info->n,
		setup_info->vclk_divider, setup_info->fout, fout_target,
		err / 100, err % 100);

	dev_dbg(dsi->dev,
		"vco_cntrl = 0x%x\tprop_cntrl = 0x%x\thsfreqrange = 0x%x\n",
		clk_cfg->vco_cntrl, clk_cfg->prop_cntrl,
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
	int ret;
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

	switch (dsi->info->model) {
	case RCAR_DSI_V3U:
	default:
		ret = rcar_mipi_dsi_init_phtw_v3u(dsi);
		if (ret < 0)
			return ret;
		break;

	case RCAR_DSI_V4H:
		ret = rcar_mipi_dsi_init_phtw_v4h(dsi, &setup_info);
		if (ret < 0)
			return ret;
		break;
	}

	/* PLL Clock Setting */
	rcar_mipi_dsi_clr(dsi, CLOCKSET1, CLOCKSET1_SHADOW_CLEAR);
	rcar_mipi_dsi_set(dsi, CLOCKSET1, CLOCKSET1_SHADOW_CLEAR);
	rcar_mipi_dsi_clr(dsi, CLOCKSET1, CLOCKSET1_SHADOW_CLEAR);

	clockset2 = CLOCKSET2_M(setup_info.m - dsi->info->clockset2_m_offset)
		  | CLOCKSET2_N(setup_info.n - 1)
		  | CLOCKSET2_VCO_CNTRL(setup_info.clkset->vco_cntrl);
	clockset3 = CLOCKSET3_PROP_CNTRL(setup_info.clkset->prop_cntrl)
		  | CLOCKSET3_INT_CNTRL(setup_info.clkset->int_cntrl)
		  | CLOCKSET3_CPBIAS_CNTRL(setup_info.clkset->cpbias_cntrl)
		  | CLOCKSET3_GMP_CNTRL(setup_info.clkset->gmp_cntrl);
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

	switch (dsi->info->model) {
	case RCAR_DSI_V3U:
	default:
		ret = rcar_mipi_dsi_post_init_phtw_v3u(dsi);
		if (ret < 0)
			return ret;
		break;

	case RCAR_DSI_V4H:
		ret = rcar_mipi_dsi_post_init_phtw_v4h(dsi, &setup_info);
		if (ret < 0)
			return ret;
		break;
	}

	/* Enable DOT clock */
	vclkset = VCLKSET_CKEN;
	rcar_mipi_dsi_write(dsi, VCLKSET, vclkset);

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

	vclkset |= VCLKSET_COLOR_RGB | VCLKSET_LANE(dsi->lanes - 1);

	switch (dsi->info->model) {
	case RCAR_DSI_V3U:
	default:
		vclkset |= VCLKSET_DIV_V3U(__ffs(setup_info.vclk_divider));
		break;

	case RCAR_DSI_V4H:
		vclkset |= VCLKSET_DIV_V4H(__ffs(setup_info.vclk_divider) - 1);
		break;
	}

	rcar_mipi_dsi_write(dsi, VCLKSET, vclkset);

	/* After setting VCLKSET register, enable VCLKEN */
	rcar_mipi_dsi_set(dsi, VCLKEN, VCLKEN_CKEN);

	dev_dbg(dsi->dev, "DSI device is started\n");

	return 0;
}

static void rcar_mipi_dsi_shutdown(struct rcar_mipi_dsi *dsi)
{
	/* Disable VCLKEN */
	rcar_mipi_dsi_write(dsi, VCLKSET, 0);

	/* Disable DOT clock */
	rcar_mipi_dsi_write(dsi, VCLKSET, 0);

	rcar_mipi_dsi_clr(dsi, PHYSETUP, PHYSETUP_RSTZ);
	rcar_mipi_dsi_clr(dsi, PHYSETUP, PHYSETUP_SHUTDOWNZ);

	/* CFGCLK disable */
	rcar_mipi_dsi_clr(dsi, CFGCLKSET, CFGCLKSET_CKEN);

	/* LPCLK disable */
	rcar_mipi_dsi_clr(dsi, LPCLKSET, LPCLKSET_CKEN);

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

static void rcar_mipi_dsi_stop_video(struct rcar_mipi_dsi *dsi)
{
	u32 status;
	int ret;

	/* Disable transmission in video mode. */
	rcar_mipi_dsi_clr(dsi, TXVMCR, TXVMCR_EN_VIDEO);

	ret = read_poll_timeout(rcar_mipi_dsi_read, status,
				!(status & TXVMSR_ACT),
				2000, 100000, false, dsi, TXVMSR);
	if (ret < 0) {
		dev_err(dsi->dev, "Failed to disable video transmission\n");
		return;
	}

	/* Assert video FIFO clear. */
	rcar_mipi_dsi_set(dsi, TXVMCR, TXVMCR_VFCLR);

	ret = read_poll_timeout(rcar_mipi_dsi_read, status,
				!(status & TXVMSR_VFRDY),
				2000, 100000, false, dsi, TXVMSR);
	if (ret < 0) {
		dev_err(dsi->dev, "Failed to assert video FIFO clear\n");
		return;
	}
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
	struct rcar_mipi_dsi *dsi = bridge_to_rcar_mipi_dsi(bridge);

	rcar_mipi_dsi_start_video(dsi);
}

static void rcar_mipi_dsi_atomic_disable(struct drm_bridge *bridge,
					 struct drm_bridge_state *old_bridge_state)
{
	struct rcar_mipi_dsi *dsi = bridge_to_rcar_mipi_dsi(bridge);

	rcar_mipi_dsi_stop_video(dsi);
}

void rcar_mipi_dsi_pclk_enable(struct drm_bridge *bridge,
			       struct drm_atomic_state *state)
{
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

	return;

err_dsi_start_hs:
	rcar_mipi_dsi_shutdown(dsi);
err_dsi_startup:
	rcar_mipi_dsi_clk_disable(dsi);
}
EXPORT_SYMBOL_GPL(rcar_mipi_dsi_pclk_enable);

void rcar_mipi_dsi_pclk_disable(struct drm_bridge *bridge)
{
	struct rcar_mipi_dsi *dsi = bridge_to_rcar_mipi_dsi(bridge);

	rcar_mipi_dsi_shutdown(dsi);
	rcar_mipi_dsi_clk_disable(dsi);
}
EXPORT_SYMBOL_GPL(rcar_mipi_dsi_pclk_disable);

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

static void rcar_mipi_dsi_remove(struct platform_device *pdev)
{
	struct rcar_mipi_dsi *dsi = platform_get_drvdata(pdev);

	mipi_dsi_host_unregister(&dsi->host);
}

static const struct rcar_mipi_dsi_device_info v3u_data = {
	.model = RCAR_DSI_V3U,
	.clk_cfg = dsi_clk_cfg_v3u,
	.clockset2_m_offset = 2,
	.n_min = 3,
	.n_max = 8,
	.n_mul = 1,
	.fpfd_min = MHZ(2),
	.fpfd_max = MHZ(8),
	.m_min = 64,
	.m_max = 625,
	.fout_min = MHZ(320),
	.fout_max = MHZ(1250),
};

static const struct rcar_mipi_dsi_device_info v4h_data = {
	.model = RCAR_DSI_V4H,
	.clk_cfg = dsi_clk_cfg_v4h,
	.clockset2_m_offset = 0,
	.n_min = 1,
	.n_max = 8,
	.n_mul = 2,
	.fpfd_min = MHZ(8),
	.fpfd_max = MHZ(24),
	.m_min = 167,
	.m_max = 1000,
	.fout_min = MHZ(2000),
	.fout_max = MHZ(4000),
};

static const struct of_device_id rcar_mipi_dsi_of_table[] = {
	{ .compatible = "renesas,r8a779a0-dsi-csi2-tx", .data = &v3u_data },
	{ .compatible = "renesas,r8a779g0-dsi-csi2-tx", .data = &v4h_data },
	{ }
};

MODULE_DEVICE_TABLE(of, rcar_mipi_dsi_of_table);

static struct platform_driver rcar_mipi_dsi_platform_driver = {
	.probe          = rcar_mipi_dsi_probe,
	.remove_new     = rcar_mipi_dsi_remove,
	.driver         = {
		.name   = "rcar-mipi-dsi",
		.of_match_table = rcar_mipi_dsi_of_table,
	},
};

module_platform_driver(rcar_mipi_dsi_platform_driver);

MODULE_DESCRIPTION("Renesas R-Car MIPI DSI Encoder Driver");
MODULE_LICENSE("GPL");
