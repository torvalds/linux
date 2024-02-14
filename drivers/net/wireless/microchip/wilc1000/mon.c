// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2012 - 2018 Microchip Technology Inc., and its subsidiaries.
 * All rights reserved.
 */

#include "cfg80211.h"

struct wilc_wfi_radiotap_hdr {
	struct ieee80211_radiotap_header hdr;
	u8 rate;
} __packed;

struct wilc_wfi_radiotap_cb_hdr {
	struct ieee80211_radiotap_header hdr;
	u8 rate;
	u8 dump;
	u16 tx_flags;
} __packed;

#define TX_RADIOTAP_PRESENT ((1 << IEEE80211_RADIOTAP_RATE) |	\
			     (1 << IEEE80211_RADIOTAP_TX_FLAGS))

void wilc_wfi_monitor_rx(struct net_device *mon_dev, u8 *buff, u32 size)
{
	u32 header, pkt_offset;
	struct sk_buff *skb = NULL;
	struct wilc_wfi_radiotap_hdr *hdr;
	struct wilc_wfi_radiotap_cb_hdr *cb_hdr;

	if (!mon_dev)
		return;

	if (!netif_running(mon_dev))
		return;

	/* Get WILC header */
	header = get_unaligned_le32(buff - HOST_HDR_OFFSET);
	/*
	 * The packet offset field contain info about what type of management
	 * the frame we are dealing with and ack status
	 */
	pkt_offset = FIELD_GET(WILC_PKT_HDR_OFFSET_FIELD, header);

	if (pkt_offset & IS_MANAGMEMENT_CALLBACK) {
		/* hostapd callback mgmt frame */

		skb = dev_alloc_skb(size + sizeof(*cb_hdr));
		if (!skb)
			return;

		skb_put_data(skb, buff, size);

		cb_hdr = skb_push(skb, sizeof(*cb_hdr));
		memset(cb_hdr, 0, sizeof(*cb_hdr));

		cb_hdr->hdr.it_version = 0; /* PKTHDR_RADIOTAP_VERSION; */

		cb_hdr->hdr.it_len = cpu_to_le16(sizeof(*cb_hdr));

		cb_hdr->hdr.it_present = cpu_to_le32(TX_RADIOTAP_PRESENT);

		cb_hdr->rate = 5;

		if (pkt_offset & IS_MGMT_STATUS_SUCCES)	{
			/* success */
			cb_hdr->tx_flags = IEEE80211_RADIOTAP_F_TX_RTS;
		} else {
			cb_hdr->tx_flags = IEEE80211_RADIOTAP_F_TX_FAIL;
		}

	} else {
		skb = dev_alloc_skb(size + sizeof(*hdr));

		if (!skb)
			return;

		skb_put_data(skb, buff, size);
		hdr = skb_push(skb, sizeof(*hdr));
		memset(hdr, 0, sizeof(struct wilc_wfi_radiotap_hdr));
		hdr->hdr.it_version = 0; /* PKTHDR_RADIOTAP_VERSION; */
		hdr->hdr.it_len = cpu_to_le16(sizeof(*hdr));
		hdr->hdr.it_present = cpu_to_le32
				(1 << IEEE80211_RADIOTAP_RATE);
		hdr->rate = 5;
	}

	skb->dev = mon_dev;
	skb_reset_mac_header(skb);
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	skb->pkt_type = PACKET_OTHERHOST;
	skb->protocol = htons(ETH_P_802_2);
	memset(skb->cb, 0, sizeof(skb->cb));

	netif_rx(skb);
}

struct tx_complete_mon_data {
	int size;
	void *buff;
};

static void mgmt_tx_complete(void *priv, int status)
{
	struct tx_complete_mon_data *pv_data = priv;
	/*
	 * in case of fully hosting mode, the freeing will be done
	 * in response to the cfg packet
	 */
	kfree(pv_data->buff);

	kfree(pv_data);
}

static int mon_mgmt_tx(struct net_device *dev, const u8 *buf, size_t len)
{
	struct tx_complete_mon_data *mgmt_tx = NULL;

	if (!dev)
		return -EFAULT;

	netif_stop_queue(dev);
	mgmt_tx = kmalloc(sizeof(*mgmt_tx), GFP_ATOMIC);
	if (!mgmt_tx)
		return -ENOMEM;

	mgmt_tx->buff = kmemdup(buf, len, GFP_ATOMIC);
	if (!mgmt_tx->buff) {
		kfree(mgmt_tx);
		return -ENOMEM;
	}

	mgmt_tx->size = len;

	wilc_wlan_txq_add_mgmt_pkt(dev, mgmt_tx, mgmt_tx->buff, mgmt_tx->size,
				   mgmt_tx_complete);

	netif_wake_queue(dev);
	return 0;
}

static netdev_tx_t wilc_wfi_mon_xmit(struct sk_buff *skb,
				     struct net_device *dev)
{
	u32 rtap_len, ret = 0;
	struct wilc_wfi_mon_priv  *mon_priv;
	struct sk_buff *skb2;
	struct wilc_wfi_radiotap_cb_hdr *cb_hdr;
	u8 srcadd[ETH_ALEN];
	u8 bssid[ETH_ALEN];

	mon_priv = netdev_priv(dev);
	if (!mon_priv)
		return -EFAULT;

	rtap_len = ieee80211_get_radiotap_len(skb->data);
	if (skb->len < rtap_len)
		return -1;

	skb_pull(skb, rtap_len);

	if (skb->data[0] == 0xc0 && is_broadcast_ether_addr(&skb->data[4])) {
		skb2 = dev_alloc_skb(skb->len + sizeof(*cb_hdr));
		if (!skb2)
			return -ENOMEM;

		skb_put_data(skb2, skb->data, skb->len);

		cb_hdr = skb_push(skb2, sizeof(*cb_hdr));
		memset(cb_hdr, 0, sizeof(struct wilc_wfi_radiotap_cb_hdr));

		cb_hdr->hdr.it_version = 0; /* PKTHDR_RADIOTAP_VERSION; */

		cb_hdr->hdr.it_len = cpu_to_le16(sizeof(*cb_hdr));

		cb_hdr->hdr.it_present = cpu_to_le32(TX_RADIOTAP_PRESENT);

		cb_hdr->rate = 5;
		cb_hdr->tx_flags = 0x0004;

		skb2->dev = dev;
		skb_reset_mac_header(skb2);
		skb2->ip_summed = CHECKSUM_UNNECESSARY;
		skb2->pkt_type = PACKET_OTHERHOST;
		skb2->protocol = htons(ETH_P_802_2);
		memset(skb2->cb, 0, sizeof(skb2->cb));

		netif_rx(skb2);

		return 0;
	}
	skb->dev = mon_priv->real_ndev;

	ether_addr_copy(srcadd, &skb->data[10]);
	ether_addr_copy(bssid, &skb->data[16]);
	/*
	 * Identify if data or mgmt packet, if source address and bssid
	 * fields are equal send it to mgmt frames handler
	 */
	if (!(memcmp(srcadd, bssid, 6))) {
		ret = mon_mgmt_tx(mon_priv->real_ndev, skb->data, skb->len);
		if (ret)
			netdev_err(dev, "fail to mgmt tx\n");
		dev_kfree_skb(skb);
	} else {
		ret = wilc_mac_xmit(skb, mon_priv->real_ndev);
	}

	return ret;
}

static const struct net_device_ops wilc_wfi_netdev_ops = {
	.ndo_start_xmit         = wilc_wfi_mon_xmit,

};

struct net_device *wilc_wfi_init_mon_interface(struct wilc *wl,
					       const char *name,
					       struct net_device *real_dev)
{
	struct wilc_wfi_mon_priv *priv;

	/* If monitor interface is already initialized, return it */
	if (wl->monitor_dev)
		return wl->monitor_dev;

	wl->monitor_dev = alloc_etherdev(sizeof(struct wilc_wfi_mon_priv));
	if (!wl->monitor_dev)
		return NULL;

	wl->monitor_dev->type = ARPHRD_IEEE80211_RADIOTAP;
	strscpy(wl->monitor_dev->name, name, IFNAMSIZ);
	wl->monitor_dev->netdev_ops = &wilc_wfi_netdev_ops;
	wl->monitor_dev->needs_free_netdev = true;

	if (register_netdevice(wl->monitor_dev)) {
		netdev_err(real_dev, "register_netdevice failed\n");
		free_netdev(wl->monitor_dev);
		return NULL;
	}
	priv = netdev_priv(wl->monitor_dev);

	priv->real_ndev = real_dev;

	return wl->monitor_dev;
}

void wilc_wfi_deinit_mon_interface(struct wilc *wl, bool rtnl_locked)
{
	if (!wl->monitor_dev)
		return;

	if (rtnl_locked)
		unregister_netdevice(wl->monitor_dev);
	else
		unregister_netdev(wl->monitor_dev);
	wl->monitor_dev = NULL;
}
