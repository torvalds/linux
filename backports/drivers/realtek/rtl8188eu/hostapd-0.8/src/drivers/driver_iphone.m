/*
 * WPA Supplicant - iPhone/iPod touch Apple80211 driver interface
 * Copyright (c) 2007, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#include "includes.h"
#define Boolean __DummyBoolean
#include <CoreFoundation/CoreFoundation.h>
#undef Boolean

#include "common.h"
#include "driver.h"
#include "eloop.h"
#include "common/ieee802_11_defs.h"

#include "MobileApple80211.h"

struct wpa_driver_iphone_data {
	void *ctx;
	Apple80211Ref wireless_ctx;
	CFArrayRef scan_results;
	int ctrl_power;
};


static const void * cfdict_get_key_str(CFDictionaryRef dict, const char *key)
{
	const void *res;
	CFStringRef str = CFStringCreateWithCString(kCFAllocatorDefault, key,
						    kCFStringEncodingMacRoman);
	if (str == NULL)
		return NULL;

	res = CFDictionaryGetValue(dict, str);
	CFRelease(str);
	return res;
}


static int wpa_driver_iphone_get_ssid(void *priv, u8 *ssid)
{
	struct wpa_driver_iphone_data *drv = priv;
	CFDataRef data;
	int err, len;

	err = Apple80211CopyValue(drv->wireless_ctx, APPLE80211_VALUE_SSID, 0,
				  &data);
	if (err != 0) {
		wpa_printf(MSG_DEBUG, "iPhone: Apple80211CopyValue(SSID) "
			   "failed: %d", err);
		return -1;
	}

	len = CFDataGetLength(data);
	if (len > 32) {
		CFRelease(data);
		return -1;
	}
	os_memcpy(ssid, CFDataGetBytePtr(data), len);
	CFRelease(data);

	return len;
}


static int wpa_driver_iphone_get_bssid(void *priv, u8 *bssid)
{
	struct wpa_driver_iphone_data *drv = priv;
	CFStringRef data;
	int err;
	int a1, a2, a3, a4, a5, a6;

	err = Apple80211CopyValue(drv->wireless_ctx, APPLE80211_VALUE_BSSID, 0,
				  &data);
	if (err != 0) {
		wpa_printf(MSG_DEBUG, "iPhone: Apple80211CopyValue(BSSID) "
			   "failed: %d", err);
		return -1;
	}

	sscanf(CFStringGetCStringPtr(data, kCFStringEncodingMacRoman),
	       "%x:%x:%x:%x:%x:%x", &a1, &a2, &a3, &a4, &a5, &a6);
	bssid[0] = a1;
	bssid[1] = a2;
	bssid[2] = a3;
	bssid[3] = a4;
	bssid[4] = a5;
	bssid[5] = a6;

	CFRelease(data);

	return 0;
}


static void wpa_driver_iphone_scan_timeout(void *eloop_ctx, void *timeout_ctx)
{
	wpa_supplicant_event(timeout_ctx, EVENT_SCAN_RESULTS, NULL);
}


static int wpa_driver_iphone_scan(void *priv, const u8 *ssid, size_t ssid_len)
{
	struct wpa_driver_iphone_data *drv = priv;
	int err;

	if (drv->scan_results) {
		CFRelease(drv->scan_results);
		drv->scan_results = NULL;
	}

	err = Apple80211Scan(drv->wireless_ctx, &drv->scan_results, NULL);
	if (err) {
		wpa_printf(MSG_DEBUG, "iPhone: Apple80211Scan failed: %d",
			   err);
		return -1;
	}

	eloop_register_timeout(0, 0, wpa_driver_iphone_scan_timeout, drv,
			       drv->ctx);
	return 0;
}


static int wpa_driver_iphone_get_scan_results(void *priv,
					      struct wpa_scan_result *results,
					      size_t max_size)
{
	struct wpa_driver_iphone_data *drv = priv;
	size_t i, num;

	if (drv->scan_results == NULL)
		return 0;

	num = CFArrayGetCount(drv->scan_results);
	if (num > max_size)
		num = max_size;
	os_memset(results, 0, num * sizeof(struct wpa_scan_result));

	for (i = 0; i < num; i++) {
		struct wpa_scan_result *res = &results[i];
		CFDictionaryRef dict =
			CFArrayGetValueAtIndex(drv->scan_results, i);
		CFDataRef data;
		CFStringRef str;
		CFNumberRef num;
		int val;

		data = cfdict_get_key_str(dict, "SSID");
		if (data) {
			res->ssid_len = CFDataGetLength(data);
			if (res->ssid_len > 32)
				res->ssid_len = 32;
			os_memcpy(res->ssid, CFDataGetBytePtr(data),
				  res->ssid_len);
		}

		str = cfdict_get_key_str(dict, "BSSID");
		if (str) {
			int a1, a2, a3, a4, a5, a6;
			sscanf(CFStringGetCStringPtr(
				       str, kCFStringEncodingMacRoman),
			       "%x:%x:%x:%x:%x:%x",
			       &a1, &a2, &a3, &a4, &a5, &a6);
			res->bssid[0] = a1;
			res->bssid[1] = a2;
			res->bssid[2] = a3;
			res->bssid[3] = a4;
			res->bssid[4] = a5;
			res->bssid[5] = a6;
		}

		num = cfdict_get_key_str(dict, "CAPABILITIES");
		if (num) {
			if (CFNumberGetValue(num, kCFNumberSInt32Type, &val))
				res->caps = val;
		}

		num = cfdict_get_key_str(dict, "CHANNEL");
		if (num) {
			if (CFNumberGetValue(num, kCFNumberSInt32Type, &val))
				res->freq = 2407 + val * 5;
		}

		num = cfdict_get_key_str(dict, "RSSI");
		if (num) {
			if (CFNumberGetValue(num, kCFNumberSInt32Type, &val))
				res->level = val;
		}

		num = cfdict_get_key_str(dict, "NOISE");
		if (num) {
			if (CFNumberGetValue(num, kCFNumberSInt32Type, &val))
				res->noise = val;
		}

		data = cfdict_get_key_str(dict, "IE");
		if (data) {
			u8 *ptr = (u8 *) CFDataGetBytePtr(data);
			int len = CFDataGetLength(data);
			u8 *pos = ptr, *end = ptr + len;

			while (pos + 2 < end) {
				if (pos + 2 + pos[1] > end)
					break;
				if (pos[0] == WLAN_EID_RSN &&
				    pos[1] <= SSID_MAX_WPA_IE_LEN) {
					os_memcpy(res->rsn_ie, pos,
						  2 + pos[1]);
					res->rsn_ie_len = 2 + pos[1];
				}
				if (pos[0] == WLAN_EID_VENDOR_SPECIFIC &&
				    pos[1] > 4 && pos[2] == 0x00 &&
				    pos[3] == 0x50 && pos[4] == 0xf2 &&
				    pos[5] == 0x01) {
					os_memcpy(res->wpa_ie, pos,
						  2 + pos[1]);
					res->wpa_ie_len = 2 + pos[1];
				}

				pos = pos + 2 + pos[1];
			}
		}
	}

	return num;
}


static void wpa_driver_iphone_assoc_timeout(void *eloop_ctx, void *timeout_ctx)
{
	struct wpa_driver_iphone_data *drv = eloop_ctx;
	u8 bssid[ETH_ALEN];

	if (wpa_driver_iphone_get_bssid(drv, bssid) != 0) {
		eloop_register_timeout(1, 0, wpa_driver_iphone_assoc_timeout,
				       drv, drv->ctx);
		return;
	}

	wpa_supplicant_event(timeout_ctx, EVENT_ASSOC, NULL);
}


static int wpa_driver_iphone_associate(
	void *priv, struct wpa_driver_associate_params *params)
{
	struct wpa_driver_iphone_data *drv = priv;
	int i, num, err;
	size_t ssid_len;
	CFDictionaryRef bss = NULL;

	/*
	 * TODO: Consider generating parameters instead of just using an entry
	 * from scan results in order to support ap_scan=2.
	 */

	if (drv->scan_results == NULL) {
		wpa_printf(MSG_DEBUG, "iPhone: No scan results - cannot "
			   "associate");
		return -1;
	}

	num = CFArrayGetCount(drv->scan_results);

	for (i = 0; i < num; i++) {
		CFDictionaryRef dict =
			CFArrayGetValueAtIndex(drv->scan_results, i);
		CFDataRef data;

		data = cfdict_get_key_str(dict, "SSID");
		if (data == NULL)
			continue;

		ssid_len = CFDataGetLength(data);
		if (ssid_len != params->ssid_len ||
		    os_memcmp(CFDataGetBytePtr(data), params->ssid, ssid_len)
		    != 0)
			continue;

		bss = dict;
		break;
	}

	if (bss == NULL) {
		wpa_printf(MSG_DEBUG, "iPhone: Could not find SSID from scan "
			   "results - cannot associate");
		return -1;
	}

	wpa_printf(MSG_DEBUG, "iPhone: Trying to associate with a BSS found "
		   "from scan results");

	err = Apple80211Associate(drv->wireless_ctx, bss, NULL);
	if (err) {
		wpa_printf(MSG_DEBUG, "iPhone: Apple80211Associate() failed: "
			   "%d", err);
		return -1;
	}

	/*
	 * Driver is actually already associated; report association from an
	 * eloop callback.
	 */
	eloop_cancel_timeout(wpa_driver_iphone_assoc_timeout, drv, drv->ctx);
	eloop_register_timeout(0, 0, wpa_driver_iphone_assoc_timeout, drv,
			       drv->ctx);

	return 0;
}


static int wpa_driver_iphone_set_key(void *priv, wpa_alg alg, const u8 *addr,
				     int key_idx, int set_tx, const u8 *seq,
				     size_t seq_len, const u8 *key,
				     size_t key_len)
{
	/*
	 * TODO: Need to either support configuring PMK for 4-way handshake or
	 * PTK for TKIP/CCMP.
	 */
	return -1;
}


static int wpa_driver_iphone_get_capa(void *priv, struct wpa_driver_capa *capa)
{
	os_memset(capa, 0, sizeof(*capa));

	capa->key_mgmt = WPA_DRIVER_CAPA_KEY_MGMT_WPA |
		WPA_DRIVER_CAPA_KEY_MGMT_WPA2 |
		WPA_DRIVER_CAPA_KEY_MGMT_WPA_PSK |
		WPA_DRIVER_CAPA_KEY_MGMT_WPA2_PSK;
	capa->enc = WPA_DRIVER_CAPA_ENC_WEP40 | WPA_DRIVER_CAPA_ENC_WEP104 |
		WPA_DRIVER_CAPA_ENC_TKIP | WPA_DRIVER_CAPA_ENC_CCMP;
	capa->auth = WPA_DRIVER_AUTH_OPEN | WPA_DRIVER_AUTH_SHARED |
		WPA_DRIVER_AUTH_LEAP;
	capa->flags = WPA_DRIVER_FLAGS_4WAY_HANDSHAKE;

	return 0;
}


static void * wpa_driver_iphone_init(void *ctx, const char *ifname)
{
	struct wpa_driver_iphone_data *drv;
	int err;
	char power;
	CFStringRef name;
	CFDictionaryRef dict;

	drv = os_zalloc(sizeof(*drv));
	if (drv == NULL)
		return NULL;
	drv->ctx = ctx;
	err = Apple80211Open(&drv->wireless_ctx);
	if (err) {
		wpa_printf(MSG_ERROR, "iPhone: Apple80211Open failed: %d",
			   err);
		os_free(drv);
		return NULL;
	}

	name = CFStringCreateWithCString(kCFAllocatorDefault, ifname,
					 kCFStringEncodingISOLatin1);
	if (name == NULL) {
		wpa_printf(MSG_ERROR, "iPhone: ifname -> CFString failed");
		Apple80211Close(drv->wireless_ctx);
		os_free(drv);
		return NULL;
	}

	err = Apple80211BindToInterface(drv->wireless_ctx, name);
	CFRelease(name);

	if (err) {
		wpa_printf(MSG_ERROR, "iPhone: Apple80211BindToInterface "
			   "failed: %d", err);
		Apple80211Close(drv->wireless_ctx);
		os_free(drv);
		return NULL;
	}

	err = Apple80211GetPower(drv->wireless_ctx, &power);
	if (err)
		wpa_printf(MSG_DEBUG, "iPhone: Apple80211GetPower failed: %d",
			   err);

	wpa_printf(MSG_DEBUG, "iPhone: Power=%d", power);

	if (!power) {
		drv->ctrl_power = 1;
		err = Apple80211SetPower(drv->wireless_ctx, 1);
		if (err) {
			wpa_printf(MSG_DEBUG, "iPhone: Apple80211SetPower "
				   "failed: %d", err);
			Apple80211Close(drv->wireless_ctx);
			os_free(drv);
			return NULL;
		}
	}

	err = Apple80211GetInfoCopy(drv->wireless_ctx, &dict);
	if (err == 0) {
		CFShow(dict);
		CFRelease(dict);
	} else {
		printf("Apple80211GetInfoCopy: %d\n", err);
	}

	return drv;
}


static void wpa_driver_iphone_deinit(void *priv)
{
	struct wpa_driver_iphone_data *drv = priv;
	int err;

	eloop_cancel_timeout(wpa_driver_iphone_scan_timeout, drv, drv->ctx);
	eloop_cancel_timeout(wpa_driver_iphone_assoc_timeout, drv, drv->ctx);

	if (drv->ctrl_power) {
		wpa_printf(MSG_DEBUG, "iPhone: Power down the interface");
		err = Apple80211SetPower(drv->wireless_ctx, 0);
		if (err) {
			wpa_printf(MSG_DEBUG, "iPhone: Apple80211SetPower(0) "
				   "failed: %d", err);
		}
	}

	err = Apple80211Close(drv->wireless_ctx);
	if (err) {
		wpa_printf(MSG_DEBUG, "iPhone: Apple80211Close failed: %d",
			   err);
	}

	if (drv->scan_results)
		CFRelease(drv->scan_results);

	os_free(drv);
}


const struct wpa_driver_ops wpa_driver_iphone_ops = {
	.name = "iphone",
	.desc = "iPhone/iPod touch Apple80211 driver",
	.get_ssid = wpa_driver_iphone_get_ssid,
	.get_bssid = wpa_driver_iphone_get_bssid,
	.init = wpa_driver_iphone_init,
	.deinit = wpa_driver_iphone_deinit,
	.scan = wpa_driver_iphone_scan,
	.get_scan_results = wpa_driver_iphone_get_scan_results,
	.associate = wpa_driver_iphone_associate,
	.set_key = wpa_driver_iphone_set_key,
	.get_capa = wpa_driver_iphone_get_capa,
};
