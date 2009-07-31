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

#define IWL_ABSOLUTE_ZERO		0
#define IWL_ABSOLUTE_MAX		0xFFFFFFFF
#define IWL_TT_INCREASE_MARGIN	5

/* Tx/Rx restrictions */
#define IWL_TX_MULTI		0x02
#define IWL_TX_SINGLE		0x01
#define IWL_TX_NONE		0x00
#define IWL_RX_MULTI		0x02
#define IWL_RX_SINGLE		0x01
#define IWL_RX_NONE		0x00

/* Thermal Throttling State Machine states */
enum  iwl_tt_state {
	IWL_TI_0,	/* normal temperature, system power state */
	IWL_TI_1,	/* high temperature detect, low power state */
	IWL_TI_2,	/* higher temperature detected, lower power state */
	IWL_TI_CT_KILL, /* critical temperature detected, lowest power state */
	IWL_TI_STATE_MAX
};

/**
 * struct iwl_tt_restriction - Thermal Throttling restriction table used
 *		by advance thermal throttling management
 *		based on the current thermal throttling state, determine
 *		number of tx/rx streams; and the status of HT operation
 * @tx_stream: number of tx stream allowed
 * @is_ht: ht enable/disable
 * @rx_stream: number of rx stream allowed
 */
struct iwl_tt_restriction {
	u8 tx_stream;
	bool is_ht;
	u8 rx_stream;
};

/**
 * struct iwl_tt_trans - Thermal Throttling transaction table; used by
 * 		advance thermal throttling algorithm to determine next
 *		thermal state to go based on the current temperature
 * @next_state:  next thermal throttling mode
 * @tt_low: low temperature threshold to change state
 * @tt_high: high temperature threshold to change state
 */
struct iwl_tt_trans {
	enum iwl_tt_state next_state;
	u32 tt_low;
	u32 tt_high;
};

/**
 * struct iwl_tt_mgnt - Thermal Throttling Management structure
 * @state:          current Thermal Throttling state
 * @tt_power_mode:  Thermal Throttling power mode index
 *		    being used to set power level when
 * 		    when thermal throttling state != IWL_TI_0
 *		    the tt_power_mode should set to different
 *		    power mode based on the current tt state
 * @sys_power_mode: previous system power mode
 *                  before transition into TT state
 * @tt_previous_temperature: last measured temperature
 * @iwl_tt_restriction: ptr to restriction tbl, used by advance
 *		    thermal throttling to determine how many tx/rx streams
 *		    should be used in tt state; and can HT be enabled or not
 * @iwl_tt_trans: ptr to adv trans table, used by advance thermal throttling
 *		    state transaction
 */
struct iwl_tt_mgmt {
	enum iwl_tt_state state;
	u8 tt_power_mode;
	u8 sys_power_mode;
#ifdef CONFIG_IWLWIFI_DEBUG
	s32 tt_previous_temp;
#endif
	struct iwl_tt_restriction *restriction;
	struct iwl_tt_trans *transaction;
};

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
	struct iwl_tt_mgmt tt; /* Thermal Throttling Management */
	bool adv_tt;		/* false: legacy mode */
				/* true: advance mode */
	bool ct_kill_toggle;   /* use to toggle the CSR bit when
				* checking uCode temperature
				*/
	struct timer_list ct_kill_exit_tm;
};

int iwl_power_update_mode(struct iwl_priv *priv, bool force);
int iwl_power_set_user_mode(struct iwl_priv *priv, u16 mode);
bool iwl_ht_enabled(struct iwl_priv *priv);
u8 iwl_tx_ant_restriction(struct iwl_priv *priv);
u8 iwl_rx_ant_restriction(struct iwl_priv *priv);
void iwl_tt_enter_ct_kill(struct iwl_priv *priv);
void iwl_tt_exit_ct_kill(struct iwl_priv *priv);
void iwl_tt_handler(struct iwl_priv *priv);
void iwl_tt_initialize(struct iwl_priv *priv);
void iwl_tt_exit(struct iwl_priv *priv);
void iwl_power_initialize(struct iwl_priv *priv);

#endif  /* __iwl_power_setting_h__ */
