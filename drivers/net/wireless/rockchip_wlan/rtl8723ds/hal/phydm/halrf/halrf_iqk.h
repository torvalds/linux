/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
 *****************************************************************************/

#ifndef	__PHYDMIQK_H__
#define __PHYDMIQK_H__

/*--------------------------Define Parameters-------------------------------*/
#define	LOK_delay 1
#define	WBIQK_delay 10
#define	TX_IQK 0
#define	RX_IQK 1
#define	TXIQK 0
#define	RXIQK1 1
#define	RXIQK2 2
#define kcount_limit_80m 2
#define kcount_limit_others 4
#define rxiqk_gs_limit 4

#define	NUM 4
/*---------------------------End Define Parameters-------------------------------*/

struct _IQK_INFORMATION {
	boolean		LOK_fail[NUM];
	boolean		IQK_fail[2][NUM];
	u32		iqc_matrix[2][NUM];
	u8      iqk_times;
	u32		rf_reg18;
	u32		lna_idx;
	u8		rxiqk_step;
	u8		tmp1bcc;
	u8		kcount;
	u8		rfk_ing; /*bit0:IQKing, bit1:LCKing, bit2:DPKing*/
	boolean rfk_forbidden;	
#if (RTL8814A_SUPPORT == 1 || RTL8822B_SUPPORT == 1 || RTL8821C_SUPPORT == 1)
	u32		iqk_channel[2];
	boolean		IQK_fail_report[2][4][2]; /*channel/path/TRX(TX:0, RX:1) */
	u32		IQK_CFIR_real[3][4][2][8]; /*channel / path / TRX(TX:0, RX:1) / CFIR_real*/ /*channel index = 2 is just for debug*/
	u32		IQK_CFIR_imag[3][4][2][8]; /*channel / path / TRX(TX:0, RX:1) / CFIR_imag*/ /*channel index = 2 is just for debug*/
	u8		retry_count[2][4][3]; /* channel / path / (TXK:0, RXK1:1, RXK2:2) */
	u8		gs_retry_count[2][4][2]; /* channel / path / (GSRXK1:0, GSRXK2:1) */
	u8		RXIQK_fail_code[2][4]; /* channel / path 0:SRXK1 fail, 1:RXK1 fail 2:RXK2 fail */
	u32		LOK_IDAC[2][4];		/*channel / path*/
	u16		RXIQK_AGC[2][4];	 /*channel / path*/
	u32		bypass_iqk[2][4];	/*channel / 0xc94/0xe94*/
	u32		txgap_result[8]; /*txagpK result  */
	u32		tmp_GNTWL;
	boolean		is_BTG;
	boolean		isbnd;
	boolean is_reload;
	boolean segment_iqk;
	boolean is_hwtx;
	
	boolean	xym_read;
	boolean trximr_enable;
	u32		rx_xym[2][10];
	u32		tx_xym[2][10];
	u32		gs1_xym[2][6];
	u32		gs2_xym[2][6];
	u32		rxk1_xym[2][6];
#endif
};

#endif
