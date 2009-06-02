/******************************************************************************
 *
 * Copyright(c) 2003 - 2009 Intel Corporation. All rights reserved.
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
static int iwl_rfkill_soft_rf_kill(void *data, bool blocked)
{
	struct iwl_priv *priv = data;

	if (!priv->rfkill)
		return -EINVAL;

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return 0;

	IWL_DEBUG_RF_KILL(priv, "received soft RFKILL: block=%d\n", blocked);

	mutex_lock(&priv->mutex);

	if (iwl_is_rfkill_hw(priv))
		goto out_unlock;

	if (!blocked)
		iwl_radio_kill_sw_enable_radio(priv);
	else
		iwl_radio_kill_sw_disable_radio(priv);

out_unlock:
	mutex_unlock(&priv->mutex);
	return 0;
}

static const struct rfkill_ops iwl_rfkill_ops = {
	.set_block = iwl_rfkill_soft_rf_kill,
};

int iwl_rfkill_init(struct iwl_priv *priv)
{
	struct device *device = wiphy_dev(priv->hw->wiphy);
	int ret = 0;

	BUG_ON(device == NULL);

	IWL_DEBUG_RF_KILL(priv, "Initializing RFKILL.\n");
	priv->rfkill = rfkill_alloc(priv->cfg->name,
				    device,
				    RFKILL_TYPE_WLAN,
				    &iwl_rfkill_ops, priv);
	if (!priv->rfkill) {
		IWL_ERR(priv, "Unable to allocate RFKILL device.\n");
		ret = -ENOMEM;
		goto error;
	}

	ret = rfkill_register(priv->rfkill);
	if (ret) {
		IWL_ERR(priv, "Unable to register RFKILL: %d\n", ret);
		goto free_rfkill;
	}

	IWL_DEBUG_RF_KILL(priv, "RFKILL initialization complete.\n");
	return 0;

free_rfkill:
	rfkill_destroy(priv->rfkill);
	priv->rfkill = NULL;

error:
	IWL_DEBUG_RF_KILL(priv, "RFKILL initialization complete.\n");
	return ret;
}
EXPORT_SYMBOL(iwl_rfkill_init);

void iwl_rfkill_unregister(struct iwl_priv *priv)
{

	if (priv->rfkill) {
		rfkill_unregister(priv->rfkill);
		rfkill_destroy(priv->rfkill);
	}

	priv->rfkill = NULL;
}
EXPORT_SYMBOL(iwl_rfkill_unregister);

/* set RFKILL to the right state. */
void iwl_rfkill_set_hw_state(struct iwl_priv *priv)
{
	if (!priv->rfkill)
		return;

	if (rfkill_set_hw_state(priv->rfkill,
				!!iwl_is_rfkill_hw(priv)))
		iwl_radio_kill_sw_disable_radio(priv);
	else
		iwl_radio_kill_sw_enable_radio(priv);
}
EXPORT_SYMBOL(iwl_rfkill_set_hw_state);
