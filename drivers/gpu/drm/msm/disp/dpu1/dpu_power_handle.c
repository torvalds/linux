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

static const char *data_bus_name[DPU_POWER_HANDLE_DBUS_ID_MAX] = {
	[DPU_POWER_HANDLE_DBUS_ID_MNOC] = "qcom,dpu-data-bus",
	[DPU_POWER_HANDLE_DBUS_ID_LLCC] = "qcom,dpu-llcc-bus",
	[DPU_POWER_HANDLE_DBUS_ID_EBI] = "qcom,dpu-ebi-bus",
};

const char *dpu_power_handle_get_dbus_name(u32 bus_id)
{
	if (bus_id < DPU_POWER_HANDLE_DBUS_ID_MAX)
		return data_bus_name[bus_id];

	return NULL;
}

static void dpu_power_event_trigger_locked(struct dpu_power_handle *phandle,
		u32 event_type)
{
	struct dpu_power_event *event;

	list_for_each_entry(event, &phandle->event_list, list) {
		if (event->event_type & event_type)
			event->cb_fnc(event_type, event->usr);
	}
}

struct dpu_power_client *dpu_power_client_create(
	struct dpu_power_handle *phandle, char *client_name)
{
	struct dpu_power_client *client;
	static u32 id;

	if (!client_name || !phandle) {
		pr_err("client name is null or invalid power data\n");
		return ERR_PTR(-EINVAL);
	}

	client = kzalloc(sizeof(struct dpu_power_client), GFP_KERNEL);
	if (!client)
		return ERR_PTR(-ENOMEM);

	mutex_lock(&phandle->phandle_lock);
	strlcpy(client->name, client_name, MAX_CLIENT_NAME_LEN);
	client->usecase_ndx = VOTE_INDEX_DISABLE;
	client->id = id;
	client->active = true;
	pr_debug("client %s created:%pK id :%d\n", client_name,
		client, id);
	id++;
	list_add(&client->list, &phandle->power_client_clist);
	mutex_unlock(&phandle->phandle_lock);

	return client;
}

void dpu_power_client_destroy(struct dpu_power_handle *phandle,
	struct dpu_power_client *client)
{
	if (!client  || !phandle) {
		pr_err("reg bus vote: invalid client handle\n");
	} else if (!client->active) {
		pr_err("dpu power deinit already done\n");
		kfree(client);
	} else {
		pr_debug("bus vote client %s destroyed:%pK id:%u\n",
			client->name, client, client->id);
		mutex_lock(&phandle->phandle_lock);
		list_del_init(&client->list);
		mutex_unlock(&phandle->phandle_lock);
		kfree(client);
	}
}

void dpu_power_resource_init(struct platform_device *pdev,
	struct dpu_power_handle *phandle)
{
	phandle->dev = &pdev->dev;

	INIT_LIST_HEAD(&phandle->power_client_clist);
	INIT_LIST_HEAD(&phandle->event_list);

	mutex_init(&phandle->phandle_lock);
}

void dpu_power_resource_deinit(struct platform_device *pdev,
	struct dpu_power_handle *phandle)
{
	struct dpu_power_client *curr_client, *next_client;
	struct dpu_power_event *curr_event, *next_event;

	if (!phandle || !pdev) {
		pr_err("invalid input param\n");
		return;
	}

	mutex_lock(&phandle->phandle_lock);
	list_for_each_entry_safe(curr_client, next_client,
			&phandle->power_client_clist, list) {
		pr_err("cliend:%s-%d still registered with refcount:%d\n",
				curr_client->name, curr_client->id,
				curr_client->refcount);
		curr_client->active = false;
		list_del(&curr_client->list);
	}

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

int dpu_power_resource_enable(struct dpu_power_handle *phandle,
	struct dpu_power_client *pclient, bool enable)
{
	bool changed = false;
	u32 max_usecase_ndx = VOTE_INDEX_DISABLE, prev_usecase_ndx;
	struct dpu_power_client *client;

	if (!phandle || !pclient) {
		pr_err("invalid input argument\n");
		return -EINVAL;
	}

	mutex_lock(&phandle->phandle_lock);
	if (enable)
		pclient->refcount++;
	else if (pclient->refcount)
		pclient->refcount--;

	if (pclient->refcount)
		pclient->usecase_ndx = VOTE_INDEX_LOW;
	else
		pclient->usecase_ndx = VOTE_INDEX_DISABLE;

	list_for_each_entry(client, &phandle->power_client_clist, list) {
		if (client->usecase_ndx < VOTE_INDEX_MAX &&
		    client->usecase_ndx > max_usecase_ndx)
			max_usecase_ndx = client->usecase_ndx;
	}

	if (phandle->current_usecase_ndx != max_usecase_ndx) {
		changed = true;
		prev_usecase_ndx = phandle->current_usecase_ndx;
		phandle->current_usecase_ndx = max_usecase_ndx;
	}

	pr_debug("%pS: changed=%d current idx=%d request client %s id:%u enable:%d refcount:%d\n",
		__builtin_return_address(0), changed, max_usecase_ndx,
		pclient->name, pclient->id, enable, pclient->refcount);

	if (!changed)
		goto end;

	if (enable) {
		dpu_power_event_trigger_locked(phandle,
				DPU_POWER_EVENT_PRE_ENABLE);
		dpu_power_event_trigger_locked(phandle,
				DPU_POWER_EVENT_POST_ENABLE);

	} else {
		dpu_power_event_trigger_locked(phandle,
				DPU_POWER_EVENT_PRE_DISABLE);
		dpu_power_event_trigger_locked(phandle,
				DPU_POWER_EVENT_POST_DISABLE);
	}

end:
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
