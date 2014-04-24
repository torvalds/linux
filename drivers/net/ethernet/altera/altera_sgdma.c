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

#include <linux/list.h>
#include "altera_utils.h"
#include "altera_tse.h"
#include "altera_sgdmahw.h"
#include "altera_sgdma.h"

static void sgdma_descrip(struct sgdma_descrip *desc,
			  struct sgdma_descrip *ndesc,
			  dma_addr_t ndesc_phys,
			  dma_addr_t raddr,
			  dma_addr_t waddr,
			  u16 length,
			  int generate_eop,
			  int rfixed,
			  int wfixed);

static int sgdma_async_write(struct altera_tse_private *priv,
			      struct sgdma_descrip *desc);

static int sgdma_async_read(struct altera_tse_private *priv);

static dma_addr_t
sgdma_txphysaddr(struct altera_tse_private *priv,
		 struct sgdma_descrip *desc);

static dma_addr_t
sgdma_rxphysaddr(struct altera_tse_private *priv,
		 struct sgdma_descrip *desc);

static int sgdma_txbusy(struct altera_tse_private *priv);

static int sgdma_rxbusy(struct altera_tse_private *priv);

static void
queue_tx(struct altera_tse_private *priv, struct tse_buffer *buffer);

static void
queue_rx(struct altera_tse_private *priv, struct tse_buffer *buffer);

static struct tse_buffer *
dequeue_tx(struct altera_tse_private *priv);

static struct tse_buffer *
dequeue_rx(struct altera_tse_private *priv);

static struct tse_buffer *
queue_rx_peekhead(struct altera_tse_private *priv);

int sgdma_initialize(struct altera_tse_private *priv)
{
	priv->txctrlreg = SGDMA_CTRLREG_ILASTD |
		      SGDMA_CTRLREG_INTEN;

	priv->rxctrlreg = SGDMA_CTRLREG_IDESCRIP |
		      SGDMA_CTRLREG_INTEN |
		      SGDMA_CTRLREG_ILASTD;

	INIT_LIST_HEAD(&priv->txlisthd);
	INIT_LIST_HEAD(&priv->rxlisthd);

	priv->rxdescphys = (dma_addr_t) 0;
	priv->txdescphys = (dma_addr_t) 0;

	priv->rxdescphys = dma_map_single(priv->device, priv->rx_dma_desc,
					  priv->rxdescmem, DMA_BIDIRECTIONAL);

	if (dma_mapping_error(priv->device, priv->rxdescphys)) {
		sgdma_uninitialize(priv);
		netdev_err(priv->dev, "error mapping rx descriptor memory\n");
		return -EINVAL;
	}

	priv->txdescphys = dma_map_single(priv->device, priv->tx_dma_desc,
					  priv->txdescmem, DMA_TO_DEVICE);

	if (dma_mapping_error(priv->device, priv->txdescphys)) {
		sgdma_uninitialize(priv);
		netdev_err(priv->dev, "error mapping tx descriptor memory\n");
		return -EINVAL;
	}

	/* Initialize descriptor memory to all 0's, sync memory to cache */
	memset(priv->tx_dma_desc, 0, priv->txdescmem);
	memset(priv->rx_dma_desc, 0, priv->rxdescmem);

	dma_sync_single_for_device(priv->device, priv->txdescphys,
				   priv->txdescmem, DMA_TO_DEVICE);

	dma_sync_single_for_device(priv->device, priv->rxdescphys,
				   priv->rxdescmem, DMA_TO_DEVICE);

	return 0;
}

void sgdma_uninitialize(struct altera_tse_private *priv)
{
	if (priv->rxdescphys)
		dma_unmap_single(priv->device, priv->rxdescphys,
				 priv->rxdescmem, DMA_BIDIRECTIONAL);

	if (priv->txdescphys)
		dma_unmap_single(priv->device, priv->txdescphys,
				 priv->txdescmem, DMA_TO_DEVICE);
}

/* This function resets the SGDMA controller and clears the
 * descriptor memory used for transmits and receives.
 */
void sgdma_reset(struct altera_tse_private *priv)
{
	u32 *ptxdescripmem = priv->tx_dma_desc;
	u32 txdescriplen   = priv->txdescmem;
	u32 *prxdescripmem = priv->rx_dma_desc;
	u32 rxdescriplen   = priv->rxdescmem;
	struct sgdma_csr *ptxsgdma = priv->tx_dma_csr;
	struct sgdma_csr *prxsgdma = priv->rx_dma_csr;

	/* Initialize descriptor memory to 0 */
	memset(ptxdescripmem, 0, txdescriplen);
	memset(prxdescripmem, 0, rxdescriplen);

	priv->sgdmadesclen = sizeof(sgdma_descrip);

	iowrite32(SGDMA_CTRLREG_RESET, &ptxsgdma->control);
	iowrite32(0, &ptxsgdma->control);

	iowrite32(SGDMA_CTRLREG_RESET, &prxsgdma->control);
	iowrite32(0, &prxsgdma->control);
}

/* for SGDMA, RX interrupts remain enabled */
void sgdma_enable_rxirq(struct altera_tse_private *priv)
{
}

/* for SGDMA, RX interrupts remain enabled */
void sgdma_enable_txirq(struct altera_tse_private *priv)
{
}

/* for SGDMA, RX interrupts remain enabled */
void sgdma_disable_rxirq(struct altera_tse_private *priv)
{
}

/* for SGDMA, TX interrupts remain enabled */
void sgdma_disable_txirq(struct altera_tse_private *priv)
{
}

void sgdma_clear_rxirq(struct altera_tse_private *priv)
{
	struct sgdma_csr *csr = priv->rx_dma_csr;
	tse_set_bit(&csr->control, SGDMA_CTRLREG_CLRINT);
}

void sgdma_clear_txirq(struct altera_tse_private *priv)
{
	struct sgdma_csr *csr = priv->tx_dma_csr;
	tse_set_bit(&csr->control, SGDMA_CTRLREG_CLRINT);
}

/* transmits buffer through SGDMA. Returns number of buffers
 * transmitted, 0 if not possible.
 *
 * tx_lock is held by the caller
 */
int sgdma_tx_buffer(struct altera_tse_private *priv, struct tse_buffer *buffer)
{
	int pktstx = 0;
	struct sgdma_descrip *descbase = priv->tx_dma_desc;

	struct sgdma_descrip *cdesc = &descbase[0];
	struct sgdma_descrip *ndesc = &descbase[1];

	/* wait 'til the tx sgdma is ready for the next transmit request */
	if (sgdma_txbusy(priv))
		return 0;

	sgdma_descrip(cdesc,			/* current descriptor */
		      ndesc,			/* next descriptor */
		      sgdma_txphysaddr(priv, ndesc),
		      buffer->dma_addr,		/* address of packet to xmit */
		      0,			/* write addr 0 for tx dma */
		      buffer->len,		/* length of packet */
		      SGDMA_CONTROL_EOP,	/* Generate EOP */
		      0,			/* read fixed */
		      SGDMA_CONTROL_WR_FIXED);	/* Generate SOP */

	pktstx = sgdma_async_write(priv, cdesc);

	/* enqueue the request to the pending transmit queue */
	queue_tx(priv, buffer);

	return 1;
}


/* tx_lock held to protect access to queued tx list
 */
u32 sgdma_tx_completions(struct altera_tse_private *priv)
{
	u32 ready = 0;
	struct sgdma_descrip *desc = priv->tx_dma_desc;

	if (!sgdma_txbusy(priv) &&
	    ((desc->control & SGDMA_CONTROL_HW_OWNED) == 0) &&
	    (dequeue_tx(priv))) {
		ready = 1;
	}

	return ready;
}

void sgdma_start_rxdma(struct altera_tse_private *priv)
{
	sgdma_async_read(priv);
}

void sgdma_add_rx_desc(struct altera_tse_private *priv,
		      struct tse_buffer *rxbuffer)
{
	queue_rx(priv, rxbuffer);
}

/* status is returned on upper 16 bits,
 * length is returned in lower 16 bits
 */
u32 sgdma_rx_status(struct altera_tse_private *priv)
{
	struct sgdma_csr *csr = priv->rx_dma_csr;
	struct sgdma_descrip *base = priv->rx_dma_desc;
	struct sgdma_descrip *desc = NULL;
	int pktsrx;
	unsigned int rxstatus = 0;
	unsigned int pktlength = 0;
	unsigned int pktstatus = 0;
	struct tse_buffer *rxbuffer = NULL;

	u32 sts = ioread32(&csr->status);

	desc = &base[0];
	if (sts & SGDMA_STSREG_EOP) {
		dma_sync_single_for_cpu(priv->device,
					priv->rxdescphys,
					priv->sgdmadesclen,
					DMA_FROM_DEVICE);

		pktlength = desc->bytes_xferred;
		pktstatus = desc->status & 0x3f;
		rxstatus = pktstatus;
		rxstatus = rxstatus << 16;
		rxstatus |= (pktlength & 0xffff);

		if (rxstatus) {
			desc->status = 0;

			rxbuffer = dequeue_rx(priv);
			if (rxbuffer == NULL)
				netdev_info(priv->dev,
					    "sgdma rx and rx queue empty!\n");

			/* Clear control */
			iowrite32(0, &csr->control);
			/* clear status */
			iowrite32(0xf, &csr->status);

			/* kick the rx sgdma after reaping this descriptor */
			pktsrx = sgdma_async_read(priv);

		} else {
			/* If the SGDMA indicated an end of packet on recv,
			 * then it's expected that the rxstatus from the
			 * descriptor is non-zero - meaning a valid packet
			 * with a nonzero length, or an error has been
			 * indicated. if not, then all we can do is signal
			 * an error and return no packet received. Most likely
			 * there is a system design error, or an error in the
			 * underlying kernel (cache or cache management problem)
			 */
			netdev_err(priv->dev,
				   "SGDMA RX Error Info: %x, %x, %x\n",
				   sts, desc->status, rxstatus);
		}
	} else if (sts == 0) {
		pktsrx = sgdma_async_read(priv);
	}
	return rxstatus;
}


/* Private functions */
static void sgdma_descrip(struct sgdma_descrip *desc,
			  struct sgdma_descrip *ndesc,
			  dma_addr_t ndesc_phys,
			  dma_addr_t raddr,
			  dma_addr_t waddr,
			  u16 length,
			  int generate_eop,
			  int rfixed,
			  int wfixed)
{
	/* Clear the next descriptor as not owned by hardware */
	u32 ctrl = ndesc->control;
	ctrl &= ~SGDMA_CONTROL_HW_OWNED;
	ndesc->control = ctrl;

	ctrl = 0;
	ctrl = SGDMA_CONTROL_HW_OWNED;
	ctrl |= generate_eop;
	ctrl |= rfixed;
	ctrl |= wfixed;

	/* Channel is implicitly zero, initialized to 0 by default */

	desc->raddr = raddr;
	desc->waddr = waddr;
	desc->next = lower_32_bits(ndesc_phys);
	desc->control = ctrl;
	desc->status = 0;
	desc->rburst = 0;
	desc->wburst = 0;
	desc->bytes = length;
	desc->bytes_xferred = 0;
}

/* If hardware is busy, don't restart async read.
 * if status register is 0 - meaning initial state, restart async read,
 * probably for the first time when populating a receive buffer.
 * If read status indicate not busy and a status, restart the async
 * DMA read.
 */
static int sgdma_async_read(struct altera_tse_private *priv)
{
	struct sgdma_csr *csr = priv->rx_dma_csr;
	struct sgdma_descrip *descbase = priv->rx_dma_desc;
	struct sgdma_descrip *cdesc = &descbase[0];
	struct sgdma_descrip *ndesc = &descbase[1];

	struct tse_buffer *rxbuffer = NULL;

	if (!sgdma_rxbusy(priv)) {
		rxbuffer = queue_rx_peekhead(priv);
		if (rxbuffer == NULL) {
			netdev_err(priv->dev, "no rx buffers available\n");
			return 0;
		}

		sgdma_descrip(cdesc,		/* current descriptor */
			      ndesc,		/* next descriptor */
			      sgdma_rxphysaddr(priv, ndesc),
			      0,		/* read addr 0 for rx dma */
			      rxbuffer->dma_addr, /* write addr for rx dma */
			      0,		/* read 'til EOP */
			      0,		/* EOP: NA for rx dma */
			      0,		/* read fixed: NA for rx dma */
			      0);		/* SOP: NA for rx DMA */

		dma_sync_single_for_device(priv->device,
					   priv->rxdescphys,
					   priv->sgdmadesclen,
					   DMA_TO_DEVICE);

		iowrite32(lower_32_bits(sgdma_rxphysaddr(priv, cdesc)),
			  &csr->next_descrip);

		iowrite32((priv->rxctrlreg | SGDMA_CTRLREG_START),
			  &csr->control);

		return 1;
	}

	return 0;
}

static int sgdma_async_write(struct altera_tse_private *priv,
			     struct sgdma_descrip *desc)
{
	struct sgdma_csr *csr = priv->tx_dma_csr;

	if (sgdma_txbusy(priv))
		return 0;

	/* clear control and status */
	iowrite32(0, &csr->control);
	iowrite32(0x1f, &csr->status);

	dma_sync_single_for_device(priv->device, priv->txdescphys,
				   priv->sgdmadesclen, DMA_TO_DEVICE);

	iowrite32(lower_32_bits(sgdma_txphysaddr(priv, desc)),
		  &csr->next_descrip);

	iowrite32((priv->txctrlreg | SGDMA_CTRLREG_START),
		  &csr->control);

	return 1;
}

static dma_addr_t
sgdma_txphysaddr(struct altera_tse_private *priv,
		 struct sgdma_descrip *desc)
{
	dma_addr_t paddr = priv->txdescmem_busaddr;
	uintptr_t offs = (uintptr_t)desc - (uintptr_t)priv->tx_dma_desc;
	return (dma_addr_t)((uintptr_t)paddr + offs);
}

static dma_addr_t
sgdma_rxphysaddr(struct altera_tse_private *priv,
		 struct sgdma_descrip *desc)
{
	dma_addr_t paddr = priv->rxdescmem_busaddr;
	uintptr_t offs = (uintptr_t)desc - (uintptr_t)priv->rx_dma_desc;
	return (dma_addr_t)((uintptr_t)paddr + offs);
}

#define list_remove_head(list, entry, type, member)			\
	do {								\
		entry = NULL;						\
		if (!list_empty(list)) {				\
			entry = list_entry((list)->next, type, member);	\
			list_del_init(&entry->member);			\
		}							\
	} while (0)

#define list_peek_head(list, entry, type, member)			\
	do {								\
		entry = NULL;						\
		if (!list_empty(list)) {				\
			entry = list_entry((list)->next, type, member);	\
		}							\
	} while (0)

/* adds a tse_buffer to the tail of a tx buffer list.
 * assumes the caller is managing and holding a mutual exclusion
 * primitive to avoid simultaneous pushes/pops to the list.
 */
static void
queue_tx(struct altera_tse_private *priv, struct tse_buffer *buffer)
{
	list_add_tail(&buffer->lh, &priv->txlisthd);
}


/* adds a tse_buffer to the tail of a rx buffer list
 * assumes the caller is managing and holding a mutual exclusion
 * primitive to avoid simultaneous pushes/pops to the list.
 */
static void
queue_rx(struct altera_tse_private *priv, struct tse_buffer *buffer)
{
	list_add_tail(&buffer->lh, &priv->rxlisthd);
}

/* dequeues a tse_buffer from the transmit buffer list, otherwise
 * returns NULL if empty.
 * assumes the caller is managing and holding a mutual exclusion
 * primitive to avoid simultaneous pushes/pops to the list.
 */
static struct tse_buffer *
dequeue_tx(struct altera_tse_private *priv)
{
	struct tse_buffer *buffer = NULL;
	list_remove_head(&priv->txlisthd, buffer, struct tse_buffer, lh);
	return buffer;
}

/* dequeues a tse_buffer from the receive buffer list, otherwise
 * returns NULL if empty
 * assumes the caller is managing and holding a mutual exclusion
 * primitive to avoid simultaneous pushes/pops to the list.
 */
static struct tse_buffer *
dequeue_rx(struct altera_tse_private *priv)
{
	struct tse_buffer *buffer = NULL;
	list_remove_head(&priv->rxlisthd, buffer, struct tse_buffer, lh);
	return buffer;
}

/* dequeues a tse_buffer from the receive buffer list, otherwise
 * returns NULL if empty
 * assumes the caller is managing and holding a mutual exclusion
 * primitive to avoid simultaneous pushes/pops to the list while the
 * head is being examined.
 */
static struct tse_buffer *
queue_rx_peekhead(struct altera_tse_private *priv)
{
	struct tse_buffer *buffer = NULL;
	list_peek_head(&priv->rxlisthd, buffer, struct tse_buffer, lh);
	return buffer;
}

/* check and return rx sgdma status without polling
 */
static int sgdma_rxbusy(struct altera_tse_private *priv)
{
	struct sgdma_csr *csr = priv->rx_dma_csr;
	return ioread32(&csr->status) & SGDMA_STSREG_BUSY;
}

/* waits for the tx sgdma to finish it's current operation, returns 0
 * when it transitions to nonbusy, returns 1 if the operation times out
 */
static int sgdma_txbusy(struct altera_tse_private *priv)
{
	int delay = 0;
	struct sgdma_csr *csr = priv->tx_dma_csr;

	/* if DMA is busy, wait for current transactino to finish */
	while ((ioread32(&csr->status) & SGDMA_STSREG_BUSY) && (delay++ < 100))
		udelay(1);

	if (ioread32(&csr->status) & SGDMA_STSREG_BUSY) {
		netdev_err(priv->dev, "timeout waiting for tx dma\n");
		return 1;
	}
	return 0;
}
