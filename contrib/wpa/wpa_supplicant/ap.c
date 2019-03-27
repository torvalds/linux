/*
 * WPA Supplicant - Basic AP mode support routines
 * Copyright (c) 2003-2009, Jouni Malinen <j@w1.fi>
 * Copyright (c) 2009, Atheros Communications
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "utils/eloop.h"
#include "utils/uuid.h"
#include "common/ieee802_11_defs.h"
#include "common/wpa_ctrl.h"
#include "eapol_supp/eapol_supp_sm.h"
#include "crypto/dh_group5.h"
#include "ap/hostapd.h"
#include "ap/ap_config.h"
#include "ap/ap_drv_ops.h"
#ifdef NEED_AP_MLME
#include "ap/ieee802_11.h"
#endif /* NEED_AP_MLME */
#include "ap/beacon.h"
#include "ap/ieee802_1x.h"
#include "ap/wps_hostapd.h"
#include "ap/ctrl_iface_ap.h"
#include "ap/dfs.h"
#include "wps/wps.h"
#include "common/ieee802_11_defs.h"
#include "config_ssid.h"
#include "config.h"
#include "wpa_supplicant_i.h"
#include "driver_i.h"
#include "p2p_supplicant.h"
#include "ap.h"
#include "ap/sta_info.h"
#include "notify.h"


#ifdef CONFIG_WPS
static void wpas_wps_ap_pin_timeout(void *eloop_data, void *user_ctx);
#endif /* CONFIG_WPS */


#ifdef CONFIG_IEEE80211N
static void wpas_conf_ap_vht(struct wpa_supplicant *wpa_s,
			     struct wpa_ssid *ssid,
			     struct hostapd_config *conf,
			     struct hostapd_hw_modes *mode)
{
#ifdef CONFIG_P2P
	u8 center_chan = 0;
	u8 channel = conf->channel;
#endif /* CONFIG_P2P */

	if (!conf->secondary_channel)
		goto no_vht;

	/* Use the maximum oper channel width if it's given. */
	if (ssid->max_oper_chwidth)
		conf->vht_oper_chwidth = ssid->max_oper_chwidth;

	ieee80211_freq_to_chan(ssid->vht_center_freq2,
			       &conf->vht_oper_centr_freq_seg1_idx);

	if (!ssid->p2p_group) {
		if (!ssid->vht_center_freq1 ||
		    conf->vht_oper_chwidth == VHT_CHANWIDTH_USE_HT)
			goto no_vht;
		ieee80211_freq_to_chan(ssid->vht_center_freq1,
				       &conf->vht_oper_centr_freq_seg0_idx);
		wpa_printf(MSG_DEBUG, "VHT seg0 index %d for AP",
			   conf->vht_oper_centr_freq_seg0_idx);
		return;
	}

#ifdef CONFIG_P2P
	switch (conf->vht_oper_chwidth) {
	case VHT_CHANWIDTH_80MHZ:
	case VHT_CHANWIDTH_80P80MHZ:
		center_chan = wpas_p2p_get_vht80_center(wpa_s, mode, channel);
		wpa_printf(MSG_DEBUG,
			   "VHT center channel %u for 80 or 80+80 MHz bandwidth",
			   center_chan);
		break;
	case VHT_CHANWIDTH_160MHZ:
		center_chan = wpas_p2p_get_vht160_center(wpa_s, mode, channel);
		wpa_printf(MSG_DEBUG,
			   "VHT center channel %u for 160 MHz bandwidth",
			   center_chan);
		break;
	default:
		/*
		 * conf->vht_oper_chwidth might not be set for non-P2P GO cases,
		 * try oper_cwidth 160 MHz first then VHT 80 MHz, if 160 MHz is
		 * not supported.
		 */
		conf->vht_oper_chwidth = VHT_CHANWIDTH_160MHZ;
		center_chan = wpas_p2p_get_vht160_center(wpa_s, mode, channel);
		if (center_chan) {
			wpa_printf(MSG_DEBUG,
				   "VHT center channel %u for auto-selected 160 MHz bandwidth",
				   center_chan);
		} else {
			conf->vht_oper_chwidth = VHT_CHANWIDTH_80MHZ;
			center_chan = wpas_p2p_get_vht80_center(wpa_s, mode,
								channel);
			wpa_printf(MSG_DEBUG,
				   "VHT center channel %u for auto-selected 80 MHz bandwidth",
				   center_chan);
		}
		break;
	}
	if (!center_chan)
		goto no_vht;

	conf->vht_oper_centr_freq_seg0_idx = center_chan;
	wpa_printf(MSG_DEBUG, "VHT seg0 index %d for P2P GO",
		   conf->vht_oper_centr_freq_seg0_idx);
	return;
#endif /* CONFIG_P2P */

no_vht:
	wpa_printf(MSG_DEBUG,
		   "No VHT higher bandwidth support for the selected channel %d",
		   conf->channel);
	conf->vht_oper_centr_freq_seg0_idx =
		conf->channel + conf->secondary_channel * 2;
	conf->vht_oper_chwidth = VHT_CHANWIDTH_USE_HT;
}
#endif /* CONFIG_IEEE80211N */


int wpa_supplicant_conf_ap_ht(struct wpa_supplicant *wpa_s,
			      struct wpa_ssid *ssid,
			      struct hostapd_config *conf)
{
	conf->hw_mode = ieee80211_freq_to_chan(ssid->frequency,
					       &conf->channel);

	if (conf->hw_mode == NUM_HOSTAPD_MODES) {
		wpa_printf(MSG_ERROR, "Unsupported AP mode frequency: %d MHz",
			   ssid->frequency);
		return -1;
	}

	/* TODO: enable HT40 if driver supports it;
	 * drop to 11b if driver does not support 11g */

#ifdef CONFIG_IEEE80211N
	/*
	 * Enable HT20 if the driver supports it, by setting conf->ieee80211n
	 * and a mask of allowed capabilities within conf->ht_capab.
	 * Using default config settings for: conf->ht_op_mode_fixed,
	 * conf->secondary_channel, conf->require_ht
	 */
	if (wpa_s->hw.modes) {
		struct hostapd_hw_modes *mode = NULL;
		int i, no_ht = 0;

		wpa_printf(MSG_DEBUG,
			   "Determining HT/VHT options based on driver capabilities (freq=%u chan=%u)",
			   ssid->frequency, conf->channel);

		for (i = 0; i < wpa_s->hw.num_modes; i++) {
			if (wpa_s->hw.modes[i].mode == conf->hw_mode) {
				mode = &wpa_s->hw.modes[i];
				break;
			}
		}

#ifdef CONFIG_HT_OVERRIDES
		if (ssid->disable_ht)
			ssid->ht = 0;
#endif /* CONFIG_HT_OVERRIDES */

		if (!ssid->ht) {
			wpa_printf(MSG_DEBUG,
				   "HT not enabled in network profile");
			conf->ieee80211n = 0;
			conf->ht_capab = 0;
			no_ht = 1;
		}

		if (!no_ht && mode && mode->ht_capab) {
			wpa_printf(MSG_DEBUG,
				   "Enable HT support (p2p_group=%d 11a=%d ht40_hw_capab=%d ssid->ht40=%d)",
				   ssid->p2p_group,
				   conf->hw_mode == HOSTAPD_MODE_IEEE80211A,
				   !!(mode->ht_capab &
				      HT_CAP_INFO_SUPP_CHANNEL_WIDTH_SET),
				   ssid->ht40);
			conf->ieee80211n = 1;
#ifdef CONFIG_P2P
			if (ssid->p2p_group &&
			    conf->hw_mode == HOSTAPD_MODE_IEEE80211A &&
			    (mode->ht_capab &
			     HT_CAP_INFO_SUPP_CHANNEL_WIDTH_SET) &&
			    ssid->ht40) {
				conf->secondary_channel =
					wpas_p2p_get_ht40_mode(wpa_s, mode,
							       conf->channel);
				wpa_printf(MSG_DEBUG,
					   "HT secondary channel offset %d for P2P group",
					   conf->secondary_channel);
			}
#endif /* CONFIG_P2P */

			if (!ssid->p2p_group &&
			    (mode->ht_capab &
			     HT_CAP_INFO_SUPP_CHANNEL_WIDTH_SET)) {
				conf->secondary_channel = ssid->ht40;
				wpa_printf(MSG_DEBUG,
					   "HT secondary channel offset %d for AP",
					   conf->secondary_channel);
			}

			if (conf->secondary_channel)
				conf->ht_capab |=
					HT_CAP_INFO_SUPP_CHANNEL_WIDTH_SET;

			/*
			 * white-list capabilities that won't cause issues
			 * to connecting stations, while leaving the current
			 * capabilities intact (currently disabled SMPS).
			 */
			conf->ht_capab |= mode->ht_capab &
				(HT_CAP_INFO_GREEN_FIELD |
				 HT_CAP_INFO_SHORT_GI20MHZ |
				 HT_CAP_INFO_SHORT_GI40MHZ |
				 HT_CAP_INFO_RX_STBC_MASK |
				 HT_CAP_INFO_TX_STBC |
				 HT_CAP_INFO_MAX_AMSDU_SIZE);

			if (mode->vht_capab && ssid->vht) {
				conf->ieee80211ac = 1;
				conf->vht_capab |= mode->vht_capab;
				wpas_conf_ap_vht(wpa_s, ssid, conf, mode);
			}
		}
	}

	if (conf->secondary_channel) {
		struct wpa_supplicant *iface;

		for (iface = wpa_s->global->ifaces; iface; iface = iface->next)
		{
			if (iface == wpa_s ||
			    iface->wpa_state < WPA_AUTHENTICATING ||
			    (int) iface->assoc_freq != ssid->frequency)
				continue;

			/*
			 * Do not allow 40 MHz co-ex PRI/SEC switch to force us
			 * to change our PRI channel since we have an existing,
			 * concurrent connection on that channel and doing
			 * multi-channel concurrency is likely to cause more
			 * harm than using different PRI/SEC selection in
			 * environment with multiple BSSes on these two channels
			 * with mixed 20 MHz or PRI channel selection.
			 */
			conf->no_pri_sec_switch = 1;
		}
	}
#endif /* CONFIG_IEEE80211N */

	return 0;
}


static int wpa_supplicant_conf_ap(struct wpa_supplicant *wpa_s,
				  struct wpa_ssid *ssid,
				  struct hostapd_config *conf)
{
	struct hostapd_bss_config *bss = conf->bss[0];

	conf->driver = wpa_s->driver;

	os_strlcpy(bss->iface, wpa_s->ifname, sizeof(bss->iface));

	if (wpa_supplicant_conf_ap_ht(wpa_s, ssid, conf))
		return -1;

	if (ssid->pbss > 1) {
		wpa_printf(MSG_ERROR, "Invalid pbss value(%d) for AP mode",
			   ssid->pbss);
		return -1;
	}
	bss->pbss = ssid->pbss;

#ifdef CONFIG_ACS
	if (ssid->acs) {
		/* Setting channel to 0 in order to enable ACS */
		conf->channel = 0;
		wpa_printf(MSG_DEBUG, "Use automatic channel selection");
	}
#endif /* CONFIG_ACS */

	if (ieee80211_is_dfs(ssid->frequency, wpa_s->hw.modes,
			     wpa_s->hw.num_modes) && wpa_s->conf->country[0]) {
		conf->ieee80211h = 1;
		conf->ieee80211d = 1;
		conf->country[0] = wpa_s->conf->country[0];
		conf->country[1] = wpa_s->conf->country[1];
		conf->country[2] = ' ';
	}

#ifdef CONFIG_P2P
	if (conf->hw_mode == HOSTAPD_MODE_IEEE80211G &&
	    (ssid->mode == WPAS_MODE_P2P_GO ||
	     ssid->mode == WPAS_MODE_P2P_GROUP_FORMATION)) {
		/* Remove 802.11b rates from supported and basic rate sets */
		int *list = os_malloc(4 * sizeof(int));
		if (list) {
			list[0] = 60;
			list[1] = 120;
			list[2] = 240;
			list[3] = -1;
		}
		conf->basic_rates = list;

		list = os_malloc(9 * sizeof(int));
		if (list) {
			list[0] = 60;
			list[1] = 90;
			list[2] = 120;
			list[3] = 180;
			list[4] = 240;
			list[5] = 360;
			list[6] = 480;
			list[7] = 540;
			list[8] = -1;
		}
		conf->supported_rates = list;
	}

	bss->isolate = !wpa_s->conf->p2p_intra_bss;
	bss->force_per_enrollee_psk = wpa_s->global->p2p_per_sta_psk;

	if (ssid->p2p_group) {
		os_memcpy(bss->ip_addr_go, wpa_s->p2pdev->conf->ip_addr_go, 4);
		os_memcpy(bss->ip_addr_mask, wpa_s->p2pdev->conf->ip_addr_mask,
			  4);
		os_memcpy(bss->ip_addr_start,
			  wpa_s->p2pdev->conf->ip_addr_start, 4);
		os_memcpy(bss->ip_addr_end, wpa_s->p2pdev->conf->ip_addr_end,
			  4);
	}
#endif /* CONFIG_P2P */

	if (ssid->ssid_len == 0) {
		wpa_printf(MSG_ERROR, "No SSID configured for AP mode");
		return -1;
	}
	os_memcpy(bss->ssid.ssid, ssid->ssid, ssid->ssid_len);
	bss->ssid.ssid_len = ssid->ssid_len;
	bss->ssid.ssid_set = 1;

	bss->ignore_broadcast_ssid = ssid->ignore_broadcast_ssid;

	if (ssid->auth_alg)
		bss->auth_algs = ssid->auth_alg;

	if (wpa_key_mgmt_wpa_psk(ssid->key_mgmt))
		bss->wpa = ssid->proto;
	if (ssid->key_mgmt == DEFAULT_KEY_MGMT)
		bss->wpa_key_mgmt = WPA_KEY_MGMT_PSK;
	else
		bss->wpa_key_mgmt = ssid->key_mgmt;
	bss->wpa_pairwise = ssid->pairwise_cipher;
	if (ssid->psk_set) {
		bin_clear_free(bss->ssid.wpa_psk, sizeof(*bss->ssid.wpa_psk));
		bss->ssid.wpa_psk = os_zalloc(sizeof(struct hostapd_wpa_psk));
		if (bss->ssid.wpa_psk == NULL)
			return -1;
		os_memcpy(bss->ssid.wpa_psk->psk, ssid->psk, PMK_LEN);
		bss->ssid.wpa_psk->group = 1;
		bss->ssid.wpa_psk_set = 1;
	} else if (ssid->passphrase) {
		bss->ssid.wpa_passphrase = os_strdup(ssid->passphrase);
	} else if (ssid->wep_key_len[0] || ssid->wep_key_len[1] ||
		   ssid->wep_key_len[2] || ssid->wep_key_len[3]) {
		struct hostapd_wep_keys *wep = &bss->ssid.wep;
		int i;
		for (i = 0; i < NUM_WEP_KEYS; i++) {
			if (ssid->wep_key_len[i] == 0)
				continue;
			wep->key[i] = os_memdup(ssid->wep_key[i],
						ssid->wep_key_len[i]);
			if (wep->key[i] == NULL)
				return -1;
			wep->len[i] = ssid->wep_key_len[i];
		}
		wep->idx = ssid->wep_tx_keyidx;
		wep->keys_set = 1;
	}

	if (wpa_s->conf->go_interworking) {
		wpa_printf(MSG_DEBUG,
			   "P2P: Enable Interworking with access_network_type: %d",
			   wpa_s->conf->go_access_network_type);
		bss->interworking = wpa_s->conf->go_interworking;
		bss->access_network_type = wpa_s->conf->go_access_network_type;
		bss->internet = wpa_s->conf->go_internet;
		if (wpa_s->conf->go_venue_group) {
			wpa_printf(MSG_DEBUG,
				   "P2P: Venue group: %d  Venue type: %d",
				   wpa_s->conf->go_venue_group,
				   wpa_s->conf->go_venue_type);
			bss->venue_group = wpa_s->conf->go_venue_group;
			bss->venue_type = wpa_s->conf->go_venue_type;
			bss->venue_info_set = 1;
		}
	}

	if (ssid->ap_max_inactivity)
		bss->ap_max_inactivity = ssid->ap_max_inactivity;

	if (ssid->dtim_period)
		bss->dtim_period = ssid->dtim_period;
	else if (wpa_s->conf->dtim_period)
		bss->dtim_period = wpa_s->conf->dtim_period;

	if (ssid->beacon_int)
		conf->beacon_int = ssid->beacon_int;
	else if (wpa_s->conf->beacon_int)
		conf->beacon_int = wpa_s->conf->beacon_int;

#ifdef CONFIG_P2P
	if (ssid->mode == WPAS_MODE_P2P_GO ||
	    ssid->mode == WPAS_MODE_P2P_GROUP_FORMATION) {
		if (wpa_s->conf->p2p_go_ctwindow > conf->beacon_int) {
			wpa_printf(MSG_INFO,
				   "CTWindow (%d) is bigger than beacon interval (%d) - avoid configuring it",
				   wpa_s->conf->p2p_go_ctwindow,
				   conf->beacon_int);
			conf->p2p_go_ctwindow = 0;
		} else {
			conf->p2p_go_ctwindow = wpa_s->conf->p2p_go_ctwindow;
		}
	}
#endif /* CONFIG_P2P */

	if ((bss->wpa & 2) && bss->rsn_pairwise == 0)
		bss->rsn_pairwise = bss->wpa_pairwise;
	bss->wpa_group = wpa_select_ap_group_cipher(bss->wpa, bss->wpa_pairwise,
						    bss->rsn_pairwise);

	if (bss->wpa && bss->ieee802_1x)
		bss->ssid.security_policy = SECURITY_WPA;
	else if (bss->wpa)
		bss->ssid.security_policy = SECURITY_WPA_PSK;
	else if (bss->ieee802_1x) {
		int cipher = WPA_CIPHER_NONE;
		bss->ssid.security_policy = SECURITY_IEEE_802_1X;
		bss->ssid.wep.default_len = bss->default_wep_key_len;
		if (bss->default_wep_key_len)
			cipher = bss->default_wep_key_len >= 13 ?
				WPA_CIPHER_WEP104 : WPA_CIPHER_WEP40;
		bss->wpa_group = cipher;
		bss->wpa_pairwise = cipher;
		bss->rsn_pairwise = cipher;
	} else if (bss->ssid.wep.keys_set) {
		int cipher = WPA_CIPHER_WEP40;
		if (bss->ssid.wep.len[0] >= 13)
			cipher = WPA_CIPHER_WEP104;
		bss->ssid.security_policy = SECURITY_STATIC_WEP;
		bss->wpa_group = cipher;
		bss->wpa_pairwise = cipher;
		bss->rsn_pairwise = cipher;
	} else {
		bss->ssid.security_policy = SECURITY_PLAINTEXT;
		bss->wpa_group = WPA_CIPHER_NONE;
		bss->wpa_pairwise = WPA_CIPHER_NONE;
		bss->rsn_pairwise = WPA_CIPHER_NONE;
	}

	if (bss->wpa_group_rekey < 86400 && (bss->wpa & 2) &&
	    (bss->wpa_group == WPA_CIPHER_CCMP ||
	     bss->wpa_group == WPA_CIPHER_GCMP ||
	     bss->wpa_group == WPA_CIPHER_CCMP_256 ||
	     bss->wpa_group == WPA_CIPHER_GCMP_256)) {
		/*
		 * Strong ciphers do not need frequent rekeying, so increase
		 * the default GTK rekeying period to 24 hours.
		 */
		bss->wpa_group_rekey = 86400;
	}

#ifdef CONFIG_IEEE80211W
	if (ssid->ieee80211w != MGMT_FRAME_PROTECTION_DEFAULT)
		bss->ieee80211w = ssid->ieee80211w;
#endif /* CONFIG_IEEE80211W */

#ifdef CONFIG_WPS
	/*
	 * Enable WPS by default for open and WPA/WPA2-Personal network, but
	 * require user interaction to actually use it. Only the internal
	 * Registrar is supported.
	 */
	if (bss->ssid.security_policy != SECURITY_WPA_PSK &&
	    bss->ssid.security_policy != SECURITY_PLAINTEXT)
		goto no_wps;
	if (bss->ssid.security_policy == SECURITY_WPA_PSK &&
	    (!(bss->rsn_pairwise & (WPA_CIPHER_CCMP | WPA_CIPHER_GCMP)) ||
	     !(bss->wpa & 2)))
		goto no_wps; /* WPS2 does not allow WPA/TKIP-only
			      * configuration */
	if (ssid->wps_disabled)
		goto no_wps;
	bss->eap_server = 1;

	if (!ssid->ignore_broadcast_ssid)
		bss->wps_state = 2;

	bss->ap_setup_locked = 2;
	if (wpa_s->conf->config_methods)
		bss->config_methods = os_strdup(wpa_s->conf->config_methods);
	os_memcpy(bss->device_type, wpa_s->conf->device_type,
		  WPS_DEV_TYPE_LEN);
	if (wpa_s->conf->device_name) {
		bss->device_name = os_strdup(wpa_s->conf->device_name);
		bss->friendly_name = os_strdup(wpa_s->conf->device_name);
	}
	if (wpa_s->conf->manufacturer)
		bss->manufacturer = os_strdup(wpa_s->conf->manufacturer);
	if (wpa_s->conf->model_name)
		bss->model_name = os_strdup(wpa_s->conf->model_name);
	if (wpa_s->conf->model_number)
		bss->model_number = os_strdup(wpa_s->conf->model_number);
	if (wpa_s->conf->serial_number)
		bss->serial_number = os_strdup(wpa_s->conf->serial_number);
	if (is_nil_uuid(wpa_s->conf->uuid))
		os_memcpy(bss->uuid, wpa_s->wps->uuid, WPS_UUID_LEN);
	else
		os_memcpy(bss->uuid, wpa_s->conf->uuid, WPS_UUID_LEN);
	os_memcpy(bss->os_version, wpa_s->conf->os_version, 4);
	bss->pbc_in_m1 = wpa_s->conf->pbc_in_m1;
	if (ssid->eap.fragment_size != DEFAULT_FRAGMENT_SIZE)
		bss->fragment_size = ssid->eap.fragment_size;
no_wps:
#endif /* CONFIG_WPS */

	if (wpa_s->max_stations &&
	    wpa_s->max_stations < wpa_s->conf->max_num_sta)
		bss->max_num_sta = wpa_s->max_stations;
	else
		bss->max_num_sta = wpa_s->conf->max_num_sta;

	if (!bss->isolate)
		bss->isolate = wpa_s->conf->ap_isolate;

	bss->disassoc_low_ack = wpa_s->conf->disassoc_low_ack;

	if (wpa_s->conf->ap_vendor_elements) {
		bss->vendor_elements =
			wpabuf_dup(wpa_s->conf->ap_vendor_elements);
	}

	bss->ftm_responder = wpa_s->conf->ftm_responder;
	bss->ftm_initiator = wpa_s->conf->ftm_initiator;

	return 0;
}


static void ap_public_action_rx(void *ctx, const u8 *buf, size_t len, int freq)
{
#ifdef CONFIG_P2P
	struct wpa_supplicant *wpa_s = ctx;
	const struct ieee80211_mgmt *mgmt;

	mgmt = (const struct ieee80211_mgmt *) buf;
	if (len < IEEE80211_HDRLEN + 1)
		return;
	if (mgmt->u.action.category != WLAN_ACTION_PUBLIC)
		return;
	wpas_p2p_rx_action(wpa_s, mgmt->da, mgmt->sa, mgmt->bssid,
			   mgmt->u.action.category,
			   buf + IEEE80211_HDRLEN + 1,
			   len - IEEE80211_HDRLEN - 1, freq);
#endif /* CONFIG_P2P */
}


static void ap_wps_event_cb(void *ctx, enum wps_event event,
			    union wps_event_data *data)
{
#ifdef CONFIG_P2P
	struct wpa_supplicant *wpa_s = ctx;

	if (event == WPS_EV_FAIL) {
		struct wps_event_fail *fail = &data->fail;

		if (wpa_s->p2pdev && wpa_s->p2pdev != wpa_s &&
		    wpa_s == wpa_s->global->p2p_group_formation) {
			/*
			 * src/ap/wps_hostapd.c has already sent this on the
			 * main interface, so only send on the parent interface
			 * here if needed.
			 */
			wpa_msg(wpa_s->p2pdev, MSG_INFO, WPS_EVENT_FAIL
				"msg=%d config_error=%d",
				fail->msg, fail->config_error);
		}
		wpas_p2p_wps_failed(wpa_s, fail);
	}
#endif /* CONFIG_P2P */
}


static void ap_sta_authorized_cb(void *ctx, const u8 *mac_addr,
				 int authorized, const u8 *p2p_dev_addr)
{
	wpas_notify_sta_authorized(ctx, mac_addr, authorized, p2p_dev_addr);
}


#ifdef CONFIG_P2P
static void ap_new_psk_cb(void *ctx, const u8 *mac_addr, const u8 *p2p_dev_addr,
			  const u8 *psk, size_t psk_len)
{

	struct wpa_supplicant *wpa_s = ctx;
	if (wpa_s->ap_iface == NULL || wpa_s->current_ssid == NULL)
		return;
	wpas_p2p_new_psk_cb(wpa_s, mac_addr, p2p_dev_addr, psk, psk_len);
}
#endif /* CONFIG_P2P */


static int ap_vendor_action_rx(void *ctx, const u8 *buf, size_t len, int freq)
{
#ifdef CONFIG_P2P
	struct wpa_supplicant *wpa_s = ctx;
	const struct ieee80211_mgmt *mgmt;

	mgmt = (const struct ieee80211_mgmt *) buf;
	if (len < IEEE80211_HDRLEN + 1)
		return -1;
	wpas_p2p_rx_action(wpa_s, mgmt->da, mgmt->sa, mgmt->bssid,
			   mgmt->u.action.category,
			   buf + IEEE80211_HDRLEN + 1,
			   len - IEEE80211_HDRLEN - 1, freq);
#endif /* CONFIG_P2P */
	return 0;
}


static int ap_probe_req_rx(void *ctx, const u8 *sa, const u8 *da,
			   const u8 *bssid, const u8 *ie, size_t ie_len,
			   int ssi_signal)
{
	struct wpa_supplicant *wpa_s = ctx;
	unsigned int freq = 0;

	if (wpa_s->ap_iface)
		freq = wpa_s->ap_iface->freq;

	return wpas_p2p_probe_req_rx(wpa_s, sa, da, bssid, ie, ie_len,
				     freq, ssi_signal);
}


static void ap_wps_reg_success_cb(void *ctx, const u8 *mac_addr,
				  const u8 *uuid_e)
{
	struct wpa_supplicant *wpa_s = ctx;
	wpas_p2p_wps_success(wpa_s, mac_addr, 1);
}


static void wpas_ap_configured_cb(void *ctx)
{
	struct wpa_supplicant *wpa_s = ctx;

	wpa_printf(MSG_DEBUG, "AP interface setup completed - state %s",
		   hostapd_state_text(wpa_s->ap_iface->state));
	if (wpa_s->ap_iface->state == HAPD_IFACE_DISABLED) {
		wpa_supplicant_ap_deinit(wpa_s);
		return;
	}

#ifdef CONFIG_ACS
	if (wpa_s->current_ssid && wpa_s->current_ssid->acs) {
		wpa_s->assoc_freq = wpa_s->ap_iface->freq;
		wpa_s->current_ssid->frequency = wpa_s->ap_iface->freq;
	}
#endif /* CONFIG_ACS */

	wpa_supplicant_set_state(wpa_s, WPA_COMPLETED);

	if (wpa_s->ap_configured_cb)
		wpa_s->ap_configured_cb(wpa_s->ap_configured_cb_ctx,
					wpa_s->ap_configured_cb_data);
}


int wpa_supplicant_create_ap(struct wpa_supplicant *wpa_s,
			     struct wpa_ssid *ssid)
{
	struct wpa_driver_associate_params params;
	struct hostapd_iface *hapd_iface;
	struct hostapd_config *conf;
	size_t i;

	if (ssid->ssid == NULL || ssid->ssid_len == 0) {
		wpa_printf(MSG_ERROR, "No SSID configured for AP mode");
		return -1;
	}

	wpa_supplicant_ap_deinit(wpa_s);

	wpa_printf(MSG_DEBUG, "Setting up AP (SSID='%s')",
		   wpa_ssid_txt(ssid->ssid, ssid->ssid_len));

	os_memset(&params, 0, sizeof(params));
	params.ssid = ssid->ssid;
	params.ssid_len = ssid->ssid_len;
	switch (ssid->mode) {
	case WPAS_MODE_AP:
	case WPAS_MODE_P2P_GO:
	case WPAS_MODE_P2P_GROUP_FORMATION:
		params.mode = IEEE80211_MODE_AP;
		break;
	default:
		return -1;
	}
	if (ssid->frequency == 0)
		ssid->frequency = 2462; /* default channel 11 */
	params.freq.freq = ssid->frequency;

	params.wpa_proto = ssid->proto;
	if (ssid->key_mgmt & WPA_KEY_MGMT_PSK)
		wpa_s->key_mgmt = WPA_KEY_MGMT_PSK;
	else
		wpa_s->key_mgmt = WPA_KEY_MGMT_NONE;
	params.key_mgmt_suite = wpa_s->key_mgmt;

	wpa_s->pairwise_cipher = wpa_pick_pairwise_cipher(ssid->pairwise_cipher,
							  1);
	if (wpa_s->pairwise_cipher < 0) {
		wpa_printf(MSG_WARNING, "WPA: Failed to select pairwise "
			   "cipher.");
		return -1;
	}
	params.pairwise_suite = wpa_s->pairwise_cipher;
	params.group_suite = params.pairwise_suite;

#ifdef CONFIG_P2P
	if (ssid->mode == WPAS_MODE_P2P_GO ||
	    ssid->mode == WPAS_MODE_P2P_GROUP_FORMATION)
		params.p2p = 1;
#endif /* CONFIG_P2P */

	if (wpa_s->p2pdev->set_ap_uapsd)
		params.uapsd = wpa_s->p2pdev->ap_uapsd;
	else if (params.p2p && (wpa_s->drv_flags & WPA_DRIVER_FLAGS_AP_UAPSD))
		params.uapsd = 1; /* mandatory for P2P GO */
	else
		params.uapsd = -1;

	if (ieee80211_is_dfs(params.freq.freq, wpa_s->hw.modes,
			     wpa_s->hw.num_modes))
		params.freq.freq = 0; /* set channel after CAC */

	if (params.p2p)
		wpa_drv_get_ext_capa(wpa_s, WPA_IF_P2P_GO);
	else
		wpa_drv_get_ext_capa(wpa_s, WPA_IF_AP_BSS);

	if (wpa_drv_associate(wpa_s, &params) < 0) {
		wpa_msg(wpa_s, MSG_INFO, "Failed to start AP functionality");
		return -1;
	}

	wpa_s->ap_iface = hapd_iface = hostapd_alloc_iface();
	if (hapd_iface == NULL)
		return -1;
	hapd_iface->owner = wpa_s;
	hapd_iface->drv_flags = wpa_s->drv_flags;
	hapd_iface->smps_modes = wpa_s->drv_smps_modes;
	hapd_iface->probe_resp_offloads = wpa_s->probe_resp_offloads;
	hapd_iface->extended_capa = wpa_s->extended_capa;
	hapd_iface->extended_capa_mask = wpa_s->extended_capa_mask;
	hapd_iface->extended_capa_len = wpa_s->extended_capa_len;

	wpa_s->ap_iface->conf = conf = hostapd_config_defaults();
	if (conf == NULL) {
		wpa_supplicant_ap_deinit(wpa_s);
		return -1;
	}

	os_memcpy(wpa_s->ap_iface->conf->wmm_ac_params,
		  wpa_s->conf->wmm_ac_params,
		  sizeof(wpa_s->conf->wmm_ac_params));

	if (params.uapsd > 0) {
		conf->bss[0]->wmm_enabled = 1;
		conf->bss[0]->wmm_uapsd = 1;
	}

	if (wpa_supplicant_conf_ap(wpa_s, ssid, conf)) {
		wpa_printf(MSG_ERROR, "Failed to create AP configuration");
		wpa_supplicant_ap_deinit(wpa_s);
		return -1;
	}

#ifdef CONFIG_P2P
	if (ssid->mode == WPAS_MODE_P2P_GO)
		conf->bss[0]->p2p = P2P_ENABLED | P2P_GROUP_OWNER;
	else if (ssid->mode == WPAS_MODE_P2P_GROUP_FORMATION)
		conf->bss[0]->p2p = P2P_ENABLED | P2P_GROUP_OWNER |
			P2P_GROUP_FORMATION;
#endif /* CONFIG_P2P */

	hapd_iface->num_bss = conf->num_bss;
	hapd_iface->bss = os_calloc(conf->num_bss,
				    sizeof(struct hostapd_data *));
	if (hapd_iface->bss == NULL) {
		wpa_supplicant_ap_deinit(wpa_s);
		return -1;
	}

	for (i = 0; i < conf->num_bss; i++) {
		hapd_iface->bss[i] =
			hostapd_alloc_bss_data(hapd_iface, conf,
					       conf->bss[i]);
		if (hapd_iface->bss[i] == NULL) {
			wpa_supplicant_ap_deinit(wpa_s);
			return -1;
		}

		hapd_iface->bss[i]->msg_ctx = wpa_s;
		hapd_iface->bss[i]->msg_ctx_parent = wpa_s->p2pdev;
		hapd_iface->bss[i]->public_action_cb = ap_public_action_rx;
		hapd_iface->bss[i]->public_action_cb_ctx = wpa_s;
		hapd_iface->bss[i]->vendor_action_cb = ap_vendor_action_rx;
		hapd_iface->bss[i]->vendor_action_cb_ctx = wpa_s;
		hostapd_register_probereq_cb(hapd_iface->bss[i],
					     ap_probe_req_rx, wpa_s);
		hapd_iface->bss[i]->wps_reg_success_cb = ap_wps_reg_success_cb;
		hapd_iface->bss[i]->wps_reg_success_cb_ctx = wpa_s;
		hapd_iface->bss[i]->wps_event_cb = ap_wps_event_cb;
		hapd_iface->bss[i]->wps_event_cb_ctx = wpa_s;
		hapd_iface->bss[i]->sta_authorized_cb = ap_sta_authorized_cb;
		hapd_iface->bss[i]->sta_authorized_cb_ctx = wpa_s;
#ifdef CONFIG_P2P
		hapd_iface->bss[i]->new_psk_cb = ap_new_psk_cb;
		hapd_iface->bss[i]->new_psk_cb_ctx = wpa_s;
		hapd_iface->bss[i]->p2p = wpa_s->global->p2p;
		hapd_iface->bss[i]->p2p_group = wpas_p2p_group_init(wpa_s,
								    ssid);
#endif /* CONFIG_P2P */
		hapd_iface->bss[i]->setup_complete_cb = wpas_ap_configured_cb;
		hapd_iface->bss[i]->setup_complete_cb_ctx = wpa_s;
#ifdef CONFIG_TESTING_OPTIONS
		hapd_iface->bss[i]->ext_eapol_frame_io =
			wpa_s->ext_eapol_frame_io;
#endif /* CONFIG_TESTING_OPTIONS */
	}

	os_memcpy(hapd_iface->bss[0]->own_addr, wpa_s->own_addr, ETH_ALEN);
	hapd_iface->bss[0]->driver = wpa_s->driver;
	hapd_iface->bss[0]->drv_priv = wpa_s->drv_priv;

	wpa_s->current_ssid = ssid;
	eapol_sm_notify_config(wpa_s->eapol, NULL, NULL);
	os_memcpy(wpa_s->bssid, wpa_s->own_addr, ETH_ALEN);
	wpa_s->assoc_freq = ssid->frequency;

#if defined(CONFIG_P2P) && defined(CONFIG_ACS)
	if (wpa_s->p2p_go_do_acs) {
		wpa_s->ap_iface->conf->channel = 0;
		wpa_s->ap_iface->conf->hw_mode = wpa_s->p2p_go_acs_band;
		ssid->acs = 1;
	}
#endif /* CONFIG_P2P && CONFIG_ACS */

	if (hostapd_setup_interface(wpa_s->ap_iface)) {
		wpa_printf(MSG_ERROR, "Failed to initialize AP interface");
		wpa_supplicant_ap_deinit(wpa_s);
		return -1;
	}

	return 0;
}


void wpa_supplicant_ap_deinit(struct wpa_supplicant *wpa_s)
{
#ifdef CONFIG_WPS
	eloop_cancel_timeout(wpas_wps_ap_pin_timeout, wpa_s, NULL);
#endif /* CONFIG_WPS */

	if (wpa_s->ap_iface == NULL)
		return;

	wpa_s->current_ssid = NULL;
	eapol_sm_notify_config(wpa_s->eapol, NULL, NULL);
	wpa_s->assoc_freq = 0;
	wpas_p2p_ap_deinit(wpa_s);
	wpa_s->ap_iface->driver_ap_teardown =
		!!(wpa_s->drv_flags & WPA_DRIVER_FLAGS_AP_TEARDOWN_SUPPORT);

	hostapd_interface_deinit(wpa_s->ap_iface);
	hostapd_interface_free(wpa_s->ap_iface);
	wpa_s->ap_iface = NULL;
	wpa_drv_deinit_ap(wpa_s);
	wpa_msg(wpa_s, MSG_INFO, WPA_EVENT_DISCONNECTED "bssid=" MACSTR
		" reason=%d locally_generated=1",
		MAC2STR(wpa_s->own_addr), WLAN_REASON_DEAUTH_LEAVING);
}


void ap_tx_status(void *ctx, const u8 *addr,
		  const u8 *buf, size_t len, int ack)
{
#ifdef NEED_AP_MLME
	struct wpa_supplicant *wpa_s = ctx;
	hostapd_tx_status(wpa_s->ap_iface->bss[0], addr, buf, len, ack);
#endif /* NEED_AP_MLME */
}


void ap_eapol_tx_status(void *ctx, const u8 *dst,
			const u8 *data, size_t len, int ack)
{
#ifdef NEED_AP_MLME
	struct wpa_supplicant *wpa_s = ctx;
	if (!wpa_s->ap_iface)
		return;
	hostapd_tx_status(wpa_s->ap_iface->bss[0], dst, data, len, ack);
#endif /* NEED_AP_MLME */
}


void ap_client_poll_ok(void *ctx, const u8 *addr)
{
#ifdef NEED_AP_MLME
	struct wpa_supplicant *wpa_s = ctx;
	if (wpa_s->ap_iface)
		hostapd_client_poll_ok(wpa_s->ap_iface->bss[0], addr);
#endif /* NEED_AP_MLME */
}


void ap_rx_from_unknown_sta(void *ctx, const u8 *addr, int wds)
{
#ifdef NEED_AP_MLME
	struct wpa_supplicant *wpa_s = ctx;
	ieee802_11_rx_from_unknown(wpa_s->ap_iface->bss[0], addr, wds);
#endif /* NEED_AP_MLME */
}


void ap_mgmt_rx(void *ctx, struct rx_mgmt *rx_mgmt)
{
#ifdef NEED_AP_MLME
	struct wpa_supplicant *wpa_s = ctx;
	struct hostapd_frame_info fi;
	os_memset(&fi, 0, sizeof(fi));
	fi.datarate = rx_mgmt->datarate;
	fi.ssi_signal = rx_mgmt->ssi_signal;
	ieee802_11_mgmt(wpa_s->ap_iface->bss[0], rx_mgmt->frame,
			rx_mgmt->frame_len, &fi);
#endif /* NEED_AP_MLME */
}


void ap_mgmt_tx_cb(void *ctx, const u8 *buf, size_t len, u16 stype, int ok)
{
#ifdef NEED_AP_MLME
	struct wpa_supplicant *wpa_s = ctx;
	ieee802_11_mgmt_cb(wpa_s->ap_iface->bss[0], buf, len, stype, ok);
#endif /* NEED_AP_MLME */
}


void wpa_supplicant_ap_rx_eapol(struct wpa_supplicant *wpa_s,
				const u8 *src_addr, const u8 *buf, size_t len)
{
	ieee802_1x_receive(wpa_s->ap_iface->bss[0], src_addr, buf, len);
}


#ifdef CONFIG_WPS

int wpa_supplicant_ap_wps_pbc(struct wpa_supplicant *wpa_s, const u8 *bssid,
			      const u8 *p2p_dev_addr)
{
	if (!wpa_s->ap_iface)
		return -1;
	return hostapd_wps_button_pushed(wpa_s->ap_iface->bss[0],
					 p2p_dev_addr);
}


int wpa_supplicant_ap_wps_cancel(struct wpa_supplicant *wpa_s)
{
	struct wps_registrar *reg;
	int reg_sel = 0, wps_sta = 0;

	if (!wpa_s->ap_iface || !wpa_s->ap_iface->bss[0]->wps)
		return -1;

	reg = wpa_s->ap_iface->bss[0]->wps->registrar;
	reg_sel = wps_registrar_wps_cancel(reg);
	wps_sta = ap_for_each_sta(wpa_s->ap_iface->bss[0],
				  ap_sta_wps_cancel, NULL);

	if (!reg_sel && !wps_sta) {
		wpa_printf(MSG_DEBUG, "No WPS operation in progress at this "
			   "time");
		return -1;
	}

	/*
	 * There are 2 cases to return wps cancel as success:
	 * 1. When wps cancel was initiated but no connection has been
	 *    established with client yet.
	 * 2. Client is in the middle of exchanging WPS messages.
	 */

	return 0;
}


int wpa_supplicant_ap_wps_pin(struct wpa_supplicant *wpa_s, const u8 *bssid,
			      const char *pin, char *buf, size_t buflen,
			      int timeout)
{
	int ret, ret_len = 0;

	if (!wpa_s->ap_iface)
		return -1;

	if (pin == NULL) {
		unsigned int rpin;

		if (wps_generate_pin(&rpin) < 0)
			return -1;
		ret_len = os_snprintf(buf, buflen, "%08d", rpin);
		if (os_snprintf_error(buflen, ret_len))
			return -1;
		pin = buf;
	} else if (buf) {
		ret_len = os_snprintf(buf, buflen, "%s", pin);
		if (os_snprintf_error(buflen, ret_len))
			return -1;
	}

	ret = hostapd_wps_add_pin(wpa_s->ap_iface->bss[0], bssid, "any", pin,
				  timeout);
	if (ret)
		return -1;
	return ret_len;
}


static void wpas_wps_ap_pin_timeout(void *eloop_data, void *user_ctx)
{
	struct wpa_supplicant *wpa_s = eloop_data;
	wpa_printf(MSG_DEBUG, "WPS: AP PIN timed out");
	wpas_wps_ap_pin_disable(wpa_s);
}


static void wpas_wps_ap_pin_enable(struct wpa_supplicant *wpa_s, int timeout)
{
	struct hostapd_data *hapd;

	if (wpa_s->ap_iface == NULL)
		return;
	hapd = wpa_s->ap_iface->bss[0];
	wpa_printf(MSG_DEBUG, "WPS: Enabling AP PIN (timeout=%d)", timeout);
	hapd->ap_pin_failures = 0;
	eloop_cancel_timeout(wpas_wps_ap_pin_timeout, wpa_s, NULL);
	if (timeout > 0)
		eloop_register_timeout(timeout, 0,
				       wpas_wps_ap_pin_timeout, wpa_s, NULL);
}


void wpas_wps_ap_pin_disable(struct wpa_supplicant *wpa_s)
{
	struct hostapd_data *hapd;

	if (wpa_s->ap_iface == NULL)
		return;
	wpa_printf(MSG_DEBUG, "WPS: Disabling AP PIN");
	hapd = wpa_s->ap_iface->bss[0];
	os_free(hapd->conf->ap_pin);
	hapd->conf->ap_pin = NULL;
	eloop_cancel_timeout(wpas_wps_ap_pin_timeout, wpa_s, NULL);
}


const char * wpas_wps_ap_pin_random(struct wpa_supplicant *wpa_s, int timeout)
{
	struct hostapd_data *hapd;
	unsigned int pin;
	char pin_txt[9];

	if (wpa_s->ap_iface == NULL)
		return NULL;
	hapd = wpa_s->ap_iface->bss[0];
	if (wps_generate_pin(&pin) < 0)
		return NULL;
	os_snprintf(pin_txt, sizeof(pin_txt), "%08u", pin);
	os_free(hapd->conf->ap_pin);
	hapd->conf->ap_pin = os_strdup(pin_txt);
	if (hapd->conf->ap_pin == NULL)
		return NULL;
	wpas_wps_ap_pin_enable(wpa_s, timeout);

	return hapd->conf->ap_pin;
}


const char * wpas_wps_ap_pin_get(struct wpa_supplicant *wpa_s)
{
	struct hostapd_data *hapd;
	if (wpa_s->ap_iface == NULL)
		return NULL;
	hapd = wpa_s->ap_iface->bss[0];
	return hapd->conf->ap_pin;
}


int wpas_wps_ap_pin_set(struct wpa_supplicant *wpa_s, const char *pin,
			int timeout)
{
	struct hostapd_data *hapd;
	char pin_txt[9];
	int ret;

	if (wpa_s->ap_iface == NULL)
		return -1;
	hapd = wpa_s->ap_iface->bss[0];
	ret = os_snprintf(pin_txt, sizeof(pin_txt), "%s", pin);
	if (os_snprintf_error(sizeof(pin_txt), ret))
		return -1;
	os_free(hapd->conf->ap_pin);
	hapd->conf->ap_pin = os_strdup(pin_txt);
	if (hapd->conf->ap_pin == NULL)
		return -1;
	wpas_wps_ap_pin_enable(wpa_s, timeout);

	return 0;
}


void wpa_supplicant_ap_pwd_auth_fail(struct wpa_supplicant *wpa_s)
{
	struct hostapd_data *hapd;

	if (wpa_s->ap_iface == NULL)
		return;
	hapd = wpa_s->ap_iface->bss[0];

	/*
	 * Registrar failed to prove its knowledge of the AP PIN. Disable AP
	 * PIN if this happens multiple times to slow down brute force attacks.
	 */
	hapd->ap_pin_failures++;
	wpa_printf(MSG_DEBUG, "WPS: AP PIN authentication failure number %u",
		   hapd->ap_pin_failures);
	if (hapd->ap_pin_failures < 3)
		return;

	wpa_printf(MSG_DEBUG, "WPS: Disable AP PIN");
	hapd->ap_pin_failures = 0;
	os_free(hapd->conf->ap_pin);
	hapd->conf->ap_pin = NULL;
}


#ifdef CONFIG_WPS_NFC

struct wpabuf * wpas_ap_wps_nfc_config_token(struct wpa_supplicant *wpa_s,
					     int ndef)
{
	struct hostapd_data *hapd;

	if (wpa_s->ap_iface == NULL)
		return NULL;
	hapd = wpa_s->ap_iface->bss[0];
	return hostapd_wps_nfc_config_token(hapd, ndef);
}


struct wpabuf * wpas_ap_wps_nfc_handover_sel(struct wpa_supplicant *wpa_s,
					     int ndef)
{
	struct hostapd_data *hapd;

	if (wpa_s->ap_iface == NULL)
		return NULL;
	hapd = wpa_s->ap_iface->bss[0];
	return hostapd_wps_nfc_hs_cr(hapd, ndef);
}


int wpas_ap_wps_nfc_report_handover(struct wpa_supplicant *wpa_s,
				    const struct wpabuf *req,
				    const struct wpabuf *sel)
{
	struct hostapd_data *hapd;

	if (wpa_s->ap_iface == NULL)
		return -1;
	hapd = wpa_s->ap_iface->bss[0];
	return hostapd_wps_nfc_report_handover(hapd, req, sel);
}

#endif /* CONFIG_WPS_NFC */

#endif /* CONFIG_WPS */


#ifdef CONFIG_CTRL_IFACE

int ap_ctrl_iface_sta_first(struct wpa_supplicant *wpa_s,
			    char *buf, size_t buflen)
{
	struct hostapd_data *hapd;

	if (wpa_s->ap_iface)
		hapd = wpa_s->ap_iface->bss[0];
	else if (wpa_s->ifmsh)
		hapd = wpa_s->ifmsh->bss[0];
	else
		return -1;
	return hostapd_ctrl_iface_sta_first(hapd, buf, buflen);
}


int ap_ctrl_iface_sta(struct wpa_supplicant *wpa_s, const char *txtaddr,
		      char *buf, size_t buflen)
{
	struct hostapd_data *hapd;

	if (wpa_s->ap_iface)
		hapd = wpa_s->ap_iface->bss[0];
	else if (wpa_s->ifmsh)
		hapd = wpa_s->ifmsh->bss[0];
	else
		return -1;
	return hostapd_ctrl_iface_sta(hapd, txtaddr, buf, buflen);
}


int ap_ctrl_iface_sta_next(struct wpa_supplicant *wpa_s, const char *txtaddr,
			   char *buf, size_t buflen)
{
	struct hostapd_data *hapd;

	if (wpa_s->ap_iface)
		hapd = wpa_s->ap_iface->bss[0];
	else if (wpa_s->ifmsh)
		hapd = wpa_s->ifmsh->bss[0];
	else
		return -1;
	return hostapd_ctrl_iface_sta_next(hapd, txtaddr, buf, buflen);
}


int ap_ctrl_iface_sta_disassociate(struct wpa_supplicant *wpa_s,
				   const char *txtaddr)
{
	if (wpa_s->ap_iface == NULL)
		return -1;
	return hostapd_ctrl_iface_disassociate(wpa_s->ap_iface->bss[0],
					       txtaddr);
}


int ap_ctrl_iface_sta_deauthenticate(struct wpa_supplicant *wpa_s,
				     const char *txtaddr)
{
	if (wpa_s->ap_iface == NULL)
		return -1;
	return hostapd_ctrl_iface_deauthenticate(wpa_s->ap_iface->bss[0],
						 txtaddr);
}


int ap_ctrl_iface_wpa_get_status(struct wpa_supplicant *wpa_s, char *buf,
				 size_t buflen, int verbose)
{
	char *pos = buf, *end = buf + buflen;
	int ret;
	struct hostapd_bss_config *conf;

	if (wpa_s->ap_iface == NULL)
		return -1;

	conf = wpa_s->ap_iface->bss[0]->conf;
	if (conf->wpa == 0)
		return 0;

	ret = os_snprintf(pos, end - pos,
			  "pairwise_cipher=%s\n"
			  "group_cipher=%s\n"
			  "key_mgmt=%s\n",
			  wpa_cipher_txt(conf->rsn_pairwise),
			  wpa_cipher_txt(conf->wpa_group),
			  wpa_key_mgmt_txt(conf->wpa_key_mgmt,
					   conf->wpa));
	if (os_snprintf_error(end - pos, ret))
		return pos - buf;
	pos += ret;
	return pos - buf;
}

#endif /* CONFIG_CTRL_IFACE */


int wpa_supplicant_ap_update_beacon(struct wpa_supplicant *wpa_s)
{
	struct hostapd_iface *iface = wpa_s->ap_iface;
	struct wpa_ssid *ssid = wpa_s->current_ssid;
	struct hostapd_data *hapd;

	if (ssid == NULL || wpa_s->ap_iface == NULL ||
	    ssid->mode == WPAS_MODE_INFRA ||
	    ssid->mode == WPAS_MODE_IBSS)
		return -1;

#ifdef CONFIG_P2P
	if (ssid->mode == WPAS_MODE_P2P_GO)
		iface->conf->bss[0]->p2p = P2P_ENABLED | P2P_GROUP_OWNER;
	else if (ssid->mode == WPAS_MODE_P2P_GROUP_FORMATION)
		iface->conf->bss[0]->p2p = P2P_ENABLED | P2P_GROUP_OWNER |
			P2P_GROUP_FORMATION;
#endif /* CONFIG_P2P */

	hapd = iface->bss[0];
	if (hapd->drv_priv == NULL)
		return -1;
	ieee802_11_set_beacons(iface);
	hostapd_set_ap_wps_ie(hapd);

	return 0;
}


int ap_switch_channel(struct wpa_supplicant *wpa_s,
		      struct csa_settings *settings)
{
#ifdef NEED_AP_MLME
	if (!wpa_s->ap_iface || !wpa_s->ap_iface->bss[0])
		return -1;

	return hostapd_switch_channel(wpa_s->ap_iface->bss[0], settings);
#else /* NEED_AP_MLME */
	return -1;
#endif /* NEED_AP_MLME */
}


#ifdef CONFIG_CTRL_IFACE
int ap_ctrl_iface_chanswitch(struct wpa_supplicant *wpa_s, const char *pos)
{
	struct csa_settings settings;
	int ret = hostapd_parse_csa_settings(pos, &settings);

	if (ret)
		return ret;

	return ap_switch_channel(wpa_s, &settings);
}
#endif /* CONFIG_CTRL_IFACE */


void wpas_ap_ch_switch(struct wpa_supplicant *wpa_s, int freq, int ht,
		       int offset, int width, int cf1, int cf2)
{
	if (!wpa_s->ap_iface)
		return;

	wpa_s->assoc_freq = freq;
	if (wpa_s->current_ssid)
		wpa_s->current_ssid->frequency = freq;
	hostapd_event_ch_switch(wpa_s->ap_iface->bss[0], freq, ht,
				offset, width, cf1, cf2);
}


int wpa_supplicant_ap_mac_addr_filter(struct wpa_supplicant *wpa_s,
				      const u8 *addr)
{
	struct hostapd_data *hapd;
	struct hostapd_bss_config *conf;

	if (!wpa_s->ap_iface)
		return -1;

	if (addr)
		wpa_printf(MSG_DEBUG, "AP: Set MAC address filter: " MACSTR,
			   MAC2STR(addr));
	else
		wpa_printf(MSG_DEBUG, "AP: Clear MAC address filter");

	hapd = wpa_s->ap_iface->bss[0];
	conf = hapd->conf;

	os_free(conf->accept_mac);
	conf->accept_mac = NULL;
	conf->num_accept_mac = 0;
	os_free(conf->deny_mac);
	conf->deny_mac = NULL;
	conf->num_deny_mac = 0;

	if (addr == NULL) {
		conf->macaddr_acl = ACCEPT_UNLESS_DENIED;
		return 0;
	}

	conf->macaddr_acl = DENY_UNLESS_ACCEPTED;
	conf->accept_mac = os_zalloc(sizeof(struct mac_acl_entry));
	if (conf->accept_mac == NULL)
		return -1;
	os_memcpy(conf->accept_mac[0].addr, addr, ETH_ALEN);
	conf->num_accept_mac = 1;

	return 0;
}


#ifdef CONFIG_WPS_NFC
int wpas_ap_wps_add_nfc_pw(struct wpa_supplicant *wpa_s, u16 pw_id,
			   const struct wpabuf *pw, const u8 *pubkey_hash)
{
	struct hostapd_data *hapd;
	struct wps_context *wps;

	if (!wpa_s->ap_iface)
		return -1;
	hapd = wpa_s->ap_iface->bss[0];
	wps = hapd->wps;

	if (wpa_s->p2pdev->conf->wps_nfc_dh_pubkey == NULL ||
	    wpa_s->p2pdev->conf->wps_nfc_dh_privkey == NULL) {
		wpa_printf(MSG_DEBUG, "P2P: No NFC DH key known");
		return -1;
	}

	dh5_free(wps->dh_ctx);
	wpabuf_free(wps->dh_pubkey);
	wpabuf_free(wps->dh_privkey);
	wps->dh_privkey = wpabuf_dup(
		wpa_s->p2pdev->conf->wps_nfc_dh_privkey);
	wps->dh_pubkey = wpabuf_dup(
		wpa_s->p2pdev->conf->wps_nfc_dh_pubkey);
	if (wps->dh_privkey == NULL || wps->dh_pubkey == NULL) {
		wps->dh_ctx = NULL;
		wpabuf_free(wps->dh_pubkey);
		wps->dh_pubkey = NULL;
		wpabuf_free(wps->dh_privkey);
		wps->dh_privkey = NULL;
		return -1;
	}
	wps->dh_ctx = dh5_init_fixed(wps->dh_privkey, wps->dh_pubkey);
	if (wps->dh_ctx == NULL)
		return -1;

	return wps_registrar_add_nfc_pw_token(hapd->wps->registrar, pubkey_hash,
					      pw_id,
					      pw ? wpabuf_head(pw) : NULL,
					      pw ? wpabuf_len(pw) : 0, 1);
}
#endif /* CONFIG_WPS_NFC */


#ifdef CONFIG_CTRL_IFACE
int wpas_ap_stop_ap(struct wpa_supplicant *wpa_s)
{
	struct hostapd_data *hapd;

	if (!wpa_s->ap_iface)
		return -1;
	hapd = wpa_s->ap_iface->bss[0];
	return hostapd_ctrl_iface_stop_ap(hapd);
}


int wpas_ap_pmksa_cache_list(struct wpa_supplicant *wpa_s, char *buf,
			     size_t len)
{
	size_t reply_len = 0, i;
	char ap_delimiter[] = "---- AP ----\n";
	char mesh_delimiter[] = "---- mesh ----\n";
	size_t dlen;

	if (wpa_s->ap_iface) {
		dlen = os_strlen(ap_delimiter);
		if (dlen > len - reply_len)
			return reply_len;
		os_memcpy(&buf[reply_len], ap_delimiter, dlen);
		reply_len += dlen;

		for (i = 0; i < wpa_s->ap_iface->num_bss; i++) {
			reply_len += hostapd_ctrl_iface_pmksa_list(
				wpa_s->ap_iface->bss[i],
				&buf[reply_len], len - reply_len);
		}
	}

	if (wpa_s->ifmsh) {
		dlen = os_strlen(mesh_delimiter);
		if (dlen > len - reply_len)
			return reply_len;
		os_memcpy(&buf[reply_len], mesh_delimiter, dlen);
		reply_len += dlen;

		reply_len += hostapd_ctrl_iface_pmksa_list(
			wpa_s->ifmsh->bss[0], &buf[reply_len],
			len - reply_len);
	}

	return reply_len;
}


void wpas_ap_pmksa_cache_flush(struct wpa_supplicant *wpa_s)
{
	size_t i;

	if (wpa_s->ap_iface) {
		for (i = 0; i < wpa_s->ap_iface->num_bss; i++)
			hostapd_ctrl_iface_pmksa_flush(wpa_s->ap_iface->bss[i]);
	}

	if (wpa_s->ifmsh)
		hostapd_ctrl_iface_pmksa_flush(wpa_s->ifmsh->bss[0]);
}


#ifdef CONFIG_PMKSA_CACHE_EXTERNAL
#ifdef CONFIG_MESH

int wpas_ap_pmksa_cache_list_mesh(struct wpa_supplicant *wpa_s, const u8 *addr,
				  char *buf, size_t len)
{
	return hostapd_ctrl_iface_pmksa_list_mesh(wpa_s->ifmsh->bss[0], addr,
						  &buf[0], len);
}


int wpas_ap_pmksa_cache_add_external(struct wpa_supplicant *wpa_s, char *cmd)
{
	struct external_pmksa_cache *entry;
	void *pmksa_cache;

	pmksa_cache = hostapd_ctrl_iface_pmksa_create_entry(wpa_s->own_addr,
							    cmd);
	if (!pmksa_cache)
		return -1;

	entry = os_zalloc(sizeof(struct external_pmksa_cache));
	if (!entry)
		return -1;

	entry->pmksa_cache = pmksa_cache;

	dl_list_add(&wpa_s->mesh_external_pmksa_cache, &entry->list);

	return 0;
}

#endif /* CONFIG_MESH */
#endif /* CONFIG_PMKSA_CACHE_EXTERNAL */

#endif /* CONFIG_CTRL_IFACE */


#ifdef NEED_AP_MLME
void wpas_ap_event_dfs_radar_detected(struct wpa_supplicant *wpa_s,
				      struct dfs_event *radar)
{
	if (!wpa_s->ap_iface || !wpa_s->ap_iface->bss[0])
		return;
	wpa_printf(MSG_DEBUG, "DFS radar detected on %d MHz", radar->freq);
	hostapd_dfs_radar_detected(wpa_s->ap_iface, radar->freq,
				   radar->ht_enabled, radar->chan_offset,
				   radar->chan_width,
				   radar->cf1, radar->cf2);
}


void wpas_ap_event_dfs_cac_started(struct wpa_supplicant *wpa_s,
				   struct dfs_event *radar)
{
	if (!wpa_s->ap_iface || !wpa_s->ap_iface->bss[0])
		return;
	wpa_printf(MSG_DEBUG, "DFS CAC started on %d MHz", radar->freq);
	hostapd_dfs_start_cac(wpa_s->ap_iface, radar->freq,
			      radar->ht_enabled, radar->chan_offset,
			      radar->chan_width, radar->cf1, radar->cf2);
}


void wpas_ap_event_dfs_cac_finished(struct wpa_supplicant *wpa_s,
				    struct dfs_event *radar)
{
	if (!wpa_s->ap_iface || !wpa_s->ap_iface->bss[0])
		return;
	wpa_printf(MSG_DEBUG, "DFS CAC finished on %d MHz", radar->freq);
	hostapd_dfs_complete_cac(wpa_s->ap_iface, 1, radar->freq,
				 radar->ht_enabled, radar->chan_offset,
				 radar->chan_width, radar->cf1, radar->cf2);
}


void wpas_ap_event_dfs_cac_aborted(struct wpa_supplicant *wpa_s,
				   struct dfs_event *radar)
{
	if (!wpa_s->ap_iface || !wpa_s->ap_iface->bss[0])
		return;
	wpa_printf(MSG_DEBUG, "DFS CAC aborted on %d MHz", radar->freq);
	hostapd_dfs_complete_cac(wpa_s->ap_iface, 0, radar->freq,
				 radar->ht_enabled, radar->chan_offset,
				 radar->chan_width, radar->cf1, radar->cf2);
}


void wpas_ap_event_dfs_cac_nop_finished(struct wpa_supplicant *wpa_s,
					struct dfs_event *radar)
{
	if (!wpa_s->ap_iface || !wpa_s->ap_iface->bss[0])
		return;
	wpa_printf(MSG_DEBUG, "DFS NOP finished on %d MHz", radar->freq);
	hostapd_dfs_nop_finished(wpa_s->ap_iface, radar->freq,
				 radar->ht_enabled, radar->chan_offset,
				 radar->chan_width, radar->cf1, radar->cf2);
}
#endif /* NEED_AP_MLME */


void ap_periodic(struct wpa_supplicant *wpa_s)
{
	if (wpa_s->ap_iface)
		hostapd_periodic_iface(wpa_s->ap_iface);
}
