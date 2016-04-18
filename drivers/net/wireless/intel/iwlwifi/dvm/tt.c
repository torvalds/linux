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
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *****************************************************************************/


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <net/mac80211.h>
#include "iwl-io.h"
#include "iwl-modparams.h"
#include "iwl-debug.h"
#include "agn.h"
#include "dev.h"
#include "commands.h"
#include "tt.h"

/* default Thermal Throttling transaction table
 * Current state   |         Throttling Down               |  Throttling Up
 *=============================================================================
 *                 Condition Nxt State  Condition Nxt State Condition Nxt State
 *-----------------------------------------------------------------------------
 *     IWL_TI_0     T >= 114   CT_KILL  114>T>=105   TI_1      N/A      N/A
 *     IWL_TI_1     T >= 114   CT_KILL  114>T>=110   TI_2     T<=95     TI_0
 *     IWL_TI_2     T >= 114   CT_KILL                        T<=100    TI_1
 *    IWL_CT_KILL      N/A       N/A       N/A        N/A     T<=95     TI_0
 *=============================================================================
 */
static const struct iwl_tt_trans tt_range_0[IWL_TI_STATE_MAX - 1] = {
	{IWL_TI_0, IWL_ABSOLUTE_ZERO, 104},
	{IWL_TI_1, 105, CT_KILL_THRESHOLD - 1},
	{IWL_TI_CT_KILL, CT_KILL_THRESHOLD, IWL_ABSOLUTE_MAX}
};
static const struct iwl_tt_trans tt_range_1[IWL_TI_STATE_MAX - 1] = {
	{IWL_TI_0, IWL_ABSOLUTE_ZERO, 95},
	{IWL_TI_2, 110, CT_KILL_THRESHOLD - 1},
	{IWL_TI_CT_KILL, CT_KILL_THRESHOLD, IWL_ABSOLUTE_MAX}
};
static const struct iwl_tt_trans tt_range_2[IWL_TI_STATE_MAX - 1] = {
	{IWL_TI_1, IWL_ABSOLUTE_ZERO, 100},
	{IWL_TI_CT_KILL, CT_KILL_THRESHOLD, IWL_ABSOLUTE_MAX},
	{IWL_TI_CT_KILL, CT_KILL_THRESHOLD, IWL_ABSOLUTE_MAX}
};
static const struct iwl_tt_trans tt_range_3[IWL_TI_STATE_MAX - 1] = {
	{IWL_TI_0, IWL_ABSOLUTE_ZERO, CT_KILL_EXIT_THRESHOLD},
	{IWL_TI_CT_KILL, CT_KILL_EXIT_THRESHOLD + 1, IWL_ABSOLUTE_MAX},
	{IWL_TI_CT_KILL, CT_KILL_EXIT_THRESHOLD + 1, IWL_ABSOLUTE_MAX}
};

/* Advance Thermal Throttling default restriction table */
static const struct iwl_tt_restriction restriction_range[IWL_TI_STATE_MAX] = {
	{IWL_ANT_OK_MULTI, IWL_ANT_OK_MULTI, true },
	{IWL_ANT_OK_SINGLE, IWL_ANT_OK_MULTI, true },
	{IWL_ANT_OK_SINGLE, IWL_ANT_OK_SINGLE, false },
	{IWL_ANT_OK_NONE, IWL_ANT_OK_NONE, false }
};

bool iwl_tt_is_low_power_state(struct iwl_priv *priv)
{
	struct iwl_tt_mgmt *tt = &priv->thermal_throttle;

	if (tt->state >= IWL_TI_1)
		return true;
	return false;
}

u8 iwl_tt_current_power_mode(struct iwl_priv *priv)
{
	struct iwl_tt_mgmt *tt = &priv->thermal_throttle;

	return tt->tt_power_mode;
}

bool iwl_ht_enabled(struct iwl_priv *priv)
{
	struct iwl_tt_mgmt *tt = &priv->thermal_throttle;
	struct iwl_tt_restriction *restriction;

	if (!priv->thermal_throttle.advanced_tt)
		return true;
	restriction = tt->restriction + tt->state;
	return restriction->is_ht;
}

static bool iwl_within_ct_kill_margin(struct iwl_priv *priv)
{
	s32 temp = priv->temperature; /* degrees CELSIUS except specified */
	bool within_margin = false;

	if (!priv->thermal_throttle.advanced_tt)
		within_margin = ((temp + IWL_TT_CT_KILL_MARGIN) >=
				CT_KILL_THRESHOLD_LEGACY) ? true : false;
	else
		within_margin = ((temp + IWL_TT_CT_KILL_MARGIN) >=
				CT_KILL_THRESHOLD) ? true : false;
	return within_margin;
}

bool iwl_check_for_ct_kill(struct iwl_priv *priv)
{
	bool is_ct_kill = false;

	if (iwl_within_ct_kill_margin(priv)) {
		iwl_tt_enter_ct_kill(priv);
		is_ct_kill = true;
	}
	return is_ct_kill;
}

enum iwl_antenna_ok iwl_tx_ant_restriction(struct iwl_priv *priv)
{
	struct iwl_tt_mgmt *tt = &priv->thermal_throttle;
	struct iwl_tt_restriction *restriction;

	if (!priv->thermal_throttle.advanced_tt)
		return IWL_ANT_OK_MULTI;
	restriction = tt->restriction + tt->state;
	return restriction->tx_stream;
}

enum iwl_antenna_ok iwl_rx_ant_restriction(struct iwl_priv *priv)
{
	struct iwl_tt_mgmt *tt = &priv->thermal_throttle;
	struct iwl_tt_restriction *restriction;

	if (!priv->thermal_throttle.advanced_tt)
		return IWL_ANT_OK_MULTI;
	restriction = tt->restriction + tt->state;
	return restriction->rx_stream;
}

#define CT_KILL_EXIT_DURATION (5)	/* 5 seconds duration */
#define CT_KILL_WAITING_DURATION (300)	/* 300ms duration */

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
	struct iwl_tt_mgmt *tt = &priv->thermal_throttle;
	unsigned long flags;

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	if (tt->state == IWL_TI_CT_KILL) {
		if (priv->thermal_throttle.ct_kill_toggle) {
			iwl_write32(priv->trans, CSR_UCODE_DRV_GP1_CLR,
				    CSR_UCODE_DRV_GP1_REG_BIT_CT_KILL_EXIT);
			priv->thermal_throttle.ct_kill_toggle = false;
		} else {
			iwl_write32(priv->trans, CSR_UCODE_DRV_GP1_SET,
				    CSR_UCODE_DRV_GP1_REG_BIT_CT_KILL_EXIT);
			priv->thermal_throttle.ct_kill_toggle = true;
		}
		iwl_read32(priv->trans, CSR_UCODE_DRV_GP1);
		if (iwl_trans_grab_nic_access(priv->trans, &flags))
			iwl_trans_release_nic_access(priv->trans, &flags);

		/* Reschedule the ct_kill timer to occur in
		 * CT_KILL_EXIT_DURATION seconds to ensure we get a
		 * thermal update */
		IWL_DEBUG_TEMP(priv, "schedule ct_kill exit timer\n");
		mod_timer(&priv->thermal_throttle.ct_kill_exit_tm,
			  jiffies + CT_KILL_EXIT_DURATION * HZ);
	}
}

static void iwl_perform_ct_kill_task(struct iwl_priv *priv,
			   bool stop)
{
	if (stop) {
		IWL_DEBUG_TEMP(priv, "Stop all queues\n");
		if (priv->mac80211_registered)
			ieee80211_stop_queues(priv->hw);
		IWL_DEBUG_TEMP(priv,
				"Schedule 5 seconds CT_KILL Timer\n");
		mod_timer(&priv->thermal_throttle.ct_kill_exit_tm,
			  jiffies + CT_KILL_EXIT_DURATION * HZ);
	} else {
		IWL_DEBUG_TEMP(priv, "Wake all queues\n");
		if (priv->mac80211_registered)
			ieee80211_wake_queues(priv->hw);
	}
}

static void iwl_tt_ready_for_ct_kill(unsigned long data)
{
	struct iwl_priv *priv = (struct iwl_priv *)data;
	struct iwl_tt_mgmt *tt = &priv->thermal_throttle;

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	/* temperature timer expired, ready to go into CT_KILL state */
	if (tt->state != IWL_TI_CT_KILL) {
		IWL_DEBUG_TEMP(priv, "entering CT_KILL state when "
				"temperature timer expired\n");
		tt->state = IWL_TI_CT_KILL;
		set_bit(STATUS_CT_KILL, &priv->status);
		iwl_perform_ct_kill_task(priv, true);
	}
}

static void iwl_prepare_ct_kill_task(struct iwl_priv *priv)
{
	IWL_DEBUG_TEMP(priv, "Prepare to enter IWL_TI_CT_KILL\n");
	/* make request to retrieve statistics information */
	iwl_send_statistics_request(priv, 0, false);
	/* Reschedule the ct_kill wait timer */
	mod_timer(&priv->thermal_throttle.ct_kill_waiting_tm,
		 jiffies + msecs_to_jiffies(CT_KILL_WAITING_DURATION));
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
static void iwl_legacy_tt_handler(struct iwl_priv *priv, s32 temp, bool force)
{
	struct iwl_tt_mgmt *tt = &priv->thermal_throttle;
	enum iwl_tt_state old_state;

#ifdef CONFIG_IWLWIFI_DEBUG
	if ((tt->tt_previous_temp) &&
	    (temp > tt->tt_previous_temp) &&
	    ((temp - tt->tt_previous_temp) >
	    IWL_TT_INCREASE_MARGIN)) {
		IWL_DEBUG_TEMP(priv,
			"Temperature increase %d degree Celsius\n",
			(temp - tt->tt_previous_temp));
	}
#endif
	old_state = tt->state;
	/* in Celsius */
	if (temp >= IWL_MINIMAL_POWER_THRESHOLD)
		tt->state = IWL_TI_CT_KILL;
	else if (temp >= IWL_REDUCED_PERFORMANCE_THRESHOLD_2)
		tt->state = IWL_TI_2;
	else if (temp >= IWL_REDUCED_PERFORMANCE_THRESHOLD_1)
		tt->state = IWL_TI_1;
	else
		tt->state = IWL_TI_0;

#ifdef CONFIG_IWLWIFI_DEBUG
	tt->tt_previous_temp = temp;
#endif
	/* stop ct_kill_waiting_tm timer */
	del_timer_sync(&priv->thermal_throttle.ct_kill_waiting_tm);
	if (tt->state != old_state) {
		switch (tt->state) {
		case IWL_TI_0:
			/*
			 * When the system is ready to go back to IWL_TI_0
			 * we only have to call iwl_power_update_mode() to
			 * do so.
			 */
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
		mutex_lock(&priv->mutex);
		if (old_state == IWL_TI_CT_KILL)
			clear_bit(STATUS_CT_KILL, &priv->status);
		if (tt->state != IWL_TI_CT_KILL &&
		    iwl_power_update_mode(priv, true)) {
			/* TT state not updated
			 * try again during next temperature read
			 */
			if (old_state == IWL_TI_CT_KILL)
				set_bit(STATUS_CT_KILL, &priv->status);
			tt->state = old_state;
			IWL_ERR(priv, "Cannot update power mode, "
					"TT state not updated\n");
		} else {
			if (tt->state == IWL_TI_CT_KILL) {
				if (force) {
					set_bit(STATUS_CT_KILL, &priv->status);
					iwl_perform_ct_kill_task(priv, true);
				} else {
					iwl_prepare_ct_kill_task(priv);
					tt->state = old_state;
				}
			} else if (old_state == IWL_TI_CT_KILL &&
				 tt->state != IWL_TI_CT_KILL)
				iwl_perform_ct_kill_task(priv, false);
			IWL_DEBUG_TEMP(priv, "Temperature state changed %u\n",
					tt->state);
			IWL_DEBUG_TEMP(priv, "Power Index change to %u\n",
					tt->tt_power_mode);
		}
		mutex_unlock(&priv->mutex);
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
 *     IWL_TI_0     T >= 114   CT_KILL  114>T>=105   TI_1      N/A      N/A
 *     IWL_TI_1     T >= 114   CT_KILL  114>T>=110   TI_2     T<=95     TI_0
 *     IWL_TI_2     T >= 114   CT_KILL                        T<=100    TI_1
 *    IWL_CT_KILL      N/A       N/A       N/A        N/A     T<=95     TI_0
 *=============================================================================
 */
static void iwl_advance_tt_handler(struct iwl_priv *priv, s32 temp, bool force)
{
	struct iwl_tt_mgmt *tt = &priv->thermal_throttle;
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
				IWL_DEBUG_TEMP(priv,
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
	/* stop ct_kill_waiting_tm timer */
	del_timer_sync(&priv->thermal_throttle.ct_kill_waiting_tm);
	if (changed) {
		if (tt->state >= IWL_TI_1) {
			/* force PI = IWL_POWER_INDEX_5 in the case of TI > 0 */
			tt->tt_power_mode = IWL_POWER_INDEX_5;

			if (!iwl_ht_enabled(priv)) {
				struct iwl_rxon_context *ctx;

				for_each_context(priv, ctx) {
					struct iwl_rxon_cmd *rxon;

					rxon = &ctx->staging;

					/* disable HT */
					rxon->flags &= ~(
						RXON_FLG_CHANNEL_MODE_MSK |
						RXON_FLG_CTRL_CHANNEL_LOC_HI_MSK |
						RXON_FLG_HT40_PROT_MSK |
						RXON_FLG_HT_PROT_MSK);
				}
			} else {
				/* check HT capability and set
				 * according to the system HT capability
				 * in case get disabled before */
				iwl_set_rxon_ht(priv, &priv->current_ht_config);
			}

		} else {
			/*
			 * restore system power setting -- it will be
			 * recalculated automatically.
			 */

			/* check HT capability and set
			 * according to the system HT capability
			 * in case get disabled before */
			iwl_set_rxon_ht(priv, &priv->current_ht_config);
		}
		mutex_lock(&priv->mutex);
		if (old_state == IWL_TI_CT_KILL)
			clear_bit(STATUS_CT_KILL, &priv->status);
		if (tt->state != IWL_TI_CT_KILL &&
		    iwl_power_update_mode(priv, true)) {
			/* TT state not updated
			 * try again during next temperature read
			 */
			IWL_ERR(priv, "Cannot update power mode, "
					"TT state not updated\n");
			if (old_state == IWL_TI_CT_KILL)
				set_bit(STATUS_CT_KILL, &priv->status);
			tt->state = old_state;
		} else {
			IWL_DEBUG_TEMP(priv,
					"Thermal Throttling to new state: %u\n",
					tt->state);
			if (old_state != IWL_TI_CT_KILL &&
			    tt->state == IWL_TI_CT_KILL) {
				if (force) {
					IWL_DEBUG_TEMP(priv,
						"Enter IWL_TI_CT_KILL\n");
					set_bit(STATUS_CT_KILL, &priv->status);
					iwl_perform_ct_kill_task(priv, true);
				} else {
					tt->state = old_state;
					iwl_prepare_ct_kill_task(priv);
				}
			} else if (old_state == IWL_TI_CT_KILL &&
				  tt->state != IWL_TI_CT_KILL) {
				IWL_DEBUG_TEMP(priv, "Exit IWL_TI_CT_KILL\n");
				iwl_perform_ct_kill_task(priv, false);
			}
		}
		mutex_unlock(&priv->mutex);
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
static void iwl_bg_ct_enter(struct work_struct *work)
{
	struct iwl_priv *priv = container_of(work, struct iwl_priv, ct_enter);
	struct iwl_tt_mgmt *tt = &priv->thermal_throttle;

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	if (!iwl_is_ready(priv))
		return;

	if (tt->state != IWL_TI_CT_KILL) {
		IWL_ERR(priv, "Device reached critical temperature "
			      "- ucode going to sleep!\n");
		if (!priv->thermal_throttle.advanced_tt)
			iwl_legacy_tt_handler(priv,
					      IWL_MINIMAL_POWER_THRESHOLD,
					      true);
		else
			iwl_advance_tt_handler(priv,
					       CT_KILL_THRESHOLD + 1, true);
	}
}

/* Card State Notification indicated out of critical temperature
 * since Card State Notification will not provide any temperature reading
 * so pass the IWL_REDUCED_PERFORMANCE_THRESHOLD_2 temperature
 * to iwl_legacy_tt_handler() to get out of IWL_CT_KILL state
 */
static void iwl_bg_ct_exit(struct work_struct *work)
{
	struct iwl_priv *priv = container_of(work, struct iwl_priv, ct_exit);
	struct iwl_tt_mgmt *tt = &priv->thermal_throttle;

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	if (!iwl_is_ready(priv))
		return;

	/* stop ct_kill_exit_tm timer */
	del_timer_sync(&priv->thermal_throttle.ct_kill_exit_tm);

	if (tt->state == IWL_TI_CT_KILL) {
		IWL_ERR(priv,
			"Device temperature below critical"
			"- ucode awake!\n");
		/*
		 * exit from CT_KILL state
		 * reset the current temperature reading
		 */
		priv->temperature = 0;
		if (!priv->thermal_throttle.advanced_tt)
			iwl_legacy_tt_handler(priv,
				      IWL_REDUCED_PERFORMANCE_THRESHOLD_2,
				      true);
		else
			iwl_advance_tt_handler(priv, CT_KILL_EXIT_THRESHOLD,
					       true);
	}
}

void iwl_tt_enter_ct_kill(struct iwl_priv *priv)
{
	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	IWL_DEBUG_TEMP(priv, "Queueing critical temperature enter.\n");
	queue_work(priv->workqueue, &priv->ct_enter);
}

void iwl_tt_exit_ct_kill(struct iwl_priv *priv)
{
	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	IWL_DEBUG_TEMP(priv, "Queueing critical temperature exit.\n");
	queue_work(priv->workqueue, &priv->ct_exit);
}

static void iwl_bg_tt_work(struct work_struct *work)
{
	struct iwl_priv *priv = container_of(work, struct iwl_priv, tt_work);
	s32 temp = priv->temperature; /* degrees CELSIUS except specified */

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	if (!priv->thermal_throttle.advanced_tt)
		iwl_legacy_tt_handler(priv, temp, false);
	else
		iwl_advance_tt_handler(priv, temp, false);
}

void iwl_tt_handler(struct iwl_priv *priv)
{
	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	IWL_DEBUG_TEMP(priv, "Queueing thermal throttling work.\n");
	queue_work(priv->workqueue, &priv->tt_work);
}

/* Thermal throttling initialization
 * For advance thermal throttling:
 *     Initialize Thermal Index and temperature threshold table
 *     Initialize thermal throttling restriction table
 */
void iwl_tt_initialize(struct iwl_priv *priv)
{
	struct iwl_tt_mgmt *tt = &priv->thermal_throttle;
	int size = sizeof(struct iwl_tt_trans) * (IWL_TI_STATE_MAX - 1);
	struct iwl_tt_trans *transaction;

	IWL_DEBUG_TEMP(priv, "Initialize Thermal Throttling\n");

	memset(tt, 0, sizeof(struct iwl_tt_mgmt));

	tt->state = IWL_TI_0;
	setup_timer(&priv->thermal_throttle.ct_kill_exit_tm,
		    iwl_tt_check_exit_ct_kill, (unsigned long)priv);
	setup_timer(&priv->thermal_throttle.ct_kill_waiting_tm,
		    iwl_tt_ready_for_ct_kill, (unsigned long)priv);
	/* setup deferred ct kill work */
	INIT_WORK(&priv->tt_work, iwl_bg_tt_work);
	INIT_WORK(&priv->ct_enter, iwl_bg_ct_enter);
	INIT_WORK(&priv->ct_exit, iwl_bg_ct_exit);

	if (priv->lib->adv_thermal_throttle) {
		IWL_DEBUG_TEMP(priv, "Advanced Thermal Throttling\n");
		tt->restriction = kcalloc(IWL_TI_STATE_MAX,
					  sizeof(struct iwl_tt_restriction),
					  GFP_KERNEL);
		tt->transaction = kcalloc(IWL_TI_STATE_MAX *
					  (IWL_TI_STATE_MAX - 1),
					  sizeof(struct iwl_tt_trans),
					  GFP_KERNEL);
		if (!tt->restriction || !tt->transaction) {
			IWL_ERR(priv, "Fallback to Legacy Throttling\n");
			priv->thermal_throttle.advanced_tt = false;
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
			priv->thermal_throttle.advanced_tt = true;
		}
	} else {
		IWL_DEBUG_TEMP(priv, "Legacy Thermal Throttling\n");
		priv->thermal_throttle.advanced_tt = false;
	}
}

/* cleanup thermal throttling management related memory and timer */
void iwl_tt_exit(struct iwl_priv *priv)
{
	struct iwl_tt_mgmt *tt = &priv->thermal_throttle;

	/* stop ct_kill_exit_tm timer if activated */
	del_timer_sync(&priv->thermal_throttle.ct_kill_exit_tm);
	/* stop ct_kill_waiting_tm timer if activated */
	del_timer_sync(&priv->thermal_throttle.ct_kill_waiting_tm);
	cancel_work_sync(&priv->tt_work);
	cancel_work_sync(&priv->ct_enter);
	cancel_work_sync(&priv->ct_exit);

	if (priv->thermal_throttle.advanced_tt) {
		/* free advance thermal throttling memory */
		kfree(tt->restriction);
		tt->restriction = NULL;
		kfree(tt->transaction);
		tt->transaction = NULL;
	}
}
