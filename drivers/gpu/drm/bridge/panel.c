/*
 * Copyright (C) 2016 Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 * Copyright (C) 2017 Broadcom
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include <drm/drmP.h>
#include <drm/drm_panel.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_encoder.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_panel.h>

struct panel_bridge {
	struct drm_bridge bridge;
	struct drm_connector connector;
	struct drm_panel *panel;
	u32 connector_type;
};

static inline struct panel_bridge *
drm_bridge_to_panel_bridge(struct drm_bridge *bridge)
{
	return container_of(bridge, struct panel_bridge, bridge);
}

static inline struct panel_bridge *
drm_connector_to_panel_bridge(struct drm_connector *connector)
{
	return container_of(connector, struct panel_bridge, connector);
}

static int panel_bridge_connector_get_modes(struct drm_connector *connector)
{
	struct panel_bridge *panel_bridge =
		drm_connector_to_panel_bridge(connector);

	return drm_panel_get_modes(panel_bridge->panel);
}

static const struct drm_connector_helper_funcs
panel_bridge_connector_helper_funcs = {
	.get_modes = panel_bridge_connector_get_modes,
};

static const struct drm_connector_funcs panel_bridge_connector_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int panel_bridge_attach(struct drm_bridge *bridge)
{
	struct panel_bridge *panel_bridge = drm_bridge_to_panel_bridge(bridge);
	struct drm_connector *connector = &panel_bridge->connector;
	int ret;

	if (!bridge->encoder) {
		DRM_ERROR("Missing encoder\n");
		return -ENODEV;
	}

	drm_connector_helper_add(connector,
				 &panel_bridge_connector_helper_funcs);

	ret = drm_connector_init(bridge->dev, connector,
				 &panel_bridge_connector_funcs,
				 panel_bridge->connector_type);
	if (ret) {
		DRM_ERROR("Failed to initialize connector\n");
		return ret;
	}

	drm_connector_attach_encoder(&panel_bridge->connector,
					  bridge->encoder);

	ret = drm_panel_attach(panel_bridge->panel, &panel_bridge->connector);
	if (ret < 0)
		return ret;

	return 0;
}

static void panel_bridge_detach(struct drm_bridge *bridge)
{
	struct panel_bridge *panel_bridge = drm_bridge_to_panel_bridge(bridge);

	drm_panel_detach(panel_bridge->panel);
}

static void panel_bridge_pre_enable(struct drm_bridge *bridge)
{
	struct panel_bridge *panel_bridge = drm_bridge_to_panel_bridge(bridge);

	drm_panel_prepare(panel_bridge->panel);
}

static void panel_bridge_enable(struct drm_bridge *bridge)
{
	struct panel_bridge *panel_bridge = drm_bridge_to_panel_bridge(bridge);

	drm_panel_enable(panel_bridge->panel);
}

static void panel_bridge_disable(struct drm_bridge *bridge)
{
	struct panel_bridge *panel_bridge = drm_bridge_to_panel_bridge(bridge);

	drm_panel_disable(panel_bridge->panel);
}

static void panel_bridge_post_disable(struct drm_bridge *bridge)
{
	struct panel_bridge *panel_bridge = drm_bridge_to_panel_bridge(bridge);

	drm_panel_unprepare(panel_bridge->panel);
}

static const struct drm_bridge_funcs panel_bridge_bridge_funcs = {
	.attach = panel_bridge_attach,
	.detach = panel_bridge_detach,
	.pre_enable = panel_bridge_pre_enable,
	.enable = panel_bridge_enable,
	.disable = panel_bridge_disable,
	.post_disable = panel_bridge_post_disable,
};

/**
 * drm_panel_bridge_add - Creates a drm_bridge and drm_connector that
 * just calls the appropriate functions from drm_panel.
 *
 * @panel: The drm_panel being wrapped.  Must be non-NULL.
 * @connector_type: The DRM_MODE_CONNECTOR_* for the connector to be
 * created.
 *
 * For drivers converting from directly using drm_panel: The expected
 * usage pattern is that during either encoder module probe or DSI
 * host attach, a drm_panel will be looked up through
 * drm_of_find_panel_or_bridge().  drm_panel_bridge_add() is used to
 * wrap that panel in the new bridge, and the result can then be
 * passed to drm_bridge_attach().  The drm_panel_prepare() and related
 * functions can be dropped from the encoder driver (they're now
 * called by the KMS helpers before calling into the encoder), along
 * with connector creation.  When done with the bridge,
 * drm_bridge_detach() should be called as normal, then
 * drm_panel_bridge_remove() to free it.
 */
struct drm_bridge *drm_panel_bridge_add(struct drm_panel *panel,
					u32 connector_type)
{
	struct panel_bridge *panel_bridge;

	if (!panel)
		return ERR_PTR(-EINVAL);

	panel_bridge = devm_kzalloc(panel->dev, sizeof(*panel_bridge),
				    GFP_KERNEL);
	if (!panel_bridge)
		return ERR_PTR(-ENOMEM);

	panel_bridge->connector_type = connector_type;
	panel_bridge->panel = panel;

	panel_bridge->bridge.funcs = &panel_bridge_bridge_funcs;
#ifdef CONFIG_OF
	panel_bridge->bridge.of_node = panel->dev->of_node;
#endif

	drm_bridge_add(&panel_bridge->bridge);

	return &panel_bridge->bridge;
}
EXPORT_SYMBOL(drm_panel_bridge_add);

/**
 * drm_panel_bridge_remove - Unregisters and frees a drm_bridge
 * created by drm_panel_bridge_add().
 *
 * @bridge: The drm_bridge being freed.
 */
void drm_panel_bridge_remove(struct drm_bridge *bridge)
{
	struct panel_bridge *panel_bridge;

	if (!bridge)
		return;

	if (bridge->funcs != &panel_bridge_bridge_funcs)
		return;

	panel_bridge = drm_bridge_to_panel_bridge(bridge);

	drm_bridge_remove(bridge);
	devm_kfree(panel_bridge->panel->dev, bridge);
}
EXPORT_SYMBOL(drm_panel_bridge_remove);

static void devm_drm_panel_bridge_release(struct device *dev, void *res)
{
	struct drm_bridge **bridge = res;

	drm_panel_bridge_remove(*bridge);
}

struct drm_bridge *devm_drm_panel_bridge_add(struct device *dev,
					     struct drm_panel *panel,
					     u32 connector_type)
{
	struct drm_bridge **ptr, *bridge;

	ptr = devres_alloc(devm_drm_panel_bridge_release, sizeof(*ptr),
			   GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	bridge = drm_panel_bridge_add(panel, connector_type);
	if (!IS_ERR(bridge)) {
		*ptr = bridge;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return bridge;
}
EXPORT_SYMBOL(devm_drm_panel_bridge_add);
