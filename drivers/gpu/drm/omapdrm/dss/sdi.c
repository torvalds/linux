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
#include <linux/string.h>
#include <linux/of.h>
#include <linux/component.h>

#include <video/omapdss.h>
#include "dss.h"

static struct {
	struct platform_device *pdev;

	bool update_enabled;
	struct regulator *vdds_sdi_reg;

	struct dss_lcd_mgr_config mgr_config;
	struct omap_video_timings timings;
	int datapairs;

	struct omap_dss_device output;

	bool port_initialized;
} sdi;

struct sdi_clk_calc_ctx {
	unsigned long pck_min, pck_max;

	unsigned long fck;
	struct dispc_clock_info dispc_cinfo;
};

static bool dpi_calc_dispc_cb(int lckd, int pckd, unsigned long lck,
		unsigned long pck, void *data)
{
	struct sdi_clk_calc_ctx *ctx = data;

	ctx->dispc_cinfo.lck_div = lckd;
	ctx->dispc_cinfo.pck_div = pckd;
	ctx->dispc_cinfo.lck = lck;
	ctx->dispc_cinfo.pck = pck;

	return true;
}

static bool dpi_calc_dss_cb(unsigned long fck, void *data)
{
	struct sdi_clk_calc_ctx *ctx = data;

	ctx->fck = fck;

	return dispc_div_calc(fck, ctx->pck_min, ctx->pck_max,
			dpi_calc_dispc_cb, ctx);
}

static int sdi_calc_clock_div(unsigned long pclk,
		unsigned long *fck,
		struct dispc_clock_info *dispc_cinfo)
{
	int i;
	struct sdi_clk_calc_ctx ctx;

	/*
	 * DSS fclk gives us very few possibilities, so finding a good pixel
	 * clock may not be possible. We try multiple times to find the clock,
	 * each time widening the pixel clock range we look for, up to
	 * +/- 1MHz.
	 */

	for (i = 0; i < 10; ++i) {
		bool ok;

		memset(&ctx, 0, sizeof(ctx));
		if (pclk > 1000 * i * i * i)
			ctx.pck_min = max(pclk - 1000 * i * i * i, 0lu);
		else
			ctx.pck_min = 0;
		ctx.pck_max = pclk + 1000 * i * i * i;

		ok = dss_div_calc(pclk, ctx.pck_min, dpi_calc_dss_cb, &ctx);
		if (ok) {
			*fck = ctx.fck;
			*dispc_cinfo = ctx.dispc_cinfo;
			return 0;
		}
	}

	return -EINVAL;
}

static void sdi_config_lcd_manager(struct omap_dss_device *dssdev)
{
	enum omap_channel channel = dssdev->dispc_channel;

	sdi.mgr_config.io_pad_mode = DSS_IO_PAD_MODE_BYPASS;

	sdi.mgr_config.stallmode = false;
	sdi.mgr_config.fifohandcheck = false;

	sdi.mgr_config.video_port_width = 24;
	sdi.mgr_config.lcden_sig_polarity = 1;

	dss_mgr_set_lcd_config(channel, &sdi.mgr_config);
}

static int sdi_display_enable(struct omap_dss_device *dssdev)
{
	struct omap_dss_device *out = &sdi.output;
	enum omap_channel channel = dssdev->dispc_channel;
	struct omap_video_timings *t = &sdi.timings;
	unsigned long fck;
	struct dispc_clock_info dispc_cinfo;
	unsigned long pck;
	int r;

	if (!out->dispc_channel_connected) {
		DSSERR("failed to enable display: no output/manager\n");
		return -ENODEV;
	}

	r = regulator_enable(sdi.vdds_sdi_reg);
	if (r)
		goto err_reg_enable;

	r = dispc_runtime_get();
	if (r)
		goto err_get_dispc;

	/* 15.5.9.1.2 */
	t->data_pclk_edge = OMAPDSS_DRIVE_SIG_RISING_EDGE;
	t->sync_pclk_edge = OMAPDSS_DRIVE_SIG_RISING_EDGE;

	r = sdi_calc_clock_div(t->pixelclock, &fck, &dispc_cinfo);
	if (r)
		goto err_calc_clock_div;

	sdi.mgr_config.clock_info = dispc_cinfo;

	pck = fck / dispc_cinfo.lck_div / dispc_cinfo.pck_div;

	if (pck != t->pixelclock) {
		DSSWARN("Could not find exact pixel clock. Requested %d Hz, got %lu Hz\n",
			t->pixelclock, pck);

		t->pixelclock = pck;
	}


	dss_mgr_set_timings(channel, t);

	r = dss_set_fck_rate(fck);
	if (r)
		goto err_set_dss_clock_div;

	sdi_config_lcd_manager(dssdev);

	/*
	 * LCLK and PCLK divisors are located in shadow registers, and we
	 * normally write them to DISPC registers when enabling the output.
	 * However, SDI uses pck-free as source clock for its PLL, and pck-free
	 * is affected by the divisors. And as we need the PLL before enabling
	 * the output, we need to write the divisors early.
	 *
	 * It seems just writing to the DISPC register is enough, and we don't
	 * need to care about the shadow register mechanism for pck-free. The
	 * exact reason for this is unknown.
	 */
	dispc_mgr_set_clock_div(channel, &sdi.mgr_config.clock_info);

	dss_sdi_init(sdi.datapairs);
	r = dss_sdi_enable();
	if (r)
		goto err_sdi_enable;
	mdelay(2);

	r = dss_mgr_enable(channel);
	if (r)
		goto err_mgr_enable;

	return 0;

err_mgr_enable:
	dss_sdi_disable();
err_sdi_enable:
err_set_dss_clock_div:
err_calc_clock_div:
	dispc_runtime_put();
err_get_dispc:
	regulator_disable(sdi.vdds_sdi_reg);
err_reg_enable:
	return r;
}

static void sdi_display_disable(struct omap_dss_device *dssdev)
{
	enum omap_channel channel = dssdev->dispc_channel;

	dss_mgr_disable(channel);

	dss_sdi_disable();

	dispc_runtime_put();

	regulator_disable(sdi.vdds_sdi_reg);
}

static void sdi_set_timings(struct omap_dss_device *dssdev,
		struct omap_video_timings *timings)
{
	sdi.timings = *timings;
}

static void sdi_get_timings(struct omap_dss_device *dssdev,
		struct omap_video_timings *timings)
{
	*timings = sdi.timings;
}

static int sdi_check_timings(struct omap_dss_device *dssdev,
			struct omap_video_timings *timings)
{
	enum omap_channel channel = dssdev->dispc_channel;

	if (!dispc_mgr_timings_ok(channel, timings))
		return -EINVAL;

	if (timings->pixelclock == 0)
		return -EINVAL;

	return 0;
}

static void sdi_set_datapairs(struct omap_dss_device *dssdev, int datapairs)
{
	sdi.datapairs = datapairs;
}

static int sdi_init_regulator(void)
{
	struct regulator *vdds_sdi;

	if (sdi.vdds_sdi_reg)
		return 0;

	vdds_sdi = devm_regulator_get(&sdi.pdev->dev, "vdds_sdi");
	if (IS_ERR(vdds_sdi)) {
		if (PTR_ERR(vdds_sdi) != -EPROBE_DEFER)
			DSSERR("can't get VDDS_SDI regulator\n");
		return PTR_ERR(vdds_sdi);
	}

	sdi.vdds_sdi_reg = vdds_sdi;

	return 0;
}

static int sdi_connect(struct omap_dss_device *dssdev,
		struct omap_dss_device *dst)
{
	enum omap_channel channel = dssdev->dispc_channel;
	int r;

	r = sdi_init_regulator();
	if (r)
		return r;

	r = dss_mgr_connect(channel, dssdev);
	if (r)
		return r;

	r = omapdss_output_set_device(dssdev, dst);
	if (r) {
		DSSERR("failed to connect output to new device: %s\n",
				dst->name);
		dss_mgr_disconnect(channel, dssdev);
		return r;
	}

	return 0;
}

static void sdi_disconnect(struct omap_dss_device *dssdev,
		struct omap_dss_device *dst)
{
	enum omap_channel channel = dssdev->dispc_channel;

	WARN_ON(dst != dssdev->dst);

	if (dst != dssdev->dst)
		return;

	omapdss_output_unset_device(dssdev);

	dss_mgr_disconnect(channel, dssdev);
}

static const struct omapdss_sdi_ops sdi_ops = {
	.connect = sdi_connect,
	.disconnect = sdi_disconnect,

	.enable = sdi_display_enable,
	.disable = sdi_display_disable,

	.check_timings = sdi_check_timings,
	.set_timings = sdi_set_timings,
	.get_timings = sdi_get_timings,

	.set_datapairs = sdi_set_datapairs,
};

static void sdi_init_output(struct platform_device *pdev)
{
	struct omap_dss_device *out = &sdi.output;

	out->dev = &pdev->dev;
	out->id = OMAP_DSS_OUTPUT_SDI;
	out->output_type = OMAP_DISPLAY_TYPE_SDI;
	out->name = "sdi.0";
	out->dispc_channel = OMAP_DSS_CHANNEL_LCD;
	/* We have SDI only on OMAP3, where it's on port 1 */
	out->port_num = 1;
	out->ops.sdi = &sdi_ops;
	out->owner = THIS_MODULE;

	omapdss_register_output(out);
}

static void sdi_uninit_output(struct platform_device *pdev)
{
	struct omap_dss_device *out = &sdi.output;

	omapdss_unregister_output(out);
}

static int sdi_bind(struct device *dev, struct device *master, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);

	sdi.pdev = pdev;

	sdi_init_output(pdev);

	return 0;
}

static void sdi_unbind(struct device *dev, struct device *master, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);

	sdi_uninit_output(pdev);
}

static const struct component_ops sdi_component_ops = {
	.bind	= sdi_bind,
	.unbind	= sdi_unbind,
};

static int sdi_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &sdi_component_ops);
}

static int sdi_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &sdi_component_ops);
	return 0;
}

static struct platform_driver omap_sdi_driver = {
	.probe		= sdi_probe,
	.remove         = sdi_remove,
	.driver         = {
		.name   = "omapdss_sdi",
		.suppress_bind_attrs = true,
	},
};

int __init sdi_init_platform_driver(void)
{
	return platform_driver_register(&omap_sdi_driver);
}

void sdi_uninit_platform_driver(void)
{
	platform_driver_unregister(&omap_sdi_driver);
}

int sdi_init_port(struct platform_device *pdev, struct device_node *port)
{
	struct device_node *ep;
	u32 datapairs;
	int r;

	ep = omapdss_of_get_next_endpoint(port, NULL);
	if (!ep)
		return 0;

	r = of_property_read_u32(ep, "datapairs", &datapairs);
	if (r) {
		DSSERR("failed to parse datapairs\n");
		goto err_datapairs;
	}

	sdi.datapairs = datapairs;

	of_node_put(ep);

	sdi.pdev = pdev;

	sdi_init_output(pdev);

	sdi.port_initialized = true;

	return 0;

err_datapairs:
	of_node_put(ep);

	return r;
}

void sdi_uninit_port(struct device_node *port)
{
	if (!sdi.port_initialized)
		return;

	sdi_uninit_output(sdi.pdev);
}
