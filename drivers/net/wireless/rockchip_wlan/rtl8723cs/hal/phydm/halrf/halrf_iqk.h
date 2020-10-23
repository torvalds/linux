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

#ifndef __HALRF_IQK_H__
#define __HALRF_IQK_H__

/*@--------------------------Define Parameters-------------------------------*/
#define LOK_delay 1
#define WBIQK_delay 10
#define TX_IQK 0
#define RX_IQK 1
#define TXIQK 0
#define RXIQK1 1
#define RXIQK2 2
#define kcount_limit_80m 2
#define kcount_limit_others 4
#define rxiqk_gs_limit 6
#define TXWBIQK_EN 1
#define RXWBIQK_EN 1
#if (RTL8814A_SUPPORT == 1 || RTL8198F_SUPPORT == 1 ||\
	RTL8814B_SUPPORT)
#define NUM 4
#elif (RTL8822B_SUPPORT == 1 || RTL8822C_SUPPORT == 1 ||\
	RTL8812F_SUPPORT == 1 ||	RTL8197G_SUPPORT == 1)
#define NUM 2
#else
#define NUM 1
#endif

/*@-----------------------End Define Parameters-----------------------*/

struct dm_dack_info {
	boolean dack_en;
	u16 msbk_d[2][2][15];
	u8 dck_d[2][2][2];
	u16 biask_d[2][2];
};

struct dm_iqk_info {
	boolean lok_fail[NUM];
	boolean iqk_fail[2][NUM];
	u32 iqc_matrix[2][NUM];
	u8 iqk_times;
	u32 rf_reg18;
	u32 rf_reg08;
	u32 lna_idx;
	u8 iqk_step;
	u8 rxiqk_step;
	u8 tmp1bcc;
	u8 txgain;
	u32 txgain56;
	u8 kcount;
	u8 rfk_ing; /*bit0:IQKing, bit1:LCKing, bit2:DPKing*/
	boolean rfk_forbidden;
	u8 rxbb;
	u32 rf_reg58;
	boolean segment_iqk;
	boolean is_tssi_mode;
	u8 iqk_band;
	u8 iqk_ch;
	u8 iqk_bw;
#if (RTL8814A_SUPPORT == 1 || RTL8822B_SUPPORT == 1 || RTL8821C_SUPPORT == 1 ||\
	RTL8195B_SUPPORT == 1 || RTL8198F_SUPPORT == 1 ||\
	RTL8814B_SUPPORT == 1 || RTL8822C_SUPPORT == 1 ||\
	RTL8812F_SUPPORT == 1 || RTL8197G_SUPPORT == 1 ||\
	RTL8710C_SUPPORT == 1 || RTL8723F_SUPPORT == 1)
	u32 iqk_channel[2];
	boolean iqk_fail_report[2][NUM][2]; /*channel/path/TRX(TX:0, RX:1) */
	/*channel / path / TRX(TX:0, RX:1) / CFIR_real*/
	/*channel index = 2 is just for debug*/
#if (RTL8814B_SUPPORT == 1)
	u16 iqk_cfir_real[3][NUM][2][19];
	u16 iqk_cfir_imag[3][NUM][2][19];
#elif (RTL8812F_SUPPORT == 1 || RTL8822C_SUPPORT == 1 )
	u16 iqk_cfir_real[3][2][2][17];
	/*channel / path / TRX(TX:0, RX:1) / CFIR_imag*/
	/*channel index = 2 is just for debug*/
	u16 iqk_cfir_imag[3][2][2][17];
	/*times/path*/
#elif (RTL8195B_SUPPORT == 1)
	u32 iqk_cfir_real[3][NUM][2][9];
	u32 iqk_cfir_imag[3][NUM][2][9];
	/*channel / path / TRX(TX:0, RX:1) / CFIR_imag*/
	/*channel index = 2 is just for debug*/
#else
	u32 iqk_cfir_real[3][NUM][2][8];
	/*channel / path / TRX(TX:0, RX:1) / CFIR_imag*/
	/*channel index = 2 is just for debug*/
	u32 iqk_cfir_imag[3][NUM][2][8];
#endif

#if (RTL8812F_SUPPORT == 1 || RTL8822C_SUPPORT == 1 )
	u32 rx_cfir_real[2][2][17];
	u32 rx_cfir_imag[2][2][17];
	u32 rx_cfir[2][2];
#endif
	u8 retry_count[2][NUM][3]; /* channel / path / (TXK:0, RXK1:1, RXK2:2) */
	u8 gs_retry_count[2][NUM][2]; /* channel / path / (GSRXK1:0, GSRXK2:1) */
	/* channel / path 0:SRXK1 fail, 1:RXK1 fail 2:RXK2 fail */
	u8 rxiqk_fail_code[2][NUM];
	u32 lok_idac[2][NUM]; /*channel / path*/
	u16 rxiqk_agc[2][NUM]; /*channel / path*/
	u32 bypass_iqk[2][NUM]; /*channel / 0xc94/0xe94*/
	u32 txgap_result[8]; /*txagpK result  */
	u32 tmp_gntwl;
	boolean is_btg;
	boolean isbnd;
	boolean is_reload;
	boolean is_hwtx;
	boolean xym_read;
	boolean trximr_enable;
#if (RTL8822B_SUPPORT == 1 || RTL8821C_SUPPORT == 1 ||\
	RTL8814B_SUPPORT == 1 || RTL8822C_SUPPORT == 1)
	u32 rx_xym[2][10];
	u32 tx_xym[2][10];
	u32 gs1_xym[2][6];
	u32 gs2_xym[2][6];
	u32 rxk1_xym[2][6];
	u32 nbtxk_1b38[2];
	u32 nbrxk_1b3c[2];
#endif
#if (RTL8710C_SUPPORT == 1 || RTL8197G_SUPPORT == 1 )
	u32 txxy[2][2];
	u32 rxxy[2][2];
#endif
#endif
};

#endif /*__HALRF_IQK_H__*/
