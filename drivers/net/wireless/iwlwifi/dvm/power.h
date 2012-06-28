/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Intel Corporation. All rights reserved.
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

#include "commands.h"

struct iwl_power_mgr {
	struct iwl_powertable_cmd sleep_cmd;
	struct iwl_powertable_cmd sleep_cmd_next;
	int debug_sleep_level_override;
	bool bus_pm;
};

int iwl_power_set_mode(struct iwl_priv *priv, struct iwl_powertable_cmd *cmd,
		       bool force);
int iwl_power_update_mode(struct iwl_priv *priv, bool force);
void iwl_power_initialize(struct iwl_priv *priv);

extern bool no_sleep_autoadjust;

#endif  /* __iwl_power_setting_h__ */
