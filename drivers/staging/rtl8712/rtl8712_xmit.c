/******************************************************************************
 * rtl8712_xmit.c
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
 * Linux device driver for RTL8192SU
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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * Modifications for inclusion into the Linux staging tree are
 * Copyright(c) 2010 Larry Finger. All rights reserved.
 *
 * Contact information:
 * WLAN FAE <wlanfae@realtek.com>
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ******************************************************************************/

#define _RTL8712_XMIT_C_

#include "osdep_service.h"
#include "drv_types.h"
#include "rtl871x_byteorder.h"
#include "wifi.h"
#include "osdep_intf.h"
#include "usb_ops.h"

static void dump_xframe(struct _adapter *padapter,
			struct xmit_frame *pxmitframe);

sint _r8712_init_hw_txqueue(struct hw_txqueue *phw_txqueue, u8 ac_tag)
{
	phw_txqueue->ac_tag = ac_tag;
	switch (ac_tag) {
	case BE_QUEUE_INX:
		phw_txqueue->ff_hwaddr = RTL8712_DMA_BEQ;
		break;
	case BK_QUEUE_INX:
		phw_txqueue->ff_hwaddr = RTL8712_DMA_BKQ;
		break;
	case VI_QUEUE_INX:
		phw_txqueue->ff_hwaddr = RTL8712_DMA_VIQ;
		break;
	case VO_QUEUE_INX:
		phw_txqueue->ff_hwaddr = RTL8712_DMA_VOQ;
		break;
	case BMC_QUEUE_INX:
		phw_txqueue->ff_hwaddr = RTL8712_DMA_BEQ;
		break;
	}
	return _SUCCESS;
}

int r8712_txframes_sta_ac_pending(struct _adapter *padapter,
				  struct pkt_attrib *pattrib)
{
	struct sta_info *psta;
	struct tx_servq *ptxservq;
	int priority = pattrib->priority;

	psta = pattrib->psta;
	switch (priority) {
	case 1:
	case 2:
		ptxservq = &(psta->sta_xmitpriv.bk_q);
		break;
	case 4:
	case 5:
		ptxservq = &(psta->sta_xmitpriv.vi_q);
		break;
	case 6:
	case 7:
		ptxservq = &(psta->sta_xmitpriv.vo_q);
		break;
	case 0:
	case 3:
	default:
		ptxservq = &(psta->sta_xmitpriv.be_q);
	break;
	}
	return ptxservq->qcnt;
}

static u32 get_ff_hwaddr(struct xmit_frame *pxmitframe)
{
	u32 addr = 0;
	struct pkt_attrib *pattrib = &pxmitframe->attrib;
	struct _adapter *padapter = pxmitframe->padapter;
	struct dvobj_priv *pdvobj = (struct dvobj_priv *)&padapter->dvobjpriv;

	if (pxmitframe->frame_tag == TXAGG_FRAMETAG)
		addr = RTL8712_DMA_H2CCMD;
	else if (pxmitframe->frame_tag == MGNT_FRAMETAG)
		addr = RTL8712_DMA_MGTQ;
	else if (pdvobj->nr_endpoint == 6) {
		switch (pattrib->priority) {
		case 0:
		case 3:
			addr = RTL8712_DMA_BEQ;
			break;
		case 1:
		case 2:
			addr = RTL8712_DMA_BKQ;
			break;
		case 4:
		case 5:
			addr = RTL8712_DMA_VIQ;
			break;
		case 6:
		case 7:
			addr = RTL8712_DMA_VOQ;
			break;
		case 0x10:
		case 0x11:
		case 0x12:
		case 0x13:
			addr = RTL8712_DMA_H2CCMD;
			break;
		default:
			addr = RTL8712_DMA_BEQ;
			break;
		}
	} else if (pdvobj->nr_endpoint == 4) {
		switch (pattrib->qsel) {
		case 0:
		case 3:
		case 1:
		case 2:
			addr = RTL8712_DMA_BEQ;/*RTL8712_EP_LO;*/
			break;
		case 4:
		case 5:
		case 6:
		case 7:
			addr = RTL8712_DMA_VOQ;/*RTL8712_EP_HI;*/
			break;
		case 0x10:
		case 0x11:
		case 0x12:
		case 0x13:
			addr = RTL8712_DMA_H2CCMD;;
			break;
		default:
			addr = RTL8712_DMA_BEQ;/*RTL8712_EP_LO;*/
			break;
		}
	}
	return addr;
}

static struct xmit_frame *dequeue_one_xmitframe(struct xmit_priv *pxmitpriv,
					 struct hw_xmit *phwxmit,
					 struct tx_servq *ptxservq,
					 struct  __queue *pframe_queue)
{
	struct list_head *xmitframe_plist, *xmitframe_phead;
	struct	xmit_frame *pxmitframe = NULL;

	xmitframe_phead = get_list_head(pframe_queue);
	xmitframe_plist = get_next(xmitframe_phead);
	if ((end_of_queue_search(xmitframe_phead, xmitframe_plist)) == false) {
		pxmitframe = LIST_CONTAINOR(xmitframe_plist,
			     struct xmit_frame, list);
		list_delete(&pxmitframe->list);
		ptxservq->qcnt--;
		phwxmit->txcmdcnt++;
	}
	return pxmitframe;
}

static struct xmit_frame *dequeue_xframe_ex(struct xmit_priv *pxmitpriv,
				     struct hw_xmit *phwxmit_i, sint entry)
{
	unsigned long irqL0;
	struct list_head *sta_plist, *sta_phead;
	struct hw_xmit *phwxmit;
	struct tx_servq *ptxservq = NULL;
	struct  __queue *pframe_queue = NULL;
	struct	xmit_frame *pxmitframe = NULL;
	int i, inx[4];
	int j, tmp, acirp_cnt[4];

	/*entry indx: 0->vo, 1->vi, 2->be, 3->bk.*/
	inx[0] = 0; acirp_cnt[0] = pxmitpriv->voq_cnt;
	inx[1] = 1; acirp_cnt[1] = pxmitpriv->viq_cnt;
	inx[2] = 2; acirp_cnt[2] = pxmitpriv->beq_cnt;
	inx[3] = 3; acirp_cnt[3] = pxmitpriv->bkq_cnt;
	for (i = 0; i < 4; i++) {
		for (j = i + 1; j < 4; j++) {
			if (acirp_cnt[j] < acirp_cnt[i]) {
				tmp = acirp_cnt[i];
				acirp_cnt[i] = acirp_cnt[j];
				acirp_cnt[j] = tmp;
				tmp = inx[i];
				inx[i] = inx[j];
				inx[j] = tmp;
			}
		}
	}
	spin_lock_irqsave(&pxmitpriv->lock, irqL0);
	for (i = 0; i < entry; i++) {
		phwxmit = phwxmit_i + inx[i];
		sta_phead = get_list_head(phwxmit->sta_queue);
		sta_plist = get_next(sta_phead);
		while ((end_of_queue_search(sta_phead, sta_plist)) == false) {
			ptxservq = LIST_CONTAINOR(sta_plist, struct tx_servq,
				  tx_pending);
			pframe_queue = &ptxservq->sta_pending;
			pxmitframe = dequeue_one_xmitframe(pxmitpriv, phwxmit,
				     ptxservq, pframe_queue);
			if (pxmitframe) {
				phwxmit->accnt--;
				goto exit_dequeue_xframe_ex;
			}
			sta_plist = get_next(sta_plist);
			/*Remove sta node when there are no pending packets.*/
			if (_queue_empty(pframe_queue)) {
				/*must be done after get_next and before break*/
				list_delete(&ptxservq->tx_pending);
			}
		}
	}
exit_dequeue_xframe_ex:
	spin_unlock_irqrestore(&pxmitpriv->lock, irqL0);
	return pxmitframe;
}

void r8712_do_queue_select(struct _adapter *padapter,
			   struct pkt_attrib *pattrib)
{
	u8 qsel = 0;
	struct dvobj_priv *pdvobj = (struct dvobj_priv *)&padapter->dvobjpriv;

	if (pdvobj->nr_endpoint == 6)
		qsel = pattrib->priority;
	else if (pdvobj->nr_endpoint == 4)
		qsel = pattrib->priority;
	pattrib->qsel = qsel;
}

static void update_txdesc(struct xmit_frame *pxmitframe, uint *pmem, int sz)
{
	uint qsel;
	struct _adapter *padapter = pxmitframe->padapter;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct qos_priv *pqospriv = &pmlmepriv->qospriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct pkt_attrib *pattrib = &pxmitframe->attrib;
	struct tx_desc *ptxdesc = (struct tx_desc *)pmem;
	struct dvobj_priv *pdvobj = (struct dvobj_priv *)&padapter->dvobjpriv;
	u8 blnSetTxDescOffset;
	sint bmcst = IS_MCAST(pattrib->ra);
	struct ht_priv *phtpriv = &pmlmepriv->htpriv;
	struct tx_desc txdesc_mp;

	memcpy(&txdesc_mp, ptxdesc, sizeof(struct tx_desc));
	memset(ptxdesc, 0, sizeof(struct tx_desc));
	/* offset 0 */
	ptxdesc->txdw0 |= cpu_to_le32(sz&0x0000ffff);
	if (pdvobj->ishighspeed) {
		if (((sz + TXDESC_SIZE) % 512) == 0)
			blnSetTxDescOffset = 1;
		else
			blnSetTxDescOffset = 0;
	} else {
		if (((sz + TXDESC_SIZE) % 64) == 0)
			blnSetTxDescOffset = 1;
		else
			blnSetTxDescOffset = 0;
	}
	if (blnSetTxDescOffset) {
		/* 32 bytes for TX Desc + 8 bytes pending */
		ptxdesc->txdw0 |= cpu_to_le32(((TXDESC_SIZE+OFFSET_SZ + 8) <<
			      OFFSET_SHT) & 0x00ff0000);
	} else {
		/* default = 32 bytes for TX Desc */
		ptxdesc->txdw0 |= cpu_to_le32(((TXDESC_SIZE+OFFSET_SZ) <<
				  OFFSET_SHT) & 0x00ff0000);
	}
	ptxdesc->txdw0 |= cpu_to_le32(OWN | FSG | LSG);
	if (pxmitframe->frame_tag == DATA_FRAMETAG) {
		/* offset 4 */
		ptxdesc->txdw1 |= cpu_to_le32((pattrib->mac_id)&0x1f);
		qsel = (uint)(pattrib->qsel & 0x0000001f);
		ptxdesc->txdw1 |= cpu_to_le32((qsel << QSEL_SHT) & 0x00001f00);
		if (!pqospriv->qos_option)
			ptxdesc->txdw1 |= cpu_to_le32(BIT(16));/*Non-QoS*/
		if ((pattrib->encrypt > 0) && !pattrib->bswenc) {
			switch (pattrib->encrypt) {	/*SEC_TYPE*/
			case _WEP40_:
			case _WEP104_:
				ptxdesc->txdw1 |= cpu_to_le32((0x01 << 22) &
						  0x00c00000);
				/*KEY_ID when WEP is used;*/
				ptxdesc->txdw1 |= cpu_to_le32((psecuritypriv->
						  PrivacyKeyIndex << 17) &
						  0x00060000);
				break;
			case _TKIP_:
			case _TKIP_WTMIC_:
				ptxdesc->txdw1 |= cpu_to_le32((0x02 << 22) &
						  0x00c00000);
				break;
			case _AES_:
				ptxdesc->txdw1 |= cpu_to_le32((0x03 << 22) &
						  0x00c00000);
				break;
			case _NO_PRIVACY_:
			default:
				break;
			}
		}
		/*offset 8*/
		if (bmcst)
			ptxdesc->txdw2 |= cpu_to_le32(BMC);

		/*offset 12*/
		/* f/w will increase the seqnum by itself, driver pass the
		 * correct priority to fw
		 * fw will check the correct priority for increasing the
		 * seqnum per tid. about usb using 4-endpoint, qsel points out
		 * the correct mapping between AC&Endpoint,
		 * the purpose is that correct mapping lets the MAC release
		 * the AC Queue list correctly. */
		ptxdesc->txdw3 = cpu_to_le32((pattrib->priority << SEQ_SHT) &
				 0x0fff0000);
		if ((pattrib->ether_type != 0x888e) &&
		    (pattrib->ether_type != 0x0806) &&
		    (pattrib->dhcp_pkt != 1)) {
			/*Not EAP & ARP type data packet*/
			if (phtpriv->ht_option == 1) { /*B/G/N Mode*/
				if (phtpriv->ampdu_enable != true)
					ptxdesc->txdw2 |= cpu_to_le32(BK);
			}
		} else {
			/* EAP data packet and ARP packet.
			 * Use the 1M data rate to send the EAP/ARP packet.
			 * This will maybe make the handshake smooth.
			 */
			/*driver uses data rate*/
			ptxdesc->txdw4 = cpu_to_le32(0x80000000);
			ptxdesc->txdw5 = cpu_to_le32(0x001f8000);/*1M*/
		}
		if (pattrib->pctrl == 1) { /* mp tx packets */
			struct tx_desc *ptxdesc_mp;
			ptxdesc_mp = &txdesc_mp;
			/* offset 8 */
			ptxdesc->txdw2 = cpu_to_le32(ptxdesc_mp->txdw2);
			if (bmcst)
				ptxdesc->txdw2 |= cpu_to_le32(BMC);
			ptxdesc->txdw2 |= cpu_to_le32(BK);
			/* offset 16 */
			ptxdesc->txdw4 = cpu_to_le32(ptxdesc_mp->txdw4);
			/* offset 20 */
			ptxdesc->txdw5 = cpu_to_le32(ptxdesc_mp->txdw5);
			pattrib->pctrl = 0;/* reset to zero; */
		}
	} else if (pxmitframe->frame_tag == MGNT_FRAMETAG) {
		/* offset 4 */
		ptxdesc->txdw1 |= (0x05) & 0x1f;/*CAM_ID(MAC_ID), default=5;*/
		qsel = (uint)(pattrib->qsel & 0x0000001f);
		ptxdesc->txdw1 |= cpu_to_le32((qsel << QSEL_SHT) & 0x00001f00);
		ptxdesc->txdw1 |= cpu_to_le32(BIT(16));/* Non-QoS */
		/* offset 8 */
		if (bmcst)
			ptxdesc->txdw2 |= cpu_to_le32(BMC);
		/* offset 12 */
		/* f/w will increase the seqnum by itself, driver pass the
		 * correct priority to fw
		 * fw will check the correct priority for increasing the seqnum
		 * per tid. about usb using 4-endpoint, qsel points out the
		 * correct mapping between AC&Endpoint,
		 * the purpose is that correct mapping let the MAC releases
		 * the AC Queue list correctly. */
		ptxdesc->txdw3 = cpu_to_le32((pattrib->priority << SEQ_SHT) &
					      0x0fff0000);
		/* offset 16 */
		ptxdesc->txdw4 = cpu_to_le32(0x80002040);/*gtest*/
		/* offset 20 */
		ptxdesc->txdw5 = cpu_to_le32(0x001f8000);/* gtest 1M */
	} else if (pxmitframe->frame_tag == TXAGG_FRAMETAG) {
		/* offset 4 */
		qsel = 0x13;
		ptxdesc->txdw1 |= cpu_to_le32((qsel << QSEL_SHT) & 0x00001f00);
	} else {
		/* offset 4 */
		qsel = (uint)(pattrib->priority&0x0000001f);
		ptxdesc->txdw1 |= cpu_to_le32((qsel << QSEL_SHT) & 0x00001f00);
		/*offset 8*/
		/*offset 12*/
		ptxdesc->txdw3 = cpu_to_le32((pattrib->seqnum << SEQ_SHT) &
					      0x0fff0000);
		/*offset 16*/
		ptxdesc->txdw4 = cpu_to_le32(0x80002040);/*gtest*/
		/*offset 20*/
		ptxdesc->txdw5 = cpu_to_le32(0x001f9600);/*gtest*/
	}
}

int r8712_xmitframe_complete(struct _adapter *padapter,
			     struct xmit_priv *pxmitpriv,
			     struct xmit_buf *pxmitbuf)
{
	struct hw_xmit *phwxmits;
	sint hwentry;
	struct xmit_frame *pxmitframe = NULL;
	int res = _SUCCESS, xcnt = 0;

	phwxmits = pxmitpriv->hwxmits;
	hwentry = pxmitpriv->hwxmit_entry;
	if (pxmitbuf == NULL) {
		pxmitbuf = r8712_alloc_xmitbuf(pxmitpriv);
		if (!pxmitbuf)
			return false;
	}
	do {
		pxmitframe = dequeue_xframe_ex(pxmitpriv, phwxmits, hwentry);
		if (pxmitframe) {
			pxmitframe->pxmitbuf = pxmitbuf;
			pxmitframe->pxmit_urb[0] = pxmitbuf->pxmit_urb[0];
			pxmitframe->buf_addr = pxmitbuf->pbuf;
			if (pxmitframe->frame_tag == DATA_FRAMETAG) {
				if (pxmitframe->attrib.priority <= 15)
					res = r8712_xmitframe_coalesce(padapter,
					      pxmitframe->pkt, pxmitframe);
				/* always return ndis_packet after
				 *  r8712_xmitframe_coalesce */
				r8712_xmit_complete(padapter, pxmitframe);
			}
			if (res == _SUCCESS)
				dump_xframe(padapter, pxmitframe);
			else
				r8712_free_xmitframe_ex(pxmitpriv, pxmitframe);
			xcnt++;
		} else {
			r8712_free_xmitbuf(pxmitpriv, pxmitbuf);
			return false;
		}
		break;
	} while (0);
	return true;
}

static void dump_xframe(struct _adapter *padapter,
			struct xmit_frame *pxmitframe)
{
	int t, sz, w_sz;
	u8 *mem_addr;
	u32 ff_hwaddr;
	struct pkt_attrib *pattrib = &pxmitframe->attrib;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;

	if (pxmitframe->attrib.ether_type != 0x0806) {
		if (pxmitframe->attrib.ether_type != 0x888e)
			r8712_issue_addbareq_cmd(padapter, pattrib->priority);
	}
	mem_addr = pxmitframe->buf_addr;
	for (t = 0; t < pattrib->nr_frags; t++) {
		if (t != (pattrib->nr_frags - 1)) {
			sz = pxmitpriv->frag_len;
			sz = sz - 4 - (psecuritypriv->sw_encrypt ? 0 :
			     pattrib->icv_len);
			pxmitframe->last[t] = 0;
		} else {
			sz = pattrib->last_txcmdsz;
			pxmitframe->last[t] = 1;
		}
		update_txdesc(pxmitframe, (uint *)mem_addr, sz);
		w_sz = sz + TXDESC_SIZE;
		pxmitframe->mem_addr = mem_addr;
		pxmitframe->bpending[t] = false;
		ff_hwaddr = get_ff_hwaddr(pxmitframe);
		r8712_write_port(padapter, ff_hwaddr, w_sz,
			   (unsigned char *)pxmitframe);
		mem_addr += w_sz;
		mem_addr = (u8 *)RND4(((addr_t)(mem_addr)));
	}
}

int r8712_xmit_direct(struct _adapter *padapter, struct xmit_frame *pxmitframe)
{
	int res = _SUCCESS;

	res = r8712_xmitframe_coalesce(padapter, pxmitframe->pkt, pxmitframe);
	pxmitframe->pkt = NULL;
	if (res == _SUCCESS)
		dump_xframe(padapter, pxmitframe);
	return res;
}

int r8712_xmit_enqueue(struct _adapter *padapter, struct xmit_frame *pxmitframe)
{
	if (r8712_xmit_classifier(padapter, pxmitframe) == _FAIL) {
		pxmitframe->pkt = NULL;
		return _FAIL;
	}
	return _SUCCESS;
}
