/*
 * Copyright (C) 2016 Samsung Electronics Co.Ltd
 * Authors:
 *	Marek Szyprowski <m.szyprowski@samsung.com>
 *
 * DRM core plane blending related functions
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */
#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_crtc.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/sort.h>

#include "drm_internal.h"

/**
 * drm_plane_create_zpos_property - create mutable zpos property
 * @plane: drm plane
 * @zpos: initial value of zpos property
 * @min: minimal possible value of zpos property
 * @max: maximal possible value of zpos property
 *
 * This function initializes generic mutable zpos property and enables support
 * for it in drm core. Drivers can then attach this property to planes to enable
 * support for configurable planes arrangement during blending operation.
 * Once mutable zpos property has been enabled, the DRM core will automatically
 * calculate drm_plane_state->normalized_zpos values. Usually min should be set
 * to 0 and max to maximal number of planes for given crtc - 1.
 *
 * If zpos of some planes cannot be changed (like fixed background or
 * cursor/topmost planes), driver should adjust min/max values and assign those
 * planes immutable zpos property with lower or higher values (for more
 * information, see drm_mode_create_zpos_immutable_property() function). In such
 * case driver should also assign proper initial zpos values for all planes in
 * its plane_reset() callback, so the planes will be always sorted properly.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_plane_create_zpos_property(struct drm_plane *plane,
				   unsigned int zpos,
				   unsigned int min, unsigned int max)
{
	struct drm_property *prop;

	prop = drm_property_create_range(plane->dev, 0, "zpos", min, max);
	if (!prop)
		return -ENOMEM;

	drm_object_attach_property(&plane->base, prop, zpos);

	plane->zpos_property = prop;

	if (plane->state) {
		plane->state->zpos = zpos;
		plane->state->normalized_zpos = zpos;
	}

	return 0;
}
EXPORT_SYMBOL(drm_plane_create_zpos_property);

/**
 * drm_plane_create_zpos_immutable_property - create immuttable zpos property
 * @plane: drm plane
 * @zpos: value of zpos property
 *
 * This function initializes generic immutable zpos property and enables
 * support for it in drm core. Using this property driver lets userspace
 * to get the arrangement of the planes for blending operation and notifies
 * it that the hardware (or driver) doesn't support changing of the planes'
 * order.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_plane_create_zpos_immutable_property(struct drm_plane *plane,
					     unsigned int zpos)
{
	struct drm_property *prop;

	prop = drm_property_create_range(plane->dev, DRM_MODE_PROP_IMMUTABLE,
					 "zpos", zpos, zpos);
	if (!prop)
		return -ENOMEM;

	drm_object_attach_property(&plane->base, prop, zpos);

	plane->zpos_property = prop;

	if (plane->state) {
		plane->state->zpos = zpos;
		plane->state->normalized_zpos = zpos;
	}

	return 0;
}
EXPORT_SYMBOL(drm_plane_create_zpos_immutable_property);

static int drm_atomic_state_zpos_cmp(const void *a, const void *b)
{
	const struct drm_plane_state *sa = *(struct drm_plane_state **)a;
	const struct drm_plane_state *sb = *(struct drm_plane_state **)b;

	if (sa->zpos != sb->zpos)
		return sa->zpos - sb->zpos;
	else
		return sa->plane->base.id - sb->plane->base.id;
}

/**
 * drm_atomic_helper_crtc_normalize_zpos - calculate normalized zpos values
 * @crtc: crtc with planes, which have to be considered for normalization
 * @crtc_state: new atomic state to apply
 *
 * This function checks new states of all planes assigned to given crtc and
 * calculates normalized zpos value for them. Planes are compared first by their
 * zpos values, then by plane id (if zpos equals). Plane with lowest zpos value
 * is at the bottom. The plane_state->normalized_zpos is then filled with unique
 * values from 0 to number of active planes in crtc minus one.
 *
 * RETURNS
 * Zero for success or -errno
 */
static int drm_atomic_helper_crtc_normalize_zpos(struct drm_crtc *crtc,
					  struct drm_crtc_state *crtc_state)
{
	struct drm_atomic_state *state = crtc_state->state;
	struct drm_device *dev = crtc->dev;
	int total_planes = dev->mode_config.num_total_plane;
	struct drm_plane_state **states;
	struct drm_plane *plane;
	int i, n = 0;
	int ret = 0;

	DRM_DEBUG_ATOMIC("[CRTC:%d:%s] calculating normalized zpos values\n",
			 crtc->base.id, crtc->name);

	states = kmalloc_array(total_planes, sizeof(*states), GFP_TEMPORARY);
	if (!states)
		return -ENOMEM;

	/*
	 * Normalization process might create new states for planes which
	 * normalized_zpos has to be recalculated.
	 */
	drm_for_each_plane_mask(plane, dev, crtc_state->plane_mask) {
		struct drm_plane_state *plane_state =
			drm_atomic_get_plane_state(state, plane);
		if (IS_ERR(plane_state)) {
			ret = PTR_ERR(plane_state);
			goto done;
		}
		states[n++] = plane_state;
		DRM_DEBUG_ATOMIC("[PLANE:%d:%s] processing zpos value %d\n",
				 plane->base.id, plane->name,
				 plane_state->zpos);
	}

	sort(states, n, sizeof(*states), drm_atomic_state_zpos_cmp, NULL);

	for (i = 0; i < n; i++) {
		plane = states[i]->plane;

		states[i]->normalized_zpos = i;
		DRM_DEBUG_ATOMIC("[PLANE:%d:%s] normalized zpos value %d\n",
				 plane->base.id, plane->name, i);
	}
	crtc_state->zpos_changed = true;

done:
	kfree(states);
	return ret;
}

/**
 * drm_atomic_helper_normalize_zpos - calculate normalized zpos values for all
 *				      crtcs
 * @dev: DRM device
 * @state: atomic state of DRM device
 *
 * This function calculates normalized zpos value for all modified planes in
 * the provided atomic state of DRM device. For more information, see
 * drm_atomic_helper_crtc_normalize_zpos() function.
 *
 * RETURNS
 * Zero for success or -errno
 */
int drm_atomic_helper_normalize_zpos(struct drm_device *dev,
				     struct drm_atomic_state *state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	struct drm_plane *plane;
	struct drm_plane_state *plane_state;
	int i, ret = 0;

	for_each_plane_in_state(state, plane, plane_state, i) {
		crtc = plane_state->crtc;
		if (!crtc)
			continue;
		if (plane->state->zpos != plane_state->zpos) {
			crtc_state =
				drm_atomic_get_existing_crtc_state(state, crtc);
			crtc_state->zpos_changed = true;
		}
	}

	for_each_crtc_in_state(state, crtc, crtc_state, i) {
		if (crtc_state->plane_mask != crtc->state->plane_mask ||
		    crtc_state->zpos_changed) {
			ret = drm_atomic_helper_crtc_normalize_zpos(crtc,
								    crtc_state);
			if (ret)
				return ret;
		}
	}
	return 0;
}
