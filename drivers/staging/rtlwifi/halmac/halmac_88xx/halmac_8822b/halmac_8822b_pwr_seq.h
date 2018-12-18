/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2016  Realtek Corporation.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/
#ifndef HALMAC_POWER_SEQUENCE_8822B
#define HALMAC_POWER_SEQUENCE_8822B

#include "../../halmac_pwr_seq_cmd.h"

#define HALMAC_8822B_PWR_SEQ_VER "V17"
extern struct halmac_wl_pwr_cfg_ *halmac_8822b_card_disable_flow[];
extern struct halmac_wl_pwr_cfg_ *halmac_8822b_card_enable_flow[];
extern struct halmac_wl_pwr_cfg_ *halmac_8822b_suspend_flow[];
extern struct halmac_wl_pwr_cfg_ *halmac_8822b_resume_flow[];
extern struct halmac_wl_pwr_cfg_ *halmac_8822b_hwpdn_flow[];
extern struct halmac_wl_pwr_cfg_ *halmac_8822b_enter_lps_flow[];
extern struct halmac_wl_pwr_cfg_ *halmac_8822b_enter_deep_lps_flow[];
extern struct halmac_wl_pwr_cfg_ *halmac_8822b_leave_lps_flow[];

#endif
