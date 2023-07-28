// SPDX-License-Identifier: GPL-2.0-only
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2019 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include "net_driver.h"
#include "efx.h"
#include "nic.h"
#include "mcdi_functions.h"
#include "mcdi.h"
#include "mcdi_pcol.h"

int efx_mcdi_free_vis(struct efx_nic *efx)
{
	MCDI_DECLARE_BUF_ERR(outbuf);
	size_t outlen;
	int rc = efx_mcdi_rpc_quiet(efx, MC_CMD_FREE_VIS, NULL, 0,
				    outbuf, sizeof(outbuf), &outlen);

	/* -EALREADY means nothing to free, so ignore */
	if (rc == -EALREADY)
		rc = 0;
	if (rc)
		efx_mcdi_display_error(efx, MC_CMD_FREE_VIS, 0, outbuf, outlen,
				       rc);
	return rc;
}

int efx_mcdi_alloc_vis(struct efx_nic *efx, unsigned int min_vis,
		       unsigned int max_vis, unsigned int *vi_base,
		       unsigned int *allocated_vis)
{
	MCDI_DECLARE_BUF(outbuf, MC_CMD_ALLOC_VIS_OUT_LEN);
	MCDI_DECLARE_BUF(inbuf, MC_CMD_ALLOC_VIS_IN_LEN);
	size_t outlen;
	int rc;

	MCDI_SET_DWORD(inbuf, ALLOC_VIS_IN_MIN_VI_COUNT, min_vis);
	MCDI_SET_DWORD(inbuf, ALLOC_VIS_IN_MAX_VI_COUNT, max_vis);
	rc = efx_mcdi_rpc(efx, MC_CMD_ALLOC_VIS, inbuf, sizeof(inbuf),
			  outbuf, sizeof(outbuf), &outlen);
	if (rc != 0)
		return rc;

	if (outlen < MC_CMD_ALLOC_VIS_OUT_LEN)
		return -EIO;

	netif_dbg(efx, drv, efx->net_dev, "base VI is A0x%03x\n",
		  MCDI_DWORD(outbuf, ALLOC_VIS_OUT_VI_BASE));

	if (vi_base)
		*vi_base = MCDI_DWORD(outbuf, ALLOC_VIS_OUT_VI_BASE);
	if (allocated_vis)
		*allocated_vis = MCDI_DWORD(outbuf, ALLOC_VIS_OUT_VI_COUNT);
	return 0;
}

int efx_mcdi_ev_probe(struct efx_channel *channel)
{
	return efx_nic_alloc_buffer(channel->efx, &channel->eventq,
				    (channel->eventq_mask + 1) *
				    sizeof(efx_qword_t),
				    GFP_KERNEL);
}

int efx_mcdi_ev_init(struct efx_channel *channel, bool v1_cut_thru, bool v2)
{
	MCDI_DECLARE_BUF(inbuf,
			 MC_CMD_INIT_EVQ_V2_IN_LEN(EFX_MAX_EVQ_SIZE * 8 /
						   EFX_BUF_SIZE));
	MCDI_DECLARE_BUF(outbuf, MC_CMD_INIT_EVQ_V2_OUT_LEN);
	size_t entries = channel->eventq.len / EFX_BUF_SIZE;
	struct efx_nic *efx = channel->efx;
	size_t inlen, outlen;
	dma_addr_t dma_addr;
	int rc, i;

	/* Fill event queue with all ones (i.e. empty events) */
	memset(channel->eventq.addr, 0xff, channel->eventq.len);

	MCDI_SET_DWORD(inbuf, INIT_EVQ_IN_SIZE, channel->eventq_mask + 1);
	MCDI_SET_DWORD(inbuf, INIT_EVQ_IN_INSTANCE, channel->channel);
	/* INIT_EVQ expects index in vector table, not absolute */
	MCDI_SET_DWORD(inbuf, INIT_EVQ_IN_IRQ_NUM, channel->channel);
	MCDI_SET_DWORD(inbuf, INIT_EVQ_IN_TMR_MODE,
		       MC_CMD_INIT_EVQ_IN_TMR_MODE_DIS);
	MCDI_SET_DWORD(inbuf, INIT_EVQ_IN_TMR_LOAD, 0);
	MCDI_SET_DWORD(inbuf, INIT_EVQ_IN_TMR_RELOAD, 0);
	MCDI_SET_DWORD(inbuf, INIT_EVQ_IN_COUNT_MODE,
		       MC_CMD_INIT_EVQ_IN_COUNT_MODE_DIS);
	MCDI_SET_DWORD(inbuf, INIT_EVQ_IN_COUNT_THRSHLD, 0);

	if (v2) {
		/* Use the new generic approach to specifying event queue
		 * configuration, requesting lower latency or higher throughput.
		 * The options that actually get used appear in the output.
		 */
		MCDI_POPULATE_DWORD_2(inbuf, INIT_EVQ_V2_IN_FLAGS,
				      INIT_EVQ_V2_IN_FLAG_INTERRUPTING, 1,
				      INIT_EVQ_V2_IN_FLAG_TYPE,
				      MC_CMD_INIT_EVQ_V2_IN_FLAG_TYPE_AUTO);
	} else {
		MCDI_POPULATE_DWORD_4(inbuf, INIT_EVQ_IN_FLAGS,
				      INIT_EVQ_IN_FLAG_INTERRUPTING, 1,
				      INIT_EVQ_IN_FLAG_RX_MERGE, 1,
				      INIT_EVQ_IN_FLAG_TX_MERGE, 1,
				      INIT_EVQ_IN_FLAG_CUT_THRU, v1_cut_thru);
	}

	dma_addr = channel->eventq.dma_addr;
	for (i = 0; i < entries; ++i) {
		MCDI_SET_ARRAY_QWORD(inbuf, INIT_EVQ_IN_DMA_ADDR, i, dma_addr);
		dma_addr += EFX_BUF_SIZE;
	}

	inlen = MC_CMD_INIT_EVQ_IN_LEN(entries);

	rc = efx_mcdi_rpc(efx, MC_CMD_INIT_EVQ, inbuf, inlen,
			  outbuf, sizeof(outbuf), &outlen);

	if (outlen >= MC_CMD_INIT_EVQ_V2_OUT_LEN)
		netif_dbg(efx, drv, efx->net_dev,
			  "Channel %d using event queue flags %08x\n",
			  channel->channel,
			  MCDI_DWORD(outbuf, INIT_EVQ_V2_OUT_FLAGS));

	return rc;
}

void efx_mcdi_ev_remove(struct efx_channel *channel)
{
	efx_nic_free_buffer(channel->efx, &channel->eventq);
}

void efx_mcdi_ev_fini(struct efx_channel *channel)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_FINI_EVQ_IN_LEN);
	MCDI_DECLARE_BUF_ERR(outbuf);
	struct efx_nic *efx = channel->efx;
	size_t outlen;
	int rc;

	MCDI_SET_DWORD(inbuf, FINI_EVQ_IN_INSTANCE, channel->channel);

	rc = efx_mcdi_rpc_quiet(efx, MC_CMD_FINI_EVQ, inbuf, sizeof(inbuf),
				outbuf, sizeof(outbuf), &outlen);

	if (rc && rc != -EALREADY)
		goto fail;

	return;

fail:
	efx_mcdi_display_error(efx, MC_CMD_FINI_EVQ, MC_CMD_FINI_EVQ_IN_LEN,
			       outbuf, outlen, rc);
}

int efx_mcdi_tx_init(struct efx_tx_queue *tx_queue)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_INIT_TXQ_IN_LEN(EFX_MAX_DMAQ_SIZE * 8 /
						       EFX_BUF_SIZE));
	bool csum_offload = tx_queue->type & EFX_TXQ_TYPE_OUTER_CSUM;
	bool inner_csum = tx_queue->type & EFX_TXQ_TYPE_INNER_CSUM;
	size_t entries = tx_queue->txd.len / EFX_BUF_SIZE;
	struct efx_channel *channel = tx_queue->channel;
	struct efx_nic *efx = tx_queue->efx;
	dma_addr_t dma_addr;
	size_t inlen;
	int rc, i;

	BUILD_BUG_ON(MC_CMD_INIT_TXQ_OUT_LEN != 0);

	MCDI_SET_DWORD(inbuf, INIT_TXQ_IN_SIZE, tx_queue->ptr_mask + 1);
	MCDI_SET_DWORD(inbuf, INIT_TXQ_IN_TARGET_EVQ, channel->channel);
	MCDI_SET_DWORD(inbuf, INIT_TXQ_IN_LABEL, tx_queue->label);
	MCDI_SET_DWORD(inbuf, INIT_TXQ_IN_INSTANCE, tx_queue->queue);
	MCDI_SET_DWORD(inbuf, INIT_TXQ_IN_OWNER_ID, 0);
	MCDI_SET_DWORD(inbuf, INIT_TXQ_IN_PORT_ID, efx->vport_id);

	dma_addr = tx_queue->txd.dma_addr;

	netif_dbg(efx, hw, efx->net_dev, "pushing TXQ %d. %zu entries (%llx)\n",
		  tx_queue->queue, entries, (u64)dma_addr);

	for (i = 0; i < entries; ++i) {
		MCDI_SET_ARRAY_QWORD(inbuf, INIT_TXQ_IN_DMA_ADDR, i, dma_addr);
		dma_addr += EFX_BUF_SIZE;
	}

	inlen = MC_CMD_INIT_TXQ_IN_LEN(entries);

	do {
		bool tso_v2 = tx_queue->tso_version == 2;

		/* TSOv2 implies IP header checksum offload for TSO frames,
		 * so we can safely disable IP header checksum offload for
		 * everything else.  If we don't have TSOv2, then we have to
		 * enable IP header checksum offload, which is strictly
		 * incorrect but better than breaking TSO.
		 */
		MCDI_POPULATE_DWORD_6(inbuf, INIT_TXQ_IN_FLAGS,
				/* This flag was removed from mcdi_pcol.h for
				 * the non-_EXT version of INIT_TXQ.  However,
				 * firmware still honours it.
				 */
				INIT_TXQ_EXT_IN_FLAG_TSOV2_EN, tso_v2,
				INIT_TXQ_IN_FLAG_IP_CSUM_DIS, !(csum_offload && tso_v2),
				INIT_TXQ_IN_FLAG_TCP_CSUM_DIS, !csum_offload,
				INIT_TXQ_EXT_IN_FLAG_TIMESTAMP, tx_queue->timestamping,
				INIT_TXQ_IN_FLAG_INNER_IP_CSUM_EN, inner_csum && !tso_v2,
				INIT_TXQ_IN_FLAG_INNER_TCP_CSUM_EN, inner_csum);

		rc = efx_mcdi_rpc_quiet(efx, MC_CMD_INIT_TXQ, inbuf, inlen,
					NULL, 0, NULL);
		if (rc == -ENOSPC && tso_v2) {
			/* Retry without TSOv2 if we're short on contexts. */
			tx_queue->tso_version = 0;
			netif_warn(efx, probe, efx->net_dev,
				   "TSOv2 context not available to segment in "
				   "hardware. TCP performance may be reduced.\n"
				   );
		} else if (rc) {
			efx_mcdi_display_error(efx, MC_CMD_INIT_TXQ,
					       MC_CMD_INIT_TXQ_EXT_IN_LEN,
					       NULL, 0, rc);
			goto fail;
		}
	} while (rc);

	return 0;

fail:
	return rc;
}

void efx_mcdi_tx_remove(struct efx_tx_queue *tx_queue)
{
	efx_nic_free_buffer(tx_queue->efx, &tx_queue->txd);
}

void efx_mcdi_tx_fini(struct efx_tx_queue *tx_queue)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_FINI_TXQ_IN_LEN);
	MCDI_DECLARE_BUF_ERR(outbuf);
	struct efx_nic *efx = tx_queue->efx;
	size_t outlen;
	int rc;

	MCDI_SET_DWORD(inbuf, FINI_TXQ_IN_INSTANCE,
		       tx_queue->queue);

	rc = efx_mcdi_rpc_quiet(efx, MC_CMD_FINI_TXQ, inbuf, sizeof(inbuf),
				outbuf, sizeof(outbuf), &outlen);

	if (rc && rc != -EALREADY)
		goto fail;

	return;

fail:
	efx_mcdi_display_error(efx, MC_CMD_FINI_TXQ, MC_CMD_FINI_TXQ_IN_LEN,
			       outbuf, outlen, rc);
}

int efx_mcdi_rx_probe(struct efx_rx_queue *rx_queue)
{
	return efx_nic_alloc_buffer(rx_queue->efx, &rx_queue->rxd,
				    (rx_queue->ptr_mask + 1) *
				    sizeof(efx_qword_t),
				    GFP_KERNEL);
}

void efx_mcdi_rx_init(struct efx_rx_queue *rx_queue)
{
	struct efx_channel *channel = efx_rx_queue_channel(rx_queue);
	size_t entries = rx_queue->rxd.len / EFX_BUF_SIZE;
	MCDI_DECLARE_BUF(inbuf, MC_CMD_INIT_RXQ_V4_IN_LEN);
	struct efx_nic *efx = rx_queue->efx;
	unsigned int buffer_size;
	dma_addr_t dma_addr;
	int rc;
	int i;
	BUILD_BUG_ON(MC_CMD_INIT_RXQ_OUT_LEN != 0);

	rx_queue->scatter_n = 0;
	rx_queue->scatter_len = 0;
	if (efx->type->revision == EFX_REV_EF100)
		buffer_size = efx->rx_page_buf_step;
	else
		buffer_size = 0;

	MCDI_SET_DWORD(inbuf, INIT_RXQ_IN_SIZE, rx_queue->ptr_mask + 1);
	MCDI_SET_DWORD(inbuf, INIT_RXQ_IN_TARGET_EVQ, channel->channel);
	MCDI_SET_DWORD(inbuf, INIT_RXQ_IN_LABEL, efx_rx_queue_index(rx_queue));
	MCDI_SET_DWORD(inbuf, INIT_RXQ_IN_INSTANCE,
		       efx_rx_queue_index(rx_queue));
	MCDI_POPULATE_DWORD_2(inbuf, INIT_RXQ_IN_FLAGS,
			      INIT_RXQ_IN_FLAG_PREFIX, 1,
			      INIT_RXQ_IN_FLAG_TIMESTAMP, 1);
	MCDI_SET_DWORD(inbuf, INIT_RXQ_IN_OWNER_ID, 0);
	MCDI_SET_DWORD(inbuf, INIT_RXQ_IN_PORT_ID, efx->vport_id);
	MCDI_SET_DWORD(inbuf, INIT_RXQ_V4_IN_BUFFER_SIZE_BYTES, buffer_size);

	dma_addr = rx_queue->rxd.dma_addr;

	netif_dbg(efx, hw, efx->net_dev, "pushing RXQ %d. %zu entries (%llx)\n",
		  efx_rx_queue_index(rx_queue), entries, (u64)dma_addr);

	for (i = 0; i < entries; ++i) {
		MCDI_SET_ARRAY_QWORD(inbuf, INIT_RXQ_IN_DMA_ADDR, i, dma_addr);
		dma_addr += EFX_BUF_SIZE;
	}

	rc = efx_mcdi_rpc(efx, MC_CMD_INIT_RXQ, inbuf, sizeof(inbuf),
			  NULL, 0, NULL);
	if (rc)
		netdev_WARN(efx->net_dev, "failed to initialise RXQ %d\n",
			    efx_rx_queue_index(rx_queue));
}

void efx_mcdi_rx_remove(struct efx_rx_queue *rx_queue)
{
	efx_nic_free_buffer(rx_queue->efx, &rx_queue->rxd);
}

void efx_mcdi_rx_fini(struct efx_rx_queue *rx_queue)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_FINI_RXQ_IN_LEN);
	MCDI_DECLARE_BUF_ERR(outbuf);
	struct efx_nic *efx = rx_queue->efx;
	size_t outlen;
	int rc;

	MCDI_SET_DWORD(inbuf, FINI_RXQ_IN_INSTANCE,
		       efx_rx_queue_index(rx_queue));

	rc = efx_mcdi_rpc_quiet(efx, MC_CMD_FINI_RXQ, inbuf, sizeof(inbuf),
				outbuf, sizeof(outbuf), &outlen);

	if (rc && rc != -EALREADY)
		goto fail;

	return;

fail:
	efx_mcdi_display_error(efx, MC_CMD_FINI_RXQ, MC_CMD_FINI_RXQ_IN_LEN,
			       outbuf, outlen, rc);
}

int efx_fini_dmaq(struct efx_nic *efx)
{
	struct efx_tx_queue *tx_queue;
	struct efx_rx_queue *rx_queue;
	struct efx_channel *channel;
	int pending;

	/* If the MC has just rebooted, the TX/RX queues will have already been
	 * torn down, but efx->active_queues needs to be set to zero.
	 */
	if (efx->must_realloc_vis) {
		atomic_set(&efx->active_queues, 0);
		return 0;
	}

	/* Do not attempt to write to the NIC during EEH recovery */
	if (efx->state != STATE_RECOVERY) {
		efx_for_each_channel(channel, efx) {
			efx_for_each_channel_rx_queue(rx_queue, channel)
				efx_mcdi_rx_fini(rx_queue);
			efx_for_each_channel_tx_queue(tx_queue, channel)
				efx_mcdi_tx_fini(tx_queue);
		}

		wait_event_timeout(efx->flush_wq,
				   atomic_read(&efx->active_queues) == 0,
				   msecs_to_jiffies(EFX_MAX_FLUSH_TIME));
		pending = atomic_read(&efx->active_queues);
		if (pending) {
			netif_err(efx, hw, efx->net_dev, "failed to flush %d queues\n",
				  pending);
			return -ETIMEDOUT;
		}
	}

	return 0;
}

int efx_mcdi_window_mode_to_stride(struct efx_nic *efx, u8 vi_window_mode)
{
	switch (vi_window_mode) {
	case MC_CMD_GET_CAPABILITIES_V3_OUT_VI_WINDOW_MODE_8K:
		efx->vi_stride = 8192;
		break;
	case MC_CMD_GET_CAPABILITIES_V3_OUT_VI_WINDOW_MODE_16K:
		efx->vi_stride = 16384;
		break;
	case MC_CMD_GET_CAPABILITIES_V3_OUT_VI_WINDOW_MODE_64K:
		efx->vi_stride = 65536;
		break;
	default:
		netif_err(efx, probe, efx->net_dev,
			  "Unrecognised VI window mode %d\n",
			  vi_window_mode);
		return -EIO;
	}
	netif_dbg(efx, probe, efx->net_dev, "vi_stride = %u\n",
		  efx->vi_stride);
	return 0;
}

int efx_get_pf_index(struct efx_nic *efx, unsigned int *pf_index)
{
	MCDI_DECLARE_BUF(outbuf, MC_CMD_GET_FUNCTION_INFO_OUT_LEN);
	size_t outlen;
	int rc;

	rc = efx_mcdi_rpc(efx, MC_CMD_GET_FUNCTION_INFO, NULL, 0, outbuf,
			  sizeof(outbuf), &outlen);
	if (rc)
		return rc;
	if (outlen < sizeof(outbuf))
		return -EIO;

	*pf_index = MCDI_DWORD(outbuf, GET_FUNCTION_INFO_OUT_PF);
	return 0;
}
