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

void drm_panel_init(struct drm_panel *panel)
{
	INIT_LIST_HEAD(&panel->list);
}
EXPORT_SYMBOL(drm_panel_init);

int drm_panel_add(struct drm_panel *panel)
{
	mutex_lock(&panel_lock);
	list_add_tail(&panel->list, &panel_list);
	mutex_unlock(&panel_lock);

	return 0;
}
EXPORT_SYMBOL(drm_panel_add);

void drm_panel_remove(struct drm_panel *panel)
{
	mutex_lock(&panel_lock);
	list_del_init(&panel->list);
	mutex_unlock(&panel_lock);
}
EXPORT_SYMBOL(drm_panel_remove);

int drm_panel_attach(struct drm_panel *panel, struct drm_connector *connector)
{
	if (panel->connector)
		return -EBUSY;

	panel->connector = connector;
	panel->drm = connector->dev;

	return 0;
}
EXPORT_SYMBOL(drm_panel_attach);

int drm_panel_detach(struct drm_panel *panel)
{
	panel->connector = NULL;
	panel->drm = NULL;

	return 0;
}
EXPORT_SYMBOL(drm_panel_detach);

struct drm_panel *drm_find_panel_by_connector(struct drm_connector *connector)
{
	struct drm_panel *panel;

	mutex_lock(&panel_lock);

	list_for_each_entry(panel, &panel_list, list) {
		if (panel->connector == connector) {
			mutex_unlock(&panel_lock);
			return panel;
		}
	}

	mutex_unlock(&panel_lock);
	return NULL;
}
EXPORT_SYMBOL(drm_find_panel_by_connector);

#ifdef CONFIG_OF
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
