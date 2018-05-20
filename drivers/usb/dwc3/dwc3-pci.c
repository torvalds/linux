/**
 * dwc3-pci.c - PCI Specific glue layer
 *
 * Copyright (C) 2010-2011 Texas Instruments Incorporated - http://www.ti.com
 *
 * Authors: Felipe Balbi <balbi@ti.com>,
 *	    Sebastian Andrzej Siewior <bigeasy@linutronix.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2  of
 * the License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/acpi.h>

#include "platform_data.h"

#define PCI_DEVICE_ID_SYNOPSYS_HAPSUSB3		0xabcd
#define PCI_DEVICE_ID_SYNOPSYS_HAPSUSB3_AXI	0xabce
#define PCI_DEVICE_ID_SYNOPSYS_HAPSUSB31	0xabcf
#define PCI_DEVICE_ID_INTEL_BYT			0x0f37
#define PCI_DEVICE_ID_INTEL_MRFLD		0x119e
#define PCI_DEVICE_ID_INTEL_BSW			0x22b7
#define PCI_DEVICE_ID_INTEL_SPTLP		0x9d30
#define PCI_DEVICE_ID_INTEL_SPTH		0xa130
#define PCI_DEVICE_ID_INTEL_BXT			0x0aaa
#define PCI_DEVICE_ID_INTEL_APL			0x5aaa
#define PCI_DEVICE_ID_INTEL_KBP			0xa2b0
#define PCI_DEVICE_ID_INTEL_GLK			0x31aa

static const struct acpi_gpio_params reset_gpios = { 0, 0, false };
static const struct acpi_gpio_params cs_gpios = { 1, 0, false };

static const struct acpi_gpio_mapping acpi_dwc3_byt_gpios[] = {
	{ "reset-gpios", &reset_gpios, 1 },
	{ "cs-gpios", &cs_gpios, 1 },
	{ },
};

static int dwc3_pci_quirks(struct pci_dev *pdev)
{
	if (pdev->vendor == PCI_VENDOR_ID_AMD &&
	    pdev->device == PCI_DEVICE_ID_AMD_NL_USB) {
		struct dwc3_platform_data pdata;

		memset(&pdata, 0, sizeof(pdata));

		pdata.has_lpm_erratum = true;
		pdata.lpm_nyet_threshold = 0xf;

		pdata.u2exit_lfps_quirk = true;
		pdata.u2ss_inp3_quirk = true;
		pdata.req_p1p2p3_quirk = true;
		pdata.del_p1p2p3_quirk = true;
		pdata.del_phy_power_chg_quirk = true;
		pdata.lfps_filter_quirk = true;
		pdata.rx_detect_poll_quirk = true;

		pdata.tx_de_emphasis_quirk = true;
		pdata.tx_de_emphasis = 1;

		/*
		 * FIXME these quirks should be removed when AMD NL
		 * taps out
		 */
		pdata.disable_scramble_quirk = true;
		pdata.dis_u3_susphy_quirk = true;
		pdata.dis_u2_susphy_quirk = true;

		return platform_device_add_data(pci_get_drvdata(pdev), &pdata,
						sizeof(pdata));
	}

	if (pdev->vendor == PCI_VENDOR_ID_INTEL &&
	    pdev->device == PCI_DEVICE_ID_INTEL_BYT) {
		struct gpio_desc *gpio;

		acpi_dev_add_driver_gpios(ACPI_COMPANION(&pdev->dev),
					  acpi_dwc3_byt_gpios);

		/*
		 * These GPIOs will turn on the USB2 PHY. Note that we have to
		 * put the gpio descriptors again here because the phy driver
		 * might want to grab them, too.
		 */
		gpio = gpiod_get_optional(&pdev->dev, "cs", GPIOD_OUT_LOW);
		if (IS_ERR(gpio))
			return PTR_ERR(gpio);

		gpiod_set_value_cansleep(gpio, 1);
		gpiod_put(gpio);

		gpio = gpiod_get_optional(&pdev->dev, "reset", GPIOD_OUT_LOW);
		if (IS_ERR(gpio))
			return PTR_ERR(gpio);

		if (gpio) {
			gpiod_set_value_cansleep(gpio, 1);
			gpiod_put(gpio);
			usleep_range(10000, 11000);
		}
	}

	if (pdev->vendor == PCI_VENDOR_ID_SYNOPSYS &&
	    (pdev->device == PCI_DEVICE_ID_SYNOPSYS_HAPSUSB3 ||
	     pdev->device == PCI_DEVICE_ID_SYNOPSYS_HAPSUSB3_AXI ||
	     pdev->device == PCI_DEVICE_ID_SYNOPSYS_HAPSUSB31)) {

		struct dwc3_platform_data pdata;

		memset(&pdata, 0, sizeof(pdata));
		pdata.usb3_lpm_capable = true;
		pdata.has_lpm_erratum = true;
		pdata.dis_enblslpm_quirk = true;

		return platform_device_add_data(pci_get_drvdata(pdev), &pdata,
						sizeof(pdata));
	}

	return 0;
}

static int dwc3_pci_probe(struct pci_dev *pci,
		const struct pci_device_id *id)
{
	struct resource		res[2];
	struct platform_device	*dwc3;
	int			ret;
	struct device		*dev = &pci->dev;

	ret = pcim_enable_device(pci);
	if (ret) {
		dev_err(dev, "failed to enable pci device\n");
		return -ENODEV;
	}

	pci_set_master(pci);

	dwc3 = platform_device_alloc("dwc3", PLATFORM_DEVID_AUTO);
	if (!dwc3) {
		dev_err(dev, "couldn't allocate dwc3 device\n");
		return -ENOMEM;
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
		goto err;
	}

	pci_set_drvdata(pci, dwc3);
	ret = dwc3_pci_quirks(pci);
	if (ret)
		goto err;

	dwc3->dev.parent = dev;
	ACPI_COMPANION_SET(&dwc3->dev, ACPI_COMPANION(dev));

	ret = platform_device_add(dwc3);
	if (ret) {
		dev_err(dev, "failed to register dwc3 device\n");
		goto err;
	}

	return 0;
err:
	platform_device_put(dwc3);
	return ret;
}

static void dwc3_pci_remove(struct pci_dev *pci)
{
	acpi_dev_remove_driver_gpios(ACPI_COMPANION(&pci->dev));
	platform_device_unregister(pci_get_drvdata(pci));
}

static const struct pci_device_id dwc3_pci_id_table[] = {
	{
		PCI_DEVICE(PCI_VENDOR_ID_SYNOPSYS,
				PCI_DEVICE_ID_SYNOPSYS_HAPSUSB3),
	},
	{
		PCI_DEVICE(PCI_VENDOR_ID_SYNOPSYS,
				PCI_DEVICE_ID_SYNOPSYS_HAPSUSB3_AXI),
	},
	{
		PCI_DEVICE(PCI_VENDOR_ID_SYNOPSYS,
				PCI_DEVICE_ID_SYNOPSYS_HAPSUSB31),
	},
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_BSW), },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_BYT), },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_MRFLD), },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_SPTLP), },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_SPTH), },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_BXT), },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_APL), },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_KBP), },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_GLK), },
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_NL_USB), },
	{  }	/* Terminating Entry */
};
MODULE_DEVICE_TABLE(pci, dwc3_pci_id_table);

static struct pci_driver dwc3_pci_driver = {
	.name		= "dwc3-pci",
	.id_table	= dwc3_pci_id_table,
	.probe		= dwc3_pci_probe,
	.remove		= dwc3_pci_remove,
};

MODULE_AUTHOR("Felipe Balbi <balbi@ti.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DesignWare USB3 PCI Glue Layer");

module_pci_driver(dwc3_pci_driver);
