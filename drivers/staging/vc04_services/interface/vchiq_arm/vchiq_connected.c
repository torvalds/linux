// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright (c) 2010-2012 Broadcom. All rights reserved. */

#include "vchiq_arm.h"
#include "vchiq_connected.h"
#include "vchiq_core.h"
#include <linux/module.h>
#include <linux/mutex.h>

#define  MAX_CALLBACKS  10

/*
 * This function is used to defer initialization until the vchiq stack is
 * initialized. If the stack is already initialized, then the callback will
 * be made immediately, otherwise it will be deferred until
 * vchiq_call_connected_callbacks is called.
 */
void vchiq_add_connected_callback(struct vchiq_device *device, void (*callback)(void))
{
	struct vchiq_drv_mgmt *drv_mgmt = device->drv_mgmt;

	if (mutex_lock_killable(&drv_mgmt->connected_mutex))
		return;

	if (drv_mgmt->connected) {
		/* We're already connected. Call the callback immediately. */
		callback();
	} else {
		if (drv_mgmt->num_deferred_callbacks >= MAX_CALLBACKS) {
			dev_err(&device->dev,
				"core: There already %d callback registered - please increase MAX_CALLBACKS\n",
				drv_mgmt->num_deferred_callbacks);
		} else {
			drv_mgmt->deferred_callback[drv_mgmt->num_deferred_callbacks] =
				callback;
			drv_mgmt->num_deferred_callbacks++;
		}
	}
	mutex_unlock(&drv_mgmt->connected_mutex);
}
EXPORT_SYMBOL(vchiq_add_connected_callback);

/*
 * This function is called by the vchiq stack once it has been connected to
 * the videocore and clients can start to use the stack.
 */
void vchiq_call_connected_callbacks(struct vchiq_drv_mgmt *drv_mgmt)
{
	int i;

	if (mutex_lock_killable(&drv_mgmt->connected_mutex))
		return;

	for (i = 0; i <  drv_mgmt->num_deferred_callbacks; i++)
		drv_mgmt->deferred_callback[i]();

	drv_mgmt->num_deferred_callbacks = 0;
	drv_mgmt->connected = true;
	mutex_unlock(&drv_mgmt->connected_mutex);
}
