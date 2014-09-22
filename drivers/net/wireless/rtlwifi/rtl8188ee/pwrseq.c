/******************************************************************************
 *
 * Copyright(c) 2009-2013  Realtek Corporation.
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
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#include "pwrseqcmd.h"
#include "pwrseq.h"

/* drivers should parse below arrays and do the corresponding actions */
/*3 Power on  Array*/
struct wlan_pwr_cfg rtl8188E_power_on_flow[RTL8188E_TRANS_CARDEMU_TO_ACT_STEPS
					+ RTL8188E_TRANS_END_STEPS] = {
	RTL8188E_TRANS_CARDEMU_TO_ACT
	RTL8188E_TRANS_END
};

/*3Radio off GPIO Array */
struct wlan_pwr_cfg rtl8188E_radio_off_flow[RTL8188E_TRANS_ACT_TO_CARDEMU_STEPS
					+ RTL8188E_TRANS_END_STEPS] = {
	RTL8188E_TRANS_ACT_TO_CARDEMU
	RTL8188E_TRANS_END
};

/*3Card Disable Array*/
struct wlan_pwr_cfg rtl8188E_card_disable_flow
		[RTL8188E_TRANS_ACT_TO_CARDEMU_STEPS +
		 RTL8188E_TRANS_CARDEMU_TO_PDN_STEPS +
		 RTL8188E_TRANS_END_STEPS] = {
	RTL8188E_TRANS_ACT_TO_CARDEMU
	RTL8188E_TRANS_CARDEMU_TO_CARDDIS
	RTL8188E_TRANS_END
};

/*3 Card Enable Array*/
struct wlan_pwr_cfg rtl8188E_card_enable_flow
		[RTL8188E_TRANS_ACT_TO_CARDEMU_STEPS +
		 RTL8188E_TRANS_CARDEMU_TO_PDN_STEPS +
		 RTL8188E_TRANS_END_STEPS] = {
	RTL8188E_TRANS_CARDDIS_TO_CARDEMU
	RTL8188E_TRANS_CARDEMU_TO_ACT
	RTL8188E_TRANS_END
};

/*3Suspend Array*/
struct wlan_pwr_cfg rtl8188E_suspend_flow[RTL8188E_TRANS_ACT_TO_CARDEMU_STEPS
					+ RTL8188E_TRANS_CARDEMU_TO_SUS_STEPS
					+ RTL8188E_TRANS_END_STEPS] = {
	RTL8188E_TRANS_ACT_TO_CARDEMU
	RTL8188E_TRANS_CARDEMU_TO_SUS
	RTL8188E_TRANS_END
};

/*3 Resume Array*/
struct wlan_pwr_cfg rtl8188E_resume_flow[RTL8188E_TRANS_ACT_TO_CARDEMU_STEPS
					+ RTL8188E_TRANS_CARDEMU_TO_SUS_STEPS
					+ RTL8188E_TRANS_END_STEPS] = {
	RTL8188E_TRANS_SUS_TO_CARDEMU
	RTL8188E_TRANS_CARDEMU_TO_ACT
	RTL8188E_TRANS_END
};

/*3HWPDN Array*/
struct wlan_pwr_cfg rtl8188E_hwpdn_flow[RTL8188E_TRANS_ACT_TO_CARDEMU_STEPS
				+ RTL8188E_TRANS_CARDEMU_TO_PDN_STEPS
				+ RTL8188E_TRANS_END_STEPS] = {
	RTL8188E_TRANS_ACT_TO_CARDEMU
	RTL8188E_TRANS_CARDEMU_TO_PDN
	RTL8188E_TRANS_END
};

/*3 Enter LPS */
struct wlan_pwr_cfg rtl8188E_enter_lps_flow[RTL8188E_TRANS_ACT_TO_LPS_STEPS
					+ RTL8188E_TRANS_END_STEPS] = {
	/*FW behavior*/
	RTL8188E_TRANS_ACT_TO_LPS
	RTL8188E_TRANS_END
};

/*3 Leave LPS */
struct wlan_pwr_cfg rtl8188E_leave_lps_flow[RTL8188E_TRANS_LPS_TO_ACT_STEPS
					+ RTL8188E_TRANS_END_STEPS] = {
	/*FW behavior*/
	RTL8188E_TRANS_LPS_TO_ACT
	RTL8188E_TRANS_END
};
