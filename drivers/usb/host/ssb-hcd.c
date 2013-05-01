/*
 * Sonics Silicon Backplane
 * Broadcom USB-core driver  (SSB bus glue)
 *
 * Copyright 2011-2012 Hauke Mehrtens <hauke@hauke-m.de>
 *
 * Based on ssb-ohci driver
 * Copyright 2007 Michael Buesch <m@bues.ch>
 *
 * Derived from the OHCI-PCI driver
 * Copyright 1999 Roman Weissgaerber
 * Copyright 2000-2002 David Brownell
 * Copyright 1999 Linus Torvalds
 * Copyright 1999 Gregory P. Smith
 *
 * Derived from the USBcore related parts of Broadcom-SB
 * Copyright 2005-2011 Broadcom Corporation
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */
#include <linux/ssb/ssb.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb/ehci_pdriver.h>
#include <linux/usb/ohci_pdriver.h>

MODULE_AUTHOR("Hauke Mehrtens");
MODULE_DESCRIPTION("Common USB driver for SSB Bus");
MODULE_LICENSE("GPL");

#define SSB_HCD_TMSLOW_HOSTMODE	(1 << 29)

struct ssb_hcd_device {
	struct platform_device *ehci_dev;
	struct platform_device *ohci_dev;

	u32 enable_flags;
};

static void ssb_hcd_5354wa(struct ssb_device *dev)
{
#ifdef CONFIG_SSB_DRIVER_MIPS
	/* Work around for 5354 failures */
	if (dev->id.revision == 2 && dev->bus->chip_id == 0x5354) {
		/* Change syn01 reg */
		ssb_write32(dev, 0x894, 0x00fe00fe);

		/* Change syn03 reg */
		ssb_write32(dev, 0x89c, ssb_read32(dev, 0x89c) | 0x1);
	}
#endif
}

static void ssb_hcd_usb20wa(struct ssb_device *dev)
{
	if (dev->id.coreid == SSB_DEV_USB20_HOST) {
		/*
		 * USB 2.0 special considerations:
		 *
		 * In addition to the standard SSB reset sequence, the Host
		 * Control Register must be programmed to bring the USB core
		 * and various phy components out of reset.
		 */
		ssb_write32(dev, 0x200, 0x7ff);

		/* Change Flush control reg */
		ssb_write32(dev, 0x400, ssb_read32(dev, 0x400) & ~8);
		ssb_read32(dev, 0x400);

		/* Change Shim control reg */
		ssb_write32(dev, 0x304, ssb_read32(dev, 0x304) & ~0x100);
		ssb_read32(dev, 0x304);

		udelay(1);

		ssb_hcd_5354wa(dev);
	}
}

/* based on arch/mips/brcm-boards/bcm947xx/pcibios.c */
static u32 ssb_hcd_init_chip(struct ssb_device *dev)
{
	u32 flags = 0;

	if (dev->id.coreid == SSB_DEV_USB11_HOSTDEV)
		/* Put the device into host-mode. */
		flags |= SSB_HCD_TMSLOW_HOSTMODE;

	ssb_device_enable(dev, flags);

	ssb_hcd_usb20wa(dev);

	return flags;
}

static const struct usb_ehci_pdata ehci_pdata = {
};

static const struct usb_ohci_pdata ohci_pdata = {
};

static struct platform_device *ssb_hcd_create_pdev(struct ssb_device *dev, bool ohci, u32 addr, u32 len)
{
	struct platform_device *hci_dev;
	struct resource hci_res[2];
	int ret = -ENOMEM;

	memset(hci_res, 0, sizeof(hci_res));

	hci_res[0].start = addr;
	hci_res[0].end = hci_res[0].start + len - 1;
	hci_res[0].flags = IORESOURCE_MEM;

	hci_res[1].start = dev->irq;
	hci_res[1].flags = IORESOURCE_IRQ;

	hci_dev = platform_device_alloc(ohci ? "ohci-platform" :
					"ehci-platform" , 0);
	if (!hci_dev)
		return NULL;

	hci_dev->dev.parent = dev->dev;
	hci_dev->dev.dma_mask = &hci_dev->dev.coherent_dma_mask;

	ret = platform_device_add_resources(hci_dev, hci_res,
					    ARRAY_SIZE(hci_res));
	if (ret)
		goto err_alloc;
	if (ohci)
		ret = platform_device_add_data(hci_dev, &ohci_pdata,
					       sizeof(ohci_pdata));
	else
		ret = platform_device_add_data(hci_dev, &ehci_pdata,
					       sizeof(ehci_pdata));
	if (ret)
		goto err_alloc;
	ret = platform_device_add(hci_dev);
	if (ret)
		goto err_alloc;

	return hci_dev;

err_alloc:
	platform_device_put(hci_dev);
	return ERR_PTR(ret);
}

static int ssb_hcd_probe(struct ssb_device *dev,
				   const struct ssb_device_id *id)
{
	int err, tmp;
	int start, len;
	u16 chipid_top;
	u16 coreid = dev->id.coreid;
	struct ssb_hcd_device *usb_dev;

	/* USBcores are only connected on embedded devices. */
	chipid_top = (dev->bus->chip_id & 0xFF00);
	if (chipid_top != 0x4700 && chipid_top != 0x5300)
		return -ENODEV;

	/* TODO: Probably need checks here; is the core connected? */

	if (dma_set_mask(dev->dma_dev, DMA_BIT_MASK(32)) ||
	    dma_set_coherent_mask(dev->dma_dev, DMA_BIT_MASK(32)))
		return -EOPNOTSUPP;

	usb_dev = kzalloc(sizeof(struct ssb_hcd_device), GFP_KERNEL);
	if (!usb_dev)
		return -ENOMEM;

	/* We currently always attach SSB_DEV_USB11_HOSTDEV
	 * as HOST OHCI. If we want to attach it as Client device,
	 * we must branch here and call into the (yet to
	 * be written) Client mode driver. Same for remove(). */
	usb_dev->enable_flags = ssb_hcd_init_chip(dev);

	tmp = ssb_read32(dev, SSB_ADMATCH0);

	start = ssb_admatch_base(tmp);
	len = (coreid == SSB_DEV_USB20_HOST) ? 0x800 : ssb_admatch_size(tmp);
	usb_dev->ohci_dev = ssb_hcd_create_pdev(dev, true, start, len);
	if (IS_ERR(usb_dev->ohci_dev)) {
		err = PTR_ERR(usb_dev->ohci_dev);
		goto err_free_usb_dev;
	}

	if (coreid == SSB_DEV_USB20_HOST) {
		start = ssb_admatch_base(tmp) + 0x800; /* ehci core offset */
		usb_dev->ehci_dev = ssb_hcd_create_pdev(dev, false, start, len);
		if (IS_ERR(usb_dev->ehci_dev)) {
			err = PTR_ERR(usb_dev->ehci_dev);
			goto err_unregister_ohci_dev;
		}
	}

	ssb_set_drvdata(dev, usb_dev);
	return 0;

err_unregister_ohci_dev:
	platform_device_unregister(usb_dev->ohci_dev);
err_free_usb_dev:
	kfree(usb_dev);
	return err;
}

static void ssb_hcd_remove(struct ssb_device *dev)
{
	struct ssb_hcd_device *usb_dev = ssb_get_drvdata(dev);
	struct platform_device *ohci_dev = usb_dev->ohci_dev;
	struct platform_device *ehci_dev = usb_dev->ehci_dev;

	if (ohci_dev)
		platform_device_unregister(ohci_dev);
	if (ehci_dev)
		platform_device_unregister(ehci_dev);

	ssb_device_disable(dev, 0);
}

static void ssb_hcd_shutdown(struct ssb_device *dev)
{
	ssb_device_disable(dev, 0);
}

#ifdef CONFIG_PM

static int ssb_hcd_suspend(struct ssb_device *dev, pm_message_t state)
{
	ssb_device_disable(dev, 0);

	return 0;
}

static int ssb_hcd_resume(struct ssb_device *dev)
{
	struct ssb_hcd_device *usb_dev = ssb_get_drvdata(dev);

	ssb_device_enable(dev, usb_dev->enable_flags);

	return 0;
}

#else /* !CONFIG_PM */
#define ssb_hcd_suspend	NULL
#define ssb_hcd_resume	NULL
#endif /* CONFIG_PM */

static const struct ssb_device_id ssb_hcd_table[] = {
	SSB_DEVICE(SSB_VENDOR_BROADCOM, SSB_DEV_USB11_HOSTDEV, SSB_ANY_REV),
	SSB_DEVICE(SSB_VENDOR_BROADCOM, SSB_DEV_USB11_HOST, SSB_ANY_REV),
	SSB_DEVICE(SSB_VENDOR_BROADCOM, SSB_DEV_USB20_HOST, SSB_ANY_REV),
	SSB_DEVTABLE_END
};
MODULE_DEVICE_TABLE(ssb, ssb_hcd_table);

static struct ssb_driver ssb_hcd_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= ssb_hcd_table,
	.probe		= ssb_hcd_probe,
	.remove		= ssb_hcd_remove,
	.shutdown	= ssb_hcd_shutdown,
	.suspend	= ssb_hcd_suspend,
	.resume		= ssb_hcd_resume,
};

static int __init ssb_hcd_init(void)
{
	return ssb_driver_register(&ssb_hcd_driver);
}
module_init(ssb_hcd_init);

static void __exit ssb_hcd_exit(void)
{
	ssb_driver_unregister(&ssb_hcd_driver);
}
module_exit(ssb_hcd_exit);
