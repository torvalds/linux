/*
 * WPA Supplicant - auto scan
 * Copyright (c) 2012, Intel Corporation. All rights reserved.
 * Copyright 2015	Intel Deutschland GmbH
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "config.h"
#include "wpa_supplicant_i.h"
#include "bss.h"
#include "scan.h"
#include "autoscan.h"


static const struct autoscan_ops * autoscan_modules[] = {
#ifdef CONFIG_AUTOSCAN_EXPONENTIAL
	&autoscan_exponential_ops,
#endif /* CONFIG_AUTOSCAN_EXPONENTIAL */
#ifdef CONFIG_AUTOSCAN_PERIODIC
	&autoscan_periodic_ops,
#endif /* CONFIG_AUTOSCAN_PERIODIC */
	NULL
};


static void request_scan(struct wpa_supplicant *wpa_s)
{
	wpa_s->scan_req = MANUAL_SCAN_REQ;

	if (wpa_supplicant_req_sched_scan(wpa_s))
		wpa_supplicant_req_scan(wpa_s, wpa_s->scan_interval, 0);
}


int autoscan_init(struct wpa_supplicant *wpa_s, int req_scan)
{
	const char *name = wpa_s->conf->autoscan;
	const char *params;
	size_t nlen;
	int i;
	const struct autoscan_ops *ops = NULL;
	struct sched_scan_plan *scan_plans;

	/* Give preference to scheduled scan plans if supported/configured */
	if (wpa_s->sched_scan_plans) {
		wpa_printf(MSG_DEBUG,
			   "autoscan: sched_scan_plans set - use it instead");
		return 0;
	}

	if (wpa_s->autoscan && wpa_s->autoscan_priv) {
		wpa_printf(MSG_DEBUG, "autoscan: Already initialized");
		return 0;
	}

	if (name == NULL)
		return 0;

	params = os_strchr(name, ':');
	if (params == NULL) {
		params = "";
		nlen = os_strlen(name);
	} else {
		nlen = params - name;
		params++;
	}

	for (i = 0; autoscan_modules[i]; i++) {
		if (os_strncmp(name, autoscan_modules[i]->name, nlen) == 0) {
			ops = autoscan_modules[i];
			break;
		}
	}

	if (ops == NULL) {
		wpa_printf(MSG_ERROR, "autoscan: Could not find module "
			   "matching the parameter '%s'", name);
		return -1;
	}

	scan_plans = os_malloc(sizeof(*wpa_s->sched_scan_plans));
	if (!scan_plans)
		return -1;

	wpa_s->autoscan_params = NULL;

	wpa_s->autoscan_priv = ops->init(wpa_s, params);
	if (!wpa_s->autoscan_priv) {
		os_free(scan_plans);
		return -1;
	}

	scan_plans[0].interval = 5;
	scan_plans[0].iterations = 0;
	os_free(wpa_s->sched_scan_plans);
	wpa_s->sched_scan_plans = scan_plans;
	wpa_s->sched_scan_plans_num = 1;
	wpa_s->autoscan = ops;

	wpa_printf(MSG_DEBUG, "autoscan: Initialized module '%s' with "
		   "parameters '%s'", ops->name, params);
	if (!req_scan)
		return 0;

	/*
	 * Cancelling existing scan requests, if any.
	 */
	wpa_supplicant_cancel_sched_scan(wpa_s);
	wpa_supplicant_cancel_scan(wpa_s);

	/*
	 * Firing first scan, which will lead to call autoscan_notify_scan.
	 */
	request_scan(wpa_s);

	return 0;
}


void autoscan_deinit(struct wpa_supplicant *wpa_s)
{
	if (wpa_s->autoscan && wpa_s->autoscan_priv) {
		wpa_printf(MSG_DEBUG, "autoscan: Deinitializing module '%s'",
			   wpa_s->autoscan->name);
		wpa_s->autoscan->deinit(wpa_s->autoscan_priv);
		wpa_s->autoscan = NULL;
		wpa_s->autoscan_priv = NULL;

		wpa_s->scan_interval = 5;

		os_free(wpa_s->sched_scan_plans);
		wpa_s->sched_scan_plans = NULL;
		wpa_s->sched_scan_plans_num = 0;
	}
}


int autoscan_notify_scan(struct wpa_supplicant *wpa_s,
			 struct wpa_scan_results *scan_res)
{
	int interval;

	if (wpa_s->autoscan && wpa_s->autoscan_priv) {
		interval = wpa_s->autoscan->notify_scan(wpa_s->autoscan_priv,
							scan_res);

		if (interval <= 0)
			return -1;

		wpa_s->scan_interval = interval;
		wpa_s->sched_scan_plans[0].interval = interval;

		request_scan(wpa_s);
	}

	return 0;
}
