/*
 * linux/drivers/video/omap2/dss/sdi.c
 *
 * Copyright (C) 2009 Nokia Corporation
 * Author: Tomi Valkeinen <tomi.valkeinen@nokia.com>
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

#define DSS_SUBSYS_NAME "SDI"

#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>

#include <plat/display.h>
#include "dss.h"

static struct {
	bool skip_init;
	bool update_enabled;
} sdi;

static void sdi_basic_init(void)
{
	dispc_set_parallel_interface_mode(OMAP_DSS_PARALLELMODE_BYPASS);

	dispc_set_lcd_display_type(OMAP_DSS_LCD_DISPLAY_TFT);
	dispc_set_tft_data_lines(24);
	dispc_lcd_enable_signal_polarity(1);
}

int omapdss_sdi_display_enable(struct omap_dss_device *dssdev)
{
	struct omap_video_timings *t = &dssdev->panel.timings;
	struct dss_clock_info dss_cinfo;
	struct dispc_clock_info dispc_cinfo;
	u16 lck_div, pck_div;
	unsigned long fck;
	unsigned long pck;
	int r;

	r = omap_dss_start_device(dssdev);
	if (r) {
		DSSERR("failed to start device\n");
		goto err0;
	}

	/* In case of skip_init sdi_init has already enabled the clocks */
	if (!sdi.skip_init)
		dss_clk_enable(DSS_CLK_ICK | DSS_CLK_FCK1);

	sdi_basic_init();

	/* 15.5.9.1.2 */
	dssdev->panel.config |= OMAP_DSS_LCD_RF | OMAP_DSS_LCD_ONOFF;

	dispc_set_pol_freq(dssdev->panel.config, dssdev->panel.acbi,
			dssdev->panel.acb);

	if (!sdi.skip_init) {
		r = dss_calc_clock_div(1, t->pixel_clock * 1000,
				&dss_cinfo, &dispc_cinfo);
	} else {
		r = dss_get_clock_div(&dss_cinfo);
		r = dispc_get_clock_div(&dispc_cinfo);
	}

	if (r)
		goto err2;

	fck = dss_cinfo.fck;
	lck_div = dispc_cinfo.lck_div;
	pck_div = dispc_cinfo.pck_div;

	pck = fck / lck_div / pck_div / 1000;

	if (pck != t->pixel_clock) {
		DSSWARN("Could not find exact pixel clock. Requested %d kHz, "
				"got %lu kHz\n",
				t->pixel_clock, pck);

		t->pixel_clock = pck;
	}


	dispc_set_lcd_timings(t);

	r = dss_set_clock_div(&dss_cinfo);
	if (r)
		goto err2;

	r = dispc_set_clock_div(&dispc_cinfo);
	if (r)
		goto err2;

	if (!sdi.skip_init) {
		dss_sdi_init(dssdev->phy.sdi.datapairs);
		r = dss_sdi_enable();
		if (r)
			goto err1;
		mdelay(2);
	}

	dssdev->manager->enable(dssdev->manager);

	if (dssdev->driver->enable) {
		r = dssdev->driver->enable(dssdev);
		if (r)
			goto err3;
	}

	sdi.skip_init = 0;

	return 0;
err3:
	dssdev->manager->disable(dssdev->manager);
err2:
	dss_clk_disable(DSS_CLK_ICK | DSS_CLK_FCK1);
err1:
	omap_dss_stop_device(dssdev);
err0:
	return r;
}
EXPORT_SYMBOL(omapdss_sdi_display_enable);

void omapdss_sdi_display_disable(struct omap_dss_device *dssdev)
{
	if (dssdev->driver->disable)
		dssdev->driver->disable(dssdev);

	dssdev->manager->disable(dssdev->manager);

	dss_sdi_disable();

	dss_clk_disable(DSS_CLK_ICK | DSS_CLK_FCK1);

	omap_dss_stop_device(dssdev);
}
EXPORT_SYMBOL(omapdss_sdi_display_disable);

int sdi_init_display(struct omap_dss_device *dssdev)
{
	DSSDBG("SDI init\n");

	return 0;
}

int sdi_init(bool skip_init)
{
	/* we store this for first display enable, then clear it */
	sdi.skip_init = skip_init;

	/*
	 * Enable clocks already here, otherwise there would be a toggle
	 * of them until sdi_display_enable is called.
	 */
	if (skip_init)
		dss_clk_enable(DSS_CLK_ICK | DSS_CLK_FCK1);
	return 0;
}

void sdi_exit(void)
{
}
