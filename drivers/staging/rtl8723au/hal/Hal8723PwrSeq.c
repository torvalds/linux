/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 ******************************************************************************/

#include "Hal8723PwrSeq.h"

/*
    drivers should parse below arrays and do the corresponding actions
*/
/* 3 Power on  Array */
struct wlan_pwr_cfg rtl8723AU_power_on_flow[RTL8723A_TRANS_CARDEMU_TO_ACT_STEPS+RTL8723A_TRANS_END_STEPS] = {
	RTL8723A_TRANS_CARDEMU_TO_ACT
	RTL8723A_TRANS_END
};

/* 3 Radio off GPIO Array */
struct wlan_pwr_cfg rtl8723AU_radio_off_flow[RTL8723A_TRANS_ACT_TO_CARDEMU_STEPS+RTL8723A_TRANS_END_STEPS] = {
	RTL8723A_TRANS_ACT_TO_CARDEMU
	RTL8723A_TRANS_END
};

/* 3 Card Disable Array */
struct wlan_pwr_cfg rtl8723AU_card_disable_flow[RTL8723A_TRANS_ACT_TO_CARDEMU_STEPS+RTL8723A_TRANS_CARDEMU_TO_PDN_STEPS+RTL8723A_TRANS_END_STEPS] = {
	RTL8723A_TRANS_ACT_TO_CARDEMU
	RTL8723A_TRANS_CARDEMU_TO_CARDDIS
	RTL8723A_TRANS_END
};

/* 3 Card Enable Array */
struct wlan_pwr_cfg rtl8723AU_card_enable_flow[RTL8723A_TRANS_ACT_TO_CARDEMU_STEPS+RTL8723A_TRANS_CARDEMU_TO_PDN_STEPS+RTL8723A_TRANS_END_STEPS] = {
	RTL8723A_TRANS_CARDDIS_TO_CARDEMU
	RTL8723A_TRANS_CARDEMU_TO_ACT
	RTL8723A_TRANS_END
};

/* 3 Suspend Array */
struct wlan_pwr_cfg rtl8723AU_suspend_flow[RTL8723A_TRANS_ACT_TO_CARDEMU_STEPS+RTL8723A_TRANS_CARDEMU_TO_SUS_STEPS+RTL8723A_TRANS_END_STEPS] = {
	RTL8723A_TRANS_ACT_TO_CARDEMU
	RTL8723A_TRANS_CARDEMU_TO_SUS
	RTL8723A_TRANS_END
};

/* 3 Resume Array */
struct wlan_pwr_cfg rtl8723AU_resume_flow[RTL8723A_TRANS_ACT_TO_CARDEMU_STEPS+RTL8723A_TRANS_CARDEMU_TO_SUS_STEPS+RTL8723A_TRANS_END_STEPS] = {
	RTL8723A_TRANS_SUS_TO_CARDEMU
	RTL8723A_TRANS_CARDEMU_TO_ACT
	RTL8723A_TRANS_END
};

/* 3 HWPDN Array */
struct wlan_pwr_cfg rtl8723AU_hwpdn_flow[RTL8723A_TRANS_ACT_TO_CARDEMU_STEPS+RTL8723A_TRANS_CARDEMU_TO_PDN_STEPS+RTL8723A_TRANS_END_STEPS] = {
	RTL8723A_TRANS_ACT_TO_CARDEMU
	RTL8723A_TRANS_CARDEMU_TO_PDN
	RTL8723A_TRANS_END
};

/* 3 Enter LPS */
struct wlan_pwr_cfg rtl8723AU_enter_lps_flow[RTL8723A_TRANS_ACT_TO_LPS_STEPS+RTL8723A_TRANS_END_STEPS] = {
	/* FW behavior */
	RTL8723A_TRANS_ACT_TO_LPS
	RTL8723A_TRANS_END
};

/* 3 Leave LPS */
struct wlan_pwr_cfg rtl8723AU_leave_lps_flow[RTL8723A_TRANS_LPS_TO_ACT_STEPS+RTL8723A_TRANS_END_STEPS] = {
	/* FW behavior */
	RTL8723A_TRANS_LPS_TO_ACT
	RTL8723A_TRANS_END
};
