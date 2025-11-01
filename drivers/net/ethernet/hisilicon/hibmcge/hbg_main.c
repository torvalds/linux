// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) 2024 Hisilicon Limited.

#include <linux/etherdevice.h>
#include <linux/if_vlan.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/phy.h>
#include "hbg_common.h"
#include "hbg_diagnose.h"
#include "hbg_err.h"
#include "hbg_ethtool.h"
#include "hbg_hw.h"
#include "hbg_irq.h"
#include "hbg_mdio.h"
#include "hbg_txrx.h"
#include "hbg_debugfs.h"

#define HBG_SUPPORT_FEATURES (NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM | \
			     NETIF_F_RXCSUM)

static void hbg_all_irq_enable(struct hbg_priv *priv, bool enabled)
{
	const struct hbg_irq_info *info;
	u32 i;

	for (i = 0; i < priv->vectors.info_array_len; i++) {
		info = &priv->vectors.info_array[i];
		hbg_hw_irq_enable(priv, info->mask, enabled);
	}
}

static int hbg_net_open(struct net_device *netdev)
{
	struct hbg_priv *priv = netdev_priv(netdev);
	int ret;

	ret = hbg_txrx_init(priv);
	if (ret)
		return ret;

	hbg_all_irq_enable(priv, true);
	hbg_hw_mac_enable(priv, HBG_STATUS_ENABLE);
	netif_start_queue(netdev);
	hbg_phy_start(priv);

	return 0;
}

/* This function only can be called after hbg_txrx_uninit() */
static int hbg_hw_txrx_clear(struct hbg_priv *priv)
{
	int ret;

	/* After ring buffers have been released,
	 * do a reset to release hw fifo rx ring buffer
	 */
	ret = hbg_hw_event_notify(priv, HBG_HW_EVENT_RESET);
	if (ret)
		return ret;

	/* After reset, regs need to be reconfigured */
	return hbg_rebuild(priv);
}

static int hbg_net_stop(struct net_device *netdev)
{
	struct hbg_priv *priv = netdev_priv(netdev);

	hbg_phy_stop(priv);
	netif_stop_queue(netdev);
	hbg_hw_mac_enable(priv, HBG_STATUS_DISABLE);
	hbg_all_irq_enable(priv, false);
	hbg_txrx_uninit(priv);
	return hbg_hw_txrx_clear(priv);
}

static void hbg_update_promisc_mode(struct net_device *netdev, bool overflow)
{
	struct hbg_priv *priv = netdev_priv(netdev);

	/* Only when not table_overflow, and netdev->flags not set IFF_PROMISC,
	 * The MAC filter will be enabled.
	 * Otherwise the filter will be disabled.
	 */
	priv->filter.enabled = !(overflow || (netdev->flags & IFF_PROMISC));
	hbg_hw_set_mac_filter_enable(priv, priv->filter.enabled);
}

static void hbg_set_mac_to_mac_table(struct hbg_priv *priv,
				     u32 index, const u8 *addr)
{
	if (addr) {
		ether_addr_copy(priv->filter.mac_table[index].addr, addr);
		hbg_hw_set_uc_addr(priv, ether_addr_to_u64(addr), index);
	} else {
		eth_zero_addr(priv->filter.mac_table[index].addr);
		hbg_hw_set_uc_addr(priv, 0, index);
	}
}

static int hbg_get_index_from_mac_table(struct hbg_priv *priv,
					const u8 *addr, u32 *index)
{
	u32 i;

	for (i = 0; i < priv->filter.table_max_len; i++)
		if (ether_addr_equal(priv->filter.mac_table[i].addr, addr)) {
			*index = i;
			return 0;
		}

	return -EINVAL;
}

static int hbg_add_mac_to_filter(struct hbg_priv *priv, const u8 *addr)
{
	u32 index;

	/* already exists */
	if (!hbg_get_index_from_mac_table(priv, addr, &index))
		return 0;

	for (index = 0; index < priv->filter.table_max_len; index++)
		if (is_zero_ether_addr(priv->filter.mac_table[index].addr)) {
			hbg_set_mac_to_mac_table(priv, index, addr);
			return 0;
		}

	return -ENOSPC;
}

static void hbg_del_mac_from_filter(struct hbg_priv *priv, const u8 *addr)
{
	u32 index;

	/* not exists */
	if (hbg_get_index_from_mac_table(priv, addr, &index))
		return;

	hbg_set_mac_to_mac_table(priv, index, NULL);
}

static int hbg_uc_sync(struct net_device *netdev, const unsigned char *addr)
{
	struct hbg_priv *priv = netdev_priv(netdev);

	return hbg_add_mac_to_filter(priv, addr);
}

static int hbg_uc_unsync(struct net_device *netdev, const unsigned char *addr)
{
	struct hbg_priv *priv = netdev_priv(netdev);

	if (ether_addr_equal(netdev->dev_addr, (u8 *)addr))
		return 0;

	hbg_del_mac_from_filter(priv, addr);
	return 0;
}

static void hbg_net_set_rx_mode(struct net_device *netdev)
{
	int ret;

	ret = __dev_uc_sync(netdev, hbg_uc_sync, hbg_uc_unsync);

	/* If ret != 0, overflow has occurred */
	hbg_update_promisc_mode(netdev, !!ret);
}

static int hbg_net_set_mac_address(struct net_device *netdev, void *addr)
{
	struct hbg_priv *priv = netdev_priv(netdev);
	u8 *mac_addr;
	bool exists;
	u32 index;

	mac_addr = ((struct sockaddr *)addr)->sa_data;

	if (!is_valid_ether_addr(mac_addr))
		return -EADDRNOTAVAIL;

	/* The index of host mac is always 0.
	 * If new mac address already exists,
	 * delete the existing mac address and
	 * add it to the position with index 0.
	 */
	exists = !hbg_get_index_from_mac_table(priv, mac_addr, &index);
	hbg_set_mac_to_mac_table(priv, 0, mac_addr);
	if (exists)
		hbg_set_mac_to_mac_table(priv, index, NULL);

	hbg_hw_set_rx_pause_mac_addr(priv, ether_addr_to_u64(mac_addr));
	dev_addr_set(netdev, mac_addr);
	return 0;
}

static int hbg_net_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct hbg_priv *priv = netdev_priv(netdev);

	if (netif_running(netdev))
		return -EBUSY;

	dev_dbg(&priv->pdev->dev,
		"change mtu from %u to %u\n", netdev->mtu, new_mtu);

	hbg_hw_set_mtu(priv, new_mtu);
	WRITE_ONCE(netdev->mtu, new_mtu);

	return 0;
}

static void hbg_net_tx_timeout(struct net_device *netdev, unsigned int txqueue)
{
	struct hbg_priv *priv = netdev_priv(netdev);
	struct hbg_ring *ring = &priv->tx_ring;
	char *buf = ring->tout_log_buf;
	u32 pos = 0;

	priv->stats.tx_timeout_cnt++;

	pos += scnprintf(buf + pos, HBG_TX_TIMEOUT_BUF_LEN - pos,
			 "tx_timeout cnt: %llu\n", priv->stats.tx_timeout_cnt);
	pos += scnprintf(buf + pos, HBG_TX_TIMEOUT_BUF_LEN - pos,
			 "ring used num: %u, fifo used num: %u\n",
			 hbg_get_queue_used_num(ring),
			 hbg_hw_get_fifo_used_num(priv, HBG_DIR_TX));
	pos += scnprintf(buf + pos, HBG_TX_TIMEOUT_BUF_LEN - pos,
			 "ntc: %u, ntu: %u, irq enabled: %u\n",
			 ring->ntc, ring->ntu,
			 hbg_hw_irq_is_enabled(priv, HBG_INT_MSK_TX_B));

	netdev_info(netdev, "%s", buf);
}

static void hbg_net_get_stats(struct net_device *netdev,
			      struct rtnl_link_stats64 *stats)
{
	struct hbg_priv *priv = netdev_priv(netdev);
	struct hbg_stats *h_stats = &priv->stats;

	hbg_update_stats(priv);
	dev_get_tstats64(netdev, stats);

	/* fifo empty */
	stats->tx_fifo_errors += h_stats->tx_drop_cnt;

	stats->tx_dropped += h_stats->tx_excessive_length_drop_cnt +
			     h_stats->tx_drop_cnt;
	stats->tx_errors += h_stats->tx_add_cs_fail_cnt +
			    h_stats->tx_bufrl_err_cnt +
			    h_stats->tx_underrun_err_cnt +
			    h_stats->tx_crc_err_cnt;
	stats->rx_errors += h_stats->rx_data_error_cnt;
	stats->multicast += h_stats->rx_mc_pkt_cnt;
	stats->rx_dropped += h_stats->rx_desc_drop;
	stats->rx_length_errors += h_stats->rx_frame_very_long_err_cnt +
				   h_stats->rx_frame_long_err_cnt +
				   h_stats->rx_frame_runt_err_cnt +
				   h_stats->rx_frame_short_err_cnt +
				   h_stats->rx_lengthfield_err_cnt;
	stats->rx_frame_errors += h_stats->rx_desc_l2_err_cnt +
				  h_stats->rx_desc_l3l4_err_cnt;
	stats->rx_fifo_errors += h_stats->rx_overflow_cnt +
				 h_stats->rx_overrun_cnt;
	stats->rx_crc_errors += h_stats->rx_fcs_error_cnt;
}

static const struct net_device_ops hbg_netdev_ops = {
	.ndo_open		= hbg_net_open,
	.ndo_stop		= hbg_net_stop,
	.ndo_start_xmit		= hbg_net_start_xmit,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= hbg_net_set_mac_address,
	.ndo_change_mtu		= hbg_net_change_mtu,
	.ndo_tx_timeout		= hbg_net_tx_timeout,
	.ndo_set_rx_mode	= hbg_net_set_rx_mode,
	.ndo_get_stats64	= hbg_net_get_stats,
	.ndo_eth_ioctl		= phy_do_ioctl_running,
};

static void hbg_service_task(struct work_struct *work)
{
	struct hbg_priv *priv = container_of(work, struct hbg_priv,
					     service_task.work);

	if (test_and_clear_bit(HBG_NIC_STATE_NEED_RESET, &priv->state))
		hbg_err_reset(priv);

	if (test_and_clear_bit(HBG_NIC_STATE_NP_LINK_FAIL, &priv->state))
		hbg_fix_np_link_fail(priv);

	hbg_diagnose_message_push(priv);

	/* The type of statistics register is u32,
	 * To prevent the statistics register from overflowing,
	 * the driver dumps the statistics every 30 seconds.
	 */
	if (time_after(jiffies, priv->last_update_stats_time + 30 * HZ)) {
		hbg_update_stats(priv);
		priv->last_update_stats_time = jiffies;
	}

	schedule_delayed_work(&priv->service_task,
			      msecs_to_jiffies(MSEC_PER_SEC));
}

void hbg_err_reset_task_schedule(struct hbg_priv *priv)
{
	set_bit(HBG_NIC_STATE_NEED_RESET, &priv->state);
	schedule_delayed_work(&priv->service_task, 0);
}

void hbg_np_link_fail_task_schedule(struct hbg_priv *priv)
{
	set_bit(HBG_NIC_STATE_NP_LINK_FAIL, &priv->state);
	schedule_delayed_work(&priv->service_task, 0);
}

static void hbg_cancel_delayed_work_sync(void *data)
{
	cancel_delayed_work_sync(data);
}

static int hbg_delaywork_init(struct hbg_priv *priv)
{
	INIT_DELAYED_WORK(&priv->service_task, hbg_service_task);
	schedule_delayed_work(&priv->service_task, 0);
	return devm_add_action_or_reset(&priv->pdev->dev,
					hbg_cancel_delayed_work_sync,
					&priv->service_task);
}

static int hbg_mac_filter_init(struct hbg_priv *priv)
{
	struct hbg_dev_specs *dev_specs = &priv->dev_specs;
	struct hbg_mac_filter *filter = &priv->filter;
	struct hbg_mac_table_entry *tmp_table;

	tmp_table = devm_kcalloc(&priv->pdev->dev, dev_specs->uc_mac_num,
				 sizeof(*tmp_table), GFP_KERNEL);
	if (!tmp_table)
		return -ENOMEM;

	filter->mac_table = tmp_table;
	filter->table_max_len = dev_specs->uc_mac_num;
	filter->enabled = true;

	hbg_hw_set_mac_filter_enable(priv, filter->enabled);
	return 0;
}

static void hbg_init_user_def(struct hbg_priv *priv)
{
	struct ethtool_pauseparam *pause_param = &priv->user_def.pause_param;

	priv->mac.pause_autoneg = HBG_STATUS_ENABLE;

	pause_param->autoneg = priv->mac.pause_autoneg;
	hbg_hw_get_pause_enable(priv, &pause_param->tx_pause,
				&pause_param->rx_pause);
}

static int hbg_init(struct hbg_priv *priv)
{
	int ret;

	ret = hbg_hw_event_notify(priv, HBG_HW_EVENT_INIT);
	if (ret)
		return ret;

	ret = hbg_hw_init(priv);
	if (ret)
		return ret;

	ret = hbg_irq_init(priv);
	if (ret)
		return ret;

	ret = hbg_mdio_init(priv);
	if (ret)
		return ret;

	ret = hbg_mac_filter_init(priv);
	if (ret)
		return ret;

	ret = hbg_delaywork_init(priv);
	if (ret)
		return ret;

	hbg_debugfs_init(priv);
	hbg_init_user_def(priv);
	return 0;
}

static int hbg_pci_init(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct hbg_priv *priv = netdev_priv(netdev);
	struct device *dev = &pdev->dev;
	int ret;

	ret = pcim_enable_device(pdev);
	if (ret)
		return dev_err_probe(dev, ret, "failed to enable PCI device\n");

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret)
		return dev_err_probe(dev, ret, "failed to set PCI DMA mask\n");

	ret = pcim_iomap_regions(pdev, BIT(0), dev_driver_string(dev));
	if (ret)
		return dev_err_probe(dev, ret, "failed to map PCI bar space\n");

	priv->io_base = pcim_iomap_table(pdev)[0];
	if (!priv->io_base)
		return -ENOMEM;

	pci_set_master(pdev);
	return 0;
}

static int hbg_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct device *dev = &pdev->dev;
	struct net_device *netdev;
	struct hbg_priv *priv;
	int ret;

	netdev = devm_alloc_etherdev(dev, sizeof(struct hbg_priv));
	if (!netdev)
		return -ENOMEM;

	pci_set_drvdata(pdev, netdev);
	SET_NETDEV_DEV(netdev, dev);

	priv = netdev_priv(netdev);
	priv->netdev = netdev;
	priv->pdev = pdev;

	ret = hbg_pci_init(pdev);
	if (ret)
		return ret;

	ret = hbg_init(priv);
	if (ret)
		return ret;

	/* set default features */
	netdev->features |= HBG_SUPPORT_FEATURES;
	netdev->hw_features |= HBG_SUPPORT_FEATURES;
	netdev->priv_flags |= IFF_UNICAST_FLT;

	netdev->pcpu_stat_type = NETDEV_PCPU_STAT_TSTATS;
	netdev->max_mtu = priv->dev_specs.max_mtu;
	netdev->min_mtu = priv->dev_specs.min_mtu;
	netdev->netdev_ops = &hbg_netdev_ops;
	netdev->watchdog_timeo = 5 * HZ;

	hbg_hw_set_mtu(priv, ETH_DATA_LEN);
	hbg_net_set_mac_address(priv->netdev, &priv->dev_specs.mac_addr);
	hbg_ethtool_set_ops(netdev);

	ret = devm_register_netdev(dev, netdev);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register netdev\n");

	netif_carrier_off(netdev);
	return 0;
}

static const struct pci_device_id hbg_pci_tbl[] = {
	{PCI_VDEVICE(HUAWEI, 0x3730), 0},
	{ }
};
MODULE_DEVICE_TABLE(pci, hbg_pci_tbl);

static struct pci_driver hbg_driver = {
	.name		= "hibmcge",
	.id_table	= hbg_pci_tbl,
	.probe		= hbg_probe,
};

static int __init hbg_module_init(void)
{
	int ret;

	hbg_debugfs_register();
	hbg_set_pci_err_handler(&hbg_driver);
	ret = pci_register_driver(&hbg_driver);
	if (ret)
		hbg_debugfs_unregister();

	return ret;
}
module_init(hbg_module_init);

static void __exit hbg_module_exit(void)
{
	pci_unregister_driver(&hbg_driver);
	hbg_debugfs_unregister();
}
module_exit(hbg_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Huawei Tech. Co., Ltd.");
MODULE_DESCRIPTION("hibmcge driver");
MODULE_VERSION("1.0");
