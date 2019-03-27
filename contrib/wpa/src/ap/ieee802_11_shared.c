/*
 * hostapd / IEEE 802.11 Management
 * Copyright (c) 2002-2012, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "common/ieee802_11_defs.h"
#include "hostapd.h"
#include "sta_info.h"
#include "ap_config.h"
#include "ap_drv_ops.h"
#include "ieee802_11.h"


#ifdef CONFIG_IEEE80211W

u8 * hostapd_eid_assoc_comeback_time(struct hostapd_data *hapd,
				     struct sta_info *sta, u8 *eid)
{
	u8 *pos = eid;
	u32 timeout, tu;
	struct os_reltime now, passed;

	*pos++ = WLAN_EID_TIMEOUT_INTERVAL;
	*pos++ = 5;
	*pos++ = WLAN_TIMEOUT_ASSOC_COMEBACK;
	os_get_reltime(&now);
	os_reltime_sub(&now, &sta->sa_query_start, &passed);
	tu = (passed.sec * 1000000 + passed.usec) / 1024;
	if (hapd->conf->assoc_sa_query_max_timeout > tu)
		timeout = hapd->conf->assoc_sa_query_max_timeout - tu;
	else
		timeout = 0;
	if (timeout < hapd->conf->assoc_sa_query_max_timeout)
		timeout++; /* add some extra time for local timers */
	WPA_PUT_LE32(pos, timeout);
	pos += 4;

	return pos;
}


/* MLME-SAQuery.request */
void ieee802_11_send_sa_query_req(struct hostapd_data *hapd,
				  const u8 *addr, const u8 *trans_id)
{
	struct ieee80211_mgmt mgmt;
	u8 *end;

	wpa_printf(MSG_DEBUG, "IEEE 802.11: Sending SA Query Request to "
		   MACSTR, MAC2STR(addr));
	wpa_hexdump(MSG_DEBUG, "IEEE 802.11: SA Query Transaction ID",
		    trans_id, WLAN_SA_QUERY_TR_ID_LEN);

	os_memset(&mgmt, 0, sizeof(mgmt));
	mgmt.frame_control = IEEE80211_FC(WLAN_FC_TYPE_MGMT,
					  WLAN_FC_STYPE_ACTION);
	os_memcpy(mgmt.da, addr, ETH_ALEN);
	os_memcpy(mgmt.sa, hapd->own_addr, ETH_ALEN);
	os_memcpy(mgmt.bssid, hapd->own_addr, ETH_ALEN);
	mgmt.u.action.category = WLAN_ACTION_SA_QUERY;
	mgmt.u.action.u.sa_query_req.action = WLAN_SA_QUERY_REQUEST;
	os_memcpy(mgmt.u.action.u.sa_query_req.trans_id, trans_id,
		  WLAN_SA_QUERY_TR_ID_LEN);
	end = mgmt.u.action.u.sa_query_req.trans_id + WLAN_SA_QUERY_TR_ID_LEN;
	if (hostapd_drv_send_mlme(hapd, &mgmt, end - (u8 *) &mgmt, 0) < 0)
		wpa_printf(MSG_INFO, "ieee802_11_send_sa_query_req: send failed");
}


static void ieee802_11_send_sa_query_resp(struct hostapd_data *hapd,
					  const u8 *sa, const u8 *trans_id)
{
	struct sta_info *sta;
	struct ieee80211_mgmt resp;
	u8 *end;

	wpa_printf(MSG_DEBUG, "IEEE 802.11: Received SA Query Request from "
		   MACSTR, MAC2STR(sa));
	wpa_hexdump(MSG_DEBUG, "IEEE 802.11: SA Query Transaction ID",
		    trans_id, WLAN_SA_QUERY_TR_ID_LEN);

	sta = ap_get_sta(hapd, sa);
	if (sta == NULL || !(sta->flags & WLAN_STA_ASSOC)) {
		wpa_printf(MSG_DEBUG, "IEEE 802.11: Ignore SA Query Request "
			   "from unassociated STA " MACSTR, MAC2STR(sa));
		return;
	}

	wpa_printf(MSG_DEBUG, "IEEE 802.11: Sending SA Query Response to "
		   MACSTR, MAC2STR(sa));

	os_memset(&resp, 0, sizeof(resp));
	resp.frame_control = IEEE80211_FC(WLAN_FC_TYPE_MGMT,
					  WLAN_FC_STYPE_ACTION);
	os_memcpy(resp.da, sa, ETH_ALEN);
	os_memcpy(resp.sa, hapd->own_addr, ETH_ALEN);
	os_memcpy(resp.bssid, hapd->own_addr, ETH_ALEN);
	resp.u.action.category = WLAN_ACTION_SA_QUERY;
	resp.u.action.u.sa_query_req.action = WLAN_SA_QUERY_RESPONSE;
	os_memcpy(resp.u.action.u.sa_query_req.trans_id, trans_id,
		  WLAN_SA_QUERY_TR_ID_LEN);
	end = resp.u.action.u.sa_query_req.trans_id + WLAN_SA_QUERY_TR_ID_LEN;
	if (hostapd_drv_send_mlme(hapd, &resp, end - (u8 *) &resp, 0) < 0)
		wpa_printf(MSG_INFO, "ieee80211_mgmt_sa_query_request: send failed");
}


void ieee802_11_sa_query_action(struct hostapd_data *hapd, const u8 *sa,
				const u8 action_type, const u8 *trans_id)
{
	struct sta_info *sta;
	int i;

	if (action_type == WLAN_SA_QUERY_REQUEST) {
		ieee802_11_send_sa_query_resp(hapd, sa, trans_id);
		return;
	}

	if (action_type != WLAN_SA_QUERY_RESPONSE) {
		wpa_printf(MSG_DEBUG, "IEEE 802.11: Unexpected SA Query "
			   "Action %d", action_type);
		return;
	}

	wpa_printf(MSG_DEBUG, "IEEE 802.11: Received SA Query Response from "
		   MACSTR, MAC2STR(sa));
	wpa_hexdump(MSG_DEBUG, "IEEE 802.11: SA Query Transaction ID",
		    trans_id, WLAN_SA_QUERY_TR_ID_LEN);

	/* MLME-SAQuery.confirm */

	sta = ap_get_sta(hapd, sa);
	if (sta == NULL || sta->sa_query_trans_id == NULL) {
		wpa_printf(MSG_DEBUG, "IEEE 802.11: No matching STA with "
			   "pending SA Query request found");
		return;
	}

	for (i = 0; i < sta->sa_query_count; i++) {
		if (os_memcmp(sta->sa_query_trans_id +
			      i * WLAN_SA_QUERY_TR_ID_LEN,
			      trans_id, WLAN_SA_QUERY_TR_ID_LEN) == 0)
			break;
	}

	if (i >= sta->sa_query_count) {
		wpa_printf(MSG_DEBUG, "IEEE 802.11: No matching SA Query "
			   "transaction identifier found");
		return;
	}

	hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE80211,
		       HOSTAPD_LEVEL_DEBUG,
		       "Reply to pending SA Query received");
	ap_sta_stop_sa_query(hapd, sta);
}

#endif /* CONFIG_IEEE80211W */


static void hostapd_ext_capab_byte(struct hostapd_data *hapd, u8 *pos, int idx)
{
	*pos = 0x00;

	switch (idx) {
	case 0: /* Bits 0-7 */
		if (hapd->iconf->obss_interval)
			*pos |= 0x01; /* Bit 0 - Coexistence management */
		if (hapd->iface->drv_flags & WPA_DRIVER_FLAGS_AP_CSA)
			*pos |= 0x04; /* Bit 2 - Extended Channel Switching */
		break;
	case 1: /* Bits 8-15 */
		if (hapd->conf->proxy_arp)
			*pos |= 0x10; /* Bit 12 - Proxy ARP */
		if (hapd->conf->coloc_intf_reporting) {
			/* Bit 13 - Collocated Interference Reporting */
			*pos |= 0x20;
		}
		break;
	case 2: /* Bits 16-23 */
		if (hapd->conf->wnm_sleep_mode)
			*pos |= 0x02; /* Bit 17 - WNM-Sleep Mode */
		if (hapd->conf->bss_transition)
			*pos |= 0x08; /* Bit 19 - BSS Transition */
		break;
	case 3: /* Bits 24-31 */
#ifdef CONFIG_WNM_AP
		*pos |= 0x02; /* Bit 25 - SSID List */
#endif /* CONFIG_WNM_AP */
		if (hapd->conf->time_advertisement == 2)
			*pos |= 0x08; /* Bit 27 - UTC TSF Offset */
		if (hapd->conf->interworking)
			*pos |= 0x80; /* Bit 31 - Interworking */
		break;
	case 4: /* Bits 32-39 */
		if (hapd->conf->qos_map_set_len)
			*pos |= 0x01; /* Bit 32 - QoS Map */
		if (hapd->conf->tdls & TDLS_PROHIBIT)
			*pos |= 0x40; /* Bit 38 - TDLS Prohibited */
		if (hapd->conf->tdls & TDLS_PROHIBIT_CHAN_SWITCH) {
			/* Bit 39 - TDLS Channel Switching Prohibited */
			*pos |= 0x80;
		}
		break;
	case 5: /* Bits 40-47 */
#ifdef CONFIG_HS20
		if (hapd->conf->hs20)
			*pos |= 0x40; /* Bit 46 - WNM-Notification */
#endif /* CONFIG_HS20 */
#ifdef CONFIG_MBO
		if (hapd->conf->mbo_enabled)
			*pos |= 0x40; /* Bit 46 - WNM-Notification */
#endif /* CONFIG_MBO */
		break;
	case 6: /* Bits 48-55 */
		if (hapd->conf->ssid.utf8_ssid)
			*pos |= 0x01; /* Bit 48 - UTF-8 SSID */
		break;
	case 7: /* Bits 56-63 */
		break;
	case 8: /* Bits 64-71 */
		if (hapd->conf->ftm_responder)
			*pos |= 0x40; /* Bit 70 - FTM responder */
		if (hapd->conf->ftm_initiator)
			*pos |= 0x80; /* Bit 71 - FTM initiator */
		break;
	case 9: /* Bits 72-79 */
#ifdef CONFIG_FILS
		if ((hapd->conf->wpa & WPA_PROTO_RSN) &&
		    wpa_key_mgmt_fils(hapd->conf->wpa_key_mgmt))
			*pos |= 0x01;
#endif /* CONFIG_FILS */
		break;
	}
}


u8 * hostapd_eid_ext_capab(struct hostapd_data *hapd, u8 *eid)
{
	u8 *pos = eid;
	u8 len = 0, i;

	if (hapd->conf->tdls & (TDLS_PROHIBIT | TDLS_PROHIBIT_CHAN_SWITCH))
		len = 5;
	if (len < 4 && hapd->conf->interworking)
		len = 4;
	if (len < 3 && hapd->conf->wnm_sleep_mode)
		len = 3;
	if (len < 1 && hapd->iconf->obss_interval)
		len = 1;
	if (len < 7 && hapd->conf->ssid.utf8_ssid)
		len = 7;
	if (len < 9 &&
	    (hapd->conf->ftm_initiator || hapd->conf->ftm_responder))
		len = 9;
#ifdef CONFIG_WNM_AP
	if (len < 4)
		len = 4;
#endif /* CONFIG_WNM_AP */
#ifdef CONFIG_HS20
	if (hapd->conf->hs20 && len < 6)
		len = 6;
#endif /* CONFIG_HS20 */
#ifdef CONFIG_MBO
	if (hapd->conf->mbo_enabled && len < 6)
		len = 6;
#endif /* CONFIG_MBO */
#ifdef CONFIG_FILS
	if ((!(hapd->conf->wpa & WPA_PROTO_RSN) ||
	     !wpa_key_mgmt_fils(hapd->conf->wpa_key_mgmt)) && len < 10)
		len = 10;
#endif /* CONFIG_FILS */
	if (len < hapd->iface->extended_capa_len)
		len = hapd->iface->extended_capa_len;
	if (len == 0)
		return eid;

	*pos++ = WLAN_EID_EXT_CAPAB;
	*pos++ = len;
	for (i = 0; i < len; i++, pos++) {
		hostapd_ext_capab_byte(hapd, pos, i);

		if (i < hapd->iface->extended_capa_len) {
			*pos &= ~hapd->iface->extended_capa_mask[i];
			*pos |= hapd->iface->extended_capa[i];
		}
	}

	while (len > 0 && eid[1 + len] == 0) {
		len--;
		eid[1] = len;
	}
	if (len == 0)
		return eid;

	return eid + 2 + len;
}


u8 * hostapd_eid_qos_map_set(struct hostapd_data *hapd, u8 *eid)
{
	u8 *pos = eid;
	u8 len = hapd->conf->qos_map_set_len;

	if (!len)
		return eid;

	*pos++ = WLAN_EID_QOS_MAP_SET;
	*pos++ = len;
	os_memcpy(pos, hapd->conf->qos_map_set, len);
	pos += len;

	return pos;
}


u8 * hostapd_eid_interworking(struct hostapd_data *hapd, u8 *eid)
{
	u8 *pos = eid;
#ifdef CONFIG_INTERWORKING
	u8 *len;

	if (!hapd->conf->interworking)
		return eid;

	*pos++ = WLAN_EID_INTERWORKING;
	len = pos++;

	*pos = hapd->conf->access_network_type;
	if (hapd->conf->internet)
		*pos |= INTERWORKING_ANO_INTERNET;
	if (hapd->conf->asra)
		*pos |= INTERWORKING_ANO_ASRA;
	if (hapd->conf->esr)
		*pos |= INTERWORKING_ANO_ESR;
	if (hapd->conf->uesa)
		*pos |= INTERWORKING_ANO_UESA;
	pos++;

	if (hapd->conf->venue_info_set) {
		*pos++ = hapd->conf->venue_group;
		*pos++ = hapd->conf->venue_type;
	}

	if (!is_zero_ether_addr(hapd->conf->hessid)) {
		os_memcpy(pos, hapd->conf->hessid, ETH_ALEN);
		pos += ETH_ALEN;
	}

	*len = pos - len - 1;
#endif /* CONFIG_INTERWORKING */

	return pos;
}


u8 * hostapd_eid_adv_proto(struct hostapd_data *hapd, u8 *eid)
{
	u8 *pos = eid;
#ifdef CONFIG_INTERWORKING

	/* TODO: Separate configuration for ANQP? */
	if (!hapd->conf->interworking)
		return eid;

	*pos++ = WLAN_EID_ADV_PROTO;
	*pos++ = 2;
	*pos++ = 0x7F; /* Query Response Length Limit | PAME-BI */
	*pos++ = ACCESS_NETWORK_QUERY_PROTOCOL;
#endif /* CONFIG_INTERWORKING */

	return pos;
}


u8 * hostapd_eid_roaming_consortium(struct hostapd_data *hapd, u8 *eid)
{
	u8 *pos = eid;
#ifdef CONFIG_INTERWORKING
	u8 *len;
	unsigned int i, count;

	if (!hapd->conf->interworking ||
	    hapd->conf->roaming_consortium == NULL ||
	    hapd->conf->roaming_consortium_count == 0)
		return eid;

	*pos++ = WLAN_EID_ROAMING_CONSORTIUM;
	len = pos++;

	/* Number of ANQP OIs (in addition to the max 3 listed here) */
	if (hapd->conf->roaming_consortium_count > 3 + 255)
		*pos++ = 255;
	else if (hapd->conf->roaming_consortium_count > 3)
		*pos++ = hapd->conf->roaming_consortium_count - 3;
	else
		*pos++ = 0;

	/* OU #1 and #2 Lengths */
	*pos = hapd->conf->roaming_consortium[0].len;
	if (hapd->conf->roaming_consortium_count > 1)
		*pos |= hapd->conf->roaming_consortium[1].len << 4;
	pos++;

	if (hapd->conf->roaming_consortium_count > 3)
		count = 3;
	else
		count = hapd->conf->roaming_consortium_count;

	for (i = 0; i < count; i++) {
		os_memcpy(pos, hapd->conf->roaming_consortium[i].oi,
			  hapd->conf->roaming_consortium[i].len);
		pos += hapd->conf->roaming_consortium[i].len;
	}

	*len = pos - len - 1;
#endif /* CONFIG_INTERWORKING */

	return pos;
}


u8 * hostapd_eid_time_adv(struct hostapd_data *hapd, u8 *eid)
{
	if (hapd->conf->time_advertisement != 2)
		return eid;

	if (hapd->time_adv == NULL &&
	    hostapd_update_time_adv(hapd) < 0)
		return eid;

	if (hapd->time_adv == NULL)
		return eid;

	os_memcpy(eid, wpabuf_head(hapd->time_adv),
		  wpabuf_len(hapd->time_adv));
	eid += wpabuf_len(hapd->time_adv);

	return eid;
}


u8 * hostapd_eid_time_zone(struct hostapd_data *hapd, u8 *eid)
{
	size_t len;

	if (hapd->conf->time_advertisement != 2 || !hapd->conf->time_zone)
		return eid;

	len = os_strlen(hapd->conf->time_zone);

	*eid++ = WLAN_EID_TIME_ZONE;
	*eid++ = len;
	os_memcpy(eid, hapd->conf->time_zone, len);
	eid += len;

	return eid;
}


int hostapd_update_time_adv(struct hostapd_data *hapd)
{
	const int elen = 2 + 1 + 10 + 5 + 1;
	struct os_time t;
	struct os_tm tm;
	u8 *pos;

	if (hapd->conf->time_advertisement != 2)
		return 0;

	if (os_get_time(&t) < 0 || os_gmtime(t.sec, &tm) < 0)
		return -1;

	if (!hapd->time_adv) {
		hapd->time_adv = wpabuf_alloc(elen);
		if (hapd->time_adv == NULL)
			return -1;
		pos = wpabuf_put(hapd->time_adv, elen);
	} else
		pos = wpabuf_mhead_u8(hapd->time_adv);

	*pos++ = WLAN_EID_TIME_ADVERTISEMENT;
	*pos++ = 1 + 10 + 5 + 1;

	*pos++ = 2; /* UTC time at which the TSF timer is 0 */

	/* Time Value at TSF 0 */
	/* FIX: need to calculate this based on the current TSF value */
	WPA_PUT_LE16(pos, tm.year); /* Year */
	pos += 2;
	*pos++ = tm.month; /* Month */
	*pos++ = tm.day; /* Day of month */
	*pos++ = tm.hour; /* Hours */
	*pos++ = tm.min; /* Minutes */
	*pos++ = tm.sec; /* Seconds */
	WPA_PUT_LE16(pos, 0); /* Milliseconds (not used) */
	pos += 2;
	*pos++ = 0; /* Reserved */

	/* Time Error */
	/* TODO: fill in an estimate on the error */
	*pos++ = 0;
	*pos++ = 0;
	*pos++ = 0;
	*pos++ = 0;
	*pos++ = 0;

	*pos++ = hapd->time_update_counter++;

	return 0;
}


u8 * hostapd_eid_bss_max_idle_period(struct hostapd_data *hapd, u8 *eid)
{
	u8 *pos = eid;

#ifdef CONFIG_WNM_AP
	if (hapd->conf->ap_max_inactivity > 0) {
		unsigned int val;
		*pos++ = WLAN_EID_BSS_MAX_IDLE_PERIOD;
		*pos++ = 3;
		val = hapd->conf->ap_max_inactivity;
		if (val > 68000)
			val = 68000;
		val *= 1000;
		val /= 1024;
		if (val == 0)
			val = 1;
		if (val > 65535)
			val = 65535;
		WPA_PUT_LE16(pos, val);
		pos += 2;
		*pos++ = 0x00; /* TODO: Protected Keep-Alive Required */
	}
#endif /* CONFIG_WNM_AP */

	return pos;
}


#ifdef CONFIG_MBO

u8 * hostapd_eid_mbo(struct hostapd_data *hapd, u8 *eid, size_t len)
{
	u8 mbo[9], *mbo_pos = mbo;
	u8 *pos = eid;

	if (!hapd->conf->mbo_enabled &&
	    !OCE_STA_CFON_ENABLED(hapd) && !OCE_AP_ENABLED(hapd))
		return eid;

	if (hapd->conf->mbo_enabled) {
		*mbo_pos++ = MBO_ATTR_ID_AP_CAPA_IND;
		*mbo_pos++ = 1;
		/* Not Cellular aware */
		*mbo_pos++ = 0;
	}

	if (hapd->conf->mbo_enabled && hapd->mbo_assoc_disallow) {
		*mbo_pos++ = MBO_ATTR_ID_ASSOC_DISALLOW;
		*mbo_pos++ = 1;
		*mbo_pos++ = hapd->mbo_assoc_disallow;
	}

	if (OCE_STA_CFON_ENABLED(hapd) || OCE_AP_ENABLED(hapd)) {
		u8 ctrl;

		ctrl = OCE_RELEASE;
		if (OCE_STA_CFON_ENABLED(hapd) && !OCE_AP_ENABLED(hapd))
			ctrl |= OCE_IS_STA_CFON;

		*mbo_pos++ = OCE_ATTR_ID_CAPA_IND;
		*mbo_pos++ = 1;
		*mbo_pos++ = ctrl;
	}

	pos += mbo_add_ie(pos, len, mbo, mbo_pos - mbo);

	return pos;
}


u8 hostapd_mbo_ie_len(struct hostapd_data *hapd)
{
	u8 len;

	if (!hapd->conf->mbo_enabled &&
	    !OCE_STA_CFON_ENABLED(hapd) && !OCE_AP_ENABLED(hapd))
		return 0;

	/*
	 * MBO IE header (6) + Capability Indication attribute (3) +
	 * Association Disallowed attribute (3) = 12
	 */
	len = 6;
	if (hapd->conf->mbo_enabled)
		len += 3 + (hapd->mbo_assoc_disallow ? 3 : 0);

	/* OCE capability indication attribute (3) */
	if (OCE_STA_CFON_ENABLED(hapd) || OCE_AP_ENABLED(hapd))
		len += 3;

	return len;
}

#endif /* CONFIG_MBO */


#ifdef CONFIG_OWE
static int hostapd_eid_owe_trans_enabled(struct hostapd_data *hapd)
{
	return hapd->conf->owe_transition_ssid_len > 0 &&
		!is_zero_ether_addr(hapd->conf->owe_transition_bssid);
}
#endif /* CONFIG_OWE */


size_t hostapd_eid_owe_trans_len(struct hostapd_data *hapd)
{
#ifdef CONFIG_OWE
	if (!hostapd_eid_owe_trans_enabled(hapd))
		return 0;
	return 6 + ETH_ALEN + 1 + hapd->conf->owe_transition_ssid_len;
#else /* CONFIG_OWE */
	return 0;
#endif /* CONFIG_OWE */
}


u8 * hostapd_eid_owe_trans(struct hostapd_data *hapd, u8 *eid,
				  size_t len)
{
#ifdef CONFIG_OWE
	u8 *pos = eid;
	size_t elen;

	if (hapd->conf->owe_transition_ifname[0] &&
	    !hostapd_eid_owe_trans_enabled(hapd))
		hostapd_owe_trans_get_info(hapd);

	if (!hostapd_eid_owe_trans_enabled(hapd))
		return pos;

	elen = hostapd_eid_owe_trans_len(hapd);
	if (len < elen) {
		wpa_printf(MSG_DEBUG,
			   "OWE: Not enough room in the buffer for OWE IE");
		return pos;
	}

	*pos++ = WLAN_EID_VENDOR_SPECIFIC;
	*pos++ = elen - 2;
	WPA_PUT_BE24(pos, OUI_WFA);
	pos += 3;
	*pos++ = OWE_OUI_TYPE;
	os_memcpy(pos, hapd->conf->owe_transition_bssid, ETH_ALEN);
	pos += ETH_ALEN;
	*pos++ = hapd->conf->owe_transition_ssid_len;
	os_memcpy(pos, hapd->conf->owe_transition_ssid,
		  hapd->conf->owe_transition_ssid_len);
	pos += hapd->conf->owe_transition_ssid_len;

	return pos;
#else /* CONFIG_OWE */
	return eid;
#endif /* CONFIG_OWE */
}


void ap_copy_sta_supp_op_classes(struct sta_info *sta,
				 const u8 *supp_op_classes,
				 size_t supp_op_classes_len)
{
	if (!supp_op_classes)
		return;
	os_free(sta->supp_op_classes);
	sta->supp_op_classes = os_malloc(1 + supp_op_classes_len);
	if (!sta->supp_op_classes)
		return;

	sta->supp_op_classes[0] = supp_op_classes_len;
	os_memcpy(sta->supp_op_classes + 1, supp_op_classes,
		  supp_op_classes_len);
}


u8 * hostapd_eid_fils_indic(struct hostapd_data *hapd, u8 *eid, int hessid)
{
	u8 *pos = eid;
#ifdef CONFIG_FILS
	u8 *len;
	u16 fils_info = 0;
	size_t realms;
	struct fils_realm *realm;

	if (!(hapd->conf->wpa & WPA_PROTO_RSN) ||
	    !wpa_key_mgmt_fils(hapd->conf->wpa_key_mgmt))
		return pos;

	realms = dl_list_len(&hapd->conf->fils_realms);
	if (realms > 7)
		realms = 7; /* 3 bit count field limits this to max 7 */

	*pos++ = WLAN_EID_FILS_INDICATION;
	len = pos++;
	/* TODO: B0..B2: Number of Public Key Identifiers */
	if (hapd->conf->erp_domain) {
		/* B3..B5: Number of Realm Identifiers */
		fils_info |= realms << 3;
	}
	/* TODO: B6: FILS IP Address Configuration */
	if (hapd->conf->fils_cache_id_set)
		fils_info |= BIT(7);
	if (hessid && !is_zero_ether_addr(hapd->conf->hessid))
		fils_info |= BIT(8); /* HESSID Included */
	/* FILS Shared Key Authentication without PFS Supported */
	fils_info |= BIT(9);
	if (hapd->conf->fils_dh_group) {
		/* FILS Shared Key Authentication with PFS Supported */
		fils_info |= BIT(10);
	}
	/* TODO: B11: FILS Public Key Authentication Supported */
	/* B12..B15: Reserved */
	WPA_PUT_LE16(pos, fils_info);
	pos += 2;
	if (hapd->conf->fils_cache_id_set) {
		os_memcpy(pos, hapd->conf->fils_cache_id, FILS_CACHE_ID_LEN);
		pos += FILS_CACHE_ID_LEN;
	}
	if (hessid && !is_zero_ether_addr(hapd->conf->hessid)) {
		os_memcpy(pos, hapd->conf->hessid, ETH_ALEN);
		pos += ETH_ALEN;
	}

	dl_list_for_each(realm, &hapd->conf->fils_realms, struct fils_realm,
			 list) {
		if (realms == 0)
			break;
		realms--;
		os_memcpy(pos, realm->hash, 2);
		pos += 2;
	}
	*len = pos - len - 1;
#endif /* CONFIG_FILS */

	return pos;
}
