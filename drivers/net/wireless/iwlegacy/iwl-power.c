/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Intel Corporation. All rights reserved.
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
 * This defines the old power levels. They are still used by default
 * (level 1) and for thermal throttle (levels 3 through 5)
 */

struct iwl_power_vec_entry {
	struct iwl_powertable_cmd cmd;
	u8 no_dtim;	/* number of skip dtim */
};

static void iwl_legacy_power_sleep_cam_cmd(struct iwl_priv *priv,
				    struct iwl_powertable_cmd *cmd)
{
	memset(cmd, 0, sizeof(*cmd));

	if (priv->power_data.pci_pm)
		cmd->flags |= IWL_POWER_PCI_PM_MSK;

	IWL_DEBUG_POWER(priv, "Sleep command for CAM\n");
}

static int
iwl_legacy_set_power(struct iwl_priv *priv, struct iwl_powertable_cmd *cmd)
{
	IWL_DEBUG_POWER(priv, "Sending power/sleep command\n");
	IWL_DEBUG_POWER(priv, "Flags value = 0x%08X\n", cmd->flags);
	IWL_DEBUG_POWER(priv, "Tx timeout = %u\n",
					le32_to_cpu(cmd->tx_data_timeout));
	IWL_DEBUG_POWER(priv, "Rx timeout = %u\n",
					le32_to_cpu(cmd->rx_data_timeout));
	IWL_DEBUG_POWER(priv,
			"Sleep interval vector = { %d , %d , %d , %d , %d }\n",
			le32_to_cpu(cmd->sleep_interval[0]),
			le32_to_cpu(cmd->sleep_interval[1]),
			le32_to_cpu(cmd->sleep_interval[2]),
			le32_to_cpu(cmd->sleep_interval[3]),
			le32_to_cpu(cmd->sleep_interval[4]));

	return iwl_legacy_send_cmd_pdu(priv, POWER_TABLE_CMD,
				sizeof(struct iwl_powertable_cmd), cmd);
}

int
iwl_legacy_power_set_mode(struct iwl_priv *priv, struct iwl_powertable_cmd *cmd,
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

	if (!iwl_legacy_is_ready_rf(priv))
		return -EIO;

	/* scan complete use sleep_power_next, need to be updated */
	memcpy(&priv->power_data.sleep_cmd_next, cmd, sizeof(*cmd));
	if (test_bit(STATUS_SCANNING, &priv->status) && !force) {
		IWL_DEBUG_INFO(priv, "Defer power set mode while scanning\n");
		return 0;
	}

	if (cmd->flags & IWL_POWER_DRIVER_ALLOW_SLEEP_MSK)
		set_bit(STATUS_POWER_PMI, &priv->status);

	ret = iwl_legacy_set_power(priv, cmd);
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

int iwl_legacy_power_update_mode(struct iwl_priv *priv, bool force)
{
	struct iwl_powertable_cmd cmd;

	iwl_legacy_power_sleep_cam_cmd(priv, &cmd);
	return iwl_legacy_power_set_mode(priv, &cmd, force);
}
EXPORT_SYMBOL(iwl_legacy_power_update_mode);

/* initialize to default */
void iwl_legacy_power_initialize(struct iwl_priv *priv)
{
	u16 lctl = iwl_legacy_pcie_link_ctl(priv);

	priv->power_data.pci_pm = !(lctl & PCI_CFG_LINK_CTRL_VAL_L0S_EN);

	priv->power_data.debug_sleep_level_override = -1;

	memset(&priv->power_data.sleep_cmd, 0,
		sizeof(priv->power_data.sleep_cmd));
}
EXPORT_SYMBOL(iwl_legacy_power_initialize);
