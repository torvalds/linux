/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
 *
 * Modifications for inclusion into the Linux staging tree are
 * Copyright(c) 2010 Larry Finger. All rights reserved.
 *
 * Contact information:
 * WLAN FAE <wlanfae@realtek.com>
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ******************************************************************************/
#ifndef _RTL8712_XMIT_H_
#define _RTL8712_XMIT_H_

#define HWXMIT_ENTRY	4

#define VO_QUEUE_INX	0
#define VI_QUEUE_INX	1
#define BE_QUEUE_INX	2
#define BK_QUEUE_INX	3
#define TS_QUEUE_INX	4
#define MGT_QUEUE_INX	5
#define BMC_QUEUE_INX	6
#define BCN_QUEUE_INX	7

#define HW_QUEUE_ENTRY	8

#define TXDESC_SIZE 32
#define TXDESC_OFFSET TXDESC_SIZE

#define NR_AMSDU_XMITFRAME 8
#define NR_TXAGG_XMITFRAME 8

#define MAX_AMSDU_XMITBUF_SZ 8704
#define MAX_TXAGG_XMITBUF_SZ 16384 /*16k*/


#define tx_cmd tx_desc


/*
 *defined for TX DESC Operation
 */

#define MAX_TID (15)

/*OFFSET 0*/
#define OFFSET_SZ (0)
#define OFFSET_SHT (16)
#define OWN	BIT(31)
#define FSG	BIT(27)
#define LSG	BIT(26)
#define TYPE_SHT (24)
#define TYPE_MSK (0x03000000)

/*OFFSET 4*/
#define PKT_OFFSET_SZ (0)
#define QSEL_SHT (8)
#define HWPC BIT(31)

/*OFFSET 8*/
#define BMC BIT(7)
#define BK BIT(30)
#define AGG_EN BIT(29)
#define RTS_RC_SHT (16)

/*OFFSET 12*/
#define SEQ_SHT (16)

/*OFFSET 16*/
#define TXBW BIT(18)

/*OFFSET 20*/
#define DISFB BIT(15)
#define RSVD6_MSK (0x00E00000)
#define RSVD6_SHT (21)

struct tx_desc {
	/*DWORD 0*/
	__le32 txdw0;
	__le32 txdw1;
	__le32 txdw2;
	__le32 txdw3;
	__le32 txdw4;
	__le32 txdw5;
	__le32 txdw6;
	__le32 txdw7;
};


union txdesc {
	struct tx_desc txdesc;
	unsigned int value[TXDESC_SIZE>>2];
};

int r8712_xmitframe_complete(struct _adapter *padapter,
			     struct xmit_priv *pxmitpriv,
			     struct xmit_buf *pxmitbuf);
void r8712_do_queue_select(struct _adapter *padapter,
			   struct pkt_attrib *pattrib);

#ifdef CONFIG_R8712_TX_AGGR
u8 r8712_xmitframe_aggr_1st(struct xmit_buf *pxmitbuf,
			struct xmit_frame *pxmitframe);
u8 r8712_dump_aggr_xframe(struct xmit_buf *pxmitbuf,
			struct xmit_frame *pxmitframe);
#endif

#endif
