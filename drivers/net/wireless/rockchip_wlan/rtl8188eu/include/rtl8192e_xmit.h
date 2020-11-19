/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2012 - 2017 Realtek Corporation.
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
#ifndef __RTL8192E_XMIT_H__
#define __RTL8192E_XMIT_H__

typedef struct txdescriptor_8192e {
	/* Offset 0 */
	u32 pktlen:16;
	u32 offset:8;
	u32 bmc:1;
	u32 htc:1;
	u32 ls:1;
	u32 fs:1;
	u32 linip:1;
	u32 noacm:1;
	u32 gf:1;
	u32 own:1;

	/* Offset 4 */
	u32 macid:6;
	u32 rsvd0406:2;
	u32 qsel:5;
	u32 rd_nav_ext:1;
	u32 lsig_txop_en:1;
	u32 pifs:1;
	u32 rate_id:4;
	u32 navusehdr:1;
	u32 en_desc_id:1;
	u32 sectype:2;
	u32 rsvd0424:2;
	u32 pkt_offset:5;	/* unit: 8 bytes */
	u32 rsvd0431:1;

	/* Offset 8 */
	u32 rts_rc:6;
	u32 data_rc:6;
	u32 agg_en:1;
	u32 rd_en:1;
	u32 bar_rty_th:2;
	u32 bk:1;
	u32 morefrag:1;
	u32 raw:1;
	u32 ccx:1;
	u32 ampdu_density:3;
	u32 bt_null:1;
	u32 ant_sel_a:1;
	u32 ant_sel_b:1;
	u32 tx_ant_cck:2;
	u32 tx_antl:2;
	u32 tx_ant_ht:2;

	/* Offset 12 */
	u32 nextheadpage:8;
	u32 tailpage:8;
	u32 seq:12;
	u32 cpu_handle:1;
	u32 tag1:1;
	u32 trigger_int:1;
	u32 hwseq_en:1;

	/* Offset 16 */
	u32 rtsrate:5;
	u32 ap_dcfe:1;
	u32 hwseq_sel:2;
	u32 userate:1;
	u32 disrtsfb:1;
	u32 disdatafb:1;
	u32 cts2self:1;
	u32 rtsen:1;
	u32 hw_rts_en:1;
	u32 port_id:1;
	u32 pwr_status:3;
	u32 wait_dcts:1;
	u32 cts2ap_en:1;
	u32 data_sc:2;
	u32 data_stbc:2;
	u32 data_short:1;
	u32 data_bw:1;
	u32 rts_short:1;
	u32 rts_bw:1;
	u32 rts_sc:2;
	u32 vcs_stbc:2;

	/* Offset 20 */
	u32 datarate:6;
	u32 sgi:1;
	u32 try_rate:1;
	u32 data_ratefb_lmt:5;
	u32 rts_ratefb_lmt:4;
	u32 rty_lmt_en:1;
	u32 data_rt_lmt:6;
	u32 usb_txagg_num:8;

	/* Offset 24 */
	u32 txagg_a:5;
	u32 txagg_b:5;
	u32 use_max_len:1;
	u32 max_agg_num:5;
	u32 mcsg1_max_len:4;
	u32 mcsg2_max_len:4;
	u32 mcsg3_max_len:4;
	u32 mcs7_sgi_max_len:4;

	/* Offset 28 */
	u32 checksum:16;	/* TxBuffSize(PCIe)/CheckSum(USB) */
	u32 mcsg4_max_len:4;
	u32 mcsg5_max_len:4;
	u32 mcsg6_max_len:4;
	u32 mcs15_sgi_max_len:4;
} TXDESC_8192E, *PTXDESC_8192E;



/* For 88e early mode */
#define SET_EARLYMODE_PKTNUM(__pAddr, __Value) SET_BITS_TO_LE_4BYTE(__pAddr, 0, 3, __Value)
#define SET_EARLYMODE_LEN0(__pAddr, __Value) SET_BITS_TO_LE_4BYTE(__pAddr, 4, 12, __Value)
#define SET_EARLYMODE_LEN1(__pAddr, __Value) SET_BITS_TO_LE_4BYTE(__pAddr, 16, 12, __Value)
#define SET_EARLYMODE_LEN2_1(__pAddr, __Value) SET_BITS_TO_LE_4BYTE(__pAddr, 28, 4, __Value)
#define SET_EARLYMODE_LEN2_2(__pAddr, __Value) SET_BITS_TO_LE_4BYTE(__pAddr+4, 0, 8, __Value)
#define SET_EARLYMODE_LEN3(__pAddr, __Value) SET_BITS_TO_LE_4BYTE(__pAddr+4, 8, 12, __Value)
#define SET_EARLYMODE_LEN4(__pAddr, __Value) SET_BITS_TO_LE_4BYTE(__pAddr+4, 20, 12, __Value)

/*
 * defined for TX DESC Operation
 *   */

#define MAX_TID (15)

/* OFFSET 0 */
#define OFFSET_SZ	0
#define OFFSET_SHT	16
#define BMC		BIT(24)
#define LSG		BIT(26)
#define FSG		BIT(27)
#define OWN		BIT(31)


/* OFFSET 4 */
#define PKT_OFFSET_SZ		0
#define QSEL_SHT			8
#define RATE_ID_SHT			16
#define NAVUSEHDR			BIT(20)
#define SEC_TYPE_SHT		22
#define PKT_OFFSET_SHT		26

/* OFFSET 8 */
#define AGG_EN				BIT(12)
#define AGG_BK					BIT(16)
#define AMPDU_DENSITY_SHT	20
#define ANTSEL_A			BIT(24)
#define ANTSEL_B			BIT(25)
#define TX_ANT_CCK_SHT		26
#define TX_ANTL_SHT			28
#define TX_ANT_HT_SHT		30

/* OFFSET 12 */
#define SEQ_SHT				16
#define EN_HWSEQ			BIT(31)

/* OFFSET 16 */
#define	QOS                          BIT(6)
#define	HW_SSN				BIT(7)
#define	USERATE			BIT(8)
#define	DISDATAFB			BIT(10)
#define   CTS_2_SELF			BIT(11)
#define	RTS_EN				BIT(12)
#define	HW_RTS_EN			BIT(13)
#define	DATA_SHORT			BIT(24)
#define	PWR_STATUS_SHT	15
#define	DATA_SC_SHT		20
#define	DATA_BW			BIT(25)

/* OFFSET 20 */
#define	RTY_LMT_EN			BIT(17)


/* OFFSET 20 */
#define SGI					BIT(6)
#define USB_TXAGG_NUM_SHT	24


/* *****Tx Desc Buffer content */

/* config element for each tx buffer
 *
#define SET_TXBUFFER_DESC_LEN_WITH_OFFSET(__pTxDesc, __Offset, __Valeu) SET_BITS_TO_LE_4BYTE(__pTxDesc+(__Offset*16), 0, 16, __Valeu)
#define SET_TXBUFFER_DESC_AMSDU_WITH_OFFSET(__pTxDesc, __Offset, __Valeu) SET_BITS_TO_LE_4BYTE(__pTxDesc+(__Offset*16), 31, 1, __Valeu)
#define SET_TXBUFFER_DESC_ADD_LOW_WITH_OFFSET(__pTxDesc, __Offset, __Valeu) SET_BITS_TO_LE_4BYTE(__pTxDesc+(__Offset*16)+4, 0, 32, __Valeu)
#define SET_TXBUFFER_DESC_ADD_HIGT_WITH_OFFSET(__pTxDesc, __Offset, __Valeu) SET_BITS_TO_LE_4BYTE(__pTxDesc+(__Offset*16)+8, 0, 32, __Valeu)
*/
#define SET_TXBUFFER_DESC_LEN_WITH_OFFSET(__pTxDesc, __Offset, __Valeu) SET_BITS_TO_LE_4BYTE(__pTxDesc+(__Offset*8), 0, 16, __Valeu)
#define SET_TXBUFFER_DESC_AMSDU_WITH_OFFSET(__pTxDesc, __Offset, __Valeu) SET_BITS_TO_LE_4BYTE(__pTxDesc+(__Offset*8), 31, 1, __Valeu)
#define SET_TXBUFFER_DESC_ADD_LOW_WITH_OFFSET(__pTxDesc, __Offset, __Valeu) SET_BITS_TO_LE_4BYTE(__pTxDesc+(__Offset*8)+4, 0, 32, __Valeu)
#define SET_TXBUFFER_DESC_ADD_HIGT_WITH_OFFSET(__pTxDesc, __Offset, __Valeu) SET_BITS_TO_LE_4BYTE(__pTxDesc+(__Offset*16)+8, 0, 32, __Valeu)


/* Dword 0 */
#define SET_TX_BUFF_DESC_LEN_0_92E(__pTxDesc, __Valeu) SET_BITS_TO_LE_4BYTE(__pTxDesc, 0, 14, __Valeu)
#define SET_TX_BUFF_DESC_PSB_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc, 16, 15, __Value)
#define SET_TX_BUFF_DESC_OWN_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc, 31, 1, __Value)
/* Dword 1 */
#define SET_TX_BUFF_DESC_ADDR_LOW_0_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+4, 0, 32, __Value)
#define GET_TX_DESC_TX_BUFFER_ADDRESS_92E(__pTxDesc) LE_BITS_TO_4BYTE(__pTxDesc+4, 0, 32)


/* Dword 2 */
#define SET_TX_BUFF_DESC_ADDR_HIGH_0_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 0, 32, __Value)
/* Dword 3, RESERVED */


/* *****Tx Desc content
 * Dword 0 */
#define SET_TX_DESC_PKT_SIZE_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc, 0, 16, __Value)
#define SET_TX_DESC_OFFSET_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc, 16, 8, __Value)
#define SET_TX_DESC_BMC_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc, 24, 1, __Value)
#define SET_TX_DESC_HTC_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc, 25, 1, __Value)
#define SET_TX_DESC_LAST_SEG_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc, 26, 1, __Value)
#define SET_TX_DESC_FIRST_SEG_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc, 27, 1, __Value)
#define SET_TX_DESC_LINIP_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc, 28, 1, __Value)
#define SET_TX_DESC_NO_ACM_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc, 29, 1, __Value)
#define SET_TX_DESC_GF_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc, 30, 1, __Value)
#define SET_TX_DESC_OWN_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc, 31, 1, __Value)
#define GET_TX_DESC_OWN_92E(__pTxDesc) LE_BITS_TO_4BYTE(__pTxDesc, 31, 1)

/* Dword 1 */
#define SET_TX_DESC_MACID_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+4, 0, 7, __Value)
#define SET_TX_DESC_QUEUE_SEL_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+4, 8, 5, __Value)
#define SET_TX_DESC_RDG_NAV_EXT_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+4, 13, 1, __Value)
#define SET_TX_DESC_LSIG_TXOP_EN_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+4, 14, 1, __Value)
#define SET_TX_DESC_PIFS_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+4, 15, 1, __Value)
#define SET_TX_DESC_RATE_ID_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+4, 16, 5, __Value)
#define SET_TX_DESC_EN_DESC_ID_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+4, 21, 1, __Value)
#define SET_TX_DESC_SEC_TYPE_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+4, 22, 2, __Value)
#define SET_TX_DESC_PKT_OFFSET_92E(__pTxDesc, __Value)		SET_BITS_TO_LE_4BYTE(__pTxDesc+4, 24, 5, __Value)
#define SET_TX_DESC_MORE_DATA_92E(__pTxDesc, __Value)		SET_BITS_TO_LE_4BYTE(__pTxDesc+4, 29, 1, __Value)
#define SET_TX_DESC_TXOP_PS_CAP_92E(__pTxDesc, __Value)		SET_BITS_TO_LE_4BYTE(__pTxDesc+4, 30, 1, __Value)
#define SET_TX_DESC_TXOP_PS_MODE_92E(__pTxDesc, __Value)		SET_BITS_TO_LE_4BYTE(__pTxDesc+4, 31, 1, __Value)


/* Dword 2 */
#define SET_TX_DESC_PAID_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 0,  9, __Value)
#define SET_TX_DESC_CCA_RTS_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 10, 2, __Value)
#define SET_TX_DESC_AGG_ENABLE_92E(__pTxDesc, __Value)		SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 12, 1, __Value)
#define SET_TX_DESC_RDG_ENABLE_92E(__pTxDesc, __Value)		SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 13, 1, __Value)
#define SET_TX_DESC_NULL_0_92E(__pTxDesc, __Value)		SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 14, 1, __Value)
#define SET_TX_DESC_NULL_1_92E(__pTxDesc, __Value)		SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 15, 1, __Value)
#define SET_TX_DESC_BK_92E(__pTxDesc, __Value)				SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 16, 1, __Value)
#define SET_TX_DESC_MORE_FRAG_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 17, 1, __Value)
#define SET_TX_DESC_RAW_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 18, 1, __Value)
#define GET_TX_DESC_MORE_FRAG_92E(__pTxDesc)				LE_BITS_TO_4BYTE(__pTxDesc+8, 17, 1)
#define SET_TX_DESC_SPE_RPT_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 19, 1, __Value)
#define SET_TX_DESC_AMPDU_DENSITY_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 20, 3, __Value)
#define SET_TX_DESC_BT_NULL_92E(__pTxDesc, __Value)			SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 23, 1, __Value)
#define SET_TX_DESC_GID_92E(__pTxDesc, __Value)			SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 24, 6, __Value)


/* Dword 3 */
#define SET_TX_DESC_WHEADER_LEN_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 0, 4, __Value)
#define SET_TX_DESC_CHK_EN_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 4, 1, __Value)
#define SET_TX_DESC_EARLY_RATE_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 5, 1, __Value)
#define SET_TX_DESC_HWSEQ_SEL_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 6, 2, __Value)
#define SET_TX_DESC_USE_RATE_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 8, 1, __Value)
#define SET_TX_DESC_DISABLE_RTS_FB_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 9, 1, __Value)
#define SET_TX_DESC_DISABLE_FB_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 10, 1, __Value)
#define SET_TX_DESC_CTS2SELF_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 11, 1, __Value)
#define SET_TX_DESC_RTS_ENABLE_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 12, 1, __Value)
#define SET_TX_DESC_HW_RTS_ENABLE_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 13, 1, __Value)
#define SET_TX_DESC_HW_PORT_ID_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 14, 1, __Value)
#define SET_TX_DESC_NAV_USE_HDR_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 15, 1, __Value)
#define SET_TX_DESC_USE_MAX_LEN_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 16, 1, __Value)
#define SET_TX_DESC_MAX_AGG_NUM_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 17, 5, __Value)
#define SET_TX_DESC_NDPA_92E(__pTxDesc, __Value)		SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 22, 2, __Value)
#define SET_TX_DESC_AMPDU_MAX_TIME_92E(__pTxDesc, __Value)		SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 24, 8, __Value)

/* Dword 4 */
#define SET_TX_DESC_TX_RATE_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+16, 0, 7, __Value)
#define SET_TX_DESC_TRY_RATE_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+16, 7, 1, __Value)
#define SET_TX_DESC_DATA_RATE_FB_LIMIT_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+16, 8, 5, __Value)
#define SET_TX_DESC_RTS_RATE_FB_LIMIT_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+16, 13, 4, __Value)
#define SET_TX_DESC_RETRY_LIMIT_ENABLE_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+16, 17, 1, __Value)
#define SET_TX_DESC_DATA_RETRY_LIMIT_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+16, 18, 6, __Value)
#define SET_TX_DESC_RTS_RATE_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+16, 24, 5, __Value)
#define SET_TX_DESC_PCTS_ENABLE_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+16, 29, 1, __Value)
#define SET_TX_DESC_PCTS_MASK_IDX_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+16, 30, 2, __Value)


/* Dword 5 */
#define SET_TX_DESC_DATA_SC_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+20, 0, 4, __Value)
#define SET_TX_DESC_DATA_SHORT_92E(__pTxDesc, __Value)	SET_BITS_TO_LE_4BYTE(__pTxDesc+20, 4, 1, __Value)
#define SET_TX_DESC_DATA_BW_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+20, 5, 2, __Value)
#define SET_TX_DESC_DATA_LDPC_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+20, 7, 1, __Value)
#define SET_TX_DESC_DATA_STBC_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+20, 8, 2, __Value)
#define SET_TX_DESC_VCS_STBC_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+20, 10, 2, __Value)
#define SET_TX_DESC_RTS_SHORT_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+20, 12, 1, __Value)
#define SET_TX_DESC_RTS_SC_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+20, 13, 4, __Value)
#define SET_TX_DESC_TX_ANT_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+20, 24, 4, __Value)
#define SET_TX_DESC_TX_POWER_0_PSET_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+20, 28, 3, __Value)

/* Dword 6 */
#define SET_TX_DESC_SW_DEFINE_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+24, 0, 12, __Value)
#define SET_TX_DESC_MBSSID_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+24, 12, 4, __Value)
#define SET_TX_DESC_ANTSEL_A_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+24, 16, 3, __Value)
#define SET_TX_DESC_ANTSEL_B_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+24, 19, 3, __Value)
#define SET_TX_DESC_ANTSEL_C_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+24, 22, 3, __Value)
#define SET_TX_DESC_ANTSEL_D_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+24, 25, 3, __Value)

/* Dword 7 */
#ifdef CONFIG_PCI_HCI
	#define SET_TX_DESC_TX_BUFFER_SIZE_92E(__pTxDesc, __Value)		SET_BITS_TO_LE_4BYTE(__pTxDesc+28, 0, 16, __Value)
#endif

#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_USB_HCI)
	#define SET_TX_DESC_TX_DESC_CHECKSUM_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+28, 0, 16, __Value)
#endif
#define SET_TX_DESC_USB_TXAGG_NUM_92E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+28, 24, 8, __Value)


/* #define SET_TX_DESC_HWSEQ_EN_92E(__pTxDesc, __Value)			SET_BITS_TO_LE_4BYTE(__pTxDesc+32, 15, 1, __Value) */
/* Dword 8 */

#define SET_TX_DESC_RTS_RC_92E(__pTxDesc, __Value)			SET_BITS_TO_LE_4BYTE(__pTxDesc+32, 0, 6, __Value)
#define SET_TX_DESC_BAR_RTY_TH_92E(__pTxDesc, __Value)			SET_BITS_TO_LE_4BYTE(__pTxDesc+32, 6, 2, __Value)
#define SET_TX_DESC_DATA_RC_92E(__pTxDesc, __Value)			SET_BITS_TO_LE_4BYTE(__pTxDesc+32, 8, 6, __Value)
#define SET_TX_DESC_EN_HWSEQ_92E(__pTxDesc, __Value)			SET_BITS_TO_LE_4BYTE(__pTxDesc+32, 15, 1, __Value)
#define SET_TX_DESC_NEXT_HEAD_PAGE_92E(__pTxDesc, __Value)(__pTxDesc, __Value)	SET_BITS_TO_LE_4BYTE(__pTxDesc+32, 16, 8, __Value)
#define SET_TX_DESC_TAIL_PAGE_92E(__pTxDesc, __Value)(__pTxDesc, __Value)	SET_BITS_TO_LE_4BYTE(__pTxDesc+32, 24, 8, __Value)

/* Dword 9 */
#define SET_TX_DESC_PADDING_LENGTH_92E(__pTxDesc, __Value)					SET_BITS_TO_LE_4BYTE(__pTxDesc+36, 0, 11, __Value)
#define SET_TX_DESC_TXBF_PATH_92E(__pTxDesc, __Value)					SET_BITS_TO_LE_4BYTE(__pTxDesc+36, 11, 1, __Value)
#define SET_TX_DESC_SEQ_92E(__pTxDesc, __Value)					SET_BITS_TO_LE_4BYTE(__pTxDesc+36, 12, 12, __Value)
#define SET_TX_DESC_FINAL_DATA_RATE_92E(__pTxDesc, __Value)					SET_BITS_TO_LE_4BYTE(__pTxDesc+36, 24, 8, __Value)


#define SET_EARLYMODE_PKTNUM_92E(__pAddr, __Value)					SET_BITS_TO_LE_4BYTE(__pAddr, 0, 4, __Value)
#define SET_EARLYMODE_LEN0_92E(__pAddr, __Value)					SET_BITS_TO_LE_4BYTE(__pAddr, 4, 15, __Value)
#define SET_EARLYMODE_LEN1_1_92E(__pAddr, __Value)					SET_BITS_TO_LE_4BYTE(__pAddr, 19, 13, __Value)
#define SET_EARLYMODE_LEN1_2_92E(__pAddr, __Value)					SET_BITS_TO_LE_4BYTE(__pAddr+4, 0, 2, __Value)
#define SET_EARLYMODE_LEN2_92E(__pAddr, __Value)					SET_BITS_TO_LE_4BYTE(__pAddr+4, 2, 15,  __Value)
#define SET_EARLYMODE_LEN3_92E(__pAddr, __Value)					SET_BITS_TO_LE_4BYTE(__pAddr+4, 17, 15, __Value)

void rtl8192e_cal_txdesc_chksum(u8 *ptxdesc);

#ifdef CONFIG_USB_HCI
	s32 rtl8192eu_init_xmit_priv(PADAPTER padapter);
	void rtl8192eu_free_xmit_priv(PADAPTER padapter);
	s32 rtl8192eu_hal_xmit(PADAPTER padapter, struct xmit_frame *pxmitframe);
	s32 rtl8192eu_mgnt_xmit(PADAPTER padapter, struct xmit_frame *pmgntframe);
	s32	rtl8192eu_hal_xmitframe_enqueue(_adapter *padapter, struct xmit_frame *pxmitframe);
	s32 rtl8192eu_xmit_buf_handler(PADAPTER padapter);
	#define hal_xmit_handler rtl8192eu_xmit_buf_handler
	void rtl8192eu_xmit_tasklet(void *priv);
	s32 rtl8192eu_xmitframe_complete(_adapter *padapter, struct xmit_priv *pxmitpriv, struct xmit_buf *pxmitbuf);
#endif

#ifdef CONFIG_PCI_HCI
	s32 rtl8192ee_init_xmit_priv(PADAPTER padapter);
	void rtl8192ee_free_xmit_priv(PADAPTER padapter);
	struct xmit_buf *rtl8192ee_dequeue_xmitbuf(struct rtw_tx_ring *ring);
	s32	rtl8192ee_hal_xmitframe_enqueue(_adapter *padapter, struct xmit_frame *pxmitframe);
	void	rtl8192ee_xmitframe_resume(_adapter *padapter);
	s32 rtl8192ee_hal_xmit(PADAPTER padapter, struct xmit_frame *pxmitframe);
	s32 rtl8192ee_mgnt_xmit(PADAPTER padapter, struct xmit_frame *pmgntframe);
	void rtl8192ee_xmit_tasklet(void *priv);
#endif

#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
	s32 rtl8192es_init_xmit_priv(PADAPTER padapter);
	void rtl8192es_free_xmit_priv(PADAPTER padapter);

	s32 rtl8192es_hal_xmit(PADAPTER padapter, struct xmit_frame *pxmitframe);
	s32 rtl8192es_mgnt_xmit(PADAPTER padapter, struct xmit_frame *pmgntframe);
	s32	rtl8192es_hal_xmitframe_enqueue(_adapter *padapter, struct xmit_frame *pxmitframe);
	thread_return rtl8192es_xmit_thread(thread_context context);
	s32 rtl8192es_xmit_buf_handler(PADAPTER padapter);

	#ifdef CONFIG_SDIO_TX_TASKLET
		void rtl8192es_xmit_tasklet(void *priv);
	#endif
#endif

struct txrpt_ccx_92e {
	/* offset 0 */
	u8 tag1:1;
	u8 pkt_num:3;
	u8 txdma_underflow:1;
	u8 int_bt:1;
	u8 int_tri:1;
	u8 int_ccx:1;

	/* offset 1 */
	u8 mac_id:6;
	u8 pkt_ok:1;
	u8 bmc:1;

	/* offset 2 */
	u8 retry_cnt:6;
	u8 lifetime_over:1;
	u8 retry_over:1;

	/* offset 3 */
	u8 ccx_qtime0;
	u8 ccx_qtime1;

	/* offset 5 */
	u8 final_data_rate;

	/* offset 6 */
	u8 sw1:4;
	u8 qsel:4;

	/* offset 7 */
	u8 sw0;
};

#ifdef CONFIG_TX_EARLY_MODE
	void UpdateEarlyModeInfo8192E(struct xmit_priv *pxmitpriv, struct xmit_buf *pxmitbuf);
#endif
s32	rtl8192e_init_xmit_priv(_adapter *padapter);
void _dbg_dump_tx_info(_adapter	*padapter, int frame_tag, u8 *ptxdesc);

void rtl8192e_fill_fake_txdesc(PADAPTER	padapter, u8 *pDesc, u32 BufferLen,
			       u8 IsPsPoll, u8	IsBTQosNull, u8 bDataFrame);
void rtl8192e_cal_txdesc_chksum(u8 *ptxdesc);

u8	BWMapping_92E(PADAPTER Adapter, struct pkt_attrib *pattrib);
u8	SCMapping_92E(PADAPTER Adapter, struct pkt_attrib	*pattrib);
void fill_txdesc_phy(PADAPTER padapter, struct pkt_attrib *pattrib, u8 *ptxdesc);
void fill_txdesc_vcs(struct pkt_attrib *pattrib, u8 *ptxdesc);
#if defined(CONFIG_CONCURRENT_MODE)
	void fill_txdesc_force_bmc_camid(struct pkt_attrib *pattrib, u8 *ptxdesc);
#endif
void fill_txdesc_bmc_tx_rate(struct pkt_attrib *pattrib, u8 *ptxdesc);

void fill_txdesc_sectype(struct pkt_attrib *pattrib, u8 *ptxdesc);
void rtl8192e_fixed_rate(_adapter *padapter, u8 *ptxdesc);

#endif /* __RTL8192E_XMIT_H__ */
