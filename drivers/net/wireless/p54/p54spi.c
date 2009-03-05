/*
 * Copyright (C) 2008 Christian Lamparter <chunkeey@web.de>
 * Copyright 2008       Johannes Berg <johannes@sipsolutions.net>
 *
 * This driver is a port from stlc45xx:
 *	Copyright (C) 2008 Nokia Corporation and/or its subsidiary(-ies).
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
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

#include "p54spi.h"
#include "p54spi_eeprom.h"
#include "p54.h"

#include "p54common.h"

MODULE_FIRMWARE("3826.arm");
MODULE_ALIAS("stlc45xx");

/*
 * gpios should be handled in board files and provided via platform data,
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
	t[1].len = len;
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

static u16 p54spi_read16(struct p54s_priv *priv, u8 addr)
{
	__le16 val;

	p54spi_spi_read(priv, addr, &val, sizeof(val));

	return le16_to_cpu(val);
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

struct p54spi_spi_reg {
	u16 address;		/* __le16 ? */
	u16 length;
	char *name;
};

static const struct p54spi_spi_reg p54spi_registers_array[] =
{
	{ SPI_ADRS_ARM_INTERRUPTS,	32, "ARM_INT     " },
	{ SPI_ADRS_ARM_INT_EN,		32, "ARM_INT_ENA " },
	{ SPI_ADRS_HOST_INTERRUPTS,	32, "HOST_INT    " },
	{ SPI_ADRS_HOST_INT_EN,		32, "HOST_INT_ENA" },
	{ SPI_ADRS_HOST_INT_ACK,	32, "HOST_INT_ACK" },
	{ SPI_ADRS_GEN_PURP_1,		32, "GP1_COMM    " },
	{ SPI_ADRS_GEN_PURP_2,		32, "GP2_COMM    " },
	{ SPI_ADRS_DEV_CTRL_STAT,	32, "DEV_CTRL_STA" },
	{ SPI_ADRS_DMA_DATA,		16, "DMA_DATA    " },
	{ SPI_ADRS_DMA_WRITE_CTRL,	16, "DMA_WR_CTRL " },
	{ SPI_ADRS_DMA_WRITE_LEN,	16, "DMA_WR_LEN  " },
	{ SPI_ADRS_DMA_WRITE_BASE,	32, "DMA_WR_BASE " },
	{ SPI_ADRS_DMA_READ_CTRL,	16, "DMA_RD_CTRL " },
	{ SPI_ADRS_DMA_READ_LEN,	16, "DMA_RD_LEN  " },
	{ SPI_ADRS_DMA_WRITE_BASE,	32, "DMA_RD_BASE " }
};

static int p54spi_wait_bit(struct p54s_priv *priv, u16 reg, __le32 bits)
{
	int i;
	__le32 buffer;

	for (i = 0; i < 2000; i++) {
		p54spi_spi_read(priv, reg, &buffer, sizeof(buffer));
		if (buffer == bits)
			return 1;

		msleep(1);
	}
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
		release_firmware(priv->firmware);
		return ret;
	}

	return 0;
}

static int p54spi_request_eeprom(struct ieee80211_hw *dev)
{
	struct p54s_priv *priv = dev->priv;
	const struct firmware *eeprom;
	int ret;

	/*
	 * allow users to customize their eeprom.
	 */

	ret = request_firmware(&eeprom, "3826.eeprom", &priv->spi->dev);
	if (ret < 0) {
		dev_info(&priv->spi->dev, "loading default eeprom...\n");
		ret = p54_parse_eeprom(dev, (void *) p54spi_eeprom,
				       sizeof(p54spi_eeprom));
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
	unsigned long fw_len, fw_addr;
	long _fw_len;

	/* stop the device */
	p54spi_write16(priv, SPI_ADRS_DEV_CTRL_STAT, cpu_to_le16(
		       SPI_CTRL_STAT_HOST_OVERRIDE | SPI_CTRL_STAT_HOST_RESET |
		       SPI_CTRL_STAT_START_HALTED));

	msleep(TARGET_BOOT_SLEEP);

	p54spi_write16(priv, SPI_ADRS_DEV_CTRL_STAT, cpu_to_le16(
		       SPI_CTRL_STAT_HOST_OVERRIDE |
		       SPI_CTRL_STAT_START_HALTED));

	msleep(TARGET_BOOT_SLEEP);

	fw_addr = ISL38XX_DEV_FIRMWARE_ADDR;
	fw_len = priv->firmware->size;

	while (fw_len > 0) {
		_fw_len = min_t(long, fw_len, SPI_MAX_PACKET_SIZE);

		p54spi_write16(priv, SPI_ADRS_DMA_WRITE_CTRL,
			       cpu_to_le16(SPI_DMA_WRITE_CTRL_ENABLE));

		if (p54spi_wait_bit(priv, SPI_ADRS_DMA_WRITE_CTRL,
				    cpu_to_le32(HOST_ALLOWED)) == 0) {
			dev_err(&priv->spi->dev, "fw_upload not allowed "
				"to DMA write.");
			return -EAGAIN;
		}

		p54spi_write16(priv, SPI_ADRS_DMA_WRITE_LEN,
			       cpu_to_le16(_fw_len));
		p54spi_write32(priv, SPI_ADRS_DMA_WRITE_BASE,
			       cpu_to_le32(fw_addr));

		p54spi_spi_write(priv, SPI_ADRS_DMA_DATA,
				 &priv->firmware->data, _fw_len);

		fw_len -= _fw_len;
		fw_addr += _fw_len;

		/* FIXME: I think this doesn't work if firmware is large,
		 * this loop goes to second round. fw->data is not
		 * increased at all! */
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
	return 0;
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

	/*
	 * need to wait a while before device can be accessed, the lenght
	 * is just a guess
	 */
	msleep(10);
}

static inline void p54spi_int_ack(struct p54s_priv *priv, u32 val)
{
	p54spi_write32(priv, SPI_ADRS_HOST_INT_ACK, cpu_to_le32(val));
}

static void p54spi_wakeup(struct p54s_priv *priv)
{
	unsigned long timeout;
	u32 ints;

	/* wake the chip */
	p54spi_write32(priv, SPI_ADRS_ARM_INTERRUPTS,
		       cpu_to_le32(SPI_TARGET_INT_WAKEUP));

	/* And wait for the READY interrupt */
	timeout = jiffies + HZ;

	ints =  p54spi_read32(priv, SPI_ADRS_HOST_INTERRUPTS);
	while (!(ints & SPI_HOST_INT_READY)) {
		if (time_after(jiffies, timeout))
				goto out;
		ints = p54spi_read32(priv, SPI_ADRS_HOST_INTERRUPTS);
	}

	p54spi_int_ack(priv, SPI_HOST_INT_READY);

out:
	return;
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

	p54spi_wakeup(priv);

	/* dummy read to flush SPI DMA controller bug */
	p54spi_read16(priv, SPI_ADRS_GEN_PURP_1);

	len = p54spi_read16(priv, SPI_ADRS_DMA_DATA);

	if (len == 0) {
		dev_err(&priv->spi->dev, "rx request of zero bytes");
		return 0;
	}

	skb = dev_alloc_skb(len);
	if (!skb) {
		dev_err(&priv->spi->dev, "could not alloc skb");
		return 0;
	}

	p54spi_spi_read(priv, SPI_ADRS_DMA_DATA, skb_put(skb, len), len);
	p54spi_sleep(priv);

	if (p54_rx(priv->hw, skb) == 0)
		dev_kfree_skb(skb);

	return 0;
}


static irqreturn_t p54spi_interrupt(int irq, void *config)
{
	struct spi_device *spi = config;
	struct p54s_priv *priv = dev_get_drvdata(&spi->dev);

	queue_work(priv->hw->workqueue, &priv->work);

	return IRQ_HANDLED;
}

static int p54spi_tx_frame(struct p54s_priv *priv, struct sk_buff *skb)
{
	struct p54_hdr *hdr = (struct p54_hdr *) skb->data;
	struct p54s_dma_regs dma_regs;
	unsigned long timeout;
	int ret = 0;
	u32 ints;

	p54spi_wakeup(priv);

	dma_regs.cmd = cpu_to_le16(SPI_DMA_WRITE_CTRL_ENABLE);
	dma_regs.len = cpu_to_le16(skb->len);
	dma_regs.addr = hdr->req_id;

	p54spi_spi_write(priv, SPI_ADRS_DMA_WRITE_CTRL, &dma_regs,
			   sizeof(dma_regs));

	p54spi_spi_write(priv, SPI_ADRS_DMA_DATA, skb->data, skb->len);

	timeout = jiffies + 2 * HZ;
	ints = p54spi_read32(priv, SPI_ADRS_HOST_INTERRUPTS);
	while (!(ints & SPI_HOST_INT_WR_READY)) {
		if (time_after(jiffies, timeout)) {
			dev_err(&priv->spi->dev, "WR_READY timeout");
			ret = -1;
			goto out;
		}
		ints = p54spi_read32(priv, SPI_ADRS_HOST_INTERRUPTS);
	}

	p54spi_int_ack(priv, SPI_HOST_INT_WR_READY);
	p54spi_sleep(priv);

out:
	if (FREE_AFTER_TX(skb))
		p54_free_skb(priv->hw, skb);
	return ret;
}

static int p54spi_wq_tx(struct p54s_priv *priv)
{
	struct p54s_tx_info *entry;
	struct sk_buff *skb;
	struct ieee80211_tx_info *info;
	struct p54_tx_info *minfo;
	struct p54s_tx_info *dinfo;
	int ret = 0;

	spin_lock_bh(&priv->tx_lock);

	while (!list_empty(&priv->tx_pending)) {
		entry = list_entry(priv->tx_pending.next,
				   struct p54s_tx_info, tx_list);

		list_del_init(&entry->tx_list);

		spin_unlock_bh(&priv->tx_lock);

		dinfo = container_of((void *) entry, struct p54s_tx_info,
				     tx_list);
		minfo = container_of((void *) dinfo, struct p54_tx_info,
				     data);
		info = container_of((void *) minfo, struct ieee80211_tx_info,
				    rate_driver_data);
		skb = container_of((void *) info, struct sk_buff, cb);

		ret = p54spi_tx_frame(priv, skb);

		spin_lock_bh(&priv->tx_lock);

		if (ret < 0) {
			p54_free_skb(priv->hw, skb);
			goto out;
		}
	}

out:
	spin_unlock_bh(&priv->tx_lock);
	return ret;
}

static void p54spi_op_tx(struct ieee80211_hw *dev, struct sk_buff *skb)
{
	struct p54s_priv *priv = dev->priv;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct p54_tx_info *mi = (struct p54_tx_info *) info->rate_driver_data;
	struct p54s_tx_info *di = (struct p54s_tx_info *) mi->data;

	BUILD_BUG_ON(sizeof(*di) > sizeof((mi->data)));

	spin_lock_bh(&priv->tx_lock);
	list_add_tail(&di->tx_list, &priv->tx_pending);
	spin_unlock_bh(&priv->tx_lock);

	queue_work(priv->hw->workqueue, &priv->work);
}

static void p54spi_work(struct work_struct *work)
{
	struct p54s_priv *priv = container_of(work, struct p54s_priv, work);
	u32 ints;
	int ret;

	mutex_lock(&priv->mutex);

	if (priv->fw_state == FW_STATE_OFF &&
	    priv->fw_state == FW_STATE_RESET)
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
	if (ret < 0)
		goto out;

	ints = p54spi_read32(priv, SPI_ADRS_HOST_INTERRUPTS);

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

	if (mutex_lock_interruptible(&priv->mutex)) {
		/* FIXME: how to handle this error? */
		return;
	}

	WARN_ON(priv->fw_state != FW_STATE_READY);

	cancel_work_sync(&priv->work);

	p54spi_power_off(priv);
	spin_lock_bh(&priv->tx_lock);
	INIT_LIST_HEAD(&priv->tx_pending);
	spin_unlock_bh(&priv->tx_lock);

	priv->fw_state = FW_STATE_OFF;
	mutex_unlock(&priv->mutex);
}

static int __devinit p54spi_probe(struct spi_device *spi)
{
	struct p54s_priv *priv = NULL;
	struct ieee80211_hw *hw;
	int ret = -EINVAL;

	hw = p54_init_common(sizeof(*priv));
	if (!hw) {
		dev_err(&priv->spi->dev, "could not alloc ieee80211_hw");
		return -ENOMEM;
	}

	priv = hw->priv;
	priv->hw = hw;
	dev_set_drvdata(&spi->dev, priv);
	priv->spi = spi;

	spi->bits_per_word = 16;
	spi->max_speed_hz = 24000000;

	ret = spi_setup(spi);
	if (ret < 0) {
		dev_err(&priv->spi->dev, "spi_setup failed");
		goto err_free_common;
	}

	ret = gpio_request(p54spi_gpio_power, "p54spi power");
	if (ret < 0) {
		dev_err(&priv->spi->dev, "power GPIO request failed: %d", ret);
		goto err_free_common;
	}

	ret = gpio_request(p54spi_gpio_irq, "p54spi irq");
	if (ret < 0) {
		dev_err(&priv->spi->dev, "irq GPIO request failed: %d", ret);
		goto err_free_common;
	}

	gpio_direction_output(p54spi_gpio_power, 0);
	gpio_direction_input(p54spi_gpio_irq);

	ret = request_irq(gpio_to_irq(p54spi_gpio_irq),
			  p54spi_interrupt, IRQF_DISABLED, "p54spi",
			  priv->spi);
	if (ret < 0) {
		dev_err(&priv->spi->dev, "request_irq() failed");
		goto err_free_common;
	}

	set_irq_type(gpio_to_irq(p54spi_gpio_irq),
		     IRQ_TYPE_EDGE_RISING);

	disable_irq(gpio_to_irq(p54spi_gpio_irq));

	INIT_WORK(&priv->work, p54spi_work);
	init_completion(&priv->fw_comp);
	INIT_LIST_HEAD(&priv->tx_pending);
	mutex_init(&priv->mutex);
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
	p54_free_common(priv->hw);
	return ret;
}

static int __devexit p54spi_remove(struct spi_device *spi)
{
	struct p54s_priv *priv = dev_get_drvdata(&spi->dev);

	ieee80211_unregister_hw(priv->hw);

	free_irq(gpio_to_irq(p54spi_gpio_irq), spi);

	gpio_free(p54spi_gpio_power);
	gpio_free(p54spi_gpio_irq);
	release_firmware(priv->firmware);

	mutex_destroy(&priv->mutex);

	p54_free_common(priv->hw);
	ieee80211_free_hw(priv->hw);

	return 0;
}


static struct spi_driver p54spi_driver = {
	.driver = {
		/* use cx3110x name because board-n800.c uses that for the
		 * SPI port */
		.name		= "cx3110x",
		.bus		= &spi_bus_type,
		.owner		= THIS_MODULE,
	},

	.probe		= p54spi_probe,
	.remove		= __devexit_p(p54spi_remove),
};

static int __init p54spi_init(void)
{
	int ret;

	ret = spi_register_driver(&p54spi_driver);
	if (ret < 0) {
		printk(KERN_ERR "failed to register SPI driver: %d", ret);
		goto out;
	}

out:
	return ret;
}

static void __exit p54spi_exit(void)
{
	spi_unregister_driver(&p54spi_driver);
}

module_init(p54spi_init);
module_exit(p54spi_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Christian Lamparter <chunkeey@web.de>");
