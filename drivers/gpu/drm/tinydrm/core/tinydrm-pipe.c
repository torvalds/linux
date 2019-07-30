// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2016 Noralf Trønnes
 */

#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_modes.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_print.h>
#include <drm/drm_simple_kms_helper.h>

struct tinydrm_connector {
	struct drm_connector base;
	struct drm_display_mode mode;
};

static inline struct tinydrm_connector *
to_tinydrm_connector(struct drm_connector *connector)
{
	return container_of(connector, struct tinydrm_connector, base);
}

static int tinydrm_connector_get_modes(struct drm_connector *connector)
{
	struct tinydrm_connector *tconn = to_tinydrm_connector(connector);
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &tconn->mode);
	if (!mode) {
		DRM_ERROR("Failed to duplicate mode\n");
		return 0;
	}

	if (mode->name[0] == '\0')
		drm_mode_set_name(mode);

	mode->type |= DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	if (mode->width_mm) {
		connector->display_info.width_mm = mode->width_mm;
		connector->display_info.height_mm = mode->height_mm;
	}

	return 1;
}

static const struct drm_connector_helper_funcs tinydrm_connector_hfuncs = {
	.get_modes = tinydrm_connector_get_modes,
};

static enum drm_connector_status
tinydrm_connector_detect(struct drm_connector *connector, bool force)
{
	if (drm_dev_is_unplugged(connector->dev))
		return connector_status_disconnected;

	return connector->status;
}

static void tinydrm_connector_destroy(struct drm_connector *connector)
{
	struct tinydrm_connector *tconn = to_tinydrm_connector(connector);

	drm_connector_cleanup(connector);
	kfree(tconn);
}

static const struct drm_connector_funcs tinydrm_connector_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.detect = tinydrm_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = tinydrm_connector_destroy,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

struct drm_connector *
tinydrm_connector_create(struct drm_device *drm,
			 const struct drm_display_mode *mode,
			 int connector_type)
{
	struct tinydrm_connector *tconn;
	struct drm_connector *connector;
	int ret;

	tconn = kzalloc(sizeof(*tconn), GFP_KERNEL);
	if (!tconn)
		return ERR_PTR(-ENOMEM);

	drm_mode_copy(&tconn->mode, mode);
	connector = &tconn->base;

	drm_connector_helper_add(connector, &tinydrm_connector_hfuncs);
	ret = drm_connector_init(drm, connector, &tinydrm_connector_funcs,
				 connector_type);
	if (ret) {
		kfree(tconn);
		return ERR_PTR(ret);
	}

	connector->status = connector_status_connected;

	return connector;
}

static int tinydrm_rotate_mode(struct drm_display_mode *mode,
			       unsigned int rotation)
{
	if (rotation == 0 || rotation == 180) {
		return 0;
	} else if (rotation == 90 || rotation == 270) {
		swap(mode->hdisplay, mode->vdisplay);
		swap(mode->hsync_start, mode->vsync_start);
		swap(mode->hsync_end, mode->vsync_end);
		swap(mode->htotal, mode->vtotal);
		swap(mode->width_mm, mode->height_mm);
		return 0;
	} else {
		return -EINVAL;
	}
}

/**
 * tinydrm_display_pipe_init - Initialize display pipe
 * @drm: DRM device
 * @pipe: Display pipe
 * @funcs: Display pipe functions
 * @connector_type: Connector type
 * @formats: Array of supported formats (DRM_FORMAT\_\*)
 * @format_count: Number of elements in @formats
 * @mode: Supported mode
 * @rotation: Initial @mode rotation in degrees Counter Clock Wise
 *
 * This function sets up a &drm_simple_display_pipe with a &drm_connector that
 * has one fixed &drm_display_mode which is rotated according to @rotation.
 *
 * Returns:
 * Zero on success, negative error code on failure.
 */
int tinydrm_display_pipe_init(struct drm_device *drm,
			      struct drm_simple_display_pipe *pipe,
			      const struct drm_simple_display_pipe_funcs *funcs,
			      int connector_type,
			      const uint32_t *formats,
			      unsigned int format_count,
			      const struct drm_display_mode *mode,
			      unsigned int rotation)
{
	struct drm_display_mode mode_copy;
	struct drm_connector *connector;
	int ret;
	static const uint64_t modifiers[] = {
		DRM_FORMAT_MOD_LINEAR,
		DRM_FORMAT_MOD_INVALID
	};

	drm_mode_copy(&mode_copy, mode);
	ret = tinydrm_rotate_mode(&mode_copy, rotation);
	if (ret) {
		DRM_ERROR("Illegal rotation value %u\n", rotation);
		return -EINVAL;
	}

	drm->mode_config.min_width = mode_copy.hdisplay;
	drm->mode_config.max_width = mode_copy.hdisplay;
	drm->mode_config.min_height = mode_copy.vdisplay;
	drm->mode_config.max_height = mode_copy.vdisplay;

	connector = tinydrm_connector_create(drm, &mode_copy, connector_type);
	if (IS_ERR(connector))
		return PTR_ERR(connector);

	return drm_simple_display_pipe_init(drm, pipe, funcs, formats,
					    format_count, modifiers, connector);
}
EXPORT_SYMBOL(tinydrm_display_pipe_init);
