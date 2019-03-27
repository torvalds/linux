/*
 * WPA Supplicant - auto scan periodic module
 * Copyright (c) 2012, Intel Corporation. All rights reserved.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "wpa_supplicant_i.h"
#include "autoscan.h"


struct autoscan_periodic_data {
	int periodic_interval;
};


static int autoscan_periodic_get_params(struct autoscan_periodic_data *data,
					const char *params)
{
	int interval;

	if (params == NULL)
		return -1;

	interval = atoi(params);

	if (interval < 0)
		return -1;

	data->periodic_interval = interval;

	return 0;
}


static void * autoscan_periodic_init(struct wpa_supplicant *wpa_s,
				     const char *params)
{
	struct autoscan_periodic_data *data;

	data = os_zalloc(sizeof(struct autoscan_periodic_data));
	if (data == NULL)
		return NULL;

	if (autoscan_periodic_get_params(data, params) < 0) {
		os_free(data);
		return NULL;
	}

	wpa_printf(MSG_DEBUG, "autoscan periodic: interval is %d",
		   data->periodic_interval);

	return data;
}


static void autoscan_periodic_deinit(void *priv)
{
	struct autoscan_periodic_data *data = priv;

	os_free(data);
}


static int autoscan_periodic_notify_scan(void *priv,
					 struct wpa_scan_results *scan_res)
{
	struct autoscan_periodic_data *data = priv;

	wpa_printf(MSG_DEBUG, "autoscan periodic: scan result notification");

	return data->periodic_interval;
}


const struct autoscan_ops autoscan_periodic_ops = {
	.name = "periodic",
	.init = autoscan_periodic_init,
	.deinit = autoscan_periodic_deinit,
	.notify_scan = autoscan_periodic_notify_scan,
};
