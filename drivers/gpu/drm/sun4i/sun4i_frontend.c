// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2017 Free Electrons
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 */
#include <drm/drmP.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_fb_cma_helper.h>

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#include "sun4i_drv.h"
#include "sun4i_frontend.h"

static const u32 sun4i_frontend_vert_coef[32] = {
	0x00004000, 0x000140ff, 0x00033ffe, 0x00043ffd,
	0x00063efc, 0xff083dfc, 0x000a3bfb, 0xff0d39fb,
	0xff0f37fb, 0xff1136fa, 0xfe1433fb, 0xfe1631fb,
	0xfd192ffb, 0xfd1c2cfb, 0xfd1f29fb, 0xfc2127fc,
	0xfc2424fc, 0xfc2721fc, 0xfb291ffd, 0xfb2c1cfd,
	0xfb2f19fd, 0xfb3116fe, 0xfb3314fe, 0xfa3611ff,
	0xfb370fff, 0xfb390dff, 0xfb3b0a00, 0xfc3d08ff,
	0xfc3e0600, 0xfd3f0400, 0xfe3f0300, 0xff400100,
};

static const u32 sun4i_frontend_horz_coef[64] = {
	0x40000000, 0x00000000, 0x40fe0000, 0x0000ff03,
	0x3ffd0000, 0x0000ff05, 0x3ffc0000, 0x0000ff06,
	0x3efb0000, 0x0000ff08, 0x3dfb0000, 0x0000ff09,
	0x3bfa0000, 0x0000fe0d, 0x39fa0000, 0x0000fe0f,
	0x38fa0000, 0x0000fe10, 0x36fa0000, 0x0000fe12,
	0x33fa0000, 0x0000fd16, 0x31fa0000, 0x0000fd18,
	0x2ffa0000, 0x0000fd1a, 0x2cfa0000, 0x0000fc1e,
	0x29fa0000, 0x0000fc21, 0x27fb0000, 0x0000fb23,
	0x24fb0000, 0x0000fb26, 0x21fb0000, 0x0000fb29,
	0x1ffc0000, 0x0000fa2b, 0x1cfc0000, 0x0000fa2e,
	0x19fd0000, 0x0000fa30, 0x16fd0000, 0x0000fa33,
	0x14fd0000, 0x0000fa35, 0x11fe0000, 0x0000fa37,
	0x0ffe0000, 0x0000fa39, 0x0dfe0000, 0x0000fa3b,
	0x0afe0000, 0x0000fa3e, 0x08ff0000, 0x0000fb3e,
	0x06ff0000, 0x0000fb40, 0x05ff0000, 0x0000fc40,
	0x03ff0000, 0x0000fd41, 0x01ff0000, 0x0000fe42,
};

static void sun4i_frontend_scaler_init(struct sun4i_frontend *frontend)
{
	int i;

	for (i = 0; i < 32; i++) {
		regmap_write(frontend->regs, SUN4I_FRONTEND_CH0_HORZCOEF0_REG(i),
			     sun4i_frontend_horz_coef[2 * i]);
		regmap_write(frontend->regs, SUN4I_FRONTEND_CH1_HORZCOEF0_REG(i),
			     sun4i_frontend_horz_coef[2 * i]);
		regmap_write(frontend->regs, SUN4I_FRONTEND_CH0_HORZCOEF1_REG(i),
			     sun4i_frontend_horz_coef[2 * i + 1]);
		regmap_write(frontend->regs, SUN4I_FRONTEND_CH1_HORZCOEF1_REG(i),
			     sun4i_frontend_horz_coef[2 * i + 1]);
		regmap_write(frontend->regs, SUN4I_FRONTEND_CH0_VERTCOEF_REG(i),
			     sun4i_frontend_vert_coef[i]);
		regmap_write(frontend->regs, SUN4I_FRONTEND_CH1_VERTCOEF_REG(i),
			     sun4i_frontend_vert_coef[i]);
	}

	regmap_update_bits(frontend->regs, SUN4I_FRONTEND_FRM_CTRL_REG,
			   SUN4I_FRONTEND_FRM_CTRL_COEF_ACCESS_CTRL,
			   SUN4I_FRONTEND_FRM_CTRL_COEF_ACCESS_CTRL);
}

int sun4i_frontend_init(struct sun4i_frontend *frontend)
{
	return pm_runtime_get_sync(frontend->dev);
}
EXPORT_SYMBOL(sun4i_frontend_init);

void sun4i_frontend_exit(struct sun4i_frontend *frontend)
{
	pm_runtime_put(frontend->dev);
}
EXPORT_SYMBOL(sun4i_frontend_exit);

void sun4i_frontend_update_buffer(struct sun4i_frontend *frontend,
				  struct drm_plane *plane)
{
	struct drm_plane_state *state = plane->state;
	struct drm_framebuffer *fb = state->fb;
	dma_addr_t paddr;

	/* Set the line width */
	DRM_DEBUG_DRIVER("Frontend stride: %d bytes\n", fb->pitches[0]);
	regmap_write(frontend->regs, SUN4I_FRONTEND_LINESTRD0_REG,
		     fb->pitches[0]);

	/* Set the physical address of the buffer in memory */
	paddr = drm_fb_cma_get_gem_addr(fb, state, 0);
	paddr -= PHYS_OFFSET;
	DRM_DEBUG_DRIVER("Setting buffer address to %pad\n", &paddr);
	regmap_write(frontend->regs, SUN4I_FRONTEND_BUF_ADDR0_REG, paddr);
}
EXPORT_SYMBOL(sun4i_frontend_update_buffer);

static int sun4i_frontend_drm_format_to_input_fmt(uint32_t fmt, u32 *val)
{
	switch (fmt) {
	case DRM_FORMAT_ARGB8888:
		*val = 5;
		return 0;

	default:
		return -EINVAL;
	}
}

static int sun4i_frontend_drm_format_to_output_fmt(uint32_t fmt, u32 *val)
{
	switch (fmt) {
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
		*val = 2;
		return 0;

	default:
		return -EINVAL;
	}
}

int sun4i_frontend_update_formats(struct sun4i_frontend *frontend,
				  struct drm_plane *plane, uint32_t out_fmt)
{
	struct drm_plane_state *state = plane->state;
	struct drm_framebuffer *fb = state->fb;
	u32 out_fmt_val;
	u32 in_fmt_val;
	int ret;

	ret = sun4i_frontend_drm_format_to_input_fmt(fb->format->format,
						     &in_fmt_val);
	if (ret) {
		DRM_DEBUG_DRIVER("Invalid input format\n");
		return ret;
	}

	ret = sun4i_frontend_drm_format_to_output_fmt(out_fmt, &out_fmt_val);
	if (ret) {
		DRM_DEBUG_DRIVER("Invalid output format\n");
		return ret;
	}

	/*
	 * I have no idea what this does exactly, but it seems to be
	 * related to the scaler FIR filter phase parameters.
	 */
	regmap_write(frontend->regs, SUN4I_FRONTEND_CH0_HORZPHASE_REG, 0x400);
	regmap_write(frontend->regs, SUN4I_FRONTEND_CH1_HORZPHASE_REG, 0x400);
	regmap_write(frontend->regs, SUN4I_FRONTEND_CH0_VERTPHASE0_REG, 0x400);
	regmap_write(frontend->regs, SUN4I_FRONTEND_CH1_VERTPHASE0_REG, 0x400);
	regmap_write(frontend->regs, SUN4I_FRONTEND_CH0_VERTPHASE1_REG, 0x400);
	regmap_write(frontend->regs, SUN4I_FRONTEND_CH1_VERTPHASE1_REG, 0x400);

	regmap_write(frontend->regs, SUN4I_FRONTEND_INPUT_FMT_REG,
		     SUN4I_FRONTEND_INPUT_FMT_DATA_MOD(1) |
		     SUN4I_FRONTEND_INPUT_FMT_DATA_FMT(in_fmt_val) |
		     SUN4I_FRONTEND_INPUT_FMT_PS(1));

	/*
	 * TODO: It look like the A31 and A80 at least will need the
	 * bit 7 (ALPHA_EN) enabled when using a format with alpha (so
	 * ARGB8888).
	 */
	regmap_write(frontend->regs, SUN4I_FRONTEND_OUTPUT_FMT_REG,
		     SUN4I_FRONTEND_OUTPUT_FMT_DATA_FMT(out_fmt_val));

	return 0;
}
EXPORT_SYMBOL(sun4i_frontend_update_formats);

void sun4i_frontend_update_coord(struct sun4i_frontend *frontend,
				 struct drm_plane *plane)
{
	struct drm_plane_state *state = plane->state;

	/* Set height and width */
	DRM_DEBUG_DRIVER("Frontend size W: %u H: %u\n",
			 state->crtc_w, state->crtc_h);
	regmap_write(frontend->regs, SUN4I_FRONTEND_CH0_INSIZE_REG,
		     SUN4I_FRONTEND_INSIZE(state->src_h >> 16,
					   state->src_w >> 16));
	regmap_write(frontend->regs, SUN4I_FRONTEND_CH1_INSIZE_REG,
		     SUN4I_FRONTEND_INSIZE(state->src_h >> 16,
					   state->src_w >> 16));

	regmap_write(frontend->regs, SUN4I_FRONTEND_CH0_OUTSIZE_REG,
		     SUN4I_FRONTEND_OUTSIZE(state->crtc_h, state->crtc_w));
	regmap_write(frontend->regs, SUN4I_FRONTEND_CH1_OUTSIZE_REG,
		     SUN4I_FRONTEND_OUTSIZE(state->crtc_h, state->crtc_w));

	regmap_write(frontend->regs, SUN4I_FRONTEND_CH0_HORZFACT_REG,
		     state->src_w / state->crtc_w);
	regmap_write(frontend->regs, SUN4I_FRONTEND_CH1_HORZFACT_REG,
		     state->src_w / state->crtc_w);

	regmap_write(frontend->regs, SUN4I_FRONTEND_CH0_VERTFACT_REG,
		     state->src_h / state->crtc_h);
	regmap_write(frontend->regs, SUN4I_FRONTEND_CH1_VERTFACT_REG,
		     state->src_h / state->crtc_h);

	regmap_write_bits(frontend->regs, SUN4I_FRONTEND_FRM_CTRL_REG,
			  SUN4I_FRONTEND_FRM_CTRL_REG_RDY,
			  SUN4I_FRONTEND_FRM_CTRL_REG_RDY);
}
EXPORT_SYMBOL(sun4i_frontend_update_coord);

int sun4i_frontend_enable(struct sun4i_frontend *frontend)
{
	regmap_write_bits(frontend->regs, SUN4I_FRONTEND_FRM_CTRL_REG,
			  SUN4I_FRONTEND_FRM_CTRL_FRM_START,
			  SUN4I_FRONTEND_FRM_CTRL_FRM_START);

	return 0;
}
EXPORT_SYMBOL(sun4i_frontend_enable);

static struct regmap_config sun4i_frontend_regmap_config = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.max_register	= 0x0a14,
};

static int sun4i_frontend_bind(struct device *dev, struct device *master,
			 void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sun4i_frontend *frontend;
	struct drm_device *drm = data;
	struct sun4i_drv *drv = drm->dev_private;
	struct resource *res;
	void __iomem *regs;

	frontend = devm_kzalloc(dev, sizeof(*frontend), GFP_KERNEL);
	if (!frontend)
		return -ENOMEM;

	dev_set_drvdata(dev, frontend);
	frontend->dev = dev;
	frontend->node = dev->of_node;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	frontend->regs = devm_regmap_init_mmio(dev, regs,
					       &sun4i_frontend_regmap_config);
	if (IS_ERR(frontend->regs)) {
		dev_err(dev, "Couldn't create the frontend regmap\n");
		return PTR_ERR(frontend->regs);
	}

	frontend->reset = devm_reset_control_get(dev, NULL);
	if (IS_ERR(frontend->reset)) {
		dev_err(dev, "Couldn't get our reset line\n");
		return PTR_ERR(frontend->reset);
	}

	frontend->bus_clk = devm_clk_get(dev, "ahb");
	if (IS_ERR(frontend->bus_clk)) {
		dev_err(dev, "Couldn't get our bus clock\n");
		return PTR_ERR(frontend->bus_clk);
	}

	frontend->mod_clk = devm_clk_get(dev, "mod");
	if (IS_ERR(frontend->mod_clk)) {
		dev_err(dev, "Couldn't get our mod clock\n");
		return PTR_ERR(frontend->mod_clk);
	}

	frontend->ram_clk = devm_clk_get(dev, "ram");
	if (IS_ERR(frontend->ram_clk)) {
		dev_err(dev, "Couldn't get our ram clock\n");
		return PTR_ERR(frontend->ram_clk);
	}

	list_add_tail(&frontend->list, &drv->frontend_list);
	pm_runtime_enable(dev);

	return 0;
}

static void sun4i_frontend_unbind(struct device *dev, struct device *master,
			    void *data)
{
	struct sun4i_frontend *frontend = dev_get_drvdata(dev);

	list_del(&frontend->list);
	pm_runtime_force_suspend(dev);
}

static const struct component_ops sun4i_frontend_ops = {
	.bind	= sun4i_frontend_bind,
	.unbind	= sun4i_frontend_unbind,
};

static int sun4i_frontend_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &sun4i_frontend_ops);
}

static int sun4i_frontend_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &sun4i_frontend_ops);

	return 0;
}

static int sun4i_frontend_runtime_resume(struct device *dev)
{
	struct sun4i_frontend *frontend = dev_get_drvdata(dev);
	int ret;

	clk_set_rate(frontend->mod_clk, 300000000);

	clk_prepare_enable(frontend->bus_clk);
	clk_prepare_enable(frontend->mod_clk);
	clk_prepare_enable(frontend->ram_clk);

	ret = reset_control_reset(frontend->reset);
	if (ret) {
		dev_err(dev, "Couldn't reset our device\n");
		return ret;
	}

	regmap_update_bits(frontend->regs, SUN4I_FRONTEND_EN_REG,
			   SUN4I_FRONTEND_EN_EN,
			   SUN4I_FRONTEND_EN_EN);

	regmap_update_bits(frontend->regs, SUN4I_FRONTEND_BYPASS_REG,
			   SUN4I_FRONTEND_BYPASS_CSC_EN,
			   SUN4I_FRONTEND_BYPASS_CSC_EN);

	sun4i_frontend_scaler_init(frontend);

	return 0;
}

static int sun4i_frontend_runtime_suspend(struct device *dev)
{
	struct sun4i_frontend *frontend = dev_get_drvdata(dev);

	clk_disable_unprepare(frontend->ram_clk);
	clk_disable_unprepare(frontend->mod_clk);
	clk_disable_unprepare(frontend->bus_clk);

	reset_control_assert(frontend->reset);

	return 0;
}

static const struct dev_pm_ops sun4i_frontend_pm_ops = {
	.runtime_resume		= sun4i_frontend_runtime_resume,
	.runtime_suspend	= sun4i_frontend_runtime_suspend,
};

const struct of_device_id sun4i_frontend_of_table[] = {
	{ .compatible = "allwinner,sun8i-a33-display-frontend" },
	{ }
};
EXPORT_SYMBOL(sun4i_frontend_of_table);
MODULE_DEVICE_TABLE(of, sun4i_frontend_of_table);

static struct platform_driver sun4i_frontend_driver = {
	.probe		= sun4i_frontend_probe,
	.remove		= sun4i_frontend_remove,
	.driver		= {
		.name		= "sun4i-frontend",
		.of_match_table	= sun4i_frontend_of_table,
		.pm		= &sun4i_frontend_pm_ops,
	},
};
module_platform_driver(sun4i_frontend_driver);

MODULE_AUTHOR("Maxime Ripard <maxime.ripard@free-electrons.com>");
MODULE_DESCRIPTION("Allwinner A10 Display Engine Frontend Driver");
MODULE_LICENSE("GPL");
