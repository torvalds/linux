// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) 2016-2017 Hisilicon Limited.

#include <linux/etherdevice.h>
#include <linux/string.h>
#include <linux/phy.h>
#include <linux/sfp.h>

#include "hns3_enet.h"

struct hns3_stats {
	char stats_string[ETH_GSTRING_LEN];
	int stats_offset;
};

struct hns3_sfp_type {
	u8 type;
	u8 ext_type;
};

struct hns3_pflag_desc {
	char name[ETH_GSTRING_LEN];
	void (*handler)(struct net_device *netdev, bool enable);
};

/* tqp related stats */
#define HNS3_TQP_STAT(_string, _member)	{			\
	.stats_string = _string,				\
	.stats_offset = offsetof(struct hns3_enet_ring, stats) +\
			offsetof(struct ring_stats, _member),   \
}

static const struct hns3_stats hns3_txq_stats[] = {
	/* Tx per-queue statistics */
	HNS3_TQP_STAT("dropped", sw_err_cnt),
	HNS3_TQP_STAT("seg_pkt_cnt", seg_pkt_cnt),
	HNS3_TQP_STAT("packets", tx_pkts),
	HNS3_TQP_STAT("bytes", tx_bytes),
	HNS3_TQP_STAT("more", tx_more),
	HNS3_TQP_STAT("wake", restart_queue),
	HNS3_TQP_STAT("busy", tx_busy),
	HNS3_TQP_STAT("copy", tx_copy),
	HNS3_TQP_STAT("vlan_err", tx_vlan_err),
	HNS3_TQP_STAT("l4_proto_err", tx_l4_proto_err),
	HNS3_TQP_STAT("l2l3l4_err", tx_l2l3l4_err),
	HNS3_TQP_STAT("tso_err", tx_tso_err),
	HNS3_TQP_STAT("over_max_recursion", over_max_recursion),
	HNS3_TQP_STAT("hw_limitation", hw_limitation),
};

#define HNS3_TXQ_STATS_COUNT ARRAY_SIZE(hns3_txq_stats)

static const struct hns3_stats hns3_rxq_stats[] = {
	/* Rx per-queue statistics */
	HNS3_TQP_STAT("dropped", sw_err_cnt),
	HNS3_TQP_STAT("seg_pkt_cnt", seg_pkt_cnt),
	HNS3_TQP_STAT("packets", rx_pkts),
	HNS3_TQP_STAT("bytes", rx_bytes),
	HNS3_TQP_STAT("errors", rx_err_cnt),
	HNS3_TQP_STAT("reuse_pg_cnt", reuse_pg_cnt),
	HNS3_TQP_STAT("err_pkt_len", err_pkt_len),
	HNS3_TQP_STAT("err_bd_num", err_bd_num),
	HNS3_TQP_STAT("l2_err", l2_err),
	HNS3_TQP_STAT("l3l4_csum_err", l3l4_csum_err),
	HNS3_TQP_STAT("csum_complete", csum_complete),
	HNS3_TQP_STAT("multicast", rx_multicast),
	HNS3_TQP_STAT("non_reuse_pg", non_reuse_pg),
};

#define HNS3_PRIV_FLAGS_LEN ARRAY_SIZE(hns3_priv_flags)

#define HNS3_RXQ_STATS_COUNT ARRAY_SIZE(hns3_rxq_stats)

#define HNS3_TQP_STATS_COUNT (HNS3_TXQ_STATS_COUNT + HNS3_RXQ_STATS_COUNT)

#define HNS3_SELF_TEST_TYPE_NUM         4
#define HNS3_NIC_LB_TEST_PKT_NUM	1
#define HNS3_NIC_LB_TEST_RING_ID	0
#define HNS3_NIC_LB_TEST_PACKET_SIZE	128
#define HNS3_NIC_LB_SETUP_USEC		10000

/* Nic loopback test err  */
#define HNS3_NIC_LB_TEST_NO_MEM_ERR	1
#define HNS3_NIC_LB_TEST_TX_CNT_ERR	2
#define HNS3_NIC_LB_TEST_RX_CNT_ERR	3

static int hns3_lp_setup(struct net_device *ndev, enum hnae3_loop loop, bool en)
{
	struct hnae3_handle *h = hns3_get_handle(ndev);
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(h->pdev);
	bool vlan_filter_enable;
	int ret;

	if (!h->ae_algo->ops->set_loopback ||
	    !h->ae_algo->ops->set_promisc_mode)
		return -EOPNOTSUPP;

	switch (loop) {
	case HNAE3_LOOP_SERIAL_SERDES:
	case HNAE3_LOOP_PARALLEL_SERDES:
	case HNAE3_LOOP_APP:
	case HNAE3_LOOP_PHY:
		ret = h->ae_algo->ops->set_loopback(h, loop, en);
		break;
	default:
		ret = -ENOTSUPP;
		break;
	}

	if (ret || ae_dev->dev_version >= HNAE3_DEVICE_VERSION_V2)
		return ret;

	if (en) {
		h->ae_algo->ops->set_promisc_mode(h, true, true);
	} else {
		/* recover promisc mode before loopback test */
		hns3_request_update_promisc_mode(h);
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
	usleep_range(HNS3_NIC_LB_SETUP_USEC, HNS3_NIC_LB_SETUP_USEC * 2);

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

	usleep_range(HNS3_NIC_LB_SETUP_USEC, HNS3_NIC_LB_SETUP_USEC * 2);

	return 0;
}

static void hns3_lp_setup_skb(struct sk_buff *skb)
{
#define	HNS3_NIC_LB_DST_MAC_ADDR	0x1f

	struct net_device *ndev = skb->dev;
	struct hnae3_handle *handle;
	struct hnae3_ae_dev *ae_dev;
	unsigned char *packet;
	struct ethhdr *ethh;
	unsigned int i;

	skb_reserve(skb, NET_IP_ALIGN);
	ethh = skb_put(skb, sizeof(struct ethhdr));
	packet = skb_put(skb, HNS3_NIC_LB_TEST_PACKET_SIZE);

	memcpy(ethh->h_dest, ndev->dev_addr, ETH_ALEN);

	/* The dst mac addr of loopback packet is the same as the host'
	 * mac addr, the SSU component may loop back the packet to host
	 * before the packet reaches mac or serdes, which will defect
	 * the purpose of mac or serdes selftest.
	 */
	handle = hns3_get_handle(ndev);
	ae_dev = pci_get_drvdata(handle->pdev);
	if (ae_dev->dev_version < HNAE3_DEVICE_VERSION_V2)
		ethh->h_dest[5] += HNS3_NIC_LB_DST_MAC_ADDR;
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
	u32 len = skb_headlen(skb);
	u32 i;

	len = min_t(u32, len, HNS3_NIC_LB_TEST_PACKET_SIZE);

	for (i = 0; i < len; i++)
		if (packet[i] != (unsigned char)(i & 0xff))
			break;

	/* The packet is correctly received */
	if (i == HNS3_NIC_LB_TEST_PACKET_SIZE)
		tqp_vector->rx_group.total_packets++;
	else
		print_hex_dump(KERN_ERR, "selftest:", DUMP_PREFIX_OFFSET, 16, 1,
			       skb->data, len, true);

	dev_kfree_skb_any(skb);
}

static u32 hns3_lb_check_rx_ring(struct hns3_nic_priv *priv, u32 budget)
{
	struct hnae3_handle *h = priv->ae_handle;
	struct hnae3_knic_private_info *kinfo;
	u32 i, rcv_good_pkt_total = 0;

	kinfo = &h->kinfo;
	for (i = kinfo->num_tqps; i < kinfo->num_tqps * 2; i++) {
		struct hns3_enet_ring *ring = &priv->ring[i];
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
		struct hns3_enet_ring *ring = &priv->ring[i];

		hns3_clean_tx_ring(ring, 0);
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
		if (tx_ret == NETDEV_TX_OK) {
			good_cnt++;
		} else {
			kfree_skb(skb);
			netdev_err(ndev, "hns3_lb_run_test xmit failed: %d\n",
				   tx_ret);
		}
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
 * hns3_self_test - self test
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
	int test_index = 0;
	u32 i;

	if (hns3_nic_resetting(ndev)) {
		netdev_err(ndev, "dev resetting!");
		return;
	}

	/* Only do offline selftest, or pass by default */
	if (eth_test->flags != ETH_TEST_FL_OFFLINE)
		return;

	netif_dbg(h, drv, ndev, "self test start");

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

	st_param[HNAE3_LOOP_PHY][0] = HNAE3_LOOP_PHY;
	st_param[HNAE3_LOOP_PHY][1] =
			h->flags & HNAE3_SUPPORT_PHY_LOOPBACK;

	if (if_running)
		ndev->netdev_ops->ndo_stop(ndev);

#if IS_ENABLED(CONFIG_VLAN_8021Q)
	/* Disable the vlan filter for selftest does not support it */
	if (h->ae_algo->ops->enable_vlan_filter)
		h->ae_algo->ops->enable_vlan_filter(h, false);
#endif

	/* Tell firmware to stop mac autoneg before loopback test start,
	 * otherwise loopback test may be failed when the port is still
	 * negotiating.
	 */
	if (h->ae_algo->ops->halt_autoneg)
		h->ae_algo->ops->halt_autoneg(h, true);

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

	if (h->ae_algo->ops->halt_autoneg)
		h->ae_algo->ops->halt_autoneg(h, false);

#if IS_ENABLED(CONFIG_VLAN_8021Q)
	if (h->ae_algo->ops->enable_vlan_filter)
		h->ae_algo->ops->enable_vlan_filter(h, true);
#endif

	if (if_running)
		ndev->netdev_ops->ndo_open(ndev);

	netif_dbg(h, drv, ndev, "self test end\n");
}

static void hns3_update_limit_promisc_mode(struct net_device *netdev,
					   bool enable)
{
	struct hnae3_handle *handle = hns3_get_handle(netdev);

	if (enable)
		set_bit(HNAE3_PFLAG_LIMIT_PROMISC, &handle->priv_flags);
	else
		clear_bit(HNAE3_PFLAG_LIMIT_PROMISC, &handle->priv_flags);

	hns3_request_update_promisc_mode(handle);
}

static const struct hns3_pflag_desc hns3_priv_flags[HNAE3_PFLAG_MAX] = {
	{ "limit_promisc",	hns3_update_limit_promisc_mode }
};

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

	case ETH_SS_PRIV_FLAGS:
		return HNAE3_PFLAG_MAX;

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
			n1 = scnprintf(data, MAX_PREFIX_SIZE, "%s%u_",
				       prefix, i);
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
	int i;

	if (!ops->get_strings)
		return;

	switch (stringset) {
	case ETH_SS_STATS:
		buff = hns3_get_strings_tqps(h, buff);
		ops->get_strings(h, stringset, (u8 *)buff);
		break;
	case ETH_SS_TEST:
		ops->get_strings(h, stringset, data);
		break;
	case ETH_SS_PRIV_FLAGS:
		for (i = 0; i < HNS3_PRIV_FLAGS_LEN; i++) {
			snprintf(buff, ETH_GSTRING_LEN, "%s",
				 hns3_priv_flags[i].name);
			buff += ETH_GSTRING_LEN;
		}
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
		ring = &nic_priv->ring[i];
		for (j = 0; j < HNS3_TXQ_STATS_COUNT; j++) {
			stat = (u8 *)ring + hns3_txq_stats[j].stats_offset;
			*data++ = *(u64 *)stat;
		}
	}

	/* get stats for Rx */
	for (i = 0; i < kinfo->num_tqps; i++) {
		ring = &nic_priv->ring[i + kinfo->num_tqps];
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

	if (hns3_nic_resetting(netdev)) {
		netdev_err(netdev, "dev resetting, could not get stats\n");
		return;
	}

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
	u32 fw_version;

	if (!h->ae_algo->ops->get_fw_version) {
		netdev_err(netdev, "could not get fw version!\n");
		return;
	}

	strncpy(drvinfo->driver, h->pdev->driver->name,
		sizeof(drvinfo->driver));
	drvinfo->driver[sizeof(drvinfo->driver) - 1] = '\0';

	strncpy(drvinfo->bus_info, pci_name(h->pdev),
		sizeof(drvinfo->bus_info));
	drvinfo->bus_info[ETHTOOL_BUSINFO_LEN - 1] = '\0';

	fw_version = priv->ae_handle->ae_algo->ops->get_fw_version(h);

	snprintf(drvinfo->fw_version, sizeof(drvinfo->fw_version),
		 "%lu.%lu.%lu.%lu",
		 hnae3_get_field(fw_version, HNAE3_FW_VERSION_BYTE3_MASK,
				 HNAE3_FW_VERSION_BYTE3_SHIFT),
		 hnae3_get_field(fw_version, HNAE3_FW_VERSION_BYTE2_MASK,
				 HNAE3_FW_VERSION_BYTE2_SHIFT),
		 hnae3_get_field(fw_version, HNAE3_FW_VERSION_BYTE1_MASK,
				 HNAE3_FW_VERSION_BYTE1_SHIFT),
		 hnae3_get_field(fw_version, HNAE3_FW_VERSION_BYTE0_MASK,
				 HNAE3_FW_VERSION_BYTE0_SHIFT));
}

static u32 hns3_get_link(struct net_device *netdev)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);

	if (h->ae_algo->ops->get_status)
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

	param->tx_pending = priv->ring[0].desc_num;
	param->rx_pending = priv->ring[queue_num].desc_num;
}

static void hns3_get_pauseparam(struct net_device *netdev,
				struct ethtool_pauseparam *param)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(h->pdev);

	if (!test_bit(HNAE3_DEV_SUPPORT_PAUSE_B, ae_dev->caps))
		return;

	if (h->ae_algo->ops->get_pauseparam)
		h->ae_algo->ops->get_pauseparam(h, &param->autoneg,
			&param->rx_pause, &param->tx_pause);
}

static int hns3_set_pauseparam(struct net_device *netdev,
			       struct ethtool_pauseparam *param)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(h->pdev);

	if (!test_bit(HNAE3_DEV_SUPPORT_PAUSE_B, ae_dev->caps))
		return -EOPNOTSUPP;

	netif_dbg(h, drv, netdev,
		  "set pauseparam: autoneg=%u, rx:%u, tx:%u\n",
		  param->autoneg, param->rx_pause, param->tx_pause);

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

	/* 2.get link mode */
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
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(h->pdev);
	const struct hnae3_ae_ops *ops;
	u8 module_type;
	u8 media_type;
	u8 link_stat;

	ops = h->ae_algo->ops;
	if (ops->get_media_type)
		ops->get_media_type(h, &media_type, &module_type);
	else
		return -EOPNOTSUPP;

	switch (media_type) {
	case HNAE3_MEDIA_TYPE_NONE:
		cmd->base.port = PORT_NONE;
		hns3_get_ksettings(h, cmd);
		break;
	case HNAE3_MEDIA_TYPE_FIBER:
		if (module_type == HNAE3_MODULE_TYPE_CR)
			cmd->base.port = PORT_DA;
		else
			cmd->base.port = PORT_FIBRE;

		hns3_get_ksettings(h, cmd);
		break;
	case HNAE3_MEDIA_TYPE_BACKPLANE:
		cmd->base.port = PORT_NONE;
		hns3_get_ksettings(h, cmd);
		break;
	case HNAE3_MEDIA_TYPE_COPPER:
		cmd->base.port = PORT_TP;
		if (test_bit(HNAE3_DEV_SUPPORT_PHY_IMP_B, ae_dev->caps) &&
		    ops->get_phy_link_ksettings)
			ops->get_phy_link_ksettings(h, cmd);
		else if (!netdev->phydev)
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

static int hns3_check_ksettings_param(const struct net_device *netdev,
				      const struct ethtool_link_ksettings *cmd)
{
	struct hnae3_handle *handle = hns3_get_handle(netdev);
	const struct hnae3_ae_ops *ops = handle->ae_algo->ops;
	u8 module_type = HNAE3_MODULE_TYPE_UNKNOWN;
	u8 media_type = HNAE3_MEDIA_TYPE_UNKNOWN;
	u8 autoneg;
	u32 speed;
	u8 duplex;
	int ret;

	/* hw doesn't support use specified speed and duplex to negotiate,
	 * unnecessary to check them when autoneg on.
	 */
	if (cmd->base.autoneg)
		return 0;

	if (ops->get_ksettings_an_result) {
		ops->get_ksettings_an_result(handle, &autoneg, &speed, &duplex);
		if (cmd->base.autoneg == autoneg && cmd->base.speed == speed &&
		    cmd->base.duplex == duplex)
			return 0;
	}

	if (ops->get_media_type)
		ops->get_media_type(handle, &media_type, &module_type);

	if (cmd->base.duplex == DUPLEX_HALF &&
	    media_type != HNAE3_MEDIA_TYPE_COPPER) {
		netdev_err(netdev,
			   "only copper port supports half duplex!");
		return -EINVAL;
	}

	if (ops->check_port_speed) {
		ret = ops->check_port_speed(handle, cmd->base.speed);
		if (ret) {
			netdev_err(netdev, "unsupported speed\n");
			return ret;
		}
	}

	return 0;
}

static int hns3_set_link_ksettings(struct net_device *netdev,
				   const struct ethtool_link_ksettings *cmd)
{
	struct hnae3_handle *handle = hns3_get_handle(netdev);
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(handle->pdev);
	const struct hnae3_ae_ops *ops = handle->ae_algo->ops;
	int ret;

	/* Chip don't support this mode. */
	if (cmd->base.speed == SPEED_1000 && cmd->base.duplex == DUPLEX_HALF)
		return -EINVAL;

	netif_dbg(handle, drv, netdev,
		  "set link(%s): autoneg=%u, speed=%u, duplex=%u\n",
		  netdev->phydev ? "phy" : "mac",
		  cmd->base.autoneg, cmd->base.speed, cmd->base.duplex);

	/* Only support ksettings_set for netdev with phy attached for now */
	if (netdev->phydev) {
		if (cmd->base.speed == SPEED_1000 &&
		    cmd->base.autoneg == AUTONEG_DISABLE)
			return -EINVAL;

		return phy_ethtool_ksettings_set(netdev->phydev, cmd);
	} else if (test_bit(HNAE3_DEV_SUPPORT_PHY_IMP_B, ae_dev->caps) &&
		   ops->set_phy_link_ksettings) {
		return ops->set_phy_link_ksettings(handle, cmd);
	}

	if (ae_dev->dev_version < HNAE3_DEVICE_VERSION_V2)
		return -EOPNOTSUPP;

	ret = hns3_check_ksettings_param(netdev, cmd);
	if (ret)
		return ret;

	if (ops->set_autoneg) {
		ret = ops->set_autoneg(handle, cmd->base.autoneg);
		if (ret)
			return ret;
	}

	/* hw doesn't support use specified speed and duplex to negotiate,
	 * ignore them when autoneg on.
	 */
	if (cmd->base.autoneg) {
		netdev_info(netdev,
			    "autoneg is on, ignore the speed and duplex\n");
		return 0;
	}

	if (ops->cfg_mac_speed_dup_h)
		ret = ops->cfg_mac_speed_dup_h(handle, cmd->base.speed,
					       cmd->base.duplex);

	return ret;
}

static u32 hns3_get_rss_key_size(struct net_device *netdev)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);

	if (!h->ae_algo->ops->get_rss_key_size)
		return 0;

	return h->ae_algo->ops->get_rss_key_size(h);
}

static u32 hns3_get_rss_indir_size(struct net_device *netdev)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(h->pdev);

	return ae_dev->dev_specs.rss_ind_tbl_size;
}

static int hns3_get_rss(struct net_device *netdev, u32 *indir, u8 *key,
			u8 *hfunc)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);

	if (!h->ae_algo->ops->get_rss)
		return -EOPNOTSUPP;

	return h->ae_algo->ops->get_rss(h, indir, key, hfunc);
}

static int hns3_set_rss(struct net_device *netdev, const u32 *indir,
			const u8 *key, const u8 hfunc)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(h->pdev);

	if (!h->ae_algo->ops->set_rss)
		return -EOPNOTSUPP;

	if ((ae_dev->dev_version < HNAE3_DEVICE_VERSION_V2 &&
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

static void hns3_change_all_ring_bd_num(struct hns3_nic_priv *priv,
					u32 tx_desc_num, u32 rx_desc_num)
{
	struct hnae3_handle *h = priv->ae_handle;
	int i;

	h->kinfo.num_tx_desc = tx_desc_num;
	h->kinfo.num_rx_desc = rx_desc_num;

	for (i = 0; i < h->kinfo.num_tqps; i++) {
		priv->ring[i].desc_num = tx_desc_num;
		priv->ring[i + h->kinfo.num_tqps].desc_num = rx_desc_num;
	}
}

static struct hns3_enet_ring *hns3_backup_ringparam(struct hns3_nic_priv *priv)
{
	struct hnae3_handle *handle = priv->ae_handle;
	struct hns3_enet_ring *tmp_rings;
	int i;

	tmp_rings = kcalloc(handle->kinfo.num_tqps * 2,
			    sizeof(struct hns3_enet_ring), GFP_KERNEL);
	if (!tmp_rings)
		return NULL;

	for (i = 0; i < handle->kinfo.num_tqps * 2; i++) {
		memcpy(&tmp_rings[i], &priv->ring[i],
		       sizeof(struct hns3_enet_ring));
		tmp_rings[i].skb = NULL;
	}

	return tmp_rings;
}

static int hns3_check_ringparam(struct net_device *ndev,
				struct ethtool_ringparam *param)
{
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

	return 0;
}

static int hns3_set_ringparam(struct net_device *ndev,
			      struct ethtool_ringparam *param)
{
	struct hns3_nic_priv *priv = netdev_priv(ndev);
	struct hnae3_handle *h = priv->ae_handle;
	struct hns3_enet_ring *tmp_rings;
	bool if_running = netif_running(ndev);
	u32 old_tx_desc_num, new_tx_desc_num;
	u32 old_rx_desc_num, new_rx_desc_num;
	u16 queue_num = h->kinfo.num_tqps;
	int ret, i;

	ret = hns3_check_ringparam(ndev, param);
	if (ret)
		return ret;

	/* Hardware requires that its descriptors must be multiple of eight */
	new_tx_desc_num = ALIGN(param->tx_pending, HNS3_RING_BD_MULTIPLE);
	new_rx_desc_num = ALIGN(param->rx_pending, HNS3_RING_BD_MULTIPLE);
	old_tx_desc_num = priv->ring[0].desc_num;
	old_rx_desc_num = priv->ring[queue_num].desc_num;
	if (old_tx_desc_num == new_tx_desc_num &&
	    old_rx_desc_num == new_rx_desc_num)
		return 0;

	tmp_rings = hns3_backup_ringparam(priv);
	if (!tmp_rings) {
		netdev_err(ndev,
			   "backup ring param failed by allocating memory fail\n");
		return -ENOMEM;
	}

	netdev_info(ndev,
		    "Changing Tx/Rx ring depth from %u/%u to %u/%u\n",
		    old_tx_desc_num, old_rx_desc_num,
		    new_tx_desc_num, new_rx_desc_num);

	if (if_running)
		ndev->netdev_ops->ndo_stop(ndev);

	hns3_change_all_ring_bd_num(priv, new_tx_desc_num, new_rx_desc_num);
	ret = hns3_init_all_ring(priv);
	if (ret) {
		netdev_err(ndev, "Change bd num fail, revert to old value(%d)\n",
			   ret);

		hns3_change_all_ring_bd_num(priv, old_tx_desc_num,
					    old_rx_desc_num);
		for (i = 0; i < h->kinfo.num_tqps * 2; i++)
			memcpy(&priv->ring[i], &tmp_rings[i],
			       sizeof(struct hns3_enet_ring));
	} else {
		for (i = 0; i < h->kinfo.num_tqps * 2; i++)
			hns3_fini_ring(&tmp_rings[i]);
	}

	kfree(tmp_rings);

	if (if_running)
		ret = ndev->netdev_ops->ndo_open(ndev);

	return ret;
}

static int hns3_set_rxnfc(struct net_device *netdev, struct ethtool_rxnfc *cmd)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);

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
	struct hnae3_handle *handle = hns3_get_handle(netdev);
	const struct hnae3_ae_ops *ops = handle->ae_algo->ops;
	struct phy_device *phy = netdev->phydev;
	int autoneg;

	if (!netif_running(netdev))
		return 0;

	if (hns3_nic_resetting(netdev)) {
		netdev_err(netdev, "dev resetting!");
		return -EBUSY;
	}

	if (!ops->get_autoneg || !ops->restart_autoneg)
		return -EOPNOTSUPP;

	autoneg = ops->get_autoneg(handle);
	if (autoneg != AUTONEG_ENABLE) {
		netdev_err(netdev,
			   "Autoneg is off, don't support to restart it\n");
		return -EINVAL;
	}

	netif_dbg(handle, drv, netdev,
		  "nway reset (using %s)\n", phy ? "phy" : "mac");

	if (phy)
		return genphy_restart_aneg(phy);

	return ops->restart_autoneg(handle);
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
			   "Invalid queue value %u! Queue max id=%u\n",
			   queue, queue_num - 1);
		return -EINVAL;
	}

	tx_vector = priv->ring[queue].tqp_vector;
	rx_vector = priv->ring[queue_num + queue].tqp_vector;

	cmd->use_adaptive_tx_coalesce =
			tx_vector->tx_group.coal.adapt_enable;
	cmd->use_adaptive_rx_coalesce =
			rx_vector->rx_group.coal.adapt_enable;

	cmd->tx_coalesce_usecs = tx_vector->tx_group.coal.int_gl;
	cmd->rx_coalesce_usecs = rx_vector->rx_group.coal.int_gl;

	cmd->tx_coalesce_usecs_high = h->kinfo.int_rl_setting;
	cmd->rx_coalesce_usecs_high = h->kinfo.int_rl_setting;

	cmd->tx_max_coalesced_frames = tx_vector->tx_group.coal.int_ql;
	cmd->rx_max_coalesced_frames = rx_vector->rx_group.coal.int_ql;

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
	struct hnae3_handle *handle = hns3_get_handle(netdev);
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(handle->pdev);
	u32 rx_gl, tx_gl;

	if (cmd->rx_coalesce_usecs > ae_dev->dev_specs.max_int_gl) {
		netdev_err(netdev,
			   "invalid rx-usecs value, rx-usecs range is 0-%u\n",
			   ae_dev->dev_specs.max_int_gl);
		return -EINVAL;
	}

	if (cmd->tx_coalesce_usecs > ae_dev->dev_specs.max_int_gl) {
		netdev_err(netdev,
			   "invalid tx-usecs value, tx-usecs range is 0-%u\n",
			   ae_dev->dev_specs.max_int_gl);
		return -EINVAL;
	}

	/* device version above V3(include V3), GL uses 1us unit,
	 * so the round down is not needed.
	 */
	if (ae_dev->dev_version >= HNAE3_DEVICE_VERSION_V3)
		return 0;

	rx_gl = hns3_gl_round_down(cmd->rx_coalesce_usecs);
	if (rx_gl != cmd->rx_coalesce_usecs) {
		netdev_info(netdev,
			    "rx_usecs(%u) rounded down to %u, because it must be multiple of 2.\n",
			    cmd->rx_coalesce_usecs, rx_gl);
	}

	tx_gl = hns3_gl_round_down(cmd->tx_coalesce_usecs);
	if (tx_gl != cmd->tx_coalesce_usecs) {
		netdev_info(netdev,
			    "tx_usecs(%u) rounded down to %u, because it must be multiple of 2.\n",
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
			    "usecs_high(%u) rounded down to %u, because it must be multiple of 4.\n",
			    cmd->rx_coalesce_usecs_high, rl);
	}

	return 0;
}

static int hns3_check_ql_coalesce_param(struct net_device *netdev,
					struct ethtool_coalesce *cmd)
{
	struct hnae3_handle *handle = hns3_get_handle(netdev);
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(handle->pdev);

	if ((cmd->tx_max_coalesced_frames || cmd->rx_max_coalesced_frames) &&
	    !ae_dev->dev_specs.int_ql_max) {
		netdev_err(netdev, "coalesced frames is not supported\n");
		return -EOPNOTSUPP;
	}

	if (cmd->tx_max_coalesced_frames > ae_dev->dev_specs.int_ql_max ||
	    cmd->rx_max_coalesced_frames > ae_dev->dev_specs.int_ql_max) {
		netdev_err(netdev,
			   "invalid coalesced_frames value, range is 0-%u\n",
			   ae_dev->dev_specs.int_ql_max);
		return -ERANGE;
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

	ret = hns3_check_ql_coalesce_param(netdev, cmd);
	if (ret)
		return ret;

	if (cmd->use_adaptive_tx_coalesce == 1 ||
	    cmd->use_adaptive_rx_coalesce == 1) {
		netdev_info(netdev,
			    "adaptive-tx=%u and adaptive-rx=%u, tx_usecs or rx_usecs will changed dynamically.\n",
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

	tx_vector = priv->ring[queue].tqp_vector;
	rx_vector = priv->ring[queue_num + queue].tqp_vector;

	tx_vector->tx_group.coal.adapt_enable =
				cmd->use_adaptive_tx_coalesce;
	rx_vector->rx_group.coal.adapt_enable =
				cmd->use_adaptive_rx_coalesce;

	tx_vector->tx_group.coal.int_gl = cmd->tx_coalesce_usecs;
	rx_vector->rx_group.coal.int_gl = cmd->rx_coalesce_usecs;

	tx_vector->tx_group.coal.int_ql = cmd->tx_max_coalesced_frames;
	rx_vector->rx_group.coal.int_ql = cmd->rx_max_coalesced_frames;

	hns3_set_vector_coalesce_tx_gl(tx_vector,
				       tx_vector->tx_group.coal.int_gl);
	hns3_set_vector_coalesce_rx_gl(rx_vector,
				       rx_vector->rx_group.coal.int_gl);

	hns3_set_vector_coalesce_rl(tx_vector, h->kinfo.int_rl_setting);
	hns3_set_vector_coalesce_rl(rx_vector, h->kinfo.int_rl_setting);

	if (tx_vector->tx_group.coal.ql_enable)
		hns3_set_vector_coalesce_tx_ql(tx_vector,
					       tx_vector->tx_group.coal.int_ql);
	if (rx_vector->rx_group.coal.ql_enable)
		hns3_set_vector_coalesce_rx_ql(rx_vector,
					       rx_vector->rx_group.coal.int_ql);
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

	if (!h->ae_algo->ops->set_led_id)
		return -EOPNOTSUPP;

	return h->ae_algo->ops->set_led_id(h, state);
}

static u32 hns3_get_msglevel(struct net_device *netdev)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);

	return h->msg_enable;
}

static void hns3_set_msglevel(struct net_device *netdev, u32 msg_level)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);

	h->msg_enable = msg_level;
}

/* Translate local fec value into ethtool value. */
static unsigned int loc_to_eth_fec(u8 loc_fec)
{
	u32 eth_fec = 0;

	if (loc_fec & BIT(HNAE3_FEC_AUTO))
		eth_fec |= ETHTOOL_FEC_AUTO;
	if (loc_fec & BIT(HNAE3_FEC_RS))
		eth_fec |= ETHTOOL_FEC_RS;
	if (loc_fec & BIT(HNAE3_FEC_BASER))
		eth_fec |= ETHTOOL_FEC_BASER;

	/* if nothing is set, then FEC is off */
	if (!eth_fec)
		eth_fec = ETHTOOL_FEC_OFF;

	return eth_fec;
}

/* Translate ethtool fec value into local value. */
static unsigned int eth_to_loc_fec(unsigned int eth_fec)
{
	u32 loc_fec = 0;

	if (eth_fec & ETHTOOL_FEC_OFF)
		return loc_fec;

	if (eth_fec & ETHTOOL_FEC_AUTO)
		loc_fec |= BIT(HNAE3_FEC_AUTO);
	if (eth_fec & ETHTOOL_FEC_RS)
		loc_fec |= BIT(HNAE3_FEC_RS);
	if (eth_fec & ETHTOOL_FEC_BASER)
		loc_fec |= BIT(HNAE3_FEC_BASER);

	return loc_fec;
}

static int hns3_get_fecparam(struct net_device *netdev,
			     struct ethtool_fecparam *fec)
{
	struct hnae3_handle *handle = hns3_get_handle(netdev);
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(handle->pdev);
	const struct hnae3_ae_ops *ops = handle->ae_algo->ops;
	u8 fec_ability;
	u8 fec_mode;

	if (!test_bit(HNAE3_DEV_SUPPORT_FEC_B, ae_dev->caps))
		return -EOPNOTSUPP;

	if (!ops->get_fec)
		return -EOPNOTSUPP;

	ops->get_fec(handle, &fec_ability, &fec_mode);

	fec->fec = loc_to_eth_fec(fec_ability);
	fec->active_fec = loc_to_eth_fec(fec_mode);

	return 0;
}

static int hns3_set_fecparam(struct net_device *netdev,
			     struct ethtool_fecparam *fec)
{
	struct hnae3_handle *handle = hns3_get_handle(netdev);
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(handle->pdev);
	const struct hnae3_ae_ops *ops = handle->ae_algo->ops;
	u32 fec_mode;

	if (!test_bit(HNAE3_DEV_SUPPORT_FEC_B, ae_dev->caps))
		return -EOPNOTSUPP;

	if (!ops->set_fec)
		return -EOPNOTSUPP;
	fec_mode = eth_to_loc_fec(fec->fec);

	netif_dbg(handle, drv, netdev, "set fecparam: mode=%u\n", fec_mode);

	return ops->set_fec(handle, fec_mode);
}

static int hns3_get_module_info(struct net_device *netdev,
				struct ethtool_modinfo *modinfo)
{
#define HNS3_SFF_8636_V1_3 0x03

	struct hnae3_handle *handle = hns3_get_handle(netdev);
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(handle->pdev);
	const struct hnae3_ae_ops *ops = handle->ae_algo->ops;
	struct hns3_sfp_type sfp_type;
	int ret;

	if (ae_dev->dev_version < HNAE3_DEVICE_VERSION_V2 ||
	    !ops->get_module_eeprom)
		return -EOPNOTSUPP;

	memset(&sfp_type, 0, sizeof(sfp_type));
	ret = ops->get_module_eeprom(handle, 0, sizeof(sfp_type) / sizeof(u8),
				     (u8 *)&sfp_type);
	if (ret)
		return ret;

	switch (sfp_type.type) {
	case SFF8024_ID_SFP:
		modinfo->type = ETH_MODULE_SFF_8472;
		modinfo->eeprom_len = ETH_MODULE_SFF_8472_LEN;
		break;
	case SFF8024_ID_QSFP_8438:
		modinfo->type = ETH_MODULE_SFF_8436;
		modinfo->eeprom_len = ETH_MODULE_SFF_8436_MAX_LEN;
		break;
	case SFF8024_ID_QSFP_8436_8636:
		if (sfp_type.ext_type < HNS3_SFF_8636_V1_3) {
			modinfo->type = ETH_MODULE_SFF_8436;
			modinfo->eeprom_len = ETH_MODULE_SFF_8436_MAX_LEN;
		} else {
			modinfo->type = ETH_MODULE_SFF_8636;
			modinfo->eeprom_len = ETH_MODULE_SFF_8636_MAX_LEN;
		}
		break;
	case SFF8024_ID_QSFP28_8636:
		modinfo->type = ETH_MODULE_SFF_8636;
		modinfo->eeprom_len = ETH_MODULE_SFF_8636_MAX_LEN;
		break;
	default:
		netdev_err(netdev, "Optical module unknown: %#x\n",
			   sfp_type.type);
		return -EINVAL;
	}

	return 0;
}

static int hns3_get_module_eeprom(struct net_device *netdev,
				  struct ethtool_eeprom *ee, u8 *data)
{
	struct hnae3_handle *handle = hns3_get_handle(netdev);
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(handle->pdev);
	const struct hnae3_ae_ops *ops = handle->ae_algo->ops;

	if (ae_dev->dev_version < HNAE3_DEVICE_VERSION_V2 ||
	    !ops->get_module_eeprom)
		return -EOPNOTSUPP;

	if (!ee->len)
		return -EINVAL;

	memset(data, 0, ee->len);

	return ops->get_module_eeprom(handle, ee->offset, ee->len, data);
}

static u32 hns3_get_priv_flags(struct net_device *netdev)
{
	struct hnae3_handle *handle = hns3_get_handle(netdev);

	return handle->priv_flags;
}

static int hns3_check_priv_flags(struct hnae3_handle *h, u32 changed)
{
	u32 i;

	for (i = 0; i < HNAE3_PFLAG_MAX; i++)
		if ((changed & BIT(i)) && !test_bit(i, &h->supported_pflags)) {
			netdev_err(h->netdev, "%s is unsupported\n",
				   hns3_priv_flags[i].name);
			return -EOPNOTSUPP;
		}

	return 0;
}

static int hns3_set_priv_flags(struct net_device *netdev, u32 pflags)
{
	struct hnae3_handle *handle = hns3_get_handle(netdev);
	u32 changed = pflags ^ handle->priv_flags;
	int ret;
	u32 i;

	ret = hns3_check_priv_flags(handle, changed);
	if (ret)
		return ret;

	for (i = 0; i < HNAE3_PFLAG_MAX; i++) {
		if (changed & BIT(i)) {
			bool enable = !(handle->priv_flags & BIT(i));

			if (enable)
				handle->priv_flags |= BIT(i);
			else
				handle->priv_flags &= ~BIT(i);
			hns3_priv_flags[i].handler(netdev, enable);
		}
	}

	return 0;
}

#define HNS3_ETHTOOL_COALESCE	(ETHTOOL_COALESCE_USECS |		\
				 ETHTOOL_COALESCE_USE_ADAPTIVE |	\
				 ETHTOOL_COALESCE_RX_USECS_HIGH |	\
				 ETHTOOL_COALESCE_TX_USECS_HIGH |	\
				 ETHTOOL_COALESCE_MAX_FRAMES)

static const struct ethtool_ops hns3vf_ethtool_ops = {
	.supported_coalesce_params = HNS3_ETHTOOL_COALESCE,
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
	.set_channels = hns3_set_channels,
	.get_coalesce = hns3_get_coalesce,
	.set_coalesce = hns3_set_coalesce,
	.get_regs_len = hns3_get_regs_len,
	.get_regs = hns3_get_regs,
	.get_link = hns3_get_link,
	.get_msglevel = hns3_get_msglevel,
	.set_msglevel = hns3_set_msglevel,
	.get_priv_flags = hns3_get_priv_flags,
	.set_priv_flags = hns3_set_priv_flags,
};

static const struct ethtool_ops hns3_ethtool_ops = {
	.supported_coalesce_params = HNS3_ETHTOOL_COALESCE,
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
	.get_msglevel = hns3_get_msglevel,
	.set_msglevel = hns3_set_msglevel,
	.get_fecparam = hns3_get_fecparam,
	.set_fecparam = hns3_set_fecparam,
	.get_module_info = hns3_get_module_info,
	.get_module_eeprom = hns3_get_module_eeprom,
	.get_priv_flags = hns3_get_priv_flags,
	.set_priv_flags = hns3_set_priv_flags,
};

void hns3_ethtool_set_ops(struct net_device *netdev)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);

	if (h->flags & HNAE3_SUPPORT_VF)
		netdev->ethtool_ops = &hns3vf_ethtool_ops;
	else
		netdev->ethtool_ops = &hns3_ethtool_ops;
}
