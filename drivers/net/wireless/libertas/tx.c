/**
  * This file contains the handling of TX in wlan driver.
  */
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/sched.h>

#include "host.h"
#include "radiotap.h"
#include "decl.h"
#include "defs.h"
#include "dev.h"
#include "wext.h"

/**
 *  @brief This function converts Tx/Rx rates from IEEE80211_RADIOTAP_RATE
 *  units (500 Kb/s) into Marvell WLAN format (see Table 8 in Section 3.2.1)
 *
 *  @param rate    Input rate
 *  @return      Output Rate (0 if invalid)
 */
static u32 convert_radiotap_rate_to_mv(u8 rate)
{
	switch (rate) {
	case 2:		/*   1 Mbps */
		return 0 | (1 << 4);
	case 4:		/*   2 Mbps */
		return 1 | (1 << 4);
	case 11:		/* 5.5 Mbps */
		return 2 | (1 << 4);
	case 22:		/*  11 Mbps */
		return 3 | (1 << 4);
	case 12:		/*   6 Mbps */
		return 4 | (1 << 4);
	case 18:		/*   9 Mbps */
		return 5 | (1 << 4);
	case 24:		/*  12 Mbps */
		return 6 | (1 << 4);
	case 36:		/*  18 Mbps */
		return 7 | (1 << 4);
	case 48:		/*  24 Mbps */
		return 8 | (1 << 4);
	case 72:		/*  36 Mbps */
		return 9 | (1 << 4);
	case 96:		/*  48 Mbps */
		return 10 | (1 << 4);
	case 108:		/*  54 Mbps */
		return 11 | (1 << 4);
	}
	return 0;
}

/**
 *  @brief This function checks the conditions and sends packet to IF
 *  layer if everything is ok.
 *
 *  @param priv    A pointer to struct lbs_private structure
 *  @param skb     A pointer to skb which includes TX packet
 *  @return 	   0 or -1
 */
netdev_tx_t lbs_hard_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	unsigned long flags;
	struct lbs_private *priv = dev->ml_priv;
	struct txpd *txpd;
	char *p802x_hdr;
	uint16_t pkt_len;
	netdev_tx_t ret = NETDEV_TX_OK;

	lbs_deb_enter(LBS_DEB_TX);

	/* We need to protect against the queues being restarted before
	   we get round to stopping them */
	spin_lock_irqsave(&priv->driver_lock, flags);

	if (priv->surpriseremoved)
		goto free;

	if (!skb->len || (skb->len > MRVDRV_ETH_TX_PACKET_BUFFER_SIZE)) {
		lbs_deb_tx("tx err: skb length %d 0 or > %zd\n",
		       skb->len, MRVDRV_ETH_TX_PACKET_BUFFER_SIZE);
		/* We'll never manage to send this one; drop it and return 'OK' */

		dev->stats.tx_dropped++;
		dev->stats.tx_errors++;
		goto free;
	}


	netif_stop_queue(priv->dev);
	if (priv->mesh_dev)
		netif_stop_queue(priv->mesh_dev);

	if (priv->tx_pending_len) {
		/* This can happen if packets come in on the mesh and eth
		   device simultaneously -- there's no mutual exclusion on
		   hard_start_xmit() calls between devices. */
		lbs_deb_tx("Packet on %s while busy\n", dev->name);
		ret = NETDEV_TX_BUSY;
		goto unlock;
	}

	priv->tx_pending_len = -1;
	spin_unlock_irqrestore(&priv->driver_lock, flags);

	lbs_deb_hex(LBS_DEB_TX, "TX Data", skb->data, min_t(unsigned int, skb->len, 100));

	txpd = (void *)priv->tx_pending_buf;
	memset(txpd, 0, sizeof(struct txpd));

	p802x_hdr = skb->data;
	pkt_len = skb->len;

	if (dev == priv->rtap_net_dev) {
		struct tx_radiotap_hdr *rtap_hdr = (void *)skb->data;

		/* set txpd fields from the radiotap header */
		txpd->tx_control = cpu_to_le32(convert_radiotap_rate_to_mv(rtap_hdr->rate));

		/* skip the radiotap header */
		p802x_hdr += sizeof(*rtap_hdr);
		pkt_len -= sizeof(*rtap_hdr);

		/* copy destination address from 802.11 header */
		memcpy(txpd->tx_dest_addr_high, p802x_hdr + 4, ETH_ALEN);
	} else {
		/* copy destination address from 802.3 header */
		memcpy(txpd->tx_dest_addr_high, p802x_hdr, ETH_ALEN);
	}

	txpd->tx_packet_length = cpu_to_le16(pkt_len);
	txpd->tx_packet_location = cpu_to_le32(sizeof(struct txpd));

	lbs_mesh_set_txpd(priv, dev, txpd);

	lbs_deb_hex(LBS_DEB_TX, "txpd", (u8 *) &txpd, sizeof(struct txpd));

	lbs_deb_hex(LBS_DEB_TX, "Tx Data", (u8 *) p802x_hdr, le16_to_cpu(txpd->tx_packet_length));

	memcpy(&txpd[1], p802x_hdr, le16_to_cpu(txpd->tx_packet_length));

	spin_lock_irqsave(&priv->driver_lock, flags);
	priv->tx_pending_len = pkt_len + sizeof(struct txpd);

	lbs_deb_tx("%s lined up packet\n", __func__);

	dev->stats.tx_packets++;
	dev->stats.tx_bytes += skb->len;

	dev->trans_start = jiffies;

	if (priv->monitormode) {
		/* Keep the skb to echo it back once Tx feedback is
		   received from FW */
		skb_orphan(skb);

		/* Keep the skb around for when we get feedback */
		priv->currenttxskb = skb;
	} else {
 free:
		dev_kfree_skb_any(skb);
	}
 unlock:
	spin_unlock_irqrestore(&priv->driver_lock, flags);
	wake_up(&priv->waitq);

	lbs_deb_leave_args(LBS_DEB_TX, "ret %d", ret);
	return ret;
}

/**
 *  @brief This function sends to the host the last transmitted packet,
 *  filling the radiotap headers with transmission information.
 *
 *  @param priv     A pointer to struct lbs_private structure
 *  @param status   A 32 bit value containing transmission status.
 *
 *  @returns void
 */
void lbs_send_tx_feedback(struct lbs_private *priv, u32 try_count)
{
	struct tx_radiotap_hdr *radiotap_hdr;

	if (!priv->monitormode || priv->currenttxskb == NULL)
		return;

	radiotap_hdr = (struct tx_radiotap_hdr *)priv->currenttxskb->data;

	radiotap_hdr->data_retries = try_count ?
		(1 + priv->txretrycount - try_count) : 0;

	priv->currenttxskb->protocol = eth_type_trans(priv->currenttxskb,
						      priv->rtap_net_dev);
	netif_rx(priv->currenttxskb);

	priv->currenttxskb = NULL;

	if (priv->connect_status == LBS_CONNECTED)
		netif_wake_queue(priv->dev);

	if (priv->mesh_dev && (priv->mesh_connect_status == LBS_CONNECTED))
		netif_wake_queue(priv->mesh_dev);
}
EXPORT_SYMBOL_GPL(lbs_send_tx_feedback);
