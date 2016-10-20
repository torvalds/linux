/*
 * Copyright (C) 2015 Free Electrons
 * Copyright (C) 2015 NextThing Co
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#include <linux/component.h>
#include <linux/ioport.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/of_irq.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#include "sun4i_crtc.h"
#include "sun4i_dotclock.h"
#include "sun4i_drv.h"
#include "sun4i_rgb.h"
#include "sun4i_tcon.h"

void sun4i_tcon_disable(struct sun4i_tcon *tcon)
{
	DRM_DEBUG_DRIVER("Disabling TCON\n");

	/* Disable the TCON */
	regmap_update_bits(tcon->regs, SUN4I_TCON_GCTL_REG,
			   SUN4I_TCON_GCTL_TCON_ENABLE, 0);
}
EXPORT_SYMBOL(sun4i_tcon_disable);

void sun4i_tcon_enable(struct sun4i_tcon *tcon)
{
	DRM_DEBUG_DRIVER("Enabling TCON\n");

	/* Enable the TCON */
	regmap_update_bits(tcon->regs, SUN4I_TCON_GCTL_REG,
			   SUN4I_TCON_GCTL_TCON_ENABLE,
			   SUN4I_TCON_GCTL_TCON_ENABLE);
}
EXPORT_SYMBOL(sun4i_tcon_enable);

void sun4i_tcon_channel_disable(struct sun4i_tcon *tcon, int channel)
{
	/* Disable the TCON's channel */
	if (channel == 0) {
		regmap_update_bits(tcon->regs, SUN4I_TCON0_CTL_REG,
				   SUN4I_TCON0_CTL_TCON_ENABLE, 0);
		clk_disable_unprepare(tcon->dclk);
		return;
	}

	WARN_ON(!tcon->quirks->has_channel_1);
	regmap_update_bits(tcon->regs, SUN4I_TCON1_CTL_REG,
			   SUN4I_TCON1_CTL_TCON_ENABLE, 0);
	clk_disable_unprepare(tcon->sclk1);
}
EXPORT_SYMBOL(sun4i_tcon_channel_disable);

void sun4i_tcon_channel_enable(struct sun4i_tcon *tcon, int channel)
{
	/* Enable the TCON's channel */
	if (channel == 0) {
		regmap_update_bits(tcon->regs, SUN4I_TCON0_CTL_REG,
				   SUN4I_TCON0_CTL_TCON_ENABLE,
				   SUN4I_TCON0_CTL_TCON_ENABLE);
		clk_prepare_enable(tcon->dclk);
		return;
	}

	WARN_ON(!tcon->quirks->has_channel_1);
	regmap_update_bits(tcon->regs, SUN4I_TCON1_CTL_REG,
			   SUN4I_TCON1_CTL_TCON_ENABLE,
			   SUN4I_TCON1_CTL_TCON_ENABLE);
	clk_prepare_enable(tcon->sclk1);
}
EXPORT_SYMBOL(sun4i_tcon_channel_enable);

void sun4i_tcon_enable_vblank(struct sun4i_tcon *tcon, bool enable)
{
	u32 mask, val = 0;

	DRM_DEBUG_DRIVER("%sabling VBLANK interrupt\n", enable ? "En" : "Dis");

	mask = SUN4I_TCON_GINT0_VBLANK_ENABLE(0) |
	       SUN4I_TCON_GINT0_VBLANK_ENABLE(1);

	if (enable)
		val = mask;

	regmap_update_bits(tcon->regs, SUN4I_TCON_GINT0_REG, mask, val);
}
EXPORT_SYMBOL(sun4i_tcon_enable_vblank);

static int sun4i_tcon_get_clk_delay(struct drm_display_mode *mode,
				    int channel)
{
	int delay = mode->vtotal - mode->vdisplay;

	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		delay /= 2;

	if (channel == 1)
		delay -= 2;

	delay = min(delay, 30);

	DRM_DEBUG_DRIVER("TCON %d clock delay %u\n", channel, delay);

	return delay;
}

void sun4i_tcon0_mode_set(struct sun4i_tcon *tcon,
			  struct drm_display_mode *mode)
{
	unsigned int bp, hsync, vsync;
	u8 clk_delay;
	u32 val = 0;

	/* Adjust clock delay */
	clk_delay = sun4i_tcon_get_clk_delay(mode, 0);
	regmap_update_bits(tcon->regs, SUN4I_TCON0_CTL_REG,
			   SUN4I_TCON0_CTL_CLK_DELAY_MASK,
			   SUN4I_TCON0_CTL_CLK_DELAY(clk_delay));

	/* Set the resolution */
	regmap_write(tcon->regs, SUN4I_TCON0_BASIC0_REG,
		     SUN4I_TCON0_BASIC0_X(mode->crtc_hdisplay) |
		     SUN4I_TCON0_BASIC0_Y(mode->crtc_vdisplay));

	/*
	 * This is called a backporch in the register documentation,
	 * but it really is the front porch + hsync
	 */
	bp = mode->crtc_htotal - mode->crtc_hsync_start;
	DRM_DEBUG_DRIVER("Setting horizontal total %d, backporch %d\n",
			 mode->crtc_htotal, bp);

	/* Set horizontal display timings */
	regmap_write(tcon->regs, SUN4I_TCON0_BASIC1_REG,
		     SUN4I_TCON0_BASIC1_H_TOTAL(mode->crtc_htotal) |
		     SUN4I_TCON0_BASIC1_H_BACKPORCH(bp));

	/*
	 * This is called a backporch in the register documentation,
	 * but it really is the front porch + hsync
	 */
	bp = mode->crtc_vtotal - mode->crtc_vsync_start;
	DRM_DEBUG_DRIVER("Setting vertical total %d, backporch %d\n",
			 mode->crtc_vtotal, bp);

	/* Set vertical display timings */
	regmap_write(tcon->regs, SUN4I_TCON0_BASIC2_REG,
		     SUN4I_TCON0_BASIC2_V_TOTAL(mode->crtc_vtotal) |
		     SUN4I_TCON0_BASIC2_V_BACKPORCH(bp));

	/* Set Hsync and Vsync length */
	hsync = mode->crtc_hsync_end - mode->crtc_hsync_start;
	vsync = mode->crtc_vsync_end - mode->crtc_vsync_start;
	DRM_DEBUG_DRIVER("Setting HSYNC %d, VSYNC %d\n", hsync, vsync);
	regmap_write(tcon->regs, SUN4I_TCON0_BASIC3_REG,
		     SUN4I_TCON0_BASIC3_V_SYNC(vsync) |
		     SUN4I_TCON0_BASIC3_H_SYNC(hsync));

	/* Setup the polarity of the various signals */
	if (!(mode->flags & DRM_MODE_FLAG_PHSYNC))
		val |= SUN4I_TCON0_IO_POL_HSYNC_POSITIVE;

	if (!(mode->flags & DRM_MODE_FLAG_PVSYNC))
		val |= SUN4I_TCON0_IO_POL_VSYNC_POSITIVE;

	regmap_update_bits(tcon->regs, SUN4I_TCON0_IO_POL_REG,
			   SUN4I_TCON0_IO_POL_HSYNC_POSITIVE | SUN4I_TCON0_IO_POL_VSYNC_POSITIVE,
			   val);

	/* Map output pins to channel 0 */
	regmap_update_bits(tcon->regs, SUN4I_TCON_GCTL_REG,
			   SUN4I_TCON_GCTL_IOMAP_MASK,
			   SUN4I_TCON_GCTL_IOMAP_TCON0);

	/* Enable the output on the pins */
	regmap_write(tcon->regs, SUN4I_TCON0_IO_TRI_REG, 0);
}
EXPORT_SYMBOL(sun4i_tcon0_mode_set);

void sun4i_tcon1_mode_set(struct sun4i_tcon *tcon,
			  struct drm_display_mode *mode)
{
	unsigned int bp, hsync, vsync;
	u8 clk_delay;
	u32 val;

	WARN_ON(!tcon->quirks->has_channel_1);

	/* Adjust clock delay */
	clk_delay = sun4i_tcon_get_clk_delay(mode, 1);
	regmap_update_bits(tcon->regs, SUN4I_TCON1_CTL_REG,
			   SUN4I_TCON1_CTL_CLK_DELAY_MASK,
			   SUN4I_TCON1_CTL_CLK_DELAY(clk_delay));

	/* Set interlaced mode */
	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		val = SUN4I_TCON1_CTL_INTERLACE_ENABLE;
	else
		val = 0;
	regmap_update_bits(tcon->regs, SUN4I_TCON1_CTL_REG,
			   SUN4I_TCON1_CTL_INTERLACE_ENABLE,
			   val);

	/* Set the input resolution */
	regmap_write(tcon->regs, SUN4I_TCON1_BASIC0_REG,
		     SUN4I_TCON1_BASIC0_X(mode->crtc_hdisplay) |
		     SUN4I_TCON1_BASIC0_Y(mode->crtc_vdisplay));

	/* Set the upscaling resolution */
	regmap_write(tcon->regs, SUN4I_TCON1_BASIC1_REG,
		     SUN4I_TCON1_BASIC1_X(mode->crtc_hdisplay) |
		     SUN4I_TCON1_BASIC1_Y(mode->crtc_vdisplay));

	/* Set the output resolution */
	regmap_write(tcon->regs, SUN4I_TCON1_BASIC2_REG,
		     SUN4I_TCON1_BASIC2_X(mode->crtc_hdisplay) |
		     SUN4I_TCON1_BASIC2_Y(mode->crtc_vdisplay));

	/* Set horizontal display timings */
	bp = mode->crtc_htotal - mode->crtc_hsync_end;
	DRM_DEBUG_DRIVER("Setting horizontal total %d, backporch %d\n",
			 mode->htotal, bp);
	regmap_write(tcon->regs, SUN4I_TCON1_BASIC3_REG,
		     SUN4I_TCON1_BASIC3_H_TOTAL(mode->crtc_htotal) |
		     SUN4I_TCON1_BASIC3_H_BACKPORCH(bp));

	/* Set vertical display timings */
	bp = mode->crtc_vtotal - mode->crtc_vsync_end;
	DRM_DEBUG_DRIVER("Setting vertical total %d, backporch %d\n",
			 mode->vtotal, bp);
	regmap_write(tcon->regs, SUN4I_TCON1_BASIC4_REG,
		     SUN4I_TCON1_BASIC4_V_TOTAL(mode->vtotal) |
		     SUN4I_TCON1_BASIC4_V_BACKPORCH(bp));

	/* Set Hsync and Vsync length */
	hsync = mode->crtc_hsync_end - mode->crtc_hsync_start;
	vsync = mode->crtc_vsync_end - mode->crtc_vsync_start;
	DRM_DEBUG_DRIVER("Setting HSYNC %d, VSYNC %d\n", hsync, vsync);
	regmap_write(tcon->regs, SUN4I_TCON1_BASIC5_REG,
		     SUN4I_TCON1_BASIC5_V_SYNC(vsync) |
		     SUN4I_TCON1_BASIC5_H_SYNC(hsync));

	/* Map output pins to channel 1 */
	regmap_update_bits(tcon->regs, SUN4I_TCON_GCTL_REG,
			   SUN4I_TCON_GCTL_IOMAP_MASK,
			   SUN4I_TCON_GCTL_IOMAP_TCON1);

	/*
	 * FIXME: Undocumented bits
	 */
	if (tcon->quirks->has_unknown_mux)
		regmap_write(tcon->regs, SUN4I_TCON_MUX_CTRL_REG, 1);
}
EXPORT_SYMBOL(sun4i_tcon1_mode_set);

static void sun4i_tcon_finish_page_flip(struct drm_device *dev,
					struct sun4i_crtc *scrtc)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);
	if (scrtc->event) {
		drm_crtc_send_vblank_event(&scrtc->crtc, scrtc->event);
		drm_crtc_vblank_put(&scrtc->crtc);
		scrtc->event = NULL;
	}
	spin_unlock_irqrestore(&dev->event_lock, flags);
}

static irqreturn_t sun4i_tcon_handler(int irq, void *private)
{
	struct sun4i_tcon *tcon = private;
	struct drm_device *drm = tcon->drm;
	struct sun4i_drv *drv = drm->dev_private;
	struct sun4i_crtc *scrtc = drv->crtc;
	unsigned int status;

	regmap_read(tcon->regs, SUN4I_TCON_GINT0_REG, &status);

	if (!(status & (SUN4I_TCON_GINT0_VBLANK_INT(0) |
			SUN4I_TCON_GINT0_VBLANK_INT(1))))
		return IRQ_NONE;

	drm_crtc_handle_vblank(&scrtc->crtc);
	sun4i_tcon_finish_page_flip(drm, scrtc);

	/* Acknowledge the interrupt */
	regmap_update_bits(tcon->regs, SUN4I_TCON_GINT0_REG,
			   SUN4I_TCON_GINT0_VBLANK_INT(0) |
			   SUN4I_TCON_GINT0_VBLANK_INT(1),
			   0);

	return IRQ_HANDLED;
}

static int sun4i_tcon_init_clocks(struct device *dev,
				  struct sun4i_tcon *tcon)
{
	tcon->clk = devm_clk_get(dev, "ahb");
	if (IS_ERR(tcon->clk)) {
		dev_err(dev, "Couldn't get the TCON bus clock\n");
		return PTR_ERR(tcon->clk);
	}
	clk_prepare_enable(tcon->clk);

	tcon->sclk0 = devm_clk_get(dev, "tcon-ch0");
	if (IS_ERR(tcon->sclk0)) {
		dev_err(dev, "Couldn't get the TCON channel 0 clock\n");
		return PTR_ERR(tcon->sclk0);
	}

	if (tcon->quirks->has_channel_1) {
		tcon->sclk1 = devm_clk_get(dev, "tcon-ch1");
		if (IS_ERR(tcon->sclk1)) {
			dev_err(dev, "Couldn't get the TCON channel 1 clock\n");
			return PTR_ERR(tcon->sclk1);
		}
	}

	return sun4i_dclk_create(dev, tcon);
}

static void sun4i_tcon_free_clocks(struct sun4i_tcon *tcon)
{
	sun4i_dclk_free(tcon);
	clk_disable_unprepare(tcon->clk);
}

static int sun4i_tcon_init_irq(struct device *dev,
			       struct sun4i_tcon *tcon)
{
	struct platform_device *pdev = to_platform_device(dev);
	int irq, ret;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "Couldn't retrieve the TCON interrupt\n");
		return irq;
	}

	ret = devm_request_irq(dev, irq, sun4i_tcon_handler, 0,
			       dev_name(dev), tcon);
	if (ret) {
		dev_err(dev, "Couldn't request the IRQ\n");
		return ret;
	}

	return 0;
}

static struct regmap_config sun4i_tcon_regmap_config = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.max_register	= 0x800,
};

static int sun4i_tcon_init_regmap(struct device *dev,
				  struct sun4i_tcon *tcon)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct resource *res;
	void __iomem *regs;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	tcon->regs = devm_regmap_init_mmio(dev, regs,
					   &sun4i_tcon_regmap_config);
	if (IS_ERR(tcon->regs)) {
		dev_err(dev, "Couldn't create the TCON regmap\n");
		return PTR_ERR(tcon->regs);
	}

	/* Make sure the TCON is disabled and all IRQs are off */
	regmap_write(tcon->regs, SUN4I_TCON_GCTL_REG, 0);
	regmap_write(tcon->regs, SUN4I_TCON_GINT0_REG, 0);
	regmap_write(tcon->regs, SUN4I_TCON_GINT1_REG, 0);

	/* Disable IO lines and set them to tristate */
	regmap_write(tcon->regs, SUN4I_TCON0_IO_TRI_REG, ~0);
	regmap_write(tcon->regs, SUN4I_TCON1_IO_TRI_REG, ~0);

	return 0;
}

struct drm_panel *sun4i_tcon_find_panel(struct device_node *node)
{
	struct device_node *port, *remote, *child;
	struct device_node *end_node = NULL;

	/* Inputs are listed first, then outputs */
	port = of_graph_get_port_by_id(node, 1);

	/*
	 * Our first output is the RGB interface where the panel will
	 * be connected.
	 */
	for_each_child_of_node(port, child) {
		u32 reg;

		of_property_read_u32(child, "reg", &reg);
		if (reg == 0)
			end_node = child;
	}

	if (!end_node) {
		DRM_DEBUG_DRIVER("Missing panel endpoint\n");
		return ERR_PTR(-ENODEV);
	}

	remote = of_graph_get_remote_port_parent(end_node);
	if (!remote) {
		DRM_DEBUG_DRIVER("Unable to parse remote node\n");
		return ERR_PTR(-EINVAL);
	}

	return of_drm_find_panel(remote) ?: ERR_PTR(-EPROBE_DEFER);
}

struct drm_bridge *sun4i_tcon_find_bridge(struct device_node *node)
{
	struct device_node *port, *remote, *child;
	struct device_node *end_node = NULL;

	/* Inputs are listed first, then outputs */
	port = of_graph_get_port_by_id(node, 1);

	/*
	 * Our first output is the RGB interface where the panel will
	 * be connected.
	 */
	for_each_child_of_node(port, child) {
		u32 reg;

		of_property_read_u32(child, "reg", &reg);
		if (reg == 0)
			end_node = child;
	}

	if (!end_node) {
		DRM_DEBUG_DRIVER("Missing bridge endpoint\n");
		return ERR_PTR(-ENODEV);
	}

	remote = of_graph_get_remote_port_parent(end_node);
	if (!remote) {
		DRM_DEBUG_DRIVER("Enable to parse remote node\n");
		return ERR_PTR(-EINVAL);
	}

	return of_drm_find_bridge(remote) ?: ERR_PTR(-EPROBE_DEFER);
}

static int sun4i_tcon_bind(struct device *dev, struct device *master,
			   void *data)
{
	struct drm_device *drm = data;
	struct sun4i_drv *drv = drm->dev_private;
	struct sun4i_tcon *tcon;
	int ret;

	tcon = devm_kzalloc(dev, sizeof(*tcon), GFP_KERNEL);
	if (!tcon)
		return -ENOMEM;
	dev_set_drvdata(dev, tcon);
	drv->tcon = tcon;
	tcon->drm = drm;
	tcon->dev = dev;
	tcon->quirks = of_device_get_match_data(dev);

	tcon->lcd_rst = devm_reset_control_get(dev, "lcd");
	if (IS_ERR(tcon->lcd_rst)) {
		dev_err(dev, "Couldn't get our reset line\n");
		return PTR_ERR(tcon->lcd_rst);
	}

	/* Make sure our TCON is reset */
	if (!reset_control_status(tcon->lcd_rst))
		reset_control_assert(tcon->lcd_rst);

	ret = reset_control_deassert(tcon->lcd_rst);
	if (ret) {
		dev_err(dev, "Couldn't deassert our reset line\n");
		return ret;
	}

	ret = sun4i_tcon_init_regmap(dev, tcon);
	if (ret) {
		dev_err(dev, "Couldn't init our TCON regmap\n");
		goto err_assert_reset;
	}

	ret = sun4i_tcon_init_clocks(dev, tcon);
	if (ret) {
		dev_err(dev, "Couldn't init our TCON clocks\n");
		goto err_assert_reset;
	}

	ret = sun4i_tcon_init_irq(dev, tcon);
	if (ret) {
		dev_err(dev, "Couldn't init our TCON interrupts\n");
		goto err_free_clocks;
	}

	ret = sun4i_rgb_init(drm);
	if (ret < 0)
		goto err_free_clocks;

	return 0;

err_free_clocks:
	sun4i_tcon_free_clocks(tcon);
err_assert_reset:
	reset_control_assert(tcon->lcd_rst);
	return ret;
}

static void sun4i_tcon_unbind(struct device *dev, struct device *master,
			      void *data)
{
	struct sun4i_tcon *tcon = dev_get_drvdata(dev);

	sun4i_tcon_free_clocks(tcon);
}

static struct component_ops sun4i_tcon_ops = {
	.bind	= sun4i_tcon_bind,
	.unbind	= sun4i_tcon_unbind,
};

static int sun4i_tcon_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct drm_bridge *bridge;
	struct drm_panel *panel;

	/*
	 * Neither the bridge or the panel is ready.
	 * Defer the probe.
	 */
	panel = sun4i_tcon_find_panel(node);
	bridge = sun4i_tcon_find_bridge(node);

	/*
	 * If we don't have a panel endpoint, just go on
	 */
	if ((PTR_ERR(panel) == -EPROBE_DEFER) &&
	    (PTR_ERR(bridge) == -EPROBE_DEFER)) {
		DRM_DEBUG_DRIVER("Still waiting for our panel/bridge. Deferring...\n");
		return -EPROBE_DEFER;
	}

	return component_add(&pdev->dev, &sun4i_tcon_ops);
}

static int sun4i_tcon_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &sun4i_tcon_ops);

	return 0;
}

static const struct sun4i_tcon_quirks sun5i_a13_quirks = {
	.has_unknown_mux = true,
	.has_channel_1	= true,
};

static const struct sun4i_tcon_quirks sun6i_a31_quirks = {
	.has_channel_1	= true,
};

static const struct sun4i_tcon_quirks sun6i_a31s_quirks = {
	.has_channel_1	= true,
};

static const struct sun4i_tcon_quirks sun8i_a33_quirks = {
	/* nothing is supported */
};

static const struct of_device_id sun4i_tcon_of_table[] = {
	{ .compatible = "allwinner,sun5i-a13-tcon", .data = &sun5i_a13_quirks },
	{ .compatible = "allwinner,sun6i-a31-tcon", .data = &sun6i_a31_quirks },
	{ .compatible = "allwinner,sun6i-a31s-tcon", .data = &sun6i_a31s_quirks },
	{ .compatible = "allwinner,sun8i-a33-tcon", .data = &sun8i_a33_quirks },
	{ }
};
MODULE_DEVICE_TABLE(of, sun4i_tcon_of_table);

static struct platform_driver sun4i_tcon_platform_driver = {
	.probe		= sun4i_tcon_probe,
	.remove		= sun4i_tcon_remove,
	.driver		= {
		.name		= "sun4i-tcon",
		.of_match_table	= sun4i_tcon_of_table,
	},
};
module_platform_driver(sun4i_tcon_platform_driver);

MODULE_AUTHOR("Maxime Ripard <maxime.ripard@free-electrons.com>");
MODULE_DESCRIPTION("Allwinner A10 Timing Controller Driver");
MODULE_LICENSE("GPL");
