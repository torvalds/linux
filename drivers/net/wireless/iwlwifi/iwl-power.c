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
	u16 uninitialized_var(final_mode);
	bool update_chains;

	/* Don't update the RX chain when chain noise calibration is running */
	update_chains = priv->chain_noise_data.state == IWL_CHAIN_NOISE_DONE ||
			priv->chain_noise_data.state == IWL_CHAIN_NOISE_ALIVE;

	final_mode = priv->power_data.user_power_setting;

	if (setting->power_disabled)
		final_mode = IWL_POWER_MODE_CAM;

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

/* initialize to default */
void iwl_power_initialize(struct iwl_priv *priv)
{
	iwl_power_init_handle(priv);
	priv->power_data.user_power_setting = IWL_POWER_INDEX_1;
	/* default to disabled until mac80211 says otherwise */
	priv->power_data.power_disabled = 1;
}
EXPORT_SYMBOL(iwl_power_initialize);
