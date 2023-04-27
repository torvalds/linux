// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 2020 Texas Instruments Incorporated - https://www.ti.com
 *  Author: Peter Ujfalusi <peter.ujfalusi@ti.com>
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/media-bus-format.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <video/mipi_display.h>
#include <video/videomode.h>

/* Global (16-bit addressable) */
#define TC358768_CHIPID			0x0000
#define TC358768_SYSCTL			0x0002
#define TC358768_CONFCTL		0x0004
#define TC358768_VSDLY			0x0006
#define TC358768_DATAFMT		0x0008
#define TC358768_GPIOEN			0x000E
#define TC358768_GPIODIR		0x0010
#define TC358768_GPIOIN			0x0012
#define TC358768_GPIOOUT		0x0014
#define TC358768_PLLCTL0		0x0016
#define TC358768_PLLCTL1		0x0018
#define TC358768_CMDBYTE		0x0022
#define TC358768_PP_MISC		0x0032
#define TC358768_DSITX_DT		0x0050
#define TC358768_FIFOSTATUS		0x00F8

/* Debug (16-bit addressable) */
#define TC358768_VBUFCTRL		0x00E0
#define TC358768_DBG_WIDTH		0x00E2
#define TC358768_DBG_VBLANK		0x00E4
#define TC358768_DBG_DATA		0x00E8

/* TX PHY (32-bit addressable) */
#define TC358768_CLW_DPHYCONTTX		0x0100
#define TC358768_D0W_DPHYCONTTX		0x0104
#define TC358768_D1W_DPHYCONTTX		0x0108
#define TC358768_D2W_DPHYCONTTX		0x010C
#define TC358768_D3W_DPHYCONTTX		0x0110
#define TC358768_CLW_CNTRL		0x0140
#define TC358768_D0W_CNTRL		0x0144
#define TC358768_D1W_CNTRL		0x0148
#define TC358768_D2W_CNTRL		0x014C
#define TC358768_D3W_CNTRL		0x0150

/* TX PPI (32-bit addressable) */
#define TC358768_STARTCNTRL		0x0204
#define TC358768_DSITXSTATUS		0x0208
#define TC358768_LINEINITCNT		0x0210
#define TC358768_LPTXTIMECNT		0x0214
#define TC358768_TCLK_HEADERCNT		0x0218
#define TC358768_TCLK_TRAILCNT		0x021C
#define TC358768_THS_HEADERCNT		0x0220
#define TC358768_TWAKEUP		0x0224
#define TC358768_TCLK_POSTCNT		0x0228
#define TC358768_THS_TRAILCNT		0x022C
#define TC358768_HSTXVREGCNT		0x0230
#define TC358768_HSTXVREGEN		0x0234
#define TC358768_TXOPTIONCNTRL		0x0238
#define TC358768_BTACNTRL1		0x023C

/* TX CTRL (32-bit addressable) */
#define TC358768_DSI_CONTROL		0x040C
#define TC358768_DSI_STATUS		0x0410
#define TC358768_DSI_INT		0x0414
#define TC358768_DSI_INT_ENA		0x0418
#define TC358768_DSICMD_RDFIFO		0x0430
#define TC358768_DSI_ACKERR		0x0434
#define TC358768_DSI_ACKERR_INTENA	0x0438
#define TC358768_DSI_ACKERR_HALT	0x043c
#define TC358768_DSI_RXERR		0x0440
#define TC358768_DSI_RXERR_INTENA	0x0444
#define TC358768_DSI_RXERR_HALT		0x0448
#define TC358768_DSI_ERR		0x044C
#define TC358768_DSI_ERR_INTENA		0x0450
#define TC358768_DSI_ERR_HALT		0x0454
#define TC358768_DSI_CONFW		0x0500
#define TC358768_DSI_LPCMD		0x0500
#define TC358768_DSI_RESET		0x0504
#define TC358768_DSI_INT_CLR		0x050C
#define TC358768_DSI_START		0x0518

/* DSITX CTRL (16-bit addressable) */
#define TC358768_DSICMD_TX		0x0600
#define TC358768_DSICMD_TYPE		0x0602
#define TC358768_DSICMD_WC		0x0604
#define TC358768_DSICMD_WD0		0x0610
#define TC358768_DSICMD_WD1		0x0612
#define TC358768_DSICMD_WD2		0x0614
#define TC358768_DSICMD_WD3		0x0616
#define TC358768_DSI_EVENT		0x0620
#define TC358768_DSI_VSW		0x0622
#define TC358768_DSI_VBPR		0x0624
#define TC358768_DSI_VACT		0x0626
#define TC358768_DSI_HSW		0x0628
#define TC358768_DSI_HBPR		0x062A
#define TC358768_DSI_HACT		0x062C

/* TC358768_DSI_CONTROL (0x040C) register */
#define TC358768_DSI_CONTROL_DIS_MODE	BIT(15)
#define TC358768_DSI_CONTROL_TXMD	BIT(7)
#define TC358768_DSI_CONTROL_HSCKMD	BIT(5)
#define TC358768_DSI_CONTROL_EOTDIS	BIT(0)

/* TC358768_DSI_CONFW (0x0500) register */
#define TC358768_DSI_CONFW_MODE_SET	(5 << 29)
#define TC358768_DSI_CONFW_MODE_CLR	(6 << 29)
#define TC358768_DSI_CONFW_ADDR_DSI_CONTROL	(0x3 << 24)

static const char * const tc358768_supplies[] = {
	"vddc", "vddmipi", "vddio"
};

struct tc358768_dsi_output {
	struct mipi_dsi_device *dev;
	struct drm_panel *panel;
	struct drm_bridge *bridge;
};

struct tc358768_priv {
	struct device *dev;
	struct regmap *regmap;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[ARRAY_SIZE(tc358768_supplies)];
	struct clk *refclk;
	int enabled;
	int error;

	struct mipi_dsi_host dsi_host;
	struct drm_bridge bridge;
	struct tc358768_dsi_output output;

	u32 pd_lines; /* number of Parallel Port Input Data Lines */
	u32 dsi_lanes; /* number of DSI Lanes */
	u32 dsi_bpp; /* number of Bits Per Pixel over DSI */

	/* Parameters for PLL programming */
	u32 fbd;	/* PLL feedback divider */
	u32 prd;	/* PLL input divider */
	u32 frs;	/* PLL Freqency range for HSCK (post divider) */

	u32 dsiclk;	/* pll_clk / 2 */
};

static inline struct tc358768_priv *dsi_host_to_tc358768(struct mipi_dsi_host
							 *host)
{
	return container_of(host, struct tc358768_priv, dsi_host);
}

static inline struct tc358768_priv *bridge_to_tc358768(struct drm_bridge
						       *bridge)
{
	return container_of(bridge, struct tc358768_priv, bridge);
}

static int tc358768_clear_error(struct tc358768_priv *priv)
{
	int ret = priv->error;

	priv->error = 0;
	return ret;
}

static void tc358768_write(struct tc358768_priv *priv, u32 reg, u32 val)
{
	/* work around https://gcc.gnu.org/bugzilla/show_bug.cgi?id=81715 */
	int tmpval = val;
	size_t count = 2;

	if (priv->error)
		return;

	/* 16-bit register? */
	if (reg < 0x100 || reg >= 0x600)
		count = 1;

	priv->error = regmap_bulk_write(priv->regmap, reg, &tmpval, count);
}

static void tc358768_read(struct tc358768_priv *priv, u32 reg, u32 *val)
{
	size_t count = 2;

	if (priv->error)
		return;

	/* 16-bit register? */
	if (reg < 0x100 || reg >= 0x600) {
		*val = 0;
		count = 1;
	}

	priv->error = regmap_bulk_read(priv->regmap, reg, val, count);
}

static void tc358768_update_bits(struct tc358768_priv *priv, u32 reg, u32 mask,
				 u32 val)
{
	u32 tmp, orig;

	tc358768_read(priv, reg, &orig);

	if (priv->error)
		return;

	tmp = orig & ~mask;
	tmp |= val & mask;
	if (tmp != orig)
		tc358768_write(priv, reg, tmp);
}

static int tc358768_sw_reset(struct tc358768_priv *priv)
{
	/* Assert Reset */
	tc358768_write(priv, TC358768_SYSCTL, 1);
	/* Release Reset, Exit Sleep */
	tc358768_write(priv, TC358768_SYSCTL, 0);

	return tc358768_clear_error(priv);
}

static void tc358768_hw_enable(struct tc358768_priv *priv)
{
	int ret;

	if (priv->enabled)
		return;

	ret = clk_prepare_enable(priv->refclk);
	if (ret < 0)
		dev_err(priv->dev, "error enabling refclk (%d)\n", ret);

	ret = regulator_bulk_enable(ARRAY_SIZE(priv->supplies), priv->supplies);
	if (ret < 0)
		dev_err(priv->dev, "error enabling regulators (%d)\n", ret);

	if (priv->reset_gpio)
		usleep_range(200, 300);

	/*
	 * The RESX is active low (GPIO_ACTIVE_LOW).
	 * DEASSERT (value = 0) the reset_gpio to enable the chip
	 */
	gpiod_set_value_cansleep(priv->reset_gpio, 0);

	/* wait for encoder clocks to stabilize */
	usleep_range(1000, 2000);

	priv->enabled = true;
}

static void tc358768_hw_disable(struct tc358768_priv *priv)
{
	int ret;

	if (!priv->enabled)
		return;

	/*
	 * The RESX is active low (GPIO_ACTIVE_LOW).
	 * ASSERT (value = 1) the reset_gpio to disable the chip
	 */
	gpiod_set_value_cansleep(priv->reset_gpio, 1);

	ret = regulator_bulk_disable(ARRAY_SIZE(priv->supplies),
				     priv->supplies);
	if (ret < 0)
		dev_err(priv->dev, "error disabling regulators (%d)\n", ret);

	clk_disable_unprepare(priv->refclk);

	priv->enabled = false;
}

static u32 tc358768_pll_to_pclk(struct tc358768_priv *priv, u32 pll_clk)
{
	return (u32)div_u64((u64)pll_clk * priv->dsi_lanes, priv->dsi_bpp);
}

static u32 tc358768_pclk_to_pll(struct tc358768_priv *priv, u32 pclk)
{
	return (u32)div_u64((u64)pclk * priv->dsi_bpp, priv->dsi_lanes);
}

static int tc358768_calc_pll(struct tc358768_priv *priv,
			     const struct drm_display_mode *mode,
			     bool verify_only)
{
	static const u32 frs_limits[] = {
		1000000000,
		500000000,
		250000000,
		125000000,
		62500000
	};
	unsigned long refclk;
	u32 prd, target_pll, i, max_pll, min_pll;
	u32 frs, best_diff, best_pll, best_prd, best_fbd;

	target_pll = tc358768_pclk_to_pll(priv, mode->clock * 1000);

	/* pll_clk = RefClk * [(FBD + 1)/ (PRD + 1)] * [1 / (2^FRS)] */

	for (i = 0; i < ARRAY_SIZE(frs_limits); i++)
		if (target_pll >= frs_limits[i])
			break;

	if (i == ARRAY_SIZE(frs_limits) || i == 0)
		return -EINVAL;

	frs = i - 1;
	max_pll = frs_limits[i - 1];
	min_pll = frs_limits[i];

	refclk = clk_get_rate(priv->refclk);

	best_diff = UINT_MAX;
	best_pll = 0;
	best_prd = 0;
	best_fbd = 0;

	for (prd = 0; prd < 16; ++prd) {
		u32 divisor = (prd + 1) * (1 << frs);
		u32 fbd;

		for (fbd = 0; fbd < 512; ++fbd) {
			u32 pll, diff, pll_in;

			pll = (u32)div_u64((u64)refclk * (fbd + 1), divisor);

			if (pll >= max_pll || pll < min_pll)
				continue;

			pll_in = (u32)div_u64((u64)refclk, prd + 1);
			if (pll_in < 4000000)
				continue;

			diff = max(pll, target_pll) - min(pll, target_pll);

			if (diff < best_diff) {
				best_diff = diff;
				best_pll = pll;
				best_prd = prd;
				best_fbd = fbd;

				if (best_diff == 0)
					goto found;
			}
		}
	}

	if (best_diff == UINT_MAX) {
		dev_err(priv->dev, "could not find suitable PLL setup\n");
		return -EINVAL;
	}

found:
	if (verify_only)
		return 0;

	priv->fbd = best_fbd;
	priv->prd = best_prd;
	priv->frs = frs;
	priv->dsiclk = best_pll / 2;

	return 0;
}

static int tc358768_dsi_host_attach(struct mipi_dsi_host *host,
				    struct mipi_dsi_device *dev)
{
	struct tc358768_priv *priv = dsi_host_to_tc358768(host);
	struct drm_bridge *bridge;
	struct drm_panel *panel;
	struct device_node *ep;
	int ret;

	if (dev->lanes > 4) {
		dev_err(priv->dev, "unsupported number of data lanes(%u)\n",
			dev->lanes);
		return -EINVAL;
	}

	/*
	 * tc358768 supports both Video and Pulse mode, but the driver only
	 * implements Video (event) mode currently
	 */
	if (!(dev->mode_flags & MIPI_DSI_MODE_VIDEO)) {
		dev_err(priv->dev, "Only MIPI_DSI_MODE_VIDEO is supported\n");
		return -ENOTSUPP;
	}

	/*
	 * tc358768 supports RGB888, RGB666, RGB666_PACKED and RGB565, but only
	 * RGB888 is verified.
	 */
	if (dev->format != MIPI_DSI_FMT_RGB888) {
		dev_warn(priv->dev, "Only MIPI_DSI_FMT_RGB888 tested!\n");
		return -ENOTSUPP;
	}

	ret = drm_of_find_panel_or_bridge(host->dev->of_node, 1, 0, &panel,
					  &bridge);
	if (ret)
		return ret;

	if (panel) {
		bridge = drm_panel_bridge_add_typed(panel,
						    DRM_MODE_CONNECTOR_DSI);
		if (IS_ERR(bridge))
			return PTR_ERR(bridge);
	}

	priv->output.dev = dev;
	priv->output.bridge = bridge;
	priv->output.panel = panel;

	priv->dsi_lanes = dev->lanes;
	priv->dsi_bpp = mipi_dsi_pixel_format_to_bpp(dev->format);

	/* get input ep (port0/endpoint0) */
	ret = -EINVAL;
	ep = of_graph_get_endpoint_by_regs(host->dev->of_node, 0, 0);
	if (ep) {
		ret = of_property_read_u32(ep, "data-lines", &priv->pd_lines);

		of_node_put(ep);
	}

	if (ret)
		priv->pd_lines = priv->dsi_bpp;

	drm_bridge_add(&priv->bridge);

	return 0;
}

static int tc358768_dsi_host_detach(struct mipi_dsi_host *host,
				    struct mipi_dsi_device *dev)
{
	struct tc358768_priv *priv = dsi_host_to_tc358768(host);

	drm_bridge_remove(&priv->bridge);
	if (priv->output.panel)
		drm_panel_bridge_remove(priv->output.bridge);

	return 0;
}

static ssize_t tc358768_dsi_host_transfer(struct mipi_dsi_host *host,
					  const struct mipi_dsi_msg *msg)
{
	struct tc358768_priv *priv = dsi_host_to_tc358768(host);
	struct mipi_dsi_packet packet;
	int ret;

	if (!priv->enabled) {
		dev_err(priv->dev, "Bridge is not enabled\n");
		return -ENODEV;
	}

	if (msg->rx_len) {
		dev_warn(priv->dev, "MIPI rx is not supported\n");
		return -ENOTSUPP;
	}

	if (msg->tx_len > 8) {
		dev_warn(priv->dev, "Maximum 8 byte MIPI tx is supported\n");
		return -ENOTSUPP;
	}

	ret = mipi_dsi_create_packet(&packet, msg);
	if (ret)
		return ret;

	if (mipi_dsi_packet_format_is_short(msg->type)) {
		tc358768_write(priv, TC358768_DSICMD_TYPE,
			       (0x10 << 8) | (packet.header[0] & 0x3f));
		tc358768_write(priv, TC358768_DSICMD_WC, 0);
		tc358768_write(priv, TC358768_DSICMD_WD0,
			       (packet.header[2] << 8) | packet.header[1]);
	} else {
		int i;

		tc358768_write(priv, TC358768_DSICMD_TYPE,
			       (0x40 << 8) | (packet.header[0] & 0x3f));
		tc358768_write(priv, TC358768_DSICMD_WC, packet.payload_length);
		for (i = 0; i < packet.payload_length; i += 2) {
			u16 val = packet.payload[i];

			if (i + 1 < packet.payload_length)
				val |= packet.payload[i + 1] << 8;

			tc358768_write(priv, TC358768_DSICMD_WD0 + i, val);
		}
	}

	/* start transfer */
	tc358768_write(priv, TC358768_DSICMD_TX, 1);

	ret = tc358768_clear_error(priv);
	if (ret)
		dev_warn(priv->dev, "Software disable failed: %d\n", ret);
	else
		ret = packet.size;

	return ret;
}

static const struct mipi_dsi_host_ops tc358768_dsi_host_ops = {
	.attach = tc358768_dsi_host_attach,
	.detach = tc358768_dsi_host_detach,
	.transfer = tc358768_dsi_host_transfer,
};

static int tc358768_bridge_attach(struct drm_bridge *bridge,
				  enum drm_bridge_attach_flags flags)
{
	struct tc358768_priv *priv = bridge_to_tc358768(bridge);

	if (!drm_core_check_feature(bridge->dev, DRIVER_ATOMIC)) {
		dev_err(priv->dev, "needs atomic updates support\n");
		return -ENOTSUPP;
	}

	return drm_bridge_attach(bridge->encoder, priv->output.bridge, bridge,
				 flags);
}

static enum drm_mode_status
tc358768_bridge_mode_valid(struct drm_bridge *bridge,
			   const struct drm_display_info *info,
			   const struct drm_display_mode *mode)
{
	struct tc358768_priv *priv = bridge_to_tc358768(bridge);

	if (tc358768_calc_pll(priv, mode, true))
		return MODE_CLOCK_RANGE;

	return MODE_OK;
}

static void tc358768_bridge_disable(struct drm_bridge *bridge)
{
	struct tc358768_priv *priv = bridge_to_tc358768(bridge);
	int ret;

	/* set FrmStop */
	tc358768_update_bits(priv, TC358768_PP_MISC, BIT(15), BIT(15));

	/* wait at least for one frame */
	msleep(50);

	/* clear PP_en */
	tc358768_update_bits(priv, TC358768_CONFCTL, BIT(6), 0);

	/* set RstPtr */
	tc358768_update_bits(priv, TC358768_PP_MISC, BIT(14), BIT(14));

	ret = tc358768_clear_error(priv);
	if (ret)
		dev_warn(priv->dev, "Software disable failed: %d\n", ret);
}

static void tc358768_bridge_post_disable(struct drm_bridge *bridge)
{
	struct tc358768_priv *priv = bridge_to_tc358768(bridge);

	tc358768_hw_disable(priv);
}

static int tc358768_setup_pll(struct tc358768_priv *priv,
			      const struct drm_display_mode *mode)
{
	u32 fbd, prd, frs;
	int ret;

	ret = tc358768_calc_pll(priv, mode, false);
	if (ret) {
		dev_err(priv->dev, "PLL calculation failed: %d\n", ret);
		return ret;
	}

	fbd = priv->fbd;
	prd = priv->prd;
	frs = priv->frs;

	dev_dbg(priv->dev, "PLL: refclk %lu, fbd %u, prd %u, frs %u\n",
		clk_get_rate(priv->refclk), fbd, prd, frs);
	dev_dbg(priv->dev, "PLL: pll_clk: %u, DSIClk %u, DSIByteClk %u\n",
		priv->dsiclk * 2, priv->dsiclk, priv->dsiclk / 4);
	dev_dbg(priv->dev, "PLL: pclk %u (panel: %u)\n",
		tc358768_pll_to_pclk(priv, priv->dsiclk * 2),
		mode->clock * 1000);

	/* PRD[15:12] FBD[8:0] */
	tc358768_write(priv, TC358768_PLLCTL0, (prd << 12) | fbd);

	/* FRS[11:10] LBWS[9:8] CKEN[4] RESETB[1] EN[0] */
	tc358768_write(priv, TC358768_PLLCTL1,
		       (frs << 10) | (0x2 << 8) | BIT(1) | BIT(0));

	/* wait for lock */
	usleep_range(1000, 2000);

	/* FRS[11:10] LBWS[9:8] CKEN[4] PLL_CKEN[4] RESETB[1] EN[0] */
	tc358768_write(priv, TC358768_PLLCTL1,
		       (frs << 10) | (0x2 << 8) | BIT(4) | BIT(1) | BIT(0));

	return tc358768_clear_error(priv);
}

#define TC358768_PRECISION	1000
static u32 tc358768_ns_to_cnt(u32 ns, u32 period_nsk)
{
	return (ns * TC358768_PRECISION + period_nsk) / period_nsk;
}

static u32 tc358768_to_ns(u32 nsk)
{
	return (nsk / TC358768_PRECISION);
}

static void tc358768_bridge_pre_enable(struct drm_bridge *bridge)
{
	struct tc358768_priv *priv = bridge_to_tc358768(bridge);
	struct mipi_dsi_device *dsi_dev = priv->output.dev;
	unsigned long mode_flags = dsi_dev->mode_flags;
	u32 val, val2, lptxcnt, hact, data_type;
	s32 raw_val;
	const struct drm_display_mode *mode;
	u32 dsibclk_nsk, dsiclk_nsk, ui_nsk;
	u32 dsiclk, dsibclk, video_start;
	const u32 internal_delay = 40;
	int ret, i;

	if (mode_flags & MIPI_DSI_CLOCK_NON_CONTINUOUS) {
		dev_warn_once(priv->dev, "Non-continuous mode unimplemented, falling back to continuous\n");
		mode_flags &= ~MIPI_DSI_CLOCK_NON_CONTINUOUS;
	}

	tc358768_hw_enable(priv);

	ret = tc358768_sw_reset(priv);
	if (ret) {
		dev_err(priv->dev, "Software reset failed: %d\n", ret);
		tc358768_hw_disable(priv);
		return;
	}

	mode = &bridge->encoder->crtc->state->adjusted_mode;
	ret = tc358768_setup_pll(priv, mode);
	if (ret) {
		dev_err(priv->dev, "PLL setup failed: %d\n", ret);
		tc358768_hw_disable(priv);
		return;
	}

	dsiclk = priv->dsiclk;
	dsibclk = dsiclk / 4;

	/* Data Format Control Register */
	val = BIT(2) | BIT(1) | BIT(0); /* rdswap_en | dsitx_en | txdt_en */
	switch (dsi_dev->format) {
	case MIPI_DSI_FMT_RGB888:
		val |= (0x3 << 4);
		hact = mode->hdisplay * 3;
		video_start = (mode->htotal - mode->hsync_start) * 3;
		data_type = MIPI_DSI_PACKED_PIXEL_STREAM_24;
		break;
	case MIPI_DSI_FMT_RGB666:
		val |= (0x4 << 4);
		hact = mode->hdisplay * 3;
		video_start = (mode->htotal - mode->hsync_start) * 3;
		data_type = MIPI_DSI_PACKED_PIXEL_STREAM_18;
		break;

	case MIPI_DSI_FMT_RGB666_PACKED:
		val |= (0x4 << 4) | BIT(3);
		hact = mode->hdisplay * 18 / 8;
		video_start = (mode->htotal - mode->hsync_start) * 18 / 8;
		data_type = MIPI_DSI_PIXEL_STREAM_3BYTE_18;
		break;

	case MIPI_DSI_FMT_RGB565:
		val |= (0x5 << 4);
		hact = mode->hdisplay * 2;
		video_start = (mode->htotal - mode->hsync_start) * 2;
		data_type = MIPI_DSI_PACKED_PIXEL_STREAM_16;
		break;
	default:
		dev_err(priv->dev, "Invalid data format (%u)\n",
			dsi_dev->format);
		tc358768_hw_disable(priv);
		return;
	}

	/* VSDly[9:0] */
	video_start = max(video_start, internal_delay + 1) - internal_delay;
	tc358768_write(priv, TC358768_VSDLY, video_start);

	tc358768_write(priv, TC358768_DATAFMT, val);
	tc358768_write(priv, TC358768_DSITX_DT, data_type);

	/* Enable D-PHY (HiZ->LP11) */
	tc358768_write(priv, TC358768_CLW_CNTRL, 0x0000);
	/* Enable lanes */
	for (i = 0; i < dsi_dev->lanes; i++)
		tc358768_write(priv, TC358768_D0W_CNTRL + i * 4, 0x0000);

	/* DSI Timings */
	dsibclk_nsk = (u32)div_u64((u64)1000000000 * TC358768_PRECISION,
				  dsibclk);
	dsiclk_nsk = (u32)div_u64((u64)1000000000 * TC358768_PRECISION, dsiclk);
	ui_nsk = dsiclk_nsk / 2;
	dev_dbg(priv->dev, "dsiclk_nsk: %u\n", dsiclk_nsk);
	dev_dbg(priv->dev, "ui_nsk: %u\n", ui_nsk);
	dev_dbg(priv->dev, "dsibclk_nsk: %u\n", dsibclk_nsk);

	/* LP11 > 100us for D-PHY Rx Init */
	val = tc358768_ns_to_cnt(100 * 1000, dsibclk_nsk) - 1;
	dev_dbg(priv->dev, "LINEINITCNT: 0x%x\n", val);
	tc358768_write(priv, TC358768_LINEINITCNT, val);

	/* LPTimeCnt > 50ns */
	val = tc358768_ns_to_cnt(50, dsibclk_nsk) - 1;
	lptxcnt = val;
	dev_dbg(priv->dev, "LPTXTIMECNT: 0x%x\n", val);
	tc358768_write(priv, TC358768_LPTXTIMECNT, val);

	/* 38ns < TCLK_PREPARE < 95ns */
	val = tc358768_ns_to_cnt(65, dsibclk_nsk) - 1;
	/* TCLK_PREPARE + TCLK_ZERO > 300ns */
	val2 = tc358768_ns_to_cnt(300 - tc358768_to_ns(2 * ui_nsk),
				  dsibclk_nsk) - 2;
	val |= val2 << 8;
	dev_dbg(priv->dev, "TCLK_HEADERCNT: 0x%x\n", val);
	tc358768_write(priv, TC358768_TCLK_HEADERCNT, val);

	/* TCLK_TRAIL > 60ns AND TEOT <= 105 ns + 12*UI */
	raw_val = tc358768_ns_to_cnt(60 + tc358768_to_ns(2 * ui_nsk), dsibclk_nsk) - 5;
	val = clamp(raw_val, 0, 127);
	dev_dbg(priv->dev, "TCLK_TRAILCNT: 0x%x\n", val);
	tc358768_write(priv, TC358768_TCLK_TRAILCNT, val);

	/* 40ns + 4*UI < THS_PREPARE < 85ns + 6*UI */
	val = 50 + tc358768_to_ns(4 * ui_nsk);
	val = tc358768_ns_to_cnt(val, dsibclk_nsk) - 1;
	/* THS_PREPARE + THS_ZERO > 145ns + 10*UI */
	raw_val = tc358768_ns_to_cnt(145 - tc358768_to_ns(3 * ui_nsk), dsibclk_nsk) - 10;
	val2 = clamp(raw_val, 0, 127);
	val |= val2 << 8;
	dev_dbg(priv->dev, "THS_HEADERCNT: 0x%x\n", val);
	tc358768_write(priv, TC358768_THS_HEADERCNT, val);

	/* TWAKEUP > 1ms in lptxcnt steps */
	val = tc358768_ns_to_cnt(1020000, dsibclk_nsk);
	val = val / (lptxcnt + 1) - 1;
	dev_dbg(priv->dev, "TWAKEUP: 0x%x\n", val);
	tc358768_write(priv, TC358768_TWAKEUP, val);

	/* TCLK_POSTCNT > 60ns + 52*UI */
	val = tc358768_ns_to_cnt(60 + tc358768_to_ns(52 * ui_nsk),
				 dsibclk_nsk) - 3;
	dev_dbg(priv->dev, "TCLK_POSTCNT: 0x%x\n", val);
	tc358768_write(priv, TC358768_TCLK_POSTCNT, val);

	/* max(60ns + 4*UI, 8*UI) < THS_TRAILCNT < 105ns + 12*UI */
	raw_val = tc358768_ns_to_cnt(60 + tc358768_to_ns(18 * ui_nsk),
				     dsibclk_nsk) - 4;
	val = clamp(raw_val, 0, 15);
	dev_dbg(priv->dev, "THS_TRAILCNT: 0x%x\n", val);
	tc358768_write(priv, TC358768_THS_TRAILCNT, val);

	val = BIT(0);
	for (i = 0; i < dsi_dev->lanes; i++)
		val |= BIT(i + 1);
	tc358768_write(priv, TC358768_HSTXVREGEN, val);

	tc358768_write(priv, TC358768_TXOPTIONCNTRL,
		       (mode_flags & MIPI_DSI_CLOCK_NON_CONTINUOUS) ? 0 : BIT(0));

	/* TXTAGOCNT[26:16] RXTASURECNT[10:0] */
	val = tc358768_to_ns((lptxcnt + 1) * dsibclk_nsk * 4);
	val = tc358768_ns_to_cnt(val, dsibclk_nsk) / 4 - 1;
	val2 = tc358768_ns_to_cnt(tc358768_to_ns((lptxcnt + 1) * dsibclk_nsk),
				  dsibclk_nsk) - 2;
	val = val << 16 | val2;
	dev_dbg(priv->dev, "BTACNTRL1: 0x%x\n", val);
	tc358768_write(priv, TC358768_BTACNTRL1, val);

	/* START[0] */
	tc358768_write(priv, TC358768_STARTCNTRL, 1);

	if (dsi_dev->mode_flags & MIPI_DSI_MODE_VIDEO_SYNC_PULSE) {
		/* Set pulse mode */
		tc358768_write(priv, TC358768_DSI_EVENT, 0);

		/* vact */
		tc358768_write(priv, TC358768_DSI_VACT, mode->vdisplay);

		/* vsw */
		tc358768_write(priv, TC358768_DSI_VSW,
			       mode->vsync_end - mode->vsync_start);
		/* vbp */
		tc358768_write(priv, TC358768_DSI_VBPR,
			       mode->vtotal - mode->vsync_end);

		/* hsw * byteclk * ndl / pclk */
		val = (u32)div_u64((mode->hsync_end - mode->hsync_start) *
				   ((u64)priv->dsiclk / 4) * priv->dsi_lanes,
				   mode->clock * 1000);
		tc358768_write(priv, TC358768_DSI_HSW, val);

		/* hbp * byteclk * ndl / pclk */
		val = (u32)div_u64((mode->htotal - mode->hsync_end) *
				   ((u64)priv->dsiclk / 4) * priv->dsi_lanes,
				   mode->clock * 1000);
		tc358768_write(priv, TC358768_DSI_HBPR, val);
	} else {
		/* Set event mode */
		tc358768_write(priv, TC358768_DSI_EVENT, 1);

		/* vact */
		tc358768_write(priv, TC358768_DSI_VACT, mode->vdisplay);

		/* vsw (+ vbp) */
		tc358768_write(priv, TC358768_DSI_VSW,
			       mode->vtotal - mode->vsync_start);
		/* vbp (not used in event mode) */
		tc358768_write(priv, TC358768_DSI_VBPR, 0);

		/* (hsw + hbp) * byteclk * ndl / pclk */
		val = (u32)div_u64((mode->htotal - mode->hsync_start) *
				   ((u64)priv->dsiclk / 4) * priv->dsi_lanes,
				   mode->clock * 1000);
		tc358768_write(priv, TC358768_DSI_HSW, val);

		/* hbp (not used in event mode) */
		tc358768_write(priv, TC358768_DSI_HBPR, 0);
	}

	/* hact (bytes) */
	tc358768_write(priv, TC358768_DSI_HACT, hact);

	/* VSYNC polarity */
	tc358768_update_bits(priv, TC358768_CONFCTL, BIT(5),
			     (mode->flags & DRM_MODE_FLAG_PVSYNC) ? BIT(5) : 0);

	/* HSYNC polarity */
	tc358768_update_bits(priv, TC358768_PP_MISC, BIT(0),
			     (mode->flags & DRM_MODE_FLAG_PHSYNC) ? BIT(0) : 0);

	/* Start DSI Tx */
	tc358768_write(priv, TC358768_DSI_START, 0x1);

	/* Configure DSI_Control register */
	val = TC358768_DSI_CONFW_MODE_CLR | TC358768_DSI_CONFW_ADDR_DSI_CONTROL;
	val |= TC358768_DSI_CONTROL_TXMD | TC358768_DSI_CONTROL_HSCKMD |
	       0x3 << 1 | TC358768_DSI_CONTROL_EOTDIS;
	tc358768_write(priv, TC358768_DSI_CONFW, val);

	val = TC358768_DSI_CONFW_MODE_SET | TC358768_DSI_CONFW_ADDR_DSI_CONTROL;
	val |= (dsi_dev->lanes - 1) << 1;

	val |= TC358768_DSI_CONTROL_TXMD;

	if (!(mode_flags & MIPI_DSI_CLOCK_NON_CONTINUOUS))
		val |= TC358768_DSI_CONTROL_HSCKMD;

	if (dsi_dev->mode_flags & MIPI_DSI_MODE_NO_EOT_PACKET)
		val |= TC358768_DSI_CONTROL_EOTDIS;

	tc358768_write(priv, TC358768_DSI_CONFW, val);

	val = TC358768_DSI_CONFW_MODE_CLR | TC358768_DSI_CONFW_ADDR_DSI_CONTROL;
	val |= TC358768_DSI_CONTROL_DIS_MODE; /* DSI mode */
	tc358768_write(priv, TC358768_DSI_CONFW, val);

	ret = tc358768_clear_error(priv);
	if (ret) {
		dev_err(priv->dev, "Bridge pre_enable failed: %d\n", ret);
		tc358768_bridge_disable(bridge);
		tc358768_bridge_post_disable(bridge);
	}
}

static void tc358768_bridge_enable(struct drm_bridge *bridge)
{
	struct tc358768_priv *priv = bridge_to_tc358768(bridge);
	int ret;

	if (!priv->enabled) {
		dev_err(priv->dev, "Bridge is not enabled\n");
		return;
	}

	/* clear FrmStop and RstPtr */
	tc358768_update_bits(priv, TC358768_PP_MISC, 0x3 << 14, 0);

	/* set PP_en */
	tc358768_update_bits(priv, TC358768_CONFCTL, BIT(6), BIT(6));

	ret = tc358768_clear_error(priv);
	if (ret) {
		dev_err(priv->dev, "Bridge enable failed: %d\n", ret);
		tc358768_bridge_disable(bridge);
		tc358768_bridge_post_disable(bridge);
	}
}

#define MAX_INPUT_SEL_FORMATS	1

static u32 *
tc358768_atomic_get_input_bus_fmts(struct drm_bridge *bridge,
				   struct drm_bridge_state *bridge_state,
				   struct drm_crtc_state *crtc_state,
				   struct drm_connector_state *conn_state,
				   u32 output_fmt,
				   unsigned int *num_input_fmts)
{
	struct tc358768_priv *priv = bridge_to_tc358768(bridge);
	u32 *input_fmts;

	*num_input_fmts = 0;

	input_fmts = kcalloc(MAX_INPUT_SEL_FORMATS, sizeof(*input_fmts),
			     GFP_KERNEL);
	if (!input_fmts)
		return NULL;

	switch (priv->pd_lines) {
	case 16:
		input_fmts[0] = MEDIA_BUS_FMT_RGB565_1X16;
		break;
	case 18:
		input_fmts[0] = MEDIA_BUS_FMT_RGB666_1X18;
		break;
	default:
	case 24:
		input_fmts[0] = MEDIA_BUS_FMT_RGB888_1X24;
		break;
	};

	*num_input_fmts = MAX_INPUT_SEL_FORMATS;

	return input_fmts;
}

static const struct drm_bridge_funcs tc358768_bridge_funcs = {
	.attach = tc358768_bridge_attach,
	.mode_valid = tc358768_bridge_mode_valid,
	.pre_enable = tc358768_bridge_pre_enable,
	.enable = tc358768_bridge_enable,
	.disable = tc358768_bridge_disable,
	.post_disable = tc358768_bridge_post_disable,

	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset = drm_atomic_helper_bridge_reset,
	.atomic_get_input_bus_fmts = tc358768_atomic_get_input_bus_fmts,
};

static const struct drm_bridge_timings default_tc358768_timings = {
	.input_bus_flags = DRM_BUS_FLAG_PIXDATA_SAMPLE_POSEDGE
		 | DRM_BUS_FLAG_SYNC_SAMPLE_NEGEDGE
		 | DRM_BUS_FLAG_DE_HIGH,
};

static bool tc358768_is_reserved_reg(unsigned int reg)
{
	switch (reg) {
	case 0x114 ... 0x13f:
	case 0x200:
	case 0x20c:
	case 0x400 ... 0x408:
	case 0x41c ... 0x42f:
		return true;
	default:
		return false;
	}
}

static bool tc358768_writeable_reg(struct device *dev, unsigned int reg)
{
	if (tc358768_is_reserved_reg(reg))
		return false;

	switch (reg) {
	case TC358768_CHIPID:
	case TC358768_FIFOSTATUS:
	case TC358768_DSITXSTATUS ... (TC358768_DSITXSTATUS + 2):
	case TC358768_DSI_CONTROL ... (TC358768_DSI_INT_ENA + 2):
	case TC358768_DSICMD_RDFIFO ... (TC358768_DSI_ERR_HALT + 2):
		return false;
	default:
		return true;
	}
}

static bool tc358768_readable_reg(struct device *dev, unsigned int reg)
{
	if (tc358768_is_reserved_reg(reg))
		return false;

	switch (reg) {
	case TC358768_STARTCNTRL:
	case TC358768_DSI_CONFW ... (TC358768_DSI_CONFW + 2):
	case TC358768_DSI_INT_CLR ... (TC358768_DSI_INT_CLR + 2):
	case TC358768_DSI_START ... (TC358768_DSI_START + 2):
	case TC358768_DBG_DATA:
		return false;
	default:
		return true;
	}
}

static const struct regmap_config tc358768_regmap_config = {
	.name = "tc358768",
	.reg_bits = 16,
	.val_bits = 16,
	.max_register = TC358768_DSI_HACT,
	.cache_type = REGCACHE_NONE,
	.writeable_reg = tc358768_writeable_reg,
	.readable_reg = tc358768_readable_reg,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_BIG,
};

static const struct i2c_device_id tc358768_i2c_ids[] = {
	{ "tc358768", 0 },
	{ "tc358778", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tc358768_i2c_ids);

static const struct of_device_id tc358768_of_ids[] = {
	{ .compatible = "toshiba,tc358768", },
	{ .compatible = "toshiba,tc358778", },
	{ }
};
MODULE_DEVICE_TABLE(of, tc358768_of_ids);

static int tc358768_get_regulators(struct tc358768_priv *priv)
{
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(priv->supplies); ++i)
		priv->supplies[i].supply = tc358768_supplies[i];

	ret = devm_regulator_bulk_get(priv->dev, ARRAY_SIZE(priv->supplies),
				      priv->supplies);
	if (ret < 0)
		dev_err(priv->dev, "failed to get regulators: %d\n", ret);

	return ret;
}

static int tc358768_i2c_probe(struct i2c_client *client,
			      const struct i2c_device_id *id)
{
	struct tc358768_priv *priv;
	struct device *dev = &client->dev;
	struct device_node *np = dev->of_node;
	int ret;

	if (!np)
		return -ENODEV;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	dev_set_drvdata(dev, priv);
	priv->dev = dev;

	ret = tc358768_get_regulators(priv);
	if (ret)
		return ret;

	priv->refclk = devm_clk_get(dev, "refclk");
	if (IS_ERR(priv->refclk))
		return PTR_ERR(priv->refclk);

	/*
	 * RESX is low active, to disable tc358768 initially (keep in reset)
	 * the gpio line must be LOW. This is the ASSERTED state of
	 * GPIO_ACTIVE_LOW (GPIOD_OUT_HIGH == ASSERTED).
	 */
	priv->reset_gpio  = devm_gpiod_get_optional(dev, "reset",
						    GPIOD_OUT_HIGH);
	if (IS_ERR(priv->reset_gpio))
		return PTR_ERR(priv->reset_gpio);

	priv->regmap = devm_regmap_init_i2c(client, &tc358768_regmap_config);
	if (IS_ERR(priv->regmap)) {
		dev_err(dev, "Failed to init regmap\n");
		return PTR_ERR(priv->regmap);
	}

	priv->dsi_host.dev = dev;
	priv->dsi_host.ops = &tc358768_dsi_host_ops;

	priv->bridge.funcs = &tc358768_bridge_funcs;
	priv->bridge.timings = &default_tc358768_timings;
	priv->bridge.of_node = np;

	i2c_set_clientdata(client, priv);

	return mipi_dsi_host_register(&priv->dsi_host);
}

static void tc358768_i2c_remove(struct i2c_client *client)
{
	struct tc358768_priv *priv = i2c_get_clientdata(client);

	mipi_dsi_host_unregister(&priv->dsi_host);
}

static struct i2c_driver tc358768_driver = {
	.driver = {
		.name = "tc358768",
		.of_match_table = tc358768_of_ids,
	},
	.id_table = tc358768_i2c_ids,
	.probe = tc358768_i2c_probe,
	.remove	= tc358768_i2c_remove,
};
module_i2c_driver(tc358768_driver);

MODULE_AUTHOR("Peter Ujfalusi <peter.ujfalusi@ti.com>");
MODULE_DESCRIPTION("TC358768AXBG/TC358778XBG DSI bridge");
MODULE_LICENSE("GPL v2");
