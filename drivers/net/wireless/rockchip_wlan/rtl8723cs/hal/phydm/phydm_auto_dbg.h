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

#ifndef __PHYDM_AUTO_DBG_H__
#define __PHYDM_AUTO_DBG_H__

#define AUTO_DBG_VERSION "1.0" /* @2017.05.015  Dino, Add phydm_auto_dbg.h*/

/* @1 ============================================================
 * 1  Definition
 * 1 ============================================================
 */

#define AUTO_CHK_HANG_STEP_MAX 3
#define DBGPORT_CHK_NUM 6

#ifdef PHYDM_AUTO_DEGBUG

/* @1 ============================================================
 * 1  enumeration
 * 1 ============================================================
 */

enum auto_dbg_type_e {
	AUTO_DBG_STOP		= 0,
	AUTO_DBG_CHECK_HANG	= 1,
	AUTO_DBG_CHECK_RA	= 2,
	AUTO_DBG_CHECK_DIG	= 3
};

/* @1 ============================================================
 * 1  structure
 * 1 ============================================================
 */

struct n_dbgport_803 {
	/*@BYTE 3*/
	u8 bb_rst_b : 1;
	u8 glb_rst_b : 1;
	u8 zero_1bit_1 : 1;
	u8 ofdm_rst_b : 1;
	u8 cck_txpe : 1;
	u8 ofdm_txpe : 1;
	u8 phy_tx_on : 1;
	u8 tdrdy : 1;
	/*@BYTE 2*/
	u8 txd : 8;
	/*@BYTE 1*/
	u8 cck_cca_pp : 1;
	u8 ofdm_cca_pp : 1;
	u8 rx_rst : 1;
	u8 rdrdy : 1;
	u8 rxd_7_4 : 4;
	/*@BYTE 0*/
	u8 rxd_3_0 : 4;
	u8 ofdm_tx_en : 1;
	u8 cck_tx_en : 1;
	u8 zero_1bit_2 : 1;
	u8 clk_80m : 1;
};

struct phydm_auto_dbg_struct {
	enum auto_dbg_type_e auto_dbg_type;
	u8 dbg_step;
	u16 dbg_port_table[DBGPORT_CHK_NUM];
	u32 dbg_port_val[DBGPORT_CHK_NUM];
	u16 ofdm_t_cnt;
	u16 ofdm_r_cnt;
	u16 cck_t_cnt;
	u16 cck_r_cnt;
	u16 ofdm_crc_error_cnt;
	u16 cck_crc_error_cnt;
};

/* @1 ============================================================
 * 1  function prototype
 * 1 ============================================================
 */

void phydm_dbg_port_dump(void *dm_void, u32 *used, char *output, u32 *out_len);

void phydm_auto_dbg_console(
	void *dm_void,
	char input[][16],
	u32 *_used,
	char *output,
	u32 *_out_len);

void phydm_auto_dbg_engine(void *dm_void);

void phydm_auto_dbg_engine_init(void *dm_void);
#endif
#endif
