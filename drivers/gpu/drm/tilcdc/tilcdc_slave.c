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
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/consumer.h>
#include <drm/drm_encoder_slave.h>

#include "tilcdc_drv.h"

struct slave_module {
	struct tilcdc_module base;
	struct i2c_adapter *i2c;
};
#define to_slave_module(x) container_of(x, struct slave_module, base)

static const struct tilcdc_panel_info slave_info = {
		.bpp                    = 16,
		.ac_bias                = 255,
		.ac_bias_intrpt         = 0,
		.dma_burst_sz           = 16,
		.fdd                    = 0x80,
		.tft_alt_mode           = 0,
		.sync_edge              = 0,
		.sync_ctrl              = 1,
		.raster_order           = 0,
};


/*
 * Encoder:
 */

struct slave_encoder {
	struct drm_encoder_slave base;
	struct slave_module *mod;
};
#define to_slave_encoder(x) container_of(to_encoder_slave(x), struct slave_encoder, base)

static inline struct drm_encoder_slave_funcs *
get_slave_funcs(struct drm_encoder *enc)
{
	return to_encoder_slave(enc)->slave_funcs;
}

static void slave_encoder_destroy(struct drm_encoder *encoder)
{
	struct slave_encoder *slave_encoder = to_slave_encoder(encoder);
	if (get_slave_funcs(encoder))
		get_slave_funcs(encoder)->destroy(encoder);
	drm_encoder_cleanup(encoder);
	kfree(slave_encoder);
}

static void slave_encoder_prepare(struct drm_encoder *encoder)
{
	drm_i2c_encoder_prepare(encoder);
	tilcdc_crtc_set_panel_info(encoder->crtc, &slave_info);
}

static const struct drm_encoder_funcs slave_encoder_funcs = {
		.destroy        = slave_encoder_destroy,
};

static const struct drm_encoder_helper_funcs slave_encoder_helper_funcs = {
		.dpms           = drm_i2c_encoder_dpms,
		.mode_fixup     = drm_i2c_encoder_mode_fixup,
		.prepare        = slave_encoder_prepare,
		.commit         = drm_i2c_encoder_commit,
		.mode_set       = drm_i2c_encoder_mode_set,
		.save           = drm_i2c_encoder_save,
		.restore        = drm_i2c_encoder_restore,
};

static const struct i2c_board_info info = {
		I2C_BOARD_INFO("tda998x", 0x70)
};

static struct drm_encoder *slave_encoder_create(struct drm_device *dev,
		struct slave_module *mod)
{
	struct slave_encoder *slave_encoder;
	struct drm_encoder *encoder;
	int ret;

	slave_encoder = kzalloc(sizeof(*slave_encoder), GFP_KERNEL);
	if (!slave_encoder) {
		dev_err(dev->dev, "allocation failed\n");
		return NULL;
	}

	slave_encoder->mod = mod;

	encoder = &slave_encoder->base.base;
	encoder->possible_crtcs = 1;

	ret = drm_encoder_init(dev, encoder, &slave_encoder_funcs,
			DRM_MODE_ENCODER_TMDS);
	if (ret)
		goto fail;

	drm_encoder_helper_add(encoder, &slave_encoder_helper_funcs);

	ret = drm_i2c_encoder_init(dev, to_encoder_slave(encoder), mod->i2c, &info);
	if (ret)
		goto fail;

	return encoder;

fail:
	slave_encoder_destroy(encoder);
	return NULL;
}

/*
 * Connector:
 */

struct slave_connector {
	struct drm_connector base;

	struct drm_encoder *encoder;  /* our connected encoder */
	struct slave_module *mod;
};
#define to_slave_connector(x) container_of(x, struct slave_connector, base)

static void slave_connector_destroy(struct drm_connector *connector)
{
	struct slave_connector *slave_connector = to_slave_connector(connector);
	drm_connector_cleanup(connector);
	kfree(slave_connector);
}

static enum drm_connector_status slave_connector_detect(
		struct drm_connector *connector,
		bool force)
{
	struct drm_encoder *encoder = to_slave_connector(connector)->encoder;
	return get_slave_funcs(encoder)->detect(encoder, connector);
}

static int slave_connector_get_modes(struct drm_connector *connector)
{
	struct drm_encoder *encoder = to_slave_connector(connector)->encoder;
	return get_slave_funcs(encoder)->get_modes(encoder, connector);
}

static int slave_connector_mode_valid(struct drm_connector *connector,
		  struct drm_display_mode *mode)
{
	struct drm_encoder *encoder = to_slave_connector(connector)->encoder;
	struct tilcdc_drm_private *priv = connector->dev->dev_private;
	int ret;

	ret = tilcdc_crtc_mode_valid(priv->crtc, mode);
	if (ret != MODE_OK)
		return ret;

	return get_slave_funcs(encoder)->mode_valid(encoder, mode);
}

static struct drm_encoder *slave_connector_best_encoder(
		struct drm_connector *connector)
{
	struct slave_connector *slave_connector = to_slave_connector(connector);
	return slave_connector->encoder;
}

static int slave_connector_set_property(struct drm_connector *connector,
		struct drm_property *property, uint64_t value)
{
	struct drm_encoder *encoder = to_slave_connector(connector)->encoder;
	return get_slave_funcs(encoder)->set_property(encoder,
			connector, property, value);
}

static const struct drm_connector_funcs slave_connector_funcs = {
	.destroy            = slave_connector_destroy,
	.dpms               = drm_helper_connector_dpms,
	.detect             = slave_connector_detect,
	.fill_modes         = drm_helper_probe_single_connector_modes,
	.set_property       = slave_connector_set_property,
};

static const struct drm_connector_helper_funcs slave_connector_helper_funcs = {
	.get_modes          = slave_connector_get_modes,
	.mode_valid         = slave_connector_mode_valid,
	.best_encoder       = slave_connector_best_encoder,
};

static struct drm_connector *slave_connector_create(struct drm_device *dev,
		struct slave_module *mod, struct drm_encoder *encoder)
{
	struct slave_connector *slave_connector;
	struct drm_connector *connector;
	int ret;

	slave_connector = kzalloc(sizeof(*slave_connector), GFP_KERNEL);
	if (!slave_connector) {
		dev_err(dev->dev, "allocation failed\n");
		return NULL;
	}

	slave_connector->encoder = encoder;
	slave_connector->mod = mod;

	connector = &slave_connector->base;

	drm_connector_init(dev, connector, &slave_connector_funcs,
			DRM_MODE_CONNECTOR_HDMIA);
	drm_connector_helper_add(connector, &slave_connector_helper_funcs);

	connector->polled = DRM_CONNECTOR_POLL_CONNECT |
			DRM_CONNECTOR_POLL_DISCONNECT;

	connector->interlace_allowed = 0;
	connector->doublescan_allowed = 0;

	get_slave_funcs(encoder)->create_resources(encoder, connector);

	ret = drm_mode_connector_attach_encoder(connector, encoder);
	if (ret)
		goto fail;

	drm_sysfs_connector_add(connector);

	return connector;

fail:
	slave_connector_destroy(connector);
	return NULL;
}

/*
 * Module:
 */

static int slave_modeset_init(struct tilcdc_module *mod, struct drm_device *dev)
{
	struct slave_module *slave_mod = to_slave_module(mod);
	struct tilcdc_drm_private *priv = dev->dev_private;
	struct drm_encoder *encoder;
	struct drm_connector *connector;

	encoder = slave_encoder_create(dev, slave_mod);
	if (!encoder)
		return -ENOMEM;

	connector = slave_connector_create(dev, slave_mod, encoder);
	if (!connector)
		return -ENOMEM;

	priv->encoders[priv->num_encoders++] = encoder;
	priv->connectors[priv->num_connectors++] = connector;

	return 0;
}

static void slave_destroy(struct tilcdc_module *mod)
{
	struct slave_module *slave_mod = to_slave_module(mod);

	tilcdc_module_cleanup(mod);
	kfree(slave_mod);
}

static const struct tilcdc_module_ops slave_module_ops = {
		.modeset_init = slave_modeset_init,
		.destroy = slave_destroy,
};

/*
 * Device:
 */

static struct of_device_id slave_of_match[];

static int slave_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct device_node *i2c_node;
	struct slave_module *slave_mod;
	struct tilcdc_module *mod;
	struct pinctrl *pinctrl;
	uint32_t i2c_phandle;
	int ret = -EINVAL;

	/* bail out early if no DT data: */
	if (!node) {
		dev_err(&pdev->dev, "device-tree data is missing\n");
		return -ENXIO;
	}

	slave_mod = kzalloc(sizeof(*slave_mod), GFP_KERNEL);
	if (!slave_mod)
		return -ENOMEM;

	mod = &slave_mod->base;

	tilcdc_module_init(mod, "slave", &slave_module_ops);

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

	slave_mod->i2c = of_find_i2c_adapter_by_node(i2c_node);
	if (!slave_mod->i2c) {
		dev_err(&pdev->dev, "could not get i2c\n");
		goto fail;
	}

	of_node_put(i2c_node);

	return 0;

fail:
	slave_destroy(mod);
	return ret;
}

static int slave_remove(struct platform_device *pdev)
{
	return 0;
}

static struct of_device_id slave_of_match[] = {
		{ .compatible = "ti,tilcdc,slave", },
		{ },
};

struct platform_driver slave_driver = {
	.probe = slave_probe,
	.remove = slave_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = "slave",
		.of_match_table = slave_of_match,
	},
};

int __init tilcdc_slave_init(void)
{
	return platform_driver_register(&slave_driver);
}

void __exit tilcdc_slave_fini(void)
{
	platform_driver_unregister(&slave_driver);
}
