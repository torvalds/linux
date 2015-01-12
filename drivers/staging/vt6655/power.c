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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *
 * File: power.c
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

/*+
 *
 * Routine Description:
 * Enable hw power saving functions
 *
 * Return Value:
 *    None.
 *
 -*/

void
PSvEnablePowerSaving(
	void *hDeviceContext,
	unsigned short wListenInterval
)
{
	struct vnt_private *pDevice = hDeviceContext;
	u16 wAID = pDevice->current_aid | BIT(14) | BIT(15);

	// set period of power up before TBTT
	VNSvOutPortW(pDevice->PortOffset + MAC_REG_PWBT, C_PWBT);
	if (pDevice->op_mode != NL80211_IFTYPE_ADHOC) {
		// set AID
		VNSvOutPortW(pDevice->PortOffset + MAC_REG_AIDATIM, wAID);
	} else {
		// set ATIM Window
#if 0 /* TODO atim window */
		MACvWriteATIMW(pDevice->PortOffset, pMgmt->wCurrATIMWindow);
#endif
	}
	// Set AutoSleep
	MACvRegBitsOn(pDevice->PortOffset, MAC_REG_PSCFG, PSCFG_AUTOSLEEP);
	// Set HWUTSF
	MACvRegBitsOn(pDevice->PortOffset, MAC_REG_TFTCTL, TFTCTL_HWUTSF);

	if (wListenInterval >= 2) {
		// clear always listen beacon
		MACvRegBitsOff(pDevice->PortOffset, MAC_REG_PSCTL, PSCTL_ALBCN);
		// first time set listen next beacon
		MACvRegBitsOn(pDevice->PortOffset, MAC_REG_PSCTL, PSCTL_LNBCN);
	} else {
		// always listen beacon
		MACvRegBitsOn(pDevice->PortOffset, MAC_REG_PSCTL, PSCTL_ALBCN);
	}

	// enable power saving hw function
	MACvRegBitsOn(pDevice->PortOffset, MAC_REG_PSCTL, PSCTL_PSEN);
	pDevice->bEnablePSMode = true;

	pDevice->bPWBitOn = true;
	pr_debug("PS:Power Saving Mode Enable...\n");
}

/*+
 *
 * Routine Description:
 * Disable hw power saving functions
 *
 * Return Value:
 *    None.
 *
 -*/

void
PSvDisablePowerSaving(
	void *hDeviceContext
)
{
	struct vnt_private *pDevice = hDeviceContext;

	// disable power saving hw function
	MACbPSWakeup(pDevice->PortOffset);
	//clear AutoSleep
	MACvRegBitsOff(pDevice->PortOffset, MAC_REG_PSCFG, PSCFG_AUTOSLEEP);
	//clear HWUTSF
	MACvRegBitsOff(pDevice->PortOffset, MAC_REG_TFTCTL, TFTCTL_HWUTSF);
	// set always listen beacon
	MACvRegBitsOn(pDevice->PortOffset, MAC_REG_PSCTL, PSCTL_ALBCN);

	pDevice->bEnablePSMode = false;

	pDevice->bPWBitOn = false;
}


/*+
 *
 * Routine Description:
 * Check if Next TBTT must wake up
 *
 * Return Value:
 *    None.
 *
 -*/

bool
PSbIsNextTBTTWakeUp(
	void *hDeviceContext
)
{
	struct vnt_private *pDevice = hDeviceContext;
	struct ieee80211_hw *hw = pDevice->hw;
	struct ieee80211_conf *conf = &hw->conf;
	bool bWakeUp = false;

	if (conf->listen_interval == 1) {
		/* Turn on wake up to listen next beacon */
		MACvRegBitsOn(pDevice->PortOffset, MAC_REG_PSCTL, PSCTL_LNBCN);
		bWakeUp = true;
	}

	return bWakeUp;
}
