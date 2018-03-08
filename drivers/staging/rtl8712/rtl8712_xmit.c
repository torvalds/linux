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
#include "wifi.h"
#include "osdep_intf.h"
#include "usb_ops.h"

static void dump_xframe(struct _adapter *padapter,
			struct xmit_frame *pxmitframe);
static void update_txdesc(struct xmit_frame *pxmitframe, uint *pmem, int sz);

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
	struct dvobj_priv *pdvobj = &padapter->dvobjpriv;

	if (pxmitframe->frame_tag == TXAGG_FRAMETAG) {
		addr = RTL8712_DMA_H2CCMD;
	} else if (pxmitframe->frame_tag == MGNT_FRAMETAG) {
		addr = RTL8712_DMA_MGTQ;
	} else if (pdvobj->nr_endpoint == 6) {
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
			addr = RTL8712_DMA_H2CCMD;
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

	xmitframe_phead = &pframe_queue->queue;
	xmitframe_plist = xmitframe_phead->next;
	if (!end_of_queue_search(xmitframe_phead, xmitframe_plist)) {
		pxmitframe = container_of(xmitframe_plist,
					  struct xmit_frame, list);
		list_del_init(&pxmitframe->list);
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
	int j, acirp_cnt[4];

	/*entry indx: 0->vo, 1->vi, 2->be, 3->bk.*/
	inx[0] = 0; acirp_cnt[0] = pxmitpriv->voq_cnt;
	inx[1] = 1; acirp_cnt[1] = pxmitpriv->viq_cnt;
	inx[2] = 2; acirp_cnt[2] = pxmitpriv->beq_cnt;
	inx[3] = 3; acirp_cnt[3] = pxmitpriv->bkq_cnt;
	for (i = 0; i < 4; i++) {
		for (j = i + 1; j < 4; j++) {
			if (acirp_cnt[j] < acirp_cnt[i]) {
				swap(acirp_cnt[i], acirp_cnt[j]);
				swap(inx[i], inx[j]);
			}
		}
	}
	spin_lock_irqsave(&pxmitpriv->lock, irqL0);
	for (i = 0; i < entry; i++) {
		phwxmit = phwxmit_i + inx[i];
		sta_phead = &phwxmit->sta_queue->queue;
		sta_plist = sta_phead->next;
		while (!end_of_queue_search(sta_phead, sta_plist)) {
			ptxservq = container_of(sta_plist, struct tx_servq,
						tx_pending);
			pframe_queue = &ptxservq->sta_pending;
			pxmitframe = dequeue_one_xmitframe(pxmitpriv, phwxmit,
				     ptxservq, pframe_queue);
			if (pxmitframe) {
				phwxmit->accnt--;
				goto exit_dequeue_xframe_ex;
			}
			sta_plist = sta_plist->next;
			/*Remove sta node when there are no pending packets.*/
			if (list_empty(&pframe_queue->queue)) {
				/* must be done after sta_plist->next
				 * and before break
				 */
				list_del_init(&ptxservq->tx_pending);
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
	unsigned int qsel = 0;
	struct dvobj_priv *pdvobj = &padapter->dvobjpriv;

	if (pdvobj->nr_endpoint == 6) {
		qsel = (unsigned int) pattrib->priority;
	} else if (pdvobj->nr_endpoint == 4) {
		qsel = (unsigned int) pattrib->priority;
		if (qsel == 0 || qsel == 3)
			qsel = 3;
		else if (qsel == 1 || qsel == 2)
			qsel = 1;
		else if (qsel == 4 || qsel == 5)
			qsel = 5;
		else if (qsel == 6 || qsel == 7)
			qsel = 7;
		else
			qsel = 3;
	}
	pattrib->qsel = qsel;
}

#ifdef CONFIG_R8712_TX_AGGR
u8 r8712_construct_txaggr_cmd_desc(struct xmit_buf *pxmitbuf)
{
	struct tx_desc *ptx_desc = (struct tx_desc *)pxmitbuf->pbuf;

	/* Fill up TxCmd Descriptor according as USB FW Tx Aaggregation info.*/
	/* dw0 */
	ptx_desc->txdw0 = cpu_to_le32(CMD_HDR_SZ & 0xffff);
	ptx_desc->txdw0 |=
		cpu_to_le32(((TXDESC_SIZE + OFFSET_SZ) << OFFSET_SHT) &
			    0x00ff0000);
	ptx_desc->txdw0 |= cpu_to_le32(OWN | FSG | LSG);

	/* dw1 */
	ptx_desc->txdw1 |= cpu_to_le32((0x13 << QSEL_SHT) & 0x00001f00);

	return _SUCCESS;
}

u8 r8712_construct_txaggr_cmd_hdr(struct xmit_buf *pxmitbuf)
{
	struct xmit_frame *pxmitframe = (struct xmit_frame *)
		pxmitbuf->priv_data;
	struct _adapter *padapter = pxmitframe->padapter;
	struct cmd_priv *pcmdpriv = &(padapter->cmdpriv);
	struct cmd_hdr *pcmd_hdr = (struct cmd_hdr  *)
		(pxmitbuf->pbuf + TXDESC_SIZE);

	/* Fill up Cmd Header for USB FW Tx Aggregation.*/
	/* dw0 */
	pcmd_hdr->cmd_dw0 = cpu_to_le32((GEN_CMD_CODE(_AMSDU_TO_AMPDU) << 16) |
					(pcmdpriv->cmd_seq << 24));
	pcmdpriv->cmd_seq++;

	return _SUCCESS;
}

u8 r8712_append_mpdu_unit(struct xmit_buf *pxmitbuf,
			struct xmit_frame *pxmitframe)
{
	struct _adapter *padapter = pxmitframe->padapter;
	struct tx_desc *ptx_desc = (struct tx_desc *)pxmitbuf->pbuf;
	int last_txcmdsz = 0;
	int padding_sz = 0;

	/* 802.3->802.11 converter */
	r8712_xmitframe_coalesce(padapter, pxmitframe->pkt, pxmitframe);
	/* free skb struct */
	r8712_xmit_complete(padapter, pxmitframe);
	if (pxmitframe->attrib.ether_type != 0x0806) {
		if ((pxmitframe->attrib.ether_type != 0x888e) &&
			(pxmitframe->attrib.dhcp_pkt != 1)) {
			r8712_issue_addbareq_cmd(padapter,
					pxmitframe->attrib.priority);
		}
	}
	pxmitframe->last[0] = 1;
	update_txdesc(pxmitframe, (uint *)(pxmitframe->buf_addr),
		pxmitframe->attrib.last_txcmdsz);
	/*padding zero */
	last_txcmdsz = pxmitframe->attrib.last_txcmdsz;
	padding_sz = (8 - (last_txcmdsz % 8));
	if ((last_txcmdsz % 8) != 0) {
		int i;

		for (i = 0; i < padding_sz; i++)
			*(pxmitframe->buf_addr + TXDESC_SIZE + last_txcmdsz +
			  i) = 0;
	}
	/* Add the new mpdu's length */
	ptx_desc->txdw0 = cpu_to_le32((ptx_desc->txdw0 & 0xffff0000) |
		((ptx_desc->txdw0 & 0x0000ffff) +
			((TXDESC_SIZE + last_txcmdsz + padding_sz) &
			 0x0000ffff)));

	return _SUCCESS;
}


u8 r8712_xmitframe_aggr_1st(struct xmit_buf *pxmitbuf,
			struct xmit_frame *pxmitframe)
{
	/* linux complete context doesn't need to protect */
	pxmitframe->pxmitbuf = pxmitbuf;
	pxmitbuf->priv_data = pxmitframe;
	pxmitframe->pxmit_urb[0] = pxmitbuf->pxmit_urb[0];
	/* buffer addr assoc */
	pxmitframe->buf_addr = pxmitbuf->pbuf + TXDESC_SIZE + CMD_HDR_SZ;
	/*RTL8712_DMA_H2CCMD */
	r8712_construct_txaggr_cmd_desc(pxmitbuf);
	r8712_construct_txaggr_cmd_hdr(pxmitbuf);
	if (r8712_append_mpdu_unit(pxmitbuf, pxmitframe) == _SUCCESS)
		pxmitbuf->aggr_nr = 1;

	return _SUCCESS;
}

u16 r8712_xmitframe_aggr_next(struct xmit_buf *pxmitbuf,
			struct xmit_frame *pxmitframe)
{
	pxmitframe->pxmitbuf = pxmitbuf;
	pxmitbuf->priv_data = pxmitframe;
	pxmitframe->pxmit_urb[0] = pxmitbuf->pxmit_urb[0];
	/* buffer addr assoc */
	pxmitframe->buf_addr = pxmitbuf->pbuf + TXDESC_SIZE +
		(((struct tx_desc *)pxmitbuf->pbuf)->txdw0 & 0x0000ffff);
	if (r8712_append_mpdu_unit(pxmitbuf, pxmitframe) == _SUCCESS) {
		r8712_free_xmitframe_ex(&pxmitframe->padapter->xmitpriv,
					pxmitframe);
		pxmitbuf->aggr_nr++;
	}

	return TXDESC_SIZE +
		(((struct tx_desc *)pxmitbuf->pbuf)->txdw0 & 0x0000ffff);
}

u8 r8712_dump_aggr_xframe(struct xmit_buf *pxmitbuf,
			struct xmit_frame *pxmitframe)
{
	struct _adapter *padapter = pxmitframe->padapter;
	struct dvobj_priv *pdvobj = &padapter->dvobjpriv;
	struct tx_desc *ptxdesc = pxmitbuf->pbuf;
	struct cmd_hdr *pcmd_hdr = (struct cmd_hdr *)
		(pxmitbuf->pbuf + TXDESC_SIZE);
	u16 total_length = (u16) (ptxdesc->txdw0 & 0xffff);

	/* use 1st xmitframe as media */
	xmitframe_xmitbuf_attach(pxmitframe, pxmitbuf);
	pcmd_hdr->cmd_dw0 = cpu_to_le32(((total_length - CMD_HDR_SZ) &
					 0x0000ffff) | (pcmd_hdr->cmd_dw0 &
							0xffff0000));

	/* urb length in cmd_dw1 */
	pcmd_hdr->cmd_dw1 = cpu_to_le32((pxmitbuf->aggr_nr & 0xff)|
					((total_length + TXDESC_SIZE) << 16));
	pxmitframe->last[0] = 1;
	pxmitframe->bpending[0] = false;
	pxmitframe->mem_addr = pxmitbuf->pbuf;

	if ((pdvobj->ishighspeed && ((total_length + TXDESC_SIZE) % 0x200) ==
	     0) || ((!pdvobj->ishighspeed && ((total_length + TXDESC_SIZE) %
					      0x40) == 0))) {
		ptxdesc->txdw0 |= cpu_to_le32
			(((TXDESC_SIZE + OFFSET_SZ + 8) << OFFSET_SHT) &
			 0x00ff0000);
		/*32 bytes for TX Desc + 8 bytes pending*/
	} else {
		ptxdesc->txdw0 |= cpu_to_le32
			(((TXDESC_SIZE + OFFSET_SZ) << OFFSET_SHT) &
			 0x00ff0000);
		/*default = 32 bytes for TX Desc*/
	}
	r8712_write_port(pxmitframe->padapter, RTL8712_DMA_H2CCMD,
			total_length + TXDESC_SIZE, (u8 *)pxmitframe);

	return _SUCCESS;
}

#endif

static void update_txdesc(struct xmit_frame *pxmitframe, uint *pmem, int sz)
{
	uint qsel;
	struct _adapter *padapter = pxmitframe->padapter;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct qos_priv *pqospriv = &pmlmepriv->qospriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct pkt_attrib *pattrib = &pxmitframe->attrib;
	struct tx_desc *ptxdesc = (struct tx_desc *)pmem;
	struct dvobj_priv *pdvobj = &padapter->dvobjpriv;
#ifdef CONFIG_R8712_TX_AGGR
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
#endif
	u8 blnSetTxDescOffset;
	sint bmcst = IS_MCAST(pattrib->ra);
	struct ht_priv *phtpriv = &pmlmepriv->htpriv;
	struct tx_desc txdesc_mp;

	memcpy(&txdesc_mp, ptxdesc, sizeof(struct tx_desc));
	memset(ptxdesc, 0, sizeof(struct tx_desc));
	/* offset 0 */
	ptxdesc->txdw0 |= cpu_to_le32(sz & 0x0000ffff);
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
		ptxdesc->txdw0 |= cpu_to_le32(((TXDESC_SIZE + OFFSET_SZ + 8) <<
			      OFFSET_SHT) & 0x00ff0000);
	} else {
		/* default = 32 bytes for TX Desc */
		ptxdesc->txdw0 |= cpu_to_le32(((TXDESC_SIZE + OFFSET_SZ) <<
				  OFFSET_SHT) & 0x00ff0000);
	}
	ptxdesc->txdw0 |= cpu_to_le32(OWN | FSG | LSG);
	if (pxmitframe->frame_tag == DATA_FRAMETAG) {
		/* offset 4 */
		ptxdesc->txdw1 |= cpu_to_le32((pattrib->mac_id) & 0x1f);

#ifdef CONFIG_R8712_TX_AGGR
		/* dirty workaround, need to check if it is aggr cmd. */
		if ((u8 *)pmem != (u8 *)pxmitframe->pxmitbuf->pbuf) {
			ptxdesc->txdw0 |= cpu_to_le32
				((0x3 << TYPE_SHT) & TYPE_MSK);
			qsel = (uint)(pattrib->qsel & 0x0000001f);
			if (qsel == 2)
				qsel = 0;
			ptxdesc->txdw1 |= cpu_to_le32
				((qsel << QSEL_SHT) & 0x00001f00);
			ptxdesc->txdw2 = cpu_to_le32
				((qsel << RTS_RC_SHT) & 0x001f0000);
			ptxdesc->txdw6 |= cpu_to_le32
				((0x5 << RSVD6_SHT) & RSVD6_MSK);
		} else {
			ptxdesc->txdw0 |= cpu_to_le32
				((0x3 << TYPE_SHT) & TYPE_MSK);
			ptxdesc->txdw1 |= cpu_to_le32
				((0x13 << QSEL_SHT) & 0x00001f00);
			qsel = (uint)(pattrib->qsel & 0x0000001f);
			if (qsel == 2)
				qsel = 0;
			ptxdesc->txdw2 = cpu_to_le32
				((qsel << RTS_RC_SHT) & 0x0001f000);
			ptxdesc->txdw7 |= cpu_to_le32
				(pcmdpriv->cmd_seq << 24);
			pcmdpriv->cmd_seq++;
		}
		pattrib->qsel = 0x13;
#else
		qsel = (uint)(pattrib->qsel & 0x0000001f);
		ptxdesc->txdw1 |= cpu_to_le32((qsel << QSEL_SHT) & 0x00001f00);
#endif
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
		 * the AC Queue list correctly.
		 */
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
			ptxdesc->txdw2 = ptxdesc_mp->txdw2;
			if (bmcst)
				ptxdesc->txdw2 |= cpu_to_le32(BMC);
			ptxdesc->txdw2 |= cpu_to_le32(BK);
			/* offset 16 */
			ptxdesc->txdw4 = ptxdesc_mp->txdw4;
			/* offset 20 */
			ptxdesc->txdw5 = ptxdesc_mp->txdw5;
			pattrib->pctrl = 0;/* reset to zero; */
		}
	} else if (pxmitframe->frame_tag == MGNT_FRAMETAG) {
		/* offset 4 */
		/* CAM_ID(MAC_ID), default=5; */
		ptxdesc->txdw1 |= cpu_to_le32((0x05) & 0x1f);
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
		 * the AC Queue list correctly.
		 */
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
		qsel = (uint)(pattrib->priority & 0x0000001f);
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
#ifdef CONFIG_R8712_TX_AGGR
	struct xmit_frame *p2ndxmitframe = NULL;
#else
	int res = _SUCCESS, xcnt = 0;
#endif

	phwxmits = pxmitpriv->hwxmits;
	hwentry = pxmitpriv->hwxmit_entry;
	if (!pxmitbuf) {
		pxmitbuf = r8712_alloc_xmitbuf(pxmitpriv);
		if (!pxmitbuf)
			return false;
#ifdef CONFIG_R8712_TX_AGGR
		pxmitbuf->aggr_nr = 0;
#endif
	}
	/* 1st frame dequeued */
	pxmitframe = dequeue_xframe_ex(pxmitpriv, phwxmits, hwentry);
	/* need to remember the 1st frame */
	if (pxmitframe) {

#ifdef CONFIG_R8712_TX_AGGR
		/* 1. dequeue 2nd frame
		 * 2. aggr if 2nd xframe is dequeued, else dump directly
		 */
		if (AGGR_NR_HIGH_BOUND > 1)
			p2ndxmitframe = dequeue_xframe_ex(pxmitpriv, phwxmits,
							hwentry);
		if (pxmitframe->frame_tag != DATA_FRAMETAG) {
			r8712_free_xmitbuf(pxmitpriv, pxmitbuf);
			return false;
		}
		if (p2ndxmitframe)
			if (p2ndxmitframe->frame_tag != DATA_FRAMETAG) {
				r8712_free_xmitbuf(pxmitpriv, pxmitbuf);
				return false;
			}
		r8712_xmitframe_aggr_1st(pxmitbuf, pxmitframe);
		if (p2ndxmitframe) {
			u16 total_length;

			total_length = r8712_xmitframe_aggr_next(
				pxmitbuf, p2ndxmitframe);
			do {
				p2ndxmitframe = dequeue_xframe_ex(
					pxmitpriv, phwxmits, hwentry);
				if (p2ndxmitframe)
					total_length =
						r8712_xmitframe_aggr_next(
							pxmitbuf,
							p2ndxmitframe);
				else
					break;
			} while (total_length <= 0x1800 &&
				pxmitbuf->aggr_nr <= AGGR_NR_HIGH_BOUND);
		}
		if (pxmitbuf->aggr_nr > 0)
			r8712_dump_aggr_xframe(pxmitbuf, pxmitframe);

#else

		xmitframe_xmitbuf_attach(pxmitframe, pxmitbuf);
		if (pxmitframe->frame_tag == DATA_FRAMETAG) {
			if (pxmitframe->attrib.priority <= 15)
				res = r8712_xmitframe_coalesce(padapter,
					pxmitframe->pkt, pxmitframe);
			/* always return ndis_packet after
			 * r8712_xmitframe_coalesce
			 */
			r8712_xmit_complete(padapter, pxmitframe);
		}
		if (res == _SUCCESS)
			dump_xframe(padapter, pxmitframe);
		else
			r8712_free_xmitframe_ex(pxmitpriv, pxmitframe);
		xcnt++;
#endif

	} else { /* pxmitframe == NULL && p2ndxmitframe == NULL */
		r8712_free_xmitbuf(pxmitpriv, pxmitbuf);
		return false;
	}
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
#ifdef CONFIG_R8712_TX_AGGR
		r8712_write_port(padapter, RTL8712_DMA_H2CCMD, w_sz,
				(unsigned char *)pxmitframe);
#else
		r8712_write_port(padapter, ff_hwaddr, w_sz,
			   (unsigned char *)pxmitframe);
#endif
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
