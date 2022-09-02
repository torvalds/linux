// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2019-2022 Bootlin
 * Author: Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 */

#include <drm/drm_print.h>

#include "logicvc_drm.h"
#include "logicvc_layer.h"
#include "logicvc_of.h"

static struct logicvc_of_property_sv logicvc_of_display_interface_sv[] = {
	{ "lvds-4bits",	LOGICVC_DISPLAY_INTERFACE_LVDS_4BITS },
	{ "lvds-3bits",	LOGICVC_DISPLAY_INTERFACE_LVDS_3BITS },
	{ },
};

static struct logicvc_of_property_sv logicvc_of_display_colorspace_sv[] = {
	{ "rgb",	LOGICVC_DISPLAY_COLORSPACE_RGB },
	{ "yuv422",	LOGICVC_DISPLAY_COLORSPACE_YUV422 },
	{ "yuv444",	LOGICVC_DISPLAY_COLORSPACE_YUV444 },
	{ },
};

static struct logicvc_of_property_sv logicvc_of_layer_colorspace_sv[] = {
	{ "rgb",	LOGICVC_LAYER_COLORSPACE_RGB },
	{ "yuv",	LOGICVC_LAYER_COLORSPACE_YUV },
	{ },
};

static struct logicvc_of_property_sv logicvc_of_layer_alpha_mode_sv[] = {
	{ "layer",	LOGICVC_LAYER_ALPHA_LAYER },
	{ "pixel",	LOGICVC_LAYER_ALPHA_PIXEL },
	{ },
};

static struct logicvc_of_property logicvc_of_properties[] = {
	[LOGICVC_OF_PROPERTY_DISPLAY_INTERFACE] = {
		.name		= "xylon,display-interface",
		.sv		= logicvc_of_display_interface_sv,
		.range		= {
			LOGICVC_DISPLAY_INTERFACE_LVDS_4BITS,
			LOGICVC_DISPLAY_INTERFACE_LVDS_3BITS,
		},
	},
	[LOGICVC_OF_PROPERTY_DISPLAY_COLORSPACE] = {
		.name		= "xylon,display-colorspace",
		.sv		= logicvc_of_display_colorspace_sv,
		.range		= {
			LOGICVC_DISPLAY_COLORSPACE_RGB,
			LOGICVC_DISPLAY_COLORSPACE_YUV444,
		},
	},
	[LOGICVC_OF_PROPERTY_DISPLAY_DEPTH] = {
		.name		= "xylon,display-depth",
		.range		= { 8, 24 },
	},
	[LOGICVC_OF_PROPERTY_ROW_STRIDE] = {
		.name		= "xylon,row-stride",
	},
	[LOGICVC_OF_PROPERTY_DITHERING] = {
		.name		= "xylon,dithering",
		.optional	= true,
	},
	[LOGICVC_OF_PROPERTY_BACKGROUND_LAYER] = {
		.name		= "xylon,background-layer",
		.optional	= true,
	},
	[LOGICVC_OF_PROPERTY_LAYERS_CONFIGURABLE] = {
		.name		= "xylon,layers-configurable",
		.optional	= true,
	},
	[LOGICVC_OF_PROPERTY_LAYERS_COUNT] = {
		.name		= "xylon,layers-count",
	},
	[LOGICVC_OF_PROPERTY_LAYER_DEPTH] = {
		.name		= "xylon,layer-depth",
		.range		= { 8, 24 },
	},
	[LOGICVC_OF_PROPERTY_LAYER_COLORSPACE] = {
		.name		= "xylon,layer-colorspace",
		.sv		= logicvc_of_layer_colorspace_sv,
		.range		= {
			LOGICVC_LAYER_COLORSPACE_RGB,
			LOGICVC_LAYER_COLORSPACE_RGB,
		},
	},
	[LOGICVC_OF_PROPERTY_LAYER_ALPHA_MODE] = {
		.name		= "xylon,layer-alpha-mode",
		.sv		= logicvc_of_layer_alpha_mode_sv,
		.range		= {
			LOGICVC_LAYER_ALPHA_LAYER,
			LOGICVC_LAYER_ALPHA_PIXEL,
		},
	},
	[LOGICVC_OF_PROPERTY_LAYER_BASE_OFFSET] = {
		.name		= "xylon,layer-base-offset",
	},
	[LOGICVC_OF_PROPERTY_LAYER_BUFFER_OFFSET] = {
		.name		= "xylon,layer-buffer-offset",
	},
	[LOGICVC_OF_PROPERTY_LAYER_PRIMARY] = {
		.name		= "xylon,layer-primary",
		.optional	= true,
	},
};

static int logicvc_of_property_sv_value(struct logicvc_of_property_sv *sv,
					const char *string, u32 *value)
{
	unsigned int i = 0;

	while (sv[i].string) {
		if (!strcmp(sv[i].string, string)) {
			*value = sv[i].value;
			return 0;
		}

		i++;
	}

	return -EINVAL;
}

int logicvc_of_property_parse_u32(struct device_node *of_node,
				  unsigned int index, u32 *target)
{
	struct logicvc_of_property *property;
	const char *string;
	u32 value;
	int ret;

	if (index >= LOGICVC_OF_PROPERTY_MAXIMUM)
		return -EINVAL;

	property = &logicvc_of_properties[index];

	if (!property->optional &&
	    !of_property_read_bool(of_node, property->name))
		return -ENODEV;

	if (property->sv) {
		ret = of_property_read_string(of_node, property->name, &string);
		if (ret)
			return ret;

		ret = logicvc_of_property_sv_value(property->sv, string,
						   &value);
		if (ret)
			return ret;
	} else {
		ret = of_property_read_u32(of_node, property->name, &value);
		if (ret)
			return ret;
	}

	if (property->range[0] || property->range[1])
		if (value < property->range[0] || value > property->range[1])
			return -ERANGE;

	*target = value;

	return 0;
}

void logicvc_of_property_parse_bool(struct device_node *of_node,
				    unsigned int index, bool *target)
{
	struct logicvc_of_property *property;

	if (index >= LOGICVC_OF_PROPERTY_MAXIMUM) {
		/* Fallback. */
		*target = false;
		return;
	}

	property = &logicvc_of_properties[index];
	*target = of_property_read_bool(of_node, property->name);
}

bool logicvc_of_node_is_layer(struct device_node *of_node)
{
	return !of_node_cmp(of_node->name, "layer");
}
