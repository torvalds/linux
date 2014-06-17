/******************************************************************************
 *
 * Copyright(c) 2009-2010  Realtek Corporation.
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
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#ifndef __RTL_RC_H__
#define __RTL_RC_H__

#define B_MODE_MAX_RIX 3
#define G_MODE_MAX_RIX 11
#define A_MODE_MAX_RIX 7

/* in mac80211 mcs0-mcs15 is idx0-idx15*/
#define N_MODE_MCS7_RIX 7
#define N_MODE_MCS15_RIX 15

struct rtl_rate_priv {
	u8 ht_cap;
};

int rtl_rate_control_register(void);
void rtl_rate_control_unregister(void);
#endif
