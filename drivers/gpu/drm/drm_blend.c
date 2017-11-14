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
#include <drm/drm_blend.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/sort.h>

#include "drm_crtc_internal.h"

/**
 * DOC: overview
 *
 * The basic plane composition model supported by standard plane properties only
 * has a source rectangle (in logical pixels within the &drm_framebuffer), with
 * sub-pixel accuracy, which is scaled up to a pixel-aligned destination
 * rectangle in the visible area of a &drm_crtc. The visible area of a CRTC is
 * defined by the horizontal and vertical visible pixels (stored in @hdisplay
 * and @vdisplay) of the requested mode (stored in &drm_crtc_state.mode). These
 * two rectangles are both stored in the &drm_plane_state.
 *
 * For the atomic ioctl the following standard (atomic) properties on the plane object
 * encode the basic plane composition model:
 *
 * SRC_X:
 * 	X coordinate offset for the source rectangle within the
 * 	&drm_framebuffer, in 16.16 fixed point. Must be positive.
 * SRC_Y:
 * 	Y coordinate offset for the source rectangle within the
 * 	&drm_framebuffer, in 16.16 fixed point. Must be positive.
 * SRC_W:
 * 	Width for the source rectangle within the &drm_framebuffer, in 16.16
 * 	fixed point. SRC_X plus SRC_W must be within the width of the source
 * 	framebuffer. Must be positive.
 * SRC_H:
 * 	Height for the source rectangle within the &drm_framebuffer, in 16.16
 * 	fixed point. SRC_Y plus SRC_H must be within the height of the source
 * 	framebuffer. Must be positive.
 * CRTC_X:
 * 	X coordinate offset for the destination rectangle. Can be negative.
 * CRTC_Y:
 * 	Y coordinate offset for the destination rectangle. Can be negative.
 * CRTC_W:
 * 	Width for the destination rectangle. CRTC_X plus CRTC_W can extend past
 * 	the currently visible horizontal area of the &drm_crtc.
 * CRTC_H:
 * 	Height for the destination rectangle. CRTC_Y plus CRTC_H can extend past
 * 	the currently visible vertical area of the &drm_crtc.
 * FB_ID:
 * 	Mode object ID of the &drm_framebuffer this plane should scan out.
 * CRTC_ID:
 * 	Mode object ID of the &drm_crtc this plane should be connected to.
 *
 * Note that the source rectangle must fully lie within the bounds of the
 * &drm_framebuffer. The destination rectangle can lie outside of the visible
 * area of the current mode of the CRTC. It must be apprpriately clipped by the
 * driver, which can be done by calling drm_plane_helper_check_update(). Drivers
 * are also allowed to round the subpixel sampling positions appropriately, but
 * only to the next full pixel. No pixel outside of the source rectangle may
 * ever be sampled, which is important when applying more sophisticated
 * filtering than just a bilinear one when scaling. The filtering mode when
 * scaling is unspecified.
 *
 * On top of this basic transformation additional properties can be exposed by
 * the driver:
 *
 * - Rotation is set up with drm_plane_create_rotation_property(). It adds a
 *   rotation and reflection step between the source and destination rectangles.
 *   Without this property the rectangle is only scaled, but not rotated or
 *   reflected.
 *
 * - Z position is set up with drm_plane_create_zpos_immutable_property() and
 *   drm_plane_create_zpos_property(). It controls the visibility of overlapping
 *   planes. Without this property the primary plane is always below the cursor
 *   plane, and ordering between all other planes is undefined.
 *
 * Note that all the property extensions described here apply either to the
 * plane or the CRTC (e.g. for the background color, which currently is not
 * exposed and assumed to be black).
 */

/**
 * drm_plane_create_rotation_property - create a new rotation property
 * @plane: drm plane
 * @rotation: initial value of the rotation property
 * @supported_rotations: bitmask of supported rotations and reflections
 *
 * This creates a new property with the selected support for transformations.
 *
 * Since a rotation by 180° degress is the same as reflecting both along the x
 * and the y axis the rotation property is somewhat redundant. Drivers can use
 * drm_rotation_simplify() to normalize values of this property.
 *
 * The property exposed to userspace is a bitmask property (see
 * drm_property_create_bitmask()) called "rotation" and has the following
 * bitmask enumaration values:
 *
 * DRM_MODE_ROTATE_0:
 * 	"rotate-0"
 * DRM_MODE_ROTATE_90:
 * 	"rotate-90"
 * DRM_MODE_ROTATE_180:
 * 	"rotate-180"
 * DRM_MODE_ROTATE_270:
 * 	"rotate-270"
 * DRM_MODE_REFLECT_X:
 * 	"reflect-x"
 * DRM_MODE_REFLECT_Y:
 * 	"reflect-y"
 *
 * Rotation is the specified amount in degrees in counter clockwise direction,
 * the X and Y axis are within the source rectangle, i.e.  the X/Y axis before
 * rotation. After reflection, the rotation is applied to the image sampled from
 * the source rectangle, before scaling it to fit the destination rectangle.
 */
int drm_plane_create_rotation_property(struct drm_plane *plane,
				       unsigned int rotation,
				       unsigned int supported_rotations)
{
	static const struct drm_prop_enum_list props[] = {
		{ __builtin_ffs(DRM_MODE_ROTATE_0) - 1,   "rotate-0" },
		{ __builtin_ffs(DRM_MODE_ROTATE_90) - 1,  "rotate-90" },
		{ __builtin_ffs(DRM_MODE_ROTATE_180) - 1, "rotate-180" },
		{ __builtin_ffs(DRM_MODE_ROTATE_270) - 1, "rotate-270" },
		{ __builtin_ffs(DRM_MODE_REFLECT_X) - 1,  "reflect-x" },
		{ __builtin_ffs(DRM_MODE_REFLECT_Y) - 1,  "reflect-y" },
	};
	struct drm_property *prop;

	WARN_ON((supported_rotations & DRM_MODE_ROTATE_MASK) == 0);
	WARN_ON(!is_power_of_2(rotation & DRM_MODE_ROTATE_MASK));
	WARN_ON(rotation & ~supported_rotations);

	prop = drm_property_create_bitmask(plane->dev, 0, "rotation",
					   props, ARRAY_SIZE(props),
					   supported_rotations);
	if (!prop)
		return -ENOMEM;

	drm_object_attach_property(&plane->base, prop, rotation);

	if (plane->state)
		plane->state->rotation = rotation;

	plane->rotation_property = prop;

	return 0;
}
EXPORT_SYMBOL(drm_plane_create_rotation_property);

/**
 * drm_rotation_simplify() - Try to simplify the rotation
 * @rotation: Rotation to be simplified
 * @supported_rotations: Supported rotations
 *
 * Attempt to simplify the rotation to a form that is supported.
 * Eg. if the hardware supports everything except DRM_MODE_REFLECT_X
 * one could call this function like this:
 *
 * drm_rotation_simplify(rotation, DRM_MODE_ROTATE_0 |
 *                       DRM_MODE_ROTATE_90 | DRM_MODE_ROTATE_180 |
 *                       DRM_MODE_ROTATE_270 | DRM_MODE_REFLECT_Y);
 *
 * to eliminate the DRM_MODE_ROTATE_X flag. Depending on what kind of
 * transforms the hardware supports, this function may not
 * be able to produce a supported transform, so the caller should
 * check the result afterwards.
 */
unsigned int drm_rotation_simplify(unsigned int rotation,
				   unsigned int supported_rotations)
{
	if (rotation & ~supported_rotations) {
		rotation ^= DRM_MODE_REFLECT_X | DRM_MODE_REFLECT_Y;
		rotation = (rotation & DRM_MODE_REFLECT_MASK) |
		           BIT((ffs(rotation & DRM_MODE_ROTATE_MASK) + 1)
		           % 4);
	}

	return rotation;
}
EXPORT_SYMBOL(drm_rotation_simplify);

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
 * calculate &drm_plane_state.normalized_zpos values. Usually min should be set
 * to 0 and max to maximal number of planes for given crtc - 1.
 *
 * If zpos of some planes cannot be changed (like fixed background or
 * cursor/topmost planes), driver should adjust min/max values and assign those
 * planes immutable zpos property with lower or higher values (for more
 * information, see drm_plane_create_zpos_immutable_property() function). In such
 * case driver should also assign proper initial zpos values for all planes in
 * its plane_reset() callback, so the planes will be always sorted properly.
 *
 * See also drm_atomic_normalize_zpos().
 *
 * The property exposed to userspace is called "zpos".
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
 * order. For mutable zpos see drm_plane_create_zpos_property().
 *
 * The property exposed to userspace is called "zpos".
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

	states = kmalloc_array(total_planes, sizeof(*states), GFP_KERNEL);
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
 * drm_atomic_normalize_zpos - calculate normalized zpos values for all crtcs
 * @dev: DRM device
 * @state: atomic state of DRM device
 *
 * This function calculates normalized zpos value for all modified planes in
 * the provided atomic state of DRM device.
 *
 * For every CRTC this function checks new states of all planes assigned to
 * it and calculates normalized zpos value for these planes. Planes are compared
 * first by their zpos values, then by plane id (if zpos is equal). The plane
 * with lowest zpos value is at the bottom. The &drm_plane_state.normalized_zpos
 * is then filled with unique values from 0 to number of active planes in crtc
 * minus one.
 *
 * RETURNS
 * Zero for success or -errno
 */
int drm_atomic_normalize_zpos(struct drm_device *dev,
			      struct drm_atomic_state *state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	struct drm_plane *plane;
	struct drm_plane_state *old_plane_state, *new_plane_state;
	int i, ret = 0;

	for_each_oldnew_plane_in_state(state, plane, old_plane_state, new_plane_state, i) {
		crtc = new_plane_state->crtc;
		if (!crtc)
			continue;
		if (old_plane_state->zpos != new_plane_state->zpos) {
			new_crtc_state = drm_atomic_get_new_crtc_state(state, crtc);
			new_crtc_state->zpos_changed = true;
		}
	}

	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state, new_crtc_state, i) {
		if (old_crtc_state->plane_mask != new_crtc_state->plane_mask ||
		    new_crtc_state->zpos_changed) {
			ret = drm_atomic_helper_crtc_normalize_zpos(crtc,
								    new_crtc_state);
			if (ret)
				return ret;
		}
	}
	return 0;
}
EXPORT_SYMBOL(drm_atomic_normalize_zpos);
