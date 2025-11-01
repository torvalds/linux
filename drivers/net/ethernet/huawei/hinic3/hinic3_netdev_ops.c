// SPDX-License-Identifier: GPL-2.0
// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

#include <linux/etherdevice.h>
#include <linux/netdevice.h>

#include "hinic3_hwif.h"
#include "hinic3_nic_cfg.h"
#include "hinic3_nic_dev.h"
#include "hinic3_nic_io.h"
#include "hinic3_rss.h"
#include "hinic3_rx.h"
#include "hinic3_tx.h"

/* try to modify the number of irq to the target number,
 * and return the actual number of irq.
 */
static u16 hinic3_qp_irq_change(struct net_device *netdev,
				u16 dst_num_qp_irq)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);
	struct msix_entry *qps_msix_entries;
	u16 resp_irq_num, irq_num_gap, i;
	u16 idx;
	int err;

	qps_msix_entries = nic_dev->qps_msix_entries;
	if (dst_num_qp_irq > nic_dev->num_qp_irq) {
		irq_num_gap = dst_num_qp_irq - nic_dev->num_qp_irq;
		err = hinic3_alloc_irqs(nic_dev->hwdev, irq_num_gap,
					&qps_msix_entries[nic_dev->num_qp_irq],
					&resp_irq_num);
		if (err) {
			netdev_err(netdev, "Failed to alloc irqs\n");
			return nic_dev->num_qp_irq;
		}

		nic_dev->num_qp_irq += resp_irq_num;
	} else if (dst_num_qp_irq < nic_dev->num_qp_irq) {
		irq_num_gap = nic_dev->num_qp_irq - dst_num_qp_irq;
		for (i = 0; i < irq_num_gap; i++) {
			idx = (nic_dev->num_qp_irq - i) - 1;
			hinic3_free_irq(nic_dev->hwdev,
					qps_msix_entries[idx].vector);
			qps_msix_entries[idx].vector = 0;
			qps_msix_entries[idx].entry = 0;
		}
		nic_dev->num_qp_irq = dst_num_qp_irq;
	}

	return nic_dev->num_qp_irq;
}

static void hinic3_config_num_qps(struct net_device *netdev,
				  struct hinic3_dyna_txrxq_params *q_params)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);
	u16 alloc_num_irq, cur_num_irq;
	u16 dst_num_irq;

	if (!test_bit(HINIC3_RSS_ENABLE, &nic_dev->flags))
		q_params->num_qps = 1;

	if (nic_dev->num_qp_irq >= q_params->num_qps)
		goto out;

	cur_num_irq = nic_dev->num_qp_irq;

	alloc_num_irq = hinic3_qp_irq_change(netdev, q_params->num_qps);
	if (alloc_num_irq < q_params->num_qps) {
		q_params->num_qps = alloc_num_irq;
		netdev_warn(netdev, "Can not get enough irqs, adjust num_qps to %u\n",
			    q_params->num_qps);

		/* The current irq may be in use, we must keep it */
		dst_num_irq = max_t(u16, cur_num_irq, q_params->num_qps);
		hinic3_qp_irq_change(netdev, dst_num_irq);
	}

out:
	netdev_dbg(netdev, "No need to change irqs, num_qps is %u\n",
		   q_params->num_qps);
}

static int hinic3_setup_num_qps(struct net_device *netdev)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);

	nic_dev->num_qp_irq = 0;

	nic_dev->qps_msix_entries = kcalloc(nic_dev->max_qps,
					    sizeof(struct msix_entry),
					    GFP_KERNEL);
	if (!nic_dev->qps_msix_entries)
		return -ENOMEM;

	hinic3_config_num_qps(netdev, &nic_dev->q_params);

	return 0;
}

static void hinic3_destroy_num_qps(struct net_device *netdev)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);
	u16 i;

	for (i = 0; i < nic_dev->num_qp_irq; i++)
		hinic3_free_irq(nic_dev->hwdev,
				nic_dev->qps_msix_entries[i].vector);

	kfree(nic_dev->qps_msix_entries);
}

static int hinic3_alloc_txrxq_resources(struct net_device *netdev,
					struct hinic3_dyna_txrxq_params *q_params)
{
	int err;

	q_params->txqs_res = kcalloc(q_params->num_qps,
				     sizeof(*q_params->txqs_res), GFP_KERNEL);
	if (!q_params->txqs_res)
		return -ENOMEM;

	q_params->rxqs_res = kcalloc(q_params->num_qps,
				     sizeof(*q_params->rxqs_res), GFP_KERNEL);
	if (!q_params->rxqs_res) {
		err = -ENOMEM;
		goto err_free_txqs_res_arr;
	}

	q_params->irq_cfg = kcalloc(q_params->num_qps,
				    sizeof(*q_params->irq_cfg), GFP_KERNEL);
	if (!q_params->irq_cfg) {
		err = -ENOMEM;
		goto err_free_rxqs_res_arr;
	}

	err = hinic3_alloc_txqs_res(netdev, q_params->num_qps,
				    q_params->sq_depth, q_params->txqs_res);
	if (err) {
		netdev_err(netdev, "Failed to alloc txqs resource\n");
		goto err_free_irq_cfg;
	}

	err = hinic3_alloc_rxqs_res(netdev, q_params->num_qps,
				    q_params->rq_depth, q_params->rxqs_res);
	if (err) {
		netdev_err(netdev, "Failed to alloc rxqs resource\n");
		goto err_free_txqs_res;
	}

	return 0;

err_free_txqs_res:
	hinic3_free_txqs_res(netdev, q_params->num_qps, q_params->sq_depth,
			     q_params->txqs_res);
err_free_irq_cfg:
	kfree(q_params->irq_cfg);
	q_params->irq_cfg = NULL;
err_free_rxqs_res_arr:
	kfree(q_params->rxqs_res);
	q_params->rxqs_res = NULL;
err_free_txqs_res_arr:
	kfree(q_params->txqs_res);
	q_params->txqs_res = NULL;

	return err;
}

static void hinic3_free_txrxq_resources(struct net_device *netdev,
					struct hinic3_dyna_txrxq_params *q_params)
{
	hinic3_free_rxqs_res(netdev, q_params->num_qps, q_params->rq_depth,
			     q_params->rxqs_res);
	hinic3_free_txqs_res(netdev, q_params->num_qps, q_params->sq_depth,
			     q_params->txqs_res);

	kfree(q_params->irq_cfg);
	q_params->irq_cfg = NULL;

	kfree(q_params->rxqs_res);
	q_params->rxqs_res = NULL;

	kfree(q_params->txqs_res);
	q_params->txqs_res = NULL;
}

static int hinic3_configure_txrxqs(struct net_device *netdev,
				   struct hinic3_dyna_txrxq_params *q_params)
{
	int err;

	err = hinic3_configure_txqs(netdev, q_params->num_qps,
				    q_params->sq_depth, q_params->txqs_res);
	if (err) {
		netdev_err(netdev, "Failed to configure txqs\n");
		return err;
	}

	err = hinic3_configure_rxqs(netdev, q_params->num_qps,
				    q_params->rq_depth, q_params->rxqs_res);
	if (err) {
		netdev_err(netdev, "Failed to configure rxqs\n");
		return err;
	}

	return 0;
}

static int hinic3_configure(struct net_device *netdev)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);
	int err;

	netdev->min_mtu = HINIC3_MIN_MTU_SIZE;
	netdev->max_mtu = HINIC3_MAX_JUMBO_FRAME_SIZE;
	err = hinic3_set_port_mtu(netdev, netdev->mtu);
	if (err) {
		netdev_err(netdev, "Failed to set mtu\n");
		return err;
	}

	/* Ensure DCB is disabled */
	hinic3_sync_dcb_state(nic_dev->hwdev, 1, 0);

	if (test_bit(HINIC3_RSS_ENABLE, &nic_dev->flags)) {
		err = hinic3_rss_init(netdev);
		if (err) {
			netdev_err(netdev, "Failed to init rss\n");
			return err;
		}
	}

	return 0;
}

static void hinic3_remove_configure(struct net_device *netdev)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);

	if (test_bit(HINIC3_RSS_ENABLE, &nic_dev->flags))
		hinic3_rss_uninit(netdev);
}

static int hinic3_alloc_channel_resources(struct net_device *netdev,
					  struct hinic3_dyna_qp_params *qp_params,
					  struct hinic3_dyna_txrxq_params *trxq_params)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);
	int err;

	qp_params->num_qps = trxq_params->num_qps;
	qp_params->sq_depth = trxq_params->sq_depth;
	qp_params->rq_depth = trxq_params->rq_depth;

	err = hinic3_alloc_qps(nic_dev, qp_params);
	if (err) {
		netdev_err(netdev, "Failed to alloc qps\n");
		return err;
	}

	err = hinic3_alloc_txrxq_resources(netdev, trxq_params);
	if (err) {
		netdev_err(netdev, "Failed to alloc txrxq resources\n");
		hinic3_free_qps(nic_dev, qp_params);
		return err;
	}

	return 0;
}

static void hinic3_free_channel_resources(struct net_device *netdev,
					  struct hinic3_dyna_qp_params *qp_params,
					  struct hinic3_dyna_txrxq_params *trxq_params)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);

	hinic3_free_txrxq_resources(netdev, trxq_params);
	hinic3_free_qps(nic_dev, qp_params);
}

static int hinic3_open_channel(struct net_device *netdev)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);
	int err;

	err = hinic3_init_qp_ctxts(nic_dev);
	if (err) {
		netdev_err(netdev, "Failed to init qps\n");
		return err;
	}

	err = hinic3_configure_txrxqs(netdev, &nic_dev->q_params);
	if (err) {
		netdev_err(netdev, "Failed to configure txrxqs\n");
		goto err_free_qp_ctxts;
	}

	err = hinic3_qps_irq_init(netdev);
	if (err) {
		netdev_err(netdev, "Failed to init txrxq irq\n");
		goto err_free_qp_ctxts;
	}

	err = hinic3_configure(netdev);
	if (err) {
		netdev_err(netdev, "Failed to init txrxq irq\n");
		goto err_uninit_qps_irq;
	}

	return 0;

err_uninit_qps_irq:
	hinic3_qps_irq_uninit(netdev);
err_free_qp_ctxts:
	hinic3_free_qp_ctxts(nic_dev);

	return err;
}

static void hinic3_close_channel(struct net_device *netdev)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);

	hinic3_remove_configure(netdev);
	hinic3_qps_irq_uninit(netdev);
	hinic3_free_qp_ctxts(nic_dev);
}

static int hinic3_vport_up(struct net_device *netdev)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);
	bool link_status_up;
	u16 glb_func_id;
	int err;

	glb_func_id = hinic3_global_func_id(nic_dev->hwdev);
	err = hinic3_set_vport_enable(nic_dev->hwdev, glb_func_id, true);
	if (err) {
		netdev_err(netdev, "Failed to enable vport\n");
		goto err_flush_qps_res;
	}

	err = netif_set_real_num_queues(netdev, nic_dev->q_params.num_qps,
					nic_dev->q_params.num_qps);
	if (err) {
		netdev_err(netdev, "Failed to set real number of queues\n");
		goto err_flush_qps_res;
	}
	netif_tx_start_all_queues(netdev);

	err = hinic3_get_link_status(nic_dev->hwdev, &link_status_up);
	if (!err && link_status_up)
		netif_carrier_on(netdev);

	return 0;

err_flush_qps_res:
	hinic3_flush_qps_res(nic_dev->hwdev);
	/* wait to guarantee that no packets will be sent to host */
	msleep(100);

	return err;
}

static void hinic3_vport_down(struct net_device *netdev)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);
	u16 glb_func_id;

	netif_carrier_off(netdev);
	netif_tx_disable(netdev);

	glb_func_id = hinic3_global_func_id(nic_dev->hwdev);
	hinic3_set_vport_enable(nic_dev->hwdev, glb_func_id, false);

	hinic3_flush_txqs(netdev);
	/* wait to guarantee that no packets will be sent to host */
	msleep(100);
	hinic3_flush_qps_res(nic_dev->hwdev);
}

static int hinic3_open(struct net_device *netdev)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);
	struct hinic3_dyna_qp_params qp_params;
	int err;

	err = hinic3_init_nicio_res(nic_dev);
	if (err) {
		netdev_err(netdev, "Failed to init nicio resources\n");
		return err;
	}

	err = hinic3_setup_num_qps(netdev);
	if (err) {
		netdev_err(netdev, "Failed to setup num_qps\n");
		goto err_free_nicio_res;
	}

	err = hinic3_alloc_channel_resources(netdev, &qp_params,
					     &nic_dev->q_params);
	if (err)
		goto err_destroy_num_qps;

	hinic3_init_qps(nic_dev, &qp_params);

	err = hinic3_open_channel(netdev);
	if (err)
		goto err_uninit_qps;

	err = hinic3_vport_up(netdev);
	if (err)
		goto err_close_channel;

	return 0;

err_close_channel:
	hinic3_close_channel(netdev);
err_uninit_qps:
	hinic3_uninit_qps(nic_dev, &qp_params);
	hinic3_free_channel_resources(netdev, &qp_params, &nic_dev->q_params);
err_destroy_num_qps:
	hinic3_destroy_num_qps(netdev);
err_free_nicio_res:
	hinic3_free_nicio_res(nic_dev);

	return err;
}

static int hinic3_close(struct net_device *netdev)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);
	struct hinic3_dyna_qp_params qp_params;

	hinic3_vport_down(netdev);
	hinic3_close_channel(netdev);
	hinic3_uninit_qps(nic_dev, &qp_params);
	hinic3_free_channel_resources(netdev, &qp_params, &nic_dev->q_params);

	return 0;
}

static int hinic3_change_mtu(struct net_device *netdev, int new_mtu)
{
	int err;

	err = hinic3_set_port_mtu(netdev, new_mtu);
	if (err) {
		netdev_err(netdev, "Failed to change port mtu to %d\n",
			   new_mtu);
		return err;
	}

	netdev_dbg(netdev, "Change mtu from %u to %d\n", netdev->mtu, new_mtu);
	WRITE_ONCE(netdev->mtu, new_mtu);

	return 0;
}

static int hinic3_set_mac_addr(struct net_device *netdev, void *addr)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);
	struct sockaddr *saddr = addr;
	int err;

	if (!is_valid_ether_addr(saddr->sa_data))
		return -EADDRNOTAVAIL;

	if (ether_addr_equal(netdev->dev_addr, saddr->sa_data))
		return 0;

	err = hinic3_update_mac(nic_dev->hwdev, netdev->dev_addr,
				saddr->sa_data, 0,
				hinic3_global_func_id(nic_dev->hwdev));

	if (err)
		return err;

	eth_hw_addr_set(netdev, saddr->sa_data);

	return 0;
}

static const struct net_device_ops hinic3_netdev_ops = {
	.ndo_open             = hinic3_open,
	.ndo_stop             = hinic3_close,
	.ndo_change_mtu       = hinic3_change_mtu,
	.ndo_set_mac_address  = hinic3_set_mac_addr,
	.ndo_start_xmit       = hinic3_xmit_frame,
};

void hinic3_set_netdev_ops(struct net_device *netdev)
{
	netdev->netdev_ops = &hinic3_netdev_ops;
}
