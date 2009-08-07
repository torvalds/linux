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


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>

#include <net/mac80211.h>

#include "iwl-eeprom.h"
#include "iwl-dev.h"
#include "iwl-core.h"
#include "iwl-io.h"
#include "iwl-commands.h"
#include "iwl-debug.h"
#include "iwl-power.h"

/*
 * Setting power level allow the card to go to sleep when not busy.
 *
 * The power level is set to INDEX_1 (the least deep state) by
 * default, and will, in the future, be the deepest state unless
 * otherwise required by pm_qos network latency requirements.
 *
 * Using INDEX_1 without pm_qos is ok because mac80211 will disable
 * PS when even checking every beacon for the TIM bit would exceed
 * the required latency.
 */

#define IWL_POWER_RANGE_0_MAX  (2)
#define IWL_POWER_RANGE_1_MAX  (10)


#define NOSLP cpu_to_le16(0), 0, 0
#define SLP IWL_POWER_DRIVER_ALLOW_SLEEP_MSK, 0, 0
#define TU_TO_USEC 1024
#define SLP_TOUT(T) cpu_to_le32((T) * TU_TO_USEC)
#define SLP_VEC(X0, X1, X2, X3, X4) {cpu_to_le32(X0), \
				     cpu_to_le32(X1), \
				     cpu_to_le32(X2), \
				     cpu_to_le32(X3), \
				     cpu_to_le32(X4)}
/* default power management (not Tx power) table values */
/* for DTIM period 0 through IWL_POWER_RANGE_0_MAX */
static const struct iwl_power_vec_entry range_0[IWL_POWER_NUM] = {
	{{NOSLP, SLP_TOUT(0), SLP_TOUT(0), SLP_VEC(0, 0, 0, 0, 0)}, 0},
	{{SLP, SLP_TOUT(200), SLP_TOUT(500), SLP_VEC(1, 2, 2, 2, 0xFF)}, 0},
	{{SLP, SLP_TOUT(200), SLP_TOUT(300), SLP_VEC(1, 2, 2, 2, 0xFF)}, 0},
	{{SLP, SLP_TOUT(50), SLP_TOUT(100), SLP_VEC(2, 2, 2, 2, 0xFF)}, 0},
	{{SLP, SLP_TOUT(50), SLP_TOUT(25), SLP_VEC(2, 2, 4, 4, 0xFF)}, 1},
	{{SLP, SLP_TOUT(25), SLP_TOUT(25), SLP_VEC(2, 2, 4, 6, 0xFF)}, 2}
};


/* for DTIM period IWL_POWER_RANGE_0_MAX + 1 through IWL_POWER_RANGE_1_MAX */
static const struct iwl_power_vec_entry range_1[IWL_POWER_NUM] = {
	{{NOSLP, SLP_TOUT(0), SLP_TOUT(0), SLP_VEC(0, 0, 0, 0, 0)}, 0},
	{{SLP, SLP_TOUT(200), SLP_TOUT(500), SLP_VEC(1, 2, 3, 4, 4)}, 0},
	{{SLP, SLP_TOUT(200), SLP_TOUT(300), SLP_VEC(1, 2, 3, 4, 7)}, 0},
	{{SLP, SLP_TOUT(50), SLP_TOUT(100), SLP_VEC(2, 4, 6, 7, 9)}, 0},
	{{SLP, SLP_TOUT(50), SLP_TOUT(25), SLP_VEC(2, 4, 6, 9, 10)}, 1},
	{{SLP, SLP_TOUT(25), SLP_TOUT(25), SLP_VEC(2, 4, 7, 10, 10)}, 2}
};

/* for DTIM period > IWL_POWER_RANGE_1_MAX */
static const struct iwl_power_vec_entry range_2[IWL_POWER_NUM] = {
	{{NOSLP, SLP_TOUT(0), SLP_TOUT(0), SLP_VEC(0, 0, 0, 0, 0)}, 0},
	{{SLP, SLP_TOUT(200), SLP_TOUT(500), SLP_VEC(1, 2, 3, 4, 0xFF)}, 0},
	{{SLP, SLP_TOUT(200), SLP_TOUT(300), SLP_VEC(2, 4, 6, 7, 0xFF)}, 0},
	{{SLP, SLP_TOUT(50), SLP_TOUT(100), SLP_VEC(2, 7, 9, 9, 0xFF)}, 0},
	{{SLP, SLP_TOUT(50), SLP_TOUT(25), SLP_VEC(2, 7, 9, 9, 0xFF)}, 0},
	{{SLP, SLP_TOUT(25), SLP_TOUT(25), SLP_VEC(4, 7, 10, 10, 0xFF)}, 0}
};

/* default Thermal Throttling transaction table
 * Current state   |         Throttling Down               |  Throttling Up
 *=============================================================================
 *                 Condition Nxt State  Condition Nxt State Condition Nxt State
 *-----------------------------------------------------------------------------
 *     IWL_TI_0     T >= 115   CT_KILL  115>T>=105   TI_1      N/A      N/A
 *     IWL_TI_1     T >= 115   CT_KILL  115>T>=110   TI_2     T<=95     TI_0
 *     IWL_TI_2     T >= 115   CT_KILL                        T<=100    TI_1
 *    IWL_CT_KILL      N/A       N/A       N/A        N/A     T<=95     TI_0
 *=============================================================================
 */
static const struct iwl_tt_trans tt_range_0[IWL_TI_STATE_MAX - 1] = {
	{IWL_TI_0, IWL_ABSOLUTE_ZERO, 104},
	{IWL_TI_1, 105, CT_KILL_THRESHOLD},
	{IWL_TI_CT_KILL, CT_KILL_THRESHOLD + 1, IWL_ABSOLUTE_MAX}
};
static const struct iwl_tt_trans tt_range_1[IWL_TI_STATE_MAX - 1] = {
	{IWL_TI_0, IWL_ABSOLUTE_ZERO, 95},
	{IWL_TI_2, 110, CT_KILL_THRESHOLD},
	{IWL_TI_CT_KILL, CT_KILL_THRESHOLD + 1, IWL_ABSOLUTE_MAX}
};
static const struct iwl_tt_trans tt_range_2[IWL_TI_STATE_MAX - 1] = {
	{IWL_TI_1, IWL_ABSOLUTE_ZERO, 100},
	{IWL_TI_CT_KILL, CT_KILL_THRESHOLD + 1, IWL_ABSOLUTE_MAX},
	{IWL_TI_CT_KILL, CT_KILL_THRESHOLD + 1, IWL_ABSOLUTE_MAX}
};
static const struct iwl_tt_trans tt_range_3[IWL_TI_STATE_MAX - 1] = {
	{IWL_TI_0, IWL_ABSOLUTE_ZERO, CT_KILL_EXIT_THRESHOLD},
	{IWL_TI_CT_KILL, CT_KILL_EXIT_THRESHOLD + 1, IWL_ABSOLUTE_MAX},
	{IWL_TI_CT_KILL, CT_KILL_EXIT_THRESHOLD + 1, IWL_ABSOLUTE_MAX}
};

/* Advance Thermal Throttling default restriction table */
static const struct iwl_tt_restriction restriction_range[IWL_TI_STATE_MAX] = {
	{IWL_TX_MULTI, true, IWL_RX_MULTI},
	{IWL_TX_SINGLE, true, IWL_RX_MULTI},
	{IWL_TX_SINGLE, false, IWL_RX_SINGLE},
	{IWL_TX_NONE, false, IWL_RX_NONE}
};

/* set card power command */
static int iwl_set_power(struct iwl_priv *priv, void *cmd)
{
	return iwl_send_cmd_pdu(priv, POWER_TABLE_CMD,
				sizeof(struct iwl_powertable_cmd), cmd);
}

/* initialize to default */
static void iwl_power_init_handle(struct iwl_priv *priv)
{
	struct iwl_power_mgr *pow_data;
	int size = sizeof(struct iwl_power_vec_entry) * IWL_POWER_NUM;
	struct iwl_powertable_cmd *cmd;
	int i;
	u16 lctl;

	IWL_DEBUG_POWER(priv, "Initialize power \n");

	pow_data = &priv->power_data;

	memset(pow_data, 0, sizeof(*pow_data));

	memcpy(&pow_data->pwr_range_0[0], &range_0[0], size);
	memcpy(&pow_data->pwr_range_1[0], &range_1[0], size);
	memcpy(&pow_data->pwr_range_2[0], &range_2[0], size);

	lctl = iwl_pcie_link_ctl(priv);

	IWL_DEBUG_POWER(priv, "adjust power command flags\n");

	for (i = 0; i < IWL_POWER_NUM; i++) {
		cmd = &pow_data->pwr_range_0[i].cmd;

		if (lctl & PCI_CFG_LINK_CTRL_VAL_L0S_EN)
			cmd->flags &= ~IWL_POWER_PCI_PM_MSK;
		else
			cmd->flags |= IWL_POWER_PCI_PM_MSK;
	}
}

/* adjust power command according to DTIM period and power level*/
static int iwl_update_power_cmd(struct iwl_priv *priv,
				struct iwl_powertable_cmd *cmd, u16 mode)
{
	struct iwl_power_vec_entry *range;
	struct iwl_power_mgr *pow_data;
	int i;
	u32 max_sleep = 0;
	u8 period;
	bool skip;

	if (mode > IWL_POWER_INDEX_5) {
		IWL_DEBUG_POWER(priv, "Error invalid power mode \n");
		return -EINVAL;
	}

	pow_data = &priv->power_data;

	if (pow_data->dtim_period <= IWL_POWER_RANGE_0_MAX)
		range = &pow_data->pwr_range_0[0];
	else if (pow_data->dtim_period <= IWL_POWER_RANGE_1_MAX)
		range = &pow_data->pwr_range_1[0];
	else
		range = &pow_data->pwr_range_2[0];

	period = pow_data->dtim_period;
	memcpy(cmd, &range[mode].cmd, sizeof(struct iwl_powertable_cmd));

	if (period == 0) {
		period = 1;
		skip = false;
	} else {
		skip = !!range[mode].no_dtim;
	}

	if (skip) {
		__le32 slp_itrvl = cmd->sleep_interval[IWL_POWER_VEC_SIZE - 1];
		max_sleep = le32_to_cpu(slp_itrvl);
		if (max_sleep == 0xFF)
			max_sleep = period * (skip + 1);
		else if (max_sleep >  period)
			max_sleep = (le32_to_cpu(slp_itrvl) / period) * period;
		cmd->flags |= IWL_POWER_SLEEP_OVER_DTIM_MSK;
	} else {
		max_sleep = period;
		cmd->flags &= ~IWL_POWER_SLEEP_OVER_DTIM_MSK;
	}

	for (i = 0; i < IWL_POWER_VEC_SIZE; i++)
		if (le32_to_cpu(cmd->sleep_interval[i]) > max_sleep)
			cmd->sleep_interval[i] = cpu_to_le32(max_sleep);

	IWL_DEBUG_POWER(priv, "Flags value = 0x%08X\n", cmd->flags);
	IWL_DEBUG_POWER(priv, "Tx timeout = %u\n", le32_to_cpu(cmd->tx_data_timeout));
	IWL_DEBUG_POWER(priv, "Rx timeout = %u\n", le32_to_cpu(cmd->rx_data_timeout));
	IWL_DEBUG_POWER(priv, "Sleep interval vector = { %d , %d , %d , %d , %d }\n",
			le32_to_cpu(cmd->sleep_interval[0]),
			le32_to_cpu(cmd->sleep_interval[1]),
			le32_to_cpu(cmd->sleep_interval[2]),
			le32_to_cpu(cmd->sleep_interval[3]),
			le32_to_cpu(cmd->sleep_interval[4]));

	return 0;
}


/*
 * compute the final power mode index
 */
int iwl_power_update_mode(struct iwl_priv *priv, bool force)
{
	struct iwl_power_mgr *setting = &(priv->power_data);
	int ret = 0;
	struct iwl_tt_mgmt *tt = &priv->power_data.tt;
	u16 uninitialized_var(final_mode);
	bool update_chains;

	/* Don't update the RX chain when chain noise calibration is running */
	update_chains = priv->chain_noise_data.state == IWL_CHAIN_NOISE_DONE ||
			priv->chain_noise_data.state == IWL_CHAIN_NOISE_ALIVE;

	final_mode = priv->power_data.user_power_setting;

	if (setting->power_disabled)
		final_mode = IWL_POWER_MODE_CAM;

	if (tt->state >= IWL_TI_1) {
		/* TT power setting overwrite user & system power setting */
		final_mode = tt->tt_power_mode;
	}
	if (iwl_is_ready_rf(priv) &&
	    ((setting->power_mode != final_mode) || force)) {
		struct iwl_powertable_cmd cmd;

		if (final_mode != IWL_POWER_MODE_CAM)
			set_bit(STATUS_POWER_PMI, &priv->status);

		iwl_update_power_cmd(priv, &cmd, final_mode);
		cmd.keep_alive_beacons = 0;

		if (final_mode == IWL_POWER_INDEX_5)
			cmd.flags |= IWL_POWER_FAST_PD;

		ret = iwl_set_power(priv, &cmd);

		if (final_mode == IWL_POWER_MODE_CAM)
			clear_bit(STATUS_POWER_PMI, &priv->status);

		if (priv->cfg->ops->lib->update_chain_flags && update_chains)
			priv->cfg->ops->lib->update_chain_flags(priv);
		else
			IWL_DEBUG_POWER(priv, "Cannot update the power, chain noise "
					"calibration running: %d\n",
					priv->chain_noise_data.state);
		if (!ret)
			setting->power_mode = final_mode;
	}

	return ret;
}
EXPORT_SYMBOL(iwl_power_update_mode);

/* set user_power_setting */
int iwl_power_set_user_mode(struct iwl_priv *priv, u16 mode)
{
	if (mode >= IWL_POWER_NUM)
		return -EINVAL;

	priv->power_data.user_power_setting = mode;

	return iwl_power_update_mode(priv, 0);
}
EXPORT_SYMBOL(iwl_power_set_user_mode);

bool iwl_ht_enabled(struct iwl_priv *priv)
{
	struct iwl_tt_mgmt *tt = &priv->power_data.tt;
	struct iwl_tt_restriction *restriction;

	if (!priv->power_data.adv_tt)
		return true;
	restriction = tt->restriction + tt->state;
	return restriction->is_ht;
}
EXPORT_SYMBOL(iwl_ht_enabled);

u8 iwl_tx_ant_restriction(struct iwl_priv *priv)
{
	struct iwl_tt_mgmt *tt = &priv->power_data.tt;
	struct iwl_tt_restriction *restriction;

	if (!priv->power_data.adv_tt)
		return IWL_TX_MULTI;
	restriction = tt->restriction + tt->state;
	return restriction->tx_stream;
}
EXPORT_SYMBOL(iwl_tx_ant_restriction);

u8 iwl_rx_ant_restriction(struct iwl_priv *priv)
{
	struct iwl_tt_mgmt *tt = &priv->power_data.tt;
	struct iwl_tt_restriction *restriction;

	if (!priv->power_data.adv_tt)
		return IWL_RX_MULTI;
	restriction = tt->restriction + tt->state;
	return restriction->rx_stream;
}
EXPORT_SYMBOL(iwl_rx_ant_restriction);

#define CT_KILL_EXIT_DURATION (5)	/* 5 seconds duration */

/*
 * toggle the bit to wake up uCode and check the temperature
 * if the temperature is below CT, uCode will stay awake and send card
 * state notification with CT_KILL bit clear to inform Thermal Throttling
 * Management to change state. Otherwise, uCode will go back to sleep
 * without doing anything, driver should continue the 5 seconds timer
 * to wake up uCode for temperature check until temperature drop below CT
 */
static void iwl_tt_check_exit_ct_kill(unsigned long data)
{
	struct iwl_priv *priv = (struct iwl_priv *)data;
	struct iwl_tt_mgmt *tt = &priv->power_data.tt;
	unsigned long flags;

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	if (tt->state == IWL_TI_CT_KILL) {
		if (priv->power_data.ct_kill_toggle) {
			iwl_write32(priv, CSR_UCODE_DRV_GP1_CLR,
				    CSR_UCODE_DRV_GP1_REG_BIT_CT_KILL_EXIT);
			priv->power_data.ct_kill_toggle = false;
		} else {
			iwl_write32(priv, CSR_UCODE_DRV_GP1_SET,
				    CSR_UCODE_DRV_GP1_REG_BIT_CT_KILL_EXIT);
			priv->power_data.ct_kill_toggle = true;
		}
		iwl_read32(priv, CSR_UCODE_DRV_GP1);
		spin_lock_irqsave(&priv->reg_lock, flags);
		if (!iwl_grab_nic_access(priv))
			iwl_release_nic_access(priv);
		spin_unlock_irqrestore(&priv->reg_lock, flags);

		/* Reschedule the ct_kill timer to occur in
		 * CT_KILL_EXIT_DURATION seconds to ensure we get a
		 * thermal update */
		mod_timer(&priv->power_data.ct_kill_exit_tm, jiffies +
			  CT_KILL_EXIT_DURATION * HZ);
	}
}

static void iwl_perform_ct_kill_task(struct iwl_priv *priv,
			   bool stop)
{
	if (stop) {
		IWL_DEBUG_POWER(priv, "Stop all queues\n");
		if (priv->mac80211_registered)
			ieee80211_stop_queues(priv->hw);
		IWL_DEBUG_POWER(priv,
				"Schedule 5 seconds CT_KILL Timer\n");
		mod_timer(&priv->power_data.ct_kill_exit_tm, jiffies +
			  CT_KILL_EXIT_DURATION * HZ);
	} else {
		IWL_DEBUG_POWER(priv, "Wake all queues\n");
		if (priv->mac80211_registered)
			ieee80211_wake_queues(priv->hw);
	}
}

#define IWL_MINIMAL_POWER_THRESHOLD		(CT_KILL_THRESHOLD_LEGACY)
#define IWL_REDUCED_PERFORMANCE_THRESHOLD_2	(100)
#define IWL_REDUCED_PERFORMANCE_THRESHOLD_1	(90)

/*
 * Legacy thermal throttling
 * 1) Avoid NIC destruction due to high temperatures
 *	Chip will identify dangerously high temperatures that can
 *	harm the device and will power down
 * 2) Avoid the NIC power down due to high temperature
 *	Throttle early enough to lower the power consumption before
 *	drastic steps are needed
 */
static void iwl_legacy_tt_handler(struct iwl_priv *priv, s32 temp)
{
	struct iwl_tt_mgmt *tt = &priv->power_data.tt;
	enum iwl_tt_state new_state;
	struct iwl_power_mgr *setting = &priv->power_data;

#ifdef CONFIG_IWLWIFI_DEBUG
	if ((tt->tt_previous_temp) &&
	    (temp > tt->tt_previous_temp) &&
	    ((temp - tt->tt_previous_temp) >
	    IWL_TT_INCREASE_MARGIN)) {
		IWL_DEBUG_POWER(priv,
			"Temperature increase %d degree Celsius\n",
			(temp - tt->tt_previous_temp));
	}
#endif
	/* in Celsius */
	if (temp >= IWL_MINIMAL_POWER_THRESHOLD)
		new_state = IWL_TI_CT_KILL;
	else if (temp >= IWL_REDUCED_PERFORMANCE_THRESHOLD_2)
		new_state = IWL_TI_2;
	else if (temp >= IWL_REDUCED_PERFORMANCE_THRESHOLD_1)
		new_state = IWL_TI_1;
	else
		new_state = IWL_TI_0;

#ifdef CONFIG_IWLWIFI_DEBUG
	tt->tt_previous_temp = temp;
#endif
	if (tt->state != new_state) {
		if (tt->state == IWL_TI_0) {
			tt->sys_power_mode = setting->power_mode;
			IWL_DEBUG_POWER(priv, "current power mode: %u\n",
				setting->power_mode);
		}
		switch (new_state) {
		case IWL_TI_0:
			/* when system ready to go back to IWL_TI_0 state
			 * using system power mode instead of TT power mode
			 * revert back to the orginal power mode which was saved
			 * before enter Thermal Throttling state
			 * update priv->power_data.user_power_setting to the
			 * required power mode to make sure
			 * iwl_power_update_mode() will update power correctly.
			 */
			priv->power_data.user_power_setting =
				tt->sys_power_mode;
			tt->tt_power_mode = tt->sys_power_mode;
			break;
		case IWL_TI_1:
			tt->tt_power_mode = IWL_POWER_INDEX_3;
			break;
		case IWL_TI_2:
			tt->tt_power_mode = IWL_POWER_INDEX_4;
			break;
		default:
			tt->tt_power_mode = IWL_POWER_INDEX_5;
			break;
		}
		if (iwl_power_update_mode(priv, true)) {
			/* TT state not updated
			 * try again during next temperature read
			 */
			IWL_ERR(priv, "Cannot update power mode, "
					"TT state not updated\n");
		} else {
			if (new_state == IWL_TI_CT_KILL)
				iwl_perform_ct_kill_task(priv, true);
			else if (tt->state == IWL_TI_CT_KILL &&
				 new_state != IWL_TI_CT_KILL)
				iwl_perform_ct_kill_task(priv, false);
			tt->state = new_state;
			IWL_DEBUG_POWER(priv, "Temperature state changed %u\n",
					tt->state);
			IWL_DEBUG_POWER(priv, "Power Index change to %u\n",
					tt->tt_power_mode);
		}
	}
}

/*
 * Advance thermal throttling
 * 1) Avoid NIC destruction due to high temperatures
 *	Chip will identify dangerously high temperatures that can
 *	harm the device and will power down
 * 2) Avoid the NIC power down due to high temperature
 *	Throttle early enough to lower the power consumption before
 *	drastic steps are needed
 *	Actions include relaxing the power down sleep thresholds and
 *	decreasing the number of TX streams
 * 3) Avoid throughput performance impact as much as possible
 *
 *=============================================================================
 *                 Condition Nxt State  Condition Nxt State Condition Nxt State
 *-----------------------------------------------------------------------------
 *     IWL_TI_0     T >= 115   CT_KILL  115>T>=105   TI_1      N/A      N/A
 *     IWL_TI_1     T >= 115   CT_KILL  115>T>=110   TI_2     T<=95     TI_0
 *     IWL_TI_2     T >= 115   CT_KILL                        T<=100    TI_1
 *    IWL_CT_KILL      N/A       N/A       N/A        N/A     T<=95     TI_0
 *=============================================================================
 */
static void iwl_advance_tt_handler(struct iwl_priv *priv, s32 temp)
{
	struct iwl_tt_mgmt *tt = &priv->power_data.tt;
	int i;
	bool changed = false;
	enum iwl_tt_state old_state;
	struct iwl_tt_trans *transaction;

	old_state = tt->state;
	for (i = 0; i < IWL_TI_STATE_MAX - 1; i++) {
		/* based on the current TT state,
		 * find the curresponding transaction table
		 * each table has (IWL_TI_STATE_MAX - 1) entries
		 * tt->transaction + ((old_state * (IWL_TI_STATE_MAX - 1))
		 * will advance to the correct table.
		 * then based on the current temperature
		 * find the next state need to transaction to
		 * go through all the possible (IWL_TI_STATE_MAX - 1) entries
		 * in the current table to see if transaction is needed
		 */
		transaction = tt->transaction +
			((old_state * (IWL_TI_STATE_MAX - 1)) + i);
		if (temp >= transaction->tt_low &&
		    temp <= transaction->tt_high) {
#ifdef CONFIG_IWLWIFI_DEBUG
			if ((tt->tt_previous_temp) &&
			    (temp > tt->tt_previous_temp) &&
			    ((temp - tt->tt_previous_temp) >
			    IWL_TT_INCREASE_MARGIN)) {
				IWL_DEBUG_POWER(priv,
					"Temperature increase %d "
					"degree Celsius\n",
					(temp - tt->tt_previous_temp));
			}
			tt->tt_previous_temp = temp;
#endif
			if (old_state !=
			    transaction->next_state) {
				changed = true;
				tt->state =
					transaction->next_state;
			}
			break;
		}
	}
	if (changed) {
		struct iwl_rxon_cmd *rxon = &priv->staging_rxon;
		struct iwl_power_mgr *setting = &priv->power_data;

		if (tt->state >= IWL_TI_1) {
			/* if switching from IWL_TI_0 to other TT state
			 * save previous power setting in tt->sys_power_mode */
			if (old_state == IWL_TI_0)
				tt->sys_power_mode = setting->power_mode;
			/* force PI = IWL_POWER_INDEX_5 in the case of TI > 0 */
			tt->tt_power_mode = IWL_POWER_INDEX_5;
			if (!iwl_ht_enabled(priv))
				/* disable HT */
				rxon->flags &= ~(RXON_FLG_CHANNEL_MODE_MSK |
					RXON_FLG_CTRL_CHANNEL_LOC_HI_MSK |
					RXON_FLG_HT40_PROT_MSK |
					RXON_FLG_HT_PROT_MSK);
			else {
				/* check HT capability and set
				 * according to the system HT capability
				 * in case get disabled before */
				iwl_set_rxon_ht(priv, &priv->current_ht_config);
			}

		} else {
			/* restore system power setting */
			/* the previous power mode was saved in
			 * tt->sys_power_mode when system move into
			 * Thermal Throttling state
			 * set power_data.user_power_setting to the previous
			 * system power mode to make sure power will get
			 * updated correctly
			 */
			priv->power_data.user_power_setting =
				tt->sys_power_mode;
			tt->tt_power_mode = tt->sys_power_mode;
			/* check HT capability and set
			 * according to the system HT capability
			 * in case get disabled before */
			iwl_set_rxon_ht(priv, &priv->current_ht_config);
		}
		if (iwl_power_update_mode(priv, true)) {
			/* TT state not updated
			 * try again during next temperature read
			 */
			IWL_ERR(priv, "Cannot update power mode, "
					"TT state not updated\n");
			tt->state = old_state;
		} else {
			IWL_DEBUG_POWER(priv,
					"Thermal Throttling to new state: %u\n",
					tt->state);
			if (old_state != IWL_TI_CT_KILL &&
			    tt->state == IWL_TI_CT_KILL) {
				IWL_DEBUG_POWER(priv, "Enter IWL_TI_CT_KILL\n");
				iwl_perform_ct_kill_task(priv, true);

			} else if (old_state == IWL_TI_CT_KILL &&
				  tt->state != IWL_TI_CT_KILL) {
				IWL_DEBUG_POWER(priv, "Exit IWL_TI_CT_KILL\n");
				iwl_perform_ct_kill_task(priv, false);
			}
		}
	}
}

/* Card State Notification indicated reach critical temperature
 * if PSP not enable, no Thermal Throttling function will be performed
 * just set the GP1 bit to acknowledge the event
 * otherwise, go into IWL_TI_CT_KILL state
 * since Card State Notification will not provide any temperature reading
 * for Legacy mode
 * so just pass the CT_KILL temperature to iwl_legacy_tt_handler()
 * for advance mode
 * pass CT_KILL_THRESHOLD+1 to make sure move into IWL_TI_CT_KILL state
 */
void iwl_tt_enter_ct_kill(struct iwl_priv *priv)
{
	struct iwl_tt_mgmt *tt = &priv->power_data.tt;

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	if (tt->state != IWL_TI_CT_KILL) {
		IWL_ERR(priv, "Device reached critical temperature "
			      "- ucode going to sleep!\n");
		if (!priv->power_data.adv_tt)
			iwl_legacy_tt_handler(priv,
					      IWL_MINIMAL_POWER_THRESHOLD);
		else
			iwl_advance_tt_handler(priv,
					       CT_KILL_THRESHOLD + 1);
	}
}
EXPORT_SYMBOL(iwl_tt_enter_ct_kill);

/* Card State Notification indicated out of critical temperature
 * since Card State Notification will not provide any temperature reading
 * so pass the IWL_REDUCED_PERFORMANCE_THRESHOLD_2 temperature
 * to iwl_legacy_tt_handler() to get out of IWL_CT_KILL state
 */
void iwl_tt_exit_ct_kill(struct iwl_priv *priv)
{
	struct iwl_tt_mgmt *tt = &priv->power_data.tt;

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	/* stop ct_kill_exit_tm timer */
	del_timer_sync(&priv->power_data.ct_kill_exit_tm);

	if (tt->state == IWL_TI_CT_KILL) {
		IWL_ERR(priv,
			"Device temperature below critical"
			"- ucode awake!\n");
		if (!priv->power_data.adv_tt)
			iwl_legacy_tt_handler(priv,
					IWL_REDUCED_PERFORMANCE_THRESHOLD_2);
		else
			iwl_advance_tt_handler(priv, CT_KILL_EXIT_THRESHOLD);
	}
}
EXPORT_SYMBOL(iwl_tt_exit_ct_kill);

void iwl_tt_handler(struct iwl_priv *priv)
{
	s32 temp = priv->temperature; /* degrees CELSIUS except 4965 */

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	if ((priv->hw_rev & CSR_HW_REV_TYPE_MSK) == CSR_HW_REV_TYPE_4965)
		temp = KELVIN_TO_CELSIUS(priv->temperature);

	if (!priv->power_data.adv_tt)
		iwl_legacy_tt_handler(priv, temp);
	else
		iwl_advance_tt_handler(priv, temp);
}
EXPORT_SYMBOL(iwl_tt_handler);

/* Thermal throttling initialization
 * For advance thermal throttling:
 *     Initialize Thermal Index and temperature threshold table
 *     Initialize thermal throttling restriction table
 */
void iwl_tt_initialize(struct iwl_priv *priv)
{
	struct iwl_tt_mgmt *tt = &priv->power_data.tt;
	struct iwl_power_mgr *setting = &priv->power_data;
	int size = sizeof(struct iwl_tt_trans) * (IWL_TI_STATE_MAX - 1);
	struct iwl_tt_trans *transaction;

	IWL_DEBUG_POWER(priv, "Initialize Thermal Throttling \n");

	memset(tt, 0, sizeof(struct iwl_tt_mgmt));

	tt->state = IWL_TI_0;
	tt->sys_power_mode = setting->power_mode;
	tt->tt_power_mode = tt->sys_power_mode;
	init_timer(&priv->power_data.ct_kill_exit_tm);
	priv->power_data.ct_kill_exit_tm.data = (unsigned long)priv;
	priv->power_data.ct_kill_exit_tm.function = iwl_tt_check_exit_ct_kill;
	switch (priv->hw_rev & CSR_HW_REV_TYPE_MSK) {
	case CSR_HW_REV_TYPE_6x00:
	case CSR_HW_REV_TYPE_6x50:
		IWL_DEBUG_POWER(priv, "Advanced Thermal Throttling\n");
		tt->restriction = kzalloc(sizeof(struct iwl_tt_restriction) *
					 IWL_TI_STATE_MAX, GFP_KERNEL);
		tt->transaction = kzalloc(sizeof(struct iwl_tt_trans) *
			IWL_TI_STATE_MAX * (IWL_TI_STATE_MAX - 1),
			GFP_KERNEL);
		if (!tt->restriction || !tt->transaction) {
			IWL_ERR(priv, "Fallback to Legacy Throttling\n");
			priv->power_data.adv_tt = false;
			kfree(tt->restriction);
			tt->restriction = NULL;
			kfree(tt->transaction);
			tt->transaction = NULL;
		} else {
			transaction = tt->transaction +
				(IWL_TI_0 * (IWL_TI_STATE_MAX - 1));
			memcpy(transaction, &tt_range_0[0], size);
			transaction = tt->transaction +
				(IWL_TI_1 * (IWL_TI_STATE_MAX - 1));
			memcpy(transaction, &tt_range_1[0], size);
			transaction = tt->transaction +
				(IWL_TI_2 * (IWL_TI_STATE_MAX - 1));
			memcpy(transaction, &tt_range_2[0], size);
			transaction = tt->transaction +
				(IWL_TI_CT_KILL * (IWL_TI_STATE_MAX - 1));
			memcpy(transaction, &tt_range_3[0], size);
			size = sizeof(struct iwl_tt_restriction) *
				IWL_TI_STATE_MAX;
			memcpy(tt->restriction,
				&restriction_range[0], size);
			priv->power_data.adv_tt = true;
		}
		break;
	default:
		IWL_DEBUG_POWER(priv, "Legacy Thermal Throttling\n");
		priv->power_data.adv_tt = false;
		break;
	}
}
EXPORT_SYMBOL(iwl_tt_initialize);

/* cleanup thermal throttling management related memory and timer */
void iwl_tt_exit(struct iwl_priv *priv)
{
	struct iwl_tt_mgmt *tt = &priv->power_data.tt;

	/* stop ct_kill_exit_tm timer if activated */
	del_timer_sync(&priv->power_data.ct_kill_exit_tm);

	if (priv->power_data.adv_tt) {
		/* free advance thermal throttling memory */
		kfree(tt->restriction);
		tt->restriction = NULL;
		kfree(tt->transaction);
		tt->transaction = NULL;
	}
}
EXPORT_SYMBOL(iwl_tt_exit);

/* initialize to default */
void iwl_power_initialize(struct iwl_priv *priv)
{
	iwl_power_init_handle(priv);
	priv->power_data.user_power_setting = IWL_POWER_INDEX_1;
	/* default to disabled until mac80211 says otherwise */
	priv->power_data.power_disabled = 1;
}
EXPORT_SYMBOL(iwl_power_initialize);
