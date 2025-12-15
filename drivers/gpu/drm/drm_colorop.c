// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2023 Advanced Micro Devices, Inc. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include <drm/drm_colorop.h>
#include <drm/drm_print.h>
#include <drm/drm_drv.h>
#include <drm/drm_plane.h>

#include "drm_crtc_internal.h"

/**
 * DOC: overview
 *
 * When userspace signals the &DRM_CLIENT_CAP_PLANE_COLOR_PIPELINE it
 * should use the COLOR_PIPELINE plane property and associated colorops
 * for any color operation on the &drm_plane. Setting of all old color
 * properties, such as COLOR_ENCODING and COLOR_RANGE, will be rejected
 * and the values of the properties will be ignored.
 *
 * Colorops are only advertised and valid for atomic drivers and atomic
 * userspace that signals the &DRM_CLIENT_CAP_PLANE_COLOR_PIPELINE
 * client cap.
 *
 * A colorop represents a single color operation. Colorops are chained
 * via the NEXT property and make up color pipelines. Color pipelines
 * are advertised and selected via the COLOR_PIPELINE &drm_plane
 * property.
 *
 * A colorop will be of a certain type, advertised by the read-only TYPE
 * property. Each type of colorop will advertise a different set of
 * properties and is programmed in a different manner. Types can be
 * enumerated 1D curves, 1D LUTs, 3D LUTs, matrices, etc. See the
 * &drm_colorop_type documentation for information on each type.
 *
 * If a colorop advertises the BYPASS property it can be bypassed.
 *
 * Information about colorop and color pipeline design decisions can be
 * found at rfc/color_pipeline.rst, but note that this document will
 * grow stale over time.
 */

static const struct drm_prop_enum_list drm_colorop_type_enum_list[] = {
	{ DRM_COLOROP_1D_CURVE, "1D Curve" },
	{ DRM_COLOROP_1D_LUT, "1D LUT" },
	{ DRM_COLOROP_CTM_3X4, "3x4 Matrix"},
	{ DRM_COLOROP_MULTIPLIER, "Multiplier"},
	{ DRM_COLOROP_3D_LUT, "3D LUT"},
};

static const char * const colorop_curve_1d_type_names[] = {
	[DRM_COLOROP_1D_CURVE_SRGB_EOTF] = "sRGB EOTF",
	[DRM_COLOROP_1D_CURVE_SRGB_INV_EOTF] = "sRGB Inverse EOTF",
	[DRM_COLOROP_1D_CURVE_PQ_125_EOTF] = "PQ 125 EOTF",
	[DRM_COLOROP_1D_CURVE_PQ_125_INV_EOTF] = "PQ 125 Inverse EOTF",
	[DRM_COLOROP_1D_CURVE_BT2020_INV_OETF] = "BT.2020 Inverse OETF",
	[DRM_COLOROP_1D_CURVE_BT2020_OETF] = "BT.2020 OETF",
	[DRM_COLOROP_1D_CURVE_GAMMA22] = "Gamma 2.2",
	[DRM_COLOROP_1D_CURVE_GAMMA22_INV] = "Gamma 2.2 Inverse",
};

static const struct drm_prop_enum_list drm_colorop_lut1d_interpolation_list[] = {
	{ DRM_COLOROP_LUT1D_INTERPOLATION_LINEAR, "Linear" },
};


static const struct drm_prop_enum_list drm_colorop_lut3d_interpolation_list[] = {
	{ DRM_COLOROP_LUT3D_INTERPOLATION_TETRAHEDRAL, "Tetrahedral" },
};

/* Init Helpers */

static int drm_plane_colorop_init(struct drm_device *dev, struct drm_colorop *colorop,
				  struct drm_plane *plane, enum drm_colorop_type type,
				  uint32_t flags)
{
	struct drm_mode_config *config = &dev->mode_config;
	struct drm_property *prop;
	int ret = 0;

	ret = drm_mode_object_add(dev, &colorop->base, DRM_MODE_OBJECT_COLOROP);
	if (ret)
		return ret;

	colorop->base.properties = &colorop->properties;
	colorop->dev = dev;
	colorop->type = type;
	colorop->plane = plane;
	colorop->next = NULL;

	list_add_tail(&colorop->head, &config->colorop_list);
	colorop->index = config->num_colorop++;

	/* add properties */

	/* type */
	prop = drm_property_create_enum(dev,
					DRM_MODE_PROP_IMMUTABLE,
					"TYPE", drm_colorop_type_enum_list,
					ARRAY_SIZE(drm_colorop_type_enum_list));

	if (!prop)
		return -ENOMEM;

	colorop->type_property = prop;

	drm_object_attach_property(&colorop->base,
				   colorop->type_property,
				   colorop->type);

	if (flags & DRM_COLOROP_FLAG_ALLOW_BYPASS) {
		/* bypass */
		prop = drm_property_create_bool(dev, DRM_MODE_PROP_ATOMIC,
						"BYPASS");
		if (!prop)
			return -ENOMEM;

		colorop->bypass_property = prop;
		drm_object_attach_property(&colorop->base,
					colorop->bypass_property,
					1);
	}

	/* next */
	prop = drm_property_create_object(dev, DRM_MODE_PROP_IMMUTABLE | DRM_MODE_PROP_ATOMIC,
					  "NEXT", DRM_MODE_OBJECT_COLOROP);
	if (!prop)
		return -ENOMEM;
	colorop->next_property = prop;
	drm_object_attach_property(&colorop->base,
				   colorop->next_property,
				   0);

	return ret;
}

/**
 * drm_colorop_cleanup - Cleanup a drm_colorop object in color_pipeline
 *
 * @colorop: The drm_colorop object to be cleaned
 */
void drm_colorop_cleanup(struct drm_colorop *colorop)
{
	struct drm_device *dev = colorop->dev;
	struct drm_mode_config *config = &dev->mode_config;

	list_del(&colorop->head);
	config->num_colorop--;

	if (colorop->state && colorop->state->data) {
		drm_property_blob_put(colorop->state->data);
		colorop->state->data = NULL;
	}

	kfree(colorop->state);
}
EXPORT_SYMBOL(drm_colorop_cleanup);

/**
 * drm_colorop_pipeline_destroy - Helper for color pipeline destruction
 *
 * @dev: - The drm_device containing the drm_planes with the color_pipelines
 *
 * Provides a default color pipeline destroy handler for drm_device.
 */
void drm_colorop_pipeline_destroy(struct drm_device *dev)
{
	struct drm_mode_config *config = &dev->mode_config;
	struct drm_colorop *colorop, *next;

	list_for_each_entry_safe(colorop, next, &config->colorop_list, head) {
		drm_colorop_cleanup(colorop);
		kfree(colorop);
	}
}
EXPORT_SYMBOL(drm_colorop_pipeline_destroy);

/**
 * drm_plane_colorop_curve_1d_init - Initialize a DRM_COLOROP_1D_CURVE
 *
 * @dev: DRM device
 * @colorop: The drm_colorop object to initialize
 * @plane: The associated drm_plane
 * @supported_tfs: A bitfield of supported drm_plane_colorop_curve_1d_init enum values,
 *                 created using BIT(curve_type) and combined with the OR '|'
 *                 operator.
 * @flags: bitmask of misc, see DRM_COLOROP_FLAG_* defines.
 * @return zero on success, -E value on failure
 */
int drm_plane_colorop_curve_1d_init(struct drm_device *dev, struct drm_colorop *colorop,
				    struct drm_plane *plane, u64 supported_tfs, uint32_t flags)
{
	struct drm_prop_enum_list enum_list[DRM_COLOROP_1D_CURVE_COUNT];
	int i, len;

	struct drm_property *prop;
	int ret;

	if (!supported_tfs) {
		drm_err(dev,
			"No supported TFs for new 1D curve colorop on [PLANE:%d:%s]\n",
			plane->base.id, plane->name);
		return -EINVAL;
	}

	if ((supported_tfs & -BIT(DRM_COLOROP_1D_CURVE_COUNT)) != 0) {
		drm_err(dev, "Unknown TF provided on [PLANE:%d:%s]\n",
			plane->base.id, plane->name);
		return -EINVAL;
	}

	ret = drm_plane_colorop_init(dev, colorop, plane, DRM_COLOROP_1D_CURVE, flags);
	if (ret)
		return ret;

	len = 0;
	for (i = 0; i < DRM_COLOROP_1D_CURVE_COUNT; i++) {
		if ((supported_tfs & BIT(i)) == 0)
			continue;

		enum_list[len].type = i;
		enum_list[len].name = colorop_curve_1d_type_names[i];
		len++;
	}

	if (WARN_ON(len <= 0))
		return -EINVAL;

	/* initialize 1D curve only attribute */
	prop = drm_property_create_enum(dev, DRM_MODE_PROP_ATOMIC, "CURVE_1D_TYPE",
					enum_list, len);

	if (!prop)
		return -ENOMEM;

	colorop->curve_1d_type_property = prop;
	drm_object_attach_property(&colorop->base, colorop->curve_1d_type_property,
				   enum_list[0].type);
	drm_colorop_reset(colorop);

	return 0;
}
EXPORT_SYMBOL(drm_plane_colorop_curve_1d_init);

static int drm_colorop_create_data_prop(struct drm_device *dev, struct drm_colorop *colorop)
{
	struct drm_property *prop;

	/* data */
	prop = drm_property_create(dev, DRM_MODE_PROP_ATOMIC | DRM_MODE_PROP_BLOB,
				   "DATA", 0);
	if (!prop)
		return -ENOMEM;

	colorop->data_property = prop;
	drm_object_attach_property(&colorop->base,
				   colorop->data_property,
				   0);

	return 0;
}

/**
 * drm_plane_colorop_curve_1d_lut_init - Initialize a DRM_COLOROP_1D_LUT
 *
 * @dev: DRM device
 * @colorop: The drm_colorop object to initialize
 * @plane: The associated drm_plane
 * @lut_size: LUT size supported by driver
 * @interpolation: 1D LUT interpolation type
 * @flags: bitmask of misc, see DRM_COLOROP_FLAG_* defines.
 * @return zero on success, -E value on failure
 */
int drm_plane_colorop_curve_1d_lut_init(struct drm_device *dev, struct drm_colorop *colorop,
					struct drm_plane *plane, uint32_t lut_size,
					enum drm_colorop_lut1d_interpolation_type interpolation,
					uint32_t flags)
{
	struct drm_property *prop;
	int ret;

	ret = drm_plane_colorop_init(dev, colorop, plane, DRM_COLOROP_1D_LUT, flags);
	if (ret)
		return ret;

	/* initialize 1D LUT only attribute */
	/* LUT size */
	prop = drm_property_create_range(dev, DRM_MODE_PROP_IMMUTABLE | DRM_MODE_PROP_ATOMIC,
					 "SIZE", 0, UINT_MAX);
	if (!prop)
		return -ENOMEM;

	colorop->size_property = prop;
	drm_object_attach_property(&colorop->base, colorop->size_property, lut_size);
	colorop->size = lut_size;

	/* interpolation */
	prop = drm_property_create_enum(dev, 0, "LUT1D_INTERPOLATION",
					drm_colorop_lut1d_interpolation_list,
					ARRAY_SIZE(drm_colorop_lut1d_interpolation_list));
	if (!prop)
		return -ENOMEM;

	colorop->lut1d_interpolation_property = prop;
	drm_object_attach_property(&colorop->base, prop, interpolation);
	colorop->lut1d_interpolation = interpolation;

	/* data */
	ret = drm_colorop_create_data_prop(dev, colorop);
	if (ret)
		return ret;

	drm_colorop_reset(colorop);

	return 0;
}
EXPORT_SYMBOL(drm_plane_colorop_curve_1d_lut_init);

int drm_plane_colorop_ctm_3x4_init(struct drm_device *dev, struct drm_colorop *colorop,
				   struct drm_plane *plane, uint32_t flags)
{
	int ret;

	ret = drm_plane_colorop_init(dev, colorop, plane, DRM_COLOROP_CTM_3X4, flags);
	if (ret)
		return ret;

	ret = drm_colorop_create_data_prop(dev, colorop);
	if (ret)
		return ret;

	drm_colorop_reset(colorop);

	return 0;
}
EXPORT_SYMBOL(drm_plane_colorop_ctm_3x4_init);

/**
 * drm_plane_colorop_mult_init - Initialize a DRM_COLOROP_MULTIPLIER
 *
 * @dev: DRM device
 * @colorop: The drm_colorop object to initialize
 * @plane: The associated drm_plane
 * @flags: bitmask of misc, see DRM_COLOROP_FLAG_* defines.
 * @return zero on success, -E value on failure
 */
int drm_plane_colorop_mult_init(struct drm_device *dev, struct drm_colorop *colorop,
				struct drm_plane *plane, uint32_t flags)
{
	struct drm_property *prop;
	int ret;

	ret = drm_plane_colorop_init(dev, colorop, plane, DRM_COLOROP_MULTIPLIER, flags);
	if (ret)
		return ret;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC, "MULTIPLIER", 0, U64_MAX);
	if (!prop)
		return -ENOMEM;

	colorop->multiplier_property = prop;
	drm_object_attach_property(&colorop->base, colorop->multiplier_property, 0);

	drm_colorop_reset(colorop);

	return 0;
}
EXPORT_SYMBOL(drm_plane_colorop_mult_init);

int drm_plane_colorop_3dlut_init(struct drm_device *dev, struct drm_colorop *colorop,
				 struct drm_plane *plane,
				 uint32_t lut_size,
				 enum drm_colorop_lut3d_interpolation_type interpolation,
				 uint32_t flags)
{
	struct drm_property *prop;
	int ret;

	ret = drm_plane_colorop_init(dev, colorop, plane, DRM_COLOROP_3D_LUT, flags);
	if (ret)
		return ret;

	/* LUT size */
	prop = drm_property_create_range(dev, DRM_MODE_PROP_IMMUTABLE  | DRM_MODE_PROP_ATOMIC,
					 "SIZE", 0, UINT_MAX);
	if (!prop)
		return -ENOMEM;

	colorop->size_property = prop;
	drm_object_attach_property(&colorop->base, colorop->size_property, lut_size);
	colorop->size = lut_size;

	/* interpolation */
	prop = drm_property_create_enum(dev, 0, "LUT3D_INTERPOLATION",
					drm_colorop_lut3d_interpolation_list,
					ARRAY_SIZE(drm_colorop_lut3d_interpolation_list));
	if (!prop)
		return -ENOMEM;

	colorop->lut3d_interpolation_property = prop;
	drm_object_attach_property(&colorop->base, prop, interpolation);
	colorop->lut3d_interpolation = interpolation;

	/* data */
	ret = drm_colorop_create_data_prop(dev, colorop);
	if (ret)
		return ret;

	drm_colorop_reset(colorop);

	return 0;
}
EXPORT_SYMBOL(drm_plane_colorop_3dlut_init);

static void __drm_atomic_helper_colorop_duplicate_state(struct drm_colorop *colorop,
							struct drm_colorop_state *state)
{
	memcpy(state, colorop->state, sizeof(*state));

	if (state->data)
		drm_property_blob_get(state->data);

	state->bypass = true;
}

struct drm_colorop_state *
drm_atomic_helper_colorop_duplicate_state(struct drm_colorop *colorop)
{
	struct drm_colorop_state *state;

	if (WARN_ON(!colorop->state))
		return NULL;

	state = kmalloc(sizeof(*state), GFP_KERNEL);
	if (state)
		__drm_atomic_helper_colorop_duplicate_state(colorop, state);

	return state;
}

void drm_colorop_atomic_destroy_state(struct drm_colorop *colorop,
				      struct drm_colorop_state *state)
{
	kfree(state);
}

/**
 * __drm_colorop_state_reset - resets colorop state to default values
 * @colorop_state: atomic colorop state, must not be NULL
 * @colorop: colorop object, must not be NULL
 *
 * Initializes the newly allocated @colorop_state with default
 * values. This is useful for drivers that subclass the CRTC state.
 */
static void __drm_colorop_state_reset(struct drm_colorop_state *colorop_state,
				      struct drm_colorop *colorop)
{
	u64 val;

	colorop_state->colorop = colorop;
	colorop_state->bypass = true;

	if (colorop->curve_1d_type_property) {
		drm_object_property_get_default_value(&colorop->base,
						      colorop->curve_1d_type_property,
						      &val);
		colorop_state->curve_1d_type = val;
	}
}

/**
 * __drm_colorop_reset - reset state on colorop
 * @colorop: drm colorop
 * @colorop_state: colorop state to assign
 *
 * Initializes the newly allocated @colorop_state and assigns it to
 * the &drm_crtc->state pointer of @colorop, usually required when
 * initializing the drivers or when called from the &drm_colorop_funcs.reset
 * hook.
 *
 * This is useful for drivers that subclass the colorop state.
 */
static void __drm_colorop_reset(struct drm_colorop *colorop,
				struct drm_colorop_state *colorop_state)
{
	if (colorop_state)
		__drm_colorop_state_reset(colorop_state, colorop);

	colorop->state = colorop_state;
}

void drm_colorop_reset(struct drm_colorop *colorop)
{
	kfree(colorop->state);
	colorop->state = kzalloc(sizeof(*colorop->state), GFP_KERNEL);

	if (colorop->state)
		__drm_colorop_reset(colorop, colorop->state);
}

static const char * const colorop_type_name[] = {
	[DRM_COLOROP_1D_CURVE] = "1D Curve",
	[DRM_COLOROP_1D_LUT] = "1D LUT",
	[DRM_COLOROP_CTM_3X4] = "3x4 Matrix",
	[DRM_COLOROP_MULTIPLIER] = "Multiplier",
	[DRM_COLOROP_3D_LUT] = "3D LUT",
};

static const char * const colorop_lu3d_interpolation_name[] = {
	[DRM_COLOROP_LUT3D_INTERPOLATION_TETRAHEDRAL] = "Tetrahedral",
};

static const char * const colorop_lut1d_interpolation_name[] = {
	[DRM_COLOROP_LUT1D_INTERPOLATION_LINEAR] = "Linear",
};

const char *drm_get_colorop_type_name(enum drm_colorop_type type)
{
	if (WARN_ON(type >= ARRAY_SIZE(colorop_type_name)))
		return "unknown";

	return colorop_type_name[type];
}

const char *drm_get_colorop_curve_1d_type_name(enum drm_colorop_curve_1d_type type)
{
	if (WARN_ON(type >= ARRAY_SIZE(colorop_curve_1d_type_names)))
		return "unknown";

	return colorop_curve_1d_type_names[type];
}

/**
 * drm_get_colorop_lut1d_interpolation_name: return a string for interpolation type
 * @type: interpolation type to compute name of
 *
 * In contrast to the other drm_get_*_name functions this one here returns a
 * const pointer and hence is threadsafe.
 */
const char *drm_get_colorop_lut1d_interpolation_name(enum drm_colorop_lut1d_interpolation_type type)
{
	if (WARN_ON(type >= ARRAY_SIZE(colorop_lut1d_interpolation_name)))
		return "unknown";

	return colorop_lut1d_interpolation_name[type];
}

/**
 * drm_get_colorop_lut3d_interpolation_name - return a string for interpolation type
 * @type: interpolation type to compute name of
 *
 * In contrast to the other drm_get_*_name functions this one here returns a
 * const pointer and hence is threadsafe.
 */
const char *drm_get_colorop_lut3d_interpolation_name(enum drm_colorop_lut3d_interpolation_type type)
{
	if (WARN_ON(type >= ARRAY_SIZE(colorop_lu3d_interpolation_name)))
		return "unknown";

	return colorop_lu3d_interpolation_name[type];
}

/**
 * drm_colorop_set_next_property - sets the next pointer
 * @colorop: drm colorop
 * @next: next colorop
 *
 * Should be used when constructing the color pipeline
 */
void drm_colorop_set_next_property(struct drm_colorop *colorop, struct drm_colorop *next)
{
	drm_object_property_set_value(&colorop->base,
				      colorop->next_property,
				      next ? next->base.id : 0);
	colorop->next = next;
}
EXPORT_SYMBOL(drm_colorop_set_next_property);
