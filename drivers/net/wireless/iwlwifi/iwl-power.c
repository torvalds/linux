/******************************************************************************
 *
 * Copyright(c) 2007 - 2008 Intel Corporation. All rights reserved.
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
 * James P. Ketrenos <ipw2100-admin@linux.intel.com>
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
#include "iwl-helpers.h"

/*
 * Setting power level allow the card to go to sleep when not busy
 * there are three factor that decide the power level to go to, they
 * are list here with its priority
 *  1- critical_power_setting this will be set according to card temperature.
 *  2- system_power_setting this will be set by system PM manager.
 *  3- user_power_setting this will be set by user either by writing to sys or
 *  	mac80211
 *
 * if system_power_setting and user_power_setting is set to auto
 * the power level will be decided according to association status and battery
 * status.
 *
 */

#define MSEC_TO_USEC 1024
#define IWL_POWER_RANGE_0_MAX  (2)
#define IWL_POWER_RANGE_1_MAX  (10)


#define NOSLP __constant_cpu_to_le16(0), 0, 0
#define SLP IWL_POWER_DRIVER_ALLOW_SLEEP_MSK, 0, 0
#define SLP_TOUT(T) __constant_cpu_to_le32((T) * MSEC_TO_USEC)
#define SLP_VEC(X0, X1, X2, X3, X4) {__constant_cpu_to_le32(X0), \
				     __constant_cpu_to_le32(X1), \
				     __constant_cpu_to_le32(X2), \
				     __constant_cpu_to_le32(X3), \
				     __constant_cpu_to_le32(X4)}

#define IWL_POWER_ON_BATTERY		IWL_POWER_INDEX_5
#define IWL_POWER_ON_AC_DISASSOC	IWL_POWER_MODE_CAM
#define IWL_POWER_ON_AC_ASSOC		IWL_POWER_MODE_CAM


#define IWL_CT_KILL_TEMPERATURE		110
#define IWL_MIN_POWER_TEMPERATURE	100
#define IWL_REDUCED_POWER_TEMPERATURE	95

/* default power management (not Tx power) table values */
/* for tim  0-10 */
static struct iwl_power_vec_entry range_0[IWL_POWER_MAX] = {
	{{NOSLP, SLP_TOUT(0), SLP_TOUT(0), SLP_VEC(0, 0, 0, 0, 0)}, 0},
	{{SLP, SLP_TOUT(200), SLP_TOUT(500), SLP_VEC(1, 2, 2, 2, 0xFF)}, 0},
	{{SLP, SLP_TOUT(200), SLP_TOUT(300), SLP_VEC(1, 2, 2, 2, 0xFF)}, 0},
	{{SLP, SLP_TOUT(50), SLP_TOUT(100), SLP_VEC(2, 2, 2, 2, 0xFF)}, 0},
	{{SLP, SLP_TOUT(50), SLP_TOUT(25), SLP_VEC(2, 2, 4, 4, 0xFF)}, 1},
	{{SLP, SLP_TOUT(25), SLP_TOUT(25), SLP_VEC(2, 2, 4, 6, 0xFF)}, 2}
};


/* for tim = 3-10 */
static struct iwl_power_vec_entry range_1[IWL_POWER_MAX] = {
	{{NOSLP, SLP_TOUT(0), SLP_TOUT(0), SLP_VEC(0, 0, 0, 0, 0)}, 0},
	{{SLP, SLP_TOUT(200), SLP_TOUT(500), SLP_VEC(1, 2, 3, 4, 4)}, 0},
	{{SLP, SLP_TOUT(200), SLP_TOUT(300), SLP_VEC(1, 2, 3, 4, 7)}, 0},
	{{SLP, SLP_TOUT(50), SLP_TOUT(100), SLP_VEC(2, 4, 6, 7, 9)}, 0},
	{{SLP, SLP_TOUT(50), SLP_TOUT(25), SLP_VEC(2, 4, 6, 9, 10)}, 1},
	{{SLP, SLP_TOUT(25), SLP_TOUT(25), SLP_VEC(2, 4, 7, 10, 10)}, 2}
};

/* for tim > 11 */
static struct iwl_power_vec_entry range_2[IWL_POWER_MAX] = {
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
	return iwl_send_cmd_pdu_async(priv, POWER_TABLE_CMD,
				      sizeof(struct iwl_powertable_cmd),
				      cmd, NULL);
}
/* decide the right power level according to association status
 * and battery status
 */
static u16 iwl_get_auto_power_mode(struct iwl_priv *priv)
{
	u16 mode;

	switch (priv->power_data.user_power_setting) {
	case IWL_POWER_AUTO:
		/* if running on battery */
		if (priv->power_data.is_battery_active)
			mode = IWL_POWER_ON_BATTERY;
		else if (iwl_is_associated(priv))
			mode = IWL_POWER_ON_AC_ASSOC;
		else
			mode = IWL_POWER_ON_AC_DISASSOC;
		break;
	/* FIXME: remove battery and ac from here */
	case IWL_POWER_BATTERY:
		mode = IWL_POWER_INDEX_3;
		break;
	case IWL_POWER_AC:
		mode = IWL_POWER_MODE_CAM;
		break;
	default:
		mode = priv->power_data.user_power_setting;
		break;
	}
	return mode;
}

/* initialize to default */
static int iwl_power_init_handle(struct iwl_priv *priv)
{
	struct iwl_power_mgr *pow_data;
	int size = sizeof(struct iwl_power_vec_entry) * IWL_POWER_MAX;
	struct iwl_powertable_cmd *cmd;
	int i;
	u16 pci_pm;

	IWL_DEBUG_POWER("Initialize power \n");

	pow_data = &(priv->power_data);

	memset(pow_data, 0, sizeof(*pow_data));

	memcpy(&pow_data->pwr_range_0[0], &range_0[0], size);
	memcpy(&pow_data->pwr_range_1[0], &range_1[0], size);
	memcpy(&pow_data->pwr_range_2[0], &range_2[0], size);

	pci_read_config_word(priv->pci_dev, PCI_CFG_LINK_CTRL, &pci_pm);

	IWL_DEBUG_POWER("adjust power command flags\n");

	for (i = 0; i < IWL_POWER_MAX; i++) {
		cmd = &pow_data->pwr_range_0[i].cmd;

		if (pci_pm & PCI_CFG_LINK_CTRL_VAL_L0S_EN)
			cmd->flags &= ~IWL_POWER_PCI_PM_MSK;
		else
			cmd->flags |= IWL_POWER_PCI_PM_MSK;
	}
	return 0;
}

/* adjust power command according to dtim period and power level*/
static int iwl_update_power_command(struct iwl_priv *priv,
				    struct iwl_powertable_cmd *cmd,
				    u16 mode)
{
	int ret = 0, i;
	u8 skip;
	u32 max_sleep = 0;
	struct iwl_power_vec_entry *range;
	u8 period = 0;
	struct iwl_power_mgr *pow_data;

	if (mode > IWL_POWER_INDEX_5) {
		IWL_DEBUG_POWER("Error invalid power mode \n");
		return -1;
	}
	pow_data = &(priv->power_data);

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
		skip = 0;
	} else
		skip = range[mode].no_dtim;

	if (skip == 0) {
		max_sleep = period;
		cmd->flags &= ~IWL_POWER_SLEEP_OVER_DTIM_MSK;
	} else {
		__le32 slp_itrvl = cmd->sleep_interval[IWL_POWER_VEC_SIZE - 1];
		max_sleep = le32_to_cpu(slp_itrvl);
		if (max_sleep == 0xFF)
			max_sleep = period * (skip + 1);
		else if (max_sleep >  period)
			max_sleep = (le32_to_cpu(slp_itrvl) / period) * period;
		cmd->flags |= IWL_POWER_SLEEP_OVER_DTIM_MSK;
	}

	for (i = 0; i < IWL_POWER_VEC_SIZE; i++) {
		if (le32_to_cpu(cmd->sleep_interval[i]) > max_sleep)
			cmd->sleep_interval[i] = cpu_to_le32(max_sleep);
	}

	IWL_DEBUG_POWER("Flags value = 0x%08X\n", cmd->flags);
	IWL_DEBUG_POWER("Tx timeout = %u\n", le32_to_cpu(cmd->tx_data_timeout));
	IWL_DEBUG_POWER("Rx timeout = %u\n", le32_to_cpu(cmd->rx_data_timeout));
	IWL_DEBUG_POWER("Sleep interval vector = { %d , %d , %d , %d , %d }\n",
			le32_to_cpu(cmd->sleep_interval[0]),
			le32_to_cpu(cmd->sleep_interval[1]),
			le32_to_cpu(cmd->sleep_interval[2]),
			le32_to_cpu(cmd->sleep_interval[3]),
			le32_to_cpu(cmd->sleep_interval[4]));

	return ret;
}


/*
 * compute the final power mode index
 */
int iwl_power_update_mode(struct iwl_priv *priv, bool force)
{
	struct iwl_power_mgr *setting = &(priv->power_data);
	int ret = 0;
	u16 uninitialized_var(final_mode);

	/* Don't update the RX chain when chain noise calibration is running */
	if (priv->chain_noise_data.state != IWL_CHAIN_NOISE_DONE &&
	    priv->chain_noise_data.state != IWL_CHAIN_NOISE_ALIVE) {
		IWL_DEBUG_POWER("Cannot update the power, chain noise "
			"calibration running: %d\n",
			priv->chain_noise_data.state);
		return -EAGAIN;
	}

	/* If on battery, set to 3,
	 * if plugged into AC power, set to CAM ("continuously aware mode"),
	 * else user level */

	switch (setting->system_power_setting) {
	case IWL_POWER_SYS_AUTO:
		final_mode = iwl_get_auto_power_mode(priv);
		break;
	case IWL_POWER_SYS_BATTERY:
		final_mode = IWL_POWER_INDEX_3;
		break;
	case IWL_POWER_SYS_AC:
		final_mode = IWL_POWER_MODE_CAM;
		break;
	default:
		final_mode = IWL_POWER_INDEX_3;
		WARN_ON(1);
	}

	if (setting->critical_power_setting > final_mode)
		final_mode = setting->critical_power_setting;

	/* driver only support CAM for non STA network */
	if (priv->iw_mode != IEEE80211_IF_TYPE_STA)
		final_mode = IWL_POWER_MODE_CAM;

	if (!iwl_is_rfkill(priv) && !setting->power_disabled &&
	    ((setting->power_mode != final_mode) || force)) {
		struct iwl_powertable_cmd cmd;

		if (final_mode != IWL_POWER_MODE_CAM)
			set_bit(STATUS_POWER_PMI, &priv->status);

		iwl_update_power_command(priv, &cmd, final_mode);
		cmd.keep_alive_beacons = 0;

		if (final_mode == IWL_POWER_INDEX_5)
			cmd.flags |= IWL_POWER_FAST_PD;

		ret = iwl_set_power(priv, &cmd);

		if (final_mode == IWL_POWER_MODE_CAM)
			clear_bit(STATUS_POWER_PMI, &priv->status);
		else
			set_bit(STATUS_POWER_PMI, &priv->status);

		if (priv->cfg->ops->lib->update_chain_flags)
			priv->cfg->ops->lib->update_chain_flags(priv);

		if (!ret)
			setting->power_mode = final_mode;
	}

	return ret;
}
EXPORT_SYMBOL(iwl_power_update_mode);

/* Allow other iwl code to disable/enable power management active
 * this will be usefull for rate scale to disable PM during heavy
 * Tx/Rx activities
 */
int iwl_power_disable_management(struct iwl_priv *priv, u32 ms)
{
	u16 prev_mode;
	int ret = 0;

	if (priv->power_data.power_disabled)
		return -EBUSY;

	prev_mode = priv->power_data.user_power_setting;
	priv->power_data.user_power_setting = IWL_POWER_MODE_CAM;
	ret = iwl_power_update_mode(priv, 0);
	priv->power_data.power_disabled = 1;
	priv->power_data.user_power_setting = prev_mode;
	cancel_delayed_work(&priv->set_power_save);
	if (ms)
		queue_delayed_work(priv->workqueue, &priv->set_power_save,
				   msecs_to_jiffies(ms));


	return ret;
}
EXPORT_SYMBOL(iwl_power_disable_management);

/* Allow other iwl code to disable/enable power management active
 * this will be usefull for rate scale to disable PM during hight
 * valume activities
 */
int iwl_power_enable_management(struct iwl_priv *priv)
{
	int ret = 0;

	priv->power_data.power_disabled = 0;
	ret = iwl_power_update_mode(priv, 0);
	return ret;
}
EXPORT_SYMBOL(iwl_power_enable_management);

/* set user_power_setting */
int iwl_power_set_user_mode(struct iwl_priv *priv, u16 mode)
{
	if (mode > IWL_POWER_LIMIT)
		return -EINVAL;

	priv->power_data.user_power_setting = mode;

	return iwl_power_update_mode(priv, 0);
}
EXPORT_SYMBOL(iwl_power_set_user_mode);

/* set system_power_setting. This should be set by over all
 * PM application.
 */
int iwl_power_set_system_mode(struct iwl_priv *priv, u16 mode)
{
	if (mode > IWL_POWER_LIMIT)
		return -EINVAL;

	priv->power_data.system_power_setting = mode;

	return iwl_power_update_mode(priv, 0);
}
EXPORT_SYMBOL(iwl_power_set_system_mode);

/* initilize to default */
void iwl_power_initialize(struct iwl_priv *priv)
{

	iwl_power_init_handle(priv);
	priv->power_data.user_power_setting = IWL_POWER_AUTO;
	priv->power_data.power_disabled = 0;
	priv->power_data.system_power_setting = IWL_POWER_SYS_AUTO;
	priv->power_data.is_battery_active = 0;
	priv->power_data.power_disabled = 0;
	priv->power_data.critical_power_setting = 0;
}
EXPORT_SYMBOL(iwl_power_initialize);

/* set critical_power_setting according to temperature value */
int iwl_power_temperature_change(struct iwl_priv *priv)
{
	int ret = 0;
	u16 new_critical = priv->power_data.critical_power_setting;
	s32 temperature = KELVIN_TO_CELSIUS(priv->last_temperature);

	if (temperature > IWL_CT_KILL_TEMPERATURE)
		return 0;
	else if (temperature > IWL_MIN_POWER_TEMPERATURE)
		new_critical = IWL_POWER_INDEX_5;
	else if (temperature > IWL_REDUCED_POWER_TEMPERATURE)
		new_critical = IWL_POWER_INDEX_3;
	else
		new_critical = IWL_POWER_MODE_CAM;

	if (new_critical != priv->power_data.critical_power_setting)
		priv->power_data.critical_power_setting = new_critical;

	if (priv->power_data.critical_power_setting >
				priv->power_data.power_mode)
		ret = iwl_power_update_mode(priv, 0);

	return ret;
}
EXPORT_SYMBOL(iwl_power_temperature_change);

static void iwl_bg_set_power_save(struct work_struct *work)
{
	struct iwl_priv *priv = container_of(work,
				struct iwl_priv, set_power_save.work);
	IWL_DEBUG(IWL_DL_STATE, "update power\n");

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	mutex_lock(&priv->mutex);

	/* on starting association we disable power managment
	 * until association, if association failed then this
	 * timer will expire and enable PM again.
	 */
	if (!iwl_is_associated(priv))
		iwl_power_enable_management(priv);

	mutex_unlock(&priv->mutex);
}
void iwl_setup_power_deferred_work(struct iwl_priv *priv)
{
	INIT_DELAYED_WORK(&priv->set_power_save, iwl_bg_set_power_save);
}
EXPORT_SYMBOL(iwl_setup_power_deferred_work);

void iwl_power_cancel_timeout(struct iwl_priv *priv)
{
	cancel_delayed_work(&priv->set_power_save);
}
EXPORT_SYMBOL(iwl_power_cancel_timeout);
