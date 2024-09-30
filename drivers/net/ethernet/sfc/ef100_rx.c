// SPDX-License-Identifier: GPL-2.0-only
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2005-2019 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include "net_driver.h"
#include "ef100_rx.h"
#include "rx_common.h"
#include "efx.h"
#include "nic_common.h"
#include "mcdi_functions.h"
#include "ef100_regs.h"
#include "ef100_nic.h"
#include "io.h"

/* Get the value of a field in the RX prefix */
#define PREFIX_OFFSET_W(_f)	(ESF_GZ_RX_PREFIX_ ## _f ## _LBN / 32)
#define PREFIX_OFFSET_B(_f)	(ESF_GZ_RX_PREFIX_ ## _f ## _LBN % 32)
#define PREFIX_WIDTH_MASK(_f)	((1ULL << ESF_GZ_RX_PREFIX_ ## _f ## _WIDTH) - 1)
#define PREFIX_WORD(_p, _f)	le32_to_cpu((__force __le32)(_p)[PREFIX_OFFSET_W(_f)])
#define PREFIX_FIELD(_p, _f)	((PREFIX_WORD(_p, _f) >> PREFIX_OFFSET_B(_f)) & \
				 PREFIX_WIDTH_MASK(_f))

#define ESF_GZ_RX_PREFIX_NT_OR_INNER_L3_CLASS_LBN	\
		(ESF_GZ_RX_PREFIX_CLASS_LBN + ESF_GZ_RX_PREFIX_HCLASS_NT_OR_INNER_L3_CLASS_LBN)
#define ESF_GZ_RX_PREFIX_NT_OR_INNER_L3_CLASS_WIDTH	\
		ESF_GZ_RX_PREFIX_HCLASS_NT_OR_INNER_L3_CLASS_WIDTH

bool ef100_rx_buf_hash_valid(const u8 *prefix)
{
	return PREFIX_FIELD(prefix, RSS_HASH_VALID);
}

static bool ef100_has_fcs_error(struct efx_channel *channel, u32 *prefix)
{
	u16 rxclass;
	u8 l2status;

	rxclass = le16_to_cpu((__force __le16)PREFIX_FIELD(prefix, CLASS));
	l2status = PREFIX_FIELD(&rxclass, HCLASS_L2_STATUS);

	if (likely(l2status == ESE_GZ_RH_HCLASS_L2_STATUS_OK))
		/* Everything is ok */
		return false;

	if (l2status == ESE_GZ_RH_HCLASS_L2_STATUS_FCS_ERR)
		channel->n_rx_eth_crc_err++;
	return true;
}

void __ef100_rx_packet(struct efx_channel *channel)
{
	struct efx_rx_queue *rx_queue = efx_channel_get_rx_queue(channel);
	struct efx_rx_buffer *rx_buf = efx_rx_buffer(rx_queue,
						     channel->rx_pkt_index);
	struct efx_nic *efx = channel->efx;
	struct ef100_nic_data *nic_data;
	u8 *eh = efx_rx_buf_va(rx_buf);
	__wsum csum = 0;
	u16 ing_port;
	u32 *prefix;

	prefix = (u32 *)(eh - ESE_GZ_RX_PKT_PREFIX_LEN);

	if (channel->type->receive_raw) {
		u32 mark = PREFIX_FIELD(prefix, USER_MARK);

		if (channel->type->receive_raw(rx_queue, mark))
			return; /* packet was consumed */
	}

	if (ef100_has_fcs_error(channel, prefix) &&
	    unlikely(!(efx->net_dev->features & NETIF_F_RXALL)))
		goto out;

	rx_buf->len = le16_to_cpu((__force __le16)PREFIX_FIELD(prefix, LENGTH));
	if (rx_buf->len <= sizeof(struct ethhdr)) {
		if (net_ratelimit())
			netif_err(channel->efx, rx_err, channel->efx->net_dev,
				  "RX packet too small (%d)\n", rx_buf->len);
		++channel->n_rx_frm_trunc;
		goto out;
	}

	ing_port = le16_to_cpu((__force __le16) PREFIX_FIELD(prefix, INGRESS_MPORT));

	nic_data = efx->nic_data;

	if (nic_data->have_mport && ing_port != nic_data->base_mport) {
#ifdef CONFIG_SFC_SRIOV
		struct efx_rep *efv;

		rcu_read_lock();
		efv = efx_ef100_find_rep_by_mport(efx, ing_port);
		if (efv) {
			if (efv->net_dev->flags & IFF_UP)
				efx_ef100_rep_rx_packet(efv, rx_buf);
			rcu_read_unlock();
			/* Representor Rx doesn't care about PF Rx buffer
			 * ownership, it just makes a copy. So, we are done
			 * with the Rx buffer from PF point of view and should
			 * free it.
			 */
			goto free_rx_buffer;
		}
		rcu_read_unlock();
#endif
		if (net_ratelimit())
			netif_warn(efx, drv, efx->net_dev,
				   "Unrecognised ing_port %04x (base %04x), dropping\n",
				   ing_port, nic_data->base_mport);
		channel->n_rx_mport_bad++;
		goto free_rx_buffer;
	}

	if (likely(efx->net_dev->features & NETIF_F_RXCSUM)) {
		if (PREFIX_FIELD(prefix, NT_OR_INNER_L3_CLASS) == 1) {
			++channel->n_rx_ip_hdr_chksum_err;
		} else {
			u16 sum = be16_to_cpu((__force __be16)PREFIX_FIELD(prefix, CSUM_FRAME));

			csum = (__force __wsum) sum;
		}
	}

	if (channel->type->receive_skb) {
		/* no support for special channels yet, so just discard */
		WARN_ON_ONCE(1);
		goto free_rx_buffer;
	}

	++rx_queue->rx_packets;
	rx_queue->rx_bytes += rx_buf->len;

	efx_rx_packet_gro(channel, rx_buf, channel->rx_pkt_n_frags, eh, csum);
	goto out;

free_rx_buffer:
	efx_free_rx_buffers(rx_queue, rx_buf, 1);
out:
	channel->rx_pkt_n_frags = 0;
}

static void ef100_rx_packet(struct efx_rx_queue *rx_queue, unsigned int index)
{
	struct efx_rx_buffer *rx_buf = efx_rx_buffer(rx_queue, index);
	struct efx_channel *channel = efx_rx_queue_channel(rx_queue);
	struct efx_nic *efx = rx_queue->efx;

	netif_vdbg(efx, rx_status, efx->net_dev,
		   "RX queue %d received id %x\n",
		   efx_rx_queue_index(rx_queue), index);

	efx_sync_rx_buffer(efx, rx_buf, efx->rx_dma_len);

	prefetch(efx_rx_buf_va(rx_buf));

	rx_buf->page_offset += efx->rx_prefix_size;

	efx_recycle_rx_pages(channel, rx_buf, 1);

	efx_rx_flush_packet(channel);
	channel->rx_pkt_n_frags = 1;
	channel->rx_pkt_index = index;
}

void efx_ef100_ev_rx(struct efx_channel *channel, const efx_qword_t *p_event)
{
	struct efx_rx_queue *rx_queue = efx_channel_get_rx_queue(channel);
	unsigned int n_packets =
		EFX_QWORD_FIELD(*p_event, ESF_GZ_EV_RXPKTS_NUM_PKT);
	int i;

	WARN_ON_ONCE(!n_packets);
	if (n_packets > 1)
		++channel->n_rx_merge_events;

	channel->irq_mod_score += 2 * n_packets;

	for (i = 0; i < n_packets; ++i) {
		ef100_rx_packet(rx_queue,
				rx_queue->removed_count & rx_queue->ptr_mask);
		++rx_queue->removed_count;
	}
}

void ef100_rx_write(struct efx_rx_queue *rx_queue)
{
	unsigned int notified_count = rx_queue->notified_count;
	struct efx_rx_buffer *rx_buf;
	unsigned int idx;
	efx_qword_t *rxd;
	efx_dword_t rxdb;

	while (notified_count != rx_queue->added_count) {
		idx = notified_count & rx_queue->ptr_mask;
		rx_buf = efx_rx_buffer(rx_queue, idx);
		rxd = efx_rx_desc(rx_queue, idx);

		EFX_POPULATE_QWORD_1(*rxd, ESF_GZ_RX_BUF_ADDR, rx_buf->dma_addr);

		++notified_count;
	}
	if (notified_count == rx_queue->notified_count)
		return;

	wmb();
	EFX_POPULATE_DWORD_1(rxdb, ERF_GZ_RX_RING_PIDX,
			     rx_queue->added_count & rx_queue->ptr_mask);
	efx_writed_page(rx_queue->efx, &rxdb,
			ER_GZ_RX_RING_DOORBELL, efx_rx_queue_index(rx_queue));
	if (rx_queue->grant_credits)
		wmb();
	rx_queue->notified_count = notified_count;
	if (rx_queue->grant_credits)
		schedule_work(&rx_queue->grant_work);
}
