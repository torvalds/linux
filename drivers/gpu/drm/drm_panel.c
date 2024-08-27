/*
 * Copyright (C) 2013, NVIDIA Corporation.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <linux/backlight.h>
#include <linux/err.h>
#include <linux/module.h>

#include <drm/drm_crtc.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>

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

/**
 * drm_panel_init - initialize a panel
 * @panel: DRM panel
 * @dev: parent device of the panel
 * @funcs: panel operations
 * @connector_type: the connector type (DRM_MODE_CONNECTOR_*) corresponding to
 *	the panel interface
 *
 * Initialize the panel structure for subsequent registration with
 * drm_panel_add().
 */
void drm_panel_init(struct drm_panel *panel, struct device *dev,
		    const struct drm_panel_funcs *funcs, int connector_type)
{
	INIT_LIST_HEAD(&panel->list);
	INIT_LIST_HEAD(&panel->followers);
	mutex_init(&panel->follower_lock);
	panel->dev = dev;
	panel->funcs = funcs;
	panel->connector_type = connector_type;
}
EXPORT_SYMBOL(drm_panel_init);

/**
 * drm_panel_add - add a panel to the global registry
 * @panel: panel to add
 *
 * Add a panel to the global registry so that it can be looked up by display
 * drivers.
 */
void drm_panel_add(struct drm_panel *panel)
{
	mutex_lock(&panel_lock);
	list_add_tail(&panel->list, &panel_list);
	mutex_unlock(&panel_lock);
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
	mutex_lock(&panel_lock);
	list_del_init(&panel->list);
	mutex_unlock(&panel_lock);
}
EXPORT_SYMBOL(drm_panel_remove);

/**
 * drm_panel_prepare - power on a panel
 * @panel: DRM panel
 *
 * Calling this function will enable power and deassert any reset signals to
 * the panel. After this has completed it is possible to communicate with any
 * integrated circuitry via a command bus.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int drm_panel_prepare(struct drm_panel *panel)
{
	struct drm_panel_follower *follower;
	int ret;

	if (!panel)
		return -EINVAL;

	if (panel->prepared) {
		dev_warn(panel->dev, "Skipping prepare of already prepared panel\n");
		return 0;
	}

	mutex_lock(&panel->follower_lock);

	if (panel->funcs && panel->funcs->prepare) {
		ret = panel->funcs->prepare(panel);
		if (ret < 0)
			goto exit;
	}
	panel->prepared = true;

	list_for_each_entry(follower, &panel->followers, list) {
		ret = follower->funcs->panel_prepared(follower);
		if (ret < 0)
			dev_info(panel->dev, "%ps failed: %d\n",
				 follower->funcs->panel_prepared, ret);
	}

	ret = 0;
exit:
	mutex_unlock(&panel->follower_lock);

	return ret;
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
 *
 * Return: 0 on success or a negative error code on failure.
 */
int drm_panel_unprepare(struct drm_panel *panel)
{
	struct drm_panel_follower *follower;
	int ret;

	if (!panel)
		return -EINVAL;

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
		return 0;
	}

	mutex_lock(&panel->follower_lock);

	list_for_each_entry(follower, &panel->followers, list) {
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

	ret = 0;
exit:
	mutex_unlock(&panel->follower_lock);

	return ret;
}
EXPORT_SYMBOL(drm_panel_unprepare);

/**
 * drm_panel_enable - enable a panel
 * @panel: DRM panel
 *
 * Calling this function will cause the panel display drivers to be turned on
 * and the backlight to be enabled. Content will be visible on screen after
 * this call completes.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int drm_panel_enable(struct drm_panel *panel)
{
	int ret;

	if (!panel)
		return -EINVAL;

	if (panel->enabled) {
		dev_warn(panel->dev, "Skipping enable of already enabled panel\n");
		return 0;
	}

	if (panel->funcs && panel->funcs->enable) {
		ret = panel->funcs->enable(panel);
		if (ret < 0)
			return ret;
	}
	panel->enabled = true;

	ret = backlight_enable(panel->backlight);
	if (ret < 0)
		DRM_DEV_INFO(panel->dev, "failed to enable backlight: %d\n",
			     ret);

	return 0;
}
EXPORT_SYMBOL(drm_panel_enable);

/**
 * drm_panel_disable - disable a panel
 * @panel: DRM panel
 *
 * This will typically turn off the panel's backlight or disable the display
 * drivers. For smart panels it should still be possible to communicate with
 * the integrated circuitry via any command bus after this call.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int drm_panel_disable(struct drm_panel *panel)
{
	int ret;

	if (!panel)
		return -EINVAL;

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
		return 0;
	}

	ret = backlight_disable(panel->backlight);
	if (ret < 0)
		DRM_DEV_INFO(panel->dev, "failed to disable backlight: %d\n",
			     ret);

	if (panel->funcs && panel->funcs->disable) {
		ret = panel->funcs->disable(panel);
		if (ret < 0)
			return ret;
	}
	panel->enabled = false;

	return 0;
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

#ifdef CONFIG_OF
/**
 * of_drm_find_panel - look up a panel using a device tree node
 * @np: device tree node of the panel
 *
 * Searches the set of registered panels for one that matches the given device
 * tree node. If a matching panel is found, return a pointer to it.
 *
 * Return: A pointer to the panel registered for the specified device tree
 * node or an ERR_PTR() if no panel matching the device tree node can be found.
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
			mutex_unlock(&panel_lock);
			return panel;
		}
	}

	mutex_unlock(&panel_lock);
	return ERR_PTR(-EPROBE_DEFER);
}
EXPORT_SYMBOL(of_drm_find_panel);

/**
 * of_drm_get_panel_orientation - look up the orientation of the panel through
 * the "rotation" binding from a device tree node
 * @np: device tree node of the panel
 * @orientation: orientation enum to be filled in
 *
 * Looks up the rotation of a panel in the device tree. The orientation of the
 * panel is expressed as a property name "rotation" in the device tree. The
 * rotation in the device tree is counter clockwise.
 *
 * Return: 0 when a valid rotation value (0, 90, 180, or 270) is read or the
 * rotation property doesn't exist. Return a negative error code on failure.
 */
int of_drm_get_panel_orientation(const struct device_node *np,
				 enum drm_panel_orientation *orientation)
{
	int rotation, ret;

	ret = of_property_read_u32(np, "rotation", &rotation);
	if (ret == -EINVAL) {
		/* Don't return an error if there's no rotation property. */
		*orientation = DRM_MODE_PANEL_ORIENTATION_UNKNOWN;
		return 0;
	}

	if (ret < 0)
		return ret;

	if (rotation == 0)
		*orientation = DRM_MODE_PANEL_ORIENTATION_NORMAL;
	else if (rotation == 90)
		*orientation = DRM_MODE_PANEL_ORIENTATION_RIGHT_UP;
	else if (rotation == 180)
		*orientation = DRM_MODE_PANEL_ORIENTATION_BOTTOM_UP;
	else if (rotation == 270)
		*orientation = DRM_MODE_PANEL_ORIENTATION_LEFT_UP;
	else
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL(of_drm_get_panel_orientation);
#endif

/**
 * drm_is_panel_follower() - Check if the device is a panel follower
 * @dev: The 'struct device' to check
 *
 * This checks to see if a device needs to be power sequenced together with
 * a panel using the panel follower API.
 * At the moment panels can only be followed on device tree enabled systems.
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
	return of_property_read_bool(dev->of_node, "panel");
}
EXPORT_SYMBOL(drm_is_panel_follower);

/**
 * drm_panel_add_follower() - Register something to follow panel state.
 * @follower_dev: The 'struct device' for the follower.
 * @follower:     The panel follower descriptor for the follower.
 *
 * A panel follower is called right after preparing the panel and right before
 * unpreparing the panel. It's primary intention is to power on an associated
 * touchscreen, though it could be used for any similar devices. Multiple
 * devices are allowed the follow the same panel.
 *
 * If a follower is added to a panel that's already been turned on, the
 * follower's prepare callback is called right away.
 *
 * At the moment panels can only be followed on device tree enabled systems.
 * The "panel" property of the follower points to the panel to be followed.
 *
 * Return: 0 or an error code. Note that -ENODEV means that we detected that
 *         follower_dev is not actually following a panel. The caller may
 *         choose to ignore this return value if following a panel is optional.
 */
int drm_panel_add_follower(struct device *follower_dev,
			   struct drm_panel_follower *follower)
{
	struct device_node *panel_np;
	struct drm_panel *panel;
	int ret;

	panel_np = of_parse_phandle(follower_dev->of_node, "panel", 0);
	if (!panel_np)
		return -ENODEV;

	panel = of_drm_find_panel(panel_np);
	of_node_put(panel_np);
	if (IS_ERR(panel))
		return PTR_ERR(panel);

	get_device(panel->dev);
	follower->panel = panel;

	mutex_lock(&panel->follower_lock);

	list_add_tail(&follower->list, &panel->followers);
	if (panel->prepared) {
		ret = follower->funcs->panel_prepared(follower);
		if (ret < 0)
			dev_info(panel->dev, "%ps failed: %d\n",
				 follower->funcs->panel_prepared, ret);
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
 * unprepare function if we're removed from a panel that's currently prepared.
 *
 * Return: 0 or an error code.
 */
void drm_panel_remove_follower(struct drm_panel_follower *follower)
{
	struct drm_panel *panel = follower->panel;
	int ret;

	mutex_lock(&panel->follower_lock);

	if (panel->prepared) {
		ret = follower->funcs->panel_unpreparing(follower);
		if (ret < 0)
			dev_info(panel->dev, "%ps failed: %d\n",
				 follower->funcs->panel_unpreparing, ret);
	}
	list_del_init(&follower->list);

	mutex_unlock(&panel->follower_lock);

	put_device(panel->dev);
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
 * drm_panel_of_backlight() must be called after the call to drm_panel_init().
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
