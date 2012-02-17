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
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/regulator/consumer.h>
#include <linux/export.h>
#include <linux/platform_device.h>

#include <video/omapdss.h>
#include "dss.h"

static struct {
	bool update_enabled;
	struct regulator *vdds_sdi_reg;
} sdi;

static void sdi_basic_init(struct omap_dss_device *dssdev)

{
	dispc_mgr_set_io_pad_mode(DSS_IO_PAD_MODE_BYPASS);
	dispc_mgr_enable_stallmode(dssdev->manager->id, false);

	dispc_mgr_set_lcd_display_type(dssdev->manager->id,
			OMAP_DSS_LCD_DISPLAY_TFT);

	dispc_mgr_set_tft_data_lines(dssdev->manager->id, 24);
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

	if (dssdev->manager == NULL) {
		DSSERR("failed to enable display: no manager\n");
		return -ENODEV;
	}

	r = omap_dss_start_device(dssdev);
	if (r) {
		DSSERR("failed to start device\n");
		goto err_start_dev;
	}

	r = regulator_enable(sdi.vdds_sdi_reg);
	if (r)
		goto err_reg_enable;

	r = dispc_runtime_get();
	if (r)
		goto err_get_dispc;

	sdi_basic_init(dssdev);

	/* 15.5.9.1.2 */
	dssdev->panel.config |= OMAP_DSS_LCD_RF | OMAP_DSS_LCD_ONOFF;

	dispc_mgr_set_pol_freq(dssdev->manager->id, dssdev->panel.config,
			dssdev->panel.acbi, dssdev->panel.acb);

	r = dss_calc_clock_div(1, t->pixel_clock * 1000,
			&dss_cinfo, &dispc_cinfo);
	if (r)
		goto err_calc_clock_div;

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


	dss_mgr_set_timings(dssdev->manager, t);

	r = dss_set_clock_div(&dss_cinfo);
	if (r)
		goto err_set_dss_clock_div;

	r = dispc_mgr_set_clock_div(dssdev->manager->id, &dispc_cinfo);
	if (r)
		goto err_set_dispc_clock_div;

	dss_sdi_init(dssdev->phy.sdi.datapairs);
	r = dss_sdi_enable();
	if (r)
		goto err_sdi_enable;
	mdelay(2);

	r = dss_mgr_enable(dssdev->manager);
	if (r)
		goto err_mgr_enable;

	return 0;

err_mgr_enable:
	dss_sdi_disable();
err_sdi_enable:
err_set_dispc_clock_div:
err_set_dss_clock_div:
err_calc_clock_div:
	dispc_runtime_put();
err_get_dispc:
	regulator_disable(sdi.vdds_sdi_reg);
err_reg_enable:
	omap_dss_stop_device(dssdev);
err_start_dev:
	return r;
}
EXPORT_SYMBOL(omapdss_sdi_display_enable);

void omapdss_sdi_display_disable(struct omap_dss_device *dssdev)
{
	dss_mgr_disable(dssdev->manager);

	dss_sdi_disable();

	dispc_runtime_put();

	regulator_disable(sdi.vdds_sdi_reg);

	omap_dss_stop_device(dssdev);
}
EXPORT_SYMBOL(omapdss_sdi_display_disable);

int sdi_init_display(struct omap_dss_device *dssdev)
{
	DSSDBG("SDI init\n");

	if (sdi.vdds_sdi_reg == NULL) {
		struct regulator *vdds_sdi;

		vdds_sdi = dss_get_vdds_sdi();

		if (IS_ERR(vdds_sdi)) {
			DSSERR("can't get VDDS_SDI regulator\n");
			return PTR_ERR(vdds_sdi);
		}

		sdi.vdds_sdi_reg = vdds_sdi;
	}

	return 0;
}

static int __init omap_sdi_probe(struct platform_device *pdev)
{
	return 0;
}

static int __exit omap_sdi_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver omap_sdi_driver = {
	.remove         = __exit_p(omap_sdi_remove),
	.driver         = {
		.name   = "omapdss_sdi",
		.owner  = THIS_MODULE,
	},
};

int __init sdi_init_platform_driver(void)
{
	return platform_driver_probe(&omap_sdi_driver, omap_sdi_probe);
}

void __exit sdi_uninit_platform_driver(void)
{
	platform_driver_unregister(&omap_sdi_driver);
}
