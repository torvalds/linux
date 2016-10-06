/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 * File: power.c
 *
 * Purpose: Handles 802.11 power management functions
 *
 * Author: Lyndon Chen
 *
 * Date: July 17, 2002
 *
 * Functions:
 *      vnt_enable_power_saving - Enable Power Saving Mode
 *      PSvDiasblePowerSaving - Disable Power Saving Mode
 *      vnt_next_tbtt_wakeup - Decide if we need to wake up at next Beacon
 *
 * Revision History:
 *
 */

#include "mac.h"
#include "device.h"
#include "power.h"
#include "wcmd.h"
#include "rxtx.h"
#include "card.h"
#include "usbpipe.h"

/*
 *
 * Routine Description:
 * Enable hw power saving functions
 *
 * Return Value:
 *    None.
 *
 */

void vnt_enable_power_saving(struct vnt_private *priv, u16 listen_interval)
{
	u16 aid = priv->current_aid | BIT(14) | BIT(15);

	/* set period of power up before TBTT */
	vnt_mac_write_word(priv, MAC_REG_PWBT, C_PWBT);

	if (priv->op_mode != NL80211_IFTYPE_ADHOC)
		/* set AID */
		vnt_mac_write_word(priv, MAC_REG_AIDATIM, aid);

	/* Warren:06-18-2004,the sequence must follow
	 * PSEN->AUTOSLEEP->GO2DOZE
	 */
	/* enable power saving hw function */
	vnt_mac_reg_bits_on(priv, MAC_REG_PSCTL, PSCTL_PSEN);

	/* Set AutoSleep */
	vnt_mac_reg_bits_on(priv, MAC_REG_PSCFG, PSCFG_AUTOSLEEP);

	/* Warren:MUST turn on this once before turn on AUTOSLEEP ,or the
	 * AUTOSLEEP doesn't work
	 */
	vnt_mac_reg_bits_on(priv, MAC_REG_PSCTL, PSCTL_GO2DOZE);

	if (listen_interval >= 2) {

		/* clear always listen beacon */
		vnt_mac_reg_bits_off(priv, MAC_REG_PSCTL, PSCTL_ALBCN);

		/* first time set listen next beacon */
		vnt_mac_reg_bits_on(priv, MAC_REG_PSCTL, PSCTL_LNBCN);
	} else

		/* always listen beacon */
		vnt_mac_reg_bits_on(priv, MAC_REG_PSCTL, PSCTL_ALBCN);

	dev_dbg(&priv->usb->dev,  "PS:Power Saving Mode Enable...\n");
}

/*
 *
 * Routine Description:
 * Disable hw power saving functions
 *
 * Return Value:
 *    None.
 *
 */

void vnt_disable_power_saving(struct vnt_private *priv)
{

	/* disable power saving hw function */
	vnt_control_out(priv, MESSAGE_TYPE_DISABLE_PS, 0,
			0, 0, NULL);

	/* clear AutoSleep */
	vnt_mac_reg_bits_off(priv, MAC_REG_PSCFG, PSCFG_AUTOSLEEP);

	/* set always listen beacon */
	vnt_mac_reg_bits_on(priv, MAC_REG_PSCTL, PSCTL_ALBCN);
}

/*
 *
 * Routine Description:
 * Check if Next TBTT must wake up
 *
 * Return Value:
 *    None.
 *
 */

int vnt_next_tbtt_wakeup(struct vnt_private *priv)
{
	struct ieee80211_hw *hw = priv->hw;
	struct ieee80211_conf *conf = &hw->conf;
	int wake_up = false;

	if (conf->listen_interval > 1) {
		/* Turn on wake up to listen next beacon */
		vnt_mac_reg_bits_on(priv, MAC_REG_PSCTL, PSCTL_LNBCN);
		wake_up = true;
	}

	return wake_up;
}
