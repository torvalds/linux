/******************************************************************************
 *
 * Copyright(c) 2003 - 2008 Intel Corporation. All rights reserved.
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

/* software rf-kill from user */
static int iwl_rfkill_soft_rf_kill(void *data, enum rfkill_state state)
{
	struct iwl_priv *priv = data;
	int err = 0;

	if (!priv->rfkill)
		return 0;

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return 0;

	IWL_DEBUG_RF_KILL("we received soft RFKILL set to state %d\n", state);
	mutex_lock(&priv->mutex);

	switch (state) {
	case RFKILL_STATE_UNBLOCKED:
		if (iwl_is_rfkill_hw(priv)) {
			err = -EBUSY;
			goto out_unlock;
		}
		iwl_radio_kill_sw_enable_radio(priv);
		break;
	case RFKILL_STATE_SOFT_BLOCKED:
		iwl_radio_kill_sw_disable_radio(priv);
		break;
	default:
		IWL_WARNING("we received unexpected RFKILL state %d\n", state);
		break;
	}
out_unlock:
	mutex_unlock(&priv->mutex);

	return err;
}

int iwl_rfkill_init(struct iwl_priv *priv)
{
	struct device *device = wiphy_dev(priv->hw->wiphy);
	int ret = 0;

	BUG_ON(device == NULL);

	IWL_DEBUG_RF_KILL("Initializing RFKILL.\n");
	priv->rfkill = rfkill_allocate(device, RFKILL_TYPE_WLAN);
	if (!priv->rfkill) {
		IWL_ERROR("Unable to allocate RFKILL device.\n");
		ret = -ENOMEM;
		goto error;
	}

	priv->rfkill->name = priv->cfg->name;
	priv->rfkill->data = priv;
	priv->rfkill->state = RFKILL_STATE_UNBLOCKED;
	priv->rfkill->toggle_radio = iwl_rfkill_soft_rf_kill;
	priv->rfkill->user_claim_unsupported = 1;

	priv->rfkill->dev.class->suspend = NULL;
	priv->rfkill->dev.class->resume = NULL;

	ret = rfkill_register(priv->rfkill);
	if (ret) {
		IWL_ERROR("Unable to register RFKILL: %d\n", ret);
		goto free_rfkill;
	}

	IWL_DEBUG_RF_KILL("RFKILL initialization complete.\n");
	return ret;

free_rfkill:
	if (priv->rfkill != NULL)
		rfkill_free(priv->rfkill);
	priv->rfkill = NULL;

error:
	IWL_DEBUG_RF_KILL("RFKILL initialization complete.\n");
	return ret;
}
EXPORT_SYMBOL(iwl_rfkill_init);

void iwl_rfkill_unregister(struct iwl_priv *priv)
{

	if (priv->rfkill)
		rfkill_unregister(priv->rfkill);

	priv->rfkill = NULL;
}
EXPORT_SYMBOL(iwl_rfkill_unregister);

/* set RFKILL to the right state. */
void iwl_rfkill_set_hw_state(struct iwl_priv *priv)
{
	if (!priv->rfkill)
		return;

	if (iwl_is_rfkill_hw(priv)) {
		rfkill_force_state(priv->rfkill, RFKILL_STATE_HARD_BLOCKED);
		return;
	}

	if (!iwl_is_rfkill_sw(priv))
		rfkill_force_state(priv->rfkill, RFKILL_STATE_UNBLOCKED);
	else
		rfkill_force_state(priv->rfkill, RFKILL_STATE_SOFT_BLOCKED);
}
EXPORT_SYMBOL(iwl_rfkill_set_hw_state);
