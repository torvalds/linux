// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) STMicroelectronics SA 2017
 *
 * Authors: Philippe Cornu <philippe.cornu@st.com>
 *          Yannick Fertre <yannick.fertre@st.com>
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/bridge/dw_mipi_dsi.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_print.h>

#define HWVER_130			0x31333000	/* IP version 1.30 */
#define HWVER_131			0x31333100	/* IP version 1.31 */

/* DSI digital registers & bit definitions */
#define DSI_VERSION			0x00
#define VERSION				GENMASK(31, 8)

/* DSI wrapper registers & bit definitions */
/* Note: registers are named as in the Reference Manual */
#define DSI_WCFGR	0x0400		/* Wrapper ConFiGuration Reg */
#define WCFGR_DSIM	BIT(0)		/* DSI Mode */
#define WCFGR_COLMUX	GENMASK(3, 1)	/* COLor MUltipleXing */

#define DSI_WCR		0x0404		/* Wrapper Control Reg */
#define WCR_DSIEN	BIT(3)		/* DSI ENable */

#define DSI_WISR	0x040C		/* Wrapper Interrupt and Status Reg */
#define WISR_PLLLS	BIT(8)		/* PLL Lock Status */
#define WISR_RRS	BIT(12)		/* Regulator Ready Status */

#define DSI_WPCR0	0x0418		/* Wrapper Phy Conf Reg 0 */
#define WPCR0_UIX4	GENMASK(5, 0)	/* Unit Interval X 4 */
#define WPCR0_TDDL	BIT(16)		/* Turn Disable Data Lanes */

#define DSI_WRPCR	0x0430		/* Wrapper Regulator & Pll Ctrl Reg */
#define WRPCR_PLLEN	BIT(0)		/* PLL ENable */
#define WRPCR_NDIV	GENMASK(8, 2)	/* pll loop DIVision Factor */
#define WRPCR_IDF	GENMASK(14, 11)	/* pll Input Division Factor */
#define WRPCR_ODF	GENMASK(17, 16)	/* pll Output Division Factor */
#define WRPCR_REGEN	BIT(24)		/* REGulator ENable */
#define WRPCR_BGREN	BIT(28)		/* BandGap Reference ENable */
#define IDF_MIN		1
#define IDF_MAX		7
#define NDIV_MIN	10
#define NDIV_MAX	125
#define ODF_MIN		1
#define ODF_MAX		8

/* dsi color format coding according to the datasheet */
enum dsi_color {
	DSI_RGB565_CONF1,
	DSI_RGB565_CONF2,
	DSI_RGB565_CONF3,
	DSI_RGB666_CONF1,
	DSI_RGB666_CONF2,
	DSI_RGB888,
};

#define LANE_MIN_KBPS	31250
#define LANE_MAX_KBPS	500000

/* Sleep & timeout for regulator on/off, pll lock/unlock & fifo empty */
#define SLEEP_US	1000
#define TIMEOUT_US	200000

struct dw_mipi_dsi_stm {
	void __iomem *base;
	struct device *dev;
	struct clk *pllref_clk;
	struct clk *pclk;
	struct clk_hw txbyte_clk;
	struct dw_mipi_dsi *dsi;
	struct dw_mipi_dsi_plat_data pdata;
	u32 hw_version;
	int lane_min_kbps;
	int lane_max_kbps;
	struct regulator *vdd_supply;
};

static inline void dsi_write(struct dw_mipi_dsi_stm *dsi, u32 reg, u32 val)
{
	writel(val, dsi->base + reg);
}

static inline u32 dsi_read(struct dw_mipi_dsi_stm *dsi, u32 reg)
{
	return readl(dsi->base + reg);
}

static inline void dsi_set(struct dw_mipi_dsi_stm *dsi, u32 reg, u32 mask)
{
	dsi_write(dsi, reg, dsi_read(dsi, reg) | mask);
}

static inline void dsi_clear(struct dw_mipi_dsi_stm *dsi, u32 reg, u32 mask)
{
	dsi_write(dsi, reg, dsi_read(dsi, reg) & ~mask);
}

static inline void dsi_update_bits(struct dw_mipi_dsi_stm *dsi, u32 reg,
				   u32 mask, u32 val)
{
	dsi_write(dsi, reg, (dsi_read(dsi, reg) & ~mask) | val);
}

static enum dsi_color dsi_color_from_mipi(enum mipi_dsi_pixel_format fmt)
{
	switch (fmt) {
	case MIPI_DSI_FMT_RGB888:
		return DSI_RGB888;
	case MIPI_DSI_FMT_RGB666:
		return DSI_RGB666_CONF2;
	case MIPI_DSI_FMT_RGB666_PACKED:
		return DSI_RGB666_CONF1;
	case MIPI_DSI_FMT_RGB565:
		return DSI_RGB565_CONF1;
	default:
		DRM_DEBUG_DRIVER("MIPI color invalid, so we use rgb888\n");
	}
	return DSI_RGB888;
}

static int dsi_pll_get_clkout_khz(int clkin_khz, int idf, int ndiv, int odf)
{
	int divisor = idf * odf;

	/* prevent from division by 0 */
	if (!divisor)
		return 0;

	return DIV_ROUND_CLOSEST(clkin_khz * ndiv, divisor);
}

static int dsi_pll_get_params(struct dw_mipi_dsi_stm *dsi,
			      int clkin_khz, int clkout_khz,
			      int *idf, int *ndiv, int *odf)
{
	int i, o, n, n_min, n_max;
	int fvco_min, fvco_max, delta, best_delta; /* all in khz */

	/* Early checks preventing division by 0 & odd results */
	if (clkin_khz <= 0 || clkout_khz <= 0)
		return -EINVAL;

	fvco_min = dsi->lane_min_kbps * 2 * ODF_MAX;
	fvco_max = dsi->lane_max_kbps * 2 * ODF_MIN;

	best_delta = 1000000; /* big started value (1000000khz) */

	for (i = IDF_MIN; i <= IDF_MAX; i++) {
		/* Compute ndiv range according to Fvco */
		n_min = ((fvco_min * i) / (2 * clkin_khz)) + 1;
		n_max = (fvco_max * i) / (2 * clkin_khz);

		/* No need to continue idf loop if we reach ndiv max */
		if (n_min >= NDIV_MAX)
			break;

		/* Clamp ndiv to valid values */
		if (n_min < NDIV_MIN)
			n_min = NDIV_MIN;
		if (n_max > NDIV_MAX)
			n_max = NDIV_MAX;

		for (o = ODF_MIN; o <= ODF_MAX; o *= 2) {
			n = DIV_ROUND_CLOSEST(i * o * clkout_khz, clkin_khz);
			/* Check ndiv according to vco range */
			if (n < n_min || n > n_max)
				continue;
			/* Check if new delta is better & saves parameters */
			delta = dsi_pll_get_clkout_khz(clkin_khz, i, n, o) -
				clkout_khz;
			if (delta < 0)
				delta = -delta;
			if (delta < best_delta) {
				*idf = i;
				*ndiv = n;
				*odf = o;
				best_delta = delta;
			}
			/* fast return in case of "perfect result" */
			if (!delta)
				return 0;
		}
	}

	return 0;
}

#define clk_to_dw_mipi_dsi_stm(clk) \
	container_of(clk, struct dw_mipi_dsi_stm, txbyte_clk)

static void dw_mipi_dsi_clk_disable(struct clk_hw *clk)
{
	struct dw_mipi_dsi_stm *dsi = clk_to_dw_mipi_dsi_stm(clk);

	DRM_DEBUG_DRIVER("\n");

	/* Disable the DSI PLL */
	dsi_clear(dsi, DSI_WRPCR, WRPCR_PLLEN);

	/* Disable the regulator */
	dsi_clear(dsi, DSI_WRPCR, WRPCR_REGEN | WRPCR_BGREN);
}

static int dw_mipi_dsi_clk_enable(struct clk_hw *clk)
{
	struct dw_mipi_dsi_stm *dsi = clk_to_dw_mipi_dsi_stm(clk);
	u32 val;
	int ret;

	DRM_DEBUG_DRIVER("\n");

	/* Enable the regulator */
	dsi_set(dsi, DSI_WRPCR, WRPCR_REGEN | WRPCR_BGREN);
	ret = readl_poll_timeout_atomic(dsi->base + DSI_WISR, val, val & WISR_RRS,
					SLEEP_US, TIMEOUT_US);
	if (ret)
		DRM_DEBUG_DRIVER("!TIMEOUT! waiting REGU, let's continue\n");

	/* Enable the DSI PLL & wait for its lock */
	dsi_set(dsi, DSI_WRPCR, WRPCR_PLLEN);
	ret = readl_poll_timeout_atomic(dsi->base + DSI_WISR, val, val & WISR_PLLLS,
					SLEEP_US, TIMEOUT_US);
	if (ret)
		DRM_DEBUG_DRIVER("!TIMEOUT! waiting PLL, let's continue\n");

	return 0;
}

static int dw_mipi_dsi_clk_is_enabled(struct clk_hw *hw)
{
	struct dw_mipi_dsi_stm *dsi = clk_to_dw_mipi_dsi_stm(hw);

	return dsi_read(dsi, DSI_WRPCR) & WRPCR_PLLEN;
}

static unsigned long dw_mipi_dsi_clk_recalc_rate(struct clk_hw *hw,
						 unsigned long parent_rate)
{
	struct dw_mipi_dsi_stm *dsi = clk_to_dw_mipi_dsi_stm(hw);
	unsigned int idf, ndiv, odf, pll_in_khz, pll_out_khz;
	u32 val;

	DRM_DEBUG_DRIVER("\n");

	pll_in_khz = (unsigned int)(parent_rate / 1000);

	val = dsi_read(dsi, DSI_WRPCR);

	idf = (val & WRPCR_IDF) >> 11;
	if (!idf)
		idf = 1;
	ndiv = (val & WRPCR_NDIV) >> 2;
	odf = int_pow(2, (val & WRPCR_ODF) >> 16);

	/* Get the adjusted pll out value */
	pll_out_khz = dsi_pll_get_clkout_khz(pll_in_khz, idf, ndiv, odf);

	return (unsigned long)pll_out_khz * 1000;
}

static long dw_mipi_dsi_clk_round_rate(struct clk_hw *hw, unsigned long rate,
				       unsigned long *parent_rate)
{
	struct dw_mipi_dsi_stm *dsi = clk_to_dw_mipi_dsi_stm(hw);
	unsigned int idf, ndiv, odf, pll_in_khz, pll_out_khz;
	int ret;

	DRM_DEBUG_DRIVER("\n");

	pll_in_khz = (unsigned int)(*parent_rate / 1000);

	/* Compute best pll parameters */
	idf = 0;
	ndiv = 0;
	odf = 0;

	ret = dsi_pll_get_params(dsi, pll_in_khz, rate / 1000,
				 &idf, &ndiv, &odf);
	if (ret)
		DRM_WARN("Warning dsi_pll_get_params(): bad params\n");

	/* Get the adjusted pll out value */
	pll_out_khz = dsi_pll_get_clkout_khz(pll_in_khz, idf, ndiv, odf);

	return pll_out_khz * 1000;
}

static int dw_mipi_dsi_clk_set_rate(struct clk_hw *hw, unsigned long rate,
				    unsigned long parent_rate)
{
	struct dw_mipi_dsi_stm *dsi = clk_to_dw_mipi_dsi_stm(hw);
	unsigned int idf, ndiv, odf, pll_in_khz, pll_out_khz;
	int ret;
	u32 val;

	DRM_DEBUG_DRIVER("\n");

	pll_in_khz = (unsigned int)(parent_rate / 1000);

	/* Compute best pll parameters */
	idf = 0;
	ndiv = 0;
	odf = 0;

	ret = dsi_pll_get_params(dsi, pll_in_khz, rate / 1000, &idf, &ndiv, &odf);
	if (ret)
		DRM_WARN("Warning dsi_pll_get_params(): bad params\n");

	/* Get the adjusted pll out value */
	pll_out_khz = dsi_pll_get_clkout_khz(pll_in_khz, idf, ndiv, odf);

	/* Set the PLL division factors */
	dsi_update_bits(dsi, DSI_WRPCR,	WRPCR_NDIV | WRPCR_IDF | WRPCR_ODF,
			(ndiv << 2) | (idf << 11) | ((ffs(odf) - 1) << 16));

	/* Compute uix4 & set the bit period in high-speed mode */
	val = 4000000 / pll_out_khz;
	dsi_update_bits(dsi, DSI_WPCR0, WPCR0_UIX4, val);

	return 0;
}

static void dw_mipi_dsi_clk_unregister(void *data)
{
	struct dw_mipi_dsi_stm *dsi = data;

	DRM_DEBUG_DRIVER("\n");

	of_clk_del_provider(dsi->dev->of_node);
	clk_hw_unregister(&dsi->txbyte_clk);
}

static const struct clk_ops dw_mipi_dsi_stm_clk_ops = {
	.enable = dw_mipi_dsi_clk_enable,
	.disable = dw_mipi_dsi_clk_disable,
	.is_enabled = dw_mipi_dsi_clk_is_enabled,
	.recalc_rate = dw_mipi_dsi_clk_recalc_rate,
	.round_rate = dw_mipi_dsi_clk_round_rate,
	.set_rate = dw_mipi_dsi_clk_set_rate,
};

static struct clk_init_data cdata_init = {
	.name = "ck_dsi_phy",
	.ops = &dw_mipi_dsi_stm_clk_ops,
	.parent_names = (const char * []) {"ck_hse"},
	.num_parents = 1,
};

static int dw_mipi_dsi_clk_register(struct dw_mipi_dsi_stm *dsi,
				    struct device *dev)
{
	struct device_node *node = dev->of_node;
	int ret;

	DRM_DEBUG_DRIVER("Registering clk\n");

	dsi->txbyte_clk.init = &cdata_init;

	ret = clk_hw_register(dev, &dsi->txbyte_clk);
	if (ret)
		return ret;

	ret = of_clk_add_hw_provider(node, of_clk_hw_simple_get,
				     &dsi->txbyte_clk);
	if (ret)
		clk_hw_unregister(&dsi->txbyte_clk);

	return ret;
}

static int dw_mipi_dsi_phy_init(void *priv_data)
{
	struct dw_mipi_dsi_stm *dsi = priv_data;
	int ret;

	ret = clk_prepare_enable(dsi->txbyte_clk.clk);
	return ret;
}

static void dw_mipi_dsi_phy_power_on(void *priv_data)
{
	struct dw_mipi_dsi_stm *dsi = priv_data;

	DRM_DEBUG_DRIVER("\n");

	/* Enable the DSI wrapper */
	dsi_set(dsi, DSI_WCR, WCR_DSIEN);
}

static void dw_mipi_dsi_phy_power_off(void *priv_data)
{
	struct dw_mipi_dsi_stm *dsi = priv_data;

	DRM_DEBUG_DRIVER("\n");

	clk_disable_unprepare(dsi->txbyte_clk.clk);

	/* Disable the DSI wrapper */
	dsi_clear(dsi, DSI_WCR, WCR_DSIEN);
}

static int
dw_mipi_dsi_get_lane_mbps(void *priv_data, const struct drm_display_mode *mode,
			  unsigned long mode_flags, u32 lanes, u32 format,
			  unsigned int *lane_mbps)
{
	struct dw_mipi_dsi_stm *dsi = priv_data;
	unsigned int pll_in_khz, pll_out_khz;
	int ret, bpp;

	pll_in_khz = (unsigned int)(clk_get_rate(dsi->pllref_clk) / 1000);

	/* Compute requested pll out */
	bpp = mipi_dsi_pixel_format_to_bpp(format);
	pll_out_khz = mode->clock * bpp / lanes;

	/* Add 20% to pll out to be higher than pixel bw (burst mode only) */
	if (mode_flags & MIPI_DSI_MODE_VIDEO_BURST)
		pll_out_khz = (pll_out_khz * 12) / 10;

	if (pll_out_khz > dsi->lane_max_kbps) {
		pll_out_khz = dsi->lane_max_kbps;
		DRM_WARN("Warning max phy mbps is used\n");
	}
	if (pll_out_khz < dsi->lane_min_kbps) {
		pll_out_khz = dsi->lane_min_kbps;
		DRM_WARN("Warning min phy mbps is used\n");
	}

	ret = clk_set_rate((dsi->txbyte_clk.clk), pll_out_khz * 1000);
	if (ret)
		DRM_DEBUG_DRIVER("ERROR Could not set rate of %d to %s clk->name",
				 pll_out_khz, clk_hw_get_name(&dsi->txbyte_clk));

	/* Select video mode by resetting DSIM bit */
	dsi_clear(dsi, DSI_WCFGR, WCFGR_DSIM);

	/* Select the color coding */
	dsi_update_bits(dsi, DSI_WCFGR, WCFGR_COLMUX,
			dsi_color_from_mipi(format) << 1);

	*lane_mbps = pll_out_khz / 1000;

	DRM_DEBUG_DRIVER("pll_in %ukHz pll_out %ukHz lane_mbps %uMHz\n",
			 pll_in_khz, pll_out_khz, *lane_mbps);

	return 0;
}

#define DSI_PHY_DELAY(fp, vp, mbps) DIV_ROUND_UP((fp) * (mbps) + 1000 * (vp), 8000)

static int
dw_mipi_dsi_phy_get_timing(void *priv_data, unsigned int lane_mbps,
			   struct dw_mipi_dsi_dphy_timing *timing)
{
	/*
	 * From STM32MP157 datasheet, valid for STM32F469, STM32F7x9, STM32H747
	 * phy_clkhs2lp_time = (272+136*UI)/(8*UI)
	 * phy_clklp2hs_time = (512+40*UI)/(8*UI)
	 * phy_hs2lp_time = (192+64*UI)/(8*UI)
	 * phy_lp2hs_time = (256+32*UI)/(8*UI)
	 */
	timing->clk_hs2lp = DSI_PHY_DELAY(272, 136, lane_mbps);
	timing->clk_lp2hs = DSI_PHY_DELAY(512, 40, lane_mbps);
	timing->data_hs2lp = DSI_PHY_DELAY(192, 64, lane_mbps);
	timing->data_lp2hs = DSI_PHY_DELAY(256, 32, lane_mbps);

	return 0;
}

#define CLK_TOLERANCE_HZ 50

static enum drm_mode_status
dw_mipi_dsi_stm_mode_valid(void *priv_data,
			   const struct drm_display_mode *mode,
			   unsigned long mode_flags, u32 lanes, u32 format)
{
	struct dw_mipi_dsi_stm *dsi = priv_data;
	unsigned int idf, ndiv, odf, pll_in_khz, pll_out_khz;
	int ret, bpp;

	bpp = mipi_dsi_pixel_format_to_bpp(format);
	if (bpp < 0)
		return MODE_BAD;

	/* Compute requested pll out */
	pll_out_khz = mode->clock * bpp / lanes;

	if (pll_out_khz > dsi->lane_max_kbps)
		return MODE_CLOCK_HIGH;

	if (mode_flags & MIPI_DSI_MODE_VIDEO_BURST) {
		/* Add 20% to pll out to be higher than pixel bw */
		pll_out_khz = (pll_out_khz * 12) / 10;
	} else {
		if (pll_out_khz < dsi->lane_min_kbps)
			return MODE_CLOCK_LOW;
	}

	/* Compute best pll parameters */
	idf = 0;
	ndiv = 0;
	odf = 0;
	pll_in_khz = clk_get_rate(dsi->pllref_clk) / 1000;
	ret = dsi_pll_get_params(dsi, pll_in_khz, pll_out_khz, &idf, &ndiv, &odf);
	if (ret) {
		DRM_WARN("Warning dsi_pll_get_params(): bad params\n");
		return MODE_ERROR;
	}

	if (!(mode_flags & MIPI_DSI_MODE_VIDEO_BURST)) {
		unsigned int px_clock_hz, target_px_clock_hz, lane_mbps;
		int dsi_short_packet_size_px, hfp, hsync, hbp, delay_to_lp;
		struct dw_mipi_dsi_dphy_timing dphy_timing;

		/* Get the adjusted pll out value */
		pll_out_khz = dsi_pll_get_clkout_khz(pll_in_khz, idf, ndiv, odf);

		px_clock_hz = DIV_ROUND_CLOSEST_ULL(1000ULL * pll_out_khz * lanes, bpp);
		target_px_clock_hz = mode->clock * 1000;
		/*
		 * Filter modes according to the clock value, particularly useful for
		 * hdmi modes that require precise pixel clocks.
		 */
		if (px_clock_hz < target_px_clock_hz - CLK_TOLERANCE_HZ ||
		    px_clock_hz > target_px_clock_hz + CLK_TOLERANCE_HZ)
			return MODE_CLOCK_RANGE;

		/* sync packets are codes as DSI short packets (4 bytes) */
		dsi_short_packet_size_px = DIV_ROUND_UP(4 * BITS_PER_BYTE, bpp);

		hfp = mode->hsync_start - mode->hdisplay;
		hsync = mode->hsync_end - mode->hsync_start;
		hbp = mode->htotal - mode->hsync_end;

		/* hsync must be longer than 4 bytes HSS packets */
		if (hsync < dsi_short_packet_size_px)
			return MODE_HSYNC_NARROW;

		if (mode_flags & MIPI_DSI_MODE_VIDEO_SYNC_PULSE) {
			/* HBP must be longer than 4 bytes HSE packets */
			if (hbp < dsi_short_packet_size_px)
				return MODE_HSYNC_NARROW;
			hbp -= dsi_short_packet_size_px;
		} else {
			/* With sync events HBP extends in the hsync */
			hbp += hsync - dsi_short_packet_size_px;
		}

		lane_mbps = pll_out_khz / 1000;
		ret = dw_mipi_dsi_phy_get_timing(priv_data, lane_mbps, &dphy_timing);
		if (ret)
			return MODE_ERROR;
		/*
		 * In non-burst mode DSI has to enter in LP during HFP
		 * (horizontal front porch) or HBP (horizontal back porch) to
		 * resync with LTDC pixel clock.
		 */
		delay_to_lp = DIV_ROUND_UP((dphy_timing.data_hs2lp + dphy_timing.data_lp2hs) *
					   lanes * BITS_PER_BYTE, bpp);
		if (hfp < delay_to_lp && hbp < delay_to_lp)
			return MODE_HSYNC;
	}

	return MODE_OK;
}

static const struct dw_mipi_dsi_phy_ops dw_mipi_dsi_stm_phy_ops = {
	.init = dw_mipi_dsi_phy_init,
	.power_on = dw_mipi_dsi_phy_power_on,
	.power_off = dw_mipi_dsi_phy_power_off,
	.get_lane_mbps = dw_mipi_dsi_get_lane_mbps,
	.get_timing = dw_mipi_dsi_phy_get_timing,
};

static struct dw_mipi_dsi_plat_data dw_mipi_dsi_stm_plat_data = {
	.max_data_lanes = 2,
	.mode_valid = dw_mipi_dsi_stm_mode_valid,
	.phy_ops = &dw_mipi_dsi_stm_phy_ops,
};

static const struct of_device_id dw_mipi_dsi_stm_dt_ids[] = {
	{ .compatible = "st,stm32-dsi", .data = &dw_mipi_dsi_stm_plat_data, },
	{ },
};
MODULE_DEVICE_TABLE(of, dw_mipi_dsi_stm_dt_ids);

static int dw_mipi_dsi_stm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dw_mipi_dsi_stm *dsi;
	const struct dw_mipi_dsi_plat_data *pdata = of_device_get_match_data(dev);
	int ret;

	dsi = devm_kzalloc(dev, sizeof(*dsi), GFP_KERNEL);
	if (!dsi)
		return -ENOMEM;

	dsi->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(dsi->base)) {
		ret = PTR_ERR(dsi->base);
		DRM_ERROR("Unable to get dsi registers %d\n", ret);
		return ret;
	}

	dsi->vdd_supply = devm_regulator_get(dev, "phy-dsi");
	if (IS_ERR(dsi->vdd_supply)) {
		ret = PTR_ERR(dsi->vdd_supply);
		dev_err_probe(dev, ret, "Failed to request regulator\n");
		return ret;
	}

	ret = regulator_enable(dsi->vdd_supply);
	if (ret) {
		DRM_ERROR("Failed to enable regulator: %d\n", ret);
		return ret;
	}

	dsi->pllref_clk = devm_clk_get(dev, "ref");
	if (IS_ERR(dsi->pllref_clk)) {
		ret = PTR_ERR(dsi->pllref_clk);
		dev_err_probe(dev, ret, "Unable to get pll reference clock\n");
		goto err_clk_get;
	}

	ret = clk_prepare_enable(dsi->pllref_clk);
	if (ret) {
		DRM_ERROR("Failed to enable pllref_clk: %d\n", ret);
		goto err_clk_get;
	}

	dsi->pclk = devm_clk_get(dev, "pclk");
	if (IS_ERR(dsi->pclk)) {
		ret = PTR_ERR(dsi->pclk);
		DRM_ERROR("Unable to get peripheral clock: %d\n", ret);
		goto err_dsi_probe;
	}

	ret = clk_prepare_enable(dsi->pclk);
	if (ret) {
		DRM_ERROR("%s: Failed to enable peripheral clk\n", __func__);
		goto err_dsi_probe;
	}

	dsi->hw_version = dsi_read(dsi, DSI_VERSION) & VERSION;
	clk_disable_unprepare(dsi->pclk);

	if (dsi->hw_version != HWVER_130 && dsi->hw_version != HWVER_131) {
		ret = -ENODEV;
		DRM_ERROR("bad dsi hardware version\n");
		goto err_dsi_probe;
	}

	/* set lane capabilities according to hw version */
	dsi->lane_min_kbps = LANE_MIN_KBPS;
	dsi->lane_max_kbps = LANE_MAX_KBPS;
	if (dsi->hw_version == HWVER_131) {
		dsi->lane_min_kbps *= 2;
		dsi->lane_max_kbps *= 2;
	}

	dsi->pdata = *pdata;
	dsi->pdata.base = dsi->base;
	dsi->pdata.priv_data = dsi;

	dsi->pdata.max_data_lanes = 2;
	dsi->pdata.phy_ops = &dw_mipi_dsi_stm_phy_ops;

	platform_set_drvdata(pdev, dsi);

	dsi->dsi = dw_mipi_dsi_probe(pdev, &dsi->pdata);
	if (IS_ERR(dsi->dsi)) {
		ret = PTR_ERR(dsi->dsi);
		dev_err_probe(dev, ret, "Failed to initialize mipi dsi host\n");
		goto err_dsi_probe;
	}

	/*
	 * We need to wait for the generic bridge to probe before enabling and
	 * register the internal pixel clock.
	 */
	ret = clk_prepare_enable(dsi->pclk);
	if (ret) {
		DRM_ERROR("%s: Failed to enable peripheral clk\n", __func__);
		goto err_dsi_probe;
	}

	ret = dw_mipi_dsi_clk_register(dsi, dev);
	if (ret) {
		DRM_ERROR("Failed to register DSI pixel clock: %d\n", ret);
		clk_disable_unprepare(dsi->pclk);
		goto err_dsi_probe;
	}

	clk_disable_unprepare(dsi->pclk);

	return 0;

err_dsi_probe:
	clk_disable_unprepare(dsi->pllref_clk);
err_clk_get:
	regulator_disable(dsi->vdd_supply);

	return ret;
}

static void dw_mipi_dsi_stm_remove(struct platform_device *pdev)
{
	struct dw_mipi_dsi_stm *dsi = platform_get_drvdata(pdev);

	dw_mipi_dsi_remove(dsi->dsi);
	clk_disable_unprepare(dsi->pllref_clk);
	dw_mipi_dsi_clk_unregister(dsi);
	regulator_disable(dsi->vdd_supply);
}

static int dw_mipi_dsi_stm_suspend(struct device *dev)
{
	struct dw_mipi_dsi_stm *dsi = dev_get_drvdata(dev);

	DRM_DEBUG_DRIVER("\n");

	clk_disable_unprepare(dsi->pllref_clk);
	clk_disable_unprepare(dsi->pclk);
	regulator_disable(dsi->vdd_supply);

	return 0;
}

static int dw_mipi_dsi_stm_resume(struct device *dev)
{
	struct dw_mipi_dsi_stm *dsi = dev_get_drvdata(dev);
	int ret;

	DRM_DEBUG_DRIVER("\n");

	ret = regulator_enable(dsi->vdd_supply);
	if (ret) {
		DRM_ERROR("Failed to enable regulator: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(dsi->pclk);
	if (ret) {
		regulator_disable(dsi->vdd_supply);
		DRM_ERROR("Failed to enable pclk: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(dsi->pllref_clk);
	if (ret) {
		clk_disable_unprepare(dsi->pclk);
		regulator_disable(dsi->vdd_supply);
		DRM_ERROR("Failed to enable pllref_clk: %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct dev_pm_ops dw_mipi_dsi_stm_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(dw_mipi_dsi_stm_suspend,
			    dw_mipi_dsi_stm_resume)
	RUNTIME_PM_OPS(dw_mipi_dsi_stm_suspend,
		       dw_mipi_dsi_stm_resume, NULL)
};

static struct platform_driver dw_mipi_dsi_stm_driver = {
	.probe		= dw_mipi_dsi_stm_probe,
	.remove		= dw_mipi_dsi_stm_remove,
	.driver		= {
		.of_match_table = dw_mipi_dsi_stm_dt_ids,
		.name	= "stm32-display-dsi",
		.pm = &dw_mipi_dsi_stm_pm_ops,
	},
};

module_platform_driver(dw_mipi_dsi_stm_driver);

MODULE_AUTHOR("Philippe Cornu <philippe.cornu@st.com>");
MODULE_AUTHOR("Yannick Fertre <yannick.fertre@st.com>");
MODULE_DESCRIPTION("STMicroelectronics DW MIPI DSI host controller driver");
MODULE_LICENSE("GPL v2");
