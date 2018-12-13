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
 * Rob Clark <robdclark@gmail.com>
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
 * Drivers implementing damage can use drm_atomic_helper_damage_iter_init() and
 * drm_atomic_helper_damage_iter_next() helper iterator function to get damage
 * rectangles clipped to &drm_plane_state.src.
 */

static void convert_clip_rect_to_rect(const struct drm_clip_rect *src,
				      struct drm_mode_rect *dest,
				      uint32_t num_clips, uint32_t src_inc)
{
	while (num_clips > 0) {
		dest->x1 = src->x1;
		dest->y1 = src->y1;
		dest->x2 = src->x2;
		dest->y2 = src->y2;
		src += src_inc;
		dest++;
		num_clips--;
	}
}

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

/**
 * drm_atomic_helper_dirtyfb - Helper for dirtyfb.
 * @fb: DRM framebuffer.
 * @file_priv: Drm file for the ioctl call.
 * @flags: Dirty fb annotate flags.
 * @color: Color for annotate fill.
 * @clips: Dirty region.
 * @num_clips: Count of clip in clips.
 *
 * A helper to implement &drm_framebuffer_funcs.dirty using damage interface
 * during plane update. If num_clips is 0 then this helper will do a full plane
 * update. This is the same behaviour expected by DIRTFB IOCTL.
 *
 * Note that this helper is blocking implementation. This is what current
 * drivers and userspace expect in their DIRTYFB IOCTL implementation, as a way
 * to rate-limit userspace and make sure its rendering doesn't get ahead of
 * uploading new data too much.
 *
 * Return: Zero on success, negative errno on failure.
 */
int drm_atomic_helper_dirtyfb(struct drm_framebuffer *fb,
			      struct drm_file *file_priv, unsigned int flags,
			      unsigned int color, struct drm_clip_rect *clips,
			      unsigned int num_clips)
{
	struct drm_modeset_acquire_ctx ctx;
	struct drm_property_blob *damage = NULL;
	struct drm_mode_rect *rects = NULL;
	struct drm_atomic_state *state;
	struct drm_plane *plane;
	int ret = 0;

	/*
	 * When called from ioctl, we are interruptable, but not when called
	 * internally (ie. defio worker)
	 */
	drm_modeset_acquire_init(&ctx,
		file_priv ? DRM_MODESET_ACQUIRE_INTERRUPTIBLE : 0);

	state = drm_atomic_state_alloc(fb->dev);
	if (!state) {
		ret = -ENOMEM;
		goto out;
	}
	state->acquire_ctx = &ctx;

	if (clips) {
		uint32_t inc = 1;

		if (flags & DRM_MODE_FB_DIRTY_ANNOTATE_COPY) {
			inc = 2;
			num_clips /= 2;
		}

		rects = kcalloc(num_clips, sizeof(*rects), GFP_KERNEL);
		if (!rects) {
			ret = -ENOMEM;
			goto out;
		}

		convert_clip_rect_to_rect(clips, rects, num_clips, inc);
		damage = drm_property_create_blob(fb->dev,
						  num_clips * sizeof(*rects),
						  rects);
		if (IS_ERR(damage)) {
			ret = PTR_ERR(damage);
			damage = NULL;
			goto out;
		}
	}

retry:
	drm_for_each_plane(plane, fb->dev) {
		struct drm_plane_state *plane_state;

		if (plane->state->fb != fb)
			continue;

		plane_state = drm_atomic_get_plane_state(state, plane);
		if (IS_ERR(plane_state)) {
			ret = PTR_ERR(plane_state);
			goto out;
		}

		drm_property_replace_blob(&plane_state->fb_damage_clips,
					  damage);
	}

	ret = drm_atomic_commit(state);

out:
	if (ret == -EDEADLK) {
		drm_atomic_state_clear(state);
		ret = drm_modeset_backoff(&ctx);
		if (!ret)
			goto retry;
	}

	drm_property_blob_put(damage);
	kfree(rects);
	drm_atomic_state_put(state);

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);

	return ret;

}
EXPORT_SYMBOL(drm_atomic_helper_dirtyfb);

/**
 * drm_atomic_helper_damage_iter_init - Initialize the damage iterator.
 * @iter: The iterator to initialize.
 * @old_state: Old plane state for validation.
 * @state: Plane state from which to iterate the damage clips.
 *
 * Initialize an iterator, which clips plane damage
 * &drm_plane_state.fb_damage_clips to plane &drm_plane_state.src. This iterator
 * returns full plane src in case damage is not present because either
 * user-space didn't sent or driver discarded it (it want to do full plane
 * update). Currently this iterator returns full plane src in case plane src
 * changed but that can be changed in future to return damage.
 *
 * For the case when plane is not visible or plane update should not happen the
 * first call to iter_next will return false. Note that this helper use clipped
 * &drm_plane_state.src, so driver calling this helper should have called
 * drm_atomic_helper_check_plane_state() earlier.
 */
void
drm_atomic_helper_damage_iter_init(struct drm_atomic_helper_damage_iter *iter,
				   const struct drm_plane_state *old_state,
				   const struct drm_plane_state *state)
{
	memset(iter, 0, sizeof(*iter));

	if (!state || !state->crtc || !state->fb || !state->visible)
		return;

	iter->clips = drm_helper_get_plane_damage_clips(state);
	iter->num_clips = drm_plane_get_damage_clips_count(state);

	/* Round down for x1/y1 and round up for x2/y2 to catch all pixels */
	iter->plane_src.x1 = state->src.x1 >> 16;
	iter->plane_src.y1 = state->src.y1 >> 16;
	iter->plane_src.x2 = (state->src.x2 >> 16) + !!(state->src.x2 & 0xFFFF);
	iter->plane_src.y2 = (state->src.y2 >> 16) + !!(state->src.y2 & 0xFFFF);

	if (!iter->clips || !drm_rect_equals(&state->src, &old_state->src)) {
		iter->clips = 0;
		iter->num_clips = 0;
		iter->full_update = true;
	}
}
EXPORT_SYMBOL(drm_atomic_helper_damage_iter_init);

/**
 * drm_atomic_helper_damage_iter_next - Advance the damage iterator.
 * @iter: The iterator to advance.
 * @rect: Return a rectangle in fb coordinate clipped to plane src.
 *
 * Since plane src is in 16.16 fixed point and damage clips are whole number,
 * this iterator round off clips that intersect with plane src. Round down for
 * x1/y1 and round up for x2/y2 for the intersected coordinate. Similar rounding
 * off for full plane src, in case it's returned as damage. This iterator will
 * skip damage clips outside of plane src.
 *
 * Return: True if the output is valid, false if reached the end.
 *
 * If the first call to iterator next returns false then it means no need to
 * update the plane.
 */
bool
drm_atomic_helper_damage_iter_next(struct drm_atomic_helper_damage_iter *iter,
				   struct drm_rect *rect)
{
	bool ret = false;

	if (iter->full_update) {
		*rect = iter->plane_src;
		iter->full_update = false;
		return true;
	}

	while (iter->curr_clip < iter->num_clips) {
		*rect = iter->clips[iter->curr_clip];
		iter->curr_clip++;

		if (drm_rect_intersect(rect, &iter->plane_src)) {
			ret = true;
			break;
		}
	}

	return ret;
}
EXPORT_SYMBOL(drm_atomic_helper_damage_iter_next);
