// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/dev_printk.h>
#include <linux/dma-mapping.h>
#include <linux/gfp.h>
#include <linux/types.h>

#include "fbnic.h"
#include "fbnic_tlv.h"

static void __fbnic_mbx_wr_desc(struct fbnic_dev *fbd, int mbx_idx,
				int desc_idx, u64 desc)
{
	u32 desc_offset = FBNIC_IPC_MBX(mbx_idx, desc_idx);

	fw_wr32(fbd, desc_offset + 1, upper_32_bits(desc));
	fw_wrfl(fbd);
	fw_wr32(fbd, desc_offset, lower_32_bits(desc));
}

static u64 __fbnic_mbx_rd_desc(struct fbnic_dev *fbd, int mbx_idx, int desc_idx)
{
	u32 desc_offset = FBNIC_IPC_MBX(mbx_idx, desc_idx);
	u64 desc;

	desc = fw_rd32(fbd, desc_offset);
	desc |= (u64)fw_rd32(fbd, desc_offset + 1) << 32;

	return desc;
}

static void fbnic_mbx_init_desc_ring(struct fbnic_dev *fbd, int mbx_idx)
{
	int desc_idx;

	/* Initialize first descriptor to all 0s. Doing this gives us a
	 * solid stop for the firmware to hit when it is done looping
	 * through the ring.
	 */
	__fbnic_mbx_wr_desc(fbd, mbx_idx, 0, 0);

	fw_wrfl(fbd);

	/* We then fill the rest of the ring starting at the end and moving
	 * back toward descriptor 0 with skip descriptors that have no
	 * length nor address, and tell the firmware that they can skip
	 * them and just move past them to the one we initialized to 0.
	 */
	for (desc_idx = FBNIC_IPC_MBX_DESC_LEN; --desc_idx;) {
		__fbnic_mbx_wr_desc(fbd, mbx_idx, desc_idx,
				    FBNIC_IPC_MBX_DESC_FW_CMPL |
				    FBNIC_IPC_MBX_DESC_HOST_CMPL);
		fw_wrfl(fbd);
	}
}

void fbnic_mbx_init(struct fbnic_dev *fbd)
{
	int i;

	/* Initialize lock to protect Tx ring */
	spin_lock_init(&fbd->fw_tx_lock);

	/* Reinitialize mailbox memory */
	for (i = 0; i < FBNIC_IPC_MBX_INDICES; i++)
		memset(&fbd->mbx[i], 0, sizeof(struct fbnic_fw_mbx));

	/* Do not auto-clear the FW mailbox interrupt, let SW clear it */
	wr32(fbd, FBNIC_INTR_SW_AC_MODE(0), ~(1u << FBNIC_FW_MSIX_ENTRY));

	/* Clear any stale causes in vector 0 as that is used for doorbell */
	wr32(fbd, FBNIC_INTR_CLEAR(0), 1u << FBNIC_FW_MSIX_ENTRY);

	for (i = 0; i < FBNIC_IPC_MBX_INDICES; i++)
		fbnic_mbx_init_desc_ring(fbd, i);
}

static int fbnic_mbx_map_msg(struct fbnic_dev *fbd, int mbx_idx,
			     struct fbnic_tlv_msg *msg, u16 length, u8 eom)
{
	struct fbnic_fw_mbx *mbx = &fbd->mbx[mbx_idx];
	u8 tail = mbx->tail;
	dma_addr_t addr;
	int direction;

	if (!mbx->ready || !fbnic_fw_present(fbd))
		return -ENODEV;

	direction = (mbx_idx == FBNIC_IPC_MBX_RX_IDX) ? DMA_FROM_DEVICE :
							DMA_TO_DEVICE;

	if (mbx->head == ((tail + 1) % FBNIC_IPC_MBX_DESC_LEN))
		return -EBUSY;

	addr = dma_map_single(fbd->dev, msg, PAGE_SIZE, direction);
	if (dma_mapping_error(fbd->dev, addr)) {
		free_page((unsigned long)msg);

		return -ENOSPC;
	}

	mbx->buf_info[tail].msg = msg;
	mbx->buf_info[tail].addr = addr;

	mbx->tail = (tail + 1) % FBNIC_IPC_MBX_DESC_LEN;

	fw_wr32(fbd, FBNIC_IPC_MBX(mbx_idx, mbx->tail), 0);

	__fbnic_mbx_wr_desc(fbd, mbx_idx, tail,
			    FIELD_PREP(FBNIC_IPC_MBX_DESC_LEN_MASK, length) |
			    (addr & FBNIC_IPC_MBX_DESC_ADDR_MASK) |
			    (eom ? FBNIC_IPC_MBX_DESC_EOM : 0) |
			    FBNIC_IPC_MBX_DESC_HOST_CMPL);

	return 0;
}

static void fbnic_mbx_unmap_and_free_msg(struct fbnic_dev *fbd, int mbx_idx,
					 int desc_idx)
{
	struct fbnic_fw_mbx *mbx = &fbd->mbx[mbx_idx];
	int direction;

	if (!mbx->buf_info[desc_idx].msg)
		return;

	direction = (mbx_idx == FBNIC_IPC_MBX_RX_IDX) ? DMA_FROM_DEVICE :
							DMA_TO_DEVICE;
	dma_unmap_single(fbd->dev, mbx->buf_info[desc_idx].addr,
			 PAGE_SIZE, direction);

	free_page((unsigned long)mbx->buf_info[desc_idx].msg);
	mbx->buf_info[desc_idx].msg = NULL;
}

static void fbnic_mbx_clean_desc_ring(struct fbnic_dev *fbd, int mbx_idx)
{
	int i;

	fbnic_mbx_init_desc_ring(fbd, mbx_idx);

	for (i = FBNIC_IPC_MBX_DESC_LEN; i--;)
		fbnic_mbx_unmap_and_free_msg(fbd, mbx_idx, i);
}

void fbnic_mbx_clean(struct fbnic_dev *fbd)
{
	int i;

	for (i = 0; i < FBNIC_IPC_MBX_INDICES; i++)
		fbnic_mbx_clean_desc_ring(fbd, i);
}

#define FBNIC_MBX_MAX_PAGE_SIZE	FIELD_MAX(FBNIC_IPC_MBX_DESC_LEN_MASK)
#define FBNIC_RX_PAGE_SIZE	min_t(int, PAGE_SIZE, FBNIC_MBX_MAX_PAGE_SIZE)

static int fbnic_mbx_alloc_rx_msgs(struct fbnic_dev *fbd)
{
	struct fbnic_fw_mbx *rx_mbx = &fbd->mbx[FBNIC_IPC_MBX_RX_IDX];
	u8 tail = rx_mbx->tail, head = rx_mbx->head, count;
	int err = 0;

	/* Do nothing if mailbox is not ready, or we already have pages on
	 * the ring that can be used by the firmware
	 */
	if (!rx_mbx->ready)
		return -ENODEV;

	/* Fill all but 1 unused descriptors in the Rx queue. */
	count = (head - tail - 1) % FBNIC_IPC_MBX_DESC_LEN;
	while (!err && count--) {
		struct fbnic_tlv_msg *msg;

		msg = (struct fbnic_tlv_msg *)__get_free_page(GFP_ATOMIC |
							      __GFP_NOWARN);
		if (!msg) {
			err = -ENOMEM;
			break;
		}

		err = fbnic_mbx_map_msg(fbd, FBNIC_IPC_MBX_RX_IDX, msg,
					FBNIC_RX_PAGE_SIZE, 0);
		if (err)
			free_page((unsigned long)msg);
	}

	return err;
}

static void fbnic_mbx_process_tx_msgs(struct fbnic_dev *fbd)
{
	struct fbnic_fw_mbx *tx_mbx = &fbd->mbx[FBNIC_IPC_MBX_TX_IDX];
	u8 head = tx_mbx->head;
	u64 desc;

	while (head != tx_mbx->tail) {
		desc = __fbnic_mbx_rd_desc(fbd, FBNIC_IPC_MBX_TX_IDX, head);
		if (!(desc & FBNIC_IPC_MBX_DESC_FW_CMPL))
			break;

		fbnic_mbx_unmap_and_free_msg(fbd, FBNIC_IPC_MBX_TX_IDX, head);

		head++;
		head %= FBNIC_IPC_MBX_DESC_LEN;
	}

	/* Record head for next interrupt */
	tx_mbx->head = head;
}

static void fbnic_mbx_postinit_desc_ring(struct fbnic_dev *fbd, int mbx_idx)
{
	struct fbnic_fw_mbx *mbx = &fbd->mbx[mbx_idx];

	/* This is a one time init, so just exit if it is completed */
	if (mbx->ready)
		return;

	mbx->ready = true;

	switch (mbx_idx) {
	case FBNIC_IPC_MBX_RX_IDX:
		/* Make sure we have a page for the FW to write to */
		fbnic_mbx_alloc_rx_msgs(fbd);
		break;
	}
}

static void fbnic_mbx_postinit(struct fbnic_dev *fbd)
{
	int i;

	/* We only need to do this on the first interrupt following init.
	 * this primes the mailbox so that we will have cleared all the
	 * skip descriptors.
	 */
	if (!(rd32(fbd, FBNIC_INTR_STATUS(0)) & (1u << FBNIC_FW_MSIX_ENTRY)))
		return;

	wr32(fbd, FBNIC_INTR_CLEAR(0), 1u << FBNIC_FW_MSIX_ENTRY);

	for (i = 0; i < FBNIC_IPC_MBX_INDICES; i++)
		fbnic_mbx_postinit_desc_ring(fbd, i);
}

static const struct fbnic_tlv_parser fbnic_fw_tlv_parser[] = {
	FBNIC_TLV_MSG_ERROR
};

static void fbnic_mbx_process_rx_msgs(struct fbnic_dev *fbd)
{
	struct fbnic_fw_mbx *rx_mbx = &fbd->mbx[FBNIC_IPC_MBX_RX_IDX];
	u8 head = rx_mbx->head;
	u64 desc, length;

	while (head != rx_mbx->tail) {
		struct fbnic_tlv_msg *msg;
		int err;

		desc = __fbnic_mbx_rd_desc(fbd, FBNIC_IPC_MBX_RX_IDX, head);
		if (!(desc & FBNIC_IPC_MBX_DESC_FW_CMPL))
			break;

		dma_unmap_single(fbd->dev, rx_mbx->buf_info[head].addr,
				 PAGE_SIZE, DMA_FROM_DEVICE);

		msg = rx_mbx->buf_info[head].msg;

		length = FIELD_GET(FBNIC_IPC_MBX_DESC_LEN_MASK, desc);

		/* Ignore NULL mailbox descriptors */
		if (!length)
			goto next_page;

		/* Report descriptors with length greater than page size */
		if (length > PAGE_SIZE) {
			dev_warn(fbd->dev,
				 "Invalid mailbox descriptor length: %lld\n",
				 length);
			goto next_page;
		}

		if (le16_to_cpu(msg->hdr.len) * sizeof(u32) > length)
			dev_warn(fbd->dev, "Mailbox message length mismatch\n");

		/* If parsing fails dump contents of message to dmesg */
		err = fbnic_tlv_msg_parse(fbd, msg, fbnic_fw_tlv_parser);
		if (err) {
			dev_warn(fbd->dev, "Unable to process message: %d\n",
				 err);
			print_hex_dump(KERN_WARNING, "fbnic:",
				       DUMP_PREFIX_OFFSET, 16, 2,
				       msg, length, true);
		}

		dev_dbg(fbd->dev, "Parsed msg type %d\n", msg->hdr.type);
next_page:

		free_page((unsigned long)rx_mbx->buf_info[head].msg);
		rx_mbx->buf_info[head].msg = NULL;

		head++;
		head %= FBNIC_IPC_MBX_DESC_LEN;
	}

	/* Record head for next interrupt */
	rx_mbx->head = head;

	/* Make sure we have at least one page for the FW to write to */
	fbnic_mbx_alloc_rx_msgs(fbd);
}

void fbnic_mbx_poll(struct fbnic_dev *fbd)
{
	fbnic_mbx_postinit(fbd);

	fbnic_mbx_process_tx_msgs(fbd);
	fbnic_mbx_process_rx_msgs(fbd);
}

int fbnic_mbx_poll_tx_ready(struct fbnic_dev *fbd)
{
	struct fbnic_fw_mbx *tx_mbx;
	int attempts = 50;

	/* Immediate fail if BAR4 isn't there */
	if (!fbnic_fw_present(fbd))
		return -ENODEV;

	tx_mbx = &fbd->mbx[FBNIC_IPC_MBX_TX_IDX];
	while (!tx_mbx->ready && --attempts) {
		/* Force the firmware to trigger an interrupt response to
		 * avoid the mailbox getting stuck closed if the interrupt
		 * is reset.
		 */
		fbnic_mbx_init_desc_ring(fbd, FBNIC_IPC_MBX_TX_IDX);

		msleep(200);

		fbnic_mbx_poll(fbd);
	}

	return attempts ? 0 : -ETIMEDOUT;
}

void fbnic_mbx_flush_tx(struct fbnic_dev *fbd)
{
	struct fbnic_fw_mbx *tx_mbx;
	int attempts = 50;
	u8 count = 0;

	/* Nothing to do if there is no mailbox */
	if (!fbnic_fw_present(fbd))
		return;

	/* Record current Rx stats */
	tx_mbx = &fbd->mbx[FBNIC_IPC_MBX_TX_IDX];

	/* Nothing to do if mailbox never got to ready */
	if (!tx_mbx->ready)
		return;

	/* Give firmware time to process packet,
	 * we will wait up to 10 seconds which is 50 waits of 200ms.
	 */
	do {
		u8 head = tx_mbx->head;

		if (head == tx_mbx->tail)
			break;

		msleep(200);
		fbnic_mbx_process_tx_msgs(fbd);

		count += (tx_mbx->head - head) % FBNIC_IPC_MBX_DESC_LEN;
	} while (count < FBNIC_IPC_MBX_DESC_LEN && --attempts);
}
