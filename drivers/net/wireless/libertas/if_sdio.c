/*
 *  linux/drivers/net/wireless/libertas/if_sdio.c
 *
 *  Copyright 2007-2008 Pierre Ossman
 *
 * Inspired by if_cs.c, Copyright 2007 Holger Schurig
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This hardware has more or less no CMD53 support, so all registers
 * must be accessed using sdio_readb()/sdio_writeb().
 *
 * Transfers must be in one transaction or the firmware goes bonkers.
 * This means that the transfer must either be small enough to do a
 * byte based transfer or it must be padded to a multiple of the
 * current block size.
 *
 * As SDIO is still new to the kernel, it is unfortunately common with
 * bugs in the host controllers related to that. One such bug is that
 * controllers cannot do transfers that aren't a multiple of 4 bytes.
 * If you don't have time to fix the host controller driver, you can
 * work around the problem by modifying if_sdio_host_to_card() and
 * if_sdio_card_to_host() to pad the data.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/firmware.h>
#include <linux/netdevice.h>
#include <linux/delay.h>
#include <linux/mmc/card.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/host.h>
#include <linux/pm_runtime.h>

#include "host.h"
#include "decl.h"
#include "defs.h"
#include "dev.h"
#include "cmd.h"
#include "if_sdio.h"

static void if_sdio_interrupt(struct sdio_func *func);

/* The if_sdio_remove() callback function is called when
 * user removes this module from kernel space or ejects
 * the card from the slot. The driver handles these 2 cases
 * differently for SD8688 combo chip.
 * If the user is removing the module, the FUNC_SHUTDOWN
 * command for SD8688 is sent to the firmware.
 * If the card is removed, there is no need to send this command.
 *
 * The variable 'user_rmmod' is used to distinguish these two
 * scenarios. This flag is initialized as FALSE in case the card
 * is removed, and will be set to TRUE for module removal when
 * module_exit function is called.
 */
static u8 user_rmmod;

static const struct sdio_device_id if_sdio_ids[] = {
	{ SDIO_DEVICE(SDIO_VENDOR_ID_MARVELL,
			SDIO_DEVICE_ID_MARVELL_LIBERTAS) },
	{ SDIO_DEVICE(SDIO_VENDOR_ID_MARVELL,
			SDIO_DEVICE_ID_MARVELL_8688WLAN) },
	{ /* end: all zeroes */				},
};

MODULE_DEVICE_TABLE(sdio, if_sdio_ids);

#define MODEL_8385	0x04
#define MODEL_8686	0x0b
#define MODEL_8688	0x10

static const struct lbs_fw_table fw_table[] = {
	{ MODEL_8385, "libertas/sd8385_helper.bin", "libertas/sd8385.bin" },
	{ MODEL_8385, "sd8385_helper.bin", "sd8385.bin" },
	{ MODEL_8686, "libertas/sd8686_v9_helper.bin", "libertas/sd8686_v9.bin" },
	{ MODEL_8686, "libertas/sd8686_v8_helper.bin", "libertas/sd8686_v8.bin" },
	{ MODEL_8686, "sd8686_helper.bin", "sd8686.bin" },
	{ MODEL_8688, "libertas/sd8688_helper.bin", "libertas/sd8688.bin" },
	{ MODEL_8688, "sd8688_helper.bin", "sd8688.bin" },
	{ 0, NULL, NULL }
};
MODULE_FIRMWARE("libertas/sd8385_helper.bin");
MODULE_FIRMWARE("libertas/sd8385.bin");
MODULE_FIRMWARE("sd8385_helper.bin");
MODULE_FIRMWARE("sd8385.bin");
MODULE_FIRMWARE("libertas/sd8686_v9_helper.bin");
MODULE_FIRMWARE("libertas/sd8686_v9.bin");
MODULE_FIRMWARE("libertas/sd8686_v8_helper.bin");
MODULE_FIRMWARE("libertas/sd8686_v8.bin");
MODULE_FIRMWARE("sd8686_helper.bin");
MODULE_FIRMWARE("sd8686.bin");
MODULE_FIRMWARE("libertas/sd8688_helper.bin");
MODULE_FIRMWARE("libertas/sd8688.bin");
MODULE_FIRMWARE("sd8688_helper.bin");
MODULE_FIRMWARE("sd8688.bin");

struct if_sdio_packet {
	struct if_sdio_packet	*next;
	u16			nb;
	u8			buffer[0] __attribute__((aligned(4)));
};

struct if_sdio_card {
	struct sdio_func	*func;
	struct lbs_private	*priv;

	int			model;
	unsigned long		ioport;
	unsigned int		scratch_reg;
	bool			started;
	wait_queue_head_t	pwron_waitq;

	u8			buffer[65536] __attribute__((aligned(4)));

	spinlock_t		lock;
	struct if_sdio_packet	*packets;

	struct workqueue_struct	*workqueue;
	struct work_struct	packet_worker;

	u8			rx_unit;
};

static void if_sdio_finish_power_on(struct if_sdio_card *card);
static int if_sdio_power_off(struct if_sdio_card *card);

/********************************************************************/
/* I/O                                                              */
/********************************************************************/

/*
 *  For SD8385/SD8686, this function reads firmware status after
 *  the image is downloaded, or reads RX packet length when
 *  interrupt (with IF_SDIO_H_INT_UPLD bit set) is received.
 *  For SD8688, this function reads firmware status only.
 */
static u16 if_sdio_read_scratch(struct if_sdio_card *card, int *err)
{
	int ret;
	u16 scratch;

	scratch = sdio_readb(card->func, card->scratch_reg, &ret);
	if (!ret)
		scratch |= sdio_readb(card->func, card->scratch_reg + 1,
					&ret) << 8;

	if (err)
		*err = ret;

	if (ret)
		return 0xffff;

	return scratch;
}

static u8 if_sdio_read_rx_unit(struct if_sdio_card *card)
{
	int ret;
	u8 rx_unit;

	rx_unit = sdio_readb(card->func, IF_SDIO_RX_UNIT, &ret);

	if (ret)
		rx_unit = 0;

	return rx_unit;
}

static u16 if_sdio_read_rx_len(struct if_sdio_card *card, int *err)
{
	int ret;
	u16 rx_len;

	switch (card->model) {
	case MODEL_8385:
	case MODEL_8686:
		rx_len = if_sdio_read_scratch(card, &ret);
		break;
	case MODEL_8688:
	default: /* for newer chipsets */
		rx_len = sdio_readb(card->func, IF_SDIO_RX_LEN, &ret);
		if (!ret)
			rx_len <<= card->rx_unit;
		else
			rx_len = 0xffff;	/* invalid length */

		break;
	}

	if (err)
		*err = ret;

	return rx_len;
}

static int if_sdio_handle_cmd(struct if_sdio_card *card,
		u8 *buffer, unsigned size)
{
	struct lbs_private *priv = card->priv;
	int ret;
	unsigned long flags;
	u8 i;

	lbs_deb_enter(LBS_DEB_SDIO);

	if (size > LBS_CMD_BUFFER_SIZE) {
		lbs_deb_sdio("response packet too large (%d bytes)\n",
			(int)size);
		ret = -E2BIG;
		goto out;
	}

	spin_lock_irqsave(&priv->driver_lock, flags);

	i = (priv->resp_idx == 0) ? 1 : 0;
	BUG_ON(priv->resp_len[i]);
	priv->resp_len[i] = size;
	memcpy(priv->resp_buf[i], buffer, size);
	lbs_notify_command_response(priv, i);

	spin_unlock_irqrestore(&card->priv->driver_lock, flags);

	ret = 0;

out:
	lbs_deb_leave_args(LBS_DEB_SDIO, "ret %d", ret);
	return ret;
}

static int if_sdio_handle_data(struct if_sdio_card *card,
		u8 *buffer, unsigned size)
{
	int ret;
	struct sk_buff *skb;
	char *data;

	lbs_deb_enter(LBS_DEB_SDIO);

	if (size > MRVDRV_ETH_RX_PACKET_BUFFER_SIZE) {
		lbs_deb_sdio("response packet too large (%d bytes)\n",
			(int)size);
		ret = -E2BIG;
		goto out;
	}

	skb = dev_alloc_skb(MRVDRV_ETH_RX_PACKET_BUFFER_SIZE + NET_IP_ALIGN);
	if (!skb) {
		ret = -ENOMEM;
		goto out;
	}

	skb_reserve(skb, NET_IP_ALIGN);

	data = skb_put(skb, size);

	memcpy(data, buffer, size);

	lbs_process_rxed_packet(card->priv, skb);

	ret = 0;

out:
	lbs_deb_leave_args(LBS_DEB_SDIO, "ret %d", ret);

	return ret;
}

static int if_sdio_handle_event(struct if_sdio_card *card,
		u8 *buffer, unsigned size)
{
	int ret;
	u32 event;

	lbs_deb_enter(LBS_DEB_SDIO);

	if (card->model == MODEL_8385) {
		event = sdio_readb(card->func, IF_SDIO_EVENT, &ret);
		if (ret)
			goto out;

		/* right shift 3 bits to get the event id */
		event >>= 3;
	} else {
		if (size < 4) {
			lbs_deb_sdio("event packet too small (%d bytes)\n",
				(int)size);
			ret = -EINVAL;
			goto out;
		}
		event = buffer[3] << 24;
		event |= buffer[2] << 16;
		event |= buffer[1] << 8;
		event |= buffer[0] << 0;
	}

	lbs_queue_event(card->priv, event & 0xFF);
	ret = 0;

out:
	lbs_deb_leave_args(LBS_DEB_SDIO, "ret %d", ret);

	return ret;
}

static int if_sdio_wait_status(struct if_sdio_card *card, const u8 condition)
{
	u8 status;
	unsigned long timeout;
	int ret = 0;

	timeout = jiffies + HZ;
	while (1) {
		status = sdio_readb(card->func, IF_SDIO_STATUS, &ret);
		if (ret)
			return ret;
		if ((status & condition) == condition)
			break;
		if (time_after(jiffies, timeout))
			return -ETIMEDOUT;
		mdelay(1);
	}
	return ret;
}

static int if_sdio_card_to_host(struct if_sdio_card *card)
{
	int ret;
	u16 size, type, chunk;

	lbs_deb_enter(LBS_DEB_SDIO);

	size = if_sdio_read_rx_len(card, &ret);
	if (ret)
		goto out;

	if (size < 4) {
		lbs_deb_sdio("invalid packet size (%d bytes) from firmware\n",
			(int)size);
		ret = -EINVAL;
		goto out;
	}

	ret = if_sdio_wait_status(card, IF_SDIO_IO_RDY);
	if (ret)
		goto out;

	/*
	 * The transfer must be in one transaction or the firmware
	 * goes suicidal. There's no way to guarantee that for all
	 * controllers, but we can at least try.
	 */
	chunk = sdio_align_size(card->func, size);

	ret = sdio_readsb(card->func, card->buffer, card->ioport, chunk);
	if (ret)
		goto out;

	chunk = card->buffer[0] | (card->buffer[1] << 8);
	type = card->buffer[2] | (card->buffer[3] << 8);

	lbs_deb_sdio("packet of type %d and size %d bytes\n",
		(int)type, (int)chunk);

	if (chunk > size) {
		lbs_deb_sdio("packet fragment (%d > %d)\n",
			(int)chunk, (int)size);
		ret = -EINVAL;
		goto out;
	}

	if (chunk < size) {
		lbs_deb_sdio("packet fragment (%d < %d)\n",
			(int)chunk, (int)size);
	}

	switch (type) {
	case MVMS_CMD:
		ret = if_sdio_handle_cmd(card, card->buffer + 4, chunk - 4);
		if (ret)
			goto out;
		break;
	case MVMS_DAT:
		ret = if_sdio_handle_data(card, card->buffer + 4, chunk - 4);
		if (ret)
			goto out;
		break;
	case MVMS_EVENT:
		ret = if_sdio_handle_event(card, card->buffer + 4, chunk - 4);
		if (ret)
			goto out;
		break;
	default:
		lbs_deb_sdio("invalid type (%d) from firmware\n",
				(int)type);
		ret = -EINVAL;
		goto out;
	}

out:
	if (ret)
		pr_err("problem fetching packet from firmware\n");

	lbs_deb_leave_args(LBS_DEB_SDIO, "ret %d", ret);

	return ret;
}

static void if_sdio_host_to_card_worker(struct work_struct *work)
{
	struct if_sdio_card *card;
	struct if_sdio_packet *packet;
	int ret;
	unsigned long flags;

	lbs_deb_enter(LBS_DEB_SDIO);

	card = container_of(work, struct if_sdio_card, packet_worker);

	while (1) {
		spin_lock_irqsave(&card->lock, flags);
		packet = card->packets;
		if (packet)
			card->packets = packet->next;
		spin_unlock_irqrestore(&card->lock, flags);

		if (!packet)
			break;

		sdio_claim_host(card->func);

		ret = if_sdio_wait_status(card, IF_SDIO_IO_RDY);
		if (ret == 0) {
			ret = sdio_writesb(card->func, card->ioport,
					   packet->buffer, packet->nb);
		}

		if (ret)
			pr_err("error %d sending packet to firmware\n", ret);

		sdio_release_host(card->func);

		kfree(packet);
	}

	lbs_deb_leave(LBS_DEB_SDIO);
}

/********************************************************************/
/* Firmware                                                         */
/********************************************************************/

#define FW_DL_READY_STATUS (IF_SDIO_IO_RDY | IF_SDIO_DL_RDY)

static int if_sdio_prog_helper(struct if_sdio_card *card,
				const struct firmware *fw)
{
	int ret;
	unsigned long timeout;
	u8 *chunk_buffer;
	u32 chunk_size;
	const u8 *firmware;
	size_t size;

	lbs_deb_enter(LBS_DEB_SDIO);

	chunk_buffer = kzalloc(64, GFP_KERNEL);
	if (!chunk_buffer) {
		ret = -ENOMEM;
		goto out;
	}

	sdio_claim_host(card->func);

	ret = sdio_set_block_size(card->func, 32);
	if (ret)
		goto release;

	firmware = fw->data;
	size = fw->size;

	while (size) {
		ret = if_sdio_wait_status(card, FW_DL_READY_STATUS);
		if (ret)
			goto release;

		/* On some platforms (like Davinci) the chip needs more time
		 * between helper blocks.
		 */
		mdelay(2);

		chunk_size = min(size, (size_t)60);

		*((__le32*)chunk_buffer) = cpu_to_le32(chunk_size);
		memcpy(chunk_buffer + 4, firmware, chunk_size);
/*
		lbs_deb_sdio("sending %d bytes chunk\n", chunk_size);
*/
		ret = sdio_writesb(card->func, card->ioport,
				chunk_buffer, 64);
		if (ret)
			goto release;

		firmware += chunk_size;
		size -= chunk_size;
	}

	/* an empty block marks the end of the transfer */
	memset(chunk_buffer, 0, 4);
	ret = sdio_writesb(card->func, card->ioport, chunk_buffer, 64);
	if (ret)
		goto release;

	lbs_deb_sdio("waiting for helper to boot...\n");

	/* wait for the helper to boot by looking at the size register */
	timeout = jiffies + HZ;
	while (1) {
		u16 req_size;

		req_size = sdio_readb(card->func, IF_SDIO_RD_BASE, &ret);
		if (ret)
			goto release;

		req_size |= sdio_readb(card->func, IF_SDIO_RD_BASE + 1, &ret) << 8;
		if (ret)
			goto release;

		if (req_size != 0)
			break;

		if (time_after(jiffies, timeout)) {
			ret = -ETIMEDOUT;
			goto release;
		}

		msleep(10);
	}

	ret = 0;

release:
	sdio_release_host(card->func);
	kfree(chunk_buffer);

out:
	if (ret)
		pr_err("failed to load helper firmware\n");

	lbs_deb_leave_args(LBS_DEB_SDIO, "ret %d", ret);
	return ret;
}

static int if_sdio_prog_real(struct if_sdio_card *card,
				const struct firmware *fw)
{
	int ret;
	unsigned long timeout;
	u8 *chunk_buffer;
	u32 chunk_size;
	const u8 *firmware;
	size_t size, req_size;

	lbs_deb_enter(LBS_DEB_SDIO);

	chunk_buffer = kzalloc(512, GFP_KERNEL);
	if (!chunk_buffer) {
		ret = -ENOMEM;
		goto out;
	}

	sdio_claim_host(card->func);

	ret = sdio_set_block_size(card->func, 32);
	if (ret)
		goto release;

	firmware = fw->data;
	size = fw->size;

	while (size) {
		timeout = jiffies + HZ;
		while (1) {
			ret = if_sdio_wait_status(card, FW_DL_READY_STATUS);
			if (ret)
				goto release;

			req_size = sdio_readb(card->func, IF_SDIO_RD_BASE,
					&ret);
			if (ret)
				goto release;

			req_size |= sdio_readb(card->func, IF_SDIO_RD_BASE + 1,
					&ret) << 8;
			if (ret)
				goto release;

			/*
			 * For SD8688 wait until the length is not 0, 1 or 2
			 * before downloading the first FW block,
			 * since BOOT code writes the register to indicate the
			 * helper/FW download winner,
			 * the value could be 1 or 2 (Func1 or Func2).
			 */
			if ((size != fw->size) || (req_size > 2))
				break;
			if (time_after(jiffies, timeout)) {
				ret = -ETIMEDOUT;
				goto release;
			}
			mdelay(1);
		}

/*
		lbs_deb_sdio("firmware wants %d bytes\n", (int)req_size);
*/
		if (req_size == 0) {
			lbs_deb_sdio("firmware helper gave up early\n");
			ret = -EIO;
			goto release;
		}

		if (req_size & 0x01) {
			lbs_deb_sdio("firmware helper signalled error\n");
			ret = -EIO;
			goto release;
		}

		if (req_size > size)
			req_size = size;

		while (req_size) {
			chunk_size = min(req_size, (size_t)512);

			memcpy(chunk_buffer, firmware, chunk_size);
/*
			lbs_deb_sdio("sending %d bytes (%d bytes) chunk\n",
				chunk_size, (chunk_size + 31) / 32 * 32);
*/
			ret = sdio_writesb(card->func, card->ioport,
				chunk_buffer, roundup(chunk_size, 32));
			if (ret)
				goto release;

			firmware += chunk_size;
			size -= chunk_size;
			req_size -= chunk_size;
		}
	}

	ret = 0;

	lbs_deb_sdio("waiting for firmware to boot...\n");

	/* wait for the firmware to boot */
	timeout = jiffies + HZ;
	while (1) {
		u16 scratch;

		scratch = if_sdio_read_scratch(card, &ret);
		if (ret)
			goto release;

		if (scratch == IF_SDIO_FIRMWARE_OK)
			break;

		if (time_after(jiffies, timeout)) {
			ret = -ETIMEDOUT;
			goto release;
		}

		msleep(10);
	}

	ret = 0;

release:
	sdio_release_host(card->func);
	kfree(chunk_buffer);

out:
	if (ret)
		pr_err("failed to load firmware\n");

	lbs_deb_leave_args(LBS_DEB_SDIO, "ret %d", ret);
	return ret;
}

static void if_sdio_do_prog_firmware(struct lbs_private *priv, int ret,
				     const struct firmware *helper,
				     const struct firmware *mainfw)
{
	struct if_sdio_card *card = priv->card;

	if (ret) {
		pr_err("failed to find firmware (%d)\n", ret);
		return;
	}

	ret = if_sdio_prog_helper(card, helper);
	if (ret)
		goto out;

	lbs_deb_sdio("Helper firmware loaded\n");

	ret = if_sdio_prog_real(card, mainfw);
	if (ret)
		goto out;

	lbs_deb_sdio("Firmware loaded\n");
	if_sdio_finish_power_on(card);

out:
	release_firmware(helper);
	release_firmware(mainfw);
}

static int if_sdio_prog_firmware(struct if_sdio_card *card)
{
	int ret;
	u16 scratch;

	lbs_deb_enter(LBS_DEB_SDIO);

	/*
	 * Disable interrupts
	 */
	sdio_claim_host(card->func);
	sdio_writeb(card->func, 0x00, IF_SDIO_H_INT_MASK, &ret);
	sdio_release_host(card->func);

	sdio_claim_host(card->func);
	scratch = if_sdio_read_scratch(card, &ret);
	sdio_release_host(card->func);

	lbs_deb_sdio("firmware status = %#x\n", scratch);
	lbs_deb_sdio("scratch ret = %d\n", ret);

	if (ret)
		goto out;


	/*
	 * The manual clearly describes that FEDC is the right code to use
	 * to detect firmware presence, but for SD8686 it is not that simple.
	 * Scratch is also used to store the RX packet length, so we lose
	 * the FEDC value early on. So we use a non-zero check in order
	 * to validate firmware presence.
	 * Additionally, the SD8686 in the Gumstix always has the high scratch
	 * bit set, even when the firmware is not loaded. So we have to
	 * exclude that from the test.
	 */
	if (scratch == IF_SDIO_FIRMWARE_OK) {
		lbs_deb_sdio("firmware already loaded\n");
		if_sdio_finish_power_on(card);
		return 0;
	} else if ((card->model == MODEL_8686) && (scratch & 0x7fff)) {
		lbs_deb_sdio("firmware may be running\n");
		if_sdio_finish_power_on(card);
		return 0;
	}

	ret = lbs_get_firmware_async(card->priv, &card->func->dev, card->model,
				     fw_table, if_sdio_do_prog_firmware);

out:
	lbs_deb_leave_args(LBS_DEB_SDIO, "ret %d", ret);
	return ret;
}

/********************************************************************/
/* Power management                                                 */
/********************************************************************/

/* Finish power on sequence (after firmware is loaded) */
static void if_sdio_finish_power_on(struct if_sdio_card *card)
{
	struct sdio_func *func = card->func;
	struct lbs_private *priv = card->priv;
	int ret;

	sdio_claim_host(func);
	sdio_set_block_size(card->func, IF_SDIO_BLOCK_SIZE);

	/*
	 * Get rx_unit if the chip is SD8688 or newer.
	 * SD8385 & SD8686 do not have rx_unit.
	 */
	if ((card->model != MODEL_8385)
			&& (card->model != MODEL_8686))
		card->rx_unit = if_sdio_read_rx_unit(card);
	else
		card->rx_unit = 0;

	/*
	 * Set up the interrupt handler late.
	 *
	 * If we set it up earlier, the (buggy) hardware generates a spurious
	 * interrupt, even before the interrupt has been enabled, with
	 * CCCR_INTx = 0.
	 *
	 * We register the interrupt handler late so that we can handle any
	 * spurious interrupts, and also to avoid generation of that known
	 * spurious interrupt in the first place.
	 */
	ret = sdio_claim_irq(func, if_sdio_interrupt);
	if (ret)
		goto release;

	/*
	 * Enable interrupts now that everything is set up
	 */
	sdio_writeb(func, 0x0f, IF_SDIO_H_INT_MASK, &ret);
	if (ret)
		goto release_irq;

	sdio_release_host(func);

	/*
	 * FUNC_INIT is required for SD8688 WLAN/BT multiple functions
	 */
	if (card->model == MODEL_8688) {
		struct cmd_header cmd;

		memset(&cmd, 0, sizeof(cmd));

		lbs_deb_sdio("send function INIT command\n");
		if (__lbs_cmd(priv, CMD_FUNC_INIT, &cmd, sizeof(cmd),
				lbs_cmd_copyback, (unsigned long) &cmd))
			netdev_alert(priv->dev, "CMD_FUNC_INIT cmd failed\n");
	}

	priv->fw_ready = 1;
	wake_up(&card->pwron_waitq);

	if (!card->started) {
		ret = lbs_start_card(priv);
		if_sdio_power_off(card);
		if (ret == 0) {
			card->started = true;
			/* Tell PM core that we don't need the card to be
			 * powered now */
			pm_runtime_put_noidle(&func->dev);
		}
	}

	return;

release_irq:
	sdio_release_irq(func);
release:
	sdio_release_host(func);
}

static int if_sdio_power_on(struct if_sdio_card *card)
{
	struct sdio_func *func = card->func;
	struct mmc_host *host = func->card->host;
	int ret;

	sdio_claim_host(func);

	ret = sdio_enable_func(func);
	if (ret)
		goto release;

	/* For 1-bit transfers to the 8686 model, we need to enable the
	 * interrupt flag in the CCCR register. Set the MMC_QUIRK_LENIENT_FN0
	 * bit to allow access to non-vendor registers. */
	if ((card->model == MODEL_8686) &&
	    (host->caps & MMC_CAP_SDIO_IRQ) &&
	    (host->ios.bus_width == MMC_BUS_WIDTH_1)) {
		u8 reg;

		func->card->quirks |= MMC_QUIRK_LENIENT_FN0;
		reg = sdio_f0_readb(func, SDIO_CCCR_IF, &ret);
		if (ret)
			goto disable;

		reg |= SDIO_BUS_ECSI;
		sdio_f0_writeb(func, reg, SDIO_CCCR_IF, &ret);
		if (ret)
			goto disable;
	}

	card->ioport = sdio_readb(func, IF_SDIO_IOPORT, &ret);
	if (ret)
		goto disable;

	card->ioport |= sdio_readb(func, IF_SDIO_IOPORT + 1, &ret) << 8;
	if (ret)
		goto disable;

	card->ioport |= sdio_readb(func, IF_SDIO_IOPORT + 2, &ret) << 16;
	if (ret)
		goto disable;

	sdio_release_host(func);
	ret = if_sdio_prog_firmware(card);
	if (ret) {
		sdio_disable_func(func);
		return ret;
	}

	return 0;

disable:
	sdio_disable_func(func);
release:
	sdio_release_host(func);
	return ret;
}

static int if_sdio_power_off(struct if_sdio_card *card)
{
	struct sdio_func *func = card->func;
	struct lbs_private *priv = card->priv;

	priv->fw_ready = 0;

	sdio_claim_host(func);
	sdio_release_irq(func);
	sdio_disable_func(func);
	sdio_release_host(func);
	return 0;
}


/*******************************************************************/
/* Libertas callbacks                                              */
/*******************************************************************/

static int if_sdio_host_to_card(struct lbs_private *priv,
		u8 type, u8 *buf, u16 nb)
{
	int ret;
	struct if_sdio_card *card;
	struct if_sdio_packet *packet, *cur;
	u16 size;
	unsigned long flags;

	lbs_deb_enter_args(LBS_DEB_SDIO, "type %d, bytes %d", type, nb);

	card = priv->card;

	if (nb > (65536 - sizeof(struct if_sdio_packet) - 4)) {
		ret = -EINVAL;
		goto out;
	}

	/*
	 * The transfer must be in one transaction or the firmware
	 * goes suicidal. There's no way to guarantee that for all
	 * controllers, but we can at least try.
	 */
	size = sdio_align_size(card->func, nb + 4);

	packet = kzalloc(sizeof(struct if_sdio_packet) + size,
			GFP_ATOMIC);
	if (!packet) {
		ret = -ENOMEM;
		goto out;
	}

	packet->next = NULL;
	packet->nb = size;

	/*
	 * SDIO specific header.
	 */
	packet->buffer[0] = (nb + 4) & 0xff;
	packet->buffer[1] = ((nb + 4) >> 8) & 0xff;
	packet->buffer[2] = type;
	packet->buffer[3] = 0;

	memcpy(packet->buffer + 4, buf, nb);

	spin_lock_irqsave(&card->lock, flags);

	if (!card->packets)
		card->packets = packet;
	else {
		cur = card->packets;
		while (cur->next)
			cur = cur->next;
		cur->next = packet;
	}

	switch (type) {
	case MVMS_CMD:
		priv->dnld_sent = DNLD_CMD_SENT;
		break;
	case MVMS_DAT:
		priv->dnld_sent = DNLD_DATA_SENT;
		break;
	default:
		lbs_deb_sdio("unknown packet type %d\n", (int)type);
	}

	spin_unlock_irqrestore(&card->lock, flags);

	queue_work(card->workqueue, &card->packet_worker);

	ret = 0;

out:
	lbs_deb_leave_args(LBS_DEB_SDIO, "ret %d", ret);

	return ret;
}

static int if_sdio_enter_deep_sleep(struct lbs_private *priv)
{
	int ret = -1;
	struct cmd_header cmd;

	memset(&cmd, 0, sizeof(cmd));

	lbs_deb_sdio("send DEEP_SLEEP command\n");
	ret = __lbs_cmd(priv, CMD_802_11_DEEP_SLEEP, &cmd, sizeof(cmd),
			lbs_cmd_copyback, (unsigned long) &cmd);
	if (ret)
		netdev_err(priv->dev, "DEEP_SLEEP cmd failed\n");

	mdelay(200);
	return ret;
}

static int if_sdio_exit_deep_sleep(struct lbs_private *priv)
{
	struct if_sdio_card *card = priv->card;
	int ret = -1;

	lbs_deb_enter(LBS_DEB_SDIO);
	sdio_claim_host(card->func);

	sdio_writeb(card->func, HOST_POWER_UP, CONFIGURATION_REG, &ret);
	if (ret)
		netdev_err(priv->dev, "sdio_writeb failed!\n");

	sdio_release_host(card->func);
	lbs_deb_leave_args(LBS_DEB_SDIO, "ret %d", ret);
	return ret;
}

static int if_sdio_reset_deep_sleep_wakeup(struct lbs_private *priv)
{
	struct if_sdio_card *card = priv->card;
	int ret = -1;

	lbs_deb_enter(LBS_DEB_SDIO);
	sdio_claim_host(card->func);

	sdio_writeb(card->func, 0, CONFIGURATION_REG, &ret);
	if (ret)
		netdev_err(priv->dev, "sdio_writeb failed!\n");

	sdio_release_host(card->func);
	lbs_deb_leave_args(LBS_DEB_SDIO, "ret %d", ret);
	return ret;

}

static struct mmc_host *reset_host;

static void if_sdio_reset_card_worker(struct work_struct *work)
{
	/*
	 * The actual reset operation must be run outside of lbs_thread. This
	 * is because mmc_remove_host() will cause the device to be instantly
	 * destroyed, and the libertas driver then needs to end lbs_thread,
	 * leading to a deadlock.
	 *
	 * We run it in a workqueue totally independent from the if_sdio_card
	 * instance for that reason.
	 */

	pr_info("Resetting card...");
	mmc_remove_host(reset_host);
	mmc_add_host(reset_host);
}
static DECLARE_WORK(card_reset_work, if_sdio_reset_card_worker);

static void if_sdio_reset_card(struct lbs_private *priv)
{
	struct if_sdio_card *card = priv->card;

	if (work_pending(&card_reset_work))
		return;

	reset_host = card->func->card->host;
	schedule_work(&card_reset_work);
}

static int if_sdio_power_save(struct lbs_private *priv)
{
	struct if_sdio_card *card = priv->card;
	int ret;

	flush_workqueue(card->workqueue);

	ret = if_sdio_power_off(card);

	/* Let runtime PM know the card is powered off */
	pm_runtime_put_sync(&card->func->dev);

	return ret;
}

static int if_sdio_power_restore(struct lbs_private *priv)
{
	struct if_sdio_card *card = priv->card;
	int r;

	/* Make sure the card will not be powered off by runtime PM */
	pm_runtime_get_sync(&card->func->dev);

	r = if_sdio_power_on(card);
	if (r)
		return r;

	wait_event(card->pwron_waitq, priv->fw_ready);
	return 0;
}


/*******************************************************************/
/* SDIO callbacks                                                  */
/*******************************************************************/

static void if_sdio_interrupt(struct sdio_func *func)
{
	int ret;
	struct if_sdio_card *card;
	u8 cause;

	lbs_deb_enter(LBS_DEB_SDIO);

	card = sdio_get_drvdata(func);

	cause = sdio_readb(card->func, IF_SDIO_H_INT_STATUS, &ret);
	if (ret || !cause)
		goto out;

	lbs_deb_sdio("interrupt: 0x%X\n", (unsigned)cause);

	sdio_writeb(card->func, ~cause, IF_SDIO_H_INT_STATUS, &ret);
	if (ret)
		goto out;

	/*
	 * Ignore the define name, this really means the card has
	 * successfully received the command.
	 */
	card->priv->is_activity_detected = 1;
	if (cause & IF_SDIO_H_INT_DNLD)
		lbs_host_to_card_done(card->priv);


	if (cause & IF_SDIO_H_INT_UPLD) {
		ret = if_sdio_card_to_host(card);
		if (ret)
			goto out;
	}

	ret = 0;

out:
	lbs_deb_leave_args(LBS_DEB_SDIO, "ret %d", ret);
}

static int if_sdio_probe(struct sdio_func *func,
		const struct sdio_device_id *id)
{
	struct if_sdio_card *card;
	struct lbs_private *priv;
	int ret, i;
	unsigned int model;
	struct if_sdio_packet *packet;

	lbs_deb_enter(LBS_DEB_SDIO);

	for (i = 0;i < func->card->num_info;i++) {
		if (sscanf(func->card->info[i],
				"802.11 SDIO ID: %x", &model) == 1)
			break;
		if (sscanf(func->card->info[i],
				"ID: %x", &model) == 1)
			break;
		if (!strcmp(func->card->info[i], "IBIS Wireless SDIO Card")) {
			model = MODEL_8385;
			break;
		}
	}

	if (i == func->card->num_info) {
		pr_err("unable to identify card model\n");
		return -ENODEV;
	}

	card = kzalloc(sizeof(struct if_sdio_card), GFP_KERNEL);
	if (!card)
		return -ENOMEM;

	card->func = func;
	card->model = model;

	switch (card->model) {
	case MODEL_8385:
		card->scratch_reg = IF_SDIO_SCRATCH_OLD;
		break;
	case MODEL_8686:
		card->scratch_reg = IF_SDIO_SCRATCH;
		break;
	case MODEL_8688:
	default: /* for newer chipsets */
		card->scratch_reg = IF_SDIO_FW_STATUS;
		break;
	}

	spin_lock_init(&card->lock);
	card->workqueue = create_workqueue("libertas_sdio");
	INIT_WORK(&card->packet_worker, if_sdio_host_to_card_worker);
	init_waitqueue_head(&card->pwron_waitq);

	/* Check if we support this card */
	for (i = 0; i < ARRAY_SIZE(fw_table); i++) {
		if (card->model == fw_table[i].model)
			break;
	}
	if (i == ARRAY_SIZE(fw_table)) {
		pr_err("unknown card model 0x%x\n", card->model);
		ret = -ENODEV;
		goto free;
	}

	sdio_set_drvdata(func, card);

	lbs_deb_sdio("class = 0x%X, vendor = 0x%X, "
			"device = 0x%X, model = 0x%X, ioport = 0x%X\n",
			func->class, func->vendor, func->device,
			model, (unsigned)card->ioport);


	priv = lbs_add_card(card, &func->dev);
	if (!priv) {
		ret = -ENOMEM;
		goto free;
	}

	card->priv = priv;

	priv->card = card;
	priv->hw_host_to_card = if_sdio_host_to_card;
	priv->enter_deep_sleep = if_sdio_enter_deep_sleep;
	priv->exit_deep_sleep = if_sdio_exit_deep_sleep;
	priv->reset_deep_sleep_wakeup = if_sdio_reset_deep_sleep_wakeup;
	priv->reset_card = if_sdio_reset_card;
	priv->power_save = if_sdio_power_save;
	priv->power_restore = if_sdio_power_restore;

	ret = if_sdio_power_on(card);
	if (ret)
		goto err_activate_card;

out:
	lbs_deb_leave_args(LBS_DEB_SDIO, "ret %d", ret);

	return ret;

err_activate_card:
	flush_workqueue(card->workqueue);
	lbs_remove_card(priv);
free:
	destroy_workqueue(card->workqueue);
	while (card->packets) {
		packet = card->packets;
		card->packets = card->packets->next;
		kfree(packet);
	}

	kfree(card);

	goto out;
}

static void if_sdio_remove(struct sdio_func *func)
{
	struct if_sdio_card *card;
	struct if_sdio_packet *packet;

	lbs_deb_enter(LBS_DEB_SDIO);

	card = sdio_get_drvdata(func);

	/* Undo decrement done above in if_sdio_probe */
	pm_runtime_get_noresume(&func->dev);

	if (user_rmmod && (card->model == MODEL_8688)) {
		/*
		 * FUNC_SHUTDOWN is required for SD8688 WLAN/BT
		 * multiple functions
		 */
		struct cmd_header cmd;

		memset(&cmd, 0, sizeof(cmd));

		lbs_deb_sdio("send function SHUTDOWN command\n");
		if (__lbs_cmd(card->priv, CMD_FUNC_SHUTDOWN,
				&cmd, sizeof(cmd), lbs_cmd_copyback,
				(unsigned long) &cmd))
			pr_alert("CMD_FUNC_SHUTDOWN cmd failed\n");
	}


	lbs_deb_sdio("call remove card\n");
	lbs_stop_card(card->priv);
	lbs_remove_card(card->priv);

	flush_workqueue(card->workqueue);
	destroy_workqueue(card->workqueue);

	while (card->packets) {
		packet = card->packets;
		card->packets = card->packets->next;
		kfree(packet);
	}

	kfree(card);
	lbs_deb_leave(LBS_DEB_SDIO);
}

static int if_sdio_suspend(struct device *dev)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	int ret;
	struct if_sdio_card *card = sdio_get_drvdata(func);

	mmc_pm_flag_t flags = sdio_get_host_pm_caps(func);

	/* If we're powered off anyway, just let the mmc layer remove the
	 * card. */
	if (!lbs_iface_active(card->priv))
		return -ENOSYS;

	dev_info(dev, "%s: suspend: PM flags = 0x%x\n",
		 sdio_func_id(func), flags);

	/* If we aren't being asked to wake on anything, we should bail out
	 * and let the SD stack power down the card.
	 */
	if (card->priv->wol_criteria == EHS_REMOVE_WAKEUP) {
		dev_info(dev, "Suspend without wake params -- powering down card\n");
		return -ENOSYS;
	}

	if (!(flags & MMC_PM_KEEP_POWER)) {
		dev_err(dev, "%s: cannot remain alive while host is suspended\n",
			sdio_func_id(func));
		return -ENOSYS;
	}

	ret = sdio_set_host_pm_flags(func, MMC_PM_KEEP_POWER);
	if (ret)
		return ret;

	ret = lbs_suspend(card->priv);
	if (ret)
		return ret;

	return sdio_set_host_pm_flags(func, MMC_PM_WAKE_SDIO_IRQ);
}

static int if_sdio_resume(struct device *dev)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct if_sdio_card *card = sdio_get_drvdata(func);
	int ret;

	dev_info(dev, "%s: resume: we're back\n", sdio_func_id(func));

	ret = lbs_resume(card->priv);

	return ret;
}

static const struct dev_pm_ops if_sdio_pm_ops = {
	.suspend	= if_sdio_suspend,
	.resume		= if_sdio_resume,
};

static struct sdio_driver if_sdio_driver = {
	.name		= "libertas_sdio",
	.id_table	= if_sdio_ids,
	.probe		= if_sdio_probe,
	.remove		= if_sdio_remove,
	.drv = {
		.pm = &if_sdio_pm_ops,
	},
};

/*******************************************************************/
/* Module functions                                                */
/*******************************************************************/

static int __init if_sdio_init_module(void)
{
	int ret = 0;

	lbs_deb_enter(LBS_DEB_SDIO);

	printk(KERN_INFO "libertas_sdio: Libertas SDIO driver\n");
	printk(KERN_INFO "libertas_sdio: Copyright Pierre Ossman\n");

	ret = sdio_register_driver(&if_sdio_driver);

	/* Clear the flag in case user removes the card. */
	user_rmmod = 0;

	lbs_deb_leave_args(LBS_DEB_SDIO, "ret %d", ret);

	return ret;
}

static void __exit if_sdio_exit_module(void)
{
	lbs_deb_enter(LBS_DEB_SDIO);

	/* Set the flag as user is removing this module. */
	user_rmmod = 1;

	cancel_work_sync(&card_reset_work);

	sdio_unregister_driver(&if_sdio_driver);

	lbs_deb_leave(LBS_DEB_SDIO);
}

module_init(if_sdio_init_module);
module_exit(if_sdio_exit_module);

MODULE_DESCRIPTION("Libertas SDIO WLAN Driver");
MODULE_AUTHOR("Pierre Ossman");
MODULE_LICENSE("GPL");
