/******************************************************************************
 *
 * Copyright(c) 2007 - 2016  Realtek Corporation.
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

#ifndef __PHYDMIQK_H__
#define __PHYDMIQK_H__

/*--------------------------Define Parameters-------------------------------*/
#define LOK_delay 1
#define WBIQK_delay 10
#define TX_IQK 0
#define RX_IQK 1
#define TXIQK 0
#define RXIQK1 1
#define RXIQK2 2
#define GSRXK1 0
#define GSRXK2 1
#define kcount_limit_80m 2
#define kcount_limit_others 4
#define rxiqk_gs_limit 4

#define NUM 4
/*----------------------End Define Parameters-------------------------------*/

struct dm_iqk_info {
	bool lok_fail[NUM];
	bool iqk_fail[2][NUM];
	u32 iqc_matrix[2][NUM];
	u8 iqk_times;
	u32 rf_reg18;
	u32 lna_idx;
	u8 rxiqk_step;
	u8 tmp1bcc;
	u8 kcount;

	u32 iqk_channel[2];
	bool iqk_fail_report[2][4][2]; /*channel/path/TRX(TX:0, RX:1) */
	u32 iqk_cfir_real[2][4][2]
			 [8]; /*channel / path / TRX(TX:0, RX:1) / CFIR_real*/
	u32 iqk_cfir_imag[2][4][2]
			 [8]; /*channel / path / TRX(TX:0, RX:1) / CFIR_imag*/
	u8 retry_count[2][4][3]; /* channel / path / (TXK:0, RXK1:1, RXK2:2) */
	u8 gs_retry_count[2][4][2]; /* channel / path / (GSRXK1:0, GSRXK2:1) */
	u8 rxiqk_fail_code[2][4]; /* channel / path
				   * 0:SRXK1 fail, 1:RXK1 fail 2:RXK2 fail
				   */
	u32 lok_idac[2][4]; /*channel / path*/
	u16 rxiqk_agc[2][4]; /*channel / path*/
	u32 bypass_iqk[2][4]; /*channel / 0xc94/0xe94*/
	u32 tmp_gntwl;
	bool is_btg;
	bool isbnd;
};

#endif
