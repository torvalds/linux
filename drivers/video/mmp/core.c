/*
 * linux/drivers/video/mmp/common.c
 * This driver is a common framework for Marvell Display Controller
 *
 * Copyright (C) 2012 Marvell Technology Group Ltd.
 * Authors: Zhou Zhu <zzhu3@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/export.h>
#include <video/mmp_disp.h>

static struct mmp_overlay *path_get_overlay(struct mmp_path *path,
		int overlay_id)
{
	if (path && overlay_id < path->overlay_num)
		return &path->overlays[overlay_id];
	return 0;
}

static int path_check_status(struct mmp_path *path)
{
	int i;
	for (i = 0; i < path->overlay_num; i++)
		if (path->overlays[i].status)
			return 1;

	return 0;
}

/*
 * Get modelist write pointer of modelist.
 * It also returns modelist number
 * this function fetches modelist from phy/panel:
 *   for HDMI/parallel or dsi to hdmi cases, get from phy
 *   or get from panel
 */
static int path_get_modelist(struct mmp_path *path,
		struct mmp_mode **modelist)
{
	BUG_ON(!path || !modelist);

	if (path->panel && path->panel->get_modelist)
		return path->panel->get_modelist(path->panel, modelist);

	return 0;
}

/*
 * panel list is used to pair panel/path when path/panel registered
 * path list is used for both buffer driver and platdriver
 * plat driver do path register/unregister
 * panel driver do panel register/unregister
 * buffer driver get registered path
 */
static LIST_HEAD(panel_list);
static LIST_HEAD(path_list);
static DEFINE_MUTEX(disp_lock);

/*
 * mmp_register_panel - register panel to panel_list and connect to path
 * @p: panel to be registered
 *
 * this function provides interface for panel drivers to register panel
 * to panel_list and connect to path which matchs panel->plat_path_name.
 * no error returns when no matching path is found as path register after
 * panel register is permitted.
 */
void mmp_register_panel(struct mmp_panel *panel)
{
	struct mmp_path *path;

	mutex_lock(&disp_lock);

	/* add */
	list_add_tail(&panel->node, &panel_list);

	/* try to register to path */
	list_for_each_entry(path, &path_list, node) {
		if (!strcmp(panel->plat_path_name, path->name)) {
			dev_info(panel->dev, "connect to path %s\n",
				path->name);
			path->panel = panel;
			break;
		}
	}

	mutex_unlock(&disp_lock);
}
EXPORT_SYMBOL_GPL(mmp_register_panel);

/*
 * mmp_unregister_panel - unregister panel from panel_list and disconnect
 * @p: panel to be unregistered
 *
 * this function provides interface for panel drivers to unregister panel
 * from panel_list and disconnect from path.
 */
void mmp_unregister_panel(struct mmp_panel *panel)
{
	struct mmp_path *path;

	mutex_lock(&disp_lock);
	list_del(&panel->node);

	list_for_each_entry(path, &path_list, node) {
		if (path->panel && path->panel == panel) {
			dev_info(panel->dev, "disconnect from path %s\n",
				path->name);
			path->panel = NULL;
			break;
		}
	}
	mutex_unlock(&disp_lock);
}
EXPORT_SYMBOL_GPL(mmp_unregister_panel);

/*
 * mmp_get_path - get path by name
 * @p: path name
 *
 * this function checks path name in path_list and return matching path
 * return NULL if no matching path
 */
struct mmp_path *mmp_get_path(const char *name)
{
	struct mmp_path *path;
	int found = 0;

	mutex_lock(&disp_lock);
	list_for_each_entry(path, &path_list, node) {
		if (!strcmp(name, path->name)) {
			found = 1;
			break;
		}
	}
	mutex_unlock(&disp_lock);

	return found ? path : NULL;
}
EXPORT_SYMBOL_GPL(mmp_get_path);

/*
 * mmp_register_path - init and register path by path_info
 * @p: path info provided by display controller
 *
 * this function init by path info and register path to path_list
 * this function also try to connect path with panel by name
 */
struct mmp_path *mmp_register_path(struct mmp_path_info *info)
{
	int i;
	size_t size;
	struct mmp_path *path = NULL;
	struct mmp_panel *panel;

	size = sizeof(struct mmp_path)
		+ sizeof(struct mmp_overlay) * info->overlay_num;
	path = kzalloc(size, GFP_KERNEL);
	if (!path)
		goto failed;

	/* path set */
	mutex_init(&path->access_ok);
	path->dev = info->dev;
	path->id = info->id;
	path->name = info->name;
	path->output_type = info->output_type;
	path->overlay_num = info->overlay_num;
	path->plat_data = info->plat_data;
	path->ops.set_mode = info->set_mode;

	mutex_lock(&disp_lock);
	/* get panel */
	list_for_each_entry(panel, &panel_list, node) {
		if (!strcmp(info->name, panel->plat_path_name)) {
			dev_info(path->dev, "get panel %s\n", panel->name);
			path->panel = panel;
			break;
		}
	}

	dev_info(path->dev, "register %s, overlay_num %d\n",
			path->name, path->overlay_num);

	/* default op set: if already set by driver, never cover it */
	if (!path->ops.check_status)
		path->ops.check_status = path_check_status;
	if (!path->ops.get_overlay)
		path->ops.get_overlay = path_get_overlay;
	if (!path->ops.get_modelist)
		path->ops.get_modelist = path_get_modelist;

	/* step3: init overlays */
	for (i = 0; i < path->overlay_num; i++) {
		path->overlays[i].path = path;
		path->overlays[i].id = i;
		mutex_init(&path->overlays[i].access_ok);
		path->overlays[i].ops = info->overlay_ops;
	}

	/* add to pathlist */
	list_add_tail(&path->node, &path_list);

	mutex_unlock(&disp_lock);
	return path;

failed:
	kfree(path);
	mutex_unlock(&disp_lock);
	return NULL;
}
EXPORT_SYMBOL_GPL(mmp_register_path);

/*
 * mmp_unregister_path - unregister and destory path
 * @p: path to be destoried.
 *
 * this function registers path and destorys it.
 */
void mmp_unregister_path(struct mmp_path *path)
{
	int i;

	if (!path)
		return;

	mutex_lock(&disp_lock);
	/* del from pathlist */
	list_del(&path->node);

	/* deinit overlays */
	for (i = 0; i < path->overlay_num; i++)
		mutex_destroy(&path->overlays[i].access_ok);

	mutex_destroy(&path->access_ok);

	kfree(path);
	mutex_unlock(&disp_lock);

	dev_info(path->dev, "de-register %s\n", path->name);
}
EXPORT_SYMBOL_GPL(mmp_unregister_path);
