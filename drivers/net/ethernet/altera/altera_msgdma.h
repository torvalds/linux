/* Altera TSE SGDMA and MSGDMA Linux driver
 * Copyright (C) 2014 Altera Corporation. All rights reserved
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __ALTERA_MSGDMA_H__
#define __ALTERA_MSGDMA_H__

void msgdma_reset(struct altera_tse_private *);
void msgdma_enable_txirq(struct altera_tse_private *);
void msgdma_enable_rxirq(struct altera_tse_private *);
void msgdma_disable_rxirq(struct altera_tse_private *);
void msgdma_disable_txirq(struct altera_tse_private *);
void msgdma_clear_rxirq(struct altera_tse_private *);
void msgdma_clear_txirq(struct altera_tse_private *);
u32 msgdma_tx_completions(struct altera_tse_private *);
int msgdma_add_rx_desc(struct altera_tse_private *, struct tse_buffer *);
int msgdma_tx_buffer(struct altera_tse_private *, struct tse_buffer *);
u32 msgdma_rx_status(struct altera_tse_private *);
int msgdma_initialize(struct altera_tse_private *);
void msgdma_uninitialize(struct altera_tse_private *);

#endif /*  __ALTERA_MSGDMA_H__ */
