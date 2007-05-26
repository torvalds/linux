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
 *  @brief This function processes a single packet and sends
 *  to IF layer
 *
 *  @param priv    A pointer to wlan_private structure
 *  @param skb     A pointer to skb which includes TX packet
 *  @return 	   0 or -1
 */
static int SendSinglePacket(wlan_private * priv, struct sk_buff *skb)
{
	wlan_adapter *adapter = priv->adapter;
	int ret = 0;
	struct txpd localtxpd;
	struct txpd *plocaltxpd = &localtxpd;
	u8 *p802x_hdr;
	struct tx_radiotap_hdr *pradiotap_hdr;
	u32 new_rate;
	u8 *ptr = priv->adapter->tmptxbuf;

	lbs_deb_enter(LBS_DEB_TX);

	if (priv->adapter->surpriseremoved)
		return -1;

	if ((priv->adapter->debugmode & MRVDRV_DEBUG_TX_PATH) != 0)
		lbs_dbg_hex("TX packet: ", skb->data,
			 min_t(unsigned int, skb->len, 100));

	if (!skb->len || (skb->len > MRVDRV_ETH_TX_PACKET_BUFFER_SIZE)) {
		lbs_deb_tx("tx err: skb length %d 0 or > %zd\n",
		       skb->len, MRVDRV_ETH_TX_PACKET_BUFFER_SIZE);
		ret = -1;
		goto done;
	}

	memset(plocaltxpd, 0, sizeof(struct txpd));

	plocaltxpd->tx_packet_length = cpu_to_le16(skb->len);

	/* offset of actual data */
	plocaltxpd->tx_packet_location = cpu_to_le32(sizeof(struct txpd));

	/* TxCtrl set by user or default */
	plocaltxpd->tx_control = cpu_to_le32(adapter->pkttxctrl);

	p802x_hdr = skb->data;
	if (priv->adapter->radiomode == WLAN_RADIOMODE_RADIOTAP) {

		/* locate radiotap header */
		pradiotap_hdr = (struct tx_radiotap_hdr *)skb->data;

		/* set txpd fields from the radiotap header */
		new_rate = convert_radiotap_rate_to_mv(pradiotap_hdr->rate);
		if (new_rate != 0) {
			/* use new tx_control[4:0] */
			new_rate |= (adapter->pkttxctrl & ~0x1f);
			plocaltxpd->tx_control = cpu_to_le32(new_rate);
		}

		/* skip the radiotap header */
		p802x_hdr += sizeof(struct tx_radiotap_hdr);
		plocaltxpd->tx_packet_length =
			cpu_to_le16(le16_to_cpu(plocaltxpd->tx_packet_length)
				    - sizeof(struct tx_radiotap_hdr));

	}
	/* copy destination address from 802.3 or 802.11 header */
	if (priv->adapter->linkmode == WLAN_LINKMODE_802_11)
		memcpy(plocaltxpd->tx_dest_addr_high, p802x_hdr + 4, ETH_ALEN);
	else
		memcpy(plocaltxpd->tx_dest_addr_high, p802x_hdr, ETH_ALEN);

	lbs_dbg_hex("txpd", (u8 *) plocaltxpd, sizeof(struct txpd));

	if (IS_MESH_FRAME(skb)) {
		plocaltxpd->tx_control |= cpu_to_le32(TxPD_MESH_FRAME);
	}

	memcpy(ptr, plocaltxpd, sizeof(struct txpd));

	ptr += sizeof(struct txpd);

	lbs_dbg_hex("Tx Data", (u8 *) p802x_hdr, le16_to_cpu(plocaltxpd->tx_packet_length));
	memcpy(ptr, p802x_hdr, le16_to_cpu(plocaltxpd->tx_packet_length));
	ret = priv->hw_host_to_card(priv, MVMS_DAT,
				    priv->adapter->tmptxbuf,
				    le16_to_cpu(plocaltxpd->tx_packet_length) +
				    sizeof(struct txpd));

	if (ret) {
		lbs_deb_tx("tx err: hw_host_to_card returned 0x%X\n", ret);
		goto done;
	}

	lbs_deb_tx("SendSinglePacket succeeds\n");

done:
	if (!ret) {
		priv->stats.tx_packets++;
		priv->stats.tx_bytes += skb->len;
	} else {
		priv->stats.tx_dropped++;
		priv->stats.tx_errors++;
	}

	if (!ret && priv->adapter->radiomode == WLAN_RADIOMODE_RADIOTAP) {
		/* Keep the skb to echo it back once Tx feedback is
		   received from FW */
		skb_orphan(skb);
		/* stop processing outgoing pkts */
		netif_stop_queue(priv->dev);
		netif_stop_queue(priv->mesh_dev);
		/* freeze any packets already in our queues */
		priv->adapter->TxLockFlag = 1;
	} else {
		dev_kfree_skb_any(skb);
		priv->adapter->currenttxskb = NULL;
	}

	lbs_deb_leave_args(LBS_DEB_TX, "ret %d", ret);
	return ret;
}


void libertas_tx_runqueue(wlan_private *priv)
{
	wlan_adapter *adapter = priv->adapter;
	int i;

	spin_lock(&adapter->txqueue_lock);
	for (i = 0; i < adapter->tx_queue_idx; i++) {
		struct sk_buff *skb = adapter->tx_queue_ps[i];
		spin_unlock(&adapter->txqueue_lock);
		SendSinglePacket(priv, skb);
		spin_lock(&adapter->txqueue_lock);
	}
	adapter->tx_queue_idx = 0;
	spin_unlock(&adapter->txqueue_lock);
}

static void wlan_tx_queue(wlan_private *priv, struct sk_buff *skb)
{
	wlan_adapter *adapter = priv->adapter;

	spin_lock(&adapter->txqueue_lock);

	WARN_ON(priv->adapter->tx_queue_idx >= NR_TX_QUEUE);
	adapter->tx_queue_ps[adapter->tx_queue_idx++] = skb;
	if (adapter->tx_queue_idx == NR_TX_QUEUE) {
		netif_stop_queue(priv->dev);
		netif_stop_queue(priv->mesh_dev);
	} else {
		netif_start_queue(priv->dev);
		netif_start_queue(priv->mesh_dev);
	}

	spin_unlock(&adapter->txqueue_lock);
}

/**
 *  @brief This function checks the conditions and sends packet to IF
 *  layer if everything is ok.
 *
 *  @param priv    A pointer to wlan_private structure
 *  @return 	   n/a
 */
int libertas_process_tx(wlan_private * priv, struct sk_buff *skb)
{
	int ret = -1;

	lbs_deb_enter(LBS_DEB_TX);
	lbs_dbg_hex("TX Data", skb->data, min_t(unsigned int, skb->len, 100));

	if (priv->dnld_sent) {
		lbs_pr_alert( "TX error: dnld_sent = %d, not sending\n",
		       priv->dnld_sent);
		goto done;
	}

	if ((priv->adapter->psstate == PS_STATE_SLEEP) ||
	    (priv->adapter->psstate == PS_STATE_PRE_SLEEP)) {
		wlan_tx_queue(priv, skb);
		return ret;
	}

	priv->adapter->currenttxskb = skb;

	ret = SendSinglePacket(priv, skb);
done:
	lbs_deb_leave_args(LBS_DEB_TX, "ret %d", ret);
	return ret;
}

/**
 *  @brief This function sends to the host the last transmitted packet,
 *  filling the radiotap headers with transmission information.
 *
 *  @param priv     A pointer to wlan_private structure
 *  @param status   A 32 bit value containing transmission status.
 *
 *  @returns void
 */
void libertas_send_tx_feedback(wlan_private * priv)
{
	wlan_adapter *adapter = priv->adapter;
	struct tx_radiotap_hdr *radiotap_hdr;
	u32 status = adapter->eventcause;
	int txfail;
	int try_count;

	if (adapter->radiomode != WLAN_RADIOMODE_RADIOTAP ||
	    adapter->currenttxskb == NULL)
		return;

	radiotap_hdr = (struct tx_radiotap_hdr *)adapter->currenttxskb->data;

	if ((adapter->debugmode & MRVDRV_DEBUG_TX_PATH) != 0)
		lbs_dbg_hex("TX feedback: ", (u8 *) radiotap_hdr,
			min_t(unsigned int, adapter->currenttxskb->len, 100));

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
	    (1 + adapter->txretrycount - try_count) : 0;
	libertas_upload_rx_packet(priv, adapter->currenttxskb);
	adapter->currenttxskb = NULL;
	priv->adapter->TxLockFlag = 0;
	if (priv->adapter->connect_status == libertas_connected) {
		netif_wake_queue(priv->dev);
		netif_wake_queue(priv->mesh_dev);
	}
}
EXPORT_SYMBOL_GPL(libertas_send_tx_feedback);
