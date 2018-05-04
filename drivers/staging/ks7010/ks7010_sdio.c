// SPDX-License-Identifier: GPL-2.0
/*
 *   Driver for KeyStream, KS7010 based SDIO cards.
 *
 *   Copyright (C) 2006-2008 KeyStream Corp.
 *   Copyright (C) 2009 Renesas Technology Corp.
 *   Copyright (C) 2016 Sang Engineering, Wolfram Sang
 */

#include <linux/atomic.h>
#include <linux/firmware.h>
#include <linux/jiffies.h>
#include <linux/mmc/card.h>
#include <linux/mmc/sdio_func.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include "ks_wlan.h"
#include "ks_hostif.h"

#define ROM_FILE "ks7010sd.rom"

/*  SDIO KeyStream vendor and device */
#define SDIO_VENDOR_ID_KS_CODE_A	0x005b
#define SDIO_VENDOR_ID_KS_CODE_B	0x0023

/* Older sources suggest earlier versions were named 7910 or 79xx */
#define SDIO_DEVICE_ID_KS_7010		0x7910

/* Read/Write Status Register */
#define READ_STATUS_REG		0x000000
#define WRITE_STATUS_REG	0x00000C
enum reg_status_type {
	REG_STATUS_BUSY,
	REG_STATUS_IDLE
};

/* Read Index Register */
#define READ_INDEX_REG		0x000004

/* Read Data Size Register */
#define READ_DATA_SIZE_REG	0x000008

/* Write Index Register */
#define WRITE_INDEX_REG		0x000010

/*
 * Write Status/Read Data Size Register
 * for network packet (less than 2048 bytes data)
 */
#define WSTATUS_RSIZE_REG	0x000014

/* Write Status Register value */
#define WSTATUS_MASK		0x80

/* Read Data Size Register value [10:4] */
#define RSIZE_MASK		0x7F

/* ARM to SD interrupt Enable */
#define INT_ENABLE_REG		0x000020
/* ARM to SD interrupt Pending */
#define INT_PENDING_REG		0x000024

#define INT_GCR_B              BIT(7)
#define INT_GCR_A              BIT(6)
#define INT_WRITE_STATUS       BIT(5)
#define INT_WRITE_INDEX        BIT(4)
#define INT_WRITE_SIZE         BIT(3)
#define INT_READ_STATUS        BIT(2)
#define INT_READ_INDEX         BIT(1)
#define INT_READ_SIZE          BIT(0)

/* General Communication Register A */
#define GCR_A_REG		0x000028
enum gen_com_reg_a {
	GCR_A_INIT,
	GCR_A_REMAP,
	GCR_A_RUN
};

/* General Communication Register B */
#define GCR_B_REG		0x00002C
enum gen_com_reg_b {
	GCR_B_ACTIVE,
	GCR_B_DOZE
};

/* Wakeup Register */
#define WAKEUP_REG		0x008018
#define WAKEUP_REQ		0x5a

/* AHB Data Window  0x010000-0x01FFFF */
#define DATA_WINDOW		0x010000
#define WINDOW_SIZE		(64 * 1024)

#define KS7010_IRAM_ADDRESS	0x06000000

#define KS7010_IO_BLOCK_SIZE 512

/**
 * struct ks_sdio_card - SDIO device data.
 *
 * Structure is used as the &struct sdio_func private data.
 *
 * @func: Pointer to the SDIO function device.
 * @priv: Pointer to the &struct net_device private data.
 */
struct ks_sdio_card {
	struct sdio_func *func;
	struct ks_wlan_private *priv;
};

static struct sdio_func *ks7010_to_func(struct ks_wlan_private *priv)
{
	struct ks_sdio_card *ks_sdio = priv->if_hw;

	return ks_sdio->func;
}

/* Read single byte from device address into byte (CMD52) */
static int ks7010_sdio_readb(struct ks_wlan_private *priv,
			     u32 address, u8 *byte)
{
	struct sdio_func *func = ks7010_to_func(priv);
	int ret;

	*byte = sdio_readb(func, address, &ret);

	return ret;
}

/* Read length bytes from device address into buffer (CMD53) */
static int ks7010_sdio_read(struct ks_wlan_private *priv, u32 address,
			    u8 *buffer, unsigned int length)
{
	struct sdio_func *func = ks7010_to_func(priv);

	return sdio_memcpy_fromio(func, buffer, address, length);
}

/* Write single byte to device address (CMD52) */
static int ks7010_sdio_writeb(struct ks_wlan_private *priv,
			      u32 address, u8 byte)
{
	struct sdio_func *func = ks7010_to_func(priv);
	int ret;

	sdio_writeb(func, byte, address, &ret);

	return ret;
}

/* Write length bytes to device address from buffer (CMD53) */
static int ks7010_sdio_write(struct ks_wlan_private *priv, u32 address,
			     u8 *buffer, unsigned int length)
{
	struct sdio_func *func = ks7010_to_func(priv);

	return sdio_memcpy_toio(func, address, buffer, length);
}

static void ks_wlan_hw_sleep_doze_request(struct ks_wlan_private *priv)
{
	int ret;

	/* clear request */
	atomic_set(&priv->sleepstatus.doze_request, 0);

	if (atomic_read(&priv->sleepstatus.status) == 0) {
		ret = ks7010_sdio_writeb(priv, GCR_B_REG, GCR_B_DOZE);
		if (ret) {
			netdev_err(priv->net_dev, "write GCR_B_REG\n");
			goto set_sleep_mode;
		}
		atomic_set(&priv->sleepstatus.status, 1);
		priv->last_doze = jiffies;
	}

set_sleep_mode:
	priv->sleep_mode = atomic_read(&priv->sleepstatus.status);
}

static void ks_wlan_hw_sleep_wakeup_request(struct ks_wlan_private *priv)
{
	int ret;

	/* clear request */
	atomic_set(&priv->sleepstatus.wakeup_request, 0);

	if (atomic_read(&priv->sleepstatus.status) == 1) {
		ret = ks7010_sdio_writeb(priv, WAKEUP_REG, WAKEUP_REQ);
		if (ret) {
			netdev_err(priv->net_dev, "write WAKEUP_REG\n");
			goto set_sleep_mode;
		}
		atomic_set(&priv->sleepstatus.status, 0);
		priv->last_wakeup = jiffies;
		++priv->wakeup_count;
	}

set_sleep_mode:
	priv->sleep_mode = atomic_read(&priv->sleepstatus.status);
}

void ks_wlan_hw_wakeup_request(struct ks_wlan_private *priv)
{
	int ret;

	if (atomic_read(&priv->psstatus.status) == PS_SNOOZE) {
		ret = ks7010_sdio_writeb(priv, WAKEUP_REG, WAKEUP_REQ);
		if (ret)
			netdev_err(priv->net_dev, "write WAKEUP_REG\n");

		priv->last_wakeup = jiffies;
		++priv->wakeup_count;
	}
}

static void _ks_wlan_hw_power_save(struct ks_wlan_private *priv)
{
	unsigned char byte;
	int ret;

	if (priv->reg.power_mgmt == POWER_MGMT_ACTIVE)
		return;

	if (priv->reg.operation_mode != MODE_INFRASTRUCTURE)
		return;

	if (!is_connect_status(priv->connect_status))
		return;

	if (priv->dev_state != DEVICE_STATE_SLEEP)
		return;

	if (atomic_read(&priv->psstatus.status) == PS_SNOOZE)
		return;

	netdev_dbg(priv->net_dev,
		   "STATUS:\n"
		   "- psstatus.status = %d\n"
		   "- psstatus.confirm_wait = %d\n"
		   "- psstatus.snooze_guard = %d\n"
		   "- txq_count = %d\n",
		   atomic_read(&priv->psstatus.status),
		   atomic_read(&priv->psstatus.confirm_wait),
		   atomic_read(&priv->psstatus.snooze_guard),
		   txq_count(priv));

	if (atomic_read(&priv->psstatus.confirm_wait) ||
	    atomic_read(&priv->psstatus.snooze_guard) ||
	    txq_has_space(priv)) {
		queue_delayed_work(priv->wq, &priv->rw_dwork, 0);
		return;
	}

	ret = ks7010_sdio_readb(priv, INT_PENDING_REG, &byte);
	if (ret) {
		netdev_err(priv->net_dev, "read INT_PENDING_REG\n");
		goto queue_delayed_work;
	}
	if (byte)
		goto queue_delayed_work;

	ret = ks7010_sdio_writeb(priv, GCR_B_REG, GCR_B_DOZE);
	if (ret) {
		netdev_err(priv->net_dev, "write GCR_B_REG\n");
		goto queue_delayed_work;
	}
	atomic_set(&priv->psstatus.status, PS_SNOOZE);

	return;

queue_delayed_work:
	queue_delayed_work(priv->wq, &priv->rw_dwork, 1);
}

int ks_wlan_hw_power_save(struct ks_wlan_private *priv)
{
	queue_delayed_work(priv->wq, &priv->rw_dwork, 1);
	return 0;
}

static int enqueue_txdev(struct ks_wlan_private *priv, unsigned char *p,
			 unsigned long size,
			 void (*complete_handler)(struct ks_wlan_private *priv,
						  struct sk_buff *skb),
			 struct sk_buff *skb)
{
	struct tx_device_buffer *sp;
	int ret;

	if (priv->dev_state < DEVICE_STATE_BOOT) {
		ret = -EPERM;
		goto err_complete;
	}

	if ((TX_DEVICE_BUFF_SIZE - 1) <= txq_count(priv)) {
		netdev_err(priv->net_dev, "tx buffer overflow\n");
		ret = -EOVERFLOW;
		goto err_complete;
	}

	sp = &priv->tx_dev.tx_dev_buff[priv->tx_dev.qtail];
	sp->sendp = p;
	sp->size = size;
	sp->complete_handler = complete_handler;
	sp->skb = skb;
	inc_txqtail(priv);

	return 0;

err_complete:
	kfree(p);
	if (complete_handler)
		(*complete_handler)(priv, skb);

	return ret;
}

/* write data */
static int write_to_device(struct ks_wlan_private *priv, u8 *buffer,
			   unsigned long size)
{
	struct hostif_hdr *hdr;
	int ret;

	hdr = (struct hostif_hdr *)buffer;

	if (le16_to_cpu(hdr->event) < HIF_DATA_REQ ||
	    le16_to_cpu(hdr->event) > HIF_REQ_MAX) {
		netdev_err(priv->net_dev, "unknown event=%04X\n", hdr->event);
		return 0;
	}

	ret = ks7010_sdio_write(priv, DATA_WINDOW, buffer, size);
	if (ret) {
		netdev_err(priv->net_dev, "write DATA_WINDOW\n");
		return ret;
	}

	ret = ks7010_sdio_writeb(priv, WRITE_STATUS_REG, REG_STATUS_BUSY);
	if (ret) {
		netdev_err(priv->net_dev, "write WRITE_STATUS_REG\n");
		return ret;
	}

	return 0;
}

static void tx_device_task(struct ks_wlan_private *priv)
{
	struct tx_device_buffer *sp;
	int ret;

	if (!txq_has_space(priv) ||
	    atomic_read(&priv->psstatus.status) == PS_SNOOZE)
		return;

	sp = &priv->tx_dev.tx_dev_buff[priv->tx_dev.qhead];
	if (priv->dev_state >= DEVICE_STATE_BOOT) {
		ret = write_to_device(priv, sp->sendp, sp->size);
		if (ret) {
			netdev_err(priv->net_dev,
				   "write_to_device error !!(%d)\n", ret);
			queue_delayed_work(priv->wq, &priv->rw_dwork, 1);
			return;
		}
	}
	kfree(sp->sendp);
	if (sp->complete_handler)	/* TX Complete */
		(*sp->complete_handler)(priv, sp->skb);
	inc_txqhead(priv);

	if (txq_has_space(priv))
		queue_delayed_work(priv->wq, &priv->rw_dwork, 0);
}

int ks_wlan_hw_tx(struct ks_wlan_private *priv, void *p, unsigned long size,
		  void (*complete_handler)(struct ks_wlan_private *priv,
					   struct sk_buff *skb),
		  struct sk_buff *skb)
{
	int result = 0;
	struct hostif_hdr *hdr;

	hdr = (struct hostif_hdr *)p;

	if (le16_to_cpu(hdr->event) < HIF_DATA_REQ ||
	    le16_to_cpu(hdr->event) > HIF_REQ_MAX) {
		netdev_err(priv->net_dev, "unknown event=%04X\n", hdr->event);
		return 0;
	}

	/* add event to hostt buffer */
	priv->hostt.buff[priv->hostt.qtail] = le16_to_cpu(hdr->event);
	priv->hostt.qtail = (priv->hostt.qtail + 1) % SME_EVENT_BUFF_SIZE;

	spin_lock(&priv->tx_dev.tx_dev_lock);
	result = enqueue_txdev(priv, p, size, complete_handler, skb);
	spin_unlock(&priv->tx_dev.tx_dev_lock);

	if (txq_has_space(priv))
		queue_delayed_work(priv->wq, &priv->rw_dwork, 0);

	return result;
}

static void rx_event_task(unsigned long dev)
{
	struct ks_wlan_private *priv = (struct ks_wlan_private *)dev;
	struct rx_device_buffer *rp;

	if (rxq_has_space(priv) && priv->dev_state >= DEVICE_STATE_BOOT) {
		rp = &priv->rx_dev.rx_dev_buff[priv->rx_dev.qhead];
		hostif_receive(priv, rp->data, rp->size);
		inc_rxqhead(priv);

		if (rxq_has_space(priv))
			tasklet_schedule(&priv->rx_bh_task);
	}
}

static void ks_wlan_hw_rx(struct ks_wlan_private *priv, uint16_t size)
{
	int ret;
	struct rx_device_buffer *rx_buffer;
	struct hostif_hdr *hdr;
	unsigned short event = 0;

	/* receive data */
	if (rxq_count(priv) >= (RX_DEVICE_BUFF_SIZE - 1)) {
		netdev_err(priv->net_dev, "rx buffer overflow\n");
		return;
	}
	rx_buffer = &priv->rx_dev.rx_dev_buff[priv->rx_dev.qtail];

	ret = ks7010_sdio_read(priv, DATA_WINDOW, &rx_buffer->data[0],
			       hif_align_size(size));
	if (ret)
		return;

	/* length check */
	if (size > 2046 || size == 0) {
#ifdef DEBUG
		print_hex_dump_bytes("INVALID DATA dump: ",
				     DUMP_PREFIX_OFFSET,
				     rx_buffer->data, 32);
#endif
		ret = ks7010_sdio_writeb(priv, READ_STATUS_REG, REG_STATUS_IDLE);
		if (ret)
			netdev_err(priv->net_dev, "write READ_STATUS_REG\n");

		/* length check fail */
		return;
	}

	hdr = (struct hostif_hdr *)&rx_buffer->data[0];
	rx_buffer->size = le16_to_cpu(hdr->size) + sizeof(hdr->size);
	event = le16_to_cpu(hdr->event);
	inc_rxqtail(priv);

	ret = ks7010_sdio_writeb(priv, READ_STATUS_REG, REG_STATUS_IDLE);
	if (ret)
		netdev_err(priv->net_dev, "write READ_STATUS_REG\n");

	if (atomic_read(&priv->psstatus.confirm_wait) && is_hif_conf(event)) {
		netdev_dbg(priv->net_dev, "IS_HIF_CONF true !!\n");
		atomic_dec(&priv->psstatus.confirm_wait);
	}

	tasklet_schedule(&priv->rx_bh_task);
}

static void ks7010_rw_function(struct work_struct *work)
{
	struct ks_wlan_private *priv = container_of(work,
						    struct ks_wlan_private,
						    rw_dwork.work);
	struct sdio_func *func = ks7010_to_func(priv);
	unsigned char byte;
	int ret;

	/* wait after DOZE */
	if (time_after(priv->last_doze + msecs_to_jiffies(30), jiffies)) {
		netdev_dbg(priv->net_dev, "wait after DOZE\n");
		queue_delayed_work(priv->wq, &priv->rw_dwork, 1);
		return;
	}

	/* wait after WAKEUP */
	while (time_after(priv->last_wakeup + msecs_to_jiffies(30), jiffies)) {
		netdev_dbg(priv->net_dev, "wait after WAKEUP\n");
		dev_info(&func->dev, "wake: %lu %lu\n",
			 priv->last_wakeup + msecs_to_jiffies(30), jiffies);
		msleep(30);
	}

	sdio_claim_host(func);

	/* power save wakeup */
	if (atomic_read(&priv->psstatus.status) == PS_SNOOZE) {
		if (txq_has_space(priv)) {
			ks_wlan_hw_wakeup_request(priv);
			queue_delayed_work(priv->wq, &priv->rw_dwork, 1);
		}
		goto release_host;
	}

	/* sleep mode doze */
	if (atomic_read(&priv->sleepstatus.doze_request) == 1) {
		ks_wlan_hw_sleep_doze_request(priv);
		goto release_host;
	}
	/* sleep mode wakeup */
	if (atomic_read(&priv->sleepstatus.wakeup_request) == 1) {
		ks_wlan_hw_sleep_wakeup_request(priv);
		goto release_host;
	}

	/* read (WriteStatus/ReadDataSize FN1:00_0014) */
	ret = ks7010_sdio_readb(priv, WSTATUS_RSIZE_REG, &byte);
	if (ret) {
		netdev_err(priv->net_dev, "read WSTATUS_RSIZE_REG psstatus=%d\n",
			   atomic_read(&priv->psstatus.status));
		goto release_host;
	}

	if (byte & RSIZE_MASK) {	/* Read schedule */
		ks_wlan_hw_rx(priv, (uint16_t)((byte & RSIZE_MASK) << 4));
	}
	if ((byte & WSTATUS_MASK))
		tx_device_task(priv);

	_ks_wlan_hw_power_save(priv);

release_host:
	sdio_release_host(func);
}

static void ks_sdio_interrupt(struct sdio_func *func)
{
	int ret;
	struct ks_sdio_card *card;
	struct ks_wlan_private *priv;
	unsigned char status, rsize, byte;

	card = sdio_get_drvdata(func);
	priv = card->priv;

	if (priv->dev_state < DEVICE_STATE_BOOT)
		goto queue_delayed_work;

	ret = ks7010_sdio_readb(priv, INT_PENDING_REG, &status);
	if (ret) {
		netdev_err(priv->net_dev, "read INT_PENDING_REG\n");
		goto queue_delayed_work;
	}

	/* schedule task for interrupt status */
	/* bit7 -> Write General Communication B register */
	/* read (General Communication B register) */
	/* bit5 -> Write Status Idle */
	/* bit2 -> Read Status Busy  */
	if (status & INT_GCR_B ||
	    atomic_read(&priv->psstatus.status) == PS_SNOOZE) {
		ret = ks7010_sdio_readb(priv, GCR_B_REG, &byte);
		if (ret) {
			netdev_err(priv->net_dev, "read GCR_B_REG\n");
			goto queue_delayed_work;
		}
		if (byte == GCR_B_ACTIVE) {
			if (atomic_read(&priv->psstatus.status) == PS_SNOOZE) {
				atomic_set(&priv->psstatus.status, PS_WAKEUP);
				priv->wakeup_count = 0;
			}
			complete(&priv->psstatus.wakeup_wait);
		}
	}

	do {
		/* read (WriteStatus/ReadDataSize FN1:00_0014) */
		ret = ks7010_sdio_readb(priv, WSTATUS_RSIZE_REG, &byte);
		if (ret) {
			netdev_err(priv->net_dev, "read WSTATUS_RSIZE_REG\n");
			goto queue_delayed_work;
		}
		rsize = byte & RSIZE_MASK;
		if (rsize != 0)		/* Read schedule */
			ks_wlan_hw_rx(priv, (uint16_t)(rsize << 4));

		if (byte & WSTATUS_MASK) {
			if (atomic_read(&priv->psstatus.status) == PS_SNOOZE) {
				if (txq_has_space(priv)) {
					ks_wlan_hw_wakeup_request(priv);
					queue_delayed_work(priv->wq,
							   &priv->rw_dwork, 1);
					return;
				}
			} else {
				tx_device_task(priv);
			}
		}
	} while (rsize);

queue_delayed_work:
	queue_delayed_work(priv->wq, &priv->rw_dwork, 0);
}

static int trx_device_init(struct ks_wlan_private *priv)
{
	priv->tx_dev.qhead = 0;
	priv->tx_dev.qtail = 0;

	priv->rx_dev.qhead = 0;
	priv->rx_dev.qtail = 0;

	spin_lock_init(&priv->tx_dev.tx_dev_lock);
	spin_lock_init(&priv->rx_dev.rx_dev_lock);

	tasklet_init(&priv->rx_bh_task, rx_event_task, (unsigned long)priv);

	return 0;
}

static void trx_device_exit(struct ks_wlan_private *priv)
{
	struct tx_device_buffer *sp;

	/* tx buffer clear */
	while (txq_has_space(priv)) {
		sp = &priv->tx_dev.tx_dev_buff[priv->tx_dev.qhead];
		kfree(sp->sendp);
		if (sp->complete_handler)	/* TX Complete */
			(*sp->complete_handler)(priv, sp->skb);
		inc_txqhead(priv);
	}

	tasklet_kill(&priv->rx_bh_task);
}

static int ks7010_sdio_update_index(struct ks_wlan_private *priv, u32 index)
{
	int ret;
	unsigned char *data_buf;

	data_buf = kmemdup(&index, sizeof(u32), GFP_KERNEL);
	if (!data_buf)
		return -ENOMEM;

	ret = ks7010_sdio_write(priv, WRITE_INDEX_REG, data_buf, sizeof(index));
	if (ret)
		goto err_free_data_buf;

	ret = ks7010_sdio_write(priv, READ_INDEX_REG, data_buf, sizeof(index));
	if (ret)
		goto err_free_data_buf;

	return 0;

err_free_data_buf:
	kfree(data_buf);

	return ret;
}

#define ROM_BUFF_SIZE (64 * 1024)
static int ks7010_sdio_data_compare(struct ks_wlan_private *priv, u32 address,
				    u8 *data, unsigned int size)
{
	int ret;
	u8 *read_buf;

	read_buf = kmalloc(ROM_BUFF_SIZE, GFP_KERNEL);
	if (!read_buf)
		return -ENOMEM;

	ret = ks7010_sdio_read(priv, address, read_buf, size);
	if (ret)
		goto err_free_read_buf;

	if (memcmp(data, read_buf, size) != 0) {
		ret = -EIO;
		netdev_err(priv->net_dev, "data compare error (%d)\n", ret);
		goto err_free_read_buf;
	}

	return 0;

err_free_read_buf:
	kfree(read_buf);

	return ret;
}

static int ks7010_copy_firmware(struct ks_wlan_private *priv,
				const struct firmware *fw_entry)
{
	unsigned int length;
	unsigned int size;
	unsigned int offset;
	unsigned int n = 0;
	u8 *rom_buf;
	int ret;

	rom_buf = kmalloc(ROM_BUFF_SIZE, GFP_KERNEL);
	if (!rom_buf)
		return -ENOMEM;

	length = fw_entry->size;

	do {
		if (length >= ROM_BUFF_SIZE) {
			size = ROM_BUFF_SIZE;
			length = length - ROM_BUFF_SIZE;
		} else {
			size = length;
			length = 0;
		}
		if (size == 0)
			break;

		memcpy(rom_buf, fw_entry->data + n, size);

		offset = n;
		ret = ks7010_sdio_update_index(priv,
					       KS7010_IRAM_ADDRESS + offset);
		if (ret)
			goto free_rom_buf;

		ret = ks7010_sdio_write(priv, DATA_WINDOW, rom_buf, size);
		if (ret)
			goto free_rom_buf;

		ret = ks7010_sdio_data_compare(priv,
					       DATA_WINDOW, rom_buf, size);
		if (ret)
			goto free_rom_buf;

		n += size;

	} while (size);

	ret = ks7010_sdio_writeb(priv, GCR_A_REG, GCR_A_REMAP);

free_rom_buf:
	kfree(rom_buf);
	return ret;
}

static int ks7010_upload_firmware(struct ks_sdio_card *card)
{
	struct ks_wlan_private *priv = card->priv;
	struct sdio_func *func = ks7010_to_func(priv);
	unsigned int n;
	unsigned char byte = 0;
	int ret;
	const struct firmware *fw_entry = NULL;


	sdio_claim_host(func);

	/* Firmware running ? */
	ret = ks7010_sdio_readb(priv, GCR_A_REG, &byte);
	if (ret)
		goto release_host;
	if (byte == GCR_A_RUN) {
		netdev_dbg(priv->net_dev, "MAC firmware running ...\n");
		ret = -EBUSY;
		goto release_host;
	}

	ret = request_firmware(&fw_entry, ROM_FILE,
			       &func->dev);
	if (ret)
		goto release_host;

	ret = ks7010_copy_firmware(priv, fw_entry);
	if (ret)
		goto release_firmware;

	/* Firmware running check */
	for (n = 0; n < 50; ++n) {
		usleep_range(10000, 11000);	/* wait_ms(10); */
		ret = ks7010_sdio_readb(priv, GCR_A_REG, &byte);
		if (ret)
			goto release_firmware;

		if (byte == GCR_A_RUN)
			break;
	}
	if ((50) <= n) {
		netdev_err(priv->net_dev, "firmware can't start\n");
		ret = -EIO;
		goto release_firmware;
	}

	ret = 0;

 release_firmware:
	release_firmware(fw_entry);
 release_host:
	sdio_release_host(func);

	return ret;
}

static void ks7010_sme_enqueue_events(struct ks_wlan_private *priv)
{
	hostif_sme_enqueue(priv, SME_GET_EEPROM_CKSUM);

	/* load initial wireless parameter */
	hostif_sme_enqueue(priv, SME_STOP_REQUEST);

	hostif_sme_enqueue(priv, SME_RTS_THRESHOLD_REQUEST);
	hostif_sme_enqueue(priv, SME_FRAGMENTATION_THRESHOLD_REQUEST);

	hostif_sme_enqueue(priv, SME_WEP_INDEX_REQUEST);
	hostif_sme_enqueue(priv, SME_WEP_KEY1_REQUEST);
	hostif_sme_enqueue(priv, SME_WEP_KEY2_REQUEST);
	hostif_sme_enqueue(priv, SME_WEP_KEY3_REQUEST);
	hostif_sme_enqueue(priv, SME_WEP_KEY4_REQUEST);

	hostif_sme_enqueue(priv, SME_WEP_FLAG_REQUEST);
	hostif_sme_enqueue(priv, SME_RSN_ENABLED_REQUEST);
	hostif_sme_enqueue(priv, SME_MODE_SET_REQUEST);
	hostif_sme_enqueue(priv, SME_START_REQUEST);
}

static void ks7010_card_init(struct ks_wlan_private *priv)
{
	init_completion(&priv->confirm_wait);

	/* get mac address & firmware version */
	hostif_sme_enqueue(priv, SME_START);

	if (!wait_for_completion_interruptible_timeout
	    (&priv->confirm_wait, 5 * HZ)) {
		netdev_dbg(priv->net_dev, "wait time out!! SME_START\n");
	}

	if (priv->mac_address_valid && priv->version_size != 0)
		priv->dev_state = DEVICE_STATE_PREINIT;

	ks7010_sme_enqueue_events(priv);

	if (!wait_for_completion_interruptible_timeout
	    (&priv->confirm_wait, 5 * HZ)) {
		netdev_dbg(priv->net_dev, "wait time out!! wireless parameter set\n");
	}

	if (priv->dev_state >= DEVICE_STATE_PREINIT) {
		netdev_dbg(priv->net_dev, "DEVICE READY!!\n");
		priv->dev_state = DEVICE_STATE_READY;
	}
}

static void ks7010_init_defaults(struct ks_wlan_private *priv)
{
	priv->reg.tx_rate = TX_RATE_AUTO;
	priv->reg.preamble = LONG_PREAMBLE;
	priv->reg.power_mgmt = POWER_MGMT_ACTIVE;
	priv->reg.scan_type = ACTIVE_SCAN;
	priv->reg.beacon_lost_count = 20;
	priv->reg.rts = 2347UL;
	priv->reg.fragment = 2346UL;
	priv->reg.phy_type = D_11BG_COMPATIBLE_MODE;
	priv->reg.cts_mode = CTS_MODE_FALSE;
	priv->reg.rate_set.body[11] = TX_RATE_54M;
	priv->reg.rate_set.body[10] = TX_RATE_48M;
	priv->reg.rate_set.body[9] = TX_RATE_36M;
	priv->reg.rate_set.body[8] = TX_RATE_18M;
	priv->reg.rate_set.body[7] = TX_RATE_9M;
	priv->reg.rate_set.body[6] = TX_RATE_24M | BASIC_RATE;
	priv->reg.rate_set.body[5] = TX_RATE_12M | BASIC_RATE;
	priv->reg.rate_set.body[4] = TX_RATE_6M | BASIC_RATE;
	priv->reg.rate_set.body[3] = TX_RATE_11M | BASIC_RATE;
	priv->reg.rate_set.body[2] = TX_RATE_5M | BASIC_RATE;
	priv->reg.rate_set.body[1] = TX_RATE_2M | BASIC_RATE;
	priv->reg.rate_set.body[0] = TX_RATE_1M | BASIC_RATE;
	priv->reg.tx_rate = TX_RATE_FULL_AUTO;
	priv->reg.rate_set.size = 12;
}

static int ks7010_sdio_setup_irqs(struct sdio_func *func)
{
	int ret;

	/* interrupt disable */
	sdio_writeb(func, 0, INT_ENABLE_REG, &ret);
	if (ret)
		goto irq_error;

	sdio_writeb(func, 0xff, INT_PENDING_REG, &ret);
	if (ret)
		goto irq_error;

	/* setup interrupt handler */
	ret = sdio_claim_irq(func, ks_sdio_interrupt);

irq_error:
	return ret;
}

static void ks7010_sdio_init_irqs(struct sdio_func *func,
				  struct ks_wlan_private *priv)
{
	unsigned char byte;
	int ret;

	/*
	 * interrupt setting
	 * clear Interrupt status write
	 * (ARMtoSD_InterruptPending FN1:00_0024)
	 */
	sdio_claim_host(func);
	ret = ks7010_sdio_writeb(priv, INT_PENDING_REG, 0xff);
	sdio_release_host(func);
	if (ret)
		netdev_err(priv->net_dev, "write INT_PENDING_REG\n");

	/* enable ks7010sdio interrupt */
	byte = (INT_GCR_B | INT_READ_STATUS | INT_WRITE_STATUS);
	sdio_claim_host(func);
	ret = ks7010_sdio_writeb(priv, INT_ENABLE_REG, byte);
	sdio_release_host(func);
	if (ret)
		netdev_err(priv->net_dev, "write INT_ENABLE_REG\n");
}

static void ks7010_private_init(struct ks_wlan_private *priv,
				struct ks_sdio_card *card,
				struct net_device *netdev)
{
	/* private memory initialize */
	priv->if_hw = card;

	priv->dev_state = DEVICE_STATE_PREBOOT;
	priv->net_dev = netdev;
	priv->firmware_version[0] = '\0';
	priv->version_size = 0;
	priv->last_doze = jiffies;
	priv->last_wakeup = jiffies;
	memset(&priv->nstats, 0, sizeof(priv->nstats));
	memset(&priv->wstats, 0, sizeof(priv->wstats));

	/* sleep mode */
	atomic_set(&priv->sleepstatus.doze_request, 0);
	atomic_set(&priv->sleepstatus.wakeup_request, 0);
	atomic_set(&priv->sleepstatus.wakeup_request, 0);

	trx_device_init(priv);
	hostif_init(priv);
	ks_wlan_net_start(netdev);
	ks7010_init_defaults(priv);
}

static int ks7010_sdio_probe(struct sdio_func *func,
			     const struct sdio_device_id *device)
{
	struct ks_wlan_private *priv = NULL;
	struct net_device *netdev = NULL;
	struct ks_sdio_card *card;
	int ret;

	card = kzalloc(sizeof(*card), GFP_KERNEL);
	if (!card)
		return -ENOMEM;

	card->func = func;

	sdio_claim_host(func);

	ret = sdio_set_block_size(func, KS7010_IO_BLOCK_SIZE);
	if (ret)
		goto err_free_card;

	dev_dbg(&card->func->dev, "multi_block=%d sdio_set_block_size()=%d %d\n",
		func->card->cccr.multi_block, func->cur_blksize, ret);

	ret = sdio_enable_func(func);
	if (ret)
		goto err_free_card;

	ret = ks7010_sdio_setup_irqs(func);
	if (ret)
		goto err_disable_func;

	sdio_release_host(func);

	sdio_set_drvdata(func, card);

	dev_dbg(&card->func->dev, "class = 0x%X, vendor = 0x%X, device = 0x%X\n",
		func->class, func->vendor, func->device);

	/* private memory allocate */
	netdev = alloc_etherdev(sizeof(*priv));
	if (!netdev) {
		dev_err(&card->func->dev, "Unable to alloc new net device\n");
		goto err_release_irq;
	}

	ret = dev_alloc_name(netdev, "wlan%d");
	if (ret < 0) {
		dev_err(&card->func->dev, "Couldn't get name!\n");
		goto err_free_netdev;
	}

	priv = netdev_priv(netdev);

	card->priv = priv;
	SET_NETDEV_DEV(netdev, &card->func->dev);

	ks7010_private_init(priv, card, netdev);

	ret = ks7010_upload_firmware(card);
	if (ret) {
		netdev_err(priv->net_dev,
			   "firmware load failed !! ret = %d\n", ret);
		goto err_free_netdev;
	}

	ks7010_sdio_init_irqs(func, priv);

	priv->dev_state = DEVICE_STATE_BOOT;

	priv->wq = alloc_workqueue("wq", WQ_MEM_RECLAIM, 1);
	if (!priv->wq) {
		netdev_err(priv->net_dev, "create_workqueue failed !!\n");
		goto err_free_netdev;
	}

	INIT_DELAYED_WORK(&priv->rw_dwork, ks7010_rw_function);
	ks7010_card_init(priv);

	ret = register_netdev(priv->net_dev);
	if (ret)
		goto err_free_netdev;

	return 0;

 err_free_netdev:
	free_netdev(netdev);
 err_release_irq:
	sdio_claim_host(func);
	sdio_release_irq(func);
 err_disable_func:
	sdio_disable_func(func);
 err_free_card:
	sdio_release_host(func);
	sdio_set_drvdata(func, NULL);
	kfree(card);

	return -ENODEV;
}

/* send stop request to MAC */
static int send_stop_request(struct sdio_func *func)
{
	struct hostif_stop_request *pp;
	struct ks_sdio_card *card;
	size_t size;

	card = sdio_get_drvdata(func);

	pp = kzalloc(hif_align_size(sizeof(*pp)), GFP_KERNEL);
	if (!pp)
		return -ENOMEM;

	size = sizeof(*pp) - sizeof(pp->header.size);
	pp->header.size = cpu_to_le16((uint16_t)size);
	pp->header.event = cpu_to_le16((uint16_t)HIF_STOP_REQ);

	sdio_claim_host(func);
	write_to_device(card->priv, (u8 *)pp, hif_align_size(sizeof(*pp)));
	sdio_release_host(func);

	kfree(pp);
	return 0;
}

static void ks7010_sdio_remove(struct sdio_func *func)
{
	int ret;
	struct ks_sdio_card *card;
	struct ks_wlan_private *priv;

	card = sdio_get_drvdata(func);

	if (!card)
		return;

	priv = card->priv;
	if (!priv)
		goto err_free_card;

	ks_wlan_net_stop(priv->net_dev);

	/* interrupt disable */
	sdio_claim_host(func);
	sdio_writeb(func, 0, INT_ENABLE_REG, &ret);
	sdio_writeb(func, 0xff, INT_PENDING_REG, &ret);
	sdio_release_host(func);

	ret = send_stop_request(func);
	if (ret)	/* memory allocation failure */
		goto err_free_card;

	if (priv->wq) {
		flush_workqueue(priv->wq);
		destroy_workqueue(priv->wq);
	}

	hostif_exit(priv);

	unregister_netdev(priv->net_dev);

	trx_device_exit(priv);
	free_netdev(priv->net_dev);
	card->priv = NULL;

	sdio_claim_host(func);
	sdio_release_irq(func);
	sdio_disable_func(func);
	sdio_release_host(func);
err_free_card:
	sdio_set_drvdata(func, NULL);
	kfree(card);
}

static const struct sdio_device_id ks7010_sdio_ids[] = {
	{SDIO_DEVICE(SDIO_VENDOR_ID_KS_CODE_A, SDIO_DEVICE_ID_KS_7010)},
	{SDIO_DEVICE(SDIO_VENDOR_ID_KS_CODE_B, SDIO_DEVICE_ID_KS_7010)},
	{ /* all zero */ }
};
MODULE_DEVICE_TABLE(sdio, ks7010_sdio_ids);

static struct sdio_driver ks7010_sdio_driver = {
	.name = "ks7010_sdio",
	.id_table = ks7010_sdio_ids,
	.probe = ks7010_sdio_probe,
	.remove = ks7010_sdio_remove,
};

module_driver(ks7010_sdio_driver, sdio_register_driver, sdio_unregister_driver);
MODULE_AUTHOR("Sang Engineering, Qi-Hardware, KeyStream");
MODULE_DESCRIPTION("Driver for KeyStream KS7010 based SDIO cards");
MODULE_LICENSE("GPL v2");
MODULE_FIRMWARE(ROM_FILE);
