/*
 * hostapd / TKIP countermeasures
 * Copyright (c) 2002-2009, Jouni Malinen <j@w1.fi>
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
#include "common/ieee802_11_defs.h"
#include "hostapd.h"
#include "sta_info.h"
#include "ap_mlme.h"
#include "wpa_auth.h"
#include "ap_drv_ops.h"
#include "tkip_countermeasures.h"


static void ieee80211_tkip_countermeasures_stop(void *eloop_ctx,
						void *timeout_ctx)
{
	struct hostapd_data *hapd = eloop_ctx;
	hapd->tkip_countermeasures = 0;
	hostapd_drv_set_countermeasures(hapd, 0);
	hostapd_logger(hapd, NULL, HOSTAPD_MODULE_IEEE80211,
		       HOSTAPD_LEVEL_INFO, "TKIP countermeasures ended");
}


static void ieee80211_tkip_countermeasures_start(struct hostapd_data *hapd)
{
	struct sta_info *sta;

	hostapd_logger(hapd, NULL, HOSTAPD_MODULE_IEEE80211,
		       HOSTAPD_LEVEL_INFO, "TKIP countermeasures initiated");

	wpa_auth_countermeasures_start(hapd->wpa_auth);
	hapd->tkip_countermeasures = 1;
	hostapd_drv_set_countermeasures(hapd, 1);
	wpa_gtk_rekey(hapd->wpa_auth);
	eloop_cancel_timeout(ieee80211_tkip_countermeasures_stop, hapd, NULL);
	eloop_register_timeout(60, 0, ieee80211_tkip_countermeasures_stop,
			       hapd, NULL);
	for (sta = hapd->sta_list; sta != NULL; sta = sta->next) {
		hostapd_drv_sta_deauth(hapd, sta->addr,
				       WLAN_REASON_MICHAEL_MIC_FAILURE);
		ap_sta_set_authorized(hapd, sta, 0);
		sta->flags &= ~(WLAN_STA_AUTH | WLAN_STA_ASSOC);
		hostapd_drv_sta_remove(hapd, sta->addr);
	}
}


void michael_mic_failure(struct hostapd_data *hapd, const u8 *addr, int local)
{
	time_t now;

	if (addr && local) {
		struct sta_info *sta = ap_get_sta(hapd, addr);
		if (sta != NULL) {
			wpa_auth_sta_local_mic_failure_report(sta->wpa_sm);
			hostapd_logger(hapd, addr, HOSTAPD_MODULE_IEEE80211,
				       HOSTAPD_LEVEL_INFO,
				       "Michael MIC failure detected in "
				       "received frame");
			mlme_michaelmicfailure_indication(hapd, addr);
		} else {
			wpa_printf(MSG_DEBUG,
				   "MLME-MICHAELMICFAILURE.indication "
				   "for not associated STA (" MACSTR
				   ") ignored", MAC2STR(addr));
			return;
		}
	}

	time(&now);
	if (now > hapd->michael_mic_failure + 60) {
		hapd->michael_mic_failures = 1;
	} else {
		hapd->michael_mic_failures++;
		if (hapd->michael_mic_failures > 1)
			ieee80211_tkip_countermeasures_start(hapd);
	}
	hapd->michael_mic_failure = now;
}
