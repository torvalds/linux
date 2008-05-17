/*
 * Glue code for the ISP1760 driver and bus
 * Currently there is support for
 * - OpenFirmware
 * - PCI
 *
 * (c) 2007 Sebastian Siewior <bigeasy@linutronix.de>
 *
 */

#include <linux/usb.h>
#include <linux/io.h>

#include "../core/hcd.h"
#include "isp1760-hcd.h"

#ifdef CONFIG_USB_ISP1760_OF
#include <linux/of.h>
#include <linux/of_platform.h>
#endif

#ifdef CONFIG_USB_ISP1760_PCI
#include <linux/pci.h>
#endif

#ifdef CONFIG_USB_ISP1760_OF
static int of_isp1760_probe(struct of_device *dev,
		const struct of_device_id *match)
{
	struct usb_hcd *hcd;
	struct device_node *dp = dev->node;
	struct resource *res;
	struct resource memory;
	struct of_irq oirq;
	int virq;
	u64 res_len;
	int ret;

	ret = of_address_to_resource(dp, 0, &memory);
	if (ret)
		return -ENXIO;

	res = request_mem_region(memory.start, memory.end - memory.start + 1,
			dev->dev.bus_id);
	if (!res)
		return -EBUSY;

	res_len = memory.end - memory.start + 1;

	if (of_irq_map_one(dp, 0, &oirq)) {
		ret = -ENODEV;
		goto release_reg;
	}

	virq = irq_create_of_mapping(oirq.controller, oirq.specifier,
			oirq.size);

	hcd = isp1760_register(memory.start, res_len, virq,
		IRQF_SHARED | IRQF_DISABLED, &dev->dev, dev->dev.bus_id);
	if (IS_ERR(hcd)) {
		ret = PTR_ERR(hcd);
		goto release_reg;
	}

	dev_set_drvdata(&dev->dev, hcd);
	return ret;

release_reg:
	release_mem_region(memory.start, memory.end - memory.start + 1);
	return ret;
}

static int of_isp1760_remove(struct of_device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(&dev->dev);

	dev_set_drvdata(&dev->dev, NULL);

	usb_remove_hcd(hcd);
	iounmap(hcd->regs);
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
	usb_put_hcd(hcd);
	return 0;
}

static struct of_device_id of_isp1760_match[] = {
	{
		.compatible = "nxp,usb-isp1760",
	},
	{ },
};
MODULE_DEVICE_TABLE(of, of_isp1760_match);

static struct of_platform_driver isp1760_of_driver = {
	.name           = "nxp-isp1760",
	.match_table    = of_isp1760_match,
	.probe          = of_isp1760_probe,
	.remove         = of_isp1760_remove,
};
#endif

#ifdef CONFIG_USB_ISP1760_PCI
static u32 nxp_pci_io_base;
static u32 iolength;
static u32 pci_mem_phy0;
static u32 length;
static u8 *chip_addr;
static u8 *iobase;

static int __devinit isp1761_pci_probe(struct pci_dev *dev,
		const struct pci_device_id *id)
{
	u8 latency, limit;
	__u32 reg_data;
	int retry_count;
	int length;
	int status = 1;
	struct usb_hcd *hcd;

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
		release_mem_region(nxp_pci_io_base, iolength);
		return -ENOMEM;
	}
	/* Grab the PLX PCI shared memory of the ISP 1761 we need  */
	pci_mem_phy0 = pci_resource_start(dev, 3);
	length = pci_resource_len(dev, 3);

	if (length < 0xffff) {
		printk(KERN_ERR "memory length for this resource is less than "
				"required\n");
		release_mem_region(nxp_pci_io_base, iolength);
		iounmap(iobase);
		return  -ENOMEM;
	}

	if (!request_mem_region(pci_mem_phy0, length, "ISP-PCI")) {
		printk(KERN_ERR "host controller already in use\n");
		release_mem_region(nxp_pci_io_base, iolength);
		iounmap(iobase);
		return -EBUSY;
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
		reg_data = readl(chip_addr + HC_SCRATCH_REG);
		retry_count--;
	}

	/* Host Controller presence is detected by writing to scratch register
	 * and reading back and checking the contents are same or not
	 */
	if (reg_data != 0xFACE) {
		err("scratch register mismatch %x", reg_data);
		goto clean;
	}

	pci_set_master(dev);

	status = readl(iobase + 0x68);
	status |= 0x900;
	writel(status, iobase + 0x68);

	dev->dev.dma_mask = NULL;
	hcd = isp1760_register(pci_mem_phy0, length, dev->irq,
		IRQF_SHARED | IRQF_DISABLED, &dev->dev, dev->dev.bus_id);
	pci_set_drvdata(dev, hcd);
	if (!hcd)
		return 0;
clean:
	status = -ENODEV;
	iounmap(iobase);
	release_mem_region(pci_mem_phy0, length);
	release_mem_region(nxp_pci_io_base, iolength);
	return status;
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

	iounmap(iobase);
	iounmap(chip_addr);

	release_mem_region(nxp_pci_io_base, iolength);
	release_mem_region(pci_mem_phy0, length);
}

static void isp1761_pci_shutdown(struct pci_dev *dev)
{
	printk(KERN_ERR "ips1761_pci_shutdown\n");
}

static const struct pci_device_id isp1760_plx [] = { {
	/* handle any USB 2.0 EHCI controller */
	PCI_DEVICE_CLASS(((PCI_CLASS_BRIDGE_OTHER << 8) | (0x06 << 16)), ~0),
		.driver_data = 0,
},
{ /* end: all zeroes */ }
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

static int __init isp1760_init(void)
{
	int ret = -ENODEV;

	init_kmem_once();

#ifdef CONFIG_USB_ISP1760_OF
	ret = of_register_platform_driver(&isp1760_of_driver);
	if (ret) {
		deinit_kmem_cache();
		return ret;
	}
#endif
#ifdef CONFIG_USB_ISP1760_PCI
	ret = pci_register_driver(&isp1761_pci_driver);
	if (ret)
		goto unreg_of;
#endif
	return ret;

#ifdef CONFIG_USB_ISP1760_PCI
unreg_of:
#endif
#ifdef CONFIG_USB_ISP1760_OF
	of_unregister_platform_driver(&isp1760_of_driver);
#endif
	deinit_kmem_cache();
	return ret;
}
module_init(isp1760_init);

static void __exit isp1760_exit(void)
{
#ifdef CONFIG_USB_ISP1760_OF
	of_unregister_platform_driver(&isp1760_of_driver);
#endif
#ifdef CONFIG_USB_ISP1760_PCI
	pci_unregister_driver(&isp1761_pci_driver);
#endif
	deinit_kmem_cache();
}
module_exit(isp1760_exit);
