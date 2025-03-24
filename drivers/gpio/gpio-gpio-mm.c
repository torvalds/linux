// SPDX-License-Identifier: GPL-2.0-only
/*
 * GPIO driver for the Diamond Systems GPIO-MM
 * Copyright (C) 2016 William Breathitt Gray
 *
 * This driver supports the following Diamond Systems devices: GPIO-MM and
 * GPIO-MM-12.
 */
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/isa.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/regmap.h>
#include <linux/types.h>

#include "gpio-i8255.h"

MODULE_IMPORT_NS("I8255");

#define GPIOMM_EXTENT 8
#define MAX_NUM_GPIOMM max_num_isa_dev(GPIOMM_EXTENT)

static unsigned int base[MAX_NUM_GPIOMM];
static unsigned int num_gpiomm;
module_param_hw_array(base, uint, ioport, &num_gpiomm, 0);
MODULE_PARM_DESC(base, "Diamond Systems GPIO-MM base addresses");

#define GPIOMM_NUM_PPI 2

static const struct regmap_range gpiomm_volatile_ranges[] = {
	i8255_volatile_regmap_range(0x0), i8255_volatile_regmap_range(0x4),
};
static const struct regmap_access_table gpiomm_volatile_table = {
	.yes_ranges = gpiomm_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(gpiomm_volatile_ranges),
};
static const struct regmap_config gpiomm_regmap_config = {
	.reg_bits = 8,
	.reg_stride = 1,
	.val_bits = 8,
	.io_port = true,
	.max_register = 0x7,
	.volatile_table = &gpiomm_volatile_table,
	.cache_type = REGCACHE_FLAT,
};

#define GPIOMM_NGPIO 48
static const char *gpiomm_names[GPIOMM_NGPIO] = {
	"Port 1A0", "Port 1A1", "Port 1A2", "Port 1A3", "Port 1A4", "Port 1A5",
	"Port 1A6", "Port 1A7", "Port 1B0", "Port 1B1", "Port 1B2", "Port 1B3",
	"Port 1B4", "Port 1B5", "Port 1B6", "Port 1B7", "Port 1C0", "Port 1C1",
	"Port 1C2", "Port 1C3", "Port 1C4", "Port 1C5", "Port 1C6", "Port 1C7",
	"Port 2A0", "Port 2A1", "Port 2A2", "Port 2A3", "Port 2A4", "Port 2A5",
	"Port 2A6", "Port 2A7", "Port 2B0", "Port 2B1", "Port 2B2", "Port 2B3",
	"Port 2B4", "Port 2B5", "Port 2B6", "Port 2B7", "Port 2C0", "Port 2C1",
	"Port 2C2", "Port 2C3", "Port 2C4", "Port 2C5", "Port 2C6", "Port 2C7",
};

static int gpiomm_probe(struct device *dev, unsigned int id)
{
	const char *const name = dev_name(dev);
	struct i8255_regmap_config config = {};
	void __iomem *regs;

	if (!devm_request_region(dev, base[id], GPIOMM_EXTENT, name)) {
		dev_err(dev, "Unable to lock port addresses (0x%X-0x%X)\n",
			base[id], base[id] + GPIOMM_EXTENT);
		return -EBUSY;
	}

	regs = devm_ioport_map(dev, base[id], GPIOMM_EXTENT);
	if (!regs)
		return -ENOMEM;

	config.map = devm_regmap_init_mmio(dev, regs, &gpiomm_regmap_config);
	if (IS_ERR(config.map))
		return dev_err_probe(dev, PTR_ERR(config.map),
				     "Unable to initialize register map\n");

	config.parent = dev;
	config.num_ppi = GPIOMM_NUM_PPI;
	config.names = gpiomm_names;

	return devm_i8255_regmap_register(dev, &config);
}

static struct isa_driver gpiomm_driver = {
	.probe = gpiomm_probe,
	.driver = {
		.name = "gpio-mm"
	},
};

module_isa_driver(gpiomm_driver, num_gpiomm);

MODULE_AUTHOR("William Breathitt Gray <vilhelm.gray@gmail.com>");
MODULE_DESCRIPTION("Diamond Systems GPIO-MM GPIO driver");
MODULE_LICENSE("GPL v2");
