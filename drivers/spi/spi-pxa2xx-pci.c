// SPDX-License-Identifier: GPL-2.0-only
/*
 * PCI glue driver for SPI PXA2xx compatible controllers.
 * CE4100's SPI device is more or less the same one as found on PXA.
 *
 * Copyright (C) 2016, 2021 Intel Corporation
 */
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_device.h>

#include <linux/spi/pxa2xx_spi.h>

#include <linux/dmaengine.h>
#include <linux/platform_data/dma-dw.h>

#define PCI_DEVICE_ID_INTEL_QUARK_X1000		0x0935
#define PCI_DEVICE_ID_INTEL_BYT			0x0f0e
#define PCI_DEVICE_ID_INTEL_MRFLD		0x1194
#define PCI_DEVICE_ID_INTEL_BSW0		0x228e
#define PCI_DEVICE_ID_INTEL_BSW1		0x2290
#define PCI_DEVICE_ID_INTEL_BSW2		0x22ac
#define PCI_DEVICE_ID_INTEL_CE4100		0x2e6a
#define PCI_DEVICE_ID_INTEL_LPT0_0		0x9c65
#define PCI_DEVICE_ID_INTEL_LPT0_1		0x9c66
#define PCI_DEVICE_ID_INTEL_LPT1_0		0x9ce5
#define PCI_DEVICE_ID_INTEL_LPT1_1		0x9ce6

struct pxa_spi_info {
	enum pxa_ssp_type type;
	unsigned int port_id;
	unsigned int num_chipselect;
	unsigned long max_clk_rate;

	/* DMA channel request parameters */
	bool (*dma_filter)(struct dma_chan *chan, void *param);
	void *tx_param;
	void *rx_param;

	unsigned int dma_burst_size;

	int (*setup)(struct pci_dev *pdev, struct pxa_spi_info *c);
};

static struct dw_dma_slave byt_tx_param = { .dst_id = 0 };
static struct dw_dma_slave byt_rx_param = { .src_id = 1 };

static struct dw_dma_slave mrfld3_tx_param = { .dst_id = 15 };
static struct dw_dma_slave mrfld3_rx_param = { .src_id = 14 };
static struct dw_dma_slave mrfld5_tx_param = { .dst_id = 13 };
static struct dw_dma_slave mrfld5_rx_param = { .src_id = 12 };
static struct dw_dma_slave mrfld6_tx_param = { .dst_id = 11 };
static struct dw_dma_slave mrfld6_rx_param = { .src_id = 10 };

static struct dw_dma_slave bsw0_tx_param = { .dst_id = 0 };
static struct dw_dma_slave bsw0_rx_param = { .src_id = 1 };
static struct dw_dma_slave bsw1_tx_param = { .dst_id = 6 };
static struct dw_dma_slave bsw1_rx_param = { .src_id = 7 };
static struct dw_dma_slave bsw2_tx_param = { .dst_id = 8 };
static struct dw_dma_slave bsw2_rx_param = { .src_id = 9 };

static struct dw_dma_slave lpt1_tx_param = { .dst_id = 0 };
static struct dw_dma_slave lpt1_rx_param = { .src_id = 1 };
static struct dw_dma_slave lpt0_tx_param = { .dst_id = 2 };
static struct dw_dma_slave lpt0_rx_param = { .src_id = 3 };

static bool lpss_dma_filter(struct dma_chan *chan, void *param)
{
	struct dw_dma_slave *dws = param;

	if (dws->dma_dev != chan->device->dev)
		return false;

	chan->private = dws;
	return true;
}

static void lpss_dma_put_device(void *dma_dev)
{
	pci_dev_put(dma_dev);
}

static int lpss_spi_setup(struct pci_dev *dev, struct pxa_spi_info *c)
{
	struct pci_dev *dma_dev;
	int ret;

	switch (dev->device) {
	case PCI_DEVICE_ID_INTEL_BYT:
		c->type = LPSS_BYT_SSP;
		c->port_id = 0;
		c->tx_param = &byt_tx_param;
		c->rx_param = &byt_rx_param;
		break;
	case PCI_DEVICE_ID_INTEL_BSW0:
		c->type = LPSS_BSW_SSP;
		c->port_id = 0;
		c->tx_param = &bsw0_tx_param;
		c->rx_param = &bsw0_rx_param;
		break;
	case PCI_DEVICE_ID_INTEL_BSW1:
		c->type = LPSS_BSW_SSP;
		c->port_id = 1;
		c->tx_param = &bsw1_tx_param;
		c->rx_param = &bsw1_rx_param;
		break;
	case PCI_DEVICE_ID_INTEL_BSW2:
		c->type = LPSS_BSW_SSP;
		c->port_id = 2;
		c->tx_param = &bsw2_tx_param;
		c->rx_param = &bsw2_rx_param;
		break;
	case PCI_DEVICE_ID_INTEL_LPT0_0:
	case PCI_DEVICE_ID_INTEL_LPT1_0:
		c->type = LPSS_LPT_SSP;
		c->port_id = 0;
		c->tx_param = &lpt0_tx_param;
		c->rx_param = &lpt0_rx_param;
		break;
	case PCI_DEVICE_ID_INTEL_LPT0_1:
	case PCI_DEVICE_ID_INTEL_LPT1_1:
		c->type = LPSS_LPT_SSP;
		c->port_id = 1;
		c->tx_param = &lpt1_tx_param;
		c->rx_param = &lpt1_rx_param;
		break;
	default:
		return -ENODEV;
	}

	c->num_chipselect = 1;
	c->max_clk_rate = 50000000;

	dma_dev = pci_get_slot(dev->bus, PCI_DEVFN(PCI_SLOT(dev->devfn), 0));
	ret = devm_add_action_or_reset(&dev->dev, lpss_dma_put_device, dma_dev);
	if (ret)
		return ret;

	if (c->tx_param) {
		struct dw_dma_slave *slave = c->tx_param;

		slave->dma_dev = &dma_dev->dev;
		slave->m_master = 0;
		slave->p_master = 1;
	}

	if (c->rx_param) {
		struct dw_dma_slave *slave = c->rx_param;

		slave->dma_dev = &dma_dev->dev;
		slave->m_master = 0;
		slave->p_master = 1;
	}

	c->dma_filter = lpss_dma_filter;
	c->dma_burst_size = 1;
	return 0;
}

static struct pxa_spi_info lpss_info_config = {
	.setup = lpss_spi_setup,
};

static int ce4100_spi_setup(struct pci_dev *dev, struct pxa_spi_info *c)
{
	c->type = PXA25x_SSP;
	c->port_id = dev->devfn;
	c->num_chipselect = dev->devfn;
	c->max_clk_rate = 3686400;

	return 0;
}

static struct pxa_spi_info ce4100_info_config = {
	.setup = ce4100_spi_setup,
};

static int mrfld_spi_setup(struct pci_dev *dev, struct pxa_spi_info *c)
{
	struct dw_dma_slave *tx, *rx;
	struct pci_dev *dma_dev;
	int ret;

	switch (PCI_FUNC(dev->devfn)) {
	case 0:
		c->port_id = 3;
		c->num_chipselect = 1;
		c->tx_param = &mrfld3_tx_param;
		c->rx_param = &mrfld3_rx_param;
		break;
	case 1:
		c->port_id = 5;
		c->num_chipselect = 4;
		c->tx_param = &mrfld5_tx_param;
		c->rx_param = &mrfld5_rx_param;
		break;
	case 2:
		c->port_id = 6;
		c->num_chipselect = 1;
		c->tx_param = &mrfld6_tx_param;
		c->rx_param = &mrfld6_rx_param;
		break;
	default:
		return -ENODEV;
	}

	c->type = MRFLD_SSP;
	c->max_clk_rate = 25000000;

	dma_dev = pci_get_slot(dev->bus, PCI_DEVFN(21, 0));
	ret = devm_add_action_or_reset(&dev->dev, lpss_dma_put_device, dma_dev);
	if (ret)
		return ret;

	tx = c->tx_param;
	tx->dma_dev = &dma_dev->dev;

	rx = c->rx_param;
	rx->dma_dev = &dma_dev->dev;

	c->dma_filter = lpss_dma_filter;
	c->dma_burst_size = 8;
	return 0;
}

static struct pxa_spi_info mrfld_info_config = {
	.setup = mrfld_spi_setup,
};

static int qrk_spi_setup(struct pci_dev *dev, struct pxa_spi_info *c)
{
	c->type = QUARK_X1000_SSP;
	c->port_id = dev->devfn;
	c->num_chipselect = 1;
	c->max_clk_rate = 50000000;

	return 0;
}

static struct pxa_spi_info qrk_info_config = {
	.setup = qrk_spi_setup,
};

static int pxa2xx_spi_pci_probe(struct pci_dev *dev,
		const struct pci_device_id *ent)
{
	struct platform_device_info pi;
	int ret;
	struct platform_device *pdev;
	struct pxa2xx_spi_controller spi_pdata;
	struct ssp_device *ssp;
	struct pxa_spi_info *c;
	char buf[40];

	ret = pcim_enable_device(dev);
	if (ret)
		return ret;

	ret = pcim_iomap_regions(dev, 1 << 0, "PXA2xx SPI");
	if (ret)
		return ret;

	c = (struct pxa_spi_info *)ent->driver_data;
	ret = c->setup(dev, c);
	if (ret)
		return ret;

	memset(&spi_pdata, 0, sizeof(spi_pdata));
	spi_pdata.num_chipselect = c->num_chipselect;
	spi_pdata.dma_filter = c->dma_filter;
	spi_pdata.tx_param = c->tx_param;
	spi_pdata.rx_param = c->rx_param;
	spi_pdata.enable_dma = c->rx_param && c->tx_param;
	spi_pdata.dma_burst_size = c->dma_burst_size;

	ssp = &spi_pdata.ssp;
	ssp->dev = &dev->dev;
	ssp->phys_base = pci_resource_start(dev, 0);
	ssp->mmio_base = pcim_iomap_table(dev)[0];
	ssp->type = c->type;
	ssp->port_id = c->port_id;

	pci_set_master(dev);

	ret = pci_alloc_irq_vectors(dev, 1, 1, PCI_IRQ_ALL_TYPES);
	if (ret < 0)
		return ret;
	ssp->irq = pci_irq_vector(dev, 0);

	snprintf(buf, sizeof(buf), "pxa2xx-spi.%d", ssp->port_id);
	ssp->clk = clk_register_fixed_rate(&dev->dev, buf, NULL, 0,
					   c->max_clk_rate);
	if (IS_ERR(ssp->clk))
		return PTR_ERR(ssp->clk);

	memset(&pi, 0, sizeof(pi));
	pi.fwnode = dev_fwnode(&dev->dev);
	pi.parent = &dev->dev;
	pi.name = "pxa2xx-spi";
	pi.id = ssp->port_id;
	pi.data = &spi_pdata;
	pi.size_data = sizeof(spi_pdata);

	pdev = platform_device_register_full(&pi);
	if (IS_ERR(pdev)) {
		clk_unregister(ssp->clk);
		return PTR_ERR(pdev);
	}

	pci_set_drvdata(dev, pdev);

	return 0;
}

static void pxa2xx_spi_pci_remove(struct pci_dev *dev)
{
	struct platform_device *pdev = pci_get_drvdata(dev);
	struct pxa2xx_spi_controller *spi_pdata;

	spi_pdata = dev_get_platdata(&pdev->dev);

	platform_device_unregister(pdev);
	clk_unregister(spi_pdata->ssp.clk);
}

static const struct pci_device_id pxa2xx_spi_pci_devices[] = {
	{ PCI_DEVICE_DATA(INTEL, QUARK_X1000, &qrk_info_config) },
	{ PCI_DEVICE_DATA(INTEL, BYT, &lpss_info_config) },
	{ PCI_DEVICE_DATA(INTEL, MRFLD, &mrfld_info_config) },
	{ PCI_DEVICE_DATA(INTEL, BSW0, &lpss_info_config) },
	{ PCI_DEVICE_DATA(INTEL, BSW1, &lpss_info_config) },
	{ PCI_DEVICE_DATA(INTEL, BSW2, &lpss_info_config) },
	{ PCI_DEVICE_DATA(INTEL, CE4100, &ce4100_info_config) },
	{ PCI_DEVICE_DATA(INTEL, LPT0_0, &lpss_info_config) },
	{ PCI_DEVICE_DATA(INTEL, LPT0_1, &lpss_info_config) },
	{ PCI_DEVICE_DATA(INTEL, LPT1_0, &lpss_info_config) },
	{ PCI_DEVICE_DATA(INTEL, LPT1_1, &lpss_info_config) },
	{ }
};
MODULE_DEVICE_TABLE(pci, pxa2xx_spi_pci_devices);

static struct pci_driver pxa2xx_spi_pci_driver = {
	.name           = "pxa2xx_spi_pci",
	.id_table       = pxa2xx_spi_pci_devices,
	.probe          = pxa2xx_spi_pci_probe,
	.remove         = pxa2xx_spi_pci_remove,
};

module_pci_driver(pxa2xx_spi_pci_driver);

MODULE_DESCRIPTION("CE4100/LPSS PCI-SPI glue code for PXA's driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Sebastian Andrzej Siewior <bigeasy@linutronix.de>");
