// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2008 Christian Lamparter <chunkeey@web.de>
 * Copyright 2008       Johannes Berg <johannes@sipsolutions.net>
 *
 * This driver is a port from stlc45xx:
 *	Copyright (C) 2008 Nokia Corporation and/or its subsidiary(-ies).
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/firmware.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/spi/spi.h>
#include <linux/etherdevice.h>
#include <linux/gpio.h>
#include <linux/slab.h>

#include "p54spi.h"
#include "p54.h"

#include "lmac.h"

#ifdef CONFIG_P54_SPI_DEFAULT_EEPROM
#include "p54spi_eeprom.h"
#endif /* CONFIG_P54_SPI_DEFAULT_EEPROM */

MODULE_FIRMWARE("3826.arm");
MODULE_FIRMWARE("3826.eeprom");

/* gpios should be handled in board files and provided via platform data,
 * but because it's currently impossible for p54spi to have a header file
 * in include/linux, let's use module paramaters for now
 */

static int p54spi_gpio_power = 97;
module_param(p54spi_gpio_power, int, 0444);
MODULE_PARM_DESC(p54spi_gpio_power, "gpio number for power line");

static int p54spi_gpio_irq = 87;
module_param(p54spi_gpio_irq, int, 0444);
MODULE_PARM_DESC(p54spi_gpio_irq, "gpio number for irq line");

static void p54spi_spi_read(struct p54s_priv *priv, u8 address,
			      void *buf, size_t len)
{
	struct spi_transfer t[2];
	struct spi_message m;
	__le16 addr;

	/* We first push the address */
	addr = cpu_to_le16(address << 8 | SPI_ADRS_READ_BIT_15);

	spi_message_init(&m);
	memset(t, 0, sizeof(t));

	t[0].tx_buf = &addr;
	t[0].len = sizeof(addr);
	spi_message_add_tail(&t[0], &m);

	t[1].rx_buf = buf;
	t[1].len = len;
	spi_message_add_tail(&t[1], &m);

	spi_sync(priv->spi, &m);
}


static void p54spi_spi_write(struct p54s_priv *priv, u8 address,
			     const void *buf, size_t len)
{
	struct spi_transfer t[3];
	struct spi_message m;
	__le16 addr;

	/* We first push the address */
	addr = cpu_to_le16(address << 8);

	spi_message_init(&m);
	memset(t, 0, sizeof(t));

	t[0].tx_buf = &addr;
	t[0].len = sizeof(addr);
	spi_message_add_tail(&t[0], &m);

	t[1].tx_buf = buf;
	t[1].len = len & ~1;
	spi_message_add_tail(&t[1], &m);

	if (len % 2) {
		__le16 last_word;
		last_word = cpu_to_le16(((u8 *)buf)[len - 1]);

		t[2].tx_buf = &last_word;
		t[2].len = sizeof(last_word);
		spi_message_add_tail(&t[2], &m);
	}

	spi_sync(priv->spi, &m);
}

static u32 p54spi_read32(struct p54s_priv *priv, u8 addr)
{
	__le32 val;

	p54spi_spi_read(priv, addr, &val, sizeof(val));

	return le32_to_cpu(val);
}

static inline void p54spi_write16(struct p54s_priv *priv, u8 addr, __le16 val)
{
	p54spi_spi_write(priv, addr, &val, sizeof(val));
}

static inline void p54spi_write32(struct p54s_priv *priv, u8 addr, __le32 val)
{
	p54spi_spi_write(priv, addr, &val, sizeof(val));
}

static int p54spi_wait_bit(struct p54s_priv *priv, u16 reg, u32 bits)
{
	int i;

	for (i = 0; i < 2000; i++) {
		u32 buffer = p54spi_read32(priv, reg);
		if ((buffer & bits) == bits)
			return 1;
	}
	return 0;
}

static int p54spi_spi_write_dma(struct p54s_priv *priv, __le32 base,
				const void *buf, size_t len)
{
	if (!p54spi_wait_bit(priv, SPI_ADRS_DMA_WRITE_CTRL, HOST_ALLOWED)) {
		dev_err(&priv->spi->dev, "spi_write_dma not allowed "
			"to DMA write.\n");
		return -EAGAIN;
	}

	p54spi_write16(priv, SPI_ADRS_DMA_WRITE_CTRL,
		       cpu_to_le16(SPI_DMA_WRITE_CTRL_ENABLE));

	p54spi_write16(priv, SPI_ADRS_DMA_WRITE_LEN, cpu_to_le16(len));
	p54spi_write32(priv, SPI_ADRS_DMA_WRITE_BASE, base);
	p54spi_spi_write(priv, SPI_ADRS_DMA_DATA, buf, len);
	return 0;
}

static int p54spi_request_firmware(struct ieee80211_hw *dev)
{
	struct p54s_priv *priv = dev->priv;
	int ret;

	/* FIXME: should driver use it's own struct device? */
	ret = request_firmware(&priv->firmware, "3826.arm", &priv->spi->dev);

	if (ret < 0) {
		dev_err(&priv->spi->dev, "request_firmware() failed: %d", ret);
		return ret;
	}

	ret = p54_parse_firmware(dev, priv->firmware);
	if (ret) {
		/* the firmware is released by the caller */
		return ret;
	}

	return 0;
}

static int p54spi_request_eeprom(struct ieee80211_hw *dev)
{
	struct p54s_priv *priv = dev->priv;
	const struct firmware *eeprom;
	int ret;

	/* allow users to customize their eeprom.
	 */

	ret = request_firmware_direct(&eeprom, "3826.eeprom", &priv->spi->dev);
	if (ret < 0) {
#ifdef CONFIG_P54_SPI_DEFAULT_EEPROM
		dev_info(&priv->spi->dev, "loading default eeprom...\n");
		ret = p54_parse_eeprom(dev, (void *) p54spi_eeprom,
				       sizeof(p54spi_eeprom));
#else
		dev_err(&priv->spi->dev, "Failed to request user eeprom\n");
#endif /* CONFIG_P54_SPI_DEFAULT_EEPROM */
	} else {
		dev_info(&priv->spi->dev, "loading user eeprom...\n");
		ret = p54_parse_eeprom(dev, (void *) eeprom->data,
				       (int)eeprom->size);
		release_firmware(eeprom);
	}
	return ret;
}

static int p54spi_upload_firmware(struct ieee80211_hw *dev)
{
	struct p54s_priv *priv = dev->priv;
	unsigned long fw_len, _fw_len;
	unsigned int offset = 0;
	int err = 0;
	u8 *fw;

	fw_len = priv->firmware->size;
	fw = kmemdup(priv->firmware->data, fw_len, GFP_KERNEL);
	if (!fw)
		return -ENOMEM;

	/* stop the device */
	p54spi_write16(priv, SPI_ADRS_DEV_CTRL_STAT, cpu_to_le16(
		       SPI_CTRL_STAT_HOST_OVERRIDE | SPI_CTRL_STAT_HOST_RESET |
		       SPI_CTRL_STAT_START_HALTED));

	msleep(TARGET_BOOT_SLEEP);

	p54spi_write16(priv, SPI_ADRS_DEV_CTRL_STAT, cpu_to_le16(
		       SPI_CTRL_STAT_HOST_OVERRIDE |
		       SPI_CTRL_STAT_START_HALTED));

	msleep(TARGET_BOOT_SLEEP);

	while (fw_len > 0) {
		_fw_len = min_t(long, fw_len, SPI_MAX_PACKET_SIZE);

		err = p54spi_spi_write_dma(priv, cpu_to_le32(
					   ISL38XX_DEV_FIRMWARE_ADDR + offset),
					   (fw + offset), _fw_len);
		if (err < 0)
			goto out;

		fw_len -= _fw_len;
		offset += _fw_len;
	}

	BUG_ON(fw_len != 0);

	/* enable host interrupts */
	p54spi_write32(priv, SPI_ADRS_HOST_INT_EN,
		       cpu_to_le32(SPI_HOST_INTS_DEFAULT));

	/* boot the device */
	p54spi_write16(priv, SPI_ADRS_DEV_CTRL_STAT, cpu_to_le16(
		       SPI_CTRL_STAT_HOST_OVERRIDE | SPI_CTRL_STAT_HOST_RESET |
		       SPI_CTRL_STAT_RAM_BOOT));

	msleep(TARGET_BOOT_SLEEP);

	p54spi_write16(priv, SPI_ADRS_DEV_CTRL_STAT, cpu_to_le16(
		       SPI_CTRL_STAT_HOST_OVERRIDE | SPI_CTRL_STAT_RAM_BOOT));
	msleep(TARGET_BOOT_SLEEP);

out:
	kfree(fw);
	return err;
}

static void p54spi_power_off(struct p54s_priv *priv)
{
	disable_irq(gpio_to_irq(p54spi_gpio_irq));
	gpio_set_value(p54spi_gpio_power, 0);
}

static void p54spi_power_on(struct p54s_priv *priv)
{
	gpio_set_value(p54spi_gpio_power, 1);
	enable_irq(gpio_to_irq(p54spi_gpio_irq));

	/* need to wait a while before device can be accessed, the length
	 * is just a guess
	 */
	msleep(10);
}

static inline void p54spi_int_ack(struct p54s_priv *priv, u32 val)
{
	p54spi_write32(priv, SPI_ADRS_HOST_INT_ACK, cpu_to_le32(val));
}

static int p54spi_wakeup(struct p54s_priv *priv)
{
	/* wake the chip */
	p54spi_write32(priv, SPI_ADRS_ARM_INTERRUPTS,
		       cpu_to_le32(SPI_TARGET_INT_WAKEUP));

	/* And wait for the READY interrupt */
	if (!p54spi_wait_bit(priv, SPI_ADRS_HOST_INTERRUPTS,
			     SPI_HOST_INT_READY)) {
		dev_err(&priv->spi->dev, "INT_READY timeout\n");
		return -EBUSY;
	}

	p54spi_int_ack(priv, SPI_HOST_INT_READY);
	return 0;
}

static inline void p54spi_sleep(struct p54s_priv *priv)
{
	p54spi_write32(priv, SPI_ADRS_ARM_INTERRUPTS,
		       cpu_to_le32(SPI_TARGET_INT_SLEEP));
}

static void p54spi_int_ready(struct p54s_priv *priv)
{
	p54spi_write32(priv, SPI_ADRS_HOST_INT_EN, cpu_to_le32(
		       SPI_HOST_INT_UPDATE | SPI_HOST_INT_SW_UPDATE));

	switch (priv->fw_state) {
	case FW_STATE_BOOTING:
		priv->fw_state = FW_STATE_READY;
		complete(&priv->fw_comp);
		break;
	case FW_STATE_RESETTING:
		priv->fw_state = FW_STATE_READY;
		/* TODO: reinitialize state */
		break;
	default:
		break;
	}
}

static int p54spi_rx(struct p54s_priv *priv)
{
	struct sk_buff *skb;
	u16 len;
	u16 rx_head[2];
#define READAHEAD_SZ (sizeof(rx_head)-sizeof(u16))

	if (p54spi_wakeup(priv) < 0)
		return -EBUSY;

	/* Read data size and first data word in one SPI transaction
	 * This is workaround for firmware/DMA bug,
	 * when first data word gets lost under high load.
	 */
	p54spi_spi_read(priv, SPI_ADRS_DMA_DATA, rx_head, sizeof(rx_head));
	len = rx_head[0];

	if (len == 0) {
		p54spi_sleep(priv);
		dev_err(&priv->spi->dev, "rx request of zero bytes\n");
		return 0;
	}

	/* Firmware may insert up to 4 padding bytes after the lmac header,
	 * but it does not amend the size of SPI data transfer.
	 * Such packets has correct data size in header, thus referencing
	 * past the end of allocated skb. Reserve extra 4 bytes for this case
	 */
	skb = dev_alloc_skb(len + 4);
	if (!skb) {
		p54spi_sleep(priv);
		dev_err(&priv->spi->dev, "could not alloc skb");
		return -ENOMEM;
	}

	if (len <= READAHEAD_SZ) {
		skb_put_data(skb, rx_head + 1, len);
	} else {
		skb_put_data(skb, rx_head + 1, READAHEAD_SZ);
		p54spi_spi_read(priv, SPI_ADRS_DMA_DATA,
				skb_put(skb, len - READAHEAD_SZ),
				len - READAHEAD_SZ);
	}
	p54spi_sleep(priv);
	/* Put additional bytes to compensate for the possible
	 * alignment-caused truncation
	 */
	skb_put(skb, 4);

	if (p54_rx(priv->hw, skb) == 0)
		dev_kfree_skb(skb);

	return 0;
}


static irqreturn_t p54spi_interrupt(int irq, void *config)
{
	struct spi_device *spi = config;
	struct p54s_priv *priv = spi_get_drvdata(spi);

	ieee80211_queue_work(priv->hw, &priv->work);

	return IRQ_HANDLED;
}

static int p54spi_tx_frame(struct p54s_priv *priv, struct sk_buff *skb)
{
	struct p54_hdr *hdr = (struct p54_hdr *) skb->data;
	int ret = 0;

	if (p54spi_wakeup(priv) < 0)
		return -EBUSY;

	ret = p54spi_spi_write_dma(priv, hdr->req_id, skb->data, skb->len);
	if (ret < 0)
		goto out;

	if (!p54spi_wait_bit(priv, SPI_ADRS_HOST_INTERRUPTS,
			     SPI_HOST_INT_WR_READY)) {
		dev_err(&priv->spi->dev, "WR_READY timeout\n");
		ret = -EAGAIN;
		goto out;
	}

	p54spi_int_ack(priv, SPI_HOST_INT_WR_READY);

	if (FREE_AFTER_TX(skb))
		p54_free_skb(priv->hw, skb);
out:
	p54spi_sleep(priv);
	return ret;
}

static int p54spi_wq_tx(struct p54s_priv *priv)
{
	struct p54s_tx_info *entry;
	struct sk_buff *skb;
	struct ieee80211_tx_info *info;
	struct p54_tx_info *minfo;
	struct p54s_tx_info *dinfo;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&priv->tx_lock, flags);

	while (!list_empty(&priv->tx_pending)) {
		entry = list_entry(priv->tx_pending.next,
				   struct p54s_tx_info, tx_list);

		list_del_init(&entry->tx_list);

		spin_unlock_irqrestore(&priv->tx_lock, flags);

		dinfo = container_of((void *) entry, struct p54s_tx_info,
				     tx_list);
		minfo = container_of((void *) dinfo, struct p54_tx_info,
				     data);
		info = container_of((void *) minfo, struct ieee80211_tx_info,
				    rate_driver_data);
		skb = container_of((void *) info, struct sk_buff, cb);

		ret = p54spi_tx_frame(priv, skb);

		if (ret < 0) {
			p54_free_skb(priv->hw, skb);
			return ret;
		}

		spin_lock_irqsave(&priv->tx_lock, flags);
	}
	spin_unlock_irqrestore(&priv->tx_lock, flags);
	return ret;
}

static void p54spi_op_tx(struct ieee80211_hw *dev, struct sk_buff *skb)
{
	struct p54s_priv *priv = dev->priv;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct p54_tx_info *mi = (struct p54_tx_info *) info->rate_driver_data;
	struct p54s_tx_info *di = (struct p54s_tx_info *) mi->data;
	unsigned long flags;

	BUILD_BUG_ON(sizeof(*di) > sizeof((mi->data)));

	spin_lock_irqsave(&priv->tx_lock, flags);
	list_add_tail(&di->tx_list, &priv->tx_pending);
	spin_unlock_irqrestore(&priv->tx_lock, flags);

	ieee80211_queue_work(priv->hw, &priv->work);
}

static void p54spi_work(struct work_struct *work)
{
	struct p54s_priv *priv = container_of(work, struct p54s_priv, work);
	u32 ints;
	int ret;

	mutex_lock(&priv->mutex);

	if (priv->fw_state == FW_STATE_OFF)
		goto out;

	ints = p54spi_read32(priv, SPI_ADRS_HOST_INTERRUPTS);

	if (ints & SPI_HOST_INT_READY) {
		p54spi_int_ready(priv);
		p54spi_int_ack(priv, SPI_HOST_INT_READY);
	}

	if (priv->fw_state != FW_STATE_READY)
		goto out;

	if (ints & SPI_HOST_INT_UPDATE) {
		p54spi_int_ack(priv, SPI_HOST_INT_UPDATE);
		ret = p54spi_rx(priv);
		if (ret < 0)
			goto out;
	}
	if (ints & SPI_HOST_INT_SW_UPDATE) {
		p54spi_int_ack(priv, SPI_HOST_INT_SW_UPDATE);
		ret = p54spi_rx(priv);
		if (ret < 0)
			goto out;
	}

	ret = p54spi_wq_tx(priv);
out:
	mutex_unlock(&priv->mutex);
}

static int p54spi_op_start(struct ieee80211_hw *dev)
{
	struct p54s_priv *priv = dev->priv;
	unsigned long timeout;
	int ret = 0;

	if (mutex_lock_interruptible(&priv->mutex)) {
		ret = -EINTR;
		goto out;
	}

	priv->fw_state = FW_STATE_BOOTING;

	p54spi_power_on(priv);

	ret = p54spi_upload_firmware(dev);
	if (ret < 0) {
		p54spi_power_off(priv);
		goto out_unlock;
	}

	mutex_unlock(&priv->mutex);

	timeout = msecs_to_jiffies(2000);
	timeout = wait_for_completion_interruptible_timeout(&priv->fw_comp,
							    timeout);
	if (!timeout) {
		dev_err(&priv->spi->dev, "firmware boot failed");
		p54spi_power_off(priv);
		ret = -1;
		goto out;
	}

	if (mutex_lock_interruptible(&priv->mutex)) {
		ret = -EINTR;
		p54spi_power_off(priv);
		goto out;
	}

	WARN_ON(priv->fw_state != FW_STATE_READY);

out_unlock:
	mutex_unlock(&priv->mutex);

out:
	return ret;
}

static void p54spi_op_stop(struct ieee80211_hw *dev)
{
	struct p54s_priv *priv = dev->priv;
	unsigned long flags;

	mutex_lock(&priv->mutex);
	WARN_ON(priv->fw_state != FW_STATE_READY);

	p54spi_power_off(priv);
	spin_lock_irqsave(&priv->tx_lock, flags);
	INIT_LIST_HEAD(&priv->tx_pending);
	spin_unlock_irqrestore(&priv->tx_lock, flags);

	priv->fw_state = FW_STATE_OFF;
	mutex_unlock(&priv->mutex);

	cancel_work_sync(&priv->work);
}

static int p54spi_probe(struct spi_device *spi)
{
	struct p54s_priv *priv = NULL;
	struct ieee80211_hw *hw;
	int ret = -EINVAL;

	hw = p54_init_common(sizeof(*priv));
	if (!hw) {
		dev_err(&spi->dev, "could not alloc ieee80211_hw");
		return -ENOMEM;
	}

	priv = hw->priv;
	priv->hw = hw;
	spi_set_drvdata(spi, priv);
	priv->spi = spi;

	spi->bits_per_word = 16;
	spi->max_speed_hz = 24000000;

	ret = spi_setup(spi);
	if (ret < 0) {
		dev_err(&priv->spi->dev, "spi_setup failed");
		goto err_free;
	}

	ret = gpio_request(p54spi_gpio_power, "p54spi power");
	if (ret < 0) {
		dev_err(&priv->spi->dev, "power GPIO request failed: %d", ret);
		goto err_free;
	}

	ret = gpio_request(p54spi_gpio_irq, "p54spi irq");
	if (ret < 0) {
		dev_err(&priv->spi->dev, "irq GPIO request failed: %d", ret);
		goto err_free_gpio_power;
	}

	gpio_direction_output(p54spi_gpio_power, 0);
	gpio_direction_input(p54spi_gpio_irq);

	ret = request_irq(gpio_to_irq(p54spi_gpio_irq),
			  p54spi_interrupt, 0, "p54spi",
			  priv->spi);
	if (ret < 0) {
		dev_err(&priv->spi->dev, "request_irq() failed");
		goto err_free_gpio_irq;
	}

	irq_set_irq_type(gpio_to_irq(p54spi_gpio_irq), IRQ_TYPE_EDGE_RISING);

	disable_irq(gpio_to_irq(p54spi_gpio_irq));

	INIT_WORK(&priv->work, p54spi_work);
	init_completion(&priv->fw_comp);
	INIT_LIST_HEAD(&priv->tx_pending);
	mutex_init(&priv->mutex);
	spin_lock_init(&priv->tx_lock);
	SET_IEEE80211_DEV(hw, &spi->dev);
	priv->common.open = p54spi_op_start;
	priv->common.stop = p54spi_op_stop;
	priv->common.tx = p54spi_op_tx;

	ret = p54spi_request_firmware(hw);
	if (ret < 0)
		goto err_free_common;

	ret = p54spi_request_eeprom(hw);
	if (ret)
		goto err_free_common;

	ret = p54_register_common(hw, &priv->spi->dev);
	if (ret)
		goto err_free_common;

	return 0;

err_free_common:
	release_firmware(priv->firmware);
	free_irq(gpio_to_irq(p54spi_gpio_irq), spi);
err_free_gpio_irq:
	gpio_free(p54spi_gpio_irq);
err_free_gpio_power:
	gpio_free(p54spi_gpio_power);
err_free:
	p54_free_common(priv->hw);
	return ret;
}

static void p54spi_remove(struct spi_device *spi)
{
	struct p54s_priv *priv = spi_get_drvdata(spi);

	p54_unregister_common(priv->hw);

	free_irq(gpio_to_irq(p54spi_gpio_irq), spi);

	gpio_free(p54spi_gpio_power);
	gpio_free(p54spi_gpio_irq);
	release_firmware(priv->firmware);

	mutex_destroy(&priv->mutex);

	p54_free_common(priv->hw);
}


static struct spi_driver p54spi_driver = {
	.driver = {
		.name		= "p54spi",
	},

	.probe		= p54spi_probe,
	.remove		= p54spi_remove,
};

module_spi_driver(p54spi_driver);

MODULE_DESCRIPTION("Prism54 SPI wireless driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Christian Lamparter <chunkeey@web.de>");
MODULE_ALIAS("spi:cx3110x");
MODULE_ALIAS("spi:p54spi");
MODULE_ALIAS("spi:stlc45xx");
