/*
 *	linux/drivers/net/wireless/libertas/if_spi.c
 *
 *	Driver for Marvell SPI WLAN cards.
 *
 *	Copyright 2008 Analog Devices Inc.
 *
 *	Authors:
 *	Andrey Yurovsky <andrey@cozybit.com>
 *	Colin McCabe <colin@cozybit.com>
 *
 *	Inspired by if_sdio.c, Copyright 2007-2008 Pierre Ossman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/moduleparam.h>
#include <linux/firmware.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/semaphore.h>
#include <linux/slab.h>
#include <linux/spi/libertas_spi.h>
#include <linux/spi/spi.h>

#include "host.h"
#include "decl.h"
#include "defs.h"
#include "dev.h"
#include "if_spi.h"

struct if_spi_card {
	struct spi_device		*spi;
	struct lbs_private		*priv;
	struct libertas_spi_platform_data *pdata;

	/* The card ID and card revision, as reported by the hardware. */
	u16				card_id;
	u8				card_rev;

	/* The last time that we initiated an SPU operation */
	unsigned long			prev_xfer_time;

	int				use_dummy_writes;
	unsigned long			spu_port_delay;
	unsigned long			spu_reg_delay;

	/* Handles all SPI communication (except for FW load) */
	struct task_struct		*spi_thread;
	int				run_thread;

	/* Used to wake up the spi_thread */
	struct semaphore		spi_ready;
	struct semaphore		spi_thread_terminated;

	u8				cmd_buffer[IF_SPI_CMD_BUF_SIZE];
};

static void free_if_spi_card(struct if_spi_card *card)
{
	spi_set_drvdata(card->spi, NULL);
	kfree(card);
}

#define MODEL_8385	0x04
#define MODEL_8686	0x0b
#define MODEL_8688	0x10

static const struct lbs_fw_table fw_table[] = {
	{ MODEL_8385, "libertas/gspi8385_helper.bin", "libertas/gspi8385.bin" },
	{ MODEL_8385, "libertas/gspi8385_hlp.bin", "libertas/gspi8385.bin" },
	{ MODEL_8686, "libertas/gspi8686_v9_helper.bin", "libertas/gspi8686_v9.bin" },
	{ MODEL_8686, "libertas/gspi8686_hlp.bin", "libertas/gspi8686.bin" },
	{ MODEL_8688, "libertas/gspi8688_helper.bin", "libertas/gspi8688.bin" },
	{ 0, NULL, NULL }
};
MODULE_FIRMWARE("libertas/gspi8385_helper.bin");
MODULE_FIRMWARE("libertas/gspi8385_hlp.bin");
MODULE_FIRMWARE("libertas/gspi8385.bin");
MODULE_FIRMWARE("libertas/gspi8686_v9_helper.bin");
MODULE_FIRMWARE("libertas/gspi8686_v9.bin");
MODULE_FIRMWARE("libertas/gspi8686_hlp.bin");
MODULE_FIRMWARE("libertas/gspi8686.bin");
MODULE_FIRMWARE("libertas/gspi8688_helper.bin");
MODULE_FIRMWARE("libertas/gspi8688.bin");


/*
 * SPI Interface Unit Routines
 *
 * The SPU sits between the host and the WLAN module.
 * All communication with the firmware is through SPU transactions.
 *
 * First we have to put a SPU register name on the bus. Then we can
 * either read from or write to that register.
 *
 */

static void spu_transaction_init(struct if_spi_card *card)
{
	if (!time_after(jiffies, card->prev_xfer_time + 1)) {
		/* Unfortunately, the SPU requires a delay between successive
		 * transactions. If our last transaction was more than a jiffy
		 * ago, we have obviously already delayed enough.
		 * If not, we have to busy-wait to be on the safe side. */
		ndelay(400);
	}
}

static void spu_transaction_finish(struct if_spi_card *card)
{
	card->prev_xfer_time = jiffies;
}

/* Write out a byte buffer to an SPI register,
 * using a series of 16-bit transfers. */
static int spu_write(struct if_spi_card *card, u16 reg, const u8 *buf, int len)
{
	int err = 0;
	__le16 reg_out = cpu_to_le16(reg | IF_SPI_WRITE_OPERATION_MASK);
	struct spi_message m;
	struct spi_transfer reg_trans;
	struct spi_transfer data_trans;

	spi_message_init(&m);
	memset(&reg_trans, 0, sizeof(reg_trans));
	memset(&data_trans, 0, sizeof(data_trans));

	/* You must give an even number of bytes to the SPU, even if it
	 * doesn't care about the last one.  */
	BUG_ON(len & 0x1);

	spu_transaction_init(card);

	/* write SPU register index */
	reg_trans.tx_buf = &reg_out;
	reg_trans.len = sizeof(reg_out);

	data_trans.tx_buf = buf;
	data_trans.len = len;

	spi_message_add_tail(&reg_trans, &m);
	spi_message_add_tail(&data_trans, &m);

	err = spi_sync(card->spi, &m);
	spu_transaction_finish(card);
	return err;
}

static inline int spu_write_u16(struct if_spi_card *card, u16 reg, u16 val)
{
	__le16 buff;

	buff = cpu_to_le16(val);
	return spu_write(card, reg, (u8 *)&buff, sizeof(u16));
}

static inline int spu_reg_is_port_reg(u16 reg)
{
	switch (reg) {
	case IF_SPI_IO_RDWRPORT_REG:
	case IF_SPI_CMD_RDWRPORT_REG:
	case IF_SPI_DATA_RDWRPORT_REG:
		return 1;
	default:
		return 0;
	}
}

static int spu_read(struct if_spi_card *card, u16 reg, u8 *buf, int len)
{
	unsigned int delay;
	int err = 0;
	__le16 reg_out = cpu_to_le16(reg | IF_SPI_READ_OPERATION_MASK);
	struct spi_message m;
	struct spi_transfer reg_trans;
	struct spi_transfer dummy_trans;
	struct spi_transfer data_trans;

	/* You must take an even number of bytes from the SPU, even if you
	 * don't care about the last one.  */
	BUG_ON(len & 0x1);

	spu_transaction_init(card);

	spi_message_init(&m);
	memset(&reg_trans, 0, sizeof(reg_trans));
	memset(&dummy_trans, 0, sizeof(dummy_trans));
	memset(&data_trans, 0, sizeof(data_trans));

	/* write SPU register index */
	reg_trans.tx_buf = &reg_out;
	reg_trans.len = sizeof(reg_out);
	spi_message_add_tail(&reg_trans, &m);

	delay = spu_reg_is_port_reg(reg) ? card->spu_port_delay :
						card->spu_reg_delay;
	if (card->use_dummy_writes) {
		/* Clock in dummy cycles while the SPU fills the FIFO */
		dummy_trans.len = delay / 8;
		spi_message_add_tail(&dummy_trans, &m);
	} else {
		/* Busy-wait while the SPU fills the FIFO */
		reg_trans.delay_usecs =
			DIV_ROUND_UP((100 + (delay * 10)), 1000);
	}

	/* read in data */
	data_trans.rx_buf = buf;
	data_trans.len = len;
	spi_message_add_tail(&data_trans, &m);

	err = spi_sync(card->spi, &m);
	spu_transaction_finish(card);
	return err;
}

/* Read 16 bits from an SPI register */
static inline int spu_read_u16(struct if_spi_card *card, u16 reg, u16 *val)
{
	__le16 buf;
	int ret;

	ret = spu_read(card, reg, (u8 *)&buf, sizeof(buf));
	if (ret == 0)
		*val = le16_to_cpup(&buf);
	return ret;
}

/* Read 32 bits from an SPI register.
 * The low 16 bits are read first. */
static int spu_read_u32(struct if_spi_card *card, u16 reg, u32 *val)
{
	__le32 buf;
	int err;

	err = spu_read(card, reg, (u8 *)&buf, sizeof(buf));
	if (!err)
		*val = le32_to_cpup(&buf);
	return err;
}

/* Keep reading 16 bits from an SPI register until you get the correct result.
 *
 * If mask = 0, the correct result is any non-zero number.
 * If mask != 0, the correct result is any number where
 * number & target_mask == target
 *
 * Returns -ETIMEDOUT if a second passes without the correct result. */
static int spu_wait_for_u16(struct if_spi_card *card, u16 reg,
			u16 target_mask, u16 target)
{
	int err;
	unsigned long timeout = jiffies + 5*HZ;
	while (1) {
		u16 val;
		err = spu_read_u16(card, reg, &val);
		if (err)
			return err;
		if (target_mask) {
			if ((val & target_mask) == target)
				return 0;
		} else {
			if (val)
				return 0;
		}
		udelay(100);
		if (time_after(jiffies, timeout)) {
			lbs_pr_err("%s: timeout with val=%02x, "
			       "target_mask=%02x, target=%02x\n",
			       __func__, val, target_mask, target);
			return -ETIMEDOUT;
		}
	}
}

/* Read 16 bits from an SPI register until you receive a specific value.
 * Returns -ETIMEDOUT if a 4 tries pass without success. */
static int spu_wait_for_u32(struct if_spi_card *card, u32 reg, u32 target)
{
	int err, try;
	for (try = 0; try < 4; ++try) {
		u32 val = 0;
		err = spu_read_u32(card, reg, &val);
		if (err)
			return err;
		if (val == target)
			return 0;
		mdelay(100);
	}
	return -ETIMEDOUT;
}

static int spu_set_interrupt_mode(struct if_spi_card *card,
			   int suppress_host_int,
			   int auto_int)
{
	int err = 0;

	/* We can suppress a host interrupt by clearing the appropriate
	 * bit in the "host interrupt status mask" register */
	if (suppress_host_int) {
		err = spu_write_u16(card, IF_SPI_HOST_INT_STATUS_MASK_REG, 0);
		if (err)
			return err;
	} else {
		err = spu_write_u16(card, IF_SPI_HOST_INT_STATUS_MASK_REG,
			      IF_SPI_HISM_TX_DOWNLOAD_RDY |
			      IF_SPI_HISM_RX_UPLOAD_RDY |
			      IF_SPI_HISM_CMD_DOWNLOAD_RDY |
			      IF_SPI_HISM_CARDEVENT |
			      IF_SPI_HISM_CMD_UPLOAD_RDY);
		if (err)
			return err;
	}

	/* If auto-interrupts are on, the completion of certain transactions
	 * will trigger an interrupt automatically. If auto-interrupts
	 * are off, we need to set the "Card Interrupt Cause" register to
	 * trigger a card interrupt. */
	if (auto_int) {
		err = spu_write_u16(card, IF_SPI_HOST_INT_CTRL_REG,
				IF_SPI_HICT_TX_DOWNLOAD_OVER_AUTO |
				IF_SPI_HICT_RX_UPLOAD_OVER_AUTO |
				IF_SPI_HICT_CMD_DOWNLOAD_OVER_AUTO |
				IF_SPI_HICT_CMD_UPLOAD_OVER_AUTO);
		if (err)
			return err;
	} else {
		err = spu_write_u16(card, IF_SPI_HOST_INT_STATUS_MASK_REG, 0);
		if (err)
			return err;
	}
	return err;
}

static int spu_get_chip_revision(struct if_spi_card *card,
				  u16 *card_id, u8 *card_rev)
{
	int err = 0;
	u32 dev_ctrl;
	err = spu_read_u32(card, IF_SPI_DEVICEID_CTRL_REG, &dev_ctrl);
	if (err)
		return err;
	*card_id = IF_SPI_DEVICEID_CTRL_REG_TO_CARD_ID(dev_ctrl);
	*card_rev = IF_SPI_DEVICEID_CTRL_REG_TO_CARD_REV(dev_ctrl);
	return err;
}

static int spu_set_bus_mode(struct if_spi_card *card, u16 mode)
{
	int err = 0;
	u16 rval;
	/* set bus mode */
	err = spu_write_u16(card, IF_SPI_SPU_BUS_MODE_REG, mode);
	if (err)
		return err;
	/* Check that we were able to read back what we just wrote. */
	err = spu_read_u16(card, IF_SPI_SPU_BUS_MODE_REG, &rval);
	if (err)
		return err;
	if ((rval & 0xF) != mode) {
		lbs_pr_err("Can't read bus mode register.\n");
		return -EIO;
	}
	return 0;
}

static int spu_init(struct if_spi_card *card, int use_dummy_writes)
{
	int err = 0;
	u32 delay;

	/* We have to start up in timed delay mode so that we can safely
	 * read the Delay Read Register. */
	card->use_dummy_writes = 0;
	err = spu_set_bus_mode(card,
				IF_SPI_BUS_MODE_SPI_CLOCK_PHASE_RISING |
				IF_SPI_BUS_MODE_DELAY_METHOD_TIMED |
				IF_SPI_BUS_MODE_16_BIT_ADDRESS_16_BIT_DATA);
	if (err)
		return err;
	card->spu_port_delay = 1000;
	card->spu_reg_delay = 1000;
	err = spu_read_u32(card, IF_SPI_DELAY_READ_REG, &delay);
	if (err)
		return err;
	card->spu_port_delay = delay & 0x0000ffff;
	card->spu_reg_delay = (delay & 0xffff0000) >> 16;

	/* If dummy clock delay mode has been requested, switch to it now */
	if (use_dummy_writes) {
		card->use_dummy_writes = 1;
		err = spu_set_bus_mode(card,
				IF_SPI_BUS_MODE_SPI_CLOCK_PHASE_RISING |
				IF_SPI_BUS_MODE_DELAY_METHOD_DUMMY_CLOCK |
				IF_SPI_BUS_MODE_16_BIT_ADDRESS_16_BIT_DATA);
		if (err)
			return err;
	}

	lbs_deb_spi("Initialized SPU unit. "
		    "spu_port_delay=0x%04lx, spu_reg_delay=0x%04lx\n",
		    card->spu_port_delay, card->spu_reg_delay);
	return err;
}

/*
 * Firmware Loading
 */

static int if_spi_prog_helper_firmware(struct if_spi_card *card,
					const struct firmware *firmware)
{
	int err = 0;
	int bytes_remaining;
	const u8 *fw;
	u8 temp[HELPER_FW_LOAD_CHUNK_SZ];

	lbs_deb_enter(LBS_DEB_SPI);

	err = spu_set_interrupt_mode(card, 1, 0);
	if (err)
		goto out;

	bytes_remaining = firmware->size;
	fw = firmware->data;

	/* Load helper firmware image */
	while (bytes_remaining > 0) {
		/* Scratch pad 1 should contain the number of bytes we
		 * want to download to the firmware */
		err = spu_write_u16(card, IF_SPI_SCRATCH_1_REG,
					HELPER_FW_LOAD_CHUNK_SZ);
		if (err)
			goto out;

		err = spu_wait_for_u16(card, IF_SPI_HOST_INT_STATUS_REG,
					IF_SPI_HIST_CMD_DOWNLOAD_RDY,
					IF_SPI_HIST_CMD_DOWNLOAD_RDY);
		if (err)
			goto out;

		/* Feed the data into the command read/write port reg
		 * in chunks of 64 bytes */
		memset(temp, 0, sizeof(temp));
		memcpy(temp, fw,
		       min(bytes_remaining, HELPER_FW_LOAD_CHUNK_SZ));
		mdelay(10);
		err = spu_write(card, IF_SPI_CMD_RDWRPORT_REG,
					temp, HELPER_FW_LOAD_CHUNK_SZ);
		if (err)
			goto out;

		/* Interrupt the boot code */
		err = spu_write_u16(card, IF_SPI_HOST_INT_STATUS_REG, 0);
		if (err)
			goto out;
		err = spu_write_u16(card, IF_SPI_CARD_INT_CAUSE_REG,
				       IF_SPI_CIC_CMD_DOWNLOAD_OVER);
		if (err)
			goto out;
		bytes_remaining -= HELPER_FW_LOAD_CHUNK_SZ;
		fw += HELPER_FW_LOAD_CHUNK_SZ;
	}

	/* Once the helper / single stage firmware download is complete,
	 * write 0 to scratch pad 1 and interrupt the
	 * bootloader. This completes the helper download. */
	err = spu_write_u16(card, IF_SPI_SCRATCH_1_REG, FIRMWARE_DNLD_OK);
	if (err)
		goto out;
	err = spu_write_u16(card, IF_SPI_HOST_INT_STATUS_REG, 0);
	if (err)
		goto out;
	err = spu_write_u16(card, IF_SPI_CARD_INT_CAUSE_REG,
				IF_SPI_CIC_CMD_DOWNLOAD_OVER);
		goto out;

	lbs_deb_spi("waiting for helper to boot...\n");

out:
	if (err)
		lbs_pr_err("failed to load helper firmware (err=%d)\n", err);
	lbs_deb_leave_args(LBS_DEB_SPI, "err %d", err);
	return err;
}

/* Returns the length of the next packet the firmware expects us to send
 * Sets crc_err if the previous transfer had a CRC error. */
static int if_spi_prog_main_firmware_check_len(struct if_spi_card *card,
						int *crc_err)
{
	u16 len;
	int err = 0;

	/* wait until the host interrupt status register indicates
	 * that we are ready to download */
	err = spu_wait_for_u16(card, IF_SPI_HOST_INT_STATUS_REG,
				IF_SPI_HIST_CMD_DOWNLOAD_RDY,
				IF_SPI_HIST_CMD_DOWNLOAD_RDY);
	if (err) {
		lbs_pr_err("timed out waiting for host_int_status\n");
		return err;
	}

	/* Ask the device how many bytes of firmware it wants. */
	err = spu_read_u16(card, IF_SPI_SCRATCH_1_REG, &len);
	if (err)
		return err;

	if (len > IF_SPI_CMD_BUF_SIZE) {
		lbs_pr_err("firmware load device requested a larger "
			   "tranfer than we are prepared to "
			   "handle. (len = %d)\n", len);
		return -EIO;
	}
	if (len & 0x1) {
		lbs_deb_spi("%s: crc error\n", __func__);
		len &= ~0x1;
		*crc_err = 1;
	} else
		*crc_err = 0;

	return len;
}

static int if_spi_prog_main_firmware(struct if_spi_card *card,
					const struct firmware *firmware)
{
	int len, prev_len;
	int bytes, crc_err = 0, err = 0;
	const u8 *fw;
	u16 num_crc_errs;

	lbs_deb_enter(LBS_DEB_SPI);

	err = spu_set_interrupt_mode(card, 1, 0);
	if (err)
		goto out;

	err = spu_wait_for_u16(card, IF_SPI_SCRATCH_1_REG, 0, 0);
	if (err) {
		lbs_pr_err("%s: timed out waiting for initial "
			   "scratch reg = 0\n", __func__);
		goto out;
	}

	num_crc_errs = 0;
	prev_len = 0;
	bytes = firmware->size;
	fw = firmware->data;
	while ((len = if_spi_prog_main_firmware_check_len(card, &crc_err))) {
		if (len < 0) {
			err = len;
			goto out;
		}
		if (bytes < 0) {
			/* If there are no more bytes left, we would normally
			 * expect to have terminated with len = 0 */
			lbs_pr_err("Firmware load wants more bytes "
				   "than we have to offer.\n");
			break;
		}
		if (crc_err) {
			/* Previous transfer failed. */
			if (++num_crc_errs > MAX_MAIN_FW_LOAD_CRC_ERR) {
				lbs_pr_err("Too many CRC errors encountered "
					   "in firmware load.\n");
				err = -EIO;
				goto out;
			}
		} else {
			/* Previous transfer succeeded. Advance counters. */
			bytes -= prev_len;
			fw += prev_len;
		}
		if (bytes < len) {
			memset(card->cmd_buffer, 0, len);
			memcpy(card->cmd_buffer, fw, bytes);
		} else
			memcpy(card->cmd_buffer, fw, len);

		err = spu_write_u16(card, IF_SPI_HOST_INT_STATUS_REG, 0);
		if (err)
			goto out;
		err = spu_write(card, IF_SPI_CMD_RDWRPORT_REG,
				card->cmd_buffer, len);
		if (err)
			goto out;
		err = spu_write_u16(card, IF_SPI_CARD_INT_CAUSE_REG ,
					IF_SPI_CIC_CMD_DOWNLOAD_OVER);
		if (err)
			goto out;
		prev_len = len;
	}
	if (bytes > prev_len) {
		lbs_pr_err("firmware load wants fewer bytes than "
			   "we have to offer.\n");
	}

	/* Confirm firmware download */
	err = spu_wait_for_u32(card, IF_SPI_SCRATCH_4_REG,
					SUCCESSFUL_FW_DOWNLOAD_MAGIC);
	if (err) {
		lbs_pr_err("failed to confirm the firmware download\n");
		goto out;
	}

out:
	if (err)
		lbs_pr_err("failed to load firmware (err=%d)\n", err);
	lbs_deb_leave_args(LBS_DEB_SPI, "err %d", err);
	return err;
}

/*
 * SPI Transfer Thread
 *
 * The SPI thread handles all SPI transfers, so there is no need for a lock.
 */

/* Move a command from the card to the host */
static int if_spi_c2h_cmd(struct if_spi_card *card)
{
	struct lbs_private *priv = card->priv;
	unsigned long flags;
	int err = 0;
	u16 len;
	u8 i;

	/* We need a buffer big enough to handle whatever people send to
	 * hw_host_to_card */
	BUILD_BUG_ON(IF_SPI_CMD_BUF_SIZE < LBS_CMD_BUFFER_SIZE);
	BUILD_BUG_ON(IF_SPI_CMD_BUF_SIZE < LBS_UPLD_SIZE);

	/* It's just annoying if the buffer size isn't a multiple of 4, because
	 * then we might have len <  IF_SPI_CMD_BUF_SIZE but
	 * ALIGN(len, 4) > IF_SPI_CMD_BUF_SIZE */
	BUILD_BUG_ON(IF_SPI_CMD_BUF_SIZE % 4 != 0);

	lbs_deb_enter(LBS_DEB_SPI);

	/* How many bytes are there to read? */
	err = spu_read_u16(card, IF_SPI_SCRATCH_2_REG, &len);
	if (err)
		goto out;
	if (!len) {
		lbs_pr_err("%s: error: card has no data for host\n",
			   __func__);
		err = -EINVAL;
		goto out;
	} else if (len > IF_SPI_CMD_BUF_SIZE) {
		lbs_pr_err("%s: error: response packet too large: "
			   "%d bytes, but maximum is %d\n",
			   __func__, len, IF_SPI_CMD_BUF_SIZE);
		err = -EINVAL;
		goto out;
	}

	/* Read the data from the WLAN module into our command buffer */
	err = spu_read(card, IF_SPI_CMD_RDWRPORT_REG,
				card->cmd_buffer, ALIGN(len, 4));
	if (err)
		goto out;

	spin_lock_irqsave(&priv->driver_lock, flags);
	i = (priv->resp_idx == 0) ? 1 : 0;
	BUG_ON(priv->resp_len[i]);
	priv->resp_len[i] = len;
	memcpy(priv->resp_buf[i], card->cmd_buffer, len);
	lbs_notify_command_response(priv, i);
	spin_unlock_irqrestore(&priv->driver_lock, flags);

out:
	if (err)
		lbs_pr_err("%s: err=%d\n", __func__, err);
	lbs_deb_leave(LBS_DEB_SPI);
	return err;
}

/* Move data from the card to the host */
static int if_spi_c2h_data(struct if_spi_card *card)
{
	struct sk_buff *skb;
	char *data;
	u16 len;
	int err = 0;

	lbs_deb_enter(LBS_DEB_SPI);

	/* How many bytes are there to read? */
	err = spu_read_u16(card, IF_SPI_SCRATCH_1_REG, &len);
	if (err)
		goto out;
	if (!len) {
		lbs_pr_err("%s: error: card has no data for host\n",
			   __func__);
		err = -EINVAL;
		goto out;
	} else if (len > MRVDRV_ETH_RX_PACKET_BUFFER_SIZE) {
		lbs_pr_err("%s: error: card has %d bytes of data, but "
			   "our maximum skb size is %zu\n",
			   __func__, len, MRVDRV_ETH_RX_PACKET_BUFFER_SIZE);
		err = -EINVAL;
		goto out;
	}

	/* TODO: should we allocate a smaller skb if we have less data? */
	skb = dev_alloc_skb(MRVDRV_ETH_RX_PACKET_BUFFER_SIZE);
	if (!skb) {
		err = -ENOBUFS;
		goto out;
	}
	skb_reserve(skb, IPFIELD_ALIGN_OFFSET);
	data = skb_put(skb, len);

	/* Read the data from the WLAN module into our skb... */
	err = spu_read(card, IF_SPI_DATA_RDWRPORT_REG, data, ALIGN(len, 4));
	if (err)
		goto free_skb;

	/* pass the SKB to libertas */
	err = lbs_process_rxed_packet(card->priv, skb);
	if (err)
		goto free_skb;

	/* success */
	goto out;

free_skb:
	dev_kfree_skb(skb);
out:
	if (err)
		lbs_pr_err("%s: err=%d\n", __func__, err);
	lbs_deb_leave(LBS_DEB_SPI);
	return err;
}

/* Inform the host about a card event */
static void if_spi_e2h(struct if_spi_card *card)
{
	int err = 0;
	u32 cause;
	struct lbs_private *priv = card->priv;

	err = spu_read_u32(card, IF_SPI_SCRATCH_3_REG, &cause);
	if (err)
		goto out;

	/* re-enable the card event interrupt */
	spu_write_u16(card, IF_SPI_HOST_INT_STATUS_REG,
			~IF_SPI_HICU_CARD_EVENT);

	/* generate a card interrupt */
	spu_write_u16(card, IF_SPI_CARD_INT_CAUSE_REG, IF_SPI_CIC_HOST_EVENT);

	lbs_queue_event(priv, cause & 0xff);
out:
	if (err)
		lbs_pr_err("%s: error %d\n", __func__, err);
}

static int lbs_spi_thread(void *data)
{
	int err;
	struct if_spi_card *card = data;
	u16 hiStatus;

	while (1) {
		/* Wait to be woken up by one of two things.  First, our ISR
		 * could tell us that something happened on the WLAN.
		 * Secondly, libertas could call hw_host_to_card with more
		 * data, which we might be able to send.
		 */
		do {
			err = down_interruptible(&card->spi_ready);
			if (!card->run_thread) {
				up(&card->spi_thread_terminated);
				do_exit(0);
			}
		} while (err == EINTR);

		/* Read the host interrupt status register to see what we
		 * can do. */
		err = spu_read_u16(card, IF_SPI_HOST_INT_STATUS_REG,
					&hiStatus);
		if (err) {
			lbs_pr_err("I/O error\n");
			goto err;
		}

		if (hiStatus & IF_SPI_HIST_CMD_UPLOAD_RDY) {
			err = if_spi_c2h_cmd(card);
			if (err)
				goto err;
		}
		if (hiStatus & IF_SPI_HIST_RX_UPLOAD_RDY) {
			err = if_spi_c2h_data(card);
			if (err)
				goto err;
		}

		/* workaround: in PS mode, the card does not set the Command
		 * Download Ready bit, but it sets TX Download Ready. */
		if (hiStatus & IF_SPI_HIST_CMD_DOWNLOAD_RDY ||
		   (card->priv->psstate != PS_STATE_FULL_POWER &&
		    (hiStatus & IF_SPI_HIST_TX_DOWNLOAD_RDY))) {
			lbs_host_to_card_done(card->priv);
		}

		if (hiStatus & IF_SPI_HIST_CARD_EVENT)
			if_spi_e2h(card);

err:
		if (err)
			lbs_pr_err("%s: got error %d\n", __func__, err);
	}
}

/* Block until lbs_spi_thread thread has terminated */
static void if_spi_terminate_spi_thread(struct if_spi_card *card)
{
	/* It would be nice to use kthread_stop here, but that function
	 * can't wake threads waiting for a semaphore. */
	card->run_thread = 0;
	up(&card->spi_ready);
	down(&card->spi_thread_terminated);
}

/*
 * Host to Card
 *
 * Called from Libertas to transfer some data to the WLAN device
 * We can't sleep here. */
static int if_spi_host_to_card(struct lbs_private *priv,
				u8 type, u8 *buf, u16 nb)
{
	int err = 0;
	struct if_spi_card *card = priv->card;

	lbs_deb_enter_args(LBS_DEB_SPI, "type %d, bytes %d", type, nb);

	nb = ALIGN(nb, 4);

	switch (type) {
	case MVMS_CMD:
		err = spu_write(card, IF_SPI_CMD_RDWRPORT_REG, buf, nb);
		break;
	case MVMS_DAT:
		err = spu_write(card, IF_SPI_DATA_RDWRPORT_REG, buf, nb);
		break;
	default:
		lbs_pr_err("can't transfer buffer of type %d", type);
		err = -EINVAL;
		break;
	}

	lbs_deb_leave_args(LBS_DEB_SPI, "err=%d", err);
	return err;
}

/*
 * Host Interrupts
 *
 * Service incoming interrupts from the WLAN device. We can't sleep here, so
 * don't try to talk on the SPI bus, just wake up the SPI thread.
 */
static irqreturn_t if_spi_host_interrupt(int irq, void *dev_id)
{
	struct if_spi_card *card = dev_id;

	up(&card->spi_ready);
	return IRQ_HANDLED;
}

/*
 * SPI callbacks
 */

static int __devinit if_spi_probe(struct spi_device *spi)
{
	struct if_spi_card *card;
	struct lbs_private *priv = NULL;
	struct libertas_spi_platform_data *pdata = spi->dev.platform_data;
	int err = 0, i;
	u32 scratch;
	struct sched_param param = { .sched_priority = 1 };
	const struct firmware *helper = NULL;
	const struct firmware *mainfw = NULL;

	lbs_deb_enter(LBS_DEB_SPI);

	if (!pdata) {
		err = -EINVAL;
		goto out;
	}

	if (pdata->setup) {
		err = pdata->setup(spi);
		if (err)
			goto out;
	}

	/* Allocate card structure to represent this specific device */
	card = kzalloc(sizeof(struct if_spi_card), GFP_KERNEL);
	if (!card) {
		err = -ENOMEM;
		goto out;
	}
	spi_set_drvdata(spi, card);
	card->pdata = pdata;
	card->spi = spi;
	card->prev_xfer_time = jiffies;

	sema_init(&card->spi_ready, 0);
	sema_init(&card->spi_thread_terminated, 0);

	/* Initialize the SPI Interface Unit */
	err = spu_init(card, pdata->use_dummy_writes);
	if (err)
		goto free_card;
	err = spu_get_chip_revision(card, &card->card_id, &card->card_rev);
	if (err)
		goto free_card;

	/* Firmware load */
	err = spu_read_u32(card, IF_SPI_SCRATCH_4_REG, &scratch);
	if (err)
		goto free_card;
	if (scratch == SUCCESSFUL_FW_DOWNLOAD_MAGIC)
		lbs_deb_spi("Firmware is already loaded for "
			    "Marvell WLAN 802.11 adapter\n");
	else {
		/* Check if we support this card */
		for (i = 0; i < ARRAY_SIZE(fw_table); i++) {
			if (card->card_id == fw_table[i].model)
				break;
		}
		if (i == ARRAY_SIZE(fw_table)) {
			lbs_pr_err("Unsupported chip_id: 0x%02x\n",
					card->card_id);
			err = -ENODEV;
			goto free_card;
		}

		err = lbs_get_firmware(&card->spi->dev, NULL, NULL,
					card->card_id, &fw_table[0], &helper,
					&mainfw);
		if (err) {
			lbs_pr_err("failed to find firmware (%d)\n", err);
			goto free_card;
		}

		lbs_deb_spi("Initializing FW for Marvell WLAN 802.11 adapter "
				"(chip_id = 0x%04x, chip_rev = 0x%02x) "
				"attached to SPI bus_num %d, chip_select %d. "
				"spi->max_speed_hz=%d\n",
				card->card_id, card->card_rev,
				spi->master->bus_num, spi->chip_select,
				spi->max_speed_hz);
		err = if_spi_prog_helper_firmware(card, helper);
		if (err)
			goto free_card;
		err = if_spi_prog_main_firmware(card, mainfw);
		if (err)
			goto free_card;
		lbs_deb_spi("loaded FW for Marvell WLAN 802.11 adapter\n");
	}

	err = spu_set_interrupt_mode(card, 0, 1);
	if (err)
		goto free_card;

	/* Register our card with libertas.
	 * This will call alloc_etherdev */
	priv = lbs_add_card(card, &spi->dev);
	if (!priv) {
		err = -ENOMEM;
		goto free_card;
	}
	card->priv = priv;
	priv->card = card;
	priv->hw_host_to_card = if_spi_host_to_card;
	priv->enter_deep_sleep = NULL;
	priv->exit_deep_sleep = NULL;
	priv->reset_deep_sleep_wakeup = NULL;
	priv->fw_ready = 1;

	/* Initialize interrupt handling stuff. */
	card->run_thread = 1;
	card->spi_thread = kthread_run(lbs_spi_thread, card, "lbs_spi_thread");
	if (IS_ERR(card->spi_thread)) {
		card->run_thread = 0;
		err = PTR_ERR(card->spi_thread);
		lbs_pr_err("error creating SPI thread: err=%d\n", err);
		goto remove_card;
	}
	if (sched_setscheduler(card->spi_thread, SCHED_FIFO, &param))
		lbs_pr_err("Error setting scheduler, using default.\n");

	err = request_irq(spi->irq, if_spi_host_interrupt,
			IRQF_TRIGGER_FALLING, "libertas_spi", card);
	if (err) {
		lbs_pr_err("can't get host irq line-- request_irq failed\n");
		goto terminate_thread;
	}

	/* poke the IRQ handler so that we don't miss the first interrupt */
	up(&card->spi_ready);

	/* Start the card.
	 * This will call register_netdev, and we'll start
	 * getting interrupts... */
	err = lbs_start_card(priv);
	if (err)
		goto release_irq;

	lbs_deb_spi("Finished initializing WLAN module.\n");

	/* successful exit */
	goto out;

release_irq:
	free_irq(spi->irq, card);
terminate_thread:
	if_spi_terminate_spi_thread(card);
remove_card:
	lbs_remove_card(priv); /* will call free_netdev */
free_card:
	free_if_spi_card(card);
out:
	if (helper)
		release_firmware(helper);
	if (mainfw)
		release_firmware(mainfw);

	lbs_deb_leave_args(LBS_DEB_SPI, "err %d\n", err);
	return err;
}

static int __devexit libertas_spi_remove(struct spi_device *spi)
{
	struct if_spi_card *card = spi_get_drvdata(spi);
	struct lbs_private *priv = card->priv;

	lbs_deb_spi("libertas_spi_remove\n");
	lbs_deb_enter(LBS_DEB_SPI);

	lbs_stop_card(priv);
	lbs_remove_card(priv); /* will call free_netdev */

	priv->surpriseremoved = 1;
	free_irq(spi->irq, card);
	if_spi_terminate_spi_thread(card);
	if (card->pdata->teardown)
		card->pdata->teardown(spi);
	free_if_spi_card(card);
	lbs_deb_leave(LBS_DEB_SPI);
	return 0;
}

static struct spi_driver libertas_spi_driver = {
	.probe	= if_spi_probe,
	.remove = __devexit_p(libertas_spi_remove),
	.driver = {
		.name	= "libertas_spi",
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
	},
};

/*
 * Module functions
 */

static int __init if_spi_init_module(void)
{
	int ret = 0;
	lbs_deb_enter(LBS_DEB_SPI);
	printk(KERN_INFO "libertas_spi: Libertas SPI driver\n");
	ret = spi_register_driver(&libertas_spi_driver);
	lbs_deb_leave(LBS_DEB_SPI);
	return ret;
}

static void __exit if_spi_exit_module(void)
{
	lbs_deb_enter(LBS_DEB_SPI);
	spi_unregister_driver(&libertas_spi_driver);
	lbs_deb_leave(LBS_DEB_SPI);
}

module_init(if_spi_init_module);
module_exit(if_spi_exit_module);

MODULE_DESCRIPTION("Libertas SPI WLAN Driver");
MODULE_AUTHOR("Andrey Yurovsky <andrey@cozybit.com>, "
	      "Colin McCabe <colin@cozybit.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:libertas_spi");
