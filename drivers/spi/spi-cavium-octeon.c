/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2011, 2012 Cavium, Inc.
 */

#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/of.h>

#include <asm/octeon/octeon.h>

#include "spi-cavium.h"

static int octeon_spi_probe(struct platform_device *pdev)
{
	void __iomem *reg_base;
	struct spi_controller *host;
	struct octeon_spi *p;
	int err = -ENOENT;

	host = spi_alloc_host(&pdev->dev, sizeof(struct octeon_spi));
	if (!host)
		return -ENOMEM;
	p = spi_controller_get_devdata(host);
	platform_set_drvdata(pdev, host);

	reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(reg_base)) {
		err = PTR_ERR(reg_base);
		goto fail;
	}

	p->register_base = reg_base;
	p->sys_freq = octeon_get_io_clock_rate();

	p->regs.config = 0;
	p->regs.status = 0x08;
	p->regs.tx = 0x10;
	p->regs.data = 0x80;

	host->num_chipselect = 4;
	host->mode_bits = SPI_CPHA |
			    SPI_CPOL |
			    SPI_CS_HIGH |
			    SPI_LSB_FIRST |
			    SPI_3WIRE;

	host->transfer_one_message = octeon_spi_transfer_one_message;
	host->bits_per_word_mask = SPI_BPW_MASK(8);
	host->max_speed_hz = OCTEON_SPI_MAX_CLOCK_HZ;

	host->dev.of_node = pdev->dev.of_node;
	err = devm_spi_register_controller(&pdev->dev, host);
	if (err) {
		dev_err(&pdev->dev, "register host failed: %d\n", err);
		goto fail;
	}

	dev_info(&pdev->dev, "OCTEON SPI bus driver\n");

	return 0;
fail:
	spi_controller_put(host);
	return err;
}

static void octeon_spi_remove(struct platform_device *pdev)
{
	struct spi_controller *host = platform_get_drvdata(pdev);
	struct octeon_spi *p = spi_controller_get_devdata(host);

	/* Clear the CSENA* and put everything in a known state. */
	writeq(0, p->register_base + OCTEON_SPI_CFG(p));
}

static const struct of_device_id octeon_spi_match[] = {
	{ .compatible = "cavium,octeon-3010-spi", },
	{},
};
MODULE_DEVICE_TABLE(of, octeon_spi_match);

static struct platform_driver octeon_spi_driver = {
	.driver = {
		.name		= "spi-octeon",
		.of_match_table = octeon_spi_match,
	},
	.probe		= octeon_spi_probe,
	.remove		= octeon_spi_remove,
};

module_platform_driver(octeon_spi_driver);

MODULE_DESCRIPTION("Cavium, Inc. OCTEON SPI bus driver");
MODULE_AUTHOR("David Daney");
MODULE_LICENSE("GPL");
