// SPDX-License-Identifier: GPL-2.0-only
/*
 * GPIO driver for the ACCES 104-IDIO-16 family
 * Copyright (C) 2015 William Breathitt Gray
 *
 * This driver supports the following ACCES devices: 104-IDIO-16,
 * 104-IDIO-16E, 104-IDO-16, 104-IDIO-8, 104-IDIO-8E, and 104-IDO-8.
 */
#include <linux/bits.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/ioport.h>
#include <linux/irq.h>
#include <linux/isa.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/regmap.h>
#include <linux/types.h>

#include "gpio-idio-16.h"

#define IDIO_16_EXTENT 8
#define MAX_NUM_IDIO_16 max_num_isa_dev(IDIO_16_EXTENT)

static unsigned int base[MAX_NUM_IDIO_16];
static unsigned int num_idio_16;
module_param_hw_array(base, uint, ioport, &num_idio_16, 0);
MODULE_PARM_DESC(base, "ACCES 104-IDIO-16 base addresses");

static unsigned int irq[MAX_NUM_IDIO_16];
static unsigned int num_irq;
module_param_hw_array(irq, uint, irq, &num_irq, 0);
MODULE_PARM_DESC(irq, "ACCES 104-IDIO-16 interrupt line numbers");

static const struct regmap_range idio_16_wr_ranges[] = {
	regmap_reg_range(0x0, 0x2), regmap_reg_range(0x4, 0x4),
};
static const struct regmap_range idio_16_rd_ranges[] = {
	regmap_reg_range(0x1, 0x2), regmap_reg_range(0x5, 0x5),
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
		.mask = BIT(_id),					\
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

static int idio_16_probe(struct device *dev, unsigned int id)
{
	const char *const name = dev_name(dev);
	struct idio_16_regmap_config config = {};
	void __iomem *regs;
	struct regmap *map;

	if (!devm_request_region(dev, base[id], IDIO_16_EXTENT, name)) {
		dev_err(dev, "Unable to lock port addresses (0x%X-0x%X)\n",
			base[id], base[id] + IDIO_16_EXTENT);
		return -EBUSY;
	}

	regs = devm_ioport_map(dev, base[id], IDIO_16_EXTENT);
	if (!regs)
		return -ENOMEM;

	map = devm_regmap_init_mmio(dev, regs, &idio_16_regmap_config);
	if (IS_ERR(map))
		return dev_err_probe(dev, PTR_ERR(map), "Unable to initialize register map\n");

	config.parent = dev;
	config.map = map;
	config.regmap_irqs = idio_16_regmap_irqs;
	config.num_regmap_irqs = ARRAY_SIZE(idio_16_regmap_irqs);
	config.irq = irq[id];
	config.no_status = true;

	return devm_idio_16_regmap_register(dev, &config);
}

static struct isa_driver idio_16_driver = {
	.probe = idio_16_probe,
	.driver = {
		.name = "104-idio-16"
	},
};

module_isa_driver_with_irq(idio_16_driver, num_idio_16, num_irq);

MODULE_AUTHOR("William Breathitt Gray <vilhelm.gray@gmail.com>");
MODULE_DESCRIPTION("ACCES 104-IDIO-16 GPIO driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS("GPIO_IDIO_16");
