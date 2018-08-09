// SPDX-License-Identifier: GPL-2.0 OR MIT
/**************************************************************************
 *
 * Copyright (c) 2018 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 * Deepak Rawat <drawat@vmware.com>
 *
 **************************************************************************/

#include <drm/drm_atomic.h>
#include <drm/drm_damage_helper.h>

/**
 * DOC: overview
 *
 * FB_DAMAGE_CLIPS is an optional plane property which provides a means to
 * specify a list of damage rectangles on a plane in framebuffer coordinates of
 * the framebuffer attached to the plane. In current context damage is the area
 * of plane framebuffer that has changed since last plane update (also called
 * page-flip), irrespective of whether currently attached framebuffer is same as
 * framebuffer attached during last plane update or not.
 *
 * FB_DAMAGE_CLIPS is a hint to kernel which could be helpful for some drivers
 * to optimize internally especially for virtual devices where each framebuffer
 * change needs to be transmitted over network, usb, etc.
 *
 * Since FB_DAMAGE_CLIPS is a hint so it is an optional property. User-space can
 * ignore damage clips property and in that case driver will do a full plane
 * update. In case damage clips are provided then it is guaranteed that the area
 * inside damage clips will be updated to plane. For efficiency driver can do
 * full update or can update more than specified in damage clips. Since driver
 * is free to read more, user-space must always render the entire visible
 * framebuffer. Otherwise there can be corruptions. Also, if a user-space
 * provides damage clips which doesn't encompass the actual damage to
 * framebuffer (since last plane update) can result in incorrect rendering.
 *
 * FB_DAMAGE_CLIPS is a blob property with the layout of blob data is simply an
 * array of &drm_mode_rect. Unlike plane &drm_plane_state.src coordinates,
 * damage clips are not in 16.16 fixed point. Similar to plane src in
 * framebuffer, damage clips cannot be negative. In damage clip, x1/y1 are
 * inclusive and x2/y2 are exclusive. While kernel does not error for overlapped
 * damage clips, it is strongly discouraged.
 *
 * Drivers that are interested in damage interface for plane should enable
 * FB_DAMAGE_CLIPS property by calling drm_plane_enable_fb_damage_clips().
 */

/**
 * drm_plane_enable_fb_damage_clips - Enables plane fb damage clips property.
 * @plane: Plane on which to enable damage clips property.
 *
 * This function lets driver to enable the damage clips property on a plane.
 */
void drm_plane_enable_fb_damage_clips(struct drm_plane *plane)
{
	struct drm_device *dev = plane->dev;
	struct drm_mode_config *config = &dev->mode_config;

	drm_object_attach_property(&plane->base, config->prop_fb_damage_clips,
				   0);
}
EXPORT_SYMBOL(drm_plane_enable_fb_damage_clips);

/**
 * drm_atomic_helper_check_plane_damage - Verify plane damage on atomic_check.
 * @state: The driver state object.
 * @plane_state: Plane state for which to verify damage.
 *
 * This helper function makes sure that damage from plane state is discarded
 * for full modeset. If there are more reasons a driver would want to do a full
 * plane update rather than processing individual damage regions, then those
 * cases should be taken care of here.
 *
 * Note that &drm_plane_state.fb_damage_clips == NULL in plane state means that
 * full plane update should happen. It also ensure helper iterator will return
 * &drm_plane_state.src as damage.
 */
void drm_atomic_helper_check_plane_damage(struct drm_atomic_state *state,
					  struct drm_plane_state *plane_state)
{
	struct drm_crtc_state *crtc_state;

	if (plane_state->crtc) {
		crtc_state = drm_atomic_get_new_crtc_state(state,
							   plane_state->crtc);

		if (WARN_ON(!crtc_state))
			return;

		if (drm_atomic_crtc_needs_modeset(crtc_state)) {
			drm_property_blob_put(plane_state->fb_damage_clips);
			plane_state->fb_damage_clips = NULL;
		}
	}
}
EXPORT_SYMBOL(drm_atomic_helper_check_plane_damage);
