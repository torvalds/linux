// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2019-2022 Bootlin
 * Author: Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/types.h>

#include <drm/clients/drm_client_setup.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fbdev_dma.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_print.h>

#include "logicvc_crtc.h"
#include "logicvc_drm.h"
#include "logicvc_interface.h"
#include "logicvc_mode.h"
#include "logicvc_layer.h"
#include "logicvc_of.h"
#include "logicvc_regs.h"

DEFINE_DRM_GEM_DMA_FOPS(logicvc_drm_fops);

static int logicvc_drm_gem_dma_dumb_create(struct drm_file *file_priv,
					   struct drm_device *drm_dev,
					   struct drm_mode_create_dumb *args)
{
	struct logicvc_drm *logicvc = logicvc_drm(drm_dev);

	/* Stride is always fixed to its configuration value. */
	args->pitch = logicvc->config.row_stride * DIV_ROUND_UP(args->bpp, 8);

	return drm_gem_dma_dumb_create_internal(file_priv, drm_dev, args);
}

static struct drm_driver logicvc_drm_driver = {
	.driver_features		= DRIVER_GEM | DRIVER_MODESET |
					  DRIVER_ATOMIC,

	.fops				= &logicvc_drm_fops,
	.name				= "logicvc-drm",
	.desc				= "Xylon LogiCVC DRM driver",
	.major				= 1,
	.minor				= 0,

	DRM_GEM_DMA_DRIVER_OPS_VMAP_WITH_DUMB_CREATE(logicvc_drm_gem_dma_dumb_create),
	DRM_FBDEV_DMA_DRIVER_OPS,
};

static struct regmap_config logicvc_drm_regmap_config = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.name		= "logicvc-drm",
};

static irqreturn_t logicvc_drm_irq_handler(int irq, void *data)
{
	struct logicvc_drm *logicvc = data;
	irqreturn_t ret = IRQ_NONE;
	u32 stat = 0;

	/* Get pending interrupt sources. */
	regmap_read(logicvc->regmap, LOGICVC_INT_STAT_REG, &stat);

	/* Clear all pending interrupt sources. */
	regmap_write(logicvc->regmap, LOGICVC_INT_STAT_REG, stat);

	if (stat & LOGICVC_INT_STAT_V_SYNC) {
		logicvc_crtc_vblank_handler(logicvc);
		ret = IRQ_HANDLED;
	}

	return ret;
}

static int logicvc_drm_config_parse(struct logicvc_drm *logicvc)
{
	struct drm_device *drm_dev = &logicvc->drm_dev;
	struct device *dev = drm_dev->dev;
	struct device_node *of_node = dev->of_node;
	struct logicvc_drm_config *config = &logicvc->config;
	struct device_node *layers_node;
	int ret;

	logicvc_of_property_parse_bool(of_node, LOGICVC_OF_PROPERTY_DITHERING,
				       &config->dithering);
	logicvc_of_property_parse_bool(of_node,
				       LOGICVC_OF_PROPERTY_BACKGROUND_LAYER,
				       &config->background_layer);
	logicvc_of_property_parse_bool(of_node,
				       LOGICVC_OF_PROPERTY_LAYERS_CONFIGURABLE,
				       &config->layers_configurable);

	ret = logicvc_of_property_parse_u32(of_node,
					    LOGICVC_OF_PROPERTY_DISPLAY_INTERFACE,
					    &config->display_interface);
	if (ret)
		return ret;

	ret = logicvc_of_property_parse_u32(of_node,
					    LOGICVC_OF_PROPERTY_DISPLAY_COLORSPACE,
					    &config->display_colorspace);
	if (ret)
		return ret;

	ret = logicvc_of_property_parse_u32(of_node,
					    LOGICVC_OF_PROPERTY_DISPLAY_DEPTH,
					    &config->display_depth);
	if (ret)
		return ret;

	ret = logicvc_of_property_parse_u32(of_node,
					    LOGICVC_OF_PROPERTY_ROW_STRIDE,
					    &config->row_stride);
	if (ret)
		return ret;

	layers_node = of_get_child_by_name(of_node, "layers");
	if (!layers_node) {
		drm_err(drm_dev, "Missing non-optional layers node\n");
		return -EINVAL;
	}

	config->layers_count = of_get_child_count(layers_node);
	if (!config->layers_count) {
		drm_err(drm_dev,
			"Missing a non-optional layers children node\n");
		return -EINVAL;
	}

	return 0;
}

static int logicvc_clocks_prepare(struct logicvc_drm *logicvc)
{
	struct drm_device *drm_dev = &logicvc->drm_dev;
	struct device *dev = drm_dev->dev;

	struct {
		struct clk **clk;
		char *name;
		bool optional;
	} clocks_map[] = {
		{
			.clk = &logicvc->vclk,
			.name = "vclk",
			.optional = false,
		},
		{
			.clk = &logicvc->vclk2,
			.name = "vclk2",
			.optional = true,
		},
		{
			.clk = &logicvc->lvdsclk,
			.name = "lvdsclk",
			.optional = true,
		},
		{
			.clk = &logicvc->lvdsclkn,
			.name = "lvdsclkn",
			.optional = true,
		},
	};
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(clocks_map); i++) {
		struct clk *clk;

		clk = devm_clk_get(dev, clocks_map[i].name);
		if (IS_ERR(clk)) {
			if (PTR_ERR(clk) == -ENOENT && clocks_map[i].optional)
				continue;

			drm_err(drm_dev, "Missing non-optional clock %s\n",
				clocks_map[i].name);

			ret = PTR_ERR(clk);
			goto error;
		}

		ret = clk_prepare_enable(clk);
		if (ret) {
			drm_err(drm_dev,
				"Failed to prepare and enable clock %s\n",
				clocks_map[i].name);
			goto error;
		}

		*clocks_map[i].clk = clk;
	}

	return 0;

error:
	for (i = 0; i < ARRAY_SIZE(clocks_map); i++) {
		if (!*clocks_map[i].clk)
			continue;

		clk_disable_unprepare(*clocks_map[i].clk);
		*clocks_map[i].clk = NULL;
	}

	return ret;
}

static int logicvc_clocks_unprepare(struct logicvc_drm *logicvc)
{
	struct clk **clocks[] = {
		&logicvc->vclk,
		&logicvc->vclk2,
		&logicvc->lvdsclk,
		&logicvc->lvdsclkn,
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(clocks); i++) {
		if (!*clocks[i])
			continue;

		clk_disable_unprepare(*clocks[i]);
		*clocks[i] = NULL;
	}

	return 0;
}

static const struct logicvc_drm_caps logicvc_drm_caps[] = {
	{
		.major		= 3,
		.layer_address	= false,
	},
	{
		.major		= 4,
		.layer_address	= true,
	},
	{
		.major		= 5,
		.layer_address	= true,
	},
};

static const struct logicvc_drm_caps *
logicvc_drm_caps_match(struct logicvc_drm *logicvc)
{
	struct drm_device *drm_dev = &logicvc->drm_dev;
	const struct logicvc_drm_caps *caps = NULL;
	unsigned int major, minor;
	char level;
	unsigned int i;
	u32 version;

	regmap_read(logicvc->regmap, LOGICVC_IP_VERSION_REG, &version);

	major = FIELD_GET(LOGICVC_IP_VERSION_MAJOR_MASK, version);
	minor = FIELD_GET(LOGICVC_IP_VERSION_MINOR_MASK, version);
	level = FIELD_GET(LOGICVC_IP_VERSION_LEVEL_MASK, version) + 'a';

	for (i = 0; i < ARRAY_SIZE(logicvc_drm_caps); i++) {
		if (logicvc_drm_caps[i].major &&
		    logicvc_drm_caps[i].major != major)
			continue;

		if (logicvc_drm_caps[i].minor &&
		    logicvc_drm_caps[i].minor != minor)
			continue;

		if (logicvc_drm_caps[i].level &&
		    logicvc_drm_caps[i].level != level)
			continue;

		caps = &logicvc_drm_caps[i];
	}

	drm_info(drm_dev, "LogiCVC version %d.%02d.%c\n", major, minor, level);

	return caps;
}

static int logicvc_drm_probe(struct platform_device *pdev)
{
	struct device_node *of_node = pdev->dev.of_node;
	struct device_node *reserved_mem_node;
	struct reserved_mem *reserved_mem = NULL;
	const struct logicvc_drm_caps *caps;
	struct logicvc_drm *logicvc;
	struct device *dev = &pdev->dev;
	struct drm_device *drm_dev;
	struct regmap *regmap = NULL;
	struct resource res;
	void __iomem *base;
	int irq;
	int ret;

	ret = of_reserved_mem_device_init(dev);
	if (ret && ret != -ENODEV) {
		dev_err(dev, "Failed to init memory region\n");
		goto error_early;
	}

	reserved_mem_node = of_parse_phandle(of_node, "memory-region", 0);
	if (reserved_mem_node) {
		reserved_mem = of_reserved_mem_lookup(reserved_mem_node);
		of_node_put(reserved_mem_node);
	}

	/* Get regmap from parent if available. */
	if (of_node->parent)
		regmap = syscon_node_to_regmap(of_node->parent);

	/* Register our own regmap otherwise. */
	if (IS_ERR_OR_NULL(regmap)) {
		ret = of_address_to_resource(of_node, 0, &res);
		if (ret) {
			dev_err(dev, "Failed to get resource from address\n");
			goto error_reserved_mem;
		}

		base = devm_ioremap_resource(dev, &res);
		if (IS_ERR(base)) {
			dev_err(dev, "Failed to map I/O base\n");
			ret = PTR_ERR(base);
			goto error_reserved_mem;
		}

		logicvc_drm_regmap_config.max_register = resource_size(&res) -
							 4;

		regmap = devm_regmap_init_mmio(dev, base,
					       &logicvc_drm_regmap_config);
		if (IS_ERR(regmap)) {
			dev_err(dev, "Failed to create regmap for I/O\n");
			ret = PTR_ERR(regmap);
			goto error_reserved_mem;
		}
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = -ENODEV;
		goto error_reserved_mem;
	}

	logicvc = devm_drm_dev_alloc(dev, &logicvc_drm_driver,
				     struct logicvc_drm, drm_dev);
	if (IS_ERR(logicvc)) {
		ret = PTR_ERR(logicvc);
		goto error_reserved_mem;
	}

	platform_set_drvdata(pdev, logicvc);
	drm_dev = &logicvc->drm_dev;

	logicvc->regmap = regmap;
	INIT_LIST_HEAD(&logicvc->layers_list);

	caps = logicvc_drm_caps_match(logicvc);
	if (!caps) {
		ret = -EINVAL;
		goto error_reserved_mem;
	}

	logicvc->caps = caps;

	if (reserved_mem)
		logicvc->reserved_mem_base = reserved_mem->base;

	ret = logicvc_clocks_prepare(logicvc);
	if (ret) {
		drm_err(drm_dev, "Failed to prepare clocks\n");
		goto error_reserved_mem;
	}

	ret = devm_request_irq(dev, irq, logicvc_drm_irq_handler, 0,
			       dev_name(dev), logicvc);
	if (ret) {
		drm_err(drm_dev, "Failed to request IRQ\n");
		goto error_clocks;
	}

	ret = logicvc_drm_config_parse(logicvc);
	if (ret && ret != -ENODEV) {
		drm_err(drm_dev, "Failed to parse config\n");
		goto error_clocks;
	}

	ret = drmm_mode_config_init(drm_dev);
	if (ret) {
		drm_err(drm_dev, "Failed to init mode config\n");
		goto error_clocks;
	}

	ret = logicvc_layers_init(logicvc);
	if (ret) {
		drm_err(drm_dev, "Failed to initialize layers\n");
		goto error_clocks;
	}

	ret = logicvc_crtc_init(logicvc);
	if (ret) {
		drm_err(drm_dev, "Failed to initialize CRTC\n");
		goto error_clocks;
	}

	logicvc_layers_attach_crtc(logicvc);

	ret = logicvc_interface_init(logicvc);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			drm_err(drm_dev, "Failed to initialize interface\n");

		goto error_clocks;
	}

	logicvc_interface_attach_crtc(logicvc);

	ret = logicvc_mode_init(logicvc);
	if (ret) {
		drm_err(drm_dev, "Failed to initialize KMS\n");
		goto error_clocks;
	}

	ret = drm_dev_register(drm_dev, 0);
	if (ret) {
		drm_err(drm_dev, "Failed to register DRM device\n");
		goto error_mode;
	}

	drm_client_setup(drm_dev, NULL);

	return 0;

error_mode:
	logicvc_mode_fini(logicvc);

error_clocks:
	logicvc_clocks_unprepare(logicvc);

error_reserved_mem:
	of_reserved_mem_device_release(dev);

error_early:
	return ret;
}

static void logicvc_drm_remove(struct platform_device *pdev)
{
	struct logicvc_drm *logicvc = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	struct drm_device *drm_dev = &logicvc->drm_dev;

	drm_dev_unregister(drm_dev);
	drm_atomic_helper_shutdown(drm_dev);

	logicvc_mode_fini(logicvc);

	logicvc_clocks_unprepare(logicvc);

	of_reserved_mem_device_release(dev);
}

static void logicvc_drm_shutdown(struct platform_device *pdev)
{
	struct logicvc_drm *logicvc = platform_get_drvdata(pdev);
	struct drm_device *drm_dev = &logicvc->drm_dev;

	drm_atomic_helper_shutdown(drm_dev);
}

static const struct of_device_id logicvc_drm_of_table[] = {
	{ .compatible = "xylon,logicvc-3.02.a-display" },
	{ .compatible = "xylon,logicvc-4.01.a-display" },
	{},
};
MODULE_DEVICE_TABLE(of, logicvc_drm_of_table);

static struct platform_driver logicvc_drm_platform_driver = {
	.probe		= logicvc_drm_probe,
	.remove		= logicvc_drm_remove,
	.shutdown	= logicvc_drm_shutdown,
	.driver		= {
		.name		= "logicvc-drm",
		.of_match_table	= logicvc_drm_of_table,
	},
};

module_platform_driver(logicvc_drm_platform_driver);

MODULE_AUTHOR("Paul Kocialkowski <paul.kocialkowski@bootlin.com>");
MODULE_DESCRIPTION("Xylon LogiCVC DRM driver");
MODULE_LICENSE("GPL");
