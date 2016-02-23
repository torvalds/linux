/*
 * Copyright (C) STMicroelectronics SA 2014
 * Authors: Benjamin Gaignard <benjamin.gaignard@st.com>
 *          Fabien Dessenne <fabien.dessenne@st.com>
 *          for STMicroelectronics.
 * License terms:  GNU General Public License (GPL), version 2
 */

#include <drm/drmP.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>

#include "sti_compositor.h"
#include "sti_drv.h"
#include "sti_plane.h"

/* (Background) < GDP0 < GDP1 < HQVDP0 < GDP2 < GDP3 < (ForeGround) */
enum sti_plane_desc sti_plane_default_zorder[] = {
	STI_GDP_0,
	STI_GDP_1,
	STI_HQVDP_0,
	STI_GDP_2,
	STI_GDP_3,
};

const char *sti_plane_to_str(struct sti_plane *plane)
{
	switch (plane->desc) {
	case STI_GDP_0:
		return "GDP0";
	case STI_GDP_1:
		return "GDP1";
	case STI_GDP_2:
		return "GDP2";
	case STI_GDP_3:
		return "GDP3";
	case STI_HQVDP_0:
		return "HQVDP0";
	case STI_CURSOR:
		return "CURSOR";
	default:
		return "<UNKNOWN PLANE>";
	}
}

static void sti_plane_destroy(struct drm_plane *drm_plane)
{
	DRM_DEBUG_DRIVER("\n");

	drm_plane_helper_disable(drm_plane);
	drm_plane_cleanup(drm_plane);
}

static int sti_plane_set_property(struct drm_plane *drm_plane,
				  struct drm_property *property,
				  uint64_t val)
{
	struct drm_device *dev = drm_plane->dev;
	struct sti_private *private = dev->dev_private;
	struct sti_plane *plane = to_sti_plane(drm_plane);

	DRM_DEBUG_DRIVER("\n");

	if (property == private->plane_zorder_property) {
		plane->zorder = val;
		return 0;
	}

	return -EINVAL;
}

static void sti_plane_attach_zorder_property(struct drm_plane *drm_plane)
{
	struct drm_device *dev = drm_plane->dev;
	struct sti_private *private = dev->dev_private;
	struct sti_plane *plane = to_sti_plane(drm_plane);
	struct drm_property *prop;

	prop = private->plane_zorder_property;
	if (!prop) {
		prop = drm_property_create_range(dev, 0, "zpos", 1,
						 GAM_MIXER_NB_DEPTH_LEVEL);
		if (!prop)
			return;

		private->plane_zorder_property = prop;
	}

	drm_object_attach_property(&drm_plane->base, prop, plane->zorder);
}

void sti_plane_init_property(struct sti_plane *plane,
			     enum drm_plane_type type)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(sti_plane_default_zorder); i++)
		if (sti_plane_default_zorder[i] == plane->desc)
			break;

	plane->zorder = i + 1;

	if (type == DRM_PLANE_TYPE_OVERLAY)
		sti_plane_attach_zorder_property(&plane->drm_plane);

	DRM_DEBUG_DRIVER("drm plane:%d mapped to %s with zorder:%d\n",
			 plane->drm_plane.base.id,
			 sti_plane_to_str(plane), plane->zorder);
}

struct drm_plane_funcs sti_plane_helpers_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = sti_plane_destroy,
	.set_property = sti_plane_set_property,
	.reset = drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
};
