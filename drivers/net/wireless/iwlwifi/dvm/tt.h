/******************************************************************************
 *
 * Copyright(c) 2007 - 2014 Intel Corporation. All rights reserved.
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
#ifndef __iwl_tt_setting_h__
#define __iwl_tt_setting_h__

#include "commands.h"

#define IWL_ABSOLUTE_ZERO		0
#define IWL_ABSOLUTE_MAX		0xFFFFFFFF
#define IWL_TT_INCREASE_MARGIN	5
#define IWL_TT_CT_KILL_MARGIN	3

enum iwl_antenna_ok {
	IWL_ANT_OK_NONE,
	IWL_ANT_OK_SINGLE,
	IWL_ANT_OK_MULTI,
};

/* Thermal Throttling State Machine states */
enum  iwl_tt_state {
	IWL_TI_0,	/* normal temperature, system power state */
	IWL_TI_1,	/* high temperature detect, low power state */
	IWL_TI_2,	/* higher temperature detected, lower power state */
	IWL_TI_CT_KILL, /* critical temperature detected, lowest power state */
	IWL_TI_STATE_MAX
};

/**
 * struct iwl_tt_restriction - Thermal Throttling restriction table
 * @tx_stream: number of tx stream allowed
 * @is_ht: ht enable/disable
 * @rx_stream: number of rx stream allowed
 *
 * This table is used by advance thermal throttling management
 * based on the current thermal throttling state, and determines
 * the number of tx/rx streams and the status of HT operation.
 */
struct iwl_tt_restriction {
	enum iwl_antenna_ok tx_stream;
	enum iwl_antenna_ok rx_stream;
	bool is_ht;
};

/**
 * struct iwl_tt_trans - Thermal Throttling transaction table
 * @next_state:  next thermal throttling mode
 * @tt_low: low temperature threshold to change state
 * @tt_high: high temperature threshold to change state
 *
 * This is used by the advanced thermal throttling algorithm
 * to determine the next thermal state to go based on the
 * current temperature.
 */
struct iwl_tt_trans {
	enum iwl_tt_state next_state;
	u32 tt_low;
	u32 tt_high;
};

/**
 * struct iwl_tt_mgnt - Thermal Throttling Management structure
 * @advanced_tt:    advanced thermal throttle required
 * @state:          current Thermal Throttling state
 * @tt_power_mode:  Thermal Throttling power mode index
 *		    being used to set power level when
 *		    when thermal throttling state != IWL_TI_0
 *		    the tt_power_mode should set to different
 *		    power mode based on the current tt state
 * @tt_previous_temperature: last measured temperature
 * @iwl_tt_restriction: ptr to restriction tbl, used by advance
 *		    thermal throttling to determine how many tx/rx streams
 *		    should be used in tt state; and can HT be enabled or not
 * @iwl_tt_trans: ptr to adv trans table, used by advance thermal throttling
 *		    state transaction
 * @ct_kill_toggle: used to toggle the CSR bit when checking uCode temperature
 * @ct_kill_exit_tm: timer to exit thermal kill
 */
struct iwl_tt_mgmt {
	enum iwl_tt_state state;
	bool advanced_tt;
	u8 tt_power_mode;
	bool ct_kill_toggle;
#ifdef CONFIG_IWLWIFI_DEBUG
	s32 tt_previous_temp;
#endif
	struct iwl_tt_restriction *restriction;
	struct iwl_tt_trans *transaction;
	struct timer_list ct_kill_exit_tm;
	struct timer_list ct_kill_waiting_tm;
};

u8 iwl_tt_current_power_mode(struct iwl_priv *priv);
bool iwl_tt_is_low_power_state(struct iwl_priv *priv);
bool iwl_ht_enabled(struct iwl_priv *priv);
enum iwl_antenna_ok iwl_tx_ant_restriction(struct iwl_priv *priv);
enum iwl_antenna_ok iwl_rx_ant_restriction(struct iwl_priv *priv);
void iwl_tt_enter_ct_kill(struct iwl_priv *priv);
void iwl_tt_exit_ct_kill(struct iwl_priv *priv);
void iwl_tt_handler(struct iwl_priv *priv);
void iwl_tt_initialize(struct iwl_priv *priv);
void iwl_tt_exit(struct iwl_priv *priv);

#endif  /* __iwl_tt_setting_h__ */
