/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2006-2011 Solarflare Communications Inc.
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

/* If EFX_MAX_INT_ERRORS internal errors occur within
 * EFX_INT_ERROR_EXPIRE seconds, we consider the NIC broken and
 * disable it.
 */
#define EFX_INT_ERROR_EXPIRE 3600
#define EFX_MAX_INT_ERRORS 5

/* Depth of RX flush request fifo */
#define EFX_RX_FLUSH_COUNT 4

/* Driver generated events */
#define _EFX_CHANNEL_MAGIC_TEST		0x000101
#define _EFX_CHANNEL_MAGIC_FILL		0x000102
#define _EFX_CHANNEL_MAGIC_RX_DRAIN	0x000103
#define _EFX_CHANNEL_MAGIC_TX_DRAIN	0x000104

#define _EFX_CHANNEL_MAGIC(_code, _data)	((_code) << 8 | (_data))
#define _EFX_CHANNEL_MAGIC_CODE(_magic)		((_magic) >> 8)

#define EFX_CHANNEL_MAGIC_TEST(_channel)				\
	_EFX_CHANNEL_MAGIC(_EFX_CHANNEL_MAGIC_TEST, (_channel)->channel)
#define EFX_CHANNEL_MAGIC_FILL(_rx_queue)				\
	_EFX_CHANNEL_MAGIC(_EFX_CHANNEL_MAGIC_FILL,			\
			   efx_rx_queue_index(_rx_queue))
#define EFX_CHANNEL_MAGIC_RX_DRAIN(_rx_queue)				\
	_EFX_CHANNEL_MAGIC(_EFX_CHANNEL_MAGIC_RX_DRAIN,			\
			   efx_rx_queue_index(_rx_queue))
#define EFX_CHANNEL_MAGIC_TX_DRAIN(_tx_queue)				\
	_EFX_CHANNEL_MAGIC(_EFX_CHANNEL_MAGIC_TX_DRAIN,			\
			   (_tx_queue)->queue)

static void efx_magic_event(struct efx_channel *channel, u32 magic);

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
	return ((efx_qword_t *) (channel->eventq.addr)) +
		(index & channel->eventq_mask);
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
	return !(EFX_DWORD_IS_ALL_ONES(event->dword[0]) |
		  EFX_DWORD_IS_ALL_ONES(event->dword[1]));
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
	netif_err(efx, hw, efx->net_dev,
		  "wrote "EFX_OWORD_FMT" read "EFX_OWORD_FMT
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
	unsigned int index;
	dma_addr_t dma_addr;
	int i;

	EFX_BUG_ON_PARANOID(!buffer->addr);

	/* Write buffer descriptors to NIC */
	for (i = 0; i < buffer->entries; i++) {
		index = buffer->index + i;
		dma_addr = buffer->dma_addr + (i * EFX_BUF_SIZE);
		netif_dbg(efx, probe, efx->net_dev,
			  "mapping special buffer %d at %llx\n",
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

	netif_dbg(efx, hw, efx->net_dev, "unmapping special buffers %d-%d\n",
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

	buffer->addr = dma_alloc_coherent(&efx->pci_dev->dev, len,
					  &buffer->dma_addr, GFP_KERNEL);
	if (!buffer->addr)
		return -ENOMEM;
	buffer->len = len;
	buffer->entries = len / EFX_BUF_SIZE;
	BUG_ON(buffer->dma_addr & (EFX_BUF_SIZE - 1));

	/* Select new buffer ID */
	buffer->index = efx->next_buffer_table;
	efx->next_buffer_table += buffer->entries;
#ifdef CONFIG_SFC_SRIOV
	BUG_ON(efx_sriov_enabled(efx) &&
	       efx->vf_buftbl_base < efx->next_buffer_table);
#endif

	netif_dbg(efx, probe, efx->net_dev,
		  "allocating special buffers %d-%d at %llx+%x "
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

	netif_dbg(efx, hw, efx->net_dev,
		  "deallocating special buffers %d-%d at %llx+%x "
		  "(virt %p phys %llx)\n", buffer->index,
		  buffer->index + buffer->entries - 1,
		  (u64)buffer->dma_addr, buffer->len,
		  buffer->addr, (u64)virt_to_phys(buffer->addr));

	dma_free_coherent(&efx->pci_dev->dev, buffer->len, buffer->addr,
			  buffer->dma_addr);
	buffer->addr = NULL;
	buffer->entries = 0;
}

/**************************************************************************
 *
 * Generic buffer handling
 * These buffers are used for interrupt status, MAC stats, etc.
 *
 **************************************************************************/

int efx_nic_alloc_buffer(struct efx_nic *efx, struct efx_buffer *buffer,
			 unsigned int len)
{
	buffer->addr = dma_alloc_coherent(&efx->pci_dev->dev, len,
					  &buffer->dma_addr, GFP_ATOMIC);
	if (!buffer->addr)
		return -ENOMEM;
	buffer->len = len;
	memset(buffer->addr, 0, len);
	return 0;
}

void efx_nic_free_buffer(struct efx_nic *efx, struct efx_buffer *buffer)
{
	if (buffer->addr) {
		dma_free_coherent(&efx->pci_dev->dev, buffer->len,
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
	return ((efx_qword_t *) (tx_queue->txd.addr)) + index;
}

/* This writes to the TX_DESC_WPTR; write pointer for TX descriptor ring */
static inline void efx_notify_tx_desc(struct efx_tx_queue *tx_queue)
{
	unsigned write_ptr;
	efx_dword_t reg;

	write_ptr = tx_queue->write_count & tx_queue->ptr_mask;
	EFX_POPULATE_DWORD_1(reg, FRF_AZ_TX_DESC_WPTR_DWORD, write_ptr);
	efx_writed_page(tx_queue->efx, &reg,
			FR_AZ_TX_DESC_UPD_DWORD_P0, tx_queue->queue);
}

/* Write pointer and first descriptor for TX descriptor ring */
static inline void efx_push_tx_desc(struct efx_tx_queue *tx_queue,
				    const efx_qword_t *txd)
{
	unsigned write_ptr;
	efx_oword_t reg;

	BUILD_BUG_ON(FRF_AZ_TX_DESC_LBN != 0);
	BUILD_BUG_ON(FR_AA_TX_DESC_UPD_KER != FR_BZ_TX_DESC_UPD_P0);

	write_ptr = tx_queue->write_count & tx_queue->ptr_mask;
	EFX_POPULATE_OWORD_2(reg, FRF_AZ_TX_DESC_PUSH_CMD, true,
			     FRF_AZ_TX_DESC_WPTR, write_ptr);
	reg.qword[0] = *txd;
	efx_writeo_page(tx_queue->efx, &reg,
			FR_BZ_TX_DESC_UPD_P0, tx_queue->queue);
}

static inline bool
efx_may_push_tx_desc(struct efx_tx_queue *tx_queue, unsigned int write_count)
{
	unsigned empty_read_count = ACCESS_ONCE(tx_queue->empty_read_count);

	if (empty_read_count == 0)
		return false;

	tx_queue->empty_read_count = 0;
	return ((empty_read_count ^ write_count) & ~EFX_EMPTY_COUNT_VALID) == 0;
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
	unsigned old_write_count = tx_queue->write_count;

	BUG_ON(tx_queue->write_count == tx_queue->insert_count);

	do {
		write_ptr = tx_queue->write_count & tx_queue->ptr_mask;
		buffer = &tx_queue->buffer[write_ptr];
		txd = efx_tx_desc(tx_queue, write_ptr);
		++tx_queue->write_count;

		/* Create TX descriptor ring entry */
		BUILD_BUG_ON(EFX_TX_BUF_CONT != 1);
		EFX_POPULATE_QWORD_4(*txd,
				     FSF_AZ_TX_KER_CONT,
				     buffer->flags & EFX_TX_BUF_CONT,
				     FSF_AZ_TX_KER_BYTE_COUNT, buffer->len,
				     FSF_AZ_TX_KER_BUF_REGION, 0,
				     FSF_AZ_TX_KER_BUF_ADDR, buffer->dma_addr);
	} while (tx_queue->write_count != tx_queue->insert_count);

	wmb(); /* Ensure descriptors are written before they are fetched */

	if (efx_may_push_tx_desc(tx_queue, old_write_count)) {
		txd = efx_tx_desc(tx_queue,
				  old_write_count & tx_queue->ptr_mask);
		efx_push_tx_desc(tx_queue, txd);
		++tx_queue->pushes;
	} else {
		efx_notify_tx_desc(tx_queue);
	}
}

/* Allocate hardware resources for a TX queue */
int efx_nic_probe_tx(struct efx_tx_queue *tx_queue)
{
	struct efx_nic *efx = tx_queue->efx;
	unsigned entries;

	entries = tx_queue->ptr_mask + 1;
	return efx_alloc_special_buffer(efx, &tx_queue->txd,
					entries * sizeof(efx_qword_t));
}

void efx_nic_init_tx(struct efx_tx_queue *tx_queue)
{
	struct efx_nic *efx = tx_queue->efx;
	efx_oword_t reg;

	/* Pin TX descriptor ring */
	efx_init_special_buffer(efx, &tx_queue->txd);

	/* Push TX descriptor ring to card */
	EFX_POPULATE_OWORD_10(reg,
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
		int csum = tx_queue->queue & EFX_TXQ_TYPE_OFFLOAD;
		EFX_SET_OWORD_FIELD(reg, FRF_BZ_TX_IP_CHKSM_DIS, !csum);
		EFX_SET_OWORD_FIELD(reg, FRF_BZ_TX_TCP_CHKSM_DIS,
				    !csum);
	}

	efx_writeo_table(efx, &reg, efx->type->txd_ptr_tbl_base,
			 tx_queue->queue);

	if (efx_nic_rev(efx) < EFX_REV_FALCON_B0) {
		/* Only 128 bits in this register */
		BUILD_BUG_ON(EFX_MAX_TX_QUEUES > 128);

		efx_reado(efx, &reg, FR_AA_TX_CHKSM_CFG);
		if (tx_queue->queue & EFX_TXQ_TYPE_OFFLOAD)
			__clear_bit_le(tx_queue->queue, &reg);
		else
			__set_bit_le(tx_queue->queue, &reg);
		efx_writeo(efx, &reg, FR_AA_TX_CHKSM_CFG);
	}

	if (efx_nic_rev(efx) >= EFX_REV_FALCON_B0) {
		EFX_POPULATE_OWORD_1(reg,
				     FRF_BZ_TX_PACE,
				     (tx_queue->queue & EFX_TXQ_TYPE_HIGHPRI) ?
				     FFE_BZ_TX_PACE_OFF :
				     FFE_BZ_TX_PACE_RESERVED);
		efx_writeo_table(efx, &reg, FR_BZ_TX_PACE_TBL,
				 tx_queue->queue);
	}
}

static void efx_flush_tx_queue(struct efx_tx_queue *tx_queue)
{
	struct efx_nic *efx = tx_queue->efx;
	efx_oword_t tx_flush_descq;

	WARN_ON(atomic_read(&tx_queue->flush_outstanding));
	atomic_set(&tx_queue->flush_outstanding, 1);

	EFX_POPULATE_OWORD_2(tx_flush_descq,
			     FRF_AZ_TX_FLUSH_DESCQ_CMD, 1,
			     FRF_AZ_TX_FLUSH_DESCQ, tx_queue->queue);
	efx_writeo(efx, &tx_flush_descq, FR_AZ_TX_FLUSH_DESCQ);
}

void efx_nic_fini_tx(struct efx_tx_queue *tx_queue)
{
	struct efx_nic *efx = tx_queue->efx;
	efx_oword_t tx_desc_ptr;

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
	return ((efx_qword_t *) (rx_queue->rxd.addr)) + index;
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
	struct efx_nic *efx = rx_queue->efx;
	efx_dword_t reg;
	unsigned write_ptr;

	while (rx_queue->notified_count != rx_queue->added_count) {
		efx_build_rx_desc(
			rx_queue,
			rx_queue->notified_count & rx_queue->ptr_mask);
		++rx_queue->notified_count;
	}

	wmb();
	write_ptr = rx_queue->added_count & rx_queue->ptr_mask;
	EFX_POPULATE_DWORD_1(reg, FRF_AZ_RX_DESC_WPTR_DWORD, write_ptr);
	efx_writed_page(efx, &reg, FR_AZ_RX_DESC_UPD_DWORD_P0,
			efx_rx_queue_index(rx_queue));
}

int efx_nic_probe_rx(struct efx_rx_queue *rx_queue)
{
	struct efx_nic *efx = rx_queue->efx;
	unsigned entries;

	entries = rx_queue->ptr_mask + 1;
	return efx_alloc_special_buffer(efx, &rx_queue->rxd,
					entries * sizeof(efx_qword_t));
}

void efx_nic_init_rx(struct efx_rx_queue *rx_queue)
{
	efx_oword_t rx_desc_ptr;
	struct efx_nic *efx = rx_queue->efx;
	bool is_b0 = efx_nic_rev(efx) >= EFX_REV_FALCON_B0;
	bool iscsi_digest_en = is_b0;

	netif_dbg(efx, hw, efx->net_dev,
		  "RX queue %d ring in special buffers %d-%d\n",
		  efx_rx_queue_index(rx_queue), rx_queue->rxd.index,
		  rx_queue->rxd.index + rx_queue->rxd.entries - 1);

	/* Pin RX descriptor ring */
	efx_init_special_buffer(efx, &rx_queue->rxd);

	/* Push RX descriptor ring to card */
	EFX_POPULATE_OWORD_10(rx_desc_ptr,
			      FRF_AZ_RX_ISCSI_DDIG_EN, iscsi_digest_en,
			      FRF_AZ_RX_ISCSI_HDIG_EN, iscsi_digest_en,
			      FRF_AZ_RX_DESCQ_BUF_BASE_ID, rx_queue->rxd.index,
			      FRF_AZ_RX_DESCQ_EVQ_ID,
			      efx_rx_queue_channel(rx_queue)->channel,
			      FRF_AZ_RX_DESCQ_OWNER_ID, 0,
			      FRF_AZ_RX_DESCQ_LABEL,
			      efx_rx_queue_index(rx_queue),
			      FRF_AZ_RX_DESCQ_SIZE,
			      __ffs(rx_queue->rxd.entries),
			      FRF_AZ_RX_DESCQ_TYPE, 0 /* kernel queue */ ,
			      /* For >=B0 this is scatter so disable */
			      FRF_AZ_RX_DESCQ_JUMBO, !is_b0,
			      FRF_AZ_RX_DESCQ_EN, 1);
	efx_writeo_table(efx, &rx_desc_ptr, efx->type->rxd_ptr_tbl_base,
			 efx_rx_queue_index(rx_queue));
}

static void efx_flush_rx_queue(struct efx_rx_queue *rx_queue)
{
	struct efx_nic *efx = rx_queue->efx;
	efx_oword_t rx_flush_descq;

	EFX_POPULATE_OWORD_2(rx_flush_descq,
			     FRF_AZ_RX_FLUSH_DESCQ_CMD, 1,
			     FRF_AZ_RX_FLUSH_DESCQ,
			     efx_rx_queue_index(rx_queue));
	efx_writeo(efx, &rx_flush_descq, FR_AZ_RX_FLUSH_DESCQ);
}

void efx_nic_fini_rx(struct efx_rx_queue *rx_queue)
{
	efx_oword_t rx_desc_ptr;
	struct efx_nic *efx = rx_queue->efx;

	/* Remove RX descriptor ring from card */
	EFX_ZERO_OWORD(rx_desc_ptr);
	efx_writeo_table(efx, &rx_desc_ptr, efx->type->rxd_ptr_tbl_base,
			 efx_rx_queue_index(rx_queue));

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
 * Flush handling
 *
 **************************************************************************/

/* efx_nic_flush_queues() must be woken up when all flushes are completed,
 * or more RX flushes can be kicked off.
 */
static bool efx_flush_wake(struct efx_nic *efx)
{
	/* Ensure that all updates are visible to efx_nic_flush_queues() */
	smp_mb();

	return (atomic_read(&efx->drain_pending) == 0 ||
		(atomic_read(&efx->rxq_flush_outstanding) < EFX_RX_FLUSH_COUNT
		 && atomic_read(&efx->rxq_flush_pending) > 0));
}

static bool efx_check_tx_flush_complete(struct efx_nic *efx)
{
	bool i = true;
	efx_oword_t txd_ptr_tbl;
	struct efx_channel *channel;
	struct efx_tx_queue *tx_queue;

	efx_for_each_channel(channel, efx) {
		efx_for_each_channel_tx_queue(tx_queue, channel) {
			efx_reado_table(efx, &txd_ptr_tbl,
					FR_BZ_TX_DESC_PTR_TBL, tx_queue->queue);
			if (EFX_OWORD_FIELD(txd_ptr_tbl,
					    FRF_AZ_TX_DESCQ_FLUSH) ||
			    EFX_OWORD_FIELD(txd_ptr_tbl,
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
				/* Don't need to increment drain_pending as it
				 * has already been incremented for the queues
				 * which did not drain
				 */
				efx_magic_event(channel,
						EFX_CHANNEL_MAGIC_TX_DRAIN(
							tx_queue));
			}
		}
	}

	return i;
}

/* Flush all the transmit queues, and continue flushing receive queues until
 * they're all flushed. Wait for the DRAIN events to be recieved so that there
 * are no more RX and TX events left on any channel. */
int efx_nic_flush_queues(struct efx_nic *efx)
{
	unsigned timeout = msecs_to_jiffies(5000); /* 5s for all flushes and drains */
	struct efx_channel *channel;
	struct efx_rx_queue *rx_queue;
	struct efx_tx_queue *tx_queue;
	int rc = 0;

	efx->type->prepare_flush(efx);

	efx_for_each_channel(channel, efx) {
		efx_for_each_channel_tx_queue(tx_queue, channel) {
			atomic_inc(&efx->drain_pending);
			efx_flush_tx_queue(tx_queue);
		}
		efx_for_each_channel_rx_queue(rx_queue, channel) {
			atomic_inc(&efx->drain_pending);
			rx_queue->flush_pending = true;
			atomic_inc(&efx->rxq_flush_pending);
		}
	}

	while (timeout && atomic_read(&efx->drain_pending) > 0) {
		/* If SRIOV is enabled, then offload receive queue flushing to
		 * the firmware (though we will still have to poll for
		 * completion). If that fails, fall back to the old scheme.
		 */
		if (efx_sriov_enabled(efx)) {
			rc = efx_mcdi_flush_rxqs(efx);
			if (!rc)
				goto wait;
		}

		/* The hardware supports four concurrent rx flushes, each of
		 * which may need to be retried if there is an outstanding
		 * descriptor fetch
		 */
		efx_for_each_channel(channel, efx) {
			efx_for_each_channel_rx_queue(rx_queue, channel) {
				if (atomic_read(&efx->rxq_flush_outstanding) >=
				    EFX_RX_FLUSH_COUNT)
					break;

				if (rx_queue->flush_pending) {
					rx_queue->flush_pending = false;
					atomic_dec(&efx->rxq_flush_pending);
					atomic_inc(&efx->rxq_flush_outstanding);
					efx_flush_rx_queue(rx_queue);
				}
			}
		}

	wait:
		timeout = wait_event_timeout(efx->flush_wq, efx_flush_wake(efx),
					     timeout);
	}

	if (atomic_read(&efx->drain_pending) &&
	    !efx_check_tx_flush_complete(efx)) {
		netif_err(efx, hw, efx->net_dev, "failed to flush %d queues "
			  "(rx %d+%d)\n", atomic_read(&efx->drain_pending),
			  atomic_read(&efx->rxq_flush_outstanding),
			  atomic_read(&efx->rxq_flush_pending));
		rc = -ETIMEDOUT;

		atomic_set(&efx->drain_pending, 0);
		atomic_set(&efx->rxq_flush_pending, 0);
		atomic_set(&efx->rxq_flush_outstanding, 0);
	}

	efx->type->finish_flush(efx);

	return rc;
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

	EFX_POPULATE_DWORD_1(reg, FRF_AZ_EVQ_RPTR,
			     channel->eventq_read_ptr & channel->eventq_mask);

	/* For Falcon A1, EVQ_RPTR_KER is documented as having a step size
	 * of 4 bytes, but it is really 16 bytes just like later revisions.
	 */
	efx_writed(efx, &reg,
		   efx->type->evq_rptr_tbl_base +
		   FR_BZ_EVQ_RPTR_STEP * channel->channel);
}

/* Use HW to insert a SW defined event */
void efx_generate_event(struct efx_nic *efx, unsigned int evq,
			efx_qword_t *event)
{
	efx_oword_t drv_ev_reg;

	BUILD_BUG_ON(FRF_AZ_DRV_EV_DATA_LBN != 0 ||
		     FRF_AZ_DRV_EV_DATA_WIDTH != 64);
	drv_ev_reg.u32[0] = event->u32[0];
	drv_ev_reg.u32[1] = event->u32[1];
	drv_ev_reg.u32[2] = 0;
	drv_ev_reg.u32[3] = 0;
	EFX_SET_OWORD_FIELD(drv_ev_reg, FRF_AZ_DRV_EV_QID, evq);
	efx_writeo(efx, &drv_ev_reg, FR_AZ_DRV_EV);
}

static void efx_magic_event(struct efx_channel *channel, u32 magic)
{
	efx_qword_t event;

	EFX_POPULATE_QWORD_2(event, FSF_AZ_EV_CODE,
			     FSE_AZ_EV_CODE_DRV_GEN_EV,
			     FSF_AZ_DRV_GEN_EV_MAGIC, magic);
	efx_generate_event(channel->efx, channel->channel, &event);
}

/* Handle a transmit completion event
 *
 * The NIC batches TX completion events; the message we receive is of
 * the form "complete all TX events up to this index".
 */
static int
efx_handle_tx_event(struct efx_channel *channel, efx_qword_t *event)
{
	unsigned int tx_ev_desc_ptr;
	unsigned int tx_ev_q_label;
	struct efx_tx_queue *tx_queue;
	struct efx_nic *efx = channel->efx;
	int tx_packets = 0;

	if (unlikely(ACCESS_ONCE(efx->reset_pending)))
		return 0;

	if (likely(EFX_QWORD_FIELD(*event, FSF_AZ_TX_EV_COMP))) {
		/* Transmit completion */
		tx_ev_desc_ptr = EFX_QWORD_FIELD(*event, FSF_AZ_TX_EV_DESC_PTR);
		tx_ev_q_label = EFX_QWORD_FIELD(*event, FSF_AZ_TX_EV_Q_LABEL);
		tx_queue = efx_channel_get_tx_queue(
			channel, tx_ev_q_label % EFX_TXQ_TYPES);
		tx_packets = ((tx_ev_desc_ptr - tx_queue->read_count) &
			      tx_queue->ptr_mask);
		efx_xmit_done(tx_queue, tx_ev_desc_ptr);
	} else if (EFX_QWORD_FIELD(*event, FSF_AZ_TX_EV_WQ_FF_FULL)) {
		/* Rewrite the FIFO write pointer */
		tx_ev_q_label = EFX_QWORD_FIELD(*event, FSF_AZ_TX_EV_Q_LABEL);
		tx_queue = efx_channel_get_tx_queue(
			channel, tx_ev_q_label % EFX_TXQ_TYPES);

		netif_tx_lock(efx->net_dev);
		efx_notify_tx_desc(tx_queue);
		netif_tx_unlock(efx->net_dev);
	} else if (EFX_QWORD_FIELD(*event, FSF_AZ_TX_EV_PKT_ERR) &&
		   EFX_WORKAROUND_10727(efx)) {
		efx_schedule_reset(efx, RESET_TYPE_TX_DESC_FETCH);
	} else {
		netif_err(efx, tx_err, efx->net_dev,
			  "channel %d unexpected TX event "
			  EFX_QWORD_FMT"\n", channel->channel,
			  EFX_QWORD_VAL(*event));
	}

	return tx_packets;
}

/* Detect errors included in the rx_evt_pkt_ok bit. */
static u16 efx_handle_rx_not_ok(struct efx_rx_queue *rx_queue,
				const efx_qword_t *event)
{
	struct efx_channel *channel = efx_rx_queue_channel(rx_queue);
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
			  EFX_QWORD_FMT "%s%s%s%s%s%s%s%s\n",
			  efx_rx_queue_index(rx_queue), EFX_QWORD_VAL(*event),
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
		EFX_RX_PKT_DISCARD : 0;
}

/* Handle receive events that are not in-order. */
static void
efx_handle_rx_bad_index(struct efx_rx_queue *rx_queue, unsigned index)
{
	struct efx_nic *efx = rx_queue->efx;
	unsigned expected, dropped;

	expected = rx_queue->removed_count & rx_queue->ptr_mask;
	dropped = (index - expected) & rx_queue->ptr_mask;
	netif_info(efx, rx_err, efx->net_dev,
		   "dropped %d events (index=%d expected=%d)\n",
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
	bool rx_ev_pkt_ok;
	u16 flags;
	struct efx_rx_queue *rx_queue;
	struct efx_nic *efx = channel->efx;

	if (unlikely(ACCESS_ONCE(efx->reset_pending)))
		return;

	/* Basic packet information */
	rx_ev_byte_cnt = EFX_QWORD_FIELD(*event, FSF_AZ_RX_EV_BYTE_CNT);
	rx_ev_pkt_ok = EFX_QWORD_FIELD(*event, FSF_AZ_RX_EV_PKT_OK);
	rx_ev_hdr_type = EFX_QWORD_FIELD(*event, FSF_AZ_RX_EV_HDR_TYPE);
	WARN_ON(EFX_QWORD_FIELD(*event, FSF_AZ_RX_EV_JUMBO_CONT));
	WARN_ON(EFX_QWORD_FIELD(*event, FSF_AZ_RX_EV_SOP) != 1);
	WARN_ON(EFX_QWORD_FIELD(*event, FSF_AZ_RX_EV_Q_LABEL) !=
		channel->channel);

	rx_queue = efx_channel_get_rx_queue(channel);

	rx_ev_desc_ptr = EFX_QWORD_FIELD(*event, FSF_AZ_RX_EV_DESC_PTR);
	expected_ptr = rx_queue->removed_count & rx_queue->ptr_mask;
	if (unlikely(rx_ev_desc_ptr != expected_ptr))
		efx_handle_rx_bad_index(rx_queue, rx_ev_desc_ptr);

	if (likely(rx_ev_pkt_ok)) {
		/* If packet is marked as OK and packet type is TCP/IP or
		 * UDP/IP, then we can rely on the hardware checksum.
		 */
		flags = (rx_ev_hdr_type == FSE_CZ_RX_EV_HDR_TYPE_IPV4V6_TCP ||
			 rx_ev_hdr_type == FSE_CZ_RX_EV_HDR_TYPE_IPV4V6_UDP) ?
			EFX_RX_PKT_CSUMMED : 0;
	} else {
		flags = efx_handle_rx_not_ok(rx_queue, event);
	}

	/* Detect multicast packets that didn't match the filter */
	rx_ev_mcast_pkt = EFX_QWORD_FIELD(*event, FSF_AZ_RX_EV_MCAST_PKT);
	if (rx_ev_mcast_pkt) {
		unsigned int rx_ev_mcast_hash_match =
			EFX_QWORD_FIELD(*event, FSF_AZ_RX_EV_MCAST_HASH_MATCH);

		if (unlikely(!rx_ev_mcast_hash_match)) {
			++channel->n_rx_mcast_mismatch;
			flags |= EFX_RX_PKT_DISCARD;
		}
	}

	channel->irq_mod_score += 2;

	/* Handle received packet */
	efx_rx_packet(rx_queue, rx_ev_desc_ptr, rx_ev_byte_cnt, flags);
}

/* If this flush done event corresponds to a &struct efx_tx_queue, then
 * send an %EFX_CHANNEL_MAGIC_TX_DRAIN event to drain the event queue
 * of all transmit completions.
 */
static void
efx_handle_tx_flush_done(struct efx_nic *efx, efx_qword_t *event)
{
	struct efx_tx_queue *tx_queue;
	int qid;

	qid = EFX_QWORD_FIELD(*event, FSF_AZ_DRIVER_EV_SUBDATA);
	if (qid < EFX_TXQ_TYPES * efx->n_tx_channels) {
		tx_queue = efx_get_tx_queue(efx, qid / EFX_TXQ_TYPES,
					    qid % EFX_TXQ_TYPES);
		if (atomic_cmpxchg(&tx_queue->flush_outstanding, 1, 0)) {
			efx_magic_event(tx_queue->channel,
					EFX_CHANNEL_MAGIC_TX_DRAIN(tx_queue));
		}
	}
}

/* If this flush done event corresponds to a &struct efx_rx_queue: If the flush
 * was succesful then send an %EFX_CHANNEL_MAGIC_RX_DRAIN, otherwise add
 * the RX queue back to the mask of RX queues in need of flushing.
 */
static void
efx_handle_rx_flush_done(struct efx_nic *efx, efx_qword_t *event)
{
	struct efx_channel *channel;
	struct efx_rx_queue *rx_queue;
	int qid;
	bool failed;

	qid = EFX_QWORD_FIELD(*event, FSF_AZ_DRIVER_EV_RX_DESCQ_ID);
	failed = EFX_QWORD_FIELD(*event, FSF_AZ_DRIVER_EV_RX_FLUSH_FAIL);
	if (qid >= efx->n_channels)
		return;
	channel = efx_get_channel(efx, qid);
	if (!efx_channel_has_rx_queue(channel))
		return;
	rx_queue = efx_channel_get_rx_queue(channel);

	if (failed) {
		netif_info(efx, hw, efx->net_dev,
			   "RXQ %d flush retry\n", qid);
		rx_queue->flush_pending = true;
		atomic_inc(&efx->rxq_flush_pending);
	} else {
		efx_magic_event(efx_rx_queue_channel(rx_queue),
				EFX_CHANNEL_MAGIC_RX_DRAIN(rx_queue));
	}
	atomic_dec(&efx->rxq_flush_outstanding);
	if (efx_flush_wake(efx))
		wake_up(&efx->flush_wq);
}

static void
efx_handle_drain_event(struct efx_channel *channel)
{
	struct efx_nic *efx = channel->efx;

	WARN_ON(atomic_read(&efx->drain_pending) == 0);
	atomic_dec(&efx->drain_pending);
	if (efx_flush_wake(efx))
		wake_up(&efx->flush_wq);
}

static void
efx_handle_generated_event(struct efx_channel *channel, efx_qword_t *event)
{
	struct efx_nic *efx = channel->efx;
	struct efx_rx_queue *rx_queue =
		efx_channel_has_rx_queue(channel) ?
		efx_channel_get_rx_queue(channel) : NULL;
	unsigned magic, code;

	magic = EFX_QWORD_FIELD(*event, FSF_AZ_DRV_GEN_EV_MAGIC);
	code = _EFX_CHANNEL_MAGIC_CODE(magic);

	if (magic == EFX_CHANNEL_MAGIC_TEST(channel)) {
		channel->event_test_cpu = raw_smp_processor_id();
	} else if (rx_queue && magic == EFX_CHANNEL_MAGIC_FILL(rx_queue)) {
		/* The queue must be empty, so we won't receive any rx
		 * events, so efx_process_channel() won't refill the
		 * queue. Refill it here */
		efx_fast_push_rx_descriptors(rx_queue);
	} else if (rx_queue && magic == EFX_CHANNEL_MAGIC_RX_DRAIN(rx_queue)) {
		rx_queue->enabled = false;
		efx_handle_drain_event(channel);
	} else if (code == _EFX_CHANNEL_MAGIC_TX_DRAIN) {
		efx_handle_drain_event(channel);
	} else {
		netif_dbg(efx, hw, efx->net_dev, "channel %d received "
			  "generated event "EFX_QWORD_FMT"\n",
			  channel->channel, EFX_QWORD_VAL(*event));
	}
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
		netif_vdbg(efx, hw, efx->net_dev, "channel %d TXQ %d flushed\n",
			   channel->channel, ev_sub_data);
		efx_handle_tx_flush_done(efx, event);
		efx_sriov_tx_flush_done(efx, event);
		break;
	case FSE_AZ_RX_DESCQ_FLS_DONE_EV:
		netif_vdbg(efx, hw, efx->net_dev, "channel %d RXQ %d flushed\n",
			   channel->channel, ev_sub_data);
		efx_handle_rx_flush_done(efx, event);
		efx_sriov_rx_flush_done(efx, event);
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
		efx_schedule_reset(efx,
				   EFX_WORKAROUND_6555(efx) ?
				   RESET_TYPE_RX_RECOVERY :
				   RESET_TYPE_DISABLE);
		break;
	case FSE_BZ_RX_DSC_ERROR_EV:
		if (ev_sub_data < EFX_VI_BASE) {
			netif_err(efx, rx_err, efx->net_dev,
				  "RX DMA Q %d reports descriptor fetch error."
				  " RX Q %d is disabled.\n", ev_sub_data,
				  ev_sub_data);
			efx_schedule_reset(efx, RESET_TYPE_RX_DESC_FETCH);
		} else
			efx_sriov_desc_fetch_err(efx, ev_sub_data);
		break;
	case FSE_BZ_TX_DSC_ERROR_EV:
		if (ev_sub_data < EFX_VI_BASE) {
			netif_err(efx, tx_err, efx->net_dev,
				  "TX DMA Q %d reports descriptor fetch error."
				  " TX Q %d is disabled.\n", ev_sub_data,
				  ev_sub_data);
			efx_schedule_reset(efx, RESET_TYPE_TX_DESC_FETCH);
		} else
			efx_sriov_desc_fetch_err(efx, ev_sub_data);
		break;
	default:
		netif_vdbg(efx, hw, efx->net_dev,
			   "channel %d unknown driver event code %d "
			   "data %04x\n", channel->channel, ev_sub_code,
			   ev_sub_data);
		break;
	}
}

int efx_nic_process_eventq(struct efx_channel *channel, int budget)
{
	struct efx_nic *efx = channel->efx;
	unsigned int read_ptr;
	efx_qword_t event, *p_event;
	int ev_code;
	int tx_packets = 0;
	int spent = 0;

	read_ptr = channel->eventq_read_ptr;

	for (;;) {
		p_event = efx_event(channel, read_ptr);
		event = *p_event;

		if (!efx_event_present(&event))
			/* End of events */
			break;

		netif_vdbg(channel->efx, intr, channel->efx->net_dev,
			   "channel %d event is "EFX_QWORD_FMT"\n",
			   channel->channel, EFX_QWORD_VAL(event));

		/* Clear this event by marking it all ones */
		EFX_SET_QWORD(*p_event);

		++read_ptr;

		ev_code = EFX_QWORD_FIELD(event, FSF_AZ_EV_CODE);

		switch (ev_code) {
		case FSE_AZ_EV_CODE_RX_EV:
			efx_handle_rx_event(channel, &event);
			if (++spent == budget)
				goto out;
			break;
		case FSE_AZ_EV_CODE_TX_EV:
			tx_packets += efx_handle_tx_event(channel, &event);
			if (tx_packets > efx->txq_entries) {
				spent = budget;
				goto out;
			}
			break;
		case FSE_AZ_EV_CODE_DRV_GEN_EV:
			efx_handle_generated_event(channel, &event);
			break;
		case FSE_AZ_EV_CODE_DRIVER_EV:
			efx_handle_driver_event(channel, &event);
			break;
		case FSE_CZ_EV_CODE_USER_EV:
			efx_sriov_event(channel, &event);
			break;
		case FSE_CZ_EV_CODE_MCDI_EV:
			efx_mcdi_process_event(channel, &event);
			break;
		case FSE_AZ_EV_CODE_GLOBAL_EV:
			if (efx->type->handle_global_event &&
			    efx->type->handle_global_event(channel, &event))
				break;
			/* else fall through */
		default:
			netif_err(channel->efx, hw, channel->efx->net_dev,
				  "channel %d unknown event type %d (data "
				  EFX_QWORD_FMT ")\n", channel->channel,
				  ev_code, EFX_QWORD_VAL(event));
		}
	}

out:
	channel->eventq_read_ptr = read_ptr;
	return spent;
}

/* Check whether an event is present in the eventq at the current
 * read pointer.  Only useful for self-test.
 */
bool efx_nic_event_present(struct efx_channel *channel)
{
	return efx_event_present(efx_event(channel, channel->eventq_read_ptr));
}

/* Allocate buffer table entries for event queue */
int efx_nic_probe_eventq(struct efx_channel *channel)
{
	struct efx_nic *efx = channel->efx;
	unsigned entries;

	entries = channel->eventq_mask + 1;
	return efx_alloc_special_buffer(efx, &channel->eventq,
					entries * sizeof(efx_qword_t));
}

void efx_nic_init_eventq(struct efx_channel *channel)
{
	efx_oword_t reg;
	struct efx_nic *efx = channel->efx;

	netif_dbg(efx, hw, efx->net_dev,
		  "channel %d event queue in special buffers %d-%d\n",
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


void efx_nic_event_test_start(struct efx_channel *channel)
{
	channel->event_test_cpu = -1;
	smp_wmb();
	efx_magic_event(channel, EFX_CHANNEL_MAGIC_TEST(channel));
}

void efx_nic_generate_fill_event(struct efx_rx_queue *rx_queue)
{
	efx_magic_event(efx_rx_queue_channel(rx_queue),
			EFX_CHANNEL_MAGIC_FILL(rx_queue));
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

	EFX_POPULATE_OWORD_3(int_en_reg_ker,
			     FRF_AZ_KER_INT_LEVE_SEL, efx->irq_level,
			     FRF_AZ_KER_INT_KER, force,
			     FRF_AZ_DRV_INT_EN_KER, enabled);
	efx_writeo(efx, &int_en_reg_ker, FR_AZ_INT_EN_KER);
}

void efx_nic_enable_interrupts(struct efx_nic *efx)
{
	EFX_ZERO_OWORD(*((efx_oword_t *) efx->irq_status.addr));
	wmb(); /* Ensure interrupt vector is clear before interrupts enabled */

	efx_nic_interrupts(efx, true, false);
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
void efx_nic_irq_test_start(struct efx_nic *efx)
{
	efx->last_irq_cpu = -1;
	smp_wmb();
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

	netif_err(efx, hw, efx->net_dev, "SYSTEM ERROR "EFX_OWORD_FMT" status "
		  EFX_OWORD_FMT ": %s\n", EFX_OWORD_VAL(*int_ker),
		  EFX_OWORD_VAL(fatal_intr),
		  error ? "disabling bus mastering" : "no recognised error");

	/* If this is a memory parity error dump which blocks are offending */
	mem_perr = (EFX_OWORD_FIELD(fatal_intr, FRF_AZ_MEM_PERR_INT_KER) ||
		    EFX_OWORD_FIELD(fatal_intr, FRF_AZ_SRM_PERR_INT_KER));
	if (mem_perr) {
		efx_oword_t reg;
		efx_reado(efx, &reg, FR_AZ_MEM_STAT);
		netif_err(efx, hw, efx->net_dev,
			  "SYSTEM ERROR: memory parity error "EFX_OWORD_FMT"\n",
			  EFX_OWORD_VAL(reg));
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
		netif_err(efx, hw, efx->net_dev,
			  "SYSTEM ERROR - reset scheduled\n");
		efx_schedule_reset(efx, RESET_TYPE_INT_ERROR);
	} else {
		netif_err(efx, hw, efx->net_dev,
			  "SYSTEM ERROR - max number of errors seen."
			  "NIC will be disabled\n");
		efx_schedule_reset(efx, RESET_TYPE_DISABLE);
	}

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

	/* Could this be ours?  If interrupts are disabled then the
	 * channel state may not be valid.
	 */
	if (!efx->legacy_irq_enabled)
		return result;

	/* Read the ISR which also ACKs the interrupts */
	efx_readd(efx, &reg, FR_BZ_INT_ISR0);
	queues = EFX_EXTRACT_DWORD(reg, 0, 31);

	/* Handle non-event-queue sources */
	if (queues & (1U << efx->irq_level)) {
		syserr = EFX_OWORD_FIELD(*int_ker, FSF_AZ_NET_IVEC_FATAL_INT);
		if (unlikely(syserr))
			return efx_nic_fatal_interrupt(efx);
		efx->last_irq_cpu = raw_smp_processor_id();
	}

	if (queues != 0) {
		if (EFX_WORKAROUND_15783(efx))
			efx->irq_zero_count = 0;

		/* Schedule processing of any interrupting queues */
		efx_for_each_channel(channel, efx) {
			if (queues & 1)
				efx_schedule_channel_irq(channel);
			queues >>= 1;
		}
		result = IRQ_HANDLED;

	} else if (EFX_WORKAROUND_15783(efx)) {
		efx_qword_t *event;

		/* We can't return IRQ_HANDLED more than once on seeing ISR=0
		 * because this might be a shared interrupt. */
		if (efx->irq_zero_count++ == 0)
			result = IRQ_HANDLED;

		/* Ensure we schedule or rearm all event queues */
		efx_for_each_channel(channel, efx) {
			event = efx_event(channel, channel->eventq_read_ptr);
			if (efx_event_present(event))
				efx_schedule_channel_irq(channel);
			else
				efx_nic_eventq_read_ack(channel);
		}
	}

	if (result == IRQ_HANDLED)
		netif_vdbg(efx, intr, efx->net_dev,
			   "IRQ %d on CPU %d status " EFX_DWORD_FMT "\n",
			   irq, raw_smp_processor_id(), EFX_DWORD_VAL(reg));

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
	struct efx_channel *channel = *(struct efx_channel **)dev_id;
	struct efx_nic *efx = channel->efx;
	efx_oword_t *int_ker = efx->irq_status.addr;
	int syserr;

	netif_vdbg(efx, intr, efx->net_dev,
		   "IRQ %d on CPU %d status " EFX_OWORD_FMT "\n",
		   irq, raw_smp_processor_id(), EFX_OWORD_VAL(*int_ker));

	/* Handle non-event-queue sources */
	if (channel->channel == efx->irq_level) {
		syserr = EFX_OWORD_FIELD(*int_ker, FSF_AZ_NET_IVEC_FATAL_INT);
		if (unlikely(syserr))
			return efx_nic_fatal_interrupt(efx);
		efx->last_irq_cpu = raw_smp_processor_id();
	}

	/* Schedule processing of the channel */
	efx_schedule_channel_irq(channel);

	return IRQ_HANDLED;
}


/* Setup RSS indirection table.
 * This maps from the hash value of the packet to RXQ
 */
void efx_nic_push_rx_indir_table(struct efx_nic *efx)
{
	size_t i = 0;
	efx_dword_t dword;

	if (efx_nic_rev(efx) < EFX_REV_FALCON_B0)
		return;

	BUILD_BUG_ON(ARRAY_SIZE(efx->rx_indir_table) !=
		     FR_BZ_RX_INDIRECTION_TBL_ROWS);

	for (i = 0; i < FR_BZ_RX_INDIRECTION_TBL_ROWS; i++) {
		EFX_POPULATE_DWORD_1(dword, FRF_BZ_IT_QUEUE,
				     efx->rx_indir_table[i]);
		efx_writed(efx, &dword,
			   FR_BZ_RX_INDIRECTION_TBL +
			   FR_BZ_RX_INDIRECTION_TBL_STEP * i);
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
			netif_err(efx, drv, efx->net_dev,
				  "failed to hook legacy IRQ %d\n",
				  efx->pci_dev->irq);
			goto fail1;
		}
		return 0;
	}

	/* Hook MSI or MSI-X interrupt */
	efx_for_each_channel(channel, efx) {
		rc = request_irq(channel->irq, efx_msi_interrupt,
				 IRQF_PROBE_SHARED, /* Not shared */
				 efx->channel_name[channel->channel],
				 &efx->channel[channel->channel]);
		if (rc) {
			netif_err(efx, drv, efx->net_dev,
				  "failed to hook IRQ %d\n", channel->irq);
			goto fail2;
		}
	}

	return 0;

 fail2:
	efx_for_each_channel(channel, efx)
		free_irq(channel->irq, &efx->channel[channel->channel]);
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
			free_irq(channel->irq, &efx->channel[channel->channel]);
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

/* Looks at available SRAM resources and works out how many queues we
 * can support, and where things like descriptor caches should live.
 *
 * SRAM is split up as follows:
 * 0                          buftbl entries for channels
 * efx->vf_buftbl_base        buftbl entries for SR-IOV
 * efx->rx_dc_base            RX descriptor caches
 * efx->tx_dc_base            TX descriptor caches
 */
void efx_nic_dimension_resources(struct efx_nic *efx, unsigned sram_lim_qw)
{
	unsigned vi_count, buftbl_min;

	/* Account for the buffer table entries backing the datapath channels
	 * and the descriptor caches for those channels.
	 */
	buftbl_min = ((efx->n_rx_channels * EFX_MAX_DMAQ_SIZE +
		       efx->n_tx_channels * EFX_TXQ_TYPES * EFX_MAX_DMAQ_SIZE +
		       efx->n_channels * EFX_MAX_EVQ_SIZE)
		      * sizeof(efx_qword_t) / EFX_BUF_SIZE);
	vi_count = max(efx->n_channels, efx->n_tx_channels * EFX_TXQ_TYPES);

#ifdef CONFIG_SFC_SRIOV
	if (efx_sriov_wanted(efx)) {
		unsigned vi_dc_entries, buftbl_free, entries_per_vf, vf_limit;

		efx->vf_buftbl_base = buftbl_min;

		vi_dc_entries = RX_DC_ENTRIES + TX_DC_ENTRIES;
		vi_count = max(vi_count, EFX_VI_BASE);
		buftbl_free = (sram_lim_qw - buftbl_min -
			       vi_count * vi_dc_entries);

		entries_per_vf = ((vi_dc_entries + EFX_VF_BUFTBL_PER_VI) *
				  efx_vf_size(efx));
		vf_limit = min(buftbl_free / entries_per_vf,
			       (1024U - EFX_VI_BASE) >> efx->vi_scale);

		if (efx->vf_count > vf_limit) {
			netif_err(efx, probe, efx->net_dev,
				  "Reducing VF count from from %d to %d\n",
				  efx->vf_count, vf_limit);
			efx->vf_count = vf_limit;
		}
		vi_count += efx->vf_count * efx_vf_size(efx);
	}
#endif

	efx->tx_dc_base = sram_lim_qw - vi_count * TX_DC_ENTRIES;
	efx->rx_dc_base = efx->tx_dc_base - vi_count * RX_DC_ENTRIES;
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
	EFX_POPULATE_OWORD_1(temp, FRF_AZ_SRM_TX_DC_BASE_ADR, efx->tx_dc_base);
	efx_writeo(efx, &temp, FR_AZ_SRM_TX_DC_CFG);
	EFX_POPULATE_OWORD_1(temp, FRF_AZ_SRM_RX_DC_BASE_ADR, efx->rx_dc_base);
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

	if (EFX_WORKAROUND_17213(efx) && !EFX_INT_MODE_USE_MSI(efx))
		/* Use an interrupt level unused by event queues */
		efx->irq_level = 0x1f;
	else
		/* Use a valid MSI-X vector */
		efx->irq_level = 0;

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
	if (efx_nic_rev(efx) >= EFX_REV_SIENA_A0)
		EFX_SET_OWORD_FIELD(temp, FRF_CZ_SRAM_PERR_INT_P_KER_EN, 1);
	EFX_INVERT_OWORD(temp);
	efx_writeo(efx, &temp, FR_AZ_FATAL_INTR_KER);

	efx_nic_push_rx_indir_table(efx);

	/* Disable the ugly timer-based TX DMA backoff and allow TX DMA to be
	 * controlled by the RX FIFO fill level. Set arbitration to one pkt/Q.
	 */
	efx_reado(efx, &temp, FR_AZ_TX_RESERVED);
	EFX_SET_OWORD_FIELD(temp, FRF_AZ_TX_RX_SPACER, 0xfe);
	EFX_SET_OWORD_FIELD(temp, FRF_AZ_TX_RX_SPACER_EN, 1);
	EFX_SET_OWORD_FIELD(temp, FRF_AZ_TX_ONE_PKT_PER_Q, 1);
	EFX_SET_OWORD_FIELD(temp, FRF_AZ_TX_PUSH_EN, 1);
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

	if (efx_nic_rev(efx) >= EFX_REV_FALCON_B0) {
		EFX_POPULATE_OWORD_4(temp,
				     /* Default values */
				     FRF_BZ_TX_PACE_SB_NOT_AF, 0x15,
				     FRF_BZ_TX_PACE_SB_AF, 0xb,
				     FRF_BZ_TX_PACE_FB_BASE, 0,
				     /* Allow large pace values in the
				      * fast bin. */
				     FRF_BZ_TX_PACE_BIN_TH,
				     FFE_BZ_TX_PACE_RESERVED);
		efx_writeo(efx, &temp, FR_BZ_TX_PACE);
	}
}

/* Register dump */

#define REGISTER_REVISION_A	1
#define REGISTER_REVISION_B	2
#define REGISTER_REVISION_C	3
#define REGISTER_REVISION_Z	3	/* latest revision */

struct efx_nic_reg {
	u32 offset:24;
	u32 min_revision:2, max_revision:2;
};

#define REGISTER(name, min_rev, max_rev) {				\
	FR_ ## min_rev ## max_rev ## _ ## name,				\
	REGISTER_REVISION_ ## min_rev, REGISTER_REVISION_ ## max_rev	\
}
#define REGISTER_AA(name) REGISTER(name, A, A)
#define REGISTER_AB(name) REGISTER(name, A, B)
#define REGISTER_AZ(name) REGISTER(name, A, Z)
#define REGISTER_BB(name) REGISTER(name, B, B)
#define REGISTER_BZ(name) REGISTER(name, B, Z)
#define REGISTER_CZ(name) REGISTER(name, C, Z)

static const struct efx_nic_reg efx_nic_regs[] = {
	REGISTER_AZ(ADR_REGION),
	REGISTER_AZ(INT_EN_KER),
	REGISTER_BZ(INT_EN_CHAR),
	REGISTER_AZ(INT_ADR_KER),
	REGISTER_BZ(INT_ADR_CHAR),
	/* INT_ACK_KER is WO */
	/* INT_ISR0 is RC */
	REGISTER_AZ(HW_INIT),
	REGISTER_CZ(USR_EV_CFG),
	REGISTER_AB(EE_SPI_HCMD),
	REGISTER_AB(EE_SPI_HADR),
	REGISTER_AB(EE_SPI_HDATA),
	REGISTER_AB(EE_BASE_PAGE),
	REGISTER_AB(EE_VPD_CFG0),
	/* EE_VPD_SW_CNTL and EE_VPD_SW_DATA are not used */
	/* PMBX_DBG_IADDR and PBMX_DBG_IDATA are indirect */
	/* PCIE_CORE_INDIRECT is indirect */
	REGISTER_AB(NIC_STAT),
	REGISTER_AB(GPIO_CTL),
	REGISTER_AB(GLB_CTL),
	/* FATAL_INTR_KER and FATAL_INTR_CHAR are partly RC */
	REGISTER_BZ(DP_CTRL),
	REGISTER_AZ(MEM_STAT),
	REGISTER_AZ(CS_DEBUG),
	REGISTER_AZ(ALTERA_BUILD),
	REGISTER_AZ(CSR_SPARE),
	REGISTER_AB(PCIE_SD_CTL0123),
	REGISTER_AB(PCIE_SD_CTL45),
	REGISTER_AB(PCIE_PCS_CTL_STAT),
	/* DEBUG_DATA_OUT is not used */
	/* DRV_EV is WO */
	REGISTER_AZ(EVQ_CTL),
	REGISTER_AZ(EVQ_CNT1),
	REGISTER_AZ(EVQ_CNT2),
	REGISTER_AZ(BUF_TBL_CFG),
	REGISTER_AZ(SRM_RX_DC_CFG),
	REGISTER_AZ(SRM_TX_DC_CFG),
	REGISTER_AZ(SRM_CFG),
	/* BUF_TBL_UPD is WO */
	REGISTER_AZ(SRM_UPD_EVQ),
	REGISTER_AZ(SRAM_PARITY),
	REGISTER_AZ(RX_CFG),
	REGISTER_BZ(RX_FILTER_CTL),
	/* RX_FLUSH_DESCQ is WO */
	REGISTER_AZ(RX_DC_CFG),
	REGISTER_AZ(RX_DC_PF_WM),
	REGISTER_BZ(RX_RSS_TKEY),
	/* RX_NODESC_DROP is RC */
	REGISTER_AA(RX_SELF_RST),
	/* RX_DEBUG, RX_PUSH_DROP are not used */
	REGISTER_CZ(RX_RSS_IPV6_REG1),
	REGISTER_CZ(RX_RSS_IPV6_REG2),
	REGISTER_CZ(RX_RSS_IPV6_REG3),
	/* TX_FLUSH_DESCQ is WO */
	REGISTER_AZ(TX_DC_CFG),
	REGISTER_AA(TX_CHKSM_CFG),
	REGISTER_AZ(TX_CFG),
	/* TX_PUSH_DROP is not used */
	REGISTER_AZ(TX_RESERVED),
	REGISTER_BZ(TX_PACE),
	/* TX_PACE_DROP_QID is RC */
	REGISTER_BB(TX_VLAN),
	REGISTER_BZ(TX_IPFIL_PORTEN),
	REGISTER_AB(MD_TXD),
	REGISTER_AB(MD_RXD),
	REGISTER_AB(MD_CS),
	REGISTER_AB(MD_PHY_ADR),
	REGISTER_AB(MD_ID),
	/* MD_STAT is RC */
	REGISTER_AB(MAC_STAT_DMA),
	REGISTER_AB(MAC_CTRL),
	REGISTER_BB(GEN_MODE),
	REGISTER_AB(MAC_MC_HASH_REG0),
	REGISTER_AB(MAC_MC_HASH_REG1),
	REGISTER_AB(GM_CFG1),
	REGISTER_AB(GM_CFG2),
	/* GM_IPG and GM_HD are not used */
	REGISTER_AB(GM_MAX_FLEN),
	/* GM_TEST is not used */
	REGISTER_AB(GM_ADR1),
	REGISTER_AB(GM_ADR2),
	REGISTER_AB(GMF_CFG0),
	REGISTER_AB(GMF_CFG1),
	REGISTER_AB(GMF_CFG2),
	REGISTER_AB(GMF_CFG3),
	REGISTER_AB(GMF_CFG4),
	REGISTER_AB(GMF_CFG5),
	REGISTER_BB(TX_SRC_MAC_CTL),
	REGISTER_AB(XM_ADR_LO),
	REGISTER_AB(XM_ADR_HI),
	REGISTER_AB(XM_GLB_CFG),
	REGISTER_AB(XM_TX_CFG),
	REGISTER_AB(XM_RX_CFG),
	REGISTER_AB(XM_MGT_INT_MASK),
	REGISTER_AB(XM_FC),
	REGISTER_AB(XM_PAUSE_TIME),
	REGISTER_AB(XM_TX_PARAM),
	REGISTER_AB(XM_RX_PARAM),
	/* XM_MGT_INT_MSK (note no 'A') is RC */
	REGISTER_AB(XX_PWR_RST),
	REGISTER_AB(XX_SD_CTL),
	REGISTER_AB(XX_TXDRV_CTL),
	/* XX_PRBS_CTL, XX_PRBS_CHK and XX_PRBS_ERR are not used */
	/* XX_CORE_STAT is partly RC */
};

struct efx_nic_reg_table {
	u32 offset:24;
	u32 min_revision:2, max_revision:2;
	u32 step:6, rows:21;
};

#define REGISTER_TABLE_DIMENSIONS(_, offset, min_rev, max_rev, step, rows) { \
	offset,								\
	REGISTER_REVISION_ ## min_rev, REGISTER_REVISION_ ## max_rev,	\
	step, rows							\
}
#define REGISTER_TABLE(name, min_rev, max_rev)				\
	REGISTER_TABLE_DIMENSIONS(					\
		name, FR_ ## min_rev ## max_rev ## _ ## name,		\
		min_rev, max_rev,					\
		FR_ ## min_rev ## max_rev ## _ ## name ## _STEP,	\
		FR_ ## min_rev ## max_rev ## _ ## name ## _ROWS)
#define REGISTER_TABLE_AA(name) REGISTER_TABLE(name, A, A)
#define REGISTER_TABLE_AZ(name) REGISTER_TABLE(name, A, Z)
#define REGISTER_TABLE_BB(name) REGISTER_TABLE(name, B, B)
#define REGISTER_TABLE_BZ(name) REGISTER_TABLE(name, B, Z)
#define REGISTER_TABLE_BB_CZ(name)					\
	REGISTER_TABLE_DIMENSIONS(name, FR_BZ_ ## name, B, B,		\
				  FR_BZ_ ## name ## _STEP,		\
				  FR_BB_ ## name ## _ROWS),		\
	REGISTER_TABLE_DIMENSIONS(name, FR_BZ_ ## name, C, Z,		\
				  FR_BZ_ ## name ## _STEP,		\
				  FR_CZ_ ## name ## _ROWS)
#define REGISTER_TABLE_CZ(name) REGISTER_TABLE(name, C, Z)

static const struct efx_nic_reg_table efx_nic_reg_tables[] = {
	/* DRIVER is not used */
	/* EVQ_RPTR, TIMER_COMMAND, USR_EV and {RX,TX}_DESC_UPD are WO */
	REGISTER_TABLE_BB(TX_IPFIL_TBL),
	REGISTER_TABLE_BB(TX_SRC_MAC_TBL),
	REGISTER_TABLE_AA(RX_DESC_PTR_TBL_KER),
	REGISTER_TABLE_BB_CZ(RX_DESC_PTR_TBL),
	REGISTER_TABLE_AA(TX_DESC_PTR_TBL_KER),
	REGISTER_TABLE_BB_CZ(TX_DESC_PTR_TBL),
	REGISTER_TABLE_AA(EVQ_PTR_TBL_KER),
	REGISTER_TABLE_BB_CZ(EVQ_PTR_TBL),
	/* We can't reasonably read all of the buffer table (up to 8MB!).
	 * However this driver will only use a few entries.  Reading
	 * 1K entries allows for some expansion of queue count and
	 * size before we need to change the version. */
	REGISTER_TABLE_DIMENSIONS(BUF_FULL_TBL_KER, FR_AA_BUF_FULL_TBL_KER,
				  A, A, 8, 1024),
	REGISTER_TABLE_DIMENSIONS(BUF_FULL_TBL, FR_BZ_BUF_FULL_TBL,
				  B, Z, 8, 1024),
	REGISTER_TABLE_CZ(RX_MAC_FILTER_TBL0),
	REGISTER_TABLE_BB_CZ(TIMER_TBL),
	REGISTER_TABLE_BB_CZ(TX_PACE_TBL),
	REGISTER_TABLE_BZ(RX_INDIRECTION_TBL),
	/* TX_FILTER_TBL0 is huge and not used by this driver */
	REGISTER_TABLE_CZ(TX_MAC_FILTER_TBL0),
	REGISTER_TABLE_CZ(MC_TREG_SMEM),
	/* MSIX_PBA_TABLE is not mapped */
	/* SRM_DBG is not mapped (and is redundant with BUF_FLL_TBL) */
	REGISTER_TABLE_BZ(RX_FILTER_TBL0),
};

size_t efx_nic_get_regs_len(struct efx_nic *efx)
{
	const struct efx_nic_reg *reg;
	const struct efx_nic_reg_table *table;
	size_t len = 0;

	for (reg = efx_nic_regs;
	     reg < efx_nic_regs + ARRAY_SIZE(efx_nic_regs);
	     reg++)
		if (efx->type->revision >= reg->min_revision &&
		    efx->type->revision <= reg->max_revision)
			len += sizeof(efx_oword_t);

	for (table = efx_nic_reg_tables;
	     table < efx_nic_reg_tables + ARRAY_SIZE(efx_nic_reg_tables);
	     table++)
		if (efx->type->revision >= table->min_revision &&
		    efx->type->revision <= table->max_revision)
			len += table->rows * min_t(size_t, table->step, 16);

	return len;
}

void efx_nic_get_regs(struct efx_nic *efx, void *buf)
{
	const struct efx_nic_reg *reg;
	const struct efx_nic_reg_table *table;

	for (reg = efx_nic_regs;
	     reg < efx_nic_regs + ARRAY_SIZE(efx_nic_regs);
	     reg++) {
		if (efx->type->revision >= reg->min_revision &&
		    efx->type->revision <= reg->max_revision) {
			efx_reado(efx, (efx_oword_t *)buf, reg->offset);
			buf += sizeof(efx_oword_t);
		}
	}

	for (table = efx_nic_reg_tables;
	     table < efx_nic_reg_tables + ARRAY_SIZE(efx_nic_reg_tables);
	     table++) {
		size_t size, i;

		if (!(efx->type->revision >= table->min_revision &&
		      efx->type->revision <= table->max_revision))
			continue;

		size = min_t(size_t, table->step, 16);

		for (i = 0; i < table->rows; i++) {
			switch (table->step) {
			case 4: /* 32-bit SRAM */
				efx_readd(efx, buf, table->offset + 4 * i);
				break;
			case 8: /* 64-bit SRAM */
				efx_sram_readq(efx,
					       efx->membase + table->offset,
					       buf, i);
				break;
			case 16: /* 128-bit-readable register */
				efx_reado_table(efx, buf, table->offset, i);
				break;
			case 32: /* 128-bit register, interleaved */
				efx_reado_table(efx, buf, table->offset, 2 * i);
				break;
			default:
				WARN_ON(1);
				return;
			}
			buf += size;
		}
	}
}
