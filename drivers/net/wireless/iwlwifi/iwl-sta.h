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

#include <net/mac80211.h>

#include "iwl-eeprom.h"
#include "iwl-core.h"
#include "iwl-4965.h"
#include "iwl-io.h"
#include "iwl-helpers.h"

int iwl_get_free_ucode_key_index(struct iwl_priv *priv);
int iwl_send_static_wepkey_cmd(struct iwl_priv *priv, u8 send_if_empty);
int iwl_remove_default_wep_key(struct iwl_priv *priv,
				struct ieee80211_key_conf *key);
int iwl_set_default_wep_key(struct iwl_priv *priv,
				struct ieee80211_key_conf *key);
int iwl_remove_dynamic_key(struct iwl_priv *priv, u8 sta_id);
int iwl_set_dynamic_key(struct iwl_priv *priv,
				struct ieee80211_key_conf *key, u8 sta_id);
#endif /* __iwl_sta_h__ */
