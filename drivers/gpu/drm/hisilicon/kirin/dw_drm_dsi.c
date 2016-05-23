/*
 * DesignWare MIPI DSI Host Controller v1.02 driver
 *
 * Copyright (c) 2016 Linaro Limited.
 * Copyright (c) 2014-2016 Hisilicon Limited.
 *
 * Author:
 *	Xinliang Liu <z.liuxinliang@hisilicon.com>
 *	Xinliang Liu <xinliang.liu@linaro.org>
 *	Xinwei Kong <kong.kongxinwei@hisilicon.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/of_graph.h>

#include <drm/drm_of.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_encoder_slave.h>
#include <drm/drm_atomic_helper.h>

#include "dw_dsi_reg.h"

#define MAX_TX_ESC_CLK		10
#define ROUND(x, y)		((x) / (y) + \
				((x) % (y) * 10 / (y) >= 5 ? 1 : 0))
#define PHY_REF_CLK_RATE	19200000
#define PHY_REF_CLK_PERIOD_PS	(1000000000 / (PHY_REF_CLK_RATE / 1000))

#define encoder_to_dsi(encoder) \
	container_of(encoder, struct dw_dsi, encoder)
#define host_to_dsi(host) \
	container_of(host, struct dw_dsi, host)

struct mipi_phy_params {
	u32 clk_t_lpx;
	u32 clk_t_hs_prepare;
	u32 clk_t_hs_zero;
	u32 clk_t_hs_trial;
	u32 clk_t_wakeup;
	u32 data_t_lpx;
	u32 data_t_hs_prepare;
	u32 data_t_hs_zero;
	u32 data_t_hs_trial;
	u32 data_t_ta_go;
	u32 data_t_ta_get;
	u32 data_t_wakeup;
	u32 hstx_ckg_sel;
	u32 pll_fbd_div5f;
	u32 pll_fbd_div1f;
	u32 pll_fbd_2p;
	u32 pll_enbwt;
	u32 pll_fbd_p;
	u32 pll_fbd_s;
	u32 pll_pre_div1p;
	u32 pll_pre_p;
	u32 pll_vco_750M;
	u32 pll_lpf_rs;
	u32 pll_lpf_cs;
	u32 clklp2hs_time;
	u32 clkhs2lp_time;
	u32 lp2hs_time;
	u32 hs2lp_time;
	u32 clk_to_data_delay;
	u32 data_to_clk_delay;
	u32 lane_byte_clk_kHz;
	u32 clk_division;
};

struct dsi_hw_ctx {
	void __iomem *base;
	struct clk *pclk;
};

struct dw_dsi {
	struct drm_encoder encoder;
	struct drm_bridge *bridge;
	struct mipi_dsi_host host;
	struct drm_display_mode cur_mode;
	struct dsi_hw_ctx *ctx;
	struct mipi_phy_params phy;

	u32 lanes;
	enum mipi_dsi_pixel_format format;
	unsigned long mode_flags;
	bool enable;
};

struct dsi_data {
	struct dw_dsi dsi;
	struct dsi_hw_ctx ctx;
};

struct dsi_phy_range {
	u32 min_range_kHz;
	u32 max_range_kHz;
	u32 pll_vco_750M;
	u32 hstx_ckg_sel;
};

static const struct dsi_phy_range dphy_range_info[] = {
	{   46875,    62500,   1,    7 },
	{   62500,    93750,   0,    7 },
	{   93750,   125000,   1,    6 },
	{  125000,   187500,   0,    6 },
	{  187500,   250000,   1,    5 },
	{  250000,   375000,   0,    5 },
	{  375000,   500000,   1,    4 },
	{  500000,   750000,   0,    4 },
	{  750000,  1000000,   1,    0 },
	{ 1000000,  1500000,   0,    0 }
};

static u32 dsi_calc_phy_rate(u32 req_kHz, struct mipi_phy_params *phy)
{
	u32 ref_clk_ps = PHY_REF_CLK_PERIOD_PS;
	u32 tmp_kHz = req_kHz;
	u32 i = 0;
	u32 q_pll = 1;
	u32 m_pll = 0;
	u32 n_pll = 0;
	u32 r_pll = 1;
	u32 m_n = 0;
	u32 m_n_int = 0;
	u32 f_kHz = 0;
	u64 temp;

	/*
	 * Find a rate >= req_kHz.
	 */
	do {
		f_kHz = tmp_kHz;

		for (i = 0; i < ARRAY_SIZE(dphy_range_info); i++)
			if (f_kHz >= dphy_range_info[i].min_range_kHz &&
			    f_kHz <= dphy_range_info[i].max_range_kHz)
				break;

		if (i == ARRAY_SIZE(dphy_range_info)) {
			DRM_ERROR("%dkHz out of range\n", f_kHz);
			return 0;
		}

		phy->pll_vco_750M = dphy_range_info[i].pll_vco_750M;
		phy->hstx_ckg_sel = dphy_range_info[i].hstx_ckg_sel;

		if (phy->hstx_ckg_sel <= 7 &&
		    phy->hstx_ckg_sel >= 4)
			q_pll = 0x10 >> (7 - phy->hstx_ckg_sel);

		temp = f_kHz * (u64)q_pll * (u64)ref_clk_ps;
		m_n_int = temp / (u64)1000000000;
		m_n = (temp % (u64)1000000000) / (u64)100000000;

		if (m_n_int % 2 == 0) {
			if (m_n * 6 >= 50) {
				n_pll = 2;
				m_pll = (m_n_int + 1) * n_pll;
			} else if (m_n * 6 >= 30) {
				n_pll = 3;
				m_pll = m_n_int * n_pll + 2;
			} else {
				n_pll = 1;
				m_pll = m_n_int * n_pll;
			}
		} else {
			if (m_n * 6 >= 50) {
				n_pll = 1;
				m_pll = (m_n_int + 1) * n_pll;
			} else if (m_n * 6 >= 30) {
				n_pll = 1;
				m_pll = (m_n_int + 1) * n_pll;
			} else if (m_n * 6 >= 10) {
				n_pll = 3;
				m_pll = m_n_int * n_pll + 1;
			} else {
				n_pll = 2;
				m_pll = m_n_int * n_pll;
			}
		}

		if (n_pll == 1) {
			phy->pll_fbd_p = 0;
			phy->pll_pre_div1p = 1;
		} else {
			phy->pll_fbd_p = n_pll;
			phy->pll_pre_div1p = 0;
		}

		if (phy->pll_fbd_2p <= 7 && phy->pll_fbd_2p >= 4)
			r_pll = 0x10 >> (7 - phy->pll_fbd_2p);

		if (m_pll == 2) {
			phy->pll_pre_p = 0;
			phy->pll_fbd_s = 0;
			phy->pll_fbd_div1f = 0;
			phy->pll_fbd_div5f = 1;
		} else if (m_pll >= 2 * 2 * r_pll && m_pll <= 2 * 4 * r_pll) {
			phy->pll_pre_p = m_pll / (2 * r_pll);
			phy->pll_fbd_s = 0;
			phy->pll_fbd_div1f = 1;
			phy->pll_fbd_div5f = 0;
		} else if (m_pll >= 2 * 5 * r_pll && m_pll <= 2 * 150 * r_pll) {
			if (((m_pll / (2 * r_pll)) % 2) == 0) {
				phy->pll_pre_p =
					(m_pll / (2 * r_pll)) / 2 - 1;
				phy->pll_fbd_s =
					(m_pll / (2 * r_pll)) % 2 + 2;
			} else {
				phy->pll_pre_p =
					(m_pll / (2 * r_pll)) / 2;
				phy->pll_fbd_s =
					(m_pll / (2 * r_pll)) % 2;
			}
			phy->pll_fbd_div1f = 0;
			phy->pll_fbd_div5f = 0;
		} else {
			phy->pll_pre_p = 0;
			phy->pll_fbd_s = 0;
			phy->pll_fbd_div1f = 0;
			phy->pll_fbd_div5f = 1;
		}

		f_kHz = (u64)1000000000 * (u64)m_pll /
			((u64)ref_clk_ps * (u64)n_pll * (u64)q_pll);

		if (f_kHz >= req_kHz)
			break;

		tmp_kHz += 10;

	} while (true);

	return f_kHz;
}

static void dsi_get_phy_params(u32 phy_req_kHz,
			       struct mipi_phy_params *phy)
{
	u32 ref_clk_ps = PHY_REF_CLK_PERIOD_PS;
	u32 phy_rate_kHz;
	u32 ui;

	memset(phy, 0, sizeof(*phy));

	phy_rate_kHz = dsi_calc_phy_rate(phy_req_kHz, phy);
	if (!phy_rate_kHz)
		return;

	ui = 1000000 / phy_rate_kHz;

	phy->clk_t_lpx = ROUND(50, 8 * ui);
	phy->clk_t_hs_prepare = ROUND(133, 16 * ui) - 1;

	phy->clk_t_hs_zero = ROUND(262, 8 * ui);
	phy->clk_t_hs_trial = 2 * (ROUND(60, 8 * ui) - 1);
	phy->clk_t_wakeup = ROUND(1000000, (ref_clk_ps / 1000) - 1);
	if (phy->clk_t_wakeup > 0xff)
		phy->clk_t_wakeup = 0xff;
	phy->data_t_wakeup = phy->clk_t_wakeup;
	phy->data_t_lpx = phy->clk_t_lpx;
	phy->data_t_hs_prepare = ROUND(125 + 10 * ui, 16 * ui) - 1;
	phy->data_t_hs_zero = ROUND(105 + 6 * ui, 8 * ui);
	phy->data_t_hs_trial = 2 * (ROUND(60 + 4 * ui, 8 * ui) - 1);
	phy->data_t_ta_go = 3;
	phy->data_t_ta_get = 4;

	phy->pll_enbwt = 1;
	phy->clklp2hs_time = ROUND(407, 8 * ui) + 12;
	phy->clkhs2lp_time = ROUND(105 + 12 * ui, 8 * ui);
	phy->lp2hs_time = ROUND(240 + 12 * ui, 8 * ui) + 1;
	phy->hs2lp_time = phy->clkhs2lp_time;
	phy->clk_to_data_delay = 1 + phy->clklp2hs_time;
	phy->data_to_clk_delay = ROUND(60 + 52 * ui, 8 * ui) +
				phy->clkhs2lp_time;

	phy->lane_byte_clk_kHz = phy_rate_kHz / 8;
	phy->clk_division =
		DIV_ROUND_UP(phy->lane_byte_clk_kHz, MAX_TX_ESC_CLK);
}

static u32 dsi_get_dpi_color_coding(enum mipi_dsi_pixel_format format)
{
	u32 val;

	/*
	 * TODO: only support RGB888 now, to support more
	 */
	switch (format) {
	case MIPI_DSI_FMT_RGB888:
		val = DSI_24BITS_1;
		break;
	default:
		val = DSI_24BITS_1;
		break;
	}

	return val;
}

/*
 * dsi phy reg write function
 */
static void dsi_phy_tst_set(void __iomem *base, u32 reg, u32 val)
{
	u32 reg_write = 0x10000 + reg;

	/*
	 * latch reg first
	 */
	writel(reg_write, base + PHY_TST_CTRL1);
	writel(0x02, base + PHY_TST_CTRL0);
	writel(0x00, base + PHY_TST_CTRL0);

	/*
	 * then latch value
	 */
	writel(val, base + PHY_TST_CTRL1);
	writel(0x02, base + PHY_TST_CTRL0);
	writel(0x00, base + PHY_TST_CTRL0);
}

static void dsi_set_phy_timer(void __iomem *base,
			      struct mipi_phy_params *phy,
			      u32 lanes)
{
	u32 val;

	/*
	 * Set lane value and phy stop wait time.
	 */
	val = (lanes - 1) | (PHY_STOP_WAIT_TIME << 8);
	writel(val, base + PHY_IF_CFG);

	/*
	 * Set phy clk division.
	 */
	val = readl(base + CLKMGR_CFG) | phy->clk_division;
	writel(val, base + CLKMGR_CFG);

	/*
	 * Set lp and hs switching params.
	 */
	dw_update_bits(base + PHY_TMR_CFG, 24, MASK(8), phy->hs2lp_time);
	dw_update_bits(base + PHY_TMR_CFG, 16, MASK(8), phy->lp2hs_time);
	dw_update_bits(base + PHY_TMR_LPCLK_CFG, 16, MASK(10),
		       phy->clkhs2lp_time);
	dw_update_bits(base + PHY_TMR_LPCLK_CFG, 0, MASK(10),
		       phy->clklp2hs_time);
	dw_update_bits(base + CLK_DATA_TMR_CFG, 8, MASK(8),
		       phy->data_to_clk_delay);
	dw_update_bits(base + CLK_DATA_TMR_CFG, 0, MASK(8),
		       phy->clk_to_data_delay);
}

static void dsi_set_mipi_phy(void __iomem *base,
			     struct mipi_phy_params *phy,
			     u32 lanes)
{
	u32 delay_count;
	u32 val;
	u32 i;

	/* phy timer setting */
	dsi_set_phy_timer(base, phy, lanes);

	/*
	 * Reset to clean up phy tst params.
	 */
	writel(0, base + PHY_RSTZ);
	writel(0, base + PHY_TST_CTRL0);
	writel(1, base + PHY_TST_CTRL0);
	writel(0, base + PHY_TST_CTRL0);

	/*
	 * Clock lane timing control setting: TLPX, THS-PREPARE,
	 * THS-ZERO, THS-TRAIL, TWAKEUP.
	 */
	dsi_phy_tst_set(base, CLK_TLPX, phy->clk_t_lpx);
	dsi_phy_tst_set(base, CLK_THS_PREPARE, phy->clk_t_hs_prepare);
	dsi_phy_tst_set(base, CLK_THS_ZERO, phy->clk_t_hs_zero);
	dsi_phy_tst_set(base, CLK_THS_TRAIL, phy->clk_t_hs_trial);
	dsi_phy_tst_set(base, CLK_TWAKEUP, phy->clk_t_wakeup);

	/*
	 * Data lane timing control setting: TLPX, THS-PREPARE,
	 * THS-ZERO, THS-TRAIL, TTA-GO, TTA-GET, TWAKEUP.
	 */
	for (i = 0; i < lanes; i++) {
		dsi_phy_tst_set(base, DATA_TLPX(i), phy->data_t_lpx);
		dsi_phy_tst_set(base, DATA_THS_PREPARE(i),
				phy->data_t_hs_prepare);
		dsi_phy_tst_set(base, DATA_THS_ZERO(i), phy->data_t_hs_zero);
		dsi_phy_tst_set(base, DATA_THS_TRAIL(i), phy->data_t_hs_trial);
		dsi_phy_tst_set(base, DATA_TTA_GO(i), phy->data_t_ta_go);
		dsi_phy_tst_set(base, DATA_TTA_GET(i), phy->data_t_ta_get);
		dsi_phy_tst_set(base, DATA_TWAKEUP(i), phy->data_t_wakeup);
	}

	/*
	 * physical configuration: I, pll I, pll II, pll III,
	 * pll IV, pll V.
	 */
	dsi_phy_tst_set(base, PHY_CFG_I, phy->hstx_ckg_sel);
	val = (phy->pll_fbd_div5f << 5) + (phy->pll_fbd_div1f << 4) +
				(phy->pll_fbd_2p << 1) + phy->pll_enbwt;
	dsi_phy_tst_set(base, PHY_CFG_PLL_I, val);
	dsi_phy_tst_set(base, PHY_CFG_PLL_II, phy->pll_fbd_p);
	dsi_phy_tst_set(base, PHY_CFG_PLL_III, phy->pll_fbd_s);
	val = (phy->pll_pre_div1p << 7) + phy->pll_pre_p;
	dsi_phy_tst_set(base, PHY_CFG_PLL_IV, val);
	val = (5 << 5) + (phy->pll_vco_750M << 4) + (phy->pll_lpf_rs << 2) +
		phy->pll_lpf_cs;
	dsi_phy_tst_set(base, PHY_CFG_PLL_V, val);

	writel(PHY_ENABLECLK, base + PHY_RSTZ);
	udelay(1);
	writel(PHY_ENABLECLK | PHY_UNSHUTDOWNZ, base + PHY_RSTZ);
	udelay(1);
	writel(PHY_ENABLECLK | PHY_UNRSTZ | PHY_UNSHUTDOWNZ, base + PHY_RSTZ);
	usleep_range(1000, 1500);

	/*
	 * wait for phy's clock ready
	 */
	delay_count = 100;
	while (delay_count) {
		val = readl(base +  PHY_STATUS);
		if ((BIT(0) | BIT(2)) & val)
			break;

		udelay(1);
		delay_count--;
	}

	if (!delay_count)
		DRM_INFO("phylock and phystopstateclklane is not ready.\n");
}

static void dsi_set_mode_timing(void __iomem *base,
				u32 lane_byte_clk_kHz,
				struct drm_display_mode *mode,
				enum mipi_dsi_pixel_format format)
{
	u32 hfp, hbp, hsw, vfp, vbp, vsw;
	u32 hline_time;
	u32 hsa_time;
	u32 hbp_time;
	u32 pixel_clk_kHz;
	int htot, vtot;
	u32 val;
	u64 tmp;

	val = dsi_get_dpi_color_coding(format);
	writel(val, base + DPI_COLOR_CODING);

	val = (mode->flags & DRM_MODE_FLAG_NHSYNC ? 1 : 0) << 2;
	val |= (mode->flags & DRM_MODE_FLAG_NVSYNC ? 1 : 0) << 1;
	writel(val, base +  DPI_CFG_POL);

	/*
	 * The DSI IP accepts vertical timing using lines as normal,
	 * but horizontal timing is a mixture of pixel-clocks for the
	 * active region and byte-lane clocks for the blanking-related
	 * timings.  hfp is specified as the total hline_time in byte-
	 * lane clocks minus hsa, hbp and active.
	 */
	pixel_clk_kHz = mode->clock;
	htot = mode->htotal;
	vtot = mode->vtotal;
	hfp = mode->hsync_start - mode->hdisplay;
	hbp = mode->htotal - mode->hsync_end;
	hsw = mode->hsync_end - mode->hsync_start;
	vfp = mode->vsync_start - mode->vdisplay;
	vbp = mode->vtotal - mode->vsync_end;
	vsw = mode->vsync_end - mode->vsync_start;
	if (vsw > 15) {
		DRM_DEBUG_DRIVER("vsw exceeded 15\n");
		vsw = 15;
	}

	hsa_time = (hsw * lane_byte_clk_kHz) / pixel_clk_kHz;
	hbp_time = (hbp * lane_byte_clk_kHz) / pixel_clk_kHz;
	tmp = (u64)htot * (u64)lane_byte_clk_kHz;
	hline_time = DIV_ROUND_UP(tmp, pixel_clk_kHz);

	/* all specified in byte-lane clocks */
	writel(hsa_time, base + VID_HSA_TIME);
	writel(hbp_time, base + VID_HBP_TIME);
	writel(hline_time, base + VID_HLINE_TIME);

	writel(vsw, base + VID_VSA_LINES);
	writel(vbp, base + VID_VBP_LINES);
	writel(vfp, base + VID_VFP_LINES);
	writel(mode->vdisplay, base + VID_VACTIVE_LINES);
	writel(mode->hdisplay, base + VID_PKT_SIZE);

	DRM_DEBUG_DRIVER("htot=%d, hfp=%d, hbp=%d, hsw=%d\n",
			 htot, hfp, hbp, hsw);
	DRM_DEBUG_DRIVER("vtol=%d, vfp=%d, vbp=%d, vsw=%d\n",
			 vtot, vfp, vbp, vsw);
	DRM_DEBUG_DRIVER("hsa_time=%d, hbp_time=%d, hline_time=%d\n",
			 hsa_time, hbp_time, hline_time);
}

static void dsi_set_video_mode(void __iomem *base, unsigned long flags)
{
	u32 val;
	u32 mode_mask = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
		MIPI_DSI_MODE_VIDEO_SYNC_PULSE;
	u32 non_burst_sync_pulse = MIPI_DSI_MODE_VIDEO |
		MIPI_DSI_MODE_VIDEO_SYNC_PULSE;
	u32 non_burst_sync_event = MIPI_DSI_MODE_VIDEO;

	/*
	 * choose video mode type
	 */
	if ((flags & mode_mask) == non_burst_sync_pulse)
		val = DSI_NON_BURST_SYNC_PULSES;
	else if ((flags & mode_mask) == non_burst_sync_event)
		val = DSI_NON_BURST_SYNC_EVENTS;
	else
		val = DSI_BURST_SYNC_PULSES_1;
	writel(val, base + VID_MODE_CFG);

	writel(PHY_TXREQUESTCLKHS, base + LPCLK_CTRL);
	writel(DSI_VIDEO_MODE, base + MODE_CFG);
}

static void dsi_mipi_init(struct dw_dsi *dsi)
{
	struct dsi_hw_ctx *ctx = dsi->ctx;
	struct mipi_phy_params *phy = &dsi->phy;
	struct drm_display_mode *mode = &dsi->cur_mode;
	u32 bpp = mipi_dsi_pixel_format_to_bpp(dsi->format);
	void __iomem *base = ctx->base;
	u32 dphy_req_kHz;

	/*
	 * count phy params
	 */
	dphy_req_kHz = mode->clock * bpp / dsi->lanes;
	dsi_get_phy_params(dphy_req_kHz, phy);

	/* reset Core */
	writel(RESET, base + PWR_UP);

	/* set dsi phy params */
	dsi_set_mipi_phy(base, phy, dsi->lanes);

	/* set dsi mode timing */
	dsi_set_mode_timing(base, phy->lane_byte_clk_kHz, mode, dsi->format);

	/* set dsi video mode */
	dsi_set_video_mode(base, dsi->mode_flags);

	/* dsi wake up */
	writel(POWERUP, base + PWR_UP);

	DRM_DEBUG_DRIVER("lanes=%d, pixel_clk=%d kHz, bytes_freq=%d kHz\n",
			 dsi->lanes, mode->clock, phy->lane_byte_clk_kHz);
}

static void dsi_encoder_disable(struct drm_encoder *encoder)
{
	struct dw_dsi *dsi = encoder_to_dsi(encoder);
	struct dsi_hw_ctx *ctx = dsi->ctx;
	void __iomem *base = ctx->base;

	if (!dsi->enable)
		return;

	writel(0, base + PWR_UP);
	writel(0, base + LPCLK_CTRL);
	writel(0, base + PHY_RSTZ);
	clk_disable_unprepare(ctx->pclk);

	dsi->enable = false;
}

static void dsi_encoder_enable(struct drm_encoder *encoder)
{
	struct dw_dsi *dsi = encoder_to_dsi(encoder);
	struct dsi_hw_ctx *ctx = dsi->ctx;
	int ret;

	if (dsi->enable)
		return;

	ret = clk_prepare_enable(ctx->pclk);
	if (ret) {
		DRM_ERROR("fail to enable pclk: %d\n", ret);
		return;
	}

	dsi_mipi_init(dsi);

	dsi->enable = true;
}

static void dsi_encoder_mode_set(struct drm_encoder *encoder,
				 struct drm_display_mode *mode,
				 struct drm_display_mode *adj_mode)
{
	struct dw_dsi *dsi = encoder_to_dsi(encoder);

	drm_mode_copy(&dsi->cur_mode, adj_mode);
}

static int dsi_encoder_atomic_check(struct drm_encoder *encoder,
				    struct drm_crtc_state *crtc_state,
				    struct drm_connector_state *conn_state)
{
	/* do nothing */
	return 0;
}

static const struct drm_encoder_helper_funcs dw_encoder_helper_funcs = {
	.atomic_check	= dsi_encoder_atomic_check,
	.mode_set	= dsi_encoder_mode_set,
	.enable		= dsi_encoder_enable,
	.disable	= dsi_encoder_disable
};

static const struct drm_encoder_funcs dw_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static int dw_drm_encoder_init(struct device *dev,
			       struct drm_device *drm_dev,
			       struct drm_encoder *encoder)
{
	int ret;
	u32 crtc_mask = drm_of_find_possible_crtcs(drm_dev, dev->of_node);

	if (!crtc_mask) {
		DRM_ERROR("failed to find crtc mask\n");
		return -EINVAL;
	}

	encoder->possible_crtcs = crtc_mask;
	ret = drm_encoder_init(drm_dev, encoder, &dw_encoder_funcs,
			       DRM_MODE_ENCODER_DSI, NULL);
	if (ret) {
		DRM_ERROR("failed to init dsi encoder\n");
		return ret;
	}

	drm_encoder_helper_add(encoder, &dw_encoder_helper_funcs);

	return 0;
}

static int dsi_host_attach(struct mipi_dsi_host *host,
			   struct mipi_dsi_device *mdsi)
{
	struct dw_dsi *dsi = host_to_dsi(host);

	if (mdsi->lanes < 1 || mdsi->lanes > 4) {
		DRM_ERROR("dsi device params invalid\n");
		return -EINVAL;
	}

	dsi->lanes = mdsi->lanes;
	dsi->format = mdsi->format;
	dsi->mode_flags = mdsi->mode_flags;

	return 0;
}

static int dsi_host_detach(struct mipi_dsi_host *host,
			   struct mipi_dsi_device *mdsi)
{
	/* do nothing */
	return 0;
}

static const struct mipi_dsi_host_ops dsi_host_ops = {
	.attach = dsi_host_attach,
	.detach = dsi_host_detach,
};

static int dsi_host_init(struct device *dev, struct dw_dsi *dsi)
{
	struct mipi_dsi_host *host = &dsi->host;
	int ret;

	host->dev = dev;
	host->ops = &dsi_host_ops;
	ret = mipi_dsi_host_register(host);
	if (ret) {
		DRM_ERROR("failed to register dsi host\n");
		return ret;
	}

	return 0;
}

static int dsi_bridge_init(struct drm_device *dev, struct dw_dsi *dsi)
{
	struct drm_encoder *encoder = &dsi->encoder;
	struct drm_bridge *bridge = dsi->bridge;
	int ret;

	/* associate the bridge to dsi encoder */
	encoder->bridge = bridge;
	bridge->encoder = encoder;

	ret = drm_bridge_attach(dev, bridge);
	if (ret) {
		DRM_ERROR("failed to attach external bridge\n");
		return ret;
	}

	return 0;
}

static int dsi_bind(struct device *dev, struct device *master, void *data)
{
	struct dsi_data *ddata = dev_get_drvdata(dev);
	struct dw_dsi *dsi = &ddata->dsi;
	struct drm_device *drm_dev = data;
	int ret;

	ret = dw_drm_encoder_init(dev, drm_dev, &dsi->encoder);
	if (ret)
		return ret;

	ret = dsi_host_init(dev, dsi);
	if (ret)
		return ret;

	ret = dsi_bridge_init(drm_dev, dsi);
	if (ret)
		return ret;

	return 0;
}

static void dsi_unbind(struct device *dev, struct device *master, void *data)
{
	/* do nothing */
}

static const struct component_ops dsi_ops = {
	.bind	= dsi_bind,
	.unbind	= dsi_unbind,
};

static int dsi_parse_dt(struct platform_device *pdev, struct dw_dsi *dsi)
{
	struct dsi_hw_ctx *ctx = dsi->ctx;
	struct device_node *np = pdev->dev.of_node;
	struct device_node *endpoint, *bridge_node;
	struct drm_bridge *bridge;
	struct resource *res;

	/*
	 * Get the endpoint node. In our case, dsi has one output port1
	 * to which the external HDMI bridge is connected.
	 */
	endpoint = of_graph_get_endpoint_by_regs(np, 1, -1);
	if (!endpoint) {
		DRM_ERROR("no valid endpoint node\n");
		return -ENODEV;
	}
	of_node_put(endpoint);

	bridge_node = of_graph_get_remote_port_parent(endpoint);
	if (!bridge_node) {
		DRM_ERROR("no valid bridge node\n");
		return -ENODEV;
	}
	of_node_put(bridge_node);

	bridge = of_drm_find_bridge(bridge_node);
	if (!bridge) {
		DRM_INFO("wait for external HDMI bridge driver.\n");
		return -EPROBE_DEFER;
	}
	dsi->bridge = bridge;

	ctx->pclk = devm_clk_get(&pdev->dev, "pclk");
	if (IS_ERR(ctx->pclk)) {
		DRM_ERROR("failed to get pclk clock\n");
		return PTR_ERR(ctx->pclk);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ctx->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(ctx->base)) {
		DRM_ERROR("failed to remap dsi io region\n");
		return PTR_ERR(ctx->base);
	}

	return 0;
}

static int dsi_probe(struct platform_device *pdev)
{
	struct dsi_data *data;
	struct dw_dsi *dsi;
	struct dsi_hw_ctx *ctx;
	int ret;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		DRM_ERROR("failed to allocate dsi data.\n");
		return -ENOMEM;
	}
	dsi = &data->dsi;
	ctx = &data->ctx;
	dsi->ctx = ctx;

	ret = dsi_parse_dt(pdev, dsi);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, data);

	return component_add(&pdev->dev, &dsi_ops);
}

static int dsi_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &dsi_ops);

	return 0;
}

static const struct of_device_id dsi_of_match[] = {
	{.compatible = "hisilicon,hi6220-dsi"},
	{ }
};
MODULE_DEVICE_TABLE(of, dsi_of_match);

static struct platform_driver dsi_driver = {
	.probe = dsi_probe,
	.remove = dsi_remove,
	.driver = {
		.name = "dw-dsi",
		.of_match_table = dsi_of_match,
	},
};

module_platform_driver(dsi_driver);

MODULE_AUTHOR("Xinliang Liu <xinliang.liu@linaro.org>");
MODULE_AUTHOR("Xinliang Liu <z.liuxinliang@hisilicon.com>");
MODULE_AUTHOR("Xinwei Kong <kong.kongxinwei@hisilicon.com>");
MODULE_DESCRIPTION("DesignWare MIPI DSI Host Controller v1.02 driver");
MODULE_LICENSE("GPL v2");
