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

#include <linux/netdevice.h>
#include "altera_utils.h"
#include "altera_tse.h"
#include "altera_msgdmahw.h"

/* No initialization work to do for MSGDMA */
int msgdma_initialize(struct altera_tse_private *priv)
{
	return 0;
}

void msgdma_uninitialize(struct altera_tse_private *priv)
{
}

void msgdma_start_rxdma(struct altera_tse_private *priv)
{
}

void msgdma_reset(struct altera_tse_private *priv)
{
	int counter;
	struct msgdma_csr *txcsr =
		(struct msgdma_csr *)priv->tx_dma_csr;
	struct msgdma_csr *rxcsr =
		(struct msgdma_csr *)priv->rx_dma_csr;

	/* Reset Rx mSGDMA */
	iowrite32(MSGDMA_CSR_STAT_MASK, &rxcsr->status);
	iowrite32(MSGDMA_CSR_CTL_RESET, &rxcsr->control);

	counter = 0;
	while (counter++ < ALTERA_TSE_SW_RESET_WATCHDOG_CNTR) {
		if (tse_bit_is_clear(&rxcsr->status,
				     MSGDMA_CSR_STAT_RESETTING))
			break;
		udelay(1);
	}

	if (counter >= ALTERA_TSE_SW_RESET_WATCHDOG_CNTR)
		netif_warn(priv, drv, priv->dev,
			   "TSE Rx mSGDMA resetting bit never cleared!\n");

	/* clear all status bits */
	iowrite32(MSGDMA_CSR_STAT_MASK, &rxcsr->status);

	/* Reset Tx mSGDMA */
	iowrite32(MSGDMA_CSR_STAT_MASK, &txcsr->status);
	iowrite32(MSGDMA_CSR_CTL_RESET, &txcsr->control);

	counter = 0;
	while (counter++ < ALTERA_TSE_SW_RESET_WATCHDOG_CNTR) {
		if (tse_bit_is_clear(&txcsr->status,
				     MSGDMA_CSR_STAT_RESETTING))
			break;
		udelay(1);
	}

	if (counter >= ALTERA_TSE_SW_RESET_WATCHDOG_CNTR)
		netif_warn(priv, drv, priv->dev,
			   "TSE Tx mSGDMA resetting bit never cleared!\n");

	/* clear all status bits */
	iowrite32(MSGDMA_CSR_STAT_MASK, &txcsr->status);
}

void msgdma_disable_rxirq(struct altera_tse_private *priv)
{
	struct msgdma_csr *csr = priv->rx_dma_csr;
	tse_clear_bit(&csr->control, MSGDMA_CSR_CTL_GLOBAL_INTR);
}

void msgdma_enable_rxirq(struct altera_tse_private *priv)
{
	struct msgdma_csr *csr = priv->rx_dma_csr;
	tse_set_bit(&csr->control, MSGDMA_CSR_CTL_GLOBAL_INTR);
}

void msgdma_disable_txirq(struct altera_tse_private *priv)
{
	struct msgdma_csr *csr = priv->tx_dma_csr;
	tse_clear_bit(&csr->control, MSGDMA_CSR_CTL_GLOBAL_INTR);
}

void msgdma_enable_txirq(struct altera_tse_private *priv)
{
	struct msgdma_csr *csr = priv->tx_dma_csr;
	tse_set_bit(&csr->control, MSGDMA_CSR_CTL_GLOBAL_INTR);
}

void msgdma_clear_rxirq(struct altera_tse_private *priv)
{
	struct msgdma_csr *csr = priv->rx_dma_csr;
	iowrite32(MSGDMA_CSR_STAT_IRQ, &csr->status);
}

void msgdma_clear_txirq(struct altera_tse_private *priv)
{
	struct msgdma_csr *csr = priv->tx_dma_csr;
	iowrite32(MSGDMA_CSR_STAT_IRQ, &csr->status);
}

/* return 0 to indicate transmit is pending */
int msgdma_tx_buffer(struct altera_tse_private *priv, struct tse_buffer *buffer)
{
	struct msgdma_extended_desc *desc = priv->tx_dma_desc;

	iowrite32(lower_32_bits(buffer->dma_addr), &desc->read_addr_lo);
	iowrite32(upper_32_bits(buffer->dma_addr), &desc->read_addr_hi);
	iowrite32(0, &desc->write_addr_lo);
	iowrite32(0, &desc->write_addr_hi);
	iowrite32(buffer->len, &desc->len);
	iowrite32(0, &desc->burst_seq_num);
	iowrite32(MSGDMA_DESC_TX_STRIDE, &desc->stride);
	iowrite32(MSGDMA_DESC_CTL_TX_SINGLE, &desc->control);
	return 0;
}

u32 msgdma_tx_completions(struct altera_tse_private *priv)
{
	u32 ready = 0;
	u32 inuse;
	u32 status;
	struct msgdma_csr *txcsr =
		(struct msgdma_csr *)priv->tx_dma_csr;

	/* Get number of sent descriptors */
	inuse = ioread32(&txcsr->rw_fill_level) & 0xffff;

	if (inuse) { /* Tx FIFO is not empty */
		ready = priv->tx_prod - priv->tx_cons - inuse - 1;
	} else {
		/* Check for buffered last packet */
		status = ioread32(&txcsr->status);
		if (status & MSGDMA_CSR_STAT_BUSY)
			ready = priv->tx_prod - priv->tx_cons - 1;
		else
			ready = priv->tx_prod - priv->tx_cons;
	}
	return ready;
}

/* Put buffer to the mSGDMA RX FIFO
 */
void msgdma_add_rx_desc(struct altera_tse_private *priv,
			struct tse_buffer *rxbuffer)
{
	struct msgdma_extended_desc *desc = priv->rx_dma_desc;
	u32 len = priv->rx_dma_buf_sz;
	dma_addr_t dma_addr = rxbuffer->dma_addr;
	u32 control = (MSGDMA_DESC_CTL_END_ON_EOP
			| MSGDMA_DESC_CTL_END_ON_LEN
			| MSGDMA_DESC_CTL_TR_COMP_IRQ
			| MSGDMA_DESC_CTL_EARLY_IRQ
			| MSGDMA_DESC_CTL_TR_ERR_IRQ
			| MSGDMA_DESC_CTL_GO);

	iowrite32(0, &desc->read_addr_lo);
	iowrite32(0, &desc->read_addr_hi);
	iowrite32(lower_32_bits(dma_addr), &desc->write_addr_lo);
	iowrite32(upper_32_bits(dma_addr), &desc->write_addr_hi);
	iowrite32(len, &desc->len);
	iowrite32(0, &desc->burst_seq_num);
	iowrite32(0x00010001, &desc->stride);
	iowrite32(control, &desc->control);
}

/* status is returned on upper 16 bits,
 * length is returned in lower 16 bits
 */
u32 msgdma_rx_status(struct altera_tse_private *priv)
{
	u32 rxstatus = 0;
	u32 pktlength;
	u32 pktstatus;
	struct msgdma_csr *rxcsr =
		(struct msgdma_csr *)priv->rx_dma_csr;
	struct msgdma_response *rxresp =
		(struct msgdma_response *)priv->rx_dma_resp;

	if (ioread32(&rxcsr->resp_fill_level) & 0xffff) {
		pktlength = ioread32(&rxresp->bytes_transferred);
		pktstatus = ioread32(&rxresp->status);
		rxstatus = pktstatus;
		rxstatus = rxstatus << 16;
		rxstatus |= (pktlength & 0xffff);
	}
	return rxstatus;
}
