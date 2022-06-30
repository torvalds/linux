// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2019-2022 Bootlin
 * Author: Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 */

#include <linux/of.h>
#include <linux/types.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_blend.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_plane.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_print.h>

#include "logicvc_crtc.h"
#include "logicvc_drm.h"
#include "logicvc_layer.h"
#include "logicvc_of.h"
#include "logicvc_regs.h"

#define logicvc_layer(p) \
	container_of(p, struct logicvc_layer, drm_plane)

static uint32_t logicvc_layer_formats_rgb16[] = {
	DRM_FORMAT_RGB565,
	DRM_FORMAT_BGR565,
	DRM_FORMAT_INVALID,
};

static uint32_t logicvc_layer_formats_rgb24[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_INVALID,
};

/*
 * What we call depth in this driver only counts color components, not alpha.
 * This allows us to stay compatible with the LogiCVC bistream definitions.
 */
static uint32_t logicvc_layer_formats_rgb24_alpha[] = {
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_INVALID,
};

static struct logicvc_layer_formats logicvc_layer_formats[] = {
	{
		.colorspace	= LOGICVC_LAYER_COLORSPACE_RGB,
		.depth		= 16,
		.formats	= logicvc_layer_formats_rgb16,
	},
	{
		.colorspace	= LOGICVC_LAYER_COLORSPACE_RGB,
		.depth		= 24,
		.formats	= logicvc_layer_formats_rgb24,
	},
	{
		.colorspace	= LOGICVC_LAYER_COLORSPACE_RGB,
		.depth		= 24,
		.alpha		= true,
		.formats	= logicvc_layer_formats_rgb24_alpha,
	},
	{ }
};

static bool logicvc_layer_format_inverted(uint32_t format)
{
	switch (format) {
	case DRM_FORMAT_BGR565:
	case DRM_FORMAT_BGR888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ABGR8888:
		return true;
	default:
		return false;
	}
}

static int logicvc_plane_atomic_check(struct drm_plane *drm_plane,
				      struct drm_atomic_state *state)
{
	struct drm_device *drm_dev = drm_plane->dev;
	struct logicvc_layer *layer = logicvc_layer(drm_plane);
	struct logicvc_drm *logicvc = logicvc_drm(drm_dev);
	struct drm_plane_state *new_state =
		drm_atomic_get_new_plane_state(state, drm_plane);
	struct drm_crtc_state *crtc_state;
	int min_scale, max_scale;
	bool can_position;
	int ret;

	if (!new_state->crtc)
		return 0;

	crtc_state = drm_atomic_get_existing_crtc_state(new_state->state,
							new_state->crtc);
	if (WARN_ON(!crtc_state))
		return -EINVAL;

	if (new_state->crtc_x < 0 || new_state->crtc_y < 0) {
		drm_err(drm_dev,
			"Negative on-CRTC positions are not supported.\n");
		return -EINVAL;
	}

	if (!logicvc->caps->layer_address) {
		ret = logicvc_layer_buffer_find_setup(logicvc, layer, new_state,
						      NULL);
		if (ret) {
			drm_err(drm_dev, "No viable setup for buffer found.\n");
			return ret;
		}
	}

	min_scale = DRM_PLANE_HELPER_NO_SCALING;
	max_scale = DRM_PLANE_HELPER_NO_SCALING;

	can_position = (drm_plane->type == DRM_PLANE_TYPE_OVERLAY &&
			layer->index != (logicvc->config.layers_count - 1) &&
			logicvc->config.layers_configurable);

	ret = drm_atomic_helper_check_plane_state(new_state, crtc_state,
						  min_scale, max_scale,
						  can_position, true);
	if (ret) {
		drm_err(drm_dev, "Invalid plane state\n\n");
		return ret;
	}

	return 0;
}

static void logicvc_plane_atomic_update(struct drm_plane *drm_plane,
					struct drm_atomic_state *state)
{
	struct logicvc_layer *layer = logicvc_layer(drm_plane);
	struct logicvc_drm *logicvc = logicvc_drm(drm_plane->dev);
	struct drm_device *drm_dev = &logicvc->drm_dev;
	struct drm_plane_state *new_state =
		drm_atomic_get_new_plane_state(state, drm_plane);
	struct drm_crtc *drm_crtc = &logicvc->crtc->drm_crtc;
	struct drm_display_mode *mode = &drm_crtc->state->adjusted_mode;
	struct drm_framebuffer *fb = new_state->fb;
	struct logicvc_layer_buffer_setup setup = {};
	u32 index = layer->index;
	u32 reg;

	/* Layer dimensions */

	regmap_write(logicvc->regmap, LOGICVC_LAYER_WIDTH_REG(index),
		     new_state->crtc_w - 1);
	regmap_write(logicvc->regmap, LOGICVC_LAYER_HEIGHT_REG(index),
		     new_state->crtc_h - 1);

	if (logicvc->caps->layer_address) {
		phys_addr_t fb_addr = drm_fb_cma_get_gem_addr(fb, new_state, 0);

		regmap_write(logicvc->regmap, LOGICVC_LAYER_ADDRESS_REG(index),
			     fb_addr);
	} else {
		/* Rely on offsets to configure the address. */

		logicvc_layer_buffer_find_setup(logicvc, layer, new_state,
						&setup);

		/* Layer memory offsets */

		regmap_write(logicvc->regmap, LOGICVC_BUFFER_SEL_REG,
			     LOGICVC_BUFFER_SEL_VALUE(index, setup.buffer_sel));
		regmap_write(logicvc->regmap, LOGICVC_LAYER_HOFFSET_REG(index),
			     setup.hoffset);
		regmap_write(logicvc->regmap, LOGICVC_LAYER_VOFFSET_REG(index),
			     setup.voffset);
	}

	/* Layer position */

	regmap_write(logicvc->regmap, LOGICVC_LAYER_HPOSITION_REG(index),
		     mode->hdisplay - 1 - new_state->crtc_x);

	/* Vertical position must be set last to sync layer register changes. */
	regmap_write(logicvc->regmap, LOGICVC_LAYER_VPOSITION_REG(index),
		     mode->vdisplay - 1 - new_state->crtc_y);

	/* Layer alpha */

	if (layer->config.alpha_mode == LOGICVC_LAYER_ALPHA_LAYER) {
		u32 alpha_bits;
		u32 alpha_max;
		u32 alpha;

		switch (layer->config.depth) {
		case 8:
			alpha_bits = 3;
			break;
		case 16:
			if (layer->config.colorspace ==
			    LOGICVC_LAYER_COLORSPACE_YUV)
				alpha_bits = 8;
			else
				alpha_bits = 6;
			break;
		default:
			alpha_bits = 8;
			break;
		}

		alpha_max = BIT(alpha_bits) - 1;
		alpha = new_state->alpha * alpha_max / DRM_BLEND_ALPHA_OPAQUE;

		drm_dbg_kms(drm_dev, "Setting layer %d alpha to %d/%d\n", index,
			    alpha, alpha_max);

		regmap_write(logicvc->regmap, LOGICVC_LAYER_ALPHA_REG(index),
			     alpha);
	}

	/* Layer control */

	reg = LOGICVC_LAYER_CTRL_ENABLE;

	if (logicvc_layer_format_inverted(fb->format->format))
		reg |= LOGICVC_LAYER_CTRL_PIXEL_FORMAT_INVERT;

	reg |= LOGICVC_LAYER_CTRL_COLOR_KEY_DISABLE;

	regmap_write(logicvc->regmap, LOGICVC_LAYER_CTRL_REG(index), reg);
}

static void logicvc_plane_atomic_disable(struct drm_plane *drm_plane,
					 struct drm_atomic_state *state)
{
	struct logicvc_layer *layer = logicvc_layer(drm_plane);
	struct logicvc_drm *logicvc = logicvc_drm(drm_plane->dev);
	u32 index = layer->index;

	regmap_write(logicvc->regmap, LOGICVC_LAYER_CTRL_REG(index), 0);
}

static struct drm_plane_helper_funcs logicvc_plane_helper_funcs = {
	.atomic_check		= logicvc_plane_atomic_check,
	.atomic_update		= logicvc_plane_atomic_update,
	.atomic_disable		= logicvc_plane_atomic_disable,
};

static const struct drm_plane_funcs logicvc_plane_funcs = {
	.update_plane		= drm_atomic_helper_update_plane,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.destroy		= drm_plane_cleanup,
	.reset			= drm_atomic_helper_plane_reset,
	.atomic_duplicate_state	= drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_plane_destroy_state,
};

int logicvc_layer_buffer_find_setup(struct logicvc_drm *logicvc,
				    struct logicvc_layer *layer,
				    struct drm_plane_state *state,
				    struct logicvc_layer_buffer_setup *setup)
{
	struct drm_device *drm_dev = &logicvc->drm_dev;
	struct drm_framebuffer *fb = state->fb;
	/* All the supported formats have a single data plane. */
	u32 layer_bytespp = fb->format->cpp[0];
	u32 layer_stride = layer_bytespp * logicvc->config.row_stride;
	u32 base_offset = layer->config.base_offset * layer_stride;
	u32 buffer_offset = layer->config.buffer_offset * layer_stride;
	u8 buffer_sel = 0;
	u16 voffset = 0;
	u16 hoffset = 0;
	phys_addr_t fb_addr;
	u32 fb_offset;
	u32 gap;

	if (!logicvc->reserved_mem_base) {
		drm_err(drm_dev, "No reserved memory base was registered!\n");
		return -ENOMEM;
	}

	fb_addr = drm_fb_cma_get_gem_addr(fb, state, 0);
	if (fb_addr < logicvc->reserved_mem_base) {
		drm_err(drm_dev,
			"Framebuffer memory below reserved memory base!\n");
		return -EINVAL;
	}

	fb_offset = (u32) (fb_addr - logicvc->reserved_mem_base);

	if (fb_offset < base_offset) {
		drm_err(drm_dev,
			"Framebuffer offset below layer base offset!\n");
		return -EINVAL;
	}

	gap = fb_offset - base_offset;

	/* Use the possible video buffers selection. */
	if (gap && buffer_offset) {
		buffer_sel = gap / buffer_offset;
		if (buffer_sel > LOGICVC_BUFFER_SEL_MAX)
			buffer_sel = LOGICVC_BUFFER_SEL_MAX;

		gap -= buffer_sel * buffer_offset;
	}

	/* Use the vertical offset. */
	if (gap && layer_stride && logicvc->config.layers_configurable) {
		voffset = gap / layer_stride;
		if (voffset > LOGICVC_LAYER_VOFFSET_MAX)
			voffset = LOGICVC_LAYER_VOFFSET_MAX;

		gap -= voffset * layer_stride;
	}

	/* Use the horizontal offset. */
	if (gap && layer_bytespp && logicvc->config.layers_configurable) {
		hoffset = gap / layer_bytespp;
		if (hoffset > LOGICVC_DIMENSIONS_MAX)
			hoffset = LOGICVC_DIMENSIONS_MAX;

		gap -= hoffset * layer_bytespp;
	}

	if (gap) {
		drm_err(drm_dev,
			"Unable to find layer %d buffer setup for 0x%x byte gap\n",
			layer->index, fb_offset - base_offset);
		return -EINVAL;
	}

	drm_dbg_kms(drm_dev, "Found layer %d buffer setup for 0x%x byte gap:\n",
		    layer->index, fb_offset - base_offset);

	drm_dbg_kms(drm_dev, "- buffer_sel = 0x%x chunks of 0x%x bytes\n",
		    buffer_sel, buffer_offset);
	drm_dbg_kms(drm_dev, "- voffset = 0x%x chunks of 0x%x bytes\n", voffset,
		    layer_stride);
	drm_dbg_kms(drm_dev, "- hoffset = 0x%x chunks of 0x%x bytes\n", hoffset,
		    layer_bytespp);

	if (setup) {
		setup->buffer_sel = buffer_sel;
		setup->voffset = voffset;
		setup->hoffset = hoffset;
	}

	return 0;
}

static struct logicvc_layer_formats *logicvc_layer_formats_lookup(struct logicvc_layer *layer)
{
	bool alpha;
	unsigned int i = 0;

	alpha = (layer->config.alpha_mode == LOGICVC_LAYER_ALPHA_PIXEL);

	while (logicvc_layer_formats[i].formats) {
		if (logicvc_layer_formats[i].colorspace == layer->config.colorspace &&
		    logicvc_layer_formats[i].depth == layer->config.depth &&
		    logicvc_layer_formats[i].alpha == alpha)
			return &logicvc_layer_formats[i];

		i++;
	}

	return NULL;
}

static unsigned int logicvc_layer_formats_count(struct logicvc_layer_formats *formats)
{
	unsigned int count = 0;

	while (formats->formats[count] != DRM_FORMAT_INVALID)
		count++;

	return count;
}

static int logicvc_layer_config_parse(struct logicvc_drm *logicvc,
				      struct logicvc_layer *layer)
{
	struct device_node *of_node = layer->of_node;
	struct logicvc_layer_config *config = &layer->config;
	int ret;

	logicvc_of_property_parse_bool(of_node,
				       LOGICVC_OF_PROPERTY_LAYER_PRIMARY,
				       &config->primary);

	ret = logicvc_of_property_parse_u32(of_node,
					    LOGICVC_OF_PROPERTY_LAYER_COLORSPACE,
					    &config->colorspace);
	if (ret)
		return ret;

	ret = logicvc_of_property_parse_u32(of_node,
					    LOGICVC_OF_PROPERTY_LAYER_DEPTH,
					    &config->depth);
	if (ret)
		return ret;

	ret = logicvc_of_property_parse_u32(of_node,
					    LOGICVC_OF_PROPERTY_LAYER_ALPHA_MODE,
					    &config->alpha_mode);
	if (ret)
		return ret;

	/*
	 * Memory offset is only relevant without layer address configuration.
	 */
	if (logicvc->caps->layer_address)
		return 0;

	ret = logicvc_of_property_parse_u32(of_node,
					    LOGICVC_OF_PROPERTY_LAYER_BASE_OFFSET,
					    &config->base_offset);
	if (ret)
		return ret;

	ret = logicvc_of_property_parse_u32(of_node,
					    LOGICVC_OF_PROPERTY_LAYER_BUFFER_OFFSET,
					    &config->buffer_offset);
	if (ret)
		return ret;

	return 0;
}

struct logicvc_layer *logicvc_layer_get_from_index(struct logicvc_drm *logicvc,
						   u32 index)
{
	struct logicvc_layer *layer;

	list_for_each_entry(layer, &logicvc->layers_list, list)
		if (layer->index == index)
			return layer;

	return NULL;
}

struct logicvc_layer *logicvc_layer_get_from_type(struct logicvc_drm *logicvc,
						  enum drm_plane_type type)
{
	struct logicvc_layer *layer;

	list_for_each_entry(layer, &logicvc->layers_list, list)
		if (layer->drm_plane.type == type)
			return layer;

	return NULL;
}

struct logicvc_layer *logicvc_layer_get_primary(struct logicvc_drm *logicvc)
{
	return logicvc_layer_get_from_type(logicvc, DRM_PLANE_TYPE_PRIMARY);
}

static int logicvc_layer_init(struct logicvc_drm *logicvc,
			      struct device_node *of_node, u32 index)
{
	struct drm_device *drm_dev = &logicvc->drm_dev;
	struct device *dev = drm_dev->dev;
	struct logicvc_layer *layer = NULL;
	struct logicvc_layer_formats *formats;
	unsigned int formats_count;
	enum drm_plane_type type;
	unsigned int zpos;
	int ret;

	layer = devm_kzalloc(dev, sizeof(*layer), GFP_KERNEL);
	if (!layer) {
		ret = -ENOMEM;
		goto error;
	}

	layer->of_node = of_node;
	layer->index = index;

	ret = logicvc_layer_config_parse(logicvc, layer);
	if (ret) {
		drm_err(drm_dev, "Failed to parse config for layer #%d\n",
			index);
		goto error;
	}

	formats = logicvc_layer_formats_lookup(layer);
	if (!formats) {
		drm_err(drm_dev, "Failed to lookup formats for layer #%d\n",
			index);
		ret = -EINVAL;
		goto error;
	}

	formats_count = logicvc_layer_formats_count(formats);

	/* The final layer can be configured as a background layer. */
	if (logicvc->config.background_layer &&
	    index == (logicvc->config.layers_count - 1)) {
		/*
		 * A zero value for black is only valid for RGB, not for YUV,
		 * so this will need to take the format in account for YUV.
		 */
		u32 background = 0;

		drm_dbg_kms(drm_dev, "Using layer #%d as background layer\n",
			    index);

		regmap_write(logicvc->regmap, LOGICVC_BACKGROUND_COLOR_REG,
			     background);

		devm_kfree(dev, layer);

		return 0;
	}

	if (layer->config.primary)
		type = DRM_PLANE_TYPE_PRIMARY;
	else
		type = DRM_PLANE_TYPE_OVERLAY;

	ret = drm_universal_plane_init(drm_dev, &layer->drm_plane, 0,
				       &logicvc_plane_funcs, formats->formats,
				       formats_count, NULL, type, NULL);
	if (ret) {
		drm_err(drm_dev, "Failed to initialize layer plane\n");
		return ret;
	}

	drm_plane_helper_add(&layer->drm_plane, &logicvc_plane_helper_funcs);

	zpos = logicvc->config.layers_count - index - 1;
	drm_dbg_kms(drm_dev, "Giving layer #%d zpos %d\n", index, zpos);

	if (layer->config.alpha_mode == LOGICVC_LAYER_ALPHA_LAYER)
		drm_plane_create_alpha_property(&layer->drm_plane);

	drm_plane_create_zpos_immutable_property(&layer->drm_plane, zpos);

	drm_dbg_kms(drm_dev, "Registering layer #%d\n", index);

	layer->formats = formats;

	list_add_tail(&layer->list, &logicvc->layers_list);

	return 0;

error:
	if (layer)
		devm_kfree(dev, layer);

	return ret;
}

static void logicvc_layer_fini(struct logicvc_drm *logicvc,
			       struct logicvc_layer *layer)
{
	struct device *dev = logicvc->drm_dev.dev;

	list_del(&layer->list);
	devm_kfree(dev, layer);
}

void logicvc_layers_attach_crtc(struct logicvc_drm *logicvc)
{
	uint32_t possible_crtcs = drm_crtc_mask(&logicvc->crtc->drm_crtc);
	struct logicvc_layer *layer;

	list_for_each_entry(layer, &logicvc->layers_list, list) {
		if (layer->drm_plane.type != DRM_PLANE_TYPE_OVERLAY)
			continue;

		layer->drm_plane.possible_crtcs = possible_crtcs;
	}
}

int logicvc_layers_init(struct logicvc_drm *logicvc)
{
	struct drm_device *drm_dev = &logicvc->drm_dev;
	struct device *dev = drm_dev->dev;
	struct device_node *of_node = dev->of_node;
	struct device_node *layer_node = NULL;
	struct device_node *layers_node;
	struct logicvc_layer *layer;
	struct logicvc_layer *next;
	int ret = 0;

	layers_node = of_get_child_by_name(of_node, "layers");
	if (!layers_node) {
		drm_err(drm_dev, "No layers node found in the description\n");
		ret = -ENODEV;
		goto error;
	}

	for_each_child_of_node(layers_node, layer_node) {
		u32 index = 0;

		if (!logicvc_of_node_is_layer(layer_node))
			continue;

		ret = of_property_read_u32(layer_node, "reg", &index);
		if (ret)
			continue;

		layer = logicvc_layer_get_from_index(logicvc, index);
		if (layer) {
			drm_err(drm_dev, "Duplicated entry for layer #%d\n",
				index);
			continue;
		}

		ret = logicvc_layer_init(logicvc, layer_node, index);
		if (ret) {
			of_node_put(layers_node);
			goto error;
		}
	}

	of_node_put(layers_node);

	return 0;

error:
	list_for_each_entry_safe(layer, next, &logicvc->layers_list, list)
		logicvc_layer_fini(logicvc, layer);

	return ret;
}
