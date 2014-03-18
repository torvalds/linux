/*
 * CE4100's SPI device is more or less the same one as found on PXA
 *
 */
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/module.h>
#include <linux/spi/pxa2xx_spi.h>

static int ce4100_spi_probe(struct pci_dev *dev,
		const struct pci_device_id *ent)
{
	struct platform_device_info pi;
	int ret;
	struct platform_device *pdev;
	struct pxa2xx_spi_master spi_pdata;
	struct ssp_device *ssp;

	ret = pcim_enable_device(dev);
	if (ret)
		return ret;

	ret = pcim_iomap_regions(dev, 1 << 0, "PXA2xx SPI");
	if (ret)
		return ret;

	memset(&spi_pdata, 0, sizeof(spi_pdata));
	spi_pdata.num_chipselect = dev->devfn;

	ssp = &spi_pdata.ssp;
	ssp->phys_base = pci_resource_start(dev, 0);
	ssp->mmio_base = pcim_iomap_table(dev)[0];
	if (!ssp->mmio_base) {
		dev_err(&dev->dev, "failed to ioremap() registers\n");
		return -EIO;
	}
	ssp->irq = dev->irq;
	ssp->port_id = dev->devfn;
	ssp->type = PXA25x_SSP;

	memset(&pi, 0, sizeof(pi));
	pi.parent = &dev->dev;
	pi.name = "pxa2xx-spi";
	pi.id = ssp->port_id;
	pi.data = &spi_pdata;
	pi.size_data = sizeof(spi_pdata);

	pdev = platform_device_register_full(&pi);
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	pci_set_drvdata(dev, pdev);

	return 0;
}

static void ce4100_spi_remove(struct pci_dev *dev)
{
	struct platform_device *pdev = pci_get_drvdata(dev);

	platform_device_unregister(pdev);
}

static const struct pci_device_id ce4100_spi_devices[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2e6a) },
	{ },
};
MODULE_DEVICE_TABLE(pci, ce4100_spi_devices);

static struct pci_driver ce4100_spi_driver = {
	.name           = "ce4100_spi",
	.id_table       = ce4100_spi_devices,
	.probe          = ce4100_spi_probe,
	.remove         = ce4100_spi_remove,
};

module_pci_driver(ce4100_spi_driver);

MODULE_DESCRIPTION("CE4100 PCI-SPI glue code for PXA's driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Sebastian Andrzej Siewior <bigeasy@linutronix.de>");
