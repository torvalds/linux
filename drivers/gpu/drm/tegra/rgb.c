/*
 * Copyright (C) 2012 Avionic Design GmbH
 * Copyright (C) 2012 NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_panel.h>

#include "drm.h"
#include "dc.h"

struct tegra_rgb {
	struct tegra_output output;
	struct tegra_dc *dc;
	bool enabled;

	struct clk *clk_parent;
	struct clk *clk;
};

static inline struct tegra_rgb *to_rgb(struct tegra_output *output)
{
	return container_of(output, struct tegra_rgb, output);
}

struct reg_entry {
	unsigned long offset;
	unsigned long value;
};

static const struct reg_entry rgb_enable[] = {
	{ DC_COM_PIN_OUTPUT_ENABLE(0),   0x00000000 },
	{ DC_COM_PIN_OUTPUT_ENABLE(1),   0x00000000 },
	{ DC_COM_PIN_OUTPUT_ENABLE(2),   0x00000000 },
	{ DC_COM_PIN_OUTPUT_ENABLE(3),   0x00000000 },
	{ DC_COM_PIN_OUTPUT_POLARITY(0), 0x00000000 },
	{ DC_COM_PIN_OUTPUT_POLARITY(1), 0x01000000 },
	{ DC_COM_PIN_OUTPUT_POLARITY(2), 0x00000000 },
	{ DC_COM_PIN_OUTPUT_POLARITY(3), 0x00000000 },
	{ DC_COM_PIN_OUTPUT_DATA(0),     0x00000000 },
	{ DC_COM_PIN_OUTPUT_DATA(1),     0x00000000 },
	{ DC_COM_PIN_OUTPUT_DATA(2),     0x00000000 },
	{ DC_COM_PIN_OUTPUT_DATA(3),     0x00000000 },
	{ DC_COM_PIN_OUTPUT_SELECT(0),   0x00000000 },
	{ DC_COM_PIN_OUTPUT_SELECT(1),   0x00000000 },
	{ DC_COM_PIN_OUTPUT_SELECT(2),   0x00000000 },
	{ DC_COM_PIN_OUTPUT_SELECT(3),   0x00000000 },
	{ DC_COM_PIN_OUTPUT_SELECT(4),   0x00210222 },
	{ DC_COM_PIN_OUTPUT_SELECT(5),   0x00002200 },
	{ DC_COM_PIN_OUTPUT_SELECT(6),   0x00020000 },
};

static const struct reg_entry rgb_disable[] = {
	{ DC_COM_PIN_OUTPUT_SELECT(6),   0x00000000 },
	{ DC_COM_PIN_OUTPUT_SELECT(5),   0x00000000 },
	{ DC_COM_PIN_OUTPUT_SELECT(4),   0x00000000 },
	{ DC_COM_PIN_OUTPUT_SELECT(3),   0x00000000 },
	{ DC_COM_PIN_OUTPUT_SELECT(2),   0x00000000 },
	{ DC_COM_PIN_OUTPUT_SELECT(1),   0x00000000 },
	{ DC_COM_PIN_OUTPUT_SELECT(0),   0x00000000 },
	{ DC_COM_PIN_OUTPUT_DATA(3),     0xaaaaaaaa },
	{ DC_COM_PIN_OUTPUT_DATA(2),     0xaaaaaaaa },
	{ DC_COM_PIN_OUTPUT_DATA(1),     0xaaaaaaaa },
	{ DC_COM_PIN_OUTPUT_DATA(0),     0xaaaaaaaa },
	{ DC_COM_PIN_OUTPUT_POLARITY(3), 0x00000000 },
	{ DC_COM_PIN_OUTPUT_POLARITY(2), 0x00000000 },
	{ DC_COM_PIN_OUTPUT_POLARITY(1), 0x00000000 },
	{ DC_COM_PIN_OUTPUT_POLARITY(0), 0x00000000 },
	{ DC_COM_PIN_OUTPUT_ENABLE(3),   0x55555555 },
	{ DC_COM_PIN_OUTPUT_ENABLE(2),   0x55555555 },
	{ DC_COM_PIN_OUTPUT_ENABLE(1),   0x55150005 },
	{ DC_COM_PIN_OUTPUT_ENABLE(0),   0x55555555 },
};

static void tegra_dc_write_regs(struct tegra_dc *dc,
				const struct reg_entry *table,
				unsigned int num)
{
	unsigned int i;

	for (i = 0; i < num; i++)
		tegra_dc_writel(dc, table[i].value, table[i].offset);
}

static void tegra_rgb_connector_dpms(struct drm_connector *connector,
				     int mode)
{
}

static const struct drm_connector_funcs tegra_rgb_connector_funcs = {
	.dpms = tegra_rgb_connector_dpms,
	.reset = drm_atomic_helper_connector_reset,
	.detect = tegra_output_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = tegra_output_connector_destroy,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static enum drm_mode_status
tegra_rgb_connector_mode_valid(struct drm_connector *connector,
			       struct drm_display_mode *mode)
{
	/*
	 * FIXME: For now, always assume that the mode is okay. There are
	 * unresolved issues with clk_round_rate(), which doesn't always
	 * reliably report whether a frequency can be set or not.
	 */
	return MODE_OK;
}

static const struct drm_connector_helper_funcs tegra_rgb_connector_helper_funcs = {
	.get_modes = tegra_output_connector_get_modes,
	.mode_valid = tegra_rgb_connector_mode_valid,
	.best_encoder = tegra_output_connector_best_encoder,
};

static const struct drm_encoder_funcs tegra_rgb_encoder_funcs = {
	.destroy = tegra_output_encoder_destroy,
};

static void tegra_rgb_encoder_dpms(struct drm_encoder *encoder, int mode)
{
}

static void tegra_rgb_encoder_prepare(struct drm_encoder *encoder)
{
}

static void tegra_rgb_encoder_commit(struct drm_encoder *encoder)
{
}

static void tegra_rgb_encoder_mode_set(struct drm_encoder *encoder,
				       struct drm_display_mode *mode,
				       struct drm_display_mode *adjusted)
{
	struct tegra_output *output = encoder_to_output(encoder);
	struct tegra_rgb *rgb = to_rgb(output);
	u32 value;

	if (output->panel)
		drm_panel_prepare(output->panel);

	tegra_dc_write_regs(rgb->dc, rgb_enable, ARRAY_SIZE(rgb_enable));

	value = DE_SELECT_ACTIVE | DE_CONTROL_NORMAL;
	tegra_dc_writel(rgb->dc, value, DC_DISP_DATA_ENABLE_OPTIONS);

	/* XXX: parameterize? */
	value = tegra_dc_readl(rgb->dc, DC_COM_PIN_OUTPUT_POLARITY(1));
	value &= ~LVS_OUTPUT_POLARITY_LOW;
	value &= ~LHS_OUTPUT_POLARITY_LOW;
	tegra_dc_writel(rgb->dc, value, DC_COM_PIN_OUTPUT_POLARITY(1));

	/* XXX: parameterize? */
	value = DISP_DATA_FORMAT_DF1P1C | DISP_ALIGNMENT_MSB |
		DISP_ORDER_RED_BLUE;
	tegra_dc_writel(rgb->dc, value, DC_DISP_DISP_INTERFACE_CONTROL);

	/* XXX: parameterize? */
	value = SC0_H_QUALIFIER_NONE | SC1_H_QUALIFIER_NONE;
	tegra_dc_writel(rgb->dc, value, DC_DISP_SHIFT_CLOCK_OPTIONS);

	tegra_dc_commit(rgb->dc);

	if (output->panel)
		drm_panel_enable(output->panel);
}

static void tegra_rgb_encoder_disable(struct drm_encoder *encoder)
{
	struct tegra_output *output = encoder_to_output(encoder);
	struct tegra_rgb *rgb = to_rgb(output);

	if (output->panel)
		drm_panel_disable(output->panel);

	tegra_dc_write_regs(rgb->dc, rgb_disable, ARRAY_SIZE(rgb_disable));
	tegra_dc_commit(rgb->dc);

	if (output->panel)
		drm_panel_unprepare(output->panel);
}

static int
tegra_rgb_encoder_atomic_check(struct drm_encoder *encoder,
			       struct drm_crtc_state *crtc_state,
			       struct drm_connector_state *conn_state)
{
	struct tegra_output *output = encoder_to_output(encoder);
	struct tegra_dc *dc = to_tegra_dc(conn_state->crtc);
	unsigned long pclk = crtc_state->mode.clock * 1000;
	struct tegra_rgb *rgb = to_rgb(output);
	unsigned int div;
	int err;

	/*
	 * We may not want to change the frequency of the parent clock, since
	 * it may be a parent for other peripherals. This is due to the fact
	 * that on Tegra20 there's only a single clock dedicated to display
	 * (pll_d_out0), whereas later generations have a second one that can
	 * be used to independently drive a second output (pll_d2_out0).
	 *
	 * As a way to support multiple outputs on Tegra20 as well, pll_p is
	 * typically used as the parent clock for the display controllers.
	 * But this comes at a cost: pll_p is the parent of several other
	 * peripherals, so its frequency shouldn't change out of the blue.
	 *
	 * The best we can do at this point is to use the shift clock divider
	 * and hope that the desired frequency can be matched (or at least
	 * matched sufficiently close that the panel will still work).
	 */
	div = ((clk_get_rate(rgb->clk) * 2) / pclk) - 2;
	pclk = 0;

	err = tegra_dc_state_setup_clock(dc, crtc_state, rgb->clk_parent,
					 pclk, div);
	if (err < 0) {
		dev_err(output->dev, "failed to setup CRTC state: %d\n", err);
		return err;
	}

	return err;
}

static const struct drm_encoder_helper_funcs tegra_rgb_encoder_helper_funcs = {
	.dpms = tegra_rgb_encoder_dpms,
	.prepare = tegra_rgb_encoder_prepare,
	.commit = tegra_rgb_encoder_commit,
	.mode_set = tegra_rgb_encoder_mode_set,
	.disable = tegra_rgb_encoder_disable,
	.atomic_check = tegra_rgb_encoder_atomic_check,
};

int tegra_dc_rgb_probe(struct tegra_dc *dc)
{
	struct device_node *np;
	struct tegra_rgb *rgb;
	int err;

	np = of_get_child_by_name(dc->dev->of_node, "rgb");
	if (!np || !of_device_is_available(np))
		return -ENODEV;

	rgb = devm_kzalloc(dc->dev, sizeof(*rgb), GFP_KERNEL);
	if (!rgb)
		return -ENOMEM;

	rgb->output.dev = dc->dev;
	rgb->output.of_node = np;
	rgb->dc = dc;

	err = tegra_output_probe(&rgb->output);
	if (err < 0)
		return err;

	rgb->clk = devm_clk_get(dc->dev, NULL);
	if (IS_ERR(rgb->clk)) {
		dev_err(dc->dev, "failed to get clock\n");
		return PTR_ERR(rgb->clk);
	}

	rgb->clk_parent = devm_clk_get(dc->dev, "parent");
	if (IS_ERR(rgb->clk_parent)) {
		dev_err(dc->dev, "failed to get parent clock\n");
		return PTR_ERR(rgb->clk_parent);
	}

	err = clk_set_parent(rgb->clk, rgb->clk_parent);
	if (err < 0) {
		dev_err(dc->dev, "failed to set parent clock: %d\n", err);
		return err;
	}

	dc->rgb = &rgb->output;

	return 0;
}

int tegra_dc_rgb_remove(struct tegra_dc *dc)
{
	if (!dc->rgb)
		return 0;

	tegra_output_remove(dc->rgb);
	dc->rgb = NULL;

	return 0;
}

int tegra_dc_rgb_init(struct drm_device *drm, struct tegra_dc *dc)
{
	struct tegra_output *output = dc->rgb;
	int err;

	if (!dc->rgb)
		return -ENODEV;

	drm_connector_init(drm, &output->connector, &tegra_rgb_connector_funcs,
			   DRM_MODE_CONNECTOR_LVDS);
	drm_connector_helper_add(&output->connector,
				 &tegra_rgb_connector_helper_funcs);
	output->connector.dpms = DRM_MODE_DPMS_OFF;

	drm_encoder_init(drm, &output->encoder, &tegra_rgb_encoder_funcs,
			 DRM_MODE_ENCODER_LVDS);
	drm_encoder_helper_add(&output->encoder,
			       &tegra_rgb_encoder_helper_funcs);

	drm_mode_connector_attach_encoder(&output->connector,
					  &output->encoder);
	drm_connector_register(&output->connector);

	err = tegra_output_init(drm, output);
	if (err < 0) {
		dev_err(output->dev, "failed to initialize output: %d\n", err);
		return err;
	}

	/*
	 * Other outputs can be attached to either display controller. The RGB
	 * outputs are an exception and work only with their parent display
	 * controller.
	 */
	output->encoder.possible_crtcs = drm_crtc_mask(&dc->base);

	return 0;
}

int tegra_dc_rgb_exit(struct tegra_dc *dc)
{
	if (dc->rgb)
		tegra_output_exit(dc->rgb);

	return 0;
}
