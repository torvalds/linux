// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2018 Intel Corporation. */

#include <linux/bpf_trace.h>
#include <net/xdp_sock_drv.h>
#include <net/xdp.h>

#include "e1000_hw.h"
#include "igb.h"

static int igb_realloc_rx_buffer_info(struct igb_ring *ring, bool pool_present)
{
	int size = pool_present ?
		sizeof(*ring->rx_buffer_info_zc) * ring->count :
		sizeof(*ring->rx_buffer_info) * ring->count;
	void *buff_info = vmalloc(size);

	if (!buff_info)
		return -ENOMEM;

	if (pool_present) {
		vfree(ring->rx_buffer_info);
		ring->rx_buffer_info = NULL;
		ring->rx_buffer_info_zc = buff_info;
	} else {
		vfree(ring->rx_buffer_info_zc);
		ring->rx_buffer_info_zc = NULL;
		ring->rx_buffer_info = buff_info;
	}

	return 0;
}

static void igb_txrx_ring_disable(struct igb_adapter *adapter, u16 qid)
{
	struct igb_ring *tx_ring = adapter->tx_ring[qid];
	struct igb_ring *rx_ring = adapter->rx_ring[qid];
	struct e1000_hw *hw = &adapter->hw;

	set_bit(IGB_RING_FLAG_TX_DISABLED, &tx_ring->flags);

	wr32(E1000_TXDCTL(tx_ring->reg_idx), 0);
	wr32(E1000_RXDCTL(rx_ring->reg_idx), 0);

	synchronize_net();

	/* Rx/Tx share the same napi context. */
	napi_disable(&rx_ring->q_vector->napi);

	igb_clean_tx_ring(tx_ring);
	igb_clean_rx_ring(rx_ring);

	memset(&rx_ring->rx_stats, 0, sizeof(rx_ring->rx_stats));
	memset(&tx_ring->tx_stats, 0, sizeof(tx_ring->tx_stats));
}

static void igb_txrx_ring_enable(struct igb_adapter *adapter, u16 qid)
{
	struct igb_ring *tx_ring = adapter->tx_ring[qid];
	struct igb_ring *rx_ring = adapter->rx_ring[qid];

	igb_configure_tx_ring(adapter, tx_ring);
	igb_configure_rx_ring(adapter, rx_ring);

	synchronize_net();

	clear_bit(IGB_RING_FLAG_TX_DISABLED, &tx_ring->flags);

	/* call igb_desc_unused which always leaves
	 * at least 1 descriptor unused to make sure
	 * next_to_use != next_to_clean
	 */
	igb_alloc_rx_buffers(rx_ring, igb_desc_unused(rx_ring));

	/* Rx/Tx share the same napi context. */
	napi_enable(&rx_ring->q_vector->napi);
}

struct xsk_buff_pool *igb_xsk_pool(struct igb_adapter *adapter,
				   struct igb_ring *ring)
{
	int qid = ring->queue_index;
	struct xsk_buff_pool *pool;

	pool = xsk_get_pool_from_qid(adapter->netdev, qid);

	if (!igb_xdp_is_enabled(adapter))
		return NULL;

	return (pool && pool->dev) ? pool : NULL;
}

static int igb_xsk_pool_enable(struct igb_adapter *adapter,
			       struct xsk_buff_pool *pool,
			       u16 qid)
{
	struct net_device *netdev = adapter->netdev;
	struct igb_ring *rx_ring;
	bool if_running;
	int err;

	if (qid >= adapter->num_rx_queues)
		return -EINVAL;

	if (qid >= netdev->real_num_rx_queues ||
	    qid >= netdev->real_num_tx_queues)
		return -EINVAL;

	err = xsk_pool_dma_map(pool, &adapter->pdev->dev, IGB_RX_DMA_ATTR);
	if (err)
		return err;

	rx_ring = adapter->rx_ring[qid];
	if_running = netif_running(adapter->netdev) && igb_xdp_is_enabled(adapter);
	if (if_running)
		igb_txrx_ring_disable(adapter, qid);

	if (if_running) {
		err = igb_realloc_rx_buffer_info(rx_ring, true);
		if (!err) {
			igb_txrx_ring_enable(adapter, qid);
			/* Kick start the NAPI context so that receiving will start */
			err = igb_xsk_wakeup(adapter->netdev, qid, XDP_WAKEUP_RX);
		}

		if (err) {
			xsk_pool_dma_unmap(pool, IGB_RX_DMA_ATTR);
			return err;
		}
	}

	return 0;
}

static int igb_xsk_pool_disable(struct igb_adapter *adapter, u16 qid)
{
	struct xsk_buff_pool *pool;
	struct igb_ring *rx_ring;
	bool if_running;
	int err;

	pool = xsk_get_pool_from_qid(adapter->netdev, qid);
	if (!pool)
		return -EINVAL;

	rx_ring = adapter->rx_ring[qid];
	if_running = netif_running(adapter->netdev) && igb_xdp_is_enabled(adapter);
	if (if_running)
		igb_txrx_ring_disable(adapter, qid);

	xsk_pool_dma_unmap(pool, IGB_RX_DMA_ATTR);

	if (if_running) {
		err = igb_realloc_rx_buffer_info(rx_ring, false);
		if (err)
			return err;

		igb_txrx_ring_enable(adapter, qid);
	}

	return 0;
}

int igb_xsk_pool_setup(struct igb_adapter *adapter,
		       struct xsk_buff_pool *pool,
		       u16 qid)
{
	return pool ? igb_xsk_pool_enable(adapter, pool, qid) :
		igb_xsk_pool_disable(adapter, qid);
}

int igb_xsk_wakeup(struct net_device *dev, u32 qid, u32 flags)
{
	struct igb_adapter *adapter = netdev_priv(dev);
	struct e1000_hw *hw = &adapter->hw;
	struct igb_ring *ring;
	u32 eics = 0;

	if (test_bit(__IGB_DOWN, &adapter->state))
		return -ENETDOWN;

	if (!igb_xdp_is_enabled(adapter))
		return -EINVAL;

	if (qid >= adapter->num_tx_queues)
		return -EINVAL;

	ring = adapter->tx_ring[qid];

	if (test_bit(IGB_RING_FLAG_TX_DISABLED, &ring->flags))
		return -ENETDOWN;

	if (!READ_ONCE(ring->xsk_pool))
		return -EINVAL;

	if (!napi_if_scheduled_mark_missed(&ring->q_vector->napi)) {
		/* Cause software interrupt */
		if (adapter->flags & IGB_FLAG_HAS_MSIX) {
			eics |= ring->q_vector->eims_value;
			wr32(E1000_EICS, eics);
		} else {
			wr32(E1000_ICS, E1000_ICS_RXDMT0);
		}
	}

	return 0;
}
