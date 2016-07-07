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

#include <linux/err.h>
#include <linux/module.h>

#include <drm/drm_crtc.h>
#include <drm/drm_panel.h>

static DEFINE_MUTEX(panel_lock);
static LIST_HEAD(panel_list);

/**
 * DOC: drm panel
 *
 * The DRM panel helpers allow drivers to register panel objects with a
 * central registry and provide functions to retrieve those panels in display
 * drivers.
 */

/**
 * drm_panel_init - initialize a panel
 * @panel: DRM panel
 *
 * Sets up internal fields of the panel so that it can subsequently be added
 * to the registry.
 */
void drm_panel_init(struct drm_panel *panel)
{
	INIT_LIST_HEAD(&panel->list);
}
EXPORT_SYMBOL(drm_panel_init);

/**
 * drm_panel_add - add a panel to the global registry
 * @panel: panel to add
 *
 * Add a panel to the global registry so that it can be looked up by display
 * drivers.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int drm_panel_add(struct drm_panel *panel)
{
	mutex_lock(&panel_lock);
	list_add_tail(&panel->list, &panel_list);
	mutex_unlock(&panel_lock);

	return 0;
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
 * drm_panel_attach - attach a panel to a connector
 * @panel: DRM panel
 * @connector: DRM connector
 *
 * After obtaining a pointer to a DRM panel a display driver calls this
 * function to attach a panel to a connector.
 *
 * An error is returned if the panel is already attached to another connector.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int drm_panel_attach(struct drm_panel *panel, struct drm_connector *connector)
{
	if (panel->connector)
		return -EBUSY;

	panel->connector = connector;
	panel->drm = connector->dev;

	return 0;
}
EXPORT_SYMBOL(drm_panel_attach);

/**
 * drm_panel_detach - detach a panel from a connector
 * @panel: DRM panel
 *
 * Detaches a panel from the connector it is attached to. If a panel is not
 * attached to any connector this is effectively a no-op.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int drm_panel_detach(struct drm_panel *panel)
{
	panel->connector = NULL;
	panel->drm = NULL;

	return 0;
}
EXPORT_SYMBOL(drm_panel_detach);

#ifdef CONFIG_OF
/**
 * of_drm_find_panel - look up a panel using a device tree node
 * @np: device tree node of the panel
 *
 * Searches the set of registered panels for one that matches the given device
 * tree node. If a matching panel is found, return a pointer to it.
 *
 * Return: A pointer to the panel registered for the specified device tree
 * node or NULL if no panel matching the device tree node can be found.
 */
struct drm_panel *of_drm_find_panel(struct device_node *np)
{
	struct drm_panel *panel;

	mutex_lock(&panel_lock);

	list_for_each_entry(panel, &panel_list, list) {
		if (panel->dev->of_node == np) {
			mutex_unlock(&panel_lock);
			return panel;
		}
	}

	mutex_unlock(&panel_lock);
	return NULL;
}
EXPORT_SYMBOL(of_drm_find_panel);
#endif

MODULE_AUTHOR("Thierry Reding <treding@nvidia.com>");
MODULE_DESCRIPTION("DRM panel infrastructure");
MODULE_LICENSE("GPL and additional rights");
