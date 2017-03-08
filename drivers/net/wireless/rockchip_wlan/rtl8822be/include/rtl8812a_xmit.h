/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#ifndef __RTL8812A_XMIT_H__
#define __RTL8812A_XMIT_H__


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
#define BMC			BIT(24)
#define LSG			BIT(26)
#define FSG			BIT(27)
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
#define AGG_BK				BIT(16)
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
#define QOS					BIT(6)
#define	HW_SSN				BIT(7)
#define USERATE				BIT(8)
#define DISDATAFB			BIT(10)
#define CTS_2_SELF			BIT(11)
#define	RTS_EN				BIT(12)
#define	HW_RTS_EN			BIT(13)
#define DATA_SHORT			BIT(24)
#define PWR_STATUS_SHT	15
#define DATA_SC_SHT		20
#define DATA_BW				BIT(25)

/* OFFSET 20 */
#define	RTY_LMT_EN			BIT(17)

/* OFFSET 20 */
#define SGI					BIT(6)
#define USB_TXAGG_NUM_SHT	24

typedef struct txdescriptor_8812 {
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

	/* Offset 32 */
	u32 rsvd32;

	/* Offset 36 */
	u32 rsvd36;
} TXDESC_8812, *PTXDESC_8812;


/* Dword 0 */
#define GET_TX_DESC_OWN_8812(__pTxDesc)				LE_BITS_TO_4BYTE(__pTxDesc, 31, 1)
#define SET_TX_DESC_PKT_SIZE_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc, 0, 16, __Value)
#define SET_TX_DESC_OFFSET_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc, 16, 8, __Value)
#define SET_TX_DESC_BMC_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc, 24, 1, __Value)
#define SET_TX_DESC_HTC_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc, 25, 1, __Value)
#define SET_TX_DESC_LAST_SEG_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc, 26, 1, __Value)
#define SET_TX_DESC_FIRST_SEG_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc, 27, 1, __Value)
#define SET_TX_DESC_LINIP_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc, 28, 1, __Value)
#define SET_TX_DESC_NO_ACM_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc, 29, 1, __Value)
#define SET_TX_DESC_GF_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc, 30, 1, __Value)
#define SET_TX_DESC_OWN_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc, 31, 1, __Value)

/* Dword 1 */
#define SET_TX_DESC_MACID_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+4, 0, 7, __Value)
#define SET_TX_DESC_QUEUE_SEL_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+4, 8, 5, __Value)
#define SET_TX_DESC_RDG_NAV_EXT_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+4, 13, 1, __Value)
#define SET_TX_DESC_LSIG_TXOP_EN_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+4, 14, 1, __Value)
#define SET_TX_DESC_PIFS_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+4, 15, 1, __Value)
#define SET_TX_DESC_RATE_ID_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+4, 16, 5, __Value)
#define SET_TX_DESC_EN_DESC_ID_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+4, 21, 1, __Value)
#define SET_TX_DESC_SEC_TYPE_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+4, 22, 2, __Value)
#define SET_TX_DESC_PKT_OFFSET_8812(__pTxDesc, __Value)		SET_BITS_TO_LE_4BYTE(__pTxDesc+4, 24, 5, __Value)

/* Dword 2 */
#define SET_TX_DESC_PAID_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 0,  9, __Value)
#define SET_TX_DESC_CCA_RTS_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 10, 2, __Value)
#define SET_TX_DESC_AGG_ENABLE_8812(__pTxDesc, __Value)		SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 12, 1, __Value)
#define SET_TX_DESC_RDG_ENABLE_8812(__pTxDesc, __Value)		SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 13, 1, __Value)
#define SET_TX_DESC_AGG_BREAK_8812(__pTxDesc, __Value)				SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 16, 1, __Value)
#define SET_TX_DESC_MORE_FRAG_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 17, 1, __Value)
#define SET_TX_DESC_RAW_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 18, 1, __Value)
#define SET_TX_DESC_SPE_RPT_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 19, 1, __Value)
#define SET_TX_DESC_AMPDU_DENSITY_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 20, 3, __Value)
#define SET_TX_DESC_BT_INT_8812(__pTxDesc, __Value)			SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 23, 1, __Value)
#define SET_TX_DESC_GID_8812(__pTxDesc, __Value)			SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 24, 6, __Value)

/* Dword 3 */
#define SET_TX_DESC_WHEADER_LEN_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 0, 4, __Value)
#define SET_TX_DESC_CHK_EN_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 4, 1, __Value)
#define SET_TX_DESC_EARLY_MODE_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 5, 1, __Value)
#define SET_TX_DESC_HWSEQ_SEL_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 6, 2, __Value)
#define SET_TX_DESC_USE_RATE_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 8, 1, __Value)
#define SET_TX_DESC_DISABLE_RTS_FB_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 9, 1, __Value)
#define SET_TX_DESC_DISABLE_FB_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 10, 1, __Value)
#define SET_TX_DESC_CTS2SELF_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 11, 1, __Value)
#define SET_TX_DESC_RTS_ENABLE_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 12, 1, __Value)
#define SET_TX_DESC_HW_RTS_ENABLE_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 13, 1, __Value)
#define SET_TX_DESC_NAV_USE_HDR_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 15, 1, __Value)
#define SET_TX_DESC_USE_MAX_LEN_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 16, 1, __Value)
#define SET_TX_DESC_MAX_AGG_NUM_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 17, 5, __Value)
#define SET_TX_DESC_NDPA_8812(__pTxDesc, __Value)		SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 22, 2, __Value)
#define SET_TX_DESC_AMPDU_MAX_TIME_8812(__pTxDesc, __Value)		SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 24, 8, __Value)

/* Dword 4 */
#define SET_TX_DESC_TX_RATE_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+16, 0, 7, __Value)
#define SET_TX_DESC_DATA_RATE_FB_LIMIT_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+16, 8, 5, __Value)
#define SET_TX_DESC_RTS_RATE_FB_LIMIT_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+16, 13, 4, __Value)
#define SET_TX_DESC_RETRY_LIMIT_ENABLE_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+16, 17, 1, __Value)
#define SET_TX_DESC_DATA_RETRY_LIMIT_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+16, 18, 6, __Value)
#define SET_TX_DESC_RTS_RATE_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+16, 24, 5, __Value)

/* Dword 5 */
#define SET_TX_DESC_DATA_SC_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+20, 0, 4, __Value)
#define SET_TX_DESC_DATA_SHORT_8812(__pTxDesc, __Value)	SET_BITS_TO_LE_4BYTE(__pTxDesc+20, 4, 1, __Value)
#define SET_TX_DESC_DATA_BW_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+20, 5, 2, __Value)
#define SET_TX_DESC_DATA_LDPC_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+20, 7, 1, __Value)
#define SET_TX_DESC_DATA_STBC_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+20, 8, 2, __Value)
#define SET_TX_DESC_CTROL_STBC_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+20, 10, 2, __Value)
#define SET_TX_DESC_RTS_SHORT_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+20, 12, 1, __Value)
#define SET_TX_DESC_RTS_SC_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+20, 13, 4, __Value)
#define SET_TX_DESC_TX_ANT_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+20, 24, 4, __Value)

/* Dword 6 */
#define SET_TX_DESC_SW_DEFINE_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+24, 0, 12, __Value)
#define SET_TX_DESC_ANTSEL_A_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+24, 16, 3, __Value)
#define SET_TX_DESC_ANTSEL_B_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+24, 19, 3, __Value)
#define SET_TX_DESC_ANTSEL_C_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+24, 22, 3, __Value)
#define SET_TX_DESC_ANTSEL_D_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+24, 25, 3, __Value)
#define SET_TX_DESC_MBSSID_8821(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+24, 12, 4, __Value)

/* Dword 7 */
#define SET_TX_DESC_TX_BUFFER_SIZE_8812(__pTxDesc, __Value)		SET_BITS_TO_LE_4BYTE(__pTxDesc+28, 0, 16, __Value)
#define SET_TX_DESC_TX_DESC_CHECKSUM_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+28, 0, 16, __Value)
#define SET_TX_DESC_USB_TXAGG_NUM_8812(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+28, 24, 8, __Value)
#ifdef CONFIG_SDIO_HCI
	#define SET_TX_DESC_SDIO_TXSEQ_8812(__pTxDesc, __Value)			SET_BITS_TO_LE_4BYTE(__pTxDesc+28, 16, 8, __Value)
#endif

/* Dword 8 */
#define SET_TX_DESC_HWSEQ_EN_8812(__pTxDesc, __Value)			SET_BITS_TO_LE_4BYTE(__pTxDesc+32, 15, 1, __Value)

/* Dword 9 */
#define SET_TX_DESC_SEQ_8812(__pTxDesc, __Value)					SET_BITS_TO_LE_4BYTE(__pTxDesc+36, 12, 12, __Value)

/* Dword 10 */
#define SET_TX_DESC_TX_BUFFER_ADDRESS_8812(__pTxDesc, __Value)	SET_BITS_TO_LE_4BYTE(__pTxDesc+40, 0, 32, __Value)
#define GET_TX_DESC_TX_BUFFER_ADDRESS_8812(__pTxDesc)	LE_BITS_TO_4BYTE(__pTxDesc+40, 0, 32)

/* Dword 11 */
#define SET_TX_DESC_NEXT_DESC_ADDRESS_8812(__pTxDesc, __Value)	SET_BITS_TO_LE_4BYTE(__pTxDesc+48, 0, 32, __Value)


#define SET_EARLYMODE_PKTNUM_8812(__pAddr, __Value)					SET_BITS_TO_LE_4BYTE(__pAddr, 0, 4, __Value)
#define SET_EARLYMODE_LEN0_8812(__pAddr, __Value)					SET_BITS_TO_LE_4BYTE(__pAddr, 4, 15, __Value)
#define SET_EARLYMODE_LEN1_1_8812(__pAddr, __Value)					SET_BITS_TO_LE_4BYTE(__pAddr, 19, 13, __Value)
#define SET_EARLYMODE_LEN1_2_8812(__pAddr, __Value)					SET_BITS_TO_LE_4BYTE(__pAddr+4, 0, 2, __Value)
#define SET_EARLYMODE_LEN2_8812(__pAddr, __Value)					SET_BITS_TO_LE_4BYTE(__pAddr+4, 2, 15,  __Value)
#define SET_EARLYMODE_LEN3_8812(__pAddr, __Value)					SET_BITS_TO_LE_4BYTE(__pAddr+4, 17, 15, __Value)

#ifdef CONFIG_TX_EARLY_MODE
	#define USB_DUMMY_OFFSET		2
#else
	#define USB_DUMMY_OFFSET		1
#endif
#define USB_DUMMY_LENGTH		(USB_DUMMY_OFFSET * PACKET_OFFSET_SZ)


void rtl8812a_cal_txdesc_chksum(u8 *ptxdesc);
void rtl8812a_fill_fake_txdesc(PADAPTER	padapter, u8 *pDesc, u32 BufferLen, u8 IsPsPoll, u8	IsBTQosNull, u8 bDataFrame);
void rtl8812a_fill_txdesc_sectype(struct pkt_attrib *pattrib, u8 *ptxdesc);
void rtl8812a_fill_txdesc_vcs(PADAPTER padapter, struct pkt_attrib *pattrib, u8 *ptxdesc);
void rtl8812a_fill_txdesc_phy(PADAPTER padapter, struct pkt_attrib *pattrib, u8 *ptxdesc);
#if defined(CONFIG_CONCURRENT_MODE)
	void fill_txdesc_force_bmc_camid(struct pkt_attrib *pattrib, u8 *ptxdesc);
#endif

#ifdef CONFIG_USB_HCI
	s32 rtl8812au_init_xmit_priv(PADAPTER padapter);
	void rtl8812au_free_xmit_priv(PADAPTER padapter);
	s32 rtl8812au_hal_xmit(PADAPTER padapter, struct xmit_frame *pxmitframe);
	s32 rtl8812au_mgnt_xmit(PADAPTER padapter, struct xmit_frame *pmgntframe);
	s32	 rtl8812au_hal_xmitframe_enqueue(_adapter *padapter, struct xmit_frame *pxmitframe);
	s32 rtl8812au_xmit_buf_handler(PADAPTER padapter);
	void rtl8812au_xmit_tasklet(void *priv);
	s32 rtl8812au_xmitframe_complete(_adapter *padapter, struct xmit_priv *pxmitpriv, struct xmit_buf *pxmitbuf);
#endif

#ifdef CONFIG_PCI_HCI
	s32 rtl8812ae_init_xmit_priv(PADAPTER padapter);
	void rtl8812ae_free_xmit_priv(PADAPTER padapter);
	struct xmit_buf *rtl8812ae_dequeue_xmitbuf(struct rtw_tx_ring *ring);
	void	rtl8812ae_xmitframe_resume(_adapter *padapter);
	s32 rtl8812ae_hal_xmit(PADAPTER padapter, struct xmit_frame *pxmitframe);
	s32 rtl8812ae_mgnt_xmit(PADAPTER padapter, struct xmit_frame *pmgntframe);
	s32	rtl8812ae_hal_xmitframe_enqueue(_adapter *padapter, struct xmit_frame *pxmitframe);
	void rtl8812ae_xmit_tasklet(void *priv);
#endif

#ifdef CONFIG_TX_EARLY_MODE
	void UpdateEarlyModeInfo8812(struct xmit_priv *pxmitpriv, struct xmit_buf *pxmitbuf);
#endif

void _dbg_dump_tx_info(_adapter	*padapter, int frame_tag, u8 *ptxdesc);

u8	BWMapping_8812(PADAPTER Adapter, struct pkt_attrib *pattrib);

u8	SCMapping_8812(PADAPTER Adapter, struct pkt_attrib	*pattrib);

#endif /* __RTL8812_XMIT_H__ */

#ifdef CONFIG_RTL8821A
	#include "rtl8821a_xmit.h"
#endif /* CONFIG_RTL8821A */
