/* SPDX-License-Identifier: GPL-2.0-only */
/* Altera TSE SGDMA and MSGDMA Linux driver
 * Copyright (C) 2014 Altera Corporation. All rights reserved
 */

#ifndef __ALTERA_SGDMA_H__
#define __ALTERA_SGDMA_H__

void sgdma_reset(struct altera_tse_private *);
void sgdma_enable_txirq(struct altera_tse_private *);
void sgdma_enable_rxirq(struct altera_tse_private *);
void sgdma_disable_rxirq(struct altera_tse_private *);
void sgdma_disable_txirq(struct altera_tse_private *);
void sgdma_clear_rxirq(struct altera_tse_private *);
void sgdma_clear_txirq(struct altera_tse_private *);
int sgdma_tx_buffer(struct altera_tse_private *priv, struct tse_buffer *);
u32 sgdma_tx_completions(struct altera_tse_private *);
void sgdma_add_rx_desc(struct altera_tse_private *priv, struct tse_buffer *);
void sgdma_status(struct altera_tse_private *);
u32 sgdma_rx_status(struct altera_tse_private *);
int sgdma_initialize(struct altera_tse_private *);
void sgdma_uninitialize(struct altera_tse_private *);
void sgdma_start_rxdma(struct altera_tse_private *);

#endif /*  __ALTERA_SGDMA_H__ */
