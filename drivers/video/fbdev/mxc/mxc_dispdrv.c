/*
 * Copyright (C) 2011-2014 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

/*!
 * @file mxc_dispdrv.c
 * @brief mxc display driver framework.
 *
 * A display device driver could call mxc_dispdrv_register(drv) in its dev_probe() function.
 * Move all dev_probe() things into mxc_dispdrv_driver->init(), init() function should init
 * and feedback setting;
 * Move all dev_remove() things into mxc_dispdrv_driver->deinit();
 * Move all dev_suspend() things into fb_notifier for SUSPEND, if there is;
 * Move all dev_resume() things into fb_notifier for RESUME, if there is;
 *
 * mxc fb driver could call mxc_dispdrv_gethandle(name, setting) before a fb
 * need be added, with fbi param passing by setting, after
 * mxc_dispdrv_gethandle() return, FB driver should get the basic setting
 * about fbi info and crtc.
 *
 * @ingroup Framebuffer
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/string.h>
#include "mxc_dispdrv.h"

static LIST_HEAD(dispdrv_list);
static DEFINE_MUTEX(dispdrv_lock);

struct mxc_dispdrv_entry {
	/* Note: drv always the first element */
	struct mxc_dispdrv_driver *drv;
	bool active;
	void *priv;
	struct list_head list;
};

struct mxc_dispdrv_handle *mxc_dispdrv_register(struct mxc_dispdrv_driver *drv)
{
	struct mxc_dispdrv_entry *new;

	mutex_lock(&dispdrv_lock);

	new = kzalloc(sizeof(struct mxc_dispdrv_entry), GFP_KERNEL);
	if (!new) {
		mutex_unlock(&dispdrv_lock);
		return ERR_PTR(-ENOMEM);
	}

	new->drv = drv;
	list_add_tail(&new->list, &dispdrv_list);

	mutex_unlock(&dispdrv_lock);

	return (struct mxc_dispdrv_handle *)new;
}
EXPORT_SYMBOL_GPL(mxc_dispdrv_register);

int mxc_dispdrv_unregister(struct mxc_dispdrv_handle *handle)
{
	struct mxc_dispdrv_entry *entry = (struct mxc_dispdrv_entry *)handle;

	if (entry) {
		mutex_lock(&dispdrv_lock);
		list_del(&entry->list);
		mutex_unlock(&dispdrv_lock);
		kfree(entry);
		return 0;
	} else
		return -EINVAL;
}
EXPORT_SYMBOL_GPL(mxc_dispdrv_unregister);

struct mxc_dispdrv_handle *mxc_dispdrv_gethandle(char *name,
	struct mxc_dispdrv_setting *setting)
{
	int ret, found = 0;
	struct mxc_dispdrv_entry *entry;

	mutex_lock(&dispdrv_lock);
	list_for_each_entry(entry, &dispdrv_list, list) {
		if (!strcmp(entry->drv->name, name) && (entry->drv->init)) {
			ret = entry->drv->init((struct mxc_dispdrv_handle *)
				entry, setting);
			if (ret >= 0) {
				entry->active = true;
				found = 1;
				break;
			}
		}
	}
	mutex_unlock(&dispdrv_lock);

	return found ? (struct mxc_dispdrv_handle *)entry:ERR_PTR(-ENODEV);
}
EXPORT_SYMBOL_GPL(mxc_dispdrv_gethandle);

void mxc_dispdrv_puthandle(struct mxc_dispdrv_handle *handle)
{
	struct mxc_dispdrv_entry *entry = (struct mxc_dispdrv_entry *)handle;

	mutex_lock(&dispdrv_lock);
	if (entry && entry->active && entry->drv->deinit) {
		entry->drv->deinit(handle);
		entry->active = false;
	}
	mutex_unlock(&dispdrv_lock);

}
EXPORT_SYMBOL_GPL(mxc_dispdrv_puthandle);

int mxc_dispdrv_setdata(struct mxc_dispdrv_handle *handle, void *data)
{
	struct mxc_dispdrv_entry *entry = (struct mxc_dispdrv_entry *)handle;

	if (entry) {
		entry->priv = data;
		return 0;
	} else
		return -EINVAL;
}
EXPORT_SYMBOL_GPL(mxc_dispdrv_setdata);

void *mxc_dispdrv_getdata(struct mxc_dispdrv_handle *handle)
{
	struct mxc_dispdrv_entry *entry = (struct mxc_dispdrv_entry *)handle;

	if (entry) {
		return entry->priv;
	} else
		return ERR_PTR(-EINVAL);
}
EXPORT_SYMBOL_GPL(mxc_dispdrv_getdata);
