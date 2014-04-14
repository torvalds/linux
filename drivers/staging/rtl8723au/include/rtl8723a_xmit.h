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
 ******************************************************************************/
#ifndef __RTL8723A_XMIT_H__
#define __RTL8723A_XMIT_H__

/*  */
/*  Queue Select Value in TxDesc */
/*  */
#define QSLT_BK							0x2/* 0x01 */
#define QSLT_BE							0x0
#define QSLT_VI							0x5/* 0x4 */
#define QSLT_VO							0x7/* 0x6 */
#define QSLT_BEACON						0x10
#define QSLT_HIGH						0x11
#define QSLT_MGNT						0x12
#define QSLT_CMD						0x13

/*  */
/* defined for TX DESC Operation */
/*  */

#define MAX_TID (15)

/* OFFSET 0 */
#define OFFSET_SZ	0
#define OFFSET_SHT	16
#define BMC		BIT(24)
#define LSG		BIT(26)
#define FSG		BIT(27)
#define OWN		BIT(31)


/* OFFSET 4 */
#define PKT_OFFSET_SZ	0
#define BK		BIT(6)
#define QSEL_SHT	8
#define Rate_ID_SHT	16
#define NAVUSEHDR	BIT(20)
#define PKT_OFFSET_SHT	26
#define HWPC		BIT(31)

/* OFFSET 8 */
#define AGG_EN		BIT(29)

/* OFFSET 12 */
#define SEQ_SHT		16

/* OFFSET 16 */
#define QoS		BIT(6)
#define HW_SEQ_EN	BIT(7)
#define USERATE		BIT(8)
#define DISDATAFB	BIT(10)
#define DATA_SHORT	BIT(24)
#define DATA_BW		BIT(25)

/* OFFSET 20 */
#define SGI		BIT(6)

struct txdesc_8723a {
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

	u32 macid:5;
	u32 agg_en:1;
	u32 bk:1;
	u32 rd_en:1;
	u32 qsel:5;
	u32 rd_nav_ext:1;
	u32 lsig_txop_en:1;
	u32 pifs:1;
	u32 rate_id:4;
	u32 navusehdr:1;
	u32 en_desc_id:1;
	u32 sectype:2;
	u32 rsvd0424:2;
	u32 pkt_offset:5;	/*  unit: 8 bytes */
	u32 rsvd0431:1;

	u32 rts_rc:6;
	u32 data_rc:6;
	u32 rsvd0812:2;
	u32 bar_rty_th:2;
	u32 rsvd0816:1;
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

	u32 nextheadpage:8;
	u32 tailpage:8;
	u32 seq:12;
	u32 cpu_handle:1;
	u32 tag1:1;
	u32 trigger_int:1;
	u32 hwseq_en:1;

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
	u32 rsvd1615:3;
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

	u32 datarate:6;
	u32 sgi:1;
	u32 try_rate:1;
	u32 data_ratefb_lmt:5;
	u32 rts_ratefb_lmt:4;
	u32 rty_lmt_en:1;
	u32 data_rt_lmt:6;
	u32 usb_txagg_num:8;

	u32 txagg_a:5;
	u32 txagg_b:5;
	u32 use_max_len:1;
	u32 max_agg_num:5;
	u32 mcsg1_max_len:4;
	u32 mcsg2_max_len:4;
	u32 mcsg3_max_len:4;
	u32 mcs7_sgi_max_len:4;

	u32 checksum:16;	/*  TxBuffSize(PCIe)/CheckSum(USB) */
	u32 mcsg4_max_len:4;
	u32 mcsg5_max_len:4;
	u32 mcsg6_max_len:4;
	u32 mcs15_sgi_max_len:4;
};

#define txdesc_set_ccx_sw_8723a(txdesc, value) \
	do { \
		((struct txdesc_8723a *)(txdesc))->mcsg4_max_len = (((value)>>8) & 0x0f); \
		((struct txdesc_8723a *)(txdesc))->mcs15_sgi_max_len= (((value)>>4) & 0x0f); \
		((struct txdesc_8723a *)(txdesc))->mcsg6_max_len = ((value) & 0x0f); \
	} while (0)

struct txrpt_ccx_8723a {
	/* offset 0 */
	u8 tag1:1;
	u8 rsvd:4;
	u8 int_bt:1;
	u8 int_tri:1;
	u8 int_ccx:1;

	/* offset 1 */
	u8 mac_id:5;
	u8 pkt_drop:1;
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

#define txrpt_ccx_sw_8723a(txrpt_ccx) ((txrpt_ccx)->sw0 + ((txrpt_ccx)->sw1<<8))
#define txrpt_ccx_qtime_8723a(txrpt_ccx) ((txrpt_ccx)->ccx_qtime0+((txrpt_ccx)->ccx_qtime1<<8))

void dump_txrpt_ccx_8723a(void *buf);
void handle_txrpt_ccx_8723a(struct rtw_adapter *adapter, void *buf);
void rtl8723a_update_txdesc(struct xmit_frame *pxmitframe, u8 *pmem);
void rtl8723a_fill_fake_txdesc(struct rtw_adapter *padapter, u8 *pDesc, u32 BufferLen, u8 IsPsPoll, u8 IsBTQosNull);

s32	rtl8723au_hal_xmitframe_enqueue(struct rtw_adapter *padapter, struct xmit_frame *pxmitframe);
s32 rtl8723au_xmit_buf_handler(struct rtw_adapter *padapter);
#define hal_xmit_handler rtl8723au_xmit_buf_handler
s32	rtl8723au_init_xmit_priv(struct rtw_adapter * padapter);
void	rtl8723au_free_xmit_priv(struct rtw_adapter * padapter);
s32 rtl8723au_hal_xmit(struct rtw_adapter *padapter, struct xmit_frame *pxmitframe);
s32 rtl8723au_mgnt_xmit(struct rtw_adapter *padapter, struct xmit_frame *pmgntframe);
s32 rtl8723au_xmitframe_complete(struct rtw_adapter *padapter, struct xmit_priv *pxmitpriv, struct xmit_buf *pxmitbuf);


#endif
