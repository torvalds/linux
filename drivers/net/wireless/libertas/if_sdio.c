/*
 *  linux/drivers/net/wireless/libertas/if_sdio.c
 *
 *  Copyright 2007 Pierre Ossman
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

#include <linux/moduleparam.h>
#include <linux/firmware.h>
#include <linux/netdevice.h>
#include <linux/delay.h>
#include <linux/mmc/card.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>

#include "host.h"
#include "decl.h"
#include "defs.h"
#include "dev.h"
#include "if_sdio.h"

static char *libertas_helper_name = NULL;
module_param_named(helper_name, libertas_helper_name, charp, 0644);

static char *libertas_fw_name = NULL;
module_param_named(fw_name, libertas_fw_name, charp, 0644);

static const struct sdio_device_id if_sdio_ids[] = {
	{ SDIO_DEVICE(SDIO_VENDOR_ID_MARVELL, SDIO_DEVICE_ID_MARVELL_LIBERTAS) },
	{ /* end: all zeroes */						},
};

MODULE_DEVICE_TABLE(sdio, if_sdio_ids);

struct if_sdio_model {
	int model;
	const char *helper;
	const char *firmware;
};

static struct if_sdio_model if_sdio_models[] = {
	{
		/* 8385 */
		.model = 0x04,
		.helper = "sd8385_helper.bin",
		.firmware = "sd8385.bin",
	},
	{
		/* 8686 */
		.model = 0x0B,
		.helper = "sd8686_helper.bin",
		.firmware = "sd8686.bin",
	},
};

struct if_sdio_packet {
	struct if_sdio_packet	*next;
	u16			nb;
	u8			buffer[0] __attribute__((aligned(4)));
};

struct if_sdio_card {
	struct sdio_func	*func;
	wlan_private		*priv;

	int			model;
	unsigned long		ioport;

	const char		*helper;
	const char		*firmware;

	u8			buffer[65536];
	u8			int_cause;
	u32			event;

	spinlock_t		lock;
	struct if_sdio_packet	*packets;
	struct work_struct	packet_worker;
};

/********************************************************************/
/* I/O                                                              */
/********************************************************************/

static u16 if_sdio_read_scratch(struct if_sdio_card *card, int *err)
{
	int ret, reg;
	u16 scratch;

	if (card->model == 0x04)
		reg = IF_SDIO_SCRATCH_OLD;
	else
		reg = IF_SDIO_SCRATCH;

	scratch = sdio_readb(card->func, reg, &ret);
	if (!ret)
		scratch |= sdio_readb(card->func, reg + 1, &ret) << 8;

	if (err)
		*err = ret;

	if (ret)
		return 0xffff;

	return scratch;
}

static int if_sdio_handle_cmd(struct if_sdio_card *card,
		u8 *buffer, unsigned size)
{
	int ret;
	unsigned long flags;

	lbs_deb_enter(LBS_DEB_SDIO);

	spin_lock_irqsave(&card->priv->adapter->driver_lock, flags);

	if (!card->priv->adapter->cur_cmd) {
		lbs_deb_sdio("discarding spurious response\n");
		ret = 0;
		goto out;
	}

	if (size > MRVDRV_SIZE_OF_CMD_BUFFER) {
		lbs_deb_sdio("response packet too large (%d bytes)\n",
			(int)size);
		ret = -E2BIG;
		goto out;
	}

	memcpy(card->priv->adapter->cur_cmd->bufvirtualaddr, buffer, size);
	card->priv->upld_len = size;

	card->int_cause |= MRVDRV_CMD_UPLD_RDY;

	libertas_interrupt(card->priv->dev);

	ret = 0;

out:
	spin_unlock_irqrestore(&card->priv->adapter->driver_lock, flags);

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

	libertas_process_rxed_packet(card->priv, skb);

	ret = 0;

out:
	lbs_deb_leave_args(LBS_DEB_SDIO, "ret %d", ret);

	return ret;
}

static int if_sdio_handle_event(struct if_sdio_card *card,
		u8 *buffer, unsigned size)
{
	int ret;
	unsigned long flags;
	u32 event;

	lbs_deb_enter(LBS_DEB_SDIO);

	if (card->model == 0x04) {
		event = sdio_readb(card->func, IF_SDIO_EVENT, &ret);
		if (ret)
			goto out;
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
		event <<= SBI_EVENT_CAUSE_SHIFT;
	}

	spin_lock_irqsave(&card->priv->adapter->driver_lock, flags);

	card->event = event;
	card->int_cause |= MRVDRV_CARDEVENT;

	libertas_interrupt(card->priv->dev);

	spin_unlock_irqrestore(&card->priv->adapter->driver_lock, flags);

	ret = 0;

out:
	lbs_deb_leave_args(LBS_DEB_SDIO, "ret %d", ret);

	return ret;
}

static int if_sdio_card_to_host(struct if_sdio_card *card)
{
	int ret;
	u8 status;
	u16 size, type, chunk;
	unsigned long timeout;

	lbs_deb_enter(LBS_DEB_SDIO);

	size = if_sdio_read_scratch(card, &ret);
	if (ret)
		goto out;

	if (size < 4) {
		lbs_deb_sdio("invalid packet size (%d bytes) from firmware\n",
			(int)size);
		ret = -EINVAL;
		goto out;
	}

	timeout = jiffies + HZ;
	while (1) {
		status = sdio_readb(card->func, IF_SDIO_STATUS, &ret);
		if (ret)
			goto out;
		if (status & IF_SDIO_IO_RDY)
			break;
		if (time_after(jiffies, timeout)) {
			ret = -ETIMEDOUT;
			goto out;
		}
		mdelay(1);
	}

	/*
	 * The transfer must be in one transaction or the firmware
	 * goes suicidal.
	 */
	chunk = size;
	if ((chunk > card->func->cur_blksize) || (chunk > 512)) {
		chunk = (chunk + card->func->cur_blksize - 1) /
			card->func->cur_blksize * card->func->cur_blksize;
	}

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
		lbs_pr_err("problem fetching packet from firmware\n");

	lbs_deb_leave_args(LBS_DEB_SDIO, "ret %d", ret);

	return ret;
}

static void if_sdio_host_to_card_worker(struct work_struct *work)
{
	struct if_sdio_card *card;
	struct if_sdio_packet *packet;
	unsigned long timeout;
	u8 status;
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

		timeout = jiffies + HZ;
		while (1) {
			status = sdio_readb(card->func, IF_SDIO_STATUS, &ret);
			if (ret)
				goto release;
			if (status & IF_SDIO_IO_RDY)
				break;
			if (time_after(jiffies, timeout)) {
				ret = -ETIMEDOUT;
				goto release;
			}
			mdelay(1);
		}

		ret = sdio_writesb(card->func, card->ioport,
				packet->buffer, packet->nb);
		if (ret)
			goto release;
release:
		sdio_release_host(card->func);

		kfree(packet);
	}

	lbs_deb_leave(LBS_DEB_SDIO);
}

/********************************************************************/
/* Firmware                                                         */
/********************************************************************/

static int if_sdio_prog_helper(struct if_sdio_card *card)
{
	int ret;
	u8 status;
	const struct firmware *fw;
	unsigned long timeout;
	u8 *chunk_buffer;
	u32 chunk_size;
	u8 *firmware;
	size_t size;

	lbs_deb_enter(LBS_DEB_SDIO);

	ret = request_firmware(&fw, card->helper, &card->func->dev);
	if (ret) {
		lbs_pr_err("can't load helper firmware\n");
		goto out;
	}

	chunk_buffer = kzalloc(64, GFP_KERNEL);
	if (!chunk_buffer) {
		ret = -ENOMEM;
		goto release_fw;
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
			status = sdio_readb(card->func, IF_SDIO_STATUS, &ret);
			if (ret)
				goto release;
			if ((status & IF_SDIO_IO_RDY) &&
					(status & IF_SDIO_DL_RDY))
				break;
			if (time_after(jiffies, timeout)) {
				ret = -ETIMEDOUT;
				goto release;
			}
			mdelay(1);
		}

		chunk_size = min(size, (size_t)60);

		*((u32*)chunk_buffer) = cpu_to_le32(chunk_size);
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
	sdio_set_block_size(card->func, 0);
	sdio_release_host(card->func);
	kfree(chunk_buffer);
release_fw:
	release_firmware(fw);

out:
	if (ret)
		lbs_pr_err("failed to load helper firmware\n");

	lbs_deb_leave_args(LBS_DEB_SDIO, "ret %d", ret);

	return ret;
}

static int if_sdio_prog_real(struct if_sdio_card *card)
{
	int ret;
	u8 status;
	const struct firmware *fw;
	unsigned long timeout;
	u8 *chunk_buffer;
	u32 chunk_size;
	u8 *firmware;
	size_t size, req_size;

	lbs_deb_enter(LBS_DEB_SDIO);

	ret = request_firmware(&fw, card->firmware, &card->func->dev);
	if (ret) {
		lbs_pr_err("can't load firmware\n");
		goto out;
	}

	chunk_buffer = kzalloc(512, GFP_KERNEL);
	if (!chunk_buffer) {
		ret = -ENOMEM;
		goto release_fw;
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
			status = sdio_readb(card->func, IF_SDIO_STATUS, &ret);
			if (ret)
				goto release;
			if ((status & IF_SDIO_IO_RDY) &&
					(status & IF_SDIO_DL_RDY))
				break;
			if (time_after(jiffies, timeout)) {
				ret = -ETIMEDOUT;
				goto release;
			}
			mdelay(1);
		}

		req_size = sdio_readb(card->func, IF_SDIO_RD_BASE, &ret);
		if (ret)
			goto release;

		req_size |= sdio_readb(card->func, IF_SDIO_RD_BASE + 1, &ret) << 8;
		if (ret)
			goto release;
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
				chunk_buffer, (chunk_size + 31) / 32 * 32);
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
	sdio_set_block_size(card->func, 0);
	sdio_release_host(card->func);
	kfree(chunk_buffer);
release_fw:
	release_firmware(fw);

out:
	if (ret)
		lbs_pr_err("failed to load firmware\n");

	lbs_deb_leave_args(LBS_DEB_SDIO, "ret %d", ret);

	return ret;
}

static int if_sdio_prog_firmware(struct if_sdio_card *card)
{
	int ret;
	u16 scratch;

	lbs_deb_enter(LBS_DEB_SDIO);

	sdio_claim_host(card->func);
	scratch = if_sdio_read_scratch(card, &ret);
	sdio_release_host(card->func);

	if (ret)
		goto out;

	if (scratch == IF_SDIO_FIRMWARE_OK) {
		lbs_deb_sdio("firmware already loaded\n");
		goto success;
	}

	ret = if_sdio_prog_helper(card);
	if (ret)
		goto out;

	ret = if_sdio_prog_real(card);
	if (ret)
		goto out;

success:
	ret = 0;

out:
	lbs_deb_leave_args(LBS_DEB_SDIO, "ret %d", ret);

	return ret;
}

/*******************************************************************/
/* Libertas callbacks                                              */
/*******************************************************************/

static int if_sdio_host_to_card(wlan_private *priv, u8 type, u8 *buf, u16 nb)
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
	 * goes suicidal.
	 */
	size = nb + 4;
	if ((size > card->func->cur_blksize) || (size > 512)) {
		size = (size + card->func->cur_blksize - 1) /
			card->func->cur_blksize * card->func->cur_blksize;
	}

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

	schedule_work(&card->packet_worker);

	ret = 0;

out:
	lbs_deb_leave_args(LBS_DEB_SDIO, "ret %d", ret);

	return ret;
}

static int if_sdio_get_int_status(wlan_private *priv, u8 *ireg)
{
	struct if_sdio_card *card;

	lbs_deb_enter(LBS_DEB_SDIO);

	card = priv->card;

	*ireg = card->int_cause;
	card->int_cause = 0;

	lbs_deb_leave(LBS_DEB_SDIO);

	return 0;
}

static int if_sdio_read_event_cause(wlan_private *priv)
{
	struct if_sdio_card *card;

	lbs_deb_enter(LBS_DEB_SDIO);

	card = priv->card;

	priv->adapter->eventcause = card->event;

	lbs_deb_leave(LBS_DEB_SDIO);

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
	if (ret)
		goto out;

	lbs_deb_sdio("interrupt: 0x%X\n", (unsigned)cause);

	sdio_writeb(card->func, ~cause, IF_SDIO_H_INT_STATUS, &ret);
	if (ret)
		goto out;

	/*
	 * Ignore the define name, this really means the card has
	 * successfully received the command.
	 */
	if (cause & IF_SDIO_H_INT_DNLD) {
		if ((card->priv->dnld_sent == DNLD_DATA_SENT) &&
			(card->priv->adapter->connect_status == LIBERTAS_CONNECTED))
			netif_wake_queue(card->priv->dev);
		card->priv->dnld_sent = DNLD_RES_RECEIVED;
	}

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
	wlan_private *priv;
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
                       model = 4;
                       break;
               }
	}

	if (i == func->card->num_info) {
		lbs_pr_err("unable to identify card model\n");
		return -ENODEV;
	}

	card = kzalloc(sizeof(struct if_sdio_card), GFP_KERNEL);
	if (!card)
		return -ENOMEM;

	card->func = func;
	card->model = model;
	spin_lock_init(&card->lock);
	INIT_WORK(&card->packet_worker, if_sdio_host_to_card_worker);

	for (i = 0;i < ARRAY_SIZE(if_sdio_models);i++) {
		if (card->model == if_sdio_models[i].model)
			break;
	}

	if (i == ARRAY_SIZE(if_sdio_models)) {
		lbs_pr_err("unkown card model 0x%x\n", card->model);
		ret = -ENODEV;
		goto free;
	}

	card->helper = if_sdio_models[i].helper;
	card->firmware = if_sdio_models[i].firmware;

	if (libertas_helper_name) {
		lbs_deb_sdio("overriding helper firmware: %s\n",
			libertas_helper_name);
		card->helper = libertas_helper_name;
	}

	if (libertas_fw_name) {
		lbs_deb_sdio("overriding firmware: %s\n", libertas_fw_name);
		card->firmware = libertas_fw_name;
	}

	sdio_claim_host(func);

	ret = sdio_enable_func(func);
	if (ret)
		goto release;

	ret = sdio_claim_irq(func, if_sdio_interrupt);
	if (ret)
		goto disable;

	card->ioport = sdio_readb(func, IF_SDIO_IOPORT, &ret);
	if (ret)
		goto release_int;

	card->ioport |= sdio_readb(func, IF_SDIO_IOPORT + 1, &ret) << 8;
	if (ret)
		goto release_int;

	card->ioport |= sdio_readb(func, IF_SDIO_IOPORT + 2, &ret) << 16;
	if (ret)
		goto release_int;

	sdio_release_host(func);

	sdio_set_drvdata(func, card);

	lbs_deb_sdio("class = 0x%X, vendor = 0x%X, "
			"device = 0x%X, model = 0x%X, ioport = 0x%X\n",
			func->class, func->vendor, func->device,
			model, (unsigned)card->ioport);

	ret = if_sdio_prog_firmware(card);
	if (ret)
		goto reclaim;

	priv = libertas_add_card(card, &func->dev);
	if (!priv) {
		ret = -ENOMEM;
		goto reclaim;
	}

	card->priv = priv;

	priv->card = card;
	priv->hw_host_to_card = if_sdio_host_to_card;
	priv->hw_get_int_status = if_sdio_get_int_status;
	priv->hw_read_event_cause = if_sdio_read_event_cause;

	priv->adapter->fw_ready = 1;

	/*
	 * Enable interrupts now that everything is set up
	 */
	sdio_claim_host(func);
	sdio_writeb(func, 0x0f, IF_SDIO_H_INT_MASK, &ret);
	sdio_release_host(func);
	if (ret)
		goto reclaim;

	ret = libertas_start_card(priv);
	if (ret)
		goto err_activate_card;

out:
	lbs_deb_leave_args(LBS_DEB_SDIO, "ret %d", ret);

	return ret;

err_activate_card:
	flush_scheduled_work();
	free_netdev(priv->dev);
	kfree(priv->adapter);
reclaim:
	sdio_claim_host(func);
release_int:
	sdio_release_irq(func);
disable:
	sdio_disable_func(func);
release:
	sdio_release_host(func);
free:
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

	card->priv->adapter->surpriseremoved = 1;

	lbs_deb_sdio("call remove card\n");
	libertas_stop_card(card->priv);
	libertas_remove_card(card->priv);

	flush_scheduled_work();

	sdio_claim_host(func);
	sdio_release_irq(func);
	sdio_disable_func(func);
	sdio_release_host(func);

	while (card->packets) {
		packet = card->packets;
		card->packets = card->packets->next;
		kfree(packet);
	}

	kfree(card);

	lbs_deb_leave(LBS_DEB_SDIO);
}

static struct sdio_driver if_sdio_driver = {
	.name		= "libertas_sdio",
	.id_table	= if_sdio_ids,
	.probe		= if_sdio_probe,
	.remove		= if_sdio_remove,
};

/*******************************************************************/
/* Module functions                                                */
/*******************************************************************/

static int if_sdio_init_module(void)
{
	int ret = 0;

	lbs_deb_enter(LBS_DEB_SDIO);

	printk(KERN_INFO "libertas_sdio: Libertas SDIO driver\n");
	printk(KERN_INFO "libertas_sdio: Copyright Pierre Ossman\n");

	ret = sdio_register_driver(&if_sdio_driver);

	lbs_deb_leave_args(LBS_DEB_SDIO, "ret %d", ret);

	return ret;
}

static void if_sdio_exit_module(void)
{
	lbs_deb_enter(LBS_DEB_SDIO);

	sdio_unregister_driver(&if_sdio_driver);

	lbs_deb_leave(LBS_DEB_SDIO);
}

module_init(if_sdio_init_module);
module_exit(if_sdio_exit_module);

MODULE_DESCRIPTION("Libertas SDIO WLAN Driver");
MODULE_AUTHOR("Pierre Ossman");
MODULE_LICENSE("GPL");
