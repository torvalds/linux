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

#ifndef __PHYDM_PMAC_TX_SETTING_H__
#define __PHYDM_PMAC_TX_SETTING_H__

/* 2019.06.10 Modify STBC setting for different OS*/
#define PMAC_TX_SETTING_VERSION "1.6"

/* 1 ============================================================
 * 1  Definition
 * 1 ============================================================
 */
#define RANDOM_BY_PN32 0x12
/* 1 ============================================================
 * 1  structure
 * 1 ============================================================
 */
struct phydm_pmac_info {
	u8 en_pmac_tx:1; /*0: disable pmac 1: enable pmac */
	u8 mode:3; /*0: Packet TX 3:Continuous TX */
	/* u8 Ntx:4; */
	u8 tx_rate; /* @should be HW rate*/
	/* u8 TX_RATE_HEX; */
	u8 tx_sc;
	/* u8 bSGI:1; */
	u8 is_short_preamble:1;
	/* u8 bSTBC:1; */
	/* u8 bLDPC:1; */
	u8 ndp_sound:1;
	u8 bw:3; /* 0:20 1:40 2:80Mhz */
	u8 m_stbc; /* bSTBC + 1 for WIN/CE, bSTBC for others*/
	u16 packet_period;
	u32 packet_count;
	/* u32 PacketLength; */
	u8 packet_pattern;
	u16 sfd;
	u8 signal_field;
	u8 service_field;
	u16 length;
	u8 crc16[2];
	u8 lsig[3];
	u8 ht_sig[6];
	u8 vht_sig_a[6];
	u8 vht_sig_b[4];
	u8 vht_sig_b_crc;
	u8 vht_delimiter[4];
	/* u8 mac_addr[6]; */
};

struct phydm_pmac_tx {
	boolean is_cck_rate;
	boolean is_ofdm_rate;
	boolean is_ht_rate;
	boolean is_vht_rate;
	boolean cck_cont_tx;
	boolean ofdm_cont_tx;
	u8 path;
	u32 tx_scailing;
};

/* 1 ============================================================
 * 1  enumeration
 * 1 ============================================================
 */

enum phydm_pmac_mode {
	NONE_TEST,
	PKTS_TX,
	PKTS_RX,
	CONT_TX,
	OFDM_SINGLE_TONE_TX,
	CCK_CARRIER_SIPPRESSION_TX
};

/* 1 ============================================================
 * 1  function prototype
 * 1 ============================================================
 */
void phydm_start_cck_cont_tx(void *dm_void, struct phydm_pmac_info *tx_info);

void phydm_stop_cck_cont_tx(void *dm_void);

void phydm_start_ofdm_cont_tx(void *dm_void);

void phydm_stop_ofdm_cont_tx(void *dm_void);

void phydm_set_single_tone(void *dm_void, boolean is_single_tone,
			   boolean en_pmac_tx, u8 path);

void phydm_set_pmac_tx(void *dm_void, struct phydm_pmac_info *tx_info,
		       enum rf_path mpt_rf_path);

void phydm_set_tmac_tx(void *dm_void);

#endif
