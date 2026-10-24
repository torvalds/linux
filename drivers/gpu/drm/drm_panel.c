// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013, NVIDIA Corporation.  All rights reserved.
 * Copyright (C) 2016 Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 * Copyright (C) 2017 Broadcom
 */

#include <linux/backlight.h>
#include <linux/debugfs.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/of.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_encoder.h>
#include <drm/drm_managed.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

static DEFINE_MUTEX(panel_lock);
static LIST_HEAD(panel_list);

/**
 * DOC: drm panel
 *
 * The DRM panel helpers allow drivers to register panel objects with a
 * central registry and provide functions to retrieve those panels in display
 * drivers.
 *
 * For easy integration into drivers using the &drm_bridge infrastructure please
 * take look at drm_panel_bridge_add() and devm_drm_panel_bridge_add().
 */

static inline struct drm_panel *
drm_bridge_to_panel(const struct drm_bridge *bridge)
{
	return container_of(bridge, struct drm_panel, bridge);
}

static inline struct drm_panel *
drm_connector_to_panel(const struct drm_connector *connector)
{
	return container_of(connector, struct drm_panel, connector);
}

struct panel_bridge {
	struct drm_bridge bridge;
	struct drm_connector connector;
	struct drm_panel *panel;
	u32 connector_type;
};

static int panel_bridge_connector_get_modes(struct drm_connector *connector)
{
	struct drm_panel *panel = drm_connector_to_panel(connector);

	return drm_panel_get_modes(panel, connector);
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

static int panel_bridge_attach(struct drm_bridge *bridge,
			       struct drm_encoder *encoder,
			       enum drm_bridge_attach_flags flags)
{
	struct drm_panel *panel = drm_bridge_to_panel(bridge);
	struct drm_connector *connector = &panel->connector;
	int ret;

	if (flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR)
		return 0;

	drm_connector_helper_add(connector,
				 &panel_bridge_connector_helper_funcs);

	ret = drm_connector_init(bridge->dev, connector,
				 &panel_bridge_connector_funcs,
				 panel->connector_type);
	if (ret) {
		DRM_ERROR("Failed to initialize connector\n");
		return ret;
	}

	drm_panel_bridge_set_orientation(connector, bridge);

	drm_connector_attach_encoder(connector, encoder);

	if (bridge->dev->registered) {
		if (connector->funcs->reset)
			connector->funcs->reset(connector);
		drm_connector_register(connector);
	}

	return 0;
}

static void panel_bridge_detach(struct drm_bridge *bridge)
{
	struct drm_panel *panel = drm_bridge_to_panel(bridge);
	struct drm_connector *connector = &panel->connector;

	/* Cleanup the connector if we know it was initialized */
	if (connector->dev)
		drm_connector_cleanup(connector);
}

static void panel_bridge_atomic_pre_enable(struct drm_bridge *bridge,
					   struct drm_atomic_commit *atomic_state)
{
	struct drm_panel *panel = drm_bridge_to_panel(bridge);
	struct drm_encoder *encoder = bridge->encoder;
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state;

	crtc = drm_atomic_get_new_crtc_for_encoder(atomic_state, encoder);
	if (!crtc)
		return;

	old_crtc_state = drm_atomic_get_old_crtc_state(atomic_state, crtc);
	if (old_crtc_state && old_crtc_state->self_refresh_active)
		return;

	drm_panel_prepare(panel);
}

static void panel_bridge_atomic_enable(struct drm_bridge *bridge,
				       struct drm_atomic_commit *atomic_state)
{
	struct drm_panel *panel = drm_bridge_to_panel(bridge);
	struct drm_encoder *encoder = bridge->encoder;
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state;

	crtc = drm_atomic_get_new_crtc_for_encoder(atomic_state, encoder);
	if (!crtc)
		return;

	old_crtc_state = drm_atomic_get_old_crtc_state(atomic_state, crtc);
	if (old_crtc_state && old_crtc_state->self_refresh_active)
		return;

	drm_panel_enable(panel);
}

static void panel_bridge_atomic_disable(struct drm_bridge *bridge,
					struct drm_atomic_commit *atomic_state)
{
	struct drm_panel *panel = drm_bridge_to_panel(bridge);
	struct drm_encoder *encoder = bridge->encoder;
	struct drm_crtc *crtc;
	struct drm_crtc_state *new_crtc_state;

	crtc = drm_atomic_get_old_crtc_for_encoder(atomic_state, encoder);
	if (!crtc)
		return;

	new_crtc_state = drm_atomic_get_new_crtc_state(atomic_state, crtc);
	if (new_crtc_state && new_crtc_state->self_refresh_active)
		return;

	drm_panel_disable(panel);
}

static void panel_bridge_atomic_post_disable(struct drm_bridge *bridge,
					     struct drm_atomic_commit *atomic_state)
{
	struct drm_panel *panel = drm_bridge_to_panel(bridge);
	struct drm_encoder *encoder = bridge->encoder;
	struct drm_crtc *crtc;
	struct drm_crtc_state *new_crtc_state;

	crtc = drm_atomic_get_old_crtc_for_encoder(atomic_state, encoder);
	if (!crtc)
		return;

	new_crtc_state = drm_atomic_get_new_crtc_state(atomic_state, crtc);
	if (new_crtc_state && new_crtc_state->self_refresh_active)
		return;

	drm_panel_unprepare(panel);
}

static int panel_bridge_get_modes(struct drm_bridge *bridge,
				  struct drm_connector *connector)
{
	struct drm_panel *panel = drm_bridge_to_panel(bridge);

	return drm_panel_get_modes(panel, connector);
}

static void panel_bridge_debugfs_init(struct drm_bridge *bridge,
				      struct dentry *root)
{
	struct drm_panel *panel = drm_bridge_to_panel(bridge);

	root = debugfs_create_dir("panel", root);
	if (panel->funcs->debugfs_init)
		panel->funcs->debugfs_init(panel, root);
}

static const struct drm_bridge_funcs panel_bridge_bridge_funcs = {
	.attach = panel_bridge_attach,
	.detach = panel_bridge_detach,
	.atomic_pre_enable = panel_bridge_atomic_pre_enable,
	.atomic_enable = panel_bridge_atomic_enable,
	.atomic_disable = panel_bridge_atomic_disable,
	.atomic_post_disable = panel_bridge_atomic_post_disable,
	.get_modes = panel_bridge_get_modes,
	.atomic_create_state = drm_atomic_helper_bridge_create_state,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_get_input_bus_fmts = drm_atomic_helper_bridge_propagate_bus_fmt,
	.debugfs_init = panel_bridge_debugfs_init,
};

/**
 * drm_bridge_is_panel - Checks if a drm_bridge is a panel_bridge.
 *
 * @bridge: The drm_bridge to be checked.
 *
 * Returns true if the bridge is a panel bridge, or false otherwise.
 */
bool drm_bridge_is_panel(const struct drm_bridge *bridge)
{
	return bridge->funcs == &panel_bridge_bridge_funcs;
}
EXPORT_SYMBOL(drm_bridge_is_panel);

/**
 * drm_panel_bridge_add - Creates a &drm_bridge and &drm_connector that
 * just calls the appropriate functions from &drm_panel.
 *
 * @panel: The drm_panel being wrapped.  Must be non-NULL.
 *
 * This function is deprecated. A drm_bridge is embedded into every panel.
 * Don't use it in new drivers and update your driver to stop using it.
 *
 * For drivers converting from directly using drm_panel: The expected
 * usage pattern is that during either encoder module probe or DSI
 * host attach, a drm_panel will be looked up through
 * drm_of_find_panel_or_bridge().  drm_panel_bridge_add() is used to
 * wrap that panel in the new bridge, and the result can then be
 * passed to drm_bridge_attach().  The drm_panel_prepare() and related
 * functions can be dropped from the encoder driver (they're now
 * called by the KMS helpers before calling into the encoder), along
 * with connector creation.  When done with the bridge (after
 * drm_mode_config_cleanup() if the bridge has already been attached), then
 * drm_panel_bridge_remove() to free it.
 *
 * The connector type is set to @panel->connector_type, which must be set to a
 * known type. Calling this function with a panel whose connector type is
 * DRM_MODE_CONNECTOR_Unknown will return ERR_PTR(-EINVAL).
 *
 * See devm_drm_panel_bridge_add() for an automatically managed version of this
 * function.
 */
struct drm_bridge *drm_panel_bridge_add(struct drm_panel *panel)
{
	if (WARN_ON(panel->connector_type == DRM_MODE_CONNECTOR_Unknown))
		return ERR_PTR(-EINVAL);

	return drm_panel_bridge_add_typed(panel, panel->connector_type);
}
EXPORT_SYMBOL(drm_panel_bridge_add);

/**
 * drm_panel_bridge_add_typed - Pretend to create a &drm_bridge and &drm_connector with
 * an explicit connector type.
 * @panel: The drm_panel being wrapped.  Must be non-NULL.
 * @connector_type: The connector type (DRM_MODE_CONNECTOR_*)
 *
 * This is just like drm_panel_bridge_add(), but forces the connector type to
 * @connector_type instead of infering it from the panel.
 *
 * This function is deprecated. A drm_bridge is embedded into every panel.
 * Don't use it in new drivers and update your driver to stop using it.
 */
struct drm_bridge *drm_panel_bridge_add_typed(struct drm_panel *panel,
					      u32 connector_type)
{
	if (!panel)
		return ERR_PTR(-EINVAL);

	return drm_bridge_get(&panel->bridge);
}
EXPORT_SYMBOL(drm_panel_bridge_add_typed);

/**
 * drm_panel_bridge_remove - Unregisters and frees a drm_bridge
 * created by drm_panel_bridge_add().
 *
 * @bridge: The drm_bridge being freed.
 *
 * This function is deprecated. A drm_bridge is embedded into every panel
 * and removed when the panel is removed.  Don't use it in new drivers and
 * update your driver to stop using it.
 */
void drm_panel_bridge_remove(struct drm_bridge *bridge)
{
	if (!bridge)
		return;

	if (!drm_bridge_is_panel(bridge)) {
		drm_warn(bridge->dev, "%s: called on non-panel bridge!\n", __func__);
		return;
	}

	drm_bridge_put(bridge);
}
EXPORT_SYMBOL(drm_panel_bridge_remove);

/**
 * drm_panel_bridge_set_orientation - Set the connector's panel orientation
 * from the bridge that can be transformed to panel bridge.
 *
 * @connector: The connector to be set panel orientation.
 * @bridge: The drm_bridge whose orientation should be set.
 *
 * Returns 0 on success, negative errno on failure.
 */
int drm_panel_bridge_set_orientation(struct drm_connector *connector,
				     struct drm_bridge *bridge)
{
	struct drm_panel *panel = drm_bridge_to_panel(bridge);

	return drm_connector_set_orientation_from_panel(connector, panel);
}
EXPORT_SYMBOL(drm_panel_bridge_set_orientation);

static void devm_drm_panel_bridge_release(struct device *dev, void *res)
{
	struct drm_bridge *bridge = *(struct drm_bridge **)res;

	if (!bridge)
		return;

	drm_bridge_put(bridge);
}

/**
 * devm_drm_panel_bridge_add - Creates a managed &drm_bridge and &drm_connector
 * that just calls the appropriate functions from &drm_panel.
 * @dev: device to tie the bridge lifetime to
 * @panel: The drm_panel being wrapped.  Must be non-NULL.
 *
 * This function is deprecated. A drm_bridge is embedded into every panel.
 * Don't use it in new drivers and update your driver to stop using it.
 *
 * This is the managed version of drm_panel_bridge_add() which automatically
 * calls drm_panel_bridge_remove() when @dev is unbound.
 */
struct drm_bridge *devm_drm_panel_bridge_add(struct device *dev,
					     struct drm_panel *panel)
{
	if (WARN_ON(panel->connector_type == DRM_MODE_CONNECTOR_Unknown))
		return ERR_PTR(-EINVAL);

	return devm_drm_panel_bridge_add_typed(dev, panel,
					       panel->connector_type);
}
EXPORT_SYMBOL(devm_drm_panel_bridge_add);

/**
 * devm_drm_panel_bridge_add_typed - Creates a managed &drm_bridge and
 * &drm_connector with an explicit connector type.
 * @dev: device to tie the bridge lifetime to
 * @panel: The drm_panel being wrapped.  Must be non-NULL.
 * @connector_type: The connector type (DRM_MODE_CONNECTOR_*)
 *
 * This is just like devm_drm_panel_bridge_add(), but forces the connector type
 * to @connector_type instead of infering it from the panel.
 *
 * This function is deprecated. A drm_bridge is embedded into every panel.
 * Don't use it in new drivers and update your driver to stop using it.
 */
struct drm_bridge *devm_drm_panel_bridge_add_typed(struct device *dev,
						   struct drm_panel *panel,
						   u32 connector_type)
{
	struct drm_bridge **ptr, *bridge;

	ptr = devres_alloc(devm_drm_panel_bridge_release, sizeof(*ptr),
			   GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	bridge = drm_panel_bridge_add_typed(panel, connector_type);
	if (IS_ERR(bridge)) {
		devres_free(ptr);
		return bridge;
	}

	*ptr = bridge;
	devres_add(dev, ptr);

	return &panel->bridge;
}
EXPORT_SYMBOL(devm_drm_panel_bridge_add_typed);

static void drmm_drm_panel_bridge_release(struct drm_device *drm, void *ptr)
{
	struct drm_bridge *bridge = ptr;

	drm_panel_bridge_remove(bridge);
}

/**
 * drmm_panel_bridge_add - Creates a DRM-managed &drm_bridge and
 *                         &drm_connector that just calls the
 *                         appropriate functions from &drm_panel.
 *
 * @drm: DRM device to tie the bridge lifetime to
 * @panel: The drm_panel being wrapped.  Must be non-NULL.
 *
 * This function is deprecated. A drm_bridge is embedded into every panel.
 * Don't use it in new drivers and update your driver to stop using it.
 *
 * This is the DRM-managed version of drm_panel_bridge_add() which
 * automatically calls drm_panel_bridge_remove() when @dev is cleaned
 * up.
 */
struct drm_bridge *drmm_panel_bridge_add(struct drm_device *drm,
					 struct drm_panel *panel)
{
	struct drm_bridge *bridge;
	int ret;

	bridge = drm_panel_bridge_add_typed(panel, panel->connector_type);
	if (IS_ERR(bridge))
		return bridge;

	ret = drmm_add_action_or_reset(drm, drmm_drm_panel_bridge_release,
				       bridge);
	if (ret)
		return ERR_PTR(ret);

	return bridge;
}
EXPORT_SYMBOL(drmm_panel_bridge_add);

/**
 * drm_panel_bridge_connector - return the connector for the panel (for
 * legacy drivers not using DRM_BRIDGE_ATTACH_NO_CONNECTOR)
 * @bridge: The drm_bridge.
 *
 * This function gives external access to the connector.
 *
 * Returns: Pointer to drm_connector
 */
struct drm_connector *drm_panel_bridge_connector(struct drm_bridge *bridge)
{
	struct drm_panel *panel;

	panel = drm_bridge_to_panel(bridge);

	return &panel->connector;
}
EXPORT_SYMBOL(drm_panel_bridge_connector);

#ifdef CONFIG_OF
/**
 * devm_drm_of_get_bridge - Return next bridge in the chain
 * @dev: device to tie the bridge lifetime to
 * @np: device tree node containing encoder output ports
 * @port: port in the device tree node
 * @endpoint: endpoint in the device tree node
 *
 * This function is deprecated. A drm_bridge is embedded into every panel.
 * Don't use it in new drivers and convert your driver to
 * of_drm_get_bridge_by_endpoint().
 *
 * Given a DT node's port and endpoint number, finds the connected node
 * and returns the associated bridge if any, or creates and returns a
 * drm panel bridge instance if a panel is connected.
 *
 * Returns a pointer to the bridge if successful, or an error pointer
 * otherwise.
 */
struct drm_bridge *devm_drm_of_get_bridge(struct device *dev,
					  struct device_node *np,
					  u32 port, u32 endpoint)
{
	struct drm_bridge *bridge;
	struct drm_panel *panel;
	int ret;

	ret = drm_of_find_panel_or_bridge(np, port, endpoint,
					  &panel, &bridge);
	if (ret)
		return ERR_PTR(ret);

	if (panel) {
		bridge = devm_drm_panel_bridge_add(dev, panel);
		drm_panel_put(panel);
	}

	return bridge;
}
EXPORT_SYMBOL(devm_drm_of_get_bridge);

/**
 * drmm_of_get_bridge - Return next bridge in the chain
 * @drm: device to tie the bridge lifetime to
 * @np: device tree node containing encoder output ports
 * @port: port in the device tree node
 * @endpoint: endpoint in the device tree node
 *
 * This function is deprecated. A drm_bridge is embedded into every panel.
 * Don't use it in new drivers and convert your driver to
 * of_drm_get_bridge_by_endpoint().
 *
 * Given a DT node's port and endpoint number, finds the connected node
 * and returns the associated bridge if any, or creates and returns a
 * drm panel bridge instance if a panel is connected.
 *
 * Returns a drmm managed pointer to the bridge if successful, or an error
 * pointer otherwise.
 */
struct drm_bridge *drmm_of_get_bridge(struct drm_device *drm,
				      struct device_node *np,
				      u32 port, u32 endpoint)
{
	struct drm_bridge *bridge;
	struct drm_panel *panel;
	int ret;

	ret = drm_of_find_panel_or_bridge(np, port, endpoint,
					  &panel, &bridge);
	if (ret)
		return ERR_PTR(ret);

	if (panel) {
		bridge = drmm_panel_bridge_add(drm, panel);
		drm_panel_put(panel);
	}

	return bridge;
}
EXPORT_SYMBOL(drmm_of_get_bridge);

#endif

/**
 * drm_panel_init - initialize a panel
 * @panel: DRM panel
 * @dev: parent device of the panel
 * @funcs: panel operations
 * @connector_type: the connector type (DRM_MODE_CONNECTOR_*) corresponding to
 *	the panel interface (must NOT be DRM_MODE_CONNECTOR_Unknown)
 *
 * Initialize the panel structure for subsequent registration with
 * drm_panel_add().
 */
static void drm_panel_init(struct drm_panel *panel, struct device *dev,
			   const struct drm_panel_funcs *funcs,
			   int connector_type)
{
	if (connector_type == DRM_MODE_CONNECTOR_Unknown)
		DRM_WARN("%s: %s: a valid connector type is required!\n", __func__, dev_name(dev));

	INIT_LIST_HEAD(&panel->list);
	INIT_LIST_HEAD(&panel->followers);
	mutex_init(&panel->follower_lock);
	panel->dev = dev;
	panel->funcs = funcs;
	panel->connector_type = connector_type;
}

/**
 * drm_panel_add - add a panel to the global registry
 * @panel: panel to add
 *
 * Add a panel to the global registry so that it can be looked
 * up by display drivers. The panel to be added must have been
 * allocated by devm_drm_panel_alloc().
 */
void drm_panel_add(struct drm_panel *panel)
{
	drm_panel_get(panel);
	mutex_lock(&panel_lock);
	list_add_tail(&panel->list, &panel_list);
	mutex_unlock(&panel_lock);

	panel->bridge.of_node = panel->dev->of_node;
	panel->bridge.ops = DRM_BRIDGE_OP_MODES;
	panel->bridge.type = panel->connector_type;
	panel->bridge.pre_enable_prev_first = panel->prepare_prev_first;

	drm_bridge_add(&panel->bridge);
}
EXPORT_SYMBOL(drm_panel_add);

/**
 * drm_panel_remove - remove a panel from the global registry
 * @panel: DRM panel
 *
 * Removes a panel from the global registry.
 */
void drm_panel_remove(struct drm_panel *panel)
{
	drm_bridge_remove(&panel->bridge);
	mutex_lock(&panel_lock);
	list_del_init(&panel->list);
	mutex_unlock(&panel_lock);
	drm_panel_put(panel);
}
EXPORT_SYMBOL(drm_panel_remove);

static void drm_panel_add_release(void *data)
{
	drm_panel_remove(data);
}

/**
 * devm_drm_panel_add - add a panel to the global registry using devres
 * @dev: device to which the panel is attached
 * @panel: panel to add
 *
 * Add a panel to the global registry so that it can be looked
 * up by display drivers. The panel to be added must have been
 * allocated by devm_drm_panel_alloc(). Unlike drm_panel_add() with this
 * function there is no need to call drm_panel_remove(), it will be called
 * automatically.
 */
int devm_drm_panel_add(struct device *dev, struct drm_panel *panel)
{
	drm_panel_add(panel);

	return devm_add_action_or_reset(dev, drm_panel_add_release, panel);
}
EXPORT_SYMBOL(devm_drm_panel_add);

/**
 * drm_panel_prepare - power on a panel
 * @panel: DRM panel
 *
 * Calling this function will enable power and deassert any reset signals to
 * the panel. After this has completed it is possible to communicate with any
 * integrated circuitry via a command bus. This function cannot fail (as it is
 * called from the pre_enable call chain). There will always be a call to
 * drm_panel_disable() afterwards.
 */
void drm_panel_prepare(struct drm_panel *panel)
{
	struct drm_panel_follower *follower;
	int ret;

	if (!panel)
		return;

	if (panel->prepared) {
		dev_warn(panel->dev, "Skipping prepare of already prepared panel\n");
		return;
	}

	mutex_lock(&panel->follower_lock);

	if (panel->funcs && panel->funcs->prepare) {
		ret = panel->funcs->prepare(panel);
		if (ret < 0)
			goto exit;
	}
	panel->prepared = true;

	list_for_each_entry(follower, &panel->followers, list) {
		if (!follower->funcs->panel_prepared)
			continue;

		ret = follower->funcs->panel_prepared(follower);
		if (ret < 0)
			dev_info(panel->dev, "%ps failed: %d\n",
				 follower->funcs->panel_prepared, ret);
	}

exit:
	mutex_unlock(&panel->follower_lock);
}
EXPORT_SYMBOL(drm_panel_prepare);

/**
 * drm_panel_unprepare - power off a panel
 * @panel: DRM panel
 *
 * Calling this function will completely power off a panel (assert the panel's
 * reset, turn off power supplies, ...). After this function has completed, it
 * is usually no longer possible to communicate with the panel until another
 * call to drm_panel_prepare().
 */
void drm_panel_unprepare(struct drm_panel *panel)
{
	struct drm_panel_follower *follower;
	int ret;

	if (!panel)
		return;

	/*
	 * If you are seeing the warning below it likely means one of two things:
	 * - Your panel driver incorrectly calls drm_panel_unprepare() in its
	 *   shutdown routine. You should delete this.
	 * - You are using panel-edp or panel-simple and your DRM modeset
	 *   driver's shutdown() callback happened after the panel's shutdown().
	 *   In this case the warning is harmless though ideally you should
	 *   figure out how to reverse the order of the shutdown() callbacks.
	 */
	if (!panel->prepared) {
		dev_warn(panel->dev, "Skipping unprepare of already unprepared panel\n");
		return;
	}

	mutex_lock(&panel->follower_lock);

	list_for_each_entry(follower, &panel->followers, list) {
		if (!follower->funcs->panel_unpreparing)
			continue;

		ret = follower->funcs->panel_unpreparing(follower);
		if (ret < 0)
			dev_info(panel->dev, "%ps failed: %d\n",
				 follower->funcs->panel_unpreparing, ret);
	}

	if (panel->funcs && panel->funcs->unprepare) {
		ret = panel->funcs->unprepare(panel);
		if (ret < 0)
			goto exit;
	}
	panel->prepared = false;

exit:
	mutex_unlock(&panel->follower_lock);
}
EXPORT_SYMBOL(drm_panel_unprepare);

/**
 * drm_panel_enable - enable a panel
 * @panel: DRM panel
 *
 * Calling this function will cause the panel display drivers to be turned on
 * and the backlight to be enabled. Content will be visible on screen after
 * this call completes. This function cannot fail (as it is called from the
 * enable call chain). There will always be a call to drm_panel_disable()
 * afterwards.
 */
void drm_panel_enable(struct drm_panel *panel)
{
	struct drm_panel_follower *follower;
	int ret;

	if (!panel)
		return;

	if (panel->enabled) {
		dev_warn(panel->dev, "Skipping enable of already enabled panel\n");
		return;
	}

	mutex_lock(&panel->follower_lock);

	if (panel->funcs && panel->funcs->enable) {
		ret = panel->funcs->enable(panel);
		if (ret < 0)
			goto exit;
	}
	panel->enabled = true;

	ret = backlight_enable(panel->backlight);
	if (ret < 0)
		DRM_DEV_INFO(panel->dev, "failed to enable backlight: %d\n",
			     ret);

	list_for_each_entry(follower, &panel->followers, list) {
		if (!follower->funcs->panel_enabled)
			continue;

		ret = follower->funcs->panel_enabled(follower);
		if (ret < 0)
			dev_info(panel->dev, "%ps failed: %d\n",
				 follower->funcs->panel_enabled, ret);
	}

exit:
	mutex_unlock(&panel->follower_lock);
}
EXPORT_SYMBOL(drm_panel_enable);

/**
 * drm_panel_disable - disable a panel
 * @panel: DRM panel
 *
 * This will typically turn off the panel's backlight or disable the display
 * drivers. For smart panels it should still be possible to communicate with
 * the integrated circuitry via any command bus after this call.
 */
void drm_panel_disable(struct drm_panel *panel)
{
	struct drm_panel_follower *follower;
	int ret;

	if (!panel)
		return;

	/*
	 * If you are seeing the warning below it likely means one of two things:
	 * - Your panel driver incorrectly calls drm_panel_disable() in its
	 *   shutdown routine. You should delete this.
	 * - You are using panel-edp or panel-simple and your DRM modeset
	 *   driver's shutdown() callback happened after the panel's shutdown().
	 *   In this case the warning is harmless though ideally you should
	 *   figure out how to reverse the order of the shutdown() callbacks.
	 */
	if (!panel->enabled) {
		dev_warn(panel->dev, "Skipping disable of already disabled panel\n");
		return;
	}

	mutex_lock(&panel->follower_lock);

	list_for_each_entry(follower, &panel->followers, list) {
		if (!follower->funcs->panel_disabling)
			continue;

		ret = follower->funcs->panel_disabling(follower);
		if (ret < 0)
			dev_info(panel->dev, "%ps failed: %d\n",
				 follower->funcs->panel_disabling, ret);
	}

	ret = backlight_disable(panel->backlight);
	if (ret < 0)
		DRM_DEV_INFO(panel->dev, "failed to disable backlight: %d\n",
			     ret);

	if (panel->funcs && panel->funcs->disable) {
		ret = panel->funcs->disable(panel);
		if (ret < 0)
			goto exit;
	}
	panel->enabled = false;

exit:
	mutex_unlock(&panel->follower_lock);
}
EXPORT_SYMBOL(drm_panel_disable);

/**
 * drm_panel_get_modes - probe the available display modes of a panel
 * @panel: DRM panel
 * @connector: DRM connector
 *
 * The modes probed from the panel are automatically added to the connector
 * that the panel is attached to.
 *
 * Return: The number of modes available from the panel on success, or 0 on
 * failure (no modes).
 */
int drm_panel_get_modes(struct drm_panel *panel,
			struct drm_connector *connector)
{
	if (!panel)
		return 0;

	if (panel->funcs && panel->funcs->get_modes) {
		int num;

		num = panel->funcs->get_modes(panel, connector);
		if (num > 0)
			return num;
	}

	return 0;
}
EXPORT_SYMBOL(drm_panel_get_modes);

/**
 * drm_panel_get - Acquire a panel reference
 * @panel: DRM panel
 *
 * This function increments the panel's refcount.
 * Returns:
 * Pointer to @panel
 */
struct drm_panel *drm_panel_get(struct drm_panel *panel)
{
	if (panel)
		drm_bridge_get(&panel->bridge);

	return panel;
}
EXPORT_SYMBOL(drm_panel_get);

/**
 * drm_panel_put - Release a panel reference
 * @panel: DRM panel
 *
 * This function decrements the panel's reference count and frees the
 * object if the reference count drops to zero.
 */
void drm_panel_put(struct drm_panel *panel)
{
	if (panel)
		drm_bridge_put(&panel->bridge);
}
EXPORT_SYMBOL(drm_panel_put);

/**
 * drm_panel_put_void - wrapper to drm_panel_put() taking a void pointer
 *
 * @data: pointer to @struct drm_panel, cast to a void pointer
 *
 * Wrapper of drm_panel_put() to be used when a function taking a void
 * pointer is needed, for example as a devm action.
 */
static void drm_panel_put_void(void *data)
{
	struct drm_panel *panel = (struct drm_panel *)data;

	drm_panel_put(panel);
}

void *__devm_drm_panel_alloc(struct device *dev, size_t size, size_t offset,
			     const struct drm_panel_funcs *funcs,
			     int connector_type)
{
	/*
	 * Struct embedding and offsets:
	 *
	 *    |--------------- user container struct ------------|
	 *    :    |---------- struct drm_panel ------------|
	 *    :    :    |----- struct drm_bridge ------|
	 *    A    B    C
	 *
	 * B - A = offset (passed as argument)
	 * C - B = panel_bridge_offset
	 * C - A = alloc_bridge_offset
	 */
	const size_t panel_bridge_offset = offsetof(struct drm_panel, bridge);
	const size_t alloc_bridge_offset = offset + panel_bridge_offset;
	struct drm_panel *panel;
	void *container;
	int err;

	if (!funcs) {
		dev_warn(dev, "Missing funcs pointer\n");
		return ERR_PTR(-EINVAL);
	}

	container = __devm_drm_bridge_alloc(dev, size, alloc_bridge_offset,
					    &panel_bridge_bridge_funcs);
	if (IS_ERR(container))
		return container;

	panel = container + offset;
	panel->funcs = funcs;
	panel->bridge.of_node = dev->of_node;

	drm_panel_get(panel);

	err = devm_add_action_or_reset(dev, drm_panel_put_void, panel);
	if (err)
		return ERR_PTR(err);

	drm_panel_init(panel, dev, funcs, connector_type);

	return container;
}
EXPORT_SYMBOL(__devm_drm_panel_alloc);

#ifdef CONFIG_OF
/**
 * of_drm_find_panel - look up and reference a panel by device tree node
 * @np: device tree node of the panel
 *
 * Searches the set of registered panels for one that matches the given device
 * tree node. If a matching panel is found, the panel's reference count is
 * incremented before returning a pointer to it. The caller must call
 * drm_panel_put() when it no longer needs the panel pointer.
 *
 * Return: A reference-counted pointer to the panel registered for the specified
 * device tree node or an ERR_PTR() if no panel matching the device tree node
 * can be found.
 *
 * Possible error codes returned by this function:
 *
 * - EPROBE_DEFER: the panel device has not been probed yet, and the caller
 *   should retry later
 * - ENODEV: the device is not available (status != "okay" or "ok")
 */
struct drm_panel *of_drm_find_panel(const struct device_node *np)
{
	struct drm_panel *panel;

	if (!of_device_is_available(np))
		return ERR_PTR(-ENODEV);

	mutex_lock(&panel_lock);

	list_for_each_entry(panel, &panel_list, list) {
		if (panel->dev->of_node == np) {
			drm_panel_get(panel);
			mutex_unlock(&panel_lock);
			return panel;
		}
	}

	mutex_unlock(&panel_lock);
	return ERR_PTR(-EPROBE_DEFER);
}
EXPORT_SYMBOL(of_drm_find_panel);

/**
 * drm_of_find_panel_or_bridge - return connected panel or bridge device
 * @np: device tree node containing encoder output ports
 * @port: port in the device tree node
 * @endpoint: endpoint in the device tree node
 * @panel: pointer to hold returned drm_panel, must not be NULL. On success
 *         the caller must call drm_panel_put() when done with the panel
 * @bridge: pointer to hold returned drm_bridge
 *
 * Given a DT node's port and endpoint number, find the connected node and
 * return either the associated struct drm_panel or drm_bridge device.
 *
 * This function is deprecated and should not be used in new drivers. Use
 * of_drm_get_bridge_by_endpoint() instead when not looking for a panel, or
 * devm_drm_of_get_bridge() otherwise.
 *
 * Returns zero if successful, or one of the standard error codes if it fails.
 */
int drm_of_find_panel_or_bridge(const struct device_node *np,
				int port, int endpoint,
				struct drm_panel **panel,
				struct drm_bridge **bridge)
{
	if (WARN_ON(!panel))
		return -EINVAL;

	*panel = NULL;
	if (bridge)
		*bridge = NULL;

	/*
	 * of_graph_get_remote_node() produces a noisy error message if port
	 * node isn't found and the absence of the port is a legit case here,
	 * so at first we silently check whether a graph is present in the
	 * device-tree node.
	 */
	if (!of_graph_is_present(np))
		return -ENODEV;

	struct device_node *remote __free(device_node) =
		of_graph_get_remote_node(np, port, endpoint);
	if (!remote)
		return -ENODEV;

	*panel = of_drm_find_panel(remote);
	if (!IS_ERR(*panel))
		return 0;

	*panel = NULL;

	if (bridge) {
		/* No panel found yet, check for a bridge next. */
		*bridge = of_drm_find_bridge(remote);
		if (*bridge)
			return 0;

		*bridge = NULL;
	}

	return -EPROBE_DEFER;
}
EXPORT_SYMBOL_GPL(drm_of_find_panel_or_bridge);
#endif

/*
 * Find panel by fwnode, returning a counted reference.
 *
 * Behaves identically to of_drm_find_panel(). On success the returned
 * pointer has been passed through drm_panel_get(); the caller must call
 * drm_panel_put() when done with it.
 */
static struct drm_panel *find_panel_by_fwnode(const struct fwnode_handle *fwnode)
{
	struct drm_panel *panel;

	if (!fwnode_device_is_available(fwnode))
		return ERR_PTR(-ENODEV);

	mutex_lock(&panel_lock);

	list_for_each_entry(panel, &panel_list, list) {
		if (dev_fwnode(panel->dev) == fwnode) {
			drm_panel_get(panel);
			mutex_unlock(&panel_lock);
			return panel;
		}
	}

	mutex_unlock(&panel_lock);

	return ERR_PTR(-EPROBE_DEFER);
}

/* Find panel by follower device */
static struct drm_panel *find_panel_by_dev(struct device *follower_dev)
{
	struct fwnode_handle *fwnode;
	struct drm_panel *panel;

	fwnode = fwnode_find_reference(dev_fwnode(follower_dev), "panel", 0);
	if (IS_ERR(fwnode))
		return ERR_PTR(-ENODEV);

	panel = find_panel_by_fwnode(fwnode);
	fwnode_handle_put(fwnode);

	return panel;
}

/**
 * drm_is_panel_follower() - Check if the device is a panel follower
 * @dev: The 'struct device' to check
 *
 * This checks to see if a device needs to be power sequenced together with
 * a panel using the panel follower API.
 *
 * The "panel" property of the follower points to the panel to be followed.
 *
 * Return: true if we should be power sequenced with a panel; false otherwise.
 */
bool drm_is_panel_follower(struct device *dev)
{
	/*
	 * The "panel" property is actually a phandle, but for simplicity we
	 * don't bother trying to parse it here. We just need to know if the
	 * property is there.
	 */
	return device_property_present(dev, "panel");
}
EXPORT_SYMBOL(drm_is_panel_follower);

/**
 * drm_panel_add_follower() - Register something to follow panel state.
 * @follower_dev: The 'struct device' for the follower.
 * @follower:     The panel follower descriptor for the follower.
 *
 * A panel follower is called right after preparing/enabling the panel and right
 * before unpreparing/disabling the panel. It's primary intention is to power on
 * an associated touchscreen, though it could be used for any similar devices.
 * Multiple devices are allowed the follow the same panel.
 *
 * If a follower is added to a panel that's already been prepared/enabled, the
 * follower's prepared/enabled callback is called right away.
 *
 * The "panel" property of the follower points to the panel to be followed.
 *
 * Return: 0 or an error code. Note that -ENODEV means that we detected that
 *         follower_dev is not actually following a panel. The caller may
 *         choose to ignore this return value if following a panel is optional.
 */
int drm_panel_add_follower(struct device *follower_dev,
			   struct drm_panel_follower *follower)
{
	struct drm_panel *panel;
	int ret;

	panel = find_panel_by_dev(follower_dev);
	if (IS_ERR(panel))
		return PTR_ERR(panel);

	get_device(panel->dev);
	follower->panel = panel;

	mutex_lock(&panel->follower_lock);

	list_add_tail(&follower->list, &panel->followers);
	if (panel->prepared && follower->funcs->panel_prepared) {
		ret = follower->funcs->panel_prepared(follower);
		if (ret < 0)
			dev_info(panel->dev, "%ps failed: %d\n",
				 follower->funcs->panel_prepared, ret);
	}
	if (panel->enabled && follower->funcs->panel_enabled) {
		ret = follower->funcs->panel_enabled(follower);
		if (ret < 0)
			dev_info(panel->dev, "%ps failed: %d\n",
				 follower->funcs->panel_enabled, ret);
	}

	mutex_unlock(&panel->follower_lock);

	return 0;
}
EXPORT_SYMBOL(drm_panel_add_follower);

/**
 * drm_panel_remove_follower() - Reverse drm_panel_add_follower().
 * @follower:     The panel follower descriptor for the follower.
 *
 * Undo drm_panel_add_follower(). This includes calling the follower's
 * unpreparing/disabling function if we're removed from a panel that's currently
 * prepared/enabled.
 *
 * Return: 0 or an error code.
 */
void drm_panel_remove_follower(struct drm_panel_follower *follower)
{
	struct drm_panel *panel = follower->panel;
	int ret;

	mutex_lock(&panel->follower_lock);

	if (panel->enabled && follower->funcs->panel_disabling) {
		ret = follower->funcs->panel_disabling(follower);
		if (ret < 0)
			dev_info(panel->dev, "%ps failed: %d\n",
				 follower->funcs->panel_disabling, ret);
	}
	if (panel->prepared && follower->funcs->panel_unpreparing) {
		ret = follower->funcs->panel_unpreparing(follower);
		if (ret < 0)
			dev_info(panel->dev, "%ps failed: %d\n",
				 follower->funcs->panel_unpreparing, ret);
	}
	list_del_init(&follower->list);

	mutex_unlock(&panel->follower_lock);

	put_device(panel->dev);
	drm_panel_put(panel);
}
EXPORT_SYMBOL(drm_panel_remove_follower);

static void drm_panel_remove_follower_void(void *follower)
{
	drm_panel_remove_follower(follower);
}

/**
 * devm_drm_panel_add_follower() - devm version of drm_panel_add_follower()
 * @follower_dev: The 'struct device' for the follower.
 * @follower:     The panel follower descriptor for the follower.
 *
 * Handles calling drm_panel_remove_follower() using devm on the follower_dev.
 *
 * Return: 0 or an error code.
 */
int devm_drm_panel_add_follower(struct device *follower_dev,
				struct drm_panel_follower *follower)
{
	int ret;

	ret = drm_panel_add_follower(follower_dev, follower);
	if (ret)
		return ret;

	return devm_add_action_or_reset(follower_dev,
					drm_panel_remove_follower_void, follower);
}
EXPORT_SYMBOL(devm_drm_panel_add_follower);

#if IS_REACHABLE(CONFIG_BACKLIGHT_CLASS_DEVICE)
/**
 * drm_panel_of_backlight - use backlight device node for backlight
 * @panel: DRM panel
 *
 * Use this function to enable backlight handling if your panel
 * uses device tree and has a backlight phandle.
 *
 * When the panel is enabled backlight will be enabled after a
 * successful call to &drm_panel_funcs.enable()
 *
 * When the panel is disabled backlight will be disabled before the
 * call to &drm_panel_funcs.disable().
 *
 * A typical implementation for a panel driver supporting device tree
 * will call this function at probe time. Backlight will then be handled
 * transparently without requiring any intervention from the driver.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int drm_panel_of_backlight(struct drm_panel *panel)
{
	struct backlight_device *backlight;

	if (!panel || !panel->dev)
		return -EINVAL;

	backlight = devm_of_find_backlight(panel->dev);

	if (IS_ERR(backlight))
		return PTR_ERR(backlight);

	panel->backlight = backlight;
	return 0;
}
EXPORT_SYMBOL(drm_panel_of_backlight);
#endif

MODULE_AUTHOR("Thierry Reding <treding@nvidia.com>");
MODULE_DESCRIPTION("DRM panel infrastructure");
MODULE_LICENSE("GPL and additional rights");
