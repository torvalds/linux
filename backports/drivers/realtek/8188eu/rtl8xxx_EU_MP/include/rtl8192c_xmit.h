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
#ifndef _RTL8192C_XMIT_H_
#define _RTL8192C_XMIT_H_

//
//defined for TX DESC Operation
//

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

struct txrpt_ccx_8192c {
	/* offset 0 */
	u8 retry_cnt:6;
	u8 rsvd_0:2;

	/* offset 1 */
	u8 rts_retry_cnt:6;
	u8 rsvd_1:2;

	/* offset 2 */
	u8 ccx_qtime0;
	u8 ccx_qtime1;

	/* offset 4 */
	u8 missed_pkt_num:5;
	u8 rsvd_4:3;

	/* offset 5 */
	u8 mac_id:5;
	u8 des1_fragssn:3;

	/* offset 6 */
	u8 rpt_pkt_num:5;
	u8 pkt_drop:1;
	u8 lifetime_over:1;
	u8 retry_over:1;

	/* offset 7*/
	u8 edca_tx_queue:4;
	u8 rsvd_7:1;
	u8 bmc:1;
	u8 pkt_ok:1;
	u8 int_ccx:1;
};

#define txrpt_ccx_qtime_8192c(txrpt_ccx) ((txrpt_ccx)->ccx_qtime0+((txrpt_ccx)->ccx_qtime1<<8))

#ifdef CONFIG_XMIT_ACK
void dump_txrpt_ccx_8192c(void *buf);
void handle_txrpt_ccx_8192c(_adapter *adapter, void *buf);
#else
#define dump_txrpt_ccx_8192c(buf) do {} while(0)
#define handle_txrpt_ccx_8192c(adapter, buf) do {} while(0)
#endif

#ifdef CONFIG_USB_HCI

#ifdef CONFIG_USB_TX_AGGREGATION
#define MAX_TX_AGG_PACKET_NUMBER 0xFF
#endif

s32	rtl8192cu_init_xmit_priv(_adapter * padapter);

void	rtl8192cu_free_xmit_priv(_adapter * padapter);

void rtl8192cu_cal_txdesc_chksum(struct tx_desc	*ptxdesc);

s32 rtl8192cu_xmitframe_complete(_adapter *padapter, struct xmit_priv *pxmitpriv, struct xmit_buf *pxmitbuf);

s32 rtl8192cu_mgnt_xmit(_adapter *padapter, struct xmit_frame *pmgntframe);

s32 rtl8192cu_hal_xmit(_adapter *padapter, struct xmit_frame *pxmitframe);

#ifdef CONFIG_HOSTAPD_MLME
s32 rtl8192cu_hostap_mgnt_xmit_entry(_adapter *padapter, _pkt *pkt);
#endif

#endif

#ifdef CONFIG_PCI_HCI
s32	rtl8192ce_init_xmit_priv(_adapter * padapter);
void	rtl8192ce_free_xmit_priv(_adapter * padapter);

s32	rtl8192ce_enqueue_xmitbuf(struct rtw_tx_ring *ring, struct xmit_buf *pxmitbuf);
struct xmit_buf *rtl8192ce_dequeue_xmitbuf(struct rtw_tx_ring *ring);

void	rtl8192ce_xmitframe_resume(_adapter *padapter);

s32	rtl8192ce_mgnt_xmit(_adapter *padapter, struct xmit_frame *pmgntframe);

s32	rtl8192ce_hal_xmit(_adapter *padapter, struct xmit_frame *pxmitframe);

#ifdef CONFIG_HOSTAPD_MLME
s32	rtl8192ce_hostap_mgnt_xmit_entry(_adapter *padapter, _pkt *pkt);
#endif

#endif

#endif

