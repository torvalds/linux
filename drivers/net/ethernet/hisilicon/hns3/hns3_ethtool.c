/*
 * Copyright (c) 2016~2017 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/etherdevice.h>
#include <linux/string.h>
#include <linux/phy.h>

#include "hns3_enet.h"

struct hns3_stats {
	char stats_string[ETH_GSTRING_LEN];
	int stats_offset;
};

/* tqp related stats */
#define HNS3_TQP_STAT(_string, _member)	{			\
	.stats_string = _string,				\
	.stats_offset = offsetof(struct hns3_enet_ring, stats) +\
			offsetof(struct ring_stats, _member),   \
}

static const struct hns3_stats hns3_txq_stats[] = {
	/* Tx per-queue statistics */
	HNS3_TQP_STAT("io_err_cnt", io_err_cnt),
	HNS3_TQP_STAT("tx_dropped", sw_err_cnt),
	HNS3_TQP_STAT("seg_pkt_cnt", seg_pkt_cnt),
	HNS3_TQP_STAT("packets", tx_pkts),
	HNS3_TQP_STAT("bytes", tx_bytes),
	HNS3_TQP_STAT("errors", tx_err_cnt),
	HNS3_TQP_STAT("tx_wake", restart_queue),
	HNS3_TQP_STAT("tx_busy", tx_busy),
};

#define HNS3_TXQ_STATS_COUNT ARRAY_SIZE(hns3_txq_stats)

static const struct hns3_stats hns3_rxq_stats[] = {
	/* Rx per-queue statistics */
	HNS3_TQP_STAT("io_err_cnt", io_err_cnt),
	HNS3_TQP_STAT("rx_dropped", sw_err_cnt),
	HNS3_TQP_STAT("seg_pkt_cnt", seg_pkt_cnt),
	HNS3_TQP_STAT("packets", rx_pkts),
	HNS3_TQP_STAT("bytes", rx_bytes),
	HNS3_TQP_STAT("errors", rx_err_cnt),
	HNS3_TQP_STAT("reuse_pg_cnt", reuse_pg_cnt),
	HNS3_TQP_STAT("err_pkt_len", err_pkt_len),
	HNS3_TQP_STAT("non_vld_descs", non_vld_descs),
	HNS3_TQP_STAT("err_bd_num", err_bd_num),
	HNS3_TQP_STAT("l2_err", l2_err),
	HNS3_TQP_STAT("l3l4_csum_err", l3l4_csum_err),
};

#define HNS3_RXQ_STATS_COUNT ARRAY_SIZE(hns3_rxq_stats)

#define HNS3_TQP_STATS_COUNT (HNS3_TXQ_STATS_COUNT + HNS3_RXQ_STATS_COUNT)

/* netdev stats */
#define HNS3_NETDEV_STAT(_string, _member)	{			\
	.stats_string = _string,					\
	.stats_offset = offsetof(struct rtnl_link_stats64, _member)	\
}

static const struct hns3_stats hns3_netdev_stats[] = {
	/* Rx per-queue statistics */
	HNS3_NETDEV_STAT("rx_packets", rx_packets),
	HNS3_NETDEV_STAT("tx_packets", tx_packets),
	HNS3_NETDEV_STAT("rx_bytes", rx_bytes),
	HNS3_NETDEV_STAT("tx_bytes", tx_bytes),
	HNS3_NETDEV_STAT("rx_errors", rx_errors),
	HNS3_NETDEV_STAT("tx_errors", tx_errors),
	HNS3_NETDEV_STAT("rx_dropped", rx_dropped),
	HNS3_NETDEV_STAT("tx_dropped", tx_dropped),
	HNS3_NETDEV_STAT("multicast", multicast),
	HNS3_NETDEV_STAT("collisions", collisions),
	HNS3_NETDEV_STAT("rx_length_errors", rx_length_errors),
	HNS3_NETDEV_STAT("rx_over_errors", rx_over_errors),
	HNS3_NETDEV_STAT("rx_crc_errors", rx_crc_errors),
	HNS3_NETDEV_STAT("rx_frame_errors", rx_frame_errors),
	HNS3_NETDEV_STAT("rx_fifo_errors", rx_fifo_errors),
	HNS3_NETDEV_STAT("rx_missed_errors", rx_missed_errors),
	HNS3_NETDEV_STAT("tx_aborted_errors", tx_aborted_errors),
	HNS3_NETDEV_STAT("tx_carrier_errors", tx_carrier_errors),
	HNS3_NETDEV_STAT("tx_fifo_errors", tx_fifo_errors),
	HNS3_NETDEV_STAT("tx_heartbeat_errors", tx_heartbeat_errors),
	HNS3_NETDEV_STAT("tx_window_errors", tx_window_errors),
	HNS3_NETDEV_STAT("rx_compressed", rx_compressed),
	HNS3_NETDEV_STAT("tx_compressed", tx_compressed),
};

#define HNS3_NETDEV_STATS_COUNT ARRAY_SIZE(hns3_netdev_stats)

#define HNS3_SELF_TEST_TPYE_NUM		1
#define HNS3_NIC_LB_TEST_PKT_NUM	1
#define HNS3_NIC_LB_TEST_RING_ID	0
#define HNS3_NIC_LB_TEST_PACKET_SIZE	128

/* Nic loopback test err  */
#define HNS3_NIC_LB_TEST_NO_MEM_ERR	1
#define HNS3_NIC_LB_TEST_TX_CNT_ERR	2
#define HNS3_NIC_LB_TEST_RX_CNT_ERR	3

struct hns3_link_mode_mapping {
	u32 hns3_link_mode;
	u32 ethtool_link_mode;
};

static const struct hns3_link_mode_mapping hns3_lm_map[] = {
	{HNS3_LM_FIBRE_BIT, ETHTOOL_LINK_MODE_FIBRE_BIT},
	{HNS3_LM_AUTONEG_BIT, ETHTOOL_LINK_MODE_Autoneg_BIT},
	{HNS3_LM_TP_BIT, ETHTOOL_LINK_MODE_TP_BIT},
	{HNS3_LM_PAUSE_BIT, ETHTOOL_LINK_MODE_Pause_BIT},
	{HNS3_LM_BACKPLANE_BIT, ETHTOOL_LINK_MODE_Backplane_BIT},
	{HNS3_LM_10BASET_HALF_BIT, ETHTOOL_LINK_MODE_10baseT_Half_BIT},
	{HNS3_LM_10BASET_FULL_BIT, ETHTOOL_LINK_MODE_10baseT_Full_BIT},
	{HNS3_LM_100BASET_HALF_BIT, ETHTOOL_LINK_MODE_100baseT_Half_BIT},
	{HNS3_LM_100BASET_FULL_BIT, ETHTOOL_LINK_MODE_100baseT_Full_BIT},
	{HNS3_LM_1000BASET_FULL_BIT, ETHTOOL_LINK_MODE_1000baseT_Full_BIT},
};

static int hns3_lp_setup(struct net_device *ndev, enum hnae3_loop loop)
{
	struct hnae3_handle *h = hns3_get_handle(ndev);
	int ret;

	if (!h->ae_algo->ops->set_loopback ||
	    !h->ae_algo->ops->set_promisc_mode)
		return -EOPNOTSUPP;

	switch (loop) {
	case HNAE3_MAC_INTER_LOOP_MAC:
		ret = h->ae_algo->ops->set_loopback(h, loop, true);
		break;
	case HNAE3_MAC_LOOP_NONE:
		ret = h->ae_algo->ops->set_loopback(h,
			HNAE3_MAC_INTER_LOOP_MAC, false);
		break;
	default:
		ret = -ENOTSUPP;
		break;
	}

	if (ret)
		return ret;

	if (loop == HNAE3_MAC_LOOP_NONE)
		h->ae_algo->ops->set_promisc_mode(h, ndev->flags & IFF_PROMISC);
	else
		h->ae_algo->ops->set_promisc_mode(h, 1);

	return ret;
}

static int hns3_lp_up(struct net_device *ndev, enum hnae3_loop loop_mode)
{
	struct hnae3_handle *h = hns3_get_handle(ndev);
	int ret;

	if (!h->ae_algo->ops->start)
		return -EOPNOTSUPP;

	ret = h->ae_algo->ops->start(h);
	if (ret) {
		netdev_err(ndev,
			   "hns3_lb_up ae start return error: %d\n", ret);
		return ret;
	}

	ret = hns3_lp_setup(ndev, loop_mode);
	usleep_range(10000, 20000);

	return ret;
}

static int hns3_lp_down(struct net_device *ndev)
{
	struct hnae3_handle *h = hns3_get_handle(ndev);
	int ret;

	if (!h->ae_algo->ops->stop)
		return -EOPNOTSUPP;

	ret = hns3_lp_setup(ndev, HNAE3_MAC_LOOP_NONE);
	if (ret) {
		netdev_err(ndev, "lb_setup return error: %d\n", ret);
		return ret;
	}

	h->ae_algo->ops->stop(h);
	usleep_range(10000, 20000);

	return 0;
}

static void hns3_lp_setup_skb(struct sk_buff *skb)
{
	struct net_device *ndev = skb->dev;
	unsigned char *packet;
	struct ethhdr *ethh;
	unsigned int i;

	skb_reserve(skb, NET_IP_ALIGN);
	ethh = skb_put(skb, sizeof(struct ethhdr));
	packet = skb_put(skb, HNS3_NIC_LB_TEST_PACKET_SIZE);

	memcpy(ethh->h_dest, ndev->dev_addr, ETH_ALEN);
	eth_zero_addr(ethh->h_source);
	ethh->h_proto = htons(ETH_P_ARP);
	skb_reset_mac_header(skb);

	for (i = 0; i < HNS3_NIC_LB_TEST_PACKET_SIZE; i++)
		packet[i] = (unsigned char)(i & 0xff);
}

static void hns3_lb_check_skb_data(struct hns3_enet_ring *ring,
				   struct sk_buff *skb)
{
	struct hns3_enet_tqp_vector *tqp_vector = ring->tqp_vector;
	unsigned char *packet = skb->data;
	u32 i;

	for (i = 0; i < skb->len; i++)
		if (packet[i] != (unsigned char)(i & 0xff))
			break;

	/* The packet is correctly received */
	if (i == skb->len)
		tqp_vector->rx_group.total_packets++;
	else
		print_hex_dump(KERN_ERR, "selftest:", DUMP_PREFIX_OFFSET, 16, 1,
			       skb->data, skb->len, true);

	dev_kfree_skb_any(skb);
}

static u32 hns3_lb_check_rx_ring(struct hns3_nic_priv *priv, u32 budget)
{
	struct hnae3_handle *h = priv->ae_handle;
	struct hnae3_knic_private_info *kinfo;
	u32 i, rcv_good_pkt_total = 0;

	kinfo = &h->kinfo;
	for (i = kinfo->num_tqps; i < kinfo->num_tqps * 2; i++) {
		struct hns3_enet_ring *ring = priv->ring_data[i].ring;
		struct hns3_enet_ring_group *rx_group;
		u64 pre_rx_pkt;

		rx_group = &ring->tqp_vector->rx_group;
		pre_rx_pkt = rx_group->total_packets;

		hns3_clean_rx_ring(ring, budget, hns3_lb_check_skb_data);

		rcv_good_pkt_total += (rx_group->total_packets - pre_rx_pkt);
		rx_group->total_packets = pre_rx_pkt;
	}
	return rcv_good_pkt_total;
}

static void hns3_lb_clear_tx_ring(struct hns3_nic_priv *priv, u32 start_ringid,
				  u32 end_ringid, u32 budget)
{
	u32 i;

	for (i = start_ringid; i <= end_ringid; i++) {
		struct hns3_enet_ring *ring = priv->ring_data[i].ring;

		hns3_clean_tx_ring(ring, budget);
	}
}

/**
 * hns3_lp_run_test -  run loopback test
 * @ndev: net device
 * @mode: loopback type
 */
static int hns3_lp_run_test(struct net_device *ndev, enum hnae3_loop mode)
{
	struct hns3_nic_priv *priv = netdev_priv(ndev);
	struct sk_buff *skb;
	u32 i, good_cnt;
	int ret_val = 0;

	skb = alloc_skb(HNS3_NIC_LB_TEST_PACKET_SIZE + ETH_HLEN + NET_IP_ALIGN,
			GFP_KERNEL);
	if (!skb)
		return HNS3_NIC_LB_TEST_NO_MEM_ERR;

	skb->dev = ndev;
	hns3_lp_setup_skb(skb);
	skb->queue_mapping = HNS3_NIC_LB_TEST_RING_ID;

	good_cnt = 0;
	for (i = 0; i < HNS3_NIC_LB_TEST_PKT_NUM; i++) {
		netdev_tx_t tx_ret;

		skb_get(skb);
		tx_ret = hns3_nic_net_xmit(skb, ndev);
		if (tx_ret == NETDEV_TX_OK)
			good_cnt++;
		else
			netdev_err(ndev, "hns3_lb_run_test xmit failed: %d\n",
				   tx_ret);
	}
	if (good_cnt != HNS3_NIC_LB_TEST_PKT_NUM) {
		ret_val = HNS3_NIC_LB_TEST_TX_CNT_ERR;
		netdev_err(ndev, "mode %d sent fail, cnt=0x%x, budget=0x%x\n",
			   mode, good_cnt, HNS3_NIC_LB_TEST_PKT_NUM);
		goto out;
	}

	/* Allow 200 milliseconds for packets to go from Tx to Rx */
	msleep(200);

	good_cnt = hns3_lb_check_rx_ring(priv, HNS3_NIC_LB_TEST_PKT_NUM);
	if (good_cnt != HNS3_NIC_LB_TEST_PKT_NUM) {
		ret_val = HNS3_NIC_LB_TEST_RX_CNT_ERR;
		netdev_err(ndev, "mode %d recv fail, cnt=0x%x, budget=0x%x\n",
			   mode, good_cnt, HNS3_NIC_LB_TEST_PKT_NUM);
	}

out:
	hns3_lb_clear_tx_ring(priv, HNS3_NIC_LB_TEST_RING_ID,
			      HNS3_NIC_LB_TEST_RING_ID,
			      HNS3_NIC_LB_TEST_PKT_NUM);

	kfree_skb(skb);
	return ret_val;
}

/**
 * hns3_nic_self_test - self test
 * @ndev: net device
 * @eth_test: test cmd
 * @data: test result
 */
static void hns3_self_test(struct net_device *ndev,
			   struct ethtool_test *eth_test, u64 *data)
{
	struct hns3_nic_priv *priv = netdev_priv(ndev);
	struct hnae3_handle *h = priv->ae_handle;
	int st_param[HNS3_SELF_TEST_TPYE_NUM][2];
	bool if_running = netif_running(ndev);
	int test_index = 0;
	u32 i;

	/* Only do offline selftest, or pass by default */
	if (eth_test->flags != ETH_TEST_FL_OFFLINE)
		return;

	st_param[HNAE3_MAC_INTER_LOOP_MAC][0] = HNAE3_MAC_INTER_LOOP_MAC;
	st_param[HNAE3_MAC_INTER_LOOP_MAC][1] =
			h->flags & HNAE3_SUPPORT_MAC_LOOPBACK;

	if (if_running)
		dev_close(ndev);

	set_bit(HNS3_NIC_STATE_TESTING, &priv->state);

	for (i = 0; i < HNS3_SELF_TEST_TPYE_NUM; i++) {
		enum hnae3_loop loop_type = (enum hnae3_loop)st_param[i][0];

		if (!st_param[i][1])
			continue;

		data[test_index] = hns3_lp_up(ndev, loop_type);
		if (!data[test_index]) {
			data[test_index] = hns3_lp_run_test(ndev, loop_type);
			hns3_lp_down(ndev);
		}

		if (data[test_index])
			eth_test->flags |= ETH_TEST_FL_FAILED;

		test_index++;
	}

	clear_bit(HNS3_NIC_STATE_TESTING, &priv->state);

	if (if_running)
		dev_open(ndev);
}

static void hns3_driv_to_eth_caps(u32 caps, struct ethtool_link_ksettings *cmd,
				  bool is_advertised)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(hns3_lm_map); i++) {
		if (!(caps & hns3_lm_map[i].hns3_link_mode))
			continue;

		if (is_advertised)
			__set_bit(hns3_lm_map[i].ethtool_link_mode,
				  cmd->link_modes.advertising);
		else
			__set_bit(hns3_lm_map[i].ethtool_link_mode,
				  cmd->link_modes.supported);
	}
}

static int hns3_get_sset_count(struct net_device *netdev, int stringset)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);
	const struct hnae3_ae_ops *ops = h->ae_algo->ops;

	if (!ops->get_sset_count)
		return -EOPNOTSUPP;

	switch (stringset) {
	case ETH_SS_STATS:
		return ((HNS3_TQP_STATS_COUNT * h->kinfo.num_tqps) +
			ops->get_sset_count(h, stringset));

	case ETH_SS_TEST:
		return ops->get_sset_count(h, stringset);
	}

	return 0;
}

static void *hns3_update_strings(u8 *data, const struct hns3_stats *stats,
		u32 stat_count, u32 num_tqps, const char *prefix)
{
#define MAX_PREFIX_SIZE (6 + 4)
	u32 size_left;
	u32 i, j;
	u32 n1;

	for (i = 0; i < num_tqps; i++) {
		for (j = 0; j < stat_count; j++) {
			data[ETH_GSTRING_LEN - 1] = '\0';

			/* first, prepend the prefix string */
			n1 = snprintf(data, MAX_PREFIX_SIZE, "%s#%d_",
				      prefix, i);
			n1 = min_t(uint, n1, MAX_PREFIX_SIZE - 1);
			size_left = (ETH_GSTRING_LEN - 1) - n1;

			/* now, concatenate the stats string to it */
			strncat(data, stats[j].stats_string, size_left);
			data += ETH_GSTRING_LEN;
		}
	}

	return data;
}

static u8 *hns3_get_strings_tqps(struct hnae3_handle *handle, u8 *data)
{
	struct hnae3_knic_private_info *kinfo = &handle->kinfo;
	const char tx_prefix[] = "txq";
	const char rx_prefix[] = "rxq";

	/* get strings for Tx */
	data = hns3_update_strings(data, hns3_txq_stats, HNS3_TXQ_STATS_COUNT,
				   kinfo->num_tqps, tx_prefix);

	/* get strings for Rx */
	data = hns3_update_strings(data, hns3_rxq_stats, HNS3_RXQ_STATS_COUNT,
				   kinfo->num_tqps, rx_prefix);

	return data;
}

static u8 *hns3_netdev_stats_get_strings(u8 *data)
{
	int i;

	/* get strings for netdev */
	for (i = 0; i < HNS3_NETDEV_STATS_COUNT; i++) {
		snprintf(data, ETH_GSTRING_LEN,
			 hns3_netdev_stats[i].stats_string);
		data += ETH_GSTRING_LEN;
	}

	snprintf(data, ETH_GSTRING_LEN, "netdev_rx_dropped");
	data += ETH_GSTRING_LEN;
	snprintf(data, ETH_GSTRING_LEN, "netdev_tx_dropped");
	data += ETH_GSTRING_LEN;
	snprintf(data, ETH_GSTRING_LEN, "netdev_tx_timeout");
	data += ETH_GSTRING_LEN;

	return data;
}

static void hns3_get_strings(struct net_device *netdev, u32 stringset, u8 *data)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);
	const struct hnae3_ae_ops *ops = h->ae_algo->ops;
	char *buff = (char *)data;

	if (!ops->get_strings)
		return;

	switch (stringset) {
	case ETH_SS_STATS:
		buff = hns3_netdev_stats_get_strings(buff);
		buff = hns3_get_strings_tqps(h, buff);
		h->ae_algo->ops->get_strings(h, stringset, (u8 *)buff);
		break;
	case ETH_SS_TEST:
		ops->get_strings(h, stringset, data);
		break;
	}
}

static u64 *hns3_get_stats_tqps(struct hnae3_handle *handle, u64 *data)
{
	struct hns3_nic_priv *nic_priv = (struct hns3_nic_priv *)handle->priv;
	struct hnae3_knic_private_info *kinfo = &handle->kinfo;
	struct hns3_enet_ring *ring;
	u8 *stat;
	int i, j;

	/* get stats for Tx */
	for (i = 0; i < kinfo->num_tqps; i++) {
		ring = nic_priv->ring_data[i].ring;
		for (j = 0; j < HNS3_TXQ_STATS_COUNT; j++) {
			stat = (u8 *)ring + hns3_txq_stats[j].stats_offset;
			*data++ = *(u64 *)stat;
		}
	}

	/* get stats for Rx */
	for (i = 0; i < kinfo->num_tqps; i++) {
		ring = nic_priv->ring_data[i + kinfo->num_tqps].ring;
		for (j = 0; j < HNS3_RXQ_STATS_COUNT; j++) {
			stat = (u8 *)ring + hns3_rxq_stats[j].stats_offset;
			*data++ = *(u64 *)stat;
		}
	}

	return data;
}

static u64 *hns3_get_netdev_stats(struct net_device *netdev, u64 *data)
{
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	const struct rtnl_link_stats64 *net_stats;
	struct rtnl_link_stats64 temp;
	u8 *stat;
	int i;

	net_stats = dev_get_stats(netdev, &temp);
	for (i = 0; i < HNS3_NETDEV_STATS_COUNT; i++) {
		stat = (u8 *)net_stats + hns3_netdev_stats[i].stats_offset;
		*data++ = *(u64 *)stat;
	}

	*data++ = netdev->rx_dropped.counter;
	*data++ = netdev->tx_dropped.counter;
	*data++ = priv->tx_timeout_count;

	return data;
}

/* hns3_get_stats - get detail statistics.
 * @netdev: net device
 * @stats: statistics info.
 * @data: statistics data.
 */
static void hns3_get_stats(struct net_device *netdev,
			   struct ethtool_stats *stats, u64 *data)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);
	u64 *p = data;

	if (!h->ae_algo->ops->get_stats || !h->ae_algo->ops->update_stats) {
		netdev_err(netdev, "could not get any statistics\n");
		return;
	}

	p = hns3_get_netdev_stats(netdev, p);

	/* get per-queue stats */
	p = hns3_get_stats_tqps(h, p);

	/* get MAC & other misc hardware stats */
	h->ae_algo->ops->get_stats(h, p);
}

static void hns3_get_drvinfo(struct net_device *netdev,
			     struct ethtool_drvinfo *drvinfo)
{
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	struct hnae3_handle *h = priv->ae_handle;

	strncpy(drvinfo->version, hns3_driver_version,
		sizeof(drvinfo->version));
	drvinfo->version[sizeof(drvinfo->version) - 1] = '\0';

	strncpy(drvinfo->driver, h->pdev->driver->name,
		sizeof(drvinfo->driver));
	drvinfo->driver[sizeof(drvinfo->driver) - 1] = '\0';

	strncpy(drvinfo->bus_info, pci_name(h->pdev),
		sizeof(drvinfo->bus_info));
	drvinfo->bus_info[ETHTOOL_BUSINFO_LEN - 1] = '\0';

	snprintf(drvinfo->fw_version, sizeof(drvinfo->fw_version), "0x%08x",
		 priv->ae_handle->ae_algo->ops->get_fw_version(h));
}

static u32 hns3_get_link(struct net_device *netdev)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);

	if (h->ae_algo && h->ae_algo->ops && h->ae_algo->ops->get_status)
		return h->ae_algo->ops->get_status(h);
	else
		return 0;
}

static void hns3_get_ringparam(struct net_device *netdev,
			       struct ethtool_ringparam *param)
{
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	struct hnae3_handle *h = priv->ae_handle;
	int queue_num = h->kinfo.num_tqps;

	param->tx_max_pending = HNS3_RING_MAX_PENDING;
	param->rx_max_pending = HNS3_RING_MAX_PENDING;

	param->tx_pending = priv->ring_data[0].ring->desc_num;
	param->rx_pending = priv->ring_data[queue_num].ring->desc_num;
}

static void hns3_get_pauseparam(struct net_device *netdev,
				struct ethtool_pauseparam *param)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);

	if (h->ae_algo && h->ae_algo->ops && h->ae_algo->ops->get_pauseparam)
		h->ae_algo->ops->get_pauseparam(h, &param->autoneg,
			&param->rx_pause, &param->tx_pause);
}

static int hns3_set_pauseparam(struct net_device *netdev,
			       struct ethtool_pauseparam *param)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);

	if (h->ae_algo->ops->set_pauseparam)
		return h->ae_algo->ops->set_pauseparam(h, param->autoneg,
						       param->rx_pause,
						       param->tx_pause);
	return -EOPNOTSUPP;
}

static int hns3_get_link_ksettings(struct net_device *netdev,
				   struct ethtool_link_ksettings *cmd)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);
	u32 flowctrl_adv = 0;
	u32 supported_caps;
	u32 advertised_caps;
	u8 media_type = HNAE3_MEDIA_TYPE_UNKNOWN;
	u8 link_stat;

	if (!h->ae_algo || !h->ae_algo->ops)
		return -EOPNOTSUPP;

	/* 1.auto_neg & speed & duplex from cmd */
	if (netdev->phydev)
		phy_ethtool_ksettings_get(netdev->phydev, cmd);
	else if (h->ae_algo->ops->get_ksettings_an_result)
		h->ae_algo->ops->get_ksettings_an_result(h,
							 &cmd->base.autoneg,
							 &cmd->base.speed,
							 &cmd->base.duplex);
	else
		return -EOPNOTSUPP;

	link_stat = hns3_get_link(netdev);
	if (!link_stat) {
		cmd->base.speed = SPEED_UNKNOWN;
		cmd->base.duplex = DUPLEX_UNKNOWN;
	}

	/* 2.media_type get from bios parameter block */
	if (h->ae_algo->ops->get_media_type) {
		h->ae_algo->ops->get_media_type(h, &media_type);

		switch (media_type) {
		case HNAE3_MEDIA_TYPE_FIBER:
			cmd->base.port = PORT_FIBRE;
			supported_caps = HNS3_LM_FIBRE_BIT |
					 HNS3_LM_AUTONEG_BIT |
					 HNS3_LM_PAUSE_BIT |
					 HNS3_LM_1000BASET_FULL_BIT;

			advertised_caps = supported_caps;
			break;
		case HNAE3_MEDIA_TYPE_COPPER:
			cmd->base.port = PORT_TP;
			supported_caps = HNS3_LM_TP_BIT |
					 HNS3_LM_AUTONEG_BIT |
					 HNS3_LM_PAUSE_BIT |
					 HNS3_LM_1000BASET_FULL_BIT |
					 HNS3_LM_100BASET_FULL_BIT |
					 HNS3_LM_100BASET_HALF_BIT |
					 HNS3_LM_10BASET_FULL_BIT |
					 HNS3_LM_10BASET_HALF_BIT;
			advertised_caps = supported_caps;
			break;
		case HNAE3_MEDIA_TYPE_BACKPLANE:
			cmd->base.port = PORT_NONE;
			supported_caps = HNS3_LM_BACKPLANE_BIT |
					 HNS3_LM_PAUSE_BIT |
					 HNS3_LM_AUTONEG_BIT |
					 HNS3_LM_1000BASET_FULL_BIT |
					 HNS3_LM_100BASET_FULL_BIT |
					 HNS3_LM_100BASET_HALF_BIT |
					 HNS3_LM_10BASET_FULL_BIT |
					 HNS3_LM_10BASET_HALF_BIT;

			advertised_caps = supported_caps;
			break;
		case HNAE3_MEDIA_TYPE_UNKNOWN:
		default:
			cmd->base.port = PORT_OTHER;
			supported_caps = 0;
			advertised_caps = 0;
			break;
		}

		if (!cmd->base.autoneg)
			advertised_caps &= ~HNS3_LM_AUTONEG_BIT;

		advertised_caps &= ~HNS3_LM_PAUSE_BIT;

		/* now, map driver link modes to ethtool link modes */
		hns3_driv_to_eth_caps(supported_caps, cmd, false);
		hns3_driv_to_eth_caps(advertised_caps, cmd, true);
	}

	/* 3.mdix_ctrl&mdix get from phy reg */
	if (h->ae_algo->ops->get_mdix_mode)
		h->ae_algo->ops->get_mdix_mode(h, &cmd->base.eth_tp_mdix_ctrl,
					       &cmd->base.eth_tp_mdix);
	/* 4.mdio_support */
	cmd->base.mdio_support = ETH_MDIO_SUPPORTS_C22;

	/* 5.get flow control setttings */
	if (h->ae_algo->ops->get_flowctrl_adv)
		h->ae_algo->ops->get_flowctrl_adv(h, &flowctrl_adv);

	if (flowctrl_adv & ADVERTISED_Pause)
		ethtool_link_ksettings_add_link_mode(cmd, advertising,
						     Pause);

	if (flowctrl_adv & ADVERTISED_Asym_Pause)
		ethtool_link_ksettings_add_link_mode(cmd, advertising,
						     Asym_Pause);

	return 0;
}

static int hns3_set_link_ksettings(struct net_device *netdev,
				   const struct ethtool_link_ksettings *cmd)
{
	/* Only support ksettings_set for netdev with phy attached for now */
	if (netdev->phydev)
		return phy_ethtool_ksettings_set(netdev->phydev, cmd);

	return -EOPNOTSUPP;
}

static u32 hns3_get_rss_key_size(struct net_device *netdev)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);

	if (!h->ae_algo || !h->ae_algo->ops ||
	    !h->ae_algo->ops->get_rss_key_size)
		return -EOPNOTSUPP;

	return h->ae_algo->ops->get_rss_key_size(h);
}

static u32 hns3_get_rss_indir_size(struct net_device *netdev)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);

	if (!h->ae_algo || !h->ae_algo->ops ||
	    !h->ae_algo->ops->get_rss_indir_size)
		return -EOPNOTSUPP;

	return h->ae_algo->ops->get_rss_indir_size(h);
}

static int hns3_get_rss(struct net_device *netdev, u32 *indir, u8 *key,
			u8 *hfunc)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);

	if (!h->ae_algo || !h->ae_algo->ops || !h->ae_algo->ops->get_rss)
		return -EOPNOTSUPP;

	return h->ae_algo->ops->get_rss(h, indir, key, hfunc);
}

static int hns3_set_rss(struct net_device *netdev, const u32 *indir,
			const u8 *key, const u8 hfunc)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);

	if (!h->ae_algo || !h->ae_algo->ops || !h->ae_algo->ops->set_rss)
		return -EOPNOTSUPP;

	/* currently we only support Toeplitz hash */
	if ((hfunc != ETH_RSS_HASH_NO_CHANGE) && (hfunc != ETH_RSS_HASH_TOP)) {
		netdev_err(netdev,
			   "hash func not supported (only Toeplitz hash)\n");
		return -EOPNOTSUPP;
	}
	if (!indir) {
		netdev_err(netdev,
			   "set rss failed for indir is empty\n");
		return -EOPNOTSUPP;
	}

	return h->ae_algo->ops->set_rss(h, indir, key, hfunc);
}

static int hns3_get_rxnfc(struct net_device *netdev,
			  struct ethtool_rxnfc *cmd,
			  u32 *rule_locs)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);

	if (!h->ae_algo || !h->ae_algo->ops || !h->ae_algo->ops->get_rss_tuple)
		return -EOPNOTSUPP;

	switch (cmd->cmd) {
	case ETHTOOL_GRXRINGS:
		cmd->data = h->kinfo.rss_size;
		break;
	case ETHTOOL_GRXFH:
		return h->ae_algo->ops->get_rss_tuple(h, cmd);
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int hns3_change_all_ring_bd_num(struct hns3_nic_priv *priv,
				       u32 new_desc_num)
{
	struct hnae3_handle *h = priv->ae_handle;
	int i;

	h->kinfo.num_desc = new_desc_num;

	for (i = 0; i < h->kinfo.num_tqps * 2; i++)
		priv->ring_data[i].ring->desc_num = new_desc_num;

	return hns3_init_all_ring(priv);
}

static int hns3_set_ringparam(struct net_device *ndev,
			      struct ethtool_ringparam *param)
{
	struct hns3_nic_priv *priv = netdev_priv(ndev);
	struct hnae3_handle *h = priv->ae_handle;
	bool if_running = netif_running(ndev);
	u32 old_desc_num, new_desc_num;
	int ret;

	if (param->rx_mini_pending || param->rx_jumbo_pending)
		return -EINVAL;

	if (param->tx_pending != param->rx_pending) {
		netdev_err(ndev,
			   "Descriptors of tx and rx must be equal");
		return -EINVAL;
	}

	if (param->tx_pending > HNS3_RING_MAX_PENDING ||
	    param->tx_pending < HNS3_RING_MIN_PENDING) {
		netdev_err(ndev,
			   "Descriptors requested (Tx/Rx: %d) out of range [%d-%d]\n",
			   param->tx_pending, HNS3_RING_MIN_PENDING,
			   HNS3_RING_MAX_PENDING);
		return -EINVAL;
	}

	new_desc_num = param->tx_pending;

	/* Hardware requires that its descriptors must be multiple of eight */
	new_desc_num = ALIGN(new_desc_num, HNS3_RING_BD_MULTIPLE);
	old_desc_num = h->kinfo.num_desc;
	if (old_desc_num == new_desc_num)
		return 0;

	netdev_info(ndev,
		    "Changing descriptor count from %d to %d.\n",
		    old_desc_num, new_desc_num);

	if (if_running)
		dev_close(ndev);

	ret = hns3_uninit_all_ring(priv);
	if (ret)
		return ret;

	ret = hns3_change_all_ring_bd_num(priv, new_desc_num);
	if (ret) {
		ret = hns3_change_all_ring_bd_num(priv, old_desc_num);
		if (ret) {
			netdev_err(ndev,
				   "Revert to old bd num fail, ret=%d.\n", ret);
			return ret;
		}
	}

	if (if_running)
		ret = dev_open(ndev);

	return ret;
}

static int hns3_set_rxnfc(struct net_device *netdev, struct ethtool_rxnfc *cmd)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);

	if (!h->ae_algo || !h->ae_algo->ops || !h->ae_algo->ops->set_rss_tuple)
		return -EOPNOTSUPP;

	switch (cmd->cmd) {
	case ETHTOOL_SRXFH:
		return h->ae_algo->ops->set_rss_tuple(h, cmd);
	default:
		return -EOPNOTSUPP;
	}
}

static int hns3_nway_reset(struct net_device *netdev)
{
	struct phy_device *phy = netdev->phydev;

	if (!netif_running(netdev))
		return 0;

	/* Only support nway_reset for netdev with phy attached for now */
	if (!phy)
		return -EOPNOTSUPP;

	if (phy->autoneg != AUTONEG_ENABLE)
		return -EINVAL;

	return genphy_restart_aneg(phy);
}

static void hns3_get_channels(struct net_device *netdev,
			      struct ethtool_channels *ch)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);

	if (h->ae_algo->ops->get_channels)
		h->ae_algo->ops->get_channels(h, ch);
}

static const struct ethtool_ops hns3vf_ethtool_ops = {
	.get_drvinfo = hns3_get_drvinfo,
	.get_ringparam = hns3_get_ringparam,
	.set_ringparam = hns3_set_ringparam,
	.get_strings = hns3_get_strings,
	.get_ethtool_stats = hns3_get_stats,
	.get_sset_count = hns3_get_sset_count,
	.get_rxnfc = hns3_get_rxnfc,
	.get_rxfh_key_size = hns3_get_rss_key_size,
	.get_rxfh_indir_size = hns3_get_rss_indir_size,
	.get_rxfh = hns3_get_rss,
	.set_rxfh = hns3_set_rss,
	.get_link_ksettings = hns3_get_link_ksettings,
};

static const struct ethtool_ops hns3_ethtool_ops = {
	.self_test = hns3_self_test,
	.get_drvinfo = hns3_get_drvinfo,
	.get_link = hns3_get_link,
	.get_ringparam = hns3_get_ringparam,
	.set_ringparam = hns3_set_ringparam,
	.get_pauseparam = hns3_get_pauseparam,
	.set_pauseparam = hns3_set_pauseparam,
	.get_strings = hns3_get_strings,
	.get_ethtool_stats = hns3_get_stats,
	.get_sset_count = hns3_get_sset_count,
	.get_rxnfc = hns3_get_rxnfc,
	.set_rxnfc = hns3_set_rxnfc,
	.get_rxfh_key_size = hns3_get_rss_key_size,
	.get_rxfh_indir_size = hns3_get_rss_indir_size,
	.get_rxfh = hns3_get_rss,
	.set_rxfh = hns3_set_rss,
	.get_link_ksettings = hns3_get_link_ksettings,
	.set_link_ksettings = hns3_set_link_ksettings,
	.nway_reset = hns3_nway_reset,
	.get_channels = hns3_get_channels,
	.set_channels = hns3_set_channels,
};

void hns3_ethtool_set_ops(struct net_device *netdev)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);

	if (h->flags & HNAE3_SUPPORT_VF)
		netdev->ethtool_ops = &hns3vf_ethtool_ops;
	else
		netdev->ethtool_ops = &hns3_ethtool_ops;
}
