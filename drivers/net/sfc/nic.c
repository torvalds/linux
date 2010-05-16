/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2006-2009 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include "net_driver.h"
#include "bitfield.h"
#include "efx.h"
#include "nic.h"
#include "regs.h"
#include "io.h"
#include "workarounds.h"

/**************************************************************************
 *
 * Configurable values
 *
 **************************************************************************
 */

/* This is set to 16 for a good reason.  In summary, if larger than
 * 16, the descriptor cache holds more than a default socket
 * buffer's worth of packets (for UDP we can only have at most one
 * socket buffer's worth outstanding).  This combined with the fact
 * that we only get 1 TX event per descriptor cache means the NIC
 * goes idle.
 */
#define TX_DC_ENTRIES 16
#define TX_DC_ENTRIES_ORDER 1

#define RX_DC_ENTRIES 64
#define RX_DC_ENTRIES_ORDER 3

/* RX FIFO XOFF watermark
 *
 * When the amount of the RX FIFO increases used increases past this
 * watermark send XOFF. Only used if RX flow control is enabled (ethtool -A)
 * This also has an effect on RX/TX arbitration
 */
int efx_nic_rx_xoff_thresh = -1;
module_param_named(rx_xoff_thresh_bytes, efx_nic_rx_xoff_thresh, int, 0644);
MODULE_PARM_DESC(rx_xoff_thresh_bytes, "RX fifo XOFF threshold");

/* RX FIFO XON watermark
 *
 * When the amount of the RX FIFO used decreases below this
 * watermark send XON. Only used if TX flow control is enabled (ethtool -A)
 * This also has an effect on RX/TX arbitration
 */
int efx_nic_rx_xon_thresh = -1;
module_param_named(rx_xon_thresh_bytes, efx_nic_rx_xon_thresh, int, 0644);
MODULE_PARM_DESC(rx_xon_thresh_bytes, "RX fifo XON threshold");

/* If EFX_MAX_INT_ERRORS internal errors occur within
 * EFX_INT_ERROR_EXPIRE seconds, we consider the NIC broken and
 * disable it.
 */
#define EFX_INT_ERROR_EXPIRE 3600
#define EFX_MAX_INT_ERRORS 5

/* We poll for events every FLUSH_INTERVAL ms, and check FLUSH_POLL_COUNT times
 */
#define EFX_FLUSH_INTERVAL 10
#define EFX_FLUSH_POLL_COUNT 100

/* Size and alignment of special buffers (4KB) */
#define EFX_BUF_SIZE 4096

/* Depth of RX flush request fifo */
#define EFX_RX_FLUSH_COUNT 4

/**************************************************************************
 *
 * Solarstorm hardware access
 *
 **************************************************************************/

static inline void efx_write_buf_tbl(struct efx_nic *efx, efx_qword_t *value,
				     unsigned int index)
{
	efx_sram_writeq(efx, efx->membase + efx->type->buf_tbl_base,
			value, index);
}

/* Read the current event from the event queue */
static inline efx_qword_t *efx_event(struct efx_channel *channel,
				     unsigned int index)
{
	return (((efx_qword_t *) (channel->eventq.addr)) + index);
}

/* See if an event is present
 *
 * We check both the high and low dword of the event for all ones.  We
 * wrote all ones when we cleared the event, and no valid event can
 * have all ones in either its high or low dwords.  This approach is
 * robust against reordering.
 *
 * Note that using a single 64-bit comparison is incorrect; even
 * though the CPU read will be atomic, the DMA write may not be.
 */
static inline int efx_event_present(efx_qword_t *event)
{
	return (!(EFX_DWORD_IS_ALL_ONES(event->dword[0]) |
		  EFX_DWORD_IS_ALL_ONES(event->dword[1])));
}

static bool efx_masked_compare_oword(const efx_oword_t *a, const efx_oword_t *b,
				     const efx_oword_t *mask)
{
	return ((a->u64[0] ^ b->u64[0]) & mask->u64[0]) ||
		((a->u64[1] ^ b->u64[1]) & mask->u64[1]);
}

int efx_nic_test_registers(struct efx_nic *efx,
			   const struct efx_nic_register_test *regs,
			   size_t n_regs)
{
	unsigned address = 0, i, j;
	efx_oword_t mask, imask, original, reg, buf;

	/* Falcon should be in loopback to isolate the XMAC from the PHY */
	WARN_ON(!LOOPBACK_INTERNAL(efx));

	for (i = 0; i < n_regs; ++i) {
		address = regs[i].address;
		mask = imask = regs[i].mask;
		EFX_INVERT_OWORD(imask);

		efx_reado(efx, &original, address);

		/* bit sweep on and off */
		for (j = 0; j < 128; j++) {
			if (!EFX_EXTRACT_OWORD32(mask, j, j))
				continue;

			/* Test this testable bit can be set in isolation */
			EFX_AND_OWORD(reg, original, mask);
			EFX_SET_OWORD32(reg, j, j, 1);

			efx_writeo(efx, &reg, address);
			efx_reado(efx, &buf, address);

			if (efx_masked_compare_oword(&reg, &buf, &mask))
				goto fail;

			/* Test this testable bit can be cleared in isolation */
			EFX_OR_OWORD(reg, original, mask);
			EFX_SET_OWORD32(reg, j, j, 0);

			efx_writeo(efx, &reg, address);
			efx_reado(efx, &buf, address);

			if (efx_masked_compare_oword(&reg, &buf, &mask))
				goto fail;
		}

		efx_writeo(efx, &original, address);
	}

	return 0;

fail:
	EFX_ERR(efx, "wrote "EFX_OWORD_FMT" read "EFX_OWORD_FMT
		" at address 0x%x mask "EFX_OWORD_FMT"\n", EFX_OWORD_VAL(reg),
		EFX_OWORD_VAL(buf), address, EFX_OWORD_VAL(mask));
	return -EIO;
}

/**************************************************************************
 *
 * Special buffer handling
 * Special buffers are used for event queues and the TX and RX
 * descriptor rings.
 *
 *************************************************************************/

/*
 * Initialise a special buffer
 *
 * This will define a buffer (previously allocated via
 * efx_alloc_special_buffer()) in the buffer table, allowing
 * it to be used for event queues, descriptor rings etc.
 */
static void
efx_init_special_buffer(struct efx_nic *efx, struct efx_special_buffer *buffer)
{
	efx_qword_t buf_desc;
	int index;
	dma_addr_t dma_addr;
	int i;

	EFX_BUG_ON_PARANOID(!buffer->addr);

	/* Write buffer descriptors to NIC */
	for (i = 0; i < buffer->entries; i++) {
		index = buffer->index + i;
		dma_addr = buffer->dma_addr + (i * 4096);
		EFX_LOG(efx, "mapping special buffer %d at %llx\n",
			index, (unsigned long long)dma_addr);
		EFX_POPULATE_QWORD_3(buf_desc,
				     FRF_AZ_BUF_ADR_REGION, 0,
				     FRF_AZ_BUF_ADR_FBUF, dma_addr >> 12,
				     FRF_AZ_BUF_OWNER_ID_FBUF, 0);
		efx_write_buf_tbl(efx, &buf_desc, index);
	}
}

/* Unmaps a buffer and clears the buffer table entries */
static void
efx_fini_special_buffer(struct efx_nic *efx, struct efx_special_buffer *buffer)
{
	efx_oword_t buf_tbl_upd;
	unsigned int start = buffer->index;
	unsigned int end = (buffer->index + buffer->entries - 1);

	if (!buffer->entries)
		return;

	EFX_LOG(efx, "unmapping special buffers %d-%d\n",
		buffer->index, buffer->index + buffer->entries - 1);

	EFX_POPULATE_OWORD_4(buf_tbl_upd,
			     FRF_AZ_BUF_UPD_CMD, 0,
			     FRF_AZ_BUF_CLR_CMD, 1,
			     FRF_AZ_BUF_CLR_END_ID, end,
			     FRF_AZ_BUF_CLR_START_ID, start);
	efx_writeo(efx, &buf_tbl_upd, FR_AZ_BUF_TBL_UPD);
}

/*
 * Allocate a new special buffer
 *
 * This allocates memory for a new buffer, clears it and allocates a
 * new buffer ID range.  It does not write into the buffer table.
 *
 * This call will allocate 4KB buffers, since 8KB buffers can't be
 * used for event queues and descriptor rings.
 */
static int efx_alloc_special_buffer(struct efx_nic *efx,
				    struct efx_special_buffer *buffer,
				    unsigned int len)
{
	len = ALIGN(len, EFX_BUF_SIZE);

	buffer->addr = pci_alloc_consistent(efx->pci_dev, len,
					    &buffer->dma_addr);
	if (!buffer->addr)
		return -ENOMEM;
	buffer->len = len;
	buffer->entries = len / EFX_BUF_SIZE;
	BUG_ON(buffer->dma_addr & (EFX_BUF_SIZE - 1));

	/* All zeros is a potentially valid event so memset to 0xff */
	memset(buffer->addr, 0xff, len);

	/* Select new buffer ID */
	buffer->index = efx->next_buffer_table;
	efx->next_buffer_table += buffer->entries;

	EFX_LOG(efx, "allocating special buffers %d-%d at %llx+%x "
		"(virt %p phys %llx)\n", buffer->index,
		buffer->index + buffer->entries - 1,
		(u64)buffer->dma_addr, len,
		buffer->addr, (u64)virt_to_phys(buffer->addr));

	return 0;
}

static void
efx_free_special_buffer(struct efx_nic *efx, struct efx_special_buffer *buffer)
{
	if (!buffer->addr)
		return;

	EFX_LOG(efx, "deallocating special buffers %d-%d at %llx+%x "
		"(virt %p phys %llx)\n", buffer->index,
		buffer->index + buffer->entries - 1,
		(u64)buffer->dma_addr, buffer->len,
		buffer->addr, (u64)virt_to_phys(buffer->addr));

	pci_free_consistent(efx->pci_dev, buffer->len, buffer->addr,
			    buffer->dma_addr);
	buffer->addr = NULL;
	buffer->entries = 0;
}

/**************************************************************************
 *
 * Generic buffer handling
 * These buffers are used for interrupt status and MAC stats
 *
 **************************************************************************/

int efx_nic_alloc_buffer(struct efx_nic *efx, struct efx_buffer *buffer,
			 unsigned int len)
{
	buffer->addr = pci_alloc_consistent(efx->pci_dev, len,
					    &buffer->dma_addr);
	if (!buffer->addr)
		return -ENOMEM;
	buffer->len = len;
	memset(buffer->addr, 0, len);
	return 0;
}

void efx_nic_free_buffer(struct efx_nic *efx, struct efx_buffer *buffer)
{
	if (buffer->addr) {
		pci_free_consistent(efx->pci_dev, buffer->len,
				    buffer->addr, buffer->dma_addr);
		buffer->addr = NULL;
	}
}

/**************************************************************************
 *
 * TX path
 *
 **************************************************************************/

/* Returns a pointer to the specified transmit descriptor in the TX
 * descriptor queue belonging to the specified channel.
 */
static inline efx_qword_t *
efx_tx_desc(struct efx_tx_queue *tx_queue, unsigned int index)
{
	return (((efx_qword_t *) (tx_queue->txd.addr)) + index);
}

/* This writes to the TX_DESC_WPTR; write pointer for TX descriptor ring */
static inline void efx_notify_tx_desc(struct efx_tx_queue *tx_queue)
{
	unsigned write_ptr;
	efx_dword_t reg;

	write_ptr = tx_queue->write_count & EFX_TXQ_MASK;
	EFX_POPULATE_DWORD_1(reg, FRF_AZ_TX_DESC_WPTR_DWORD, write_ptr);
	efx_writed_page(tx_queue->efx, &reg,
			FR_AZ_TX_DESC_UPD_DWORD_P0, tx_queue->queue);
}


/* For each entry inserted into the software descriptor ring, create a
 * descriptor in the hardware TX descriptor ring (in host memory), and
 * write a doorbell.
 */
void efx_nic_push_buffers(struct efx_tx_queue *tx_queue)
{

	struct efx_tx_buffer *buffer;
	efx_qword_t *txd;
	unsigned write_ptr;

	BUG_ON(tx_queue->write_count == tx_queue->insert_count);

	do {
		write_ptr = tx_queue->write_count & EFX_TXQ_MASK;
		buffer = &tx_queue->buffer[write_ptr];
		txd = efx_tx_desc(tx_queue, write_ptr);
		++tx_queue->write_count;

		/* Create TX descriptor ring entry */
		EFX_POPULATE_QWORD_4(*txd,
				     FSF_AZ_TX_KER_CONT, buffer->continuation,
				     FSF_AZ_TX_KER_BYTE_COUNT, buffer->len,
				     FSF_AZ_TX_KER_BUF_REGION, 0,
				     FSF_AZ_TX_KER_BUF_ADDR, buffer->dma_addr);
	} while (tx_queue->write_count != tx_queue->insert_count);

	wmb(); /* Ensure descriptors are written before they are fetched */
	efx_notify_tx_desc(tx_queue);
}

/* Allocate hardware resources for a TX queue */
int efx_nic_probe_tx(struct efx_tx_queue *tx_queue)
{
	struct efx_nic *efx = tx_queue->efx;
	BUILD_BUG_ON(EFX_TXQ_SIZE < 512 || EFX_TXQ_SIZE > 4096 ||
		     EFX_TXQ_SIZE & EFX_TXQ_MASK);
	return efx_alloc_special_buffer(efx, &tx_queue->txd,
					EFX_TXQ_SIZE * sizeof(efx_qword_t));
}

void efx_nic_init_tx(struct efx_tx_queue *tx_queue)
{
	efx_oword_t tx_desc_ptr;
	struct efx_nic *efx = tx_queue->efx;

	tx_queue->flushed = FLUSH_NONE;

	/* Pin TX descriptor ring */
	efx_init_special_buffer(efx, &tx_queue->txd);

	/* Push TX descriptor ring to card */
	EFX_POPULATE_OWORD_10(tx_desc_ptr,
			      FRF_AZ_TX_DESCQ_EN, 1,
			      FRF_AZ_TX_ISCSI_DDIG_EN, 0,
			      FRF_AZ_TX_ISCSI_HDIG_EN, 0,
			      FRF_AZ_TX_DESCQ_BUF_BASE_ID, tx_queue->txd.index,
			      FRF_AZ_TX_DESCQ_EVQ_ID,
			      tx_queue->channel->channel,
			      FRF_AZ_TX_DESCQ_OWNER_ID, 0,
			      FRF_AZ_TX_DESCQ_LABEL, tx_queue->queue,
			      FRF_AZ_TX_DESCQ_SIZE,
			      __ffs(tx_queue->txd.entries),
			      FRF_AZ_TX_DESCQ_TYPE, 0,
			      FRF_BZ_TX_NON_IP_DROP_DIS, 1);

	if (efx_nic_rev(efx) >= EFX_REV_FALCON_B0) {
		int csum = tx_queue->queue == EFX_TX_QUEUE_OFFLOAD_CSUM;
		EFX_SET_OWORD_FIELD(tx_desc_ptr, FRF_BZ_TX_IP_CHKSM_DIS, !csum);
		EFX_SET_OWORD_FIELD(tx_desc_ptr, FRF_BZ_TX_TCP_CHKSM_DIS,
				    !csum);
	}

	efx_writeo_table(efx, &tx_desc_ptr, efx->type->txd_ptr_tbl_base,
			 tx_queue->queue);

	if (efx_nic_rev(efx) < EFX_REV_FALCON_B0) {
		efx_oword_t reg;

		/* Only 128 bits in this register */
		BUILD_BUG_ON(EFX_TX_QUEUE_COUNT >= 128);

		efx_reado(efx, &reg, FR_AA_TX_CHKSM_CFG);
		if (tx_queue->queue == EFX_TX_QUEUE_OFFLOAD_CSUM)
			clear_bit_le(tx_queue->queue, (void *)&reg);
		else
			set_bit_le(tx_queue->queue, (void *)&reg);
		efx_writeo(efx, &reg, FR_AA_TX_CHKSM_CFG);
	}
}

static void efx_flush_tx_queue(struct efx_tx_queue *tx_queue)
{
	struct efx_nic *efx = tx_queue->efx;
	efx_oword_t tx_flush_descq;

	tx_queue->flushed = FLUSH_PENDING;

	/* Post a flush command */
	EFX_POPULATE_OWORD_2(tx_flush_descq,
			     FRF_AZ_TX_FLUSH_DESCQ_CMD, 1,
			     FRF_AZ_TX_FLUSH_DESCQ, tx_queue->queue);
	efx_writeo(efx, &tx_flush_descq, FR_AZ_TX_FLUSH_DESCQ);
}

void efx_nic_fini_tx(struct efx_tx_queue *tx_queue)
{
	struct efx_nic *efx = tx_queue->efx;
	efx_oword_t tx_desc_ptr;

	/* The queue should have been flushed */
	WARN_ON(tx_queue->flushed != FLUSH_DONE);

	/* Remove TX descriptor ring from card */
	EFX_ZERO_OWORD(tx_desc_ptr);
	efx_writeo_table(efx, &tx_desc_ptr, efx->type->txd_ptr_tbl_base,
			 tx_queue->queue);

	/* Unpin TX descriptor ring */
	efx_fini_special_buffer(efx, &tx_queue->txd);
}

/* Free buffers backing TX queue */
void efx_nic_remove_tx(struct efx_tx_queue *tx_queue)
{
	efx_free_special_buffer(tx_queue->efx, &tx_queue->txd);
}

/**************************************************************************
 *
 * RX path
 *
 **************************************************************************/

/* Returns a pointer to the specified descriptor in the RX descriptor queue */
static inline efx_qword_t *
efx_rx_desc(struct efx_rx_queue *rx_queue, unsigned int index)
{
	return (((efx_qword_t *) (rx_queue->rxd.addr)) + index);
}

/* This creates an entry in the RX descriptor queue */
static inline void
efx_build_rx_desc(struct efx_rx_queue *rx_queue, unsigned index)
{
	struct efx_rx_buffer *rx_buf;
	efx_qword_t *rxd;

	rxd = efx_rx_desc(rx_queue, index);
	rx_buf = efx_rx_buffer(rx_queue, index);
	EFX_POPULATE_QWORD_3(*rxd,
			     FSF_AZ_RX_KER_BUF_SIZE,
			     rx_buf->len -
			     rx_queue->efx->type->rx_buffer_padding,
			     FSF_AZ_RX_KER_BUF_REGION, 0,
			     FSF_AZ_RX_KER_BUF_ADDR, rx_buf->dma_addr);
}

/* This writes to the RX_DESC_WPTR register for the specified receive
 * descriptor ring.
 */
void efx_nic_notify_rx_desc(struct efx_rx_queue *rx_queue)
{
	efx_dword_t reg;
	unsigned write_ptr;

	while (rx_queue->notified_count != rx_queue->added_count) {
		efx_build_rx_desc(rx_queue,
				  rx_queue->notified_count &
				  EFX_RXQ_MASK);
		++rx_queue->notified_count;
	}

	wmb();
	write_ptr = rx_queue->added_count & EFX_RXQ_MASK;
	EFX_POPULATE_DWORD_1(reg, FRF_AZ_RX_DESC_WPTR_DWORD, write_ptr);
	efx_writed_page(rx_queue->efx, &reg,
			FR_AZ_RX_DESC_UPD_DWORD_P0, rx_queue->queue);
}

int efx_nic_probe_rx(struct efx_rx_queue *rx_queue)
{
	struct efx_nic *efx = rx_queue->efx;
	BUILD_BUG_ON(EFX_RXQ_SIZE < 512 || EFX_RXQ_SIZE > 4096 ||
		     EFX_RXQ_SIZE & EFX_RXQ_MASK);
	return efx_alloc_special_buffer(efx, &rx_queue->rxd,
					EFX_RXQ_SIZE * sizeof(efx_qword_t));
}

void efx_nic_init_rx(struct efx_rx_queue *rx_queue)
{
	efx_oword_t rx_desc_ptr;
	struct efx_nic *efx = rx_queue->efx;
	bool is_b0 = efx_nic_rev(efx) >= EFX_REV_FALCON_B0;
	bool iscsi_digest_en = is_b0;

	EFX_LOG(efx, "RX queue %d ring in special buffers %d-%d\n",
		rx_queue->queue, rx_queue->rxd.index,
		rx_queue->rxd.index + rx_queue->rxd.entries - 1);

	rx_queue->flushed = FLUSH_NONE;

	/* Pin RX descriptor ring */
	efx_init_special_buffer(efx, &rx_queue->rxd);

	/* Push RX descriptor ring to card */
	EFX_POPULATE_OWORD_10(rx_desc_ptr,
			      FRF_AZ_RX_ISCSI_DDIG_EN, iscsi_digest_en,
			      FRF_AZ_RX_ISCSI_HDIG_EN, iscsi_digest_en,
			      FRF_AZ_RX_DESCQ_BUF_BASE_ID, rx_queue->rxd.index,
			      FRF_AZ_RX_DESCQ_EVQ_ID,
			      rx_queue->channel->channel,
			      FRF_AZ_RX_DESCQ_OWNER_ID, 0,
			      FRF_AZ_RX_DESCQ_LABEL, rx_queue->queue,
			      FRF_AZ_RX_DESCQ_SIZE,
			      __ffs(rx_queue->rxd.entries),
			      FRF_AZ_RX_DESCQ_TYPE, 0 /* kernel queue */ ,
			      /* For >=B0 this is scatter so disable */
			      FRF_AZ_RX_DESCQ_JUMBO, !is_b0,
			      FRF_AZ_RX_DESCQ_EN, 1);
	efx_writeo_table(efx, &rx_desc_ptr, efx->type->rxd_ptr_tbl_base,
			 rx_queue->queue);
}

static void efx_flush_rx_queue(struct efx_rx_queue *rx_queue)
{
	struct efx_nic *efx = rx_queue->efx;
	efx_oword_t rx_flush_descq;

	rx_queue->flushed = FLUSH_PENDING;

	/* Post a flush command */
	EFX_POPULATE_OWORD_2(rx_flush_descq,
			     FRF_AZ_RX_FLUSH_DESCQ_CMD, 1,
			     FRF_AZ_RX_FLUSH_DESCQ, rx_queue->queue);
	efx_writeo(efx, &rx_flush_descq, FR_AZ_RX_FLUSH_DESCQ);
}

void efx_nic_fini_rx(struct efx_rx_queue *rx_queue)
{
	efx_oword_t rx_desc_ptr;
	struct efx_nic *efx = rx_queue->efx;

	/* The queue should already have been flushed */
	WARN_ON(rx_queue->flushed != FLUSH_DONE);

	/* Remove RX descriptor ring from card */
	EFX_ZERO_OWORD(rx_desc_ptr);
	efx_writeo_table(efx, &rx_desc_ptr, efx->type->rxd_ptr_tbl_base,
			 rx_queue->queue);

	/* Unpin RX descriptor ring */
	efx_fini_special_buffer(efx, &rx_queue->rxd);
}

/* Free buffers backing RX queue */
void efx_nic_remove_rx(struct efx_rx_queue *rx_queue)
{
	efx_free_special_buffer(rx_queue->efx, &rx_queue->rxd);
}

/**************************************************************************
 *
 * Event queue processing
 * Event queues are processed by per-channel tasklets.
 *
 **************************************************************************/

/* Update a channel's event queue's read pointer (RPTR) register
 *
 * This writes the EVQ_RPTR_REG register for the specified channel's
 * event queue.
 */
void efx_nic_eventq_read_ack(struct efx_channel *channel)
{
	efx_dword_t reg;
	struct efx_nic *efx = channel->efx;

	EFX_POPULATE_DWORD_1(reg, FRF_AZ_EVQ_RPTR, channel->eventq_read_ptr);
	efx_writed_table(efx, &reg, efx->type->evq_rptr_tbl_base,
			 channel->channel);
}

/* Use HW to insert a SW defined event */
void efx_generate_event(struct efx_channel *channel, efx_qword_t *event)
{
	efx_oword_t drv_ev_reg;

	BUILD_BUG_ON(FRF_AZ_DRV_EV_DATA_LBN != 0 ||
		     FRF_AZ_DRV_EV_DATA_WIDTH != 64);
	drv_ev_reg.u32[0] = event->u32[0];
	drv_ev_reg.u32[1] = event->u32[1];
	drv_ev_reg.u32[2] = 0;
	drv_ev_reg.u32[3] = 0;
	EFX_SET_OWORD_FIELD(drv_ev_reg, FRF_AZ_DRV_EV_QID, channel->channel);
	efx_writeo(channel->efx, &drv_ev_reg, FR_AZ_DRV_EV);
}

/* Handle a transmit completion event
 *
 * The NIC batches TX completion events; the message we receive is of
 * the form "complete all TX events up to this index".
 */
static void
efx_handle_tx_event(struct efx_channel *channel, efx_qword_t *event)
{
	unsigned int tx_ev_desc_ptr;
	unsigned int tx_ev_q_label;
	struct efx_tx_queue *tx_queue;
	struct efx_nic *efx = channel->efx;

	if (likely(EFX_QWORD_FIELD(*event, FSF_AZ_TX_EV_COMP))) {
		/* Transmit completion */
		tx_ev_desc_ptr = EFX_QWORD_FIELD(*event, FSF_AZ_TX_EV_DESC_PTR);
		tx_ev_q_label = EFX_QWORD_FIELD(*event, FSF_AZ_TX_EV_Q_LABEL);
		tx_queue = &efx->tx_queue[tx_ev_q_label];
		channel->irq_mod_score +=
			(tx_ev_desc_ptr - tx_queue->read_count) &
			EFX_TXQ_MASK;
		efx_xmit_done(tx_queue, tx_ev_desc_ptr);
	} else if (EFX_QWORD_FIELD(*event, FSF_AZ_TX_EV_WQ_FF_FULL)) {
		/* Rewrite the FIFO write pointer */
		tx_ev_q_label = EFX_QWORD_FIELD(*event, FSF_AZ_TX_EV_Q_LABEL);
		tx_queue = &efx->tx_queue[tx_ev_q_label];

		if (efx_dev_registered(efx))
			netif_tx_lock(efx->net_dev);
		efx_notify_tx_desc(tx_queue);
		if (efx_dev_registered(efx))
			netif_tx_unlock(efx->net_dev);
	} else if (EFX_QWORD_FIELD(*event, FSF_AZ_TX_EV_PKT_ERR) &&
		   EFX_WORKAROUND_10727(efx)) {
		efx_schedule_reset(efx, RESET_TYPE_TX_DESC_FETCH);
	} else {
		EFX_ERR(efx, "channel %d unexpected TX event "
			EFX_QWORD_FMT"\n", channel->channel,
			EFX_QWORD_VAL(*event));
	}
}

/* Detect errors included in the rx_evt_pkt_ok bit. */
static void efx_handle_rx_not_ok(struct efx_rx_queue *rx_queue,
				 const efx_qword_t *event,
				 bool *rx_ev_pkt_ok,
				 bool *discard)
{
	struct efx_nic *efx = rx_queue->efx;
	bool rx_ev_buf_owner_id_err, rx_ev_ip_hdr_chksum_err;
	bool rx_ev_tcp_udp_chksum_err, rx_ev_eth_crc_err;
	bool rx_ev_frm_trunc, rx_ev_drib_nib, rx_ev_tobe_disc;
	bool rx_ev_other_err, rx_ev_pause_frm;
	bool rx_ev_hdr_type, rx_ev_mcast_pkt;
	unsigned rx_ev_pkt_type;

	rx_ev_hdr_type = EFX_QWORD_FIELD(*event, FSF_AZ_RX_EV_HDR_TYPE);
	rx_ev_mcast_pkt = EFX_QWORD_FIELD(*event, FSF_AZ_RX_EV_MCAST_PKT);
	rx_ev_tobe_disc = EFX_QWORD_FIELD(*event, FSF_AZ_RX_EV_TOBE_DISC);
	rx_ev_pkt_type = EFX_QWORD_FIELD(*event, FSF_AZ_RX_EV_PKT_TYPE);
	rx_ev_buf_owner_id_err = EFX_QWORD_FIELD(*event,
						 FSF_AZ_RX_EV_BUF_OWNER_ID_ERR);
	rx_ev_ip_hdr_chksum_err = EFX_QWORD_FIELD(*event,
						  FSF_AZ_RX_EV_IP_HDR_CHKSUM_ERR);
	rx_ev_tcp_udp_chksum_err = EFX_QWORD_FIELD(*event,
						   FSF_AZ_RX_EV_TCP_UDP_CHKSUM_ERR);
	rx_ev_eth_crc_err = EFX_QWORD_FIELD(*event, FSF_AZ_RX_EV_ETH_CRC_ERR);
	rx_ev_frm_trunc = EFX_QWORD_FIELD(*event, FSF_AZ_RX_EV_FRM_TRUNC);
	rx_ev_drib_nib = ((efx_nic_rev(efx) >= EFX_REV_FALCON_B0) ?
			  0 : EFX_QWORD_FIELD(*event, FSF_AA_RX_EV_DRIB_NIB));
	rx_ev_pause_frm = EFX_QWORD_FIELD(*event, FSF_AZ_RX_EV_PAUSE_FRM_ERR);

	/* Every error apart from tobe_disc and pause_frm */
	rx_ev_other_err = (rx_ev_drib_nib | rx_ev_tcp_udp_chksum_err |
			   rx_ev_buf_owner_id_err | rx_ev_eth_crc_err |
			   rx_ev_frm_trunc | rx_ev_ip_hdr_chksum_err);

	/* Count errors that are not in MAC stats.  Ignore expected
	 * checksum errors during self-test. */
	if (rx_ev_frm_trunc)
		++rx_queue->channel->n_rx_frm_trunc;
	else if (rx_ev_tobe_disc)
		++rx_queue->channel->n_rx_tobe_disc;
	else if (!efx->loopback_selftest) {
		if (rx_ev_ip_hdr_chksum_err)
			++rx_queue->channel->n_rx_ip_hdr_chksum_err;
		else if (rx_ev_tcp_udp_chksum_err)
			++rx_queue->channel->n_rx_tcp_udp_chksum_err;
	}

	/* The frame must be discarded if any of these are true. */
	*discard = (rx_ev_eth_crc_err | rx_ev_frm_trunc | rx_ev_drib_nib |
		    rx_ev_tobe_disc | rx_ev_pause_frm);

	/* TOBE_DISC is expected on unicast mismatches; don't print out an
	 * error message.  FRM_TRUNC indicates RXDP dropped the packet due
	 * to a FIFO overflow.
	 */
#ifdef EFX_ENABLE_DEBUG
	if (rx_ev_other_err) {
		EFX_INFO_RL(efx, " RX queue %d unexpected RX event "
			    EFX_QWORD_FMT "%s%s%s%s%s%s%s%s\n",
			    rx_queue->queue, EFX_QWORD_VAL(*event),
			    rx_ev_buf_owner_id_err ? " [OWNER_ID_ERR]" : "",
			    rx_ev_ip_hdr_chksum_err ?
			    " [IP_HDR_CHKSUM_ERR]" : "",
			    rx_ev_tcp_udp_chksum_err ?
			    " [TCP_UDP_CHKSUM_ERR]" : "",
			    rx_ev_eth_crc_err ? " [ETH_CRC_ERR]" : "",
			    rx_ev_frm_trunc ? " [FRM_TRUNC]" : "",
			    rx_ev_drib_nib ? " [DRIB_NIB]" : "",
			    rx_ev_tobe_disc ? " [TOBE_DISC]" : "",
			    rx_ev_pause_frm ? " [PAUSE]" : "");
	}
#endif
}

/* Handle receive events that are not in-order. */
static void
efx_handle_rx_bad_index(struct efx_rx_queue *rx_queue, unsigned index)
{
	struct efx_nic *efx = rx_queue->efx;
	unsigned expected, dropped;

	expected = rx_queue->removed_count & EFX_RXQ_MASK;
	dropped = (index - expected) & EFX_RXQ_MASK;
	EFX_INFO(efx, "dropped %d events (index=%d expected=%d)\n",
		dropped, index, expected);

	efx_schedule_reset(efx, EFX_WORKAROUND_5676(efx) ?
			   RESET_TYPE_RX_RECOVERY : RESET_TYPE_DISABLE);
}

/* Handle a packet received event
 *
 * The NIC gives a "discard" flag if it's a unicast packet with the
 * wrong destination address
 * Also "is multicast" and "matches multicast filter" flags can be used to
 * discard non-matching multicast packets.
 */
static void
efx_handle_rx_event(struct efx_channel *channel, const efx_qword_t *event)
{
	unsigned int rx_ev_desc_ptr, rx_ev_byte_cnt;
	unsigned int rx_ev_hdr_type, rx_ev_mcast_pkt;
	unsigned expected_ptr;
	bool rx_ev_pkt_ok, discard = false, checksummed;
	struct efx_rx_queue *rx_queue;
	struct efx_nic *efx = channel->efx;

	/* Basic packet information */
	rx_ev_byte_cnt = EFX_QWORD_FIELD(*event, FSF_AZ_RX_EV_BYTE_CNT);
	rx_ev_pkt_ok = EFX_QWORD_FIELD(*event, FSF_AZ_RX_EV_PKT_OK);
	rx_ev_hdr_type = EFX_QWORD_FIELD(*event, FSF_AZ_RX_EV_HDR_TYPE);
	WARN_ON(EFX_QWORD_FIELD(*event, FSF_AZ_RX_EV_JUMBO_CONT));
	WARN_ON(EFX_QWORD_FIELD(*event, FSF_AZ_RX_EV_SOP) != 1);
	WARN_ON(EFX_QWORD_FIELD(*event, FSF_AZ_RX_EV_Q_LABEL) !=
		channel->channel);

	rx_queue = &efx->rx_queue[channel->channel];

	rx_ev_desc_ptr = EFX_QWORD_FIELD(*event, FSF_AZ_RX_EV_DESC_PTR);
	expected_ptr = rx_queue->removed_count & EFX_RXQ_MASK;
	if (unlikely(rx_ev_desc_ptr != expected_ptr))
		efx_handle_rx_bad_index(rx_queue, rx_ev_desc_ptr);

	if (likely(rx_ev_pkt_ok)) {
		/* If packet is marked as OK and packet type is TCP/IP or
		 * UDP/IP, then we can rely on the hardware checksum.
		 */
		checksummed =
			likely(efx->rx_checksum_enabled) &&
			(rx_ev_hdr_type == FSE_CZ_RX_EV_HDR_TYPE_IPV4V6_TCP ||
			 rx_ev_hdr_type == FSE_CZ_RX_EV_HDR_TYPE_IPV4V6_UDP);
	} else {
		efx_handle_rx_not_ok(rx_queue, event, &rx_ev_pkt_ok, &discard);
		checksummed = false;
	}

	/* Detect multicast packets that didn't match the filter */
	rx_ev_mcast_pkt = EFX_QWORD_FIELD(*event, FSF_AZ_RX_EV_MCAST_PKT);
	if (rx_ev_mcast_pkt) {
		unsigned int rx_ev_mcast_hash_match =
			EFX_QWORD_FIELD(*event, FSF_AZ_RX_EV_MCAST_HASH_MATCH);

		if (unlikely(!rx_ev_mcast_hash_match)) {
			++channel->n_rx_mcast_mismatch;
			discard = true;
		}
	}

	channel->irq_mod_score += 2;

	/* Handle received packet */
	efx_rx_packet(rx_queue, rx_ev_desc_ptr, rx_ev_byte_cnt,
		      checksummed, discard);
}

/* Global events are basically PHY events */
static void
efx_handle_global_event(struct efx_channel *channel, efx_qword_t *event)
{
	struct efx_nic *efx = channel->efx;
	bool handled = false;

	if (EFX_QWORD_FIELD(*event, FSF_AB_GLB_EV_G_PHY0_INTR) ||
	    EFX_QWORD_FIELD(*event, FSF_AB_GLB_EV_XG_PHY0_INTR) ||
	    EFX_QWORD_FIELD(*event, FSF_AB_GLB_EV_XFP_PHY0_INTR)) {
		/* Ignored */
		handled = true;
	}

	if ((efx_nic_rev(efx) >= EFX_REV_FALCON_B0) &&
	    EFX_QWORD_FIELD(*event, FSF_BB_GLB_EV_XG_MGT_INTR)) {
		efx->xmac_poll_required = true;
		handled = true;
	}

	if (efx_nic_rev(efx) <= EFX_REV_FALCON_A1 ?
	    EFX_QWORD_FIELD(*event, FSF_AA_GLB_EV_RX_RECOVERY) :
	    EFX_QWORD_FIELD(*event, FSF_BB_GLB_EV_RX_RECOVERY)) {
		EFX_ERR(efx, "channel %d seen global RX_RESET "
			"event. Resetting.\n", channel->channel);

		atomic_inc(&efx->rx_reset);
		efx_schedule_reset(efx, EFX_WORKAROUND_6555(efx) ?
				   RESET_TYPE_RX_RECOVERY : RESET_TYPE_DISABLE);
		handled = true;
	}

	if (!handled)
		EFX_ERR(efx, "channel %d unknown global event "
			EFX_QWORD_FMT "\n", channel->channel,
			EFX_QWORD_VAL(*event));
}

static void
efx_handle_driver_event(struct efx_channel *channel, efx_qword_t *event)
{
	struct efx_nic *efx = channel->efx;
	unsigned int ev_sub_code;
	unsigned int ev_sub_data;

	ev_sub_code = EFX_QWORD_FIELD(*event, FSF_AZ_DRIVER_EV_SUBCODE);
	ev_sub_data = EFX_QWORD_FIELD(*event, FSF_AZ_DRIVER_EV_SUBDATA);

	switch (ev_sub_code) {
	case FSE_AZ_TX_DESCQ_FLS_DONE_EV:
		EFX_TRACE(efx, "channel %d TXQ %d flushed\n",
			  channel->channel, ev_sub_data);
		break;
	case FSE_AZ_RX_DESCQ_FLS_DONE_EV:
		EFX_TRACE(efx, "channel %d RXQ %d flushed\n",
			  channel->channel, ev_sub_data);
		break;
	case FSE_AZ_EVQ_INIT_DONE_EV:
		EFX_LOG(efx, "channel %d EVQ %d initialised\n",
			channel->channel, ev_sub_data);
		break;
	case FSE_AZ_SRM_UPD_DONE_EV:
		EFX_TRACE(efx, "channel %d SRAM update done\n",
			  channel->channel);
		break;
	case FSE_AZ_WAKE_UP_EV:
		EFX_TRACE(efx, "channel %d RXQ %d wakeup event\n",
			  channel->channel, ev_sub_data);
		break;
	case FSE_AZ_TIMER_EV:
		EFX_TRACE(efx, "channel %d RX queue %d timer expired\n",
			  channel->channel, ev_sub_data);
		break;
	case FSE_AA_RX_RECOVER_EV:
		EFX_ERR(efx, "channel %d seen DRIVER RX_RESET event. "
			"Resetting.\n", channel->channel);
		atomic_inc(&efx->rx_reset);
		efx_schedule_reset(efx,
				   EFX_WORKAROUND_6555(efx) ?
				   RESET_TYPE_RX_RECOVERY :
				   RESET_TYPE_DISABLE);
		break;
	case FSE_BZ_RX_DSC_ERROR_EV:
		EFX_ERR(efx, "RX DMA Q %d reports descriptor fetch error."
			" RX Q %d is disabled.\n", ev_sub_data, ev_sub_data);
		efx_schedule_reset(efx, RESET_TYPE_RX_DESC_FETCH);
		break;
	case FSE_BZ_TX_DSC_ERROR_EV:
		EFX_ERR(efx, "TX DMA Q %d reports descriptor fetch error."
			" TX Q %d is disabled.\n", ev_sub_data, ev_sub_data);
		efx_schedule_reset(efx, RESET_TYPE_TX_DESC_FETCH);
		break;
	default:
		EFX_TRACE(efx, "channel %d unknown driver event code %d "
			  "data %04x\n", channel->channel, ev_sub_code,
			  ev_sub_data);
		break;
	}
}

int efx_nic_process_eventq(struct efx_channel *channel, int rx_quota)
{
	unsigned int read_ptr;
	efx_qword_t event, *p_event;
	int ev_code;
	int rx_packets = 0;

	read_ptr = channel->eventq_read_ptr;

	do {
		p_event = efx_event(channel, read_ptr);
		event = *p_event;

		if (!efx_event_present(&event))
			/* End of events */
			break;

		EFX_TRACE(channel->efx, "channel %d event is "EFX_QWORD_FMT"\n",
			  channel->channel, EFX_QWORD_VAL(event));

		/* Clear this event by marking it all ones */
		EFX_SET_QWORD(*p_event);

		ev_code = EFX_QWORD_FIELD(event, FSF_AZ_EV_CODE);

		switch (ev_code) {
		case FSE_AZ_EV_CODE_RX_EV:
			efx_handle_rx_event(channel, &event);
			++rx_packets;
			break;
		case FSE_AZ_EV_CODE_TX_EV:
			efx_handle_tx_event(channel, &event);
			break;
		case FSE_AZ_EV_CODE_DRV_GEN_EV:
			channel->eventq_magic = EFX_QWORD_FIELD(
				event, FSF_AZ_DRV_GEN_EV_MAGIC);
			EFX_LOG(channel->efx, "channel %d received generated "
				"event "EFX_QWORD_FMT"\n", channel->channel,
				EFX_QWORD_VAL(event));
			break;
		case FSE_AZ_EV_CODE_GLOBAL_EV:
			efx_handle_global_event(channel, &event);
			break;
		case FSE_AZ_EV_CODE_DRIVER_EV:
			efx_handle_driver_event(channel, &event);
			break;
		case FSE_CZ_EV_CODE_MCDI_EV:
			efx_mcdi_process_event(channel, &event);
			break;
		default:
			EFX_ERR(channel->efx, "channel %d unknown event type %d"
				" (data " EFX_QWORD_FMT ")\n", channel->channel,
				ev_code, EFX_QWORD_VAL(event));
		}

		/* Increment read pointer */
		read_ptr = (read_ptr + 1) & EFX_EVQ_MASK;

	} while (rx_packets < rx_quota);

	channel->eventq_read_ptr = read_ptr;
	return rx_packets;
}


/* Allocate buffer table entries for event queue */
int efx_nic_probe_eventq(struct efx_channel *channel)
{
	struct efx_nic *efx = channel->efx;
	BUILD_BUG_ON(EFX_EVQ_SIZE < 512 || EFX_EVQ_SIZE > 32768 ||
		     EFX_EVQ_SIZE & EFX_EVQ_MASK);
	return efx_alloc_special_buffer(efx, &channel->eventq,
					EFX_EVQ_SIZE * sizeof(efx_qword_t));
}

void efx_nic_init_eventq(struct efx_channel *channel)
{
	efx_oword_t reg;
	struct efx_nic *efx = channel->efx;

	EFX_LOG(efx, "channel %d event queue in special buffers %d-%d\n",
		channel->channel, channel->eventq.index,
		channel->eventq.index + channel->eventq.entries - 1);

	if (efx_nic_rev(efx) >= EFX_REV_SIENA_A0) {
		EFX_POPULATE_OWORD_3(reg,
				     FRF_CZ_TIMER_Q_EN, 1,
				     FRF_CZ_HOST_NOTIFY_MODE, 0,
				     FRF_CZ_TIMER_MODE, FFE_CZ_TIMER_MODE_DIS);
		efx_writeo_table(efx, &reg, FR_BZ_TIMER_TBL, channel->channel);
	}

	/* Pin event queue buffer */
	efx_init_special_buffer(efx, &channel->eventq);

	/* Fill event queue with all ones (i.e. empty events) */
	memset(channel->eventq.addr, 0xff, channel->eventq.len);

	/* Push event queue to card */
	EFX_POPULATE_OWORD_3(reg,
			     FRF_AZ_EVQ_EN, 1,
			     FRF_AZ_EVQ_SIZE, __ffs(channel->eventq.entries),
			     FRF_AZ_EVQ_BUF_BASE_ID, channel->eventq.index);
	efx_writeo_table(efx, &reg, efx->type->evq_ptr_tbl_base,
			 channel->channel);

	efx->type->push_irq_moderation(channel);
}

void efx_nic_fini_eventq(struct efx_channel *channel)
{
	efx_oword_t reg;
	struct efx_nic *efx = channel->efx;

	/* Remove event queue from card */
	EFX_ZERO_OWORD(reg);
	efx_writeo_table(efx, &reg, efx->type->evq_ptr_tbl_base,
			 channel->channel);
	if (efx_nic_rev(efx) >= EFX_REV_SIENA_A0)
		efx_writeo_table(efx, &reg, FR_BZ_TIMER_TBL, channel->channel);

	/* Unpin event queue */
	efx_fini_special_buffer(efx, &channel->eventq);
}

/* Free buffers backing event queue */
void efx_nic_remove_eventq(struct efx_channel *channel)
{
	efx_free_special_buffer(channel->efx, &channel->eventq);
}


/* Generates a test event on the event queue.  A subsequent call to
 * process_eventq() should pick up the event and place the value of
 * "magic" into channel->eventq_magic;
 */
void efx_nic_generate_test_event(struct efx_channel *channel, unsigned int magic)
{
	efx_qword_t test_event;

	EFX_POPULATE_QWORD_2(test_event, FSF_AZ_EV_CODE,
			     FSE_AZ_EV_CODE_DRV_GEN_EV,
			     FSF_AZ_DRV_GEN_EV_MAGIC, magic);
	efx_generate_event(channel, &test_event);
}

/**************************************************************************
 *
 * Flush handling
 *
 **************************************************************************/


static void efx_poll_flush_events(struct efx_nic *efx)
{
	struct efx_channel *channel = &efx->channel[0];
	struct efx_tx_queue *tx_queue;
	struct efx_rx_queue *rx_queue;
	unsigned int read_ptr = channel->eventq_read_ptr;
	unsigned int end_ptr = (read_ptr - 1) & EFX_EVQ_MASK;

	do {
		efx_qword_t *event = efx_event(channel, read_ptr);
		int ev_code, ev_sub_code, ev_queue;
		bool ev_failed;

		if (!efx_event_present(event))
			break;

		ev_code = EFX_QWORD_FIELD(*event, FSF_AZ_EV_CODE);
		ev_sub_code = EFX_QWORD_FIELD(*event,
					      FSF_AZ_DRIVER_EV_SUBCODE);
		if (ev_code == FSE_AZ_EV_CODE_DRIVER_EV &&
		    ev_sub_code == FSE_AZ_TX_DESCQ_FLS_DONE_EV) {
			ev_queue = EFX_QWORD_FIELD(*event,
						   FSF_AZ_DRIVER_EV_SUBDATA);
			if (ev_queue < EFX_TX_QUEUE_COUNT) {
				tx_queue = efx->tx_queue + ev_queue;
				tx_queue->flushed = FLUSH_DONE;
			}
		} else if (ev_code == FSE_AZ_EV_CODE_DRIVER_EV &&
			   ev_sub_code == FSE_AZ_RX_DESCQ_FLS_DONE_EV) {
			ev_queue = EFX_QWORD_FIELD(
				*event, FSF_AZ_DRIVER_EV_RX_DESCQ_ID);
			ev_failed = EFX_QWORD_FIELD(
				*event, FSF_AZ_DRIVER_EV_RX_FLUSH_FAIL);
			if (ev_queue < efx->n_rx_queues) {
				rx_queue = efx->rx_queue + ev_queue;
				rx_queue->flushed =
					ev_failed ? FLUSH_FAILED : FLUSH_DONE;
			}
		}

		/* We're about to destroy the queue anyway, so
		 * it's ok to throw away every non-flush event */
		EFX_SET_QWORD(*event);

		read_ptr = (read_ptr + 1) & EFX_EVQ_MASK;
	} while (read_ptr != end_ptr);

	channel->eventq_read_ptr = read_ptr;
}

/* Handle tx and rx flushes at the same time, since they run in
 * parallel in the hardware and there's no reason for us to
 * serialise them */
int efx_nic_flush_queues(struct efx_nic *efx)
{
	struct efx_rx_queue *rx_queue;
	struct efx_tx_queue *tx_queue;
	int i, tx_pending, rx_pending;

	/* If necessary prepare the hardware for flushing */
	efx->type->prepare_flush(efx);

	/* Flush all tx queues in parallel */
	efx_for_each_tx_queue(tx_queue, efx)
		efx_flush_tx_queue(tx_queue);

	/* The hardware supports four concurrent rx flushes, each of which may
	 * need to be retried if there is an outstanding descriptor fetch */
	for (i = 0; i < EFX_FLUSH_POLL_COUNT; ++i) {
		rx_pending = tx_pending = 0;
		efx_for_each_rx_queue(rx_queue, efx) {
			if (rx_queue->flushed == FLUSH_PENDING)
				++rx_pending;
		}
		efx_for_each_rx_queue(rx_queue, efx) {
			if (rx_pending == EFX_RX_FLUSH_COUNT)
				break;
			if (rx_queue->flushed == FLUSH_FAILED ||
			    rx_queue->flushed == FLUSH_NONE) {
				efx_flush_rx_queue(rx_queue);
				++rx_pending;
			}
		}
		efx_for_each_tx_queue(tx_queue, efx) {
			if (tx_queue->flushed != FLUSH_DONE)
				++tx_pending;
		}

		if (rx_pending == 0 && tx_pending == 0)
			return 0;

		msleep(EFX_FLUSH_INTERVAL);
		efx_poll_flush_events(efx);
	}

	/* Mark the queues as all flushed. We're going to return failure
	 * leading to a reset, or fake up success anyway */
	efx_for_each_tx_queue(tx_queue, efx) {
		if (tx_queue->flushed != FLUSH_DONE)
			EFX_ERR(efx, "tx queue %d flush command timed out\n",
				tx_queue->queue);
		tx_queue->flushed = FLUSH_DONE;
	}
	efx_for_each_rx_queue(rx_queue, efx) {
		if (rx_queue->flushed != FLUSH_DONE)
			EFX_ERR(efx, "rx queue %d flush command timed out\n",
				rx_queue->queue);
		rx_queue->flushed = FLUSH_DONE;
	}

	if (EFX_WORKAROUND_7803(efx))
		return 0;

	return -ETIMEDOUT;
}

/**************************************************************************
 *
 * Hardware interrupts
 * The hardware interrupt handler does very little work; all the event
 * queue processing is carried out by per-channel tasklets.
 *
 **************************************************************************/

/* Enable/disable/generate interrupts */
static inline void efx_nic_interrupts(struct efx_nic *efx,
				      bool enabled, bool force)
{
	efx_oword_t int_en_reg_ker;
	unsigned int level = 0;

	if (EFX_WORKAROUND_17213(efx) && !EFX_INT_MODE_USE_MSI(efx))
		/* Set the level always even if we're generating a test
		 * interrupt, because our legacy interrupt handler is safe */
		level = 0x1f;

	EFX_POPULATE_OWORD_3(int_en_reg_ker,
			     FRF_AZ_KER_INT_LEVE_SEL, level,
			     FRF_AZ_KER_INT_KER, force,
			     FRF_AZ_DRV_INT_EN_KER, enabled);
	efx_writeo(efx, &int_en_reg_ker, FR_AZ_INT_EN_KER);
}

void efx_nic_enable_interrupts(struct efx_nic *efx)
{
	struct efx_channel *channel;

	EFX_ZERO_OWORD(*((efx_oword_t *) efx->irq_status.addr));
	wmb(); /* Ensure interrupt vector is clear before interrupts enabled */

	/* Enable interrupts */
	efx_nic_interrupts(efx, true, false);

	/* Force processing of all the channels to get the EVQ RPTRs up to
	   date */
	efx_for_each_channel(channel, efx)
		efx_schedule_channel(channel);
}

void efx_nic_disable_interrupts(struct efx_nic *efx)
{
	/* Disable interrupts */
	efx_nic_interrupts(efx, false, false);
}

/* Generate a test interrupt
 * Interrupt must already have been enabled, otherwise nasty things
 * may happen.
 */
void efx_nic_generate_interrupt(struct efx_nic *efx)
{
	efx_nic_interrupts(efx, true, true);
}

/* Process a fatal interrupt
 * Disable bus mastering ASAP and schedule a reset
 */
irqreturn_t efx_nic_fatal_interrupt(struct efx_nic *efx)
{
	struct falcon_nic_data *nic_data = efx->nic_data;
	efx_oword_t *int_ker = efx->irq_status.addr;
	efx_oword_t fatal_intr;
	int error, mem_perr;

	efx_reado(efx, &fatal_intr, FR_AZ_FATAL_INTR_KER);
	error = EFX_OWORD_FIELD(fatal_intr, FRF_AZ_FATAL_INTR);

	EFX_ERR(efx, "SYSTEM ERROR " EFX_OWORD_FMT " status "
		EFX_OWORD_FMT ": %s\n", EFX_OWORD_VAL(*int_ker),
		EFX_OWORD_VAL(fatal_intr),
		error ? "disabling bus mastering" : "no recognised error");
	if (error == 0)
		goto out;

	/* If this is a memory parity error dump which blocks are offending */
	mem_perr = EFX_OWORD_FIELD(fatal_intr, FRF_AZ_MEM_PERR_INT_KER);
	if (mem_perr) {
		efx_oword_t reg;
		efx_reado(efx, &reg, FR_AZ_MEM_STAT);
		EFX_ERR(efx, "SYSTEM ERROR: memory parity error "
			EFX_OWORD_FMT "\n", EFX_OWORD_VAL(reg));
	}

	/* Disable both devices */
	pci_clear_master(efx->pci_dev);
	if (efx_nic_is_dual_func(efx))
		pci_clear_master(nic_data->pci_dev2);
	efx_nic_disable_interrupts(efx);

	/* Count errors and reset or disable the NIC accordingly */
	if (efx->int_error_count == 0 ||
	    time_after(jiffies, efx->int_error_expire)) {
		efx->int_error_count = 0;
		efx->int_error_expire =
			jiffies + EFX_INT_ERROR_EXPIRE * HZ;
	}
	if (++efx->int_error_count < EFX_MAX_INT_ERRORS) {
		EFX_ERR(efx, "SYSTEM ERROR - reset scheduled\n");
		efx_schedule_reset(efx, RESET_TYPE_INT_ERROR);
	} else {
		EFX_ERR(efx, "SYSTEM ERROR - max number of errors seen."
			"NIC will be disabled\n");
		efx_schedule_reset(efx, RESET_TYPE_DISABLE);
	}
out:
	return IRQ_HANDLED;
}

/* Handle a legacy interrupt
 * Acknowledges the interrupt and schedule event queue processing.
 */
static irqreturn_t efx_legacy_interrupt(int irq, void *dev_id)
{
	struct efx_nic *efx = dev_id;
	efx_oword_t *int_ker = efx->irq_status.addr;
	irqreturn_t result = IRQ_NONE;
	struct efx_channel *channel;
	efx_dword_t reg;
	u32 queues;
	int syserr;

	/* Read the ISR which also ACKs the interrupts */
	efx_readd(efx, &reg, FR_BZ_INT_ISR0);
	queues = EFX_EXTRACT_DWORD(reg, 0, 31);

	/* Check to see if we have a serious error condition */
	syserr = EFX_OWORD_FIELD(*int_ker, FSF_AZ_NET_IVEC_FATAL_INT);
	if (unlikely(syserr))
		return efx_nic_fatal_interrupt(efx);

	if (queues != 0) {
		if (EFX_WORKAROUND_15783(efx))
			efx->irq_zero_count = 0;

		/* Schedule processing of any interrupting queues */
		efx_for_each_channel(channel, efx) {
			if (queues & 1)
				efx_schedule_channel(channel);
			queues >>= 1;
		}
		result = IRQ_HANDLED;

	} else if (EFX_WORKAROUND_15783(efx) &&
		   efx->irq_zero_count++ == 0) {
		efx_qword_t *event;

		/* Ensure we rearm all event queues */
		efx_for_each_channel(channel, efx) {
			event = efx_event(channel, channel->eventq_read_ptr);
			if (efx_event_present(event))
				efx_schedule_channel(channel);
		}

		result = IRQ_HANDLED;
	}

	if (result == IRQ_HANDLED) {
		efx->last_irq_cpu = raw_smp_processor_id();
		EFX_TRACE(efx, "IRQ %d on CPU %d status " EFX_DWORD_FMT "\n",
			  irq, raw_smp_processor_id(), EFX_DWORD_VAL(reg));
	} else if (EFX_WORKAROUND_15783(efx)) {
		/* We can't return IRQ_HANDLED more than once on seeing ISR0=0
		 * because this might be a shared interrupt, but we do need to
		 * check the channel every time and preemptively rearm it if
		 * it's idle. */
		efx_for_each_channel(channel, efx) {
			if (!channel->work_pending)
				efx_nic_eventq_read_ack(channel);
		}
	}

	return result;
}

/* Handle an MSI interrupt
 *
 * Handle an MSI hardware interrupt.  This routine schedules event
 * queue processing.  No interrupt acknowledgement cycle is necessary.
 * Also, we never need to check that the interrupt is for us, since
 * MSI interrupts cannot be shared.
 */
static irqreturn_t efx_msi_interrupt(int irq, void *dev_id)
{
	struct efx_channel *channel = dev_id;
	struct efx_nic *efx = channel->efx;
	efx_oword_t *int_ker = efx->irq_status.addr;
	int syserr;

	efx->last_irq_cpu = raw_smp_processor_id();
	EFX_TRACE(efx, "IRQ %d on CPU %d status " EFX_OWORD_FMT "\n",
		  irq, raw_smp_processor_id(), EFX_OWORD_VAL(*int_ker));

	/* Check to see if we have a serious error condition */
	syserr = EFX_OWORD_FIELD(*int_ker, FSF_AZ_NET_IVEC_FATAL_INT);
	if (unlikely(syserr))
		return efx_nic_fatal_interrupt(efx);

	/* Schedule processing of the channel */
	efx_schedule_channel(channel);

	return IRQ_HANDLED;
}


/* Setup RSS indirection table.
 * This maps from the hash value of the packet to RXQ
 */
static void efx_setup_rss_indir_table(struct efx_nic *efx)
{
	int i = 0;
	unsigned long offset;
	efx_dword_t dword;

	if (efx_nic_rev(efx) < EFX_REV_FALCON_B0)
		return;

	for (offset = FR_BZ_RX_INDIRECTION_TBL;
	     offset < FR_BZ_RX_INDIRECTION_TBL + 0x800;
	     offset += 0x10) {
		EFX_POPULATE_DWORD_1(dword, FRF_BZ_IT_QUEUE,
				     i % efx->n_rx_queues);
		efx_writed(efx, &dword, offset);
		i++;
	}
}

/* Hook interrupt handler(s)
 * Try MSI and then legacy interrupts.
 */
int efx_nic_init_interrupt(struct efx_nic *efx)
{
	struct efx_channel *channel;
	int rc;

	if (!EFX_INT_MODE_USE_MSI(efx)) {
		irq_handler_t handler;
		if (efx_nic_rev(efx) >= EFX_REV_FALCON_B0)
			handler = efx_legacy_interrupt;
		else
			handler = falcon_legacy_interrupt_a1;

		rc = request_irq(efx->legacy_irq, handler, IRQF_SHARED,
				 efx->name, efx);
		if (rc) {
			EFX_ERR(efx, "failed to hook legacy IRQ %d\n",
				efx->pci_dev->irq);
			goto fail1;
		}
		return 0;
	}

	/* Hook MSI or MSI-X interrupt */
	efx_for_each_channel(channel, efx) {
		rc = request_irq(channel->irq, efx_msi_interrupt,
				 IRQF_PROBE_SHARED, /* Not shared */
				 channel->name, channel);
		if (rc) {
			EFX_ERR(efx, "failed to hook IRQ %d\n", channel->irq);
			goto fail2;
		}
	}

	return 0;

 fail2:
	efx_for_each_channel(channel, efx)
		free_irq(channel->irq, channel);
 fail1:
	return rc;
}

void efx_nic_fini_interrupt(struct efx_nic *efx)
{
	struct efx_channel *channel;
	efx_oword_t reg;

	/* Disable MSI/MSI-X interrupts */
	efx_for_each_channel(channel, efx) {
		if (channel->irq)
			free_irq(channel->irq, channel);
	}

	/* ACK legacy interrupt */
	if (efx_nic_rev(efx) >= EFX_REV_FALCON_B0)
		efx_reado(efx, &reg, FR_BZ_INT_ISR0);
	else
		falcon_irq_ack_a1(efx);

	/* Disable legacy interrupt */
	if (efx->legacy_irq)
		free_irq(efx->legacy_irq, efx);
}

u32 efx_nic_fpga_ver(struct efx_nic *efx)
{
	efx_oword_t altera_build;
	efx_reado(efx, &altera_build, FR_AZ_ALTERA_BUILD);
	return EFX_OWORD_FIELD(altera_build, FRF_AZ_ALTERA_BUILD_VER);
}

void efx_nic_init_common(struct efx_nic *efx)
{
	efx_oword_t temp;

	/* Set positions of descriptor caches in SRAM. */
	EFX_POPULATE_OWORD_1(temp, FRF_AZ_SRM_TX_DC_BASE_ADR,
			     efx->type->tx_dc_base / 8);
	efx_writeo(efx, &temp, FR_AZ_SRM_TX_DC_CFG);
	EFX_POPULATE_OWORD_1(temp, FRF_AZ_SRM_RX_DC_BASE_ADR,
			     efx->type->rx_dc_base / 8);
	efx_writeo(efx, &temp, FR_AZ_SRM_RX_DC_CFG);

	/* Set TX descriptor cache size. */
	BUILD_BUG_ON(TX_DC_ENTRIES != (8 << TX_DC_ENTRIES_ORDER));
	EFX_POPULATE_OWORD_1(temp, FRF_AZ_TX_DC_SIZE, TX_DC_ENTRIES_ORDER);
	efx_writeo(efx, &temp, FR_AZ_TX_DC_CFG);

	/* Set RX descriptor cache size.  Set low watermark to size-8, as
	 * this allows most efficient prefetching.
	 */
	BUILD_BUG_ON(RX_DC_ENTRIES != (8 << RX_DC_ENTRIES_ORDER));
	EFX_POPULATE_OWORD_1(temp, FRF_AZ_RX_DC_SIZE, RX_DC_ENTRIES_ORDER);
	efx_writeo(efx, &temp, FR_AZ_RX_DC_CFG);
	EFX_POPULATE_OWORD_1(temp, FRF_AZ_RX_DC_PF_LWM, RX_DC_ENTRIES - 8);
	efx_writeo(efx, &temp, FR_AZ_RX_DC_PF_WM);

	/* Program INT_KER address */
	EFX_POPULATE_OWORD_2(temp,
			     FRF_AZ_NORM_INT_VEC_DIS_KER,
			     EFX_INT_MODE_USE_MSI(efx),
			     FRF_AZ_INT_ADR_KER, efx->irq_status.dma_addr);
	efx_writeo(efx, &temp, FR_AZ_INT_ADR_KER);

	/* Enable all the genuinely fatal interrupts.  (They are still
	 * masked by the overall interrupt mask, controlled by
	 * falcon_interrupts()).
	 *
	 * Note: All other fatal interrupts are enabled
	 */
	EFX_POPULATE_OWORD_3(temp,
			     FRF_AZ_ILL_ADR_INT_KER_EN, 1,
			     FRF_AZ_RBUF_OWN_INT_KER_EN, 1,
			     FRF_AZ_TBUF_OWN_INT_KER_EN, 1);
	EFX_INVERT_OWORD(temp);
	efx_writeo(efx, &temp, FR_AZ_FATAL_INTR_KER);

	efx_setup_rss_indir_table(efx);

	/* Disable the ugly timer-based TX DMA backoff and allow TX DMA to be
	 * controlled by the RX FIFO fill level. Set arbitration to one pkt/Q.
	 */
	efx_reado(efx, &temp, FR_AZ_TX_RESERVED);
	EFX_SET_OWORD_FIELD(temp, FRF_AZ_TX_RX_SPACER, 0xfe);
	EFX_SET_OWORD_FIELD(temp, FRF_AZ_TX_RX_SPACER_EN, 1);
	EFX_SET_OWORD_FIELD(temp, FRF_AZ_TX_ONE_PKT_PER_Q, 1);
	EFX_SET_OWORD_FIELD(temp, FRF_AZ_TX_PUSH_EN, 0);
	EFX_SET_OWORD_FIELD(temp, FRF_AZ_TX_DIS_NON_IP_EV, 1);
	/* Enable SW_EV to inherit in char driver - assume harmless here */
	EFX_SET_OWORD_FIELD(temp, FRF_AZ_TX_SOFT_EVT_EN, 1);
	/* Prefetch threshold 2 => fetch when descriptor cache half empty */
	EFX_SET_OWORD_FIELD(temp, FRF_AZ_TX_PREF_THRESHOLD, 2);
	/* Disable hardware watchdog which can misfire */
	EFX_SET_OWORD_FIELD(temp, FRF_AZ_TX_PREF_WD_TMR, 0x3fffff);
	/* Squash TX of packets of 16 bytes or less */
	if (efx_nic_rev(efx) >= EFX_REV_FALCON_B0)
		EFX_SET_OWORD_FIELD(temp, FRF_BZ_TX_FLUSH_MIN_LEN_EN, 1);
	efx_writeo(efx, &temp, FR_AZ_TX_RESERVED);
}
