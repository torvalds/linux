/**
 * dwc3-pci.c - PCI Specific glue layer
 *
 * Copyright (C) 2010-2011 Texas Instruments Incorporated - http://www.ti.com
 *
 * Authors: Felipe Balbi <balbi@ti.com>,
 *	    Sebastian Andrzej Siewior <bigeasy@linutronix.de>
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
 * GNU General Public License ("GPL") version 2, as published by the Free
 * Software Foundation.
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/platform_device.h>

#include <linux/usb/otg.h>
#include <linux/usb/nop-usb-xceiv.h>

#include "core.h"

/* FIXME define these in <linux/pci_ids.h> */
#define PCI_VENDOR_ID_SYNOPSYS		0x16c3
#define PCI_DEVICE_ID_SYNOPSYS_HAPSUSB3	0xabcd

struct dwc3_pci {
	struct device		*dev;
	struct platform_device	*dwc3;
	struct platform_device	*usb2_phy;
	struct platform_device	*usb3_phy;
};

static int __devinit dwc3_pci_register_phys(struct dwc3_pci *glue)
{
	struct nop_usb_xceiv_platform_data pdata;
	struct platform_device	*pdev;
	int			ret;

	memset(&pdata, 0x00, sizeof(pdata));

	pdev = platform_device_alloc("nop_usb_xceiv", 0);
	if (!pdev)
		return -ENOMEM;

	glue->usb2_phy = pdev;
	pdata.type = USB_PHY_TYPE_USB2;

	ret = platform_device_add_data(glue->usb2_phy, &pdata, sizeof(pdata));
	if (ret)
		goto err1;

	pdev = platform_device_alloc("nop_usb_xceiv", 1);
	if (!pdev) {
		ret = -ENOMEM;
		goto err1;
	}

	glue->usb3_phy = pdev;
	pdata.type = USB_PHY_TYPE_USB3;

	ret = platform_device_add_data(glue->usb3_phy, &pdata, sizeof(pdata));
	if (ret)
		goto err2;

	ret = platform_device_add(glue->usb2_phy);
	if (ret)
		goto err2;

	ret = platform_device_add(glue->usb3_phy);
	if (ret)
		goto err3;

	return 0;

err3:
	platform_device_del(glue->usb2_phy);

err2:
	platform_device_put(glue->usb3_phy);

err1:
	platform_device_put(glue->usb2_phy);

	return ret;
}

static int __devinit dwc3_pci_probe(struct pci_dev *pci,
		const struct pci_device_id *id)
{
	struct resource		res[2];
	struct platform_device	*dwc3;
	struct dwc3_pci		*glue;
	int			ret = -ENOMEM;
	struct device		*dev = &pci->dev;

	glue = devm_kzalloc(dev, sizeof(*glue), GFP_KERNEL);
	if (!glue) {
		dev_err(dev, "not enough memory\n");
		return -ENOMEM;
	}

	glue->dev = dev;

	ret = pci_enable_device(pci);
	if (ret) {
		dev_err(dev, "failed to enable pci device\n");
		return -ENODEV;
	}

	pci_set_power_state(pci, PCI_D0);
	pci_set_master(pci);

	ret = dwc3_pci_register_phys(glue);
	if (ret) {
		dev_err(dev, "couldn't register PHYs\n");
		return ret;
	}

	dwc3 = platform_device_alloc("dwc3", PLATFORM_DEVID_AUTO);
	if (!dwc3) {
		dev_err(dev, "couldn't allocate dwc3 device\n");
		ret = -ENOMEM;
		goto err1;
	}

	memset(res, 0x00, sizeof(struct resource) * ARRAY_SIZE(res));

	res[0].start	= pci_resource_start(pci, 0);
	res[0].end	= pci_resource_end(pci, 0);
	res[0].name	= "dwc_usb3";
	res[0].flags	= IORESOURCE_MEM;

	res[1].start	= pci->irq;
	res[1].name	= "dwc_usb3";
	res[1].flags	= IORESOURCE_IRQ;

	ret = platform_device_add_resources(dwc3, res, ARRAY_SIZE(res));
	if (ret) {
		dev_err(dev, "couldn't add resources to dwc3 device\n");
		goto err1;
	}

	pci_set_drvdata(pci, glue);

	dma_set_coherent_mask(&dwc3->dev, dev->coherent_dma_mask);

	dwc3->dev.dma_mask = dev->dma_mask;
	dwc3->dev.dma_parms = dev->dma_parms;
	dwc3->dev.parent = dev;
	glue->dwc3 = dwc3;

	ret = platform_device_add(dwc3);
	if (ret) {
		dev_err(dev, "failed to register dwc3 device\n");
		goto err3;
	}

	return 0;

err3:
	pci_set_drvdata(pci, NULL);
	platform_device_put(dwc3);
err1:
	pci_disable_device(pci);

	return ret;
}

static void __devexit dwc3_pci_remove(struct pci_dev *pci)
{
	struct dwc3_pci	*glue = pci_get_drvdata(pci);

	platform_device_unregister(glue->usb2_phy);
	platform_device_unregister(glue->usb3_phy);
	platform_device_unregister(glue->dwc3);
	pci_set_drvdata(pci, NULL);
	pci_disable_device(pci);
}

static DEFINE_PCI_DEVICE_TABLE(dwc3_pci_id_table) = {
	{
		PCI_DEVICE(PCI_VENDOR_ID_SYNOPSYS,
				PCI_DEVICE_ID_SYNOPSYS_HAPSUSB3),
	},
	{  }	/* Terminating Entry */
};
MODULE_DEVICE_TABLE(pci, dwc3_pci_id_table);

static struct pci_driver dwc3_pci_driver = {
	.name		= "dwc3-pci",
	.id_table	= dwc3_pci_id_table,
	.probe		= dwc3_pci_probe,
	.remove		= __devexit_p(dwc3_pci_remove),
};

MODULE_AUTHOR("Felipe Balbi <balbi@ti.com>");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("DesignWare USB3 PCI Glue Layer");

module_pci_driver(dwc3_pci_driver);
