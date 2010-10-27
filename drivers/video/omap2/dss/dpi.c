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

#include <plat/display.h>
#include <plat/cpu.h>

#include "dss.h"

static struct {
	struct regulator *vdds_dsi_reg;
} dpi;

#ifdef CONFIG_OMAP2_DSS_USE_DSI_PLL
static int dpi_set_dsi_clk(bool is_tft, unsigned long pck_req,
		unsigned long *fck, int *lck_div, int *pck_div)
{
	struct dsi_clock_info dsi_cinfo;
	struct dispc_clock_info dispc_cinfo;
	int r;

	r = dsi_pll_calc_clock_div_pck(is_tft, pck_req, &dsi_cinfo,
			&dispc_cinfo);
	if (r)
		return r;

	r = dsi_pll_set_clock_div(&dsi_cinfo);
	if (r)
		return r;

	dss_select_dispc_clk_source(DSS_SRC_DSI1_PLL_FCLK);

	r = dispc_set_clock_div(&dispc_cinfo);
	if (r)
		return r;

	*fck = dsi_cinfo.dsi1_pll_fclk;
	*lck_div = dispc_cinfo.lck_div;
	*pck_div = dispc_cinfo.pck_div;

	return 0;
}
#else
static int dpi_set_dispc_clk(bool is_tft, unsigned long pck_req,
		unsigned long *fck, int *lck_div, int *pck_div)
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

	r = dispc_set_clock_div(&dispc_cinfo);
	if (r)
		return r;

	*fck = dss_cinfo.fck;
	*lck_div = dispc_cinfo.lck_div;
	*pck_div = dispc_cinfo.pck_div;

	return 0;
}
#endif

static int dpi_set_mode(struct omap_dss_device *dssdev)
{
	struct omap_video_timings *t = &dssdev->panel.timings;
	int lck_div, pck_div;
	unsigned long fck;
	unsigned long pck;
	bool is_tft;
	int r = 0;

	dss_clk_enable(DSS_CLK_ICK | DSS_CLK_FCK1);

	dispc_set_pol_freq(dssdev->panel.config, dssdev->panel.acbi,
			dssdev->panel.acb);

	is_tft = (dssdev->panel.config & OMAP_DSS_LCD_TFT) != 0;

#ifdef CONFIG_OMAP2_DSS_USE_DSI_PLL
	r = dpi_set_dsi_clk(is_tft, t->pixel_clock * 1000,
			&fck, &lck_div, &pck_div);
#else
	r = dpi_set_dispc_clk(is_tft, t->pixel_clock * 1000,
			&fck, &lck_div, &pck_div);
#endif
	if (r)
		goto err0;

	pck = fck / lck_div / pck_div / 1000;

	if (pck != t->pixel_clock) {
		DSSWARN("Could not find exact pixel clock. "
				"Requested %d kHz, got %lu kHz\n",
				t->pixel_clock, pck);

		t->pixel_clock = pck;
	}

	dispc_set_lcd_timings(t);

err0:
	dss_clk_disable(DSS_CLK_ICK | DSS_CLK_FCK1);
	return r;
}

static int dpi_basic_init(struct omap_dss_device *dssdev)
{
	bool is_tft;

	is_tft = (dssdev->panel.config & OMAP_DSS_LCD_TFT) != 0;

	dispc_set_parallel_interface_mode(OMAP_DSS_PARALLELMODE_BYPASS);
	dispc_set_lcd_display_type(is_tft ? OMAP_DSS_LCD_DISPLAY_TFT :
			OMAP_DSS_LCD_DISPLAY_STN);
	dispc_set_tft_data_lines(dssdev->phy.dpi.data_lines);

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

	dss_clk_enable(DSS_CLK_ICK | DSS_CLK_FCK1);

	r = dpi_basic_init(dssdev);
	if (r)
		goto err2;

#ifdef CONFIG_OMAP2_DSS_USE_DSI_PLL
	dss_clk_enable(DSS_CLK_FCK2);
	r = dsi_pll_init(dssdev, 0, 1);
	if (r)
		goto err3;
#endif
	r = dpi_set_mode(dssdev);
	if (r)
		goto err4;

	mdelay(2);

	dssdev->manager->enable(dssdev->manager);

	return 0;

err4:
#ifdef CONFIG_OMAP2_DSS_USE_DSI_PLL
	dsi_pll_uninit();
err3:
	dss_clk_disable(DSS_CLK_FCK2);
#endif
err2:
	dss_clk_disable(DSS_CLK_ICK | DSS_CLK_FCK1);
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

#ifdef CONFIG_OMAP2_DSS_USE_DSI_PLL
	dss_select_dispc_clk_source(DSS_SRC_DSS1_ALWON_FCLK);
	dsi_pll_uninit();
	dss_clk_disable(DSS_CLK_FCK2);
#endif

	dss_clk_disable(DSS_CLK_ICK | DSS_CLK_FCK1);

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
		dispc_go(OMAP_DSS_CHANNEL_LCD);
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

	if (!dispc_lcd_timings_ok(timings))
		return -EINVAL;

	if (timings->pixel_clock == 0)
		return -EINVAL;

	is_tft = (dssdev->panel.config & OMAP_DSS_LCD_TFT) != 0;

#ifdef CONFIG_OMAP2_DSS_USE_DSI_PLL
	{
		struct dsi_clock_info dsi_cinfo;
		struct dispc_clock_info dispc_cinfo;
		r = dsi_pll_calc_clock_div_pck(is_tft,
				timings->pixel_clock * 1000,
				&dsi_cinfo, &dispc_cinfo);

		if (r)
			return r;

		fck = dsi_cinfo.dsi1_pll_fclk;
		lck_div = dispc_cinfo.lck_div;
		pck_div = dispc_cinfo.pck_div;
	}
#else
	{
		struct dss_clock_info dss_cinfo;
		struct dispc_clock_info dispc_cinfo;
		r = dss_calc_clock_div(is_tft, timings->pixel_clock * 1000,
				&dss_cinfo, &dispc_cinfo);

		if (r)
			return r;

		fck = dss_cinfo.fck;
		lck_div = dispc_cinfo.lck_div;
		pck_div = dispc_cinfo.pck_div;
	}
#endif

	pck = fck / lck_div / pck_div / 1000;

	timings->pixel_clock = pck;

	return 0;
}
EXPORT_SYMBOL(dpi_check_timings);

int dpi_init_display(struct omap_dss_device *dssdev)
{
	DSSDBG("init_display\n");

	return 0;
}

int dpi_init(struct platform_device *pdev)
{
	if (cpu_is_omap34xx()) {
		dpi.vdds_dsi_reg = dss_get_vdds_dsi();
		if (IS_ERR(dpi.vdds_dsi_reg)) {
			DSSERR("can't get VDDS_DSI regulator\n");
			return PTR_ERR(dpi.vdds_dsi_reg);
		}
	}

	return 0;
}

void dpi_exit(void)
{
}

