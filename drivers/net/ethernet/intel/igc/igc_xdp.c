// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020, Intel Corporation. */

#include <linux/if_vlan.h>
#include <net/xdp_sock_drv.h>

#include "igc.h"
#include "igc_xdp.h"

int igc_xdp_set_prog(struct igc_adapter *adapter, struct bpf_prog *prog,
		     struct netlink_ext_ack *extack)
{
	struct net_device *dev = adapter->netdev;
	bool if_running = netif_running(dev);
	struct bpf_prog *old_prog;

	if (dev->mtu > ETH_DATA_LEN) {
		/* For now, the driver doesn't support XDP functionality with
		 * jumbo frames so we return error.
		 */
		NL_SET_ERR_MSG_MOD(extack, "Jumbo frames not supported");
		return -EOPNOTSUPP;
	}

	if (if_running)
		igc_close(dev);

	old_prog = xchg(&adapter->xdp_prog, prog);
	if (old_prog)
		bpf_prog_put(old_prog);

	if (prog)
		xdp_features_set_redirect_target(dev, true);
	else
		xdp_features_clear_redirect_target(dev);

	if (if_running)
		igc_open(dev);

	return 0;
}

static int igc_xdp_enable_pool(struct igc_adapter *adapter,
			       struct xsk_buff_pool *pool, u16 queue_id)
{
	struct net_device *ndev = adapter->netdev;
	struct device *dev = &adapter->pdev->dev;
	struct igc_ring *rx_ring, *tx_ring;
	struct napi_struct *napi;
	bool needs_reset;
	u32 frame_size;
	int err;

	if (queue_id >= adapter->num_rx_queues ||
	    queue_id >= adapter->num_tx_queues)
		return -EINVAL;

	frame_size = xsk_pool_get_rx_frame_size(pool);
	if (frame_size < ETH_FRAME_LEN + VLAN_HLEN * 2) {
		/* When XDP is enabled, the driver doesn't support frames that
		 * span over multiple buffers. To avoid that, we check if xsk
		 * frame size is big enough to fit the max ethernet frame size
		 * + vlan double tagging.
		 */
		return -EOPNOTSUPP;
	}

	err = xsk_pool_dma_map(pool, dev, IGC_RX_DMA_ATTR);
	if (err) {
		netdev_err(ndev, "Failed to map xsk pool\n");
		return err;
	}

	needs_reset = netif_running(adapter->netdev) && igc_xdp_is_enabled(adapter);

	rx_ring = adapter->rx_ring[queue_id];
	tx_ring = adapter->tx_ring[queue_id];
	/* Rx and Tx rings share the same napi context. */
	napi = &rx_ring->q_vector->napi;

	if (needs_reset) {
		igc_disable_rx_ring(rx_ring);
		igc_disable_tx_ring(tx_ring);
		napi_disable(napi);
	}

	set_bit(IGC_RING_FLAG_AF_XDP_ZC, &rx_ring->flags);
	set_bit(IGC_RING_FLAG_AF_XDP_ZC, &tx_ring->flags);

	if (needs_reset) {
		napi_enable(napi);
		igc_enable_rx_ring(rx_ring);
		igc_enable_tx_ring(tx_ring);

		err = igc_xsk_wakeup(ndev, queue_id, XDP_WAKEUP_RX);
		if (err) {
			xsk_pool_dma_unmap(pool, IGC_RX_DMA_ATTR);
			return err;
		}
	}

	return 0;
}

static int igc_xdp_disable_pool(struct igc_adapter *adapter, u16 queue_id)
{
	struct igc_ring *rx_ring, *tx_ring;
	struct xsk_buff_pool *pool;
	struct napi_struct *napi;
	bool needs_reset;

	if (queue_id >= adapter->num_rx_queues ||
	    queue_id >= adapter->num_tx_queues)
		return -EINVAL;

	pool = xsk_get_pool_from_qid(adapter->netdev, queue_id);
	if (!pool)
		return -EINVAL;

	needs_reset = netif_running(adapter->netdev) && igc_xdp_is_enabled(adapter);

	rx_ring = adapter->rx_ring[queue_id];
	tx_ring = adapter->tx_ring[queue_id];
	/* Rx and Tx rings share the same napi context. */
	napi = &rx_ring->q_vector->napi;

	if (needs_reset) {
		igc_disable_rx_ring(rx_ring);
		igc_disable_tx_ring(tx_ring);
		napi_disable(napi);
	}

	xsk_pool_dma_unmap(pool, IGC_RX_DMA_ATTR);
	clear_bit(IGC_RING_FLAG_AF_XDP_ZC, &rx_ring->flags);
	clear_bit(IGC_RING_FLAG_AF_XDP_ZC, &tx_ring->flags);

	if (needs_reset) {
		napi_enable(napi);
		igc_enable_rx_ring(rx_ring);
		igc_enable_tx_ring(tx_ring);
	}

	return 0;
}

int igc_xdp_setup_pool(struct igc_adapter *adapter, struct xsk_buff_pool *pool,
		       u16 queue_id)
{
	return pool ? igc_xdp_enable_pool(adapter, pool, queue_id) :
		      igc_xdp_disable_pool(adapter, queue_id);
}
