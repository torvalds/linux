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
 * James P. Ketrenos <ipw2100-admin@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *****************************************************************************/
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/init.h>

#include <net/mac80211.h>

#include "iwl-eeprom.h"
#include "iwl-4965.h"
#include "iwl-core.h"
#include "iwl-helpers.h"


/* software rf-kill from user */
static int iwl_rfkill_soft_rf_kill(void *data, enum rfkill_state state)
{
	struct iwl_priv *priv = data;
	int err = 0;

	if (!priv->rfkill_mngr.rfkill)
		return 0;

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return 0;

	IWL_DEBUG_RF_KILL("we recieved soft RFKILL set to state %d\n", state);
	mutex_lock(&priv->mutex);

	switch (state) {
	case RFKILL_STATE_ON:
		priv->cfg->ops->lib->radio_kill_sw(priv, 0);
		/* if HW rf-kill is set dont allow ON state */
		if (iwl_is_rfkill(priv))
			err = -EBUSY;
		break;
	case RFKILL_STATE_OFF:
		priv->cfg->ops->lib->radio_kill_sw(priv, 1);
		if (!iwl_is_rfkill(priv))
			err = -EBUSY;
		break;
	}
	mutex_unlock(&priv->mutex);

	return err;
}

int iwl_rfkill_init(struct iwl_priv *priv)
{
	struct device *device = wiphy_dev(priv->hw->wiphy);
	int ret = 0;

	BUG_ON(device == NULL);

	IWL_DEBUG_RF_KILL("Initializing RFKILL.\n");
	priv->rfkill_mngr.rfkill = rfkill_allocate(device, RFKILL_TYPE_WLAN);
	if (!priv->rfkill_mngr.rfkill) {
		IWL_ERROR("Unable to allocate rfkill device.\n");
		ret = -ENOMEM;
		goto error;
	}

	priv->rfkill_mngr.rfkill->name = priv->cfg->name;
	priv->rfkill_mngr.rfkill->data = priv;
	priv->rfkill_mngr.rfkill->state = RFKILL_STATE_ON;
	priv->rfkill_mngr.rfkill->toggle_radio = iwl_rfkill_soft_rf_kill;
	priv->rfkill_mngr.rfkill->user_claim_unsupported = 1;

	priv->rfkill_mngr.rfkill->dev.class->suspend = NULL;
	priv->rfkill_mngr.rfkill->dev.class->resume = NULL;

	priv->rfkill_mngr.input_dev = input_allocate_device();
	if (!priv->rfkill_mngr.input_dev) {
		IWL_ERROR("Unable to allocate rfkill input device.\n");
		ret = -ENOMEM;
		goto freed_rfkill;
	}

	priv->rfkill_mngr.input_dev->name = priv->cfg->name;
	priv->rfkill_mngr.input_dev->phys = wiphy_name(priv->hw->wiphy);
	priv->rfkill_mngr.input_dev->id.bustype = BUS_HOST;
	priv->rfkill_mngr.input_dev->id.vendor = priv->pci_dev->vendor;
	priv->rfkill_mngr.input_dev->dev.parent = device;
	priv->rfkill_mngr.input_dev->evbit[0] = BIT(EV_KEY);
	set_bit(KEY_WLAN, priv->rfkill_mngr.input_dev->keybit);

	ret = rfkill_register(priv->rfkill_mngr.rfkill);
	if (ret) {
		IWL_ERROR("Unable to register rfkill: %d\n", ret);
		goto free_input_dev;
	}

	ret = input_register_device(priv->rfkill_mngr.input_dev);
	if (ret) {
		IWL_ERROR("Unable to register rfkill input device: %d\n", ret);
		goto unregister_rfkill;
	}

	IWL_DEBUG_RF_KILL("RFKILL initialization complete.\n");
	return ret;

unregister_rfkill:
	rfkill_unregister(priv->rfkill_mngr.rfkill);
	priv->rfkill_mngr.rfkill = NULL;

free_input_dev:
	input_free_device(priv->rfkill_mngr.input_dev);
	priv->rfkill_mngr.input_dev = NULL;

freed_rfkill:
	if (priv->rfkill_mngr.rfkill != NULL)
		rfkill_free(priv->rfkill_mngr.rfkill);
	priv->rfkill_mngr.rfkill = NULL;

error:
	IWL_DEBUG_RF_KILL("RFKILL initialization complete.\n");
	return ret;
}
EXPORT_SYMBOL(iwl_rfkill_init);

void iwl_rfkill_unregister(struct iwl_priv *priv)
{

	if (priv->rfkill_mngr.input_dev)
		input_unregister_device(priv->rfkill_mngr.input_dev);

	if (priv->rfkill_mngr.rfkill)
		rfkill_unregister(priv->rfkill_mngr.rfkill);

	priv->rfkill_mngr.input_dev = NULL;
	priv->rfkill_mngr.rfkill = NULL;
}
EXPORT_SYMBOL(iwl_rfkill_unregister);

/* set rf-kill to the right state. */
void iwl_rfkill_set_hw_state(struct iwl_priv *priv)
{

	if (!priv->rfkill_mngr.rfkill)
		return;

	if (!iwl_is_rfkill(priv))
		priv->rfkill_mngr.rfkill->state = RFKILL_STATE_ON;
	else
		priv->rfkill_mngr.rfkill->state = RFKILL_STATE_OFF;
}
EXPORT_SYMBOL(iwl_rfkill_set_hw_state);
