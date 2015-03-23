/*
 * hostapd / State dump
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
#include "radius/radius_client.h"
#include "radius/radius_server.h"
#include "eapol_auth/eapol_auth_sm.h"
#include "eapol_auth/eapol_auth_sm_i.h"
#include "eap_server/eap.h"
#include "ap/hostapd.h"
#include "ap/ap_config.h"
#include "ap/sta_info.h"
#include "dump_state.h"


static void fprint_char(FILE *f, char c)
{
	if (c >= 32 && c < 127)
		fprintf(f, "%c", c);
	else
		fprintf(f, "<%02x>", c);
}


static void ieee802_1x_dump_state(FILE *f, const char *prefix,
				  struct sta_info *sta)
{
	struct eapol_state_machine *sm = sta->eapol_sm;
	if (sm == NULL)
		return;

	fprintf(f, "%sIEEE 802.1X:\n", prefix);

	if (sm->identity) {
		size_t i;
		fprintf(f, "%sidentity=", prefix);
		for (i = 0; i < sm->identity_len; i++)
			fprint_char(f, sm->identity[i]);
		fprintf(f, "\n");
	}

	fprintf(f, "%slast EAP type: Authentication Server: %d (%s) "
		"Supplicant: %d (%s)\n", prefix,
		sm->eap_type_authsrv,
		eap_server_get_name(0, sm->eap_type_authsrv),
		sm->eap_type_supp, eap_server_get_name(0, sm->eap_type_supp));

	fprintf(f, "%scached_packets=%s\n", prefix,
		sm->last_recv_radius ? "[RX RADIUS]" : "");

	eapol_auth_dump_state(f, prefix, sm);
}


/**
 * hostapd_dump_state - SIGUSR1 handler to dump hostapd state to a text file
 */
static void hostapd_dump_state(struct hostapd_data *hapd)
{
	FILE *f;
	time_t now;
	struct sta_info *sta;
	int i;
#ifndef CONFIG_NO_RADIUS
	char *buf;
#endif /* CONFIG_NO_RADIUS */

	if (!hapd->conf->dump_log_name) {
		wpa_printf(MSG_DEBUG, "Dump file not defined - ignoring dump "
			   "request");
		return;
	}

	wpa_printf(MSG_DEBUG, "Dumping hostapd state to '%s'",
		   hapd->conf->dump_log_name);
	f = fopen(hapd->conf->dump_log_name, "w");
	if (f == NULL) {
		wpa_printf(MSG_WARNING, "Could not open dump file '%s' for "
			   "writing.", hapd->conf->dump_log_name);
		return;
	}

	time(&now);
	fprintf(f, "hostapd state dump - %s", ctime(&now));
	fprintf(f, "num_sta=%d num_sta_non_erp=%d "
		"num_sta_no_short_slot_time=%d\n"
		"num_sta_no_short_preamble=%d\n",
		hapd->num_sta, hapd->iface->num_sta_non_erp,
		hapd->iface->num_sta_no_short_slot_time,
		hapd->iface->num_sta_no_short_preamble);

	for (sta = hapd->sta_list; sta != NULL; sta = sta->next) {
		fprintf(f, "\nSTA=" MACSTR "\n", MAC2STR(sta->addr));

		fprintf(f,
			"  AID=%d flags=0x%x %s%s%s%s%s%s%s%s%s%s%s%s%s%s%s\n"
			"  capability=0x%x listen_interval=%d\n",
			sta->aid,
			sta->flags,
			(sta->flags & WLAN_STA_AUTH ? "[AUTH]" : ""),
			(sta->flags & WLAN_STA_ASSOC ? "[ASSOC]" : ""),
			(sta->flags & WLAN_STA_PS ? "[PS]" : ""),
			(sta->flags & WLAN_STA_TIM ? "[TIM]" : ""),
			(sta->flags & WLAN_STA_PERM ? "[PERM]" : ""),
			(ap_sta_is_authorized(sta) ? "[AUTHORIZED]" : ""),
			(sta->flags & WLAN_STA_PENDING_POLL ? "[PENDING_POLL" :
			 ""),
			(sta->flags & WLAN_STA_SHORT_PREAMBLE ?
			 "[SHORT_PREAMBLE]" : ""),
			(sta->flags & WLAN_STA_PREAUTH ? "[PREAUTH]" : ""),
			(sta->flags & WLAN_STA_WMM ? "[WMM]" : ""),
			(sta->flags & WLAN_STA_MFP ? "[MFP]" : ""),
			(sta->flags & WLAN_STA_WPS ? "[WPS]" : ""),
			(sta->flags & WLAN_STA_MAYBE_WPS ? "[MAYBE_WPS]" : ""),
			(sta->flags & WLAN_STA_WDS ? "[WDS]" : ""),
			(sta->flags & WLAN_STA_NONERP ? "[NonERP]" : ""),
			sta->capability,
			sta->listen_interval);

		fprintf(f, "  supported_rates=");
		for (i = 0; i < sta->supported_rates_len; i++)
			fprintf(f, "%02x ", sta->supported_rates[i]);
		fprintf(f, "\n");

		fprintf(f,
			"  timeout_next=%s\n",
			(sta->timeout_next == STA_NULLFUNC ? "NULLFUNC POLL" :
			 (sta->timeout_next == STA_DISASSOC ? "DISASSOC" :
			  "DEAUTH")));

		ieee802_1x_dump_state(f, "  ", sta);
	}

#ifndef CONFIG_NO_RADIUS
	buf = os_malloc(4096);
	if (buf) {
		int count = radius_client_get_mib(hapd->radius, buf, 4096);
		if (count < 0)
			count = 0;
		else if (count > 4095)
			count = 4095;
		buf[count] = '\0';
		fprintf(f, "%s", buf);

#ifdef RADIUS_SERVER
		count = radius_server_get_mib(hapd->radius_srv, buf, 4096);
		if (count < 0)
			count = 0;
		else if (count > 4095)
			count = 4095;
		buf[count] = '\0';
		fprintf(f, "%s", buf);
#endif /* RADIUS_SERVER */

		os_free(buf);
	}
#endif /* CONFIG_NO_RADIUS */
	fclose(f);
}


int handle_dump_state_iface(struct hostapd_iface *iface, void *ctx)
{
	size_t i;

	for (i = 0; i < iface->num_bss; i++)
		hostapd_dump_state(iface->bss[i]);

	return 0;
}
