/*
 * Intel Quark MFD PCI driver for I2C & GPIO
 *
 * Copyright(c) 2014 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * Intel Quark PCI device for I2C and GPIO controller sharing the same
 * PCI function. This PCI driver will split the 2 devices into their
 * respective drivers.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/mfd/core.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/dmi.h>
#include <linux/platform_data/gpio-dwapb.h>
#include <linux/platform_data/i2c-designware.h>

/* PCI BAR for register base address */
#define MFD_I2C_BAR		0
#define MFD_GPIO_BAR		1

/* ACPI _ADR value to match the child node */
#define MFD_ACPI_MATCH_GPIO	0ULL
#define MFD_ACPI_MATCH_I2C	1ULL

/* The base GPIO number under GPIOLIB framework */
#define INTEL_QUARK_MFD_GPIO_BASE	8

/* The default number of South-Cluster GPIO on Quark. */
#define INTEL_QUARK_MFD_NGPIO		8

/* The DesignWare GPIO ports on Quark. */
#define INTEL_QUARK_GPIO_NPORTS	1

#define INTEL_QUARK_IORES_MEM	0
#define INTEL_QUARK_IORES_IRQ	1

#define INTEL_QUARK_I2C_CONTROLLER_CLK "i2c_designware.0"

/* The Quark I2C controller source clock */
#define INTEL_QUARK_I2C_CLK_HZ	33000000

struct intel_quark_mfd {
	struct device		*dev;
	struct clk		*i2c_clk;
	struct clk_lookup	*i2c_clk_lookup;
};

static const struct dmi_system_id dmi_platform_info[] = {
	{
		.matches = {
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "Galileo"),
		},
		.driver_data = (void *)100000,
	},
	{
		.matches = {
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "GalileoGen2"),
		},
		.driver_data = (void *)400000,
	},
	{
		.matches = {
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "SIMATIC IOT2000"),
			DMI_EXACT_MATCH(DMI_BOARD_ASSET_TAG,
					"6ES7647-0AA00-0YA2"),
		},
		.driver_data = (void *)400000,
	},
	{
		.matches = {
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "SIMATIC IOT2000"),
			DMI_EXACT_MATCH(DMI_BOARD_ASSET_TAG,
					"6ES7647-0AA00-1YA2"),
		},
		.driver_data = (void *)400000,
	},
	{}
};

static struct resource intel_quark_i2c_res[] = {
	[INTEL_QUARK_IORES_MEM] = {
		.flags = IORESOURCE_MEM,
	},
	[INTEL_QUARK_IORES_IRQ] = {
		.flags = IORESOURCE_IRQ,
	},
};

static struct mfd_cell_acpi_match intel_quark_acpi_match_i2c = {
	.adr = MFD_ACPI_MATCH_I2C,
};

static struct resource intel_quark_gpio_res[] = {
	[INTEL_QUARK_IORES_MEM] = {
		.flags = IORESOURCE_MEM,
	},
};

static struct mfd_cell_acpi_match intel_quark_acpi_match_gpio = {
	.adr = MFD_ACPI_MATCH_GPIO,
};

static struct mfd_cell intel_quark_mfd_cells[] = {
	{
		.id = MFD_GPIO_BAR,
		.name = "gpio-dwapb",
		.acpi_match = &intel_quark_acpi_match_gpio,
		.num_resources = ARRAY_SIZE(intel_quark_gpio_res),
		.resources = intel_quark_gpio_res,
		.ignore_resource_conflicts = true,
	},
	{
		.id = MFD_I2C_BAR,
		.name = "i2c_designware",
		.acpi_match = &intel_quark_acpi_match_i2c,
		.num_resources = ARRAY_SIZE(intel_quark_i2c_res),
		.resources = intel_quark_i2c_res,
		.ignore_resource_conflicts = true,
	},
};

static const struct pci_device_id intel_quark_mfd_ids[] = {
	{ PCI_VDEVICE(INTEL, 0x0934), },
	{},
};
MODULE_DEVICE_TABLE(pci, intel_quark_mfd_ids);

static int intel_quark_register_i2c_clk(struct device *dev)
{
	struct intel_quark_mfd *quark_mfd = dev_get_drvdata(dev);
	struct clk *i2c_clk;

	i2c_clk = clk_register_fixed_rate(dev,
					  INTEL_QUARK_I2C_CONTROLLER_CLK, NULL,
					  0, INTEL_QUARK_I2C_CLK_HZ);
	if (IS_ERR(i2c_clk))
		return PTR_ERR(i2c_clk);

	quark_mfd->i2c_clk = i2c_clk;
	quark_mfd->i2c_clk_lookup = clkdev_create(i2c_clk, NULL,
						INTEL_QUARK_I2C_CONTROLLER_CLK);

	if (!quark_mfd->i2c_clk_lookup) {
		clk_unregister(quark_mfd->i2c_clk);
		dev_err(dev, "Fixed clk register failed\n");
		return -ENOMEM;
	}

	return 0;
}

static void intel_quark_unregister_i2c_clk(struct device *dev)
{
	struct intel_quark_mfd *quark_mfd = dev_get_drvdata(dev);

	if (!quark_mfd->i2c_clk_lookup)
		return;

	clkdev_drop(quark_mfd->i2c_clk_lookup);
	clk_unregister(quark_mfd->i2c_clk);
}

static int intel_quark_i2c_setup(struct pci_dev *pdev, struct mfd_cell *cell)
{
	const struct dmi_system_id *dmi_id;
	struct dw_i2c_platform_data *pdata;
	struct resource *res = (struct resource *)cell->resources;
	struct device *dev = &pdev->dev;

	res[INTEL_QUARK_IORES_MEM].start =
		pci_resource_start(pdev, MFD_I2C_BAR);
	res[INTEL_QUARK_IORES_MEM].end =
		pci_resource_end(pdev, MFD_I2C_BAR);

	res[INTEL_QUARK_IORES_IRQ].start = pdev->irq;
	res[INTEL_QUARK_IORES_IRQ].end = pdev->irq;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	/* Normal mode by default */
	pdata->i2c_scl_freq = 100000;

	dmi_id = dmi_first_match(dmi_platform_info);
	if (dmi_id)
		pdata->i2c_scl_freq = (uintptr_t)dmi_id->driver_data;

	cell->platform_data = pdata;
	cell->pdata_size = sizeof(*pdata);

	return 0;
}

static int intel_quark_gpio_setup(struct pci_dev *pdev, struct mfd_cell *cell)
{
	struct dwapb_platform_data *pdata;
	struct resource *res = (struct resource *)cell->resources;
	struct device *dev = &pdev->dev;

	res[INTEL_QUARK_IORES_MEM].start =
		pci_resource_start(pdev, MFD_GPIO_BAR);
	res[INTEL_QUARK_IORES_MEM].end =
		pci_resource_end(pdev, MFD_GPIO_BAR);

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	/* For intel quark x1000, it has only one port: portA */
	pdata->nports = INTEL_QUARK_GPIO_NPORTS;
	pdata->properties = devm_kcalloc(dev, pdata->nports,
					 sizeof(*pdata->properties),
					 GFP_KERNEL);
	if (!pdata->properties)
		return -ENOMEM;

	/* Set the properties for portA */
	pdata->properties->fwnode	= NULL;
	pdata->properties->idx		= 0;
	pdata->properties->ngpio	= INTEL_QUARK_MFD_NGPIO;
	pdata->properties->gpio_base	= INTEL_QUARK_MFD_GPIO_BASE;
	pdata->properties->irq[0]	= pdev->irq;
	pdata->properties->has_irq	= true;
	pdata->properties->irq_shared	= true;

	cell->platform_data = pdata;
	cell->pdata_size = sizeof(*pdata);

	return 0;
}

static int intel_quark_mfd_probe(struct pci_dev *pdev,
				 const struct pci_device_id *id)
{
	struct intel_quark_mfd *quark_mfd;
	int ret;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	quark_mfd = devm_kzalloc(&pdev->dev, sizeof(*quark_mfd), GFP_KERNEL);
	if (!quark_mfd)
		return -ENOMEM;

	quark_mfd->dev = &pdev->dev;
	dev_set_drvdata(&pdev->dev, quark_mfd);

	ret = intel_quark_register_i2c_clk(&pdev->dev);
	if (ret)
		return ret;

	ret = intel_quark_i2c_setup(pdev, &intel_quark_mfd_cells[1]);
	if (ret)
		goto err_unregister_i2c_clk;

	ret = intel_quark_gpio_setup(pdev, &intel_quark_mfd_cells[0]);
	if (ret)
		goto err_unregister_i2c_clk;

	ret = mfd_add_devices(&pdev->dev, 0, intel_quark_mfd_cells,
			      ARRAY_SIZE(intel_quark_mfd_cells), NULL, 0,
			      NULL);
	if (ret)
		goto err_unregister_i2c_clk;

	return 0;

err_unregister_i2c_clk:
	intel_quark_unregister_i2c_clk(&pdev->dev);
	return ret;
}

static void intel_quark_mfd_remove(struct pci_dev *pdev)
{
	intel_quark_unregister_i2c_clk(&pdev->dev);
	mfd_remove_devices(&pdev->dev);
}

static struct pci_driver intel_quark_mfd_driver = {
	.name		= "intel_quark_mfd_i2c_gpio",
	.id_table	= intel_quark_mfd_ids,
	.probe		= intel_quark_mfd_probe,
	.remove		= intel_quark_mfd_remove,
};

module_pci_driver(intel_quark_mfd_driver);

MODULE_AUTHOR("Raymond Tan <raymond.tan@intel.com>");
MODULE_DESCRIPTION("Intel Quark MFD PCI driver for I2C & GPIO");
MODULE_LICENSE("GPL v2");
