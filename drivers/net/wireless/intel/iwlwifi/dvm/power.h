/* SPDX-License-Identifier: GPL-2.0-only */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2014 Intel Corporation. All rights reserved.
 *
 * Portions of this file are derived from the ipw3945 project, as well
 * as portions of the ieee80211 subsystem header files.
 *
 * Contact Information:
 *  Intel Linux Wireless <linuxwifi@intel.com>
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
