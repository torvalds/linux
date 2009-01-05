/******************************************************************************
 *
 * Copyright(c) 2007 - 2008 Intel Corporation. All rights reserved.
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
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *****************************************************************************/
#ifndef __iwl_rf_kill_h__
#define __iwl_rf_kill_h__

struct iwl_priv;

#include <linux/rfkill.h>

#ifdef CONFIG_IWLWIFI_RFKILL

void iwl_rfkill_set_hw_state(struct iwl_priv *priv);
void iwl_rfkill_unregister(struct iwl_priv *priv);
int iwl_rfkill_init(struct iwl_priv *priv);
#else
static inline void iwl_rfkill_set_hw_state(struct iwl_priv *priv) {}
static inline void iwl_rfkill_unregister(struct iwl_priv *priv) {}
static inline int iwl_rfkill_init(struct iwl_priv *priv) { return 0; }
#endif



#endif  /* __iwl_rf_kill_h__ */
