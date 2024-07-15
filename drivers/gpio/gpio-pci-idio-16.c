// SPDX-License-Identifier: GPL-2.0-only
/*
 * GPIO driver for the ACCES PCI-IDIO-16
 * Copyright (C) 2017 William Breathitt Gray
 */
#include <linux/bits.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/regmap.h>
#include <linux/types.h>

#include "gpio-idio-16.h"

static const struct regmap_range idio_16_wr_ranges[] = {
	regmap_reg_range(0x0, 0x2), regmap_reg_range(0x3, 0x4),
};
static const struct regmap_range idio_16_rd_ranges[] = {
	regmap_reg_range(0x1, 0x2), regmap_reg_range(0x5, 0x6),
};
static const struct regmap_range idio_16_precious_ranges[] = {
	regmap_reg_range(0x2, 0x2),
};
static const struct regmap_access_table idio_16_wr_table = {
	.yes_ranges = idio_16_wr_ranges,
	.n_yes_ranges = ARRAY_SIZE(idio_16_wr_ranges),
};
static const struct regmap_access_table idio_16_rd_table = {
	.yes_ranges = idio_16_rd_ranges,
	.n_yes_ranges = ARRAY_SIZE(idio_16_rd_ranges),
};
static const struct regmap_access_table idio_16_precious_table = {
	.yes_ranges = idio_16_precious_ranges,
	.n_yes_ranges = ARRAY_SIZE(idio_16_precious_ranges),
};
static const struct regmap_config idio_16_regmap_config = {
	.reg_bits = 8,
	.reg_stride = 1,
	.val_bits = 8,
	.io_port = true,
	.wr_table = &idio_16_wr_table,
	.rd_table = &idio_16_rd_table,
	.volatile_table = &idio_16_rd_table,
	.precious_table = &idio_16_precious_table,
	.cache_type = REGCACHE_FLAT,
	.use_raw_spinlock = true,
};

/* Only input lines (GPIO 16-31) support interrupts */
#define IDIO_16_REGMAP_IRQ(_id)						\
	[16 + _id] = {							\
		.mask = BIT(2),						\
		.type = { .types_supported = IRQ_TYPE_EDGE_BOTH },	\
	}

static const struct regmap_irq idio_16_regmap_irqs[] = {
	IDIO_16_REGMAP_IRQ(0), IDIO_16_REGMAP_IRQ(1), IDIO_16_REGMAP_IRQ(2), /* 0-2 */
	IDIO_16_REGMAP_IRQ(3), IDIO_16_REGMAP_IRQ(4), IDIO_16_REGMAP_IRQ(5), /* 3-5 */
	IDIO_16_REGMAP_IRQ(6), IDIO_16_REGMAP_IRQ(7), IDIO_16_REGMAP_IRQ(8), /* 6-8 */
	IDIO_16_REGMAP_IRQ(9), IDIO_16_REGMAP_IRQ(10), IDIO_16_REGMAP_IRQ(11), /* 9-11 */
	IDIO_16_REGMAP_IRQ(12), IDIO_16_REGMAP_IRQ(13), IDIO_16_REGMAP_IRQ(14), /* 12-14 */
	IDIO_16_REGMAP_IRQ(15), /* 15 */
};

static int idio_16_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct device *const dev = &pdev->dev;
	int err;
	const size_t pci_bar_index = 2;
	const char *const name = pci_name(pdev);
	struct idio_16_regmap_config config = {};
	void __iomem *regs;
	struct regmap *map;

	err = pcim_enable_device(pdev);
	if (err) {
		dev_err(dev, "Failed to enable PCI device (%d)\n", err);
		return err;
	}

	err = pcim_iomap_regions(pdev, BIT(pci_bar_index), name);
	if (err) {
		dev_err(dev, "Unable to map PCI I/O addresses (%d)\n", err);
		return err;
	}

	regs = pcim_iomap_table(pdev)[pci_bar_index];

	map = devm_regmap_init_mmio(dev, regs, &idio_16_regmap_config);
	if (IS_ERR(map))
		return dev_err_probe(dev, PTR_ERR(map), "Unable to initialize register map\n");

	config.parent = dev;
	config.map = map;
	config.regmap_irqs = idio_16_regmap_irqs;
	config.num_regmap_irqs = ARRAY_SIZE(idio_16_regmap_irqs);
	config.irq = pdev->irq;
	config.filters = true;

	return devm_idio_16_regmap_register(dev, &config);
}

static const struct pci_device_id idio_16_pci_dev_id[] = {
	{ PCI_DEVICE(0x494F, 0x0DC8) }, { 0 }
};
MODULE_DEVICE_TABLE(pci, idio_16_pci_dev_id);

static struct pci_driver idio_16_driver = {
	.name = "pci-idio-16",
	.id_table = idio_16_pci_dev_id,
	.probe = idio_16_probe
};

module_pci_driver(idio_16_driver);

MODULE_AUTHOR("William Breathitt Gray <vilhelm.gray@gmail.com>");
MODULE_DESCRIPTION("ACCES PCI-IDIO-16 GPIO driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(GPIO_IDIO_16);
