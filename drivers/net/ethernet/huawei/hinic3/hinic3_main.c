// SPDX-License-Identifier: GPL-2.0
// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

#include <linux/etherdevice.h>
#include <linux/netdevice.h>

#include "hinic3_common.h"
#include "hinic3_hw_comm.h"
#include "hinic3_hwdev.h"
#include "hinic3_hwif.h"
#include "hinic3_lld.h"
#include "hinic3_nic_cfg.h"
#include "hinic3_nic_dev.h"
#include "hinic3_nic_io.h"
#include "hinic3_rss.h"
#include "hinic3_rx.h"
#include "hinic3_tx.h"

#define HINIC3_NIC_DRV_DESC  "Intelligent Network Interface Card Driver"

#define HINIC3_RX_BUF_LEN          2048
#define HINIC3_LRO_REPLENISH_THLD  256
#define HINIC3_NIC_DEV_WQ_NAME     "hinic3_nic_dev_wq"

#define HINIC3_SQ_DEPTH            1024
#define HINIC3_RQ_DEPTH            1024

#define HINIC3_DEFAULT_TXRX_MSIX_PENDING_LIMIT      2
#define HINIC3_DEFAULT_TXRX_MSIX_COALESC_TIMER_CFG  25
#define HINIC3_DEFAULT_TXRX_MSIX_RESEND_TIMER_CFG   7

static void init_intr_coal_param(struct net_device *netdev)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);
	struct hinic3_intr_coal_info *info;
	u16 i;

	for (i = 0; i < nic_dev->max_qps; i++) {
		info = &nic_dev->intr_coalesce[i];
		info->pending_limit = HINIC3_DEFAULT_TXRX_MSIX_PENDING_LIMIT;
		info->coalesce_timer_cfg = HINIC3_DEFAULT_TXRX_MSIX_COALESC_TIMER_CFG;
		info->resend_timer_cfg = HINIC3_DEFAULT_TXRX_MSIX_RESEND_TIMER_CFG;
	}
}

static int hinic3_init_intr_coalesce(struct net_device *netdev)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);

	nic_dev->intr_coalesce = kcalloc(nic_dev->max_qps,
					 sizeof(*nic_dev->intr_coalesce),
					 GFP_KERNEL);

	if (!nic_dev->intr_coalesce)
		return -ENOMEM;

	init_intr_coal_param(netdev);

	return 0;
}

static void hinic3_free_intr_coalesce(struct net_device *netdev)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);

	kfree(nic_dev->intr_coalesce);
}

static int hinic3_alloc_txrxqs(struct net_device *netdev)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);
	struct hinic3_hwdev *hwdev = nic_dev->hwdev;
	int err;

	err = hinic3_alloc_txqs(netdev);
	if (err) {
		dev_err(hwdev->dev, "Failed to alloc txqs\n");
		return err;
	}

	err = hinic3_alloc_rxqs(netdev);
	if (err) {
		dev_err(hwdev->dev, "Failed to alloc rxqs\n");
		goto err_free_txqs;
	}

	err = hinic3_init_intr_coalesce(netdev);
	if (err) {
		dev_err(hwdev->dev, "Failed to init_intr_coalesce\n");
		goto err_free_rxqs;
	}

	return 0;

err_free_rxqs:
	hinic3_free_rxqs(netdev);

err_free_txqs:
	hinic3_free_txqs(netdev);

	return err;
}

static void hinic3_free_txrxqs(struct net_device *netdev)
{
	hinic3_free_intr_coalesce(netdev);
	hinic3_free_rxqs(netdev);
	hinic3_free_txqs(netdev);
}

static int hinic3_init_nic_dev(struct net_device *netdev,
			       struct hinic3_hwdev *hwdev)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);
	struct pci_dev *pdev = hwdev->pdev;

	nic_dev->netdev = netdev;
	SET_NETDEV_DEV(netdev, &pdev->dev);
	nic_dev->hwdev = hwdev;
	nic_dev->pdev = pdev;

	nic_dev->rx_buf_len = HINIC3_RX_BUF_LEN;
	nic_dev->lro_replenish_thld = HINIC3_LRO_REPLENISH_THLD;
	nic_dev->nic_svc_cap = hwdev->cfg_mgmt->cap.nic_svc_cap;

	return 0;
}

static int hinic3_sw_init(struct net_device *netdev)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);
	struct hinic3_hwdev *hwdev = nic_dev->hwdev;
	int err;

	nic_dev->q_params.sq_depth = HINIC3_SQ_DEPTH;
	nic_dev->q_params.rq_depth = HINIC3_RQ_DEPTH;

	hinic3_try_to_enable_rss(netdev);

	/* VF driver always uses random MAC address. During VM migration to a
	 * new device, the new device should learn the VMs old MAC rather than
	 * provide its own MAC. The product design assumes that every VF is
	 * suspectable to migration so the device avoids offering MAC address
	 * to VFs.
	 */
	eth_hw_addr_random(netdev);
	err = hinic3_set_mac(hwdev, netdev->dev_addr, 0,
			     hinic3_global_func_id(hwdev));
	if (err) {
		dev_err(hwdev->dev, "Failed to set default MAC\n");
		goto err_clear_rss_config;
	}

	err = hinic3_alloc_txrxqs(netdev);
	if (err) {
		dev_err(hwdev->dev, "Failed to alloc qps\n");
		goto err_del_mac;
	}

	return 0;

err_del_mac:
	hinic3_del_mac(hwdev, netdev->dev_addr, 0,
		       hinic3_global_func_id(hwdev));
err_clear_rss_config:
	hinic3_clear_rss_config(netdev);

	return err;
}

static void hinic3_sw_uninit(struct net_device *netdev)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);

	hinic3_free_txrxqs(netdev);
	hinic3_del_mac(nic_dev->hwdev, netdev->dev_addr, 0,
		       hinic3_global_func_id(nic_dev->hwdev));
	hinic3_clear_rss_config(netdev);
}

static void hinic3_assign_netdev_ops(struct net_device *netdev)
{
	hinic3_set_netdev_ops(netdev);
}

static void netdev_feature_init(struct net_device *netdev)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);
	netdev_features_t cso_fts = 0;
	netdev_features_t tso_fts = 0;
	netdev_features_t dft_fts;

	dft_fts = NETIF_F_SG | NETIF_F_HIGHDMA;
	if (hinic3_test_support(nic_dev, HINIC3_NIC_F_CSUM))
		cso_fts |= NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM | NETIF_F_RXCSUM;
	if (hinic3_test_support(nic_dev, HINIC3_NIC_F_SCTP_CRC))
		cso_fts |= NETIF_F_SCTP_CRC;
	if (hinic3_test_support(nic_dev, HINIC3_NIC_F_TSO))
		tso_fts |= NETIF_F_TSO | NETIF_F_TSO6;

	netdev->features |= dft_fts | cso_fts | tso_fts;
}

static int hinic3_set_default_hw_feature(struct net_device *netdev)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);
	struct hinic3_hwdev *hwdev = nic_dev->hwdev;
	int err;

	err = hinic3_set_nic_feature_to_hw(nic_dev);
	if (err) {
		dev_err(hwdev->dev, "Failed to set nic features\n");
		return err;
	}

	return 0;
}

static void hinic3_link_status_change(struct net_device *netdev,
				      bool link_status_up)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);

	if (link_status_up) {
		if (netif_carrier_ok(netdev))
			return;

		nic_dev->link_status_up = true;
		netif_carrier_on(netdev);
		netdev_dbg(netdev, "Link is up\n");
	} else {
		if (!netif_carrier_ok(netdev))
			return;

		nic_dev->link_status_up = false;
		netif_carrier_off(netdev);
		netdev_dbg(netdev, "Link is down\n");
	}
}

static void hinic3_nic_event(struct auxiliary_device *adev,
			     struct hinic3_event_info *event)
{
	struct hinic3_nic_dev *nic_dev = dev_get_drvdata(&adev->dev);
	struct net_device *netdev;

	netdev = nic_dev->netdev;

	switch (HINIC3_SRV_EVENT_TYPE(event->service, event->type)) {
	case HINIC3_SRV_EVENT_TYPE(HINIC3_EVENT_SRV_NIC,
				   HINIC3_NIC_EVENT_LINK_UP):
		hinic3_link_status_change(netdev, true);
		break;
	case HINIC3_SRV_EVENT_TYPE(HINIC3_EVENT_SRV_NIC,
				   HINIC3_NIC_EVENT_LINK_DOWN):
		hinic3_link_status_change(netdev, false);
		break;
	default:
		break;
	}
}

static int hinic3_nic_probe(struct auxiliary_device *adev,
			    const struct auxiliary_device_id *id)
{
	struct hinic3_hwdev *hwdev = hinic3_adev_get_hwdev(adev);
	struct pci_dev *pdev = hwdev->pdev;
	struct hinic3_nic_dev *nic_dev;
	struct net_device *netdev;
	u16 max_qps, glb_func_id;
	int err;

	if (!hinic3_support_nic(hwdev)) {
		dev_dbg(&adev->dev, "HW doesn't support nic\n");
		return 0;
	}

	hinic3_adev_event_register(adev, hinic3_nic_event);

	glb_func_id = hinic3_global_func_id(hwdev);
	err = hinic3_func_reset(hwdev, glb_func_id, COMM_FUNC_RESET_BIT_NIC);
	if (err) {
		dev_err(&adev->dev, "Failed to reset function\n");
		goto err_unregister_adev_event;
	}

	max_qps = hinic3_func_max_qnum(hwdev);
	netdev = alloc_etherdev_mq(sizeof(*nic_dev), max_qps);
	if (!netdev) {
		dev_err(&adev->dev, "Failed to allocate netdev\n");
		err = -ENOMEM;
		goto err_unregister_adev_event;
	}

	nic_dev = netdev_priv(netdev);
	dev_set_drvdata(&adev->dev, nic_dev);
	err = hinic3_init_nic_dev(netdev, hwdev);
	if (err)
		goto err_free_netdev;

	err = hinic3_init_nic_io(nic_dev);
	if (err)
		goto err_free_netdev;

	err = hinic3_sw_init(netdev);
	if (err)
		goto err_free_nic_io;

	hinic3_assign_netdev_ops(netdev);

	netdev_feature_init(netdev);
	err = hinic3_set_default_hw_feature(netdev);
	if (err)
		goto err_uninit_sw;

	netif_carrier_off(netdev);

	err = register_netdev(netdev);
	if (err)
		goto err_uninit_nic_feature;

	return 0;

err_uninit_nic_feature:
	hinic3_update_nic_feature(nic_dev, 0);
	hinic3_set_nic_feature_to_hw(nic_dev);

err_uninit_sw:
	hinic3_sw_uninit(netdev);

err_free_nic_io:
	hinic3_free_nic_io(nic_dev);

err_free_netdev:
	free_netdev(netdev);

err_unregister_adev_event:
	hinic3_adev_event_unregister(adev);
	dev_err(&pdev->dev, "NIC service probe failed\n");

	return err;
}

static void hinic3_nic_remove(struct auxiliary_device *adev)
{
	struct hinic3_nic_dev *nic_dev = dev_get_drvdata(&adev->dev);
	struct net_device *netdev;

	if (!hinic3_support_nic(nic_dev->hwdev))
		return;

	netdev = nic_dev->netdev;
	unregister_netdev(netdev);

	hinic3_update_nic_feature(nic_dev, 0);
	hinic3_set_nic_feature_to_hw(nic_dev);
	hinic3_sw_uninit(netdev);

	hinic3_free_nic_io(nic_dev);

	free_netdev(netdev);
}

static const struct auxiliary_device_id hinic3_nic_id_table[] = {
	{
		.name = HINIC3_NIC_DRV_NAME ".nic",
	},
	{}
};

static struct auxiliary_driver hinic3_nic_driver = {
	.probe    = hinic3_nic_probe,
	.remove   = hinic3_nic_remove,
	.suspend  = NULL,
	.resume   = NULL,
	.name     = "nic",
	.id_table = hinic3_nic_id_table,
};

static __init int hinic3_nic_lld_init(void)
{
	int err;

	err = hinic3_lld_init();
	if (err)
		return err;

	err = auxiliary_driver_register(&hinic3_nic_driver);
	if (err) {
		hinic3_lld_exit();
		return err;
	}

	return 0;
}

static __exit void hinic3_nic_lld_exit(void)
{
	auxiliary_driver_unregister(&hinic3_nic_driver);

	hinic3_lld_exit();
}

module_init(hinic3_nic_lld_init);
module_exit(hinic3_nic_lld_exit);

MODULE_AUTHOR("Huawei Technologies CO., Ltd");
MODULE_DESCRIPTION(HINIC3_NIC_DRV_DESC);
MODULE_LICENSE("GPL");
