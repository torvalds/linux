/* Copyright (c) 2014-2018, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define pr_fmt(fmt)	"[drm:%s:%d]: " fmt, __func__, __LINE__

#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/string.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/of_platform.h>

#include "dpu_power_handle.h"
#include "dpu_trace.h"

static void dpu_power_event_trigger_locked(struct dpu_power_handle *phandle,
		u32 event_type)
{
	struct dpu_power_event *event;

	list_for_each_entry(event, &phandle->event_list, list) {
		if (event->event_type & event_type)
			event->cb_fnc(event_type, event->usr);
	}
}

void dpu_power_resource_init(struct platform_device *pdev,
	struct dpu_power_handle *phandle)
{
	phandle->dev = &pdev->dev;

	INIT_LIST_HEAD(&phandle->event_list);

	mutex_init(&phandle->phandle_lock);
}

void dpu_power_resource_deinit(struct platform_device *pdev,
	struct dpu_power_handle *phandle)
{
	struct dpu_power_event *curr_event, *next_event;

	if (!phandle || !pdev) {
		pr_err("invalid input param\n");
		return;
	}

	mutex_lock(&phandle->phandle_lock);
	list_for_each_entry_safe(curr_event, next_event,
			&phandle->event_list, list) {
		pr_err("event:%d, client:%s still registered\n",
				curr_event->event_type,
				curr_event->client_name);
		curr_event->active = false;
		list_del(&curr_event->list);
	}
	mutex_unlock(&phandle->phandle_lock);
}

int dpu_power_resource_enable(struct dpu_power_handle *phandle, bool enable)
{
	u32 event_type;

	if (!phandle) {
		pr_err("invalid input argument\n");
		return -EINVAL;
	}

	mutex_lock(&phandle->phandle_lock);

	event_type = enable ? DPU_POWER_EVENT_ENABLE : DPU_POWER_EVENT_DISABLE;

	dpu_power_event_trigger_locked(phandle,	event_type);

	mutex_unlock(&phandle->phandle_lock);
	return 0;
}

struct dpu_power_event *dpu_power_handle_register_event(
		struct dpu_power_handle *phandle,
		u32 event_type, void (*cb_fnc)(u32 event_type, void *usr),
		void *usr, char *client_name)
{
	struct dpu_power_event *event;

	if (!phandle) {
		pr_err("invalid power handle\n");
		return ERR_PTR(-EINVAL);
	} else if (!cb_fnc || !event_type) {
		pr_err("no callback fnc or event type\n");
		return ERR_PTR(-EINVAL);
	}

	event = kzalloc(sizeof(struct dpu_power_event), GFP_KERNEL);
	if (!event)
		return ERR_PTR(-ENOMEM);

	event->event_type = event_type;
	event->cb_fnc = cb_fnc;
	event->usr = usr;
	strlcpy(event->client_name, client_name, MAX_CLIENT_NAME_LEN);
	event->active = true;

	mutex_lock(&phandle->phandle_lock);
	list_add(&event->list, &phandle->event_list);
	mutex_unlock(&phandle->phandle_lock);

	return event;
}

void dpu_power_handle_unregister_event(
		struct dpu_power_handle *phandle,
		struct dpu_power_event *event)
{
	if (!phandle || !event) {
		pr_err("invalid phandle or event\n");
	} else if (!event->active) {
		pr_err("power handle deinit already done\n");
		kfree(event);
	} else {
		mutex_lock(&phandle->phandle_lock);
		list_del_init(&event->list);
		mutex_unlock(&phandle->phandle_lock);
		kfree(event);
	}
}
