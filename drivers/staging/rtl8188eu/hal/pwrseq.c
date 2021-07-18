// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/

#include "pwrseq.h"
#include <rtl8188e_hal.h>

/* drivers should parse below arrays and do the corresponding actions */

/* 3 Power on  Array */
struct wl_pwr_cfg rtl8188E_power_on_flow[RTL8188E_TRANS_CARDEMU_TO_ACT_STEPS +
					 RTL8188E_TRANS_END_STEPS] = {
	RTL8188E_TRANS_CARDEMU_TO_ACT
	RTL8188E_TRANS_END
};

/* 3Card Disable Array */
struct wl_pwr_cfg rtl8188E_card_disable_flow
	[RTL8188E_TRANS_ACT_TO_CARDEMU_STEPS +
	 RTL8188E_TRANS_CARDEMU_TO_PDN_STEPS +
	 RTL8188E_TRANS_END_STEPS] = {
		RTL8188E_TRANS_ACT_TO_CARDEMU
		RTL8188E_TRANS_CARDEMU_TO_CARDDIS
		RTL8188E_TRANS_END
};

/* 3 Enter LPS */
struct wl_pwr_cfg rtl8188E_enter_lps_flow[RTL8188E_TRANS_ACT_TO_LPS_STEPS +
					  RTL8188E_TRANS_END_STEPS] = {
	/* FW behavior */
	RTL8188E_TRANS_ACT_TO_LPS
	RTL8188E_TRANS_END
};
