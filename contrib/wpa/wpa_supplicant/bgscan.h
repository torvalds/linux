/*
 * WPA Supplicant - background scan and roaming interface
 * Copyright (c) 2009-2010, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef BGSCAN_H
#define BGSCAN_H

struct wpa_supplicant;
struct wpa_ssid;

struct bgscan_ops {
	const char *name;

	void * (*init)(struct wpa_supplicant *wpa_s, const char *params,
		       const struct wpa_ssid *ssid);
	void (*deinit)(void *priv);

	int (*notify_scan)(void *priv, struct wpa_scan_results *scan_res);
	void (*notify_beacon_loss)(void *priv);
	void (*notify_signal_change)(void *priv, int above,
				     int current_signal,
				     int current_noise,
				     int current_txrate);
};

#ifdef CONFIG_BGSCAN

int bgscan_init(struct wpa_supplicant *wpa_s, struct wpa_ssid *ssid,
		const char *name);
void bgscan_deinit(struct wpa_supplicant *wpa_s);
int bgscan_notify_scan(struct wpa_supplicant *wpa_s,
		       struct wpa_scan_results *scan_res);
void bgscan_notify_beacon_loss(struct wpa_supplicant *wpa_s);
void bgscan_notify_signal_change(struct wpa_supplicant *wpa_s, int above,
				 int current_signal, int current_noise,
				 int current_txrate);

/* Available bgscan modules */

#ifdef CONFIG_BGSCAN_SIMPLE
extern const struct bgscan_ops bgscan_simple_ops;
#endif /* CONFIG_BGSCAN_SIMPLE */
#ifdef CONFIG_BGSCAN_LEARN
extern const struct bgscan_ops bgscan_learn_ops;
#endif /* CONFIG_BGSCAN_LEARN */

#else /* CONFIG_BGSCAN */

static inline int bgscan_init(struct wpa_supplicant *wpa_s,
			      struct wpa_ssid *ssid, const char name)
{
	return 0;
}

static inline void bgscan_deinit(struct wpa_supplicant *wpa_s)
{
}

static inline int bgscan_notify_scan(struct wpa_supplicant *wpa_s,
				     struct wpa_scan_results *scan_res)
{
	return 0;
}

static inline void bgscan_notify_beacon_loss(struct wpa_supplicant *wpa_s)
{
}

static inline void bgscan_notify_signal_change(struct wpa_supplicant *wpa_s,
					       int above, int current_signal,
					       int current_noise,
					       int current_txrate)
{
}

#endif /* CONFIG_BGSCAN */

#endif /* BGSCAN_H */
