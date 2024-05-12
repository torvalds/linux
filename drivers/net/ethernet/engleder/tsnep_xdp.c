// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2022 Gerhard Engleder <gerhard@engleder-embedded.com> */

#include <linux/if_vlan.h>
#include <net/xdp_sock_drv.h>

#include "tsnep.h"

int tsnep_xdp_setup_prog(struct tsnep_adapter *adapter, struct bpf_prog *prog,
			 struct netlink_ext_ack *extack)
{
	struct bpf_prog *old_prog;

	old_prog = xchg(&adapter->xdp_prog, prog);
	if (old_prog)
		bpf_prog_put(old_prog);

	return 0;
}

static int tsnep_xdp_enable_pool(struct tsnep_adapter *adapter,
				 struct xsk_buff_pool *pool, u16 queue_id)
{
	struct tsnep_queue *queue;
	int retval;

	if (queue_id >= adapter->num_rx_queues ||
	    queue_id >= adapter->num_tx_queues)
		return -EINVAL;

	queue = &adapter->queue[queue_id];
	if (queue->rx->queue_index != queue_id ||
	    queue->tx->queue_index != queue_id) {
		netdev_err(adapter->netdev,
			   "XSK support only for TX/RX queue pairs\n");

		return -EOPNOTSUPP;
	}

	retval = xsk_pool_dma_map(pool, adapter->dmadev,
				  DMA_ATTR_SKIP_CPU_SYNC);
	if (retval) {
		netdev_err(adapter->netdev, "failed to map XSK pool\n");

		return retval;
	}

	retval = tsnep_enable_xsk(queue, pool);
	if (retval) {
		xsk_pool_dma_unmap(pool, DMA_ATTR_SKIP_CPU_SYNC);

		return retval;
	}

	return 0;
}

static int tsnep_xdp_disable_pool(struct tsnep_adapter *adapter, u16 queue_id)
{
	struct xsk_buff_pool *pool;
	struct tsnep_queue *queue;

	if (queue_id >= adapter->num_rx_queues ||
	    queue_id >= adapter->num_tx_queues)
		return -EINVAL;

	pool = xsk_get_pool_from_qid(adapter->netdev, queue_id);
	if (!pool)
		return -EINVAL;

	queue = &adapter->queue[queue_id];

	tsnep_disable_xsk(queue);

	xsk_pool_dma_unmap(pool, DMA_ATTR_SKIP_CPU_SYNC);

	return 0;
}

int tsnep_xdp_setup_pool(struct tsnep_adapter *adapter,
			 struct xsk_buff_pool *pool, u16 queue_id)
{
	return pool ? tsnep_xdp_enable_pool(adapter, pool, queue_id) :
		      tsnep_xdp_disable_pool(adapter, queue_id);
}
