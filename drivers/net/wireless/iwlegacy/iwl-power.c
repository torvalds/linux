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

struct il_power_vec_entry {
	struct il_powertable_cmd cmd;
	u8 no_dtim;	/* number of skip dtim */
};

static void il_power_sleep_cam_cmd(struct il_priv *il,
				    struct il_powertable_cmd *cmd)
{
	memset(cmd, 0, sizeof(*cmd));

	if (il->power_data.pci_pm)
		cmd->flags |= IL_POWER_PCI_PM_MSK;

	D_POWER("Sleep command for CAM\n");
}

static int
il_set_power(struct il_priv *il, struct il_powertable_cmd *cmd)
{
	D_POWER("Sending power/sleep command\n");
	D_POWER("Flags value = 0x%08X\n", cmd->flags);
	D_POWER("Tx timeout = %u\n",
					le32_to_cpu(cmd->tx_data_timeout));
	D_POWER("Rx timeout = %u\n",
					le32_to_cpu(cmd->rx_data_timeout));
	D_POWER(
			"Sleep interval vector = { %d , %d , %d , %d , %d }\n",
			le32_to_cpu(cmd->sleep_interval[0]),
			le32_to_cpu(cmd->sleep_interval[1]),
			le32_to_cpu(cmd->sleep_interval[2]),
			le32_to_cpu(cmd->sleep_interval[3]),
			le32_to_cpu(cmd->sleep_interval[4]));

	return il_send_cmd_pdu(il, C_POWER_TBL,
				sizeof(struct il_powertable_cmd), cmd);
}

int
il_power_set_mode(struct il_priv *il, struct il_powertable_cmd *cmd,
		       bool force)
{
	int ret;
	bool update_chains;

	lockdep_assert_held(&il->mutex);

	/* Don't update the RX chain when chain noise calibration is running */
	update_chains = il->chain_noise_data.state == IL_CHAIN_NOISE_DONE ||
			il->chain_noise_data.state == IL_CHAIN_NOISE_ALIVE;

	if (!memcmp(&il->power_data.sleep_cmd, cmd, sizeof(*cmd)) && !force)
		return 0;

	if (!il_is_ready_rf(il))
		return -EIO;

	/* scan complete use sleep_power_next, need to be updated */
	memcpy(&il->power_data.sleep_cmd_next, cmd, sizeof(*cmd));
	if (test_bit(S_SCANNING, &il->status) && !force) {
		D_INFO("Defer power set mode while scanning\n");
		return 0;
	}

	if (cmd->flags & IL_POWER_DRIVER_ALLOW_SLEEP_MSK)
		set_bit(S_POWER_PMI, &il->status);

	ret = il_set_power(il, cmd);
	if (!ret) {
		if (!(cmd->flags & IL_POWER_DRIVER_ALLOW_SLEEP_MSK))
			clear_bit(S_POWER_PMI, &il->status);

		if (il->cfg->ops->lib->update_chain_flags && update_chains)
			il->cfg->ops->lib->update_chain_flags(il);
		else if (il->cfg->ops->lib->update_chain_flags)
			D_POWER(
					"Cannot update the power, chain noise "
					"calibration running: %d\n",
					il->chain_noise_data.state);

		memcpy(&il->power_data.sleep_cmd, cmd, sizeof(*cmd));
	} else
		IL_ERR("set power fail, ret = %d", ret);

	return ret;
}

int il_power_update_mode(struct il_priv *il, bool force)
{
	struct il_powertable_cmd cmd;

	il_power_sleep_cam_cmd(il, &cmd);
	return il_power_set_mode(il, &cmd, force);
}
EXPORT_SYMBOL(il_power_update_mode);

/* initialize to default */
void il_power_initialize(struct il_priv *il)
{
	u16 lctl = il_pcie_link_ctl(il);

	il->power_data.pci_pm = !(lctl & PCI_CFG_LINK_CTRL_VAL_L0S_EN);

	il->power_data.debug_sleep_level_override = -1;

	memset(&il->power_data.sleep_cmd, 0,
		sizeof(il->power_data.sleep_cmd));
}
EXPORT_SYMBOL(il_power_initialize);
