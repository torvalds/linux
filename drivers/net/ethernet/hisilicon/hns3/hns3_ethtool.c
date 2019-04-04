// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) 2016-2017 Hisilicon Limited.

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
	HNS3_TQP_STAT("dropped", sw_err_cnt),
	HNS3_TQP_STAT("seg_pkt_cnt", seg_pkt_cnt),
	HNS3_TQP_STAT("packets", tx_pkts),
	HNS3_TQP_STAT("bytes", tx_bytes),
	HNS3_TQP_STAT("errors", tx_err_cnt),
	HNS3_TQP_STAT("wake", restart_queue),
	HNS3_TQP_STAT("busy", tx_busy),
};

#define HNS3_TXQ_STATS_COUNT ARRAY_SIZE(hns3_txq_stats)

static const struct hns3_stats hns3_rxq_stats[] = {
	/* Rx per-queue statistics */
	HNS3_TQP_STAT("io_err_cnt", io_err_cnt),
	HNS3_TQP_STAT("dropped", sw_err_cnt),
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
	HNS3_TQP_STAT("multicast", rx_multicast),
};

#define HNS3_RXQ_STATS_COUNT ARRAY_SIZE(hns3_rxq_stats)

#define HNS3_TQP_STATS_COUNT (HNS3_TXQ_STATS_COUNT + HNS3_RXQ_STATS_COUNT)

#define HNS3_SELF_TEST_TYPE_NUM         3
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

static int hns3_lp_setup(struct net_device *ndev, enum hnae3_loop loop, bool en)
{
	struct hnae3_handle *h = hns3_get_handle(ndev);
	bool vlan_filter_enable;
	int ret;

	if (!h->ae_algo->ops->set_loopback ||
	    !h->ae_algo->ops->set_promisc_mode)
		return -EOPNOTSUPP;

	switch (loop) {
	case HNAE3_LOOP_SERIAL_SERDES:
	case HNAE3_LOOP_PARALLEL_SERDES:
	case HNAE3_LOOP_APP:
		ret = h->ae_algo->ops->set_loopback(h, loop, en);
		break;
	default:
		ret = -ENOTSUPP;
		break;
	}

	if (ret)
		return ret;

	if (en) {
		h->ae_algo->ops->set_promisc_mode(h, true, true);
	} else {
		/* recover promisc mode before loopback test */
		hns3_update_promisc_mode(ndev, h->netdev_flags);
		vlan_filter_enable = ndev->flags & IFF_PROMISC ? false : true;
		hns3_enable_vlan_filter(ndev, vlan_filter_enable);
	}

	return ret;
}

static int hns3_lp_up(struct net_device *ndev, enum hnae3_loop loop_mode)
{
	struct hnae3_handle *h = hns3_get_handle(ndev);
	int ret;

	ret = hns3_nic_reset_all_ring(h);
	if (ret)
		return ret;

	ret = hns3_lp_setup(ndev, loop_mode, true);
	usleep_range(10000, 20000);

	return ret;
}

static int hns3_lp_down(struct net_device *ndev, enum hnae3_loop loop_mode)
{
	int ret;

	ret = hns3_lp_setup(ndev, loop_mode, false);
	if (ret) {
		netdev_err(ndev, "lb_setup return error: %d\n", ret);
		return ret;
	}

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
	ethh->h_dest[5] += 0x1f;
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

		preempt_disable();
		hns3_clean_rx_ring(ring, budget, hns3_lb_check_skb_data);
		preempt_enable();

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

		hns3_clean_tx_ring(ring);
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
	int st_param[HNS3_SELF_TEST_TYPE_NUM][2];
	bool if_running = netif_running(ndev);
#if IS_ENABLED(CONFIG_VLAN_8021Q)
	bool dis_vlan_filter;
#endif
	int test_index = 0;
	u32 i;

	if (hns3_nic_resetting(ndev)) {
		netdev_err(ndev, "dev resetting!");
		return;
	}

	/* Only do offline selftest, or pass by default */
	if (eth_test->flags != ETH_TEST_FL_OFFLINE)
		return;

	st_param[HNAE3_LOOP_APP][0] = HNAE3_LOOP_APP;
	st_param[HNAE3_LOOP_APP][1] =
			h->flags & HNAE3_SUPPORT_APP_LOOPBACK;

	st_param[HNAE3_LOOP_SERIAL_SERDES][0] = HNAE3_LOOP_SERIAL_SERDES;
	st_param[HNAE3_LOOP_SERIAL_SERDES][1] =
			h->flags & HNAE3_SUPPORT_SERDES_SERIAL_LOOPBACK;

	st_param[HNAE3_LOOP_PARALLEL_SERDES][0] =
			HNAE3_LOOP_PARALLEL_SERDES;
	st_param[HNAE3_LOOP_PARALLEL_SERDES][1] =
			h->flags & HNAE3_SUPPORT_SERDES_PARALLEL_LOOPBACK;

	if (if_running)
		ndev->netdev_ops->ndo_stop(ndev);

#if IS_ENABLED(CONFIG_VLAN_8021Q)
	/* Disable the vlan filter for selftest does not support it */
	dis_vlan_filter = (ndev->features & NETIF_F_HW_VLAN_CTAG_FILTER) &&
				h->ae_algo->ops->enable_vlan_filter;
	if (dis_vlan_filter)
		h->ae_algo->ops->enable_vlan_filter(h, false);
#endif

	set_bit(HNS3_NIC_STATE_TESTING, &priv->state);

	for (i = 0; i < HNS3_SELF_TEST_TYPE_NUM; i++) {
		enum hnae3_loop loop_type = (enum hnae3_loop)st_param[i][0];

		if (!st_param[i][1])
			continue;

		data[test_index] = hns3_lp_up(ndev, loop_type);
		if (!data[test_index])
			data[test_index] = hns3_lp_run_test(ndev, loop_type);

		hns3_lp_down(ndev, loop_type);

		if (data[test_index])
			eth_test->flags |= ETH_TEST_FL_FAILED;

		test_index++;
	}

	clear_bit(HNS3_NIC_STATE_TESTING, &priv->state);

#if IS_ENABLED(CONFIG_VLAN_8021Q)
	if (dis_vlan_filter)
		h->ae_algo->ops->enable_vlan_filter(h, true);
#endif

	if (if_running)
		ndev->netdev_ops->ndo_open(ndev);
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

	default:
		return -EOPNOTSUPP;
	}
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
			n1 = snprintf(data, MAX_PREFIX_SIZE, "%s%d_",
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

static void hns3_get_strings(struct net_device *netdev, u32 stringset, u8 *data)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);
	const struct hnae3_ae_ops *ops = h->ae_algo->ops;
	char *buff = (char *)data;

	if (!ops->get_strings)
		return;

	switch (stringset) {
	case ETH_SS_STATS:
		buff = hns3_get_strings_tqps(h, buff);
		h->ae_algo->ops->get_strings(h, stringset, (u8 *)buff);
		break;
	case ETH_SS_TEST:
		ops->get_strings(h, stringset, data);
		break;
	default:
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

	h->ae_algo->ops->update_stats(h, &netdev->stats);

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

	if (hns3_nic_resetting(netdev)) {
		netdev_err(netdev, "dev resetting!");
		return;
	}

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

static void hns3_get_ksettings(struct hnae3_handle *h,
			       struct ethtool_link_ksettings *cmd)
{
	const struct hnae3_ae_ops *ops = h->ae_algo->ops;

	/* 1.auto_neg & speed & duplex from cmd */
	if (ops->get_ksettings_an_result)
		ops->get_ksettings_an_result(h,
					     &cmd->base.autoneg,
					     &cmd->base.speed,
					     &cmd->base.duplex);

	/* 2.get link mode*/
	if (ops->get_link_mode)
		ops->get_link_mode(h,
				   cmd->link_modes.supported,
				   cmd->link_modes.advertising);

	/* 3.mdix_ctrl&mdix get from phy reg */
	if (ops->get_mdix_mode)
		ops->get_mdix_mode(h, &cmd->base.eth_tp_mdix_ctrl,
				   &cmd->base.eth_tp_mdix);
}

static int hns3_get_link_ksettings(struct net_device *netdev,
				   struct ethtool_link_ksettings *cmd)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);
	const struct hnae3_ae_ops *ops;
	u8 media_type;
	u8 link_stat;

	if (!h->ae_algo || !h->ae_algo->ops)
		return -EOPNOTSUPP;

	ops = h->ae_algo->ops;
	if (ops->get_media_type)
		ops->get_media_type(h, &media_type);
	else
		return -EOPNOTSUPP;

	switch (media_type) {
	case HNAE3_MEDIA_TYPE_NONE:
		cmd->base.port = PORT_NONE;
		hns3_get_ksettings(h, cmd);
		break;
	case HNAE3_MEDIA_TYPE_FIBER:
		cmd->base.port = PORT_FIBRE;
		hns3_get_ksettings(h, cmd);
		break;
	case HNAE3_MEDIA_TYPE_COPPER:
		cmd->base.port = PORT_TP;
		if (!netdev->phydev)
			hns3_get_ksettings(h, cmd);
		else
			phy_ethtool_ksettings_get(netdev->phydev, cmd);
		break;
	default:

		netdev_warn(netdev, "Unknown media type");
		return 0;
	}

	/* mdio_support */
	cmd->base.mdio_support = ETH_MDIO_SUPPORTS_C22;

	link_stat = hns3_get_link(netdev);
	if (!link_stat) {
		cmd->base.speed = SPEED_UNKNOWN;
		cmd->base.duplex = DUPLEX_UNKNOWN;
	}

	return 0;
}

static int hns3_set_link_ksettings(struct net_device *netdev,
				   const struct ethtool_link_ksettings *cmd)
{
	/* Chip doesn't support this mode. */
	if (cmd->base.speed == SPEED_1000 && cmd->base.duplex == DUPLEX_HALF)
		return -EINVAL;

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
		return 0;

	return h->ae_algo->ops->get_rss_key_size(h);
}

static u32 hns3_get_rss_indir_size(struct net_device *netdev)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);

	if (!h->ae_algo || !h->ae_algo->ops ||
	    !h->ae_algo->ops->get_rss_indir_size)
		return 0;

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

	if ((h->pdev->revision == 0x20 &&
	     hfunc != ETH_RSS_HASH_TOP) || (hfunc != ETH_RSS_HASH_NO_CHANGE &&
	     hfunc != ETH_RSS_HASH_TOP && hfunc != ETH_RSS_HASH_XOR)) {
		netdev_err(netdev, "hash func not supported\n");
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

	if (!h->ae_algo || !h->ae_algo->ops)
		return -EOPNOTSUPP;

	switch (cmd->cmd) {
	case ETHTOOL_GRXRINGS:
		cmd->data = h->kinfo.num_tqps;
		return 0;
	case ETHTOOL_GRXFH:
		if (h->ae_algo->ops->get_rss_tuple)
			return h->ae_algo->ops->get_rss_tuple(h, cmd);
		return -EOPNOTSUPP;
	case ETHTOOL_GRXCLSRLCNT:
		if (h->ae_algo->ops->get_fd_rule_cnt)
			return h->ae_algo->ops->get_fd_rule_cnt(h, cmd);
		return -EOPNOTSUPP;
	case ETHTOOL_GRXCLSRULE:
		if (h->ae_algo->ops->get_fd_rule_info)
			return h->ae_algo->ops->get_fd_rule_info(h, cmd);
		return -EOPNOTSUPP;
	case ETHTOOL_GRXCLSRLALL:
		if (h->ae_algo->ops->get_fd_all_rules)
			return h->ae_algo->ops->get_fd_all_rules(h, cmd,
								 rule_locs);
		return -EOPNOTSUPP;
	default:
		return -EOPNOTSUPP;
	}
}

static int hns3_change_all_ring_bd_num(struct hns3_nic_priv *priv,
				       u32 tx_desc_num, u32 rx_desc_num)
{
	struct hnae3_handle *h = priv->ae_handle;
	int i;

	h->kinfo.num_tx_desc = tx_desc_num;
	h->kinfo.num_rx_desc = rx_desc_num;

	for (i = 0; i < h->kinfo.num_tqps; i++) {
		priv->ring_data[i].ring->desc_num = tx_desc_num;
		priv->ring_data[i + h->kinfo.num_tqps].ring->desc_num =
			rx_desc_num;
	}

	return hns3_init_all_ring(priv);
}

static int hns3_set_ringparam(struct net_device *ndev,
			      struct ethtool_ringparam *param)
{
	struct hns3_nic_priv *priv = netdev_priv(ndev);
	struct hnae3_handle *h = priv->ae_handle;
	bool if_running = netif_running(ndev);
	u32 old_tx_desc_num, new_tx_desc_num;
	u32 old_rx_desc_num, new_rx_desc_num;
	int queue_num = h->kinfo.num_tqps;
	int ret;

	if (hns3_nic_resetting(ndev))
		return -EBUSY;

	if (param->rx_mini_pending || param->rx_jumbo_pending)
		return -EINVAL;

	if (param->tx_pending > HNS3_RING_MAX_PENDING ||
	    param->tx_pending < HNS3_RING_MIN_PENDING ||
	    param->rx_pending > HNS3_RING_MAX_PENDING ||
	    param->rx_pending < HNS3_RING_MIN_PENDING) {
		netdev_err(ndev, "Queue depth out of range [%d-%d]\n",
			   HNS3_RING_MIN_PENDING, HNS3_RING_MAX_PENDING);
		return -EINVAL;
	}

	/* Hardware requires that its descriptors must be multiple of eight */
	new_tx_desc_num = ALIGN(param->tx_pending, HNS3_RING_BD_MULTIPLE);
	new_rx_desc_num = ALIGN(param->rx_pending, HNS3_RING_BD_MULTIPLE);
	old_tx_desc_num = priv->ring_data[0].ring->desc_num;
	old_rx_desc_num = priv->ring_data[queue_num].ring->desc_num;
	if (old_tx_desc_num == new_tx_desc_num &&
	    old_rx_desc_num == new_rx_desc_num)
		return 0;

	netdev_info(ndev,
		    "Changing Tx/Rx ring depth from %d/%d to %d/%d\n",
		    old_tx_desc_num, old_rx_desc_num,
		    new_tx_desc_num, new_rx_desc_num);

	if (if_running)
		ndev->netdev_ops->ndo_stop(ndev);

	ret = hns3_uninit_all_ring(priv);
	if (ret)
		return ret;

	ret = hns3_change_all_ring_bd_num(priv, new_tx_desc_num,
					  new_rx_desc_num);
	if (ret) {
		ret = hns3_change_all_ring_bd_num(priv, old_tx_desc_num,
						  old_rx_desc_num);
		if (ret) {
			netdev_err(ndev,
				   "Revert to old bd num fail, ret=%d.\n", ret);
			return ret;
		}
	}

	if (if_running)
		ret = ndev->netdev_ops->ndo_open(ndev);

	return ret;
}

static int hns3_set_rxnfc(struct net_device *netdev, struct ethtool_rxnfc *cmd)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);

	if (!h->ae_algo || !h->ae_algo->ops)
		return -EOPNOTSUPP;

	switch (cmd->cmd) {
	case ETHTOOL_SRXFH:
		if (h->ae_algo->ops->set_rss_tuple)
			return h->ae_algo->ops->set_rss_tuple(h, cmd);
		return -EOPNOTSUPP;
	case ETHTOOL_SRXCLSRLINS:
		if (h->ae_algo->ops->add_fd_entry)
			return h->ae_algo->ops->add_fd_entry(h, cmd);
		return -EOPNOTSUPP;
	case ETHTOOL_SRXCLSRLDEL:
		if (h->ae_algo->ops->del_fd_entry)
			return h->ae_algo->ops->del_fd_entry(h, cmd);
		return -EOPNOTSUPP;
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

static int hns3_get_coalesce_per_queue(struct net_device *netdev, u32 queue,
				       struct ethtool_coalesce *cmd)
{
	struct hns3_enet_tqp_vector *tx_vector, *rx_vector;
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	struct hnae3_handle *h = priv->ae_handle;
	u16 queue_num = h->kinfo.num_tqps;

	if (hns3_nic_resetting(netdev))
		return -EBUSY;

	if (queue >= queue_num) {
		netdev_err(netdev,
			   "Invalid queue value %d! Queue max id=%d\n",
			   queue, queue_num - 1);
		return -EINVAL;
	}

	tx_vector = priv->ring_data[queue].ring->tqp_vector;
	rx_vector = priv->ring_data[queue_num + queue].ring->tqp_vector;

	cmd->use_adaptive_tx_coalesce =
			tx_vector->tx_group.coal.gl_adapt_enable;
	cmd->use_adaptive_rx_coalesce =
			rx_vector->rx_group.coal.gl_adapt_enable;

	cmd->tx_coalesce_usecs = tx_vector->tx_group.coal.int_gl;
	cmd->rx_coalesce_usecs = rx_vector->rx_group.coal.int_gl;

	cmd->tx_coalesce_usecs_high = h->kinfo.int_rl_setting;
	cmd->rx_coalesce_usecs_high = h->kinfo.int_rl_setting;

	return 0;
}

static int hns3_get_coalesce(struct net_device *netdev,
			     struct ethtool_coalesce *cmd)
{
	return hns3_get_coalesce_per_queue(netdev, 0, cmd);
}

static int hns3_check_gl_coalesce_para(struct net_device *netdev,
				       struct ethtool_coalesce *cmd)
{
	u32 rx_gl, tx_gl;

	if (cmd->rx_coalesce_usecs > HNS3_INT_GL_MAX) {
		netdev_err(netdev,
			   "Invalid rx-usecs value, rx-usecs range is 0-%d\n",
			   HNS3_INT_GL_MAX);
		return -EINVAL;
	}

	if (cmd->tx_coalesce_usecs > HNS3_INT_GL_MAX) {
		netdev_err(netdev,
			   "Invalid tx-usecs value, tx-usecs range is 0-%d\n",
			   HNS3_INT_GL_MAX);
		return -EINVAL;
	}

	rx_gl = hns3_gl_round_down(cmd->rx_coalesce_usecs);
	if (rx_gl != cmd->rx_coalesce_usecs) {
		netdev_info(netdev,
			    "rx_usecs(%d) rounded down to %d, because it must be multiple of 2.\n",
			    cmd->rx_coalesce_usecs, rx_gl);
	}

	tx_gl = hns3_gl_round_down(cmd->tx_coalesce_usecs);
	if (tx_gl != cmd->tx_coalesce_usecs) {
		netdev_info(netdev,
			    "tx_usecs(%d) rounded down to %d, because it must be multiple of 2.\n",
			    cmd->tx_coalesce_usecs, tx_gl);
	}

	return 0;
}

static int hns3_check_rl_coalesce_para(struct net_device *netdev,
				       struct ethtool_coalesce *cmd)
{
	u32 rl;

	if (cmd->tx_coalesce_usecs_high != cmd->rx_coalesce_usecs_high) {
		netdev_err(netdev,
			   "tx_usecs_high must be same as rx_usecs_high.\n");
		return -EINVAL;
	}

	if (cmd->rx_coalesce_usecs_high > HNS3_INT_RL_MAX) {
		netdev_err(netdev,
			   "Invalid usecs_high value, usecs_high range is 0-%d\n",
			   HNS3_INT_RL_MAX);
		return -EINVAL;
	}

	rl = hns3_rl_round_down(cmd->rx_coalesce_usecs_high);
	if (rl != cmd->rx_coalesce_usecs_high) {
		netdev_info(netdev,
			    "usecs_high(%d) rounded down to %d, because it must be multiple of 4.\n",
			    cmd->rx_coalesce_usecs_high, rl);
	}

	return 0;
}

static int hns3_check_coalesce_para(struct net_device *netdev,
				    struct ethtool_coalesce *cmd)
{
	int ret;

	ret = hns3_check_gl_coalesce_para(netdev, cmd);
	if (ret) {
		netdev_err(netdev,
			   "Check gl coalesce param fail. ret = %d\n", ret);
		return ret;
	}

	ret = hns3_check_rl_coalesce_para(netdev, cmd);
	if (ret) {
		netdev_err(netdev,
			   "Check rl coalesce param fail. ret = %d\n", ret);
		return ret;
	}

	if (cmd->use_adaptive_tx_coalesce == 1 ||
	    cmd->use_adaptive_rx_coalesce == 1) {
		netdev_info(netdev,
			    "adaptive-tx=%d and adaptive-rx=%d, tx_usecs or rx_usecs will changed dynamically.\n",
			    cmd->use_adaptive_tx_coalesce,
			    cmd->use_adaptive_rx_coalesce);
	}

	return 0;
}

static void hns3_set_coalesce_per_queue(struct net_device *netdev,
					struct ethtool_coalesce *cmd,
					u32 queue)
{
	struct hns3_enet_tqp_vector *tx_vector, *rx_vector;
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	struct hnae3_handle *h = priv->ae_handle;
	int queue_num = h->kinfo.num_tqps;

	tx_vector = priv->ring_data[queue].ring->tqp_vector;
	rx_vector = priv->ring_data[queue_num + queue].ring->tqp_vector;

	tx_vector->tx_group.coal.gl_adapt_enable =
				cmd->use_adaptive_tx_coalesce;
	rx_vector->rx_group.coal.gl_adapt_enable =
				cmd->use_adaptive_rx_coalesce;

	tx_vector->tx_group.coal.int_gl = cmd->tx_coalesce_usecs;
	rx_vector->rx_group.coal.int_gl = cmd->rx_coalesce_usecs;

	hns3_set_vector_coalesce_tx_gl(tx_vector,
				       tx_vector->tx_group.coal.int_gl);
	hns3_set_vector_coalesce_rx_gl(rx_vector,
				       rx_vector->rx_group.coal.int_gl);

	hns3_set_vector_coalesce_rl(tx_vector, h->kinfo.int_rl_setting);
	hns3_set_vector_coalesce_rl(rx_vector, h->kinfo.int_rl_setting);
}

static int hns3_set_coalesce(struct net_device *netdev,
			     struct ethtool_coalesce *cmd)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);
	u16 queue_num = h->kinfo.num_tqps;
	int ret;
	int i;

	if (hns3_nic_resetting(netdev))
		return -EBUSY;

	ret = hns3_check_coalesce_para(netdev, cmd);
	if (ret)
		return ret;

	h->kinfo.int_rl_setting =
		hns3_rl_round_down(cmd->rx_coalesce_usecs_high);

	for (i = 0; i < queue_num; i++)
		hns3_set_coalesce_per_queue(netdev, cmd, i);

	return 0;
}

static int hns3_get_regs_len(struct net_device *netdev)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);

	if (!h->ae_algo->ops->get_regs_len)
		return -EOPNOTSUPP;

	return h->ae_algo->ops->get_regs_len(h);
}

static void hns3_get_regs(struct net_device *netdev,
			  struct ethtool_regs *cmd, void *data)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);

	if (!h->ae_algo->ops->get_regs)
		return;

	h->ae_algo->ops->get_regs(h, &cmd->version, data);
}

static int hns3_set_phys_id(struct net_device *netdev,
			    enum ethtool_phys_id_state state)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);

	if (!h->ae_algo || !h->ae_algo->ops || !h->ae_algo->ops->set_led_id)
		return -EOPNOTSUPP;

	return h->ae_algo->ops->set_led_id(h, state);
}

static const struct ethtool_ops hns3vf_ethtool_ops = {
	.get_drvinfo = hns3_get_drvinfo,
	.get_ringparam = hns3_get_ringparam,
	.set_ringparam = hns3_set_ringparam,
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
	.get_channels = hns3_get_channels,
	.get_coalesce = hns3_get_coalesce,
	.set_coalesce = hns3_set_coalesce,
	.get_regs_len = hns3_get_regs_len,
	.get_regs = hns3_get_regs,
	.get_link = hns3_get_link,
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
	.get_coalesce = hns3_get_coalesce,
	.set_coalesce = hns3_set_coalesce,
	.get_regs_len = hns3_get_regs_len,
	.get_regs = hns3_get_regs,
	.set_phys_id = hns3_set_phys_id,
};

void hns3_ethtool_set_ops(struct net_device *netdev)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);

	if (h->flags & HNAE3_SUPPORT_VF)
		netdev->ethtool_ops = &hns3vf_ethtool_ops;
	else
		netdev->ethtool_ops = &hns3_ethtool_ops;
}
