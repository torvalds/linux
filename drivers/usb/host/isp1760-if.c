/*
 * Glue code for the ISP1760 driver and bus
 * Currently there is support for
 * - OpenFirmware
 * - PCI
 * - PDEV (generic platform device centralized driver model)
 *
 * (c) 2007 Sebastian Siewior <bigeasy@linutronix.de>
 *
 */

#include <linux/usb.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/usb/isp1760.h>
#include <linux/usb/hcd.h>

#include "isp1760-hcd.h"

#if defined(CONFIG_OF) && defined(CONFIG_OF_IRQ)
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#endif

#ifdef CONFIG_PCI
#include <linux/pci.h>
#endif

#if defined(CONFIG_OF) && defined(CONFIG_OF_IRQ)
struct isp1760 {
	struct usb_hcd *hcd;
	int rst_gpio;
};

static int of_isp1760_probe(struct platform_device *dev)
{
	struct isp1760 *drvdata;
	struct device_node *dp = dev->dev.of_node;
	struct resource *res;
	struct resource memory;
	int virq;
	resource_size_t res_len;
	int ret;
	unsigned int devflags = 0;
	enum of_gpio_flags gpio_flags;
	u32 bus_width = 0;

	drvdata = kzalloc(sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	ret = of_address_to_resource(dp, 0, &memory);
	if (ret) {
		ret = -ENXIO;
		goto free_data;
	}

	res_len = resource_size(&memory);

	res = request_mem_region(memory.start, res_len, dev_name(&dev->dev));
	if (!res) {
		ret = -EBUSY;
		goto free_data;
	}

	virq = irq_of_parse_and_map(dp, 0);
	if (!virq) {
		ret = -ENODEV;
		goto release_reg;
	}

	if (of_device_is_compatible(dp, "nxp,usb-isp1761"))
		devflags |= ISP1760_FLAG_ISP1761;

	/* Some systems wire up only 16 of the 32 data lines */
	of_property_read_u32(dp, "bus-width", &bus_width);
	if (bus_width == 16)
		devflags |= ISP1760_FLAG_BUS_WIDTH_16;

	if (of_get_property(dp, "port1-otg", NULL) != NULL)
		devflags |= ISP1760_FLAG_OTG_EN;

	if (of_get_property(dp, "analog-oc", NULL) != NULL)
		devflags |= ISP1760_FLAG_ANALOG_OC;

	if (of_get_property(dp, "dack-polarity", NULL) != NULL)
		devflags |= ISP1760_FLAG_DACK_POL_HIGH;

	if (of_get_property(dp, "dreq-polarity", NULL) != NULL)
		devflags |= ISP1760_FLAG_DREQ_POL_HIGH;

	drvdata->rst_gpio = of_get_gpio_flags(dp, 0, &gpio_flags);
	if (gpio_is_valid(drvdata->rst_gpio)) {
		ret = gpio_request(drvdata->rst_gpio, dev_name(&dev->dev));
		if (!ret) {
			if (!(gpio_flags & OF_GPIO_ACTIVE_LOW)) {
				devflags |= ISP1760_FLAG_RESET_ACTIVE_HIGH;
				gpio_direction_output(drvdata->rst_gpio, 0);
			} else {
				gpio_direction_output(drvdata->rst_gpio, 1);
			}
		} else {
			drvdata->rst_gpio = ret;
		}
	}

	drvdata->hcd = isp1760_register(memory.start, res_len, virq,
					IRQF_SHARED, drvdata->rst_gpio,
					&dev->dev, dev_name(&dev->dev),
					devflags);
	if (IS_ERR(drvdata->hcd)) {
		ret = PTR_ERR(drvdata->hcd);
		goto free_gpio;
	}

	platform_set_drvdata(dev, drvdata);
	return ret;

free_gpio:
	if (gpio_is_valid(drvdata->rst_gpio))
		gpio_free(drvdata->rst_gpio);
release_reg:
	release_mem_region(memory.start, res_len);
free_data:
	kfree(drvdata);
	return ret;
}

static int of_isp1760_remove(struct platform_device *dev)
{
	struct isp1760 *drvdata = platform_get_drvdata(dev);

	usb_remove_hcd(drvdata->hcd);
	iounmap(drvdata->hcd->regs);
	release_mem_region(drvdata->hcd->rsrc_start, drvdata->hcd->rsrc_len);
	usb_put_hcd(drvdata->hcd);

	if (gpio_is_valid(drvdata->rst_gpio))
		gpio_free(drvdata->rst_gpio);

	kfree(drvdata);
	return 0;
}

static const struct of_device_id of_isp1760_match[] = {
	{
		.compatible = "nxp,usb-isp1760",
	},
	{
		.compatible = "nxp,usb-isp1761",
	},
	{ },
};
MODULE_DEVICE_TABLE(of, of_isp1760_match);

static struct platform_driver isp1760_of_driver = {
	.driver = {
		.name = "nxp-isp1760",
		.owner = THIS_MODULE,
		.of_match_table = of_isp1760_match,
	},
	.probe          = of_isp1760_probe,
	.remove         = of_isp1760_remove,
};
#endif

#ifdef CONFIG_PCI
static int isp1761_pci_probe(struct pci_dev *dev,
		const struct pci_device_id *id)
{
	u8 latency, limit;
	__u32 reg_data;
	int retry_count;
	struct usb_hcd *hcd;
	unsigned int devflags = 0;
	int ret_status = 0;

	resource_size_t pci_mem_phy0;
	resource_size_t memlength;

	u8 __iomem *chip_addr;
	u8 __iomem *iobase;
	resource_size_t nxp_pci_io_base;
	resource_size_t iolength;

	if (usb_disabled())
		return -ENODEV;

	if (pci_enable_device(dev) < 0)
		return -ENODEV;

	if (!dev->irq)
		return -ENODEV;

	/* Grab the PLX PCI mem maped port start address we need  */
	nxp_pci_io_base = pci_resource_start(dev, 0);
	iolength = pci_resource_len(dev, 0);

	if (!request_mem_region(nxp_pci_io_base, iolength, "ISP1761 IO MEM")) {
		printk(KERN_ERR "request region #1\n");
		return -EBUSY;
	}

	iobase = ioremap_nocache(nxp_pci_io_base, iolength);
	if (!iobase) {
		printk(KERN_ERR "ioremap #1\n");
		ret_status = -ENOMEM;
		goto cleanup1;
	}
	/* Grab the PLX PCI shared memory of the ISP 1761 we need  */
	pci_mem_phy0 = pci_resource_start(dev, 3);
	memlength = pci_resource_len(dev, 3);
	if (memlength < 0xffff) {
		printk(KERN_ERR "memory length for this resource is wrong\n");
		ret_status = -ENOMEM;
		goto cleanup2;
	}

	if (!request_mem_region(pci_mem_phy0, memlength, "ISP-PCI")) {
		printk(KERN_ERR "host controller already in use\n");
		ret_status = -EBUSY;
		goto cleanup2;
	}

	/* map available memory */
	chip_addr = ioremap_nocache(pci_mem_phy0,memlength);
	if (!chip_addr) {
		printk(KERN_ERR "Error ioremap failed\n");
		ret_status = -ENOMEM;
		goto cleanup3;
	}

	/* bad pci latencies can contribute to overruns */
	pci_read_config_byte(dev, PCI_LATENCY_TIMER, &latency);
	if (latency) {
		pci_read_config_byte(dev, PCI_MAX_LAT, &limit);
		if (limit && limit < latency)
			pci_write_config_byte(dev, PCI_LATENCY_TIMER, limit);
	}

	/* Try to check whether we can access Scratch Register of
	 * Host Controller or not. The initial PCI access is retried until
	 * local init for the PCI bridge is completed
	 */
	retry_count = 20;
	reg_data = 0;
	while ((reg_data != 0xFACE) && retry_count) {
		/*by default host is in 16bit mode, so
		 * io operations at this stage must be 16 bit
		 * */
		writel(0xface, chip_addr + HC_SCRATCH_REG);
		udelay(100);
		reg_data = readl(chip_addr + HC_SCRATCH_REG) & 0x0000ffff;
		retry_count--;
	}

	iounmap(chip_addr);

	/* Host Controller presence is detected by writing to scratch register
	 * and reading back and checking the contents are same or not
	 */
	if (reg_data != 0xFACE) {
		dev_err(&dev->dev, "scratch register mismatch %x\n", reg_data);
		ret_status = -ENOMEM;
		goto cleanup3;
	}

	pci_set_master(dev);

	/* configure PLX PCI chip to pass interrupts */
#define PLX_INT_CSR_REG 0x68
	reg_data = readl(iobase + PLX_INT_CSR_REG);
	reg_data |= 0x900;
	writel(reg_data, iobase + PLX_INT_CSR_REG);

	dev->dev.dma_mask = NULL;
	hcd = isp1760_register(pci_mem_phy0, memlength, dev->irq,
		IRQF_SHARED, -ENOENT, &dev->dev, dev_name(&dev->dev),
		devflags);
	if (IS_ERR(hcd)) {
		ret_status = -ENODEV;
		goto cleanup3;
	}

	/* done with PLX IO access */
	iounmap(iobase);
	release_mem_region(nxp_pci_io_base, iolength);

	pci_set_drvdata(dev, hcd);
	return 0;

cleanup3:
	release_mem_region(pci_mem_phy0, memlength);
cleanup2:
	iounmap(iobase);
cleanup1:
	release_mem_region(nxp_pci_io_base, iolength);
	return ret_status;
}

static void isp1761_pci_remove(struct pci_dev *dev)
{
	struct usb_hcd *hcd;

	hcd = pci_get_drvdata(dev);

	usb_remove_hcd(hcd);
	iounmap(hcd->regs);
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
	usb_put_hcd(hcd);

	pci_disable_device(dev);
}

static void isp1761_pci_shutdown(struct pci_dev *dev)
{
	printk(KERN_ERR "ips1761_pci_shutdown\n");
}

static const struct pci_device_id isp1760_plx [] = {
	{
		.class          = PCI_CLASS_BRIDGE_OTHER << 8,
		.class_mask     = ~0,
		.vendor		= PCI_VENDOR_ID_PLX,
		.device		= 0x5406,
		.subvendor	= PCI_VENDOR_ID_PLX,
		.subdevice	= 0x9054,
	},
	{ }
};
MODULE_DEVICE_TABLE(pci, isp1760_plx);

static struct pci_driver isp1761_pci_driver = {
	.name =         "isp1760",
	.id_table =     isp1760_plx,
	.probe =        isp1761_pci_probe,
	.remove =       isp1761_pci_remove,
	.shutdown =     isp1761_pci_shutdown,
};
#endif

static int isp1760_plat_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct usb_hcd *hcd;
	struct resource *mem_res;
	struct resource *irq_res;
	resource_size_t mem_size;
	struct isp1760_platform_data *priv = dev_get_platdata(&pdev->dev);
	unsigned int devflags = 0;
	unsigned long irqflags = IRQF_SHARED;

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem_res) {
		pr_warning("isp1760: Memory resource not available\n");
		ret = -ENODEV;
		goto out;
	}
	mem_size = resource_size(mem_res);
	if (!request_mem_region(mem_res->start, mem_size, "isp1760")) {
		pr_warning("isp1760: Cannot reserve the memory resource\n");
		ret = -EBUSY;
		goto out;
	}

	irq_res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!irq_res) {
		pr_warning("isp1760: IRQ resource not available\n");
		ret = -ENODEV;
		goto cleanup;
	}

	irqflags |= irq_res->flags & IRQF_TRIGGER_MASK;

	if (priv) {
		if (priv->is_isp1761)
			devflags |= ISP1760_FLAG_ISP1761;
		if (priv->bus_width_16)
			devflags |= ISP1760_FLAG_BUS_WIDTH_16;
		if (priv->port1_otg)
			devflags |= ISP1760_FLAG_OTG_EN;
		if (priv->analog_oc)
			devflags |= ISP1760_FLAG_ANALOG_OC;
		if (priv->dack_polarity_high)
			devflags |= ISP1760_FLAG_DACK_POL_HIGH;
		if (priv->dreq_polarity_high)
			devflags |= ISP1760_FLAG_DREQ_POL_HIGH;
	}

	hcd = isp1760_register(mem_res->start, mem_size, irq_res->start,
			       irqflags, -ENOENT,
			       &pdev->dev, dev_name(&pdev->dev), devflags);

	platform_set_drvdata(pdev, hcd);

	if (IS_ERR(hcd)) {
		pr_warning("isp1760: Failed to register the HCD device\n");
		ret = -ENODEV;
		goto cleanup;
	}

	pr_info("ISP1760 USB device initialised\n");
	return ret;

cleanup:
	release_mem_region(mem_res->start, mem_size);
out:
	return ret;
}

static int isp1760_plat_remove(struct platform_device *pdev)
{
	struct resource *mem_res;
	resource_size_t mem_size;
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	usb_remove_hcd(hcd);

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mem_size = resource_size(mem_res);
	release_mem_region(mem_res->start, mem_size);

	usb_put_hcd(hcd);

	return 0;
}

static struct platform_driver isp1760_plat_driver = {
	.probe	= isp1760_plat_probe,
	.remove	= isp1760_plat_remove,
	.driver	= {
		.name	= "isp1760",
	},
};

static int __init isp1760_init(void)
{
	int ret, any_ret = -ENODEV;

	init_kmem_once();

	ret = platform_driver_register(&isp1760_plat_driver);
	if (!ret)
		any_ret = 0;
#if defined(CONFIG_OF) && defined(CONFIG_OF_IRQ)
	ret = platform_driver_register(&isp1760_of_driver);
	if (!ret)
		any_ret = 0;
#endif
#ifdef CONFIG_PCI
	ret = pci_register_driver(&isp1761_pci_driver);
	if (!ret)
		any_ret = 0;
#endif

	if (any_ret)
		deinit_kmem_cache();
	return any_ret;
}
module_init(isp1760_init);

static void __exit isp1760_exit(void)
{
	platform_driver_unregister(&isp1760_plat_driver);
#if defined(CONFIG_OF) && defined(CONFIG_OF_IRQ)
	platform_driver_unregister(&isp1760_of_driver);
#endif
#ifdef CONFIG_PCI
	pci_unregister_driver(&isp1761_pci_driver);
#endif
	deinit_kmem_cache();
}
module_exit(isp1760_exit);
