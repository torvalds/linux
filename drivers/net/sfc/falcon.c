/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2006-2008 Solarflare Communications Inc.
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
#include "mac.h"
#include "gmii.h"
#include "spi.h"
#include "falcon.h"
#include "falcon_hwdefs.h"
#include "falcon_io.h"
#include "mdio_10g.h"
#include "phy.h"
#include "boards.h"
#include "workarounds.h"

/* Falcon hardware control.
 * Falcon is the internal codename for the SFC4000 controller that is
 * present in SFE400X evaluation boards
 */

/**
 * struct falcon_nic_data - Falcon NIC state
 * @next_buffer_table: First available buffer table id
 * @pci_dev2: The secondary PCI device if present
 */
struct falcon_nic_data {
	unsigned next_buffer_table;
	struct pci_dev *pci_dev2;
};

/**************************************************************************
 *
 * Configurable values
 *
 **************************************************************************
 */

static int disable_dma_stats;

/* This is set to 16 for a good reason.  In summary, if larger than
 * 16, the descriptor cache holds more than a default socket
 * buffer's worth of packets (for UDP we can only have at most one
 * socket buffer's worth outstanding).  This combined with the fact
 * that we only get 1 TX event per descriptor cache means the NIC
 * goes idle.
 */
#define TX_DC_ENTRIES 16
#define TX_DC_ENTRIES_ORDER 0
#define TX_DC_BASE 0x130000

#define RX_DC_ENTRIES 64
#define RX_DC_ENTRIES_ORDER 2
#define RX_DC_BASE 0x100000

/* RX FIFO XOFF watermark
 *
 * When the amount of the RX FIFO increases used increases past this
 * watermark send XOFF. Only used if RX flow control is enabled (ethtool -A)
 * This also has an effect on RX/TX arbitration
 */
static int rx_xoff_thresh_bytes = -1;
module_param(rx_xoff_thresh_bytes, int, 0644);
MODULE_PARM_DESC(rx_xoff_thresh_bytes, "RX fifo XOFF threshold");

/* RX FIFO XON watermark
 *
 * When the amount of the RX FIFO used decreases below this
 * watermark send XON. Only used if TX flow control is enabled (ethtool -A)
 * This also has an effect on RX/TX arbitration
 */
static int rx_xon_thresh_bytes = -1;
module_param(rx_xon_thresh_bytes, int, 0644);
MODULE_PARM_DESC(rx_xon_thresh_bytes, "RX fifo XON threshold");

/* TX descriptor ring size - min 512 max 4k */
#define FALCON_TXD_RING_ORDER TX_DESCQ_SIZE_1K
#define FALCON_TXD_RING_SIZE 1024
#define FALCON_TXD_RING_MASK (FALCON_TXD_RING_SIZE - 1)

/* RX descriptor ring size - min 512 max 4k */
#define FALCON_RXD_RING_ORDER RX_DESCQ_SIZE_1K
#define FALCON_RXD_RING_SIZE 1024
#define FALCON_RXD_RING_MASK (FALCON_RXD_RING_SIZE - 1)

/* Event queue size - max 32k */
#define FALCON_EVQ_ORDER EVQ_SIZE_4K
#define FALCON_EVQ_SIZE 4096
#define FALCON_EVQ_MASK (FALCON_EVQ_SIZE - 1)

/* Max number of internal errors. After this resets will not be performed */
#define FALCON_MAX_INT_ERRORS 4

/* Maximum period that we wait for flush events. If the flush event
 * doesn't arrive in this period of time then we check if the queue
 * was disabled anyway. */
#define FALCON_FLUSH_TIMEOUT 10 /* 10ms */

/**************************************************************************
 *
 * Falcon constants
 *
 **************************************************************************
 */

/* DMA address mask (up to 46-bit, avoiding compiler warnings)
 *
 * Note that it is possible to have a platform with 64-bit longs and
 * 32-bit DMA addresses, or vice versa.  EFX_DMA_MASK takes care of the
 * platform DMA mask.
 */
#if BITS_PER_LONG == 64
#define FALCON_DMA_MASK EFX_DMA_MASK(0x00003fffffffffffUL)
#else
#define FALCON_DMA_MASK EFX_DMA_MASK(0x00003fffffffffffULL)
#endif

/* TX DMA length mask (13-bit) */
#define FALCON_TX_DMA_MASK (4096 - 1)

/* Size and alignment of special buffers (4KB) */
#define FALCON_BUF_SIZE 4096

/* Dummy SRAM size code */
#define SRM_NB_BSZ_ONCHIP_ONLY (-1)

/* Be nice if these (or equiv.) were in linux/pci_regs.h, but they're not. */
#define PCI_EXP_DEVCAP_PWR_VAL_LBN	18
#define PCI_EXP_DEVCAP_PWR_SCL_LBN	26
#define PCI_EXP_DEVCTL_PAYLOAD_LBN	5
#define PCI_EXP_LNKSTA_LNK_WID		0x3f0
#define PCI_EXP_LNKSTA_LNK_WID_LBN	4

#define FALCON_IS_DUAL_FUNC(efx)		\
	(FALCON_REV(efx) < FALCON_REV_B0)

/**************************************************************************
 *
 * Falcon hardware access
 *
 **************************************************************************/

/* Read the current event from the event queue */
static inline efx_qword_t *falcon_event(struct efx_channel *channel,
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
static inline int falcon_event_present(efx_qword_t *event)
{
	return (!(EFX_DWORD_IS_ALL_ONES(event->dword[0]) |
		  EFX_DWORD_IS_ALL_ONES(event->dword[1])));
}

/**************************************************************************
 *
 * I2C bus - this is a bit-bashing interface using GPIO pins
 * Note that it uses the output enables to tristate the outputs
 * SDA is the data pin and SCL is the clock
 *
 **************************************************************************
 */
static void falcon_setsdascl(struct efx_i2c_interface *i2c)
{
	efx_oword_t reg;

	falcon_read(i2c->efx, &reg, GPIO_CTL_REG_KER);
	EFX_SET_OWORD_FIELD(reg, GPIO0_OEN, (i2c->scl ? 0 : 1));
	EFX_SET_OWORD_FIELD(reg, GPIO3_OEN, (i2c->sda ? 0 : 1));
	falcon_write(i2c->efx, &reg, GPIO_CTL_REG_KER);
}

static int falcon_getsda(struct efx_i2c_interface *i2c)
{
	efx_oword_t reg;

	falcon_read(i2c->efx, &reg, GPIO_CTL_REG_KER);
	return EFX_OWORD_FIELD(reg, GPIO3_IN);
}

static int falcon_getscl(struct efx_i2c_interface *i2c)
{
	efx_oword_t reg;

	falcon_read(i2c->efx, &reg, GPIO_CTL_REG_KER);
	return EFX_DWORD_FIELD(reg, GPIO0_IN);
}

static struct efx_i2c_bit_operations falcon_i2c_bit_operations = {
	.setsda		= falcon_setsdascl,
	.setscl		= falcon_setsdascl,
	.getsda		= falcon_getsda,
	.getscl		= falcon_getscl,
	.udelay		= 100,
	.mdelay		= 10,
};

/**************************************************************************
 *
 * Falcon special buffer handling
 * Special buffers are used for event queues and the TX and RX
 * descriptor rings.
 *
 *************************************************************************/

/*
 * Initialise a Falcon special buffer
 *
 * This will define a buffer (previously allocated via
 * falcon_alloc_special_buffer()) in Falcon's buffer table, allowing
 * it to be used for event queues, descriptor rings etc.
 */
static int
falcon_init_special_buffer(struct efx_nic *efx,
			   struct efx_special_buffer *buffer)
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
		EFX_POPULATE_QWORD_4(buf_desc,
				     IP_DAT_BUF_SIZE, IP_DAT_BUF_SIZE_4K,
				     BUF_ADR_REGION, 0,
				     BUF_ADR_FBUF, (dma_addr >> 12),
				     BUF_OWNER_ID_FBUF, 0);
		falcon_write_sram(efx, &buf_desc, index);
	}

	return 0;
}

/* Unmaps a buffer from Falcon and clears the buffer table entries */
static void
falcon_fini_special_buffer(struct efx_nic *efx,
			   struct efx_special_buffer *buffer)
{
	efx_oword_t buf_tbl_upd;
	unsigned int start = buffer->index;
	unsigned int end = (buffer->index + buffer->entries - 1);

	if (!buffer->entries)
		return;

	EFX_LOG(efx, "unmapping special buffers %d-%d\n",
		buffer->index, buffer->index + buffer->entries - 1);

	EFX_POPULATE_OWORD_4(buf_tbl_upd,
			     BUF_UPD_CMD, 0,
			     BUF_CLR_CMD, 1,
			     BUF_CLR_END_ID, end,
			     BUF_CLR_START_ID, start);
	falcon_write(efx, &buf_tbl_upd, BUF_TBL_UPD_REG_KER);
}

/*
 * Allocate a new Falcon special buffer
 *
 * This allocates memory for a new buffer, clears it and allocates a
 * new buffer ID range.  It does not write into Falcon's buffer table.
 *
 * This call will allocate 4KB buffers, since Falcon can't use 8KB
 * buffers for event queues and descriptor rings.
 */
static int falcon_alloc_special_buffer(struct efx_nic *efx,
				       struct efx_special_buffer *buffer,
				       unsigned int len)
{
	struct falcon_nic_data *nic_data = efx->nic_data;

	len = ALIGN(len, FALCON_BUF_SIZE);

	buffer->addr = pci_alloc_consistent(efx->pci_dev, len,
					    &buffer->dma_addr);
	if (!buffer->addr)
		return -ENOMEM;
	buffer->len = len;
	buffer->entries = len / FALCON_BUF_SIZE;
	BUG_ON(buffer->dma_addr & (FALCON_BUF_SIZE - 1));

	/* All zeros is a potentially valid event so memset to 0xff */
	memset(buffer->addr, 0xff, len);

	/* Select new buffer ID */
	buffer->index = nic_data->next_buffer_table;
	nic_data->next_buffer_table += buffer->entries;

	EFX_LOG(efx, "allocating special buffers %d-%d at %llx+%x "
		"(virt %p phys %lx)\n", buffer->index,
		buffer->index + buffer->entries - 1,
		(unsigned long long)buffer->dma_addr, len,
		buffer->addr, virt_to_phys(buffer->addr));

	return 0;
}

static void falcon_free_special_buffer(struct efx_nic *efx,
				       struct efx_special_buffer *buffer)
{
	if (!buffer->addr)
		return;

	EFX_LOG(efx, "deallocating special buffers %d-%d at %llx+%x "
		"(virt %p phys %lx)\n", buffer->index,
		buffer->index + buffer->entries - 1,
		(unsigned long long)buffer->dma_addr, buffer->len,
		buffer->addr, virt_to_phys(buffer->addr));

	pci_free_consistent(efx->pci_dev, buffer->len, buffer->addr,
			    buffer->dma_addr);
	buffer->addr = NULL;
	buffer->entries = 0;
}

/**************************************************************************
 *
 * Falcon generic buffer handling
 * These buffers are used for interrupt status and MAC stats
 *
 **************************************************************************/

static int falcon_alloc_buffer(struct efx_nic *efx,
			       struct efx_buffer *buffer, unsigned int len)
{
	buffer->addr = pci_alloc_consistent(efx->pci_dev, len,
					    &buffer->dma_addr);
	if (!buffer->addr)
		return -ENOMEM;
	buffer->len = len;
	memset(buffer->addr, 0, len);
	return 0;
}

static void falcon_free_buffer(struct efx_nic *efx, struct efx_buffer *buffer)
{
	if (buffer->addr) {
		pci_free_consistent(efx->pci_dev, buffer->len,
				    buffer->addr, buffer->dma_addr);
		buffer->addr = NULL;
	}
}

/**************************************************************************
 *
 * Falcon TX path
 *
 **************************************************************************/

/* Returns a pointer to the specified transmit descriptor in the TX
 * descriptor queue belonging to the specified channel.
 */
static inline efx_qword_t *falcon_tx_desc(struct efx_tx_queue *tx_queue,
					       unsigned int index)
{
	return (((efx_qword_t *) (tx_queue->txd.addr)) + index);
}

/* This writes to the TX_DESC_WPTR; write pointer for TX descriptor ring */
static inline void falcon_notify_tx_desc(struct efx_tx_queue *tx_queue)
{
	unsigned write_ptr;
	efx_dword_t reg;

	write_ptr = tx_queue->write_count & FALCON_TXD_RING_MASK;
	EFX_POPULATE_DWORD_1(reg, TX_DESC_WPTR_DWORD, write_ptr);
	falcon_writel_page(tx_queue->efx, &reg,
			   TX_DESC_UPD_REG_KER_DWORD, tx_queue->queue);
}


/* For each entry inserted into the software descriptor ring, create a
 * descriptor in the hardware TX descriptor ring (in host memory), and
 * write a doorbell.
 */
void falcon_push_buffers(struct efx_tx_queue *tx_queue)
{

	struct efx_tx_buffer *buffer;
	efx_qword_t *txd;
	unsigned write_ptr;

	BUG_ON(tx_queue->write_count == tx_queue->insert_count);

	do {
		write_ptr = tx_queue->write_count & FALCON_TXD_RING_MASK;
		buffer = &tx_queue->buffer[write_ptr];
		txd = falcon_tx_desc(tx_queue, write_ptr);
		++tx_queue->write_count;

		/* Create TX descriptor ring entry */
		EFX_POPULATE_QWORD_5(*txd,
				     TX_KER_PORT, 0,
				     TX_KER_CONT, buffer->continuation,
				     TX_KER_BYTE_CNT, buffer->len,
				     TX_KER_BUF_REGION, 0,
				     TX_KER_BUF_ADR, buffer->dma_addr);
	} while (tx_queue->write_count != tx_queue->insert_count);

	wmb(); /* Ensure descriptors are written before they are fetched */
	falcon_notify_tx_desc(tx_queue);
}

/* Allocate hardware resources for a TX queue */
int falcon_probe_tx(struct efx_tx_queue *tx_queue)
{
	struct efx_nic *efx = tx_queue->efx;
	return falcon_alloc_special_buffer(efx, &tx_queue->txd,
					   FALCON_TXD_RING_SIZE *
					   sizeof(efx_qword_t));
}

int falcon_init_tx(struct efx_tx_queue *tx_queue)
{
	efx_oword_t tx_desc_ptr;
	struct efx_nic *efx = tx_queue->efx;
	int rc;

	/* Pin TX descriptor ring */
	rc = falcon_init_special_buffer(efx, &tx_queue->txd);
	if (rc)
		return rc;

	/* Push TX descriptor ring to card */
	EFX_POPULATE_OWORD_10(tx_desc_ptr,
			      TX_DESCQ_EN, 1,
			      TX_ISCSI_DDIG_EN, 0,
			      TX_ISCSI_HDIG_EN, 0,
			      TX_DESCQ_BUF_BASE_ID, tx_queue->txd.index,
			      TX_DESCQ_EVQ_ID, tx_queue->channel->evqnum,
			      TX_DESCQ_OWNER_ID, 0,
			      TX_DESCQ_LABEL, tx_queue->queue,
			      TX_DESCQ_SIZE, FALCON_TXD_RING_ORDER,
			      TX_DESCQ_TYPE, 0,
			      TX_NON_IP_DROP_DIS_B0, 1);

	if (FALCON_REV(efx) >= FALCON_REV_B0) {
		int csum = !(efx->net_dev->features & NETIF_F_IP_CSUM);
		EFX_SET_OWORD_FIELD(tx_desc_ptr, TX_IP_CHKSM_DIS_B0, csum);
		EFX_SET_OWORD_FIELD(tx_desc_ptr, TX_TCP_CHKSM_DIS_B0, csum);
	}

	falcon_write_table(efx, &tx_desc_ptr, efx->type->txd_ptr_tbl_base,
			   tx_queue->queue);

	if (FALCON_REV(efx) < FALCON_REV_B0) {
		efx_oword_t reg;

		BUG_ON(tx_queue->queue >= 128); /* HW limit */

		falcon_read(efx, &reg, TX_CHKSM_CFG_REG_KER_A1);
		if (efx->net_dev->features & NETIF_F_IP_CSUM)
			clear_bit_le(tx_queue->queue, (void *)&reg);
		else
			set_bit_le(tx_queue->queue, (void *)&reg);
		falcon_write(efx, &reg, TX_CHKSM_CFG_REG_KER_A1);
	}

	return 0;
}

static int falcon_flush_tx_queue(struct efx_tx_queue *tx_queue)
{
	struct efx_nic *efx = tx_queue->efx;
	struct efx_channel *channel = &efx->channel[0];
	efx_oword_t tx_flush_descq;
	unsigned int read_ptr, i;

	/* Post a flush command */
	EFX_POPULATE_OWORD_2(tx_flush_descq,
			     TX_FLUSH_DESCQ_CMD, 1,
			     TX_FLUSH_DESCQ, tx_queue->queue);
	falcon_write(efx, &tx_flush_descq, TX_FLUSH_DESCQ_REG_KER);
	msleep(FALCON_FLUSH_TIMEOUT);

	if (EFX_WORKAROUND_7803(efx))
		return 0;

	/* Look for a flush completed event */
	read_ptr = channel->eventq_read_ptr;
	for (i = 0; i < FALCON_EVQ_SIZE; ++i) {
		efx_qword_t *event = falcon_event(channel, read_ptr);
		int ev_code, ev_sub_code, ev_queue;
		if (!falcon_event_present(event))
			break;

		ev_code = EFX_QWORD_FIELD(*event, EV_CODE);
		ev_sub_code = EFX_QWORD_FIELD(*event, DRIVER_EV_SUB_CODE);
		ev_queue = EFX_QWORD_FIELD(*event, DRIVER_EV_TX_DESCQ_ID);
		if ((ev_sub_code == TX_DESCQ_FLS_DONE_EV_DECODE) &&
		    (ev_queue == tx_queue->queue)) {
			EFX_LOG(efx, "tx queue %d flush command succesful\n",
				tx_queue->queue);
			return 0;
		}

		read_ptr = (read_ptr + 1) & FALCON_EVQ_MASK;
	}

	if (EFX_WORKAROUND_11557(efx)) {
		efx_oword_t reg;
		int enabled;

		falcon_read_table(efx, &reg, efx->type->txd_ptr_tbl_base,
				  tx_queue->queue);
		enabled = EFX_OWORD_FIELD(reg, TX_DESCQ_EN);
		if (!enabled) {
			EFX_LOG(efx, "tx queue %d disabled without a "
				"flush event seen\n", tx_queue->queue);
			return 0;
		}
	}

	EFX_ERR(efx, "tx queue %d flush command timed out\n", tx_queue->queue);
	return -ETIMEDOUT;
}

void falcon_fini_tx(struct efx_tx_queue *tx_queue)
{
	struct efx_nic *efx = tx_queue->efx;
	efx_oword_t tx_desc_ptr;

	/* Stop the hardware using the queue */
	if (falcon_flush_tx_queue(tx_queue))
		EFX_ERR(efx, "failed to flush tx queue %d\n", tx_queue->queue);

	/* Remove TX descriptor ring from card */
	EFX_ZERO_OWORD(tx_desc_ptr);
	falcon_write_table(efx, &tx_desc_ptr, efx->type->txd_ptr_tbl_base,
			   tx_queue->queue);

	/* Unpin TX descriptor ring */
	falcon_fini_special_buffer(efx, &tx_queue->txd);
}

/* Free buffers backing TX queue */
void falcon_remove_tx(struct efx_tx_queue *tx_queue)
{
	falcon_free_special_buffer(tx_queue->efx, &tx_queue->txd);
}

/**************************************************************************
 *
 * Falcon RX path
 *
 **************************************************************************/

/* Returns a pointer to the specified descriptor in the RX descriptor queue */
static inline efx_qword_t *falcon_rx_desc(struct efx_rx_queue *rx_queue,
					       unsigned int index)
{
	return (((efx_qword_t *) (rx_queue->rxd.addr)) + index);
}

/* This creates an entry in the RX descriptor queue */
static inline void falcon_build_rx_desc(struct efx_rx_queue *rx_queue,
					unsigned index)
{
	struct efx_rx_buffer *rx_buf;
	efx_qword_t *rxd;

	rxd = falcon_rx_desc(rx_queue, index);
	rx_buf = efx_rx_buffer(rx_queue, index);
	EFX_POPULATE_QWORD_3(*rxd,
			     RX_KER_BUF_SIZE,
			     rx_buf->len -
			     rx_queue->efx->type->rx_buffer_padding,
			     RX_KER_BUF_REGION, 0,
			     RX_KER_BUF_ADR, rx_buf->dma_addr);
}

/* This writes to the RX_DESC_WPTR register for the specified receive
 * descriptor ring.
 */
void falcon_notify_rx_desc(struct efx_rx_queue *rx_queue)
{
	efx_dword_t reg;
	unsigned write_ptr;

	while (rx_queue->notified_count != rx_queue->added_count) {
		falcon_build_rx_desc(rx_queue,
				     rx_queue->notified_count &
				     FALCON_RXD_RING_MASK);
		++rx_queue->notified_count;
	}

	wmb();
	write_ptr = rx_queue->added_count & FALCON_RXD_RING_MASK;
	EFX_POPULATE_DWORD_1(reg, RX_DESC_WPTR_DWORD, write_ptr);
	falcon_writel_page(rx_queue->efx, &reg,
			   RX_DESC_UPD_REG_KER_DWORD, rx_queue->queue);
}

int falcon_probe_rx(struct efx_rx_queue *rx_queue)
{
	struct efx_nic *efx = rx_queue->efx;
	return falcon_alloc_special_buffer(efx, &rx_queue->rxd,
					   FALCON_RXD_RING_SIZE *
					   sizeof(efx_qword_t));
}

int falcon_init_rx(struct efx_rx_queue *rx_queue)
{
	efx_oword_t rx_desc_ptr;
	struct efx_nic *efx = rx_queue->efx;
	int rc;
	int is_b0 = FALCON_REV(efx) >= FALCON_REV_B0;
	int iscsi_digest_en = is_b0;

	EFX_LOG(efx, "RX queue %d ring in special buffers %d-%d\n",
		rx_queue->queue, rx_queue->rxd.index,
		rx_queue->rxd.index + rx_queue->rxd.entries - 1);

	/* Pin RX descriptor ring */
	rc = falcon_init_special_buffer(efx, &rx_queue->rxd);
	if (rc)
		return rc;

	/* Push RX descriptor ring to card */
	EFX_POPULATE_OWORD_10(rx_desc_ptr,
			      RX_ISCSI_DDIG_EN, iscsi_digest_en,
			      RX_ISCSI_HDIG_EN, iscsi_digest_en,
			      RX_DESCQ_BUF_BASE_ID, rx_queue->rxd.index,
			      RX_DESCQ_EVQ_ID, rx_queue->channel->evqnum,
			      RX_DESCQ_OWNER_ID, 0,
			      RX_DESCQ_LABEL, rx_queue->queue,
			      RX_DESCQ_SIZE, FALCON_RXD_RING_ORDER,
			      RX_DESCQ_TYPE, 0 /* kernel queue */ ,
			      /* For >=B0 this is scatter so disable */
			      RX_DESCQ_JUMBO, !is_b0,
			      RX_DESCQ_EN, 1);
	falcon_write_table(efx, &rx_desc_ptr, efx->type->rxd_ptr_tbl_base,
			   rx_queue->queue);
	return 0;
}

static int falcon_flush_rx_queue(struct efx_rx_queue *rx_queue)
{
	struct efx_nic *efx = rx_queue->efx;
	struct efx_channel *channel = &efx->channel[0];
	unsigned int read_ptr, i;
	efx_oword_t rx_flush_descq;

	/* Post a flush command */
	EFX_POPULATE_OWORD_2(rx_flush_descq,
			     RX_FLUSH_DESCQ_CMD, 1,
			     RX_FLUSH_DESCQ, rx_queue->queue);
	falcon_write(efx, &rx_flush_descq, RX_FLUSH_DESCQ_REG_KER);
	msleep(FALCON_FLUSH_TIMEOUT);

	if (EFX_WORKAROUND_7803(efx))
		return 0;

	/* Look for a flush completed event */
	read_ptr = channel->eventq_read_ptr;
	for (i = 0; i < FALCON_EVQ_SIZE; ++i) {
		efx_qword_t *event = falcon_event(channel, read_ptr);
		int ev_code, ev_sub_code, ev_queue, ev_failed;
		if (!falcon_event_present(event))
			break;

		ev_code = EFX_QWORD_FIELD(*event, EV_CODE);
		ev_sub_code = EFX_QWORD_FIELD(*event, DRIVER_EV_SUB_CODE);
		ev_queue = EFX_QWORD_FIELD(*event, DRIVER_EV_RX_DESCQ_ID);
		ev_failed = EFX_QWORD_FIELD(*event, DRIVER_EV_RX_FLUSH_FAIL);

		if ((ev_sub_code == RX_DESCQ_FLS_DONE_EV_DECODE) &&
		    (ev_queue == rx_queue->queue)) {
			if (ev_failed) {
				EFX_INFO(efx, "rx queue %d flush command "
					 "failed\n", rx_queue->queue);
				return -EAGAIN;
			} else {
				EFX_LOG(efx, "rx queue %d flush command "
					"succesful\n", rx_queue->queue);
				return 0;
			}
		}

		read_ptr = (read_ptr + 1) & FALCON_EVQ_MASK;
	}

	if (EFX_WORKAROUND_11557(efx)) {
		efx_oword_t reg;
		int enabled;

		falcon_read_table(efx, &reg, efx->type->rxd_ptr_tbl_base,
				  rx_queue->queue);
		enabled = EFX_OWORD_FIELD(reg, RX_DESCQ_EN);
		if (!enabled) {
			EFX_LOG(efx, "rx queue %d disabled without a "
				"flush event seen\n", rx_queue->queue);
			return 0;
		}
	}

	EFX_ERR(efx, "rx queue %d flush command timed out\n", rx_queue->queue);
	return -ETIMEDOUT;
}

void falcon_fini_rx(struct efx_rx_queue *rx_queue)
{
	efx_oword_t rx_desc_ptr;
	struct efx_nic *efx = rx_queue->efx;
	int i, rc;

	/* Try and flush the rx queue. This may need to be repeated */
	for (i = 0; i < 5; i++) {
		rc = falcon_flush_rx_queue(rx_queue);
		if (rc == -EAGAIN)
			continue;
		break;
	}
	if (rc)
		EFX_ERR(efx, "failed to flush rx queue %d\n", rx_queue->queue);

	/* Remove RX descriptor ring from card */
	EFX_ZERO_OWORD(rx_desc_ptr);
	falcon_write_table(efx, &rx_desc_ptr, efx->type->rxd_ptr_tbl_base,
			   rx_queue->queue);

	/* Unpin RX descriptor ring */
	falcon_fini_special_buffer(efx, &rx_queue->rxd);
}

/* Free buffers backing RX queue */
void falcon_remove_rx(struct efx_rx_queue *rx_queue)
{
	falcon_free_special_buffer(rx_queue->efx, &rx_queue->rxd);
}

/**************************************************************************
 *
 * Falcon event queue processing
 * Event queues are processed by per-channel tasklets.
 *
 **************************************************************************/

/* Update a channel's event queue's read pointer (RPTR) register
 *
 * This writes the EVQ_RPTR_REG register for the specified channel's
 * event queue.
 *
 * Note that EVQ_RPTR_REG contains the index of the "last read" event,
 * whereas channel->eventq_read_ptr contains the index of the "next to
 * read" event.
 */
void falcon_eventq_read_ack(struct efx_channel *channel)
{
	efx_dword_t reg;
	struct efx_nic *efx = channel->efx;

	EFX_POPULATE_DWORD_1(reg, EVQ_RPTR_DWORD, channel->eventq_read_ptr);
	falcon_writel_table(efx, &reg, efx->type->evq_rptr_tbl_base,
			    channel->evqnum);
}

/* Use HW to insert a SW defined event */
void falcon_generate_event(struct efx_channel *channel, efx_qword_t *event)
{
	efx_oword_t drv_ev_reg;

	EFX_POPULATE_OWORD_2(drv_ev_reg,
			     DRV_EV_QID, channel->evqnum,
			     DRV_EV_DATA,
			     EFX_QWORD_FIELD64(*event, WHOLE_EVENT));
	falcon_write(channel->efx, &drv_ev_reg, DRV_EV_REG_KER);
}

/* Handle a transmit completion event
 *
 * Falcon batches TX completion events; the message we receive is of
 * the form "complete all TX events up to this index".
 */
static inline void falcon_handle_tx_event(struct efx_channel *channel,
					  efx_qword_t *event)
{
	unsigned int tx_ev_desc_ptr;
	unsigned int tx_ev_q_label;
	struct efx_tx_queue *tx_queue;
	struct efx_nic *efx = channel->efx;

	if (likely(EFX_QWORD_FIELD(*event, TX_EV_COMP))) {
		/* Transmit completion */
		tx_ev_desc_ptr = EFX_QWORD_FIELD(*event, TX_EV_DESC_PTR);
		tx_ev_q_label = EFX_QWORD_FIELD(*event, TX_EV_Q_LABEL);
		tx_queue = &efx->tx_queue[tx_ev_q_label];
		efx_xmit_done(tx_queue, tx_ev_desc_ptr);
	} else if (EFX_QWORD_FIELD(*event, TX_EV_WQ_FF_FULL)) {
		/* Rewrite the FIFO write pointer */
		tx_ev_q_label = EFX_QWORD_FIELD(*event, TX_EV_Q_LABEL);
		tx_queue = &efx->tx_queue[tx_ev_q_label];

		if (NET_DEV_REGISTERED(efx))
			netif_tx_lock(efx->net_dev);
		falcon_notify_tx_desc(tx_queue);
		if (NET_DEV_REGISTERED(efx))
			netif_tx_unlock(efx->net_dev);
	} else if (EFX_QWORD_FIELD(*event, TX_EV_PKT_ERR) &&
		   EFX_WORKAROUND_10727(efx)) {
		efx_schedule_reset(efx, RESET_TYPE_TX_DESC_FETCH);
	} else {
		EFX_ERR(efx, "channel %d unexpected TX event "
			EFX_QWORD_FMT"\n", channel->channel,
			EFX_QWORD_VAL(*event));
	}
}

/* Check received packet's destination MAC address. */
static int check_dest_mac(struct efx_rx_queue *rx_queue,
			  const efx_qword_t *event)
{
	struct efx_rx_buffer *rx_buf;
	struct efx_nic *efx = rx_queue->efx;
	int rx_ev_desc_ptr;
	struct ethhdr *eh;

	if (efx->promiscuous)
		return 1;

	rx_ev_desc_ptr = EFX_QWORD_FIELD(*event, RX_EV_DESC_PTR);
	rx_buf = efx_rx_buffer(rx_queue, rx_ev_desc_ptr);
	eh = (struct ethhdr *)rx_buf->data;
	if (memcmp(eh->h_dest, efx->net_dev->dev_addr, ETH_ALEN))
		return 0;
	return 1;
}

/* Detect errors included in the rx_evt_pkt_ok bit. */
static void falcon_handle_rx_not_ok(struct efx_rx_queue *rx_queue,
				    const efx_qword_t *event,
				    unsigned *rx_ev_pkt_ok,
				    int *discard, int byte_count)
{
	struct efx_nic *efx = rx_queue->efx;
	unsigned rx_ev_buf_owner_id_err, rx_ev_ip_hdr_chksum_err;
	unsigned rx_ev_tcp_udp_chksum_err, rx_ev_eth_crc_err;
	unsigned rx_ev_frm_trunc, rx_ev_drib_nib, rx_ev_tobe_disc;
	unsigned rx_ev_pkt_type, rx_ev_other_err, rx_ev_pause_frm;
	unsigned rx_ev_ip_frag_err, rx_ev_hdr_type, rx_ev_mcast_pkt;
	int snap, non_ip;

	rx_ev_hdr_type = EFX_QWORD_FIELD(*event, RX_EV_HDR_TYPE);
	rx_ev_mcast_pkt = EFX_QWORD_FIELD(*event, RX_EV_MCAST_PKT);
	rx_ev_tobe_disc = EFX_QWORD_FIELD(*event, RX_EV_TOBE_DISC);
	rx_ev_pkt_type = EFX_QWORD_FIELD(*event, RX_EV_PKT_TYPE);
	rx_ev_buf_owner_id_err = EFX_QWORD_FIELD(*event,
						 RX_EV_BUF_OWNER_ID_ERR);
	rx_ev_ip_frag_err = EFX_QWORD_FIELD(*event, RX_EV_IF_FRAG_ERR);
	rx_ev_ip_hdr_chksum_err = EFX_QWORD_FIELD(*event,
						  RX_EV_IP_HDR_CHKSUM_ERR);
	rx_ev_tcp_udp_chksum_err = EFX_QWORD_FIELD(*event,
						   RX_EV_TCP_UDP_CHKSUM_ERR);
	rx_ev_eth_crc_err = EFX_QWORD_FIELD(*event, RX_EV_ETH_CRC_ERR);
	rx_ev_frm_trunc = EFX_QWORD_FIELD(*event, RX_EV_FRM_TRUNC);
	rx_ev_drib_nib = ((FALCON_REV(efx) >= FALCON_REV_B0) ?
			  0 : EFX_QWORD_FIELD(*event, RX_EV_DRIB_NIB));
	rx_ev_pause_frm = EFX_QWORD_FIELD(*event, RX_EV_PAUSE_FRM_ERR);

	/* Every error apart from tobe_disc and pause_frm */
	rx_ev_other_err = (rx_ev_drib_nib | rx_ev_tcp_udp_chksum_err |
			   rx_ev_buf_owner_id_err | rx_ev_eth_crc_err |
			   rx_ev_frm_trunc | rx_ev_ip_hdr_chksum_err);

	snap = (rx_ev_pkt_type == RX_EV_PKT_TYPE_LLC_DECODE) ||
		(rx_ev_pkt_type == RX_EV_PKT_TYPE_VLAN_LLC_DECODE);
	non_ip = (rx_ev_hdr_type == RX_EV_HDR_TYPE_NON_IP_DECODE);

	/* SFC bug 5475/8970: The Falcon XMAC incorrectly calculates the
	 * length field of an LLC frame, which sets TOBE_DISC. We could set
	 * PASS_LEN_ERR, but we want the MAC to filter out short frames (to
	 * protect the RX block).
	 *
	 * bug5475 - LLC/SNAP: Falcon identifies SNAP packets.
	 * bug8970 - LLC/noSNAP: Falcon does not provide an LLC flag.
	 *                       LLC can't encapsulate IP, so by definition
	 *                       these packets are NON_IP.
	 *
	 * Unicast mismatch will also cause TOBE_DISC, so the driver needs
	 * to check this.
	 */
	if (EFX_WORKAROUND_5475(efx) && rx_ev_tobe_disc && (snap || non_ip)) {
		/* If all the other flags are zero then we can state the
		 * entire packet is ok, which will flag to the kernel not
		 * to recalculate checksums.
		 */
		if (!(non_ip | rx_ev_other_err | rx_ev_pause_frm))
			*rx_ev_pkt_ok = 1;

		rx_ev_tobe_disc = 0;

		/* TOBE_DISC is set for unicast mismatch.  But given that
		 * we can't trust TOBE_DISC here, we must validate the dest
		 * MAC address ourselves.
		 */
		if (!rx_ev_mcast_pkt && !check_dest_mac(rx_queue, event))
			rx_ev_tobe_disc = 1;
	}

	/* Count errors that are not in MAC stats. */
	if (rx_ev_frm_trunc)
		++rx_queue->channel->n_rx_frm_trunc;
	else if (rx_ev_tobe_disc)
		++rx_queue->channel->n_rx_tobe_disc;
	else if (rx_ev_ip_hdr_chksum_err)
		++rx_queue->channel->n_rx_ip_hdr_chksum_err;
	else if (rx_ev_tcp_udp_chksum_err)
		++rx_queue->channel->n_rx_tcp_udp_chksum_err;
	if (rx_ev_ip_frag_err)
		++rx_queue->channel->n_rx_ip_frag_err;

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
			    EFX_QWORD_FMT "%s%s%s%s%s%s%s%s%s\n",
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
			    rx_ev_pause_frm ? " [PAUSE]" : "",
			    snap ? " [SNAP/LLC]" : "");
	}
#endif

	if (unlikely(rx_ev_eth_crc_err && EFX_WORKAROUND_10750(efx) &&
		     efx->phy_type == PHY_TYPE_10XPRESS))
		tenxpress_crc_err(efx);
}

/* Handle receive events that are not in-order. */
static void falcon_handle_rx_bad_index(struct efx_rx_queue *rx_queue,
				       unsigned index)
{
	struct efx_nic *efx = rx_queue->efx;
	unsigned expected, dropped;

	expected = rx_queue->removed_count & FALCON_RXD_RING_MASK;
	dropped = ((index + FALCON_RXD_RING_SIZE - expected) &
		   FALCON_RXD_RING_MASK);
	EFX_INFO(efx, "dropped %d events (index=%d expected=%d)\n",
		dropped, index, expected);

	efx_schedule_reset(efx, EFX_WORKAROUND_5676(efx) ?
			   RESET_TYPE_RX_RECOVERY : RESET_TYPE_DISABLE);
}

/* Handle a packet received event
 *
 * Falcon silicon gives a "discard" flag if it's a unicast packet with the
 * wrong destination address
 * Also "is multicast" and "matches multicast filter" flags can be used to
 * discard non-matching multicast packets.
 */
static inline int falcon_handle_rx_event(struct efx_channel *channel,
					 const efx_qword_t *event)
{
	unsigned int rx_ev_q_label, rx_ev_desc_ptr, rx_ev_byte_cnt;
	unsigned int rx_ev_pkt_ok, rx_ev_hdr_type, rx_ev_mcast_pkt;
	unsigned expected_ptr;
	int discard = 0, checksummed;
	struct efx_rx_queue *rx_queue;
	struct efx_nic *efx = channel->efx;

	/* Basic packet information */
	rx_ev_byte_cnt = EFX_QWORD_FIELD(*event, RX_EV_BYTE_CNT);
	rx_ev_pkt_ok = EFX_QWORD_FIELD(*event, RX_EV_PKT_OK);
	rx_ev_hdr_type = EFX_QWORD_FIELD(*event, RX_EV_HDR_TYPE);
	WARN_ON(EFX_QWORD_FIELD(*event, RX_EV_JUMBO_CONT));
	WARN_ON(EFX_QWORD_FIELD(*event, RX_EV_SOP) != 1);

	rx_ev_q_label = EFX_QWORD_FIELD(*event, RX_EV_Q_LABEL);
	rx_queue = &efx->rx_queue[rx_ev_q_label];

	rx_ev_desc_ptr = EFX_QWORD_FIELD(*event, RX_EV_DESC_PTR);
	expected_ptr = rx_queue->removed_count & FALCON_RXD_RING_MASK;
	if (unlikely(rx_ev_desc_ptr != expected_ptr)) {
		falcon_handle_rx_bad_index(rx_queue, rx_ev_desc_ptr);
		return rx_ev_q_label;
	}

	if (likely(rx_ev_pkt_ok)) {
		/* If packet is marked as OK and packet type is TCP/IPv4 or
		 * UDP/IPv4, then we can rely on the hardware checksum.
		 */
		checksummed = RX_EV_HDR_TYPE_HAS_CHECKSUMS(rx_ev_hdr_type);
	} else {
		falcon_handle_rx_not_ok(rx_queue, event, &rx_ev_pkt_ok,
					&discard, rx_ev_byte_cnt);
		checksummed = 0;
	}

	/* Detect multicast packets that didn't match the filter */
	rx_ev_mcast_pkt = EFX_QWORD_FIELD(*event, RX_EV_MCAST_PKT);
	if (rx_ev_mcast_pkt) {
		unsigned int rx_ev_mcast_hash_match =
			EFX_QWORD_FIELD(*event, RX_EV_MCAST_HASH_MATCH);

		if (unlikely(!rx_ev_mcast_hash_match))
			discard = 1;
	}

	/* Handle received packet */
	efx_rx_packet(rx_queue, rx_ev_desc_ptr, rx_ev_byte_cnt,
		      checksummed, discard);

	return rx_ev_q_label;
}

/* Global events are basically PHY events */
static void falcon_handle_global_event(struct efx_channel *channel,
				       efx_qword_t *event)
{
	struct efx_nic *efx = channel->efx;
	int is_phy_event = 0, handled = 0;

	/* Check for interrupt on either port.  Some boards have a
	 * single PHY wired to the interrupt line for port 1. */
	if (EFX_QWORD_FIELD(*event, G_PHY0_INTR) ||
	    EFX_QWORD_FIELD(*event, G_PHY1_INTR) ||
	    EFX_QWORD_FIELD(*event, XG_PHY_INTR))
		is_phy_event = 1;

	if ((FALCON_REV(efx) >= FALCON_REV_B0) &&
	    EFX_OWORD_FIELD(*event, XG_MNT_INTR_B0))
		is_phy_event = 1;

	if (is_phy_event) {
		efx->phy_op->clear_interrupt(efx);
		queue_work(efx->workqueue, &efx->reconfigure_work);
		handled = 1;
	}

	if (EFX_QWORD_FIELD_VER(efx, *event, RX_RECOVERY)) {
		EFX_ERR(efx, "channel %d seen global RX_RESET "
			"event. Resetting.\n", channel->channel);

		atomic_inc(&efx->rx_reset);
		efx_schedule_reset(efx, EFX_WORKAROUND_6555(efx) ?
				   RESET_TYPE_RX_RECOVERY : RESET_TYPE_DISABLE);
		handled = 1;
	}

	if (!handled)
		EFX_ERR(efx, "channel %d unknown global event "
			EFX_QWORD_FMT "\n", channel->channel,
			EFX_QWORD_VAL(*event));
}

static void falcon_handle_driver_event(struct efx_channel *channel,
				       efx_qword_t *event)
{
	struct efx_nic *efx = channel->efx;
	unsigned int ev_sub_code;
	unsigned int ev_sub_data;

	ev_sub_code = EFX_QWORD_FIELD(*event, DRIVER_EV_SUB_CODE);
	ev_sub_data = EFX_QWORD_FIELD(*event, DRIVER_EV_SUB_DATA);

	switch (ev_sub_code) {
	case TX_DESCQ_FLS_DONE_EV_DECODE:
		EFX_TRACE(efx, "channel %d TXQ %d flushed\n",
			  channel->channel, ev_sub_data);
		break;
	case RX_DESCQ_FLS_DONE_EV_DECODE:
		EFX_TRACE(efx, "channel %d RXQ %d flushed\n",
			  channel->channel, ev_sub_data);
		break;
	case EVQ_INIT_DONE_EV_DECODE:
		EFX_LOG(efx, "channel %d EVQ %d initialised\n",
			channel->channel, ev_sub_data);
		break;
	case SRM_UPD_DONE_EV_DECODE:
		EFX_TRACE(efx, "channel %d SRAM update done\n",
			  channel->channel);
		break;
	case WAKE_UP_EV_DECODE:
		EFX_TRACE(efx, "channel %d RXQ %d wakeup event\n",
			  channel->channel, ev_sub_data);
		break;
	case TIMER_EV_DECODE:
		EFX_TRACE(efx, "channel %d RX queue %d timer expired\n",
			  channel->channel, ev_sub_data);
		break;
	case RX_RECOVERY_EV_DECODE:
		EFX_ERR(efx, "channel %d seen DRIVER RX_RESET event. "
			"Resetting.\n", channel->channel);
		efx_schedule_reset(efx,
				   EFX_WORKAROUND_6555(efx) ?
				   RESET_TYPE_RX_RECOVERY :
				   RESET_TYPE_DISABLE);
		break;
	case RX_DSC_ERROR_EV_DECODE:
		EFX_ERR(efx, "RX DMA Q %d reports descriptor fetch error."
			" RX Q %d is disabled.\n", ev_sub_data, ev_sub_data);
		efx_schedule_reset(efx, RESET_TYPE_RX_DESC_FETCH);
		break;
	case TX_DSC_ERROR_EV_DECODE:
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

int falcon_process_eventq(struct efx_channel *channel, int *rx_quota)
{
	unsigned int read_ptr;
	efx_qword_t event, *p_event;
	int ev_code;
	int rxq;
	int rxdmaqs = 0;

	read_ptr = channel->eventq_read_ptr;

	do {
		p_event = falcon_event(channel, read_ptr);
		event = *p_event;

		if (!falcon_event_present(&event))
			/* End of events */
			break;

		EFX_TRACE(channel->efx, "channel %d event is "EFX_QWORD_FMT"\n",
			  channel->channel, EFX_QWORD_VAL(event));

		/* Clear this event by marking it all ones */
		EFX_SET_QWORD(*p_event);

		ev_code = EFX_QWORD_FIELD(event, EV_CODE);

		switch (ev_code) {
		case RX_IP_EV_DECODE:
			rxq = falcon_handle_rx_event(channel, &event);
			rxdmaqs |= (1 << rxq);
			(*rx_quota)--;
			break;
		case TX_IP_EV_DECODE:
			falcon_handle_tx_event(channel, &event);
			break;
		case DRV_GEN_EV_DECODE:
			channel->eventq_magic
				= EFX_QWORD_FIELD(event, EVQ_MAGIC);
			EFX_LOG(channel->efx, "channel %d received generated "
				"event "EFX_QWORD_FMT"\n", channel->channel,
				EFX_QWORD_VAL(event));
			break;
		case GLOBAL_EV_DECODE:
			falcon_handle_global_event(channel, &event);
			break;
		case DRIVER_EV_DECODE:
			falcon_handle_driver_event(channel, &event);
			break;
		default:
			EFX_ERR(channel->efx, "channel %d unknown event type %d"
				" (data " EFX_QWORD_FMT ")\n", channel->channel,
				ev_code, EFX_QWORD_VAL(event));
		}

		/* Increment read pointer */
		read_ptr = (read_ptr + 1) & FALCON_EVQ_MASK;

	} while (*rx_quota);

	channel->eventq_read_ptr = read_ptr;
	return rxdmaqs;
}

void falcon_set_int_moderation(struct efx_channel *channel)
{
	efx_dword_t timer_cmd;
	struct efx_nic *efx = channel->efx;

	/* Set timer register */
	if (channel->irq_moderation) {
		/* Round to resolution supported by hardware.  The value we
		 * program is based at 0.  So actual interrupt moderation
		 * achieved is ((x + 1) * res).
		 */
		unsigned int res = 5;
		channel->irq_moderation -= (channel->irq_moderation % res);
		if (channel->irq_moderation < res)
			channel->irq_moderation = res;
		EFX_POPULATE_DWORD_2(timer_cmd,
				     TIMER_MODE, TIMER_MODE_INT_HLDOFF,
				     TIMER_VAL,
				     (channel->irq_moderation / res) - 1);
	} else {
		EFX_POPULATE_DWORD_2(timer_cmd,
				     TIMER_MODE, TIMER_MODE_DIS,
				     TIMER_VAL, 0);
	}
	falcon_writel_page_locked(efx, &timer_cmd, TIMER_CMD_REG_KER,
				  channel->evqnum);

}

/* Allocate buffer table entries for event queue */
int falcon_probe_eventq(struct efx_channel *channel)
{
	struct efx_nic *efx = channel->efx;
	unsigned int evq_size;

	evq_size = FALCON_EVQ_SIZE * sizeof(efx_qword_t);
	return falcon_alloc_special_buffer(efx, &channel->eventq, evq_size);
}

int falcon_init_eventq(struct efx_channel *channel)
{
	efx_oword_t evq_ptr;
	struct efx_nic *efx = channel->efx;
	int rc;

	EFX_LOG(efx, "channel %d event queue in special buffers %d-%d\n",
		channel->channel, channel->eventq.index,
		channel->eventq.index + channel->eventq.entries - 1);

	/* Pin event queue buffer */
	rc = falcon_init_special_buffer(efx, &channel->eventq);
	if (rc)
		return rc;

	/* Fill event queue with all ones (i.e. empty events) */
	memset(channel->eventq.addr, 0xff, channel->eventq.len);

	/* Push event queue to card */
	EFX_POPULATE_OWORD_3(evq_ptr,
			     EVQ_EN, 1,
			     EVQ_SIZE, FALCON_EVQ_ORDER,
			     EVQ_BUF_BASE_ID, channel->eventq.index);
	falcon_write_table(efx, &evq_ptr, efx->type->evq_ptr_tbl_base,
			   channel->evqnum);

	falcon_set_int_moderation(channel);

	return 0;
}

void falcon_fini_eventq(struct efx_channel *channel)
{
	efx_oword_t eventq_ptr;
	struct efx_nic *efx = channel->efx;

	/* Remove event queue from card */
	EFX_ZERO_OWORD(eventq_ptr);
	falcon_write_table(efx, &eventq_ptr, efx->type->evq_ptr_tbl_base,
			   channel->evqnum);

	/* Unpin event queue */
	falcon_fini_special_buffer(efx, &channel->eventq);
}

/* Free buffers backing event queue */
void falcon_remove_eventq(struct efx_channel *channel)
{
	falcon_free_special_buffer(channel->efx, &channel->eventq);
}


/* Generates a test event on the event queue.  A subsequent call to
 * process_eventq() should pick up the event and place the value of
 * "magic" into channel->eventq_magic;
 */
void falcon_generate_test_event(struct efx_channel *channel, unsigned int magic)
{
	efx_qword_t test_event;

	EFX_POPULATE_QWORD_2(test_event,
			     EV_CODE, DRV_GEN_EV_DECODE,
			     EVQ_MAGIC, magic);
	falcon_generate_event(channel, &test_event);
}


/**************************************************************************
 *
 * Falcon hardware interrupts
 * The hardware interrupt handler does very little work; all the event
 * queue processing is carried out by per-channel tasklets.
 *
 **************************************************************************/

/* Enable/disable/generate Falcon interrupts */
static inline void falcon_interrupts(struct efx_nic *efx, int enabled,
				     int force)
{
	efx_oword_t int_en_reg_ker;

	EFX_POPULATE_OWORD_2(int_en_reg_ker,
			     KER_INT_KER, force,
			     DRV_INT_EN_KER, enabled);
	falcon_write(efx, &int_en_reg_ker, INT_EN_REG_KER);
}

void falcon_enable_interrupts(struct efx_nic *efx)
{
	efx_oword_t int_adr_reg_ker;
	struct efx_channel *channel;

	EFX_ZERO_OWORD(*((efx_oword_t *) efx->irq_status.addr));
	wmb(); /* Ensure interrupt vector is clear before interrupts enabled */

	/* Program address */
	EFX_POPULATE_OWORD_2(int_adr_reg_ker,
			     NORM_INT_VEC_DIS_KER, EFX_INT_MODE_USE_MSI(efx),
			     INT_ADR_KER, efx->irq_status.dma_addr);
	falcon_write(efx, &int_adr_reg_ker, INT_ADR_REG_KER);

	/* Enable interrupts */
	falcon_interrupts(efx, 1, 0);

	/* Force processing of all the channels to get the EVQ RPTRs up to
	   date */
	efx_for_each_channel_with_interrupt(channel, efx)
		efx_schedule_channel(channel);
}

void falcon_disable_interrupts(struct efx_nic *efx)
{
	/* Disable interrupts */
	falcon_interrupts(efx, 0, 0);
}

/* Generate a Falcon test interrupt
 * Interrupt must already have been enabled, otherwise nasty things
 * may happen.
 */
void falcon_generate_interrupt(struct efx_nic *efx)
{
	falcon_interrupts(efx, 1, 1);
}

/* Acknowledge a legacy interrupt from Falcon
 *
 * This acknowledges a legacy (not MSI) interrupt via INT_ACK_KER_REG.
 *
 * Due to SFC bug 3706 (silicon revision <=A1) reads can be duplicated in the
 * BIU. Interrupt acknowledge is read sensitive so must write instead
 * (then read to ensure the BIU collector is flushed)
 *
 * NB most hardware supports MSI interrupts
 */
static inline void falcon_irq_ack_a1(struct efx_nic *efx)
{
	efx_dword_t reg;

	EFX_POPULATE_DWORD_1(reg, INT_ACK_DUMMY_DATA, 0xb7eb7e);
	falcon_writel(efx, &reg, INT_ACK_REG_KER_A1);
	falcon_readl(efx, &reg, WORK_AROUND_BROKEN_PCI_READS_REG_KER_A1);
}

/* Process a fatal interrupt
 * Disable bus mastering ASAP and schedule a reset
 */
static irqreturn_t falcon_fatal_interrupt(struct efx_nic *efx)
{
	struct falcon_nic_data *nic_data = efx->nic_data;
	efx_oword_t *int_ker = (efx_oword_t *) efx->irq_status.addr;
	efx_oword_t fatal_intr;
	int error, mem_perr;
	static int n_int_errors;

	falcon_read(efx, &fatal_intr, FATAL_INTR_REG_KER);
	error = EFX_OWORD_FIELD(fatal_intr, INT_KER_ERROR);

	EFX_ERR(efx, "SYSTEM ERROR " EFX_OWORD_FMT " status "
		EFX_OWORD_FMT ": %s\n", EFX_OWORD_VAL(*int_ker),
		EFX_OWORD_VAL(fatal_intr),
		error ? "disabling bus mastering" : "no recognised error");
	if (error == 0)
		goto out;

	/* If this is a memory parity error dump which blocks are offending */
	mem_perr = EFX_OWORD_FIELD(fatal_intr, MEM_PERR_INT_KER);
	if (mem_perr) {
		efx_oword_t reg;
		falcon_read(efx, &reg, MEM_STAT_REG_KER);
		EFX_ERR(efx, "SYSTEM ERROR: memory parity error "
			EFX_OWORD_FMT "\n", EFX_OWORD_VAL(reg));
	}

	/* Disable DMA bus mastering on both devices */
	pci_disable_device(efx->pci_dev);
	if (FALCON_IS_DUAL_FUNC(efx))
		pci_disable_device(nic_data->pci_dev2);

	if (++n_int_errors < FALCON_MAX_INT_ERRORS) {
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

/* Handle a legacy interrupt from Falcon
 * Acknowledges the interrupt and schedule event queue processing.
 */
static irqreturn_t falcon_legacy_interrupt_b0(int irq, void *dev_id)
{
	struct efx_nic *efx = (struct efx_nic *)dev_id;
	efx_oword_t *int_ker = (efx_oword_t *) efx->irq_status.addr;
	struct efx_channel *channel;
	efx_dword_t reg;
	u32 queues;
	int syserr;

	/* Read the ISR which also ACKs the interrupts */
	falcon_readl(efx, &reg, INT_ISR0_B0);
	queues = EFX_EXTRACT_DWORD(reg, 0, 31);

	/* Check to see if we have a serious error condition */
	syserr = EFX_OWORD_FIELD(*int_ker, FATAL_INT);
	if (unlikely(syserr))
		return falcon_fatal_interrupt(efx);

	if (queues == 0)
		return IRQ_NONE;

	efx->last_irq_cpu = raw_smp_processor_id();
	EFX_TRACE(efx, "IRQ %d on CPU %d status " EFX_DWORD_FMT "\n",
		  irq, raw_smp_processor_id(), EFX_DWORD_VAL(reg));

	/* Schedule processing of any interrupting queues */
	channel = &efx->channel[0];
	while (queues) {
		if (queues & 0x01)
			efx_schedule_channel(channel);
		channel++;
		queues >>= 1;
	}

	return IRQ_HANDLED;
}


static irqreturn_t falcon_legacy_interrupt_a1(int irq, void *dev_id)
{
	struct efx_nic *efx = (struct efx_nic *)dev_id;
	efx_oword_t *int_ker = (efx_oword_t *) efx->irq_status.addr;
	struct efx_channel *channel;
	int syserr;
	int queues;

	/* Check to see if this is our interrupt.  If it isn't, we
	 * exit without having touched the hardware.
	 */
	if (unlikely(EFX_OWORD_IS_ZERO(*int_ker))) {
		EFX_TRACE(efx, "IRQ %d on CPU %d not for me\n", irq,
			  raw_smp_processor_id());
		return IRQ_NONE;
	}
	efx->last_irq_cpu = raw_smp_processor_id();
	EFX_TRACE(efx, "IRQ %d on CPU %d status " EFX_OWORD_FMT "\n",
		  irq, raw_smp_processor_id(), EFX_OWORD_VAL(*int_ker));

	/* Check to see if we have a serious error condition */
	syserr = EFX_OWORD_FIELD(*int_ker, FATAL_INT);
	if (unlikely(syserr))
		return falcon_fatal_interrupt(efx);

	/* Determine interrupting queues, clear interrupt status
	 * register and acknowledge the device interrupt.
	 */
	BUILD_BUG_ON(INT_EVQS_WIDTH > EFX_MAX_CHANNELS);
	queues = EFX_OWORD_FIELD(*int_ker, INT_EVQS);
	EFX_ZERO_OWORD(*int_ker);
	wmb(); /* Ensure the vector is cleared before interrupt ack */
	falcon_irq_ack_a1(efx);

	/* Schedule processing of any interrupting queues */
	channel = &efx->channel[0];
	while (queues) {
		if (queues & 0x01)
			efx_schedule_channel(channel);
		channel++;
		queues >>= 1;
	}

	return IRQ_HANDLED;
}

/* Handle an MSI interrupt from Falcon
 *
 * Handle an MSI hardware interrupt.  This routine schedules event
 * queue processing.  No interrupt acknowledgement cycle is necessary.
 * Also, we never need to check that the interrupt is for us, since
 * MSI interrupts cannot be shared.
 */
static irqreturn_t falcon_msi_interrupt(int irq, void *dev_id)
{
	struct efx_channel *channel = (struct efx_channel *)dev_id;
	struct efx_nic *efx = channel->efx;
	efx_oword_t *int_ker = (efx_oword_t *) efx->irq_status.addr;
	int syserr;

	efx->last_irq_cpu = raw_smp_processor_id();
	EFX_TRACE(efx, "IRQ %d on CPU %d status " EFX_OWORD_FMT "\n",
		  irq, raw_smp_processor_id(), EFX_OWORD_VAL(*int_ker));

	/* Check to see if we have a serious error condition */
	syserr = EFX_OWORD_FIELD(*int_ker, FATAL_INT);
	if (unlikely(syserr))
		return falcon_fatal_interrupt(efx);

	/* Schedule processing of the channel */
	efx_schedule_channel(channel);

	return IRQ_HANDLED;
}


/* Setup RSS indirection table.
 * This maps from the hash value of the packet to RXQ
 */
static void falcon_setup_rss_indir_table(struct efx_nic *efx)
{
	int i = 0;
	unsigned long offset;
	efx_dword_t dword;

	if (FALCON_REV(efx) < FALCON_REV_B0)
		return;

	for (offset = RX_RSS_INDIR_TBL_B0;
	     offset < RX_RSS_INDIR_TBL_B0 + 0x800;
	     offset += 0x10) {
		EFX_POPULATE_DWORD_1(dword, RX_RSS_INDIR_ENT_B0,
				     i % efx->rss_queues);
		falcon_writel(efx, &dword, offset);
		i++;
	}
}

/* Hook interrupt handler(s)
 * Try MSI and then legacy interrupts.
 */
int falcon_init_interrupt(struct efx_nic *efx)
{
	struct efx_channel *channel;
	int rc;

	if (!EFX_INT_MODE_USE_MSI(efx)) {
		irq_handler_t handler;
		if (FALCON_REV(efx) >= FALCON_REV_B0)
			handler = falcon_legacy_interrupt_b0;
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
	efx_for_each_channel_with_interrupt(channel, efx) {
		rc = request_irq(channel->irq, falcon_msi_interrupt,
				 IRQF_PROBE_SHARED, /* Not shared */
				 efx->name, channel);
		if (rc) {
			EFX_ERR(efx, "failed to hook IRQ %d\n", channel->irq);
			goto fail2;
		}
	}

	return 0;

 fail2:
	efx_for_each_channel_with_interrupt(channel, efx)
		free_irq(channel->irq, channel);
 fail1:
	return rc;
}

void falcon_fini_interrupt(struct efx_nic *efx)
{
	struct efx_channel *channel;
	efx_oword_t reg;

	/* Disable MSI/MSI-X interrupts */
	efx_for_each_channel_with_interrupt(channel, efx)
		if (channel->irq)
			free_irq(channel->irq, channel);

	/* ACK legacy interrupt */
	if (FALCON_REV(efx) >= FALCON_REV_B0)
		falcon_read(efx, &reg, INT_ISR0_B0);
	else
		falcon_irq_ack_a1(efx);

	/* Disable legacy interrupt */
	if (efx->legacy_irq)
		free_irq(efx->legacy_irq, efx);
}

/**************************************************************************
 *
 * EEPROM/flash
 *
 **************************************************************************
 */

#define FALCON_SPI_MAX_LEN sizeof(efx_oword_t)

/* Wait for SPI command completion */
static int falcon_spi_wait(struct efx_nic *efx)
{
	efx_oword_t reg;
	int cmd_en, timer_active;
	int count;

	count = 0;
	do {
		falcon_read(efx, &reg, EE_SPI_HCMD_REG_KER);
		cmd_en = EFX_OWORD_FIELD(reg, EE_SPI_HCMD_CMD_EN);
		timer_active = EFX_OWORD_FIELD(reg, EE_WR_TIMER_ACTIVE);
		if (!cmd_en && !timer_active)
			return 0;
		udelay(10);
	} while (++count < 10000); /* wait upto 100msec */
	EFX_ERR(efx, "timed out waiting for SPI\n");
	return -ETIMEDOUT;
}

static int
falcon_spi_read(struct efx_nic *efx, int device_id, unsigned int command,
		unsigned int address, unsigned int addr_len,
		void *data, unsigned int len)
{
	efx_oword_t reg;
	int rc;

	BUG_ON(len > FALCON_SPI_MAX_LEN);

	/* Check SPI not currently being accessed */
	rc = falcon_spi_wait(efx);
	if (rc)
		return rc;

	/* Program address register */
	EFX_POPULATE_OWORD_1(reg, EE_SPI_HADR_ADR, address);
	falcon_write(efx, &reg, EE_SPI_HADR_REG_KER);

	/* Issue read command */
	EFX_POPULATE_OWORD_7(reg,
			     EE_SPI_HCMD_CMD_EN, 1,
			     EE_SPI_HCMD_SF_SEL, device_id,
			     EE_SPI_HCMD_DABCNT, len,
			     EE_SPI_HCMD_READ, EE_SPI_READ,
			     EE_SPI_HCMD_DUBCNT, 0,
			     EE_SPI_HCMD_ADBCNT, addr_len,
			     EE_SPI_HCMD_ENC, command);
	falcon_write(efx, &reg, EE_SPI_HCMD_REG_KER);

	/* Wait for read to complete */
	rc = falcon_spi_wait(efx);
	if (rc)
		return rc;

	/* Read data */
	falcon_read(efx, &reg, EE_SPI_HDATA_REG_KER);
	memcpy(data, &reg, len);
	return 0;
}

/**************************************************************************
 *
 * MAC wrapper
 *
 **************************************************************************
 */
void falcon_drain_tx_fifo(struct efx_nic *efx)
{
	efx_oword_t temp;
	int count;

	if (FALCON_REV(efx) < FALCON_REV_B0)
		return;

	falcon_read(efx, &temp, MAC0_CTRL_REG_KER);
	/* There is no point in draining more than once */
	if (EFX_OWORD_FIELD(temp, TXFIFO_DRAIN_EN_B0))
		return;

	/* MAC stats will fail whilst the TX fifo is draining. Serialise
	 * the drain sequence with the statistics fetch */
	spin_lock(&efx->stats_lock);

	EFX_SET_OWORD_FIELD(temp, TXFIFO_DRAIN_EN_B0, 1);
	falcon_write(efx, &temp, MAC0_CTRL_REG_KER);

	/* Reset the MAC and EM block. */
	falcon_read(efx, &temp, GLB_CTL_REG_KER);
	EFX_SET_OWORD_FIELD(temp, RST_XGTX, 1);
	EFX_SET_OWORD_FIELD(temp, RST_XGRX, 1);
	EFX_SET_OWORD_FIELD(temp, RST_EM, 1);
	falcon_write(efx, &temp, GLB_CTL_REG_KER);

	count = 0;
	while (1) {
		falcon_read(efx, &temp, GLB_CTL_REG_KER);
		if (!EFX_OWORD_FIELD(temp, RST_XGTX) &&
		    !EFX_OWORD_FIELD(temp, RST_XGRX) &&
		    !EFX_OWORD_FIELD(temp, RST_EM)) {
			EFX_LOG(efx, "Completed MAC reset after %d loops\n",
				count);
			break;
		}
		if (count > 20) {
			EFX_ERR(efx, "MAC reset failed\n");
			break;
		}
		count++;
		udelay(10);
	}

	spin_unlock(&efx->stats_lock);

	/* If we've reset the EM block and the link is up, then
	 * we'll have to kick the XAUI link so the PHY can recover */
	if (efx->link_up && EFX_WORKAROUND_5147(efx))
		falcon_reset_xaui(efx);
}

void falcon_deconfigure_mac_wrapper(struct efx_nic *efx)
{
	efx_oword_t temp;

	if (FALCON_REV(efx) < FALCON_REV_B0)
		return;

	/* Isolate the MAC -> RX */
	falcon_read(efx, &temp, RX_CFG_REG_KER);
	EFX_SET_OWORD_FIELD(temp, RX_INGR_EN_B0, 0);
	falcon_write(efx, &temp, RX_CFG_REG_KER);

	if (!efx->link_up)
		falcon_drain_tx_fifo(efx);
}

void falcon_reconfigure_mac_wrapper(struct efx_nic *efx)
{
	efx_oword_t reg;
	int link_speed;
	unsigned int tx_fc;

	if (efx->link_options & GM_LPA_10000)
		link_speed = 0x3;
	else if (efx->link_options & GM_LPA_1000)
		link_speed = 0x2;
	else if (efx->link_options & GM_LPA_100)
		link_speed = 0x1;
	else
		link_speed = 0x0;
	/* MAC_LINK_STATUS controls MAC backpressure but doesn't work
	 * as advertised.  Disable to ensure packets are not
	 * indefinitely held and TX queue can be flushed at any point
	 * while the link is down. */
	EFX_POPULATE_OWORD_5(reg,
			     MAC_XOFF_VAL, 0xffff /* max pause time */,
			     MAC_BCAD_ACPT, 1,
			     MAC_UC_PROM, efx->promiscuous,
			     MAC_LINK_STATUS, 1, /* always set */
			     MAC_SPEED, link_speed);
	/* On B0, MAC backpressure can be disabled and packets get
	 * discarded. */
	if (FALCON_REV(efx) >= FALCON_REV_B0) {
		EFX_SET_OWORD_FIELD(reg, TXFIFO_DRAIN_EN_B0,
				    !efx->link_up);
	}

	falcon_write(efx, &reg, MAC0_CTRL_REG_KER);

	/* Restore the multicast hash registers. */
	falcon_set_multicast_hash(efx);

	/* Transmission of pause frames when RX crosses the threshold is
	 * covered by RX_XOFF_MAC_EN and XM_TX_CFG_REG:XM_FCNTL.
	 * Action on receipt of pause frames is controller by XM_DIS_FCNTL */
	tx_fc = (efx->flow_control & EFX_FC_TX) ? 1 : 0;
	falcon_read(efx, &reg, RX_CFG_REG_KER);
	EFX_SET_OWORD_FIELD_VER(efx, reg, RX_XOFF_MAC_EN, tx_fc);

	/* Unisolate the MAC -> RX */
	if (FALCON_REV(efx) >= FALCON_REV_B0)
		EFX_SET_OWORD_FIELD(reg, RX_INGR_EN_B0, 1);
	falcon_write(efx, &reg, RX_CFG_REG_KER);
}

int falcon_dma_stats(struct efx_nic *efx, unsigned int done_offset)
{
	efx_oword_t reg;
	u32 *dma_done;
	int i;

	if (disable_dma_stats)
		return 0;

	/* Statistics fetch will fail if the MAC is in TX drain */
	if (FALCON_REV(efx) >= FALCON_REV_B0) {
		efx_oword_t temp;
		falcon_read(efx, &temp, MAC0_CTRL_REG_KER);
		if (EFX_OWORD_FIELD(temp, TXFIFO_DRAIN_EN_B0))
			return 0;
	}

	dma_done = (efx->stats_buffer.addr + done_offset);
	*dma_done = FALCON_STATS_NOT_DONE;
	wmb(); /* ensure done flag is clear */

	/* Initiate DMA transfer of stats */
	EFX_POPULATE_OWORD_2(reg,
			     MAC_STAT_DMA_CMD, 1,
			     MAC_STAT_DMA_ADR,
			     efx->stats_buffer.dma_addr);
	falcon_write(efx, &reg, MAC0_STAT_DMA_REG_KER);

	/* Wait for transfer to complete */
	for (i = 0; i < 400; i++) {
		if (*(volatile u32 *)dma_done == FALCON_STATS_DONE)
			return 0;
		udelay(10);
	}

	EFX_ERR(efx, "timed out waiting for statistics\n");
	return -ETIMEDOUT;
}

/**************************************************************************
 *
 * PHY access via GMII
 *
 **************************************************************************
 */

/* Use the top bit of the MII PHY id to indicate the PHY type
 * (1G/10G), with the remaining bits as the actual PHY id.
 *
 * This allows us to avoid leaking information from the mii_if_info
 * structure into other data structures.
 */
#define FALCON_PHY_ID_ID_WIDTH  EFX_WIDTH(MD_PRT_DEV_ADR)
#define FALCON_PHY_ID_ID_MASK   ((1 << FALCON_PHY_ID_ID_WIDTH) - 1)
#define FALCON_PHY_ID_WIDTH     (FALCON_PHY_ID_ID_WIDTH + 1)
#define FALCON_PHY_ID_MASK      ((1 << FALCON_PHY_ID_WIDTH) - 1)
#define FALCON_PHY_ID_10G       (1 << (FALCON_PHY_ID_WIDTH - 1))


/* Packing the clause 45 port and device fields into a single value */
#define MD_PRT_ADR_COMP_LBN   (MD_PRT_ADR_LBN - MD_DEV_ADR_LBN)
#define MD_PRT_ADR_COMP_WIDTH  MD_PRT_ADR_WIDTH
#define MD_DEV_ADR_COMP_LBN    0
#define MD_DEV_ADR_COMP_WIDTH  MD_DEV_ADR_WIDTH


/* Wait for GMII access to complete */
static int falcon_gmii_wait(struct efx_nic *efx)
{
	efx_dword_t md_stat;
	int count;

	for (count = 0; count < 1000; count++) {	/* wait upto 10ms */
		falcon_readl(efx, &md_stat, MD_STAT_REG_KER);
		if (EFX_DWORD_FIELD(md_stat, MD_BSY) == 0) {
			if (EFX_DWORD_FIELD(md_stat, MD_LNFL) != 0 ||
			    EFX_DWORD_FIELD(md_stat, MD_BSERR) != 0) {
				EFX_ERR(efx, "error from GMII access "
					EFX_DWORD_FMT"\n",
					EFX_DWORD_VAL(md_stat));
				return -EIO;
			}
			return 0;
		}
		udelay(10);
	}
	EFX_ERR(efx, "timed out waiting for GMII\n");
	return -ETIMEDOUT;
}

/* Writes a GMII register of a PHY connected to Falcon using MDIO. */
static void falcon_mdio_write(struct net_device *net_dev, int phy_id,
			      int addr, int value)
{
	struct efx_nic *efx = (struct efx_nic *)net_dev->priv;
	unsigned int phy_id2 = phy_id & FALCON_PHY_ID_ID_MASK;
	efx_oword_t reg;

	/* The 'generic' prt/dev packing in mdio_10g.h is conveniently
	 * chosen so that the only current user, Falcon, can take the
	 * packed value and use them directly.
	 * Fail to build if this assumption is broken.
	 */
	BUILD_BUG_ON(FALCON_PHY_ID_10G != MDIO45_XPRT_ID_IS10G);
	BUILD_BUG_ON(FALCON_PHY_ID_ID_WIDTH != MDIO45_PRT_DEV_WIDTH);
	BUILD_BUG_ON(MD_PRT_ADR_COMP_LBN != MDIO45_PRT_ID_COMP_LBN);
	BUILD_BUG_ON(MD_DEV_ADR_COMP_LBN != MDIO45_DEV_ID_COMP_LBN);

	if (phy_id2 == PHY_ADDR_INVALID)
		return;

	/* See falcon_mdio_read for an explanation. */
	if (!(phy_id & FALCON_PHY_ID_10G)) {
		int mmd = ffs(efx->phy_op->mmds) - 1;
		EFX_TRACE(efx, "Fixing erroneous clause22 write\n");
		phy_id2 = mdio_clause45_pack(phy_id2, mmd)
			& FALCON_PHY_ID_ID_MASK;
	}

	EFX_REGDUMP(efx, "writing GMII %d register %02x with %04x\n", phy_id,
		    addr, value);

	spin_lock_bh(&efx->phy_lock);

	/* Check MII not currently being accessed */
	if (falcon_gmii_wait(efx) != 0)
		goto out;

	/* Write the address/ID register */
	EFX_POPULATE_OWORD_1(reg, MD_PHY_ADR, addr);
	falcon_write(efx, &reg, MD_PHY_ADR_REG_KER);

	EFX_POPULATE_OWORD_1(reg, MD_PRT_DEV_ADR, phy_id2);
	falcon_write(efx, &reg, MD_ID_REG_KER);

	/* Write data */
	EFX_POPULATE_OWORD_1(reg, MD_TXD, value);
	falcon_write(efx, &reg, MD_TXD_REG_KER);

	EFX_POPULATE_OWORD_2(reg,
			     MD_WRC, 1,
			     MD_GC, 0);
	falcon_write(efx, &reg, MD_CS_REG_KER);

	/* Wait for data to be written */
	if (falcon_gmii_wait(efx) != 0) {
		/* Abort the write operation */
		EFX_POPULATE_OWORD_2(reg,
				     MD_WRC, 0,
				     MD_GC, 1);
		falcon_write(efx, &reg, MD_CS_REG_KER);
		udelay(10);
	}

 out:
	spin_unlock_bh(&efx->phy_lock);
}

/* Reads a GMII register from a PHY connected to Falcon.  If no value
 * could be read, -1 will be returned. */
static int falcon_mdio_read(struct net_device *net_dev, int phy_id, int addr)
{
	struct efx_nic *efx = (struct efx_nic *)net_dev->priv;
	unsigned int phy_addr = phy_id & FALCON_PHY_ID_ID_MASK;
	efx_oword_t reg;
	int value = -1;

	if (phy_addr == PHY_ADDR_INVALID)
		return -1;

	/* Our PHY code knows whether it needs to talk clause 22(1G) or 45(10G)
	 * but the generic Linux code does not make any distinction or have
	 * any state for this.
	 * We spot the case where someone tried to talk 22 to a 45 PHY and
	 * redirect the request to the lowest numbered MMD as a clause45
	 * request. This is enough to allow simple queries like id and link
	 * state to succeed. TODO: We may need to do more in future.
	 */
	if (!(phy_id & FALCON_PHY_ID_10G)) {
		int mmd = ffs(efx->phy_op->mmds) - 1;
		EFX_TRACE(efx, "Fixing erroneous clause22 read\n");
		phy_addr = mdio_clause45_pack(phy_addr, mmd)
			& FALCON_PHY_ID_ID_MASK;
	}

	spin_lock_bh(&efx->phy_lock);

	/* Check MII not currently being accessed */
	if (falcon_gmii_wait(efx) != 0)
		goto out;

	EFX_POPULATE_OWORD_1(reg, MD_PHY_ADR, addr);
	falcon_write(efx, &reg, MD_PHY_ADR_REG_KER);

	EFX_POPULATE_OWORD_1(reg, MD_PRT_DEV_ADR, phy_addr);
	falcon_write(efx, &reg, MD_ID_REG_KER);

	/* Request data to be read */
	EFX_POPULATE_OWORD_2(reg, MD_RDC, 1, MD_GC, 0);
	falcon_write(efx, &reg, MD_CS_REG_KER);

	/* Wait for data to become available */
	value = falcon_gmii_wait(efx);
	if (value == 0) {
		falcon_read(efx, &reg, MD_RXD_REG_KER);
		value = EFX_OWORD_FIELD(reg, MD_RXD);
		EFX_REGDUMP(efx, "read from GMII %d register %02x, got %04x\n",
			    phy_id, addr, value);
	} else {
		/* Abort the read operation */
		EFX_POPULATE_OWORD_2(reg,
				     MD_RIC, 0,
				     MD_GC, 1);
		falcon_write(efx, &reg, MD_CS_REG_KER);

		EFX_LOG(efx, "read from GMII 0x%x register %02x, got "
			"error %d\n", phy_id, addr, value);
	}

 out:
	spin_unlock_bh(&efx->phy_lock);

	return value;
}

static void falcon_init_mdio(struct mii_if_info *gmii)
{
	gmii->mdio_read = falcon_mdio_read;
	gmii->mdio_write = falcon_mdio_write;
	gmii->phy_id_mask = FALCON_PHY_ID_MASK;
	gmii->reg_num_mask = ((1 << EFX_WIDTH(MD_PHY_ADR)) - 1);
}

static int falcon_probe_phy(struct efx_nic *efx)
{
	switch (efx->phy_type) {
	case PHY_TYPE_10XPRESS:
		efx->phy_op = &falcon_tenxpress_phy_ops;
		break;
	case PHY_TYPE_XFP:
		efx->phy_op = &falcon_xfp_phy_ops;
		break;
	default:
		EFX_ERR(efx, "Unknown PHY type %d\n",
			efx->phy_type);
		return -1;
	}
	return 0;
}

/* This call is responsible for hooking in the MAC and PHY operations */
int falcon_probe_port(struct efx_nic *efx)
{
	int rc;

	/* Hook in PHY operations table */
	rc = falcon_probe_phy(efx);
	if (rc)
		return rc;

	/* Set up GMII structure for PHY */
	efx->mii.supports_gmii = 1;
	falcon_init_mdio(&efx->mii);

	/* Hardware flow ctrl. FalconA RX FIFO too small for pause generation */
	if (FALCON_REV(efx) >= FALCON_REV_B0)
		efx->flow_control = EFX_FC_RX | EFX_FC_TX;
	else
		efx->flow_control = EFX_FC_RX;

	/* Allocate buffer for stats */
	rc = falcon_alloc_buffer(efx, &efx->stats_buffer,
				 FALCON_MAC_STATS_SIZE);
	if (rc)
		return rc;
	EFX_LOG(efx, "stats buffer at %llx (virt %p phys %lx)\n",
		(unsigned long long)efx->stats_buffer.dma_addr,
		efx->stats_buffer.addr,
		virt_to_phys(efx->stats_buffer.addr));

	return 0;
}

void falcon_remove_port(struct efx_nic *efx)
{
	falcon_free_buffer(efx, &efx->stats_buffer);
}

/**************************************************************************
 *
 * Multicast filtering
 *
 **************************************************************************
 */

void falcon_set_multicast_hash(struct efx_nic *efx)
{
	union efx_multicast_hash *mc_hash = &efx->multicast_hash;

	/* Broadcast packets go through the multicast hash filter.
	 * ether_crc_le() of the broadcast address is 0xbe2612ff
	 * so we always add bit 0xff to the mask.
	 */
	set_bit_le(0xff, mc_hash->byte);

	falcon_write(efx, &mc_hash->oword[0], MAC_MCAST_HASH_REG0_KER);
	falcon_write(efx, &mc_hash->oword[1], MAC_MCAST_HASH_REG1_KER);
}

/**************************************************************************
 *
 * Device reset
 *
 **************************************************************************
 */

/* Resets NIC to known state.  This routine must be called in process
 * context and is allowed to sleep. */
int falcon_reset_hw(struct efx_nic *efx, enum reset_type method)
{
	struct falcon_nic_data *nic_data = efx->nic_data;
	efx_oword_t glb_ctl_reg_ker;
	int rc;

	EFX_LOG(efx, "performing hardware reset (%d)\n", method);

	/* Initiate device reset */
	if (method == RESET_TYPE_WORLD) {
		rc = pci_save_state(efx->pci_dev);
		if (rc) {
			EFX_ERR(efx, "failed to backup PCI state of primary "
				"function prior to hardware reset\n");
			goto fail1;
		}
		if (FALCON_IS_DUAL_FUNC(efx)) {
			rc = pci_save_state(nic_data->pci_dev2);
			if (rc) {
				EFX_ERR(efx, "failed to backup PCI state of "
					"secondary function prior to "
					"hardware reset\n");
				goto fail2;
			}
		}

		EFX_POPULATE_OWORD_2(glb_ctl_reg_ker,
				     EXT_PHY_RST_DUR, 0x7,
				     SWRST, 1);
	} else {
		int reset_phy = (method == RESET_TYPE_INVISIBLE ?
				 EXCLUDE_FROM_RESET : 0);

		EFX_POPULATE_OWORD_7(glb_ctl_reg_ker,
				     EXT_PHY_RST_CTL, reset_phy,
				     PCIE_CORE_RST_CTL, EXCLUDE_FROM_RESET,
				     PCIE_NSTCK_RST_CTL, EXCLUDE_FROM_RESET,
				     PCIE_SD_RST_CTL, EXCLUDE_FROM_RESET,
				     EE_RST_CTL, EXCLUDE_FROM_RESET,
				     EXT_PHY_RST_DUR, 0x7 /* 10ms */,
				     SWRST, 1);
	}
	falcon_write(efx, &glb_ctl_reg_ker, GLB_CTL_REG_KER);

	EFX_LOG(efx, "waiting for hardware reset\n");
	schedule_timeout_uninterruptible(HZ / 20);

	/* Restore PCI configuration if needed */
	if (method == RESET_TYPE_WORLD) {
		if (FALCON_IS_DUAL_FUNC(efx)) {
			rc = pci_restore_state(nic_data->pci_dev2);
			if (rc) {
				EFX_ERR(efx, "failed to restore PCI config for "
					"the secondary function\n");
				goto fail3;
			}
		}
		rc = pci_restore_state(efx->pci_dev);
		if (rc) {
			EFX_ERR(efx, "failed to restore PCI config for the "
				"primary function\n");
			goto fail4;
		}
		EFX_LOG(efx, "successfully restored PCI config\n");
	}

	/* Assert that reset complete */
	falcon_read(efx, &glb_ctl_reg_ker, GLB_CTL_REG_KER);
	if (EFX_OWORD_FIELD(glb_ctl_reg_ker, SWRST) != 0) {
		rc = -ETIMEDOUT;
		EFX_ERR(efx, "timed out waiting for hardware reset\n");
		goto fail5;
	}
	EFX_LOG(efx, "hardware reset complete\n");

	return 0;

	/* pci_save_state() and pci_restore_state() MUST be called in pairs */
fail2:
fail3:
	pci_restore_state(efx->pci_dev);
fail1:
fail4:
fail5:
	return rc;
}

/* Zeroes out the SRAM contents.  This routine must be called in
 * process context and is allowed to sleep.
 */
static int falcon_reset_sram(struct efx_nic *efx)
{
	efx_oword_t srm_cfg_reg_ker, gpio_cfg_reg_ker;
	int count;

	/* Set the SRAM wake/sleep GPIO appropriately. */
	falcon_read(efx, &gpio_cfg_reg_ker, GPIO_CTL_REG_KER);
	EFX_SET_OWORD_FIELD(gpio_cfg_reg_ker, GPIO1_OEN, 1);
	EFX_SET_OWORD_FIELD(gpio_cfg_reg_ker, GPIO1_OUT, 1);
	falcon_write(efx, &gpio_cfg_reg_ker, GPIO_CTL_REG_KER);

	/* Initiate SRAM reset */
	EFX_POPULATE_OWORD_2(srm_cfg_reg_ker,
			     SRAM_OOB_BT_INIT_EN, 1,
			     SRM_NUM_BANKS_AND_BANK_SIZE, 0);
	falcon_write(efx, &srm_cfg_reg_ker, SRM_CFG_REG_KER);

	/* Wait for SRAM reset to complete */
	count = 0;
	do {
		EFX_LOG(efx, "waiting for SRAM reset (attempt %d)...\n", count);

		/* SRAM reset is slow; expect around 16ms */
		schedule_timeout_uninterruptible(HZ / 50);

		/* Check for reset complete */
		falcon_read(efx, &srm_cfg_reg_ker, SRM_CFG_REG_KER);
		if (!EFX_OWORD_FIELD(srm_cfg_reg_ker, SRAM_OOB_BT_INIT_EN)) {
			EFX_LOG(efx, "SRAM reset complete\n");

			return 0;
		}
	} while (++count < 20);	/* wait upto 0.4 sec */

	EFX_ERR(efx, "timed out waiting for SRAM reset\n");
	return -ETIMEDOUT;
}

/* Extract non-volatile configuration */
static int falcon_probe_nvconfig(struct efx_nic *efx)
{
	struct falcon_nvconfig *nvconfig;
	efx_oword_t nic_stat;
	int device_id;
	unsigned addr_len;
	size_t offset, len;
	int magic_num, struct_ver, board_rev;
	int rc;

	/* Find the boot device. */
	falcon_read(efx, &nic_stat, NIC_STAT_REG);
	if (EFX_OWORD_FIELD(nic_stat, SF_PRST)) {
		device_id = EE_SPI_FLASH;
		addr_len = 3;
	} else if (EFX_OWORD_FIELD(nic_stat, EE_PRST)) {
		device_id = EE_SPI_EEPROM;
		addr_len = 2;
	} else {
		return -ENODEV;
	}

	nvconfig = kmalloc(sizeof(*nvconfig), GFP_KERNEL);

	/* Read the whole configuration structure into memory. */
	for (offset = 0; offset < sizeof(*nvconfig); offset += len) {
		len = min(sizeof(*nvconfig) - offset,
			  (size_t) FALCON_SPI_MAX_LEN);
		rc = falcon_spi_read(efx, device_id, SPI_READ,
				     NVCONFIG_BASE + offset, addr_len,
				     (char *)nvconfig + offset, len);
		if (rc)
			goto out;
	}

	/* Read the MAC addresses */
	memcpy(efx->mac_address, nvconfig->mac_address[0], ETH_ALEN);

	/* Read the board configuration. */
	magic_num = le16_to_cpu(nvconfig->board_magic_num);
	struct_ver = le16_to_cpu(nvconfig->board_struct_ver);

	if (magic_num != NVCONFIG_BOARD_MAGIC_NUM || struct_ver < 2) {
		EFX_ERR(efx, "Non volatile memory bad magic=%x ver=%x "
			"therefore using defaults\n", magic_num, struct_ver);
		efx->phy_type = PHY_TYPE_NONE;
		efx->mii.phy_id = PHY_ADDR_INVALID;
		board_rev = 0;
	} else {
		struct falcon_nvconfig_board_v2 *v2 = &nvconfig->board_v2;

		efx->phy_type = v2->port0_phy_type;
		efx->mii.phy_id = v2->port0_phy_addr;
		board_rev = le16_to_cpu(v2->board_revision);
	}

	EFX_LOG(efx, "PHY is %d phy_id %d\n", efx->phy_type, efx->mii.phy_id);

	efx_set_board_info(efx, board_rev);

 out:
	kfree(nvconfig);
	return rc;
}

/* Probe the NIC variant (revision, ASIC vs FPGA, function count, port
 * count, port speed).  Set workaround and feature flags accordingly.
 */
static int falcon_probe_nic_variant(struct efx_nic *efx)
{
	efx_oword_t altera_build;

	falcon_read(efx, &altera_build, ALTERA_BUILD_REG_KER);
	if (EFX_OWORD_FIELD(altera_build, VER_ALL)) {
		EFX_ERR(efx, "Falcon FPGA not supported\n");
		return -ENODEV;
	}

	switch (FALCON_REV(efx)) {
	case FALCON_REV_A0:
	case 0xff:
		EFX_ERR(efx, "Falcon rev A0 not supported\n");
		return -ENODEV;

	case FALCON_REV_A1:{
		efx_oword_t nic_stat;

		falcon_read(efx, &nic_stat, NIC_STAT_REG);

		if (EFX_OWORD_FIELD(nic_stat, STRAP_PCIE) == 0) {
			EFX_ERR(efx, "Falcon rev A1 PCI-X not supported\n");
			return -ENODEV;
		}
		if (!EFX_OWORD_FIELD(nic_stat, STRAP_10G)) {
			EFX_ERR(efx, "1G mode not supported\n");
			return -ENODEV;
		}
		break;
	}

	case FALCON_REV_B0:
		break;

	default:
		EFX_ERR(efx, "Unknown Falcon rev %d\n", FALCON_REV(efx));
		return -ENODEV;
	}

	return 0;
}

int falcon_probe_nic(struct efx_nic *efx)
{
	struct falcon_nic_data *nic_data;
	int rc;

	/* Initialise I2C interface state */
	efx->i2c.efx = efx;
	efx->i2c.op = &falcon_i2c_bit_operations;
	efx->i2c.sda = 1;
	efx->i2c.scl = 1;

	/* Allocate storage for hardware specific data */
	nic_data = kzalloc(sizeof(*nic_data), GFP_KERNEL);
	efx->nic_data = (void *) nic_data;

	/* Determine number of ports etc. */
	rc = falcon_probe_nic_variant(efx);
	if (rc)
		goto fail1;

	/* Probe secondary function if expected */
	if (FALCON_IS_DUAL_FUNC(efx)) {
		struct pci_dev *dev = pci_dev_get(efx->pci_dev);

		while ((dev = pci_get_device(EFX_VENDID_SFC, FALCON_A_S_DEVID,
					     dev))) {
			if (dev->bus == efx->pci_dev->bus &&
			    dev->devfn == efx->pci_dev->devfn + 1) {
				nic_data->pci_dev2 = dev;
				break;
			}
		}
		if (!nic_data->pci_dev2) {
			EFX_ERR(efx, "failed to find secondary function\n");
			rc = -ENODEV;
			goto fail2;
		}
	}

	/* Now we can reset the NIC */
	rc = falcon_reset_hw(efx, RESET_TYPE_ALL);
	if (rc) {
		EFX_ERR(efx, "failed to reset NIC\n");
		goto fail3;
	}

	/* Allocate memory for INT_KER */
	rc = falcon_alloc_buffer(efx, &efx->irq_status, sizeof(efx_oword_t));
	if (rc)
		goto fail4;
	BUG_ON(efx->irq_status.dma_addr & 0x0f);

	EFX_LOG(efx, "INT_KER at %llx (virt %p phys %lx)\n",
		(unsigned long long)efx->irq_status.dma_addr,
		efx->irq_status.addr, virt_to_phys(efx->irq_status.addr));

	/* Read in the non-volatile configuration */
	rc = falcon_probe_nvconfig(efx);
	if (rc)
		goto fail5;

	return 0;

 fail5:
	falcon_free_buffer(efx, &efx->irq_status);
 fail4:
	/* fall-thru */
 fail3:
	if (nic_data->pci_dev2) {
		pci_dev_put(nic_data->pci_dev2);
		nic_data->pci_dev2 = NULL;
	}
 fail2:
	/* fall-thru */
 fail1:
	kfree(efx->nic_data);
	return rc;
}

/* This call performs hardware-specific global initialisation, such as
 * defining the descriptor cache sizes and number of RSS channels.
 * It does not set up any buffers, descriptor rings or event queues.
 */
int falcon_init_nic(struct efx_nic *efx)
{
	struct falcon_nic_data *data;
	efx_oword_t temp;
	unsigned thresh;
	int rc;

	data = (struct falcon_nic_data *)efx->nic_data;

	/* Set up the address region register. This is only needed
	 * for the B0 FPGA, but since we are just pushing in the
	 * reset defaults this may as well be unconditional. */
	EFX_POPULATE_OWORD_4(temp, ADR_REGION0, 0,
				   ADR_REGION1, (1 << 16),
				   ADR_REGION2, (2 << 16),
				   ADR_REGION3, (3 << 16));
	falcon_write(efx, &temp, ADR_REGION_REG_KER);

	/* Use on-chip SRAM */
	falcon_read(efx, &temp, NIC_STAT_REG);
	EFX_SET_OWORD_FIELD(temp, ONCHIP_SRAM, 1);
	falcon_write(efx, &temp, NIC_STAT_REG);

	/* Set buffer table mode */
	EFX_POPULATE_OWORD_1(temp, BUF_TBL_MODE, BUF_TBL_MODE_FULL);
	falcon_write(efx, &temp, BUF_TBL_CFG_REG_KER);

	rc = falcon_reset_sram(efx);
	if (rc)
		return rc;

	/* Set positions of descriptor caches in SRAM. */
	EFX_POPULATE_OWORD_1(temp, SRM_TX_DC_BASE_ADR, TX_DC_BASE / 8);
	falcon_write(efx, &temp, SRM_TX_DC_CFG_REG_KER);
	EFX_POPULATE_OWORD_1(temp, SRM_RX_DC_BASE_ADR, RX_DC_BASE / 8);
	falcon_write(efx, &temp, SRM_RX_DC_CFG_REG_KER);

	/* Set TX descriptor cache size. */
	BUILD_BUG_ON(TX_DC_ENTRIES != (16 << TX_DC_ENTRIES_ORDER));
	EFX_POPULATE_OWORD_1(temp, TX_DC_SIZE, TX_DC_ENTRIES_ORDER);
	falcon_write(efx, &temp, TX_DC_CFG_REG_KER);

	/* Set RX descriptor cache size.  Set low watermark to size-8, as
	 * this allows most efficient prefetching.
	 */
	BUILD_BUG_ON(RX_DC_ENTRIES != (16 << RX_DC_ENTRIES_ORDER));
	EFX_POPULATE_OWORD_1(temp, RX_DC_SIZE, RX_DC_ENTRIES_ORDER);
	falcon_write(efx, &temp, RX_DC_CFG_REG_KER);
	EFX_POPULATE_OWORD_1(temp, RX_DC_PF_LWM, RX_DC_ENTRIES - 8);
	falcon_write(efx, &temp, RX_DC_PF_WM_REG_KER);

	/* Clear the parity enables on the TX data fifos as
	 * they produce false parity errors because of timing issues
	 */
	if (EFX_WORKAROUND_5129(efx)) {
		falcon_read(efx, &temp, SPARE_REG_KER);
		EFX_SET_OWORD_FIELD(temp, MEM_PERR_EN_TX_DATA, 0);
		falcon_write(efx, &temp, SPARE_REG_KER);
	}

	/* Enable all the genuinely fatal interrupts.  (They are still
	 * masked by the overall interrupt mask, controlled by
	 * falcon_interrupts()).
	 *
	 * Note: All other fatal interrupts are enabled
	 */
	EFX_POPULATE_OWORD_3(temp,
			     ILL_ADR_INT_KER_EN, 1,
			     RBUF_OWN_INT_KER_EN, 1,
			     TBUF_OWN_INT_KER_EN, 1);
	EFX_INVERT_OWORD(temp);
	falcon_write(efx, &temp, FATAL_INTR_REG_KER);

	/* Set number of RSS queues for receive path. */
	falcon_read(efx, &temp, RX_FILTER_CTL_REG);
	if (FALCON_REV(efx) >= FALCON_REV_B0)
		EFX_SET_OWORD_FIELD(temp, NUM_KER, 0);
	else
		EFX_SET_OWORD_FIELD(temp, NUM_KER, efx->rss_queues - 1);
	if (EFX_WORKAROUND_7244(efx)) {
		EFX_SET_OWORD_FIELD(temp, UDP_FULL_SRCH_LIMIT, 8);
		EFX_SET_OWORD_FIELD(temp, UDP_WILD_SRCH_LIMIT, 8);
		EFX_SET_OWORD_FIELD(temp, TCP_FULL_SRCH_LIMIT, 8);
		EFX_SET_OWORD_FIELD(temp, TCP_WILD_SRCH_LIMIT, 8);
	}
	falcon_write(efx, &temp, RX_FILTER_CTL_REG);

	falcon_setup_rss_indir_table(efx);

	/* Setup RX.  Wait for descriptor is broken and must
	 * be disabled.  RXDP recovery shouldn't be needed, but is.
	 */
	falcon_read(efx, &temp, RX_SELF_RST_REG_KER);
	EFX_SET_OWORD_FIELD(temp, RX_NODESC_WAIT_DIS, 1);
	EFX_SET_OWORD_FIELD(temp, RX_RECOVERY_EN, 1);
	if (EFX_WORKAROUND_5583(efx))
		EFX_SET_OWORD_FIELD(temp, RX_ISCSI_DIS, 1);
	falcon_write(efx, &temp, RX_SELF_RST_REG_KER);

	/* Disable the ugly timer-based TX DMA backoff and allow TX DMA to be
	 * controlled by the RX FIFO fill level. Set arbitration to one pkt/Q.
	 */
	falcon_read(efx, &temp, TX_CFG2_REG_KER);
	EFX_SET_OWORD_FIELD(temp, TX_RX_SPACER, 0xfe);
	EFX_SET_OWORD_FIELD(temp, TX_RX_SPACER_EN, 1);
	EFX_SET_OWORD_FIELD(temp, TX_ONE_PKT_PER_Q, 1);
	EFX_SET_OWORD_FIELD(temp, TX_CSR_PUSH_EN, 0);
	EFX_SET_OWORD_FIELD(temp, TX_DIS_NON_IP_EV, 1);
	/* Enable SW_EV to inherit in char driver - assume harmless here */
	EFX_SET_OWORD_FIELD(temp, TX_SW_EV_EN, 1);
	/* Prefetch threshold 2 => fetch when descriptor cache half empty */
	EFX_SET_OWORD_FIELD(temp, TX_PREF_THRESHOLD, 2);
	/* Squash TX of packets of 16 bytes or less */
	if (FALCON_REV(efx) >= FALCON_REV_B0 && EFX_WORKAROUND_9141(efx))
		EFX_SET_OWORD_FIELD(temp, TX_FLUSH_MIN_LEN_EN_B0, 1);
	falcon_write(efx, &temp, TX_CFG2_REG_KER);

	/* Do not enable TX_NO_EOP_DISC_EN, since it limits packets to 16
	 * descriptors (which is bad).
	 */
	falcon_read(efx, &temp, TX_CFG_REG_KER);
	EFX_SET_OWORD_FIELD(temp, TX_NO_EOP_DISC_EN, 0);
	falcon_write(efx, &temp, TX_CFG_REG_KER);

	/* RX config */
	falcon_read(efx, &temp, RX_CFG_REG_KER);
	EFX_SET_OWORD_FIELD_VER(efx, temp, RX_DESC_PUSH_EN, 0);
	if (EFX_WORKAROUND_7575(efx))
		EFX_SET_OWORD_FIELD_VER(efx, temp, RX_USR_BUF_SIZE,
					(3 * 4096) / 32);
	if (FALCON_REV(efx) >= FALCON_REV_B0)
		EFX_SET_OWORD_FIELD(temp, RX_INGR_EN_B0, 1);

	/* RX FIFO flow control thresholds */
	thresh = ((rx_xon_thresh_bytes >= 0) ?
		  rx_xon_thresh_bytes : efx->type->rx_xon_thresh);
	EFX_SET_OWORD_FIELD_VER(efx, temp, RX_XON_MAC_TH, thresh / 256);
	thresh = ((rx_xoff_thresh_bytes >= 0) ?
		  rx_xoff_thresh_bytes : efx->type->rx_xoff_thresh);
	EFX_SET_OWORD_FIELD_VER(efx, temp, RX_XOFF_MAC_TH, thresh / 256);
	/* RX control FIFO thresholds [32 entries] */
	EFX_SET_OWORD_FIELD_VER(efx, temp, RX_XON_TX_TH, 25);
	EFX_SET_OWORD_FIELD_VER(efx, temp, RX_XOFF_TX_TH, 20);
	falcon_write(efx, &temp, RX_CFG_REG_KER);

	/* Set destination of both TX and RX Flush events */
	if (FALCON_REV(efx) >= FALCON_REV_B0) {
		EFX_POPULATE_OWORD_1(temp, FLS_EVQ_ID, 0);
		falcon_write(efx, &temp, DP_CTRL_REG);
	}

	return 0;
}

void falcon_remove_nic(struct efx_nic *efx)
{
	struct falcon_nic_data *nic_data = efx->nic_data;

	falcon_free_buffer(efx, &efx->irq_status);

	(void) falcon_reset_hw(efx, RESET_TYPE_ALL);

	/* Release the second function after the reset */
	if (nic_data->pci_dev2) {
		pci_dev_put(nic_data->pci_dev2);
		nic_data->pci_dev2 = NULL;
	}

	/* Tear down the private nic state */
	kfree(efx->nic_data);
	efx->nic_data = NULL;
}

void falcon_update_nic_stats(struct efx_nic *efx)
{
	efx_oword_t cnt;

	falcon_read(efx, &cnt, RX_NODESC_DROP_REG_KER);
	efx->n_rx_nodesc_drop_cnt += EFX_OWORD_FIELD(cnt, RX_NODESC_DROP_CNT);
}

/**************************************************************************
 *
 * Revision-dependent attributes used by efx.c
 *
 **************************************************************************
 */

struct efx_nic_type falcon_a_nic_type = {
	.mem_bar = 2,
	.mem_map_size = 0x20000,
	.txd_ptr_tbl_base = TX_DESC_PTR_TBL_KER_A1,
	.rxd_ptr_tbl_base = RX_DESC_PTR_TBL_KER_A1,
	.buf_tbl_base = BUF_TBL_KER_A1,
	.evq_ptr_tbl_base = EVQ_PTR_TBL_KER_A1,
	.evq_rptr_tbl_base = EVQ_RPTR_REG_KER_A1,
	.txd_ring_mask = FALCON_TXD_RING_MASK,
	.rxd_ring_mask = FALCON_RXD_RING_MASK,
	.evq_size = FALCON_EVQ_SIZE,
	.max_dma_mask = FALCON_DMA_MASK,
	.tx_dma_mask = FALCON_TX_DMA_MASK,
	.bug5391_mask = 0xf,
	.rx_xoff_thresh = 2048,
	.rx_xon_thresh = 512,
	.rx_buffer_padding = 0x24,
	.max_interrupt_mode = EFX_INT_MODE_MSI,
	.phys_addr_channels = 4,
};

struct efx_nic_type falcon_b_nic_type = {
	.mem_bar = 2,
	/* Map everything up to and including the RSS indirection
	 * table.  Don't map MSI-X table, MSI-X PBA since Linux
	 * requires that they not be mapped.  */
	.mem_map_size = RX_RSS_INDIR_TBL_B0 + 0x800,
	.txd_ptr_tbl_base = TX_DESC_PTR_TBL_KER_B0,
	.rxd_ptr_tbl_base = RX_DESC_PTR_TBL_KER_B0,
	.buf_tbl_base = BUF_TBL_KER_B0,
	.evq_ptr_tbl_base = EVQ_PTR_TBL_KER_B0,
	.evq_rptr_tbl_base = EVQ_RPTR_REG_KER_B0,
	.txd_ring_mask = FALCON_TXD_RING_MASK,
	.rxd_ring_mask = FALCON_RXD_RING_MASK,
	.evq_size = FALCON_EVQ_SIZE,
	.max_dma_mask = FALCON_DMA_MASK,
	.tx_dma_mask = FALCON_TX_DMA_MASK,
	.bug5391_mask = 0,
	.rx_xoff_thresh = 54272, /* ~80Kb - 3*max MTU */
	.rx_xon_thresh = 27648,  /* ~3*max MTU */
	.rx_buffer_padding = 0,
	.max_interrupt_mode = EFX_INT_MODE_MSIX,
	.phys_addr_channels = 32, /* Hardware limit is 64, but the legacy
				   * interrupt handler only supports 32
				   * channels */
};

