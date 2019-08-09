// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) STMicroelectronics SA 2017
 *
 * Authors: Philippe Cornu <philippe.cornu@st.com>
 *          Yannick Fertre <yannick.fertre@st.com>
 */

#include <linux/clk.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <drm/drmP.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/bridge/dw_mipi_dsi.h>
#include <video/mipi_display.h>

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
	struct clk *pllref_clk;
	struct dw_mipi_dsi *dsi;
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

static int dw_mipi_dsi_phy_init(void *priv_data)
{
	struct dw_mipi_dsi_stm *dsi = priv_data;
	u32 val;
	int ret;

	/* Enable the regulator */
	dsi_set(dsi, DSI_WRPCR, WRPCR_REGEN | WRPCR_BGREN);
	ret = readl_poll_timeout(dsi->base + DSI_WISR, val, val & WISR_RRS,
				 SLEEP_US, TIMEOUT_US);
	if (ret)
		DRM_DEBUG_DRIVER("!TIMEOUT! waiting REGU, let's continue\n");

	/* Enable the DSI PLL & wait for its lock */
	dsi_set(dsi, DSI_WRPCR, WRPCR_PLLEN);
	ret = readl_poll_timeout(dsi->base + DSI_WISR, val, val & WISR_PLLLS,
				 SLEEP_US, TIMEOUT_US);
	if (ret)
		DRM_DEBUG_DRIVER("!TIMEOUT! waiting PLL, let's continue\n");

	return 0;
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

	/* Disable the DSI wrapper */
	dsi_clear(dsi, DSI_WCR, WCR_DSIEN);
}

static int
dw_mipi_dsi_get_lane_mbps(void *priv_data, const struct drm_display_mode *mode,
			  unsigned long mode_flags, u32 lanes, u32 format,
			  unsigned int *lane_mbps)
{
	struct dw_mipi_dsi_stm *dsi = priv_data;
	unsigned int idf, ndiv, odf, pll_in_khz, pll_out_khz;
	int ret, bpp;
	u32 val;

	/* Update lane capabilities according to hw version */
	dsi->lane_min_kbps = LANE_MIN_KBPS;
	dsi->lane_max_kbps = LANE_MAX_KBPS;
	if (dsi->hw_version == HWVER_131) {
		dsi->lane_min_kbps *= 2;
		dsi->lane_max_kbps *= 2;
	}

	pll_in_khz = (unsigned int)(clk_get_rate(dsi->pllref_clk) / 1000);

	/* Compute requested pll out */
	bpp = mipi_dsi_pixel_format_to_bpp(format);
	pll_out_khz = mode->clock * bpp / lanes;
	/* Add 20% to pll out to be higher than pixel bw (burst mode only) */
	pll_out_khz = (pll_out_khz * 12) / 10;
	if (pll_out_khz > dsi->lane_max_kbps) {
		pll_out_khz = dsi->lane_max_kbps;
		DRM_WARN("Warning max phy mbps is used\n");
	}
	if (pll_out_khz < dsi->lane_min_kbps) {
		pll_out_khz = dsi->lane_min_kbps;
		DRM_WARN("Warning min phy mbps is used\n");
	}

	/* Compute best pll parameters */
	idf = 0;
	ndiv = 0;
	odf = 0;
	ret = dsi_pll_get_params(dsi, pll_in_khz, pll_out_khz,
				 &idf, &ndiv, &odf);
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

static const struct dw_mipi_dsi_phy_ops dw_mipi_dsi_stm_phy_ops = {
	.init = dw_mipi_dsi_phy_init,
	.power_on = dw_mipi_dsi_phy_power_on,
	.power_off = dw_mipi_dsi_phy_power_off,
	.get_lane_mbps = dw_mipi_dsi_get_lane_mbps,
};

static struct dw_mipi_dsi_plat_data dw_mipi_dsi_stm_plat_data = {
	.max_data_lanes = 2,
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
	struct clk *pclk;
	struct resource *res;
	int ret;

	dsi = devm_kzalloc(dev, sizeof(*dsi), GFP_KERNEL);
	if (!dsi)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dsi->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(dsi->base)) {
		ret = PTR_ERR(dsi->base);
		DRM_ERROR("Unable to get dsi registers %d\n", ret);
		return ret;
	}

	dsi->vdd_supply = devm_regulator_get(dev, "phy-dsi");
	if (IS_ERR(dsi->vdd_supply)) {
		ret = PTR_ERR(dsi->vdd_supply);
		if (ret != -EPROBE_DEFER)
			DRM_ERROR("Failed to request regulator: %d\n", ret);
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
		DRM_ERROR("Unable to get pll reference clock: %d\n", ret);
		goto err_clk_get;
	}

	ret = clk_prepare_enable(dsi->pllref_clk);
	if (ret) {
		DRM_ERROR("Failed to enable pllref_clk: %d\n", ret);
		goto err_clk_get;
	}

	pclk = devm_clk_get(dev, "pclk");
	if (IS_ERR(pclk)) {
		ret = PTR_ERR(pclk);
		DRM_ERROR("Unable to get peripheral clock: %d\n", ret);
		goto err_dsi_probe;
	}

	ret = clk_prepare_enable(pclk);
	if (ret) {
		DRM_ERROR("%s: Failed to enable peripheral clk\n", __func__);
		goto err_dsi_probe;
	}

	dsi->hw_version = dsi_read(dsi, DSI_VERSION) & VERSION;
	clk_disable_unprepare(pclk);

	if (dsi->hw_version != HWVER_130 && dsi->hw_version != HWVER_131) {
		ret = -ENODEV;
		DRM_ERROR("bad dsi hardware version\n");
		goto err_dsi_probe;
	}

	dw_mipi_dsi_stm_plat_data.base = dsi->base;
	dw_mipi_dsi_stm_plat_data.priv_data = dsi;

	platform_set_drvdata(pdev, dsi);

	dsi->dsi = dw_mipi_dsi_probe(pdev, &dw_mipi_dsi_stm_plat_data);
	if (IS_ERR(dsi->dsi)) {
		ret = PTR_ERR(dsi->dsi);
		DRM_ERROR("Failed to initialize mipi dsi host: %d\n", ret);
		goto err_dsi_probe;
	}

	return 0;

err_dsi_probe:
	clk_disable_unprepare(dsi->pllref_clk);
err_clk_get:
	regulator_disable(dsi->vdd_supply);

	return ret;
}

static int dw_mipi_dsi_stm_remove(struct platform_device *pdev)
{
	struct dw_mipi_dsi_stm *dsi = platform_get_drvdata(pdev);

	dw_mipi_dsi_remove(dsi->dsi);
	clk_disable_unprepare(dsi->pllref_clk);
	regulator_disable(dsi->vdd_supply);

	return 0;
}

static int __maybe_unused dw_mipi_dsi_stm_suspend(struct device *dev)
{
	struct dw_mipi_dsi_stm *dsi = dw_mipi_dsi_stm_plat_data.priv_data;

	DRM_DEBUG_DRIVER("\n");

	clk_disable_unprepare(dsi->pllref_clk);
	regulator_disable(dsi->vdd_supply);

	return 0;
}

static int __maybe_unused dw_mipi_dsi_stm_resume(struct device *dev)
{
	struct dw_mipi_dsi_stm *dsi = dw_mipi_dsi_stm_plat_data.priv_data;
	int ret;

	DRM_DEBUG_DRIVER("\n");

	ret = regulator_enable(dsi->vdd_supply);
	if (ret) {
		DRM_ERROR("Failed to enable regulator: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(dsi->pllref_clk);
	if (ret) {
		regulator_disable(dsi->vdd_supply);
		DRM_ERROR("Failed to enable pllref_clk: %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct dev_pm_ops dw_mipi_dsi_stm_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(dw_mipi_dsi_stm_suspend,
				dw_mipi_dsi_stm_resume)
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
