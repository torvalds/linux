/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2017  Realtek Corporation.
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

#ifndef __PHYDM_CCK_RX_PATHDIV_H__
#define __PHYDM_CCK_RX_PATHDIV_H__

#define CCK_RX_PATHDIV_VERSION "1.1"

/* @1 ============================================================
 * 1  Definition
 * 1 ============================================================
 */
/* @1 ============================================================
 * 1  structure
 * 1 ============================================================
 */
struct phydm_cck_rx_pathdiv {
	boolean en_cck_rx_pathdiv;
	u32	path_a_sum;
	u32	path_b_sum;
	u16	path_a_cnt;
	u16	path_b_cnt;
	u8	rssi_fa_th;
	u8	rssi_th;
};

/* @1 ============================================================
 * 1  enumeration
 * 1 ============================================================
 */

/* @1 ============================================================
 * 1  function prototype
 * 1 ============================================================
 */
void phydm_cck_rx_pathdiv_watchdog(void *dm_void);

void phydm_cck_rx_pathdiv_init(void *dm_void);

void phydm_process_rssi_for_cck_rx_pathdiv(void *dm_void, void *phy_info_void,
					   void *pkt_info_void);

void phydm_cck_rx_pathdiv_dbg(void *dm_void, char input[][16], u32 *_used,
			      char *output, u32 *_out_len);
#endif
