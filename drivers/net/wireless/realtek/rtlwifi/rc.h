/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#ifndef __RTL_RC_H__
#define __RTL_RC_H__

#define B_MODE_MAX_RIX 3
#define G_MODE_MAX_RIX 11
#define A_MODE_MAX_RIX 7

/* in mac80211 mcs0-mcs15 is idx0-idx15*/
#define N_MODE_MCS7_RIX 7
#define N_MODE_MCS15_RIX 15

#define AC_MODE_MCS7_RIX 7
#define AC_MODE_MCS8_RIX 8
#define AC_MODE_MCS9_RIX 9

struct rtl_rate_priv {
	u8 ht_cap;
};

int rtl_rate_control_register(void);
void rtl_rate_control_unregister(void);

#endif
