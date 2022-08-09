/*
 * Copyright (c) 2014 Redpine Signals Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include <linux/firmware.h>
#include <net/rsi_91x.h>
#include "rsi_sdio.h"
#include "rsi_common.h"

/**
 * rsi_sdio_master_access_msword() - This function sets the AHB master access
 *				     MS word in the SDIO slave registers.
 * @adapter: Pointer to the adapter structure.
 * @ms_word: ms word need to be initialized.
 *
 * Return: status: 0 on success, -1 on failure.
 */
int rsi_sdio_master_access_msword(struct rsi_hw *adapter, u16 ms_word)
{
	u8 byte;
	u8 function = 0;
	int status = 0;

	byte = (u8)(ms_word & 0x00FF);

	rsi_dbg(INIT_ZONE,
		"%s: MASTER_ACCESS_MSBYTE:0x%x\n", __func__, byte);

	status = rsi_sdio_write_register(adapter,
					 function,
					 SDIO_MASTER_ACCESS_MSBYTE,
					 &byte);
	if (status) {
		rsi_dbg(ERR_ZONE,
			"%s: fail to access MASTER_ACCESS_MSBYTE\n",
			__func__);
		return -1;
	}

	byte = (u8)(ms_word >> 8);

	rsi_dbg(INIT_ZONE, "%s:MASTER_ACCESS_LSBYTE:0x%x\n", __func__, byte);
	status = rsi_sdio_write_register(adapter,
					 function,
					 SDIO_MASTER_ACCESS_LSBYTE,
					 &byte);
	return status;
}

static void rsi_rx_handler(struct rsi_hw *adapter);

void rsi_sdio_rx_thread(struct rsi_common *common)
{
	struct rsi_hw *adapter = common->priv;
	struct rsi_91x_sdiodev *sdev = adapter->rsi_dev;

	do {
		rsi_wait_event(&sdev->rx_thread.event, EVENT_WAIT_FOREVER);
		rsi_reset_event(&sdev->rx_thread.event);
		rsi_rx_handler(adapter);
	} while (!atomic_read(&sdev->rx_thread.thread_done));

	rsi_dbg(INFO_ZONE, "%s: Terminated SDIO RX thread\n", __func__);
	atomic_inc(&sdev->rx_thread.thread_done);
	kthread_complete_and_exit(&sdev->rx_thread.completion, 0);
}

/**
 * rsi_process_pkt() - This Function reads rx_blocks register and figures out
 *		       the size of the rx pkt.
 * @common: Pointer to the driver private structure.
 *
 * Return: 0 on success, -1 on failure.
 */
static int rsi_process_pkt(struct rsi_common *common)
{
	struct rsi_hw *adapter = common->priv;
	struct rsi_91x_sdiodev *dev =
		(struct rsi_91x_sdiodev *)adapter->rsi_dev;
	u8 num_blks = 0;
	u32 rcv_pkt_len = 0;
	int status = 0;
	u8 value = 0;

	num_blks = ((adapter->interrupt_status & 1) |
			((adapter->interrupt_status >> RECV_NUM_BLOCKS) << 1));

	if (!num_blks) {
		status = rsi_sdio_read_register(adapter,
						SDIO_RX_NUM_BLOCKS_REG,
						&value);
		if (status) {
			rsi_dbg(ERR_ZONE,
				"%s: Failed to read pkt length from the card:\n",
				__func__);
			return status;
		}
		num_blks = value & 0x1f;
	}

	if (dev->write_fail == 2)
		rsi_sdio_ack_intr(common->priv, (1 << MSDU_PKT_PENDING));

	if (unlikely(!num_blks)) {
		dev->write_fail = 2;
		return -1;
	}

	rcv_pkt_len = (num_blks * 256);

	status = rsi_sdio_host_intf_read_pkt(adapter, dev->pktbuffer,
					     rcv_pkt_len);
	if (status) {
		rsi_dbg(ERR_ZONE, "%s: Failed to read packet from card\n",
			__func__);
		return status;
	}

	status = rsi_read_pkt(common, dev->pktbuffer, rcv_pkt_len);
	if (status) {
		rsi_dbg(ERR_ZONE, "Failed to read the packet\n");
		return status;
	}

	return 0;
}

/**
 * rsi_init_sdio_slave_regs() - This function does the actual initialization
 *				of SDBUS slave registers.
 * @adapter: Pointer to the adapter structure.
 *
 * Return: status: 0 on success, -1 on failure.
 */
int rsi_init_sdio_slave_regs(struct rsi_hw *adapter)
{
	struct rsi_91x_sdiodev *dev =
		(struct rsi_91x_sdiodev *)adapter->rsi_dev;
	u8 function = 0;
	u8 byte;
	int status = 0;

	if (dev->next_read_delay) {
		byte = dev->next_read_delay;
		status = rsi_sdio_write_register(adapter,
						 function,
						 SDIO_NXT_RD_DELAY2,
						 &byte);
		if (status) {
			rsi_dbg(ERR_ZONE,
				"%s: Failed to write SDIO_NXT_RD_DELAY2\n",
				__func__);
			return -1;
		}
	}

	if (dev->sdio_high_speed_enable) {
		rsi_dbg(INIT_ZONE, "%s: Enabling SDIO High speed\n", __func__);
		byte = 0x3;

		status = rsi_sdio_write_register(adapter,
						 function,
						 SDIO_REG_HIGH_SPEED,
						 &byte);
		if (status) {
			rsi_dbg(ERR_ZONE,
				"%s: Failed to enable SDIO high speed\n",
				__func__);
			return -1;
		}
	}

	/* This tells SDIO FIFO when to start read to host */
	rsi_dbg(INIT_ZONE, "%s: Initializing SDIO read start level\n", __func__);
	byte = 0x24;

	status = rsi_sdio_write_register(adapter,
					 function,
					 SDIO_READ_START_LVL,
					 &byte);
	if (status) {
		rsi_dbg(ERR_ZONE,
			"%s: Failed to write SDIO_READ_START_LVL\n", __func__);
		return -1;
	}

	rsi_dbg(INIT_ZONE, "%s: Initializing FIFO ctrl registers\n", __func__);
	byte = (128 - 32);

	status = rsi_sdio_write_register(adapter,
					 function,
					 SDIO_READ_FIFO_CTL,
					 &byte);
	if (status) {
		rsi_dbg(ERR_ZONE,
			"%s: Failed to write SDIO_READ_FIFO_CTL\n", __func__);
		return -1;
	}

	byte = 32;
	status = rsi_sdio_write_register(adapter,
					 function,
					 SDIO_WRITE_FIFO_CTL,
					 &byte);
	if (status) {
		rsi_dbg(ERR_ZONE,
			"%s: Failed to write SDIO_WRITE_FIFO_CTL\n", __func__);
		return -1;
	}

	return 0;
}

/**
 * rsi_rx_handler() - Read and process SDIO interrupts.
 * @adapter: Pointer to the adapter structure.
 *
 * Return: None.
 */
static void rsi_rx_handler(struct rsi_hw *adapter)
{
	struct rsi_common *common = adapter->priv;
	struct rsi_91x_sdiodev *dev =
		(struct rsi_91x_sdiodev *)adapter->rsi_dev;
	int status;
	u8 isr_status = 0;
	u8 fw_status = 0;

	dev->rx_info.sdio_int_counter++;

	do {
		mutex_lock(&common->rx_lock);
		status = rsi_sdio_read_register(common->priv,
						RSI_FN1_INT_REGISTER,
						&isr_status);
		if (status) {
			rsi_dbg(ERR_ZONE,
				"%s: Failed to Read Intr Status Register\n",
				__func__);
			mutex_unlock(&common->rx_lock);
			return;
		}
		adapter->interrupt_status = isr_status;

		if (isr_status == 0) {
			rsi_set_event(&common->tx_thread.event);
			dev->rx_info.sdio_intr_status_zero++;
			mutex_unlock(&common->rx_lock);
			return;
		}

		rsi_dbg(ISR_ZONE, "%s: Intr_status = %x %d %d\n",
			__func__, isr_status, (1 << MSDU_PKT_PENDING),
			(1 << FW_ASSERT_IND));

		if (isr_status & BIT(PKT_BUFF_AVAILABLE)) {
			status = rsi_sdio_check_buffer_status(adapter, 0);
			if (status < 0)
				rsi_dbg(ERR_ZONE,
					"%s: Failed to check buffer status\n",
					__func__);
			rsi_sdio_ack_intr(common->priv,
					  BIT(PKT_BUFF_AVAILABLE));
			rsi_set_event(&common->tx_thread.event);

			rsi_dbg(ISR_ZONE, "%s: ==> BUFFER_AVAILABLE <==\n",
				__func__);
			dev->buff_status_updated = true;

			isr_status &= ~BIT(PKT_BUFF_AVAILABLE);
		}

		if (isr_status & BIT(FW_ASSERT_IND)) {
			rsi_dbg(ERR_ZONE, "%s: ==> FIRMWARE Assert <==\n",
				__func__);
			status = rsi_sdio_read_register(common->priv,
							SDIO_FW_STATUS_REG,
							&fw_status);
			if (status) {
				rsi_dbg(ERR_ZONE,
					"%s: Failed to read f/w reg\n",
					__func__);
			} else {
				rsi_dbg(ERR_ZONE,
					"%s: Firmware Status is 0x%x\n",
					__func__, fw_status);
				rsi_sdio_ack_intr(common->priv,
						  BIT(FW_ASSERT_IND));
			}

			common->fsm_state = FSM_CARD_NOT_READY;

			isr_status &= ~BIT(FW_ASSERT_IND);
		}

		if (isr_status & BIT(MSDU_PKT_PENDING)) {
			rsi_dbg(ISR_ZONE, "Pkt pending interrupt\n");
			dev->rx_info.total_sdio_msdu_pending_intr++;

			status = rsi_process_pkt(common);
			if (status) {
				rsi_dbg(ERR_ZONE, "%s: Failed to read pkt\n",
					__func__);
				mutex_unlock(&common->rx_lock);
				return;
			}

			isr_status &= ~BIT(MSDU_PKT_PENDING);
		}

		if (isr_status) {
			rsi_sdio_ack_intr(common->priv, isr_status);
			dev->rx_info.total_sdio_unknown_intr++;
			isr_status = 0;
			rsi_dbg(ISR_ZONE, "Unknown Interrupt %x\n",
				isr_status);
		}

		mutex_unlock(&common->rx_lock);
	} while (1);
}

/* This function is used to read buffer status register and
 * set relevant fields in rsi_91x_sdiodev struct.
 */
int rsi_sdio_check_buffer_status(struct rsi_hw *adapter, u8 q_num)
{
	struct rsi_common *common = adapter->priv;
	struct rsi_91x_sdiodev *dev =
		(struct rsi_91x_sdiodev *)adapter->rsi_dev;
	u8 buf_status = 0;
	int status = 0;
	static int counter = 4;

	if (!dev->buff_status_updated && counter) {
		counter--;
		goto out;
	}

	dev->buff_status_updated = false;
	status = rsi_sdio_read_register(common->priv,
					RSI_DEVICE_BUFFER_STATUS_REGISTER,
					&buf_status);

	if (status) {
		rsi_dbg(ERR_ZONE,
			"%s: Failed to read status register\n", __func__);
		return -1;
	}

	if (buf_status & (BIT(PKT_MGMT_BUFF_FULL))) {
		if (!dev->rx_info.mgmt_buffer_full)
			dev->rx_info.mgmt_buf_full_counter++;
		dev->rx_info.mgmt_buffer_full = true;
	} else {
		dev->rx_info.mgmt_buffer_full = false;
	}

	if (buf_status & (BIT(PKT_BUFF_FULL))) {
		if (!dev->rx_info.buffer_full)
			dev->rx_info.buf_full_counter++;
		dev->rx_info.buffer_full = true;
	} else {
		dev->rx_info.buffer_full = false;
	}

	if (buf_status & (BIT(PKT_BUFF_SEMI_FULL))) {
		if (!dev->rx_info.semi_buffer_full)
			dev->rx_info.buf_semi_full_counter++;
		dev->rx_info.semi_buffer_full = true;
	} else {
		dev->rx_info.semi_buffer_full = false;
	}

	if (dev->rx_info.mgmt_buffer_full || dev->rx_info.buf_full_counter)
		counter = 1;
	else
		counter = 4;

out:
	if ((q_num == MGMT_SOFT_Q) && (dev->rx_info.mgmt_buffer_full))
		return QUEUE_FULL;

	if ((q_num < MGMT_SOFT_Q) && (dev->rx_info.buffer_full))
		return QUEUE_FULL;

	return QUEUE_NOT_FULL;
}

/**
 * rsi_sdio_determine_event_timeout() - This Function determines the event
 *					timeout duration.
 * @adapter: Pointer to the adapter structure.
 *
 * Return: timeout duration is returned.
 */
int rsi_sdio_determine_event_timeout(struct rsi_hw *adapter)
{
	struct rsi_91x_sdiodev *dev =
		(struct rsi_91x_sdiodev *)adapter->rsi_dev;

	/* Once buffer full is seen, event timeout to occur every 2 msecs */
	if (dev->rx_info.buffer_full)
		return 2;

	return EVENT_WAIT_FOREVER;
}
