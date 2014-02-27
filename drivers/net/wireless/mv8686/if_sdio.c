/*
 *  linux/drivers/net/wireless/mv8686/if_sdio.c
 *
 *  Copyright 2007-2008 Pierre Ossman
 *
 * Inspired by if_cs.c, Copyright 2007 Holger Schurig
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

#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/firmware.h>
#include <linux/netdevice.h>
#include <linux/delay.h>
#include <linux/mmc/card.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/host.h>

#include "host.h"
#include "decl.h"
#include "defs.h"
#include "dev.h"
#include "if_sdio.h"
#include "wifi_power.h"

#if (SDIO_SPEED_EVUL == 1)
#include <linux/time.h>
#endif

struct sdio_func *wifi_sdio_func = NULL;
EXPORT_SYMBOL(wifi_sdio_func);

/*
 * Externs from MMC core.
 */
extern struct mmc_host *wifi_mmc_host;

static const struct sdio_device_id if_sdio_ids[] = 
{
	{ SDIO_DEVICE(SDIO_VENDOR_ID_MARVELL, SDIO_DEVICE_ID_MARVELL_LIBERTAS) },
	{ /* end: all zeroes */						},
};
MODULE_DEVICE_TABLE(sdio, if_sdio_ids);

struct if_sdio_model 
{
	int model;
	const char *helper;
	const char *firmware;
};

static struct if_sdio_model if_sdio_models[] = 
{
	{ /* 8686 */
		.model = 0x0B,
		.helper = "sd8686_helper.bin",
		.firmware = "sd8686.bin",
	},
};

struct if_sdio_packet
{
	struct if_sdio_packet   *next;
	u16                     nb;
	u8                      buffer[0] __attribute__((aligned(4)));
};

struct if_sdio_card
{
	struct sdio_func	    *func;
	struct lbs_private	    *priv;

	int                     model;
	unsigned long		    ioport;

	const char              *helper;
	const char              *firmware;

	u8                      buffer[65536];  /* buffer for rx packet/resp */

	spinlock_t              lock;
	struct if_sdio_packet   *packets;

	struct workqueue_struct	*workqueue;
	struct work_struct      packet_worker;
};

/********************************************************************/
/* I/O                                                              */
/********************************************************************/

int lbs_reset_deep_sleep_wakeup(struct lbs_private * priv)
{
	int ret = 0;
	struct if_sdio_card *card = (struct if_sdio_card *)priv->card;
	
	sdio_claim_host(card->func);
	sdio_writeb(card->func, IF_SDIO_HOST_RESET, IF_SDIO_CONFIGURE, &ret);
	sdio_release_host(card->func);

	return ret;
}

int lbs_exit_deep_sleep(struct lbs_private * priv)
{
	int ret;
	struct if_sdio_card *card = (struct if_sdio_card *)priv->card;
	
	sdio_claim_host(card->func);
	sdio_writeb(card->func, IF_SDIO_HOST_UP, IF_SDIO_CONFIGURE, &ret);
	sdio_release_host(card->func);
	
	return ret;
}

static u16 if_sdio_read_scratch(struct if_sdio_card *card, int *err)
{
	int ret = 0;
	u16 scratch;
	
	scratch = sdio_readb(card->func, IF_SDIO_SCRATCH, &ret);
	if (!ret)
		scratch |= sdio_readb(card->func, IF_SDIO_SCRATCH + 1, &ret) << 8;

	if (err)
		*err = ret;

	if (ret)
		return 0xffff;

	return scratch;
}

static int if_sdio_handle_cmd(struct if_sdio_card *card,
		u8 *buffer, unsigned size)
{
	struct lbs_private *priv = card->priv;
	int ret = 0;
	unsigned long flags;
	u8 i;

	lbs_deb_enter(LBS_DEB_SDIO);

	if (size > LBS_CMD_BUFFER_SIZE) 
	{
		lbs_deb_sdio("Cmd resp pkt is too large (%d bytes)\n",	(int)size);
		ret = -E2BIG;
		goto out;
	}

	spin_lock_irqsave(&priv->driver_lock, flags);

	i = (priv->resp_idx == 0) ? 1 : 0;
	//BUG_ON(priv->resp_len[i]);
	priv->resp_len[i] = size;
	memcpy(priv->resp_buf[i], buffer, size);
	lbs_notify_command_response(priv, i);

	spin_unlock_irqrestore(&card->priv->driver_lock, flags);

out:
	lbs_deb_leave_args(LBS_DEB_SDIO, "ret %d", ret);
	
	return ret;
}

static int if_sdio_handle_data(struct if_sdio_card *card,
		u8 *buffer, u32 size)
{
	int ret = 0;
	struct sk_buff *skb;
	char *data;

	lbs_deb_enter(LBS_DEB_SDIO);

	if (size > MRVDRV_ETH_RX_PACKET_BUFFER_SIZE) 
	{
		lbs_deb_sdio("Data resp pkt is too large (%d bytes)\n", (int)size);
		ret = -E2BIG;
		goto out;
	}

	skb = dev_alloc_skb(MRVDRV_ETH_RX_PACKET_BUFFER_SIZE + NET_IP_ALIGN);
	if (!skb) 
	{
		ret = -ENOMEM;
		goto out;
	}

	skb_reserve(skb, NET_IP_ALIGN);

	data = skb_put(skb, size);

	memcpy(data, buffer, size);

	lbs_process_rxed_packet(card->priv, skb);

out:
	lbs_deb_leave_args(LBS_DEB_SDIO, "ret %d", ret);

	return ret;
}

static int if_sdio_handle_event(struct if_sdio_card *card,
		u8 *buffer, u32 size)
{
	int ret = 0;
	u32 event;

	lbs_deb_enter(LBS_DEB_SDIO);

	if (size < 4) 
	{
			lbs_deb_sdio("event packet too small (%d bytes)\n",
				(int)size);
			ret = -EINVAL;
			goto out;
	}
	event = buffer[3] << 24;
	event |= buffer[2] << 16;
	event |= buffer[1] << 8;
	event |= buffer[0];

	lbs_queue_event(card->priv, event & 0xFF);

out:
	lbs_deb_leave_args(LBS_DEB_SDIO, "ret %d", ret);

	return ret;
}

static int if_sdio_card_to_host(struct if_sdio_card *card)
{
	int ret = 0;
	u8 status;
	u16 size, type, chunk;
	unsigned long timeout;

	lbs_deb_enter(LBS_DEB_SDIO);

	size = if_sdio_read_scratch(card, &ret);
	if (ret)
		goto out;

	if (size < 4) 
	{
		lbs_deb_sdio("Invalid pkt size (%d bytes) from FW.\n",	(int)size);
		ret = -EINVAL;
		goto out;
	}

	timeout = jiffies + HZ;
	while (1) 
	{
		status = sdio_readb(card->func, IF_SDIO_STATUS, &ret);
		if (ret)
		{
			printk("Card to host: read sdio_status failed.\n");
			goto out;
		}

		if (status & IF_SDIO_IO_RDY)
			break;

		if (time_after(jiffies, timeout)) 
		{
			ret = -ETIMEDOUT;
			goto out;
		}
		//mdelay(1);
	}

	/*
	 * The transfer must be in one transaction or the firmware
	 * goes suicidal. There's no way to guarantee that for all
	 * controllers, but we can at least try.
	 */
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,25)
	chunk = size;
#else
	chunk = sdio_align_size(card->func, size);
#endif
	if ((chunk > card->func->cur_blksize) || (chunk > 512)) 
	{
		chunk = (chunk + card->func->cur_blksize - 1) /
			card->func->cur_blksize * card->func->cur_blksize;
	}

	ret = sdio_readsb(card->func, card->buffer, card->ioport, chunk);
	if (ret)
		goto out;

	chunk = card->buffer[0] | (card->buffer[1] << 8);
	type = card->buffer[2] | (card->buffer[3] << 8);

	lbs_deb_sdio("packet of type %d and size %d bytes\n",	(int)type, (int)chunk);

	if (chunk > size) {
		lbs_deb_sdio("packet fragment (%d > %d)\n",	(int)chunk, (int)size);
		ret = -EINVAL;
		goto out;
	}

	if (chunk < size) {
		lbs_deb_sdio("packet fragment (%d < %d)\n",
			(int)chunk, (int)size);
	}

	switch (type) 
	{
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
		lbs_deb_sdio("Invalid type (%d) from FW.\n",	(int)type);
		ret = -EINVAL;
		goto out;
	}

out:
	if (ret)
		printk("Problem fetching packet from FW: ret=%d\n", ret);

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

	while (1) 
	{
		spin_lock_irqsave(&card->lock, flags);
		packet = card->packets;
		if (packet)
			card->packets = packet->next;
		spin_unlock_irqrestore(&card->lock, flags);

		if (!packet)
			break;

		sdio_claim_host(card->func);

		timeout = jiffies + HZ;
		while (1) 
		{
			status = sdio_readb(card->func, IF_SDIO_STATUS, &ret);
			if (ret)
			{
				printk("Host to card worker: read sdio_status failed.\n");
				goto release;
			}

			if (status & IF_SDIO_IO_RDY)
				break;
			if (time_after(jiffies, timeout)) 
			{
				ret = -ETIMEDOUT;
				goto release;
			}
			//mdelay(1);
		}

		ret = sdio_writesb(card->func, card->ioport, packet->buffer, packet->nb);
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

inline int if_sdio_wait_dnld_ready(struct if_sdio_card *card)
{
	int ret;
	unsigned long timeout;
	volatile u32 status;
	
	timeout = jiffies + HZ;
	
	while (1) 
	{
		status = sdio_readb(card->func, IF_SDIO_STATUS, &ret);
		if (ret)
			return 1;

		if ((status & IF_SDIO_IO_RDY) && (status & IF_SDIO_DL_RDY))
				return 0;
		
		if (time_after(jiffies, timeout))
				return 2;

		//mdelay(1);
	}
	
	return -1;
}

/*
 * Download WIFI helper to module via SDIO.
 */
static int if_sdio_prog_helper(struct if_sdio_card *card)
{
	int ret;
	const struct firmware *fw;
	unsigned long timeout;
	u8 *chunk_buffer;
	u32 chunk_size, tx_len;
	const u8 *firmware;
	size_t size;

	lbs_deb_enter(LBS_DEB_SDIO);

	ret = request_firmware(&fw, card->helper, &card->func->dev);
	if (ret) 
	{
		printk("Fail to request helper firmware.\n");
		goto out;
	}
	printk("%s: request fw ok.\n", __func__);

	tx_len = SD_BLOCK_SIZE * FW_TRANSFER_NBLOCK;  //SDIO frame length.
	
	chunk_buffer = kzalloc(tx_len, GFP_KERNEL);
	if (!chunk_buffer) 
	{
		printk("kzalloc for chunk buffer failed.\n");
		ret = -ENOMEM;
		goto release_fw;
	}

	sdio_claim_host(card->func);

	/* Here, we need to make sure the block-size is 256. */
	ret = sdio_set_block_size(card->func, 0);
	if (ret)
	{
		printk("Set block size to 256 for download firmware failed.\n");
		goto release;
	}

	firmware = fw->data;
	size = fw->size;

	while (size) 
	{
		ret = if_sdio_wait_dnld_ready(card);
		if (ret != 0)
		{
			printk("Waiting for DNLD READY failed: %d\n", ret);
			goto release;
		}

		chunk_size = min(size, (size_t)(tx_len - SDIO_HEADER_LEN));

		*((__le32*)chunk_buffer) = cpu_to_le32(chunk_size);
		memcpy(chunk_buffer + 4, firmware, chunk_size);

		ret = sdio_writesb(card->func, card->ioport, chunk_buffer, tx_len);
		if (ret)
			goto release;

		firmware += chunk_size;
		size -= chunk_size;
	}

	/* an empty block marks the end of the transfer */
	memset(chunk_buffer, 0, 4);
	ret = sdio_writesb(card->func, card->ioport, chunk_buffer, tx_len);
	if (ret)
		goto release;

	/* wait for the helper to boot by looking at the size register */
	timeout = jiffies + HZ;
	while (1) 
	{
		u16 req_size;

		req_size = sdio_readb(card->func, IF_SDIO_RD_BASE, &ret);
		if (ret)
			goto release;

		req_size |= sdio_readb(card->func, IF_SDIO_RD_BASE + 1, &ret) << 8;
		if (ret)
			goto release;

		if (req_size != 0)
			break;

		if (time_after(jiffies, timeout)) 
		{
			ret = -ETIMEDOUT;
			goto release;
		}
		//msleep(10);
	}

	ret = 0;

release:
	sdio_release_host(card->func);
	kfree(chunk_buffer);

release_fw:
	release_firmware(fw);

out:
	if (ret)
		printk("Failed to download helper firmware.\n");

	lbs_deb_leave_args(LBS_DEB_SDIO, "ret %d", ret);

	return ret;
}

static int if_sdio_prog_real(struct if_sdio_card *card)
{
	int ret;
	const struct firmware *fw;
	unsigned long timeout;
	u8 *chunk_buffer;
	u32 chunk_size;
	const u8 *firmware;
	size_t size, req_size;
#if (SDIO_SPEED_EVUL == 1)
	struct timeval sdio_start_tv, sdio_stop_tv;
	unsigned long download_time, download_second;
#endif

	lbs_deb_enter(LBS_DEB_SDIO);

	ret = request_firmware(&fw, card->firmware, &card->func->dev);
	if (ret) 
	{
		printk("Fail to request real firmware.\n");
		goto out;
	}

	chunk_buffer = kzalloc(512, GFP_KERNEL);
	if (!chunk_buffer) {
		ret = -ENOMEM;
		goto release_fw;
	}

	sdio_claim_host(card->func);

	/* Here, we need to make sure the block-size is 256. */
	ret = sdio_set_block_size(card->func, 0);
	if (ret)
	{
		printk("Set block size to 256 for download FW failed.\n");
		goto release;
	}

	firmware = fw->data;
	size = fw->size;

#if (SDIO_SPEED_EVUL == 1)
	do_gettimeofday(&sdio_start_tv);
#endif

	while (size) 
	{
		ret = if_sdio_wait_dnld_ready(card);
		if (ret != 0)
		{
			printk("Waiting for DNLD READY failed: %d\n", ret);
			goto release;
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

		while (req_size)
		{
			chunk_size = min(req_size, (size_t)512);

			memcpy(chunk_buffer, firmware, chunk_size);
/*
			lbs_deb_sdio("sending %d bytes (%d bytes) chunk\n",
				chunk_size, (chunk_size + 31) / 32 * 32);
*/
			ret = sdio_writesb(card->func, card->ioport,
				chunk_buffer, roundup(chunk_size, SD_BLOCK_SIZE));
			if (ret)
				goto release;

			firmware += chunk_size;
			size -= chunk_size;
			req_size -= chunk_size;
		}
	}
#if (SDIO_SPEED_EVUL == 1)
		do_gettimeofday(&sdio_stop_tv);

		if (sdio_stop_tv.tv_usec < sdio_start_tv.tv_usec)
		{
			sdio_stop_tv.tv_sec -= 1;
		}
		download_second = sdio_stop_tv.tv_sec - sdio_start_tv.tv_sec;

		if (sdio_stop_tv.tv_usec < sdio_start_tv.tv_usec)
		{
			download_time = 1*1000*1000 - sdio_start_tv.tv_usec;
			download_time += sdio_stop_tv.tv_usec;
		}
		else
		{
			download_time = sdio_stop_tv.tv_usec - sdio_start_tv.tv_usec;

		}
		download_time /= 1000; //ms
		printk("Data size = %d bytes, Time = %2lu.%03lu seconds, Rate = %lu kbps\n", 
			fw->size, download_second, download_time,
			(fw->size * 8 * 1000) / (download_second * 1000 + download_time) / 1000); 
#endif

	ret = 0;

	//lbs_deb_sdio("waiting for firmware to boot...\n");
	//printk("waiting for firmware to boot...\n");

	/* wait for the firmware to boot */
	timeout = jiffies + HZ;
	while (1) 
	{
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
		//msleep(10);
	}

	ret = 0;

release:
	sdio_release_host(card->func);
	kfree(chunk_buffer);
release_fw:
	release_firmware(fw);

out:
	if (ret)
		printk("Failed to download firmware.\n");
	//else
		//printk("Success to download firmware.\n");

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

	if (scratch == IF_SDIO_FIRMWARE_OK) 
	{
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
/* HIF callbacks                                              */
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

	if (nb > (65536 - sizeof(struct if_sdio_packet) - 4)) 
	{
		ret = -EINVAL;
		goto out;
	}

	/*
	 * The transfer must be in one transaction or the firmware
	 * goes suicidal. There's no way to guarantee that for all
	 * controllers, but we can at least try.
	 */
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,25)
	size = nb + 4;
#else
	size = sdio_align_size(card->func, nb + 4);
#endif
	if ((size > card->func->cur_blksize) || (size > 512)) 
	{
		size = (size + card->func->cur_blksize - 1) /
			card->func->cur_blksize * card->func->cur_blksize;
	}

	packet = kzalloc(sizeof(struct if_sdio_packet) + size, GFP_ATOMIC);
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

	switch (type) 
	{
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

	printk("%s: enter.\n", __func__);

	for (i = 0;i < func->card->num_info;i++) 
	{
		if (sscanf(func->card->info[i],
				"802.11 SDIO ID: %x", &model) == 1)
			break;
		if (sscanf(func->card->info[i],
				"ID: %x", &model) == 1)
			break;
		if (!strcmp(func->card->info[i], "IBIS Wireless SDIO Card")) 
		{
			model = 4;
			break;
		}
	}

	if (i == func->card->num_info) 
	{
		lbs_pr_err("unable to identify card model\n");
		return -ENODEV;
	}

	card = kzalloc(sizeof(struct if_sdio_card), GFP_KERNEL);
	if (!card)
		return -ENOMEM;

	card->func = func;
	card->model = model;
	spin_lock_init(&card->lock);
	card->workqueue = create_workqueue("mv8686_sdio");
	INIT_WORK(&card->packet_worker, if_sdio_host_to_card_worker);

	for (i = 0;i < ARRAY_SIZE(if_sdio_models);i++) 
	{
		if (card->model == if_sdio_models[i].model)
			break;
	}

	if (i == ARRAY_SIZE(if_sdio_models)) 
	{
		lbs_pr_err("unkown card model 0x%x\n", card->model);
		ret = -ENODEV;
		goto free;
	}

	card->helper = if_sdio_models[i].helper;
	card->firmware = if_sdio_models[i].firmware;

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

	priv = lbs_add_card(card, &func->dev);
	if (!priv) {
		ret = -ENOMEM;
		goto reclaim;
	}

	card->priv = priv;

	priv->card = card;
	priv->hw_host_to_card = if_sdio_host_to_card;

	priv->fw_ready = 1;

	/*
	 * Enable interrupts now that everything is set up
	 */
	sdio_claim_host(func);
	sdio_writeb(func, 0x0f, IF_SDIO_H_INT_MASK, &ret);
	sdio_release_host(func);
	if (ret)
		goto reclaim;

	ret = lbs_start_card(priv);
	if (ret)
		goto err_activate_card;

	if (priv->fwcapinfo & FW_CAPINFO_PS)
		priv->ps_supported = 1;
	
	wifi_sdio_func = func;
	
out:
	lbs_deb_leave_args(LBS_DEB_SDIO, "ret %d", ret);

	return ret;

err_activate_card:
	flush_workqueue(card->workqueue);
	lbs_remove_card(priv);
reclaim:
	sdio_claim_host(func);
release_int:
	sdio_release_irq(func);
disable:
	sdio_disable_func(func);
release:
	sdio_release_host(func);
free:
	destroy_workqueue(card->workqueue);

	while (card->packets) 
	{
		packet = card->packets;
		card->packets = card->packets->next;
		kfree(packet);
	}

	kfree(card);

	goto out;
}

int wifi_func_removed;

static void if_sdio_remove(struct sdio_func *func)
{
	struct if_sdio_card *card;
	struct if_sdio_packet *packet;

	lbs_deb_enter(LBS_DEB_SDIO);

	card = sdio_get_drvdata(func);
	card->priv->surpriseremoved = 1;

    wifi_sdio_func = NULL;
    
	/*
	 * Remove netdev, break wext and socket path.
	 */
	lbs_stop_card(card->priv);
    
	/*
	 * Destroy the driver completely.
	 */
	lbs_remove_card(card->priv);

	flush_scheduled_work();
	if (card->workqueue != NULL)
		destroy_workqueue(card->workqueue);
	
	sdio_claim_host(func);
	sdio_release_irq(func);
	sdio_disable_func(func);
	sdio_release_host(func);

	while (card->packets)
	{
		packet = card->packets;
		card->packets = card->packets->next;
		kfree(packet);
	}
	kfree(card);

	wifi_func_removed = 1;
	
	lbs_deb_leave(LBS_DEB_SDIO);
}

static struct sdio_driver if_sdio_driver = 
{
	.name		= "mv8686_sdio",
	.id_table	= if_sdio_ids,
	.probe		= if_sdio_probe,
	.remove		= if_sdio_remove,
};

/*******************************************************************/
/* Module functions                                                */
/*******************************************************************/

/*
 * Whether a GPIO is used to control the power of WiFi.
 */
int wifi_no_power_gpio = 0;  /* 0-No 1-Yes */

extern int wifi_ps_status;

#include "wifi_version.h"

extern int mv8686_main_init(void);
extern void mv8686_main_exit(void);

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,25)
extern int rk29sdk_wifi_set_carddetect(int val);
#endif

int rockchip_wifi_init_module(void)
{
	int ret = 0, timeout;

	mv8686_main_init();

	wifi_func_removed = 0;
	wifi_sdio_func = NULL;
	wifi_no_power_gpio = 0;
	
	/* Make sure we are in awake. -- Yongle Lai */
	wifi_ps_status = WIFI_PS_AWAKE;

	printk("MV8686 WiFi driver (Powered by Rockchip,Ver %s) init.\n", MV8686_DRV_VERSION);

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,25)

	if (wifi_mmc_host == NULL)
	{
		printk("SDIO host (mmc1) for WiFi is inactive.\n");
		return -ENODEV;
	}
#endif

	ret = sdio_register_driver(&if_sdio_driver);
	if (ret != 0)
	{
		printk("Register SDIO driver for WIFI failed.\n");
		return -ENODEV;
	}

#ifdef WIFI_GPIO_POWER_CONTROL

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,25)

	if (wifi_mmc_host->bus_ops != NULL) /* mmc/card is attached already. */
	{
		printk("SDIO maybe be attached already.\n");
		wifi_no_power_gpio = 1;
		//return 0;
		goto wait_wifi;
	}
#endif

	wifi_turn_on_card(WIFI_CHIP_MV8686);

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,25)
	mmc_detect_change(wifi_mmc_host, 1);
#else
	rk29sdk_wifi_set_carddetect(1);
#endif

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,25)
wait_wifi:
#endif
	//printk("Waiting for SDIO card to be attached.\n");
	for (timeout = 100; timeout >=0; timeout--)
	{
		if (wifi_sdio_func != NULL)
			break;
		msleep(100);
	}
	if (timeout <= 0)
	{
		printk("No WiFi function card has been attached (10s).\n");
		
		sdio_unregister_driver(&if_sdio_driver);
			
		wifi_turn_off_card();
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,25)
		mmc_detect_change(wifi_mmc_host, 1);
#else
		rk29sdk_wifi_set_carddetect(0);
#endif
		return -ENODEV;
	}
	//printk("%s: timeout = %d (interval = 100 ms)\n", __func__, timeout);

#endif

	/*
	 * Make sure system is awake when we're start up.
	 */
#if (NEW_MV8686_PS == 1)
	wifi_power_save_init();
#endif


	return 0;
}

void rockchip_wifi_exit_module(void)
{
	int timeout;
	
	lbs_deb_enter(LBS_DEB_SDIO);

#if (NEW_MV8686_PS == 1)
	wifi_power_save_stop();
#endif

#if (ANDROID_POWER_SAVE == 1)
	wifi_ps_status = WIFI_PS_AWAKE;
#endif

	sdio_unregister_driver(&if_sdio_driver);
	
#ifdef WIFI_GPIO_POWER_CONTROL

    //if (wifi_no_power_gpio == 1)
	//	return;
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,25)		
	if (wifi_mmc_host == NULL)
	{
		printk("No SDIO host is present.\n");
		goto out;
	}
#endif
	wifi_turn_off_card();
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,25)
	mmc_detect_change(wifi_mmc_host, 2);
#else
	rk29sdk_wifi_set_carddetect(0);
#endif

	//printk("Waiting for sdio card to be released ... \n");
	for (timeout = 40; timeout >= 0; timeout--)
	{
		msleep(100);
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,25)
		if (wifi_mmc_host->bus_ops == NULL)
#else
		if (wifi_func_removed == 1)
#endif
			break;
	}
	if (timeout < 0)
		printk("Fail to release SDIO card.\n");
	//else
		//printk("Success to release SDIO card.\n");
	printk("%s: timeout = %d (interval = 100 ms)\n", __func__, timeout);
#endif

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,25)
out:
#endif

#if (NEW_MV8686_PS == 1)
	wifi_power_save_exit();
#endif

	mv8686_main_exit();

	lbs_deb_leave(LBS_DEB_SDIO);
}

#if 0

extern int rk29sdk_wifi_set_carddetect(int val);

int rockchip_wifi_init_module(void)
{
	int ret, timeout;
	
	printk("MV8686 WiFi driver (Powered by Rockchip,Ver %s) init.\n", MV8686_DRV_VERSION);

	wifi_func_removed = 0;
	mv8686_main_init();
	wifi_sdio_func = NULL;
	wifi_no_power_gpio = 0;
	
	/* Make sure we are in awake. -- Yongle Lai */
	wifi_ps_status = WIFI_PS_AWAKE;
	
	ret = sdio_register_driver(&if_sdio_driver);
	if (ret != 0)
	{
		printk("Register SDIO driver for WIFI failed.\n");
		return -ENODEV;
	}

	wifi_turn_on_card(WIFI_CHIP_MV8686);
	
	rk29sdk_wifi_set_carddetect(1);

	for (timeout = 100; timeout >=0; timeout--)
	{
		if (wifi_sdio_func != NULL)
			break;
		msleep(100);
	}
	if (timeout <= 0)
	{
		printk("No WiFi function card has been attached (10s).\n");
		
		sdio_unregister_driver(&if_sdio_driver);
		
		wifi_turn_off_card();
		rk29sdk_wifi_set_carddetect(0);

		wifi_func_removed = 1;
		
		return -ENODEV;
	}
	
	return 0;
}

void rockchip_wifi_exit_module(void)
{
	int timeout;
	
	sdio_unregister_driver(&if_sdio_driver);

	wifi_turn_off_card();

	rk29sdk_wifi_set_carddetect(0);

	for (timeout = 40; timeout >= 0; timeout--)
	{
		msleep(100);
		if (wifi_func_removed == 1)
			break;
	}
	if (timeout < 0)
		printk("Fail to release SDIO card.\n");

	mv8686_main_exit();
	
	printk("%s: timeout = %d (interval = 100 ms)\n", __func__, timeout);
}
#endif

int mv88w8686_if_sdio_init_module(void)
{
	return rockchip_wifi_init_module();
}

void mv88w8686_if_sdio_exit_module(void)
{
	rockchip_wifi_exit_module();
}

#ifndef MODULE
EXPORT_SYMBOL(rockchip_wifi_init_module);
EXPORT_SYMBOL(rockchip_wifi_exit_module);
EXPORT_SYMBOL(mv88w8686_if_sdio_init_module);
EXPORT_SYMBOL(mv88w8686_if_sdio_exit_module);
#endif

#ifdef MODULE
module_init(rockchip_wifi_init_module);
module_exit(rockchip_wifi_exit_module);
#endif

MODULE_DESCRIPTION("MV8686 SDIO WLAN Driver");
MODULE_AUTHOR("Yongle Lai");
MODULE_LICENSE("GPL");

