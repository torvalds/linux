/*
 * pci.c - DesignWare HS OTG Controller PCI driver
 *
 * Copyright (C) 2004-2013 Synopsys, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the above-listed copyright holders may not be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Provides the initialization and cleanup entry points for the DWC_otg PCI
 * driver
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/usb.h>

#include <linux/usb/hcd.h>
#include <linux/usb/ch11.h>
#include <linux/platform_device.h>
#include <linux/usb/usb_phy_generic.h>

#define PCI_PRODUCT_ID_HAPS_HSOTG	0xabc0

static const char dwc2_driver_name[] = "dwc2-pci";

struct dwc2_pci_glue {
	struct platform_device *dwc2;
	struct platform_device *phy;
};

static int dwc2_pci_quirks(struct pci_dev *pdev, struct platform_device *dwc2)
{
	if (pdev->vendor == PCI_VENDOR_ID_SYNOPSYS &&
	    pdev->device == PCI_PRODUCT_ID_HAPS_HSOTG) {
		struct property_entry properties[] = {
			{ },
		};

		return platform_device_add_properties(dwc2, properties);
	}

	return 0;
}

static void dwc2_pci_remove(struct pci_dev *pci)
{
	struct dwc2_pci_glue *glue = pci_get_drvdata(pci);

	platform_device_unregister(glue->dwc2);
	usb_phy_generic_unregister(glue->phy);
	kfree(glue);
	pci_set_drvdata(pci, NULL);
}

static int dwc2_pci_probe(struct pci_dev *pci,
			  const struct pci_device_id *id)
{
	struct resource		res[2];
	struct platform_device	*dwc2;
	struct platform_device	*phy;
	int			ret;
	struct device		*dev = &pci->dev;
	struct dwc2_pci_glue	*glue;

	ret = pcim_enable_device(pci);
	if (ret) {
		dev_err(dev, "failed to enable pci device\n");
		return -ENODEV;
	}

	pci_set_master(pci);

	dwc2 = platform_device_alloc("dwc2", PLATFORM_DEVID_AUTO);
	if (!dwc2) {
		dev_err(dev, "couldn't allocate dwc2 device\n");
		return -ENOMEM;
	}

	memset(res, 0x00, sizeof(struct resource) * ARRAY_SIZE(res));

	res[0].start	= pci_resource_start(pci, 0);
	res[0].end	= pci_resource_end(pci, 0);
	res[0].name	= "dwc2";
	res[0].flags	= IORESOURCE_MEM;

	res[1].start	= pci->irq;
	res[1].name	= "dwc2";
	res[1].flags	= IORESOURCE_IRQ;

	ret = platform_device_add_resources(dwc2, res, ARRAY_SIZE(res));
	if (ret) {
		dev_err(dev, "couldn't add resources to dwc2 device\n");
		return ret;
	}

	dwc2->dev.parent = dev;

	phy = usb_phy_generic_register();
	if (IS_ERR(phy)) {
		dev_err(dev, "error registering generic PHY (%ld)\n",
			PTR_ERR(phy));
		return PTR_ERR(phy);
	}

	ret = dwc2_pci_quirks(pci, dwc2);
	if (ret)
		goto err;

	ret = platform_device_add(dwc2);
	if (ret) {
		dev_err(dev, "failed to register dwc2 device\n");
		goto err;
	}

	glue = kzalloc(sizeof(*glue), GFP_KERNEL);
	if (!glue)
		return -ENOMEM;

	glue->phy = phy;
	glue->dwc2 = dwc2;
	pci_set_drvdata(pci, glue);

	return 0;
err:
	usb_phy_generic_unregister(phy);
	platform_device_put(dwc2);
	return ret;
}

static const struct pci_device_id dwc2_pci_ids[] = {
	{
		PCI_DEVICE(PCI_VENDOR_ID_SYNOPSYS, PCI_PRODUCT_ID_HAPS_HSOTG),
	},
	{
		PCI_DEVICE(PCI_VENDOR_ID_STMICRO,
			   PCI_DEVICE_ID_STMICRO_USB_OTG),
	},
	{ /* end: all zeroes */ }
};
MODULE_DEVICE_TABLE(pci, dwc2_pci_ids);

static struct pci_driver dwc2_pci_driver = {
	.name = dwc2_driver_name,
	.id_table = dwc2_pci_ids,
	.probe = dwc2_pci_probe,
	.remove = dwc2_pci_remove,
};

module_pci_driver(dwc2_pci_driver);

MODULE_DESCRIPTION("DESIGNWARE HS OTG PCI Bus Glue");
MODULE_AUTHOR("Synopsys, Inc.");
MODULE_LICENSE("Dual BSD/GPL");
