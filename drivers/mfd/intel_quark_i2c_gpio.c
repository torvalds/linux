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

#define INTEL_QUARK_I2C_NCLK	1

struct intel_quark_mfd {
	struct pci_dev		*pdev;
	struct clk		*i2c_clk;
	struct clk_lookup	*i2c_clk_lookup;
};

struct i2c_mode_info {
	const char *name;
	unsigned int i2c_scl_freq;
};

static const struct i2c_mode_info platform_i2c_mode_info[] = {
	{
		.name = "Galileo",
		.i2c_scl_freq = 100000,
	},
	{
		.name = "GalileoGen2",
		.i2c_scl_freq = 400000,
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

static struct resource intel_quark_gpio_res[] = {
	[INTEL_QUARK_IORES_MEM] = {
		.flags = IORESOURCE_MEM,
	},
};

static struct mfd_cell intel_quark_mfd_cells[] = {
	{
		.id = MFD_I2C_BAR,
		.name = "i2c_designware",
		.num_resources = ARRAY_SIZE(intel_quark_i2c_res),
		.resources = intel_quark_i2c_res,
		.ignore_resource_conflicts = true,
	},
	{
		.id = MFD_GPIO_BAR,
		.name = "gpio-dwapb",
		.num_resources = ARRAY_SIZE(intel_quark_gpio_res),
		.resources = intel_quark_gpio_res,
		.ignore_resource_conflicts = true,
	},
};

static const struct pci_device_id intel_quark_mfd_ids[] = {
	{ PCI_VDEVICE(INTEL, 0x0934), },
	{},
};
MODULE_DEVICE_TABLE(pci, intel_quark_mfd_ids);

static int intel_quark_register_i2c_clk(struct intel_quark_mfd *quark_mfd)
{
	struct pci_dev *pdev = quark_mfd->pdev;
	struct clk_lookup *i2c_clk_lookup;
	struct clk *i2c_clk;
	int ret;

	i2c_clk_lookup = devm_kcalloc(&pdev->dev, INTEL_QUARK_I2C_NCLK,
				      sizeof(*i2c_clk_lookup), GFP_KERNEL);
	if (!i2c_clk_lookup)
		return -ENOMEM;

	i2c_clk_lookup[0].dev_id = INTEL_QUARK_I2C_CONTROLLER_CLK;

	i2c_clk = clk_register_fixed_rate(&pdev->dev,
					  INTEL_QUARK_I2C_CONTROLLER_CLK, NULL,
					  CLK_IS_ROOT, INTEL_QUARK_I2C_CLK_HZ);

	quark_mfd->i2c_clk_lookup = i2c_clk_lookup;
	quark_mfd->i2c_clk = i2c_clk;

	ret = clk_register_clkdevs(i2c_clk, i2c_clk_lookup,
				   INTEL_QUARK_I2C_NCLK);
	if (ret)
		dev_err(&pdev->dev, "Fixed clk register failed: %d\n", ret);

	return ret;
}

static void intel_quark_unregister_i2c_clk(struct pci_dev *pdev)
{
	struct intel_quark_mfd *quark_mfd = dev_get_drvdata(&pdev->dev);

	if (!quark_mfd->i2c_clk || !quark_mfd->i2c_clk_lookup)
		return;

	clkdev_drop(quark_mfd->i2c_clk_lookup);
	clk_unregister(quark_mfd->i2c_clk);
}

static int intel_quark_i2c_setup(struct pci_dev *pdev, struct mfd_cell *cell)
{
	const char *board_name = dmi_get_system_info(DMI_BOARD_NAME);
	const struct i2c_mode_info *info;
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

	if (board_name) {
		for (info = platform_i2c_mode_info; info->name; info++) {
			if (!strcmp(board_name, info->name)) {
				pdata->i2c_scl_freq = info->i2c_scl_freq;
				break;
			}
		}
	}

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
	pdata->properties->node		= NULL;
	pdata->properties->name		= "intel-quark-x1000-gpio-portA";
	pdata->properties->idx		= 0;
	pdata->properties->ngpio	= INTEL_QUARK_MFD_NGPIO;
	pdata->properties->gpio_base	= INTEL_QUARK_MFD_GPIO_BASE;
	pdata->properties->irq		= pdev->irq;
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
	quark_mfd->pdev = pdev;

	ret = intel_quark_register_i2c_clk(quark_mfd);
	if (ret)
		return ret;

	dev_set_drvdata(&pdev->dev, quark_mfd);

	ret = intel_quark_i2c_setup(pdev, &intel_quark_mfd_cells[MFD_I2C_BAR]);
	if (ret)
		return ret;

	ret = intel_quark_gpio_setup(pdev,
				     &intel_quark_mfd_cells[MFD_GPIO_BAR]);
	if (ret)
		return ret;

	return mfd_add_devices(&pdev->dev, 0, intel_quark_mfd_cells,
			       ARRAY_SIZE(intel_quark_mfd_cells), NULL, 0,
			       NULL);
}

static void intel_quark_mfd_remove(struct pci_dev *pdev)
{
	intel_quark_unregister_i2c_clk(pdev);
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
