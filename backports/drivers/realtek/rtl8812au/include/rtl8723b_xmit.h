/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
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
#ifndef __RTL8723B_XMIT_H__
#define __RTL8723B_XMIT_H__

//
// Queue Select Value in TxDesc
//
#define QSLT_BK							0x2//0x01
#define QSLT_BE							0x0
#define QSLT_VI							0x5//0x4
#define QSLT_VO							0x7//0x6
#define QSLT_BEACON						0x10
#define QSLT_HIGH						0x11
#define QSLT_MGNT						0x12
#define QSLT_CMD						0x13

#define MAX_TID (15)

//OFFSET 0
#define OFFSET_SZ	0
#define OFFSET_SHT	16
#define BMC		BIT(24)
#define LSG		BIT(26)
#define FSG		BIT(27)
#define OWN 		BIT(31)


//OFFSET 4
#define PKT_OFFSET_SZ	0
#define BK		BIT(6)
#define QSEL_SHT	8
#define Rate_ID_SHT	16
#define NAVUSEHDR	BIT(20)
#define PKT_OFFSET_SHT	26
#define HWPC		BIT(31)

//OFFSET 8
#define AGG_EN		BIT(29)

//OFFSET 12
#define SEQ_SHT		16

//OFFSET 16
#define QoS		BIT(6)
#define HW_SEQ_EN	BIT(7)
#define USERATE		BIT(8)
#define DISDATAFB	BIT(10)
#define DATA_SHORT	BIT(24)
#define DATA_BW		BIT(25)

//OFFSET 20
#define SGI		BIT(6)

//
//defined for TX DESC Operation
//
typedef struct txdesc_8723b
{
	//0
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

	//4//4
	u32 macid:7;
	u32 rsvd0406:1;
	u32 qsel:5;
	u32 rd_nav_ext:1;
	u32 lsig_txop_en:1;
	u32 pifs:1;
	u32 rate_id:5;
	u32 en_desc_id:1;
	u32 sectype:2;
	u32 pkt_offset:5;	// unit: 8 bytes
	u32 rsvd0431:3;

	//8
	u32 p_aid:9;
	u32 rsvd0809:1;
	u32 cca_rts:2;
	u32 agg_en:1;
	u32 rd_en:1;
	u32 null_0:1;	
	u32 null_1:1;	
	u32 bk:1;
	u32 morefrag:1;
	u32 raw:1;
	u32 sep_rpt:1;
	u32 ampdu_density:3;
	u32 bt_null:1;
	u32 gid:6;
	u32 rsvd0830:2;

	//12
	u32 wheader_len:4;
	u32 chk_en:1;
	u32 early_rate:1;
	u32 hwseq_sel:2;
	u32 userate:1;
	u32 disrtsfb:1;
	u32 disdatafb:1;
	u32 cts2self:1;
	u32 rtsen:1;
	u32 hw_rts_en:1;
	u32 port_id:1;
	u32 navusehdr:1;
	u32 use_max_len:1;
	u32 max_agg_num:5;
	u32 ndpa:2;
	u32 ampdu_max_time:8;
	
	//16
	u32 datarate:7;
	u32 try_rate:1;
	u32 data_ratefb_lmt:5;
	u32 rts_ratefb_lmt:4;
	u32 rty_lmt_en:1;
	u32 data_rt_lmt:6;	
	u32 rtsrate:5;
	u32 pcts_en:1;
	u32 pcts_mask_idx:2;

	//20
	u32 data_sc:4;
	u32 data_short:1;
	u32 data_bw:2;
	u32 data_ldpc:1;
	u32 data_stbc:2;
	u32 vcs_stbc:2;
	u32 rts_short:1;
	u32 rts_sc:4;
	u32 rsvd2023:7;
	u32 tx_antl:4;
	u32 txpwr_ofset:3;
	u32 rsvd2031:1;
	
	//24
	u32 sw_def:12;
	u32 rsvd2412:4;
	u32 ant_sel_a:3;
	u32 ant_sel_b:3;
	u32 ant_sel_c:3;
	u32 ant_sel_d:3;
	u32 rsvd2428:4;

	//28
	u32 checksum:16;
	u32 rsvd2816:8;
	u32 usb_txagg_num:8;
	
	//32
	u32 rts_rc:6;
	u32 bar_rty_th:2;
	u32 data_rc:6;
	u32 rsvd3214:1;
	u32 hwseq_en:1;
	u32 nextheadpage:8;
	u32 tailpage:8;
	
	//36
	u32 padding_len:11;
	u32 txbf_path:1;
	u32 seq:12;
	u32 final_data_rate:8;
}TXDESC_8723B, *PTXDESC_8723B;


void rtl8723b_update_txdesc(struct xmit_frame *pxmitframe, u8 *pmem);
void rtl8723b_fill_fake_txdesc(PADAPTER padapter, u8 *pDesc, u32 BufferLen, u8 type, u8 IsBTQosNull);

#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
s32 rtl8723bs_init_xmit_priv(PADAPTER padapter);
void rtl8723bs_free_xmit_priv(PADAPTER padapter);
s32 rtl8723bs_hal_xmit(PADAPTER padapter, struct xmit_frame *pxmitframe);
s32 rtl8723bs_mgnt_xmit(PADAPTER padapter, struct xmit_frame *pmgntframe);
s32	rtl8723bs_hal_xmitframe_enqueue(_adapter *padapter, struct xmit_frame *pxmitframe);
s32 rtl8723bs_xmit_buf_handler(PADAPTER padapter);
thread_return rtl8723bs_xmit_thread(thread_context context);
#define hal_xmit_handler rtl8723bs_xmit_buf_handler
#endif

#ifdef CONFIG_USB_HCI
s32 rtl8723bu_xmit_buf_handler(PADAPTER padapter);
#define hal_xmit_handler rtl8723bu_xmit_buf_handler
#endif
#endif

