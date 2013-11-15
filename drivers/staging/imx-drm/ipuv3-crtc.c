/*
 * i.MX IPUv3 Graphics driver
 *
 * Copyright (C) 2011 Sascha Hauer, Pengutronix
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */
#include <linux/module.h>
#include <linux/export.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <linux/fb.h>
#include <linux/clk.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_fb_cma_helper.h>

#include "ipu-v3/imx-ipu-v3.h"
#include "imx-drm.h"

#define DRIVER_DESC		"i.MX IPUv3 Graphics"

struct ipu_framebuffer {
	struct drm_framebuffer	base;
	void			*virt;
	dma_addr_t		phys;
	size_t			len;
};

struct ipu_crtc {
	struct device		*dev;
	struct drm_crtc		base;
	struct imx_drm_crtc	*imx_crtc;
	struct ipuv3_channel	*ipu_ch;
	struct ipu_dc		*dc;
	struct ipu_dp		*dp;
	struct dmfc_channel	*dmfc;
	struct ipu_di		*di;
	int			enabled;
	struct drm_pending_vblank_event *page_flip_event;
	struct drm_framebuffer	*newfb;
	int			irq;
	u32			interface_pix_fmt;
	unsigned long		di_clkflags;
	int			di_hsync_pin;
	int			di_vsync_pin;
};

#define to_ipu_crtc(x) container_of(x, struct ipu_crtc, base)

static int calc_vref(struct drm_display_mode *mode)
{
	unsigned long htotal, vtotal;

	htotal = mode->htotal;
	vtotal = mode->vtotal;

	if (!htotal || !vtotal)
		return 60;

	return mode->clock * 1000 / vtotal / htotal;
}

static int calc_bandwidth(struct drm_display_mode *mode, unsigned int vref)
{
	return mode->hdisplay * mode->vdisplay * vref;
}

static void ipu_fb_enable(struct ipu_crtc *ipu_crtc)
{
	if (ipu_crtc->enabled)
		return;

	ipu_di_enable(ipu_crtc->di);
	ipu_dmfc_enable_channel(ipu_crtc->dmfc);
	ipu_idmac_enable_channel(ipu_crtc->ipu_ch);
	ipu_dc_enable_channel(ipu_crtc->dc);
	if (ipu_crtc->dp)
		ipu_dp_enable_channel(ipu_crtc->dp);

	ipu_crtc->enabled = 1;
}

static void ipu_fb_disable(struct ipu_crtc *ipu_crtc)
{
	if (!ipu_crtc->enabled)
		return;

	if (ipu_crtc->dp)
		ipu_dp_disable_channel(ipu_crtc->dp);
	ipu_dc_disable_channel(ipu_crtc->dc);
	ipu_idmac_disable_channel(ipu_crtc->ipu_ch);
	ipu_dmfc_disable_channel(ipu_crtc->dmfc);
	ipu_di_disable(ipu_crtc->di);

	ipu_crtc->enabled = 0;
}

static void ipu_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	struct ipu_crtc *ipu_crtc = to_ipu_crtc(crtc);

	dev_dbg(ipu_crtc->dev, "%s mode: %d\n", __func__, mode);

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		ipu_fb_enable(ipu_crtc);
		break;
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_OFF:
		ipu_fb_disable(ipu_crtc);
		break;
	}
}

static int ipu_page_flip(struct drm_crtc *crtc,
		struct drm_framebuffer *fb,
		struct drm_pending_vblank_event *event,
		uint32_t page_flip_flags)
{
	struct ipu_crtc *ipu_crtc = to_ipu_crtc(crtc);
	int ret;

	if (ipu_crtc->newfb)
		return -EBUSY;

	ret = imx_drm_crtc_vblank_get(ipu_crtc->imx_crtc);
	if (ret) {
		dev_dbg(ipu_crtc->dev, "failed to acquire vblank counter\n");
		list_del(&event->base.link);

		return ret;
	}

	ipu_crtc->newfb = fb;
	ipu_crtc->page_flip_event = event;
	crtc->fb = fb;

	return 0;
}

static const struct drm_crtc_funcs ipu_crtc_funcs = {
	.set_config = drm_crtc_helper_set_config,
	.destroy = drm_crtc_cleanup,
	.page_flip = ipu_page_flip,
};

static int ipu_drm_set_base(struct drm_crtc *crtc, int x, int y)
{
	struct ipu_crtc *ipu_crtc = to_ipu_crtc(crtc);
	struct drm_gem_cma_object *cma_obj;
	struct drm_framebuffer *fb = crtc->fb;
	unsigned long phys;

	cma_obj = drm_fb_cma_get_gem_obj(fb, 0);
	if (!cma_obj) {
		DRM_LOG_KMS("entry is null.\n");
		return -EFAULT;
	}

	phys = cma_obj->paddr;
	phys += x * (fb->bits_per_pixel >> 3);
	phys += y * fb->pitches[0];

	dev_dbg(ipu_crtc->dev, "%s: phys: 0x%lx\n", __func__, phys);
	dev_dbg(ipu_crtc->dev, "%s: xy: %dx%d\n", __func__, x, y);

	ipu_cpmem_set_stride(ipu_get_cpmem(ipu_crtc->ipu_ch), fb->pitches[0]);
	ipu_cpmem_set_buffer(ipu_get_cpmem(ipu_crtc->ipu_ch),
			  0, phys);

	return 0;
}

static int ipu_crtc_mode_set(struct drm_crtc *crtc,
			       struct drm_display_mode *orig_mode,
			       struct drm_display_mode *mode,
			       int x, int y,
			       struct drm_framebuffer *old_fb)
{
	struct ipu_crtc *ipu_crtc = to_ipu_crtc(crtc);
	struct drm_framebuffer *fb = ipu_crtc->base.fb;
	int ret;
	struct ipu_di_signal_cfg sig_cfg = {};
	u32 out_pixel_fmt;
	struct ipu_ch_param __iomem *cpmem = ipu_get_cpmem(ipu_crtc->ipu_ch);
	int bpp;
	u32 v4l2_fmt;

	dev_dbg(ipu_crtc->dev, "%s: mode->hdisplay: %d\n", __func__,
			mode->hdisplay);
	dev_dbg(ipu_crtc->dev, "%s: mode->vdisplay: %d\n", __func__,
			mode->vdisplay);

	ipu_ch_param_zero(cpmem);

	switch (fb->pixel_format) {
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
		v4l2_fmt = V4L2_PIX_FMT_RGB32;
		bpp = 32;
		break;
	case DRM_FORMAT_RGB565:
		v4l2_fmt = V4L2_PIX_FMT_RGB565;
		bpp = 16;
		break;
	case DRM_FORMAT_RGB888:
		v4l2_fmt = V4L2_PIX_FMT_RGB24;
		bpp = 24;
		break;
	default:
		dev_err(ipu_crtc->dev, "unsupported pixel format 0x%08x\n",
				fb->pixel_format);
		return -EINVAL;
	}

	out_pixel_fmt = ipu_crtc->interface_pix_fmt;

	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		sig_cfg.interlaced = 1;
	if (mode->flags & DRM_MODE_FLAG_PHSYNC)
		sig_cfg.Hsync_pol = 1;
	if (mode->flags & DRM_MODE_FLAG_PVSYNC)
		sig_cfg.Vsync_pol = 1;

	sig_cfg.enable_pol = 1;
	sig_cfg.clk_pol = 0;
	sig_cfg.width = mode->hdisplay;
	sig_cfg.height = mode->vdisplay;
	sig_cfg.pixel_fmt = out_pixel_fmt;
	sig_cfg.h_start_width = mode->htotal - mode->hsync_end;
	sig_cfg.h_sync_width = mode->hsync_end - mode->hsync_start;
	sig_cfg.h_end_width = mode->hsync_start - mode->hdisplay;

	sig_cfg.v_start_width = mode->vtotal - mode->vsync_end;
	sig_cfg.v_sync_width = mode->vsync_end - mode->vsync_start;
	sig_cfg.v_end_width = mode->vsync_start - mode->vdisplay;
	sig_cfg.pixelclock = mode->clock * 1000;
	sig_cfg.clkflags = ipu_crtc->di_clkflags;

	sig_cfg.v_to_h_sync = 0;

	sig_cfg.hsync_pin = ipu_crtc->di_hsync_pin;
	sig_cfg.vsync_pin = ipu_crtc->di_vsync_pin;

	if (ipu_crtc->dp) {
		ret = ipu_dp_setup_channel(ipu_crtc->dp, IPUV3_COLORSPACE_RGB,
				IPUV3_COLORSPACE_RGB);
		if (ret) {
			dev_err(ipu_crtc->dev,
				"initializing display processor failed with %d\n",
				ret);
			return ret;
		}
		ipu_dp_set_global_alpha(ipu_crtc->dp, 1, 0, 1);
	}

	ret = ipu_dc_init_sync(ipu_crtc->dc, ipu_crtc->di, sig_cfg.interlaced,
			out_pixel_fmt, mode->hdisplay);
	if (ret) {
		dev_err(ipu_crtc->dev,
				"initializing display controller failed with %d\n",
				ret);
		return ret;
	}

	ret = ipu_di_init_sync_panel(ipu_crtc->di, &sig_cfg);
	if (ret) {
		dev_err(ipu_crtc->dev,
				"initializing panel failed with %d\n", ret);
		return ret;
	}

	ipu_cpmem_set_resolution(cpmem, mode->hdisplay, mode->vdisplay);
	ipu_cpmem_set_fmt(cpmem, v4l2_fmt);
	ipu_cpmem_set_high_priority(ipu_crtc->ipu_ch);

	ret = ipu_dmfc_init_channel(ipu_crtc->dmfc, mode->hdisplay);
	if (ret) {
		dev_err(ipu_crtc->dev,
				"initializing dmfc channel failed with %d\n",
				ret);
		return ret;
	}

	ret = ipu_dmfc_alloc_bandwidth(ipu_crtc->dmfc,
			calc_bandwidth(mode, calc_vref(mode)), 64);
	if (ret) {
		dev_err(ipu_crtc->dev,
				"allocating dmfc bandwidth failed with %d\n",
				ret);
		return ret;
	}

	ipu_drm_set_base(crtc, x, y);

	return 0;
}

static void ipu_crtc_handle_pageflip(struct ipu_crtc *ipu_crtc)
{
	unsigned long flags;
	struct drm_device *drm = ipu_crtc->base.dev;

	spin_lock_irqsave(&drm->event_lock, flags);
	if (ipu_crtc->page_flip_event)
		drm_send_vblank_event(drm, -1, ipu_crtc->page_flip_event);
	ipu_crtc->page_flip_event = NULL;
	imx_drm_crtc_vblank_put(ipu_crtc->imx_crtc);
	spin_unlock_irqrestore(&drm->event_lock, flags);
}

static irqreturn_t ipu_irq_handler(int irq, void *dev_id)
{
	struct ipu_crtc *ipu_crtc = dev_id;

	imx_drm_handle_vblank(ipu_crtc->imx_crtc);

	if (ipu_crtc->newfb) {
		ipu_crtc->newfb = NULL;
		ipu_drm_set_base(&ipu_crtc->base, 0, 0);
		ipu_crtc_handle_pageflip(ipu_crtc);
	}

	return IRQ_HANDLED;
}

static bool ipu_crtc_mode_fixup(struct drm_crtc *crtc,
				  const struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode)
{
	return true;
}

static void ipu_crtc_prepare(struct drm_crtc *crtc)
{
	struct ipu_crtc *ipu_crtc = to_ipu_crtc(crtc);

	ipu_fb_disable(ipu_crtc);
}

static void ipu_crtc_commit(struct drm_crtc *crtc)
{
	struct ipu_crtc *ipu_crtc = to_ipu_crtc(crtc);

	ipu_fb_enable(ipu_crtc);
}

static struct drm_crtc_helper_funcs ipu_helper_funcs = {
	.dpms = ipu_crtc_dpms,
	.mode_fixup = ipu_crtc_mode_fixup,
	.mode_set = ipu_crtc_mode_set,
	.prepare = ipu_crtc_prepare,
	.commit = ipu_crtc_commit,
};

static int ipu_enable_vblank(struct drm_crtc *crtc)
{
	struct ipu_crtc *ipu_crtc = to_ipu_crtc(crtc);

	enable_irq(ipu_crtc->irq);

	return 0;
}

static void ipu_disable_vblank(struct drm_crtc *crtc)
{
	struct ipu_crtc *ipu_crtc = to_ipu_crtc(crtc);

	disable_irq(ipu_crtc->irq);
}

static int ipu_set_interface_pix_fmt(struct drm_crtc *crtc, u32 encoder_type,
		u32 pixfmt, int hsync_pin, int vsync_pin)
{
	struct ipu_crtc *ipu_crtc = to_ipu_crtc(crtc);

	ipu_crtc->interface_pix_fmt = pixfmt;
	ipu_crtc->di_hsync_pin = hsync_pin;
	ipu_crtc->di_vsync_pin = vsync_pin;

	switch (encoder_type) {
	case DRM_MODE_ENCODER_DAC:
	case DRM_MODE_ENCODER_TVDAC:
	case DRM_MODE_ENCODER_LVDS:
		ipu_crtc->di_clkflags = IPU_DI_CLKMODE_SYNC |
			IPU_DI_CLKMODE_EXT;
		break;
	case DRM_MODE_ENCODER_NONE:
		ipu_crtc->di_clkflags = 0;
		break;
	}

	return 0;
}

static const struct imx_drm_crtc_helper_funcs ipu_crtc_helper_funcs = {
	.enable_vblank = ipu_enable_vblank,
	.disable_vblank = ipu_disable_vblank,
	.set_interface_pix_fmt = ipu_set_interface_pix_fmt,
	.crtc_funcs = &ipu_crtc_funcs,
	.crtc_helper_funcs = &ipu_helper_funcs,
};

static void ipu_put_resources(struct ipu_crtc *ipu_crtc)
{
	if (!IS_ERR_OR_NULL(ipu_crtc->ipu_ch))
		ipu_idmac_put(ipu_crtc->ipu_ch);
	if (!IS_ERR_OR_NULL(ipu_crtc->dmfc))
		ipu_dmfc_put(ipu_crtc->dmfc);
	if (!IS_ERR_OR_NULL(ipu_crtc->dp))
		ipu_dp_put(ipu_crtc->dp);
	if (!IS_ERR_OR_NULL(ipu_crtc->di))
		ipu_di_put(ipu_crtc->di);
}

static int ipu_get_resources(struct ipu_crtc *ipu_crtc,
		struct ipu_client_platformdata *pdata)
{
	struct ipu_soc *ipu = dev_get_drvdata(ipu_crtc->dev->parent);
	int ret;

	ipu_crtc->ipu_ch = ipu_idmac_get(ipu, pdata->dma[0]);
	if (IS_ERR(ipu_crtc->ipu_ch)) {
		ret = PTR_ERR(ipu_crtc->ipu_ch);
		goto err_out;
	}

	ipu_crtc->dc = ipu_dc_get(ipu, pdata->dc);
	if (IS_ERR(ipu_crtc->dc)) {
		ret = PTR_ERR(ipu_crtc->dc);
		goto err_out;
	}

	ipu_crtc->dmfc = ipu_dmfc_get(ipu, pdata->dma[0]);
	if (IS_ERR(ipu_crtc->dmfc)) {
		ret = PTR_ERR(ipu_crtc->dmfc);
		goto err_out;
	}

	if (pdata->dp >= 0) {
		ipu_crtc->dp = ipu_dp_get(ipu, pdata->dp);
		if (IS_ERR(ipu_crtc->dp)) {
			ret = PTR_ERR(ipu_crtc->dp);
			goto err_out;
		}
	}

	ipu_crtc->di = ipu_di_get(ipu, pdata->di);
	if (IS_ERR(ipu_crtc->di)) {
		ret = PTR_ERR(ipu_crtc->di);
		goto err_out;
	}

	return 0;
err_out:
	ipu_put_resources(ipu_crtc);

	return ret;
}

static int ipu_crtc_init(struct ipu_crtc *ipu_crtc,
		struct ipu_client_platformdata *pdata)
{
	struct ipu_soc *ipu = dev_get_drvdata(ipu_crtc->dev->parent);
	int ret;

	ret = ipu_get_resources(ipu_crtc, pdata);
	if (ret) {
		dev_err(ipu_crtc->dev, "getting resources failed with %d.\n",
				ret);
		return ret;
	}

	ret = imx_drm_add_crtc(&ipu_crtc->base,
			&ipu_crtc->imx_crtc,
			&ipu_crtc_helper_funcs, THIS_MODULE,
			ipu_crtc->dev->parent->of_node, pdata->di);
	if (ret) {
		dev_err(ipu_crtc->dev, "adding crtc failed with %d.\n", ret);
		goto err_put_resources;
	}

	ipu_crtc->irq = ipu_idmac_channel_irq(ipu, ipu_crtc->ipu_ch,
			IPU_IRQ_EOF);
	ret = devm_request_irq(ipu_crtc->dev, ipu_crtc->irq, ipu_irq_handler, 0,
			"imx_drm", ipu_crtc);
	if (ret < 0) {
		dev_err(ipu_crtc->dev, "irq request failed with %d.\n", ret);
		goto err_put_resources;
	}

	disable_irq(ipu_crtc->irq);

	return 0;

err_put_resources:
	ipu_put_resources(ipu_crtc);

	return ret;
}

static int ipu_drm_probe(struct platform_device *pdev)
{
	struct ipu_client_platformdata *pdata = pdev->dev.platform_data;
	struct ipu_crtc *ipu_crtc;
	int ret;

	if (!pdata)
		return -EINVAL;

	pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);

	ipu_crtc = devm_kzalloc(&pdev->dev, sizeof(*ipu_crtc), GFP_KERNEL);
	if (!ipu_crtc)
		return -ENOMEM;

	ipu_crtc->dev = &pdev->dev;

	ret = ipu_crtc_init(ipu_crtc, pdata);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, ipu_crtc);

	return 0;
}

static int ipu_drm_remove(struct platform_device *pdev)
{
	struct ipu_crtc *ipu_crtc = platform_get_drvdata(pdev);

	imx_drm_remove_crtc(ipu_crtc->imx_crtc);

	ipu_put_resources(ipu_crtc);

	return 0;
}

static struct platform_driver ipu_drm_driver = {
	.driver = {
		.name = "imx-ipuv3-crtc",
	},
	.probe = ipu_drm_probe,
	.remove = ipu_drm_remove,
};
module_platform_driver(ipu_drm_driver);

MODULE_AUTHOR("Sascha Hauer <s.hauer@pengutronix.de>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:imx-ipuv3-crtc");
