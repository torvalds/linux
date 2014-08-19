/*
 * CE4100's SPI device is more or less the same one as found on PXA
 *
 */
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/module.h>
#include <linux/spi/pxa2xx_spi.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>

enum {
	PORT_CE4100,
	PORT_BYT,
};

struct pxa_spi_info {
	enum pxa_ssp_type type;
	int port_id;
	int num_chipselect;
	int tx_slave_id;
	int tx_chan_id;
	int rx_slave_id;
	int rx_chan_id;
	unsigned long max_clk_rate;
};

static struct pxa_spi_info spi_info_configs[] = {
	[PORT_CE4100] = {
		.type = PXA25x_SSP,
		.port_id =  -1,
		.num_chipselect = -1,
		.tx_slave_id = -1,
		.tx_chan_id = -1,
		.rx_slave_id = -1,
		.rx_chan_id = -1,
		.max_clk_rate = 3686400,
	},
	[PORT_BYT] = {
		.type = LPSS_SSP,
		.port_id = 0,
		.num_chipselect = 1,
		.tx_slave_id = 0,
		.tx_chan_id = 0,
		.rx_slave_id = 1,
		.rx_chan_id = 1,
		.max_clk_rate = 50000000,
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

	memset(&spi_pdata, 0, sizeof(spi_pdata));
	spi_pdata.num_chipselect = (c->num_chipselect > 0) ?
					c->num_chipselect : dev->devfn;
	spi_pdata.tx_slave_id = c->tx_slave_id;
	spi_pdata.tx_chan_id = c->tx_chan_id;
	spi_pdata.rx_slave_id = c->rx_slave_id;
	spi_pdata.rx_chan_id = c->rx_chan_id;
	spi_pdata.enable_dma = c->rx_slave_id >= 0 && c->tx_slave_id >= 0;

	ssp = &spi_pdata.ssp;
	ssp->phys_base = pci_resource_start(dev, 0);
	ssp->mmio_base = pcim_iomap_table(dev)[0];
	if (!ssp->mmio_base) {
		dev_err(&dev->dev, "failed to ioremap() registers\n");
		return -EIO;
	}
	ssp->irq = dev->irq;
	ssp->port_id = (c->port_id >= 0) ? c->port_id : dev->devfn;
	ssp->type = c->type;

	snprintf(buf, sizeof(buf), "pxa2xx-spi.%d", ssp->port_id);
	ssp->clk = clk_register_fixed_rate(&dev->dev, buf , NULL,
					CLK_IS_ROOT, c->max_clk_rate);
	 if (IS_ERR(ssp->clk))
		return PTR_ERR(ssp->clk);

	memset(&pi, 0, sizeof(pi));
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
	{ PCI_VDEVICE(INTEL, 0x2e6a), PORT_CE4100 },
	{ PCI_VDEVICE(INTEL, 0x0f0e), PORT_BYT },
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
