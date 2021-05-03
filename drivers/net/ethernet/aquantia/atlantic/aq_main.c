// SPDX-License-Identifier: GPL-2.0-only
/* Atlantic Network Driver
 *
 * Copyright (C) 2014-2019 aQuantia Corporation
 * Copyright (C) 2019-2020 Marvell International Ltd.
 */

/* File aq_main.c: Main file for aQuantia Linux driver. */

#include "aq_main.h"
#include "aq_nic.h"
#include "aq_pci_func.h"
#include "aq_ethtool.h"
#include "aq_ptp.h"
#include "aq_filters.h"
#include "aq_hw_utils.h"

#include <linux/netdevice.h>
#include <linux/module.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <net/pkt_cls.h>

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR(AQ_CFG_DRV_AUTHOR);
MODULE_DESCRIPTION(AQ_CFG_DRV_DESC);

static const char aq_ndev_driver_name[] = AQ_CFG_DRV_NAME;

static const struct net_device_ops aq_ndev_ops;

static struct workqueue_struct *aq_ndev_wq;

void aq_ndev_schedule_work(struct work_struct *work)
{
	queue_work(aq_ndev_wq, work);
}

struct net_device *aq_ndev_alloc(void)
{
	struct net_device *ndev = NULL;
	struct aq_nic_s *aq_nic = NULL;

	ndev = alloc_etherdev_mq(sizeof(struct aq_nic_s), AQ_HW_QUEUES_MAX);
	if (!ndev)
		return NULL;

	aq_nic = netdev_priv(ndev);
	aq_nic->ndev = ndev;
	ndev->netdev_ops = &aq_ndev_ops;
	ndev->ethtool_ops = &aq_ethtool_ops;

	return ndev;
}

static int aq_ndev_open(struct net_device *ndev)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);
	int err = 0;

	err = aq_nic_init(aq_nic);
	if (err < 0)
		goto err_exit;

	err = aq_reapply_rxnfc_all_rules(aq_nic);
	if (err < 0)
		goto err_exit;

	err = aq_filters_vlans_update(aq_nic);
	if (err < 0)
		goto err_exit;

	err = aq_nic_start(aq_nic);
	if (err < 0) {
		aq_nic_stop(aq_nic);
		goto err_exit;
	}

err_exit:
	if (err < 0)
		aq_nic_deinit(aq_nic, true);

	return err;
}

static int aq_ndev_close(struct net_device *ndev)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);
	int err = 0;

	err = aq_nic_stop(aq_nic);
	if (err < 0)
		goto err_exit;
	aq_nic_deinit(aq_nic, true);

err_exit:
	return err;
}

static netdev_tx_t aq_ndev_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);

#if IS_REACHABLE(CONFIG_PTP_1588_CLOCK)
	if (unlikely(aq_utils_obj_test(&aq_nic->flags, AQ_NIC_PTP_DPATH_UP))) {
		/* Hardware adds the Timestamp for PTPv2 802.AS1
		 * and PTPv2 IPv4 UDP.
		 * We have to push even general 320 port messages to the ptp
		 * queue explicitly. This is a limitation of current firmware
		 * and hardware PTP design of the chip. Otherwise ptp stream
		 * will fail to sync
		 */
		if (unlikely(skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) ||
		    unlikely((ip_hdr(skb)->version == 4) &&
			     (ip_hdr(skb)->protocol == IPPROTO_UDP) &&
			     ((udp_hdr(skb)->dest == htons(319)) ||
			      (udp_hdr(skb)->dest == htons(320)))) ||
		    unlikely(eth_hdr(skb)->h_proto == htons(ETH_P_1588)))
			return aq_ptp_xmit(aq_nic, skb);
	}
#endif

	skb_tx_timestamp(skb);
	return aq_nic_xmit(aq_nic, skb);
}

static int aq_ndev_change_mtu(struct net_device *ndev, int new_mtu)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);
	int err;

	err = aq_nic_set_mtu(aq_nic, new_mtu + ETH_HLEN);

	if (err < 0)
		goto err_exit;
	ndev->mtu = new_mtu;

err_exit:
	return err;
}

static int aq_ndev_set_features(struct net_device *ndev,
				netdev_features_t features)
{
	bool is_vlan_tx_insert = !!(features & NETIF_F_HW_VLAN_CTAG_TX);
	bool is_vlan_rx_strip = !!(features & NETIF_F_HW_VLAN_CTAG_RX);
	struct aq_nic_s *aq_nic = netdev_priv(ndev);
	bool need_ndev_restart = false;
	struct aq_nic_cfg_s *aq_cfg;
	bool is_lro = false;
	int err = 0;

	aq_cfg = aq_nic_get_cfg(aq_nic);

	if (!(features & NETIF_F_NTUPLE)) {
		if (aq_nic->ndev->features & NETIF_F_NTUPLE) {
			err = aq_clear_rxnfc_all_rules(aq_nic);
			if (unlikely(err))
				goto err_exit;
		}
	}
	if (!(features & NETIF_F_HW_VLAN_CTAG_FILTER)) {
		if (aq_nic->ndev->features & NETIF_F_HW_VLAN_CTAG_FILTER) {
			err = aq_filters_vlan_offload_off(aq_nic);
			if (unlikely(err))
				goto err_exit;
		}
	}

	aq_cfg->features = features;

	if (aq_cfg->aq_hw_caps->hw_features & NETIF_F_LRO) {
		is_lro = features & NETIF_F_LRO;

		if (aq_cfg->is_lro != is_lro) {
			aq_cfg->is_lro = is_lro;
			need_ndev_restart = true;
		}
	}

	if ((aq_nic->ndev->features ^ features) & NETIF_F_RXCSUM) {
		err = aq_nic->aq_hw_ops->hw_set_offload(aq_nic->aq_hw,
							aq_cfg);

		if (unlikely(err))
			goto err_exit;
	}

	if (aq_cfg->is_vlan_rx_strip != is_vlan_rx_strip) {
		aq_cfg->is_vlan_rx_strip = is_vlan_rx_strip;
		need_ndev_restart = true;
	}
	if (aq_cfg->is_vlan_tx_insert != is_vlan_tx_insert) {
		aq_cfg->is_vlan_tx_insert = is_vlan_tx_insert;
		need_ndev_restart = true;
	}

	if (need_ndev_restart && netif_running(ndev)) {
		aq_ndev_close(ndev);
		aq_ndev_open(ndev);
	}

err_exit:
	return err;
}

static int aq_ndev_set_mac_address(struct net_device *ndev, void *addr)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);
	int err = 0;

	err = eth_mac_addr(ndev, addr);
	if (err < 0)
		goto err_exit;
	err = aq_nic_set_mac(aq_nic, ndev);
	if (err < 0)
		goto err_exit;

err_exit:
	return err;
}

static void aq_ndev_set_multicast_settings(struct net_device *ndev)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);

	(void)aq_nic_set_multicast_list(aq_nic, ndev);
}

#if IS_REACHABLE(CONFIG_PTP_1588_CLOCK)
static int aq_ndev_config_hwtstamp(struct aq_nic_s *aq_nic,
				   struct hwtstamp_config *config)
{
	if (config->flags)
		return -EINVAL;

	switch (config->tx_type) {
	case HWTSTAMP_TX_OFF:
	case HWTSTAMP_TX_ON:
		break;
	default:
		return -ERANGE;
	}

	switch (config->rx_filter) {
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
		config->rx_filter = HWTSTAMP_FILTER_PTP_V2_EVENT;
		break;
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
	case HWTSTAMP_FILTER_NONE:
		break;
	default:
		return -ERANGE;
	}

	return aq_ptp_hwtstamp_config_set(aq_nic->aq_ptp, config);
}
#endif

static int aq_ndev_hwtstamp_set(struct aq_nic_s *aq_nic, struct ifreq *ifr)
{
	struct hwtstamp_config config;
#if IS_REACHABLE(CONFIG_PTP_1588_CLOCK)
	int ret_val;
#endif

	if (!aq_nic->aq_ptp)
		return -EOPNOTSUPP;

	if (copy_from_user(&config, ifr->ifr_data, sizeof(config)))
		return -EFAULT;
#if IS_REACHABLE(CONFIG_PTP_1588_CLOCK)
	ret_val = aq_ndev_config_hwtstamp(aq_nic, &config);
	if (ret_val)
		return ret_val;
#endif

	return copy_to_user(ifr->ifr_data, &config, sizeof(config)) ?
	       -EFAULT : 0;
}

#if IS_REACHABLE(CONFIG_PTP_1588_CLOCK)
static int aq_ndev_hwtstamp_get(struct aq_nic_s *aq_nic, struct ifreq *ifr)
{
	struct hwtstamp_config config;

	if (!aq_nic->aq_ptp)
		return -EOPNOTSUPP;

	aq_ptp_hwtstamp_config_get(aq_nic->aq_ptp, &config);
	return copy_to_user(ifr->ifr_data, &config, sizeof(config)) ?
	       -EFAULT : 0;
}
#endif

static int aq_ndev_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd)
{
	struct aq_nic_s *aq_nic = netdev_priv(netdev);

	switch (cmd) {
	case SIOCSHWTSTAMP:
		return aq_ndev_hwtstamp_set(aq_nic, ifr);

#if IS_REACHABLE(CONFIG_PTP_1588_CLOCK)
	case SIOCGHWTSTAMP:
		return aq_ndev_hwtstamp_get(aq_nic, ifr);
#endif
	}

	return -EOPNOTSUPP;
}

static int aq_ndo_vlan_rx_add_vid(struct net_device *ndev, __be16 proto,
				  u16 vid)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);

	if (!aq_nic->aq_hw_ops->hw_filter_vlan_set)
		return -EOPNOTSUPP;

	set_bit(vid, aq_nic->active_vlans);

	return aq_filters_vlans_update(aq_nic);
}

static int aq_ndo_vlan_rx_kill_vid(struct net_device *ndev, __be16 proto,
				   u16 vid)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);

	if (!aq_nic->aq_hw_ops->hw_filter_vlan_set)
		return -EOPNOTSUPP;

	clear_bit(vid, aq_nic->active_vlans);

	if (-ENOENT == aq_del_fvlan_by_vlan(aq_nic, vid))
		return aq_filters_vlans_update(aq_nic);

	return 0;
}

static int aq_validate_mqprio_opt(struct aq_nic_s *self,
				  struct tc_mqprio_qopt_offload *mqprio,
				  const unsigned int num_tc)
{
	const bool has_min_rate = !!(mqprio->flags & TC_MQPRIO_F_MIN_RATE);
	struct aq_nic_cfg_s *aq_nic_cfg = aq_nic_get_cfg(self);
	const unsigned int tcs_max = min_t(u8, aq_nic_cfg->aq_hw_caps->tcs_max,
					   AQ_CFG_TCS_MAX);

	if (num_tc > tcs_max) {
		netdev_err(self->ndev, "Too many TCs requested\n");
		return -EOPNOTSUPP;
	}

	if (num_tc != 0 && !is_power_of_2(num_tc)) {
		netdev_err(self->ndev, "TC count should be power of 2\n");
		return -EOPNOTSUPP;
	}

	if (has_min_rate && !ATL_HW_IS_CHIP_FEATURE(self->aq_hw, ANTIGUA)) {
		netdev_err(self->ndev, "Min tx rate is not supported\n");
		return -EOPNOTSUPP;
	}

	return 0;
}

static int aq_ndo_setup_tc(struct net_device *dev, enum tc_setup_type type,
			   void *type_data)
{
	struct tc_mqprio_qopt_offload *mqprio = type_data;
	struct aq_nic_s *aq_nic = netdev_priv(dev);
	bool has_min_rate;
	bool has_max_rate;
	int err;
	int i;

	if (type != TC_SETUP_QDISC_MQPRIO)
		return -EOPNOTSUPP;

	has_min_rate = !!(mqprio->flags & TC_MQPRIO_F_MIN_RATE);
	has_max_rate = !!(mqprio->flags & TC_MQPRIO_F_MAX_RATE);

	err = aq_validate_mqprio_opt(aq_nic, mqprio, mqprio->qopt.num_tc);
	if (err)
		return err;

	for (i = 0; i < mqprio->qopt.num_tc; i++) {
		if (has_max_rate) {
			u64 max_rate = mqprio->max_rate[i];

			do_div(max_rate, AQ_MBPS_DIVISOR);
			aq_nic_setup_tc_max_rate(aq_nic, i, (u32)max_rate);
		}

		if (has_min_rate) {
			u64 min_rate = mqprio->min_rate[i];

			do_div(min_rate, AQ_MBPS_DIVISOR);
			aq_nic_setup_tc_min_rate(aq_nic, i, (u32)min_rate);
		}
	}

	return aq_nic_setup_tc_mqprio(aq_nic, mqprio->qopt.num_tc,
				      mqprio->qopt.prio_tc_map);
}

static const struct net_device_ops aq_ndev_ops = {
	.ndo_open = aq_ndev_open,
	.ndo_stop = aq_ndev_close,
	.ndo_start_xmit = aq_ndev_start_xmit,
	.ndo_set_rx_mode = aq_ndev_set_multicast_settings,
	.ndo_change_mtu = aq_ndev_change_mtu,
	.ndo_set_mac_address = aq_ndev_set_mac_address,
	.ndo_set_features = aq_ndev_set_features,
	.ndo_do_ioctl = aq_ndev_ioctl,
	.ndo_vlan_rx_add_vid = aq_ndo_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid = aq_ndo_vlan_rx_kill_vid,
	.ndo_setup_tc = aq_ndo_setup_tc,
};

static int __init aq_ndev_init_module(void)
{
	int ret;

	aq_ndev_wq = create_singlethread_workqueue(aq_ndev_driver_name);
	if (!aq_ndev_wq) {
		pr_err("Failed to create workqueue\n");
		return -ENOMEM;
	}

	ret = aq_pci_func_register_driver();
	if (ret) {
		destroy_workqueue(aq_ndev_wq);
		return ret;
	}

	return 0;
}

static void __exit aq_ndev_exit_module(void)
{
	aq_pci_func_unregister_driver();

	if (aq_ndev_wq) {
		destroy_workqueue(aq_ndev_wq);
		aq_ndev_wq = NULL;
	}
}

module_init(aq_ndev_init_module);
module_exit(aq_ndev_exit_module);
