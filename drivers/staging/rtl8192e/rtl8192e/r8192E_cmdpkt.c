// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * Contact Information: wlanfae <wlanfae@realtek.com>
 */
#include "rtl_core.h"
#include "r8192E_hw.h"
#include "r8192E_cmdpkt.h"

bool rtl92e_send_cmd_pkt(struct net_device *dev, u32 type, const void *data,
			 u32 len)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	u16				frag_length = 0, frag_offset = 0;
	struct sk_buff		*skb;
	unsigned char		*seg_ptr;
	struct cb_desc *tcb_desc;
	u8				bLastIniPkt;

	struct tx_fwinfo_8190pci *pTxFwInfo = NULL;

	do {
		if ((len - frag_offset) > CMDPACKET_FRAG_SIZE) {
			frag_length = CMDPACKET_FRAG_SIZE;
			bLastIniPkt = 0;

		} else {
			frag_length = (u16)(len - frag_offset);
			bLastIniPkt = 1;
		}

		if (type == DESC_PACKET_TYPE_NORMAL)
			skb = dev_alloc_skb(frag_length +
					    priv->rtllib->tx_headroom + 4);
		else
			skb = dev_alloc_skb(frag_length + 4);

		if (!skb)
			return false;

		memcpy((unsigned char *)(skb->cb), &dev, sizeof(dev));
		tcb_desc = (struct cb_desc *)(skb->cb + MAX_DEV_ADDR_SIZE);
		tcb_desc->queue_index = TXCMD_QUEUE;
		tcb_desc->bCmdOrInit = type;
		tcb_desc->bLastIniPkt = bLastIniPkt;

		if (type == DESC_PACKET_TYPE_NORMAL) {
			tcb_desc->pkt_size = frag_length;

			seg_ptr = skb_put(skb, priv->rtllib->tx_headroom);
			pTxFwInfo = (struct tx_fwinfo_8190pci *)seg_ptr;
			memset(pTxFwInfo, 0, sizeof(struct tx_fwinfo_8190pci));
			memset(pTxFwInfo, 0x12, 8);
		} else {
			tcb_desc->txbuf_size = frag_length;
		}

		skb_put_data(skb, data, frag_length);

		if (type == DESC_PACKET_TYPE_INIT &&
		    (!priv->rtllib->check_nic_enough_desc(dev, TXCMD_QUEUE) ||
		     (!skb_queue_empty(&priv->rtllib->skb_waitq[TXCMD_QUEUE])) ||
		     (priv->rtllib->queue_stop))) {
			skb_queue_tail(&priv->rtllib->skb_waitq[TXCMD_QUEUE],
				       skb);
		} else {
			priv->rtllib->softmac_hard_start_xmit(skb, dev);
		}

		data += frag_length;
		frag_offset += frag_length;

	} while (frag_offset < len);

	rtl92e_writeb(dev, TP_POLL, TP_POLL_CQ);

	return true;
}
