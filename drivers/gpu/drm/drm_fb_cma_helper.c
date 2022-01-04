// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * drm kms/fb cma (contiguous memory allocator) helper functions
 *
 * Copyright (C) 2012 Analog Devices Inc.
 *   Author: Lars-Peter Clausen <lars@metafoo.de>
 *
 * Based on udl_fbdev.c
 *  Copyright (C) 2012 Red Hat
 */

#include <drm/drm_damage_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_plane.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>

/**
 * DOC: framebuffer cma helper functions
 *
 * Provides helper functions for creating a cma (contiguous memory allocator)
 * backed framebuffer.
 *
 * drm_gem_fb_create() is used in the &drm_mode_config_funcs.fb_create
 * callback function to create a cma backed framebuffer.
 */

/**
 * drm_fb_cma_get_gem_obj() - Get CMA GEM object for framebuffer
 * @fb: The framebuffer
 * @plane: Which plane
 *
 * Return the CMA GEM object for given framebuffer.
 *
 * This function will usually be called from the CRTC callback functions.
 */
struct drm_gem_cma_object *drm_fb_cma_get_gem_obj(struct drm_framebuffer *fb,
						  unsigned int plane)
{
	struct drm_gem_object *gem;

	gem = drm_gem_fb_get_obj(fb, plane);
	if (!gem)
		return NULL;

	return to_drm_gem_cma_obj(gem);
}
EXPORT_SYMBOL_GPL(drm_fb_cma_get_gem_obj);

/**
 * drm_fb_cma_get_gem_addr() - Get physical address for framebuffer, for pixel
 * formats where values are grouped in blocks this will get you the beginning of
 * the block
 * @fb: The framebuffer
 * @state: Which state of drm plane
 * @plane: Which plane
 * Return the CMA GEM address for given framebuffer.
 *
 * This function will usually be called from the PLANE callback functions.
 */
dma_addr_t drm_fb_cma_get_gem_addr(struct drm_framebuffer *fb,
				   struct drm_plane_state *state,
				   unsigned int plane)
{
	struct drm_gem_cma_object *obj;
	dma_addr_t paddr;
	u8 h_div = 1, v_div = 1;
	u32 block_w = drm_format_info_block_width(fb->format, plane);
	u32 block_h = drm_format_info_block_height(fb->format, plane);
	u32 block_size = fb->format->char_per_block[plane];
	u32 sample_x;
	u32 sample_y;
	u32 block_start_y;
	u32 num_hblocks;

	obj = drm_fb_cma_get_gem_obj(fb, plane);
	if (!obj)
		return 0;

	paddr = obj->paddr + fb->offsets[plane];

	if (plane > 0) {
		h_div = fb->format->hsub;
		v_div = fb->format->vsub;
	}

	sample_x = (state->src_x >> 16) / h_div;
	sample_y = (state->src_y >> 16) / v_div;
	block_start_y = (sample_y / block_h) * block_h;
	num_hblocks = sample_x / block_w;

	paddr += fb->pitches[plane] * block_start_y;
	paddr += block_size * num_hblocks;

	return paddr;
}
EXPORT_SYMBOL_GPL(drm_fb_cma_get_gem_addr);

/**
 * drm_fb_cma_sync_non_coherent - Sync GEM object to non-coherent backing
 *	memory
 * @drm: DRM device
 * @old_state: Old plane state
 * @state: New plane state
 *
 * This function can be used by drivers that use damage clips and have
 * CMA GEM objects backed by non-coherent memory. Calling this function
 * in a plane's .atomic_update ensures that all the data in the backing
 * memory have been written to RAM.
 */
void drm_fb_cma_sync_non_coherent(struct drm_device *drm,
				  struct drm_plane_state *old_state,
				  struct drm_plane_state *state)
{
	const struct drm_format_info *finfo = state->fb->format;
	struct drm_atomic_helper_damage_iter iter;
	const struct drm_gem_cma_object *cma_obj;
	unsigned int offset, i;
	struct drm_rect clip;
	dma_addr_t daddr;
	size_t nb_bytes;

	for (i = 0; i < finfo->num_planes; i++) {
		cma_obj = drm_fb_cma_get_gem_obj(state->fb, i);
		if (!cma_obj->map_noncoherent)
			continue;

		daddr = drm_fb_cma_get_gem_addr(state->fb, state, i);
		drm_atomic_helper_damage_iter_init(&iter, old_state, state);

		drm_atomic_for_each_plane_damage(&iter, &clip) {
			/* Ignore x1/x2 values, invalidate complete lines */
			offset = clip.y1 * state->fb->pitches[i];

			nb_bytes = (clip.y2 - clip.y1) * state->fb->pitches[i];
			dma_sync_single_for_device(drm->dev, daddr + offset,
						   nb_bytes, DMA_TO_DEVICE);
		}
	}
}
EXPORT_SYMBOL_GPL(drm_fb_cma_sync_non_coherent);
