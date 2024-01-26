// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright (c) 2010-2012 Broadcom. All rights reserved. */

#include "vchiq_connected.h"
#include "vchiq_core.h"
#include <linux/module.h>
#include <linux/mutex.h>

#define  MAX_CALLBACKS  10

static   int                        g_connected;
static   int                        g_num_deferred_callbacks;
static   void (*g_deferred_callback[MAX_CALLBACKS])(void);
static   int                        g_once_init;
static   DEFINE_MUTEX(g_connected_mutex);

/* Function to initialize our lock */
static void connected_init(void)
{
	if (!g_once_init)
		g_once_init = 1;
}

/*
 * This function is used to defer initialization until the vchiq stack is
 * initialized. If the stack is already initialized, then the callback will
 * be made immediately, otherwise it will be deferred until
 * vchiq_call_connected_callbacks is called.
 */
void vchiq_add_connected_callback(struct vchiq_device *device, void (*callback)(void))
{
	connected_init();

	if (mutex_lock_killable(&g_connected_mutex))
		return;

	if (g_connected) {
		/* We're already connected. Call the callback immediately. */
		callback();
	} else {
		if (g_num_deferred_callbacks >= MAX_CALLBACKS) {
			dev_err(&device->dev,
				"core: There already %d callback registered - please increase MAX_CALLBACKS\n",
				g_num_deferred_callbacks);
		} else {
			g_deferred_callback[g_num_deferred_callbacks] =
				callback;
			g_num_deferred_callbacks++;
		}
	}
	mutex_unlock(&g_connected_mutex);
}
EXPORT_SYMBOL(vchiq_add_connected_callback);

/*
 * This function is called by the vchiq stack once it has been connected to
 * the videocore and clients can start to use the stack.
 */
void vchiq_call_connected_callbacks(void)
{
	int i;

	connected_init();

	if (mutex_lock_killable(&g_connected_mutex))
		return;

	for (i = 0; i <  g_num_deferred_callbacks; i++)
		g_deferred_callback[i]();

	g_num_deferred_callbacks = 0;
	g_connected = 1;
	mutex_unlock(&g_connected_mutex);
}
