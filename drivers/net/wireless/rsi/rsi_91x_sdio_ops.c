/**
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
static int rsi_sdio_master_access_msword(struct rsi_hw *adapter,
					 u16 ms_word)
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

/**
 * rsi_copy_to_card() - This function includes the actual funtionality of
 *			copying the TA firmware to the card.Basically this
 *			function includes opening the TA file,reading the
 *			TA file and writing their values in blocks of data.
 * @common: Pointer to the driver private structure.
 * @fw: Pointer to the firmware value to be written.
 * @len: length of firmware file.
 * @num_blocks: Number of blocks to be written to the card.
 *
 * Return: 0 on success and -1 on failure.
 */
static int rsi_copy_to_card(struct rsi_common *common,
			    const u8 *fw,
			    u32 len,
			    u32 num_blocks)
{
	struct rsi_hw *adapter = common->priv;
	struct rsi_91x_sdiodev *dev =
		(struct rsi_91x_sdiodev *)adapter->rsi_dev;
	u32 indx, ii;
	u32 block_size = dev->tx_blk_size;
	u32 lsb_address;
	__le32 data[] = { TA_HOLD_THREAD_VALUE, TA_SOFT_RST_CLR,
			  TA_PC_ZERO, TA_RELEASE_THREAD_VALUE };
	u32 address[] = { TA_HOLD_THREAD_REG, TA_SOFT_RESET_REG,
			  TA_TH0_PC_REG, TA_RELEASE_THREAD_REG };
	u32 base_address;
	u16 msb_address;

	base_address = TA_LOAD_ADDRESS;
	msb_address = base_address >> 16;

	for (indx = 0, ii = 0; ii < num_blocks; ii++, indx += block_size) {
		lsb_address = ((u16) base_address | RSI_SD_REQUEST_MASTER);
		if (rsi_sdio_write_register_multiple(adapter,
						     lsb_address,
						     (u8 *)(fw + indx),
						     block_size)) {
			rsi_dbg(ERR_ZONE,
				"%s: Unable to load %s blk\n", __func__,
				FIRMWARE_RSI9113);
			return -1;
		}
		rsi_dbg(INIT_ZONE, "%s: loading block: %d\n", __func__, ii);
		base_address += block_size;
		if ((base_address >> 16) != msb_address) {
			msb_address += 1;
			if (rsi_sdio_master_access_msword(adapter,
							  msb_address)) {
				rsi_dbg(ERR_ZONE,
					"%s: Unable to set ms word reg\n",
					__func__);
				return -1;
			}
		}
	}

	if (len % block_size) {
		lsb_address = ((u16) base_address | RSI_SD_REQUEST_MASTER);
		if (rsi_sdio_write_register_multiple(adapter,
						     lsb_address,
						     (u8 *)(fw + indx),
						     len % block_size)) {
			rsi_dbg(ERR_ZONE,
				"%s: Unable to load f/w\n", __func__);
			return -1;
		}
	}
	rsi_dbg(INIT_ZONE,
		"%s: Succesfully loaded TA instructions\n", __func__);

	if (rsi_sdio_master_access_msword(adapter, TA_BASE_ADDR)) {
		rsi_dbg(ERR_ZONE,
			"%s: Unable to set ms word to common reg\n",
			__func__);
		return -1;
	}

	for (ii = 0; ii < ARRAY_SIZE(data); ii++) {
		/* Bringing TA out of reset */
		if (rsi_sdio_write_register_multiple(adapter,
						     (address[ii] |
						     RSI_SD_REQUEST_MASTER),
						     (u8 *)&data[ii],
						     4)) {
			rsi_dbg(ERR_ZONE,
				"%s: Unable to hold TA threads\n", __func__);
			return -1;
		}
	}

	rsi_dbg(INIT_ZONE, "%s: loaded firmware\n", __func__);
	return 0;
}

/**
 * rsi_load_ta_instructions() - This function includes the actual funtionality
 *				of loading the TA firmware.This function also
 *				includes opening the TA file,reading the TA
 *				file and writing their value in blocks of data.
 * @common: Pointer to the driver private structure.
 *
 * Return: status: 0 on success, -1 on failure.
 */
static int rsi_load_ta_instructions(struct rsi_common *common)
{
	struct rsi_hw *adapter = common->priv;
	struct rsi_91x_sdiodev *dev =
		(struct rsi_91x_sdiodev *)adapter->rsi_dev;
	u32 len;
	u32 num_blocks;
	const struct firmware *fw_entry = NULL;
	u32 block_size = dev->tx_blk_size;
	int status = 0;
	u32 base_address;
	u16 msb_address;

	if (rsi_sdio_master_access_msword(adapter, TA_BASE_ADDR)) {
		rsi_dbg(ERR_ZONE,
			"%s: Unable to set ms word to common reg\n",
			__func__);
		return -1;
	}
	base_address = TA_LOAD_ADDRESS;
	msb_address = (base_address >> 16);

	if (rsi_sdio_master_access_msword(adapter, msb_address)) {
		rsi_dbg(ERR_ZONE,
			"%s: Unable to set ms word reg\n", __func__);
		return -1;
	}

	status = request_firmware(&fw_entry, FIRMWARE_RSI9113, adapter->device);
	if (status < 0) {
		rsi_dbg(ERR_ZONE, "%s Firmware file %s not found\n",
			__func__, FIRMWARE_RSI9113);
		return status;
	}

	len = fw_entry->size;

	if (len % 4)
		len += (4 - (len % 4));

	num_blocks = (len / block_size);

	rsi_dbg(INIT_ZONE, "%s: Instruction size:%d\n", __func__, len);
	rsi_dbg(INIT_ZONE, "%s: num blocks: %d\n", __func__, num_blocks);

	status = rsi_copy_to_card(common, fw_entry->data, len, num_blocks);
	release_firmware(fw_entry);
	return status;
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
	u8 num_blks = 0;
	u32 rcv_pkt_len = 0;
	int status = 0;

	status = rsi_sdio_read_register(adapter,
					SDIO_RX_NUM_BLOCKS_REG,
					&num_blks);

	if (status) {
		rsi_dbg(ERR_ZONE,
			"%s: Failed to read pkt length from the card:\n",
			__func__);
		return status;
	}
	rcv_pkt_len = (num_blks * 256);

	common->rx_data_pkt = kmalloc(rcv_pkt_len, GFP_KERNEL);
	if (!common->rx_data_pkt) {
		rsi_dbg(ERR_ZONE, "%s: Failed in memory allocation\n",
			__func__);
		return -ENOMEM;
	}

	status = rsi_sdio_host_intf_read_pkt(adapter,
					     common->rx_data_pkt,
					     rcv_pkt_len);
	if (status) {
		rsi_dbg(ERR_ZONE, "%s: Failed to read packet from card\n",
			__func__);
		goto fail;
	}

	status = rsi_read_pkt(common, rcv_pkt_len);

fail:
	kfree(common->rx_data_pkt);
	return status;
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
	rsi_dbg(INIT_ZONE, "%s: Initialzing SDIO read start level\n", __func__);
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

	rsi_dbg(INIT_ZONE, "%s: Initialzing FIFO ctrl registers\n", __func__);
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
 * rsi_interrupt_handler() - This function read and process SDIO interrupts.
 * @adapter: Pointer to the adapter structure.
 *
 * Return: None.
 */
void rsi_interrupt_handler(struct rsi_hw *adapter)
{
	struct rsi_common *common = adapter->priv;
	struct rsi_91x_sdiodev *dev =
		(struct rsi_91x_sdiodev *)adapter->rsi_dev;
	int status;
	enum sdio_interrupt_type isr_type;
	u8 isr_status = 0;
	u8 fw_status = 0;

	dev->rx_info.sdio_int_counter++;

	do {
		mutex_lock(&common->tx_rxlock);
		status = rsi_sdio_read_register(common->priv,
						RSI_FN1_INT_REGISTER,
						&isr_status);
		if (status) {
			rsi_dbg(ERR_ZONE,
				"%s: Failed to Read Intr Status Register\n",
				__func__);
			mutex_unlock(&common->tx_rxlock);
			return;
		}

		if (isr_status == 0) {
			rsi_set_event(&common->tx_thread.event);
			dev->rx_info.sdio_intr_status_zero++;
			mutex_unlock(&common->tx_rxlock);
			return;
		}

		rsi_dbg(ISR_ZONE, "%s: Intr_status = %x %d %d\n",
			__func__, isr_status, (1 << MSDU_PKT_PENDING),
			(1 << FW_ASSERT_IND));

		do {
			RSI_GET_SDIO_INTERRUPT_TYPE(isr_status, isr_type);

			switch (isr_type) {
			case BUFFER_AVAILABLE:
				dev->rx_info.watch_bufferfull_count = 0;
				dev->rx_info.buffer_full = false;
				dev->rx_info.semi_buffer_full = false;
				dev->rx_info.mgmt_buffer_full = false;
				rsi_sdio_ack_intr(common->priv,
						  (1 << PKT_BUFF_AVAILABLE));
				rsi_set_event(&common->tx_thread.event);

				rsi_dbg(ISR_ZONE,
					"%s: ==> BUFFER_AVAILABLE <==\n",
					__func__);
				dev->rx_info.buf_available_counter++;
				break;

			case FIRMWARE_ASSERT_IND:
				rsi_dbg(ERR_ZONE,
					"%s: ==> FIRMWARE Assert <==\n",
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
						__func__ , fw_status);
					rsi_sdio_ack_intr(common->priv,
							  (1 << FW_ASSERT_IND));
				}

				common->fsm_state = FSM_CARD_NOT_READY;
				break;

			case MSDU_PACKET_PENDING:
				rsi_dbg(ISR_ZONE, "Pkt pending interrupt\n");
				dev->rx_info.total_sdio_msdu_pending_intr++;

				status = rsi_process_pkt(common);
				if (status) {
					rsi_dbg(ERR_ZONE,
						"%s: Failed to read pkt\n",
						__func__);
					mutex_unlock(&common->tx_rxlock);
					return;
				}
				break;
			default:
				rsi_sdio_ack_intr(common->priv, isr_status);
				dev->rx_info.total_sdio_unknown_intr++;
				isr_status = 0;
				rsi_dbg(ISR_ZONE,
					"Unknown Interrupt %x\n",
					isr_status);
				break;
			}
			isr_status ^= BIT(isr_type - 1);
		} while (isr_status);
		mutex_unlock(&common->tx_rxlock);
	} while (1);
}

/**
 * rsi_device_init() - This Function Initializes The HAL.
 * @common: Pointer to the driver private structure.
 *
 * Return: 0 on success, -1 on failure.
 */
int rsi_sdio_device_init(struct rsi_common *common)
{
	if (rsi_load_ta_instructions(common))
		return -1;

	if (rsi_sdio_master_access_msword(common->priv, MISC_CFG_BASE_ADDR)) {
		rsi_dbg(ERR_ZONE, "%s: Unable to set ms word reg\n",
			__func__);
		return -1;
	}
	rsi_dbg(INIT_ZONE,
		"%s: Setting ms word to 0x41050000\n", __func__);

	return 0;
}

/**
 * rsi_sdio_read_buffer_status_register() - This function is used to the read
 *					    buffer status register and set
 *					    relevant fields in
 *					    rsi_91x_sdiodev struct.
 * @adapter: Pointer to the driver hw structure.
 * @q_num: The Q number whose status is to be found.
 *
 * Return: status: -1 on failure or else queue full/stop is indicated.
 */
int rsi_sdio_read_buffer_status_register(struct rsi_hw *adapter, u8 q_num)
{
	struct rsi_common *common = adapter->priv;
	struct rsi_91x_sdiodev *dev =
		(struct rsi_91x_sdiodev *)adapter->rsi_dev;
	u8 buf_status = 0;
	int status = 0;

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

	if ((q_num == MGMT_SOFT_Q) && (dev->rx_info.mgmt_buffer_full))
		return QUEUE_FULL;

	if (dev->rx_info.buffer_full)
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
