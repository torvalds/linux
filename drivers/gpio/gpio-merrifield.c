// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Merrifield SoC GPIO driver
 *
 * Copyright (c) 2016, 2023 Intel Corporation.
 * Author: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 */

#include <linux/acpi.h>
#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/types.h>

#include "gpio-tangier.h"

/* Intel Merrifield has 192 GPIO pins */
#define MRFLD_NGPIO	192

static const struct tng_gpio_pinrange mrfld_gpio_ranges[] = {
	GPIO_PINRANGE(0, 11, 146),
	GPIO_PINRANGE(12, 13, 144),
	GPIO_PINRANGE(14, 15, 35),
	GPIO_PINRANGE(16, 16, 164),
	GPIO_PINRANGE(17, 18, 105),
	GPIO_PINRANGE(19, 22, 101),
	GPIO_PINRANGE(23, 30, 107),
	GPIO_PINRANGE(32, 43, 67),
	GPIO_PINRANGE(44, 63, 195),
	GPIO_PINRANGE(64, 67, 140),
	GPIO_PINRANGE(68, 69, 165),
	GPIO_PINRANGE(70, 71, 65),
	GPIO_PINRANGE(72, 76, 228),
	GPIO_PINRANGE(77, 86, 37),
	GPIO_PINRANGE(87, 87, 48),
	GPIO_PINRANGE(88, 88, 47),
	GPIO_PINRANGE(89, 96, 49),
	GPIO_PINRANGE(97, 97, 34),
	GPIO_PINRANGE(102, 119, 83),
	GPIO_PINRANGE(120, 123, 79),
	GPIO_PINRANGE(124, 135, 115),
	GPIO_PINRANGE(137, 142, 158),
	GPIO_PINRANGE(154, 163, 24),
	GPIO_PINRANGE(164, 176, 215),
	GPIO_PINRANGE(177, 189, 127),
	GPIO_PINRANGE(190, 191, 178),
};

static const char *mrfld_gpio_get_pinctrl_dev_name(struct tng_gpio *priv)
{
	struct device *dev = priv->dev;
	struct acpi_device *adev;
	const char *name;

	adev = acpi_dev_get_first_match_dev("INTC1002", NULL, -1);
	if (adev) {
		name = devm_kstrdup(dev, acpi_dev_name(adev), GFP_KERNEL);
		acpi_dev_put(adev);
	} else {
		name = "pinctrl-merrifield";
	}

	return name;
}

static int mrfld_gpio_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
	struct tng_gpio *priv;
	u32 gpio_base, irq_base;
	void __iomem *base;
	int retval;

	retval = pcim_enable_device(pdev);
	if (retval)
		return retval;

	base = pcim_iomap_region(pdev, 1, pci_name(pdev));
	if (IS_ERR(base))
		return dev_err_probe(dev, PTR_ERR(base), "I/O memory mapping error\n");

	irq_base = readl(base + 0 * sizeof(u32));
	gpio_base = readl(base + 1 * sizeof(u32));

	/* Release the IO mapping, since we already get the info from BAR1 */
	pcim_iounmap_region(pdev, 1);

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	priv->reg_base = pcim_iomap_region(pdev, 0, pci_name(pdev));
	if (IS_ERR(priv->reg_base))
		return dev_err_probe(dev, PTR_ERR(priv->reg_base),
				"I/O memory mapping error\n");

	priv->pin_info.pin_ranges = mrfld_gpio_ranges;
	priv->pin_info.nranges = ARRAY_SIZE(mrfld_gpio_ranges);
	priv->pin_info.name = mrfld_gpio_get_pinctrl_dev_name(priv);
	if (!priv->pin_info.name)
		return -ENOMEM;

	priv->info.base = gpio_base;
	priv->info.ngpio = MRFLD_NGPIO;
	priv->info.first = irq_base;

	retval = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_ALL_TYPES);
	if (retval < 0)
		return retval;

	priv->irq = pci_irq_vector(pdev, 0);

	priv->wake_regs.gwmr = GWMR_MRFLD;
	priv->wake_regs.gwsr = GWSR_MRFLD;
	priv->wake_regs.gsir = GSIR_MRFLD;

	retval = devm_tng_gpio_probe(dev, priv);
	if (retval)
		return dev_err_probe(dev, retval, "tng_gpio_probe error\n");

	pci_set_drvdata(pdev, priv);
	return 0;
}

static const struct pci_device_id mrfld_gpio_ids[] = {
	{ PCI_VDEVICE(INTEL, 0x1199) },
	{ }
};
MODULE_DEVICE_TABLE(pci, mrfld_gpio_ids);

static struct pci_driver mrfld_gpio_driver = {
	.name		= "gpio-merrifield",
	.id_table	= mrfld_gpio_ids,
	.probe		= mrfld_gpio_probe,
};
module_pci_driver(mrfld_gpio_driver);

MODULE_AUTHOR("Andy Shevchenko <andriy.shevchenko@linux.intel.com>");
MODULE_DESCRIPTION("Intel Merrifield SoC GPIO driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS("GPIO_TANGIER");
