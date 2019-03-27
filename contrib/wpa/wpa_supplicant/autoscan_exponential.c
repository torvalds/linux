/*
 * WPA Supplicant - auto scan exponential module
 * Copyright (c) 2012, Intel Corporation. All rights reserved.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "wpa_supplicant_i.h"
#include "autoscan.h"

struct autoscan_exponential_data {
	struct wpa_supplicant *wpa_s;
	int base;
	int limit;
	int interval;
};


static int
autoscan_exponential_get_params(struct autoscan_exponential_data *data,
				const char *params)
{
	const char *pos;

	if (params == NULL)
		return -1;

	data->base = atoi(params);

	pos = os_strchr(params, ':');
	if (pos == NULL)
		return -1;

	pos++;
	data->limit = atoi(pos);

	return 0;
}


static void * autoscan_exponential_init(struct wpa_supplicant *wpa_s,
					const char *params)
{
	struct autoscan_exponential_data *data;

	data = os_zalloc(sizeof(struct autoscan_exponential_data));
	if (data == NULL)
		return NULL;

	if (autoscan_exponential_get_params(data, params) < 0) {
		os_free(data);
		return NULL;
	}

	wpa_printf(MSG_DEBUG, "autoscan exponential: base exponential is %d "
		   "and limit is %d", data->base, data->limit);

	data->wpa_s = wpa_s;

	return data;
}


static void autoscan_exponential_deinit(void *priv)
{
	struct autoscan_exponential_data *data = priv;

	os_free(data);
}


static int autoscan_exponential_notify_scan(void *priv,
					    struct wpa_scan_results *scan_res)
{
	struct autoscan_exponential_data *data = priv;

	wpa_printf(MSG_DEBUG, "autoscan exponential: scan result "
		   "notification");

	if (data->interval >= data->limit)
		return data->limit;

	if (data->interval <= 0)
		data->interval = data->base;
	else {
		data->interval = data->interval * data->base;
		if (data->interval > data->limit)
			return data->limit;
	}

	return data->interval;
}


const struct autoscan_ops autoscan_exponential_ops = {
	.name = "exponential",
	.init = autoscan_exponential_init,
	.deinit = autoscan_exponential_deinit,
	.notify_scan = autoscan_exponential_notify_scan,
};
