// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * Purpose: Handles 802.11 power management  functions
 *
 * Author: Lyndon Chen
 *
 * Date: July 17, 2002
 *
 * Functions:
 *      PSvEnablePowerSaving - Enable Power Saving Mode
 *      PSvDiasblePowerSaving - Disable Power Saving Mode
 *      PSbConsiderPowerDown - Decide if we can Power Down
 *      PSvSendPSPOLL - Send PS-POLL packet
 *      PSbSendNullPacket - Send Null packet
 *      PSbIsNextTBTTWakeUp - Decide if we need to wake up at next Beacon
 *
 * Revision History:
 *
 */

#include "mac.h"
#include "device.h"
#include "power.h"
#include "card.h"

/*---------------------  Static Definitions -------------------------*/

/*---------------------  Static Classes  ----------------------------*/

/*---------------------  Static Functions  --------------------------*/

/*---------------------  Export Variables  --------------------------*/

/*---------------------  Export Functions  --------------------------*/

/*
 *
 * Routine Description:
 * Enable hw power saving functions
 *
 * Return Value:
 *    None.
 *
 */

void PSvEnablePowerSaving(struct vnt_private *priv,
			  unsigned short wListenInterval)
{
	u16 wAID = priv->current_aid | BIT(14) | BIT(15);

	/* set period of power up before TBTT */
	iowrite16(C_PWBT, priv->port_offset + MAC_REG_PWBT);
	if (priv->op_mode != NL80211_IFTYPE_ADHOC) {
		/* set AID */
		iowrite16(wAID, priv->port_offset + MAC_REG_AIDATIM);
	}

	/* Set AutoSleep */
	vt6655_mac_reg_bits_on(priv->port_offset, MAC_REG_PSCFG, PSCFG_AUTOSLEEP);

	/* Set HWUTSF */
	vt6655_mac_reg_bits_on(priv->port_offset, MAC_REG_TFTCTL, TFTCTL_HWUTSF);

	if (wListenInterval >= 2) {
		/* clear always listen beacon */
		vt6655_mac_reg_bits_off(priv->port_offset, MAC_REG_PSCTL, PSCTL_ALBCN);
		/* first time set listen next beacon */
		vt6655_mac_reg_bits_on(priv->port_offset, MAC_REG_PSCTL, PSCTL_LNBCN);
	} else {
		/* always listen beacon */
		vt6655_mac_reg_bits_on(priv->port_offset, MAC_REG_PSCTL, PSCTL_ALBCN);
	}

	/* enable power saving hw function */
	vt6655_mac_reg_bits_on(priv->port_offset, MAC_REG_PSCTL, PSCTL_PSEN);
	priv->bEnablePSMode = true;

	priv->bPWBitOn = true;
	pr_debug("PS:Power Saving Mode Enable...\n");
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

void PSvDisablePowerSaving(struct vnt_private *priv)
{
	/* disable power saving hw function */
	MACbPSWakeup(priv);

	/* clear AutoSleep */
	vt6655_mac_reg_bits_off(priv->port_offset, MAC_REG_PSCFG, PSCFG_AUTOSLEEP);

	/* clear HWUTSF */
	vt6655_mac_reg_bits_off(priv->port_offset, MAC_REG_TFTCTL, TFTCTL_HWUTSF);

	/* set always listen beacon */
	vt6655_mac_reg_bits_on(priv->port_offset, MAC_REG_PSCTL, PSCTL_ALBCN);

	priv->bEnablePSMode = false;

	priv->bPWBitOn = false;
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

bool PSbIsNextTBTTWakeUp(struct vnt_private *priv)
{
	struct ieee80211_hw *hw = priv->hw;
	struct ieee80211_conf *conf = &hw->conf;
	bool wake_up = false;

	if (conf->listen_interval > 1) {
		if (!priv->wake_up_count)
			priv->wake_up_count = conf->listen_interval;

		--priv->wake_up_count;

		if (priv->wake_up_count == 1) {
			/* Turn on wake up to listen next beacon */
			vt6655_mac_reg_bits_on(priv->port_offset, MAC_REG_PSCTL, PSCTL_LNBCN);
			wake_up = true;
		}
	}

	return wake_up;
}
