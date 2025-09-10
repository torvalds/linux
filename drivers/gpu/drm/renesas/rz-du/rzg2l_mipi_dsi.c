// SPDX-License-Identifier: GPL-2.0
/*
 * RZ/G2L MIPI DSI Encoder Driver
 *
 * Copyright (C) 2022 Renesas Electronics Corporation
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/math.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/units.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>
#include <video/mipi_display.h>

#include "rzg2l_mipi_dsi_regs.h"

#define RZG2L_DCS_BUF_SIZE	128 /* Maximum DCS buffer size in external memory. */

#define RZ_MIPI_DSI_FEATURE_16BPP	BIT(0)

struct rzg2l_mipi_dsi;

struct rzg2l_mipi_dsi_hw_info {
	int (*dphy_init)(struct rzg2l_mipi_dsi *dsi, u64 hsfreq_millihz);
	void (*dphy_startup_late_init)(struct rzg2l_mipi_dsi *dsi);
	void (*dphy_exit)(struct rzg2l_mipi_dsi *dsi);
	int (*dphy_conf_clks)(struct rzg2l_mipi_dsi *dsi, unsigned long mode_freq,
			      u64 *hsfreq_millihz);
	unsigned int (*dphy_mode_clk_check)(struct rzg2l_mipi_dsi *dsi,
					    unsigned long mode_freq);
	u32 phy_reg_offset;
	u32 link_reg_offset;
	unsigned long min_dclk;
	unsigned long max_dclk;
	u8 features;
};

struct rzg2l_mipi_dsi {
	struct device *dev;
	void __iomem *mmio;

	const struct rzg2l_mipi_dsi_hw_info *info;

	struct reset_control *rstc;
	struct reset_control *arstc;
	struct reset_control *prstc;

	struct mipi_dsi_host host;
	struct drm_bridge bridge;
	struct drm_bridge *next_bridge;

	struct clk *vclk;

	enum mipi_dsi_pixel_format format;
	unsigned int num_data_lanes;
	unsigned int lanes;
	unsigned long mode_flags;

	/* DCS buffer pointers when using external memory. */
	dma_addr_t dcs_buf_phys;
	u8 *dcs_buf_virt;
};

static inline struct rzg2l_mipi_dsi *
bridge_to_rzg2l_mipi_dsi(struct drm_bridge *bridge)
{
	return container_of(bridge, struct rzg2l_mipi_dsi, bridge);
}

static inline struct rzg2l_mipi_dsi *
host_to_rzg2l_mipi_dsi(struct mipi_dsi_host *host)
{
	return container_of(host, struct rzg2l_mipi_dsi, host);
}

struct rzg2l_mipi_dsi_timings {
	unsigned long hsfreq_max;
	u32 t_init;
	u32 tclk_prepare;
	u32 ths_prepare;
	u32 tclk_zero;
	u32 tclk_pre;
	u32 tclk_post;
	u32 tclk_trail;
	u32 ths_zero;
	u32 ths_trail;
	u32 ths_exit;
	u32 tlpx;
};

static const struct rzg2l_mipi_dsi_timings rzg2l_mipi_dsi_global_timings[] = {
	{
		.hsfreq_max = 80000000,
		.t_init = 79801,
		.tclk_prepare = 8,
		.ths_prepare = 13,
		.tclk_zero = 33,
		.tclk_pre = 24,
		.tclk_post = 94,
		.tclk_trail = 10,
		.ths_zero = 23,
		.ths_trail = 17,
		.ths_exit = 13,
		.tlpx = 6,
	},
	{
		.hsfreq_max = 125000000,
		.t_init = 79801,
		.tclk_prepare = 8,
		.ths_prepare = 12,
		.tclk_zero = 33,
		.tclk_pre = 15,
		.tclk_post = 94,
		.tclk_trail = 10,
		.ths_zero = 23,
		.ths_trail = 17,
		.ths_exit = 13,
		.tlpx = 6,
	},
	{
		.hsfreq_max = 250000000,
		.t_init = 79801,
		.tclk_prepare = 8,
		.ths_prepare = 12,
		.tclk_zero = 33,
		.tclk_pre = 13,
		.tclk_post = 94,
		.tclk_trail = 10,
		.ths_zero = 23,
		.ths_trail = 16,
		.ths_exit = 13,
		.tlpx = 6,
	},
	{
		.hsfreq_max = 360000000,
		.t_init = 79801,
		.tclk_prepare = 8,
		.ths_prepare = 10,
		.tclk_zero = 33,
		.tclk_pre = 4,
		.tclk_post = 35,
		.tclk_trail = 7,
		.ths_zero = 16,
		.ths_trail = 9,
		.ths_exit = 13,
		.tlpx = 6,
	},
	{
		.hsfreq_max = 720000000,
		.t_init = 79801,
		.tclk_prepare = 8,
		.ths_prepare = 9,
		.tclk_zero = 33,
		.tclk_pre = 4,
		.tclk_post = 35,
		.tclk_trail = 7,
		.ths_zero = 16,
		.ths_trail = 9,
		.ths_exit = 13,
		.tlpx = 6,
	},
	{
		.hsfreq_max = 1500000000,
		.t_init = 79801,
		.tclk_prepare = 8,
		.ths_prepare = 9,
		.tclk_zero = 33,
		.tclk_pre = 4,
		.tclk_post = 35,
		.tclk_trail = 7,
		.ths_zero = 16,
		.ths_trail = 9,
		.ths_exit = 13,
		.tlpx = 6,
	},
};

static void rzg2l_mipi_dsi_phy_write(struct rzg2l_mipi_dsi *dsi, u32 reg, u32 data)
{
	iowrite32(data, dsi->mmio + dsi->info->phy_reg_offset + reg);
}

static void rzg2l_mipi_dsi_link_write(struct rzg2l_mipi_dsi *dsi, u32 reg, u32 data)
{
	iowrite32(data, dsi->mmio + dsi->info->link_reg_offset + reg);
}

static u32 rzg2l_mipi_dsi_phy_read(struct rzg2l_mipi_dsi *dsi, u32 reg)
{
	return ioread32(dsi->mmio + dsi->info->phy_reg_offset + reg);
}

static u32 rzg2l_mipi_dsi_link_read(struct rzg2l_mipi_dsi *dsi, u32 reg)
{
	return ioread32(dsi->mmio + dsi->info->link_reg_offset + reg);
}

/* -----------------------------------------------------------------------------
 * Hardware Setup
 */

static int rzg2l_mipi_dsi_dphy_init(struct rzg2l_mipi_dsi *dsi,
				    u64 hsfreq_millihz)
{
	unsigned long hsfreq = DIV_ROUND_CLOSEST_ULL(hsfreq_millihz, MILLI);
	const struct rzg2l_mipi_dsi_timings *dphy_timings;
	unsigned int i;
	u32 dphyctrl0;
	u32 dphytim0;
	u32 dphytim1;
	u32 dphytim2;
	u32 dphytim3;
	int ret;

	/* All DSI global operation timings are set with recommended setting */
	for (i = 0; i < ARRAY_SIZE(rzg2l_mipi_dsi_global_timings); ++i) {
		dphy_timings = &rzg2l_mipi_dsi_global_timings[i];
		if (hsfreq <= dphy_timings->hsfreq_max)
			break;
	}

	/* Initializing DPHY before accessing LINK */
	dphyctrl0 = DSIDPHYCTRL0_CAL_EN_HSRX_OFS | DSIDPHYCTRL0_CMN_MASTER_EN |
		    DSIDPHYCTRL0_RE_VDD_DETVCCQLV18 | DSIDPHYCTRL0_EN_BGR;

	rzg2l_mipi_dsi_phy_write(dsi, DSIDPHYCTRL0, dphyctrl0);
	usleep_range(20, 30);

	dphyctrl0 |= DSIDPHYCTRL0_EN_LDO1200;
	rzg2l_mipi_dsi_phy_write(dsi, DSIDPHYCTRL0, dphyctrl0);
	usleep_range(10, 20);

	dphytim0 = DSIDPHYTIM0_TCLK_MISS(0) |
		   DSIDPHYTIM0_T_INIT(dphy_timings->t_init);
	dphytim1 = DSIDPHYTIM1_THS_PREPARE(dphy_timings->ths_prepare) |
		   DSIDPHYTIM1_TCLK_PREPARE(dphy_timings->tclk_prepare) |
		   DSIDPHYTIM1_THS_SETTLE(0) |
		   DSIDPHYTIM1_TCLK_SETTLE(0);
	dphytim2 = DSIDPHYTIM2_TCLK_TRAIL(dphy_timings->tclk_trail) |
		   DSIDPHYTIM2_TCLK_POST(dphy_timings->tclk_post) |
		   DSIDPHYTIM2_TCLK_PRE(dphy_timings->tclk_pre) |
		   DSIDPHYTIM2_TCLK_ZERO(dphy_timings->tclk_zero);
	dphytim3 = DSIDPHYTIM3_TLPX(dphy_timings->tlpx) |
		   DSIDPHYTIM3_THS_EXIT(dphy_timings->ths_exit) |
		   DSIDPHYTIM3_THS_TRAIL(dphy_timings->ths_trail) |
		   DSIDPHYTIM3_THS_ZERO(dphy_timings->ths_zero);

	rzg2l_mipi_dsi_phy_write(dsi, DSIDPHYTIM0, dphytim0);
	rzg2l_mipi_dsi_phy_write(dsi, DSIDPHYTIM1, dphytim1);
	rzg2l_mipi_dsi_phy_write(dsi, DSIDPHYTIM2, dphytim2);
	rzg2l_mipi_dsi_phy_write(dsi, DSIDPHYTIM3, dphytim3);

	ret = reset_control_deassert(dsi->rstc);
	if (ret < 0)
		return ret;

	udelay(1);

	return 0;
}

static void rzg2l_mipi_dsi_dphy_exit(struct rzg2l_mipi_dsi *dsi)
{
	u32 dphyctrl0;

	dphyctrl0 = rzg2l_mipi_dsi_phy_read(dsi, DSIDPHYCTRL0);

	dphyctrl0 &= ~(DSIDPHYCTRL0_EN_LDO1200 | DSIDPHYCTRL0_EN_BGR);
	rzg2l_mipi_dsi_phy_write(dsi, DSIDPHYCTRL0, dphyctrl0);

	reset_control_assert(dsi->rstc);
}

static int rzg2l_dphy_conf_clks(struct rzg2l_mipi_dsi *dsi, unsigned long mode_freq,
				u64 *hsfreq_millihz)
{
	unsigned long vclk_rate;
	unsigned int bpp;

	clk_set_rate(dsi->vclk, mode_freq * KILO);
	vclk_rate = clk_get_rate(dsi->vclk);
	if (vclk_rate != mode_freq * KILO)
		dev_dbg(dsi->dev, "Requested vclk rate %lu, actual %lu mismatch\n",
			mode_freq * KILO, vclk_rate);
	/*
	 * Relationship between hsclk and vclk must follow
	 * vclk * bpp = hsclk * 8 * lanes
	 * where vclk: video clock (Hz)
	 *       bpp: video pixel bit depth
	 *       hsclk: DSI HS Byte clock frequency (Hz)
	 *       lanes: number of data lanes
	 *
	 * hsclk(bit) = hsclk(byte) * 8 = hsfreq
	 */
	bpp = mipi_dsi_pixel_format_to_bpp(dsi->format);
	*hsfreq_millihz = DIV_ROUND_CLOSEST_ULL(mul_u32_u32(vclk_rate, bpp * MILLI),
						dsi->lanes);

	return 0;
}

static int rzg2l_mipi_dsi_startup(struct rzg2l_mipi_dsi *dsi,
				  const struct drm_display_mode *mode)
{
	unsigned long hsfreq;
	u64 hsfreq_millihz;
	u32 txsetr;
	u32 clstptsetr;
	u32 lptrnstsetr;
	u32 clkkpt;
	u32 clkbfht;
	u32 clkstpt;
	u32 golpbkt;
	u32 dsisetr;
	int ret;

	ret = pm_runtime_resume_and_get(dsi->dev);
	if (ret < 0)
		return ret;

	ret = dsi->info->dphy_conf_clks(dsi, mode->clock, &hsfreq_millihz);
	if (ret < 0)
		goto err_phy;

	ret = dsi->info->dphy_init(dsi, hsfreq_millihz);
	if (ret < 0)
		goto err_phy;

	/* Enable Data lanes and Clock lanes */
	txsetr = TXSETR_DLEN | TXSETR_NUMLANEUSE(dsi->lanes - 1) | TXSETR_CLEN;
	rzg2l_mipi_dsi_link_write(dsi, TXSETR, txsetr);

	if (dsi->info->dphy_startup_late_init)
		dsi->info->dphy_startup_late_init(dsi);

	hsfreq = DIV_ROUND_CLOSEST_ULL(hsfreq_millihz, MILLI);
	/*
	 * Global timings characteristic depends on high speed Clock Frequency
	 * Currently MIPI DSI-IF just supports maximum FHD@60 with:
	 * - videoclock = 148.5 (MHz)
	 * - bpp: maximum 24bpp
	 * - data lanes: maximum 4 lanes
	 * Therefore maximum hsclk will be 891 Mbps.
	 */
	if (hsfreq > 445500000) {
		clkkpt = 12;
		clkbfht = 15;
		clkstpt = 48;
		golpbkt = 75;
	} else if (hsfreq > 250000000) {
		clkkpt = 7;
		clkbfht = 8;
		clkstpt = 27;
		golpbkt = 40;
	} else {
		clkkpt = 8;
		clkbfht = 6;
		clkstpt = 24;
		golpbkt = 29;
	}

	clstptsetr = CLSTPTSETR_CLKKPT(clkkpt) | CLSTPTSETR_CLKBFHT(clkbfht) |
		     CLSTPTSETR_CLKSTPT(clkstpt);
	rzg2l_mipi_dsi_link_write(dsi, CLSTPTSETR, clstptsetr);

	lptrnstsetr = LPTRNSTSETR_GOLPBKT(golpbkt);
	rzg2l_mipi_dsi_link_write(dsi, LPTRNSTSETR, lptrnstsetr);

	/*
	 * Increase MRPSZ as the default value of 1 will result in long read
	 * commands payload not being saved to memory.
	 */
	dsisetr = rzg2l_mipi_dsi_link_read(dsi, DSISETR);
	dsisetr &= ~DSISETR_MRPSZ;
	dsisetr |= FIELD_PREP(DSISETR_MRPSZ, RZG2L_DCS_BUF_SIZE);
	rzg2l_mipi_dsi_link_write(dsi, DSISETR, dsisetr);

	return 0;

err_phy:
	dsi->info->dphy_exit(dsi);
	pm_runtime_put(dsi->dev);

	return ret;
}

static void rzg2l_mipi_dsi_stop(struct rzg2l_mipi_dsi *dsi)
{
	dsi->info->dphy_exit(dsi);
	pm_runtime_put(dsi->dev);
}

static void rzg2l_mipi_dsi_set_display_timing(struct rzg2l_mipi_dsi *dsi,
					      const struct drm_display_mode *mode)
{
	u32 vich1ppsetr;
	u32 vich1vssetr;
	u32 vich1vpsetr;
	u32 vich1hssetr;
	u32 vich1hpsetr;
	int dsi_format;
	u32 delay[2];
	u8 index;

	/* Configuration for Pixel Packet */
	dsi_format = mipi_dsi_pixel_format_to_bpp(dsi->format);
	switch (dsi_format) {
	case 24:
		vich1ppsetr = VICH1PPSETR_DT_RGB24;
		break;
	case 18:
		vich1ppsetr = VICH1PPSETR_DT_RGB18;
		break;
	}

	if ((dsi->mode_flags & MIPI_DSI_MODE_VIDEO_SYNC_PULSE) &&
	    !(dsi->mode_flags & MIPI_DSI_MODE_VIDEO_BURST))
		vich1ppsetr |= VICH1PPSETR_TXESYNC_PULSE;

	rzg2l_mipi_dsi_link_write(dsi, VICH1PPSETR, vich1ppsetr);

	/* Configuration for Video Parameters */
	vich1vssetr = VICH1VSSETR_VACTIVE(mode->vdisplay) |
		      VICH1VSSETR_VSA(mode->vsync_end - mode->vsync_start);
	vich1vssetr |= (mode->flags & DRM_MODE_FLAG_PVSYNC) ?
			VICH1VSSETR_VSPOL_HIGH : VICH1VSSETR_VSPOL_LOW;

	vich1vpsetr = VICH1VPSETR_VFP(mode->vsync_start - mode->vdisplay) |
		      VICH1VPSETR_VBP(mode->vtotal - mode->vsync_end);

	vich1hssetr = VICH1HSSETR_HACTIVE(mode->hdisplay) |
		      VICH1HSSETR_HSA(mode->hsync_end - mode->hsync_start);
	vich1hssetr |= (mode->flags & DRM_MODE_FLAG_PHSYNC) ?
			VICH1HSSETR_HSPOL_HIGH : VICH1HSSETR_HSPOL_LOW;

	vich1hpsetr = VICH1HPSETR_HFP(mode->hsync_start - mode->hdisplay) |
		      VICH1HPSETR_HBP(mode->htotal - mode->hsync_end);

	rzg2l_mipi_dsi_link_write(dsi, VICH1VSSETR, vich1vssetr);
	rzg2l_mipi_dsi_link_write(dsi, VICH1VPSETR, vich1vpsetr);
	rzg2l_mipi_dsi_link_write(dsi, VICH1HSSETR, vich1hssetr);
	rzg2l_mipi_dsi_link_write(dsi, VICH1HPSETR, vich1hpsetr);

	/*
	 * Configuration for Delay Value
	 * Delay value based on 2 ranges of video clock.
	 * 74.25MHz is videoclock of HD@60p or FHD@30p
	 */
	if (mode->clock > 74250) {
		delay[0] = 231;
		delay[1] = 216;
	} else {
		delay[0] = 220;
		delay[1] = 212;
	}

	if (dsi->mode_flags & MIPI_DSI_CLOCK_NON_CONTINUOUS)
		index = 0;
	else
		index = 1;

	rzg2l_mipi_dsi_link_write(dsi, VICH1SET1R,
				  VICH1SET1R_DLY(delay[index]));
}

static int rzg2l_mipi_dsi_start_hs_clock(struct rzg2l_mipi_dsi *dsi)
{
	bool is_clk_cont;
	u32 hsclksetr;
	u32 status;
	int ret;

	is_clk_cont = !(dsi->mode_flags & MIPI_DSI_CLOCK_NON_CONTINUOUS);

	/* Start HS clock */
	hsclksetr = HSCLKSETR_HSCLKRUN_HS | (is_clk_cont ?
					     HSCLKSETR_HSCLKMODE_CONT :
					     HSCLKSETR_HSCLKMODE_NON_CONT);
	rzg2l_mipi_dsi_link_write(dsi, HSCLKSETR, hsclksetr);

	if (is_clk_cont) {
		ret = read_poll_timeout(rzg2l_mipi_dsi_link_read, status,
					status & PLSR_CLLP2HS,
					2000, 20000, false, dsi, PLSR);
		if (ret < 0) {
			dev_err(dsi->dev, "failed to start HS clock\n");
			return ret;
		}
	}

	dev_dbg(dsi->dev, "Start High Speed Clock with %s clock mode",
		is_clk_cont ? "continuous" : "non-continuous");

	return 0;
}

static int rzg2l_mipi_dsi_stop_hs_clock(struct rzg2l_mipi_dsi *dsi)
{
	bool is_clk_cont;
	u32 status;
	int ret;

	is_clk_cont = !(dsi->mode_flags & MIPI_DSI_CLOCK_NON_CONTINUOUS);

	/* Stop HS clock */
	rzg2l_mipi_dsi_link_write(dsi, HSCLKSETR,
				  is_clk_cont ? HSCLKSETR_HSCLKMODE_CONT :
				  HSCLKSETR_HSCLKMODE_NON_CONT);

	if (is_clk_cont) {
		ret = read_poll_timeout(rzg2l_mipi_dsi_link_read, status,
					status & PLSR_CLHS2LP,
					2000, 20000, false, dsi, PLSR);
		if (ret < 0) {
			dev_err(dsi->dev, "failed to stop HS clock\n");
			return ret;
		}
	}

	return 0;
}

static int rzg2l_mipi_dsi_start_video(struct rzg2l_mipi_dsi *dsi)
{
	u32 vich1set0r;
	u32 status;
	int ret;

	/* Configuration for Blanking sequence and start video input */
	vich1set0r = VICH1SET0R_HFPNOLP | VICH1SET0R_HBPNOLP |
		     VICH1SET0R_HSANOLP | VICH1SET0R_VSTART;
	rzg2l_mipi_dsi_link_write(dsi, VICH1SET0R, vich1set0r);

	ret = read_poll_timeout(rzg2l_mipi_dsi_link_read, status,
				status & VICH1SR_VIRDY,
				2000, 20000, false, dsi, VICH1SR);
	if (ret < 0)
		dev_err(dsi->dev, "Failed to start video signal input\n");

	return ret;
}

static int rzg2l_mipi_dsi_stop_video(struct rzg2l_mipi_dsi *dsi)
{
	u32 status;
	int ret;

	rzg2l_mipi_dsi_link_write(dsi, VICH1SET0R, VICH1SET0R_VSTPAFT);
	ret = read_poll_timeout(rzg2l_mipi_dsi_link_read, status,
				(status & VICH1SR_STOP) && (!(status & VICH1SR_RUNNING)),
				2000, 20000, false, dsi, VICH1SR);
	if (ret < 0)
		goto err;

	ret = read_poll_timeout(rzg2l_mipi_dsi_link_read, status,
				!(status & LINKSR_HSBUSY),
				2000, 20000, false, dsi, LINKSR);
	if (ret < 0)
		goto err;

	return 0;

err:
	dev_err(dsi->dev, "Failed to stop video signal input\n");
	return ret;
}

/* -----------------------------------------------------------------------------
 * Bridge
 */

static int rzg2l_mipi_dsi_attach(struct drm_bridge *bridge,
				 struct drm_encoder *encoder,
				 enum drm_bridge_attach_flags flags)
{
	struct rzg2l_mipi_dsi *dsi = bridge_to_rzg2l_mipi_dsi(bridge);

	return drm_bridge_attach(encoder, dsi->next_bridge, bridge,
				 flags);
}

static void rzg2l_mipi_dsi_atomic_pre_enable(struct drm_bridge *bridge,
					     struct drm_atomic_state *state)
{
	struct rzg2l_mipi_dsi *dsi = bridge_to_rzg2l_mipi_dsi(bridge);
	const struct drm_display_mode *mode;
	struct drm_connector *connector;
	struct drm_crtc *crtc;
	int ret;

	connector = drm_atomic_get_new_connector_for_encoder(state, bridge->encoder);
	crtc = drm_atomic_get_new_connector_state(state, connector)->crtc;
	mode = &drm_atomic_get_new_crtc_state(state, crtc)->adjusted_mode;

	ret = rzg2l_mipi_dsi_startup(dsi, mode);
	if (ret < 0)
		return;

	rzg2l_mipi_dsi_set_display_timing(dsi, mode);
}

static void rzg2l_mipi_dsi_atomic_enable(struct drm_bridge *bridge,
					 struct drm_atomic_state *state)
{
	struct rzg2l_mipi_dsi *dsi = bridge_to_rzg2l_mipi_dsi(bridge);
	int ret;

	ret = rzg2l_mipi_dsi_start_hs_clock(dsi);
	if (ret < 0)
		goto err_stop;

	ret = rzg2l_mipi_dsi_start_video(dsi);
	if (ret < 0)
		goto err_stop_clock;

	return;

err_stop_clock:
	rzg2l_mipi_dsi_stop_hs_clock(dsi);
err_stop:
	rzg2l_mipi_dsi_stop(dsi);
}

static void rzg2l_mipi_dsi_atomic_disable(struct drm_bridge *bridge,
					  struct drm_atomic_state *state)
{
	struct rzg2l_mipi_dsi *dsi = bridge_to_rzg2l_mipi_dsi(bridge);

	rzg2l_mipi_dsi_stop_video(dsi);
	rzg2l_mipi_dsi_stop_hs_clock(dsi);
	rzg2l_mipi_dsi_stop(dsi);
}

static enum drm_mode_status
rzg2l_mipi_dsi_bridge_mode_valid(struct drm_bridge *bridge,
				 const struct drm_display_info *info,
				 const struct drm_display_mode *mode)
{
	struct rzg2l_mipi_dsi *dsi = bridge_to_rzg2l_mipi_dsi(bridge);

	if (mode->clock > dsi->info->max_dclk)
		return MODE_CLOCK_HIGH;

	if (mode->clock < dsi->info->min_dclk)
		return MODE_CLOCK_LOW;

	if (dsi->info->dphy_mode_clk_check) {
		enum drm_mode_status status;

		status = dsi->info->dphy_mode_clk_check(dsi, mode->clock);
		if (status != MODE_OK)
			return status;
	}

	return MODE_OK;
}

static const struct drm_bridge_funcs rzg2l_mipi_dsi_bridge_ops = {
	.attach = rzg2l_mipi_dsi_attach,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset = drm_atomic_helper_bridge_reset,
	.atomic_pre_enable = rzg2l_mipi_dsi_atomic_pre_enable,
	.atomic_enable = rzg2l_mipi_dsi_atomic_enable,
	.atomic_disable = rzg2l_mipi_dsi_atomic_disable,
	.mode_valid = rzg2l_mipi_dsi_bridge_mode_valid,
};

/* -----------------------------------------------------------------------------
 * Host setting
 */

static int rzg2l_mipi_dsi_host_attach(struct mipi_dsi_host *host,
				      struct mipi_dsi_device *device)
{
	struct rzg2l_mipi_dsi *dsi = host_to_rzg2l_mipi_dsi(host);
	int ret;

	if (device->lanes > dsi->num_data_lanes) {
		dev_err(dsi->dev,
			"Number of lines of device (%u) exceeds host (%u)\n",
			device->lanes, dsi->num_data_lanes);
		return -EINVAL;
	}

	switch (mipi_dsi_pixel_format_to_bpp(device->format)) {
	case 24:
		break;
	case 18:
		break;
	case 16:
		if (!(dsi->info->features & RZ_MIPI_DSI_FEATURE_16BPP)) {
			dev_err(dsi->dev, "Unsupported format 0x%04x\n",
				device->format);
			return -EINVAL;
		}
		break;
	default:
		dev_err(dsi->dev, "Unsupported format 0x%04x\n", device->format);
		return -EINVAL;
	}

	dsi->lanes = device->lanes;
	dsi->format = device->format;
	dsi->mode_flags = device->mode_flags;

	dsi->next_bridge = devm_drm_of_get_bridge(dsi->dev, dsi->dev->of_node,
						  1, 0);
	if (IS_ERR(dsi->next_bridge)) {
		ret = PTR_ERR(dsi->next_bridge);
		dev_err(dsi->dev, "failed to get next bridge: %d\n", ret);
		return ret;
	}

	drm_bridge_add(&dsi->bridge);

	return 0;
}

static int rzg2l_mipi_dsi_host_detach(struct mipi_dsi_host *host,
				      struct mipi_dsi_device *device)
{
	struct rzg2l_mipi_dsi *dsi = host_to_rzg2l_mipi_dsi(host);

	drm_bridge_remove(&dsi->bridge);

	return 0;
}

static ssize_t rzg2l_mipi_dsi_read_response(struct rzg2l_mipi_dsi *dsi,
					    const struct mipi_dsi_msg *msg)
{
	u8 *msg_rx = msg->rx_buf;
	u8 datatype;
	u32 result;
	u16 size;

	result = rzg2l_mipi_dsi_link_read(dsi, RXRSS0R);
	if (result & RXRSS0R_RXPKTDFAIL) {
		dev_err(dsi->dev, "packet rx data did not save correctly\n");
		return -EPROTO;
	}

	if (result & RXRSS0R_RXFAIL) {
		dev_err(dsi->dev, "packet rx failure\n");
		return -EPROTO;
	}

	if (!(result & RXRSS0R_RXSUC))
		return -EPROTO;

	datatype = FIELD_GET(RXRSS0R_DT, result);

	switch (datatype) {
	case 0:
		dev_dbg(dsi->dev, "ACK\n");
		return 0;
	case MIPI_DSI_RX_END_OF_TRANSMISSION:
		dev_dbg(dsi->dev, "EoTp\n");
		return 0;
	case MIPI_DSI_RX_ACKNOWLEDGE_AND_ERROR_REPORT:
		dev_dbg(dsi->dev, "Acknowledge and error report: $%02x%02x\n",
			(u8)FIELD_GET(RXRSS0R_DATA1, result),
			(u8)FIELD_GET(RXRSS0R_DATA0, result));
		return 0;
	case MIPI_DSI_RX_DCS_SHORT_READ_RESPONSE_1BYTE:
	case MIPI_DSI_RX_GENERIC_SHORT_READ_RESPONSE_1BYTE:
		msg_rx[0] = FIELD_GET(RXRSS0R_DATA0, result);
		return 1;
	case MIPI_DSI_RX_DCS_SHORT_READ_RESPONSE_2BYTE:
	case MIPI_DSI_RX_GENERIC_SHORT_READ_RESPONSE_2BYTE:
		msg_rx[0] = FIELD_GET(RXRSS0R_DATA0, result);
		msg_rx[1] = FIELD_GET(RXRSS0R_DATA1, result);
		return 2;
	case MIPI_DSI_RX_GENERIC_LONG_READ_RESPONSE:
	case MIPI_DSI_RX_DCS_LONG_READ_RESPONSE:
		size = FIELD_GET(RXRSS0R_WC, result);

		if (size > msg->rx_len) {
			dev_err(dsi->dev, "rx buffer too small");
			return -ENOSPC;
		}

		memcpy(msg_rx, dsi->dcs_buf_virt, size);
		return size;
	default:
		dev_err(dsi->dev, "unhandled response type: %02x\n", datatype);
		return -EPROTO;
	}
}

static ssize_t rzg2l_mipi_dsi_host_transfer(struct mipi_dsi_host *host,
					    const struct mipi_dsi_msg *msg)
{
	struct rzg2l_mipi_dsi *dsi = host_to_rzg2l_mipi_dsi(host);
	struct mipi_dsi_packet packet;
	bool need_bta;
	u32 value;
	int ret;

	ret = mipi_dsi_create_packet(&packet, msg);
	if (ret < 0)
		return ret;

	/* Terminate operation after this descriptor is finished */
	value = SQCH0DSC0AR_NXACT_TERM;

	if (msg->flags & MIPI_DSI_MSG_REQ_ACK) {
		need_bta = true; /* Message with explicitly requested ACK */
		value |= FIELD_PREP(SQCH0DSC0AR_BTA, SQCH0DSC0AR_BTA_NON_READ);
	} else if (msg->rx_buf && msg->rx_len > 0) {
		need_bta = true; /* Read request */
		value |= FIELD_PREP(SQCH0DSC0AR_BTA, SQCH0DSC0AR_BTA_READ);
	} else {
		need_bta = false;
		value |= FIELD_PREP(SQCH0DSC0AR_BTA, SQCH0DSC0AR_BTA_NONE);
	}

	/* Set transmission speed */
	if (msg->flags & MIPI_DSI_MSG_USE_LPM)
		value |= SQCH0DSC0AR_SPD_LOW;
	else
		value |= SQCH0DSC0AR_SPD_HIGH;

	/* Write TX packet header */
	value |= FIELD_PREP(SQCH0DSC0AR_DT, packet.header[0]) |
		FIELD_PREP(SQCH0DSC0AR_DATA0, packet.header[1]) |
		FIELD_PREP(SQCH0DSC0AR_DATA1, packet.header[2]);

	if (mipi_dsi_packet_format_is_long(msg->type)) {
		value |= SQCH0DSC0AR_FMT_LONG;

		if (packet.payload_length > RZG2L_DCS_BUF_SIZE) {
			dev_err(dsi->dev, "Packet Tx payload size (%d) too large",
				(unsigned int)packet.payload_length);
			return -ENOSPC;
		}

		/* Copy TX packet payload data to memory space */
		memcpy(dsi->dcs_buf_virt, packet.payload, packet.payload_length);
	} else {
		value |= SQCH0DSC0AR_FMT_SHORT;
	}

	rzg2l_mipi_dsi_link_write(dsi, SQCH0DSC0AR, value);

	/*
	 * Write: specify payload data source location, only used for
	 *        long packet.
	 * Read:  specify payload data storage location of response
	 *        packet. Note: a read packet is always a short packet.
	 *        If the response packet is a short packet or a long packet
	 *        with WC = 0 (no payload), DTSEL is meaningless.
	 */
	rzg2l_mipi_dsi_link_write(dsi, SQCH0DSC0BR, SQCH0DSC0BR_DTSEL_MEM_SPACE);

	/*
	 * Set SQCHxSR.AACTFIN bit when descriptor actions are finished.
	 * Read: set Rx result save slot number to 0 (ACTCODE).
	 */
	rzg2l_mipi_dsi_link_write(dsi, SQCH0DSC0CR, SQCH0DSC0CR_FINACT);

	/* Set rx/tx payload data address, only relevant for long packet. */
	rzg2l_mipi_dsi_link_write(dsi, SQCH0DSC0DR, (u32)dsi->dcs_buf_phys);

	/* Start sequence 0 operation */
	value = rzg2l_mipi_dsi_link_read(dsi, SQCH0SET0R);
	value |= SQCH0SET0R_START;
	rzg2l_mipi_dsi_link_write(dsi, SQCH0SET0R, value);

	/* Wait for operation to finish */
	ret = read_poll_timeout(rzg2l_mipi_dsi_link_read,
				value, value & SQCH0SR_ADESFIN,
				2000, 20000, false, dsi, SQCH0SR);
	if (ret == 0) {
		/* Success: clear status bit */
		rzg2l_mipi_dsi_link_write(dsi, SQCH0SCR, SQCH0SCR_ADESFIN);

		if (need_bta)
			ret = rzg2l_mipi_dsi_read_response(dsi, msg);
		else
			ret = packet.payload_length;
	}

	return ret;
}

static const struct mipi_dsi_host_ops rzg2l_mipi_dsi_host_ops = {
	.attach = rzg2l_mipi_dsi_host_attach,
	.detach = rzg2l_mipi_dsi_host_detach,
	.transfer = rzg2l_mipi_dsi_host_transfer,
};

/* -----------------------------------------------------------------------------
 * Power Management
 */

static int rzg2l_mipi_pm_runtime_suspend(struct device *dev)
{
	struct rzg2l_mipi_dsi *dsi = dev_get_drvdata(dev);

	reset_control_assert(dsi->prstc);
	reset_control_assert(dsi->arstc);

	return 0;
}

static int rzg2l_mipi_pm_runtime_resume(struct device *dev)
{
	struct rzg2l_mipi_dsi *dsi = dev_get_drvdata(dev);
	int ret;

	ret = reset_control_deassert(dsi->arstc);
	if (ret < 0)
		return ret;

	ret = reset_control_deassert(dsi->prstc);
	if (ret < 0)
		reset_control_assert(dsi->arstc);

	return ret;
}

static const struct dev_pm_ops rzg2l_mipi_pm_ops = {
	RUNTIME_PM_OPS(rzg2l_mipi_pm_runtime_suspend, rzg2l_mipi_pm_runtime_resume, NULL)
};

/* -----------------------------------------------------------------------------
 * Probe & Remove
 */

static int rzg2l_mipi_dsi_probe(struct platform_device *pdev)
{
	unsigned int num_data_lanes;
	struct rzg2l_mipi_dsi *dsi;
	u32 txsetr;
	int ret;

	dsi = devm_drm_bridge_alloc(&pdev->dev, struct rzg2l_mipi_dsi, bridge,
				    &rzg2l_mipi_dsi_bridge_ops);
	if (IS_ERR(dsi))
		return PTR_ERR(dsi);

	platform_set_drvdata(pdev, dsi);
	dsi->dev = &pdev->dev;

	dsi->info = of_device_get_match_data(&pdev->dev);

	ret = drm_of_get_data_lanes_count_ep(dsi->dev->of_node, 1, 0, 1, 4);
	if (ret < 0)
		return dev_err_probe(dsi->dev, ret,
				     "missing or invalid data-lanes property\n");

	num_data_lanes = ret;

	dsi->mmio = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(dsi->mmio))
		return PTR_ERR(dsi->mmio);

	dsi->vclk = devm_clk_get(dsi->dev, "vclk");
	if (IS_ERR(dsi->vclk))
		return PTR_ERR(dsi->vclk);

	dsi->rstc = devm_reset_control_get_optional_exclusive(dsi->dev, "rst");
	if (IS_ERR(dsi->rstc))
		return dev_err_probe(dsi->dev, PTR_ERR(dsi->rstc),
				     "failed to get rst\n");

	dsi->arstc = devm_reset_control_get_exclusive(dsi->dev, "arst");
	if (IS_ERR(dsi->arstc))
		return dev_err_probe(&pdev->dev, PTR_ERR(dsi->arstc),
				     "failed to get arst\n");

	dsi->prstc = devm_reset_control_get_exclusive(dsi->dev, "prst");
	if (IS_ERR(dsi->prstc))
		return dev_err_probe(dsi->dev, PTR_ERR(dsi->prstc),
				     "failed to get prst\n");

	platform_set_drvdata(pdev, dsi);

	pm_runtime_enable(dsi->dev);

	ret = pm_runtime_resume_and_get(dsi->dev);
	if (ret < 0)
		goto err_pm_disable;

	/*
	 * TXSETR register can be read only after DPHY init. But during probe
	 * mode->clock and format are not available. So initialize DPHY with
	 * timing parameters for 80Mbps.
	 */
	ret = dsi->info->dphy_init(dsi, 80000000ULL * MILLI);
	if (ret < 0)
		goto err_phy;

	txsetr = rzg2l_mipi_dsi_link_read(dsi, TXSETR);
	dsi->num_data_lanes = min(((txsetr >> 16) & 3) + 1, num_data_lanes);
	dsi->info->dphy_exit(dsi);
	pm_runtime_put(dsi->dev);

	/* Initialize the DRM bridge. */
	dsi->bridge.of_node = dsi->dev->of_node;

	/* Init host device */
	dsi->host.dev = dsi->dev;
	dsi->host.ops = &rzg2l_mipi_dsi_host_ops;
	ret = mipi_dsi_host_register(&dsi->host);
	if (ret < 0)
		goto err_pm_disable;

	dsi->dcs_buf_virt = dma_alloc_coherent(dsi->host.dev, RZG2L_DCS_BUF_SIZE,
					       &dsi->dcs_buf_phys, GFP_KERNEL);
	if (!dsi->dcs_buf_virt)
		return -ENOMEM;

	return 0;

err_phy:
	dsi->info->dphy_exit(dsi);
	pm_runtime_put(dsi->dev);
err_pm_disable:
	pm_runtime_disable(dsi->dev);
	return ret;
}

static void rzg2l_mipi_dsi_remove(struct platform_device *pdev)
{
	struct rzg2l_mipi_dsi *dsi = platform_get_drvdata(pdev);

	dma_free_coherent(dsi->host.dev, RZG2L_DCS_BUF_SIZE, dsi->dcs_buf_virt,
			  dsi->dcs_buf_phys);
	mipi_dsi_host_unregister(&dsi->host);
	pm_runtime_disable(&pdev->dev);
}

static const struct rzg2l_mipi_dsi_hw_info rzg2l_mipi_dsi_info = {
	.dphy_init = rzg2l_mipi_dsi_dphy_init,
	.dphy_exit = rzg2l_mipi_dsi_dphy_exit,
	.dphy_conf_clks = rzg2l_dphy_conf_clks,
	.link_reg_offset = 0x10000,
	.min_dclk = 5803,
	.max_dclk = 148500,
};

static const struct of_device_id rzg2l_mipi_dsi_of_table[] = {
	{ .compatible = "renesas,rzg2l-mipi-dsi", .data = &rzg2l_mipi_dsi_info, },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, rzg2l_mipi_dsi_of_table);

static struct platform_driver rzg2l_mipi_dsi_platform_driver = {
	.probe	= rzg2l_mipi_dsi_probe,
	.remove = rzg2l_mipi_dsi_remove,
	.driver	= {
		.name = "rzg2l-mipi-dsi",
		.pm = pm_ptr(&rzg2l_mipi_pm_ops),
		.of_match_table = rzg2l_mipi_dsi_of_table,
	},
};

module_platform_driver(rzg2l_mipi_dsi_platform_driver);

MODULE_AUTHOR("Biju Das <biju.das.jz@bp.renesas.com>");
MODULE_DESCRIPTION("Renesas RZ/G2L MIPI DSI Encoder Driver");
MODULE_LICENSE("GPL");
