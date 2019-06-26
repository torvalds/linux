// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 * rtl8712_recv.c
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
 * Linux device driver for RTL8192SU
 *
 * Modifications for inclusion into the Linux staging tree are
 * Copyright(c) 2010 Larry Finger. All rights reserved.
 *
 * Contact information:
 * WLAN FAE <wlanfae@realtek.com>
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ******************************************************************************/

#define _RTL8712_RECV_C_

#include <linux/if_ether.h>
#include <linux/ip.h>

#include "osdep_service.h"
#include "drv_types.h"
#include "recv_osdep.h"
#include "mlme_osdep.h"
#include "ethernet.h"
#include "usb_ops.h"
#include "wifi.h"

/* Bridge-Tunnel header (for EtherTypes ETH_P_AARP and ETH_P_IPX) */
static u8 bridge_tunnel_header[] = {0xaa, 0xaa, 0x03, 0x00, 0x00, 0xf8};

/* Ethernet-II snap header (RFC1042 for most EtherTypes) */
static u8 rfc1042_header[] = {0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00};

static void recv_tasklet(void *priv);

int r8712_init_recv_priv(struct recv_priv *precvpriv, struct _adapter *padapter)
{
	int i;
	struct recv_buf *precvbuf;
	int res = _SUCCESS;
	addr_t tmpaddr = 0;
	int alignment = 0;
	struct sk_buff *pskb = NULL;

	/*init recv_buf*/
	_init_queue(&precvpriv->free_recv_buf_queue);
	precvpriv->pallocated_recv_buf =
		kzalloc(NR_RECVBUFF * sizeof(struct recv_buf) + 4, GFP_ATOMIC);
	if (!precvpriv->pallocated_recv_buf)
		return _FAIL;
	precvpriv->precv_buf = precvpriv->pallocated_recv_buf + 4 -
			      ((addr_t) (precvpriv->pallocated_recv_buf) & 3);
	precvbuf = (struct recv_buf *)precvpriv->precv_buf;
	for (i = 0; i < NR_RECVBUFF; i++) {
		INIT_LIST_HEAD(&precvbuf->list);
		spin_lock_init(&precvbuf->recvbuf_lock);
		res = r8712_os_recvbuf_resource_alloc(padapter, precvbuf);
		if (res == _FAIL)
			break;
		precvbuf->ref_cnt = 0;
		precvbuf->adapter = padapter;
		list_add_tail(&precvbuf->list,
				 &(precvpriv->free_recv_buf_queue.queue));
		precvbuf++;
	}
	precvpriv->free_recv_buf_queue_cnt = NR_RECVBUFF;
	tasklet_init(&precvpriv->recv_tasklet,
	     (void(*)(unsigned long))recv_tasklet,
	     (unsigned long)padapter);
	skb_queue_head_init(&precvpriv->rx_skb_queue);

	skb_queue_head_init(&precvpriv->free_recv_skb_queue);
	for (i = 0; i < NR_PREALLOC_RECV_SKB; i++) {
		pskb = netdev_alloc_skb(padapter->pnetdev, MAX_RECVBUF_SZ +
		       RECVBUFF_ALIGN_SZ);
		if (pskb) {
			tmpaddr = (addr_t)pskb->data;
			alignment = tmpaddr & (RECVBUFF_ALIGN_SZ - 1);
			skb_reserve(pskb, (RECVBUFF_ALIGN_SZ - alignment));
			skb_queue_tail(&precvpriv->free_recv_skb_queue, pskb);
		}
		pskb = NULL;
	}
	return res;
}

void r8712_free_recv_priv(struct recv_priv *precvpriv)
{
	int i;
	struct recv_buf *precvbuf;
	struct _adapter *padapter = precvpriv->adapter;

	precvbuf = (struct recv_buf *)precvpriv->precv_buf;
	for (i = 0; i < NR_RECVBUFF; i++) {
		r8712_os_recvbuf_resource_free(padapter, precvbuf);
		precvbuf++;
	}
	kfree(precvpriv->pallocated_recv_buf);
	skb_queue_purge(&precvpriv->rx_skb_queue);
	if (skb_queue_len(&precvpriv->rx_skb_queue))
		netdev_warn(padapter->pnetdev, "r8712u: rx_skb_queue not empty\n");
	skb_queue_purge(&precvpriv->free_recv_skb_queue);
	if (skb_queue_len(&precvpriv->free_recv_skb_queue))
		netdev_warn(padapter->pnetdev, "r8712u: free_recv_skb_queue not empty %d\n",
			    skb_queue_len(&precvpriv->free_recv_skb_queue));
}

int r8712_init_recvbuf(struct _adapter *padapter, struct recv_buf *precvbuf)
{
	precvbuf->transfer_len = 0;
	precvbuf->len = 0;
	precvbuf->ref_cnt = 0;
	if (precvbuf->pbuf) {
		precvbuf->pdata = precvbuf->pbuf;
		precvbuf->phead = precvbuf->pbuf;
		precvbuf->ptail = precvbuf->pbuf;
		precvbuf->pend = precvbuf->pdata + MAX_RECVBUF_SZ;
	}
	return _SUCCESS;
}

int r8712_free_recvframe(union recv_frame *precvframe,
		   struct  __queue *pfree_recv_queue)
{
	unsigned long irqL;
	struct _adapter *padapter = precvframe->u.hdr.adapter;
	struct recv_priv *precvpriv = &padapter->recvpriv;

	if (precvframe->u.hdr.pkt) {
		dev_kfree_skb_any(precvframe->u.hdr.pkt);/*free skb by driver*/
		precvframe->u.hdr.pkt = NULL;
	}
	spin_lock_irqsave(&pfree_recv_queue->lock, irqL);
	list_del_init(&(precvframe->u.hdr.list));
	list_add_tail(&(precvframe->u.hdr.list), &pfree_recv_queue->queue);
	if (padapter != NULL) {
		if (pfree_recv_queue == &precvpriv->free_recv_queue)
			precvpriv->free_recvframe_cnt++;
	}
	spin_unlock_irqrestore(&pfree_recv_queue->lock, irqL);
	return _SUCCESS;
}

static void update_recvframe_attrib_from_recvstat(struct rx_pkt_attrib *pattrib,
					   struct recv_stat *prxstat)
{
	u16 drvinfo_sz;

	drvinfo_sz = (le32_to_cpu(prxstat->rxdw0) & 0x000f0000) >> 16;
	drvinfo_sz <<= 3;
	/*TODO:
	 * Offset 0
	 */
	pattrib->bdecrypted = ((le32_to_cpu(prxstat->rxdw0) & BIT(27)) >> 27)
				 ? 0 : 1;
	pattrib->crc_err = (le32_to_cpu(prxstat->rxdw0) & BIT(14)) >> 14;
	/*Offset 4*/
	/*Offset 8*/
	/*Offset 12*/
	if (le32_to_cpu(prxstat->rxdw3) & BIT(13)) {
		pattrib->tcpchk_valid = 1; /* valid */
		if (le32_to_cpu(prxstat->rxdw3) & BIT(11))
			pattrib->tcp_chkrpt = 1; /* correct */
		else
			pattrib->tcp_chkrpt = 0; /* incorrect */
		if (le32_to_cpu(prxstat->rxdw3) & BIT(12))
			pattrib->ip_chkrpt = 1; /* correct */
		else
			pattrib->ip_chkrpt = 0; /* incorrect */
	} else {
		pattrib->tcpchk_valid = 0; /* invalid */
	}
	pattrib->mcs_rate = (u8)((le32_to_cpu(prxstat->rxdw3)) & 0x3f);
	pattrib->htc = (u8)((le32_to_cpu(prxstat->rxdw3) >> 14) & 0x1);
	/*Offset 16*/
	/*Offset 20*/
	/*phy_info*/
}

/*perform defrag*/
static union recv_frame *recvframe_defrag(struct _adapter *adapter,
				   struct  __queue *defrag_q)
{
	struct list_head *plist, *phead;
	u8 wlanhdr_offset;
	u8	curfragnum;
	struct recv_frame_hdr *pfhdr, *pnfhdr;
	union recv_frame *prframe, *pnextrframe;
	struct  __queue	*pfree_recv_queue;

	pfree_recv_queue = &adapter->recvpriv.free_recv_queue;
	phead = &defrag_q->queue;
	plist = phead->next;
	prframe = container_of(plist, union recv_frame, u.list);
	list_del_init(&prframe->u.list);
	pfhdr = &prframe->u.hdr;
	curfragnum = 0;
	if (curfragnum != pfhdr->attrib.frag_num) {
		/*the first fragment number must be 0
		 *free the whole queue
		 */
		r8712_free_recvframe(prframe, pfree_recv_queue);
		r8712_free_recvframe_queue(defrag_q, pfree_recv_queue);
		return NULL;
	}
	curfragnum++;
	plist = &defrag_q->queue;
	plist = plist->next;
	while (!end_of_queue_search(phead, plist)) {
		pnextrframe = container_of(plist, union recv_frame, u.list);
		pnfhdr = &pnextrframe->u.hdr;
		/*check the fragment sequence  (2nd ~n fragment frame) */
		if (curfragnum != pnfhdr->attrib.frag_num) {
			/* the fragment number must increase  (after decache)
			 * release the defrag_q & prframe
			 */
			r8712_free_recvframe(prframe, pfree_recv_queue);
			r8712_free_recvframe_queue(defrag_q, pfree_recv_queue);
			return NULL;
		}
		curfragnum++;
		/* copy the 2nd~n fragment frame's payload to the first fragment
		 * get the 2nd~last fragment frame's payload
		 */
		wlanhdr_offset = pnfhdr->attrib.hdrlen + pnfhdr->attrib.iv_len;
		recvframe_pull(pnextrframe, wlanhdr_offset);
		/* append  to first fragment frame's tail (if privacy frame,
		 * pull the ICV)
		 */
		recvframe_pull_tail(prframe, pfhdr->attrib.icv_len);
		memcpy(pfhdr->rx_tail, pnfhdr->rx_data, pnfhdr->len);
		recvframe_put(prframe, pnfhdr->len);
		pfhdr->attrib.icv_len = pnfhdr->attrib.icv_len;
		plist = plist->next;
	}
	/* free the defrag_q queue and return the prframe */
	r8712_free_recvframe_queue(defrag_q, pfree_recv_queue);
	return prframe;
}

/* check if need to defrag, if needed queue the frame to defrag_q */
union recv_frame *r8712_recvframe_chk_defrag(struct _adapter *padapter,
					     union recv_frame *precv_frame)
{
	u8	ismfrag;
	u8	fragnum;
	u8   *psta_addr;
	struct recv_frame_hdr *pfhdr;
	struct sta_info *psta;
	struct	sta_priv *pstapriv;
	struct list_head *phead;
	union recv_frame *prtnframe = NULL;
	struct  __queue *pfree_recv_queue, *pdefrag_q;

	pstapriv = &padapter->stapriv;
	pfhdr = &precv_frame->u.hdr;
	pfree_recv_queue = &padapter->recvpriv.free_recv_queue;
	/* need to define struct of wlan header frame ctrl */
	ismfrag = pfhdr->attrib.mfrag;
	fragnum = pfhdr->attrib.frag_num;
	psta_addr = pfhdr->attrib.ta;
	psta = r8712_get_stainfo(pstapriv, psta_addr);
	if (!psta)
		pdefrag_q = NULL;
	else
		pdefrag_q = &psta->sta_recvpriv.defrag_q;

	if ((ismfrag == 0) && (fragnum == 0))
		prtnframe = precv_frame;/*isn't a fragment frame*/
	if (ismfrag == 1) {
		/* 0~(n-1) fragment frame
		 * enqueue to defraf_g
		 */
		if (pdefrag_q != NULL) {
			if (fragnum == 0) {
				/*the first fragment*/
				if (!list_empty(&pdefrag_q->queue)) {
					/*free current defrag_q */
					r8712_free_recvframe_queue(pdefrag_q,
							     pfree_recv_queue);
				}
			}
			/* Then enqueue the 0~(n-1) fragment to the defrag_q */
			phead = &pdefrag_q->queue;
			list_add_tail(&pfhdr->list, phead);
			prtnframe = NULL;
		} else {
			/* can't find this ta's defrag_queue, so free this
			 * recv_frame
			 */
			r8712_free_recvframe(precv_frame, pfree_recv_queue);
			prtnframe = NULL;
		}

	}
	if ((ismfrag == 0) && (fragnum != 0)) {
		/* the last fragment frame
		 * enqueue the last fragment
		 */
		if (pdefrag_q != NULL) {
			phead = &pdefrag_q->queue;
			list_add_tail(&pfhdr->list, phead);
			/*call recvframe_defrag to defrag*/
			precv_frame = recvframe_defrag(padapter, pdefrag_q);
			prtnframe = precv_frame;
		} else {
			/* can't find this ta's defrag_queue, so free this
			 *  recv_frame
			 */
			r8712_free_recvframe(precv_frame, pfree_recv_queue);
			prtnframe = NULL;
		}
	}
	if ((prtnframe != NULL) && (prtnframe->u.hdr.attrib.privacy)) {
		/* after defrag we must check tkip mic code */
		if (r8712_recvframe_chkmic(padapter, prtnframe) == _FAIL) {
			r8712_free_recvframe(prtnframe, pfree_recv_queue);
			prtnframe = NULL;
		}
	}
	return prtnframe;
}

static int amsdu_to_msdu(struct _adapter *padapter, union recv_frame *prframe)
{
	int	a_len, padding_len;
	u16	eth_type, nSubframe_Length;
	u8	nr_subframes, i;
	unsigned char *pdata;
	struct rx_pkt_attrib *pattrib;
	_pkt *sub_skb, *subframes[MAX_SUBFRAME_COUNT];
	struct recv_priv *precvpriv = &padapter->recvpriv;
	struct  __queue *pfree_recv_queue = &(precvpriv->free_recv_queue);

	nr_subframes = 0;
	pattrib = &prframe->u.hdr.attrib;
	recvframe_pull(prframe, prframe->u.hdr.attrib.hdrlen);
	if (prframe->u.hdr.attrib.iv_len > 0)
		recvframe_pull(prframe, prframe->u.hdr.attrib.iv_len);
	a_len = prframe->u.hdr.len;
	pdata = prframe->u.hdr.rx_data;
	while (a_len > ETH_HLEN) {
		/* Offset 12 denote 2 mac address */
		nSubframe_Length = *((u16 *)(pdata + 12));
		/*==m==>change the length order*/
		nSubframe_Length = (nSubframe_Length >> 8) +
				   (nSubframe_Length << 8);
		if (a_len < (ETHERNET_HEADER_SIZE + nSubframe_Length)) {
			netdev_warn(padapter->pnetdev, "r8712u: nRemain_Length is %d and nSubframe_Length is: %d\n",
				    a_len, nSubframe_Length);
			goto exit;
		}
		/* move the data point to data content */
		pdata += ETH_HLEN;
		a_len -= ETH_HLEN;
		/* Allocate new skb for releasing to upper layer */
		sub_skb = dev_alloc_skb(nSubframe_Length + 12);
		if (!sub_skb)
			break;
		skb_reserve(sub_skb, 12);
		skb_put_data(sub_skb, pdata, nSubframe_Length);
		subframes[nr_subframes++] = sub_skb;
		if (nr_subframes >= MAX_SUBFRAME_COUNT) {
			netdev_warn(padapter->pnetdev, "r8712u: ParseSubframe(): Too many Subframes! Packets dropped!\n");
			break;
		}
		pdata += nSubframe_Length;
		a_len -= nSubframe_Length;
		if (a_len != 0) {
			padding_len = 4 - ((nSubframe_Length + ETH_HLEN) & 3);
			if (padding_len == 4)
				padding_len = 0;
			if (a_len < padding_len)
				goto exit;
			pdata += padding_len;
			a_len -= padding_len;
		}
	}
	for (i = 0; i < nr_subframes; i++) {
		sub_skb = subframes[i];
		/* convert hdr + possible LLC headers into Ethernet header */
		eth_type = (sub_skb->data[6] << 8) | sub_skb->data[7];
		if (sub_skb->len >= 8 &&
		   ((!memcmp(sub_skb->data, rfc1042_header, SNAP_SIZE) &&
		   eth_type != ETH_P_AARP && eth_type != ETH_P_IPX) ||
		   !memcmp(sub_skb->data, bridge_tunnel_header, SNAP_SIZE))) {
			/* remove RFC1042 or Bridge-Tunnel encapsulation and
			 * replace EtherType
			 */
			skb_pull(sub_skb, SNAP_SIZE);
			memcpy(skb_push(sub_skb, ETH_ALEN), pattrib->src,
				ETH_ALEN);
			memcpy(skb_push(sub_skb, ETH_ALEN), pattrib->dst,
				ETH_ALEN);
		} else {
			__be16 len;
			/* Leave Ethernet header part of hdr and full payload */
			len = htons(sub_skb->len);
			memcpy(skb_push(sub_skb, 2), &len, 2);
			memcpy(skb_push(sub_skb, ETH_ALEN), pattrib->src,
				ETH_ALEN);
			memcpy(skb_push(sub_skb, ETH_ALEN), pattrib->dst,
				ETH_ALEN);
		}
		/* Indicate the packets to upper layer */
		if (sub_skb) {
			sub_skb->protocol =
				 eth_type_trans(sub_skb, padapter->pnetdev);
			sub_skb->dev = padapter->pnetdev;
			if ((pattrib->tcpchk_valid == 1) &&
			    (pattrib->tcp_chkrpt == 1)) {
				sub_skb->ip_summed = CHECKSUM_UNNECESSARY;
			} else {
				sub_skb->ip_summed = CHECKSUM_NONE;
			}
			netif_rx(sub_skb);
		}
	}
exit:
	prframe->u.hdr.len = 0;
	r8712_free_recvframe(prframe, pfree_recv_queue);
	return _SUCCESS;
}

void r8712_rxcmd_event_hdl(struct _adapter *padapter, void *prxcmdbuf)
{
	__le32 voffset;
	u8 *poffset;
	u16 cmd_len, drvinfo_sz;
	struct recv_stat *prxstat;

	poffset = prxcmdbuf;
	voffset = *(__le32 *)poffset;
	prxstat = prxcmdbuf;
	drvinfo_sz = (le32_to_cpu(prxstat->rxdw0) & 0x000f0000) >> 16;
	drvinfo_sz <<= 3;
	poffset += RXDESC_SIZE + drvinfo_sz;
	do {
		voffset  = *(__le32 *)poffset;
		cmd_len = (u16)(le32_to_cpu(voffset) & 0xffff);
		r8712_event_handle(padapter, (__le32 *)poffset);
		poffset += (cmd_len + 8);/*8 bytes alignment*/
	} while (le32_to_cpu(voffset) & BIT(31));

}

static int check_indicate_seq(struct recv_reorder_ctrl *preorder_ctrl,
			      u16 seq_num)
{
	u8 wsize = preorder_ctrl->wsize_b;
	u16 wend = (preorder_ctrl->indicate_seq + wsize - 1) % 4096;

	/* Rx Reorder initialize condition.*/
	if (preorder_ctrl->indicate_seq == 0xffff)
		preorder_ctrl->indicate_seq = seq_num;
	/* Drop out the packet which SeqNum is smaller than WinStart */
	if (SN_LESS(seq_num, preorder_ctrl->indicate_seq))
		return false;
	/*
	 * Sliding window manipulation. Conditions includes:
	 * 1. Incoming SeqNum is equal to WinStart =>Window shift 1
	 * 2. Incoming SeqNum is larger than the WinEnd => Window shift N
	 */
	if (SN_EQUAL(seq_num, preorder_ctrl->indicate_seq))
		preorder_ctrl->indicate_seq = (preorder_ctrl->indicate_seq +
					      1) % 4096;
	else if (SN_LESS(wend, seq_num)) {
		if (seq_num >= (wsize - 1))
			preorder_ctrl->indicate_seq = seq_num + 1 - wsize;
		else
			preorder_ctrl->indicate_seq = 4095 - (wsize -
						      (seq_num + 1)) + 1;
	}
	return true;
}

static int enqueue_reorder_recvframe(struct recv_reorder_ctrl *preorder_ctrl,
			      union recv_frame *prframe)
{
	struct list_head *phead, *plist;
	union recv_frame *pnextrframe;
	struct rx_pkt_attrib *pnextattrib;
	struct  __queue *ppending_recvframe_queue =
					&preorder_ctrl->pending_recvframe_queue;
	struct rx_pkt_attrib *pattrib = &prframe->u.hdr.attrib;

	phead = &ppending_recvframe_queue->queue;
	plist = phead->next;
	while (!end_of_queue_search(phead, plist)) {
		pnextrframe = container_of(plist, union recv_frame, u.list);
		pnextattrib = &pnextrframe->u.hdr.attrib;
		if (SN_LESS(pnextattrib->seq_num, pattrib->seq_num))
			plist = plist->next;
		else if (SN_EQUAL(pnextattrib->seq_num, pattrib->seq_num))
			return false;
		else
			break;
	}
	list_del_init(&(prframe->u.hdr.list));
	list_add_tail(&(prframe->u.hdr.list), plist);
	return true;
}

int r8712_recv_indicatepkts_in_order(struct _adapter *padapter,
			       struct recv_reorder_ctrl *preorder_ctrl,
			       int bforced)
{
	struct list_head *phead, *plist;
	union recv_frame *prframe;
	struct rx_pkt_attrib *pattrib;
	int bPktInBuf = false;
	struct recv_priv *precvpriv = &padapter->recvpriv;
	struct  __queue *ppending_recvframe_queue =
			 &preorder_ctrl->pending_recvframe_queue;

	phead = &ppending_recvframe_queue->queue;
	plist = phead->next;
	/* Handling some condition for forced indicate case.*/
	if (bforced) {
		if (list_empty(phead))
			return true;

		prframe = container_of(plist, union recv_frame, u.list);
		pattrib = &prframe->u.hdr.attrib;
		preorder_ctrl->indicate_seq = pattrib->seq_num;
	}
	/* Prepare indication list and indication.
	 * Check if there is any packet need indicate.
	 */
	while (!list_empty(phead)) {
		prframe = container_of(plist, union recv_frame, u.list);
		pattrib = &prframe->u.hdr.attrib;
		if (!SN_LESS(preorder_ctrl->indicate_seq, pattrib->seq_num)) {
			plist = plist->next;
			list_del_init(&(prframe->u.hdr.list));
			if (SN_EQUAL(preorder_ctrl->indicate_seq,
			    pattrib->seq_num))
				preorder_ctrl->indicate_seq =
				  (preorder_ctrl->indicate_seq + 1) % 4096;
			/*indicate this recv_frame*/
			if (!pattrib->amsdu) {
				if (!padapter->bDriverStopped &&
				    !padapter->bSurpriseRemoved) {
					/* indicate this recv_frame */
					r8712_recv_indicatepkt(padapter,
							       prframe);
				}
			} else if (pattrib->amsdu == 1) {
				if (amsdu_to_msdu(padapter, prframe) !=
				    _SUCCESS)
					r8712_free_recvframe(prframe,
						   &precvpriv->free_recv_queue);
			}
			/* Update local variables. */
			bPktInBuf = false;
		} else {
			bPktInBuf = true;
			break;
		}
	}
	return bPktInBuf;
}

static int recv_indicatepkt_reorder(struct _adapter *padapter,
			     union recv_frame *prframe)
{
	unsigned long irql;
	struct rx_pkt_attrib *pattrib = &prframe->u.hdr.attrib;
	struct recv_reorder_ctrl *preorder_ctrl = prframe->u.hdr.preorder_ctrl;
	struct  __queue *ppending_recvframe_queue =
			 &preorder_ctrl->pending_recvframe_queue;

	if (!pattrib->amsdu) {
		/* s1. */
		r8712_wlanhdr_to_ethhdr(prframe);
		if (pattrib->qos != 1) {
			if (!padapter->bDriverStopped &&
			    !padapter->bSurpriseRemoved) {
				r8712_recv_indicatepkt(padapter, prframe);
				return _SUCCESS;
			} else {
				return _FAIL;
			}
		}
	}
	spin_lock_irqsave(&ppending_recvframe_queue->lock, irql);
	/*s2. check if winstart_b(indicate_seq) needs to be updated*/
	if (!check_indicate_seq(preorder_ctrl, pattrib->seq_num))
		goto _err_exit;
	/*s3. Insert all packet into Reorder Queue to maintain its ordering.*/
	if (!enqueue_reorder_recvframe(preorder_ctrl, prframe))
		goto _err_exit;
	/*s4.
	 * Indication process.
	 * After Packet dropping and Sliding Window shifting as above, we can
	 * now just indicate the packets with the SeqNum smaller than latest
	 * WinStart and buffer other packets.
	 *
	 * For Rx Reorder condition:
	 * 1. All packets with SeqNum smaller than WinStart => Indicate
	 * 2. All packets with SeqNum larger than or equal to
	 * WinStart => Buffer it.
	 */
	if (r8712_recv_indicatepkts_in_order(padapter, preorder_ctrl, false) ==
	    true) {
		mod_timer(&preorder_ctrl->reordering_ctrl_timer,
			  jiffies + msecs_to_jiffies(REORDER_WAIT_TIME));
		spin_unlock_irqrestore(&ppending_recvframe_queue->lock, irql);
	} else {
		spin_unlock_irqrestore(&ppending_recvframe_queue->lock, irql);
		del_timer(&preorder_ctrl->reordering_ctrl_timer);
	}
	return _SUCCESS;
_err_exit:
	spin_unlock_irqrestore(&ppending_recvframe_queue->lock, irql);
	return _FAIL;
}

void r8712_reordering_ctrl_timeout_handler(void *pcontext)
{
	unsigned long irql;
	struct recv_reorder_ctrl *preorder_ctrl = pcontext;
	struct _adapter *padapter = preorder_ctrl->padapter;
	struct  __queue *ppending_recvframe_queue =
				 &preorder_ctrl->pending_recvframe_queue;

	if (padapter->bDriverStopped || padapter->bSurpriseRemoved)
		return;
	spin_lock_irqsave(&ppending_recvframe_queue->lock, irql);
	r8712_recv_indicatepkts_in_order(padapter, preorder_ctrl, true);
	spin_unlock_irqrestore(&ppending_recvframe_queue->lock, irql);
}

static int r8712_process_recv_indicatepkts(struct _adapter *padapter,
			      union recv_frame *prframe)
{
	int retval = _SUCCESS;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct ht_priv	*phtpriv = &pmlmepriv->htpriv;

	if (phtpriv->ht_option == 1) { /*B/G/N Mode*/
		if (recv_indicatepkt_reorder(padapter, prframe) != _SUCCESS) {
			/* including perform A-MPDU Rx Ordering Buffer Control*/
			if (!padapter->bDriverStopped &&
			    !padapter->bSurpriseRemoved)
				return _FAIL;
		}
	} else { /*B/G mode*/
		retval = r8712_wlanhdr_to_ethhdr(prframe);
		if (retval != _SUCCESS)
			return retval;
		if (!padapter->bDriverStopped && !padapter->bSurpriseRemoved) {
			/* indicate this recv_frame */
			r8712_recv_indicatepkt(padapter, prframe);
		} else {
			return _FAIL;
		}
	}
	return retval;
}

static u8 query_rx_pwr_percentage(s8 antpower)
{
	if ((antpower <= -100) || (antpower >= 20))
		return	0;
	else if (antpower >= 0)
		return	100;
	else
		return 100 + antpower;
}

static u8 evm_db2percentage(s8 value)
{
	/*
	 * -33dB~0dB to 0%~99%
	 */
	s8 ret_val;

	ret_val = value;
	if (ret_val >= 0)
		ret_val = 0;
	if (ret_val <= -33)
		ret_val = -33;
	ret_val = -ret_val;
	ret_val *= 3;
	if (ret_val == 99)
		ret_val = 100;
	return ret_val;
}

s32 r8712_signal_scale_mapping(s32 cur_sig)
{
	s32 ret_sig;

	if (cur_sig >= 51 && cur_sig <= 100)
		ret_sig = 100;
	else if (cur_sig >= 41 && cur_sig <= 50)
		ret_sig = 80 + ((cur_sig - 40) * 2);
	else if (cur_sig >= 31 && cur_sig <= 40)
		ret_sig = 66 + (cur_sig - 30);
	else if (cur_sig >= 21 && cur_sig <= 30)
		ret_sig = 54 + (cur_sig - 20);
	else if (cur_sig >= 10 && cur_sig <= 20)
		ret_sig = 42 + (((cur_sig - 10) * 2) / 3);
	else if (cur_sig >= 5 && cur_sig <= 9)
		ret_sig = 22 + (((cur_sig - 5) * 3) / 2);
	else if (cur_sig >= 1 && cur_sig <= 4)
		ret_sig = 6 + (((cur_sig - 1) * 3) / 2);
	else
		ret_sig = cur_sig;
	return ret_sig;
}

static s32  translate2dbm(struct _adapter *padapter, u8 signal_strength_idx)
{
	s32 signal_power; /* in dBm.*/
	/* Translate to dBm (x=0.5y-95).*/
	signal_power = (s32)((signal_strength_idx + 1) >> 1);
	signal_power -= 95;
	return signal_power;
}

static void query_rx_phy_status(struct _adapter *padapter,
				union recv_frame *prframe)
{
	u8 i, max_spatial_stream, evm;
	struct recv_stat *prxstat = (struct recv_stat *)prframe->u.hdr.rx_head;
	struct phy_stat *pphy_stat = (struct phy_stat *)(prxstat + 1);
	u8 *pphy_head = (u8 *)(prxstat + 1);
	s8 rx_pwr[4], rx_pwr_all;
	u8 pwdb_all;
	u32 rssi, total_rssi = 0;
	u8 bcck_rate = 0, rf_rx_num = 0, cck_highpwr = 0;
	struct phy_cck_rx_status *pcck_buf;
	u8 sq;

	/* Record it for next packet processing*/
	bcck_rate = (prframe->u.hdr.attrib.mcs_rate <= 3 ? 1 : 0);
	if (bcck_rate) {
		u8 report;

		/* CCK Driver info Structure is not the same as OFDM packet.*/
		pcck_buf = (struct phy_cck_rx_status *)pphy_stat;
		/* (1)Hardware does not provide RSSI for CCK
		 * (2)PWDB, Average PWDB calculated by hardware
		 * (for rate adaptive)
		 */
		if (!cck_highpwr) {
			report = pcck_buf->cck_agc_rpt & 0xc0;
			report >>= 6;
			switch (report) {
			/* Modify the RF RNA gain value to -40, -20,
			 * -2, 14 by Jenyu's suggestion
			 * Note: different RF with the different
			 * RNA gain.
			 */
			case 0x3:
				rx_pwr_all = -40 - (pcck_buf->cck_agc_rpt &
					     0x3e);
				break;
			case 0x2:
				rx_pwr_all = -20 - (pcck_buf->cck_agc_rpt &
					     0x3e);
				break;
			case 0x1:
				rx_pwr_all = -2 - (pcck_buf->cck_agc_rpt &
					     0x3e);
				break;
			case 0x0:
				rx_pwr_all = 14 - (pcck_buf->cck_agc_rpt &
					     0x3e);
				break;
			}
		} else {
			report = ((u8)(le32_to_cpu(pphy_stat->phydw1) >> 8)) &
				 0x60;
			report >>= 5;
			switch (report) {
			case 0x3:
				rx_pwr_all = -40 - ((pcck_buf->cck_agc_rpt &
					     0x1f) << 1);
				break;
			case 0x2:
				rx_pwr_all = -20 - ((pcck_buf->cck_agc_rpt &
					     0x1f) << 1);
				break;
			case 0x1:
				rx_pwr_all = -2 - ((pcck_buf->cck_agc_rpt &
					     0x1f) << 1);
				break;
			case 0x0:
				rx_pwr_all = 14 - ((pcck_buf->cck_agc_rpt &
					     0x1f) << 1);
				break;
			}
		}
		pwdb_all = query_rx_pwr_percentage(rx_pwr_all);
		/* CCK gain is smaller than OFDM/MCS gain,*/
		/* so we add gain diff by experiences, the val is 6 */
		pwdb_all += 6;
		if (pwdb_all > 100)
			pwdb_all = 100;
		/* modify the offset to make the same gain index with OFDM.*/
		if (pwdb_all > 34 && pwdb_all <= 42)
			pwdb_all -= 2;
		else if (pwdb_all > 26 && pwdb_all <= 34)
			pwdb_all -= 6;
		else if (pwdb_all > 14 && pwdb_all <= 26)
			pwdb_all -= 8;
		else if (pwdb_all > 4 && pwdb_all <= 14)
			pwdb_all -= 4;
		/*
		 * (3) Get Signal Quality (EVM)
		 */
		if (pwdb_all > 40) {
			sq = 100;
		} else {
			sq = pcck_buf->sq_rpt;
			if (pcck_buf->sq_rpt > 64)
				sq = 0;
			else if (pcck_buf->sq_rpt < 20)
				sq = 100;
			else
				sq = ((64 - sq) * 100) / 44;
		}
		prframe->u.hdr.attrib.signal_qual = sq;
		prframe->u.hdr.attrib.rx_mimo_signal_qual[0] = sq;
		prframe->u.hdr.attrib.rx_mimo_signal_qual[1] = -1;
	} else {
		/* (1)Get RSSI for HT rate */
		for (i = 0; i < ((padapter->registrypriv.rf_config) &
			    0x0f); i++) {
			rf_rx_num++;
			rx_pwr[i] = ((pphy_head[PHY_STAT_GAIN_TRSW_SHT + i]
				    & 0x3F) * 2) - 110;
			/* Translate DBM to percentage. */
			rssi = query_rx_pwr_percentage(rx_pwr[i]);
			total_rssi += rssi;
		}
		/* (2)PWDB, Average PWDB calculated by hardware (for
		 * rate adaptive)
		 */
		rx_pwr_all = (((pphy_head[PHY_STAT_PWDB_ALL_SHT]) >> 1) & 0x7f)
			     - 106;
		pwdb_all = query_rx_pwr_percentage(rx_pwr_all);

		{
			/* (3)EVM of HT rate */
			if (prframe->u.hdr.attrib.htc &&
			    prframe->u.hdr.attrib.mcs_rate >= 20 &&
			    prframe->u.hdr.attrib.mcs_rate <= 27) {
				/* both spatial stream make sense */
				max_spatial_stream = 2;
			} else {
				/* only spatial stream 1 makes sense */
				max_spatial_stream = 1;
			}
			for (i = 0; i < max_spatial_stream; i++) {
				evm = evm_db2percentage((pphy_head
				      [PHY_STAT_RXEVM_SHT + i]));/*dbm*/
				prframe->u.hdr.attrib.signal_qual =
					 (u8)(evm & 0xff);
				prframe->u.hdr.attrib.rx_mimo_signal_qual[i] =
					 (u8)(evm & 0xff);
			}
		}
	}
	/* UI BSS List signal strength(in percentage), make it good looking,
	 * from 0~100. It is assigned to the BSS List in
	 * GetValueFromBeaconOrProbeRsp().
	 */
	if (bcck_rate) {
		prframe->u.hdr.attrib.signal_strength =
			 (u8)r8712_signal_scale_mapping(pwdb_all);
	} else {
		if (rf_rx_num != 0)
			prframe->u.hdr.attrib.signal_strength =
				 (u8)(r8712_signal_scale_mapping(total_rssi /=
				 rf_rx_num));
	}
}

static void process_link_qual(struct _adapter *padapter,
			      union recv_frame *prframe)
{
	u32	last_evm = 0, tmpVal;
	struct rx_pkt_attrib *pattrib;
	struct smooth_rssi_data *sqd = &padapter->recvpriv.signal_qual_data;

	if (prframe == NULL || padapter == NULL)
		return;
	pattrib = &prframe->u.hdr.attrib;
	if (pattrib->signal_qual != 0) {
		/*
		 * 1. Record the general EVM to the sliding window.
		 */
		if (sqd->total_num++ >= PHY_LINKQUALITY_SLID_WIN_MAX) {
			sqd->total_num = PHY_LINKQUALITY_SLID_WIN_MAX;
			last_evm = sqd->elements[sqd->index];
			sqd->total_val -= last_evm;
		}
		sqd->total_val += pattrib->signal_qual;
		sqd->elements[sqd->index++] = pattrib->signal_qual;
		if (sqd->index >= PHY_LINKQUALITY_SLID_WIN_MAX)
			sqd->index = 0;

		/* <1> Showed on UI for user, in percentage. */
		tmpVal = sqd->total_val / sqd->total_num;
		padapter->recvpriv.signal = (u8)tmpVal;
	}
}

static void process_rssi(struct _adapter *padapter, union recv_frame *prframe)
{
	u32 last_rssi, tmp_val;
	struct rx_pkt_attrib *pattrib = &prframe->u.hdr.attrib;
	struct smooth_rssi_data *ssd = &padapter->recvpriv.signal_strength_data;

	if (ssd->total_num++ >= PHY_RSSI_SLID_WIN_MAX) {
		ssd->total_num = PHY_RSSI_SLID_WIN_MAX;
		last_rssi = ssd->elements[ssd->index];
		ssd->total_val -= last_rssi;
	}
	ssd->total_val += pattrib->signal_strength;
	ssd->elements[ssd->index++] = pattrib->signal_strength;
	if (ssd->index >= PHY_RSSI_SLID_WIN_MAX)
		ssd->index = 0;
	tmp_val = ssd->total_val / ssd->total_num;
	padapter->recvpriv.rssi = (s8)translate2dbm(padapter, (u8)tmp_val);
}

static void process_phy_info(struct _adapter *padapter,
			     union recv_frame *prframe)
{
	query_rx_phy_status(padapter, prframe);
	process_rssi(padapter, prframe);
	process_link_qual(padapter,  prframe);
}

int recv_func(struct _adapter *padapter, void *pcontext)
{
	struct rx_pkt_attrib *pattrib;
	union recv_frame *prframe, *orig_prframe;
	int retval = _SUCCESS;
	struct  __queue *pfree_recv_queue = &padapter->recvpriv.free_recv_queue;
	struct	mlme_priv	*pmlmepriv = &padapter->mlmepriv;

	prframe = pcontext;
	orig_prframe = prframe;
	pattrib = &prframe->u.hdr.attrib;
	if (check_fwstate(pmlmepriv, WIFI_MP_STATE)) {
		if (pattrib->crc_err == 1)
			padapter->mppriv.rx_crcerrpktcount++;
		else
			padapter->mppriv.rx_pktcount++;
		if (!check_fwstate(pmlmepriv, WIFI_MP_LPBK_STATE)) {
			/* free this recv_frame */
			r8712_free_recvframe(orig_prframe, pfree_recv_queue);
			goto _exit_recv_func;
		}
	}
	/* check the frame crtl field and decache */
	retval = r8712_validate_recv_frame(padapter, prframe);
	if (retval != _SUCCESS) {
		/* free this recv_frame */
		r8712_free_recvframe(orig_prframe, pfree_recv_queue);
		goto _exit_recv_func;
	}
	process_phy_info(padapter, prframe);
	prframe = r8712_decryptor(padapter, prframe);
	if (!prframe) {
		retval = _FAIL;
		goto _exit_recv_func;
	}
	prframe = r8712_recvframe_chk_defrag(padapter, prframe);
	if (!prframe)
		goto _exit_recv_func;
	prframe = r8712_portctrl(padapter, prframe);
	if (!prframe) {
		retval = _FAIL;
		goto _exit_recv_func;
	}
	retval = r8712_process_recv_indicatepkts(padapter, prframe);
	if (retval != _SUCCESS) {
		r8712_free_recvframe(orig_prframe, pfree_recv_queue);
		goto _exit_recv_func;
	}
_exit_recv_func:
	return retval;
}

static int recvbuf2recvframe(struct _adapter *padapter, struct sk_buff *pskb)
{
	u8 *pbuf, shift_sz = 0;
	u8	frag, mf;
	uint	pkt_len;
	u32 transfer_len;
	struct recv_stat *prxstat;
	u16	pkt_cnt, drvinfo_sz, pkt_offset, tmp_len, alloc_sz;
	struct  __queue *pfree_recv_queue;
	_pkt  *pkt_copy = NULL;
	union recv_frame *precvframe = NULL;
	struct recv_priv *precvpriv = &padapter->recvpriv;

	pfree_recv_queue = &(precvpriv->free_recv_queue);
	pbuf = pskb->data;
	prxstat = (struct recv_stat *)pbuf;
	pkt_cnt = (le32_to_cpu(prxstat->rxdw2) >> 16) & 0xff;
	pkt_len =  le32_to_cpu(prxstat->rxdw0) & 0x00003fff;
	transfer_len = pskb->len;
	/* Test throughput with Netgear 3700 (No security) with Chariot 3T3R
	 * pairs. The packet count will be a big number so that the containing
	 * packet will effect the Rx reordering.
	 */
	if (transfer_len < pkt_len) {
		/* In this case, it means the MAX_RECVBUF_SZ is too small to
		 * get the data from 8712u.
		 */
		return _FAIL;
	}
	do {
		prxstat = (struct recv_stat *)pbuf;
		pkt_len =  le32_to_cpu(prxstat->rxdw0) & 0x00003fff;
		/* more fragment bit */
		mf = (le32_to_cpu(prxstat->rxdw1) >> 27) & 0x1;
		/* ragmentation number */
		frag = (le32_to_cpu(prxstat->rxdw2) >> 12) & 0xf;
		/* uint 2^3 = 8 bytes */
		drvinfo_sz = (le32_to_cpu(prxstat->rxdw0) & 0x000f0000) >> 16;
		drvinfo_sz <<= 3;
		if (pkt_len <= 0)
			goto  _exit_recvbuf2recvframe;
		/* Qos data, wireless lan header length is 26 */
		if ((le32_to_cpu(prxstat->rxdw0) >> 23) & 0x01)
			shift_sz = 2;
		precvframe = r8712_alloc_recvframe(pfree_recv_queue);
		if (!precvframe)
			goto  _exit_recvbuf2recvframe;
		INIT_LIST_HEAD(&precvframe->u.hdr.list);
		precvframe->u.hdr.precvbuf = NULL; /*can't access the precvbuf*/
		precvframe->u.hdr.len = 0;
		tmp_len = pkt_len + drvinfo_sz + RXDESC_SIZE;
		pkt_offset = (u16)round_up(tmp_len, 128);
		/* for first fragment packet, driver need allocate 1536 +
		 * drvinfo_sz + RXDESC_SIZE to defrag packet.
		 */
		if ((mf == 1) && (frag == 0))
			/*1658+6=1664, 1664 is 128 alignment.*/
			alloc_sz = max_t(u16, tmp_len, 1658);
		else
			alloc_sz = tmp_len;
		/* 2 is for IP header 4 bytes alignment in QoS packet case.
		 * 4 is for skb->data 4 bytes alignment.
		 */
		alloc_sz += 6;
		pkt_copy = netdev_alloc_skb(padapter->pnetdev, alloc_sz);
		if (pkt_copy) {
			precvframe->u.hdr.pkt = pkt_copy;
			skb_reserve(pkt_copy, 4 - ((addr_t)(pkt_copy->data)
				    % 4));
			skb_reserve(pkt_copy, shift_sz);
			memcpy(pkt_copy->data, pbuf, tmp_len);
			precvframe->u.hdr.rx_head = precvframe->u.hdr.rx_data =
				 precvframe->u.hdr.rx_tail = pkt_copy->data;
			precvframe->u.hdr.rx_end = pkt_copy->data + alloc_sz;
		} else {
			precvframe->u.hdr.pkt = skb_clone(pskb, GFP_ATOMIC);
			if (!precvframe->u.hdr.pkt)
				return _FAIL;
			precvframe->u.hdr.rx_head = pbuf;
			precvframe->u.hdr.rx_data = pbuf;
			precvframe->u.hdr.rx_tail = pbuf;
			precvframe->u.hdr.rx_end = pbuf + alloc_sz;
		}
		recvframe_put(precvframe, tmp_len);
		recvframe_pull(precvframe, drvinfo_sz + RXDESC_SIZE);
		/* because the endian issue, driver avoid reference to the
		 * rxstat after calling update_recvframe_attrib_from_recvstat();
		 */
		update_recvframe_attrib_from_recvstat(&precvframe->u.hdr.attrib,
						      prxstat);
		r8712_recv_entry(precvframe);
		transfer_len -= pkt_offset;
		pbuf += pkt_offset;
		pkt_cnt--;
		precvframe = NULL;
		pkt_copy = NULL;
	} while ((transfer_len > 0) && pkt_cnt > 0);
_exit_recvbuf2recvframe:
	return _SUCCESS;
}

static void recv_tasklet(void *priv)
{
	struct sk_buff *pskb;
	struct _adapter *padapter = priv;
	struct recv_priv *precvpriv = &padapter->recvpriv;

	while (NULL != (pskb = skb_dequeue(&precvpriv->rx_skb_queue))) {
		recvbuf2recvframe(padapter, pskb);
		skb_reset_tail_pointer(pskb);
		pskb->len = 0;
		if (!skb_cloned(pskb))
			skb_queue_tail(&precvpriv->free_recv_skb_queue, pskb);
		else
			consume_skb(pskb);
	}
}
