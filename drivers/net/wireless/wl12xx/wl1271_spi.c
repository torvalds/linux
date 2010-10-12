/*
 * This file is part of wl1271
 *
 * Copyright (C) 2008-2009 Nokia Corporation
 *
 * Contact: Luciano Coelho <luciano.coelho@nokia.com>
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
 *
 */

#include <linux/irq.h>
#include <linux/module.h>
#include <linux/crc7.h>
#include <linux/spi/spi.h>
#include <linux/spi/wl12xx.h>
#include <linux/slab.h>

#include "wl1271.h"
#include "wl12xx_80211.h"
#include "wl1271_io.h"

#include "wl1271_reg.h"

#define WSPI_CMD_READ                 0x40000000
#define WSPI_CMD_WRITE                0x00000000
#define WSPI_CMD_FIXED                0x20000000
#define WSPI_CMD_BYTE_LENGTH          0x1FFE0000
#define WSPI_CMD_BYTE_LENGTH_OFFSET   17
#define WSPI_CMD_BYTE_ADDR            0x0001FFFF

#define WSPI_INIT_CMD_CRC_LEN       5

#define WSPI_INIT_CMD_START         0x00
#define WSPI_INIT_CMD_TX            0x40
/* the extra bypass bit is sampled by the TNET as '1' */
#define WSPI_INIT_CMD_BYPASS_BIT    0x80
#define WSPI_INIT_CMD_FIXEDBUSY_LEN 0x07
#define WSPI_INIT_CMD_EN_FIXEDBUSY  0x80
#define WSPI_INIT_CMD_DIS_FIXEDBUSY 0x00
#define WSPI_INIT_CMD_IOD           0x40
#define WSPI_INIT_CMD_IP            0x20
#define WSPI_INIT_CMD_CS            0x10
#define WSPI_INIT_CMD_WS            0x08
#define WSPI_INIT_CMD_WSPI          0x01
#define WSPI_INIT_CMD_END           0x01

#define WSPI_INIT_CMD_LEN           8

#define HW_ACCESS_WSPI_FIXED_BUSY_LEN \
		((WL1271_BUSY_WORD_LEN - 4) / sizeof(u32))
#define HW_ACCESS_WSPI_INIT_CMD_MASK  0

static inline struct spi_device *wl_to_spi(struct wl1271 *wl)
{
	return wl->if_priv;
}

static struct device *wl1271_spi_wl_to_dev(struct wl1271 *wl)
{
	return &(wl_to_spi(wl)->dev);
}

static void wl1271_spi_disable_interrupts(struct wl1271 *wl)
{
	disable_irq(wl->irq);
}

static void wl1271_spi_enable_interrupts(struct wl1271 *wl)
{
	enable_irq(wl->irq);
}

static void wl1271_spi_reset(struct wl1271 *wl)
{
	u8 *cmd;
	struct spi_transfer t;
	struct spi_message m;

	cmd = kzalloc(WSPI_INIT_CMD_LEN, GFP_KERNEL);
	if (!cmd) {
		wl1271_error("could not allocate cmd for spi reset");
		return;
	}

	memset(&t, 0, sizeof(t));
	spi_message_init(&m);

	memset(cmd, 0xff, WSPI_INIT_CMD_LEN);

	t.tx_buf = cmd;
	t.len = WSPI_INIT_CMD_LEN;
	spi_message_add_tail(&t, &m);

	spi_sync(wl_to_spi(wl), &m);
	kfree(cmd);

	wl1271_dump(DEBUG_SPI, "spi reset -> ", cmd, WSPI_INIT_CMD_LEN);
}

static void wl1271_spi_init(struct wl1271 *wl)
{
	u8 crc[WSPI_INIT_CMD_CRC_LEN], *cmd;
	struct spi_transfer t;
	struct spi_message m;

	cmd = kzalloc(WSPI_INIT_CMD_LEN, GFP_KERNEL);
	if (!cmd) {
		wl1271_error("could not allocate cmd for spi init");
		return;
	}

	memset(crc, 0, sizeof(crc));
	memset(&t, 0, sizeof(t));
	spi_message_init(&m);

	/*
	 * Set WSPI_INIT_COMMAND
	 * the data is being send from the MSB to LSB
	 */
	cmd[2] = 0xff;
	cmd[3] = 0xff;
	cmd[1] = WSPI_INIT_CMD_START | WSPI_INIT_CMD_TX;
	cmd[0] = 0;
	cmd[7] = 0;
	cmd[6] |= HW_ACCESS_WSPI_INIT_CMD_MASK << 3;
	cmd[6] |= HW_ACCESS_WSPI_FIXED_BUSY_LEN & WSPI_INIT_CMD_FIXEDBUSY_LEN;

	if (HW_ACCESS_WSPI_FIXED_BUSY_LEN == 0)
		cmd[5] |=  WSPI_INIT_CMD_DIS_FIXEDBUSY;
	else
		cmd[5] |= WSPI_INIT_CMD_EN_FIXEDBUSY;

	cmd[5] |= WSPI_INIT_CMD_IOD | WSPI_INIT_CMD_IP | WSPI_INIT_CMD_CS
		| WSPI_INIT_CMD_WSPI | WSPI_INIT_CMD_WS;

	crc[0] = cmd[1];
	crc[1] = cmd[0];
	crc[2] = cmd[7];
	crc[3] = cmd[6];
	crc[4] = cmd[5];

	cmd[4] |= crc7(0, crc, WSPI_INIT_CMD_CRC_LEN) << 1;
	cmd[4] |= WSPI_INIT_CMD_END;

	t.tx_buf = cmd;
	t.len = WSPI_INIT_CMD_LEN;
	spi_message_add_tail(&t, &m);

	spi_sync(wl_to_spi(wl), &m);
	wl1271_dump(DEBUG_SPI, "spi init -> ", cmd, WSPI_INIT_CMD_LEN);
	kfree(cmd);
}

#define WL1271_BUSY_WORD_TIMEOUT 1000

static int wl1271_spi_read_busy(struct wl1271 *wl)
{
	struct spi_transfer t[1];
	struct spi_message m;
	u32 *busy_buf;
	int num_busy_bytes = 0;

	/*
	 * Read further busy words from SPI until a non-busy word is
	 * encountered, then read the data itself into the buffer.
	 */

	num_busy_bytes = WL1271_BUSY_WORD_TIMEOUT;
	busy_buf = wl->buffer_busyword;
	while (num_busy_bytes) {
		num_busy_bytes--;
		spi_message_init(&m);
		memset(t, 0, sizeof(t));
		t[0].rx_buf = busy_buf;
		t[0].len = sizeof(u32);
		t[0].cs_change = true;
		spi_message_add_tail(&t[0], &m);
		spi_sync(wl_to_spi(wl), &m);

		if (*busy_buf & 0x1)
			return 0;
	}

	/* The SPI bus is unresponsive, the read failed. */
	wl1271_error("SPI read busy-word timeout!\n");
	return -ETIMEDOUT;
}

static void wl1271_spi_raw_read(struct wl1271 *wl, int addr, void *buf,
				size_t len, bool fixed)
{
	struct spi_transfer t[3];
	struct spi_message m;
	u32 *busy_buf;
	u32 *cmd;

	cmd = &wl->buffer_cmd;
	busy_buf = wl->buffer_busyword;

	*cmd = 0;
	*cmd |= WSPI_CMD_READ;
	*cmd |= (len << WSPI_CMD_BYTE_LENGTH_OFFSET) & WSPI_CMD_BYTE_LENGTH;
	*cmd |= addr & WSPI_CMD_BYTE_ADDR;

	if (fixed)
		*cmd |= WSPI_CMD_FIXED;

	spi_message_init(&m);
	memset(t, 0, sizeof(t));

	t[0].tx_buf = cmd;
	t[0].len = 4;
	t[0].cs_change = true;
	spi_message_add_tail(&t[0], &m);

	/* Busy and non busy words read */
	t[1].rx_buf = busy_buf;
	t[1].len = WL1271_BUSY_WORD_LEN;
	t[1].cs_change = true;
	spi_message_add_tail(&t[1], &m);

	spi_sync(wl_to_spi(wl), &m);

	if (!(busy_buf[WL1271_BUSY_WORD_CNT - 1] & 0x1) &&
	    wl1271_spi_read_busy(wl)) {
		memset(buf, 0, len);
		return;
	}

	spi_message_init(&m);
	memset(t, 0, sizeof(t));

	t[0].rx_buf = buf;
	t[0].len = len;
	t[0].cs_change = true;
	spi_message_add_tail(&t[0], &m);

	spi_sync(wl_to_spi(wl), &m);

	wl1271_dump(DEBUG_SPI, "spi_read cmd -> ", cmd, sizeof(*cmd));
	wl1271_dump(DEBUG_SPI, "spi_read buf <- ", buf, len);
}

static void wl1271_spi_raw_write(struct wl1271 *wl, int addr, void *buf,
			  size_t len, bool fixed)
{
	struct spi_transfer t[2];
	struct spi_message m;
	u32 *cmd;

	cmd = &wl->buffer_cmd;

	*cmd = 0;
	*cmd |= WSPI_CMD_WRITE;
	*cmd |= (len << WSPI_CMD_BYTE_LENGTH_OFFSET) & WSPI_CMD_BYTE_LENGTH;
	*cmd |= addr & WSPI_CMD_BYTE_ADDR;

	if (fixed)
		*cmd |= WSPI_CMD_FIXED;

	spi_message_init(&m);
	memset(t, 0, sizeof(t));

	t[0].tx_buf = cmd;
	t[0].len = sizeof(*cmd);
	spi_message_add_tail(&t[0], &m);

	t[1].tx_buf = buf;
	t[1].len = len;
	spi_message_add_tail(&t[1], &m);

	spi_sync(wl_to_spi(wl), &m);

	wl1271_dump(DEBUG_SPI, "spi_write cmd -> ", cmd, sizeof(*cmd));
	wl1271_dump(DEBUG_SPI, "spi_write buf -> ", buf, len);
}

static irqreturn_t wl1271_irq(int irq, void *cookie)
{
	struct wl1271 *wl;
	unsigned long flags;

	wl1271_debug(DEBUG_IRQ, "IRQ");

	wl = cookie;

	/* complete the ELP completion */
	spin_lock_irqsave(&wl->wl_lock, flags);
	if (wl->elp_compl) {
		complete(wl->elp_compl);
		wl->elp_compl = NULL;
	}

	if (!test_and_set_bit(WL1271_FLAG_IRQ_RUNNING, &wl->flags))
		ieee80211_queue_work(wl->hw, &wl->irq_work);
	set_bit(WL1271_FLAG_IRQ_PENDING, &wl->flags);
	spin_unlock_irqrestore(&wl->wl_lock, flags);

	return IRQ_HANDLED;
}

static void wl1271_spi_set_power(struct wl1271 *wl, bool enable)
{
	if (wl->set_power)
		wl->set_power(enable);
}

static struct wl1271_if_operations spi_ops = {
	.read		= wl1271_spi_raw_read,
	.write		= wl1271_spi_raw_write,
	.reset		= wl1271_spi_reset,
	.init		= wl1271_spi_init,
	.power		= wl1271_spi_set_power,
	.dev		= wl1271_spi_wl_to_dev,
	.enable_irq	= wl1271_spi_enable_interrupts,
	.disable_irq	= wl1271_spi_disable_interrupts
};

static int __devinit wl1271_probe(struct spi_device *spi)
{
	struct wl12xx_platform_data *pdata;
	struct ieee80211_hw *hw;
	struct wl1271 *wl;
	int ret;

	pdata = spi->dev.platform_data;
	if (!pdata) {
		wl1271_error("no platform data");
		return -ENODEV;
	}

	hw = wl1271_alloc_hw();
	if (IS_ERR(hw))
		return PTR_ERR(hw);

	wl = hw->priv;

	dev_set_drvdata(&spi->dev, wl);
	wl->if_priv = spi;

	wl->if_ops = &spi_ops;

	/* This is the only SPI value that we need to set here, the rest
	 * comes from the board-peripherals file */
	spi->bits_per_word = 32;

	ret = spi_setup(spi);
	if (ret < 0) {
		wl1271_error("spi_setup failed");
		goto out_free;
	}

	wl->set_power = pdata->set_power;
	if (!wl->set_power) {
		wl1271_error("set power function missing in platform data");
		ret = -ENODEV;
		goto out_free;
	}

	wl->irq = spi->irq;
	if (wl->irq < 0) {
		wl1271_error("irq missing in platform data");
		ret = -ENODEV;
		goto out_free;
	}

	ret = request_irq(wl->irq, wl1271_irq, 0, DRIVER_NAME, wl);
	if (ret < 0) {
		wl1271_error("request_irq() failed: %d", ret);
		goto out_free;
	}

	set_irq_type(wl->irq, IRQ_TYPE_EDGE_RISING);

	disable_irq(wl->irq);

	ret = wl1271_init_ieee80211(wl);
	if (ret)
		goto out_irq;

	ret = wl1271_register_hw(wl);
	if (ret)
		goto out_irq;

	wl1271_notice("initialized");

	return 0;

 out_irq:
	free_irq(wl->irq, wl);

 out_free:
	wl1271_free_hw(wl);

	return ret;
}

static int __devexit wl1271_remove(struct spi_device *spi)
{
	struct wl1271 *wl = dev_get_drvdata(&spi->dev);

	free_irq(wl->irq, wl);

	wl1271_unregister_hw(wl);
	wl1271_free_hw(wl);

	return 0;
}


static struct spi_driver wl1271_spi_driver = {
	.driver = {
		.name		= "wl1271_spi",
		.bus		= &spi_bus_type,
		.owner		= THIS_MODULE,
	},

	.probe		= wl1271_probe,
	.remove		= __devexit_p(wl1271_remove),
};

static int __init wl1271_init(void)
{
	int ret;

	ret = spi_register_driver(&wl1271_spi_driver);
	if (ret < 0) {
		wl1271_error("failed to register spi driver: %d", ret);
		goto out;
	}

out:
	return ret;
}

static void __exit wl1271_exit(void)
{
	spi_unregister_driver(&wl1271_spi_driver);

	wl1271_notice("unloaded");
}

module_init(wl1271_init);
module_exit(wl1271_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Luciano Coelho <luciano.coelho@nokia.com>");
MODULE_AUTHOR("Juuso Oikarinen <juuso.oikarinen@nokia.com>");
MODULE_FIRMWARE(WL1271_FW_NAME);
MODULE_ALIAS("spi:wl1271");
