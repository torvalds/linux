/******************************************************************************
 *
 * Copyright(c) 2003 - 2008 Intel Corporation. All rights reserved.
 *
 * Portions of this file are derived from the ipw3945 project, as well
 * as portions of the ieee80211 subsystem header files.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * James P. Ketrenos <ipw2100-admin@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/
#ifndef __iwl_sta_h__
#define __iwl_sta_h__

#define HW_KEY_DYNAMIC 0
#define HW_KEY_DEFAULT 1

/**
 * iwl_find_station - Find station id for a given BSSID
 * @bssid: MAC address of station ID to find
 */
u8 iwl_find_station(struct iwl_priv *priv, const u8 *bssid);

int iwl_send_static_wepkey_cmd(struct iwl_priv *priv, u8 send_if_empty);
int iwl_remove_default_wep_key(struct iwl_priv *priv,
			       struct ieee80211_key_conf *key);
int iwl_set_default_wep_key(struct iwl_priv *priv,
			    struct ieee80211_key_conf *key);
int iwl_set_dynamic_key(struct iwl_priv *priv,
			struct ieee80211_key_conf *key, u8 sta_id);
int iwl_remove_dynamic_key(struct iwl_priv *priv,
			   struct ieee80211_key_conf *key, u8 sta_id);
int iwl_rxon_add_station(struct iwl_priv *priv, const u8 *addr, int is_ap);
int iwl_remove_station(struct iwl_priv *priv, const u8 *addr, int is_ap);
int iwl_get_sta_id(struct iwl_priv *priv, struct ieee80211_hdr *hdr);
void iwl_sta_modify_enable_tid_tx(struct iwl_priv *priv, int sta_id, int tid);
int iwl_get_ra_sta_id(struct iwl_priv *priv, struct ieee80211_hdr *hdr);
#endif /* __iwl_sta_h__ */
