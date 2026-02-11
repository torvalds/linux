// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014 Traphandler
 * Copyright (C) 2014 Free Electrons
 * Copyright (C) 2014 Atmel
 *
 * Author: Jean-Jacques Hiblot <jjhiblot@traphandler.com>
 * Author: Boris BREZILLON <boris.brezillon@free-electrons.com>
 */

#include <linux/media-bus-format.h>
#include <linux/of.h>
#include <linux/of_graph.h>

#include <drm/drm_bridge.h>
#include <drm/drm_encoder.h>
#include <drm/drm_of.h>
#include <drm/drm_print.h>
#include <drm/drm_simple_kms_helper.h>

#include "atmel_hlcdc_dc.h"

struct atmel_hlcdc_rgb_output {
	struct drm_encoder encoder;
	int bus_fmt;
};

static struct atmel_hlcdc_rgb_output *
atmel_hlcdc_encoder_to_rgb_output(struct drm_encoder *encoder)
{
	return container_of(encoder, struct atmel_hlcdc_rgb_output, encoder);
}

int atmel_hlcdc_encoder_get_bus_fmt(struct drm_encoder *encoder)
{
	struct atmel_hlcdc_rgb_output *output;

	output = atmel_hlcdc_encoder_to_rgb_output(encoder);

	return output->bus_fmt;
}

static int atmel_hlcdc_of_bus_fmt(const struct device_node *ep)
{
	u32 bus_width;
	int ret;

	ret = of_property_read_u32(ep, "bus-width", &bus_width);
	if (ret == -EINVAL)
		return 0;
	if (ret)
		return ret;

	switch (bus_width) {
	case 12:
		return MEDIA_BUS_FMT_RGB444_1X12;
	case 16:
		return MEDIA_BUS_FMT_RGB565_1X16;
	case 18:
		return MEDIA_BUS_FMT_RGB666_1X18;
	case 24:
		return MEDIA_BUS_FMT_RGB888_1X24;
	default:
		return -EINVAL;
	}
}

static int atmel_hlcdc_attach_endpoint(struct drm_device *dev, int endpoint)
{
	struct atmel_hlcdc_rgb_output *output;
	struct device_node *ep;
	struct drm_bridge *bridge;
	struct atmel_hlcdc_dc *dc = dev->dev_private;
	struct drm_crtc *crtc = dc->crtc;
	int ret = 0;

	bridge = devm_drm_of_get_bridge(dev->dev, dev->dev->of_node, 0, endpoint);
	if (IS_ERR(bridge))
		return PTR_ERR(bridge);

	output = drmm_simple_encoder_alloc(dev, struct atmel_hlcdc_rgb_output,
					   encoder, DRM_MODE_ENCODER_NONE);
	if (IS_ERR(output))
		return PTR_ERR(output);

	ep = of_graph_get_endpoint_by_regs(dev->dev->of_node, 0, endpoint);
	if (!ep)
		return -ENODEV;

	output->bus_fmt = atmel_hlcdc_of_bus_fmt(ep);
	of_node_put(ep);
	if (output->bus_fmt < 0) {
		drm_err(dev, "endpoint %d: invalid bus width\n", endpoint);
		return -EINVAL;
	}


	output->encoder.possible_crtcs = drm_crtc_mask(crtc);

	if (bridge)
		ret = drm_bridge_attach(&output->encoder, bridge, NULL, 0);

	return ret;
}

int atmel_hlcdc_create_outputs(struct drm_device *dev)
{
	int endpoint, ret = 0;
	int attached = 0;

	/*
	 * Always scan the first few endpoints even if we get -ENODEV,
	 * but keep going after that as long as we keep getting hits.
	 */
	for (endpoint = 0; !ret || endpoint < 4; endpoint++) {
		ret = atmel_hlcdc_attach_endpoint(dev, endpoint);
		if (ret == -ENODEV)
			continue;
		if (ret)
			break;
		attached++;
	}

	/* At least one device was successfully attached.*/
	if (ret == -ENODEV && attached)
		return 0;

	return ret;
}
