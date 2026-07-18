// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Alibaba Elastic Ethernet Adapter.
 *
 * Copyright (C) 2025 Alibaba Inc.
 */

#include <linux/etherdevice.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <net/netdev_queues.h>

#include "eea_adminq.h"
#include "eea_net.h"
#include "eea_pci.h"
#include "eea_ring.h"

#define EEA_SPLIT_HDR_SIZE ALIGN(128, L1_CACHE_BYTES)

static irqreturn_t eea_irq_handler(int irq, void *data)
{
	struct eea_irq_blk *blk = data;

	napi_schedule_irqoff(&blk->napi);

	return IRQ_HANDLED;
}

static void eea_free_irq_blk(struct eea_net *enet)
{
	struct eea_irq_blk *blk;
	u32 num;
	int i;

	if (!enet->irq_blks)
		return;

	num = enet->edev->rx_num;

	for (i = 0; i < num; i++) {
		blk = &enet->irq_blks[i];

		if (blk->ready)
			eea_pci_free_irq(blk);

		blk->ready = false;
	}

	kvfree(enet->irq_blks);
	enet->irq_blks = NULL;
}

/* The driver will always attempt to allocate IRQ blocks based on the maximum
 * possible queue num.
 */
static int eea_alloc_irq_blks(struct eea_net *enet)
{
	struct eea_device *edev = enet->edev;
	struct eea_irq_blk *blk, *irq_blks;
	int i, err, num;

	num = enet->edev->rx_num;

	irq_blks = kvcalloc(num, sizeof(*blk), GFP_KERNEL);
	if (!irq_blks)
		return -ENOMEM;

	enet->irq_blks = irq_blks;

	for (i = 0; i < num; i++) {
		blk = &irq_blks[i];
		blk->idx = i;

		/* vec 0 is for error notify. */
		blk->msix_vec = i + 1;

		err = eea_pci_request_irq(edev, blk, eea_irq_handler);
		if (err)
			goto err_free_irq_blk;

		blk->ready = true;
	}

	return 0;

err_free_irq_blk:
	eea_free_irq_blk(enet);
	return err;
}

static int eea_update_queues(struct eea_net *enet)
{
	return netif_set_real_num_queues(enet->netdev, enet->cfg.tx_ring_num,
					 enet->cfg.rx_ring_num);
}

void eea_init_ctx(struct eea_net *enet, struct eea_net_init_ctx *ctx)
{
	memset(ctx, 0, sizeof(*ctx));

	ctx->netdev = enet->netdev;
	ctx->edev = enet->edev;
	ctx->cfg = enet->cfg;
}

static void eea_bind_q_and_cfg(struct eea_net *enet,
			       struct eea_net_init_ctx *ctx)
{
	struct eea_irq_blk *blk;
	struct eea_net_rx *rx;
	struct eea_net_tx *tx;
	int i;

	/* Since 'ndo_get_stats64' is not called in softirq context, there is no
	 * need to use 'spin_lock_bh'.
	 */
	spin_lock(&enet->stats_lock);

	enet->cfg = ctx->cfg;
	enet->rx = ctx->rx;
	enet->tx = ctx->tx;

	for (i = 0; i < ctx->cfg.rx_ring_num; i++) {
		blk = &enet->irq_blks[i];

		rx = ctx->rx[i];
		tx = &ctx->tx[i];

		rx->enet = enet;
		rx->napi = &blk->napi;
		rx->ering->msix_vec = blk->msix_vec;

		tx->enet = enet;
		tx->ering->msix_vec = blk->msix_vec;

		blk->rx = rx;
	}

	spin_unlock(&enet->stats_lock);
}

static void eea_unbind_q_and_cfg(struct eea_net *enet,
				 struct eea_net_init_ctx *ctx)
{
	struct eea_irq_blk *blk;
	struct eea_net_rx *rx;
	int i;

	spin_lock(&enet->stats_lock);

	ctx->cfg = enet->cfg;
	ctx->rx = enet->rx;
	ctx->tx = enet->tx;

	enet->rx = NULL;
	enet->tx = NULL;

	for (i = 0; i < ctx->cfg.rx_ring_num; i++) {
		blk = &enet->irq_blks[i];

		rx = ctx->rx[i];

		rx->napi = NULL;

		blk->rx = NULL;
	}

	spin_unlock(&enet->stats_lock);
}

static void eea_free_rxtx_q_mem(struct eea_net_init_ctx *ctx)
{
	struct eea_net_rx *rx;
	struct eea_net_tx *tx;
	int i;

	for (i = 0; i < ctx->cfg.rx_ring_num; i++) {
		rx = ctx->rx[i];
		tx = &ctx->tx[i];

		eea_free_rx(rx, &ctx->cfg);
		eea_free_tx(tx, &ctx->cfg);
	}

	kvfree(ctx->rx);
	kvfree(ctx->tx);
}

/* alloc tx/rx: struct, ring, meta, pp, napi */
static int eea_alloc_rxtx_q_mem(struct eea_net_init_ctx *ctx)
{
	struct eea_net_rx *rx;
	struct eea_net_tx *tx;
	int err, i;

	ctx->tx = kvcalloc(ctx->cfg.tx_ring_num, sizeof(*ctx->tx), GFP_KERNEL);
	if (!ctx->tx)
		return -ENOMEM;

	ctx->rx = kvcalloc(ctx->cfg.rx_ring_num, sizeof(*ctx->rx), GFP_KERNEL);
	if (!ctx->rx)
		goto err_free_tx;

	ctx->cfg.rx_sq_desc_size = sizeof(struct eea_rx_desc);
	ctx->cfg.rx_cq_desc_size = sizeof(struct eea_rx_cdesc);
	ctx->cfg.tx_sq_desc_size = sizeof(struct eea_tx_desc);
	ctx->cfg.tx_cq_desc_size = sizeof(struct eea_tx_cdesc);

	/* ethtool may config this. */
	if (!ctx->cfg.split_hdr)
		ctx->cfg.rx_sq_desc_size = sizeof(struct eea_rx_desc_no_hdr);

	for (i = 0; i < ctx->cfg.rx_ring_num; i++) {
		rx = eea_alloc_rx(ctx, i);
		if (!rx)
			goto err_free;

		ctx->rx[i] = rx;

		tx = ctx->tx + i;
		err = eea_alloc_tx(ctx, tx, i);
		if (err)
			goto err_free;
	}

	return 0;

err_free:
	for (i = 0; i < ctx->cfg.rx_ring_num; i++) {
		rx = ctx->rx[i];
		tx = ctx->tx + i;

		eea_free_rx(rx, &ctx->cfg);
		eea_free_tx(tx, &ctx->cfg);
	}

	kvfree(ctx->rx);

err_free_tx:
	kvfree(ctx->tx);
	return -ENOMEM;
}

static int eea_hw_active_ring(struct eea_net *enet)
{
	return eea_adminq_create_q(enet, enet->cfg.rx_ring_num
				   + enet->cfg.tx_ring_num, 0);
}

static int eea_hw_unactive_ring(struct eea_net *enet)
{
	int err;

	err = eea_adminq_destroy_all_q(enet);
	if (err)
		netdev_warn(enet->netdev, "unactive rxtx ring failed.\n");

	return err;
}

/* stop rx napi, stop tx queue. */
static void eea_stop_rxtx(struct net_device *netdev)
{
	struct eea_net *enet = netdev_priv(netdev);
	int i;

	netif_tx_disable(netdev);

	for (i = 0; i < enet->cfg.rx_ring_num; i++)
		enet_rx_stop(enet->rx[i]);

	netif_carrier_off(netdev);
}

static void eea_start_rxtx(struct eea_net *enet)
{
	int i;

	for (i = 0; i < enet->cfg.rx_ring_num; i++)
		enet_rx_start(enet->rx[i]);

	netif_tx_start_all_queues(enet->netdev);
	netif_carrier_on(enet->netdev);

	enet->started = true;
}

static int eea_netdev_stop(struct net_device *netdev)
{
	struct eea_net *enet = netdev_priv(netdev);
	struct eea_net_init_ctx ctx;

	/* This function can be called during device anomaly recovery. To
	 * prevent duplicate stop operations, the `started` flag is introduced
	 * for checking.
	 */

	if (!enet->started) {
		netdev_warn(netdev, "eea netdev stop: but dev is not started.\n");
		return 0;
	}

	eea_init_ctx(enet, &ctx);

	eea_stop_rxtx(netdev);
	eea_hw_unactive_ring(enet);
	eea_unbind_q_and_cfg(enet, &ctx);
	eea_free_rxtx_q_mem(&ctx);

	enet->started = false;

	return 0;
}

static int eea_netdev_open(struct net_device *netdev)
{
	struct eea_net *enet = netdev_priv(netdev);
	struct eea_net_init_ctx ctx;
	int err;

	if (enet->link_err) {
		netdev_err(netdev, "netdev open err, because link error: %d\n",
			   enet->link_err);
		return -EBUSY;
	}

	eea_init_ctx(enet, &ctx);

	err = eea_alloc_rxtx_q_mem(&ctx);
	if (err)
		goto err_done;

	eea_bind_q_and_cfg(enet, &ctx);

	err = eea_update_queues(enet);
	if (err)
		goto err_free_q;

	err = eea_hw_active_ring(enet);
	if (err)
		goto err_free_q;

	eea_start_rxtx(enet);

	return 0;

err_free_q:
	eea_unbind_q_and_cfg(enet, &ctx);
	eea_free_rxtx_q_mem(&ctx);

err_done:
	return err;
}

/* Statistics may be reset to zero upon device reset. This is expected behavior
 * for now and will be addressed in the future.
 */
static void eea_stats(struct net_device *netdev, struct rtnl_link_stats64 *tot)
{
	struct eea_net *enet = netdev_priv(netdev);
	u64 packets, bytes, drop, lerr;
	u32 start;
	int i;

	spin_lock(&enet->stats_lock);

	if (enet->rx) {
		for (i = 0; i < enet->cfg.rx_ring_num; i++) {
			struct eea_net_rx *rx = enet->rx[i];

			do {
				start = u64_stats_fetch_begin(&rx->stats.syncp);
				packets = u64_stats_read(&rx->stats.packets);
				bytes = u64_stats_read(&rx->stats.bytes);
				drop = u64_stats_read(&rx->stats.drops);
				lerr = u64_stats_read(&rx->stats.length_errors);
			} while (u64_stats_fetch_retry(&rx->stats.syncp,
						       start));

			tot->rx_packets       += packets;
			tot->rx_bytes         += bytes;
			tot->rx_dropped       += drop;
			tot->rx_length_errors += lerr;
			tot->rx_errors        += lerr;
		}
	}

	if (enet->tx) {
		for (i = 0; i < enet->cfg.tx_ring_num; i++) {
			struct eea_net_tx *tx = &enet->tx[i];

			do {
				start = u64_stats_fetch_begin(&tx->stats.syncp);
				packets = u64_stats_read(&tx->stats.packets);
				bytes = u64_stats_read(&tx->stats.bytes);
				drop = u64_stats_read(&tx->stats.drops);
			} while (u64_stats_fetch_retry(&tx->stats.syncp,
						       start));

			tot->tx_packets += packets;
			tot->tx_bytes   += bytes;
			tot->tx_dropped += drop;
		}
	}

	spin_unlock(&enet->stats_lock);
}

/* resources: ring, buffers, irq */
int eea_reset_hw_resources(struct eea_net *enet, struct eea_net_init_ctx *ctx)
{
	struct eea_net_init_ctx ctx_old = {0};
	int err, error;

	if (!netif_running(enet->netdev) || !enet->started) {
		spin_lock(&enet->stats_lock);
		enet->cfg = ctx->cfg;
		spin_unlock(&enet->stats_lock);
		return 0;
	}

	err = eea_alloc_rxtx_q_mem(ctx);
	if (err) {
		netdev_warn(enet->netdev,
			    "eea reset: alloc q failed. stop reset. err %d\n",
			    err);
		return err;
	}

	eea_stop_rxtx(enet->netdev);
	eea_hw_unactive_ring(enet);

	eea_unbind_q_and_cfg(enet, &ctx_old);
	eea_bind_q_and_cfg(enet, ctx);

	err = eea_update_queues(enet);
	if (err) {
		netdev_err(enet->netdev,
			   "eea reset: set real num queues failed. err %d\n",
			   err);
		goto err_bind_old;
	}

	err = eea_hw_active_ring(enet);
	if (err) {
		netdev_err(enet->netdev, "eea reset: active new ring. err %d\n",
			   err);
		eea_unbind_q_and_cfg(enet, ctx);
		goto err_free_q;
	}

	eea_start_rxtx(enet);
	eea_free_rxtx_q_mem(&ctx_old);
	return 0;

err_bind_old:
	eea_unbind_q_and_cfg(enet, ctx);
	eea_bind_q_and_cfg(enet, &ctx_old);
	error = eea_hw_active_ring(enet);
	if (error) {
		netdev_err(enet->netdev, "eea reset: active old ring. err %d\n",
			   error);
		eea_unbind_q_and_cfg(enet, &ctx_old);
		err = error;
		goto err_free_q;
	}

	eea_start_rxtx(enet);
	eea_free_rxtx_q_mem(ctx);
	return err;

err_free_q:

	/* An exception occurred at the hardware level, and there's not much we
	 * can do about it -- we can only release the resources first.
	 */
	eea_free_rxtx_q_mem(ctx);
	eea_free_rxtx_q_mem(&ctx_old);
	enet->started = false;
	return err;
}

int eea_queues_check_and_reset(struct eea_device *edev)
{
	struct eea_aq_dev_status dstatus = {0};
	struct eea_aq_queue_status *qstatus;
	struct eea_aq_queue_status *qs;
	struct eea_net_init_ctx ctx;
	bool need_reset = false;
	int i, err = 0;

	rtnl_lock();

	if (!netif_running(edev->enet->netdev))
		goto err_unlock;

	/* Maybe stopped by ha. */
	if (!edev->enet->started || edev->enet->link_err)
		goto err_unlock;

	err = eea_adminq_dev_status(edev->enet, &dstatus);
	if (err) {
		netdev_warn(edev->enet->netdev, "query queue status failed.\n");
		goto err_unlock;
	}

	if (le16_to_cpu(dstatus.status->link_status) == EEA_LINK_DOWN_STATUS) {
		/* The device is broken, can not be up. */
		eea_netdev_stop(edev->enet->netdev);
		edev->enet->link_err = EEA_LINK_ERR_LINK_DOWN;
		netdev_warn(edev->enet->netdev, "device link is down. stop device.\n");
		goto err_free;
	}

	qstatus = dstatus.status->q_status;

	for (i = 0; i < dstatus.num; ++i) {
		qs = &qstatus[i];

		if (le16_to_cpu(qs->status) == EEA_QUEUE_STATUS_NEED_RESET) {
			netdev_warn(edev->enet->netdev,
				    "queue status: queue %u needs to reset\n",
				    le16_to_cpu(qs->qidx));
			need_reset = true;
		}
	}

	if (need_reset) {
		eea_init_ctx(edev->enet, &ctx);
		err = eea_reset_hw_resources(edev->enet, &ctx);
	}

err_free:
	kfree(dstatus.status);

err_unlock:
	rtnl_unlock();
	return err;
}

static int eea_update_cfg(struct eea_net *enet,
			  struct eea_device *edev,
			  struct eea_aq_cfg *hwcfg)
{
	u32 rx_max = le32_to_cpu(hwcfg->rx_depth_max);
	u32 tx_max = le32_to_cpu(hwcfg->tx_depth_max);
	u32 rx_def = le32_to_cpu(hwcfg->rx_depth_def);
	u32 tx_def = le32_to_cpu(hwcfg->tx_depth_def);

	/* Now, we assert that the rx ring num is equal to the tx ring num. */
	if (edev->rx_num != edev->tx_num) {
		dev_err(edev->dma_dev, "Inconsistent ring num: RX %u, TX %u\n",
			edev->rx_num, edev->tx_num);
		return -EINVAL;
	}

	if (rx_max > EEA_NET_IO_HW_RING_DEPTH_MAX ||
	    rx_max < EEA_NET_IO_HW_RING_DEPTH_MIN ||
	    tx_max > EEA_NET_IO_HW_RING_DEPTH_MAX ||
	    tx_max < EEA_NET_IO_HW_RING_DEPTH_MIN) {
		dev_err(edev->dma_dev, "Invalid HW max depth: RX %u, TX %u\n",
			rx_max, tx_max);
		return -EINVAL;
	}

	if (rx_def > rx_max ||
	    tx_def > tx_max ||
	    rx_def < EEA_NET_IO_HW_RING_DEPTH_MIN ||
	    tx_def < EEA_NET_IO_HW_RING_DEPTH_MIN) {
		dev_err(edev->dma_dev, "Invalid default depth: RX %u (max %u), TX %u (max %u)\n",
			rx_def, rx_max, tx_def, tx_max);
		return -EINVAL;
	}

	if (!is_power_of_2(rx_max) || !is_power_of_2(tx_max) ||
	    !is_power_of_2(rx_def) || !is_power_of_2(tx_def)) {
		dev_err(edev->dma_dev, "Ring depth must be power of 2\n");
		return -EINVAL;
	}

	enet->cfg_hw.rx_ring_depth = rx_max;
	enet->cfg_hw.tx_ring_depth = tx_max;
	enet->cfg_hw.rx_ring_num = edev->rx_num;
	enet->cfg_hw.tx_ring_num = edev->tx_num;
	enet->cfg_hw.split_hdr = EEA_SPLIT_HDR_SIZE;

	enet->cfg.rx_ring_depth = rx_def;
	enet->cfg.tx_ring_depth = tx_def;
	enet->cfg.rx_ring_num = edev->rx_num;
	enet->cfg.tx_ring_num = edev->tx_num;

	return 0;
}

static int eea_netdev_init_features(struct net_device *netdev,
				    struct eea_net *enet,
				    struct eea_device *edev)
{
	struct eea_aq_cfg *cfg;
	int err;
	u32 mtu;

	cfg = kzalloc(sizeof(*cfg), GFP_KERNEL);
	if (!cfg)
		return -ENOMEM;

	err = eea_adminq_query_cfg(enet, cfg);
	if (err)
		goto err_free;

	mtu = le16_to_cpu(cfg->mtu);
	if (mtu < ETH_MIN_MTU) {
		dev_err(edev->dma_dev, "The device gave us an invalid MTU. Here we can only exit the initialization. %u < %u\n",
			mtu, ETH_MIN_MTU);
		err = -EINVAL;
		goto err_free;
	}

	err = eea_update_cfg(enet, edev, cfg);
	if (err)
		goto err_free;

	netdev->priv_flags |= IFF_UNICAST_FLT;
	netdev->priv_flags |= IFF_LIVE_ADDR_CHANGE;

	netdev->hw_features |= NETIF_F_HW_CSUM;
	netdev->hw_features |= NETIF_F_GRO_HW;
	netdev->hw_features |= NETIF_F_SG;
	netdev->hw_features |= NETIF_F_TSO;
	netdev->hw_features |= NETIF_F_TSO_ECN;
	netdev->hw_features |= NETIF_F_TSO6;
	netdev->hw_features |= NETIF_F_GSO_UDP_L4;

	netdev->features |= NETIF_F_HIGHDMA;
	netdev->features |= NETIF_F_HW_CSUM;
	netdev->features |= NETIF_F_SG;
	netdev->features |= NETIF_F_GSO_ROBUST;
	netdev->features |= netdev->hw_features & NETIF_F_ALL_TSO;
	netdev->features |= NETIF_F_RXCSUM;
	netdev->features |= NETIF_F_GRO_HW;

	netdev->vlan_features = netdev->features;

	if (!is_valid_ether_addr(cfg->mac)) {
		dev_err(edev->dma_dev, "The device gave invalid mac %pM\n",
			cfg->mac);
		err = -EINVAL;
		goto err_free;
	}

	eth_hw_addr_set(netdev, cfg->mac);

	enet->speed = SPEED_UNKNOWN;
	enet->duplex = DUPLEX_UNKNOWN;

	netdev->min_mtu = ETH_MIN_MTU;

	netdev->mtu = mtu;

	/* If jumbo frames are already enabled, then the returned MTU will be a
	 * jumbo MTU, and the driver will automatically enable jumbo frame
	 * support by default.
	 */
	netdev->max_mtu = mtu;

err_free:
	kfree(cfg);
	return err;
}

static const struct net_device_ops eea_netdev = {
	.ndo_open           = eea_netdev_open,
	.ndo_stop           = eea_netdev_stop,
	.ndo_start_xmit     = eea_tx_xmit,
	.ndo_validate_addr  = eth_validate_addr,
	.ndo_get_stats64    = eea_stats,
	.ndo_features_check = passthru_features_check,
};

static struct eea_net *eea_netdev_alloc(struct eea_device *edev, u32 pairs)
{
	struct net_device *netdev;
	struct eea_net *enet;
	int err;

	netdev = alloc_etherdev_mq(sizeof(struct eea_net), pairs);
	if (!netdev) {
		dev_err(edev->dma_dev,
			"alloc_etherdev_mq failed with pairs %d\n", pairs);
		return NULL;
	}

	netdev->netdev_ops = &eea_netdev;
	netdev->ethtool_ops = &eea_ethtool_ops;
	SET_NETDEV_DEV(netdev, edev->dma_dev);

	enet = netdev_priv(netdev);
	enet->netdev = netdev;
	enet->edev = edev;
	edev->enet = enet;

	err = eea_alloc_irq_blks(enet);
	if (err) {
		dev_err(edev->dma_dev,
			"eea_alloc_irq_blks failed with pairs %d\n", pairs);
		free_netdev(netdev);
		return NULL;
	}

	spin_lock_init(&enet->stats_lock);

	return enet;
}

static void eea_update_ts_off(struct eea_device *edev, struct eea_net *enet)
{
	u64 ts;

	ts = eea_pci_device_ts(edev);

	enet->hw_ts_offset = ktime_get_real() - ts;
}

static int eea_net_reprobe(struct eea_device *edev)
{
	struct eea_net *enet = edev->enet;
	int err = 0;

	enet->edev = edev;

	if (!enet->adminq.ring) {
		err = eea_create_adminq(enet, edev->rx_num + edev->tx_num);
		if (err)
			return err;
	}

	err = eea_alloc_irq_blks(enet);
	if (err)
		goto err_destroy_aq;

	eea_update_ts_off(edev, enet);

	rtnl_lock();

	enet->link_err = 0;
	if (edev->ha_reset_netdev_running &&
	    netif_running(edev->enet->netdev)) {
		err = eea_netdev_open(enet->netdev);
		if (err) {
			enet->link_err = EEA_LINK_ERR_HA_RESET_DEV;
			rtnl_unlock();
			goto err_free_irq_blks;
		}
	}

	rtnl_unlock();

	enet->wait_pci_ready = false;
	return 0;

err_free_irq_blks:
	eea_free_irq_blk(enet);

err_destroy_aq:
	eea_destroy_adminq(enet);

	return err;
}

int eea_net_probe(struct eea_device *edev)
{
	struct eea_net *enet;
	int err = -ENOMEM;

	/* If edev->enet is not null, then this is called from ha reset worker.
	 * Call eea_net_reprobe() directly.
	 */
	if (edev->enet)
		return eea_net_reprobe(edev);

	enet = eea_netdev_alloc(edev, edev->rx_num);
	if (!enet)
		return -ENOMEM;

	err = eea_create_adminq(enet, edev->rx_num + edev->tx_num);
	if (err)
		goto err_free_netdev;

	eea_adminq_config_host_info(enet);

	err = eea_netdev_init_features(enet->netdev, enet, edev);
	if (err)
		goto err_reset_dev;

	eea_update_ts_off(edev, enet);

	netif_carrier_off(enet->netdev);

	err = register_netdev(enet->netdev);
	if (err)
		goto err_reset_dev;

	netdev_dbg(enet->netdev, "eea probe success.\n");

	return 0;

err_reset_dev:
	eea_device_reset(edev);
	eea_destroy_adminq(enet);

err_free_netdev:
	eea_free_irq_blk(enet);
	free_netdev(enet->netdev);
	return err;
}

static void eea_net_ha_reset_remove(struct eea_net *enet,
				    struct eea_device *edev)
{
	rtnl_lock();
	edev->ha_reset_netdev_running = false;
	if (netif_running(enet->netdev)) {
		eea_netdev_stop(enet->netdev);
		edev->ha_reset_netdev_running = true;
	}

	/* Prevent that the user set up the net device. */
	enet->link_err = EEA_LINK_ERR_HA_RESET_DEV;

	rtnl_unlock();

	eea_device_reset(edev);
	eea_destroy_adminq(enet);
	eea_free_irq_blk(enet);

	enet->wait_pci_ready = true;
}

void eea_net_remove(struct eea_device *edev, bool ha)
{
	struct net_device *netdev;
	struct eea_net *enet;

	enet = edev->enet;
	netdev = enet->netdev;

	if (ha) {
		if (enet->wait_pci_ready)
			return;

		eea_net_ha_reset_remove(enet, edev);
		return;
	}

	unregister_netdev(netdev);

	if (!enet->wait_pci_ready) {
		eea_device_reset(edev);
		eea_destroy_adminq(enet);
		eea_free_irq_blk(enet);
	}

	free_netdev(netdev);
}

void eea_net_shutdown(struct eea_device *edev)
{
	struct net_device *netdev;
	struct eea_net *enet;

	enet = edev->enet;
	netdev = enet->netdev;

	rtnl_lock();

	netif_device_detach(netdev);
	dev_close(netdev);

	if (!enet->wait_pci_ready) {
		eea_device_reset(edev);
		eea_destroy_adminq(enet);
		eea_free_irq_blk(enet);
	}

	rtnl_unlock();
}
