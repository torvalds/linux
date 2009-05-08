/******************************************************************************
 *
 * Copyright(c) 2007 - 2009 Intel Corporation. All rights reserved.
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
#ifndef __iwl_power_setting_h__
#define __iwl_power_setting_h__

#include <net/mac80211.h>
#include "iwl-commands.h"

struct iwl_priv;

enum {
	IWL_POWER_MODE_CAM, /* Continuously Aware Mode, always on */
	IWL_POWER_INDEX_1,
	IWL_POWER_INDEX_2,
	IWL_POWER_INDEX_3,
	IWL_POWER_INDEX_4,
	IWL_POWER_INDEX_5,
	IWL_POWER_NUM
};

/* Power management (not Tx power) structures */

struct iwl_power_vec_entry {
	struct iwl_powertable_cmd cmd;
	u8 no_dtim;
};

struct iwl_power_mgr {
	struct iwl_power_vec_entry pwr_range_0[IWL_POWER_NUM];
	struct iwl_power_vec_entry pwr_range_1[IWL_POWER_NUM];
	struct iwl_power_vec_entry pwr_range_2[IWL_POWER_NUM];
	u32 dtim_period;
	/* final power level that used to calculate final power command */
	u8 power_mode;
	u8 user_power_setting; /* set by user through sysfs */
	u8 power_disabled; /* set by mac80211's CONF_PS */
};

int iwl_power_update_mode(struct iwl_priv *priv, bool force);
int iwl_power_set_user_mode(struct iwl_priv *priv, u16 mode);
void iwl_power_initialize(struct iwl_priv *priv);

#endif  /* __iwl_power_setting_h__ */
