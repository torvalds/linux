/*
 * Copyright (C) STMicroelectronics SA 2014
 * Authors: Benjamin Gaignard <benjamin.gaignard@st.com>
 *          Fabien Dessenne <fabien.dessenne@st.com>
 *          for STMicroelectronics.
 * License terms:  GNU General Public License (GPL), version 2
 */

#include <drm/drmP.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_fb_cma_helper.h>

#include "sti_compositor.h"
#include "sti_cursor.h"
#include "sti_gdp.h"
#include "sti_layer.h"
#include "sti_vid.h"

const char *sti_layer_to_str(struct sti_layer *layer)
{
	switch (layer->desc) {
	case STI_GDP_0:
		return "GDP0";
	case STI_GDP_1:
		return "GDP1";
	case STI_GDP_2:
		return "GDP2";
	case STI_GDP_3:
		return "GDP3";
	case STI_VID_0:
		return "VID0";
	case STI_VID_1:
		return "VID1";
	case STI_CURSOR:
		return "CURSOR";
	default:
		return "<UNKNOWN LAYER>";
	}
}

struct sti_layer *sti_layer_create(struct device *dev, int desc,
				   void __iomem *baseaddr)
{

	struct sti_layer *layer = NULL;

	switch (desc & STI_LAYER_TYPE_MASK) {
	case STI_GDP:
		layer = sti_gdp_create(dev, desc);
		break;
	case STI_VID:
		layer = sti_vid_create(dev);
		break;
	case STI_CUR:
		layer = sti_cursor_create(dev);
		break;
	}

	if (!layer) {
		DRM_ERROR("Failed to create layer\n");
		return NULL;
	}

	layer->desc = desc;
	layer->dev = dev;
	layer->regs = baseaddr;

	layer->ops->init(layer);

	DRM_DEBUG_DRIVER("%s created\n", sti_layer_to_str(layer));

	return layer;
}

int sti_layer_prepare(struct sti_layer *layer, struct drm_framebuffer *fb,
		      struct drm_display_mode *mode, int mixer_id,
		      int dest_x, int dest_y, int dest_w, int dest_h,
		      int src_x, int src_y, int src_w, int src_h)
{
	int ret;
	unsigned int i;
	struct drm_gem_cma_object *cma_obj;

	if (!layer || !fb || !mode) {
		DRM_ERROR("Null fb, layer or mode\n");
		return 1;
	}

	cma_obj = drm_fb_cma_get_gem_obj(fb, 0);
	if (!cma_obj) {
		DRM_ERROR("Can't get CMA GEM object for fb\n");
		return 1;
	}

	layer->fb = fb;
	layer->mode = mode;
	layer->mixer_id = mixer_id;
	layer->dst_x = dest_x;
	layer->dst_y = dest_y;
	layer->dst_w = clamp_val(dest_w, 0, mode->crtc_hdisplay - dest_x);
	layer->dst_h = clamp_val(dest_h, 0, mode->crtc_vdisplay - dest_y);
	layer->src_x = src_x;
	layer->src_y = src_y;
	layer->src_w = src_w;
	layer->src_h = src_h;
	layer->format = fb->pixel_format;
	layer->vaddr = cma_obj->vaddr;
	layer->paddr = cma_obj->paddr;
	for (i = 0; i < 4; i++) {
		layer->pitches[i] = fb->pitches[i];
		layer->offsets[i] = fb->offsets[i];
	}

	DRM_DEBUG_DRIVER("%s is associated with mixer_id %d\n",
			 sti_layer_to_str(layer),
			 layer->mixer_id);
	DRM_DEBUG_DRIVER("%s dst=(%dx%d)@(%d,%d) - src=(%dx%d)@(%d,%d)\n",
			 sti_layer_to_str(layer),
			 layer->dst_w, layer->dst_h, layer->dst_x, layer->dst_y,
			 layer->src_w, layer->src_h, layer->src_x,
			 layer->src_y);

	DRM_DEBUG_DRIVER("drm FB:%d format:%.4s phys@:0x%lx\n", fb->base.id,
			 (char *)&layer->format, (unsigned long)layer->paddr);

	if (!layer->ops->prepare)
		goto err_no_prepare;

	ret = layer->ops->prepare(layer, !layer->enabled);
	if (!ret)
		layer->enabled = true;

	return ret;

err_no_prepare:
	DRM_ERROR("Cannot prepare\n");
	return 1;
}

int sti_layer_commit(struct sti_layer *layer)
{
	if (!layer)
		return 1;

	if (!layer->ops->commit)
		goto err_no_commit;

	return layer->ops->commit(layer);

err_no_commit:
	DRM_ERROR("Cannot commit\n");
	return 1;
}

int sti_layer_disable(struct sti_layer *layer)
{
	int ret;

	DRM_DEBUG_DRIVER("%s\n", sti_layer_to_str(layer));
	if (!layer)
		return 1;

	if (!layer->enabled)
		return 0;

	if (!layer->ops->disable)
		goto err_no_disable;

	ret = layer->ops->disable(layer);
	if (!ret)
		layer->enabled = false;
	else
		DRM_ERROR("Disable failed\n");

	return ret;

err_no_disable:
	DRM_ERROR("Cannot disable\n");
	return 1;
}

const uint32_t *sti_layer_get_formats(struct sti_layer *layer)
{
	if (!layer)
		return NULL;

	if (!layer->ops->get_formats)
		return NULL;

	return layer->ops->get_formats(layer);
}

unsigned int sti_layer_get_nb_formats(struct sti_layer *layer)
{
	if (!layer)
		return 0;

	if (!layer->ops->get_nb_formats)
		return 0;

	return layer->ops->get_nb_formats(layer);
}
