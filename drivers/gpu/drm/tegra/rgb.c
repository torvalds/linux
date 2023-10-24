// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Avionic Design GmbH
 * Copyright (C) 2012 NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/clk.h>
#include <linux/of.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_simple_kms_helper.h>

#include "drm.h"
#include "dc.h"

struct tegra_rgb {
	struct tegra_output output;
	struct tegra_dc *dc;

	struct clk *pll_d_out0;
	struct clk *pll_d2_out0;
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

static void tegra_rgb_encoder_disable(struct drm_encoder *encoder)
{
	struct tegra_output *output = encoder_to_output(encoder);
	struct tegra_rgb *rgb = to_rgb(output);

	tegra_dc_write_regs(rgb->dc, rgb_disable, ARRAY_SIZE(rgb_disable));
	tegra_dc_commit(rgb->dc);
}

static void tegra_rgb_encoder_enable(struct drm_encoder *encoder)
{
	struct drm_display_mode *mode = &encoder->crtc->state->adjusted_mode;
	struct tegra_output *output = encoder_to_output(encoder);
	struct tegra_rgb *rgb = to_rgb(output);
	u32 value;

	tegra_dc_write_regs(rgb->dc, rgb_enable, ARRAY_SIZE(rgb_enable));

	value = DE_SELECT_ACTIVE | DE_CONTROL_NORMAL;
	tegra_dc_writel(rgb->dc, value, DC_DISP_DATA_ENABLE_OPTIONS);

	/* configure H- and V-sync signal polarities */
	value = tegra_dc_readl(rgb->dc, DC_COM_PIN_OUTPUT_POLARITY(1));

	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		value |= LHS_OUTPUT_POLARITY_LOW;
	else
		value &= ~LHS_OUTPUT_POLARITY_LOW;

	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		value |= LVS_OUTPUT_POLARITY_LOW;
	else
		value &= ~LVS_OUTPUT_POLARITY_LOW;

	tegra_dc_writel(rgb->dc, value, DC_COM_PIN_OUTPUT_POLARITY(1));

	/* XXX: parameterize? */
	value = DISP_DATA_FORMAT_DF1P1C | DISP_ALIGNMENT_MSB |
		DISP_ORDER_RED_BLUE;
	tegra_dc_writel(rgb->dc, value, DC_DISP_DISP_INTERFACE_CONTROL);

	tegra_dc_commit(rgb->dc);
}

static bool tegra_rgb_pll_rate_change_allowed(struct tegra_rgb *rgb)
{
	if (!rgb->pll_d2_out0)
		return false;

	if (!clk_is_match(rgb->clk_parent, rgb->pll_d_out0) &&
	    !clk_is_match(rgb->clk_parent, rgb->pll_d2_out0))
		return false;

	return true;
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
	if (tegra_rgb_pll_rate_change_allowed(rgb)) {
		/*
		 * Set display controller clock to x2 of PCLK in order to
		 * produce higher resolution pulse positions.
		 */
		div = 2;
		pclk *= 2;
	} else {
		div = ((clk_get_rate(rgb->clk) * 2) / pclk) - 2;
		pclk = 0;
	}

	err = tegra_dc_state_setup_clock(dc, crtc_state, rgb->clk_parent,
					 pclk, div);
	if (err < 0) {
		dev_err(output->dev, "failed to setup CRTC state: %d\n", err);
		return err;
	}

	return err;
}

static const struct drm_encoder_helper_funcs tegra_rgb_encoder_helper_funcs = {
	.disable = tegra_rgb_encoder_disable,
	.enable = tegra_rgb_encoder_enable,
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

	rgb->pll_d_out0 = clk_get_sys(NULL, "pll_d_out0");
	if (IS_ERR(rgb->pll_d_out0)) {
		err = PTR_ERR(rgb->pll_d_out0);
		dev_err(dc->dev, "failed to get pll_d_out0: %d\n", err);
		return err;
	}

	if (dc->soc->has_pll_d2_out0) {
		rgb->pll_d2_out0 = clk_get_sys(NULL, "pll_d2_out0");
		if (IS_ERR(rgb->pll_d2_out0)) {
			err = PTR_ERR(rgb->pll_d2_out0);
			dev_err(dc->dev, "failed to get pll_d2_out0: %d\n", err);
			return err;
		}
	}

	dc->rgb = &rgb->output;

	return 0;
}

void tegra_dc_rgb_remove(struct tegra_dc *dc)
{
	struct tegra_rgb *rgb;

	if (!dc->rgb)
		return;

	rgb = to_rgb(dc->rgb);
	clk_put(rgb->pll_d2_out0);
	clk_put(rgb->pll_d_out0);

	tegra_output_remove(dc->rgb);
	dc->rgb = NULL;
}

int tegra_dc_rgb_init(struct drm_device *drm, struct tegra_dc *dc)
{
	struct tegra_output *output = dc->rgb;
	struct drm_connector *connector;
	int err;

	if (!dc->rgb)
		return -ENODEV;

	drm_simple_encoder_init(drm, &output->encoder, DRM_MODE_ENCODER_LVDS);
	drm_encoder_helper_add(&output->encoder,
			       &tegra_rgb_encoder_helper_funcs);

	/*
	 * Wrap directly-connected panel into DRM bridge in order to let
	 * DRM core to handle panel for us.
	 */
	if (output->panel) {
		output->bridge = devm_drm_panel_bridge_add(output->dev,
							   output->panel);
		if (IS_ERR(output->bridge)) {
			dev_err(output->dev,
				"failed to wrap panel into bridge: %pe\n",
				output->bridge);
			return PTR_ERR(output->bridge);
		}

		output->panel = NULL;
	}

	/*
	 * Tegra devices that have LVDS panel utilize LVDS encoder bridge
	 * for converting up to 28 LCD LVTTL lanes into 5/4 LVDS lanes that
	 * go to display panel's receiver.
	 *
	 * Encoder usually have a power-down control which needs to be enabled
	 * in order to transmit data to the panel.  Historically devices that
	 * use an older device-tree version didn't model the bridge, assuming
	 * that encoder is turned ON by default, while today's DRM allows us
	 * to model LVDS encoder properly.
	 *
	 * Newer device-trees utilize LVDS encoder bridge, which provides
	 * us with a connector and handles the display panel.
	 *
	 * For older device-trees we wrapped panel into the panel-bridge.
	 */
	if (output->bridge) {
		err = drm_bridge_attach(&output->encoder, output->bridge,
					NULL, DRM_BRIDGE_ATTACH_NO_CONNECTOR);
		if (err)
			return err;

		connector = drm_bridge_connector_init(drm, &output->encoder);
		if (IS_ERR(connector)) {
			dev_err(output->dev,
				"failed to initialize bridge connector: %pe\n",
				connector);
			return PTR_ERR(connector);
		}

		drm_connector_attach_encoder(connector, &output->encoder);
	}

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
