/******************************************************************************
 *
 * Copyright(c) 2013 - 2017 Realtek Corporation.
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
#ifndef __RTL8821A_XMIT_H__
#define __RTL8821A_XMIT_H__

#include <drv_types.h>

typedef struct txdescriptor_8821a {
	/* Offset 0 */
	u32 pktlen:16;
	u32 offset:8;
	u32 bmc:1;
	u32 htc:1;
	u32 rsvd0026:1;
	u32 rsvd0027:1;
	u32 linip:1;
	u32 noacm:1;
	u32 gf:1;
	u32 rsvd0031:1;

	/* Offset 4 */
	u32 macid:7;
	u32 rsvd0407:1;
	u32 qsel:5;
	u32 rdg_nav_ext:1;
	u32 lsig_txop_en:1;
	u32 pifs:1;
	u32 rate_id:5;
	u32 en_desc_id:1;
	u32 sectype:2;
	u32 pkt_offset:5; /* unit: 8 bytes */
	u32 moredata:1;
	u32 txop_ps_cap:1;
	u32 txop_ps_mode:1;

	/* Offset 8 */
	u32 p_aid:9;
	u32 rsvd0809:1;
	u32 cca_rts:2;
	u32 agg_en:1;
	u32 rdg_en:1;
	u32 null_0:1;
	u32 null_1:1;
	u32 bk:1;
	u32 morefrag:1;
	u32 raw:1;
	u32 spe_rpt:1;
	u32 ampdu_density:3;
	u32 bt_null:1;
	u32 g_id:6;
	u32 rsvd0830:2;

	/* Offset 12 */
	u32 wheader_len:4;
	u32 chk_en:1;
	u32 early_rate:1;
	u32 hw_ssn_sel:2;
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

	/* Offset 16 */
	u32 datarate:7;
	u32 try_rate:1;
	u32 data_ratefb_lmt:5;
	u32 rts_ratefb_lmt:4;
	u32 rty_lmt_en:1;
	u32 data_rt_lmt:6;
	u32 rtsrate:5;
	u32 pcts_en:1;
	u32 pcts_mask_idx:2;

	/* Offset 20 */
	u32 data_sc:4;
	u32 data_short:1;
	u32 data_bw:2;
	u32 data_ldpc:1;
	u32 data_stbc:2;
	u32 vcs_stbc:2;
	u32 rts_short:1;
	u32 rts_sc:4;
	u32 rsvd2016:7;
	u32 tx_ant:4;
	u32 txpwr_offset:3;
	u32 rsvd2031:1;

	/* Offset 24 */
	u32 sw_define:12;
	u32 mbssid:4;
	u32 antsel_A:3;
	u32 antsel_B:3;
	u32 antsel_C:3;
	u32 antsel_D:3;
	u32 rsvd2428:4;

	/* Offset 28 */
	u32 checksum:16;
	u32 rsvd2816:8;
	u32 usb_txagg_num:8;

	/* Offset 32 */
	u32 rts_rc:6;
	u32 bar_rty_th:2;
	u32 data_rc:6;
	u32 rsvd3214:1;
	u32 en_hwseq:1;
	u32 nextneadpage:8;
	u32 tailpage:8;

	/* Offset 36 */
	u32 padding_len:11;
	u32 txbf_path:1;
	u32 seq:12;
	u32 final_data_rate:8;
} TXDESC_8821A, *PTXDESC_8821A;

#ifdef CONFIG_SDIO_HCI
s32 InitXmitPriv8821AS(PADAPTER padapter);
void FreeXmitPriv8821AS(PADAPTER padapter);
s32 XmitBufHandler8821AS(PADAPTER padapter);
s32 MgntXmit8821AS(PADAPTER padapter, struct xmit_frame *pmgntframe);
#ifdef CONFIG_RTW_MGMT_QUEUE 
s32 rtl8821as_hal_mgmt_xmit_enqueue(PADAPTER adapter, struct xmit_frame *pxmitframe);
#endif
s32	HalXmitNoLock8821AS(PADAPTER padapter, struct xmit_frame *pxmitframe);
s32 HalXmit8821AS(PADAPTER padapter, struct xmit_frame *pxmitframe);
#ifndef CONFIG_SDIO_TX_TASKLET
thread_return XmitThread8821AS(thread_context context);
#endif /* !CONFIG_SDIO_TX_TASKLET */
#endif /* CONFIG_SDIO_HCI */

#if 0
#ifdef CONFIG_USB_HCI
s32 rtl8821au_init_xmit_priv(PADAPTER padapter);
void rtl8821au_free_xmit_priv(PADAPTER padapter);
s32 rtl8821au_hal_xmit(PADAPTER padapter, struct xmit_frame *pxmitframe);
s32 rtl8821au_mgnt_xmit(PADAPTER padapter, struct xmit_frame *pmgntframe);
s32 rtl8821au_hal_xmitframe_enqueue(PADAPTER padapter, struct xmit_frame *pxmitframe);
s32 rtl8821au_xmit_buf_handler(PADAPTER padapter);
void rtl8821au_xmit_tasklet(void *priv);
s32 rtl8821au_xmitframe_complete(PADAPTER padapter, struct xmit_priv *pxmitpriv, struct xmit_buf *pxmitbuf);
#endif /* CONFIG_USB_HCI */

#ifdef CONFIG_PCI_HCI
s32 rtl8821e_init_xmit_priv(PADAPTER padapter);
void rtl8821e_free_xmit_priv(PADAPTER padapter);
struct xmit_buf *rtl8821e_dequeue_xmitbuf(struct rtw_tx_ring *ring);
void rtl8821e_xmitframe_resume(PADAPTER padapter);
s32 rtl8821e_hal_xmit(PADAPTER padapter, struct xmit_frame *pxmitframe);
s32 rtl8821e_mgnt_xmit(PADAPTER padapter, struct xmit_frame *pmgntframe);
void rtl8821e_xmit_tasklet(void *priv);
#endif /* CONFIG_PCI_HCI */
#endif

#endif /* __RTL8821_XMIT_H__ */
