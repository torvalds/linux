/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2019-2022 Bootlin
 * Author: Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 */

#ifndef _LOGICVC_OF_H_
#define _LOGICVC_OF_H_

enum logicvc_of_property_index {
	LOGICVC_OF_PROPERTY_DISPLAY_INTERFACE = 0,
	LOGICVC_OF_PROPERTY_DISPLAY_COLORSPACE,
	LOGICVC_OF_PROPERTY_DISPLAY_DEPTH,
	LOGICVC_OF_PROPERTY_ROW_STRIDE,
	LOGICVC_OF_PROPERTY_DITHERING,
	LOGICVC_OF_PROPERTY_BACKGROUND_LAYER,
	LOGICVC_OF_PROPERTY_LAYERS_CONFIGURABLE,
	LOGICVC_OF_PROPERTY_LAYERS_COUNT,
	LOGICVC_OF_PROPERTY_LAYER_DEPTH,
	LOGICVC_OF_PROPERTY_LAYER_COLORSPACE,
	LOGICVC_OF_PROPERTY_LAYER_ALPHA_MODE,
	LOGICVC_OF_PROPERTY_LAYER_BASE_OFFSET,
	LOGICVC_OF_PROPERTY_LAYER_BUFFER_OFFSET,
	LOGICVC_OF_PROPERTY_LAYER_PRIMARY,
	LOGICVC_OF_PROPERTY_MAXIMUM,
};

struct logicvc_of_property_sv {
	const char *string;
	u32 value;
};

struct logicvc_of_property {
	char *name;
	bool optional;
	struct logicvc_of_property_sv *sv;
	u32 range[2];
};

int logicvc_of_property_parse_u32(struct device_node *of_node,
				  unsigned int index, u32 *target);
void logicvc_of_property_parse_bool(struct device_node *of_node,
				    unsigned int index, bool *target);
bool logicvc_of_node_is_layer(struct device_node *of_node);

#endif
