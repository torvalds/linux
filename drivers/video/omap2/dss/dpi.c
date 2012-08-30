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
} dpi;

static struct platform_device *dpi_get_dsidev(enum omap_dss_clk_source clk)
{
	int dsi_module;

	dsi_module = clk == OMAP_DSS_CLK_SRC_DSI_PLL_HSDIV_DISPC ? 0 : 1;

	return dsi_get_dsidev_from_id(dsi_module);
}

static bool dpi_use_dsi_pll(struct omap_dss_device *dssdev)
{
	if (dssdev->clocks.dispc.dispc_fclk_src ==
			OMAP_DSS_CLK_SRC_DSI_PLL_HSDIV_DISPC ||
			dssdev->clocks.dispc.dispc_fclk_src ==
			OMAP_DSS_CLK_SRC_DSI2_PLL_HSDIV_DISPC ||
			dssdev->clocks.dispc.channel.lcd_clk_src ==
			OMAP_DSS_CLK_SRC_DSI_PLL_HSDIV_DISPC ||
			dssdev->clocks.dispc.channel.lcd_clk_src ==
			OMAP_DSS_CLK_SRC_DSI2_PLL_HSDIV_DISPC)
		return true;
	else
		return false;
}

static int dpi_set_dsi_clk(struct omap_dss_device *dssdev,
		unsigned long pck_req, unsigned long *fck, int *lck_div,
		int *pck_div)
{
	struct dsi_clock_info dsi_cinfo;
	struct dispc_clock_info dispc_cinfo;
	int r;

	r = dsi_pll_calc_clock_div_pck(dpi.dsidev, pck_req, &dsi_cinfo,
			&dispc_cinfo);
	if (r)
		return r;

	r = dsi_pll_set_clock_div(dpi.dsidev, &dsi_cinfo);
	if (r)
		return r;

	dss_select_dispc_clk_source(dssdev->clocks.dispc.dispc_fclk_src);

	dpi.mgr_config.clock_info = dispc_cinfo;

	*fck = dsi_cinfo.dsi_pll_hsdiv_dispc_clk;
	*lck_div = dispc_cinfo.lck_div;
	*pck_div = dispc_cinfo.pck_div;

	return 0;
}

static int dpi_set_dispc_clk(struct omap_dss_device *dssdev,
		unsigned long pck_req, unsigned long *fck, int *lck_div,
		int *pck_div)
{
	struct dss_clock_info dss_cinfo;
	struct dispc_clock_info dispc_cinfo;
	int r;

	r = dss_calc_clock_div(pck_req, &dss_cinfo, &dispc_cinfo);
	if (r)
		return r;

	r = dss_set_clock_div(&dss_cinfo);
	if (r)
		return r;

	dpi.mgr_config.clock_info = dispc_cinfo;

	*fck = dss_cinfo.fck;
	*lck_div = dispc_cinfo.lck_div;
	*pck_div = dispc_cinfo.pck_div;

	return 0;
}

static int dpi_set_mode(struct omap_dss_device *dssdev)
{
	struct omap_video_timings *t = &dpi.timings;
	int lck_div = 0, pck_div = 0;
	unsigned long fck = 0;
	unsigned long pck;
	int r = 0;

	if (dpi_use_dsi_pll(dssdev))
		r = dpi_set_dsi_clk(dssdev, t->pixel_clock * 1000, &fck,
				&lck_div, &pck_div);
	else
		r = dpi_set_dispc_clk(dssdev, t->pixel_clock * 1000, &fck,
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

	dss_mgr_set_timings(dssdev->manager, t);

	return 0;
}

static void dpi_config_lcd_manager(struct omap_dss_device *dssdev)
{
	dpi.mgr_config.io_pad_mode = DSS_IO_PAD_MODE_BYPASS;

	dpi.mgr_config.stallmode = false;
	dpi.mgr_config.fifohandcheck = false;

	dpi.mgr_config.video_port_width = dpi.data_lines;

	dpi.mgr_config.lcden_sig_polarity = 0;

	dss_mgr_set_lcd_config(dssdev->manager, &dpi.mgr_config);
}

int omapdss_dpi_display_enable(struct omap_dss_device *dssdev)
{
	int r;

	mutex_lock(&dpi.lock);

	if (dss_has_feature(FEAT_DPI_USES_VDDS_DSI) && !dpi.vdds_dsi_reg) {
		DSSERR("no VDSS_DSI regulator\n");
		r = -ENODEV;
		goto err_no_reg;
	}

	if (dssdev->manager == NULL) {
		DSSERR("failed to enable display: no manager\n");
		r = -ENODEV;
		goto err_no_mgr;
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

	if (dpi_use_dsi_pll(dssdev)) {
		r = dsi_runtime_get(dpi.dsidev);
		if (r)
			goto err_get_dsi;

		r = dsi_pll_init(dpi.dsidev, 0, 1);
		if (r)
			goto err_dsi_pll_init;
	}

	r = dpi_set_mode(dssdev);
	if (r)
		goto err_set_mode;

	dpi_config_lcd_manager(dssdev);

	mdelay(2);

	r = dss_mgr_enable(dssdev->manager);
	if (r)
		goto err_mgr_enable;

	mutex_unlock(&dpi.lock);

	return 0;

err_mgr_enable:
err_set_mode:
	if (dpi_use_dsi_pll(dssdev))
		dsi_pll_uninit(dpi.dsidev, true);
err_dsi_pll_init:
	if (dpi_use_dsi_pll(dssdev))
		dsi_runtime_put(dpi.dsidev);
err_get_dsi:
	dispc_runtime_put();
err_get_dispc:
	if (dss_has_feature(FEAT_DPI_USES_VDDS_DSI))
		regulator_disable(dpi.vdds_dsi_reg);
err_reg_enable:
	omap_dss_stop_device(dssdev);
err_start_dev:
err_no_mgr:
err_no_reg:
	mutex_unlock(&dpi.lock);
	return r;
}
EXPORT_SYMBOL(omapdss_dpi_display_enable);

void omapdss_dpi_display_disable(struct omap_dss_device *dssdev)
{
	mutex_lock(&dpi.lock);

	dss_mgr_disable(dssdev->manager);

	if (dpi_use_dsi_pll(dssdev)) {
		dss_select_dispc_clk_source(OMAP_DSS_CLK_SRC_FCK);
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
	int r;

	DSSDBG("dpi_set_timings\n");

	mutex_lock(&dpi.lock);

	dpi.timings = *timings;

	if (dssdev->state == OMAP_DSS_DISPLAY_ACTIVE) {
		r = dispc_runtime_get();
		if (r)
			return;

		dpi_set_mode(dssdev);

		dispc_runtime_put();
	} else {
		dss_mgr_set_timings(dssdev->manager, timings);
	}

	mutex_unlock(&dpi.lock);
}
EXPORT_SYMBOL(omapdss_dpi_set_timings);

int dpi_check_timings(struct omap_dss_device *dssdev,
			struct omap_video_timings *timings)
{
	int r;
	int lck_div, pck_div;
	unsigned long fck;
	unsigned long pck;
	struct dispc_clock_info dispc_cinfo;

	if (dss_mgr_check_timings(dssdev->manager, timings))
		return -EINVAL;

	if (timings->pixel_clock == 0)
		return -EINVAL;

	if (dpi_use_dsi_pll(dssdev)) {
		struct dsi_clock_info dsi_cinfo;
		r = dsi_pll_calc_clock_div_pck(dpi.dsidev,
				timings->pixel_clock * 1000,
				&dsi_cinfo, &dispc_cinfo);

		if (r)
			return r;

		fck = dsi_cinfo.dsi_pll_hsdiv_dispc_clk;
	} else {
		struct dss_clock_info dss_cinfo;
		r = dss_calc_clock_div(timings->pixel_clock * 1000,
				&dss_cinfo, &dispc_cinfo);

		if (r)
			return r;

		fck = dss_cinfo.fck;
	}

	lck_div = dispc_cinfo.lck_div;
	pck_div = dispc_cinfo.pck_div;

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

static int __init dpi_init_display(struct omap_dss_device *dssdev)
{
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

	if (dpi_use_dsi_pll(dssdev)) {
		enum omap_dss_clk_source dispc_fclk_src =
			dssdev->clocks.dispc.dispc_fclk_src;
		dpi.dsidev = dpi_get_dsidev(dispc_fclk_src);
	}

	return 0;
}

static void __init dpi_probe_pdata(struct platform_device *pdev)
{
	struct omap_dss_board_info *pdata = pdev->dev.platform_data;
	int i, r;

	for (i = 0; i < pdata->num_devices; ++i) {
		struct omap_dss_device *dssdev = pdata->devices[i];

		if (dssdev->type != OMAP_DISPLAY_TYPE_DPI)
			continue;

		r = dpi_init_display(dssdev);
		if (r) {
			DSSERR("device %s init failed: %d\n", dssdev->name, r);
			continue;
		}

		r = omap_dss_register_device(dssdev, &pdev->dev, i);
		if (r)
			DSSERR("device %s register failed: %d\n",
					dssdev->name, r);
	}
}

static int __init omap_dpi_probe(struct platform_device *pdev)
{
	mutex_init(&dpi.lock);

	dpi_probe_pdata(pdev);

	return 0;
}

static int __exit omap_dpi_remove(struct platform_device *pdev)
{
	omap_dss_unregister_child_devices(&pdev->dev);

	return 0;
}

static struct platform_driver omap_dpi_driver = {
	.remove         = __exit_p(omap_dpi_remove),
	.driver         = {
		.name   = "omapdss_dpi",
		.owner  = THIS_MODULE,
	},
};

int __init dpi_init_platform_driver(void)
{
	return platform_driver_probe(&omap_dpi_driver, omap_dpi_probe);
}

void __exit dpi_uninit_platform_driver(void)
{
	platform_driver_unregister(&omap_dpi_driver);
}
