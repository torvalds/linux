/*
 * hostapd - MBO
 * Copyright (c) 2016, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "common/ieee802_11_defs.h"
#include "common/ieee802_11_common.h"
#include "hostapd.h"
#include "sta_info.h"
#include "mbo_ap.h"


void mbo_ap_sta_free(struct sta_info *sta)
{
	struct mbo_non_pref_chan_info *info, *prev;

	info = sta->non_pref_chan;
	sta->non_pref_chan = NULL;
	while (info) {
		prev = info;
		info = info->next;
		os_free(prev);
	}
}


static void mbo_ap_parse_non_pref_chan(struct sta_info *sta,
				       const u8 *buf, size_t len)
{
	struct mbo_non_pref_chan_info *info, *tmp;
	char channels[200], *pos, *end;
	size_t num_chan, i;
	int ret;

	if (len <= 3)
		return; /* Not enough room for any channels */

	num_chan = len - 3;
	info = os_zalloc(sizeof(*info) + num_chan);
	if (!info)
		return;
	info->op_class = buf[0];
	info->pref = buf[len - 2];
	info->reason_code = buf[len - 1];
	info->num_channels = num_chan;
	buf++;
	os_memcpy(info->channels, buf, num_chan);
	if (!sta->non_pref_chan) {
		sta->non_pref_chan = info;
	} else {
		tmp = sta->non_pref_chan;
		while (tmp->next)
			tmp = tmp->next;
		tmp->next = info;
	}

	pos = channels;
	end = pos + sizeof(channels);
	*pos = '\0';
	for (i = 0; i < num_chan; i++) {
		ret = os_snprintf(pos, end - pos, "%s%u",
				  i == 0 ? "" : " ", buf[i]);
		if (os_snprintf_error(end - pos, ret)) {
			*pos = '\0';
			break;
		}
		pos += ret;
	}

	wpa_printf(MSG_DEBUG, "MBO: STA " MACSTR
		   " non-preferred channel list (op class %u, pref %u, reason code %u, channels %s)",
		   MAC2STR(sta->addr), info->op_class, info->pref,
		   info->reason_code, channels);
}


void mbo_ap_check_sta_assoc(struct hostapd_data *hapd, struct sta_info *sta,
			    struct ieee802_11_elems *elems)
{
	const u8 *pos, *attr, *end;
	size_t len;

	if (!hapd->conf->mbo_enabled || !elems->mbo)
		return;

	pos = elems->mbo + 4;
	len = elems->mbo_len - 4;
	wpa_hexdump(MSG_DEBUG, "MBO: Association Request attributes", pos, len);

	attr = get_ie(pos, len, MBO_ATTR_ID_CELL_DATA_CAPA);
	if (attr && attr[1] >= 1)
		sta->cell_capa = attr[2];

	mbo_ap_sta_free(sta);
	end = pos + len;
	while (end - pos > 1) {
		u8 ie_len = pos[1];

		if (2 + ie_len > end - pos)
			break;

		if (pos[0] == MBO_ATTR_ID_NON_PREF_CHAN_REPORT)
			mbo_ap_parse_non_pref_chan(sta, pos + 2, ie_len);
		pos += 2 + pos[1];
	}
}


int mbo_ap_get_info(struct sta_info *sta, char *buf, size_t buflen)
{
	char *pos = buf, *end = buf + buflen;
	int ret;
	struct mbo_non_pref_chan_info *info;
	u8 i;
	unsigned int count = 0;

	if (!sta->cell_capa)
		return 0;

	ret = os_snprintf(pos, end - pos, "mbo_cell_capa=%u\n", sta->cell_capa);
	if (os_snprintf_error(end - pos, ret))
		return pos - buf;
	pos += ret;

	for (info = sta->non_pref_chan; info; info = info->next) {
		char *pos2 = pos;

		ret = os_snprintf(pos2, end - pos2,
				  "non_pref_chan[%u]=%u:%u:%u:",
				  count, info->op_class, info->pref,
				  info->reason_code);
		count++;
		if (os_snprintf_error(end - pos2, ret))
			break;
		pos2 += ret;

		for (i = 0; i < info->num_channels; i++) {
			ret = os_snprintf(pos2, end - pos2, "%u%s",
					  info->channels[i],
					  i + 1 < info->num_channels ?
					  "," : "");
			if (os_snprintf_error(end - pos2, ret)) {
				pos2 = NULL;
				break;
			}
			pos2 += ret;
		}

		if (!pos2)
			break;
		ret = os_snprintf(pos2, end - pos2, "\n");
		if (os_snprintf_error(end - pos2, ret))
			break;
		pos2 += ret;
		pos = pos2;
	}

	return pos - buf;
}


static void mbo_ap_wnm_notif_req_cell_capa(struct sta_info *sta,
					   const u8 *buf, size_t len)
{
	if (len < 1)
		return;
	wpa_printf(MSG_DEBUG, "MBO: STA " MACSTR
		   " updated cellular data capability: %u",
		   MAC2STR(sta->addr), buf[0]);
	sta->cell_capa = buf[0];
}


static void mbo_ap_wnm_notif_req_elem(struct sta_info *sta, u8 type,
				      const u8 *buf, size_t len,
				      int *first_non_pref_chan)
{
	switch (type) {
	case WFA_WNM_NOTIF_SUBELEM_NON_PREF_CHAN_REPORT:
		if (*first_non_pref_chan) {
			/*
			 * Need to free the previously stored entries now to
			 * allow the update to replace all entries.
			 */
			*first_non_pref_chan = 0;
			mbo_ap_sta_free(sta);
		}
		mbo_ap_parse_non_pref_chan(sta, buf, len);
		break;
	case WFA_WNM_NOTIF_SUBELEM_CELL_DATA_CAPA:
		mbo_ap_wnm_notif_req_cell_capa(sta, buf, len);
		break;
	default:
		wpa_printf(MSG_DEBUG,
			   "MBO: Ignore unknown WNM Notification WFA subelement %u",
			   type);
		break;
	}
}


void mbo_ap_wnm_notification_req(struct hostapd_data *hapd, const u8 *addr,
				 const u8 *buf, size_t len)
{
	const u8 *pos, *end;
	u8 ie_len;
	struct sta_info *sta;
	int first_non_pref_chan = 1;

	if (!hapd->conf->mbo_enabled)
		return;

	sta = ap_get_sta(hapd, addr);
	if (!sta)
		return;

	pos = buf;
	end = buf + len;

	while (end - pos > 1) {
		ie_len = pos[1];

		if (2 + ie_len > end - pos)
			break;

		if (pos[0] == WLAN_EID_VENDOR_SPECIFIC &&
		    ie_len >= 4 && WPA_GET_BE24(pos + 2) == OUI_WFA)
			mbo_ap_wnm_notif_req_elem(sta, pos[5],
						  pos + 6, ie_len - 4,
						  &first_non_pref_chan);
		else
			wpa_printf(MSG_DEBUG,
				   "MBO: Ignore unknown WNM Notification element %u (len=%u)",
				   pos[0], pos[1]);

		pos += 2 + pos[1];
	}
}
