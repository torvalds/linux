/*
 * Copyright (C) 2009 Nokia Corporation
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
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

#include "omapdss.h"
#include "dss.h"

struct sdi_device {
	struct platform_device *pdev;
	struct dss_device *dss;

	bool update_enabled;
	struct regulator *vdds_sdi_reg;

	struct dss_lcd_mgr_config mgr_config;
	unsigned long pixelclock;
	int datapairs;

	struct omap_dss_device output;
};

#define dssdev_to_sdi(dssdev) container_of(dssdev, struct sdi_device, output)

struct sdi_clk_calc_ctx {
	struct sdi_device *sdi;
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

	return dispc_div_calc(ctx->sdi->dss->dispc, fck,
			      ctx->pck_min, ctx->pck_max,
			      dpi_calc_dispc_cb, ctx);
}

static int sdi_calc_clock_div(struct sdi_device *sdi, unsigned long pclk,
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

		ctx.sdi = sdi;

		if (pclk > 1000 * i * i * i)
			ctx.pck_min = max(pclk - 1000 * i * i * i, 0lu);
		else
			ctx.pck_min = 0;
		ctx.pck_max = pclk + 1000 * i * i * i;

		ok = dss_div_calc(sdi->dss, pclk, ctx.pck_min,
				  dpi_calc_dss_cb, &ctx);
		if (ok) {
			*fck = ctx.fck;
			*dispc_cinfo = ctx.dispc_cinfo;
			return 0;
		}
	}

	return -EINVAL;
}

static void sdi_config_lcd_manager(struct sdi_device *sdi)
{
	sdi->mgr_config.io_pad_mode = DSS_IO_PAD_MODE_BYPASS;

	sdi->mgr_config.stallmode = false;
	sdi->mgr_config.fifohandcheck = false;

	sdi->mgr_config.video_port_width = 24;
	sdi->mgr_config.lcden_sig_polarity = 1;

	dss_mgr_set_lcd_config(&sdi->output, &sdi->mgr_config);
}

static void sdi_display_enable(struct omap_dss_device *dssdev)
{
	struct sdi_device *sdi = dssdev_to_sdi(dssdev);
	struct dispc_clock_info dispc_cinfo;
	unsigned long fck;
	int r;

	r = regulator_enable(sdi->vdds_sdi_reg);
	if (r)
		return;

	r = dispc_runtime_get(sdi->dss->dispc);
	if (r)
		goto err_get_dispc;

	r = sdi_calc_clock_div(sdi, sdi->pixelclock, &fck, &dispc_cinfo);
	if (r)
		goto err_calc_clock_div;

	sdi->mgr_config.clock_info = dispc_cinfo;

	r = dss_set_fck_rate(sdi->dss, fck);
	if (r)
		goto err_set_dss_clock_div;

	sdi_config_lcd_manager(sdi);

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
	dispc_mgr_set_clock_div(sdi->dss->dispc, sdi->output.dispc_channel,
				&sdi->mgr_config.clock_info);

	dss_sdi_init(sdi->dss, sdi->datapairs);
	r = dss_sdi_enable(sdi->dss);
	if (r)
		goto err_sdi_enable;
	mdelay(2);

	r = dss_mgr_enable(&sdi->output);
	if (r)
		goto err_mgr_enable;

	return;

err_mgr_enable:
	dss_sdi_disable(sdi->dss);
err_sdi_enable:
err_set_dss_clock_div:
err_calc_clock_div:
	dispc_runtime_put(sdi->dss->dispc);
err_get_dispc:
	regulator_disable(sdi->vdds_sdi_reg);
}

static void sdi_display_disable(struct omap_dss_device *dssdev)
{
	struct sdi_device *sdi = dssdev_to_sdi(dssdev);

	dss_mgr_disable(&sdi->output);

	dss_sdi_disable(sdi->dss);

	dispc_runtime_put(sdi->dss->dispc);

	regulator_disable(sdi->vdds_sdi_reg);
}

static void sdi_set_timings(struct omap_dss_device *dssdev,
			    const struct drm_display_mode *mode)
{
	struct sdi_device *sdi = dssdev_to_sdi(dssdev);

	sdi->pixelclock = mode->clock * 1000;
}

static int sdi_check_timings(struct omap_dss_device *dssdev,
			     struct drm_display_mode *mode)
{
	struct sdi_device *sdi = dssdev_to_sdi(dssdev);
	struct dispc_clock_info dispc_cinfo;
	unsigned long pixelclock = mode->clock * 1000;
	unsigned long fck;
	unsigned long pck;
	int r;

	if (pixelclock == 0)
		return -EINVAL;

	r = sdi_calc_clock_div(sdi, pixelclock, &fck, &dispc_cinfo);
	if (r)
		return r;

	pck = fck / dispc_cinfo.lck_div / dispc_cinfo.pck_div;

	if (pck != pixelclock) {
		DSSWARN("Pixel clock adjusted from %lu Hz to %lu Hz\n",
			pixelclock, pck);

		mode->clock = pck / 1000;
	}

	return 0;
}

static int sdi_connect(struct omap_dss_device *src,
		       struct omap_dss_device *dst)
{
	return omapdss_device_connect(dst->dss, dst, dst->next);
}

static void sdi_disconnect(struct omap_dss_device *src,
			   struct omap_dss_device *dst)
{
	omapdss_device_disconnect(dst, dst->next);
}

static const struct omap_dss_device_ops sdi_ops = {
	.connect = sdi_connect,
	.disconnect = sdi_disconnect,

	.enable = sdi_display_enable,
	.disable = sdi_display_disable,

	.check_timings = sdi_check_timings,
	.set_timings = sdi_set_timings,
};

static int sdi_init_output(struct sdi_device *sdi)
{
	struct omap_dss_device *out = &sdi->output;
	int r;

	out->dev = &sdi->pdev->dev;
	out->id = OMAP_DSS_OUTPUT_SDI;
	out->type = OMAP_DISPLAY_TYPE_SDI;
	out->name = "sdi.0";
	out->dispc_channel = OMAP_DSS_CHANNEL_LCD;
	/* We have SDI only on OMAP3, where it's on port 1 */
	out->of_ports = BIT(1);
	out->ops = &sdi_ops;
	out->owner = THIS_MODULE;
	out->bus_flags = DRM_BUS_FLAG_PIXDATA_DRIVE_POSEDGE	/* 15.5.9.1.2 */
		       | DRM_BUS_FLAG_SYNC_DRIVE_POSEDGE;

	r = omapdss_device_init_output(out);
	if (r < 0)
		return r;

	omapdss_device_register(out);

	return 0;
}

static void sdi_uninit_output(struct sdi_device *sdi)
{
	omapdss_device_unregister(&sdi->output);
	omapdss_device_cleanup_output(&sdi->output);
}

int sdi_init_port(struct dss_device *dss, struct platform_device *pdev,
		  struct device_node *port)
{
	struct sdi_device *sdi;
	struct device_node *ep;
	u32 datapairs;
	int r;

	sdi = kzalloc(sizeof(*sdi), GFP_KERNEL);
	if (!sdi)
		return -ENOMEM;

	ep = of_get_next_child(port, NULL);
	if (!ep) {
		r = 0;
		goto err_free;
	}

	r = of_property_read_u32(ep, "datapairs", &datapairs);
	of_node_put(ep);
	if (r) {
		DSSERR("failed to parse datapairs\n");
		goto err_free;
	}

	sdi->datapairs = datapairs;
	sdi->dss = dss;

	sdi->pdev = pdev;
	port->data = sdi;

	sdi->vdds_sdi_reg = devm_regulator_get(&pdev->dev, "vdds_sdi");
	if (IS_ERR(sdi->vdds_sdi_reg)) {
		r = PTR_ERR(sdi->vdds_sdi_reg);
		if (r != -EPROBE_DEFER)
			DSSERR("can't get VDDS_SDI regulator\n");
		goto err_free;
	}

	r = sdi_init_output(sdi);
	if (r)
		goto err_free;

	return 0;

err_free:
	kfree(sdi);

	return r;
}

void sdi_uninit_port(struct device_node *port)
{
	struct sdi_device *sdi = port->data;

	if (!sdi)
		return;

	sdi_uninit_output(sdi);
	kfree(sdi);
}
