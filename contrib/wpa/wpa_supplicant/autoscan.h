/*
 * WPA Supplicant - auto scan
 * Copyright (c) 2012, Intel Corporation. All rights reserved.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef AUTOSCAN_H
#define AUTOSCAN_H

struct wpa_supplicant;

struct autoscan_ops {
	const char *name;

	void * (*init)(struct wpa_supplicant *wpa_s, const char *params);
	void (*deinit)(void *priv);

	int (*notify_scan)(void *priv, struct wpa_scan_results *scan_res);
};

#ifdef CONFIG_AUTOSCAN

int autoscan_init(struct wpa_supplicant *wpa_s, int req_scan);
void autoscan_deinit(struct wpa_supplicant *wpa_s);
int autoscan_notify_scan(struct wpa_supplicant *wpa_s,
			 struct wpa_scan_results *scan_res);

/* Available autoscan modules */

#ifdef CONFIG_AUTOSCAN_EXPONENTIAL
extern const struct autoscan_ops autoscan_exponential_ops;
#endif /* CONFIG_AUTOSCAN_EXPONENTIAL */

#ifdef CONFIG_AUTOSCAN_PERIODIC
extern const struct autoscan_ops autoscan_periodic_ops;
#endif /* CONFIG_AUTOSCAN_PERIODIC */

#else /* CONFIG_AUTOSCAN */

static inline int autoscan_init(struct wpa_supplicant *wpa_s, int req_scan)
{
	return 0;
}

static inline void autoscan_deinit(struct wpa_supplicant *wpa_s)
{
}

static inline int autoscan_notify_scan(struct wpa_supplicant *wpa_s,
				       struct wpa_scan_results *scan_res)
{
	return 0;
}

#endif /* CONFIG_AUTOSCAN */

#endif /* AUTOSCAN_H */
