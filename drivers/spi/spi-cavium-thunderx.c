// SPDX-License-Identifier: GPL-2.0-only
/*
 * Cavium ThunderX SPI driver.
 *
 * Copyright (C) 2016 Cavium Inc.
 * Authors: Jan Glauber <jglauber@cavium.com>
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/spi/spi.h>

#include "spi-cavium.h"

#define DRV_NAME "spi-thunderx"

#define SYS_FREQ_DEFAULT 700000000 /* 700 Mhz */

static int thunderx_spi_probe(struct pci_dev *pdev,
			      const struct pci_device_id *ent)
{
	struct device *dev = &pdev->dev;
	struct spi_controller *host;
	struct octeon_spi *p;
	int ret;

	host = spi_alloc_host(dev, sizeof(struct octeon_spi));
	if (!host)
		return -ENOMEM;

	p = spi_controller_get_devdata(host);

	ret = pcim_enable_device(pdev);
	if (ret)
		goto error;

	ret = pci_request_regions(pdev, DRV_NAME);
	if (ret)
		goto error;

	p->register_base = pcim_iomap(pdev, 0, pci_resource_len(pdev, 0));
	if (!p->register_base) {
		ret = -EINVAL;
		goto error;
	}

	p->regs.config = 0x1000;
	p->regs.status = 0x1008;
	p->regs.tx = 0x1010;
	p->regs.data = 0x1080;

	p->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(p->clk)) {
		ret = PTR_ERR(p->clk);
		goto error;
	}

	ret = clk_prepare_enable(p->clk);
	if (ret)
		goto error;

	p->sys_freq = clk_get_rate(p->clk);
	if (!p->sys_freq)
		p->sys_freq = SYS_FREQ_DEFAULT;
	dev_info(dev, "Set system clock to %u\n", p->sys_freq);

	host->flags = SPI_CONTROLLER_HALF_DUPLEX;
	host->num_chipselect = 4;
	host->mode_bits = SPI_CPHA | SPI_CPOL | SPI_CS_HIGH |
			    SPI_LSB_FIRST | SPI_3WIRE;
	host->transfer_one_message = octeon_spi_transfer_one_message;
	host->bits_per_word_mask = SPI_BPW_MASK(8);
	host->max_speed_hz = OCTEON_SPI_MAX_CLOCK_HZ;
	host->dev.of_node = pdev->dev.of_node;

	pci_set_drvdata(pdev, host);

	ret = devm_spi_register_controller(dev, host);
	if (ret)
		goto error;

	return 0;

error:
	clk_disable_unprepare(p->clk);
	pci_release_regions(pdev);
	spi_controller_put(host);
	return ret;
}

static void thunderx_spi_remove(struct pci_dev *pdev)
{
	struct spi_controller *host = pci_get_drvdata(pdev);
	struct octeon_spi *p;

	p = spi_controller_get_devdata(host);
	if (!p)
		return;

	clk_disable_unprepare(p->clk);
	pci_release_regions(pdev);
	/* Put everything in a known state. */
	writeq(0, p->register_base + OCTEON_SPI_CFG(p));
}

static const struct pci_device_id thunderx_spi_pci_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, 0xa00b) },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, thunderx_spi_pci_id_table);

static struct pci_driver thunderx_spi_driver = {
	.name		= DRV_NAME,
	.id_table	= thunderx_spi_pci_id_table,
	.probe		= thunderx_spi_probe,
	.remove		= thunderx_spi_remove,
};

module_pci_driver(thunderx_spi_driver);

MODULE_DESCRIPTION("Cavium, Inc. ThunderX SPI bus driver");
MODULE_AUTHOR("Jan Glauber");
MODULE_LICENSE("GPL");
