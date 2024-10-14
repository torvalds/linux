// SPDX-License-Identifier: GPL-2.0 or MIT
/*
 * Copyright 2018 Noralf Trønnes
 */

#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/seq_file.h>

#include <drm/drm_client.h>
#include <drm/drm_client_event.h>
#include <drm/drm_debugfs.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_print.h>

#include "drm_internal.h"

/**
 * drm_client_dev_unregister - Unregister clients
 * @dev: DRM device
 *
 * This function releases all clients by calling each client's
 * &drm_client_funcs.unregister callback. The callback function
 * is responsibe for releaseing all resources including the client
 * itself.
 *
 * The helper drm_dev_unregister() calls this function. Drivers
 * that use it don't need to call this function themselves.
 */
void drm_client_dev_unregister(struct drm_device *dev)
{
	struct drm_client_dev *client, *tmp;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return;

	mutex_lock(&dev->clientlist_mutex);
	list_for_each_entry_safe(client, tmp, &dev->clientlist, list) {
		list_del(&client->list);
		if (client->funcs && client->funcs->unregister) {
			client->funcs->unregister(client);
		} else {
			drm_client_release(client);
			kfree(client);
		}
	}
	mutex_unlock(&dev->clientlist_mutex);
}
EXPORT_SYMBOL(drm_client_dev_unregister);

/**
 * drm_client_dev_hotplug - Send hotplug event to clients
 * @dev: DRM device
 *
 * This function calls the &drm_client_funcs.hotplug callback on the attached clients.
 *
 * drm_kms_helper_hotplug_event() calls this function, so drivers that use it
 * don't need to call this function themselves.
 */
void drm_client_dev_hotplug(struct drm_device *dev)
{
	struct drm_client_dev *client;
	int ret;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return;

	if (!dev->mode_config.num_connector) {
		drm_dbg_kms(dev, "No connectors found, will not send hotplug events!\n");
		return;
	}

	mutex_lock(&dev->clientlist_mutex);
	list_for_each_entry(client, &dev->clientlist, list) {
		if (!client->funcs || !client->funcs->hotplug)
			continue;

		if (client->hotplug_failed)
			continue;

		ret = client->funcs->hotplug(client);
		drm_dbg_kms(dev, "%s: ret=%d\n", client->name, ret);
		if (ret)
			client->hotplug_failed = true;
	}
	mutex_unlock(&dev->clientlist_mutex);
}
EXPORT_SYMBOL(drm_client_dev_hotplug);

void drm_client_dev_restore(struct drm_device *dev)
{
	struct drm_client_dev *client;
	int ret;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return;

	mutex_lock(&dev->clientlist_mutex);
	list_for_each_entry(client, &dev->clientlist, list) {
		if (!client->funcs || !client->funcs->restore)
			continue;

		ret = client->funcs->restore(client);
		drm_dbg_kms(dev, "%s: ret=%d\n", client->name, ret);
		if (!ret) /* The first one to return zero gets the privilege to restore */
			break;
	}
	mutex_unlock(&dev->clientlist_mutex);
}

static int drm_client_suspend(struct drm_client_dev *client, bool holds_console_lock)
{
	struct drm_device *dev = client->dev;
	int ret = 0;

	if (drm_WARN_ON_ONCE(dev, client->suspended))
		return 0;

	if (client->funcs && client->funcs->suspend)
		ret = client->funcs->suspend(client, holds_console_lock);
	drm_dbg_kms(dev, "%s: ret=%d\n", client->name, ret);

	client->suspended = true;

	return ret;
}

void drm_client_dev_suspend(struct drm_device *dev, bool holds_console_lock)
{
	struct drm_client_dev *client;

	mutex_lock(&dev->clientlist_mutex);
	list_for_each_entry(client, &dev->clientlist, list) {
		if (!client->suspended)
			drm_client_suspend(client, holds_console_lock);
	}
	mutex_unlock(&dev->clientlist_mutex);
}
EXPORT_SYMBOL(drm_client_dev_suspend);

static int drm_client_resume(struct drm_client_dev *client, bool holds_console_lock)
{
	struct drm_device *dev = client->dev;
	int ret = 0;

	if (drm_WARN_ON_ONCE(dev, !client->suspended))
		return 0;

	if (client->funcs && client->funcs->resume)
		ret = client->funcs->resume(client, holds_console_lock);
	drm_dbg_kms(dev, "%s: ret=%d\n", client->name, ret);

	client->suspended = false;

	return ret;
}

void drm_client_dev_resume(struct drm_device *dev, bool holds_console_lock)
{
	struct drm_client_dev *client;

	mutex_lock(&dev->clientlist_mutex);
	list_for_each_entry(client, &dev->clientlist, list) {
		if  (client->suspended)
			drm_client_resume(client, holds_console_lock);
	}
	mutex_unlock(&dev->clientlist_mutex);
}
EXPORT_SYMBOL(drm_client_dev_resume);

#ifdef CONFIG_DEBUG_FS
static int drm_client_debugfs_internal_clients(struct seq_file *m, void *data)
{
	struct drm_debugfs_entry *entry = m->private;
	struct drm_device *dev = entry->dev;
	struct drm_printer p = drm_seq_file_printer(m);
	struct drm_client_dev *client;

	mutex_lock(&dev->clientlist_mutex);
	list_for_each_entry(client, &dev->clientlist, list)
		drm_printf(&p, "%s\n", client->name);
	mutex_unlock(&dev->clientlist_mutex);

	return 0;
}

static const struct drm_debugfs_info drm_client_debugfs_list[] = {
	{ "internal_clients", drm_client_debugfs_internal_clients, 0 },
};

void drm_client_debugfs_init(struct drm_device *dev)
{
	drm_debugfs_add_files(dev, drm_client_debugfs_list,
			      ARRAY_SIZE(drm_client_debugfs_list));
}
#endif
