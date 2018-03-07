/*
 * Copyright (C) 2012 Texas Instruments
 * Author: Rob Clark <robdclark@gmail.com>
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

#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/consumer.h>
#include <drm/drm_atomic_helper.h>

#include "tilcdc_drv.h"
#include "tilcdc_tfp410.h"

struct tfp410_module {
	struct tilcdc_module base;
	struct i2c_adapter *i2c;
	int gpio;
};
#define to_tfp410_module(x) container_of(x, struct tfp410_module, base)


static const struct tilcdc_panel_info dvi_info = {
		.ac_bias                = 255,
		.ac_bias_intrpt         = 0,
		.dma_burst_sz           = 16,
		.bpp                    = 16,
		.fdd                    = 0x80,
		.tft_alt_mode           = 0,
		.sync_edge              = 0,
		.sync_ctrl              = 1,
		.raster_order           = 0,
};

/*
 * Encoder:
 */

struct tfp410_encoder {
	struct drm_encoder base;
	struct tfp410_module *mod;
	int dpms;
};
#define to_tfp410_encoder(x) container_of(x, struct tfp410_encoder, base)

static void tfp410_encoder_dpms(struct drm_encoder *encoder, int mode)
{
	struct tfp410_encoder *tfp410_encoder = to_tfp410_encoder(encoder);

	if (tfp410_encoder->dpms == mode)
		return;

	if (mode == DRM_MODE_DPMS_ON) {
		DBG("Power on");
		gpio_direction_output(tfp410_encoder->mod->gpio, 1);
	} else {
		DBG("Power off");
		gpio_direction_output(tfp410_encoder->mod->gpio, 0);
	}

	tfp410_encoder->dpms = mode;
}

static void tfp410_encoder_prepare(struct drm_encoder *encoder)
{
	tfp410_encoder_dpms(encoder, DRM_MODE_DPMS_OFF);
}

static void tfp410_encoder_commit(struct drm_encoder *encoder)
{
	tfp410_encoder_dpms(encoder, DRM_MODE_DPMS_ON);
}

static void tfp410_encoder_mode_set(struct drm_encoder *encoder,
		struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode)
{
	/* nothing needed */
}

static const struct drm_encoder_funcs tfp410_encoder_funcs = {
		.destroy        = drm_encoder_cleanup,
};

static const struct drm_encoder_helper_funcs tfp410_encoder_helper_funcs = {
		.dpms           = tfp410_encoder_dpms,
		.prepare        = tfp410_encoder_prepare,
		.commit         = tfp410_encoder_commit,
		.mode_set       = tfp410_encoder_mode_set,
};

static struct drm_encoder *tfp410_encoder_create(struct drm_device *dev,
		struct tfp410_module *mod)
{
	struct tfp410_encoder *tfp410_encoder;
	struct drm_encoder *encoder;
	int ret;

	tfp410_encoder = devm_kzalloc(dev->dev, sizeof(*tfp410_encoder),
				      GFP_KERNEL);
	if (!tfp410_encoder) {
		dev_err(dev->dev, "allocation failed\n");
		return NULL;
	}

	tfp410_encoder->dpms = DRM_MODE_DPMS_OFF;
	tfp410_encoder->mod = mod;

	encoder = &tfp410_encoder->base;
	encoder->possible_crtcs = 1;

	ret = drm_encoder_init(dev, encoder, &tfp410_encoder_funcs,
			DRM_MODE_ENCODER_TMDS, NULL);
	if (ret < 0)
		goto fail;

	drm_encoder_helper_add(encoder, &tfp410_encoder_helper_funcs);

	return encoder;

fail:
	drm_encoder_cleanup(encoder);
	return NULL;
}

/*
 * Connector:
 */

struct tfp410_connector {
	struct drm_connector base;

	struct drm_encoder *encoder;  /* our connected encoder */
	struct tfp410_module *mod;
};
#define to_tfp410_connector(x) container_of(x, struct tfp410_connector, base)


static void tfp410_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static enum drm_connector_status tfp410_connector_detect(
		struct drm_connector *connector,
		bool force)
{
	struct tfp410_connector *tfp410_connector = to_tfp410_connector(connector);

	if (drm_probe_ddc(tfp410_connector->mod->i2c))
		return connector_status_connected;

	return connector_status_unknown;
}

static int tfp410_connector_get_modes(struct drm_connector *connector)
{
	struct tfp410_connector *tfp410_connector = to_tfp410_connector(connector);
	struct edid *edid;
	int ret = 0;

	edid = drm_get_edid(connector, tfp410_connector->mod->i2c);

	drm_mode_connector_update_edid_property(connector, edid);

	if (edid) {
		ret = drm_add_edid_modes(connector, edid);
		kfree(edid);
	}

	return ret;
}

static int tfp410_connector_mode_valid(struct drm_connector *connector,
		  struct drm_display_mode *mode)
{
	struct tilcdc_drm_private *priv = connector->dev->dev_private;
	/* our only constraints are what the crtc can generate: */
	return tilcdc_crtc_mode_valid(priv->crtc, mode);
}

static struct drm_encoder *tfp410_connector_best_encoder(
		struct drm_connector *connector)
{
	struct tfp410_connector *tfp410_connector = to_tfp410_connector(connector);
	return tfp410_connector->encoder;
}

static const struct drm_connector_funcs tfp410_connector_funcs = {
	.destroy            = tfp410_connector_destroy,
	.detect             = tfp410_connector_detect,
	.fill_modes         = drm_helper_probe_single_connector_modes,
	.reset              = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static const struct drm_connector_helper_funcs tfp410_connector_helper_funcs = {
	.get_modes          = tfp410_connector_get_modes,
	.mode_valid         = tfp410_connector_mode_valid,
	.best_encoder       = tfp410_connector_best_encoder,
};

static struct drm_connector *tfp410_connector_create(struct drm_device *dev,
		struct tfp410_module *mod, struct drm_encoder *encoder)
{
	struct tfp410_connector *tfp410_connector;
	struct drm_connector *connector;
	int ret;

	tfp410_connector = devm_kzalloc(dev->dev, sizeof(*tfp410_connector),
					GFP_KERNEL);
	if (!tfp410_connector) {
		dev_err(dev->dev, "allocation failed\n");
		return NULL;
	}

	tfp410_connector->encoder = encoder;
	tfp410_connector->mod = mod;

	connector = &tfp410_connector->base;

	drm_connector_init(dev, connector, &tfp410_connector_funcs,
			DRM_MODE_CONNECTOR_DVID);
	drm_connector_helper_add(connector, &tfp410_connector_helper_funcs);

	connector->polled = DRM_CONNECTOR_POLL_CONNECT |
			DRM_CONNECTOR_POLL_DISCONNECT;

	connector->interlace_allowed = 0;
	connector->doublescan_allowed = 0;

	ret = drm_mode_connector_attach_encoder(connector, encoder);
	if (ret)
		goto fail;

	return connector;

fail:
	tfp410_connector_destroy(connector);
	return NULL;
}

/*
 * Module:
 */

static int tfp410_modeset_init(struct tilcdc_module *mod, struct drm_device *dev)
{
	struct tfp410_module *tfp410_mod = to_tfp410_module(mod);
	struct tilcdc_drm_private *priv = dev->dev_private;
	struct drm_encoder *encoder;
	struct drm_connector *connector;

	encoder = tfp410_encoder_create(dev, tfp410_mod);
	if (!encoder)
		return -ENOMEM;

	connector = tfp410_connector_create(dev, tfp410_mod, encoder);
	if (!connector)
		return -ENOMEM;

	priv->encoders[priv->num_encoders++] = encoder;
	priv->connectors[priv->num_connectors++] = connector;

	tilcdc_crtc_set_panel_info(priv->crtc, &dvi_info);
	return 0;
}

static const struct tilcdc_module_ops tfp410_module_ops = {
		.modeset_init = tfp410_modeset_init,
};

/*
 * Device:
 */

static int tfp410_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct device_node *i2c_node;
	struct tfp410_module *tfp410_mod;
	struct tilcdc_module *mod;
	struct pinctrl *pinctrl;
	uint32_t i2c_phandle;
	int ret = -EINVAL;

	/* bail out early if no DT data: */
	if (!node) {
		dev_err(&pdev->dev, "device-tree data is missing\n");
		return -ENXIO;
	}

	tfp410_mod = devm_kzalloc(&pdev->dev, sizeof(*tfp410_mod), GFP_KERNEL);
	if (!tfp410_mod)
		return -ENOMEM;

	mod = &tfp410_mod->base;
	pdev->dev.platform_data = mod;

	tilcdc_module_init(mod, "tfp410", &tfp410_module_ops);

	pinctrl = devm_pinctrl_get_select_default(&pdev->dev);
	if (IS_ERR(pinctrl))
		dev_warn(&pdev->dev, "pins are not configured\n");

	if (of_property_read_u32(node, "i2c", &i2c_phandle)) {
		dev_err(&pdev->dev, "could not get i2c bus phandle\n");
		goto fail;
	}

	i2c_node = of_find_node_by_phandle(i2c_phandle);
	if (!i2c_node) {
		dev_err(&pdev->dev, "could not get i2c bus node\n");
		goto fail;
	}

	tfp410_mod->i2c = of_find_i2c_adapter_by_node(i2c_node);
	if (!tfp410_mod->i2c) {
		dev_err(&pdev->dev, "could not get i2c\n");
		of_node_put(i2c_node);
		goto fail;
	}

	of_node_put(i2c_node);

	tfp410_mod->gpio = of_get_named_gpio_flags(node, "powerdn-gpio",
			0, NULL);
	if (tfp410_mod->gpio < 0) {
		dev_warn(&pdev->dev, "No power down GPIO\n");
	} else {
		ret = gpio_request(tfp410_mod->gpio, "DVI_PDn");
		if (ret) {
			dev_err(&pdev->dev, "could not get DVI_PDn gpio\n");
			goto fail_adapter;
		}
	}

	return 0;

fail_adapter:
	i2c_put_adapter(tfp410_mod->i2c);

fail:
	tilcdc_module_cleanup(mod);
	return ret;
}

static int tfp410_remove(struct platform_device *pdev)
{
	struct tilcdc_module *mod = dev_get_platdata(&pdev->dev);
	struct tfp410_module *tfp410_mod = to_tfp410_module(mod);

	i2c_put_adapter(tfp410_mod->i2c);
	gpio_free(tfp410_mod->gpio);

	tilcdc_module_cleanup(mod);

	return 0;
}

static const struct of_device_id tfp410_of_match[] = {
		{ .compatible = "ti,tilcdc,tfp410", },
		{ },
};

struct platform_driver tfp410_driver = {
	.probe = tfp410_probe,
	.remove = tfp410_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = "tfp410",
		.of_match_table = tfp410_of_match,
	},
};

int __init tilcdc_tfp410_init(void)
{
	return platform_driver_register(&tfp410_driver);
}

void __exit tilcdc_tfp410_fini(void)
{
	platform_driver_unregister(&tfp410_driver);
}
