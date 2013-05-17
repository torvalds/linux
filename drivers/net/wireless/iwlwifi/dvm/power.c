/******************************************************************************
 *
 * Copyright(c) 2007 - 2013 Intel Corporation. All rights reserved.
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
#include <linux/slab.h>
#include <linux/init.h>
#include <net/mac80211.h>
#include "iwl-io.h"
#include "iwl-debug.h"
#include "iwl-trans.h"
#include "iwl-modparams.h"
#include "dev.h"
#include "agn.h"
#include "commands.h"
#include "power.h"

/*
 * Setting power level allows the card to go to sleep when not busy.
 *
 * We calculate a sleep command based on the required latency, which
 * we get from mac80211. In order to handle thermal throttling, we can
 * also use pre-defined power levels.
 */

/*
 * This defines the old power levels. They are still used by default
 * (level 1) and for thermal throttle (levels 3 through 5)
 */

struct iwl_power_vec_entry {
	struct iwl_powertable_cmd cmd;
	u8 no_dtim;	/* number of skip dtim */
};

#define IWL_DTIM_RANGE_0_MAX	2
#define IWL_DTIM_RANGE_1_MAX	10

#define NOSLP cpu_to_le16(0), 0, 0
#define SLP IWL_POWER_DRIVER_ALLOW_SLEEP_MSK, 0, 0
#define ASLP (IWL_POWER_POWER_SAVE_ENA_MSK |	\
		IWL_POWER_POWER_MANAGEMENT_ENA_MSK | \
		IWL_POWER_ADVANCE_PM_ENA_MSK)
#define ASLP_TOUT(T) cpu_to_le32(T)
#define TU_TO_USEC 1024
#define SLP_TOUT(T) cpu_to_le32((T) * TU_TO_USEC)
#define SLP_VEC(X0, X1, X2, X3, X4) {cpu_to_le32(X0), \
				     cpu_to_le32(X1), \
				     cpu_to_le32(X2), \
				     cpu_to_le32(X3), \
				     cpu_to_le32(X4)}
/* default power management (not Tx power) table values */
/* for DTIM period 0 through IWL_DTIM_RANGE_0_MAX */
/* DTIM 0 - 2 */
static const struct iwl_power_vec_entry range_0[IWL_POWER_NUM] = {
	{{SLP, SLP_TOUT(200), SLP_TOUT(500), SLP_VEC(1, 1, 2, 2, 0xFF)}, 0},
	{{SLP, SLP_TOUT(200), SLP_TOUT(300), SLP_VEC(1, 2, 2, 2, 0xFF)}, 0},
	{{SLP, SLP_TOUT(50), SLP_TOUT(100), SLP_VEC(2, 2, 2, 2, 0xFF)}, 0},
	{{SLP, SLP_TOUT(50), SLP_TOUT(25), SLP_VEC(2, 2, 4, 4, 0xFF)}, 1},
	{{SLP, SLP_TOUT(25), SLP_TOUT(25), SLP_VEC(2, 2, 4, 6, 0xFF)}, 2}
};


/* for DTIM period IWL_DTIM_RANGE_0_MAX + 1 through IWL_DTIM_RANGE_1_MAX */
/* DTIM 3 - 10 */
static const struct iwl_power_vec_entry range_1[IWL_POWER_NUM] = {
	{{SLP, SLP_TOUT(200), SLP_TOUT(500), SLP_VEC(1, 2, 3, 4, 4)}, 0},
	{{SLP, SLP_TOUT(200), SLP_TOUT(300), SLP_VEC(1, 2, 3, 4, 7)}, 0},
	{{SLP, SLP_TOUT(50), SLP_TOUT(100), SLP_VEC(2, 4, 6, 7, 9)}, 0},
	{{SLP, SLP_TOUT(50), SLP_TOUT(25), SLP_VEC(2, 4, 6, 9, 10)}, 1},
	{{SLP, SLP_TOUT(25), SLP_TOUT(25), SLP_VEC(2, 4, 6, 10, 10)}, 2}
};

/* for DTIM period > IWL_DTIM_RANGE_1_MAX */
/* DTIM 11 - */
static const struct iwl_power_vec_entry range_2[IWL_POWER_NUM] = {
	{{SLP, SLP_TOUT(200), SLP_TOUT(500), SLP_VEC(1, 2, 3, 4, 0xFF)}, 0},
	{{SLP, SLP_TOUT(200), SLP_TOUT(300), SLP_VEC(2, 4, 6, 7, 0xFF)}, 0},
	{{SLP, SLP_TOUT(50), SLP_TOUT(100), SLP_VEC(2, 7, 9, 9, 0xFF)}, 0},
	{{SLP, SLP_TOUT(50), SLP_TOUT(25), SLP_VEC(2, 7, 9, 9, 0xFF)}, 0},
	{{SLP, SLP_TOUT(25), SLP_TOUT(25), SLP_VEC(4, 7, 10, 10, 0xFF)}, 0}
};

/* advance power management */
/* DTIM 0 - 2 */
static const struct iwl_power_vec_entry apm_range_0[IWL_POWER_NUM] = {
	{{ASLP, 0, 0, ASLP_TOUT(50), ASLP_TOUT(50),
		SLP_VEC(1, 2, 4, 6, 0xFF), 0}, 0},
	{{ASLP, 0, 0, ASLP_TOUT(50), ASLP_TOUT(50),
		SLP_VEC(1, 2, 4, 6, 0xFF), 0}, 0},
	{{ASLP, 0, 0, ASLP_TOUT(50), ASLP_TOUT(50),
		SLP_VEC(1, 2, 4, 6, 0xFF), 0}, 0},
	{{ASLP, 0, 0, ASLP_TOUT(50), ASLP_TOUT(50),
		SLP_VEC(1, 2, 4, 6, 0xFF), 0}, 0},
	{{ASLP, 0, 0, ASLP_TOUT(50), ASLP_TOUT(50),
		SLP_VEC(1, 2, 6, 8, 0xFF), ASLP_TOUT(2)}, 2}
};


/* for DTIM period IWL_DTIM_RANGE_0_MAX + 1 through IWL_DTIM_RANGE_1_MAX */
/* DTIM 3 - 10 */
static const struct iwl_power_vec_entry apm_range_1[IWL_POWER_NUM] = {
	{{ASLP, 0, 0, ASLP_TOUT(50), ASLP_TOUT(50),
		SLP_VEC(1, 2, 4, 6, 0xFF), 0}, 0},
	{{ASLP, 0, 0, ASLP_TOUT(50), ASLP_TOUT(50),
		SLP_VEC(1, 2, 4, 6, 0xFF), 0}, 0},
	{{ASLP, 0, 0, ASLP_TOUT(50), ASLP_TOUT(50),
		SLP_VEC(1, 2, 4, 6, 0xFF), 0}, 0},
	{{ASLP, 0, 0, ASLP_TOUT(50), ASLP_TOUT(50),
		SLP_VEC(1, 2, 4, 6, 0xFF), 0}, 0},
	{{ASLP, 0, 0, ASLP_TOUT(50), ASLP_TOUT(50),
		SLP_VEC(1, 2, 6, 8, 0xFF), 0}, 2}
};

/* for DTIM period > IWL_DTIM_RANGE_1_MAX */
/* DTIM 11 - */
static const struct iwl_power_vec_entry apm_range_2[IWL_POWER_NUM] = {
	{{ASLP, 0, 0, ASLP_TOUT(50), ASLP_TOUT(50),
		SLP_VEC(1, 2, 4, 6, 0xFF), 0}, 0},
	{{ASLP, 0, 0, ASLP_TOUT(50), ASLP_TOUT(50),
		SLP_VEC(1, 2, 4, 6, 0xFF), 0}, 0},
	{{ASLP, 0, 0, ASLP_TOUT(50), ASLP_TOUT(50),
		SLP_VEC(1, 2, 4, 6, 0xFF), 0}, 0},
	{{ASLP, 0, 0, ASLP_TOUT(50), ASLP_TOUT(50),
		SLP_VEC(1, 2, 4, 6, 0xFF), 0}, 0},
	{{ASLP, 0, 0, ASLP_TOUT(50), ASLP_TOUT(50),
		SLP_VEC(1, 2, 6, 8, 0xFF), ASLP_TOUT(2)}, 2}
};

static void iwl_static_sleep_cmd(struct iwl_priv *priv,
				 struct iwl_powertable_cmd *cmd,
				 enum iwl_power_level lvl, int period)
{
	const struct iwl_power_vec_entry *table;
	int max_sleep[IWL_POWER_VEC_SIZE] = { 0 };
	int i;
	u8 skip;
	u32 slp_itrvl;

	if (priv->lib->adv_pm) {
		table = apm_range_2;
		if (period <= IWL_DTIM_RANGE_1_MAX)
			table = apm_range_1;
		if (period <= IWL_DTIM_RANGE_0_MAX)
			table = apm_range_0;
	} else {
		table = range_2;
		if (period <= IWL_DTIM_RANGE_1_MAX)
			table = range_1;
		if (period <= IWL_DTIM_RANGE_0_MAX)
			table = range_0;
	}

	if (WARN_ON(lvl < 0 || lvl >= IWL_POWER_NUM))
		memset(cmd, 0, sizeof(*cmd));
	else
		*cmd = table[lvl].cmd;

	if (period == 0) {
		skip = 0;
		period = 1;
		for (i = 0; i < IWL_POWER_VEC_SIZE; i++)
			max_sleep[i] =  1;

	} else {
		skip = table[lvl].no_dtim;
		for (i = 0; i < IWL_POWER_VEC_SIZE; i++)
			max_sleep[i] = le32_to_cpu(cmd->sleep_interval[i]);
		max_sleep[IWL_POWER_VEC_SIZE - 1] = skip + 1;
	}

	slp_itrvl = le32_to_cpu(cmd->sleep_interval[IWL_POWER_VEC_SIZE - 1]);
	/* figure out the listen interval based on dtim period and skip */
	if (slp_itrvl == 0xFF)
		cmd->sleep_interval[IWL_POWER_VEC_SIZE - 1] =
			cpu_to_le32(period * (skip + 1));

	slp_itrvl = le32_to_cpu(cmd->sleep_interval[IWL_POWER_VEC_SIZE - 1]);
	if (slp_itrvl > period)
		cmd->sleep_interval[IWL_POWER_VEC_SIZE - 1] =
			cpu_to_le32((slp_itrvl / period) * period);

	if (skip)
		cmd->flags |= IWL_POWER_SLEEP_OVER_DTIM_MSK;
	else
		cmd->flags &= ~IWL_POWER_SLEEP_OVER_DTIM_MSK;

	if (priv->cfg->base_params->shadow_reg_enable)
		cmd->flags |= IWL_POWER_SHADOW_REG_ENA;
	else
		cmd->flags &= ~IWL_POWER_SHADOW_REG_ENA;

	if (iwl_advanced_bt_coexist(priv)) {
		if (!priv->lib->bt_params->bt_sco_disable)
			cmd->flags |= IWL_POWER_BT_SCO_ENA;
		else
			cmd->flags &= ~IWL_POWER_BT_SCO_ENA;
	}


	slp_itrvl = le32_to_cpu(cmd->sleep_interval[IWL_POWER_VEC_SIZE - 1]);
	if (slp_itrvl > IWL_CONN_MAX_LISTEN_INTERVAL)
		cmd->sleep_interval[IWL_POWER_VEC_SIZE - 1] =
			cpu_to_le32(IWL_CONN_MAX_LISTEN_INTERVAL);

	/* enforce max sleep interval */
	for (i = IWL_POWER_VEC_SIZE - 1; i >= 0 ; i--) {
		if (le32_to_cpu(cmd->sleep_interval[i]) >
		    (max_sleep[i] * period))
			cmd->sleep_interval[i] =
				cpu_to_le32(max_sleep[i] * period);
		if (i != (IWL_POWER_VEC_SIZE - 1)) {
			if (le32_to_cpu(cmd->sleep_interval[i]) >
			    le32_to_cpu(cmd->sleep_interval[i+1]))
				cmd->sleep_interval[i] =
					cmd->sleep_interval[i+1];
		}
	}

	if (priv->power_data.bus_pm)
		cmd->flags |= IWL_POWER_PCI_PM_MSK;
	else
		cmd->flags &= ~IWL_POWER_PCI_PM_MSK;

	IWL_DEBUG_POWER(priv, "numSkipDtim = %u, dtimPeriod = %d\n",
			skip, period);
	/* The power level here is 0-4 (used as array index), but user expects
	to see 1-5 (according to spec). */
	IWL_DEBUG_POWER(priv, "Sleep command for index %d\n", lvl + 1);
}

static void iwl_power_sleep_cam_cmd(struct iwl_priv *priv,
				    struct iwl_powertable_cmd *cmd)
{
	memset(cmd, 0, sizeof(*cmd));

	if (priv->power_data.bus_pm)
		cmd->flags |= IWL_POWER_PCI_PM_MSK;

	IWL_DEBUG_POWER(priv, "Sleep command for CAM\n");
}

static int iwl_set_power(struct iwl_priv *priv, struct iwl_powertable_cmd *cmd)
{
	IWL_DEBUG_POWER(priv, "Sending power/sleep command\n");
	IWL_DEBUG_POWER(priv, "Flags value = 0x%08X\n", cmd->flags);
	IWL_DEBUG_POWER(priv, "Tx timeout = %u\n", le32_to_cpu(cmd->tx_data_timeout));
	IWL_DEBUG_POWER(priv, "Rx timeout = %u\n", le32_to_cpu(cmd->rx_data_timeout));
	IWL_DEBUG_POWER(priv, "Sleep interval vector = { %d , %d , %d , %d , %d }\n",
			le32_to_cpu(cmd->sleep_interval[0]),
			le32_to_cpu(cmd->sleep_interval[1]),
			le32_to_cpu(cmd->sleep_interval[2]),
			le32_to_cpu(cmd->sleep_interval[3]),
			le32_to_cpu(cmd->sleep_interval[4]));

	return iwl_dvm_send_cmd_pdu(priv, POWER_TABLE_CMD, CMD_SYNC,
				sizeof(struct iwl_powertable_cmd), cmd);
}

static void iwl_power_build_cmd(struct iwl_priv *priv,
				struct iwl_powertable_cmd *cmd)
{
	bool enabled = priv->hw->conf.flags & IEEE80211_CONF_PS;
	int dtimper;

	dtimper = priv->hw->conf.ps_dtim_period ?: 1;

	if (priv->wowlan)
		iwl_static_sleep_cmd(priv, cmd, IWL_POWER_INDEX_5, dtimper);
	else if (!priv->lib->no_idle_support &&
		 priv->hw->conf.flags & IEEE80211_CONF_IDLE)
		iwl_static_sleep_cmd(priv, cmd, IWL_POWER_INDEX_5, 20);
	else if (iwl_tt_is_low_power_state(priv)) {
		/* in thermal throttling low power state */
		iwl_static_sleep_cmd(priv, cmd,
		    iwl_tt_current_power_mode(priv), dtimper);
	} else if (!enabled)
		iwl_power_sleep_cam_cmd(priv, cmd);
	else if (priv->power_data.debug_sleep_level_override >= 0)
		iwl_static_sleep_cmd(priv, cmd,
				     priv->power_data.debug_sleep_level_override,
				     dtimper);
	else {
		/* Note that the user parameter is 1-5 (according to spec),
		but we pass 0-4 because it acts as an array index. */
		if (iwlwifi_mod_params.power_level > IWL_POWER_INDEX_1 &&
		    iwlwifi_mod_params.power_level <= IWL_POWER_NUM)
			iwl_static_sleep_cmd(priv, cmd,
				iwlwifi_mod_params.power_level - 1, dtimper);
		else
			iwl_static_sleep_cmd(priv, cmd,
				IWL_POWER_INDEX_1, dtimper);
	}
}

int iwl_power_set_mode(struct iwl_priv *priv, struct iwl_powertable_cmd *cmd,
		       bool force)
{
	int ret;
	bool update_chains;

	lockdep_assert_held(&priv->mutex);

	/* Don't update the RX chain when chain noise calibration is running */
	update_chains = priv->chain_noise_data.state == IWL_CHAIN_NOISE_DONE ||
			priv->chain_noise_data.state == IWL_CHAIN_NOISE_ALIVE;

	if (!memcmp(&priv->power_data.sleep_cmd, cmd, sizeof(*cmd)) && !force)
		return 0;

	if (!iwl_is_ready_rf(priv))
		return -EIO;

	/* scan complete use sleep_power_next, need to be updated */
	memcpy(&priv->power_data.sleep_cmd_next, cmd, sizeof(*cmd));
	if (test_bit(STATUS_SCANNING, &priv->status) && !force) {
		IWL_DEBUG_INFO(priv, "Defer power set mode while scanning\n");
		return 0;
	}

	if (cmd->flags & IWL_POWER_DRIVER_ALLOW_SLEEP_MSK)
		iwl_dvm_set_pmi(priv, true);

	ret = iwl_set_power(priv, cmd);
	if (!ret) {
		if (!(cmd->flags & IWL_POWER_DRIVER_ALLOW_SLEEP_MSK))
			iwl_dvm_set_pmi(priv, false);

		if (update_chains)
			iwl_update_chain_flags(priv);
		else
			IWL_DEBUG_POWER(priv,
					"Cannot update the power, chain noise "
					"calibration running: %d\n",
					priv->chain_noise_data.state);

		memcpy(&priv->power_data.sleep_cmd, cmd, sizeof(*cmd));
	} else
		IWL_ERR(priv, "set power fail, ret = %d", ret);

	return ret;
}

int iwl_power_update_mode(struct iwl_priv *priv, bool force)
{
	struct iwl_powertable_cmd cmd;

	iwl_power_build_cmd(priv, &cmd);
	return iwl_power_set_mode(priv, &cmd, force);
}

/* initialize to default */
void iwl_power_initialize(struct iwl_priv *priv)
{
	priv->power_data.bus_pm = priv->trans->pm_support;

	priv->power_data.debug_sleep_level_override = -1;

	memset(&priv->power_data.sleep_cmd, 0,
		sizeof(priv->power_data.sleep_cmd));
}
