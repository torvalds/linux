/*
 * CE4100's SPI device is more or less the same one as found on PXA
 *
 */
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/module.h>
#include <linux/spi/pxa2xx_spi.h>

struct ce4100_info {
	struct ssp_device ssp;
	struct platform_device *spi_pdev;
};

static DEFINE_MUTEX(ssp_lock);
static LIST_HEAD(ssp_list);

struct ssp_device *pxa_ssp_request(int port, const char *label)
{
	struct ssp_device *ssp = NULL;

	mutex_lock(&ssp_lock);

	list_for_each_entry(ssp, &ssp_list, node) {
		if (ssp->port_id == port && ssp->use_count == 0) {
			ssp->use_count++;
			ssp->label = label;
			break;
		}
	}

	mutex_unlock(&ssp_lock);

	if (&ssp->node == &ssp_list)
		return NULL;

	return ssp;
}
EXPORT_SYMBOL_GPL(pxa_ssp_request);

void pxa_ssp_free(struct ssp_device *ssp)
{
	mutex_lock(&ssp_lock);
	if (ssp->use_count) {
		ssp->use_count--;
		ssp->label = NULL;
	} else
		dev_err(&ssp->pdev->dev, "device already free\n");
	mutex_unlock(&ssp_lock);
}
EXPORT_SYMBOL_GPL(pxa_ssp_free);

static int __devinit ce4100_spi_probe(struct pci_dev *dev,
		const struct pci_device_id *ent)
{
	int ret;
	resource_size_t phys_beg;
	resource_size_t phys_len;
	struct ce4100_info *spi_info;
	struct platform_device *pdev;
	struct pxa2xx_spi_master spi_pdata;
	struct ssp_device *ssp;

	ret = pci_enable_device(dev);
	if (ret)
		return ret;

	phys_beg = pci_resource_start(dev, 0);
	phys_len = pci_resource_len(dev, 0);

	if (!request_mem_region(phys_beg, phys_len,
				"CE4100 SPI")) {
		dev_err(&dev->dev, "Can't request register space.\n");
		ret = -EBUSY;
		return ret;
	}

	pdev = platform_device_alloc("pxa2xx-spi", dev->devfn);
	spi_info = kzalloc(sizeof(*spi_info), GFP_KERNEL);
	if (!pdev || !spi_info ) {
		ret = -ENOMEM;
		goto err_nomem;
	}
	memset(&spi_pdata, 0, sizeof(spi_pdata));
	spi_pdata.num_chipselect = dev->devfn;

	ret = platform_device_add_data(pdev, &spi_pdata, sizeof(spi_pdata));
	if (ret)
		goto err_nomem;

	pdev->dev.parent = &dev->dev;
	pdev->dev.of_node = dev->dev.of_node;
	ssp = &spi_info->ssp;
	ssp->phys_base = pci_resource_start(dev, 0);
	ssp->mmio_base = ioremap(phys_beg, phys_len);
	if (!ssp->mmio_base) {
		dev_err(&pdev->dev, "failed to ioremap() registers\n");
		ret = -EIO;
		goto err_nomem;
	}
	ssp->irq = dev->irq;
	ssp->port_id = pdev->id;
	ssp->type = PXA25x_SSP;

	mutex_lock(&ssp_lock);
	list_add(&ssp->node, &ssp_list);
	mutex_unlock(&ssp_lock);

	pci_set_drvdata(dev, spi_info);

	ret = platform_device_add(pdev);
	if (ret)
		goto err_dev_add;

	return ret;

err_dev_add:
	pci_set_drvdata(dev, NULL);
	mutex_lock(&ssp_lock);
	list_del(&ssp->node);
	mutex_unlock(&ssp_lock);
	iounmap(ssp->mmio_base);

err_nomem:
	release_mem_region(phys_beg, phys_len);
	platform_device_put(pdev);
	kfree(spi_info);
	return ret;
}

static void __devexit ce4100_spi_remove(struct pci_dev *dev)
{
	struct ce4100_info *spi_info;
	struct ssp_device *ssp;

	spi_info = pci_get_drvdata(dev);
	ssp = &spi_info->ssp;
	platform_device_unregister(spi_info->spi_pdev);

	iounmap(ssp->mmio_base);
	release_mem_region(pci_resource_start(dev, 0),
			pci_resource_len(dev, 0));

	mutex_lock(&ssp_lock);
	list_del(&ssp->node);
	mutex_unlock(&ssp_lock);

	pci_set_drvdata(dev, NULL);
	pci_disable_device(dev);
	kfree(spi_info);
}

static DEFINE_PCI_DEVICE_TABLE(ce4100_spi_devices) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2e6a) },
	{ },
};
MODULE_DEVICE_TABLE(pci, ce4100_spi_devices);

static struct pci_driver ce4100_spi_driver = {
	.name           = "ce4100_spi",
	.id_table       = ce4100_spi_devices,
	.probe          = ce4100_spi_probe,
	.remove         = __devexit_p(ce4100_spi_remove),
};

module_pci_driver(ce4100_spi_driver);

MODULE_DESCRIPTION("CE4100 PCI-SPI glue code for PXA's driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Sebastian Andrzej Siewior <bigeasy@linutronix.de>");
