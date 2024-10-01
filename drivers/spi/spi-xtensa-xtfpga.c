// SPDX-License-Identifier: GPL-2.0-only
/*
 * Xtensa xtfpga SPI controller driver
 *
 * Copyright (c) 2014 Cadence Design Systems Inc.
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi_bitbang.h>

#define XTFPGA_SPI_NAME "xtfpga_spi"

#define XTFPGA_SPI_START	0x0
#define XTFPGA_SPI_BUSY		0x4
#define XTFPGA_SPI_DATA		0x8

#define BUSY_WAIT_US		100

struct xtfpga_spi {
	struct spi_bitbang bitbang;
	void __iomem *regs;
	u32 data;
	unsigned data_sz;
};

static inline void xtfpga_spi_write32(const struct xtfpga_spi *spi,
				      unsigned addr, u32 val)
{
	__raw_writel(val, spi->regs + addr);
}

static inline unsigned int xtfpga_spi_read32(const struct xtfpga_spi *spi,
					     unsigned addr)
{
	return __raw_readl(spi->regs + addr);
}

static inline void xtfpga_spi_wait_busy(struct xtfpga_spi *xspi)
{
	unsigned i;

	for (i = 0; xtfpga_spi_read32(xspi, XTFPGA_SPI_BUSY) &&
	     i < BUSY_WAIT_US; ++i)
		udelay(1);
	WARN_ON_ONCE(i == BUSY_WAIT_US);
}

static u32 xtfpga_spi_txrx_word(struct spi_device *spi, unsigned nsecs,
				u32 v, u8 bits, unsigned flags)
{
	struct xtfpga_spi *xspi = spi_controller_get_devdata(spi->controller);

	xspi->data = (xspi->data << bits) | (v & GENMASK(bits - 1, 0));
	xspi->data_sz += bits;
	if (xspi->data_sz >= 16) {
		xtfpga_spi_write32(xspi, XTFPGA_SPI_DATA,
				   xspi->data >> (xspi->data_sz - 16));
		xspi->data_sz -= 16;
		xtfpga_spi_write32(xspi, XTFPGA_SPI_START, 1);
		xtfpga_spi_wait_busy(xspi);
		xtfpga_spi_write32(xspi, XTFPGA_SPI_START, 0);
	}

	return 0;
}

static void xtfpga_spi_chipselect(struct spi_device *spi, int is_on)
{
	struct xtfpga_spi *xspi = spi_controller_get_devdata(spi->controller);

	WARN_ON(xspi->data_sz != 0);
	xspi->data_sz = 0;
}

static int xtfpga_spi_probe(struct platform_device *pdev)
{
	struct xtfpga_spi *xspi;
	int ret;
	struct spi_controller *host;

	host = devm_spi_alloc_host(&pdev->dev, sizeof(struct xtfpga_spi));
	if (!host)
		return -ENOMEM;

	host->flags = SPI_CONTROLLER_NO_RX;
	host->bits_per_word_mask = SPI_BPW_RANGE_MASK(1, 16);
	host->bus_num = pdev->dev.id;
	host->dev.of_node = pdev->dev.of_node;

	xspi = spi_controller_get_devdata(host);
	xspi->bitbang.ctlr = host;
	xspi->bitbang.chipselect = xtfpga_spi_chipselect;
	xspi->bitbang.txrx_word[SPI_MODE_0] = xtfpga_spi_txrx_word;
	xspi->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(xspi->regs))
		return PTR_ERR(xspi->regs);

	xtfpga_spi_write32(xspi, XTFPGA_SPI_START, 0);
	usleep_range(1000, 2000);
	if (xtfpga_spi_read32(xspi, XTFPGA_SPI_BUSY)) {
		dev_err(&pdev->dev, "Device stuck in busy state\n");
		return -EBUSY;
	}

	ret = spi_bitbang_start(&xspi->bitbang);
	if (ret < 0) {
		dev_err(&pdev->dev, "spi_bitbang_start failed\n");
		return ret;
	}

	platform_set_drvdata(pdev, host);
	return 0;
}

static void xtfpga_spi_remove(struct platform_device *pdev)
{
	struct spi_controller *host = platform_get_drvdata(pdev);
	struct xtfpga_spi *xspi = spi_controller_get_devdata(host);

	spi_bitbang_stop(&xspi->bitbang);
	spi_controller_put(host);
}

MODULE_ALIAS("platform:" XTFPGA_SPI_NAME);

#ifdef CONFIG_OF
static const struct of_device_id xtfpga_spi_of_match[] = {
	{ .compatible = "cdns,xtfpga-spi", },
	{}
};
MODULE_DEVICE_TABLE(of, xtfpga_spi_of_match);
#endif

static struct platform_driver xtfpga_spi_driver = {
	.probe = xtfpga_spi_probe,
	.remove_new = xtfpga_spi_remove,
	.driver = {
		.name = XTFPGA_SPI_NAME,
		.of_match_table = of_match_ptr(xtfpga_spi_of_match),
	},
};
module_platform_driver(xtfpga_spi_driver);

MODULE_AUTHOR("Max Filippov <jcmvbkbc@gmail.com>");
MODULE_DESCRIPTION("xtensa xtfpga SPI driver");
MODULE_LICENSE("GPL");
