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
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

#include <video/omapdss.h>
#include <plat/cpu.h>

#include "dss.h"

static struct {
	struct regulator *vdds_dsi_reg;
	struct platform_device *dsidev;
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

static int dpi_set_dsi_clk(struct omap_dss_device *dssdev, bool is_tft,
		unsigned long pck_req, unsigned long *fck, int *lck_div,
		int *pck_div)
{
	struct dsi_clock_info dsi_cinfo;
	struct dispc_clock_info dispc_cinfo;
	int r;

	r = dsi_pll_calc_clock_div_pck(dpi.dsidev, is_tft, pck_req,
			&dsi_cinfo, &dispc_cinfo);
	if (r)
		return r;

	r = dsi_pll_set_clock_div(dpi.dsidev, &dsi_cinfo);
	if (r)
		return r;

	dss_select_dispc_clk_source(dssdev->clocks.dispc.dispc_fclk_src);

	r = dispc_set_clock_div(dssdev->manager->id, &dispc_cinfo);
	if (r)
		return r;

	*fck = dsi_cinfo.dsi_pll_hsdiv_dispc_clk;
	*lck_div = dispc_cinfo.lck_div;
	*pck_div = dispc_cinfo.pck_div;

	return 0;
}

static int dpi_set_dispc_clk(struct omap_dss_device *dssdev, bool is_tft,
		unsigned long pck_req, unsigned long *fck, int *lck_div,
		int *pck_div)
{
	struct dss_clock_info dss_cinfo;
	struct dispc_clock_info dispc_cinfo;
	int r;

	r = dss_calc_clock_div(is_tft, pck_req, &dss_cinfo, &dispc_cinfo);
	if (r)
		return r;

	r = dss_set_clock_div(&dss_cinfo);
	if (r)
		return r;

	r = dispc_set_clock_div(dssdev->manager->id, &dispc_cinfo);
	if (r)
		return r;

	*fck = dss_cinfo.fck;
	*lck_div = dispc_cinfo.lck_div;
	*pck_div = dispc_cinfo.pck_div;

	return 0;
}

static int dpi_set_mode(struct omap_dss_device *dssdev)
{
	struct omap_video_timings *t = &dssdev->panel.timings;
	int lck_div = 0, pck_div = 0;
	unsigned long fck = 0;
	unsigned long pck;
	bool is_tft;
	int r = 0;

	dss_clk_enable(DSS_CLK_ICK | DSS_CLK_FCK);

	dispc_set_pol_freq(dssdev->manager->id, dssdev->panel.config,
			dssdev->panel.acbi, dssdev->panel.acb);

	is_tft = (dssdev->panel.config & OMAP_DSS_LCD_TFT) != 0;

	if (dpi_use_dsi_pll(dssdev))
		r = dpi_set_dsi_clk(dssdev, is_tft, t->pixel_clock * 1000,
				&fck, &lck_div, &pck_div);
	else
		r = dpi_set_dispc_clk(dssdev, is_tft, t->pixel_clock * 1000,
				&fck, &lck_div, &pck_div);
	if (r)
		goto err0;

	pck = fck / lck_div / pck_div / 1000;

	if (pck != t->pixel_clock) {
		DSSWARN("Could not find exact pixel clock. "
				"Requested %d kHz, got %lu kHz\n",
				t->pixel_clock, pck);

		t->pixel_clock = pck;
	}

	dispc_set_lcd_timings(dssdev->manager->id, t);

err0:
	dss_clk_disable(DSS_CLK_ICK | DSS_CLK_FCK);
	return r;
}

static int dpi_basic_init(struct omap_dss_device *dssdev)
{
	bool is_tft;

	is_tft = (dssdev->panel.config & OMAP_DSS_LCD_TFT) != 0;

	dispc_set_parallel_interface_mode(dssdev->manager->id,
			OMAP_DSS_PARALLELMODE_BYPASS);
	dispc_set_lcd_display_type(dssdev->manager->id, is_tft ?
			OMAP_DSS_LCD_DISPLAY_TFT : OMAP_DSS_LCD_DISPLAY_STN);
	dispc_set_tft_data_lines(dssdev->manager->id,
			dssdev->phy.dpi.data_lines);

	return 0;
}

int omapdss_dpi_display_enable(struct omap_dss_device *dssdev)
{
	int r;

	r = omap_dss_start_device(dssdev);
	if (r) {
		DSSERR("failed to start device\n");
		goto err0;
	}

	if (cpu_is_omap34xx()) {
		r = regulator_enable(dpi.vdds_dsi_reg);
		if (r)
			goto err1;
	}

	dss_clk_enable(DSS_CLK_ICK | DSS_CLK_FCK);

	r = dpi_basic_init(dssdev);
	if (r)
		goto err2;

	if (dpi_use_dsi_pll(dssdev)) {
		dss_clk_enable(DSS_CLK_SYSCK);
		r = dsi_pll_init(dpi.dsidev, 0, 1);
		if (r)
			goto err3;
	}

	r = dpi_set_mode(dssdev);
	if (r)
		goto err4;

	mdelay(2);

	dssdev->manager->enable(dssdev->manager);

	return 0;

err4:
	if (dpi_use_dsi_pll(dssdev))
		dsi_pll_uninit(dpi.dsidev, true);
err3:
	if (dpi_use_dsi_pll(dssdev))
		dss_clk_disable(DSS_CLK_SYSCK);
err2:
	dss_clk_disable(DSS_CLK_ICK | DSS_CLK_FCK);
	if (cpu_is_omap34xx())
		regulator_disable(dpi.vdds_dsi_reg);
err1:
	omap_dss_stop_device(dssdev);
err0:
	return r;
}
EXPORT_SYMBOL(omapdss_dpi_display_enable);

void omapdss_dpi_display_disable(struct omap_dss_device *dssdev)
{
	dssdev->manager->disable(dssdev->manager);

	if (dpi_use_dsi_pll(dssdev)) {
		dss_select_dispc_clk_source(OMAP_DSS_CLK_SRC_FCK);
		dsi_pll_uninit(dpi.dsidev, true);
		dss_clk_disable(DSS_CLK_SYSCK);
	}

	dss_clk_disable(DSS_CLK_ICK | DSS_CLK_FCK);

	if (cpu_is_omap34xx())
		regulator_disable(dpi.vdds_dsi_reg);

	omap_dss_stop_device(dssdev);
}
EXPORT_SYMBOL(omapdss_dpi_display_disable);

void dpi_set_timings(struct omap_dss_device *dssdev,
			struct omap_video_timings *timings)
{
	DSSDBG("dpi_set_timings\n");
	dssdev->panel.timings = *timings;
	if (dssdev->state == OMAP_DSS_DISPLAY_ACTIVE) {
		dpi_set_mode(dssdev);
		dispc_go(dssdev->manager->id);
	}
}
EXPORT_SYMBOL(dpi_set_timings);

int dpi_check_timings(struct omap_dss_device *dssdev,
			struct omap_video_timings *timings)
{
	bool is_tft;
	int r;
	int lck_div, pck_div;
	unsigned long fck;
	unsigned long pck;
	struct dispc_clock_info dispc_cinfo;

	if (!dispc_lcd_timings_ok(timings))
		return -EINVAL;

	if (timings->pixel_clock == 0)
		return -EINVAL;

	is_tft = (dssdev->panel.config & OMAP_DSS_LCD_TFT) != 0;

	if (dpi_use_dsi_pll(dssdev)) {
		struct dsi_clock_info dsi_cinfo;
		r = dsi_pll_calc_clock_div_pck(dpi.dsidev, is_tft,
				timings->pixel_clock * 1000,
				&dsi_cinfo, &dispc_cinfo);

		if (r)
			return r;

		fck = dsi_cinfo.dsi_pll_hsdiv_dispc_clk;
	} else {
		struct dss_clock_info dss_cinfo;
		r = dss_calc_clock_div(is_tft, timings->pixel_clock * 1000,
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

int dpi_init_display(struct omap_dss_device *dssdev)
{
	DSSDBG("init_display\n");

	if (cpu_is_omap34xx() && dpi.vdds_dsi_reg == NULL) {
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

int dpi_init(void)
{
	return 0;
}

void dpi_exit(void)
{
}

