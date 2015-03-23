/*
 * hostapd - Driver operations
 * Copyright (c) 2009-2010, Jouni Malinen <j@w1.fi>
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
#include "drivers/driver.h"
#include "common/ieee802_11_defs.h"
#include "wps/wps.h"
#include "hostapd.h"
#include "ieee802_11.h"
#include "sta_info.h"
#include "ap_config.h"
#include "p2p_hostapd.h"
#include "ap_drv_ops.h"


u32 hostapd_sta_flags_to_drv(u32 flags)
{
	int res = 0;
	if (flags & WLAN_STA_AUTHORIZED)
		res |= WPA_STA_AUTHORIZED;
	if (flags & WLAN_STA_WMM)
		res |= WPA_STA_WMM;
	if (flags & WLAN_STA_SHORT_PREAMBLE)
		res |= WPA_STA_SHORT_PREAMBLE;
	if (flags & WLAN_STA_MFP)
		res |= WPA_STA_MFP;
	return res;
}


int hostapd_set_ap_wps_ie(struct hostapd_data *hapd)
{
	struct wpabuf *beacon, *proberesp, *assocresp = NULL;
	int ret;

	if (hapd->driver == NULL || hapd->driver->set_ap_wps_ie == NULL)
		return 0;

	beacon = hapd->wps_beacon_ie;
	proberesp = hapd->wps_probe_resp_ie;

#ifdef CONFIG_P2P
	if (hapd->wps_beacon_ie == NULL && hapd->p2p_beacon_ie == NULL)
		beacon = NULL;
	else {
		beacon = wpabuf_alloc((hapd->wps_beacon_ie ?
				       wpabuf_len(hapd->wps_beacon_ie) : 0) +
				      (hapd->p2p_beacon_ie ?
				       wpabuf_len(hapd->p2p_beacon_ie) : 0));
		if (beacon == NULL)
			return -1;
		if (hapd->wps_beacon_ie)
			wpabuf_put_buf(beacon, hapd->wps_beacon_ie);
		if (hapd->p2p_beacon_ie)
			wpabuf_put_buf(beacon, hapd->p2p_beacon_ie);
	}

	if (hapd->wps_probe_resp_ie == NULL && hapd->p2p_probe_resp_ie == NULL)
		proberesp = NULL;
	else {
		proberesp = wpabuf_alloc(
			(hapd->wps_probe_resp_ie ?
			 wpabuf_len(hapd->wps_probe_resp_ie) : 0) +
			(hapd->p2p_probe_resp_ie ?
			 wpabuf_len(hapd->p2p_probe_resp_ie) : 0));
		if (proberesp == NULL) {
			wpabuf_free(beacon);
			return -1;
		}
		if (hapd->wps_probe_resp_ie)
			wpabuf_put_buf(proberesp, hapd->wps_probe_resp_ie);
		if (hapd->p2p_probe_resp_ie)
			wpabuf_put_buf(proberesp, hapd->p2p_probe_resp_ie);
	}
#endif /* CONFIG_P2P */

#ifdef CONFIG_P2P_MANAGER
	if (hapd->conf->p2p & P2P_MANAGE) {
		struct wpabuf *a;

		a = wpabuf_alloc(100 + (beacon ? wpabuf_len(beacon) : 0));
		if (a) {
			u8 *start, *p;
			if (beacon)
				wpabuf_put_buf(a, beacon);
			if (beacon != hapd->wps_beacon_ie)
				wpabuf_free(beacon);
			start = wpabuf_put(a, 0);
			p = hostapd_eid_p2p_manage(hapd, start);
			wpabuf_put(a, p - start);
			beacon = a;
		}

		a = wpabuf_alloc(100 + (proberesp ? wpabuf_len(proberesp) :
					0));
		if (a) {
			u8 *start, *p;
			if (proberesp)
				wpabuf_put_buf(a, proberesp);
			if (proberesp != hapd->wps_probe_resp_ie)
				wpabuf_free(proberesp);
			start = wpabuf_put(a, 0);
			p = hostapd_eid_p2p_manage(hapd, start);
			wpabuf_put(a, p - start);
			proberesp = a;
		}
	}
#endif /* CONFIG_P2P_MANAGER */

#ifdef CONFIG_WPS2
	if (hapd->conf->wps_state)
		assocresp = wps_build_assoc_resp_ie();
#endif /* CONFIG_WPS2 */

#ifdef CONFIG_P2P_MANAGER
	if (hapd->conf->p2p & P2P_MANAGE) {
		struct wpabuf *a;
		a = wpabuf_alloc(100 + (assocresp ? wpabuf_len(assocresp) :
					0));
		if (a) {
			u8 *start, *p;
			start = wpabuf_put(a, 0);
			p = hostapd_eid_p2p_manage(hapd, start);
			wpabuf_put(a, p - start);
			if (assocresp) {
				wpabuf_put_buf(a, assocresp);
				wpabuf_free(assocresp);
			}
			assocresp = a;
		}
	}
#endif /* CONFIG_P2P_MANAGER */

	ret = hapd->driver->set_ap_wps_ie(hapd->drv_priv, beacon, proberesp,
					  assocresp);

	if (beacon != hapd->wps_beacon_ie)
		wpabuf_free(beacon);
	if (proberesp != hapd->wps_probe_resp_ie)
		wpabuf_free(proberesp);
	wpabuf_free(assocresp);

	return ret;
}


int hostapd_set_authorized(struct hostapd_data *hapd,
			   struct sta_info *sta, int authorized)
{
	if (authorized) {
		return hostapd_sta_set_flags(hapd, sta->addr,
					     hostapd_sta_flags_to_drv(
						     sta->flags),
					     WPA_STA_AUTHORIZED, ~0);
	}

	return hostapd_sta_set_flags(hapd, sta->addr,
				     hostapd_sta_flags_to_drv(sta->flags),
				     0, ~WPA_STA_AUTHORIZED);
}


int hostapd_set_sta_flags(struct hostapd_data *hapd, struct sta_info *sta)
{
	int set_flags, total_flags, flags_and, flags_or;
	total_flags = hostapd_sta_flags_to_drv(sta->flags);
	set_flags = WPA_STA_SHORT_PREAMBLE | WPA_STA_WMM | WPA_STA_MFP;
	if (((!hapd->conf->ieee802_1x && !hapd->conf->wpa) ||
	     sta->auth_alg == WLAN_AUTH_FT) &&
	    sta->flags & WLAN_STA_AUTHORIZED)
		set_flags |= WPA_STA_AUTHORIZED;
	flags_or = total_flags & set_flags;
	flags_and = total_flags | ~set_flags;
	return hostapd_sta_set_flags(hapd, sta->addr, total_flags,
				     flags_or, flags_and);
}


int hostapd_set_drv_ieee8021x(struct hostapd_data *hapd, const char *ifname,
			      int enabled)
{
	struct wpa_bss_params params;
	os_memset(&params, 0, sizeof(params));
	params.ifname = ifname;
	params.enabled = enabled;
	if (enabled) {
		params.wpa = hapd->conf->wpa;
		params.ieee802_1x = hapd->conf->ieee802_1x;
		params.wpa_group = hapd->conf->wpa_group;
		params.wpa_pairwise = hapd->conf->wpa_pairwise;
		params.wpa_key_mgmt = hapd->conf->wpa_key_mgmt;
		params.rsn_preauth = hapd->conf->rsn_preauth;
#ifdef CONFIG_IEEE80211W
		params.ieee80211w = hapd->conf->ieee80211w;
#endif /* CONFIG_IEEE80211W */
	}
	return hostapd_set_ieee8021x(hapd, &params);
}


static int hostapd_set_ap_isolate(struct hostapd_data *hapd, int value)
{
	if (hapd->driver == NULL || hapd->driver->set_intra_bss == NULL)
		return 0;
	return hapd->driver->set_intra_bss(hapd->drv_priv, !value);
}


int hostapd_set_bss_params(struct hostapd_data *hapd, int use_protection)
{
	int ret = 0;
	int preamble;
#ifdef CONFIG_IEEE80211N
	u8 buf[60], *ht_capab, *ht_oper, *pos;

	pos = buf;
	ht_capab = pos;
	pos = hostapd_eid_ht_capabilities(hapd, pos);
	ht_oper = pos;
	pos = hostapd_eid_ht_operation(hapd, pos);
	if (pos > ht_oper && ht_oper > ht_capab &&
	    hostapd_set_ht_params(hapd, ht_capab + 2, ht_capab[1],
				  ht_oper + 2, ht_oper[1])) {
		wpa_printf(MSG_ERROR, "Could not set HT capabilities "
			   "for kernel driver");
		ret = -1;
	}

#endif /* CONFIG_IEEE80211N */

	if (hostapd_set_cts_protect(hapd, use_protection)) {
		wpa_printf(MSG_ERROR, "Failed to set CTS protect in kernel "
			   "driver");
		ret = -1;
	}

	if (hapd->iface->current_mode &&
	    hapd->iface->current_mode->mode == HOSTAPD_MODE_IEEE80211G &&
	    hostapd_set_short_slot_time(hapd,
					hapd->iface->num_sta_no_short_slot_time
					> 0 ? 0 : 1)) {
		wpa_printf(MSG_ERROR, "Failed to set Short Slot Time option "
			   "in kernel driver");
		ret = -1;
	}

	if (hapd->iface->num_sta_no_short_preamble == 0 &&
	    hapd->iconf->preamble == SHORT_PREAMBLE)
		preamble = SHORT_PREAMBLE;
	else
		preamble = LONG_PREAMBLE;
	if (hostapd_set_preamble(hapd, preamble)) {
		wpa_printf(MSG_ERROR, "Could not set preamble for kernel "
			   "driver");
		ret = -1;
	}

	if (hostapd_set_ap_isolate(hapd, hapd->conf->isolate) &&
	    hapd->conf->isolate) {
		wpa_printf(MSG_ERROR, "Could not enable AP isolation in "
			   "kernel driver");
		ret = -1;
	}

	return ret;
}


int hostapd_vlan_if_add(struct hostapd_data *hapd, const char *ifname)
{
	char force_ifname[IFNAMSIZ];
	u8 if_addr[ETH_ALEN];
	return hostapd_if_add(hapd, WPA_IF_AP_VLAN, ifname, hapd->own_addr,
			      NULL, NULL, force_ifname, if_addr, NULL);
}


int hostapd_vlan_if_remove(struct hostapd_data *hapd, const char *ifname)
{
	return hostapd_if_remove(hapd, WPA_IF_AP_VLAN, ifname);
}


int hostapd_set_wds_sta(struct hostapd_data *hapd, const u8 *addr, int aid,
			int val)
{
	const char *bridge = NULL;

	if (hapd->driver == NULL || hapd->driver->set_wds_sta == NULL)
		return 0;
	if (hapd->conf->wds_bridge[0])
		bridge = hapd->conf->wds_bridge;
	else if (hapd->conf->bridge[0])
		bridge = hapd->conf->bridge;
	return hapd->driver->set_wds_sta(hapd->drv_priv, addr, aid, val,
					 bridge);
}


int hostapd_sta_add(struct hostapd_data *hapd,
		    const u8 *addr, u16 aid, u16 capability,
		    const u8 *supp_rates, size_t supp_rates_len,
		    u16 listen_interval,
		    const struct ieee80211_ht_capabilities *ht_capab)
{
	struct hostapd_sta_add_params params;

	if (hapd->driver == NULL)
		return 0;
	if (hapd->driver->sta_add == NULL)
		return 0;

	os_memset(&params, 0, sizeof(params));
	params.addr = addr;
	params.aid = aid;
	params.capability = capability;
	params.supp_rates = supp_rates;
	params.supp_rates_len = supp_rates_len;
	params.listen_interval = listen_interval;
	params.ht_capabilities = ht_capab;
	return hapd->driver->sta_add(hapd->drv_priv, &params);
}


int hostapd_set_privacy(struct hostapd_data *hapd, int enabled)
{
	if (hapd->driver == NULL || hapd->driver->set_privacy == NULL)
		return 0;
	return hapd->driver->set_privacy(hapd->drv_priv, enabled);
}


int hostapd_set_generic_elem(struct hostapd_data *hapd, const u8 *elem,
			     size_t elem_len)
{
	if (hapd->driver == NULL || hapd->driver->set_generic_elem == NULL)
		return 0;
	return hapd->driver->set_generic_elem(hapd->drv_priv, elem, elem_len);
}


int hostapd_get_ssid(struct hostapd_data *hapd, u8 *buf, size_t len)
{
	if (hapd->driver == NULL || hapd->driver->hapd_get_ssid == NULL)
		return 0;
	return hapd->driver->hapd_get_ssid(hapd->drv_priv, buf, len);
}


int hostapd_set_ssid(struct hostapd_data *hapd, const u8 *buf, size_t len)
{
	if (hapd->driver == NULL || hapd->driver->hapd_set_ssid == NULL)
		return 0;
	return hapd->driver->hapd_set_ssid(hapd->drv_priv, buf, len);
}


int hostapd_if_add(struct hostapd_data *hapd, enum wpa_driver_if_type type,
		   const char *ifname, const u8 *addr, void *bss_ctx,
		   void **drv_priv, char *force_ifname, u8 *if_addr,
		   const char *bridge)
{
	if (hapd->driver == NULL || hapd->driver->if_add == NULL)
		return -1;
	return hapd->driver->if_add(hapd->drv_priv, type, ifname, addr,
				    bss_ctx, drv_priv, force_ifname, if_addr,
				    bridge);
}


int hostapd_if_remove(struct hostapd_data *hapd, enum wpa_driver_if_type type,
		      const char *ifname)
{
	if (hapd->driver == NULL || hapd->driver->if_remove == NULL)
		return -1;
	return hapd->driver->if_remove(hapd->drv_priv, type, ifname);
}


int hostapd_set_ieee8021x(struct hostapd_data *hapd,
			  struct wpa_bss_params *params)
{
	if (hapd->driver == NULL || hapd->driver->set_ieee8021x == NULL)
		return 0;
	return hapd->driver->set_ieee8021x(hapd->drv_priv, params);
}


int hostapd_get_seqnum(const char *ifname, struct hostapd_data *hapd,
		       const u8 *addr, int idx, u8 *seq)
{
	if (hapd->driver == NULL || hapd->driver->get_seqnum == NULL)
		return 0;
	return hapd->driver->get_seqnum(ifname, hapd->drv_priv, addr, idx,
					seq);
}


int hostapd_flush(struct hostapd_data *hapd)
{
	if (hapd->driver == NULL || hapd->driver->flush == NULL)
		return 0;
	return hapd->driver->flush(hapd->drv_priv);
}


int hostapd_set_freq(struct hostapd_data *hapd, int mode, int freq,
		     int channel, int ht_enabled, int sec_channel_offset)
{
	struct hostapd_freq_params data;
	if (hapd->driver == NULL)
		return 0;
	if (hapd->driver->set_freq == NULL)
		return 0;
	os_memset(&data, 0, sizeof(data));
	data.mode = mode;
	data.freq = freq;
	data.channel = channel;
	data.ht_enabled = ht_enabled;
	data.sec_channel_offset = sec_channel_offset;
	return hapd->driver->set_freq(hapd->drv_priv, &data);
}

int hostapd_set_rts(struct hostapd_data *hapd, int rts)
{
	if (hapd->driver == NULL || hapd->driver->set_rts == NULL)
		return 0;
	return hapd->driver->set_rts(hapd->drv_priv, rts);
}


int hostapd_set_frag(struct hostapd_data *hapd, int frag)
{
	if (hapd->driver == NULL || hapd->driver->set_frag == NULL)
		return 0;
	return hapd->driver->set_frag(hapd->drv_priv, frag);
}


int hostapd_sta_set_flags(struct hostapd_data *hapd, u8 *addr,
			  int total_flags, int flags_or, int flags_and)
{
	if (hapd->driver == NULL || hapd->driver->sta_set_flags == NULL)
		return 0;
	return hapd->driver->sta_set_flags(hapd->drv_priv, addr, total_flags,
					   flags_or, flags_and);
}


int hostapd_set_rate_sets(struct hostapd_data *hapd, int *supp_rates,
			  int *basic_rates, int mode)
{
	if (hapd->driver == NULL || hapd->driver->set_rate_sets == NULL)
		return 0;
	return hapd->driver->set_rate_sets(hapd->drv_priv, supp_rates,
					   basic_rates, mode);
}


int hostapd_set_country(struct hostapd_data *hapd, const char *country)
{
	if (hapd->driver == NULL ||
	    hapd->driver->set_country == NULL)
		return 0;
	return hapd->driver->set_country(hapd->drv_priv, country);
}


int hostapd_set_cts_protect(struct hostapd_data *hapd, int value)
{
	if (hapd->driver == NULL || hapd->driver->set_cts_protect == NULL)
		return 0;
	return hapd->driver->set_cts_protect(hapd->drv_priv, value);
}


int hostapd_set_preamble(struct hostapd_data *hapd, int value)
{
	if (hapd->driver == NULL || hapd->driver->set_preamble == NULL)
		return 0;
	return hapd->driver->set_preamble(hapd->drv_priv, value);
}


int hostapd_set_short_slot_time(struct hostapd_data *hapd, int value)
{
	if (hapd->driver == NULL || hapd->driver->set_short_slot_time == NULL)
		return 0;
	return hapd->driver->set_short_slot_time(hapd->drv_priv, value);
}


int hostapd_set_tx_queue_params(struct hostapd_data *hapd, int queue, int aifs,
				int cw_min, int cw_max, int burst_time)
{
	if (hapd->driver == NULL || hapd->driver->set_tx_queue_params == NULL)
		return 0;
	return hapd->driver->set_tx_queue_params(hapd->drv_priv, queue, aifs,
						 cw_min, cw_max, burst_time);
}


int hostapd_valid_bss_mask(struct hostapd_data *hapd, const u8 *addr,
			   const u8 *mask)
{
	if (hapd->driver == NULL || hapd->driver->valid_bss_mask == NULL)
		return 1;
	return hapd->driver->valid_bss_mask(hapd->drv_priv, addr, mask);
}


struct hostapd_hw_modes *
hostapd_get_hw_feature_data(struct hostapd_data *hapd, u16 *num_modes,
			    u16 *flags)
{
	if (hapd->driver == NULL ||
	    hapd->driver->get_hw_feature_data == NULL)
		return NULL;
	return hapd->driver->get_hw_feature_data(hapd->drv_priv, num_modes,
						 flags);
}


int hostapd_driver_commit(struct hostapd_data *hapd)
{
	if (hapd->driver == NULL || hapd->driver->commit == NULL)
		return 0;
	return hapd->driver->commit(hapd->drv_priv);
}


int hostapd_set_ht_params(struct hostapd_data *hapd,
			  const u8 *ht_capab, size_t ht_capab_len,
			  const u8 *ht_oper, size_t ht_oper_len)
{
	if (hapd->driver == NULL || hapd->driver->set_ht_params == NULL ||
	    ht_capab == NULL || ht_oper == NULL)
		return 0;
	return hapd->driver->set_ht_params(hapd->drv_priv,
					   ht_capab, ht_capab_len,
					   ht_oper, ht_oper_len);
}


int hostapd_drv_none(struct hostapd_data *hapd)
{
	return hapd->driver && os_strcmp(hapd->driver->name, "none") == 0;
}


int hostapd_driver_scan(struct hostapd_data *hapd,
			struct wpa_driver_scan_params *params)
{
	if (hapd->driver && hapd->driver->scan2)
		return hapd->driver->scan2(hapd->drv_priv, params);
	return -1;
}


struct wpa_scan_results * hostapd_driver_get_scan_results(
	struct hostapd_data *hapd)
{
	if (hapd->driver && hapd->driver->get_scan_results2)
		return hapd->driver->get_scan_results2(hapd->drv_priv);
	return NULL;
}


int hostapd_driver_set_noa(struct hostapd_data *hapd, u8 count, int start,
			   int duration)
{
	if (hapd->driver && hapd->driver->set_noa)
		return hapd->driver->set_noa(hapd->drv_priv, count, start,
					     duration);
	return -1;
}


int hostapd_drv_set_key(const char *ifname, struct hostapd_data *hapd,
			enum wpa_alg alg, const u8 *addr,
			int key_idx, int set_tx,
			const u8 *seq, size_t seq_len,
			const u8 *key, size_t key_len)
{
	if (hapd->driver == NULL || hapd->driver->set_key == NULL)
		return 0;
	return hapd->driver->set_key(ifname, hapd->drv_priv, alg, addr,
				     key_idx, set_tx, seq, seq_len, key,
				     key_len);
}


int hostapd_drv_send_mlme(struct hostapd_data *hapd,
			  const void *msg, size_t len)
{
	if (hapd->driver == NULL || hapd->driver->send_mlme == NULL)
		return 0;
	return hapd->driver->send_mlme(hapd->drv_priv, msg, len);
}


int hostapd_drv_sta_deauth(struct hostapd_data *hapd,
			   const u8 *addr, int reason)
{
	if (hapd->driver == NULL || hapd->driver->sta_deauth == NULL)
		return 0;
	return hapd->driver->sta_deauth(hapd->drv_priv, hapd->own_addr, addr,
					reason);
}


int hostapd_drv_sta_disassoc(struct hostapd_data *hapd,
			     const u8 *addr, int reason)
{
	if (hapd->driver == NULL || hapd->driver->sta_disassoc == NULL)
		return 0;
	return hapd->driver->sta_disassoc(hapd->drv_priv, hapd->own_addr, addr,
					  reason);
}
