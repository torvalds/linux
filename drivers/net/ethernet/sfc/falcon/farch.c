/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2006-2013 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/crc32.h>
#include "net_driver.h"
#include "bitfield.h"
#include "efx.h"
#include "nic.h"
#include "farch_regs.h"
#include "io.h"
#include "workarounds.h"

/* Falcon-architecture (SFC4000) support */

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

/* If EF4_MAX_INT_ERRORS internal errors occur within
 * EF4_INT_ERROR_EXPIRE seconds, we consider the NIC broken and
 * disable it.
 */
#define EF4_INT_ERROR_EXPIRE 3600
#define EF4_MAX_INT_ERRORS 5

/* Depth of RX flush request fifo */
#define EF4_RX_FLUSH_COUNT 4

/* Driver generated events */
#define _EF4_CHANNEL_MAGIC_TEST		0x000101
#define _EF4_CHANNEL_MAGIC_FILL		0x000102
#define _EF4_CHANNEL_MAGIC_RX_DRAIN	0x000103
#define _EF4_CHANNEL_MAGIC_TX_DRAIN	0x000104

#define _EF4_CHANNEL_MAGIC(_code, _data)	((_code) << 8 | (_data))
#define _EF4_CHANNEL_MAGIC_CODE(_magic)		((_magic) >> 8)

#define EF4_CHANNEL_MAGIC_TEST(_channel)				\
	_EF4_CHANNEL_MAGIC(_EF4_CHANNEL_MAGIC_TEST, (_channel)->channel)
#define EF4_CHANNEL_MAGIC_FILL(_rx_queue)				\
	_EF4_CHANNEL_MAGIC(_EF4_CHANNEL_MAGIC_FILL,			\
			   ef4_rx_queue_index(_rx_queue))
#define EF4_CHANNEL_MAGIC_RX_DRAIN(_rx_queue)				\
	_EF4_CHANNEL_MAGIC(_EF4_CHANNEL_MAGIC_RX_DRAIN,			\
			   ef4_rx_queue_index(_rx_queue))
#define EF4_CHANNEL_MAGIC_TX_DRAIN(_tx_queue)				\
	_EF4_CHANNEL_MAGIC(_EF4_CHANNEL_MAGIC_TX_DRAIN,			\
			   (_tx_queue)->queue)

static void ef4_farch_magic_event(struct ef4_channel *channel, u32 magic);

/**************************************************************************
 *
 * Hardware access
 *
 **************************************************************************/

static inline void ef4_write_buf_tbl(struct ef4_nic *efx, ef4_qword_t *value,
				     unsigned int index)
{
	ef4_sram_writeq(efx, efx->membase + efx->type->buf_tbl_base,
			value, index);
}

static bool ef4_masked_compare_oword(const ef4_oword_t *a, const ef4_oword_t *b,
				     const ef4_oword_t *mask)
{
	return ((a->u64[0] ^ b->u64[0]) & mask->u64[0]) ||
		((a->u64[1] ^ b->u64[1]) & mask->u64[1]);
}

int ef4_farch_test_registers(struct ef4_nic *efx,
			     const struct ef4_farch_register_test *regs,
			     size_t n_regs)
{
	unsigned address = 0;
	int i, j;
	ef4_oword_t mask, imask, original, reg, buf;

	for (i = 0; i < n_regs; ++i) {
		address = regs[i].address;
		mask = imask = regs[i].mask;
		EF4_INVERT_OWORD(imask);

		ef4_reado(efx, &original, address);

		/* bit sweep on and off */
		for (j = 0; j < 128; j++) {
			if (!EF4_EXTRACT_OWORD32(mask, j, j))
				continue;

			/* Test this testable bit can be set in isolation */
			EF4_AND_OWORD(reg, original, mask);
			EF4_SET_OWORD32(reg, j, j, 1);

			ef4_writeo(efx, &reg, address);
			ef4_reado(efx, &buf, address);

			if (ef4_masked_compare_oword(&reg, &buf, &mask))
				goto fail;

			/* Test this testable bit can be cleared in isolation */
			EF4_OR_OWORD(reg, original, mask);
			EF4_SET_OWORD32(reg, j, j, 0);

			ef4_writeo(efx, &reg, address);
			ef4_reado(efx, &buf, address);

			if (ef4_masked_compare_oword(&reg, &buf, &mask))
				goto fail;
		}

		ef4_writeo(efx, &original, address);
	}

	return 0;

fail:
	netif_err(efx, hw, efx->net_dev,
		  "wrote "EF4_OWORD_FMT" read "EF4_OWORD_FMT
		  " at address 0x%x mask "EF4_OWORD_FMT"\n", EF4_OWORD_VAL(reg),
		  EF4_OWORD_VAL(buf), address, EF4_OWORD_VAL(mask));
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
 * ef4_alloc_special_buffer()) in the buffer table, allowing
 * it to be used for event queues, descriptor rings etc.
 */
static void
ef4_init_special_buffer(struct ef4_nic *efx, struct ef4_special_buffer *buffer)
{
	ef4_qword_t buf_desc;
	unsigned int index;
	dma_addr_t dma_addr;
	int i;

	EF4_BUG_ON_PARANOID(!buffer->buf.addr);

	/* Write buffer descriptors to NIC */
	for (i = 0; i < buffer->entries; i++) {
		index = buffer->index + i;
		dma_addr = buffer->buf.dma_addr + (i * EF4_BUF_SIZE);
		netif_dbg(efx, probe, efx->net_dev,
			  "mapping special buffer %d at %llx\n",
			  index, (unsigned long long)dma_addr);
		EF4_POPULATE_QWORD_3(buf_desc,
				     FRF_AZ_BUF_ADR_REGION, 0,
				     FRF_AZ_BUF_ADR_FBUF, dma_addr >> 12,
				     FRF_AZ_BUF_OWNER_ID_FBUF, 0);
		ef4_write_buf_tbl(efx, &buf_desc, index);
	}
}

/* Unmaps a buffer and clears the buffer table entries */
static void
ef4_fini_special_buffer(struct ef4_nic *efx, struct ef4_special_buffer *buffer)
{
	ef4_oword_t buf_tbl_upd;
	unsigned int start = buffer->index;
	unsigned int end = (buffer->index + buffer->entries - 1);

	if (!buffer->entries)
		return;

	netif_dbg(efx, hw, efx->net_dev, "unmapping special buffers %d-%d\n",
		  buffer->index, buffer->index + buffer->entries - 1);

	EF4_POPULATE_OWORD_4(buf_tbl_upd,
			     FRF_AZ_BUF_UPD_CMD, 0,
			     FRF_AZ_BUF_CLR_CMD, 1,
			     FRF_AZ_BUF_CLR_END_ID, end,
			     FRF_AZ_BUF_CLR_START_ID, start);
	ef4_writeo(efx, &buf_tbl_upd, FR_AZ_BUF_TBL_UPD);
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
static int ef4_alloc_special_buffer(struct ef4_nic *efx,
				    struct ef4_special_buffer *buffer,
				    unsigned int len)
{
	len = ALIGN(len, EF4_BUF_SIZE);

	if (ef4_nic_alloc_buffer(efx, &buffer->buf, len, GFP_KERNEL))
		return -ENOMEM;
	buffer->entries = len / EF4_BUF_SIZE;
	BUG_ON(buffer->buf.dma_addr & (EF4_BUF_SIZE - 1));

	/* Select new buffer ID */
	buffer->index = efx->next_buffer_table;
	efx->next_buffer_table += buffer->entries;

	netif_dbg(efx, probe, efx->net_dev,
		  "allocating special buffers %d-%d at %llx+%x "
		  "(virt %p phys %llx)\n", buffer->index,
		  buffer->index + buffer->entries - 1,
		  (u64)buffer->buf.dma_addr, len,
		  buffer->buf.addr, (u64)virt_to_phys(buffer->buf.addr));

	return 0;
}

static void
ef4_free_special_buffer(struct ef4_nic *efx, struct ef4_special_buffer *buffer)
{
	if (!buffer->buf.addr)
		return;

	netif_dbg(efx, hw, efx->net_dev,
		  "deallocating special buffers %d-%d at %llx+%x "
		  "(virt %p phys %llx)\n", buffer->index,
		  buffer->index + buffer->entries - 1,
		  (u64)buffer->buf.dma_addr, buffer->buf.len,
		  buffer->buf.addr, (u64)virt_to_phys(buffer->buf.addr));

	ef4_nic_free_buffer(efx, &buffer->buf);
	buffer->entries = 0;
}

/**************************************************************************
 *
 * TX path
 *
 **************************************************************************/

/* This writes to the TX_DESC_WPTR; write pointer for TX descriptor ring */
static inline void ef4_farch_notify_tx_desc(struct ef4_tx_queue *tx_queue)
{
	unsigned write_ptr;
	ef4_dword_t reg;

	write_ptr = tx_queue->write_count & tx_queue->ptr_mask;
	EF4_POPULATE_DWORD_1(reg, FRF_AZ_TX_DESC_WPTR_DWORD, write_ptr);
	ef4_writed_page(tx_queue->efx, &reg,
			FR_AZ_TX_DESC_UPD_DWORD_P0, tx_queue->queue);
}

/* Write pointer and first descriptor for TX descriptor ring */
static inline void ef4_farch_push_tx_desc(struct ef4_tx_queue *tx_queue,
					  const ef4_qword_t *txd)
{
	unsigned write_ptr;
	ef4_oword_t reg;

	BUILD_BUG_ON(FRF_AZ_TX_DESC_LBN != 0);
	BUILD_BUG_ON(FR_AA_TX_DESC_UPD_KER != FR_BZ_TX_DESC_UPD_P0);

	write_ptr = tx_queue->write_count & tx_queue->ptr_mask;
	EF4_POPULATE_OWORD_2(reg, FRF_AZ_TX_DESC_PUSH_CMD, true,
			     FRF_AZ_TX_DESC_WPTR, write_ptr);
	reg.qword[0] = *txd;
	ef4_writeo_page(tx_queue->efx, &reg,
			FR_BZ_TX_DESC_UPD_P0, tx_queue->queue);
}


/* For each entry inserted into the software descriptor ring, create a
 * descriptor in the hardware TX descriptor ring (in host memory), and
 * write a doorbell.
 */
void ef4_farch_tx_write(struct ef4_tx_queue *tx_queue)
{
	struct ef4_tx_buffer *buffer;
	ef4_qword_t *txd;
	unsigned write_ptr;
	unsigned old_write_count = tx_queue->write_count;

	tx_queue->xmit_more_available = false;
	if (unlikely(tx_queue->write_count == tx_queue->insert_count))
		return;

	do {
		write_ptr = tx_queue->write_count & tx_queue->ptr_mask;
		buffer = &tx_queue->buffer[write_ptr];
		txd = ef4_tx_desc(tx_queue, write_ptr);
		++tx_queue->write_count;

		EF4_BUG_ON_PARANOID(buffer->flags & EF4_TX_BUF_OPTION);

		/* Create TX descriptor ring entry */
		BUILD_BUG_ON(EF4_TX_BUF_CONT != 1);
		EF4_POPULATE_QWORD_4(*txd,
				     FSF_AZ_TX_KER_CONT,
				     buffer->flags & EF4_TX_BUF_CONT,
				     FSF_AZ_TX_KER_BYTE_COUNT, buffer->len,
				     FSF_AZ_TX_KER_BUF_REGION, 0,
				     FSF_AZ_TX_KER_BUF_ADDR, buffer->dma_addr);
	} while (tx_queue->write_count != tx_queue->insert_count);

	wmb(); /* Ensure descriptors are written before they are fetched */

	if (ef4_nic_may_push_tx_desc(tx_queue, old_write_count)) {
		txd = ef4_tx_desc(tx_queue,
				  old_write_count & tx_queue->ptr_mask);
		ef4_farch_push_tx_desc(tx_queue, txd);
		++tx_queue->pushes;
	} else {
		ef4_farch_notify_tx_desc(tx_queue);
	}
}

unsigned int ef4_farch_tx_limit_len(struct ef4_tx_queue *tx_queue,
				    dma_addr_t dma_addr, unsigned int len)
{
	/* Don't cross 4K boundaries with descriptors. */
	unsigned int limit = (~dma_addr & (EF4_PAGE_SIZE - 1)) + 1;

	len = min(limit, len);

	if (EF4_WORKAROUND_5391(tx_queue->efx) && (dma_addr & 0xf))
		len = min_t(unsigned int, len, 512 - (dma_addr & 0xf));

	return len;
}


/* Allocate hardware resources for a TX queue */
int ef4_farch_tx_probe(struct ef4_tx_queue *tx_queue)
{
	struct ef4_nic *efx = tx_queue->efx;
	unsigned entries;

	entries = tx_queue->ptr_mask + 1;
	return ef4_alloc_special_buffer(efx, &tx_queue->txd,
					entries * sizeof(ef4_qword_t));
}

void ef4_farch_tx_init(struct ef4_tx_queue *tx_queue)
{
	struct ef4_nic *efx = tx_queue->efx;
	ef4_oword_t reg;

	/* Pin TX descriptor ring */
	ef4_init_special_buffer(efx, &tx_queue->txd);

	/* Push TX descriptor ring to card */
	EF4_POPULATE_OWORD_10(reg,
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

	if (ef4_nic_rev(efx) >= EF4_REV_FALCON_B0) {
		int csum = tx_queue->queue & EF4_TXQ_TYPE_OFFLOAD;
		EF4_SET_OWORD_FIELD(reg, FRF_BZ_TX_IP_CHKSM_DIS, !csum);
		EF4_SET_OWORD_FIELD(reg, FRF_BZ_TX_TCP_CHKSM_DIS,
				    !csum);
	}

	ef4_writeo_table(efx, &reg, efx->type->txd_ptr_tbl_base,
			 tx_queue->queue);

	if (ef4_nic_rev(efx) < EF4_REV_FALCON_B0) {
		/* Only 128 bits in this register */
		BUILD_BUG_ON(EF4_MAX_TX_QUEUES > 128);

		ef4_reado(efx, &reg, FR_AA_TX_CHKSM_CFG);
		if (tx_queue->queue & EF4_TXQ_TYPE_OFFLOAD)
			__clear_bit_le(tx_queue->queue, &reg);
		else
			__set_bit_le(tx_queue->queue, &reg);
		ef4_writeo(efx, &reg, FR_AA_TX_CHKSM_CFG);
	}

	if (ef4_nic_rev(efx) >= EF4_REV_FALCON_B0) {
		EF4_POPULATE_OWORD_1(reg,
				     FRF_BZ_TX_PACE,
				     (tx_queue->queue & EF4_TXQ_TYPE_HIGHPRI) ?
				     FFE_BZ_TX_PACE_OFF :
				     FFE_BZ_TX_PACE_RESERVED);
		ef4_writeo_table(efx, &reg, FR_BZ_TX_PACE_TBL,
				 tx_queue->queue);
	}
}

static void ef4_farch_flush_tx_queue(struct ef4_tx_queue *tx_queue)
{
	struct ef4_nic *efx = tx_queue->efx;
	ef4_oword_t tx_flush_descq;

	WARN_ON(atomic_read(&tx_queue->flush_outstanding));
	atomic_set(&tx_queue->flush_outstanding, 1);

	EF4_POPULATE_OWORD_2(tx_flush_descq,
			     FRF_AZ_TX_FLUSH_DESCQ_CMD, 1,
			     FRF_AZ_TX_FLUSH_DESCQ, tx_queue->queue);
	ef4_writeo(efx, &tx_flush_descq, FR_AZ_TX_FLUSH_DESCQ);
}

void ef4_farch_tx_fini(struct ef4_tx_queue *tx_queue)
{
	struct ef4_nic *efx = tx_queue->efx;
	ef4_oword_t tx_desc_ptr;

	/* Remove TX descriptor ring from card */
	EF4_ZERO_OWORD(tx_desc_ptr);
	ef4_writeo_table(efx, &tx_desc_ptr, efx->type->txd_ptr_tbl_base,
			 tx_queue->queue);

	/* Unpin TX descriptor ring */
	ef4_fini_special_buffer(efx, &tx_queue->txd);
}

/* Free buffers backing TX queue */
void ef4_farch_tx_remove(struct ef4_tx_queue *tx_queue)
{
	ef4_free_special_buffer(tx_queue->efx, &tx_queue->txd);
}

/**************************************************************************
 *
 * RX path
 *
 **************************************************************************/

/* This creates an entry in the RX descriptor queue */
static inline void
ef4_farch_build_rx_desc(struct ef4_rx_queue *rx_queue, unsigned index)
{
	struct ef4_rx_buffer *rx_buf;
	ef4_qword_t *rxd;

	rxd = ef4_rx_desc(rx_queue, index);
	rx_buf = ef4_rx_buffer(rx_queue, index);
	EF4_POPULATE_QWORD_3(*rxd,
			     FSF_AZ_RX_KER_BUF_SIZE,
			     rx_buf->len -
			     rx_queue->efx->type->rx_buffer_padding,
			     FSF_AZ_RX_KER_BUF_REGION, 0,
			     FSF_AZ_RX_KER_BUF_ADDR, rx_buf->dma_addr);
}

/* This writes to the RX_DESC_WPTR register for the specified receive
 * descriptor ring.
 */
void ef4_farch_rx_write(struct ef4_rx_queue *rx_queue)
{
	struct ef4_nic *efx = rx_queue->efx;
	ef4_dword_t reg;
	unsigned write_ptr;

	while (rx_queue->notified_count != rx_queue->added_count) {
		ef4_farch_build_rx_desc(
			rx_queue,
			rx_queue->notified_count & rx_queue->ptr_mask);
		++rx_queue->notified_count;
	}

	wmb();
	write_ptr = rx_queue->added_count & rx_queue->ptr_mask;
	EF4_POPULATE_DWORD_1(reg, FRF_AZ_RX_DESC_WPTR_DWORD, write_ptr);
	ef4_writed_page(efx, &reg, FR_AZ_RX_DESC_UPD_DWORD_P0,
			ef4_rx_queue_index(rx_queue));
}

int ef4_farch_rx_probe(struct ef4_rx_queue *rx_queue)
{
	struct ef4_nic *efx = rx_queue->efx;
	unsigned entries;

	entries = rx_queue->ptr_mask + 1;
	return ef4_alloc_special_buffer(efx, &rx_queue->rxd,
					entries * sizeof(ef4_qword_t));
}

void ef4_farch_rx_init(struct ef4_rx_queue *rx_queue)
{
	ef4_oword_t rx_desc_ptr;
	struct ef4_nic *efx = rx_queue->efx;
	bool is_b0 = ef4_nic_rev(efx) >= EF4_REV_FALCON_B0;
	bool iscsi_digest_en = is_b0;
	bool jumbo_en;

	/* For kernel-mode queues in Falcon A1, the JUMBO flag enables
	 * DMA to continue after a PCIe page boundary (and scattering
	 * is not possible).  In Falcon B0 and Siena, it enables
	 * scatter.
	 */
	jumbo_en = !is_b0 || efx->rx_scatter;

	netif_dbg(efx, hw, efx->net_dev,
		  "RX queue %d ring in special buffers %d-%d\n",
		  ef4_rx_queue_index(rx_queue), rx_queue->rxd.index,
		  rx_queue->rxd.index + rx_queue->rxd.entries - 1);

	rx_queue->scatter_n = 0;

	/* Pin RX descriptor ring */
	ef4_init_special_buffer(efx, &rx_queue->rxd);

	/* Push RX descriptor ring to card */
	EF4_POPULATE_OWORD_10(rx_desc_ptr,
			      FRF_AZ_RX_ISCSI_DDIG_EN, iscsi_digest_en,
			      FRF_AZ_RX_ISCSI_HDIG_EN, iscsi_digest_en,
			      FRF_AZ_RX_DESCQ_BUF_BASE_ID, rx_queue->rxd.index,
			      FRF_AZ_RX_DESCQ_EVQ_ID,
			      ef4_rx_queue_channel(rx_queue)->channel,
			      FRF_AZ_RX_DESCQ_OWNER_ID, 0,
			      FRF_AZ_RX_DESCQ_LABEL,
			      ef4_rx_queue_index(rx_queue),
			      FRF_AZ_RX_DESCQ_SIZE,
			      __ffs(rx_queue->rxd.entries),
			      FRF_AZ_RX_DESCQ_TYPE, 0 /* kernel queue */ ,
			      FRF_AZ_RX_DESCQ_JUMBO, jumbo_en,
			      FRF_AZ_RX_DESCQ_EN, 1);
	ef4_writeo_table(efx, &rx_desc_ptr, efx->type->rxd_ptr_tbl_base,
			 ef4_rx_queue_index(rx_queue));
}

static void ef4_farch_flush_rx_queue(struct ef4_rx_queue *rx_queue)
{
	struct ef4_nic *efx = rx_queue->efx;
	ef4_oword_t rx_flush_descq;

	EF4_POPULATE_OWORD_2(rx_flush_descq,
			     FRF_AZ_RX_FLUSH_DESCQ_CMD, 1,
			     FRF_AZ_RX_FLUSH_DESCQ,
			     ef4_rx_queue_index(rx_queue));
	ef4_writeo(efx, &rx_flush_descq, FR_AZ_RX_FLUSH_DESCQ);
}

void ef4_farch_rx_fini(struct ef4_rx_queue *rx_queue)
{
	ef4_oword_t rx_desc_ptr;
	struct ef4_nic *efx = rx_queue->efx;

	/* Remove RX descriptor ring from card */
	EF4_ZERO_OWORD(rx_desc_ptr);
	ef4_writeo_table(efx, &rx_desc_ptr, efx->type->rxd_ptr_tbl_base,
			 ef4_rx_queue_index(rx_queue));

	/* Unpin RX descriptor ring */
	ef4_fini_special_buffer(efx, &rx_queue->rxd);
}

/* Free buffers backing RX queue */
void ef4_farch_rx_remove(struct ef4_rx_queue *rx_queue)
{
	ef4_free_special_buffer(rx_queue->efx, &rx_queue->rxd);
}

/**************************************************************************
 *
 * Flush handling
 *
 **************************************************************************/

/* ef4_farch_flush_queues() must be woken up when all flushes are completed,
 * or more RX flushes can be kicked off.
 */
static bool ef4_farch_flush_wake(struct ef4_nic *efx)
{
	/* Ensure that all updates are visible to ef4_farch_flush_queues() */
	smp_mb();

	return (atomic_read(&efx->active_queues) == 0 ||
		(atomic_read(&efx->rxq_flush_outstanding) < EF4_RX_FLUSH_COUNT
		 && atomic_read(&efx->rxq_flush_pending) > 0));
}

static bool ef4_check_tx_flush_complete(struct ef4_nic *efx)
{
	bool i = true;
	ef4_oword_t txd_ptr_tbl;
	struct ef4_channel *channel;
	struct ef4_tx_queue *tx_queue;

	ef4_for_each_channel(channel, efx) {
		ef4_for_each_channel_tx_queue(tx_queue, channel) {
			ef4_reado_table(efx, &txd_ptr_tbl,
					FR_BZ_TX_DESC_PTR_TBL, tx_queue->queue);
			if (EF4_OWORD_FIELD(txd_ptr_tbl,
					    FRF_AZ_TX_DESCQ_FLUSH) ||
			    EF4_OWORD_FIELD(txd_ptr_tbl,
					    FRF_AZ_TX_DESCQ_EN)) {
				netif_dbg(efx, hw, efx->net_dev,
					  "flush did not complete on TXQ %d\n",
					  tx_queue->queue);
				i = false;
			} else if (atomic_cmpxchg(&tx_queue->flush_outstanding,
						  1, 0)) {
				/* The flush is complete, but we didn't
				 * receive a flush completion event
				 */
				netif_dbg(efx, hw, efx->net_dev,
					  "flush complete on TXQ %d, so drain "
					  "the queue\n", tx_queue->queue);
				/* Don't need to increment active_queues as it
				 * has already been incremented for the queues
				 * which did not drain
				 */
				ef4_farch_magic_event(channel,
						      EF4_CHANNEL_MAGIC_TX_DRAIN(
							      tx_queue));
			}
		}
	}

	return i;
}

/* Flush all the transmit queues, and continue flushing receive queues until
 * they're all flushed. Wait for the DRAIN events to be received so that there
 * are no more RX and TX events left on any channel. */
static int ef4_farch_do_flush(struct ef4_nic *efx)
{
	unsigned timeout = msecs_to_jiffies(5000); /* 5s for all flushes and drains */
	struct ef4_channel *channel;
	struct ef4_rx_queue *rx_queue;
	struct ef4_tx_queue *tx_queue;
	int rc = 0;

	ef4_for_each_channel(channel, efx) {
		ef4_for_each_channel_tx_queue(tx_queue, channel) {
			ef4_farch_flush_tx_queue(tx_queue);
		}
		ef4_for_each_channel_rx_queue(rx_queue, channel) {
			rx_queue->flush_pending = true;
			atomic_inc(&efx->rxq_flush_pending);
		}
	}

	while (timeout && atomic_read(&efx->active_queues) > 0) {
		/* The hardware supports four concurrent rx flushes, each of
		 * which may need to be retried if there is an outstanding
		 * descriptor fetch
		 */
		ef4_for_each_channel(channel, efx) {
			ef4_for_each_channel_rx_queue(rx_queue, channel) {
				if (atomic_read(&efx->rxq_flush_outstanding) >=
				    EF4_RX_FLUSH_COUNT)
					break;

				if (rx_queue->flush_pending) {
					rx_queue->flush_pending = false;
					atomic_dec(&efx->rxq_flush_pending);
					atomic_inc(&efx->rxq_flush_outstanding);
					ef4_farch_flush_rx_queue(rx_queue);
				}
			}
		}

		timeout = wait_event_timeout(efx->flush_wq,
					     ef4_farch_flush_wake(efx),
					     timeout);
	}

	if (atomic_read(&efx->active_queues) &&
	    !ef4_check_tx_flush_complete(efx)) {
		netif_err(efx, hw, efx->net_dev, "failed to flush %d queues "
			  "(rx %d+%d)\n", atomic_read(&efx->active_queues),
			  atomic_read(&efx->rxq_flush_outstanding),
			  atomic_read(&efx->rxq_flush_pending));
		rc = -ETIMEDOUT;

		atomic_set(&efx->active_queues, 0);
		atomic_set(&efx->rxq_flush_pending, 0);
		atomic_set(&efx->rxq_flush_outstanding, 0);
	}

	return rc;
}

int ef4_farch_fini_dmaq(struct ef4_nic *efx)
{
	struct ef4_channel *channel;
	struct ef4_tx_queue *tx_queue;
	struct ef4_rx_queue *rx_queue;
	int rc = 0;

	/* Do not attempt to write to the NIC during EEH recovery */
	if (efx->state != STATE_RECOVERY) {
		/* Only perform flush if DMA is enabled */
		if (efx->pci_dev->is_busmaster) {
			efx->type->prepare_flush(efx);
			rc = ef4_farch_do_flush(efx);
			efx->type->finish_flush(efx);
		}

		ef4_for_each_channel(channel, efx) {
			ef4_for_each_channel_rx_queue(rx_queue, channel)
				ef4_farch_rx_fini(rx_queue);
			ef4_for_each_channel_tx_queue(tx_queue, channel)
				ef4_farch_tx_fini(tx_queue);
		}
	}

	return rc;
}

/* Reset queue and flush accounting after FLR
 *
 * One possible cause of FLR recovery is that DMA may be failing (eg. if bus
 * mastering was disabled), in which case we don't receive (RXQ) flush
 * completion events.  This means that efx->rxq_flush_outstanding remained at 4
 * after the FLR; also, efx->active_queues was non-zero (as no flush completion
 * events were received, and we didn't go through ef4_check_tx_flush_complete())
 * If we don't fix this up, on the next call to ef4_realloc_channels() we won't
 * flush any RX queues because efx->rxq_flush_outstanding is at the limit of 4
 * for batched flush requests; and the efx->active_queues gets messed up because
 * we keep incrementing for the newly initialised queues, but it never went to
 * zero previously.  Then we get a timeout every time we try to restart the
 * queues, as it doesn't go back to zero when we should be flushing the queues.
 */
void ef4_farch_finish_flr(struct ef4_nic *efx)
{
	atomic_set(&efx->rxq_flush_pending, 0);
	atomic_set(&efx->rxq_flush_outstanding, 0);
	atomic_set(&efx->active_queues, 0);
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
void ef4_farch_ev_read_ack(struct ef4_channel *channel)
{
	ef4_dword_t reg;
	struct ef4_nic *efx = channel->efx;

	EF4_POPULATE_DWORD_1(reg, FRF_AZ_EVQ_RPTR,
			     channel->eventq_read_ptr & channel->eventq_mask);

	/* For Falcon A1, EVQ_RPTR_KER is documented as having a step size
	 * of 4 bytes, but it is really 16 bytes just like later revisions.
	 */
	ef4_writed(efx, &reg,
		   efx->type->evq_rptr_tbl_base +
		   FR_BZ_EVQ_RPTR_STEP * channel->channel);
}

/* Use HW to insert a SW defined event */
void ef4_farch_generate_event(struct ef4_nic *efx, unsigned int evq,
			      ef4_qword_t *event)
{
	ef4_oword_t drv_ev_reg;

	BUILD_BUG_ON(FRF_AZ_DRV_EV_DATA_LBN != 0 ||
		     FRF_AZ_DRV_EV_DATA_WIDTH != 64);
	drv_ev_reg.u32[0] = event->u32[0];
	drv_ev_reg.u32[1] = event->u32[1];
	drv_ev_reg.u32[2] = 0;
	drv_ev_reg.u32[3] = 0;
	EF4_SET_OWORD_FIELD(drv_ev_reg, FRF_AZ_DRV_EV_QID, evq);
	ef4_writeo(efx, &drv_ev_reg, FR_AZ_DRV_EV);
}

static void ef4_farch_magic_event(struct ef4_channel *channel, u32 magic)
{
	ef4_qword_t event;

	EF4_POPULATE_QWORD_2(event, FSF_AZ_EV_CODE,
			     FSE_AZ_EV_CODE_DRV_GEN_EV,
			     FSF_AZ_DRV_GEN_EV_MAGIC, magic);
	ef4_farch_generate_event(channel->efx, channel->channel, &event);
}

/* Handle a transmit completion event
 *
 * The NIC batches TX completion events; the message we receive is of
 * the form "complete all TX events up to this index".
 */
static int
ef4_farch_handle_tx_event(struct ef4_channel *channel, ef4_qword_t *event)
{
	unsigned int tx_ev_desc_ptr;
	unsigned int tx_ev_q_label;
	struct ef4_tx_queue *tx_queue;
	struct ef4_nic *efx = channel->efx;
	int tx_packets = 0;

	if (unlikely(ACCESS_ONCE(efx->reset_pending)))
		return 0;

	if (likely(EF4_QWORD_FIELD(*event, FSF_AZ_TX_EV_COMP))) {
		/* Transmit completion */
		tx_ev_desc_ptr = EF4_QWORD_FIELD(*event, FSF_AZ_TX_EV_DESC_PTR);
		tx_ev_q_label = EF4_QWORD_FIELD(*event, FSF_AZ_TX_EV_Q_LABEL);
		tx_queue = ef4_channel_get_tx_queue(
			channel, tx_ev_q_label % EF4_TXQ_TYPES);
		tx_packets = ((tx_ev_desc_ptr - tx_queue->read_count) &
			      tx_queue->ptr_mask);
		ef4_xmit_done(tx_queue, tx_ev_desc_ptr);
	} else if (EF4_QWORD_FIELD(*event, FSF_AZ_TX_EV_WQ_FF_FULL)) {
		/* Rewrite the FIFO write pointer */
		tx_ev_q_label = EF4_QWORD_FIELD(*event, FSF_AZ_TX_EV_Q_LABEL);
		tx_queue = ef4_channel_get_tx_queue(
			channel, tx_ev_q_label % EF4_TXQ_TYPES);

		netif_tx_lock(efx->net_dev);
		ef4_farch_notify_tx_desc(tx_queue);
		netif_tx_unlock(efx->net_dev);
	} else if (EF4_QWORD_FIELD(*event, FSF_AZ_TX_EV_PKT_ERR)) {
		ef4_schedule_reset(efx, RESET_TYPE_DMA_ERROR);
	} else {
		netif_err(efx, tx_err, efx->net_dev,
			  "channel %d unexpected TX event "
			  EF4_QWORD_FMT"\n", channel->channel,
			  EF4_QWORD_VAL(*event));
	}

	return tx_packets;
}

/* Detect errors included in the rx_evt_pkt_ok bit. */
static u16 ef4_farch_handle_rx_not_ok(struct ef4_rx_queue *rx_queue,
				      const ef4_qword_t *event)
{
	struct ef4_channel *channel = ef4_rx_queue_channel(rx_queue);
	struct ef4_nic *efx = rx_queue->efx;
	bool rx_ev_buf_owner_id_err, rx_ev_ip_hdr_chksum_err;
	bool rx_ev_tcp_udp_chksum_err, rx_ev_eth_crc_err;
	bool rx_ev_frm_trunc, rx_ev_drib_nib, rx_ev_tobe_disc;
	bool rx_ev_other_err, rx_ev_pause_frm;
	bool rx_ev_hdr_type, rx_ev_mcast_pkt;
	unsigned rx_ev_pkt_type;

	rx_ev_hdr_type = EF4_QWORD_FIELD(*event, FSF_AZ_RX_EV_HDR_TYPE);
	rx_ev_mcast_pkt = EF4_QWORD_FIELD(*event, FSF_AZ_RX_EV_MCAST_PKT);
	rx_ev_tobe_disc = EF4_QWORD_FIELD(*event, FSF_AZ_RX_EV_TOBE_DISC);
	rx_ev_pkt_type = EF4_QWORD_FIELD(*event, FSF_AZ_RX_EV_PKT_TYPE);
	rx_ev_buf_owner_id_err = EF4_QWORD_FIELD(*event,
						 FSF_AZ_RX_EV_BUF_OWNER_ID_ERR);
	rx_ev_ip_hdr_chksum_err = EF4_QWORD_FIELD(*event,
						  FSF_AZ_RX_EV_IP_HDR_CHKSUM_ERR);
	rx_ev_tcp_udp_chksum_err = EF4_QWORD_FIELD(*event,
						   FSF_AZ_RX_EV_TCP_UDP_CHKSUM_ERR);
	rx_ev_eth_crc_err = EF4_QWORD_FIELD(*event, FSF_AZ_RX_EV_ETH_CRC_ERR);
	rx_ev_frm_trunc = EF4_QWORD_FIELD(*event, FSF_AZ_RX_EV_FRM_TRUNC);
	rx_ev_drib_nib = ((ef4_nic_rev(efx) >= EF4_REV_FALCON_B0) ?
			  0 : EF4_QWORD_FIELD(*event, FSF_AA_RX_EV_DRIB_NIB));
	rx_ev_pause_frm = EF4_QWORD_FIELD(*event, FSF_AZ_RX_EV_PAUSE_FRM_ERR);

	/* Every error apart from tobe_disc and pause_frm */
	rx_ev_other_err = (rx_ev_drib_nib | rx_ev_tcp_udp_chksum_err |
			   rx_ev_buf_owner_id_err | rx_ev_eth_crc_err |
			   rx_ev_frm_trunc | rx_ev_ip_hdr_chksum_err);

	/* Count errors that are not in MAC stats.  Ignore expected
	 * checksum errors during self-test. */
	if (rx_ev_frm_trunc)
		++channel->n_rx_frm_trunc;
	else if (rx_ev_tobe_disc)
		++channel->n_rx_tobe_disc;
	else if (!efx->loopback_selftest) {
		if (rx_ev_ip_hdr_chksum_err)
			++channel->n_rx_ip_hdr_chksum_err;
		else if (rx_ev_tcp_udp_chksum_err)
			++channel->n_rx_tcp_udp_chksum_err;
	}

	/* TOBE_DISC is expected on unicast mismatches; don't print out an
	 * error message.  FRM_TRUNC indicates RXDP dropped the packet due
	 * to a FIFO overflow.
	 */
#ifdef DEBUG
	if (rx_ev_other_err && net_ratelimit()) {
		netif_dbg(efx, rx_err, efx->net_dev,
			  " RX queue %d unexpected RX event "
			  EF4_QWORD_FMT "%s%s%s%s%s%s%s%s\n",
			  ef4_rx_queue_index(rx_queue), EF4_QWORD_VAL(*event),
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

	/* The frame must be discarded if any of these are true. */
	return (rx_ev_eth_crc_err | rx_ev_frm_trunc | rx_ev_drib_nib |
		rx_ev_tobe_disc | rx_ev_pause_frm) ?
		EF4_RX_PKT_DISCARD : 0;
}

/* Handle receive events that are not in-order. Return true if this
 * can be handled as a partial packet discard, false if it's more
 * serious.
 */
static bool
ef4_farch_handle_rx_bad_index(struct ef4_rx_queue *rx_queue, unsigned index)
{
	struct ef4_channel *channel = ef4_rx_queue_channel(rx_queue);
	struct ef4_nic *efx = rx_queue->efx;
	unsigned expected, dropped;

	if (rx_queue->scatter_n &&
	    index == ((rx_queue->removed_count + rx_queue->scatter_n - 1) &
		      rx_queue->ptr_mask)) {
		++channel->n_rx_nodesc_trunc;
		return true;
	}

	expected = rx_queue->removed_count & rx_queue->ptr_mask;
	dropped = (index - expected) & rx_queue->ptr_mask;
	netif_info(efx, rx_err, efx->net_dev,
		   "dropped %d events (index=%d expected=%d)\n",
		   dropped, index, expected);

	ef4_schedule_reset(efx, EF4_WORKAROUND_5676(efx) ?
			   RESET_TYPE_RX_RECOVERY : RESET_TYPE_DISABLE);
	return false;
}

/* Handle a packet received event
 *
 * The NIC gives a "discard" flag if it's a unicast packet with the
 * wrong destination address
 * Also "is multicast" and "matches multicast filter" flags can be used to
 * discard non-matching multicast packets.
 */
static void
ef4_farch_handle_rx_event(struct ef4_channel *channel, const ef4_qword_t *event)
{
	unsigned int rx_ev_desc_ptr, rx_ev_byte_cnt;
	unsigned int rx_ev_hdr_type, rx_ev_mcast_pkt;
	unsigned expected_ptr;
	bool rx_ev_pkt_ok, rx_ev_sop, rx_ev_cont;
	u16 flags;
	struct ef4_rx_queue *rx_queue;
	struct ef4_nic *efx = channel->efx;

	if (unlikely(ACCESS_ONCE(efx->reset_pending)))
		return;

	rx_ev_cont = EF4_QWORD_FIELD(*event, FSF_AZ_RX_EV_JUMBO_CONT);
	rx_ev_sop = EF4_QWORD_FIELD(*event, FSF_AZ_RX_EV_SOP);
	WARN_ON(EF4_QWORD_FIELD(*event, FSF_AZ_RX_EV_Q_LABEL) !=
		channel->channel);

	rx_queue = ef4_channel_get_rx_queue(channel);

	rx_ev_desc_ptr = EF4_QWORD_FIELD(*event, FSF_AZ_RX_EV_DESC_PTR);
	expected_ptr = ((rx_queue->removed_count + rx_queue->scatter_n) &
			rx_queue->ptr_mask);

	/* Check for partial drops and other errors */
	if (unlikely(rx_ev_desc_ptr != expected_ptr) ||
	    unlikely(rx_ev_sop != (rx_queue->scatter_n == 0))) {
		if (rx_ev_desc_ptr != expected_ptr &&
		    !ef4_farch_handle_rx_bad_index(rx_queue, rx_ev_desc_ptr))
			return;

		/* Discard all pending fragments */
		if (rx_queue->scatter_n) {
			ef4_rx_packet(
				rx_queue,
				rx_queue->removed_count & rx_queue->ptr_mask,
				rx_queue->scatter_n, 0, EF4_RX_PKT_DISCARD);
			rx_queue->removed_count += rx_queue->scatter_n;
			rx_queue->scatter_n = 0;
		}

		/* Return if there is no new fragment */
		if (rx_ev_desc_ptr != expected_ptr)
			return;

		/* Discard new fragment if not SOP */
		if (!rx_ev_sop) {
			ef4_rx_packet(
				rx_queue,
				rx_queue->removed_count & rx_queue->ptr_mask,
				1, 0, EF4_RX_PKT_DISCARD);
			++rx_queue->removed_count;
			return;
		}
	}

	++rx_queue->scatter_n;
	if (rx_ev_cont)
		return;

	rx_ev_byte_cnt = EF4_QWORD_FIELD(*event, FSF_AZ_RX_EV_BYTE_CNT);
	rx_ev_pkt_ok = EF4_QWORD_FIELD(*event, FSF_AZ_RX_EV_PKT_OK);
	rx_ev_hdr_type = EF4_QWORD_FIELD(*event, FSF_AZ_RX_EV_HDR_TYPE);

	if (likely(rx_ev_pkt_ok)) {
		/* If packet is marked as OK then we can rely on the
		 * hardware checksum and classification.
		 */
		flags = 0;
		switch (rx_ev_hdr_type) {
		case FSE_CZ_RX_EV_HDR_TYPE_IPV4V6_TCP:
			flags |= EF4_RX_PKT_TCP;
			/* fall through */
		case FSE_CZ_RX_EV_HDR_TYPE_IPV4V6_UDP:
			flags |= EF4_RX_PKT_CSUMMED;
			/* fall through */
		case FSE_CZ_RX_EV_HDR_TYPE_IPV4V6_OTHER:
		case FSE_AZ_RX_EV_HDR_TYPE_OTHER:
			break;
		}
	} else {
		flags = ef4_farch_handle_rx_not_ok(rx_queue, event);
	}

	/* Detect multicast packets that didn't match the filter */
	rx_ev_mcast_pkt = EF4_QWORD_FIELD(*event, FSF_AZ_RX_EV_MCAST_PKT);
	if (rx_ev_mcast_pkt) {
		unsigned int rx_ev_mcast_hash_match =
			EF4_QWORD_FIELD(*event, FSF_AZ_RX_EV_MCAST_HASH_MATCH);

		if (unlikely(!rx_ev_mcast_hash_match)) {
			++channel->n_rx_mcast_mismatch;
			flags |= EF4_RX_PKT_DISCARD;
		}
	}

	channel->irq_mod_score += 2;

	/* Handle received packet */
	ef4_rx_packet(rx_queue,
		      rx_queue->removed_count & rx_queue->ptr_mask,
		      rx_queue->scatter_n, rx_ev_byte_cnt, flags);
	rx_queue->removed_count += rx_queue->scatter_n;
	rx_queue->scatter_n = 0;
}

/* If this flush done event corresponds to a &struct ef4_tx_queue, then
 * send an %EF4_CHANNEL_MAGIC_TX_DRAIN event to drain the event queue
 * of all transmit completions.
 */
static void
ef4_farch_handle_tx_flush_done(struct ef4_nic *efx, ef4_qword_t *event)
{
	struct ef4_tx_queue *tx_queue;
	int qid;

	qid = EF4_QWORD_FIELD(*event, FSF_AZ_DRIVER_EV_SUBDATA);
	if (qid < EF4_TXQ_TYPES * efx->n_tx_channels) {
		tx_queue = ef4_get_tx_queue(efx, qid / EF4_TXQ_TYPES,
					    qid % EF4_TXQ_TYPES);
		if (atomic_cmpxchg(&tx_queue->flush_outstanding, 1, 0)) {
			ef4_farch_magic_event(tx_queue->channel,
					      EF4_CHANNEL_MAGIC_TX_DRAIN(tx_queue));
		}
	}
}

/* If this flush done event corresponds to a &struct ef4_rx_queue: If the flush
 * was successful then send an %EF4_CHANNEL_MAGIC_RX_DRAIN, otherwise add
 * the RX queue back to the mask of RX queues in need of flushing.
 */
static void
ef4_farch_handle_rx_flush_done(struct ef4_nic *efx, ef4_qword_t *event)
{
	struct ef4_channel *channel;
	struct ef4_rx_queue *rx_queue;
	int qid;
	bool failed;

	qid = EF4_QWORD_FIELD(*event, FSF_AZ_DRIVER_EV_RX_DESCQ_ID);
	failed = EF4_QWORD_FIELD(*event, FSF_AZ_DRIVER_EV_RX_FLUSH_FAIL);
	if (qid >= efx->n_channels)
		return;
	channel = ef4_get_channel(efx, qid);
	if (!ef4_channel_has_rx_queue(channel))
		return;
	rx_queue = ef4_channel_get_rx_queue(channel);

	if (failed) {
		netif_info(efx, hw, efx->net_dev,
			   "RXQ %d flush retry\n", qid);
		rx_queue->flush_pending = true;
		atomic_inc(&efx->rxq_flush_pending);
	} else {
		ef4_farch_magic_event(ef4_rx_queue_channel(rx_queue),
				      EF4_CHANNEL_MAGIC_RX_DRAIN(rx_queue));
	}
	atomic_dec(&efx->rxq_flush_outstanding);
	if (ef4_farch_flush_wake(efx))
		wake_up(&efx->flush_wq);
}

static void
ef4_farch_handle_drain_event(struct ef4_channel *channel)
{
	struct ef4_nic *efx = channel->efx;

	WARN_ON(atomic_read(&efx->active_queues) == 0);
	atomic_dec(&efx->active_queues);
	if (ef4_farch_flush_wake(efx))
		wake_up(&efx->flush_wq);
}

static void ef4_farch_handle_generated_event(struct ef4_channel *channel,
					     ef4_qword_t *event)
{
	struct ef4_nic *efx = channel->efx;
	struct ef4_rx_queue *rx_queue =
		ef4_channel_has_rx_queue(channel) ?
		ef4_channel_get_rx_queue(channel) : NULL;
	unsigned magic, code;

	magic = EF4_QWORD_FIELD(*event, FSF_AZ_DRV_GEN_EV_MAGIC);
	code = _EF4_CHANNEL_MAGIC_CODE(magic);

	if (magic == EF4_CHANNEL_MAGIC_TEST(channel)) {
		channel->event_test_cpu = raw_smp_processor_id();
	} else if (rx_queue && magic == EF4_CHANNEL_MAGIC_FILL(rx_queue)) {
		/* The queue must be empty, so we won't receive any rx
		 * events, so ef4_process_channel() won't refill the
		 * queue. Refill it here */
		ef4_fast_push_rx_descriptors(rx_queue, true);
	} else if (rx_queue && magic == EF4_CHANNEL_MAGIC_RX_DRAIN(rx_queue)) {
		ef4_farch_handle_drain_event(channel);
	} else if (code == _EF4_CHANNEL_MAGIC_TX_DRAIN) {
		ef4_farch_handle_drain_event(channel);
	} else {
		netif_dbg(efx, hw, efx->net_dev, "channel %d received "
			  "generated event "EF4_QWORD_FMT"\n",
			  channel->channel, EF4_QWORD_VAL(*event));
	}
}

static void
ef4_farch_handle_driver_event(struct ef4_channel *channel, ef4_qword_t *event)
{
	struct ef4_nic *efx = channel->efx;
	unsigned int ev_sub_code;
	unsigned int ev_sub_data;

	ev_sub_code = EF4_QWORD_FIELD(*event, FSF_AZ_DRIVER_EV_SUBCODE);
	ev_sub_data = EF4_QWORD_FIELD(*event, FSF_AZ_DRIVER_EV_SUBDATA);

	switch (ev_sub_code) {
	case FSE_AZ_TX_DESCQ_FLS_DONE_EV:
		netif_vdbg(efx, hw, efx->net_dev, "channel %d TXQ %d flushed\n",
			   channel->channel, ev_sub_data);
		ef4_farch_handle_tx_flush_done(efx, event);
		break;
	case FSE_AZ_RX_DESCQ_FLS_DONE_EV:
		netif_vdbg(efx, hw, efx->net_dev, "channel %d RXQ %d flushed\n",
			   channel->channel, ev_sub_data);
		ef4_farch_handle_rx_flush_done(efx, event);
		break;
	case FSE_AZ_EVQ_INIT_DONE_EV:
		netif_dbg(efx, hw, efx->net_dev,
			  "channel %d EVQ %d initialised\n",
			  channel->channel, ev_sub_data);
		break;
	case FSE_AZ_SRM_UPD_DONE_EV:
		netif_vdbg(efx, hw, efx->net_dev,
			   "channel %d SRAM update done\n", channel->channel);
		break;
	case FSE_AZ_WAKE_UP_EV:
		netif_vdbg(efx, hw, efx->net_dev,
			   "channel %d RXQ %d wakeup event\n",
			   channel->channel, ev_sub_data);
		break;
	case FSE_AZ_TIMER_EV:
		netif_vdbg(efx, hw, efx->net_dev,
			   "channel %d RX queue %d timer expired\n",
			   channel->channel, ev_sub_data);
		break;
	case FSE_AA_RX_RECOVER_EV:
		netif_err(efx, rx_err, efx->net_dev,
			  "channel %d seen DRIVER RX_RESET event. "
			"Resetting.\n", channel->channel);
		atomic_inc(&efx->rx_reset);
		ef4_schedule_reset(efx,
				   EF4_WORKAROUND_6555(efx) ?
				   RESET_TYPE_RX_RECOVERY :
				   RESET_TYPE_DISABLE);
		break;
	case FSE_BZ_RX_DSC_ERROR_EV:
		netif_err(efx, rx_err, efx->net_dev,
			  "RX DMA Q %d reports descriptor fetch error."
			  " RX Q %d is disabled.\n", ev_sub_data,
			  ev_sub_data);
		ef4_schedule_reset(efx, RESET_TYPE_DMA_ERROR);
		break;
	case FSE_BZ_TX_DSC_ERROR_EV:
		netif_err(efx, tx_err, efx->net_dev,
			  "TX DMA Q %d reports descriptor fetch error."
			  " TX Q %d is disabled.\n", ev_sub_data,
			  ev_sub_data);
		ef4_schedule_reset(efx, RESET_TYPE_DMA_ERROR);
		break;
	default:
		netif_vdbg(efx, hw, efx->net_dev,
			   "channel %d unknown driver event code %d "
			   "data %04x\n", channel->channel, ev_sub_code,
			   ev_sub_data);
		break;
	}
}

int ef4_farch_ev_process(struct ef4_channel *channel, int budget)
{
	struct ef4_nic *efx = channel->efx;
	unsigned int read_ptr;
	ef4_qword_t event, *p_event;
	int ev_code;
	int tx_packets = 0;
	int spent = 0;

	if (budget <= 0)
		return spent;

	read_ptr = channel->eventq_read_ptr;

	for (;;) {
		p_event = ef4_event(channel, read_ptr);
		event = *p_event;

		if (!ef4_event_present(&event))
			/* End of events */
			break;

		netif_vdbg(channel->efx, intr, channel->efx->net_dev,
			   "channel %d event is "EF4_QWORD_FMT"\n",
			   channel->channel, EF4_QWORD_VAL(event));

		/* Clear this event by marking it all ones */
		EF4_SET_QWORD(*p_event);

		++read_ptr;

		ev_code = EF4_QWORD_FIELD(event, FSF_AZ_EV_CODE);

		switch (ev_code) {
		case FSE_AZ_EV_CODE_RX_EV:
			ef4_farch_handle_rx_event(channel, &event);
			if (++spent == budget)
				goto out;
			break;
		case FSE_AZ_EV_CODE_TX_EV:
			tx_packets += ef4_farch_handle_tx_event(channel,
								&event);
			if (tx_packets > efx->txq_entries) {
				spent = budget;
				goto out;
			}
			break;
		case FSE_AZ_EV_CODE_DRV_GEN_EV:
			ef4_farch_handle_generated_event(channel, &event);
			break;
		case FSE_AZ_EV_CODE_DRIVER_EV:
			ef4_farch_handle_driver_event(channel, &event);
			break;
		case FSE_AZ_EV_CODE_GLOBAL_EV:
			if (efx->type->handle_global_event &&
			    efx->type->handle_global_event(channel, &event))
				break;
			/* else fall through */
		default:
			netif_err(channel->efx, hw, channel->efx->net_dev,
				  "channel %d unknown event type %d (data "
				  EF4_QWORD_FMT ")\n", channel->channel,
				  ev_code, EF4_QWORD_VAL(event));
		}
	}

out:
	channel->eventq_read_ptr = read_ptr;
	return spent;
}

/* Allocate buffer table entries for event queue */
int ef4_farch_ev_probe(struct ef4_channel *channel)
{
	struct ef4_nic *efx = channel->efx;
	unsigned entries;

	entries = channel->eventq_mask + 1;
	return ef4_alloc_special_buffer(efx, &channel->eventq,
					entries * sizeof(ef4_qword_t));
}

int ef4_farch_ev_init(struct ef4_channel *channel)
{
	ef4_oword_t reg;
	struct ef4_nic *efx = channel->efx;

	netif_dbg(efx, hw, efx->net_dev,
		  "channel %d event queue in special buffers %d-%d\n",
		  channel->channel, channel->eventq.index,
		  channel->eventq.index + channel->eventq.entries - 1);

	/* Pin event queue buffer */
	ef4_init_special_buffer(efx, &channel->eventq);

	/* Fill event queue with all ones (i.e. empty events) */
	memset(channel->eventq.buf.addr, 0xff, channel->eventq.buf.len);

	/* Push event queue to card */
	EF4_POPULATE_OWORD_3(reg,
			     FRF_AZ_EVQ_EN, 1,
			     FRF_AZ_EVQ_SIZE, __ffs(channel->eventq.entries),
			     FRF_AZ_EVQ_BUF_BASE_ID, channel->eventq.index);
	ef4_writeo_table(efx, &reg, efx->type->evq_ptr_tbl_base,
			 channel->channel);

	return 0;
}

void ef4_farch_ev_fini(struct ef4_channel *channel)
{
	ef4_oword_t reg;
	struct ef4_nic *efx = channel->efx;

	/* Remove event queue from card */
	EF4_ZERO_OWORD(reg);
	ef4_writeo_table(efx, &reg, efx->type->evq_ptr_tbl_base,
			 channel->channel);

	/* Unpin event queue */
	ef4_fini_special_buffer(efx, &channel->eventq);
}

/* Free buffers backing event queue */
void ef4_farch_ev_remove(struct ef4_channel *channel)
{
	ef4_free_special_buffer(channel->efx, &channel->eventq);
}


void ef4_farch_ev_test_generate(struct ef4_channel *channel)
{
	ef4_farch_magic_event(channel, EF4_CHANNEL_MAGIC_TEST(channel));
}

void ef4_farch_rx_defer_refill(struct ef4_rx_queue *rx_queue)
{
	ef4_farch_magic_event(ef4_rx_queue_channel(rx_queue),
			      EF4_CHANNEL_MAGIC_FILL(rx_queue));
}

/**************************************************************************
 *
 * Hardware interrupts
 * The hardware interrupt handler does very little work; all the event
 * queue processing is carried out by per-channel tasklets.
 *
 **************************************************************************/

/* Enable/disable/generate interrupts */
static inline void ef4_farch_interrupts(struct ef4_nic *efx,
				      bool enabled, bool force)
{
	ef4_oword_t int_en_reg_ker;

	EF4_POPULATE_OWORD_3(int_en_reg_ker,
			     FRF_AZ_KER_INT_LEVE_SEL, efx->irq_level,
			     FRF_AZ_KER_INT_KER, force,
			     FRF_AZ_DRV_INT_EN_KER, enabled);
	ef4_writeo(efx, &int_en_reg_ker, FR_AZ_INT_EN_KER);
}

void ef4_farch_irq_enable_master(struct ef4_nic *efx)
{
	EF4_ZERO_OWORD(*((ef4_oword_t *) efx->irq_status.addr));
	wmb(); /* Ensure interrupt vector is clear before interrupts enabled */

	ef4_farch_interrupts(efx, true, false);
}

void ef4_farch_irq_disable_master(struct ef4_nic *efx)
{
	/* Disable interrupts */
	ef4_farch_interrupts(efx, false, false);
}

/* Generate a test interrupt
 * Interrupt must already have been enabled, otherwise nasty things
 * may happen.
 */
int ef4_farch_irq_test_generate(struct ef4_nic *efx)
{
	ef4_farch_interrupts(efx, true, true);
	return 0;
}

/* Process a fatal interrupt
 * Disable bus mastering ASAP and schedule a reset
 */
irqreturn_t ef4_farch_fatal_interrupt(struct ef4_nic *efx)
{
	struct falcon_nic_data *nic_data = efx->nic_data;
	ef4_oword_t *int_ker = efx->irq_status.addr;
	ef4_oword_t fatal_intr;
	int error, mem_perr;

	ef4_reado(efx, &fatal_intr, FR_AZ_FATAL_INTR_KER);
	error = EF4_OWORD_FIELD(fatal_intr, FRF_AZ_FATAL_INTR);

	netif_err(efx, hw, efx->net_dev, "SYSTEM ERROR "EF4_OWORD_FMT" status "
		  EF4_OWORD_FMT ": %s\n", EF4_OWORD_VAL(*int_ker),
		  EF4_OWORD_VAL(fatal_intr),
		  error ? "disabling bus mastering" : "no recognised error");

	/* If this is a memory parity error dump which blocks are offending */
	mem_perr = (EF4_OWORD_FIELD(fatal_intr, FRF_AZ_MEM_PERR_INT_KER) ||
		    EF4_OWORD_FIELD(fatal_intr, FRF_AZ_SRM_PERR_INT_KER));
	if (mem_perr) {
		ef4_oword_t reg;
		ef4_reado(efx, &reg, FR_AZ_MEM_STAT);
		netif_err(efx, hw, efx->net_dev,
			  "SYSTEM ERROR: memory parity error "EF4_OWORD_FMT"\n",
			  EF4_OWORD_VAL(reg));
	}

	/* Disable both devices */
	pci_clear_master(efx->pci_dev);
	if (ef4_nic_is_dual_func(efx))
		pci_clear_master(nic_data->pci_dev2);
	ef4_farch_irq_disable_master(efx);

	/* Count errors and reset or disable the NIC accordingly */
	if (efx->int_error_count == 0 ||
	    time_after(jiffies, efx->int_error_expire)) {
		efx->int_error_count = 0;
		efx->int_error_expire =
			jiffies + EF4_INT_ERROR_EXPIRE * HZ;
	}
	if (++efx->int_error_count < EF4_MAX_INT_ERRORS) {
		netif_err(efx, hw, efx->net_dev,
			  "SYSTEM ERROR - reset scheduled\n");
		ef4_schedule_reset(efx, RESET_TYPE_INT_ERROR);
	} else {
		netif_err(efx, hw, efx->net_dev,
			  "SYSTEM ERROR - max number of errors seen."
			  "NIC will be disabled\n");
		ef4_schedule_reset(efx, RESET_TYPE_DISABLE);
	}

	return IRQ_HANDLED;
}

/* Handle a legacy interrupt
 * Acknowledges the interrupt and schedule event queue processing.
 */
irqreturn_t ef4_farch_legacy_interrupt(int irq, void *dev_id)
{
	struct ef4_nic *efx = dev_id;
	bool soft_enabled = ACCESS_ONCE(efx->irq_soft_enabled);
	ef4_oword_t *int_ker = efx->irq_status.addr;
	irqreturn_t result = IRQ_NONE;
	struct ef4_channel *channel;
	ef4_dword_t reg;
	u32 queues;
	int syserr;

	/* Read the ISR which also ACKs the interrupts */
	ef4_readd(efx, &reg, FR_BZ_INT_ISR0);
	queues = EF4_EXTRACT_DWORD(reg, 0, 31);

	/* Legacy interrupts are disabled too late by the EEH kernel
	 * code. Disable them earlier.
	 * If an EEH error occurred, the read will have returned all ones.
	 */
	if (EF4_DWORD_IS_ALL_ONES(reg) && ef4_try_recovery(efx) &&
	    !efx->eeh_disabled_legacy_irq) {
		disable_irq_nosync(efx->legacy_irq);
		efx->eeh_disabled_legacy_irq = true;
	}

	/* Handle non-event-queue sources */
	if (queues & (1U << efx->irq_level) && soft_enabled) {
		syserr = EF4_OWORD_FIELD(*int_ker, FSF_AZ_NET_IVEC_FATAL_INT);
		if (unlikely(syserr))
			return ef4_farch_fatal_interrupt(efx);
		efx->last_irq_cpu = raw_smp_processor_id();
	}

	if (queues != 0) {
		efx->irq_zero_count = 0;

		/* Schedule processing of any interrupting queues */
		if (likely(soft_enabled)) {
			ef4_for_each_channel(channel, efx) {
				if (queues & 1)
					ef4_schedule_channel_irq(channel);
				queues >>= 1;
			}
		}
		result = IRQ_HANDLED;

	} else {
		ef4_qword_t *event;

		/* Legacy ISR read can return zero once (SF bug 15783) */

		/* We can't return IRQ_HANDLED more than once on seeing ISR=0
		 * because this might be a shared interrupt. */
		if (efx->irq_zero_count++ == 0)
			result = IRQ_HANDLED;

		/* Ensure we schedule or rearm all event queues */
		if (likely(soft_enabled)) {
			ef4_for_each_channel(channel, efx) {
				event = ef4_event(channel,
						  channel->eventq_read_ptr);
				if (ef4_event_present(event))
					ef4_schedule_channel_irq(channel);
				else
					ef4_farch_ev_read_ack(channel);
			}
		}
	}

	if (result == IRQ_HANDLED)
		netif_vdbg(efx, intr, efx->net_dev,
			   "IRQ %d on CPU %d status " EF4_DWORD_FMT "\n",
			   irq, raw_smp_processor_id(), EF4_DWORD_VAL(reg));

	return result;
}

/* Handle an MSI interrupt
 *
 * Handle an MSI hardware interrupt.  This routine schedules event
 * queue processing.  No interrupt acknowledgement cycle is necessary.
 * Also, we never need to check that the interrupt is for us, since
 * MSI interrupts cannot be shared.
 */
irqreturn_t ef4_farch_msi_interrupt(int irq, void *dev_id)
{
	struct ef4_msi_context *context = dev_id;
	struct ef4_nic *efx = context->efx;
	ef4_oword_t *int_ker = efx->irq_status.addr;
	int syserr;

	netif_vdbg(efx, intr, efx->net_dev,
		   "IRQ %d on CPU %d status " EF4_OWORD_FMT "\n",
		   irq, raw_smp_processor_id(), EF4_OWORD_VAL(*int_ker));

	if (!likely(ACCESS_ONCE(efx->irq_soft_enabled)))
		return IRQ_HANDLED;

	/* Handle non-event-queue sources */
	if (context->index == efx->irq_level) {
		syserr = EF4_OWORD_FIELD(*int_ker, FSF_AZ_NET_IVEC_FATAL_INT);
		if (unlikely(syserr))
			return ef4_farch_fatal_interrupt(efx);
		efx->last_irq_cpu = raw_smp_processor_id();
	}

	/* Schedule processing of the channel */
	ef4_schedule_channel_irq(efx->channel[context->index]);

	return IRQ_HANDLED;
}

/* Setup RSS indirection table.
 * This maps from the hash value of the packet to RXQ
 */
void ef4_farch_rx_push_indir_table(struct ef4_nic *efx)
{
	size_t i = 0;
	ef4_dword_t dword;

	BUG_ON(ef4_nic_rev(efx) < EF4_REV_FALCON_B0);

	BUILD_BUG_ON(ARRAY_SIZE(efx->rx_indir_table) !=
		     FR_BZ_RX_INDIRECTION_TBL_ROWS);

	for (i = 0; i < FR_BZ_RX_INDIRECTION_TBL_ROWS; i++) {
		EF4_POPULATE_DWORD_1(dword, FRF_BZ_IT_QUEUE,
				     efx->rx_indir_table[i]);
		ef4_writed(efx, &dword,
			   FR_BZ_RX_INDIRECTION_TBL +
			   FR_BZ_RX_INDIRECTION_TBL_STEP * i);
	}
}

/* Looks at available SRAM resources and works out how many queues we
 * can support, and where things like descriptor caches should live.
 *
 * SRAM is split up as follows:
 * 0                          buftbl entries for channels
 * efx->vf_buftbl_base        buftbl entries for SR-IOV
 * efx->rx_dc_base            RX descriptor caches
 * efx->tx_dc_base            TX descriptor caches
 */
void ef4_farch_dimension_resources(struct ef4_nic *efx, unsigned sram_lim_qw)
{
	unsigned vi_count, buftbl_min;

	/* Account for the buffer table entries backing the datapath channels
	 * and the descriptor caches for those channels.
	 */
	buftbl_min = ((efx->n_rx_channels * EF4_MAX_DMAQ_SIZE +
		       efx->n_tx_channels * EF4_TXQ_TYPES * EF4_MAX_DMAQ_SIZE +
		       efx->n_channels * EF4_MAX_EVQ_SIZE)
		      * sizeof(ef4_qword_t) / EF4_BUF_SIZE);
	vi_count = max(efx->n_channels, efx->n_tx_channels * EF4_TXQ_TYPES);

	efx->tx_dc_base = sram_lim_qw - vi_count * TX_DC_ENTRIES;
	efx->rx_dc_base = efx->tx_dc_base - vi_count * RX_DC_ENTRIES;
}

u32 ef4_farch_fpga_ver(struct ef4_nic *efx)
{
	ef4_oword_t altera_build;
	ef4_reado(efx, &altera_build, FR_AZ_ALTERA_BUILD);
	return EF4_OWORD_FIELD(altera_build, FRF_AZ_ALTERA_BUILD_VER);
}

void ef4_farch_init_common(struct ef4_nic *efx)
{
	ef4_oword_t temp;

	/* Set positions of descriptor caches in SRAM. */
	EF4_POPULATE_OWORD_1(temp, FRF_AZ_SRM_TX_DC_BASE_ADR, efx->tx_dc_base);
	ef4_writeo(efx, &temp, FR_AZ_SRM_TX_DC_CFG);
	EF4_POPULATE_OWORD_1(temp, FRF_AZ_SRM_RX_DC_BASE_ADR, efx->rx_dc_base);
	ef4_writeo(efx, &temp, FR_AZ_SRM_RX_DC_CFG);

	/* Set TX descriptor cache size. */
	BUILD_BUG_ON(TX_DC_ENTRIES != (8 << TX_DC_ENTRIES_ORDER));
	EF4_POPULATE_OWORD_1(temp, FRF_AZ_TX_DC_SIZE, TX_DC_ENTRIES_ORDER);
	ef4_writeo(efx, &temp, FR_AZ_TX_DC_CFG);

	/* Set RX descriptor cache size.  Set low watermark to size-8, as
	 * this allows most efficient prefetching.
	 */
	BUILD_BUG_ON(RX_DC_ENTRIES != (8 << RX_DC_ENTRIES_ORDER));
	EF4_POPULATE_OWORD_1(temp, FRF_AZ_RX_DC_SIZE, RX_DC_ENTRIES_ORDER);
	ef4_writeo(efx, &temp, FR_AZ_RX_DC_CFG);
	EF4_POPULATE_OWORD_1(temp, FRF_AZ_RX_DC_PF_LWM, RX_DC_ENTRIES - 8);
	ef4_writeo(efx, &temp, FR_AZ_RX_DC_PF_WM);

	/* Program INT_KER address */
	EF4_POPULATE_OWORD_2(temp,
			     FRF_AZ_NORM_INT_VEC_DIS_KER,
			     EF4_INT_MODE_USE_MSI(efx),
			     FRF_AZ_INT_ADR_KER, efx->irq_status.dma_addr);
	ef4_writeo(efx, &temp, FR_AZ_INT_ADR_KER);

	/* Use a valid MSI-X vector */
	efx->irq_level = 0;

	/* Enable all the genuinely fatal interrupts.  (They are still
	 * masked by the overall interrupt mask, controlled by
	 * falcon_interrupts()).
	 *
	 * Note: All other fatal interrupts are enabled
	 */
	EF4_POPULATE_OWORD_3(temp,
			     FRF_AZ_ILL_ADR_INT_KER_EN, 1,
			     FRF_AZ_RBUF_OWN_INT_KER_EN, 1,
			     FRF_AZ_TBUF_OWN_INT_KER_EN, 1);
	EF4_INVERT_OWORD(temp);
	ef4_writeo(efx, &temp, FR_AZ_FATAL_INTR_KER);

	/* Disable the ugly timer-based TX DMA backoff and allow TX DMA to be
	 * controlled by the RX FIFO fill level. Set arbitration to one pkt/Q.
	 */
	ef4_reado(efx, &temp, FR_AZ_TX_RESERVED);
	EF4_SET_OWORD_FIELD(temp, FRF_AZ_TX_RX_SPACER, 0xfe);
	EF4_SET_OWORD_FIELD(temp, FRF_AZ_TX_RX_SPACER_EN, 1);
	EF4_SET_OWORD_FIELD(temp, FRF_AZ_TX_ONE_PKT_PER_Q, 1);
	EF4_SET_OWORD_FIELD(temp, FRF_AZ_TX_PUSH_EN, 1);
	EF4_SET_OWORD_FIELD(temp, FRF_AZ_TX_DIS_NON_IP_EV, 1);
	/* Enable SW_EV to inherit in char driver - assume harmless here */
	EF4_SET_OWORD_FIELD(temp, FRF_AZ_TX_SOFT_EVT_EN, 1);
	/* Prefetch threshold 2 => fetch when descriptor cache half empty */
	EF4_SET_OWORD_FIELD(temp, FRF_AZ_TX_PREF_THRESHOLD, 2);
	/* Disable hardware watchdog which can misfire */
	EF4_SET_OWORD_FIELD(temp, FRF_AZ_TX_PREF_WD_TMR, 0x3fffff);
	/* Squash TX of packets of 16 bytes or less */
	if (ef4_nic_rev(efx) >= EF4_REV_FALCON_B0)
		EF4_SET_OWORD_FIELD(temp, FRF_BZ_TX_FLUSH_MIN_LEN_EN, 1);
	ef4_writeo(efx, &temp, FR_AZ_TX_RESERVED);

	if (ef4_nic_rev(efx) >= EF4_REV_FALCON_B0) {
		EF4_POPULATE_OWORD_4(temp,
				     /* Default values */
				     FRF_BZ_TX_PACE_SB_NOT_AF, 0x15,
				     FRF_BZ_TX_PACE_SB_AF, 0xb,
				     FRF_BZ_TX_PACE_FB_BASE, 0,
				     /* Allow large pace values in the
				      * fast bin. */
				     FRF_BZ_TX_PACE_BIN_TH,
				     FFE_BZ_TX_PACE_RESERVED);
		ef4_writeo(efx, &temp, FR_BZ_TX_PACE);
	}
}

/**************************************************************************
 *
 * Filter tables
 *
 **************************************************************************
 */

/* "Fudge factors" - difference between programmed value and actual depth.
 * Due to pipelined implementation we need to program H/W with a value that
 * is larger than the hop limit we want.
 */
#define EF4_FARCH_FILTER_CTL_SRCH_FUDGE_WILD 3
#define EF4_FARCH_FILTER_CTL_SRCH_FUDGE_FULL 1

/* Hard maximum search limit.  Hardware will time-out beyond 200-something.
 * We also need to avoid infinite loops in ef4_farch_filter_search() when the
 * table is full.
 */
#define EF4_FARCH_FILTER_CTL_SRCH_MAX 200

/* Don't try very hard to find space for performance hints, as this is
 * counter-productive. */
#define EF4_FARCH_FILTER_CTL_SRCH_HINT_MAX 5

enum ef4_farch_filter_type {
	EF4_FARCH_FILTER_TCP_FULL = 0,
	EF4_FARCH_FILTER_TCP_WILD,
	EF4_FARCH_FILTER_UDP_FULL,
	EF4_FARCH_FILTER_UDP_WILD,
	EF4_FARCH_FILTER_MAC_FULL = 4,
	EF4_FARCH_FILTER_MAC_WILD,
	EF4_FARCH_FILTER_UC_DEF = 8,
	EF4_FARCH_FILTER_MC_DEF,
	EF4_FARCH_FILTER_TYPE_COUNT,		/* number of specific types */
};

enum ef4_farch_filter_table_id {
	EF4_FARCH_FILTER_TABLE_RX_IP = 0,
	EF4_FARCH_FILTER_TABLE_RX_MAC,
	EF4_FARCH_FILTER_TABLE_RX_DEF,
	EF4_FARCH_FILTER_TABLE_TX_MAC,
	EF4_FARCH_FILTER_TABLE_COUNT,
};

enum ef4_farch_filter_index {
	EF4_FARCH_FILTER_INDEX_UC_DEF,
	EF4_FARCH_FILTER_INDEX_MC_DEF,
	EF4_FARCH_FILTER_SIZE_RX_DEF,
};

struct ef4_farch_filter_spec {
	u8	type:4;
	u8	priority:4;
	u8	flags;
	u16	dmaq_id;
	u32	data[3];
};

struct ef4_farch_filter_table {
	enum ef4_farch_filter_table_id id;
	u32		offset;		/* address of table relative to BAR */
	unsigned	size;		/* number of entries */
	unsigned	step;		/* step between entries */
	unsigned	used;		/* number currently used */
	unsigned long	*used_bitmap;
	struct ef4_farch_filter_spec *spec;
	unsigned	search_limit[EF4_FARCH_FILTER_TYPE_COUNT];
};

struct ef4_farch_filter_state {
	struct ef4_farch_filter_table table[EF4_FARCH_FILTER_TABLE_COUNT];
};

static void
ef4_farch_filter_table_clear_entry(struct ef4_nic *efx,
				   struct ef4_farch_filter_table *table,
				   unsigned int filter_idx);

/* The filter hash function is LFSR polynomial x^16 + x^3 + 1 of a 32-bit
 * key derived from the n-tuple.  The initial LFSR state is 0xffff. */
static u16 ef4_farch_filter_hash(u32 key)
{
	u16 tmp;

	/* First 16 rounds */
	tmp = 0x1fff ^ key >> 16;
	tmp = tmp ^ tmp >> 3 ^ tmp >> 6;
	tmp = tmp ^ tmp >> 9;
	/* Last 16 rounds */
	tmp = tmp ^ tmp << 13 ^ key;
	tmp = tmp ^ tmp >> 3 ^ tmp >> 6;
	return tmp ^ tmp >> 9;
}

/* To allow for hash collisions, filter search continues at these
 * increments from the first possible entry selected by the hash. */
static u16 ef4_farch_filter_increment(u32 key)
{
	return key * 2 - 1;
}

static enum ef4_farch_filter_table_id
ef4_farch_filter_spec_table_id(const struct ef4_farch_filter_spec *spec)
{
	BUILD_BUG_ON(EF4_FARCH_FILTER_TABLE_RX_IP !=
		     (EF4_FARCH_FILTER_TCP_FULL >> 2));
	BUILD_BUG_ON(EF4_FARCH_FILTER_TABLE_RX_IP !=
		     (EF4_FARCH_FILTER_TCP_WILD >> 2));
	BUILD_BUG_ON(EF4_FARCH_FILTER_TABLE_RX_IP !=
		     (EF4_FARCH_FILTER_UDP_FULL >> 2));
	BUILD_BUG_ON(EF4_FARCH_FILTER_TABLE_RX_IP !=
		     (EF4_FARCH_FILTER_UDP_WILD >> 2));
	BUILD_BUG_ON(EF4_FARCH_FILTER_TABLE_RX_MAC !=
		     (EF4_FARCH_FILTER_MAC_FULL >> 2));
	BUILD_BUG_ON(EF4_FARCH_FILTER_TABLE_RX_MAC !=
		     (EF4_FARCH_FILTER_MAC_WILD >> 2));
	BUILD_BUG_ON(EF4_FARCH_FILTER_TABLE_TX_MAC !=
		     EF4_FARCH_FILTER_TABLE_RX_MAC + 2);
	return (spec->type >> 2) + ((spec->flags & EF4_FILTER_FLAG_TX) ? 2 : 0);
}

static void ef4_farch_filter_push_rx_config(struct ef4_nic *efx)
{
	struct ef4_farch_filter_state *state = efx->filter_state;
	struct ef4_farch_filter_table *table;
	ef4_oword_t filter_ctl;

	ef4_reado(efx, &filter_ctl, FR_BZ_RX_FILTER_CTL);

	table = &state->table[EF4_FARCH_FILTER_TABLE_RX_IP];
	EF4_SET_OWORD_FIELD(filter_ctl, FRF_BZ_TCP_FULL_SRCH_LIMIT,
			    table->search_limit[EF4_FARCH_FILTER_TCP_FULL] +
			    EF4_FARCH_FILTER_CTL_SRCH_FUDGE_FULL);
	EF4_SET_OWORD_FIELD(filter_ctl, FRF_BZ_TCP_WILD_SRCH_LIMIT,
			    table->search_limit[EF4_FARCH_FILTER_TCP_WILD] +
			    EF4_FARCH_FILTER_CTL_SRCH_FUDGE_WILD);
	EF4_SET_OWORD_FIELD(filter_ctl, FRF_BZ_UDP_FULL_SRCH_LIMIT,
			    table->search_limit[EF4_FARCH_FILTER_UDP_FULL] +
			    EF4_FARCH_FILTER_CTL_SRCH_FUDGE_FULL);
	EF4_SET_OWORD_FIELD(filter_ctl, FRF_BZ_UDP_WILD_SRCH_LIMIT,
			    table->search_limit[EF4_FARCH_FILTER_UDP_WILD] +
			    EF4_FARCH_FILTER_CTL_SRCH_FUDGE_WILD);

	table = &state->table[EF4_FARCH_FILTER_TABLE_RX_MAC];
	if (table->size) {
		EF4_SET_OWORD_FIELD(
			filter_ctl, FRF_CZ_ETHERNET_FULL_SEARCH_LIMIT,
			table->search_limit[EF4_FARCH_FILTER_MAC_FULL] +
			EF4_FARCH_FILTER_CTL_SRCH_FUDGE_FULL);
		EF4_SET_OWORD_FIELD(
			filter_ctl, FRF_CZ_ETHERNET_WILDCARD_SEARCH_LIMIT,
			table->search_limit[EF4_FARCH_FILTER_MAC_WILD] +
			EF4_FARCH_FILTER_CTL_SRCH_FUDGE_WILD);
	}

	table = &state->table[EF4_FARCH_FILTER_TABLE_RX_DEF];
	if (table->size) {
		EF4_SET_OWORD_FIELD(
			filter_ctl, FRF_CZ_UNICAST_NOMATCH_Q_ID,
			table->spec[EF4_FARCH_FILTER_INDEX_UC_DEF].dmaq_id);
		EF4_SET_OWORD_FIELD(
			filter_ctl, FRF_CZ_UNICAST_NOMATCH_RSS_ENABLED,
			!!(table->spec[EF4_FARCH_FILTER_INDEX_UC_DEF].flags &
			   EF4_FILTER_FLAG_RX_RSS));
		EF4_SET_OWORD_FIELD(
			filter_ctl, FRF_CZ_MULTICAST_NOMATCH_Q_ID,
			table->spec[EF4_FARCH_FILTER_INDEX_MC_DEF].dmaq_id);
		EF4_SET_OWORD_FIELD(
			filter_ctl, FRF_CZ_MULTICAST_NOMATCH_RSS_ENABLED,
			!!(table->spec[EF4_FARCH_FILTER_INDEX_MC_DEF].flags &
			   EF4_FILTER_FLAG_RX_RSS));

		/* There is a single bit to enable RX scatter for all
		 * unmatched packets.  Only set it if scatter is
		 * enabled in both filter specs.
		 */
		EF4_SET_OWORD_FIELD(
			filter_ctl, FRF_BZ_SCATTER_ENBL_NO_MATCH_Q,
			!!(table->spec[EF4_FARCH_FILTER_INDEX_UC_DEF].flags &
			   table->spec[EF4_FARCH_FILTER_INDEX_MC_DEF].flags &
			   EF4_FILTER_FLAG_RX_SCATTER));
	} else if (ef4_nic_rev(efx) >= EF4_REV_FALCON_B0) {
		/* We don't expose 'default' filters because unmatched
		 * packets always go to the queue number found in the
		 * RSS table.  But we still need to set the RX scatter
		 * bit here.
		 */
		EF4_SET_OWORD_FIELD(
			filter_ctl, FRF_BZ_SCATTER_ENBL_NO_MATCH_Q,
			efx->rx_scatter);
	}

	ef4_writeo(efx, &filter_ctl, FR_BZ_RX_FILTER_CTL);
}

static void ef4_farch_filter_push_tx_limits(struct ef4_nic *efx)
{
	struct ef4_farch_filter_state *state = efx->filter_state;
	struct ef4_farch_filter_table *table;
	ef4_oword_t tx_cfg;

	ef4_reado(efx, &tx_cfg, FR_AZ_TX_CFG);

	table = &state->table[EF4_FARCH_FILTER_TABLE_TX_MAC];
	if (table->size) {
		EF4_SET_OWORD_FIELD(
			tx_cfg, FRF_CZ_TX_ETH_FILTER_FULL_SEARCH_RANGE,
			table->search_limit[EF4_FARCH_FILTER_MAC_FULL] +
			EF4_FARCH_FILTER_CTL_SRCH_FUDGE_FULL);
		EF4_SET_OWORD_FIELD(
			tx_cfg, FRF_CZ_TX_ETH_FILTER_WILD_SEARCH_RANGE,
			table->search_limit[EF4_FARCH_FILTER_MAC_WILD] +
			EF4_FARCH_FILTER_CTL_SRCH_FUDGE_WILD);
	}

	ef4_writeo(efx, &tx_cfg, FR_AZ_TX_CFG);
}

static int
ef4_farch_filter_from_gen_spec(struct ef4_farch_filter_spec *spec,
			       const struct ef4_filter_spec *gen_spec)
{
	bool is_full = false;

	if ((gen_spec->flags & EF4_FILTER_FLAG_RX_RSS) &&
	    gen_spec->rss_context != EF4_FILTER_RSS_CONTEXT_DEFAULT)
		return -EINVAL;

	spec->priority = gen_spec->priority;
	spec->flags = gen_spec->flags;
	spec->dmaq_id = gen_spec->dmaq_id;

	switch (gen_spec->match_flags) {
	case (EF4_FILTER_MATCH_ETHER_TYPE | EF4_FILTER_MATCH_IP_PROTO |
	      EF4_FILTER_MATCH_LOC_HOST | EF4_FILTER_MATCH_LOC_PORT |
	      EF4_FILTER_MATCH_REM_HOST | EF4_FILTER_MATCH_REM_PORT):
		is_full = true;
		/* fall through */
	case (EF4_FILTER_MATCH_ETHER_TYPE | EF4_FILTER_MATCH_IP_PROTO |
	      EF4_FILTER_MATCH_LOC_HOST | EF4_FILTER_MATCH_LOC_PORT): {
		__be32 rhost, host1, host2;
		__be16 rport, port1, port2;

		EF4_BUG_ON_PARANOID(!(gen_spec->flags & EF4_FILTER_FLAG_RX));

		if (gen_spec->ether_type != htons(ETH_P_IP))
			return -EPROTONOSUPPORT;
		if (gen_spec->loc_port == 0 ||
		    (is_full && gen_spec->rem_port == 0))
			return -EADDRNOTAVAIL;
		switch (gen_spec->ip_proto) {
		case IPPROTO_TCP:
			spec->type = (is_full ? EF4_FARCH_FILTER_TCP_FULL :
				      EF4_FARCH_FILTER_TCP_WILD);
			break;
		case IPPROTO_UDP:
			spec->type = (is_full ? EF4_FARCH_FILTER_UDP_FULL :
				      EF4_FARCH_FILTER_UDP_WILD);
			break;
		default:
			return -EPROTONOSUPPORT;
		}

		/* Filter is constructed in terms of source and destination,
		 * with the odd wrinkle that the ports are swapped in a UDP
		 * wildcard filter.  We need to convert from local and remote
		 * (= zero for wildcard) addresses.
		 */
		rhost = is_full ? gen_spec->rem_host[0] : 0;
		rport = is_full ? gen_spec->rem_port : 0;
		host1 = rhost;
		host2 = gen_spec->loc_host[0];
		if (!is_full && gen_spec->ip_proto == IPPROTO_UDP) {
			port1 = gen_spec->loc_port;
			port2 = rport;
		} else {
			port1 = rport;
			port2 = gen_spec->loc_port;
		}
		spec->data[0] = ntohl(host1) << 16 | ntohs(port1);
		spec->data[1] = ntohs(port2) << 16 | ntohl(host1) >> 16;
		spec->data[2] = ntohl(host2);

		break;
	}

	case EF4_FILTER_MATCH_LOC_MAC | EF4_FILTER_MATCH_OUTER_VID:
		is_full = true;
		/* fall through */
	case EF4_FILTER_MATCH_LOC_MAC:
		spec->type = (is_full ? EF4_FARCH_FILTER_MAC_FULL :
			      EF4_FARCH_FILTER_MAC_WILD);
		spec->data[0] = is_full ? ntohs(gen_spec->outer_vid) : 0;
		spec->data[1] = (gen_spec->loc_mac[2] << 24 |
				 gen_spec->loc_mac[3] << 16 |
				 gen_spec->loc_mac[4] << 8 |
				 gen_spec->loc_mac[5]);
		spec->data[2] = (gen_spec->loc_mac[0] << 8 |
				 gen_spec->loc_mac[1]);
		break;

	case EF4_FILTER_MATCH_LOC_MAC_IG:
		spec->type = (is_multicast_ether_addr(gen_spec->loc_mac) ?
			      EF4_FARCH_FILTER_MC_DEF :
			      EF4_FARCH_FILTER_UC_DEF);
		memset(spec->data, 0, sizeof(spec->data)); /* ensure equality */
		break;

	default:
		return -EPROTONOSUPPORT;
	}

	return 0;
}

static void
ef4_farch_filter_to_gen_spec(struct ef4_filter_spec *gen_spec,
			     const struct ef4_farch_filter_spec *spec)
{
	bool is_full = false;

	/* *gen_spec should be completely initialised, to be consistent
	 * with ef4_filter_init_{rx,tx}() and in case we want to copy
	 * it back to userland.
	 */
	memset(gen_spec, 0, sizeof(*gen_spec));

	gen_spec->priority = spec->priority;
	gen_spec->flags = spec->flags;
	gen_spec->dmaq_id = spec->dmaq_id;

	switch (spec->type) {
	case EF4_FARCH_FILTER_TCP_FULL:
	case EF4_FARCH_FILTER_UDP_FULL:
		is_full = true;
		/* fall through */
	case EF4_FARCH_FILTER_TCP_WILD:
	case EF4_FARCH_FILTER_UDP_WILD: {
		__be32 host1, host2;
		__be16 port1, port2;

		gen_spec->match_flags =
			EF4_FILTER_MATCH_ETHER_TYPE |
			EF4_FILTER_MATCH_IP_PROTO |
			EF4_FILTER_MATCH_LOC_HOST | EF4_FILTER_MATCH_LOC_PORT;
		if (is_full)
			gen_spec->match_flags |= (EF4_FILTER_MATCH_REM_HOST |
						  EF4_FILTER_MATCH_REM_PORT);
		gen_spec->ether_type = htons(ETH_P_IP);
		gen_spec->ip_proto =
			(spec->type == EF4_FARCH_FILTER_TCP_FULL ||
			 spec->type == EF4_FARCH_FILTER_TCP_WILD) ?
			IPPROTO_TCP : IPPROTO_UDP;

		host1 = htonl(spec->data[0] >> 16 | spec->data[1] << 16);
		port1 = htons(spec->data[0]);
		host2 = htonl(spec->data[2]);
		port2 = htons(spec->data[1] >> 16);
		if (spec->flags & EF4_FILTER_FLAG_TX) {
			gen_spec->loc_host[0] = host1;
			gen_spec->rem_host[0] = host2;
		} else {
			gen_spec->loc_host[0] = host2;
			gen_spec->rem_host[0] = host1;
		}
		if (!!(gen_spec->flags & EF4_FILTER_FLAG_TX) ^
		    (!is_full && gen_spec->ip_proto == IPPROTO_UDP)) {
			gen_spec->loc_port = port1;
			gen_spec->rem_port = port2;
		} else {
			gen_spec->loc_port = port2;
			gen_spec->rem_port = port1;
		}

		break;
	}

	case EF4_FARCH_FILTER_MAC_FULL:
		is_full = true;
		/* fall through */
	case EF4_FARCH_FILTER_MAC_WILD:
		gen_spec->match_flags = EF4_FILTER_MATCH_LOC_MAC;
		if (is_full)
			gen_spec->match_flags |= EF4_FILTER_MATCH_OUTER_VID;
		gen_spec->loc_mac[0] = spec->data[2] >> 8;
		gen_spec->loc_mac[1] = spec->data[2];
		gen_spec->loc_mac[2] = spec->data[1] >> 24;
		gen_spec->loc_mac[3] = spec->data[1] >> 16;
		gen_spec->loc_mac[4] = spec->data[1] >> 8;
		gen_spec->loc_mac[5] = spec->data[1];
		gen_spec->outer_vid = htons(spec->data[0]);
		break;

	case EF4_FARCH_FILTER_UC_DEF:
	case EF4_FARCH_FILTER_MC_DEF:
		gen_spec->match_flags = EF4_FILTER_MATCH_LOC_MAC_IG;
		gen_spec->loc_mac[0] = spec->type == EF4_FARCH_FILTER_MC_DEF;
		break;

	default:
		WARN_ON(1);
		break;
	}
}

static void
ef4_farch_filter_init_rx_auto(struct ef4_nic *efx,
			      struct ef4_farch_filter_spec *spec)
{
	/* If there's only one channel then disable RSS for non VF
	 * traffic, thereby allowing VFs to use RSS when the PF can't.
	 */
	spec->priority = EF4_FILTER_PRI_AUTO;
	spec->flags = (EF4_FILTER_FLAG_RX |
		       (ef4_rss_enabled(efx) ? EF4_FILTER_FLAG_RX_RSS : 0) |
		       (efx->rx_scatter ? EF4_FILTER_FLAG_RX_SCATTER : 0));
	spec->dmaq_id = 0;
}

/* Build a filter entry and return its n-tuple key. */
static u32 ef4_farch_filter_build(ef4_oword_t *filter,
				  struct ef4_farch_filter_spec *spec)
{
	u32 data3;

	switch (ef4_farch_filter_spec_table_id(spec)) {
	case EF4_FARCH_FILTER_TABLE_RX_IP: {
		bool is_udp = (spec->type == EF4_FARCH_FILTER_UDP_FULL ||
			       spec->type == EF4_FARCH_FILTER_UDP_WILD);
		EF4_POPULATE_OWORD_7(
			*filter,
			FRF_BZ_RSS_EN,
			!!(spec->flags & EF4_FILTER_FLAG_RX_RSS),
			FRF_BZ_SCATTER_EN,
			!!(spec->flags & EF4_FILTER_FLAG_RX_SCATTER),
			FRF_BZ_TCP_UDP, is_udp,
			FRF_BZ_RXQ_ID, spec->dmaq_id,
			EF4_DWORD_2, spec->data[2],
			EF4_DWORD_1, spec->data[1],
			EF4_DWORD_0, spec->data[0]);
		data3 = is_udp;
		break;
	}

	case EF4_FARCH_FILTER_TABLE_RX_MAC: {
		bool is_wild = spec->type == EF4_FARCH_FILTER_MAC_WILD;
		EF4_POPULATE_OWORD_7(
			*filter,
			FRF_CZ_RMFT_RSS_EN,
			!!(spec->flags & EF4_FILTER_FLAG_RX_RSS),
			FRF_CZ_RMFT_SCATTER_EN,
			!!(spec->flags & EF4_FILTER_FLAG_RX_SCATTER),
			FRF_CZ_RMFT_RXQ_ID, spec->dmaq_id,
			FRF_CZ_RMFT_WILDCARD_MATCH, is_wild,
			FRF_CZ_RMFT_DEST_MAC_HI, spec->data[2],
			FRF_CZ_RMFT_DEST_MAC_LO, spec->data[1],
			FRF_CZ_RMFT_VLAN_ID, spec->data[0]);
		data3 = is_wild;
		break;
	}

	case EF4_FARCH_FILTER_TABLE_TX_MAC: {
		bool is_wild = spec->type == EF4_FARCH_FILTER_MAC_WILD;
		EF4_POPULATE_OWORD_5(*filter,
				     FRF_CZ_TMFT_TXQ_ID, spec->dmaq_id,
				     FRF_CZ_TMFT_WILDCARD_MATCH, is_wild,
				     FRF_CZ_TMFT_SRC_MAC_HI, spec->data[2],
				     FRF_CZ_TMFT_SRC_MAC_LO, spec->data[1],
				     FRF_CZ_TMFT_VLAN_ID, spec->data[0]);
		data3 = is_wild | spec->dmaq_id << 1;
		break;
	}

	default:
		BUG();
	}

	return spec->data[0] ^ spec->data[1] ^ spec->data[2] ^ data3;
}

static bool ef4_farch_filter_equal(const struct ef4_farch_filter_spec *left,
				   const struct ef4_farch_filter_spec *right)
{
	if (left->type != right->type ||
	    memcmp(left->data, right->data, sizeof(left->data)))
		return false;

	if (left->flags & EF4_FILTER_FLAG_TX &&
	    left->dmaq_id != right->dmaq_id)
		return false;

	return true;
}

/*
 * Construct/deconstruct external filter IDs.  At least the RX filter
 * IDs must be ordered by matching priority, for RX NFC semantics.
 *
 * Deconstruction needs to be robust against invalid IDs so that
 * ef4_filter_remove_id_safe() and ef4_filter_get_filter_safe() can
 * accept user-provided IDs.
 */

#define EF4_FARCH_FILTER_MATCH_PRI_COUNT	5

static const u8 ef4_farch_filter_type_match_pri[EF4_FARCH_FILTER_TYPE_COUNT] = {
	[EF4_FARCH_FILTER_TCP_FULL]	= 0,
	[EF4_FARCH_FILTER_UDP_FULL]	= 0,
	[EF4_FARCH_FILTER_TCP_WILD]	= 1,
	[EF4_FARCH_FILTER_UDP_WILD]	= 1,
	[EF4_FARCH_FILTER_MAC_FULL]	= 2,
	[EF4_FARCH_FILTER_MAC_WILD]	= 3,
	[EF4_FARCH_FILTER_UC_DEF]	= 4,
	[EF4_FARCH_FILTER_MC_DEF]	= 4,
};

static const enum ef4_farch_filter_table_id ef4_farch_filter_range_table[] = {
	EF4_FARCH_FILTER_TABLE_RX_IP,	/* RX match pri 0 */
	EF4_FARCH_FILTER_TABLE_RX_IP,
	EF4_FARCH_FILTER_TABLE_RX_MAC,
	EF4_FARCH_FILTER_TABLE_RX_MAC,
	EF4_FARCH_FILTER_TABLE_RX_DEF,	/* RX match pri 4 */
	EF4_FARCH_FILTER_TABLE_TX_MAC,	/* TX match pri 0 */
	EF4_FARCH_FILTER_TABLE_TX_MAC,	/* TX match pri 1 */
};

#define EF4_FARCH_FILTER_INDEX_WIDTH 13
#define EF4_FARCH_FILTER_INDEX_MASK ((1 << EF4_FARCH_FILTER_INDEX_WIDTH) - 1)

static inline u32
ef4_farch_filter_make_id(const struct ef4_farch_filter_spec *spec,
			 unsigned int index)
{
	unsigned int range;

	range = ef4_farch_filter_type_match_pri[spec->type];
	if (!(spec->flags & EF4_FILTER_FLAG_RX))
		range += EF4_FARCH_FILTER_MATCH_PRI_COUNT;

	return range << EF4_FARCH_FILTER_INDEX_WIDTH | index;
}

static inline enum ef4_farch_filter_table_id
ef4_farch_filter_id_table_id(u32 id)
{
	unsigned int range = id >> EF4_FARCH_FILTER_INDEX_WIDTH;

	if (range < ARRAY_SIZE(ef4_farch_filter_range_table))
		return ef4_farch_filter_range_table[range];
	else
		return EF4_FARCH_FILTER_TABLE_COUNT; /* invalid */
}

static inline unsigned int ef4_farch_filter_id_index(u32 id)
{
	return id & EF4_FARCH_FILTER_INDEX_MASK;
}

u32 ef4_farch_filter_get_rx_id_limit(struct ef4_nic *efx)
{
	struct ef4_farch_filter_state *state = efx->filter_state;
	unsigned int range = EF4_FARCH_FILTER_MATCH_PRI_COUNT - 1;
	enum ef4_farch_filter_table_id table_id;

	do {
		table_id = ef4_farch_filter_range_table[range];
		if (state->table[table_id].size != 0)
			return range << EF4_FARCH_FILTER_INDEX_WIDTH |
				state->table[table_id].size;
	} while (range--);

	return 0;
}

s32 ef4_farch_filter_insert(struct ef4_nic *efx,
			    struct ef4_filter_spec *gen_spec,
			    bool replace_equal)
{
	struct ef4_farch_filter_state *state = efx->filter_state;
	struct ef4_farch_filter_table *table;
	struct ef4_farch_filter_spec spec;
	ef4_oword_t filter;
	int rep_index, ins_index;
	unsigned int depth = 0;
	int rc;

	rc = ef4_farch_filter_from_gen_spec(&spec, gen_spec);
	if (rc)
		return rc;

	table = &state->table[ef4_farch_filter_spec_table_id(&spec)];
	if (table->size == 0)
		return -EINVAL;

	netif_vdbg(efx, hw, efx->net_dev,
		   "%s: type %d search_limit=%d", __func__, spec.type,
		   table->search_limit[spec.type]);

	if (table->id == EF4_FARCH_FILTER_TABLE_RX_DEF) {
		/* One filter spec per type */
		BUILD_BUG_ON(EF4_FARCH_FILTER_INDEX_UC_DEF != 0);
		BUILD_BUG_ON(EF4_FARCH_FILTER_INDEX_MC_DEF !=
			     EF4_FARCH_FILTER_MC_DEF - EF4_FARCH_FILTER_UC_DEF);
		rep_index = spec.type - EF4_FARCH_FILTER_UC_DEF;
		ins_index = rep_index;

		spin_lock_bh(&efx->filter_lock);
	} else {
		/* Search concurrently for
		 * (1) a filter to be replaced (rep_index): any filter
		 *     with the same match values, up to the current
		 *     search depth for this type, and
		 * (2) the insertion point (ins_index): (1) or any
		 *     free slot before it or up to the maximum search
		 *     depth for this priority
		 * We fail if we cannot find (2).
		 *
		 * We can stop once either
		 * (a) we find (1), in which case we have definitely
		 *     found (2) as well; or
		 * (b) we have searched exhaustively for (1), and have
		 *     either found (2) or searched exhaustively for it
		 */
		u32 key = ef4_farch_filter_build(&filter, &spec);
		unsigned int hash = ef4_farch_filter_hash(key);
		unsigned int incr = ef4_farch_filter_increment(key);
		unsigned int max_rep_depth = table->search_limit[spec.type];
		unsigned int max_ins_depth =
			spec.priority <= EF4_FILTER_PRI_HINT ?
			EF4_FARCH_FILTER_CTL_SRCH_HINT_MAX :
			EF4_FARCH_FILTER_CTL_SRCH_MAX;
		unsigned int i = hash & (table->size - 1);

		ins_index = -1;
		depth = 1;

		spin_lock_bh(&efx->filter_lock);

		for (;;) {
			if (!test_bit(i, table->used_bitmap)) {
				if (ins_index < 0)
					ins_index = i;
			} else if (ef4_farch_filter_equal(&spec,
							  &table->spec[i])) {
				/* Case (a) */
				if (ins_index < 0)
					ins_index = i;
				rep_index = i;
				break;
			}

			if (depth >= max_rep_depth &&
			    (ins_index >= 0 || depth >= max_ins_depth)) {
				/* Case (b) */
				if (ins_index < 0) {
					rc = -EBUSY;
					goto out;
				}
				rep_index = -1;
				break;
			}

			i = (i + incr) & (table->size - 1);
			++depth;
		}
	}

	/* If we found a filter to be replaced, check whether we
	 * should do so
	 */
	if (rep_index >= 0) {
		struct ef4_farch_filter_spec *saved_spec =
			&table->spec[rep_index];

		if (spec.priority == saved_spec->priority && !replace_equal) {
			rc = -EEXIST;
			goto out;
		}
		if (spec.priority < saved_spec->priority) {
			rc = -EPERM;
			goto out;
		}
		if (saved_spec->priority == EF4_FILTER_PRI_AUTO ||
		    saved_spec->flags & EF4_FILTER_FLAG_RX_OVER_AUTO)
			spec.flags |= EF4_FILTER_FLAG_RX_OVER_AUTO;
	}

	/* Insert the filter */
	if (ins_index != rep_index) {
		__set_bit(ins_index, table->used_bitmap);
		++table->used;
	}
	table->spec[ins_index] = spec;

	if (table->id == EF4_FARCH_FILTER_TABLE_RX_DEF) {
		ef4_farch_filter_push_rx_config(efx);
	} else {
		if (table->search_limit[spec.type] < depth) {
			table->search_limit[spec.type] = depth;
			if (spec.flags & EF4_FILTER_FLAG_TX)
				ef4_farch_filter_push_tx_limits(efx);
			else
				ef4_farch_filter_push_rx_config(efx);
		}

		ef4_writeo(efx, &filter,
			   table->offset + table->step * ins_index);

		/* If we were able to replace a filter by inserting
		 * at a lower depth, clear the replaced filter
		 */
		if (ins_index != rep_index && rep_index >= 0)
			ef4_farch_filter_table_clear_entry(efx, table,
							   rep_index);
	}

	netif_vdbg(efx, hw, efx->net_dev,
		   "%s: filter type %d index %d rxq %u set",
		   __func__, spec.type, ins_index, spec.dmaq_id);
	rc = ef4_farch_filter_make_id(&spec, ins_index);

out:
	spin_unlock_bh(&efx->filter_lock);
	return rc;
}

static void
ef4_farch_filter_table_clear_entry(struct ef4_nic *efx,
				   struct ef4_farch_filter_table *table,
				   unsigned int filter_idx)
{
	static ef4_oword_t filter;

	EF4_WARN_ON_PARANOID(!test_bit(filter_idx, table->used_bitmap));
	BUG_ON(table->offset == 0); /* can't clear MAC default filters */

	__clear_bit(filter_idx, table->used_bitmap);
	--table->used;
	memset(&table->spec[filter_idx], 0, sizeof(table->spec[0]));

	ef4_writeo(efx, &filter, table->offset + table->step * filter_idx);

	/* If this filter required a greater search depth than
	 * any other, the search limit for its type can now be
	 * decreased.  However, it is hard to determine that
	 * unless the table has become completely empty - in
	 * which case, all its search limits can be set to 0.
	 */
	if (unlikely(table->used == 0)) {
		memset(table->search_limit, 0, sizeof(table->search_limit));
		if (table->id == EF4_FARCH_FILTER_TABLE_TX_MAC)
			ef4_farch_filter_push_tx_limits(efx);
		else
			ef4_farch_filter_push_rx_config(efx);
	}
}

static int ef4_farch_filter_remove(struct ef4_nic *efx,
				   struct ef4_farch_filter_table *table,
				   unsigned int filter_idx,
				   enum ef4_filter_priority priority)
{
	struct ef4_farch_filter_spec *spec = &table->spec[filter_idx];

	if (!test_bit(filter_idx, table->used_bitmap) ||
	    spec->priority != priority)
		return -ENOENT;

	if (spec->flags & EF4_FILTER_FLAG_RX_OVER_AUTO) {
		ef4_farch_filter_init_rx_auto(efx, spec);
		ef4_farch_filter_push_rx_config(efx);
	} else {
		ef4_farch_filter_table_clear_entry(efx, table, filter_idx);
	}

	return 0;
}

int ef4_farch_filter_remove_safe(struct ef4_nic *efx,
				 enum ef4_filter_priority priority,
				 u32 filter_id)
{
	struct ef4_farch_filter_state *state = efx->filter_state;
	enum ef4_farch_filter_table_id table_id;
	struct ef4_farch_filter_table *table;
	unsigned int filter_idx;
	struct ef4_farch_filter_spec *spec;
	int rc;

	table_id = ef4_farch_filter_id_table_id(filter_id);
	if ((unsigned int)table_id >= EF4_FARCH_FILTER_TABLE_COUNT)
		return -ENOENT;
	table = &state->table[table_id];

	filter_idx = ef4_farch_filter_id_index(filter_id);
	if (filter_idx >= table->size)
		return -ENOENT;
	spec = &table->spec[filter_idx];

	spin_lock_bh(&efx->filter_lock);
	rc = ef4_farch_filter_remove(efx, table, filter_idx, priority);
	spin_unlock_bh(&efx->filter_lock);

	return rc;
}

int ef4_farch_filter_get_safe(struct ef4_nic *efx,
			      enum ef4_filter_priority priority,
			      u32 filter_id, struct ef4_filter_spec *spec_buf)
{
	struct ef4_farch_filter_state *state = efx->filter_state;
	enum ef4_farch_filter_table_id table_id;
	struct ef4_farch_filter_table *table;
	struct ef4_farch_filter_spec *spec;
	unsigned int filter_idx;
	int rc;

	table_id = ef4_farch_filter_id_table_id(filter_id);
	if ((unsigned int)table_id >= EF4_FARCH_FILTER_TABLE_COUNT)
		return -ENOENT;
	table = &state->table[table_id];

	filter_idx = ef4_farch_filter_id_index(filter_id);
	if (filter_idx >= table->size)
		return -ENOENT;
	spec = &table->spec[filter_idx];

	spin_lock_bh(&efx->filter_lock);

	if (test_bit(filter_idx, table->used_bitmap) &&
	    spec->priority == priority) {
		ef4_farch_filter_to_gen_spec(spec_buf, spec);
		rc = 0;
	} else {
		rc = -ENOENT;
	}

	spin_unlock_bh(&efx->filter_lock);

	return rc;
}

static void
ef4_farch_filter_table_clear(struct ef4_nic *efx,
			     enum ef4_farch_filter_table_id table_id,
			     enum ef4_filter_priority priority)
{
	struct ef4_farch_filter_state *state = efx->filter_state;
	struct ef4_farch_filter_table *table = &state->table[table_id];
	unsigned int filter_idx;

	spin_lock_bh(&efx->filter_lock);
	for (filter_idx = 0; filter_idx < table->size; ++filter_idx) {
		if (table->spec[filter_idx].priority != EF4_FILTER_PRI_AUTO)
			ef4_farch_filter_remove(efx, table,
						filter_idx, priority);
	}
	spin_unlock_bh(&efx->filter_lock);
}

int ef4_farch_filter_clear_rx(struct ef4_nic *efx,
			       enum ef4_filter_priority priority)
{
	ef4_farch_filter_table_clear(efx, EF4_FARCH_FILTER_TABLE_RX_IP,
				     priority);
	ef4_farch_filter_table_clear(efx, EF4_FARCH_FILTER_TABLE_RX_MAC,
				     priority);
	ef4_farch_filter_table_clear(efx, EF4_FARCH_FILTER_TABLE_RX_DEF,
				     priority);
	return 0;
}

u32 ef4_farch_filter_count_rx_used(struct ef4_nic *efx,
				   enum ef4_filter_priority priority)
{
	struct ef4_farch_filter_state *state = efx->filter_state;
	enum ef4_farch_filter_table_id table_id;
	struct ef4_farch_filter_table *table;
	unsigned int filter_idx;
	u32 count = 0;

	spin_lock_bh(&efx->filter_lock);

	for (table_id = EF4_FARCH_FILTER_TABLE_RX_IP;
	     table_id <= EF4_FARCH_FILTER_TABLE_RX_DEF;
	     table_id++) {
		table = &state->table[table_id];
		for (filter_idx = 0; filter_idx < table->size; filter_idx++) {
			if (test_bit(filter_idx, table->used_bitmap) &&
			    table->spec[filter_idx].priority == priority)
				++count;
		}
	}

	spin_unlock_bh(&efx->filter_lock);

	return count;
}

s32 ef4_farch_filter_get_rx_ids(struct ef4_nic *efx,
				enum ef4_filter_priority priority,
				u32 *buf, u32 size)
{
	struct ef4_farch_filter_state *state = efx->filter_state;
	enum ef4_farch_filter_table_id table_id;
	struct ef4_farch_filter_table *table;
	unsigned int filter_idx;
	s32 count = 0;

	spin_lock_bh(&efx->filter_lock);

	for (table_id = EF4_FARCH_FILTER_TABLE_RX_IP;
	     table_id <= EF4_FARCH_FILTER_TABLE_RX_DEF;
	     table_id++) {
		table = &state->table[table_id];
		for (filter_idx = 0; filter_idx < table->size; filter_idx++) {
			if (test_bit(filter_idx, table->used_bitmap) &&
			    table->spec[filter_idx].priority == priority) {
				if (count == size) {
					count = -EMSGSIZE;
					goto out;
				}
				buf[count++] = ef4_farch_filter_make_id(
					&table->spec[filter_idx], filter_idx);
			}
		}
	}
out:
	spin_unlock_bh(&efx->filter_lock);

	return count;
}

/* Restore filter stater after reset */
void ef4_farch_filter_table_restore(struct ef4_nic *efx)
{
	struct ef4_farch_filter_state *state = efx->filter_state;
	enum ef4_farch_filter_table_id table_id;
	struct ef4_farch_filter_table *table;
	ef4_oword_t filter;
	unsigned int filter_idx;

	spin_lock_bh(&efx->filter_lock);

	for (table_id = 0; table_id < EF4_FARCH_FILTER_TABLE_COUNT; table_id++) {
		table = &state->table[table_id];

		/* Check whether this is a regular register table */
		if (table->step == 0)
			continue;

		for (filter_idx = 0; filter_idx < table->size; filter_idx++) {
			if (!test_bit(filter_idx, table->used_bitmap))
				continue;
			ef4_farch_filter_build(&filter, &table->spec[filter_idx]);
			ef4_writeo(efx, &filter,
				   table->offset + table->step * filter_idx);
		}
	}

	ef4_farch_filter_push_rx_config(efx);
	ef4_farch_filter_push_tx_limits(efx);

	spin_unlock_bh(&efx->filter_lock);
}

void ef4_farch_filter_table_remove(struct ef4_nic *efx)
{
	struct ef4_farch_filter_state *state = efx->filter_state;
	enum ef4_farch_filter_table_id table_id;

	for (table_id = 0; table_id < EF4_FARCH_FILTER_TABLE_COUNT; table_id++) {
		kfree(state->table[table_id].used_bitmap);
		vfree(state->table[table_id].spec);
	}
	kfree(state);
}

int ef4_farch_filter_table_probe(struct ef4_nic *efx)
{
	struct ef4_farch_filter_state *state;
	struct ef4_farch_filter_table *table;
	unsigned table_id;

	state = kzalloc(sizeof(struct ef4_farch_filter_state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;
	efx->filter_state = state;

	if (ef4_nic_rev(efx) >= EF4_REV_FALCON_B0) {
		table = &state->table[EF4_FARCH_FILTER_TABLE_RX_IP];
		table->id = EF4_FARCH_FILTER_TABLE_RX_IP;
		table->offset = FR_BZ_RX_FILTER_TBL0;
		table->size = FR_BZ_RX_FILTER_TBL0_ROWS;
		table->step = FR_BZ_RX_FILTER_TBL0_STEP;
	}

	for (table_id = 0; table_id < EF4_FARCH_FILTER_TABLE_COUNT; table_id++) {
		table = &state->table[table_id];
		if (table->size == 0)
			continue;
		table->used_bitmap = kcalloc(BITS_TO_LONGS(table->size),
					     sizeof(unsigned long),
					     GFP_KERNEL);
		if (!table->used_bitmap)
			goto fail;
		table->spec = vzalloc(table->size * sizeof(*table->spec));
		if (!table->spec)
			goto fail;
	}

	table = &state->table[EF4_FARCH_FILTER_TABLE_RX_DEF];
	if (table->size) {
		/* RX default filters must always exist */
		struct ef4_farch_filter_spec *spec;
		unsigned i;

		for (i = 0; i < EF4_FARCH_FILTER_SIZE_RX_DEF; i++) {
			spec = &table->spec[i];
			spec->type = EF4_FARCH_FILTER_UC_DEF + i;
			ef4_farch_filter_init_rx_auto(efx, spec);
			__set_bit(i, table->used_bitmap);
		}
	}

	ef4_farch_filter_push_rx_config(efx);

	return 0;

fail:
	ef4_farch_filter_table_remove(efx);
	return -ENOMEM;
}

/* Update scatter enable flags for filters pointing to our own RX queues */
void ef4_farch_filter_update_rx_scatter(struct ef4_nic *efx)
{
	struct ef4_farch_filter_state *state = efx->filter_state;
	enum ef4_farch_filter_table_id table_id;
	struct ef4_farch_filter_table *table;
	ef4_oword_t filter;
	unsigned int filter_idx;

	spin_lock_bh(&efx->filter_lock);

	for (table_id = EF4_FARCH_FILTER_TABLE_RX_IP;
	     table_id <= EF4_FARCH_FILTER_TABLE_RX_DEF;
	     table_id++) {
		table = &state->table[table_id];

		for (filter_idx = 0; filter_idx < table->size; filter_idx++) {
			if (!test_bit(filter_idx, table->used_bitmap) ||
			    table->spec[filter_idx].dmaq_id >=
			    efx->n_rx_channels)
				continue;

			if (efx->rx_scatter)
				table->spec[filter_idx].flags |=
					EF4_FILTER_FLAG_RX_SCATTER;
			else
				table->spec[filter_idx].flags &=
					~EF4_FILTER_FLAG_RX_SCATTER;

			if (table_id == EF4_FARCH_FILTER_TABLE_RX_DEF)
				/* Pushed by ef4_farch_filter_push_rx_config() */
				continue;

			ef4_farch_filter_build(&filter, &table->spec[filter_idx]);
			ef4_writeo(efx, &filter,
				   table->offset + table->step * filter_idx);
		}
	}

	ef4_farch_filter_push_rx_config(efx);

	spin_unlock_bh(&efx->filter_lock);
}

#ifdef CONFIG_RFS_ACCEL

s32 ef4_farch_filter_rfs_insert(struct ef4_nic *efx,
				struct ef4_filter_spec *gen_spec)
{
	return ef4_farch_filter_insert(efx, gen_spec, true);
}

bool ef4_farch_filter_rfs_expire_one(struct ef4_nic *efx, u32 flow_id,
				     unsigned int index)
{
	struct ef4_farch_filter_state *state = efx->filter_state;
	struct ef4_farch_filter_table *table =
		&state->table[EF4_FARCH_FILTER_TABLE_RX_IP];

	if (test_bit(index, table->used_bitmap) &&
	    table->spec[index].priority == EF4_FILTER_PRI_HINT &&
	    rps_may_expire_flow(efx->net_dev, table->spec[index].dmaq_id,
				flow_id, index)) {
		ef4_farch_filter_table_clear_entry(efx, table, index);
		return true;
	}

	return false;
}

#endif /* CONFIG_RFS_ACCEL */

void ef4_farch_filter_sync_rx_mode(struct ef4_nic *efx)
{
	struct net_device *net_dev = efx->net_dev;
	struct netdev_hw_addr *ha;
	union ef4_multicast_hash *mc_hash = &efx->multicast_hash;
	u32 crc;
	int bit;

	if (!ef4_dev_registered(efx))
		return;

	netif_addr_lock_bh(net_dev);

	efx->unicast_filter = !(net_dev->flags & IFF_PROMISC);

	/* Build multicast hash table */
	if (net_dev->flags & (IFF_PROMISC | IFF_ALLMULTI)) {
		memset(mc_hash, 0xff, sizeof(*mc_hash));
	} else {
		memset(mc_hash, 0x00, sizeof(*mc_hash));
		netdev_for_each_mc_addr(ha, net_dev) {
			crc = ether_crc_le(ETH_ALEN, ha->addr);
			bit = crc & (EF4_MCAST_HASH_ENTRIES - 1);
			__set_bit_le(bit, mc_hash);
		}

		/* Broadcast packets go through the multicast hash filter.
		 * ether_crc_le() of the broadcast address is 0xbe2612ff
		 * so we always add bit 0xff to the mask.
		 */
		__set_bit_le(0xff, mc_hash);
	}

	netif_addr_unlock_bh(net_dev);
}
