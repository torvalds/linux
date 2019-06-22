/*
 * spi-mt7621.c -- MediaTek MT7621 SPI controller driver
 *
 * Copyright (C) 2011 Sergiy <piratfm@gmail.com>
 * Copyright (C) 2011-2013 Gabor Juhos <juhosg@openwrt.org>
 * Copyright (C) 2014-2015 Felix Fietkau <nbd@nbd.name>
 *
 * Some parts are based on spi-orion.c:
 *   Author: Shadi Ammouri <shadi@marvell.com>
 *   Copyright (C) 2007-2008 Marvell Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/reset.h>
#include <linux/spi/spi.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/swab.h>

#include <ralink_regs.h>

#define SPI_BPW_MASK(bits) BIT((bits) - 1)

#define DRIVER_NAME			"spi-mt7621"
/* in usec */
#define RALINK_SPI_WAIT_MAX_LOOP	2000

/* SPISTAT register bit field */
#define SPISTAT_BUSY			BIT(0)

#define MT7621_SPI_TRANS	0x00
#define SPITRANS_BUSY		BIT(16)

#define MT7621_SPI_OPCODE	0x04
#define MT7621_SPI_DATA0	0x08
#define MT7621_SPI_DATA4	0x18
#define SPI_CTL_TX_RX_CNT_MASK	0xff
#define SPI_CTL_START		BIT(8)

#define MT7621_SPI_POLAR	0x38
#define MT7621_SPI_MASTER	0x28
#define MT7621_SPI_MOREBUF	0x2c
#define MT7621_SPI_SPACE	0x3c

#define MT7621_CPHA		BIT(5)
#define MT7621_CPOL		BIT(4)
#define MT7621_LSB_FIRST	BIT(3)

#define RT2880_SPI_MODE_BITS	(SPI_CPOL | SPI_CPHA |		\
				 SPI_LSB_FIRST | SPI_CS_HIGH)

struct mt7621_spi;

struct mt7621_spi {
	struct spi_master	*master;
	void __iomem		*base;
	unsigned int		sys_freq;
	unsigned int		speed;
	struct clk		*clk;
	int			pending_write;

	struct mt7621_spi_ops	*ops;
};

static inline struct mt7621_spi *spidev_to_mt7621_spi(struct spi_device *spi)
{
	return spi_master_get_devdata(spi->master);
}

static inline u32 mt7621_spi_read(struct mt7621_spi *rs, u32 reg)
{
	return ioread32(rs->base + reg);
}

static inline void mt7621_spi_write(struct mt7621_spi *rs, u32 reg, u32 val)
{
	iowrite32(val, rs->base + reg);
}

static void mt7621_spi_reset(struct mt7621_spi *rs, int duplex)
{
	u32 master = mt7621_spi_read(rs, MT7621_SPI_MASTER);

	master |= 7 << 29;
	master |= 1 << 2;
	if (duplex)
		master |= 1 << 10;
	else
		master &= ~(1 << 10);

	mt7621_spi_write(rs, MT7621_SPI_MASTER, master);
	rs->pending_write = 0;
}

static void mt7621_spi_set_cs(struct spi_device *spi, int enable)
{
	struct mt7621_spi *rs = spidev_to_mt7621_spi(spi);
	int cs = spi->chip_select;
	u32 polar = 0;

	mt7621_spi_reset(rs, cs);
	if (enable)
		polar = BIT(cs);
	mt7621_spi_write(rs, MT7621_SPI_POLAR, polar);
}

static int mt7621_spi_prepare(struct spi_device *spi, unsigned int speed)
{
	struct mt7621_spi *rs = spidev_to_mt7621_spi(spi);
	u32 rate;
	u32 reg;

	dev_dbg(&spi->dev, "speed:%u\n", speed);

	rate = DIV_ROUND_UP(rs->sys_freq, speed);
	dev_dbg(&spi->dev, "rate-1:%u\n", rate);

	if (rate > 4097)
		return -EINVAL;

	if (rate < 2)
		rate = 2;

	reg = mt7621_spi_read(rs, MT7621_SPI_MASTER);
	reg &= ~(0xfff << 16);
	reg |= (rate - 2) << 16;
	rs->speed = speed;

	reg &= ~MT7621_LSB_FIRST;
	if (spi->mode & SPI_LSB_FIRST)
		reg |= MT7621_LSB_FIRST;

	reg &= ~(MT7621_CPHA | MT7621_CPOL);
	switch (spi->mode & (SPI_CPOL | SPI_CPHA)) {
	case SPI_MODE_0:
		break;
	case SPI_MODE_1:
		reg |= MT7621_CPHA;
		break;
	case SPI_MODE_2:
		reg |= MT7621_CPOL;
		break;
	case SPI_MODE_3:
		reg |= MT7621_CPOL | MT7621_CPHA;
		break;
	}
	mt7621_spi_write(rs, MT7621_SPI_MASTER, reg);

	return 0;
}

static inline int mt7621_spi_wait_till_ready(struct mt7621_spi *rs)
{
	int i;

	for (i = 0; i < RALINK_SPI_WAIT_MAX_LOOP; i++) {
		u32 status;

		status = mt7621_spi_read(rs, MT7621_SPI_TRANS);
		if ((status & SPITRANS_BUSY) == 0)
			return 0;
		cpu_relax();
		udelay(1);
	}

	return -ETIMEDOUT;
}

static void mt7621_spi_read_half_duplex(struct mt7621_spi *rs,
					int rx_len, u8 *buf)
{
	/* Combine with any pending write, and perform one or
	 * more half-duplex transactions reading 'len' bytes.
	 * Data to be written is already in MT7621_SPI_DATA*
	 */
	int tx_len = rs->pending_write;

	rs->pending_write = 0;

	while (rx_len || tx_len) {
		int i;
		u32 val = (min(tx_len, 4) * 8) << 24;
		int rx = min(rx_len, 32);

		if (tx_len > 4)
			val |= (tx_len - 4) * 8;
		val |= (rx * 8) << 12;
		mt7621_spi_write(rs, MT7621_SPI_MOREBUF, val);

		tx_len = 0;

		val = mt7621_spi_read(rs, MT7621_SPI_TRANS);
		val |= SPI_CTL_START;
		mt7621_spi_write(rs, MT7621_SPI_TRANS, val);

		mt7621_spi_wait_till_ready(rs);

		for (i = 0; i < rx; i++) {
			if ((i % 4) == 0)
				val = mt7621_spi_read(rs, MT7621_SPI_DATA0 + i);
			*buf++ = val & 0xff;
			val >>= 8;
		}
		rx_len -= i;
	}
}

static inline void mt7621_spi_flush(struct mt7621_spi *rs)
{
	mt7621_spi_read_half_duplex(rs, 0, NULL);
}

static void mt7621_spi_write_half_duplex(struct mt7621_spi *rs,
					 int tx_len, const u8 *buf)
{
	int val = 0;
	int len = rs->pending_write;

	if (len & 3) {
		val = mt7621_spi_read(rs, MT7621_SPI_OPCODE + (len & ~3));
		if (len < 4) {
			val <<= (4 - len) * 8;
			val = swab32(val);
		}
	}

	while (tx_len > 0) {
		if (len >= 36) {
			rs->pending_write = len;
			mt7621_spi_flush(rs);
			len = 0;
		}

		val |= *buf++ << (8 * (len & 3));
		len++;
		if ((len & 3) == 0) {
			if (len == 4)
				/* The byte-order of the opcode is weird! */
				val = swab32(val);
			mt7621_spi_write(rs, MT7621_SPI_OPCODE + len - 4, val);
			val = 0;
		}
		tx_len -= 1;
	}
	if (len & 3) {
		if (len < 4) {
			val = swab32(val);
			val >>= (4 - len) * 8;
		}
		mt7621_spi_write(rs, MT7621_SPI_OPCODE + (len & ~3), val);
	}
	rs->pending_write = len;
}

static int mt7621_spi_transfer_half_duplex(struct spi_master *master,
					   struct spi_message *m)
{
	struct mt7621_spi *rs = spi_master_get_devdata(master);
	struct spi_device *spi = m->spi;
	unsigned int speed = spi->max_speed_hz;
	struct spi_transfer *t = NULL;
	int status = 0;

	mt7621_spi_wait_till_ready(rs);

	list_for_each_entry(t, &m->transfers, transfer_list)
		if (t->speed_hz < speed)
			speed = t->speed_hz;

	if (mt7621_spi_prepare(spi, speed)) {
		status = -EIO;
		goto msg_done;
	}

	mt7621_spi_set_cs(spi, 1);
	m->actual_length = 0;
	list_for_each_entry(t, &m->transfers, transfer_list) {
		if (t->rx_buf)
			mt7621_spi_read_half_duplex(rs, t->len, t->rx_buf);
		else if (t->tx_buf)
			mt7621_spi_write_half_duplex(rs, t->len, t->tx_buf);
		m->actual_length += t->len;
	}
	mt7621_spi_flush(rs);

	mt7621_spi_set_cs(spi, 0);
msg_done:
	m->status = status;
	spi_finalize_current_message(master);

	return 0;
}

static int mt7621_spi_transfer_full_duplex(struct spi_master *master,
					   struct spi_message *m)
{
	struct mt7621_spi *rs = spi_master_get_devdata(master);
	struct spi_device *spi = m->spi;
	unsigned int speed = spi->max_speed_hz;
	struct spi_transfer *t = NULL;
	int status = 0;
	int i, len = 0;
	int rx_len = 0;
	u32 data[9] = { 0 };
	u32 val = 0;

	mt7621_spi_wait_till_ready(rs);

	list_for_each_entry(t, &m->transfers, transfer_list) {
		const u8 *buf = t->tx_buf;

		if (t->rx_buf)
			rx_len += t->len;

		if (!buf)
			continue;

		if (WARN_ON(len + t->len > 16)) {
			status = -EIO;
			goto msg_done;
		}

		for (i = 0; i < t->len; i++, len++)
			data[len / 4] |= buf[i] << (8 * (len & 3));
		if (speed > t->speed_hz)
			speed = t->speed_hz;
	}

	if (WARN_ON(rx_len > 16)) {
		status = -EIO;
		goto msg_done;
	}

	if (mt7621_spi_prepare(spi, speed)) {
		status = -EIO;
		goto msg_done;
	}

	for (i = 0; i < len; i += 4)
		mt7621_spi_write(rs, MT7621_SPI_DATA0 + i, data[i / 4]);

	val |= len * 8;
	val |= (rx_len * 8) << 12;
	mt7621_spi_write(rs, MT7621_SPI_MOREBUF, val);

	mt7621_spi_set_cs(spi, 1);

	val = mt7621_spi_read(rs, MT7621_SPI_TRANS);
	val |= SPI_CTL_START;
	mt7621_spi_write(rs, MT7621_SPI_TRANS, val);

	mt7621_spi_wait_till_ready(rs);

	mt7621_spi_set_cs(spi, 0);

	for (i = 0; i < rx_len; i += 4)
		data[i / 4] = mt7621_spi_read(rs, MT7621_SPI_DATA4 + i);

	m->actual_length = rx_len;

	len = 0;
	list_for_each_entry(t, &m->transfers, transfer_list) {
		u8 *buf = t->rx_buf;

		if (!buf)
			continue;

		for (i = 0; i < t->len; i++, len++)
			buf[i] = data[len / 4] >> (8 * (len & 3));
	}

msg_done:
	m->status = status;
	spi_finalize_current_message(master);

	return 0;
}

static int mt7621_spi_transfer_one_message(struct spi_master *master,
					   struct spi_message *m)
{
	struct spi_device *spi = m->spi;
	int cs = spi->chip_select;

	if (cs)
		return mt7621_spi_transfer_full_duplex(master, m);
	return mt7621_spi_transfer_half_duplex(master, m);
}

static int mt7621_spi_setup(struct spi_device *spi)
{
	struct mt7621_spi *rs = spidev_to_mt7621_spi(spi);

	if ((spi->max_speed_hz == 0) ||
		(spi->max_speed_hz > (rs->sys_freq / 2)))
		spi->max_speed_hz = (rs->sys_freq / 2);

	if (spi->max_speed_hz < (rs->sys_freq / 4097)) {
		dev_err(&spi->dev, "setup: requested speed is too low %d Hz\n",
			spi->max_speed_hz);
		return -EINVAL;
	}

	return 0;
}

static const struct of_device_id mt7621_spi_match[] = {
	{ .compatible = "ralink,mt7621-spi" },
	{},
};
MODULE_DEVICE_TABLE(of, mt7621_spi_match);

static int mt7621_spi_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct spi_master *master;
	struct mt7621_spi *rs;
	void __iomem *base;
	struct resource *r;
	int status = 0;
	struct clk *clk;
	struct mt7621_spi_ops *ops;
	int ret;

	match = of_match_device(mt7621_spi_match, &pdev->dev);
	if (!match)
		return -EINVAL;
	ops = (struct mt7621_spi_ops *)match->data;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(base))
		return PTR_ERR(base);

	clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "unable to get SYS clock, err=%d\n",
			status);
		return PTR_ERR(clk);
	}

	status = clk_prepare_enable(clk);
	if (status)
		return status;

	master = spi_alloc_master(&pdev->dev, sizeof(*rs));
	if (master == NULL) {
		dev_info(&pdev->dev, "master allocation failed\n");
		return -ENOMEM;
	}

	master->mode_bits = RT2880_SPI_MODE_BITS;

	master->setup = mt7621_spi_setup;
	master->transfer_one_message = mt7621_spi_transfer_one_message;
	master->bits_per_word_mask = SPI_BPW_MASK(8);
	master->dev.of_node = pdev->dev.of_node;
	master->num_chipselect = 2;

	dev_set_drvdata(&pdev->dev, master);

	rs = spi_master_get_devdata(master);
	rs->base = base;
	rs->clk = clk;
	rs->master = master;
	rs->sys_freq = clk_get_rate(rs->clk);
	rs->ops = ops;
	rs->pending_write = 0;
	dev_info(&pdev->dev, "sys_freq: %u\n", rs->sys_freq);

	ret = device_reset(&pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "SPI reset failed!\n");
		return ret;
	}

	mt7621_spi_reset(rs, 0);

	return spi_register_master(master);
}

static int mt7621_spi_remove(struct platform_device *pdev)
{
	struct spi_master *master;
	struct mt7621_spi *rs;

	master = dev_get_drvdata(&pdev->dev);
	rs = spi_master_get_devdata(master);

	clk_disable(rs->clk);
	spi_unregister_master(master);

	return 0;
}

MODULE_ALIAS("platform:" DRIVER_NAME);

static struct platform_driver mt7621_spi_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = mt7621_spi_match,
	},
	.probe = mt7621_spi_probe,
	.remove = mt7621_spi_remove,
};

module_platform_driver(mt7621_spi_driver);

MODULE_DESCRIPTION("MT7621 SPI driver");
MODULE_AUTHOR("Felix Fietkau <nbd@nbd.name>");
MODULE_LICENSE("GPL");
