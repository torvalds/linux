// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright 2015-2021 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#include "ena_xdp.h"

static int validate_xdp_req_id(struct ena_ring *tx_ring, u16 req_id)
{
	struct ena_tx_buffer *tx_info;

	tx_info = &tx_ring->tx_buffer_info[req_id];
	if (likely(tx_info->xdpf))
		return 0;

	return handle_invalid_req_id(tx_ring, req_id, tx_info, true);
}

static int ena_xdp_tx_map_frame(struct ena_ring *tx_ring,
				struct ena_tx_buffer *tx_info,
				struct xdp_frame *xdpf,
				struct ena_com_tx_ctx *ena_tx_ctx)
{
	struct ena_adapter *adapter = tx_ring->adapter;
	struct ena_com_buf *ena_buf;
	int push_len = 0;
	dma_addr_t dma;
	void *data;
	u32 size;

	tx_info->xdpf = xdpf;
	data = tx_info->xdpf->data;
	size = tx_info->xdpf->len;

	if (tx_ring->tx_mem_queue_type == ENA_ADMIN_PLACEMENT_POLICY_DEV) {
		/* Designate part of the packet for LLQ */
		push_len = min_t(u32, size, tx_ring->tx_max_header_size);

		ena_tx_ctx->push_header = data;

		size -= push_len;
		data += push_len;
	}

	ena_tx_ctx->header_len = push_len;

	if (size > 0) {
		dma = dma_map_single(tx_ring->dev,
				     data,
				     size,
				     DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(tx_ring->dev, dma)))
			goto error_report_dma_error;

		tx_info->map_linear_data = 0;

		ena_buf = tx_info->bufs;
		ena_buf->paddr = dma;
		ena_buf->len = size;

		ena_tx_ctx->ena_bufs = ena_buf;
		ena_tx_ctx->num_bufs = tx_info->num_of_bufs = 1;
	}

	return 0;

error_report_dma_error:
	ena_increase_stat(&tx_ring->tx_stats.dma_mapping_err, 1,
			  &tx_ring->syncp);
	netif_warn(adapter, tx_queued, adapter->netdev, "Failed to map xdp buff\n");

	return -EINVAL;
}

int ena_xdp_xmit_frame(struct ena_ring *tx_ring,
		       struct ena_adapter *adapter,
		       struct xdp_frame *xdpf,
		       int flags)
{
	struct ena_com_tx_ctx ena_tx_ctx = {};
	struct ena_tx_buffer *tx_info;
	u16 next_to_use, req_id;
	int rc;

	next_to_use = tx_ring->next_to_use;
	req_id = tx_ring->free_ids[next_to_use];
	tx_info = &tx_ring->tx_buffer_info[req_id];
	tx_info->num_of_bufs = 0;

	rc = ena_xdp_tx_map_frame(tx_ring, tx_info, xdpf, &ena_tx_ctx);
	if (unlikely(rc))
		return rc;

	ena_tx_ctx.req_id = req_id;

	rc = ena_xmit_common(adapter,
			     tx_ring,
			     tx_info,
			     &ena_tx_ctx,
			     next_to_use,
			     xdpf->len);
	if (rc)
		goto error_unmap_dma;

	/* trigger the dma engine. ena_ring_tx_doorbell()
	 * calls a memory barrier inside it.
	 */
	if (flags & XDP_XMIT_FLUSH)
		ena_ring_tx_doorbell(tx_ring);

	return rc;

error_unmap_dma:
	ena_unmap_tx_buff(tx_ring, tx_info);
	tx_info->xdpf = NULL;
	return rc;
}

int ena_xdp_xmit(struct net_device *dev, int n,
		 struct xdp_frame **frames, u32 flags)
{
	struct ena_adapter *adapter = netdev_priv(dev);
	struct ena_ring *tx_ring;
	int qid, i, nxmit = 0;

	if (unlikely(flags & ~XDP_XMIT_FLAGS_MASK))
		return -EINVAL;

	if (!test_bit(ENA_FLAG_DEV_UP, &adapter->flags))
		return -ENETDOWN;

	/* We assume that all rings have the same XDP program */
	if (!READ_ONCE(adapter->rx_ring->xdp_bpf_prog))
		return -ENXIO;

	qid = smp_processor_id() % adapter->xdp_num_queues;
	qid += adapter->xdp_first_ring;
	tx_ring = &adapter->tx_ring[qid];

	/* Other CPU ids might try to send thorugh this queue */
	spin_lock(&tx_ring->xdp_tx_lock);

	for (i = 0; i < n; i++) {
		if (ena_xdp_xmit_frame(tx_ring, adapter, frames[i], 0))
			break;
		nxmit++;
	}

	/* Ring doorbell to make device aware of the packets */
	if (flags & XDP_XMIT_FLUSH)
		ena_ring_tx_doorbell(tx_ring);

	spin_unlock(&tx_ring->xdp_tx_lock);

	/* Return number of packets sent */
	return nxmit;
}

static void ena_init_all_xdp_queues(struct ena_adapter *adapter)
{
	adapter->xdp_first_ring = adapter->num_io_queues;
	adapter->xdp_num_queues = adapter->num_io_queues;

	ena_init_io_rings(adapter,
			  adapter->xdp_first_ring,
			  adapter->xdp_num_queues);
}

int ena_setup_and_create_all_xdp_queues(struct ena_adapter *adapter)
{
	u32 xdp_first_ring = adapter->xdp_first_ring;
	u32 xdp_num_queues = adapter->xdp_num_queues;
	int rc = 0;

	rc = ena_setup_tx_resources_in_range(adapter, xdp_first_ring, xdp_num_queues);
	if (rc)
		goto setup_err;

	rc = ena_create_io_tx_queues_in_range(adapter, xdp_first_ring, xdp_num_queues);
	if (rc)
		goto create_err;

	return 0;

create_err:
	ena_free_all_io_tx_resources_in_range(adapter, xdp_first_ring, xdp_num_queues);
setup_err:
	return rc;
}

/* Provides a way for both kernel and bpf-prog to know
 * more about the RX-queue a given XDP frame arrived on.
 */
int ena_xdp_register_rxq_info(struct ena_ring *rx_ring)
{
	int rc;

	rc = xdp_rxq_info_reg(&rx_ring->xdp_rxq, rx_ring->netdev, rx_ring->qid, 0);

	netif_dbg(rx_ring->adapter, ifup, rx_ring->netdev, "Registering RX info for queue %d",
		  rx_ring->qid);
	if (rc) {
		netif_err(rx_ring->adapter, ifup, rx_ring->netdev,
			  "Failed to register xdp rx queue info. RX queue num %d rc: %d\n",
			  rx_ring->qid, rc);
		goto err;
	}

	rc = xdp_rxq_info_reg_mem_model(&rx_ring->xdp_rxq, MEM_TYPE_PAGE_SHARED, NULL);

	if (rc) {
		netif_err(rx_ring->adapter, ifup, rx_ring->netdev,
			  "Failed to register xdp rx queue info memory model. RX queue num %d rc: %d\n",
			  rx_ring->qid, rc);
		xdp_rxq_info_unreg(&rx_ring->xdp_rxq);
	}

err:
	return rc;
}

void ena_xdp_unregister_rxq_info(struct ena_ring *rx_ring)
{
	netif_dbg(rx_ring->adapter, ifdown, rx_ring->netdev,
		  "Unregistering RX info for queue %d",
		  rx_ring->qid);
	xdp_rxq_info_unreg_mem_model(&rx_ring->xdp_rxq);
	xdp_rxq_info_unreg(&rx_ring->xdp_rxq);
}

void ena_xdp_exchange_program_rx_in_range(struct ena_adapter *adapter,
					  struct bpf_prog *prog,
					  int first, int count)
{
	struct bpf_prog *old_bpf_prog;
	struct ena_ring *rx_ring;
	int i = 0;

	for (i = first; i < count; i++) {
		rx_ring = &adapter->rx_ring[i];
		old_bpf_prog = xchg(&rx_ring->xdp_bpf_prog, prog);

		if (!old_bpf_prog && prog) {
			rx_ring->rx_headroom = XDP_PACKET_HEADROOM;
		} else if (old_bpf_prog && !prog) {
			rx_ring->rx_headroom = NET_SKB_PAD;
		}
	}
}

static void ena_xdp_exchange_program(struct ena_adapter *adapter,
				     struct bpf_prog *prog)
{
	struct bpf_prog *old_bpf_prog = xchg(&adapter->xdp_bpf_prog, prog);

	ena_xdp_exchange_program_rx_in_range(adapter,
					     prog,
					     0,
					     adapter->num_io_queues);

	if (old_bpf_prog)
		bpf_prog_put(old_bpf_prog);
}

static int ena_destroy_and_free_all_xdp_queues(struct ena_adapter *adapter)
{
	bool was_up;
	int rc;

	was_up = test_bit(ENA_FLAG_DEV_UP, &adapter->flags);

	if (was_up)
		ena_down(adapter);

	adapter->xdp_first_ring = 0;
	adapter->xdp_num_queues = 0;
	ena_xdp_exchange_program(adapter, NULL);
	if (was_up) {
		rc = ena_up(adapter);
		if (rc)
			return rc;
	}
	return 0;
}

static int ena_xdp_set(struct net_device *netdev, struct netdev_bpf *bpf)
{
	struct ena_adapter *adapter = netdev_priv(netdev);
	struct bpf_prog *prog = bpf->prog;
	struct bpf_prog *old_bpf_prog;
	int rc, prev_mtu;
	bool is_up;

	is_up = test_bit(ENA_FLAG_DEV_UP, &adapter->flags);
	rc = ena_xdp_allowed(adapter);
	if (rc == ENA_XDP_ALLOWED) {
		old_bpf_prog = adapter->xdp_bpf_prog;
		if (prog) {
			if (!is_up) {
				ena_init_all_xdp_queues(adapter);
			} else if (!old_bpf_prog) {
				ena_down(adapter);
				ena_init_all_xdp_queues(adapter);
			}
			ena_xdp_exchange_program(adapter, prog);

			netif_dbg(adapter, drv, adapter->netdev, "Set a new XDP program\n");

			if (is_up && !old_bpf_prog) {
				rc = ena_up(adapter);
				if (rc)
					return rc;
			}
			xdp_features_set_redirect_target(netdev, false);
		} else if (old_bpf_prog) {
			xdp_features_clear_redirect_target(netdev);
			netif_dbg(adapter, drv, adapter->netdev, "Removing XDP program\n");

			rc = ena_destroy_and_free_all_xdp_queues(adapter);
			if (rc)
				return rc;
		}

		prev_mtu = netdev->max_mtu;
		netdev->max_mtu = prog ? ENA_XDP_MAX_MTU : adapter->max_mtu;

		if (!old_bpf_prog)
			netif_info(adapter, drv, adapter->netdev,
				   "XDP program is set, changing the max_mtu from %d to %d",
				   prev_mtu, netdev->max_mtu);

	} else if (rc == ENA_XDP_CURRENT_MTU_TOO_LARGE) {
		netif_err(adapter, drv, adapter->netdev,
			  "Failed to set xdp program, the current MTU (%d) is larger than the maximum allowed MTU (%lu) while xdp is on",
			  netdev->mtu, ENA_XDP_MAX_MTU);
		NL_SET_ERR_MSG_MOD(bpf->extack,
				   "Failed to set xdp program, the current MTU is larger than the maximum allowed MTU. Check the dmesg for more info");
		return -EINVAL;
	} else if (rc == ENA_XDP_NO_ENOUGH_QUEUES) {
		netif_err(adapter, drv, adapter->netdev,
			  "Failed to set xdp program, the Rx/Tx channel count should be at most half of the maximum allowed channel count. The current queue count (%d), the maximal queue count (%d)\n",
			  adapter->num_io_queues, adapter->max_num_io_queues);
		NL_SET_ERR_MSG_MOD(bpf->extack,
				   "Failed to set xdp program, there is no enough space for allocating XDP queues, Check the dmesg for more info");
		return -EINVAL;
	}

	return 0;
}

/* This is the main xdp callback, it's used by the kernel to set/unset the xdp
 * program as well as to query the current xdp program id.
 */
int ena_xdp(struct net_device *netdev, struct netdev_bpf *bpf)
{
	switch (bpf->command) {
	case XDP_SETUP_PROG:
		return ena_xdp_set(netdev, bpf);
	default:
		return -EINVAL;
	}
	return 0;
}

static int ena_clean_xdp_irq(struct ena_ring *tx_ring, u32 budget)
{
	u32 total_done = 0;
	u16 next_to_clean;
	int tx_pkts = 0;
	u16 req_id;
	int rc;

	if (unlikely(!tx_ring))
		return 0;
	next_to_clean = tx_ring->next_to_clean;

	while (tx_pkts < budget) {
		struct ena_tx_buffer *tx_info;
		struct xdp_frame *xdpf;

		rc = ena_com_tx_comp_req_id_get(tx_ring->ena_com_io_cq,
						&req_id);
		if (rc) {
			if (unlikely(rc == -EINVAL))
				handle_invalid_req_id(tx_ring, req_id, NULL, true);
			break;
		}

		/* validate that the request id points to a valid xdp_frame */
		rc = validate_xdp_req_id(tx_ring, req_id);
		if (rc)
			break;

		tx_info = &tx_ring->tx_buffer_info[req_id];

		tx_info->last_jiffies = 0;

		xdpf = tx_info->xdpf;
		tx_info->xdpf = NULL;
		ena_unmap_tx_buff(tx_ring, tx_info);
		xdp_return_frame(xdpf);

		tx_pkts++;
		total_done += tx_info->tx_descs;
		tx_ring->free_ids[next_to_clean] = req_id;
		next_to_clean = ENA_TX_RING_IDX_NEXT(next_to_clean,
						     tx_ring->ring_size);

		netif_dbg(tx_ring->adapter, tx_done, tx_ring->netdev,
			  "tx_poll: q %d pkt #%d req_id %d\n", tx_ring->qid, tx_pkts, req_id);
	}

	tx_ring->next_to_clean = next_to_clean;
	ena_com_comp_ack(tx_ring->ena_com_io_sq, total_done);
	ena_com_update_dev_comp_head(tx_ring->ena_com_io_cq);

	netif_dbg(tx_ring->adapter, tx_done, tx_ring->netdev,
		  "tx_poll: q %d done. total pkts: %d\n",
		  tx_ring->qid, tx_pkts);

	return tx_pkts;
}

/* This is the XDP napi callback. XDP queues use a separate napi callback
 * than Rx/Tx queues.
 */
int ena_xdp_io_poll(struct napi_struct *napi, int budget)
{
	struct ena_napi *ena_napi = container_of(napi, struct ena_napi, napi);
	struct ena_ring *tx_ring;
	u32 work_done;
	int ret;

	tx_ring = ena_napi->tx_ring;

	if (!test_bit(ENA_FLAG_DEV_UP, &tx_ring->adapter->flags) ||
	    test_bit(ENA_FLAG_TRIGGER_RESET, &tx_ring->adapter->flags)) {
		napi_complete_done(napi, 0);
		return 0;
	}

	work_done = ena_clean_xdp_irq(tx_ring, budget);

	/* If the device is about to reset or down, avoid unmask
	 * the interrupt and return 0 so NAPI won't reschedule
	 */
	if (unlikely(!test_bit(ENA_FLAG_DEV_UP, &tx_ring->adapter->flags))) {
		napi_complete_done(napi, 0);
		ret = 0;
	} else if (budget > work_done) {
		ena_increase_stat(&tx_ring->tx_stats.napi_comp, 1,
				  &tx_ring->syncp);
		if (napi_complete_done(napi, work_done))
			ena_unmask_interrupt(tx_ring, NULL);

		ena_update_ring_numa_node(tx_ring, NULL);
		ret = work_done;
	} else {
		ret = budget;
	}

	u64_stats_update_begin(&tx_ring->syncp);
	tx_ring->tx_stats.tx_poll++;
	u64_stats_update_end(&tx_ring->syncp);
	tx_ring->tx_stats.last_napi_jiffies = jiffies;

	return ret;
}
