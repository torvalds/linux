// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * RDC321x MFD southbridge driver
 *
 * Copyright (C) 2007-2010 Florian Fainelli <florian@openwrt.org>
 * Copyright (C) 2010 Bernhard Loos <bernhardloos@googlemail.com>
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/pci.h>
#include <linux/mfd/core.h>
#include <linux/mfd/rdc321x.h>

static struct rdc321x_wdt_pdata rdc321x_wdt_pdata;

static struct resource rdc321x_wdt_resource[] = {
	{
		.name	= "wdt-reg",
		.start	= RDC321X_WDT_CTRL,
		.end	= RDC321X_WDT_CTRL + 0x3,
		.flags	= IORESOURCE_IO,
	}
};

static struct rdc321x_gpio_pdata rdc321x_gpio_pdata = {
	.max_gpios	= RDC321X_NUM_GPIO,
};

static struct resource rdc321x_gpio_resources[] = {
	{
		.name	= "gpio-reg1",
		.start	= RDC321X_GPIO_CTRL_REG1,
		.end	= RDC321X_GPIO_CTRL_REG1 + 0x7,
		.flags	= IORESOURCE_IO,
	}, {
		.name	= "gpio-reg2",
		.start	= RDC321X_GPIO_CTRL_REG2,
		.end	= RDC321X_GPIO_CTRL_REG2 + 0x7,
		.flags	= IORESOURCE_IO,
	}
};

static const struct mfd_cell rdc321x_sb_cells[] = {
	{
		.name		= "rdc321x-wdt",
		.resources	= rdc321x_wdt_resource,
		.num_resources	= ARRAY_SIZE(rdc321x_wdt_resource),
		.platform_data	= &rdc321x_wdt_pdata,
		.pdata_size	= sizeof(rdc321x_wdt_pdata),
	}, {
		.name		= "rdc321x-gpio",
		.resources	= rdc321x_gpio_resources,
		.num_resources	= ARRAY_SIZE(rdc321x_gpio_resources),
		.platform_data	= &rdc321x_gpio_pdata,
		.pdata_size	= sizeof(rdc321x_gpio_pdata),
	},
};

static int rdc321x_sb_probe(struct pci_dev *pdev,
					const struct pci_device_id *ent)
{
	int err;

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "failed to enable device\n");
		return err;
	}

	rdc321x_gpio_pdata.sb_pdev = pdev;
	rdc321x_wdt_pdata.sb_pdev = pdev;

	return devm_mfd_add_devices(&pdev->dev, -1,
				    rdc321x_sb_cells,
				    ARRAY_SIZE(rdc321x_sb_cells),
				    NULL, 0, NULL);
}

static const struct pci_device_id rdc321x_sb_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_RDC, PCI_DEVICE_ID_RDC_R6030) },
	{}
};
MODULE_DEVICE_TABLE(pci, rdc321x_sb_table);

static struct pci_driver rdc321x_sb_driver = {
	.name		= "RDC321x Southbridge",
	.id_table	= rdc321x_sb_table,
	.probe		= rdc321x_sb_probe,
};

module_pci_driver(rdc321x_sb_driver);

MODULE_AUTHOR("Florian Fainelli <florian@openwrt.org>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("RDC R-321x MFD southbridge driver");
