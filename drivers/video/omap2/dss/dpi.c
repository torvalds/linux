/*
 * linux/drivers/video/omap2/dss/dpi.c
 *
 * Copyright (C) 2009 Nokia Corporation
 * Author: Tomi Valkeinen <tomi.valkeinen@nokia.com>
 *
 * Some code and ideas taken from drivers/video/omap/ driver
 * by Imre Deak.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define DSS_SUBSYS_NAME "DPI"

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/string.h>

#include <video/omapdss.h>

#include "dss.h"
#include "dss_features.h"

static struct {
	struct regulator *vdds_dsi_reg;
	struct platform_device *dsidev;

	struct mutex lock;

	struct omap_video_timings timings;
	struct dss_lcd_mgr_config mgr_config;
	int data_lines;

	struct omap_dss_output output;
} dpi;

static struct platform_device *dpi_get_dsidev(enum omap_channel channel)
{
	/*
	 * XXX we can't currently use DSI PLL for DPI with OMAP3, as the DSI PLL
	 * would also be used for DISPC fclk. Meaning, when the DPI output is
	 * disabled, DISPC clock will be disabled, and TV out will stop.
	 */
	switch (omapdss_get_version()) {
	case OMAPDSS_VER_OMAP24xx:
	case OMAPDSS_VER_OMAP34xx_ES1:
	case OMAPDSS_VER_OMAP34xx_ES3:
	case OMAPDSS_VER_OMAP3630:
	case OMAPDSS_VER_AM35xx:
		return NULL;

	case OMAPDSS_VER_OMAP4430_ES1:
	case OMAPDSS_VER_OMAP4430_ES2:
	case OMAPDSS_VER_OMAP4:
		switch (channel) {
		case OMAP_DSS_CHANNEL_LCD:
			return dsi_get_dsidev_from_id(0);
		case OMAP_DSS_CHANNEL_LCD2:
			return dsi_get_dsidev_from_id(1);
		default:
			return NULL;
		}

	case OMAPDSS_VER_OMAP5:
		switch (channel) {
		case OMAP_DSS_CHANNEL_LCD:
			return dsi_get_dsidev_from_id(0);
		case OMAP_DSS_CHANNEL_LCD3:
			return dsi_get_dsidev_from_id(1);
		default:
			return NULL;
		}

	default:
		return NULL;
	}
}

static enum omap_dss_clk_source dpi_get_alt_clk_src(enum omap_channel channel)
{
	switch (channel) {
	case OMAP_DSS_CHANNEL_LCD:
		return OMAP_DSS_CLK_SRC_DSI_PLL_HSDIV_DISPC;
	case OMAP_DSS_CHANNEL_LCD2:
		return OMAP_DSS_CLK_SRC_DSI2_PLL_HSDIV_DISPC;
	default:
		/* this shouldn't happen */
		WARN_ON(1);
		return OMAP_DSS_CLK_SRC_FCK;
	}
}

struct dpi_clk_calc_ctx {
	struct platform_device *dsidev;

	/* inputs */

	unsigned long pck_min, pck_max;

	/* outputs */

	struct dsi_clock_info dsi_cinfo;
	struct dss_clock_info dss_cinfo;
	struct dispc_clock_info dispc_cinfo;
};

static bool dpi_calc_dispc_cb(int lckd, int pckd, unsigned long lck,
		unsigned long pck, void *data)
{
	struct dpi_clk_calc_ctx *ctx = data;

	/*
	 * Odd dividers give us uneven duty cycle, causing problem when level
	 * shifted. So skip all odd dividers when the pixel clock is on the
	 * higher side.
	 */
	if (ctx->pck_min >= 100000000) {
		if (lckd > 1 && lckd % 2 != 0)
			return false;

		if (pckd > 1 && pckd % 2 != 0)
			return false;
	}

	ctx->dispc_cinfo.lck_div = lckd;
	ctx->dispc_cinfo.pck_div = pckd;
	ctx->dispc_cinfo.lck = lck;
	ctx->dispc_cinfo.pck = pck;

	return true;
}


static bool dpi_calc_hsdiv_cb(int regm_dispc, unsigned long dispc,
		void *data)
{
	struct dpi_clk_calc_ctx *ctx = data;

	/*
	 * Odd dividers give us uneven duty cycle, causing problem when level
	 * shifted. So skip all odd dividers when the pixel clock is on the
	 * higher side.
	 */
	if (regm_dispc > 1 && regm_dispc % 2 != 0 && ctx->pck_min >= 100000000)
		return false;

	ctx->dsi_cinfo.regm_dispc = regm_dispc;
	ctx->dsi_cinfo.dsi_pll_hsdiv_dispc_clk = dispc;

	return dispc_div_calc(dispc, ctx->pck_min, ctx->pck_max,
			dpi_calc_dispc_cb, ctx);
}


static bool dpi_calc_pll_cb(int regn, int regm, unsigned long fint,
		unsigned long pll,
		void *data)
{
	struct dpi_clk_calc_ctx *ctx = data;

	ctx->dsi_cinfo.regn = regn;
	ctx->dsi_cinfo.regm = regm;
	ctx->dsi_cinfo.fint = fint;
	ctx->dsi_cinfo.clkin4ddr = pll;

	return dsi_hsdiv_calc(ctx->dsidev, pll, ctx->pck_min,
			dpi_calc_hsdiv_cb, ctx);
}

static bool dpi_calc_dss_cb(int fckd, unsigned long fck, void *data)
{
	struct dpi_clk_calc_ctx *ctx = data;

	ctx->dss_cinfo.fck = fck;
	ctx->dss_cinfo.fck_div = fckd;

	return dispc_div_calc(fck, ctx->pck_min, ctx->pck_max,
			dpi_calc_dispc_cb, ctx);
}

static bool dpi_dsi_clk_calc(unsigned long pck, struct dpi_clk_calc_ctx *ctx)
{
	unsigned long clkin;
	unsigned long pll_min, pll_max;

	clkin = dsi_get_pll_clkin(dpi.dsidev);

	memset(ctx, 0, sizeof(*ctx));
	ctx->dsidev = dpi.dsidev;
	ctx->pck_min = pck - 1000;
	ctx->pck_max = pck + 1000;
	ctx->dsi_cinfo.clkin = clkin;

	pll_min = 0;
	pll_max = 0;

	return dsi_pll_calc(dpi.dsidev, clkin,
			pll_min, pll_max,
			dpi_calc_pll_cb, ctx);
}

static bool dpi_dss_clk_calc(unsigned long pck, struct dpi_clk_calc_ctx *ctx)
{
	int i;

	/*
	 * DSS fck gives us very few possibilities, so finding a good pixel
	 * clock may not be possible. We try multiple times to find the clock,
	 * each time widening the pixel clock range we look for, up to
	 * +/- ~15MHz.
	 */

	for (i = 0; i < 25; ++i) {
		bool ok;

		memset(ctx, 0, sizeof(*ctx));
		if (pck > 1000 * i * i * i)
			ctx->pck_min = max(pck - 1000 * i * i * i, 0lu);
		else
			ctx->pck_min = 0;
		ctx->pck_max = pck + 1000 * i * i * i;

		ok = dss_div_calc(ctx->pck_min, dpi_calc_dss_cb, ctx);
		if (ok)
			return ok;
	}

	return false;
}



static int dpi_set_dsi_clk(enum omap_channel channel,
		unsigned long pck_req, unsigned long *fck, int *lck_div,
		int *pck_div)
{
	struct dpi_clk_calc_ctx ctx;
	int r;
	bool ok;

	ok = dpi_dsi_clk_calc(pck_req, &ctx);
	if (!ok)
		return -EINVAL;

	r = dsi_pll_set_clock_div(dpi.dsidev, &ctx.dsi_cinfo);
	if (r)
		return r;

	dss_select_lcd_clk_source(channel,
			dpi_get_alt_clk_src(channel));

	dpi.mgr_config.clock_info = ctx.dispc_cinfo;

	*fck = ctx.dsi_cinfo.dsi_pll_hsdiv_dispc_clk;
	*lck_div = ctx.dispc_cinfo.lck_div;
	*pck_div = ctx.dispc_cinfo.pck_div;

	return 0;
}

static int dpi_set_dispc_clk(unsigned long pck_req, unsigned long *fck,
		int *lck_div, int *pck_div)
{
	struct dpi_clk_calc_ctx ctx;
	int r;
	bool ok;

	ok = dpi_dss_clk_calc(pck_req, &ctx);
	if (!ok)
		return -EINVAL;

	r = dss_set_clock_div(&ctx.dss_cinfo);
	if (r)
		return r;

	dpi.mgr_config.clock_info = ctx.dispc_cinfo;

	*fck = ctx.dss_cinfo.fck;
	*lck_div = ctx.dispc_cinfo.lck_div;
	*pck_div = ctx.dispc_cinfo.pck_div;

	return 0;
}

static int dpi_set_mode(struct omap_overlay_manager *mgr)
{
	struct omap_video_timings *t = &dpi.timings;
	int lck_div = 0, pck_div = 0;
	unsigned long fck = 0;
	unsigned long pck;
	int r = 0;

	if (dpi.dsidev)
		r = dpi_set_dsi_clk(mgr->id, t->pixel_clock * 1000, &fck,
				&lck_div, &pck_div);
	else
		r = dpi_set_dispc_clk(t->pixel_clock * 1000, &fck,
				&lck_div, &pck_div);
	if (r)
		return r;

	pck = fck / lck_div / pck_div / 1000;

	if (pck != t->pixel_clock) {
		DSSWARN("Could not find exact pixel clock. "
				"Requested %d kHz, got %lu kHz\n",
				t->pixel_clock, pck);

		t->pixel_clock = pck;
	}

	dss_mgr_set_timings(mgr, t);

	return 0;
}

static void dpi_config_lcd_manager(struct omap_overlay_manager *mgr)
{
	dpi.mgr_config.io_pad_mode = DSS_IO_PAD_MODE_BYPASS;

	dpi.mgr_config.stallmode = false;
	dpi.mgr_config.fifohandcheck = false;

	dpi.mgr_config.video_port_width = dpi.data_lines;

	dpi.mgr_config.lcden_sig_polarity = 0;

	dss_mgr_set_lcd_config(mgr, &dpi.mgr_config);
}

int omapdss_dpi_display_enable(struct omap_dss_device *dssdev)
{
	struct omap_dss_output *out = &dpi.output;
	int r;

	mutex_lock(&dpi.lock);

	if (dss_has_feature(FEAT_DPI_USES_VDDS_DSI) && !dpi.vdds_dsi_reg) {
		DSSERR("no VDSS_DSI regulator\n");
		r = -ENODEV;
		goto err_no_reg;
	}

	if (out == NULL || out->manager == NULL) {
		DSSERR("failed to enable display: no output/manager\n");
		r = -ENODEV;
		goto err_no_out_mgr;
	}

	r = omap_dss_start_device(dssdev);
	if (r) {
		DSSERR("failed to start device\n");
		goto err_start_dev;
	}

	if (dss_has_feature(FEAT_DPI_USES_VDDS_DSI)) {
		r = regulator_enable(dpi.vdds_dsi_reg);
		if (r)
			goto err_reg_enable;
	}

	r = dispc_runtime_get();
	if (r)
		goto err_get_dispc;

	r = dss_dpi_select_source(out->manager->id);
	if (r)
		goto err_src_sel;

	if (dpi.dsidev) {
		r = dsi_runtime_get(dpi.dsidev);
		if (r)
			goto err_get_dsi;

		r = dsi_pll_init(dpi.dsidev, 0, 1);
		if (r)
			goto err_dsi_pll_init;
	}

	r = dpi_set_mode(out->manager);
	if (r)
		goto err_set_mode;

	dpi_config_lcd_manager(out->manager);

	mdelay(2);

	r = dss_mgr_enable(out->manager);
	if (r)
		goto err_mgr_enable;

	mutex_unlock(&dpi.lock);

	return 0;

err_mgr_enable:
err_set_mode:
	if (dpi.dsidev)
		dsi_pll_uninit(dpi.dsidev, true);
err_dsi_pll_init:
	if (dpi.dsidev)
		dsi_runtime_put(dpi.dsidev);
err_get_dsi:
err_src_sel:
	dispc_runtime_put();
err_get_dispc:
	if (dss_has_feature(FEAT_DPI_USES_VDDS_DSI))
		regulator_disable(dpi.vdds_dsi_reg);
err_reg_enable:
	omap_dss_stop_device(dssdev);
err_start_dev:
err_no_out_mgr:
err_no_reg:
	mutex_unlock(&dpi.lock);
	return r;
}
EXPORT_SYMBOL(omapdss_dpi_display_enable);

void omapdss_dpi_display_disable(struct omap_dss_device *dssdev)
{
	struct omap_overlay_manager *mgr = dpi.output.manager;

	mutex_lock(&dpi.lock);

	dss_mgr_disable(mgr);

	if (dpi.dsidev) {
		dss_select_lcd_clk_source(mgr->id, OMAP_DSS_CLK_SRC_FCK);
		dsi_pll_uninit(dpi.dsidev, true);
		dsi_runtime_put(dpi.dsidev);
	}

	dispc_runtime_put();

	if (dss_has_feature(FEAT_DPI_USES_VDDS_DSI))
		regulator_disable(dpi.vdds_dsi_reg);

	omap_dss_stop_device(dssdev);

	mutex_unlock(&dpi.lock);
}
EXPORT_SYMBOL(omapdss_dpi_display_disable);

void omapdss_dpi_set_timings(struct omap_dss_device *dssdev,
		struct omap_video_timings *timings)
{
	DSSDBG("dpi_set_timings\n");

	mutex_lock(&dpi.lock);

	dpi.timings = *timings;

	mutex_unlock(&dpi.lock);
}
EXPORT_SYMBOL(omapdss_dpi_set_timings);

int dpi_check_timings(struct omap_dss_device *dssdev,
			struct omap_video_timings *timings)
{
	struct omap_overlay_manager *mgr = dpi.output.manager;
	int lck_div, pck_div;
	unsigned long fck;
	unsigned long pck;
	struct dpi_clk_calc_ctx ctx;
	bool ok;

	if (mgr && !dispc_mgr_timings_ok(mgr->id, timings))
		return -EINVAL;

	if (timings->pixel_clock == 0)
		return -EINVAL;

	if (dpi.dsidev) {
		ok = dpi_dsi_clk_calc(timings->pixel_clock * 1000, &ctx);
		if (!ok)
			return -EINVAL;

		fck = ctx.dsi_cinfo.dsi_pll_hsdiv_dispc_clk;
	} else {
		ok = dpi_dss_clk_calc(timings->pixel_clock * 1000, &ctx);
		if (!ok)
			return -EINVAL;

		fck = ctx.dss_cinfo.fck;
	}

	lck_div = ctx.dispc_cinfo.lck_div;
	pck_div = ctx.dispc_cinfo.pck_div;

	pck = fck / lck_div / pck_div / 1000;

	timings->pixel_clock = pck;

	return 0;
}
EXPORT_SYMBOL(dpi_check_timings);

void omapdss_dpi_set_data_lines(struct omap_dss_device *dssdev, int data_lines)
{
	mutex_lock(&dpi.lock);

	dpi.data_lines = data_lines;

	mutex_unlock(&dpi.lock);
}
EXPORT_SYMBOL(omapdss_dpi_set_data_lines);

static int dpi_verify_dsi_pll(struct platform_device *dsidev)
{
	int r;

	/* do initial setup with the PLL to see if it is operational */

	r = dsi_runtime_get(dsidev);
	if (r)
		return r;

	r = dsi_pll_init(dsidev, 0, 1);
	if (r) {
		dsi_runtime_put(dsidev);
		return r;
	}

	dsi_pll_uninit(dsidev, true);
	dsi_runtime_put(dsidev);

	return 0;
}

/*
 * Return a hardcoded channel for the DPI output. This should work for
 * current use cases, but this can be later expanded to either resolve
 * the channel in some more dynamic manner, or get the channel as a user
 * parameter.
 */
static enum omap_channel dpi_get_channel(void)
{
	switch (omapdss_get_version()) {
	case OMAPDSS_VER_OMAP24xx:
	case OMAPDSS_VER_OMAP34xx_ES1:
	case OMAPDSS_VER_OMAP34xx_ES3:
	case OMAPDSS_VER_OMAP3630:
	case OMAPDSS_VER_AM35xx:
		return OMAP_DSS_CHANNEL_LCD;

	case OMAPDSS_VER_OMAP4430_ES1:
	case OMAPDSS_VER_OMAP4430_ES2:
	case OMAPDSS_VER_OMAP4:
		return OMAP_DSS_CHANNEL_LCD2;

	case OMAPDSS_VER_OMAP5:
		return OMAP_DSS_CHANNEL_LCD3;

	default:
		DSSWARN("unsupported DSS version\n");
		return OMAP_DSS_CHANNEL_LCD;
	}
}

static int dpi_init_display(struct omap_dss_device *dssdev)
{
	struct platform_device *dsidev;

	DSSDBG("init_display\n");

	if (dss_has_feature(FEAT_DPI_USES_VDDS_DSI) &&
					dpi.vdds_dsi_reg == NULL) {
		struct regulator *vdds_dsi;

		vdds_dsi = dss_get_vdds_dsi();

		if (IS_ERR(vdds_dsi)) {
			DSSERR("can't get VDDS_DSI regulator\n");
			return PTR_ERR(vdds_dsi);
		}

		dpi.vdds_dsi_reg = vdds_dsi;
	}

	dsidev = dpi_get_dsidev(dpi.output.dispc_channel);

	if (dsidev && dpi_verify_dsi_pll(dsidev)) {
		dsidev = NULL;
		DSSWARN("DSI PLL not operational\n");
	}

	if (dsidev)
		DSSDBG("using DSI PLL for DPI clock\n");

	dpi.dsidev = dsidev;

	return 0;
}

static struct omap_dss_device *dpi_find_dssdev(struct platform_device *pdev)
{
	struct omap_dss_board_info *pdata = pdev->dev.platform_data;
	const char *def_disp_name = omapdss_get_default_display_name();
	struct omap_dss_device *def_dssdev;
	int i;

	def_dssdev = NULL;

	for (i = 0; i < pdata->num_devices; ++i) {
		struct omap_dss_device *dssdev = pdata->devices[i];

		if (dssdev->type != OMAP_DISPLAY_TYPE_DPI)
			continue;

		if (def_dssdev == NULL)
			def_dssdev = dssdev;

		if (def_disp_name != NULL &&
				strcmp(dssdev->name, def_disp_name) == 0) {
			def_dssdev = dssdev;
			break;
		}
	}

	return def_dssdev;
}

static int dpi_probe_pdata(struct platform_device *dpidev)
{
	struct omap_dss_device *plat_dssdev;
	struct omap_dss_device *dssdev;
	int r;

	plat_dssdev = dpi_find_dssdev(dpidev);

	if (!plat_dssdev)
		return 0;

	dssdev = dss_alloc_and_init_device(&dpidev->dev);
	if (!dssdev)
		return -ENOMEM;

	dss_copy_device_pdata(dssdev, plat_dssdev);

	r = dpi_init_display(dssdev);
	if (r) {
		DSSERR("device %s init failed: %d\n", dssdev->name, r);
		dss_put_device(dssdev);
		return r;
	}

	r = omapdss_output_set_device(&dpi.output, dssdev);
	if (r) {
		DSSERR("failed to connect output to new device: %s\n",
				dssdev->name);
		dss_put_device(dssdev);
		return r;
	}

	r = dss_add_device(dssdev);
	if (r) {
		DSSERR("device %s register failed: %d\n", dssdev->name, r);
		omapdss_output_unset_device(&dpi.output);
		dss_put_device(dssdev);
		return r;
	}

	return 0;
}

static void dpi_init_output(struct platform_device *pdev)
{
	struct omap_dss_output *out = &dpi.output;

	out->pdev = pdev;
	out->id = OMAP_DSS_OUTPUT_DPI;
	out->type = OMAP_DISPLAY_TYPE_DPI;
	out->name = "dpi.0";
	out->dispc_channel = dpi_get_channel();

	dss_register_output(out);
}

static void __exit dpi_uninit_output(struct platform_device *pdev)
{
	struct omap_dss_output *out = &dpi.output;

	dss_unregister_output(out);
}

static int omap_dpi_probe(struct platform_device *pdev)
{
	int r;

	mutex_init(&dpi.lock);

	dpi_init_output(pdev);

	r = dpi_probe_pdata(pdev);
	if (r) {
		dpi_uninit_output(pdev);
		return r;
	}

	return 0;
}

static int __exit omap_dpi_remove(struct platform_device *pdev)
{
	dss_unregister_child_devices(&pdev->dev);

	dpi_uninit_output(pdev);

	return 0;
}

static struct platform_driver omap_dpi_driver = {
	.probe		= omap_dpi_probe,
	.remove         = __exit_p(omap_dpi_remove),
	.driver         = {
		.name   = "omapdss_dpi",
		.owner  = THIS_MODULE,
	},
};

int __init dpi_init_platform_driver(void)
{
	return platform_driver_register(&omap_dpi_driver);
}

void __exit dpi_uninit_platform_driver(void)
{
	platform_driver_unregister(&omap_dpi_driver);
}
