/*
 * hostapd / WPS integration
 * Copyright (c) 2008-2010, Jouni Malinen <j@w1.fi>
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

#include "utils/includes.h"

#include "utils/common.h"
#include "utils/eloop.h"
#include "utils/uuid.h"
#include "crypto/dh_groups.h"
#include "common/wpa_ctrl.h"
#include "common/ieee802_11_defs.h"
#include "common/ieee802_11_common.h"
#include "eapol_auth/eapol_auth_sm.h"
#include "eapol_auth/eapol_auth_sm_i.h"
#include "wps/wps.h"
#include "wps/wps_defs.h"
#include "wps/wps_dev_attr.h"
#include "hostapd.h"
#include "ap_config.h"
#include "ap_drv_ops.h"
#include "beacon.h"
#include "sta_info.h"
#include "wps_hostapd.h"


#ifdef CONFIG_WPS_UPNP
#include "wps/wps_upnp.h"
static int hostapd_wps_upnp_init(struct hostapd_data *hapd,
				 struct wps_context *wps);
static void hostapd_wps_upnp_deinit(struct hostapd_data *hapd);
#endif /* CONFIG_WPS_UPNP */

static int hostapd_wps_probe_req_rx(void *ctx, const u8 *addr,
				    const u8 *ie, size_t ie_len);
static void hostapd_wps_ap_pin_timeout(void *eloop_data, void *user_ctx);


struct wps_for_each_data {
	int (*func)(struct hostapd_data *h, void *ctx);
	void *ctx;
};


static int wps_for_each(struct hostapd_iface *iface, void *ctx)
{
	struct wps_for_each_data *data = ctx;
	size_t j;

	if (iface == NULL)
		return 0;
	for (j = 0; j < iface->num_bss; j++) {
		struct hostapd_data *hapd = iface->bss[j];
		int ret = data->func(hapd, data->ctx);
		if (ret)
			return ret;
	}

	return 0;
}


static int hostapd_wps_for_each(struct hostapd_data *hapd,
				int (*func)(struct hostapd_data *h, void *ctx),
				void *ctx)
{
	struct hostapd_iface *iface = hapd->iface;
	struct wps_for_each_data data;
	data.func = func;
	data.ctx = ctx;
	if (iface->for_each_interface == NULL)
		return wps_for_each(iface, &data);
	return iface->for_each_interface(iface->interfaces, wps_for_each,
					 &data);
}


static int hostapd_wps_new_psk_cb(void *ctx, const u8 *mac_addr, const u8 *psk,
				  size_t psk_len)
{
	struct hostapd_data *hapd = ctx;
	struct hostapd_wpa_psk *p;
	struct hostapd_ssid *ssid = &hapd->conf->ssid;

	wpa_printf(MSG_DEBUG, "Received new WPA/WPA2-PSK from WPS for STA "
		   MACSTR, MAC2STR(mac_addr));
	wpa_hexdump_key(MSG_DEBUG, "Per-device PSK", psk, psk_len);

	if (psk_len != PMK_LEN) {
		wpa_printf(MSG_DEBUG, "Unexpected PSK length %lu",
			   (unsigned long) psk_len);
		return -1;
	}

	/* Add the new PSK to runtime PSK list */
	p = os_zalloc(sizeof(*p));
	if (p == NULL)
		return -1;
	os_memcpy(p->addr, mac_addr, ETH_ALEN);
	os_memcpy(p->psk, psk, PMK_LEN);

	p->next = ssid->wpa_psk;
	ssid->wpa_psk = p;

	if (ssid->wpa_psk_file) {
		FILE *f;
		char hex[PMK_LEN * 2 + 1];
		/* Add the new PSK to PSK list file */
		f = fopen(ssid->wpa_psk_file, "a");
		if (f == NULL) {
			wpa_printf(MSG_DEBUG, "Failed to add the PSK to "
				   "'%s'", ssid->wpa_psk_file);
			return -1;
		}

		wpa_snprintf_hex(hex, sizeof(hex), psk, psk_len);
		fprintf(f, MACSTR " %s\n", MAC2STR(mac_addr), hex);
		fclose(f);
	}

	return 0;
}


static int hostapd_wps_set_ie_cb(void *ctx, struct wpabuf *beacon_ie,
				 struct wpabuf *probe_resp_ie)
{
	struct hostapd_data *hapd = ctx;
	wpabuf_free(hapd->wps_beacon_ie);
	hapd->wps_beacon_ie = beacon_ie;
	wpabuf_free(hapd->wps_probe_resp_ie);
	hapd->wps_probe_resp_ie = probe_resp_ie;
	ieee802_11_set_beacon(hapd);
	return hostapd_set_ap_wps_ie(hapd);
}


static void hostapd_wps_pin_needed_cb(void *ctx, const u8 *uuid_e,
				      const struct wps_device_data *dev)
{
	struct hostapd_data *hapd = ctx;
	char uuid[40], txt[400];
	int len;
	char devtype[WPS_DEV_TYPE_BUFSIZE];
	if (uuid_bin2str(uuid_e, uuid, sizeof(uuid)))
		return;
	wpa_printf(MSG_DEBUG, "WPS: PIN needed for E-UUID %s", uuid);
	len = os_snprintf(txt, sizeof(txt), WPS_EVENT_PIN_NEEDED
			  "%s " MACSTR " [%s|%s|%s|%s|%s|%s]",
			  uuid, MAC2STR(dev->mac_addr), dev->device_name,
			  dev->manufacturer, dev->model_name,
			  dev->model_number, dev->serial_number,
			  wps_dev_type_bin2str(dev->pri_dev_type, devtype,
					       sizeof(devtype)));
	if (len > 0 && len < (int) sizeof(txt))
		wpa_msg(hapd->msg_ctx, MSG_INFO, "%s", txt);

	if (hapd->conf->wps_pin_requests) {
		FILE *f;
		struct os_time t;
		f = fopen(hapd->conf->wps_pin_requests, "a");
		if (f == NULL)
			return;
		os_get_time(&t);
		fprintf(f, "%ld\t%s\t" MACSTR "\t%s\t%s\t%s\t%s\t%s"
			"\t%s\n",
			t.sec, uuid, MAC2STR(dev->mac_addr), dev->device_name,
			dev->manufacturer, dev->model_name, dev->model_number,
			dev->serial_number,
			wps_dev_type_bin2str(dev->pri_dev_type, devtype,
					     sizeof(devtype)));
		fclose(f);
	}
}


static void hostapd_wps_reg_success_cb(void *ctx, const u8 *mac_addr,
				       const u8 *uuid_e)
{
	struct hostapd_data *hapd = ctx;
	char uuid[40];
	if (uuid_bin2str(uuid_e, uuid, sizeof(uuid)))
		return;
	wpa_msg(hapd->msg_ctx, MSG_INFO, WPS_EVENT_REG_SUCCESS MACSTR " %s",
		MAC2STR(mac_addr), uuid);
	if (hapd->wps_reg_success_cb)
		hapd->wps_reg_success_cb(hapd->wps_reg_success_cb_ctx,
					 mac_addr, uuid_e);
}


static void hostapd_wps_enrollee_seen_cb(void *ctx, const u8 *addr,
					 const u8 *uuid_e,
					 const u8 *pri_dev_type,
					 u16 config_methods,
					 u16 dev_password_id, u8 request_type,
					 const char *dev_name)
{
	struct hostapd_data *hapd = ctx;
	char uuid[40];
	char devtype[WPS_DEV_TYPE_BUFSIZE];
	if (uuid_bin2str(uuid_e, uuid, sizeof(uuid)))
		return;
	if (dev_name == NULL)
		dev_name = "";
	wpa_msg_ctrl(hapd->msg_ctx, MSG_INFO, WPS_EVENT_ENROLLEE_SEEN MACSTR
		     " %s %s 0x%x %u %u [%s]",
		     MAC2STR(addr), uuid,
		     wps_dev_type_bin2str(pri_dev_type, devtype,
					  sizeof(devtype)),
		     config_methods, dev_password_id, request_type, dev_name);
}


static int str_starts(const char *str, const char *start)
{
	return os_strncmp(str, start, os_strlen(start)) == 0;
}


static void wps_reload_config(void *eloop_data, void *user_ctx)
{
	struct hostapd_iface *iface = eloop_data;

	wpa_printf(MSG_DEBUG, "WPS: Reload configuration data");
	if (iface->reload_config(iface) < 0) {
		wpa_printf(MSG_WARNING, "WPS: Failed to reload the updated "
			   "configuration");
	}
}


static int hapd_wps_cred_cb(struct hostapd_data *hapd, void *ctx)
{
	const struct wps_credential *cred = ctx;
	FILE *oconf, *nconf;
	size_t len, i;
	char *tmp_fname;
	char buf[1024];
	int multi_bss;
	int wpa;

	if (hapd->wps == NULL)
		return 0;

	wpa_hexdump_key(MSG_DEBUG, "WPS: Received Credential attribute",
			cred->cred_attr, cred->cred_attr_len);

	wpa_printf(MSG_DEBUG, "WPS: Received new AP Settings");
	wpa_hexdump_ascii(MSG_DEBUG, "WPS: SSID", cred->ssid, cred->ssid_len);
	wpa_printf(MSG_DEBUG, "WPS: Authentication Type 0x%x",
		   cred->auth_type);
	wpa_printf(MSG_DEBUG, "WPS: Encryption Type 0x%x", cred->encr_type);
	wpa_printf(MSG_DEBUG, "WPS: Network Key Index %d", cred->key_idx);
	wpa_hexdump_key(MSG_DEBUG, "WPS: Network Key",
			cred->key, cred->key_len);
	wpa_printf(MSG_DEBUG, "WPS: MAC Address " MACSTR,
		   MAC2STR(cred->mac_addr));

	if ((hapd->conf->wps_cred_processing == 1 ||
	     hapd->conf->wps_cred_processing == 2) && cred->cred_attr) {
		size_t blen = cred->cred_attr_len * 2 + 1;
		char *_buf = os_malloc(blen);
		if (_buf) {
			wpa_snprintf_hex(_buf, blen,
					 cred->cred_attr, cred->cred_attr_len);
			wpa_msg(hapd->msg_ctx, MSG_INFO, "%s%s",
				WPS_EVENT_NEW_AP_SETTINGS, _buf);
			os_free(_buf);
		}
	} else
		wpa_msg(hapd->msg_ctx, MSG_INFO, WPS_EVENT_NEW_AP_SETTINGS);

	if (hapd->conf->wps_cred_processing == 1)
		return 0;

	os_memcpy(hapd->wps->ssid, cred->ssid, cred->ssid_len);
	hapd->wps->ssid_len = cred->ssid_len;
	hapd->wps->encr_types = cred->encr_type;
	hapd->wps->auth_types = cred->auth_type;
	if (cred->key_len == 0) {
		os_free(hapd->wps->network_key);
		hapd->wps->network_key = NULL;
		hapd->wps->network_key_len = 0;
	} else {
		if (hapd->wps->network_key == NULL ||
		    hapd->wps->network_key_len < cred->key_len) {
			hapd->wps->network_key_len = 0;
			os_free(hapd->wps->network_key);
			hapd->wps->network_key = os_malloc(cred->key_len);
			if (hapd->wps->network_key == NULL)
				return -1;
		}
		hapd->wps->network_key_len = cred->key_len;
		os_memcpy(hapd->wps->network_key, cred->key, cred->key_len);
	}
	hapd->wps->wps_state = WPS_STATE_CONFIGURED;

	len = os_strlen(hapd->iface->config_fname) + 5;
	tmp_fname = os_malloc(len);
	if (tmp_fname == NULL)
		return -1;
	os_snprintf(tmp_fname, len, "%s-new", hapd->iface->config_fname);

	oconf = fopen(hapd->iface->config_fname, "r");
	if (oconf == NULL) {
		wpa_printf(MSG_WARNING, "WPS: Could not open current "
			   "configuration file");
		os_free(tmp_fname);
		return -1;
	}

	nconf = fopen(tmp_fname, "w");
	if (nconf == NULL) {
		wpa_printf(MSG_WARNING, "WPS: Could not write updated "
			   "configuration file");
		os_free(tmp_fname);
		fclose(oconf);
		return -1;
	}

	fprintf(nconf, "# WPS configuration - START\n");

	fprintf(nconf, "wps_state=2\n");

	fprintf(nconf, "ssid=");
	for (i = 0; i < cred->ssid_len; i++)
		fputc(cred->ssid[i], nconf);
	fprintf(nconf, "\n");

	if ((cred->auth_type & (WPS_AUTH_WPA2 | WPS_AUTH_WPA2PSK)) &&
	    (cred->auth_type & (WPS_AUTH_WPA | WPS_AUTH_WPAPSK)))
		wpa = 3;
	else if (cred->auth_type & (WPS_AUTH_WPA2 | WPS_AUTH_WPA2PSK))
		wpa = 2;
	else if (cred->auth_type & (WPS_AUTH_WPA | WPS_AUTH_WPAPSK))
		wpa = 1;
	else
		wpa = 0;

	if (wpa) {
		char *prefix;
		fprintf(nconf, "wpa=%d\n", wpa);

		fprintf(nconf, "wpa_key_mgmt=");
		prefix = "";
		if (cred->auth_type & (WPS_AUTH_WPA2 | WPS_AUTH_WPA)) {
			fprintf(nconf, "WPA-EAP");
			prefix = " ";
		}
		if (cred->auth_type & (WPS_AUTH_WPA2PSK | WPS_AUTH_WPAPSK))
			fprintf(nconf, "%sWPA-PSK", prefix);
		fprintf(nconf, "\n");

		fprintf(nconf, "wpa_pairwise=");
		prefix = "";
		if (cred->encr_type & WPS_ENCR_AES) {
			fprintf(nconf, "CCMP");
			prefix = " ";
		}
		if (cred->encr_type & WPS_ENCR_TKIP) {
			fprintf(nconf, "%sTKIP", prefix);
		}
		fprintf(nconf, "\n");

		if (cred->key_len >= 8 && cred->key_len < 64) {
			fprintf(nconf, "wpa_passphrase=");
			for (i = 0; i < cred->key_len; i++)
				fputc(cred->key[i], nconf);
			fprintf(nconf, "\n");
		} else if (cred->key_len == 64) {
			fprintf(nconf, "wpa_psk=");
			for (i = 0; i < cred->key_len; i++)
				fputc(cred->key[i], nconf);
			fprintf(nconf, "\n");
		} else {
			wpa_printf(MSG_WARNING, "WPS: Invalid key length %lu "
				   "for WPA/WPA2",
				   (unsigned long) cred->key_len);
		}

		fprintf(nconf, "auth_algs=1\n");
	} else {
		if ((cred->auth_type & WPS_AUTH_OPEN) &&
		    (cred->auth_type & WPS_AUTH_SHARED))
			fprintf(nconf, "auth_algs=3\n");
		else if (cred->auth_type & WPS_AUTH_SHARED)
			fprintf(nconf, "auth_algs=2\n");
		else
			fprintf(nconf, "auth_algs=1\n");

		if (cred->encr_type & WPS_ENCR_WEP && cred->key_idx <= 4) {
			int key_idx = cred->key_idx;
			if (key_idx)
				key_idx--;
			fprintf(nconf, "wep_default_key=%d\n", key_idx);
			fprintf(nconf, "wep_key%d=", key_idx);
			if (cred->key_len == 10 || cred->key_len == 26) {
				/* WEP key as a hex string */
				for (i = 0; i < cred->key_len; i++)
					fputc(cred->key[i], nconf);
			} else {
				/* Raw WEP key; convert to hex */
				for (i = 0; i < cred->key_len; i++)
					fprintf(nconf, "%02x", cred->key[i]);
			}
			fprintf(nconf, "\n");
		}
	}

	fprintf(nconf, "# WPS configuration - END\n");

	multi_bss = 0;
	while (fgets(buf, sizeof(buf), oconf)) {
		if (os_strncmp(buf, "bss=", 4) == 0)
			multi_bss = 1;
		if (!multi_bss &&
		    (str_starts(buf, "ssid=") ||
		     str_starts(buf, "auth_algs=") ||
		     str_starts(buf, "wep_default_key=") ||
		     str_starts(buf, "wep_key") ||
		     str_starts(buf, "wps_state=") ||
		     str_starts(buf, "wpa=") ||
		     str_starts(buf, "wpa_psk=") ||
		     str_starts(buf, "wpa_pairwise=") ||
		     str_starts(buf, "rsn_pairwise=") ||
		     str_starts(buf, "wpa_key_mgmt=") ||
		     str_starts(buf, "wpa_passphrase="))) {
			fprintf(nconf, "#WPS# %s", buf);
		} else
			fprintf(nconf, "%s", buf);
	}

	fclose(nconf);
	fclose(oconf);

	if (rename(tmp_fname, hapd->iface->config_fname) < 0) {
		wpa_printf(MSG_WARNING, "WPS: Failed to rename the updated "
			   "configuration file: %s", strerror(errno));
		os_free(tmp_fname);
		return -1;
	}

	os_free(tmp_fname);

	/* Schedule configuration reload after short period of time to allow
	 * EAP-WSC to be finished.
	 */
	eloop_register_timeout(0, 100000, wps_reload_config, hapd->iface,
			       NULL);

	wpa_printf(MSG_DEBUG, "WPS: AP configuration updated");

	return 0;
}


static int hostapd_wps_cred_cb(void *ctx, const struct wps_credential *cred)
{
	struct hostapd_data *hapd = ctx;
	return hostapd_wps_for_each(hapd, hapd_wps_cred_cb, (void *) cred);
}


static void hostapd_wps_reenable_ap_pin(void *eloop_data, void *user_ctx)
{
	struct hostapd_data *hapd = eloop_data;

	if (hapd->conf->ap_setup_locked)
		return;

	wpa_printf(MSG_DEBUG, "WPS: Re-enable AP PIN");
	wpa_msg(hapd->msg_ctx, MSG_INFO, WPS_EVENT_AP_SETUP_UNLOCKED);
	hapd->wps->ap_setup_locked = 0;
	wps_registrar_update_ie(hapd->wps->registrar);
}


static int wps_pwd_auth_fail(struct hostapd_data *hapd, void *ctx)
{
	struct wps_event_pwd_auth_fail *data = ctx;

	if (!data->enrollee || hapd->conf->ap_pin == NULL || hapd->wps == NULL)
		return 0;

	/*
	 * Registrar failed to prove its knowledge of the AP PIN. Lock AP setup
	 * for some time if this happens multiple times to slow down brute
	 * force attacks.
	 */
	hapd->ap_pin_failures++;
	wpa_printf(MSG_DEBUG, "WPS: AP PIN authentication failure number %u",
		   hapd->ap_pin_failures);
	if (hapd->ap_pin_failures < 3)
		return 0;

	wpa_msg(hapd->msg_ctx, MSG_INFO, WPS_EVENT_AP_SETUP_LOCKED);
	hapd->wps->ap_setup_locked = 1;

	wps_registrar_update_ie(hapd->wps->registrar);

	if (!hapd->conf->ap_setup_locked) {
		if (hapd->ap_pin_lockout_time == 0)
			hapd->ap_pin_lockout_time = 60;
		else if (hapd->ap_pin_lockout_time < 365 * 24 * 60 * 60 &&
			 (hapd->ap_pin_failures % 3) == 0)
			hapd->ap_pin_lockout_time *= 2;

		wpa_printf(MSG_DEBUG, "WPS: Disable AP PIN for %u seconds",
			   hapd->ap_pin_lockout_time);
		eloop_cancel_timeout(hostapd_wps_reenable_ap_pin, hapd, NULL);
		eloop_register_timeout(hapd->ap_pin_lockout_time, 0,
				       hostapd_wps_reenable_ap_pin, hapd,
				       NULL);
	}

	return 0;
}


static void hostapd_pwd_auth_fail(struct hostapd_data *hapd,
				  struct wps_event_pwd_auth_fail *data)
{
	hostapd_wps_for_each(hapd, wps_pwd_auth_fail, data);
}


static const char * wps_event_fail_reason[NUM_WPS_EI_VALUES] = {
	"No Error", /* WPS_EI_NO_ERROR */
	"TKIP Only Prohibited", /* WPS_EI_SECURITY_TKIP_ONLY_PROHIBITED */
	"WEP Prohibited" /* WPS_EI_SECURITY_WEP_PROHIBITED */
};

static void hostapd_wps_event_fail(struct hostapd_data *hapd,
				   struct wps_event_fail *fail)
{
	if (fail->error_indication > 0 &&
	    fail->error_indication < NUM_WPS_EI_VALUES) {
		wpa_msg(hapd->msg_ctx, MSG_INFO,
			WPS_EVENT_FAIL "msg=%d config_error=%d reason=%d (%s)",
			fail->msg, fail->config_error, fail->error_indication,
			wps_event_fail_reason[fail->error_indication]);
	} else {
		wpa_msg(hapd->msg_ctx, MSG_INFO,
			WPS_EVENT_FAIL "msg=%d config_error=%d",
			fail->msg, fail->config_error);
	}
}


static void hostapd_wps_event_cb(void *ctx, enum wps_event event,
				 union wps_event_data *data)
{
	struct hostapd_data *hapd = ctx;

	switch (event) {
	case WPS_EV_M2D:
		break;
	case WPS_EV_FAIL:
		hostapd_wps_event_fail(hapd, &data->fail);
		break;
	case WPS_EV_SUCCESS:
		break;
	case WPS_EV_PWD_AUTH_FAIL:
		hostapd_pwd_auth_fail(hapd, &data->pwd_auth_fail);
		break;
	case WPS_EV_PBC_OVERLAP:
		break;
	case WPS_EV_PBC_TIMEOUT:
		break;
	case WPS_EV_ER_AP_ADD:
		break;
	case WPS_EV_ER_AP_REMOVE:
		break;
	case WPS_EV_ER_ENROLLEE_ADD:
		break;
	case WPS_EV_ER_ENROLLEE_REMOVE:
		break;
	case WPS_EV_ER_AP_SETTINGS:
		break;
	case WPS_EV_ER_SET_SELECTED_REGISTRAR:
		break;
	}
	if (hapd->wps_event_cb)
		hapd->wps_event_cb(hapd->wps_event_cb_ctx, event, data);
}


static void hostapd_wps_clear_ies(struct hostapd_data *hapd)
{
	wpabuf_free(hapd->wps_beacon_ie);
	hapd->wps_beacon_ie = NULL;

	wpabuf_free(hapd->wps_probe_resp_ie);
	hapd->wps_probe_resp_ie = NULL;

	hostapd_set_ap_wps_ie(hapd);
}


static int get_uuid_cb(struct hostapd_iface *iface, void *ctx)
{
	const u8 **uuid = ctx;
	size_t j;

	if (iface == NULL)
		return 0;
	for (j = 0; j < iface->num_bss; j++) {
		struct hostapd_data *hapd = iface->bss[j];
		if (hapd->wps && !is_nil_uuid(hapd->wps->uuid)) {
			*uuid = hapd->wps->uuid;
			return 1;
		}
	}

	return 0;
}


static const u8 * get_own_uuid(struct hostapd_iface *iface)
{
	const u8 *uuid;
	if (iface->for_each_interface == NULL)
		return NULL;
	uuid = NULL;
	iface->for_each_interface(iface->interfaces, get_uuid_cb, &uuid);
	return uuid;
}


static int count_interface_cb(struct hostapd_iface *iface, void *ctx)
{
	int *count= ctx;
	(*count)++;
	return 0;
}


static int interface_count(struct hostapd_iface *iface)
{
	int count = 0;
	if (iface->for_each_interface == NULL)
		return 0;
	iface->for_each_interface(iface->interfaces, count_interface_cb,
				  &count);
	return count;
}


static int hostapd_wps_set_vendor_ext(struct hostapd_data *hapd,
				      struct wps_context *wps)
{
	int i;

	for (i = 0; i < MAX_WPS_VENDOR_EXTENSIONS; i++) {
		wpabuf_free(wps->dev.vendor_ext[i]);
		wps->dev.vendor_ext[i] = NULL;

		if (hapd->conf->wps_vendor_ext[i] == NULL)
			continue;

		wps->dev.vendor_ext[i] =
			wpabuf_dup(hapd->conf->wps_vendor_ext[i]);
		if (wps->dev.vendor_ext[i] == NULL) {
			while (--i >= 0)
				wpabuf_free(wps->dev.vendor_ext[i]);
			return -1;
		}
	}

	return 0;
}


int hostapd_init_wps(struct hostapd_data *hapd,
		     struct hostapd_bss_config *conf)
{
	struct wps_context *wps;
	struct wps_registrar_config cfg;

	if (conf->wps_state == 0) {
		hostapd_wps_clear_ies(hapd);
		return 0;
	}

	wps = os_zalloc(sizeof(*wps));
	if (wps == NULL)
		return -1;

	wps->cred_cb = hostapd_wps_cred_cb;
	wps->event_cb = hostapd_wps_event_cb;
	wps->cb_ctx = hapd;

	os_memset(&cfg, 0, sizeof(cfg));
	wps->wps_state = hapd->conf->wps_state;
	wps->ap_setup_locked = hapd->conf->ap_setup_locked;
	if (is_nil_uuid(hapd->conf->uuid)) {
		const u8 *uuid;
		uuid = get_own_uuid(hapd->iface);
		if (uuid) {
			os_memcpy(wps->uuid, uuid, UUID_LEN);
			wpa_hexdump(MSG_DEBUG, "WPS: Clone UUID from another "
				    "interface", wps->uuid, UUID_LEN);
		} else {
			uuid_gen_mac_addr(hapd->own_addr, wps->uuid);
			wpa_hexdump(MSG_DEBUG, "WPS: UUID based on MAC "
				    "address", wps->uuid, UUID_LEN);
		}
	} else {
		os_memcpy(wps->uuid, hapd->conf->uuid, UUID_LEN);
		wpa_hexdump(MSG_DEBUG, "WPS: Use configured UUID",
			    wps->uuid, UUID_LEN);
	}
	wps->ssid_len = hapd->conf->ssid.ssid_len;
	os_memcpy(wps->ssid, hapd->conf->ssid.ssid, wps->ssid_len);
	wps->ap = 1;
	os_memcpy(wps->dev.mac_addr, hapd->own_addr, ETH_ALEN);
	wps->dev.device_name = hapd->conf->device_name ?
		os_strdup(hapd->conf->device_name) : NULL;
	wps->dev.manufacturer = hapd->conf->manufacturer ?
		os_strdup(hapd->conf->manufacturer) : NULL;
	wps->dev.model_name = hapd->conf->model_name ?
		os_strdup(hapd->conf->model_name) : NULL;
	wps->dev.model_number = hapd->conf->model_number ?
		os_strdup(hapd->conf->model_number) : NULL;
	wps->dev.serial_number = hapd->conf->serial_number ?
		os_strdup(hapd->conf->serial_number) : NULL;
	wps->config_methods =
		wps_config_methods_str2bin(hapd->conf->config_methods);
#ifdef CONFIG_WPS2
	if ((wps->config_methods &
	     (WPS_CONFIG_DISPLAY | WPS_CONFIG_VIRT_DISPLAY |
	      WPS_CONFIG_PHY_DISPLAY)) == WPS_CONFIG_DISPLAY) {
		wpa_printf(MSG_INFO, "WPS: Converting display to "
			   "virtual_display for WPS 2.0 compliance");
		wps->config_methods |= WPS_CONFIG_VIRT_DISPLAY;
	}
	if ((wps->config_methods &
	     (WPS_CONFIG_PUSHBUTTON | WPS_CONFIG_VIRT_PUSHBUTTON |
	      WPS_CONFIG_PHY_PUSHBUTTON)) == WPS_CONFIG_PUSHBUTTON) {
		wpa_printf(MSG_INFO, "WPS: Converting push_button to "
			   "virtual_push_button for WPS 2.0 compliance");
		wps->config_methods |= WPS_CONFIG_VIRT_PUSHBUTTON;
	}
#endif /* CONFIG_WPS2 */
	os_memcpy(wps->dev.pri_dev_type, hapd->conf->device_type,
		  WPS_DEV_TYPE_LEN);

	if (hostapd_wps_set_vendor_ext(hapd, wps) < 0) {
		os_free(wps);
		return -1;
	}

	wps->dev.os_version = WPA_GET_BE32(hapd->conf->os_version);
	wps->dev.rf_bands = hapd->iconf->hw_mode == HOSTAPD_MODE_IEEE80211A ?
		WPS_RF_50GHZ : WPS_RF_24GHZ; /* FIX: dualband AP */

	if (conf->wpa & WPA_PROTO_RSN) {
		if (conf->wpa_key_mgmt & WPA_KEY_MGMT_PSK)
			wps->auth_types |= WPS_AUTH_WPA2PSK;
		if (conf->wpa_key_mgmt & WPA_KEY_MGMT_IEEE8021X)
			wps->auth_types |= WPS_AUTH_WPA2;

		if (conf->rsn_pairwise & WPA_CIPHER_CCMP)
			wps->encr_types |= WPS_ENCR_AES;
		if (conf->rsn_pairwise & WPA_CIPHER_TKIP)
			wps->encr_types |= WPS_ENCR_TKIP;
	}

	if (conf->wpa & WPA_PROTO_WPA) {
		if (conf->wpa_key_mgmt & WPA_KEY_MGMT_PSK)
			wps->auth_types |= WPS_AUTH_WPAPSK;
		if (conf->wpa_key_mgmt & WPA_KEY_MGMT_IEEE8021X)
			wps->auth_types |= WPS_AUTH_WPA;

		if (conf->wpa_pairwise & WPA_CIPHER_CCMP)
			wps->encr_types |= WPS_ENCR_AES;
		if (conf->wpa_pairwise & WPA_CIPHER_TKIP)
			wps->encr_types |= WPS_ENCR_TKIP;
	}

	if (conf->ssid.security_policy == SECURITY_PLAINTEXT) {
		wps->encr_types |= WPS_ENCR_NONE;
		wps->auth_types |= WPS_AUTH_OPEN;
	} else if (conf->ssid.security_policy == SECURITY_STATIC_WEP) {
		wps->encr_types |= WPS_ENCR_WEP;
		if (conf->auth_algs & WPA_AUTH_ALG_OPEN)
			wps->auth_types |= WPS_AUTH_OPEN;
		if (conf->auth_algs & WPA_AUTH_ALG_SHARED)
			wps->auth_types |= WPS_AUTH_SHARED;
	} else if (conf->ssid.security_policy == SECURITY_IEEE_802_1X) {
		wps->auth_types |= WPS_AUTH_OPEN;
		if (conf->default_wep_key_len)
			wps->encr_types |= WPS_ENCR_WEP;
		else
			wps->encr_types |= WPS_ENCR_NONE;
	}

	if (conf->ssid.wpa_psk_file) {
		/* Use per-device PSKs */
	} else if (conf->ssid.wpa_passphrase) {
		wps->network_key = (u8 *) os_strdup(conf->ssid.wpa_passphrase);
		wps->network_key_len = os_strlen(conf->ssid.wpa_passphrase);
	} else if (conf->ssid.wpa_psk) {
		wps->network_key = os_malloc(2 * PMK_LEN + 1);
		if (wps->network_key == NULL) {
			os_free(wps);
			return -1;
		}
		wpa_snprintf_hex((char *) wps->network_key, 2 * PMK_LEN + 1,
				 conf->ssid.wpa_psk->psk, PMK_LEN);
		wps->network_key_len = 2 * PMK_LEN;
	} else if (conf->ssid.wep.keys_set && conf->ssid.wep.key[0]) {
		wps->network_key = os_malloc(conf->ssid.wep.len[0]);
		if (wps->network_key == NULL) {
			os_free(wps);
			return -1;
		}
		os_memcpy(wps->network_key, conf->ssid.wep.key[0],
			  conf->ssid.wep.len[0]);
		wps->network_key_len = conf->ssid.wep.len[0];
	}

	if (conf->ssid.wpa_psk) {
		os_memcpy(wps->psk, conf->ssid.wpa_psk->psk, PMK_LEN);
		wps->psk_set = 1;
	}

	if (conf->wps_state == WPS_STATE_NOT_CONFIGURED) {
		/* Override parameters to enable security by default */
		wps->auth_types = WPS_AUTH_WPA2PSK | WPS_AUTH_WPAPSK;
		wps->encr_types = WPS_ENCR_AES | WPS_ENCR_TKIP;
	}

	wps->ap_settings = conf->ap_settings;
	wps->ap_settings_len = conf->ap_settings_len;

	cfg.new_psk_cb = hostapd_wps_new_psk_cb;
	cfg.set_ie_cb = hostapd_wps_set_ie_cb;
	cfg.pin_needed_cb = hostapd_wps_pin_needed_cb;
	cfg.reg_success_cb = hostapd_wps_reg_success_cb;
	cfg.enrollee_seen_cb = hostapd_wps_enrollee_seen_cb;
	cfg.cb_ctx = hapd;
	cfg.skip_cred_build = conf->skip_cred_build;
	cfg.extra_cred = conf->extra_cred;
	cfg.extra_cred_len = conf->extra_cred_len;
	cfg.disable_auto_conf = (hapd->conf->wps_cred_processing == 1) &&
		conf->skip_cred_build;
	if (conf->ssid.security_policy == SECURITY_STATIC_WEP)
		cfg.static_wep_only = 1;
	cfg.dualband = interface_count(hapd->iface) > 1;
	if (cfg.dualband)
		wpa_printf(MSG_DEBUG, "WPS: Dualband AP");

	wps->registrar = wps_registrar_init(wps, &cfg);
	if (wps->registrar == NULL) {
		wpa_printf(MSG_ERROR, "Failed to initialize WPS Registrar");
		os_free(wps->network_key);
		os_free(wps);
		return -1;
	}

#ifdef CONFIG_WPS_UPNP
	wps->friendly_name = hapd->conf->friendly_name;
	wps->manufacturer_url = hapd->conf->manufacturer_url;
	wps->model_description = hapd->conf->model_description;
	wps->model_url = hapd->conf->model_url;
	wps->upc = hapd->conf->upc;

	if (hostapd_wps_upnp_init(hapd, wps) < 0) {
		wpa_printf(MSG_ERROR, "Failed to initialize WPS UPnP");
		wps_registrar_deinit(wps->registrar);
		os_free(wps->network_key);
		os_free(wps);
		return -1;
	}
#endif /* CONFIG_WPS_UPNP */

	hostapd_register_probereq_cb(hapd, hostapd_wps_probe_req_rx, hapd);

	hapd->wps = wps;

	return 0;
}


void hostapd_deinit_wps(struct hostapd_data *hapd)
{
	eloop_cancel_timeout(hostapd_wps_reenable_ap_pin, hapd, NULL);
	eloop_cancel_timeout(hostapd_wps_ap_pin_timeout, hapd, NULL);
	if (hapd->wps == NULL)
		return;
#ifdef CONFIG_WPS_UPNP
	hostapd_wps_upnp_deinit(hapd);
#endif /* CONFIG_WPS_UPNP */
	wps_registrar_deinit(hapd->wps->registrar);
	os_free(hapd->wps->network_key);
	wps_device_data_free(&hapd->wps->dev);
	wpabuf_free(hapd->wps->dh_pubkey);
	wpabuf_free(hapd->wps->dh_privkey);
	wpabuf_free(hapd->wps->oob_conf.pubkey_hash);
	wpabuf_free(hapd->wps->oob_conf.dev_password);
	wps_free_pending_msgs(hapd->wps->upnp_msgs);
	os_free(hapd->wps);
	hapd->wps = NULL;
	hostapd_wps_clear_ies(hapd);
}


void hostapd_update_wps(struct hostapd_data *hapd)
{
	if (hapd->wps == NULL)
		return;

#ifdef CONFIG_WPS_UPNP
	hapd->wps->friendly_name = hapd->conf->friendly_name;
	hapd->wps->manufacturer_url = hapd->conf->manufacturer_url;
	hapd->wps->model_description = hapd->conf->model_description;
	hapd->wps->model_url = hapd->conf->model_url;
	hapd->wps->upc = hapd->conf->upc;
#endif /* CONFIG_WPS_UPNP */

	hostapd_wps_set_vendor_ext(hapd, hapd->wps);

	if (hapd->conf->wps_state)
		wps_registrar_update_ie(hapd->wps->registrar);
	else
		hostapd_deinit_wps(hapd);
}


struct wps_add_pin_data {
	const u8 *addr;
	const u8 *uuid;
	const u8 *pin;
	size_t pin_len;
	int timeout;
	int added;
};


static int wps_add_pin(struct hostapd_data *hapd, void *ctx)
{
	struct wps_add_pin_data *data = ctx;
	int ret;

	if (hapd->wps == NULL)
		return 0;
	ret = wps_registrar_add_pin(hapd->wps->registrar, data->addr,
				    data->uuid, data->pin, data->pin_len,
				    data->timeout);
	if (ret == 0)
		data->added++;
	return ret;
}


int hostapd_wps_add_pin(struct hostapd_data *hapd, const u8 *addr,
			const char *uuid, const char *pin, int timeout)
{
	u8 u[UUID_LEN];
	struct wps_add_pin_data data;

	data.addr = addr;
	data.uuid = u;
	data.pin = (const u8 *) pin;
	data.pin_len = os_strlen(pin);
	data.timeout = timeout;
	data.added = 0;

	if (os_strcmp(uuid, "any") == 0)
		data.uuid = NULL;
	else {
		if (uuid_str2bin(uuid, u))
			return -1;
		data.uuid = u;
	}
	if (hostapd_wps_for_each(hapd, wps_add_pin, &data) < 0)
		return -1;
	return data.added ? 0 : -1;
}


static int wps_button_pushed(struct hostapd_data *hapd, void *ctx)
{
	const u8 *p2p_dev_addr = ctx;
	if (hapd->wps == NULL)
		return 0;
	return wps_registrar_button_pushed(hapd->wps->registrar, p2p_dev_addr);
}


int hostapd_wps_button_pushed(struct hostapd_data *hapd,
			      const u8 *p2p_dev_addr)
{
	return hostapd_wps_for_each(hapd, wps_button_pushed,
				    (void *) p2p_dev_addr);
}


#ifdef CONFIG_WPS_OOB
int hostapd_wps_start_oob(struct hostapd_data *hapd, char *device_type,
			  char *path, char *method, char *name)
{
	struct wps_context *wps = hapd->wps;
	struct oob_device_data *oob_dev;

	oob_dev = wps_get_oob_device(device_type);
	if (oob_dev == NULL)
		return -1;
	oob_dev->device_path = path;
	oob_dev->device_name = name;
	wps->oob_conf.oob_method = wps_get_oob_method(method);

	if (wps->oob_conf.oob_method == OOB_METHOD_DEV_PWD_R) {
		/*
		 * Use pre-configured DH keys in order to be able to write the
		 * key hash into the OOB file.
		 */
		wpabuf_free(wps->dh_pubkey);
		wpabuf_free(wps->dh_privkey);
		wps->dh_privkey = NULL;
		wps->dh_pubkey = dh_init(dh_groups_get(WPS_DH_GROUP),
					 &wps->dh_privkey);
		wps->dh_pubkey = wpabuf_zeropad(wps->dh_pubkey, 192);
		if (wps->dh_pubkey == NULL) {
			wpa_printf(MSG_ERROR, "WPS: Failed to initialize "
				   "Diffie-Hellman handshake");
			return -1;
		}
	}

	if (wps_process_oob(wps, oob_dev, 1) < 0)
		goto error;

	if ((wps->oob_conf.oob_method == OOB_METHOD_DEV_PWD_E ||
	     wps->oob_conf.oob_method == OOB_METHOD_DEV_PWD_R) &&
	    hostapd_wps_add_pin(hapd, NULL, "any",
				wpabuf_head(wps->oob_conf.dev_password), 0) <
	    0)
		goto error;

	return 0;

error:
	wpabuf_free(wps->dh_pubkey);
	wps->dh_pubkey = NULL;
	wpabuf_free(wps->dh_privkey);
	wps->dh_privkey = NULL;
	return -1;
}
#endif /* CONFIG_WPS_OOB */


static int hostapd_wps_probe_req_rx(void *ctx, const u8 *addr,
				    const u8 *ie, size_t ie_len)
{
	struct hostapd_data *hapd = ctx;
	struct wpabuf *wps_ie;
	struct ieee802_11_elems elems;

	if (hapd->wps == NULL)
		return 0;

	if (ieee802_11_parse_elems(ie, ie_len, &elems, 0) == ParseFailed) {
		wpa_printf(MSG_DEBUG, "WPS: Could not parse ProbeReq from "
			   MACSTR, MAC2STR(addr));
		return 0;
	}

	if (elems.ssid && elems.ssid_len > 0 &&
	    (elems.ssid_len != hapd->conf->ssid.ssid_len ||
	     os_memcmp(elems.ssid, hapd->conf->ssid.ssid, elems.ssid_len) !=
	     0))
		return 0; /* Not for us */

	wps_ie = ieee802_11_vendor_ie_concat(ie, ie_len, WPS_DEV_OUI_WFA);
	if (wps_ie == NULL)
		return 0;
	if (wps_validate_probe_req(wps_ie, addr) < 0) {
		wpabuf_free(wps_ie);
		return 0;
	}

	if (wpabuf_len(wps_ie) > 0) {
		int p2p_wildcard = 0;
#ifdef CONFIG_P2P
		if (elems.ssid && elems.ssid_len == P2P_WILDCARD_SSID_LEN &&
		    os_memcmp(elems.ssid, P2P_WILDCARD_SSID,
			      P2P_WILDCARD_SSID_LEN) == 0)
			p2p_wildcard = 1;
#endif /* CONFIG_P2P */
		wps_registrar_probe_req_rx(hapd->wps->registrar, addr, wps_ie,
					   p2p_wildcard);
#ifdef CONFIG_WPS_UPNP
		/* FIX: what exactly should be included in the WLANEvent?
		 * WPS attributes? Full ProbeReq frame? */
		if (!p2p_wildcard)
			upnp_wps_device_send_wlan_event(
				hapd->wps_upnp, addr,
				UPNP_WPS_WLANEVENT_TYPE_PROBE, wps_ie);
#endif /* CONFIG_WPS_UPNP */
	}

	wpabuf_free(wps_ie);

	return 0;
}


#ifdef CONFIG_WPS_UPNP

static int hostapd_rx_req_put_wlan_response(
	void *priv, enum upnp_wps_wlanevent_type ev_type,
	const u8 *mac_addr, const struct wpabuf *msg,
	enum wps_msg_type msg_type)
{
	struct hostapd_data *hapd = priv;
	struct sta_info *sta;
	struct upnp_pending_message *p;

	wpa_printf(MSG_DEBUG, "WPS UPnP: PutWLANResponse ev_type=%d mac_addr="
		   MACSTR, ev_type, MAC2STR(mac_addr));
	wpa_hexdump(MSG_MSGDUMP, "WPS UPnP: PutWLANResponse NewMessage",
		    wpabuf_head(msg), wpabuf_len(msg));
	if (ev_type != UPNP_WPS_WLANEVENT_TYPE_EAP) {
		wpa_printf(MSG_DEBUG, "WPS UPnP: Ignored unexpected "
			   "PutWLANResponse WLANEventType %d", ev_type);
		return -1;
	}

	/*
	 * EAP response to ongoing to WPS Registration. Send it to EAP-WSC
	 * server implementation for delivery to the peer.
	 */

	sta = ap_get_sta(hapd, mac_addr);
#ifndef CONFIG_WPS_STRICT
	if (!sta) {
		/*
		 * Workaround - Intel wsccmd uses bogus NewWLANEventMAC:
		 * Pick STA that is in an ongoing WPS registration without
		 * checking the MAC address.
		 */
		wpa_printf(MSG_DEBUG, "WPS UPnP: No matching STA found based "
			   "on NewWLANEventMAC; try wildcard match");
		for (sta = hapd->sta_list; sta; sta = sta->next) {
			if (sta->eapol_sm && (sta->flags & WLAN_STA_WPS))
				break;
		}
	}
#endif /* CONFIG_WPS_STRICT */

	if (!sta) {
		wpa_printf(MSG_DEBUG, "WPS UPnP: No matching STA found");
		return 0;
	}

	p = os_zalloc(sizeof(*p));
	if (p == NULL)
		return -1;
	os_memcpy(p->addr, sta->addr, ETH_ALEN);
	p->msg = wpabuf_dup(msg);
	p->type = msg_type;
	p->next = hapd->wps->upnp_msgs;
	hapd->wps->upnp_msgs = p;

	return eapol_auth_eap_pending_cb(sta->eapol_sm, sta->eapol_sm->eap);
}


static int hostapd_wps_upnp_init(struct hostapd_data *hapd,
				 struct wps_context *wps)
{
	struct upnp_wps_device_ctx *ctx;

	if (!hapd->conf->upnp_iface)
		return 0;
	ctx = os_zalloc(sizeof(*ctx));
	if (ctx == NULL)
		return -1;

	ctx->rx_req_put_wlan_response = hostapd_rx_req_put_wlan_response;
	if (hapd->conf->ap_pin)
		ctx->ap_pin = os_strdup(hapd->conf->ap_pin);

	hapd->wps_upnp = upnp_wps_device_init(ctx, wps, hapd,
					      hapd->conf->upnp_iface);
	if (hapd->wps_upnp == NULL)
		return -1;
	wps->wps_upnp = hapd->wps_upnp;

	return 0;
}


static void hostapd_wps_upnp_deinit(struct hostapd_data *hapd)
{
	upnp_wps_device_deinit(hapd->wps_upnp, hapd);
}

#endif /* CONFIG_WPS_UPNP */


int hostapd_wps_get_mib_sta(struct hostapd_data *hapd, const u8 *addr,
			    char *buf, size_t buflen)
{
	if (hapd->wps == NULL)
		return 0;
	return wps_registrar_get_info(hapd->wps->registrar, addr, buf, buflen);
}


static void hostapd_wps_ap_pin_timeout(void *eloop_data, void *user_ctx)
{
	struct hostapd_data *hapd = eloop_data;
	wpa_printf(MSG_DEBUG, "WPS: AP PIN timed out");
	hostapd_wps_ap_pin_disable(hapd);
}


static void hostapd_wps_ap_pin_enable(struct hostapd_data *hapd, int timeout)
{
	wpa_printf(MSG_DEBUG, "WPS: Enabling AP PIN (timeout=%d)", timeout);
	hapd->ap_pin_failures = 0;
	hapd->conf->ap_setup_locked = 0;
	if (hapd->wps->ap_setup_locked) {
		wpa_msg(hapd->msg_ctx, MSG_INFO, WPS_EVENT_AP_SETUP_UNLOCKED);
		hapd->wps->ap_setup_locked = 0;
		wps_registrar_update_ie(hapd->wps->registrar);
	}
	eloop_cancel_timeout(hostapd_wps_ap_pin_timeout, hapd, NULL);
	if (timeout > 0)
		eloop_register_timeout(timeout, 0,
				       hostapd_wps_ap_pin_timeout, hapd, NULL);
}


static int wps_ap_pin_disable(struct hostapd_data *hapd, void *ctx)
{
	os_free(hapd->conf->ap_pin);
	hapd->conf->ap_pin = NULL;
#ifdef CONFIG_WPS_UPNP
	upnp_wps_set_ap_pin(hapd->wps_upnp, NULL);
#endif /* CONFIG_WPS_UPNP */
	eloop_cancel_timeout(hostapd_wps_ap_pin_timeout, hapd, NULL);
	return 0;
}


void hostapd_wps_ap_pin_disable(struct hostapd_data *hapd)
{
	wpa_printf(MSG_DEBUG, "WPS: Disabling AP PIN");
	hostapd_wps_for_each(hapd, wps_ap_pin_disable, NULL);
}


struct wps_ap_pin_data {
	char pin_txt[9];
	int timeout;
};


static int wps_ap_pin_set(struct hostapd_data *hapd, void *ctx)
{
	struct wps_ap_pin_data *data = ctx;
	os_free(hapd->conf->ap_pin);
	hapd->conf->ap_pin = os_strdup(data->pin_txt);
#ifdef CONFIG_WPS_UPNP
	upnp_wps_set_ap_pin(hapd->wps_upnp, data->pin_txt);
#endif /* CONFIG_WPS_UPNP */
	hostapd_wps_ap_pin_enable(hapd, data->timeout);
	return 0;
}


const char * hostapd_wps_ap_pin_random(struct hostapd_data *hapd, int timeout)
{
	unsigned int pin;
	struct wps_ap_pin_data data;

	pin = wps_generate_pin();
	os_snprintf(data.pin_txt, sizeof(data.pin_txt), "%u", pin);
	data.timeout = timeout;
	hostapd_wps_for_each(hapd, wps_ap_pin_set, &data);
	return hapd->conf->ap_pin;
}


const char * hostapd_wps_ap_pin_get(struct hostapd_data *hapd)
{
	return hapd->conf->ap_pin;
}


int hostapd_wps_ap_pin_set(struct hostapd_data *hapd, const char *pin,
			   int timeout)
{
	struct wps_ap_pin_data data;
	int ret;

	ret = os_snprintf(data.pin_txt, sizeof(data.pin_txt), "%s", pin);
	if (ret < 0 || ret >= (int) sizeof(data.pin_txt))
		return -1;
	data.timeout = timeout;
	return hostapd_wps_for_each(hapd, wps_ap_pin_set, &data);
}


static int wps_update_ie(struct hostapd_data *hapd, void *ctx)
{
	if (hapd->wps)
		wps_registrar_update_ie(hapd->wps->registrar);
	return 0;
}


void hostapd_wps_update_ie(struct hostapd_data *hapd)
{
	hostapd_wps_for_each(hapd, wps_update_ie, NULL);
}


int hostapd_wps_config_ap(struct hostapd_data *hapd, const char *ssid,
			  const char *auth, const char *encr, const char *key)
{
	struct wps_credential cred;
	size_t len;

	os_memset(&cred, 0, sizeof(cred));

	len = os_strlen(ssid);
	if ((len & 1) || len > 2 * sizeof(cred.ssid) ||
	    hexstr2bin(ssid, cred.ssid, len / 2))
		return -1;
	cred.ssid_len = len / 2;

	if (os_strncmp(auth, "OPEN", 4) == 0)
		cred.auth_type = WPS_AUTH_OPEN;
	else if (os_strncmp(auth, "WPAPSK", 6) == 0)
		cred.auth_type = WPS_AUTH_WPAPSK;
	else if (os_strncmp(auth, "WPA2PSK", 7) == 0)
		cred.auth_type = WPS_AUTH_WPA2PSK;
	else
		return -1;

	if (encr) {
		if (os_strncmp(encr, "NONE", 4) == 0)
			cred.encr_type = WPS_ENCR_NONE;
		else if (os_strncmp(encr, "WEP", 3) == 0)
			cred.encr_type = WPS_ENCR_WEP;
		else if (os_strncmp(encr, "TKIP", 4) == 0)
			cred.encr_type = WPS_ENCR_TKIP;
		else if (os_strncmp(encr, "CCMP", 4) == 0)
			cred.encr_type = WPS_ENCR_AES;
		else
			return -1;
	} else
		cred.encr_type = WPS_ENCR_NONE;

	if (key) {
		len = os_strlen(key);
		if ((len & 1) || len > 2 * sizeof(cred.key) ||
		    hexstr2bin(key, cred.key, len / 2))
			return -1;
		cred.key_len = len / 2;
	}

	return wps_registrar_config_ap(hapd->wps->registrar, &cred);
}
