/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Intel Corporation. All rights reserved.
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

#include "iwl-eeprom.h"
#include "iwl-dev.h"
#include "iwl-core.h"
#include "iwl-io.h"
#include "iwl-commands.h"
#include "iwl-debug.h"
#include "iwl-power.h"

/*
 * Setting power level allows the card to go to sleep when not busy.
 *
 * We calculate a sleep command based on the required latency, which
 * we get from mac80211. In order to handle thermal throttling, we can
 * also use pre-defined power levels.
 */

/*
 * For now, keep using power level 1 instead of automatically
 * adjusting ...
 */
bool no_sleep_autoadjust = true;
module_param(no_sleep_autoadjust, bool, S_IRUGO);
MODULE_PARM_DESC(no_sleep_autoadjust,
		 "don't automatically adjust sleep level "
		 "according to maximum network latency");

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

	if (priv->cfg->adv_pm) {
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

	BUG_ON(lvl < 0 || lvl >= IWL_POWER_NUM);

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

	if (priv->cfg->bt_params &&
	    priv->cfg->bt_params->advanced_bt_coexist) {
		if (!priv->cfg->bt_params->bt_sco_disable)
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

	if (priv->power_data.pci_pm)
		cmd->flags |= IWL_POWER_PCI_PM_MSK;
	else
		cmd->flags &= ~IWL_POWER_PCI_PM_MSK;

	IWL_DEBUG_POWER(priv, "numSkipDtim = %u, dtimPeriod = %d\n",
			skip, period);
	IWL_DEBUG_POWER(priv, "Sleep command for index %d\n", lvl + 1);
}

static void iwl_power_sleep_cam_cmd(struct iwl_priv *priv,
				    struct iwl_powertable_cmd *cmd)
{
	memset(cmd, 0, sizeof(*cmd));

	if (priv->power_data.pci_pm)
		cmd->flags |= IWL_POWER_PCI_PM_MSK;

	IWL_DEBUG_POWER(priv, "Sleep command for CAM\n");
}

static void iwl_power_fill_sleep_cmd(struct iwl_priv *priv,
				     struct iwl_powertable_cmd *cmd,
				     int dynps_ms, int wakeup_period)
{
	/*
	 * These are the original power level 3 sleep successions. The
	 * device may behave better with such succession and was also
	 * only tested with that. Just like the original sleep commands,
	 * also adjust the succession here to the wakeup_period below.
	 * The ranges are the same as for the sleep commands, 0-2, 3-9
	 * and >10, which is selected based on the DTIM interval for
	 * the sleep index but here we use the wakeup period since that
	 * is what we need to do for the latency requirements.
	 */
	static const u8 slp_succ_r0[IWL_POWER_VEC_SIZE] = { 2, 2, 2, 2, 2 };
	static const u8 slp_succ_r1[IWL_POWER_VEC_SIZE] = { 2, 4, 6, 7, 9 };
	static const u8 slp_succ_r2[IWL_POWER_VEC_SIZE] = { 2, 7, 9, 9, 0xFF };
	const u8 *slp_succ = slp_succ_r0;
	int i;

	if (wakeup_period > IWL_DTIM_RANGE_0_MAX)
		slp_succ = slp_succ_r1;
	if (wakeup_period > IWL_DTIM_RANGE_1_MAX)
		slp_succ = slp_succ_r2;

	memset(cmd, 0, sizeof(*cmd));

	cmd->flags = IWL_POWER_DRIVER_ALLOW_SLEEP_MSK |
		     IWL_POWER_FAST_PD; /* no use seeing frames for others */

	if (priv->power_data.pci_pm)
		cmd->flags |= IWL_POWER_PCI_PM_MSK;

	if (priv->cfg->base_params->shadow_reg_enable)
		cmd->flags |= IWL_POWER_SHADOW_REG_ENA;
	else
		cmd->flags &= ~IWL_POWER_SHADOW_REG_ENA;

	if (priv->cfg->bt_params &&
	    priv->cfg->bt_params->advanced_bt_coexist) {
		if (!priv->cfg->bt_params->bt_sco_disable)
			cmd->flags |= IWL_POWER_BT_SCO_ENA;
		else
			cmd->flags &= ~IWL_POWER_BT_SCO_ENA;
	}

	cmd->rx_data_timeout = cpu_to_le32(1000 * dynps_ms);
	cmd->tx_data_timeout = cpu_to_le32(1000 * dynps_ms);

	for (i = 0; i < IWL_POWER_VEC_SIZE; i++)
		cmd->sleep_interval[i] =
			cpu_to_le32(min_t(int, slp_succ[i], wakeup_period));

	IWL_DEBUG_POWER(priv, "Automatic sleep command\n");
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

	return iwl_send_cmd_pdu(priv, POWER_TABLE_CMD,
				sizeof(struct iwl_powertable_cmd), cmd);
}

static void iwl_power_build_cmd(struct iwl_priv *priv,
				struct iwl_powertable_cmd *cmd)
{
	bool enabled = priv->hw->conf.flags & IEEE80211_CONF_PS;
	int dtimper;

	dtimper = priv->hw->conf.ps_dtim_period ?: 1;

	if (priv->cfg->base_params->broken_powersave)
		iwl_power_sleep_cam_cmd(priv, cmd);
	else if (priv->cfg->base_params->supports_idle &&
		 priv->hw->conf.flags & IEEE80211_CONF_IDLE)
		iwl_static_sleep_cmd(priv, cmd, IWL_POWER_INDEX_5, 20);
	else if (priv->cfg->ops->lib->tt_ops.lower_power_detection &&
		 priv->cfg->ops->lib->tt_ops.tt_power_mode &&
		 priv->cfg->ops->lib->tt_ops.lower_power_detection(priv)) {
		/* in thermal throttling low power state */
		iwl_static_sleep_cmd(priv, cmd,
		    priv->cfg->ops->lib->tt_ops.tt_power_mode(priv), dtimper);
	} else if (!enabled)
		iwl_power_sleep_cam_cmd(priv, cmd);
	else if (priv->power_data.debug_sleep_level_override >= 0)
		iwl_static_sleep_cmd(priv, cmd,
				     priv->power_data.debug_sleep_level_override,
				     dtimper);
	else if (no_sleep_autoadjust)
		iwl_static_sleep_cmd(priv, cmd, IWL_POWER_INDEX_1, dtimper);
	else
		iwl_power_fill_sleep_cmd(priv, cmd,
					 priv->hw->conf.dynamic_ps_timeout,
					 priv->hw->conf.max_sleep_period);
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
		set_bit(STATUS_POWER_PMI, &priv->status);

	ret = iwl_set_power(priv, cmd);
	if (!ret) {
		if (!(cmd->flags & IWL_POWER_DRIVER_ALLOW_SLEEP_MSK))
			clear_bit(STATUS_POWER_PMI, &priv->status);

		if (priv->cfg->ops->lib->update_chain_flags && update_chains)
			priv->cfg->ops->lib->update_chain_flags(priv);
		else if (priv->cfg->ops->lib->update_chain_flags)
			IWL_DEBUG_POWER(priv,
					"Cannot update the power, chain noise "
					"calibration running: %d\n",
					priv->chain_noise_data.state);

		memcpy(&priv->power_data.sleep_cmd, cmd, sizeof(*cmd));
	} else
		IWL_ERR(priv, "set power fail, ret = %d", ret);

	return ret;
}
EXPORT_SYMBOL(iwl_power_set_mode);

int iwl_power_update_mode(struct iwl_priv *priv, bool force)
{
	struct iwl_powertable_cmd cmd;

	iwl_power_build_cmd(priv, &cmd);
	return iwl_power_set_mode(priv, &cmd, force);
}
EXPORT_SYMBOL(iwl_power_update_mode);

/* initialize to default */
void iwl_power_initialize(struct iwl_priv *priv)
{
	u16 lctl = iwl_pcie_link_ctl(priv);

	priv->power_data.pci_pm = !(lctl & PCI_CFG_LINK_CTRL_VAL_L0S_EN);

	priv->power_data.debug_sleep_level_override = -1;

	memset(&priv->power_data.sleep_cmd, 0,
		sizeof(priv->power_data.sleep_cmd));
}
EXPORT_SYMBOL(iwl_power_initialize);
