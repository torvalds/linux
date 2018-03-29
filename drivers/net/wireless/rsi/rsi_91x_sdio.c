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

#include <linux/module.h>
#include "rsi_sdio.h"
#include "rsi_common.h"
#include "rsi_coex.h"
#include "rsi_hal.h"

/* Default operating mode is wlan STA + BT */
static u16 dev_oper_mode = DEV_OPMODE_STA_BT_DUAL;
module_param(dev_oper_mode, ushort, 0444);
MODULE_PARM_DESC(dev_oper_mode,
		 "1[Wi-Fi], 4[BT], 8[BT LE], 5[Wi-Fi STA + BT classic]\n"
		 "9[Wi-Fi STA + BT LE], 13[Wi-Fi STA + BT classic + BT LE]\n"
		 "6[AP + BT classic], 14[AP + BT classic + BT LE]");

/**
 * rsi_sdio_set_cmd52_arg() - This function prepares cmd 52 read/write arg.
 * @rw: Read/write
 * @func: function number
 * @raw: indicates whether to perform read after write
 * @address: address to which to read/write
 * @writedata: data to write
 *
 * Return: argument
 */
static u32 rsi_sdio_set_cmd52_arg(bool rw,
				  u8 func,
				  u8 raw,
				  u32 address,
				  u8 writedata)
{
	return ((rw & 1) << 31) | ((func & 0x7) << 28) |
		((raw & 1) << 27) | (1 << 26) |
		((address & 0x1FFFF) << 9) | (1 << 8) |
		(writedata & 0xFF);
}

/**
 * rsi_cmd52writebyte() - This function issues cmd52 byte write onto the card.
 * @card: Pointer to the mmc_card.
 * @address: Address to write.
 * @byte: Data to write.
 *
 * Return: Write status.
 */
static int rsi_cmd52writebyte(struct mmc_card *card,
			      u32 address,
			      u8 byte)
{
	struct mmc_command io_cmd;
	u32 arg;

	memset(&io_cmd, 0, sizeof(io_cmd));
	arg = rsi_sdio_set_cmd52_arg(1, 0, 0, address, byte);
	io_cmd.opcode = SD_IO_RW_DIRECT;
	io_cmd.arg = arg;
	io_cmd.flags = MMC_RSP_R5 | MMC_CMD_AC;

	return mmc_wait_for_cmd(card->host, &io_cmd, 0);
}

/**
 * rsi_cmd52readbyte() - This function issues cmd52 byte read onto the card.
 * @card: Pointer to the mmc_card.
 * @address: Address to read from.
 * @byte: Variable to store read value.
 *
 * Return: Read status.
 */
static int rsi_cmd52readbyte(struct mmc_card *card,
			     u32 address,
			     u8 *byte)
{
	struct mmc_command io_cmd;
	u32 arg;
	int err;

	memset(&io_cmd, 0, sizeof(io_cmd));
	arg = rsi_sdio_set_cmd52_arg(0, 0, 0, address, 0);
	io_cmd.opcode = SD_IO_RW_DIRECT;
	io_cmd.arg = arg;
	io_cmd.flags = MMC_RSP_R5 | MMC_CMD_AC;

	err = mmc_wait_for_cmd(card->host, &io_cmd, 0);
	if ((!err) && (byte))
		*byte =  io_cmd.resp[0] & 0xFF;
	return err;
}

/**
 * rsi_issue_sdiocommand() - This function issues sdio commands.
 * @func: Pointer to the sdio_func structure.
 * @opcode: Opcode value.
 * @arg: Arguments to pass.
 * @flags: Flags which are set.
 * @resp: Pointer to store response.
 *
 * Return: err: command status as 0 or -1.
 */
static int rsi_issue_sdiocommand(struct sdio_func *func,
				 u32 opcode,
				 u32 arg,
				 u32 flags,
				 u32 *resp)
{
	struct mmc_command cmd;
	struct mmc_host *host;
	int err;

	host = func->card->host;

	memset(&cmd, 0, sizeof(struct mmc_command));
	cmd.opcode = opcode;
	cmd.arg = arg;
	cmd.flags = flags;
	err = mmc_wait_for_cmd(host, &cmd, 3);

	if ((!err) && (resp))
		*resp = cmd.resp[0];

	return err;
}

/**
 * rsi_handle_interrupt() - This function is called upon the occurence
 *			    of an interrupt.
 * @function: Pointer to the sdio_func structure.
 *
 * Return: None.
 */
static void rsi_handle_interrupt(struct sdio_func *function)
{
	struct rsi_hw *adapter = sdio_get_drvdata(function);
	struct rsi_91x_sdiodev *dev =
		(struct rsi_91x_sdiodev *)adapter->rsi_dev;

	if (adapter->priv->fsm_state == FSM_FW_NOT_LOADED)
		return;

	dev->sdio_irq_task = current;
	rsi_interrupt_handler(adapter);
	dev->sdio_irq_task = NULL;
}

/**
 * rsi_reset_card() - This function resets and re-initializes the card.
 * @pfunction: Pointer to the sdio_func structure.
 *
 * Return: None.
 */
static void rsi_reset_card(struct sdio_func *pfunction)
{
	int ret = 0;
	int err;
	struct mmc_card *card = pfunction->card;
	struct mmc_host *host = card->host;
	s32 bit = (fls(host->ocr_avail) - 1);
	u8 cmd52_resp;
	u32 clock, resp, i;
	u16 rca;

	/* Reset 9110 chip */
	ret = rsi_cmd52writebyte(pfunction->card,
				 SDIO_CCCR_ABORT,
				 (1 << 3));

	/* Card will not send any response as it is getting reset immediately
	 * Hence expect a timeout status from host controller
	 */
	if (ret != -ETIMEDOUT)
		rsi_dbg(ERR_ZONE, "%s: Reset failed : %d\n", __func__, ret);

	/* Wait for few milli seconds to get rid of residue charges if any */
	msleep(20);

	/* Initialize the SDIO card */
	host->ios.vdd = bit;
	host->ios.chip_select = MMC_CS_DONTCARE;
	host->ios.bus_mode = MMC_BUSMODE_OPENDRAIN;
	host->ios.power_mode = MMC_POWER_UP;
	host->ios.bus_width = MMC_BUS_WIDTH_1;
	host->ios.timing = MMC_TIMING_LEGACY;
	host->ops->set_ios(host, &host->ios);

	/*
	 * This delay should be sufficient to allow the power supply
	 * to reach the minimum voltage.
	 */
	msleep(20);

	host->ios.clock = host->f_min;
	host->ios.power_mode = MMC_POWER_ON;
	host->ops->set_ios(host, &host->ios);

	/*
	 * This delay must be at least 74 clock sizes, or 1 ms, or the
	 * time required to reach a stable voltage.
	 */
	msleep(20);

	/* Issue CMD0. Goto idle state */
	host->ios.chip_select = MMC_CS_HIGH;
	host->ops->set_ios(host, &host->ios);
	msleep(20);
	err = rsi_issue_sdiocommand(pfunction,
				    MMC_GO_IDLE_STATE,
				    0,
				    (MMC_RSP_NONE | MMC_CMD_BC),
				    NULL);
	host->ios.chip_select = MMC_CS_DONTCARE;
	host->ops->set_ios(host, &host->ios);
	msleep(20);
	host->use_spi_crc = 0;

	if (err)
		rsi_dbg(ERR_ZONE, "%s: CMD0 failed : %d\n", __func__, err);

	/* Issue CMD5, arg = 0 */
	err = rsi_issue_sdiocommand(pfunction,	SD_IO_SEND_OP_COND, 0,
				    (MMC_RSP_R4 | MMC_CMD_BCR), &resp);
	if (err)
		rsi_dbg(ERR_ZONE, "%s: CMD5 failed : %d\n", __func__, err);
	card->ocr = resp;

	/* Issue CMD5, arg = ocr. Wait till card is ready  */
	for (i = 0; i < 100; i++) {
		err = rsi_issue_sdiocommand(pfunction, SD_IO_SEND_OP_COND,
					    card->ocr,
					    (MMC_RSP_R4 | MMC_CMD_BCR), &resp);
		if (err) {
			rsi_dbg(ERR_ZONE, "%s: CMD5 failed : %d\n",
				__func__, err);
			break;
		}

		if (resp & MMC_CARD_BUSY)
			break;
		msleep(20);
	}

	if ((i == 100) || (err)) {
		rsi_dbg(ERR_ZONE, "%s: card in not ready : %d %d\n",
			__func__, i, err);
		return;
	}

	/* Issue CMD3, get RCA */
	err = rsi_issue_sdiocommand(pfunction,
				    SD_SEND_RELATIVE_ADDR,
				    0,
				    (MMC_RSP_R6 | MMC_CMD_BCR),
				    &resp);
	if (err) {
		rsi_dbg(ERR_ZONE, "%s: CMD3 failed : %d\n", __func__, err);
		return;
	}
	rca = resp >> 16;
	host->ios.bus_mode = MMC_BUSMODE_PUSHPULL;
	host->ops->set_ios(host, &host->ios);

	/* Issue CMD7, select card  */
	err = rsi_issue_sdiocommand(pfunction,
				    MMC_SELECT_CARD,
				    (rca << 16),
				    (MMC_RSP_R1 | MMC_CMD_AC),
				    NULL);
	if (err) {
		rsi_dbg(ERR_ZONE, "%s: CMD7 failed : %d\n", __func__, err);
		return;
	}

	/* Enable high speed */
	if (card->host->caps & MMC_CAP_SD_HIGHSPEED) {
		rsi_dbg(ERR_ZONE, "%s: Set high speed mode\n", __func__);
		err = rsi_cmd52readbyte(card, SDIO_CCCR_SPEED, &cmd52_resp);
		if (err) {
			rsi_dbg(ERR_ZONE, "%s: CCCR speed reg read failed: %d\n",
				__func__, err);
		} else {
			err = rsi_cmd52writebyte(card,
						 SDIO_CCCR_SPEED,
						 (cmd52_resp | SDIO_SPEED_EHS));
			if (err) {
				rsi_dbg(ERR_ZONE,
					"%s: CCR speed regwrite failed %d\n",
					__func__, err);
				return;
			}
			host->ios.timing = MMC_TIMING_SD_HS;
			host->ops->set_ios(host, &host->ios);
		}
	}

	/* Set clock */
	if (mmc_card_hs(card))
		clock = 50000000;
	else
		clock = card->cis.max_dtr;

	if (clock > host->f_max)
		clock = host->f_max;

	host->ios.clock = clock;
	host->ops->set_ios(host, &host->ios);

	if (card->host->caps & MMC_CAP_4_BIT_DATA) {
		/* CMD52: Set bus width & disable card detect resistor */
		err = rsi_cmd52writebyte(card,
					 SDIO_CCCR_IF,
					 (SDIO_BUS_CD_DISABLE |
					  SDIO_BUS_WIDTH_4BIT));
		if (err) {
			rsi_dbg(ERR_ZONE, "%s: Set bus mode failed : %d\n",
				__func__, err);
			return;
		}
		host->ios.bus_width = MMC_BUS_WIDTH_4;
		host->ops->set_ios(host, &host->ios);
	}
}

/**
 * rsi_setclock() - This function sets the clock frequency.
 * @adapter: Pointer to the adapter structure.
 * @freq: Clock frequency.
 *
 * Return: None.
 */
static void rsi_setclock(struct rsi_hw *adapter, u32 freq)
{
	struct rsi_91x_sdiodev *dev =
		(struct rsi_91x_sdiodev *)adapter->rsi_dev;
	struct mmc_host *host = dev->pfunction->card->host;
	u32 clock;

	clock = freq * 1000;
	if (clock > host->f_max)
		clock = host->f_max;
	host->ios.clock = clock;
	host->ops->set_ios(host, &host->ios);
}

/**
 * rsi_setblocklength() - This function sets the host block length.
 * @adapter: Pointer to the adapter structure.
 * @length: Block length to be set.
 *
 * Return: status: 0 on success, -1 on failure.
 */
static int rsi_setblocklength(struct rsi_hw *adapter, u32 length)
{
	struct rsi_91x_sdiodev *dev =
		(struct rsi_91x_sdiodev *)adapter->rsi_dev;
	int status;
	rsi_dbg(INIT_ZONE, "%s: Setting the block length\n", __func__);

	status = sdio_set_block_size(dev->pfunction, length);
	dev->pfunction->max_blksize = 256;
	adapter->block_size = dev->pfunction->max_blksize;

	rsi_dbg(INFO_ZONE,
		"%s: Operational blk length is %d\n", __func__, length);
	return status;
}

/**
 * rsi_setupcard() - This function queries and sets the card's features.
 * @adapter: Pointer to the adapter structure.
 *
 * Return: status: 0 on success, -1 on failure.
 */
static int rsi_setupcard(struct rsi_hw *adapter)
{
	struct rsi_91x_sdiodev *dev =
		(struct rsi_91x_sdiodev *)adapter->rsi_dev;
	int status = 0;

	rsi_setclock(adapter, 50000);

	dev->tx_blk_size = 256;
	status = rsi_setblocklength(adapter, dev->tx_blk_size);
	if (status)
		rsi_dbg(ERR_ZONE,
			"%s: Unable to set block length\n", __func__);
	return status;
}

/**
 * rsi_sdio_read_register() - This function reads one byte of information
 *			      from a register.
 * @adapter: Pointer to the adapter structure.
 * @addr: Address of the register.
 * @data: Pointer to the data that stores the data read.
 *
 * Return: 0 on success, -1 on failure.
 */
int rsi_sdio_read_register(struct rsi_hw *adapter,
			   u32 addr,
			   u8 *data)
{
	struct rsi_91x_sdiodev *dev =
		(struct rsi_91x_sdiodev *)adapter->rsi_dev;
	u8 fun_num = 0;
	int status;

	if (likely(dev->sdio_irq_task != current))
		sdio_claim_host(dev->pfunction);

	if (fun_num == 0)
		*data = sdio_f0_readb(dev->pfunction, addr, &status);
	else
		*data = sdio_readb(dev->pfunction, addr, &status);

	if (likely(dev->sdio_irq_task != current))
		sdio_release_host(dev->pfunction);

	return status;
}

/**
 * rsi_sdio_write_register() - This function writes one byte of information
 *			       into a register.
 * @adapter: Pointer to the adapter structure.
 * @function: Function Number.
 * @addr: Address of the register.
 * @data: Pointer to the data tha has to be written.
 *
 * Return: 0 on success, -1 on failure.
 */
int rsi_sdio_write_register(struct rsi_hw *adapter,
			    u8 function,
			    u32 addr,
			    u8 *data)
{
	struct rsi_91x_sdiodev *dev =
		(struct rsi_91x_sdiodev *)adapter->rsi_dev;
	int status = 0;

	if (likely(dev->sdio_irq_task != current))
		sdio_claim_host(dev->pfunction);

	if (function == 0)
		sdio_f0_writeb(dev->pfunction, *data, addr, &status);
	else
		sdio_writeb(dev->pfunction, *data, addr, &status);

	if (likely(dev->sdio_irq_task != current))
		sdio_release_host(dev->pfunction);

	return status;
}

/**
 * rsi_sdio_ack_intr() - This function acks the interrupt received.
 * @adapter: Pointer to the adapter structure.
 * @int_bit: Interrupt bit to write into register.
 *
 * Return: None.
 */
void rsi_sdio_ack_intr(struct rsi_hw *adapter, u8 int_bit)
{
	int status;
	status = rsi_sdio_write_register(adapter,
					 1,
					 (SDIO_FUN1_INTR_CLR_REG |
					  RSI_SD_REQUEST_MASTER),
					 &int_bit);
	if (status)
		rsi_dbg(ERR_ZONE, "%s: unable to send ack\n", __func__);
}



/**
 * rsi_sdio_read_register_multiple() - This function read multiple bytes of
 *				       information from the SD card.
 * @adapter: Pointer to the adapter structure.
 * @addr: Address of the register.
 * @count: Number of multiple bytes to be read.
 * @data: Pointer to the read data.
 *
 * Return: 0 on success, -1 on failure.
 */
static int rsi_sdio_read_register_multiple(struct rsi_hw *adapter,
					   u32 addr,
					   u8 *data,
					   u16 count)
{
	struct rsi_91x_sdiodev *dev =
		(struct rsi_91x_sdiodev *)adapter->rsi_dev;
	u32 status;

	if (likely(dev->sdio_irq_task != current))
		sdio_claim_host(dev->pfunction);

	status =  sdio_readsb(dev->pfunction, data, addr, count);

	if (likely(dev->sdio_irq_task != current))
		sdio_release_host(dev->pfunction);

	if (status != 0)
		rsi_dbg(ERR_ZONE, "%s: Synch Cmd53 read failed\n", __func__);
	return status;
}

/**
 * rsi_sdio_write_register_multiple() - This function writes multiple bytes of
 *					information to the SD card.
 * @adapter: Pointer to the adapter structure.
 * @addr: Address of the register.
 * @data: Pointer to the data that has to be written.
 * @count: Number of multiple bytes to be written.
 *
 * Return: 0 on success, -1 on failure.
 */
int rsi_sdio_write_register_multiple(struct rsi_hw *adapter,
				     u32 addr,
				     u8 *data,
				     u16 count)
{
	struct rsi_91x_sdiodev *dev =
		(struct rsi_91x_sdiodev *)adapter->rsi_dev;
	int status;

	if (dev->write_fail > 1) {
		rsi_dbg(ERR_ZONE, "%s: Stopping card writes\n", __func__);
		return 0;
	} else if (dev->write_fail == 1) {
		/**
		 * Assuming it is a CRC failure, we want to allow another
		 *  card write
		 */
		rsi_dbg(ERR_ZONE, "%s: Continue card writes\n", __func__);
		dev->write_fail++;
	}

	if (likely(dev->sdio_irq_task != current))
		sdio_claim_host(dev->pfunction);

	status = sdio_writesb(dev->pfunction, addr, data, count);

	if (likely(dev->sdio_irq_task != current))
		sdio_release_host(dev->pfunction);

	if (status) {
		rsi_dbg(ERR_ZONE, "%s: Synch Cmd53 write failed %d\n",
			__func__, status);
		dev->write_fail = 2;
	} else {
		memcpy(dev->prev_desc, data, FRAME_DESC_SZ);
	}
	return status;
}

static int rsi_sdio_load_data_master_write(struct rsi_hw *adapter,
					   u32 base_address,
					   u32 instructions_sz,
					   u16 block_size,
					   u8 *ta_firmware)
{
	u32 num_blocks, offset, i;
	u16 msb_address, lsb_address;
	u8 *temp_buf;
	int status;

	num_blocks = instructions_sz / block_size;
	msb_address = base_address >> 16;

	rsi_dbg(INFO_ZONE, "ins_size: %d, num_blocks: %d\n",
		instructions_sz, num_blocks);

	temp_buf = kmalloc(block_size, GFP_KERNEL);
	if (!temp_buf)
		return -ENOMEM;

	/* Loading DM ms word in the sdio slave */
	status = rsi_sdio_master_access_msword(adapter, msb_address);
	if (status < 0) {
		rsi_dbg(ERR_ZONE, "%s: Unable to set ms word reg\n", __func__);
		goto out_free;
	}

	for (offset = 0, i = 0; i < num_blocks; i++, offset += block_size) {
		memcpy(temp_buf, ta_firmware + offset, block_size);
		lsb_address = (u16)base_address;
		status = rsi_sdio_write_register_multiple
					(adapter,
					 lsb_address | RSI_SD_REQUEST_MASTER,
					 temp_buf, block_size);
		if (status < 0) {
			rsi_dbg(ERR_ZONE, "%s: failed to write\n", __func__);
			goto out_free;
		}
		rsi_dbg(INFO_ZONE, "%s: loading block: %d\n", __func__, i);
		base_address += block_size;

		if ((base_address >> 16) != msb_address) {
			msb_address += 1;

			/* Loading DM ms word in the sdio slave */
			status = rsi_sdio_master_access_msword(adapter,
							       msb_address);
			if (status < 0) {
				rsi_dbg(ERR_ZONE,
					"%s: Unable to set ms word reg\n",
					__func__);
				goto out_free;
			}
		}
	}

	if (instructions_sz % block_size) {
		memset(temp_buf, 0, block_size);
		memcpy(temp_buf, ta_firmware + offset,
		       instructions_sz % block_size);
		lsb_address = (u16)base_address;
		status = rsi_sdio_write_register_multiple
					(adapter,
					 lsb_address | RSI_SD_REQUEST_MASTER,
					 temp_buf,
					 instructions_sz % block_size);
		if (status < 0)
			goto out_free;
		rsi_dbg(INFO_ZONE,
			"Written Last Block in Address 0x%x Successfully\n",
			offset | RSI_SD_REQUEST_MASTER);
	}

	status = 0;
out_free:
	kfree(temp_buf);
	return status;
}

#define FLASH_SIZE_ADDR                 0x04000016
static int rsi_sdio_master_reg_read(struct rsi_hw *adapter, u32 addr,
				    u32 *read_buf, u16 size)
{
	u32 addr_on_bus, *data;
	u16 ms_addr;
	int status;

	data = kzalloc(RSI_MASTER_REG_BUF_SIZE, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data = PTR_ALIGN(data, 8);

	ms_addr = (addr >> 16);
	status = rsi_sdio_master_access_msword(adapter, ms_addr);
	if (status < 0) {
		rsi_dbg(ERR_ZONE,
			"%s: Unable to set ms word to common reg\n",
			__func__);
		goto err;
	}
	addr &= 0xFFFF;

	addr_on_bus = (addr & 0xFF000000);
	if ((addr_on_bus == (FLASH_SIZE_ADDR & 0xFF000000)) ||
	    (addr_on_bus == 0x0))
		addr_on_bus = (addr & ~(0x3));
	else
		addr_on_bus = addr;

	/* Bring TA out of reset */
	status = rsi_sdio_read_register_multiple
					(adapter,
					 (addr_on_bus | RSI_SD_REQUEST_MASTER),
					 (u8 *)data, 4);
	if (status < 0) {
		rsi_dbg(ERR_ZONE, "%s: AHB register read failed\n", __func__);
		goto err;
	}
	if (size == 2) {
		if ((addr & 0x3) == 0)
			*read_buf = *data;
		else
			*read_buf  = (*data >> 16);
		*read_buf = (*read_buf & 0xFFFF);
	} else if (size == 1) {
		if ((addr & 0x3) == 0)
			*read_buf = *data;
		else if ((addr & 0x3) == 1)
			*read_buf = (*data >> 8);
		else if ((addr & 0x3) == 2)
			*read_buf = (*data >> 16);
		else
			*read_buf = (*data >> 24);
		*read_buf = (*read_buf & 0xFF);
	} else {
		*read_buf = *data;
	}

err:
	kfree(data);
	return status;
}

static int rsi_sdio_master_reg_write(struct rsi_hw *adapter,
				     unsigned long addr,
				     unsigned long data, u16 size)
{
	unsigned long *data_aligned;
	int status;

	data_aligned = kzalloc(RSI_MASTER_REG_BUF_SIZE, GFP_KERNEL);
	if (!data_aligned)
		return -ENOMEM;

	data_aligned = PTR_ALIGN(data_aligned, 8);

	if (size == 2) {
		*data_aligned = ((data << 16) | (data & 0xFFFF));
	} else if (size == 1) {
		u32 temp_data = data & 0xFF;

		*data_aligned = ((temp_data << 24) | (temp_data << 16) |
				 (temp_data << 8) | temp_data);
	} else {
		*data_aligned = data;
	}
	size = 4;

	status = rsi_sdio_master_access_msword(adapter, (addr >> 16));
	if (status < 0) {
		rsi_dbg(ERR_ZONE,
			"%s: Unable to set ms word to common reg\n",
			__func__);
		kfree(data_aligned);
		return -EIO;
	}
	addr = addr & 0xFFFF;

	/* Bring TA out of reset */
	status = rsi_sdio_write_register_multiple
					(adapter,
					 (addr | RSI_SD_REQUEST_MASTER),
					 (u8 *)data_aligned, size);
	if (status < 0)
		rsi_dbg(ERR_ZONE,
			"%s: Unable to do AHB reg write\n", __func__);

	kfree(data_aligned);
	return status;
}

/**
 * rsi_sdio_host_intf_write_pkt() - This function writes the packet to device.
 * @adapter: Pointer to the adapter structure.
 * @pkt: Pointer to the data to be written on to the device.
 * @len: length of the data to be written on to the device.
 *
 * Return: 0 on success, -1 on failure.
 */
static int rsi_sdio_host_intf_write_pkt(struct rsi_hw *adapter,
					u8 *pkt,
					u32 len)
{
	struct rsi_91x_sdiodev *dev =
		(struct rsi_91x_sdiodev *)adapter->rsi_dev;
	u32 block_size = dev->tx_blk_size;
	u32 num_blocks, address, length;
	u32 queueno;
	int status;

	queueno = ((pkt[1] >> 4) & 0xf);
	if (queueno == RSI_BT_MGMT_Q || queueno == RSI_BT_DATA_Q)
		queueno = RSI_BT_Q;

	num_blocks = len / block_size;

	if (len % block_size)
		num_blocks++;

	address = (num_blocks * block_size | (queueno << 12));
	length  = num_blocks * block_size;

	status = rsi_sdio_write_register_multiple(adapter,
						  address,
						  (u8 *)pkt,
						  length);
	if (status)
		rsi_dbg(ERR_ZONE, "%s: Unable to write onto the card: %d\n",
			__func__, status);
	rsi_dbg(DATA_TX_ZONE, "%s: Successfully written onto card\n", __func__);
	return status;
}

/**
 * rsi_sdio_host_intf_read_pkt() - This function reads the packet
				   from the device.
 * @adapter: Pointer to the adapter data structure.
 * @pkt: Pointer to the packet data to be read from the the device.
 * @length: Length of the data to be read from the device.
 *
 * Return: 0 on success, -1 on failure.
 */
int rsi_sdio_host_intf_read_pkt(struct rsi_hw *adapter,
				u8 *pkt,
				u32 length)
{
	int status = -EINVAL;

	if (!length) {
		rsi_dbg(ERR_ZONE, "%s: Pkt size is zero\n", __func__);
		return status;
	}

	status = rsi_sdio_read_register_multiple(adapter,
						 length,
						 (u8 *)pkt,
						 length); /*num of bytes*/

	if (status)
		rsi_dbg(ERR_ZONE, "%s: Failed to read frame: %d\n", __func__,
			status);
	return status;
}

/**
 * rsi_init_sdio_interface() - This function does init specific to SDIO.
 *
 * @adapter: Pointer to the adapter data structure.
 * @pkt: Pointer to the packet data to be read from the the device.
 *
 * Return: 0 on success, -1 on failure.
 */

static int rsi_init_sdio_interface(struct rsi_hw *adapter,
				   struct sdio_func *pfunction)
{
	struct rsi_91x_sdiodev *rsi_91x_dev;
	int status = -ENOMEM;

	rsi_91x_dev = kzalloc(sizeof(*rsi_91x_dev), GFP_KERNEL);
	if (!rsi_91x_dev)
		return status;

	adapter->rsi_dev = rsi_91x_dev;

	sdio_claim_host(pfunction);

	pfunction->enable_timeout = 100;
	status = sdio_enable_func(pfunction);
	if (status) {
		rsi_dbg(ERR_ZONE, "%s: Failed to enable interface\n", __func__);
		sdio_release_host(pfunction);
		return status;
	}

	rsi_dbg(INIT_ZONE, "%s: Enabled the interface\n", __func__);

	rsi_91x_dev->pfunction = pfunction;
	adapter->device = &pfunction->dev;

	sdio_set_drvdata(pfunction, adapter);

	status = rsi_setupcard(adapter);
	if (status) {
		rsi_dbg(ERR_ZONE, "%s: Failed to setup card\n", __func__);
		goto fail;
	}

	rsi_dbg(INIT_ZONE, "%s: Setup card succesfully\n", __func__);

	status = rsi_init_sdio_slave_regs(adapter);
	if (status) {
		rsi_dbg(ERR_ZONE, "%s: Failed to init slave regs\n", __func__);
		goto fail;
	}
	sdio_release_host(pfunction);

	adapter->determine_event_timeout = rsi_sdio_determine_event_timeout;
	adapter->check_hw_queue_status = rsi_sdio_check_buffer_status;

#ifdef CONFIG_RSI_DEBUGFS
	adapter->num_debugfs_entries = MAX_DEBUGFS_ENTRIES;
#endif
	return status;
fail:
	sdio_disable_func(pfunction);
	sdio_release_host(pfunction);
	return status;
}

static int rsi_sdio_reinit_device(struct rsi_hw *adapter)
{
	struct rsi_91x_sdiodev *sdev = adapter->rsi_dev;
	struct sdio_func *pfunction = sdev->pfunction;
	int ii;

	for (ii = 0; ii < NUM_SOFT_QUEUES; ii++)
		skb_queue_purge(&adapter->priv->tx_queue[ii]);

	/* Initialize device again */
	sdio_claim_host(pfunction);

	sdio_release_irq(pfunction);
	rsi_reset_card(pfunction);

	sdio_enable_func(pfunction);
	rsi_setupcard(adapter);
	rsi_init_sdio_slave_regs(adapter);
	sdio_claim_irq(pfunction, rsi_handle_interrupt);
	rsi_hal_device_init(adapter);

	sdio_release_host(pfunction);

	return 0;
}

static struct rsi_host_intf_ops sdio_host_intf_ops = {
	.write_pkt		= rsi_sdio_host_intf_write_pkt,
	.read_pkt		= rsi_sdio_host_intf_read_pkt,
	.master_access_msword	= rsi_sdio_master_access_msword,
	.read_reg_multiple	= rsi_sdio_read_register_multiple,
	.write_reg_multiple	= rsi_sdio_write_register_multiple,
	.master_reg_read	= rsi_sdio_master_reg_read,
	.master_reg_write	= rsi_sdio_master_reg_write,
	.load_data_master_write	= rsi_sdio_load_data_master_write,
	.reinit_device          = rsi_sdio_reinit_device,
};

/**
 * rsi_probe() - This function is called by kernel when the driver provided
 *		 Vendor and device IDs are matched. All the initialization
 *		 work is done here.
 * @pfunction: Pointer to the sdio_func structure.
 * @id: Pointer to sdio_device_id structure.
 *
 * Return: 0 on success, 1 on failure.
 */
static int rsi_probe(struct sdio_func *pfunction,
		     const struct sdio_device_id *id)
{
	struct rsi_hw *adapter;
	struct rsi_91x_sdiodev *sdev;
	int status;

	rsi_dbg(INIT_ZONE, "%s: Init function called\n", __func__);

	adapter = rsi_91x_init(dev_oper_mode);
	if (!adapter) {
		rsi_dbg(ERR_ZONE, "%s: Failed to init os intf ops\n",
			__func__);
		return -EINVAL;
	}
	adapter->rsi_host_intf = RSI_HOST_INTF_SDIO;
	adapter->host_intf_ops = &sdio_host_intf_ops;

	if (rsi_init_sdio_interface(adapter, pfunction)) {
		rsi_dbg(ERR_ZONE, "%s: Failed to init sdio interface\n",
			__func__);
		status = -EIO;
		goto fail_free_adapter;
	}
	sdev = (struct rsi_91x_sdiodev *)adapter->rsi_dev;
	rsi_init_event(&sdev->rx_thread.event);
	status = rsi_create_kthread(adapter->priv, &sdev->rx_thread,
				    rsi_sdio_rx_thread, "SDIO-RX-Thread");
	if (status) {
		rsi_dbg(ERR_ZONE, "%s: Unable to init rx thrd\n", __func__);
		goto fail_kill_thread;
	}
	skb_queue_head_init(&sdev->rx_q.head);
	sdev->rx_q.num_rx_pkts = 0;

	sdio_claim_host(pfunction);
	if (sdio_claim_irq(pfunction, rsi_handle_interrupt)) {
		rsi_dbg(ERR_ZONE, "%s: Failed to request IRQ\n", __func__);
		sdio_release_host(pfunction);
		status = -EIO;
		goto fail_claim_irq;
	}
	sdio_release_host(pfunction);
	rsi_dbg(INIT_ZONE, "%s: Registered Interrupt handler\n", __func__);

	if (rsi_hal_device_init(adapter)) {
		rsi_dbg(ERR_ZONE, "%s: Failed in device init\n", __func__);
		status = -EINVAL;
		goto fail_dev_init;
	}
	rsi_dbg(INFO_ZONE, "===> RSI Device Init Done <===\n");

	if (rsi_sdio_master_access_msword(adapter, MISC_CFG_BASE_ADDR)) {
		rsi_dbg(ERR_ZONE, "%s: Unable to set ms word reg\n", __func__);
		status = -EIO;
		goto fail_dev_init;
	}

	adapter->priv->hibernate_resume = false;
	adapter->priv->reinit_hw = false;
	return 0;

fail_dev_init:
	sdio_claim_host(pfunction);
	sdio_release_irq(pfunction);
	sdio_release_host(pfunction);
fail_claim_irq:
	rsi_kill_thread(&sdev->rx_thread);
fail_kill_thread:
	sdio_claim_host(pfunction);
	sdio_disable_func(pfunction);
	sdio_release_host(pfunction);
fail_free_adapter:
	rsi_91x_deinit(adapter);
	rsi_dbg(ERR_ZONE, "%s: Failed in probe...Exiting\n", __func__);
	return status;
}

static void ulp_read_write(struct rsi_hw *adapter, u16 addr, u32 data,
			   u16 len_in_bits)
{
	rsi_sdio_master_reg_write(adapter, RSI_GSPI_DATA_REG1,
				  ((addr << 6) | ((data >> 16) & 0xffff)), 2);
	rsi_sdio_master_reg_write(adapter, RSI_GSPI_DATA_REG0,
				  (data & 0xffff), 2);
	rsi_sdio_master_reg_write(adapter, RSI_GSPI_CTRL_REG0,
				  RSI_GSPI_CTRL_REG0_VALUE, 2);
	rsi_sdio_master_reg_write(adapter, RSI_GSPI_CTRL_REG1,
				  ((len_in_bits - 1) | RSI_GSPI_TRIG), 2);
	msleep(20);
}

/*This function resets and re-initializes the chip.*/
static void rsi_reset_chip(struct rsi_hw *adapter)
{
	__le32 data;
	u8 sdio_interrupt_status = 0;
	u8 request = 1;
	int ret;

	rsi_dbg(INFO_ZONE, "Writing disable to wakeup register\n");
	ret =  rsi_sdio_write_register(adapter, 0, SDIO_WAKEUP_REG, &request);
	if (ret < 0) {
		rsi_dbg(ERR_ZONE,
			"%s: Failed to write SDIO wakeup register\n", __func__);
		return;
	}
	msleep(20);
	ret =  rsi_sdio_read_register(adapter, RSI_FN1_INT_REGISTER,
				      &sdio_interrupt_status);
	if (ret < 0) {
		rsi_dbg(ERR_ZONE, "%s: Failed to Read Intr Status Register\n",
			__func__);
		return;
	}
	rsi_dbg(INFO_ZONE, "%s: Intr Status Register value = %d\n",
		__func__, sdio_interrupt_status);

	/* Put Thread-Arch processor on hold */
	if (rsi_sdio_master_access_msword(adapter, TA_BASE_ADDR)) {
		rsi_dbg(ERR_ZONE,
			"%s: Unable to set ms word to common reg\n",
			__func__);
		return;
	}

	data = TA_HOLD_THREAD_VALUE;
	if (rsi_sdio_write_register_multiple(adapter, TA_HOLD_THREAD_REG |
					     RSI_SD_REQUEST_MASTER,
					     (u8 *)&data, 4)) {
		rsi_dbg(ERR_ZONE,
			"%s: Unable to hold Thread-Arch processor threads\n",
			__func__);
		return;
	}

	/* This msleep will ensure Thread-Arch processor to go to hold
	 * and any pending dma transfers to rf spi in device to finish.
	 */
	msleep(100);

	ulp_read_write(adapter, RSI_ULP_RESET_REG, RSI_ULP_WRITE_0, 32);
	ulp_read_write(adapter, RSI_WATCH_DOG_TIMER_1, RSI_ULP_WRITE_2, 32);
	ulp_read_write(adapter, RSI_WATCH_DOG_TIMER_2, RSI_ULP_WRITE_0, 32);
	ulp_read_write(adapter, RSI_WATCH_DOG_DELAY_TIMER_1, RSI_ULP_WRITE_50,
		       32);
	ulp_read_write(adapter, RSI_WATCH_DOG_DELAY_TIMER_2, RSI_ULP_WRITE_0,
		       32);
	ulp_read_write(adapter, RSI_WATCH_DOG_TIMER_ENABLE,
		       RSI_ULP_TIMER_ENABLE, 32);
	/* This msleep will be sufficient for the ulp
	 * read write operations to complete for chip reset.
	 */
	msleep(500);
}

/**
 * rsi_disconnect() - This function performs the reverse of the probe function.
 * @pfunction: Pointer to the sdio_func structure.
 *
 * Return: void.
 */
static void rsi_disconnect(struct sdio_func *pfunction)
{
	struct rsi_hw *adapter = sdio_get_drvdata(pfunction);
	struct rsi_91x_sdiodev *dev;

	if (!adapter)
		return;

	dev = (struct rsi_91x_sdiodev *)adapter->rsi_dev;

	rsi_kill_thread(&dev->rx_thread);
	sdio_claim_host(pfunction);
	sdio_release_irq(pfunction);
	sdio_release_host(pfunction);
	mdelay(10);

	rsi_mac80211_detach(adapter);
	mdelay(10);

	/* Reset Chip */
	rsi_reset_chip(adapter);

	/* Resetting to take care of the case, where-in driver is re-loaded */
	sdio_claim_host(pfunction);
	rsi_reset_card(pfunction);
	sdio_disable_func(pfunction);
	sdio_release_host(pfunction);
	dev->write_fail = 2;
	rsi_91x_deinit(adapter);
	rsi_dbg(ERR_ZONE, "##### RSI SDIO device disconnected #####\n");

}

#ifdef CONFIG_PM
static int rsi_set_sdio_pm_caps(struct rsi_hw *adapter)
{
	struct rsi_91x_sdiodev *dev =
		(struct rsi_91x_sdiodev *)adapter->rsi_dev;
	struct sdio_func *func = dev->pfunction;
	int ret;

	ret = sdio_set_host_pm_flags(func, MMC_PM_KEEP_POWER);
	if (ret)
		rsi_dbg(ERR_ZONE, "Set sdio keep pwr flag failed: %d\n", ret);

	return ret;
}

static int rsi_sdio_disable_interrupts(struct sdio_func *pfunc)
{
	struct rsi_hw *adapter = sdio_get_drvdata(pfunc);
	u8 isr_status = 0, data = 0;
	int ret;
	unsigned long t1;

	rsi_dbg(INFO_ZONE, "Waiting for interrupts to be cleared..");
	t1 = jiffies;
	do {
		rsi_sdio_read_register(adapter, RSI_FN1_INT_REGISTER,
				       &isr_status);
		rsi_dbg(INFO_ZONE, ".");
	} while ((isr_status) && (jiffies_to_msecs(jiffies - t1) < 20));
	rsi_dbg(INFO_ZONE, "Interrupts cleared\n");

	sdio_claim_host(pfunc);
	ret = rsi_cmd52readbyte(pfunc->card, RSI_INT_ENABLE_REGISTER, &data);
	if (ret < 0) {
		rsi_dbg(ERR_ZONE,
			"%s: Failed to read int enable register\n",
			__func__);
		goto done;
	}

	data &= RSI_INT_ENABLE_MASK;
	ret = rsi_cmd52writebyte(pfunc->card, RSI_INT_ENABLE_REGISTER, data);
	if (ret < 0) {
		rsi_dbg(ERR_ZONE,
			"%s: Failed to write to int enable register\n",
			__func__);
		goto done;
	}
	ret = rsi_cmd52readbyte(pfunc->card, RSI_INT_ENABLE_REGISTER, &data);
	if (ret < 0) {
		rsi_dbg(ERR_ZONE,
			"%s: Failed to read int enable register\n",
			__func__);
		goto done;
	}
	rsi_dbg(INFO_ZONE, "int enable reg content = %x\n", data);

done:
	sdio_release_host(pfunc);
	return ret;
}

static int rsi_sdio_enable_interrupts(struct sdio_func *pfunc)
{
	u8 data;
	int ret;
	struct rsi_hw *adapter = sdio_get_drvdata(pfunc);
	struct rsi_common *common = adapter->priv;

	sdio_claim_host(pfunc);
	ret = rsi_cmd52readbyte(pfunc->card, RSI_INT_ENABLE_REGISTER, &data);
	if (ret < 0) {
		rsi_dbg(ERR_ZONE,
			"%s: Failed to read int enable register\n", __func__);
		goto done;
	}

	data |= ~RSI_INT_ENABLE_MASK & 0xff;

	ret = rsi_cmd52writebyte(pfunc->card, RSI_INT_ENABLE_REGISTER, data);
	if (ret < 0) {
		rsi_dbg(ERR_ZONE,
			"%s: Failed to write to int enable register\n",
			__func__);
		goto done;
	}

	if ((common->wow_flags & RSI_WOW_ENABLED) &&
	    (common->wow_flags & RSI_WOW_NO_CONNECTION))
		rsi_dbg(ERR_ZONE,
			"##### Device can not wake up through WLAN\n");

	ret = rsi_cmd52readbyte(pfunc->card, RSI_INT_ENABLE_REGISTER, &data);
	if (ret < 0) {
		rsi_dbg(ERR_ZONE,
			"%s: Failed to read int enable register\n", __func__);
		goto done;
	}
	rsi_dbg(INFO_ZONE, "int enable reg content = %x\n", data);

done:
	sdio_release_host(pfunc);
	return ret;
}

static int rsi_suspend(struct device *dev)
{
	int ret;
	struct sdio_func *pfunction = dev_to_sdio_func(dev);
	struct rsi_hw *adapter = sdio_get_drvdata(pfunction);
	struct rsi_common *common;

	if (!adapter) {
		rsi_dbg(ERR_ZONE, "Device is not ready\n");
		return -ENODEV;
	}
	common = adapter->priv;
	rsi_sdio_disable_interrupts(pfunction);

	ret = rsi_set_sdio_pm_caps(adapter);
	if (ret)
		rsi_dbg(INFO_ZONE,
			"Setting power management caps failed\n");
	common->fsm_state = FSM_CARD_NOT_READY;

	return 0;
}

static int rsi_resume(struct device *dev)
{
	struct sdio_func *pfunction = dev_to_sdio_func(dev);
	struct rsi_hw *adapter = sdio_get_drvdata(pfunction);
	struct rsi_common *common = adapter->priv;

	common->fsm_state = FSM_MAC_INIT_DONE;
	rsi_sdio_enable_interrupts(pfunction);

	return 0;
}

static int rsi_freeze(struct device *dev)
{
	int ret;
	struct sdio_func *pfunction = dev_to_sdio_func(dev);
	struct rsi_hw *adapter = sdio_get_drvdata(pfunction);
	struct rsi_common *common;
	struct rsi_91x_sdiodev *sdev;

	rsi_dbg(INFO_ZONE, "SDIO Bus freeze ===>\n");

	if (!adapter) {
		rsi_dbg(ERR_ZONE, "Device is not ready\n");
		return -ENODEV;
	}
	common = adapter->priv;
	sdev = (struct rsi_91x_sdiodev *)adapter->rsi_dev;

	if ((common->wow_flags & RSI_WOW_ENABLED) &&
	    (common->wow_flags & RSI_WOW_NO_CONNECTION))
		rsi_dbg(ERR_ZONE,
			"##### Device can not wake up through WLAN\n");

	ret = rsi_sdio_disable_interrupts(pfunction);

	if (sdev->write_fail)
		rsi_dbg(INFO_ZONE, "###### Device is not ready #######\n");

	ret = rsi_set_sdio_pm_caps(adapter);
	if (ret)
		rsi_dbg(INFO_ZONE, "Setting power management caps failed\n");

	rsi_dbg(INFO_ZONE, "***** RSI module freezed *****\n");

	return 0;
}

static int rsi_thaw(struct device *dev)
{
	struct sdio_func *pfunction = dev_to_sdio_func(dev);
	struct rsi_hw *adapter = sdio_get_drvdata(pfunction);
	struct rsi_common *common = adapter->priv;

	rsi_dbg(ERR_ZONE, "SDIO Bus thaw =====>\n");

	common->hibernate_resume = true;
	common->fsm_state = FSM_CARD_NOT_READY;
	common->iface_down = true;

	rsi_sdio_enable_interrupts(pfunction);

	rsi_dbg(INFO_ZONE, "***** RSI module thaw done *****\n");

	return 0;
}

static void rsi_shutdown(struct device *dev)
{
	struct sdio_func *pfunction = dev_to_sdio_func(dev);
	struct rsi_hw *adapter = sdio_get_drvdata(pfunction);
	struct rsi_91x_sdiodev *sdev =
		(struct rsi_91x_sdiodev *)adapter->rsi_dev;
	struct ieee80211_hw *hw = adapter->hw;
	struct cfg80211_wowlan *wowlan = hw->wiphy->wowlan_config;

	rsi_dbg(ERR_ZONE, "SDIO Bus shutdown =====>\n");

	if (rsi_config_wowlan(adapter, wowlan))
		rsi_dbg(ERR_ZONE, "Failed to configure WoWLAN\n");

	rsi_sdio_disable_interrupts(sdev->pfunction);

	if (sdev->write_fail)
		rsi_dbg(INFO_ZONE, "###### Device is not ready #######\n");

	if (rsi_set_sdio_pm_caps(adapter))
		rsi_dbg(INFO_ZONE, "Setting power management caps failed\n");

	rsi_dbg(INFO_ZONE, "***** RSI module shut down *****\n");
}

static int rsi_restore(struct device *dev)
{
	struct sdio_func *pfunction = dev_to_sdio_func(dev);
	struct rsi_hw *adapter = sdio_get_drvdata(pfunction);
	struct rsi_common *common = adapter->priv;

	rsi_dbg(INFO_ZONE, "SDIO Bus restore ======>\n");
	common->hibernate_resume = true;
	common->fsm_state = FSM_FW_NOT_LOADED;
	common->iface_down = true;

	adapter->sc_nvifs = 0;
	ieee80211_restart_hw(adapter->hw);

	common->wow_flags = 0;
	common->iface_down = false;

	rsi_dbg(INFO_ZONE, "RSI module restored\n");

	return 0;
}
static const struct dev_pm_ops rsi_pm_ops = {
	.suspend = rsi_suspend,
	.resume = rsi_resume,
	.freeze = rsi_freeze,
	.thaw = rsi_thaw,
	.restore = rsi_restore,
};
#endif

static const struct sdio_device_id rsi_dev_table[] =  {
	{ SDIO_DEVICE(0x303, 0x100) },
	{ SDIO_DEVICE(0x041B, 0x0301) },
	{ SDIO_DEVICE(0x041B, 0x0201) },
	{ SDIO_DEVICE(0x041B, 0x9330) },
	{ /* Blank */},
};

static struct sdio_driver rsi_driver = {
	.name       = "RSI-SDIO WLAN",
	.probe      = rsi_probe,
	.remove     = rsi_disconnect,
	.id_table   = rsi_dev_table,
#ifdef CONFIG_PM
	.drv = {
		.pm = &rsi_pm_ops,
		.shutdown   = rsi_shutdown,
	}
#endif
};

/**
 * rsi_module_init() - This function registers the sdio module.
 * @void: Void.
 *
 * Return: 0 on success.
 */
static int rsi_module_init(void)
{
	int ret;

	ret = sdio_register_driver(&rsi_driver);
	rsi_dbg(INIT_ZONE, "%s: Registering driver\n", __func__);
	return ret;
}

/**
 * rsi_module_exit() - This function unregisters the sdio module.
 * @void: Void.
 *
 * Return: None.
 */
static void rsi_module_exit(void)
{
	sdio_unregister_driver(&rsi_driver);
	rsi_dbg(INFO_ZONE, "%s: Unregistering driver\n", __func__);
}

module_init(rsi_module_init);
module_exit(rsi_module_exit);

MODULE_AUTHOR("Redpine Signals Inc");
MODULE_DESCRIPTION("Common SDIO layer for RSI drivers");
MODULE_SUPPORTED_DEVICE("RSI-91x");
MODULE_DEVICE_TABLE(sdio, rsi_dev_table);
MODULE_FIRMWARE(FIRMWARE_RSI9113);
MODULE_VERSION("0.1");
MODULE_LICENSE("Dual BSD/GPL");
