/*
 * CE4100's SPI device is more or less the same one as found on PXA
 *
 * Copyright (C) 2016, Intel Corporation
 */
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/spi/pxa2xx_spi.h>

#include <linux/dmaengine.h>
#include <linux/platform_data/dma-dw.h>

enum {
	PORT_QUARK_X1000,
	PORT_BYT,
	PORT_MRFLD,
	PORT_BSW0,
	PORT_BSW1,
	PORT_BSW2,
	PORT_CE4100,
	PORT_LPT,
};

struct pxa_spi_info {
	enum pxa_ssp_type type;
	int port_id;
	int num_chipselect;
	unsigned long max_clk_rate;

	/* DMA channel request parameters */
	bool (*dma_filter)(struct dma_chan *chan, void *param);
	void *tx_param;
	void *rx_param;

	int (*setup)(struct pci_dev *pdev, struct pxa_spi_info *c);
};

static struct dw_dma_slave byt_tx_param = { .dst_id = 0 };
static struct dw_dma_slave byt_rx_param = { .src_id = 1 };

static struct dw_dma_slave bsw0_tx_param = { .dst_id = 0 };
static struct dw_dma_slave bsw0_rx_param = { .src_id = 1 };
static struct dw_dma_slave bsw1_tx_param = { .dst_id = 6 };
static struct dw_dma_slave bsw1_rx_param = { .src_id = 7 };
static struct dw_dma_slave bsw2_tx_param = { .dst_id = 8 };
static struct dw_dma_slave bsw2_rx_param = { .src_id = 9 };

static struct dw_dma_slave lpt_tx_param = { .dst_id = 0 };
static struct dw_dma_slave lpt_rx_param = { .src_id = 1 };

static bool lpss_dma_filter(struct dma_chan *chan, void *param)
{
	struct dw_dma_slave *dws = param;

	if (dws->dma_dev != chan->device->dev)
		return false;

	chan->private = dws;
	return true;
}

static int lpss_spi_setup(struct pci_dev *dev, struct pxa_spi_info *c)
{
	struct pci_dev *dma_dev;

	c->num_chipselect = 1;
	c->max_clk_rate = 50000000;

	dma_dev = pci_get_slot(dev->bus, PCI_DEVFN(PCI_SLOT(dev->devfn), 0));

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
	return 0;
}

static int mrfld_spi_setup(struct pci_dev *dev, struct pxa_spi_info *c)
{
	switch (PCI_FUNC(dev->devfn)) {
	case 0:
		c->port_id = 3;
		c->num_chipselect = 1;
		break;
	case 1:
		c->port_id = 5;
		c->num_chipselect = 4;
		break;
	case 2:
		c->port_id = 6;
		c->num_chipselect = 1;
		break;
	default:
		return -ENODEV;
	}
	return 0;
}

static struct pxa_spi_info spi_info_configs[] = {
	[PORT_CE4100] = {
		.type = PXA25x_SSP,
		.port_id =  -1,
		.num_chipselect = -1,
		.max_clk_rate = 3686400,
	},
	[PORT_BYT] = {
		.type = LPSS_BYT_SSP,
		.port_id = 0,
		.setup = lpss_spi_setup,
		.tx_param = &byt_tx_param,
		.rx_param = &byt_rx_param,
	},
	[PORT_BSW0] = {
		.type = LPSS_BSW_SSP,
		.port_id = 0,
		.setup = lpss_spi_setup,
		.tx_param = &bsw0_tx_param,
		.rx_param = &bsw0_rx_param,
	},
	[PORT_BSW1] = {
		.type = LPSS_BSW_SSP,
		.port_id = 1,
		.setup = lpss_spi_setup,
		.tx_param = &bsw1_tx_param,
		.rx_param = &bsw1_rx_param,
	},
	[PORT_BSW2] = {
		.type = LPSS_BSW_SSP,
		.port_id = 2,
		.setup = lpss_spi_setup,
		.tx_param = &bsw2_tx_param,
		.rx_param = &bsw2_rx_param,
	},
	[PORT_MRFLD] = {
		.type = PXA27x_SSP,
		.max_clk_rate = 25000000,
		.setup = mrfld_spi_setup,
	},
	[PORT_QUARK_X1000] = {
		.type = QUARK_X1000_SSP,
		.port_id = -1,
		.num_chipselect = 1,
		.max_clk_rate = 50000000,
	},
	[PORT_LPT] = {
		.type = LPSS_LPT_SSP,
		.port_id = 0,
		.setup = lpss_spi_setup,
		.tx_param = &lpt_tx_param,
		.rx_param = &lpt_rx_param,
	},
};

static int pxa2xx_spi_pci_probe(struct pci_dev *dev,
		const struct pci_device_id *ent)
{
	struct platform_device_info pi;
	int ret;
	struct platform_device *pdev;
	struct pxa2xx_spi_master spi_pdata;
	struct ssp_device *ssp;
	struct pxa_spi_info *c;
	char buf[40];

	ret = pcim_enable_device(dev);
	if (ret)
		return ret;

	ret = pcim_iomap_regions(dev, 1 << 0, "PXA2xx SPI");
	if (ret)
		return ret;

	c = &spi_info_configs[ent->driver_data];
	if (c->setup) {
		ret = c->setup(dev, c);
		if (ret)
			return ret;
	}

	memset(&spi_pdata, 0, sizeof(spi_pdata));
	spi_pdata.num_chipselect = (c->num_chipselect > 0) ? c->num_chipselect : dev->devfn;
	spi_pdata.dma_filter = c->dma_filter;
	spi_pdata.tx_param = c->tx_param;
	spi_pdata.rx_param = c->rx_param;
	spi_pdata.enable_dma = c->rx_param && c->tx_param;

	ssp = &spi_pdata.ssp;
	ssp->phys_base = pci_resource_start(dev, 0);
	ssp->mmio_base = pcim_iomap_table(dev)[0];
	ssp->irq = dev->irq;
	ssp->port_id = (c->port_id >= 0) ? c->port_id : dev->devfn;
	ssp->type = c->type;

	snprintf(buf, sizeof(buf), "pxa2xx-spi.%d", ssp->port_id);
	ssp->clk = clk_register_fixed_rate(&dev->dev, buf , NULL, 0,
					   c->max_clk_rate);
	 if (IS_ERR(ssp->clk))
		return PTR_ERR(ssp->clk);

	memset(&pi, 0, sizeof(pi));
	pi.fwnode = dev->dev.fwnode;
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
	struct pxa2xx_spi_master *spi_pdata;

	spi_pdata = dev_get_platdata(&pdev->dev);

	platform_device_unregister(pdev);
	clk_unregister(spi_pdata->ssp.clk);
}

static const struct pci_device_id pxa2xx_spi_pci_devices[] = {
	{ PCI_VDEVICE(INTEL, 0x0935), PORT_QUARK_X1000 },
	{ PCI_VDEVICE(INTEL, 0x0f0e), PORT_BYT },
	{ PCI_VDEVICE(INTEL, 0x1194), PORT_MRFLD },
	{ PCI_VDEVICE(INTEL, 0x228e), PORT_BSW0 },
	{ PCI_VDEVICE(INTEL, 0x2290), PORT_BSW1 },
	{ PCI_VDEVICE(INTEL, 0x22ac), PORT_BSW2 },
	{ PCI_VDEVICE(INTEL, 0x2e6a), PORT_CE4100 },
	{ PCI_VDEVICE(INTEL, 0x9ce6), PORT_LPT },
	{ },
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
