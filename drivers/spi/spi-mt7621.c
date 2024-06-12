// SPDX-License-Identifier: GPL-2.0
//
// spi-mt7621.c -- MediaTek MT7621 SPI controller driver
//
// Copyright (C) 2011 Sergiy <piratfm@gmail.com>
// Copyright (C) 2011-2013 Gabor Juhos <juhosg@openwrt.org>
// Copyright (C) 2014-2015 Felix Fietkau <nbd@nbd.name>
//
// Some parts are based on spi-orion.c:
//   Author: Shadi Ammouri <shadi@marvell.com>
//   Copyright (C) 2007-2008 Marvell Ltd.

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/spi/spi.h>

#define DRIVER_NAME		"spi-mt7621"

/* in usec */
#define RALINK_SPI_WAIT_MAX_LOOP 2000

/* SPISTAT register bit field */
#define SPISTAT_BUSY		BIT(0)

#define MT7621_SPI_TRANS	0x00
#define SPITRANS_BUSY		BIT(16)

#define MT7621_SPI_OPCODE	0x04
#define MT7621_SPI_DATA0	0x08
#define MT7621_SPI_DATA4	0x18
#define SPI_CTL_TX_RX_CNT_MASK	0xff
#define SPI_CTL_START		BIT(8)

#define MT7621_SPI_MASTER	0x28
#define MASTER_MORE_BUFMODE	BIT(2)
#define MASTER_FULL_DUPLEX	BIT(10)
#define MASTER_RS_CLK_SEL	GENMASK(27, 16)
#define MASTER_RS_CLK_SEL_SHIFT	16
#define MASTER_RS_SLAVE_SEL	GENMASK(31, 29)

#define MT7621_SPI_MOREBUF	0x2c
#define MT7621_SPI_POLAR	0x38
#define MT7621_SPI_SPACE	0x3c

#define MT7621_CPHA		BIT(5)
#define MT7621_CPOL		BIT(4)
#define MT7621_LSB_FIRST	BIT(3)

#define MT7621_NATIVE_CS_COUNT	2

struct mt7621_spi {
	struct spi_controller	*host;
	void __iomem		*base;
	unsigned int		sys_freq;
	unsigned int		speed;
	int			pending_write;
};

static inline struct mt7621_spi *spidev_to_mt7621_spi(struct spi_device *spi)
{
	return spi_controller_get_devdata(spi->controller);
}

static inline u32 mt7621_spi_read(struct mt7621_spi *rs, u32 reg)
{
	return ioread32(rs->base + reg);
}

static inline void mt7621_spi_write(struct mt7621_spi *rs, u32 reg, u32 val)
{
	iowrite32(val, rs->base + reg);
}

static void mt7621_spi_set_native_cs(struct spi_device *spi, bool enable)
{
	struct mt7621_spi *rs = spidev_to_mt7621_spi(spi);
	int cs = spi_get_chipselect(spi, 0);
	bool active = spi->mode & SPI_CS_HIGH ? enable : !enable;
	u32 polar = 0;
	u32 host;

	/*
	 * Select SPI device 7, enable "more buffer mode" and disable
	 * full-duplex (only half-duplex really works on this chip
	 * reliably)
	 */
	host = mt7621_spi_read(rs, MT7621_SPI_MASTER);
	host |= MASTER_RS_SLAVE_SEL | MASTER_MORE_BUFMODE;
	host &= ~MASTER_FULL_DUPLEX;
	mt7621_spi_write(rs, MT7621_SPI_MASTER, host);

	rs->pending_write = 0;

	if (active)
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
	reg &= ~MASTER_RS_CLK_SEL;
	reg |= (rate - 2) << MASTER_RS_CLK_SEL_SHIFT;
	rs->speed = speed;

	reg &= ~MT7621_LSB_FIRST;
	if (spi->mode & SPI_LSB_FIRST)
		reg |= MT7621_LSB_FIRST;

	/*
	 * This SPI controller seems to be tested on SPI flash only and some
	 * bits are swizzled under other SPI modes probably due to incorrect
	 * wiring inside the silicon. Only mode 0 works correctly.
	 */
	reg &= ~(MT7621_CPHA | MT7621_CPOL);

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

static int mt7621_spi_prepare_message(struct spi_controller *host,
				      struct spi_message *m)
{
	struct mt7621_spi *rs = spi_controller_get_devdata(host);
	struct spi_device *spi = m->spi;
	unsigned int speed = spi->max_speed_hz;
	struct spi_transfer *t = NULL;

	mt7621_spi_wait_till_ready(rs);

	list_for_each_entry(t, &m->transfers, transfer_list)
		if (t->speed_hz < speed)
			speed = t->speed_hz;

	return mt7621_spi_prepare(spi, speed);
}

static void mt7621_spi_read_half_duplex(struct mt7621_spi *rs,
					int rx_len, u8 *buf)
{
	int tx_len;

	/*
	 * Combine with any pending write, and perform one or more half-duplex
	 * transactions reading 'len' bytes. Data to be written is already in
	 * MT7621_SPI_DATA.
	 */
	tx_len = rs->pending_write;
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
	int len = rs->pending_write;
	int val = 0;

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
	mt7621_spi_flush(rs);
}

static int mt7621_spi_transfer_one(struct spi_controller *host,
				   struct spi_device *spi,
				   struct spi_transfer *t)
{
	struct mt7621_spi *rs = spi_controller_get_devdata(host);

	if ((t->rx_buf) && (t->tx_buf)) {
		/*
		 * This controller will shift some extra data out
		 * of spi_opcode if (mosi_bit_cnt > 0) &&
		 * (cmd_bit_cnt == 0). So the claimed full-duplex
		 * support is broken since we have no way to read
		 * the MISO value during that bit.
		 */
		return -EIO;
	} else if (t->rx_buf) {
		mt7621_spi_read_half_duplex(rs, t->len, t->rx_buf);
	} else if (t->tx_buf) {
		mt7621_spi_write_half_duplex(rs, t->len, t->tx_buf);
	}

	return 0;
}

static int mt7621_spi_setup(struct spi_device *spi)
{
	struct mt7621_spi *rs = spidev_to_mt7621_spi(spi);

	if ((spi->max_speed_hz == 0) ||
	    (spi->max_speed_hz > (rs->sys_freq / 2)))
		spi->max_speed_hz = rs->sys_freq / 2;

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
	struct spi_controller *host;
	struct mt7621_spi *rs;
	void __iomem *base;
	struct clk *clk;
	int ret;

	match = of_match_device(mt7621_spi_match, &pdev->dev);
	if (!match)
		return -EINVAL;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	clk = devm_clk_get_enabled(&pdev->dev, NULL);
	if (IS_ERR(clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(clk),
				     "unable to get SYS clock\n");

	host = devm_spi_alloc_host(&pdev->dev, sizeof(*rs));
	if (!host) {
		dev_info(&pdev->dev, "host allocation failed\n");
		return -ENOMEM;
	}

	host->mode_bits = SPI_LSB_FIRST;
	host->flags = SPI_CONTROLLER_HALF_DUPLEX;
	host->setup = mt7621_spi_setup;
	host->prepare_message = mt7621_spi_prepare_message;
	host->set_cs = mt7621_spi_set_native_cs;
	host->transfer_one = mt7621_spi_transfer_one;
	host->bits_per_word_mask = SPI_BPW_MASK(8);
	host->dev.of_node = pdev->dev.of_node;
	host->max_native_cs = MT7621_NATIVE_CS_COUNT;
	host->num_chipselect = MT7621_NATIVE_CS_COUNT;
	host->use_gpio_descriptors = true;

	dev_set_drvdata(&pdev->dev, host);

	rs = spi_controller_get_devdata(host);
	rs->base = base;
	rs->host = host;
	rs->sys_freq = clk_get_rate(clk);
	rs->pending_write = 0;
	dev_info(&pdev->dev, "sys_freq: %u\n", rs->sys_freq);

	ret = device_reset(&pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "SPI reset failed!\n");
		return ret;
	}

	return devm_spi_register_controller(&pdev->dev, host);
}

MODULE_ALIAS("platform:" DRIVER_NAME);

static struct platform_driver mt7621_spi_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = mt7621_spi_match,
	},
	.probe = mt7621_spi_probe,
};

module_platform_driver(mt7621_spi_driver);

MODULE_DESCRIPTION("MT7621 SPI driver");
MODULE_AUTHOR("Felix Fietkau <nbd@nbd.name>");
MODULE_LICENSE("GPL");
