/* SPDX-License-Identifier: GPL-2.0-only */
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2006-2015 Solarflare Communications Inc.
 */

#ifndef EFX_TX_H
#define EFX_TX_H

#include <linux/types.h>

/* Driver internal tx-path related declarations. */

unsigned int efx_tx_limit_len(struct efx_tx_queue *tx_queue,
			      dma_addr_t dma_addr, unsigned int len);

u8 *efx_tx_get_copy_buffer_limited(struct efx_tx_queue *tx_queue,
				   struct efx_tx_buffer *buffer, size_t len);

int efx_enqueue_skb_tso(struct efx_tx_queue *tx_queue, struct sk_buff *skb,
			bool *data_mapped);

#endif /* EFX_TX_H */
