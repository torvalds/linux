/**
  * This file contains the handling of TX in wlan driver.
  */
#include <linux/netdevice.h>

#include "hostcmd.h"
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
int lbs_hard_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	unsigned long flags;
	struct lbs_private *priv = dev->priv;
	struct txpd *txpd;
	char *p802x_hdr;
	uint16_t pkt_len;
	int ret;

	lbs_deb_enter(LBS_DEB_TX);

	ret = NETDEV_TX_BUSY;

	if (priv->dnld_sent) {
		lbs_pr_alert( "TX error: dnld_sent = %d, not sending\n",
		       priv->dnld_sent);
		goto done;
	}

	if (priv->currenttxskb) {
		lbs_pr_err("%s while TX skb pending\n", __func__);
		goto done;
	}

	if ((priv->psstate == PS_STATE_SLEEP) ||
	    (priv->psstate == PS_STATE_PRE_SLEEP)) {
		lbs_pr_alert("TX error: packet xmit in %ssleep mode\n",
			     priv->psstate == PS_STATE_SLEEP?"":"pre-");
		goto done;
	}

	if (priv->surpriseremoved)
		goto drop;

	if (!skb->len || (skb->len > MRVDRV_ETH_TX_PACKET_BUFFER_SIZE)) {
		lbs_deb_tx("tx err: skb length %d 0 or > %zd\n",
		       skb->len, MRVDRV_ETH_TX_PACKET_BUFFER_SIZE);
		/* We'll never manage to send this one; drop it and return 'OK' */
		goto drop;
	}

	lbs_deb_hex(LBS_DEB_TX, "TX Data", skb->data, min_t(unsigned int, skb->len, 100));

	txpd = (void *)priv->tmptxbuf;
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

	if (dev == priv->mesh_dev)
		txpd->tx_control |= cpu_to_le32(TxPD_MESH_FRAME);

	lbs_deb_hex(LBS_DEB_TX, "txpd", (u8 *) &txpd, sizeof(struct txpd));

	lbs_deb_hex(LBS_DEB_TX, "Tx Data", (u8 *) p802x_hdr, le16_to_cpu(txpd->tx_packet_length));

	memcpy(&txpd[1], p802x_hdr, le16_to_cpu(txpd->tx_packet_length));

	/* We need to protect against the queues being restarted before
	   we get round to stopping them */
	spin_lock_irqsave(&priv->driver_lock, flags);

	ret = priv->hw_host_to_card(priv, MVMS_DAT, priv->tmptxbuf,
				    pkt_len + sizeof(struct txpd));

	if (!ret) {
		lbs_deb_tx("%s succeeds\n", __func__);

		/* Stop processing outgoing pkts before submitting */
		netif_stop_queue(priv->dev);
		if (priv->mesh_dev)
			netif_stop_queue(priv->mesh_dev);

		priv->stats.tx_packets++;
		priv->stats.tx_bytes += skb->len;

		dev->trans_start = jiffies;

		if (priv->monitormode != LBS_MONITOR_OFF) {
			/* Keep the skb to echo it back once Tx feedback is
			   received from FW */
			skb_orphan(skb);

			/* Keep the skb around for when we get feedback */
			priv->currenttxskb = skb;
		}
	}
	
	spin_unlock_irqrestore(&priv->driver_lock, flags);

	if (ret) {
		lbs_deb_tx("tx err: hw_host_to_card returned 0x%X\n", ret);
drop:
		priv->stats.tx_dropped++;
		priv->stats.tx_errors++;

		dev_kfree_skb_any(skb);
	}

	/* Even if we dropped the packet, return OK. Otherwise the
	   packet gets requeued. */
	ret = NETDEV_TX_OK;

done:
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
void lbs_send_tx_feedback(struct lbs_private *priv)
{
	struct tx_radiotap_hdr *radiotap_hdr;
	u32 status = priv->eventcause;
	int txfail;
	int try_count;

	if (priv->monitormode == LBS_MONITOR_OFF ||
	    priv->currenttxskb == NULL)
		return;

	radiotap_hdr = (struct tx_radiotap_hdr *)priv->currenttxskb->data;

	txfail = (status >> 24);

#if 0
	/* The version of roofnet that we've tested does not use this yet
	 * But it may be used in the future.
	 */
	if (txfail)
		radiotap_hdr->flags &= IEEE80211_RADIOTAP_F_TX_FAIL;
#endif
	try_count = (status >> 16) & 0xff;
	radiotap_hdr->data_retries = (try_count) ?
	    (1 + priv->txretrycount - try_count) : 0;
	lbs_upload_rx_packet(priv, priv->currenttxskb);
	priv->currenttxskb = NULL;

	if (priv->connect_status == LBS_CONNECTED)
		netif_wake_queue(priv->dev);

	if (priv->mesh_dev && (priv->mesh_connect_status == LBS_CONNECTED))
		netif_wake_queue(priv->mesh_dev);
}
EXPORT_SYMBOL_GPL(lbs_send_tx_feedback);
