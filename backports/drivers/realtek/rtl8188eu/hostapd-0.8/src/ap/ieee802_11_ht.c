/*
 * hostapd / IEEE 802.11n HT
 * Copyright (c) 2002-2009, Jouni Malinen <j@w1.fi>
 * Copyright (c) 2007-2008, Intel Corporation
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
#include "common/ieee802_11_defs.h"
#include "drivers/driver.h"
#include "hostapd.h"
#include "ap_config.h"
#include "sta_info.h"
#include "beacon.h"
#include "ieee802_11.h"


u8 * hostapd_eid_ht_capabilities(struct hostapd_data *hapd, u8 *eid)
{
	struct ieee80211_ht_capabilities *cap;
	u8 *pos = eid;

	if (!hapd->iconf->ieee80211n || !hapd->iface->current_mode ||
	    hapd->conf->disable_11n)
		return eid;

	*pos++ = WLAN_EID_HT_CAP;
	*pos++ = sizeof(*cap);

	cap = (struct ieee80211_ht_capabilities *) pos;
	os_memset(cap, 0, sizeof(*cap));
	cap->ht_capabilities_info = host_to_le16(hapd->iconf->ht_capab);
	cap->a_mpdu_params = hapd->iface->current_mode->a_mpdu_params;
	os_memcpy(cap->supported_mcs_set, hapd->iface->current_mode->mcs_set,
		  16);

	/* TODO: ht_extended_capabilities (now fully disabled) */
	/* TODO: tx_bf_capability_info (now fully disabled) */
	/* TODO: asel_capabilities (now fully disabled) */

 	pos += sizeof(*cap);

	return pos;
}


u8 * hostapd_eid_ht_operation(struct hostapd_data *hapd, u8 *eid)
{
	struct ieee80211_ht_operation *oper;
	u8 *pos = eid;

	if (!hapd->iconf->ieee80211n || hapd->conf->disable_11n)
		return eid;

	*pos++ = WLAN_EID_HT_OPERATION;
	*pos++ = sizeof(*oper);

	oper = (struct ieee80211_ht_operation *) pos;
	os_memset(oper, 0, sizeof(*oper));

	oper->control_chan = hapd->iconf->channel;
	oper->operation_mode = host_to_le16(hapd->iface->ht_op_mode);
	if (hapd->iconf->secondary_channel == 1)
		oper->ht_param |= HT_INFO_HT_PARAM_SECONDARY_CHNL_ABOVE |
			HT_INFO_HT_PARAM_REC_TRANS_CHNL_WIDTH;
	if (hapd->iconf->secondary_channel == -1)
		oper->ht_param |= HT_INFO_HT_PARAM_SECONDARY_CHNL_BELOW |
			HT_INFO_HT_PARAM_REC_TRANS_CHNL_WIDTH;

	pos += sizeof(*oper);

	return pos;
}


/*
op_mode
Set to 0 (HT pure) under the followign conditions
	- all STAs in the BSS are 20/40 MHz HT in 20/40 MHz BSS or
	- all STAs in the BSS are 20 MHz HT in 20 MHz BSS
Set to 1 (HT non-member protection) if there may be non-HT STAs
	in both the primary and the secondary channel
Set to 2 if only HT STAs are associated in BSS,
	however and at least one 20 MHz HT STA is associated
Set to 3 (HT mixed mode) when one or more non-HT STAs are associated
*/
int hostapd_ht_operation_update(struct hostapd_iface *iface)
{
	u16 cur_op_mode, new_op_mode;
	int op_mode_changes = 0;

	if (!iface->conf->ieee80211n || iface->conf->ht_op_mode_fixed)
		return 0;

	wpa_printf(MSG_DEBUG, "%s current operation mode=0x%X",
		   __func__, iface->ht_op_mode);

	if (!(iface->ht_op_mode & HT_INFO_OPERATION_MODE_NON_GF_DEVS_PRESENT)
	    && iface->num_sta_ht_no_gf) {
		iface->ht_op_mode |=
			HT_INFO_OPERATION_MODE_NON_GF_DEVS_PRESENT;
		op_mode_changes++;
	} else if ((iface->ht_op_mode &
		    HT_INFO_OPERATION_MODE_NON_GF_DEVS_PRESENT) &&
		   iface->num_sta_ht_no_gf == 0) {
		iface->ht_op_mode &=
			~HT_INFO_OPERATION_MODE_NON_GF_DEVS_PRESENT;
		op_mode_changes++;
	}

	if (!(iface->ht_op_mode & HT_INFO_OPERATION_MODE_NON_HT_STA_PRESENT) &&
	    (iface->num_sta_no_ht || iface->olbc_ht)) {
		iface->ht_op_mode |= HT_INFO_OPERATION_MODE_NON_HT_STA_PRESENT;
		op_mode_changes++;
	} else if ((iface->ht_op_mode &
		    HT_INFO_OPERATION_MODE_NON_HT_STA_PRESENT) &&
		   (iface->num_sta_no_ht == 0 && !iface->olbc_ht)) {
		iface->ht_op_mode &=
			~HT_INFO_OPERATION_MODE_NON_HT_STA_PRESENT;
		op_mode_changes++;
	}

	new_op_mode = 0;
	if (iface->num_sta_no_ht)
		new_op_mode = OP_MODE_MIXED;
	else if ((iface->conf->ht_capab & HT_CAP_INFO_SUPP_CHANNEL_WIDTH_SET)
		 && iface->num_sta_ht_20mhz)
		new_op_mode = OP_MODE_20MHZ_HT_STA_ASSOCED;
	else if (iface->olbc_ht)
		new_op_mode = OP_MODE_MAY_BE_LEGACY_STAS;
	else
		new_op_mode = OP_MODE_PURE;

	cur_op_mode = iface->ht_op_mode & HT_INFO_OPERATION_MODE_OP_MODE_MASK;
	if (cur_op_mode != new_op_mode) {
		iface->ht_op_mode &= ~HT_INFO_OPERATION_MODE_OP_MODE_MASK;
		iface->ht_op_mode |= new_op_mode;
		op_mode_changes++;
	}

	wpa_printf(MSG_DEBUG, "%s new operation mode=0x%X changes=%d",
		   __func__, iface->ht_op_mode, op_mode_changes);

	return op_mode_changes;
}


u16 copy_sta_ht_capab(struct hostapd_data *hapd, struct sta_info *sta,
		      const u8 *ht_capab, size_t ht_capab_len)
{
	/* Disable HT caps for STAs associated to no-HT BSSes. */
	if (!ht_capab ||
	    ht_capab_len < sizeof(struct ieee80211_ht_capabilities) ||
	    hapd->conf->disable_11n) {
		sta->flags &= ~WLAN_STA_HT;
		os_free(sta->ht_capabilities);
		sta->ht_capabilities = NULL;
		return WLAN_STATUS_SUCCESS;
	}

	if (sta->ht_capabilities == NULL) {
		sta->ht_capabilities =
			os_zalloc(sizeof(struct ieee80211_ht_capabilities));
		if (sta->ht_capabilities == NULL)
			return WLAN_STATUS_UNSPECIFIED_FAILURE;
	}

	sta->flags |= WLAN_STA_HT;
	os_memcpy(sta->ht_capabilities, ht_capab,
		  sizeof(struct ieee80211_ht_capabilities));

	return WLAN_STATUS_SUCCESS;
}


static void update_sta_ht(struct hostapd_data *hapd, struct sta_info *sta)
{
	u16 ht_capab;

	ht_capab = le_to_host16(sta->ht_capabilities->ht_capabilities_info);
	wpa_printf(MSG_DEBUG, "HT: STA " MACSTR " HT Capabilities Info: "
		   "0x%04x", MAC2STR(sta->addr), ht_capab);
	if ((ht_capab & HT_CAP_INFO_GREEN_FIELD) == 0) {
		if (!sta->no_ht_gf_set) {
			sta->no_ht_gf_set = 1;
			hapd->iface->num_sta_ht_no_gf++;
		}
		wpa_printf(MSG_DEBUG, "%s STA " MACSTR " - no greenfield, num "
			   "of non-gf stations %d",
			   __func__, MAC2STR(sta->addr),
			   hapd->iface->num_sta_ht_no_gf);
	}
	if ((ht_capab & HT_CAP_INFO_SUPP_CHANNEL_WIDTH_SET) == 0) {
		if (!sta->ht_20mhz_set) {
			sta->ht_20mhz_set = 1;
			hapd->iface->num_sta_ht_20mhz++;
		}
		wpa_printf(MSG_DEBUG, "%s STA " MACSTR " - 20 MHz HT, num of "
			   "20MHz HT STAs %d",
			   __func__, MAC2STR(sta->addr),
			   hapd->iface->num_sta_ht_20mhz);
	}
}


static void update_sta_no_ht(struct hostapd_data *hapd, struct sta_info *sta)
{
	if (!sta->no_ht_set) {
		sta->no_ht_set = 1;
		hapd->iface->num_sta_no_ht++;
	}
	if (hapd->iconf->ieee80211n) {
		wpa_printf(MSG_DEBUG, "%s STA " MACSTR " - no HT, num of "
			   "non-HT stations %d",
			   __func__, MAC2STR(sta->addr),
			   hapd->iface->num_sta_no_ht);
	}
}


void update_ht_state(struct hostapd_data *hapd, struct sta_info *sta)
{
	if ((sta->flags & WLAN_STA_HT) && sta->ht_capabilities)
		update_sta_ht(hapd, sta);
	else
		update_sta_no_ht(hapd, sta);

	if (hostapd_ht_operation_update(hapd->iface) > 0)
		ieee802_11_set_beacons(hapd->iface);
}


void hostapd_get_ht_capab(struct hostapd_data *hapd,
			  struct ieee80211_ht_capabilities *ht_cap,
			  struct ieee80211_ht_capabilities *neg_ht_cap)
{
	u16 cap;

	if (ht_cap == NULL)
		return;
	os_memcpy(neg_ht_cap, ht_cap, sizeof(*neg_ht_cap));
	cap = le_to_host16(neg_ht_cap->ht_capabilities_info);
	cap &= hapd->iconf->ht_capab;
	cap |= (hapd->iconf->ht_capab & HT_CAP_INFO_SMPS_DISABLED);

	/*
	 * STBC needs to be handled specially
	 * if we don't support RX STBC, mask out TX STBC in the STA's HT caps
	 * if we don't support TX STBC, mask out RX STBC in the STA's HT caps
	 */
	if (!(hapd->iconf->ht_capab & HT_CAP_INFO_RX_STBC_MASK))
		cap &= ~HT_CAP_INFO_TX_STBC;
	if (!(hapd->iconf->ht_capab & HT_CAP_INFO_TX_STBC))
		cap &= ~HT_CAP_INFO_RX_STBC_MASK;

	neg_ht_cap->ht_capabilities_info = host_to_le16(cap);
}
