// SPDX-License-Identifier: GPL-2.0-only
/*
 * GPIO driver for the ACCES 104-DIO-48E series
 * Copyright (C) 2016 William Breathitt Gray
 *
 * This driver supports the following ACCES devices: 104-DIO-48E and
 * 104-DIO-24E.
 */
#include <linux/bits.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/i8254.h>
#include <linux/ioport.h>
#include <linux/irq.h>
#include <linux/isa.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/regmap.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include "gpio-i8255.h"

MODULE_IMPORT_NS("I8255");

#define DIO48E_EXTENT 16
#define MAX_NUM_DIO48E max_num_isa_dev(DIO48E_EXTENT)

static unsigned int base[MAX_NUM_DIO48E];
static unsigned int num_dio48e;
module_param_hw_array(base, uint, ioport, &num_dio48e, 0);
MODULE_PARM_DESC(base, "ACCES 104-DIO-48E base addresses");

static unsigned int irq[MAX_NUM_DIO48E];
static unsigned int num_irq;
module_param_hw_array(irq, uint, irq, &num_irq, 0);
MODULE_PARM_DESC(irq, "ACCES 104-DIO-48E interrupt line numbers");

#define DIO48E_ENABLE_INTERRUPT 0xB
#define DIO48E_DISABLE_INTERRUPT DIO48E_ENABLE_INTERRUPT
#define DIO48E_ENABLE_COUNTER_TIMER_ADDRESSING 0xD
#define DIO48E_DISABLE_COUNTER_TIMER_ADDRESSING DIO48E_ENABLE_COUNTER_TIMER_ADDRESSING
#define DIO48E_CLEAR_INTERRUPT 0xF

#define DIO48E_NUM_PPI 2

static const struct regmap_range dio48e_wr_ranges[] = {
	regmap_reg_range(0x0, 0x9), regmap_reg_range(0xB, 0xB),
	regmap_reg_range(0xD, 0xD), regmap_reg_range(0xF, 0xF),
};
static const struct regmap_range dio48e_rd_ranges[] = {
	regmap_reg_range(0x0, 0x2), regmap_reg_range(0x4, 0x6),
	regmap_reg_range(0xB, 0xB), regmap_reg_range(0xD, 0xD),
	regmap_reg_range(0xF, 0xF),
};
static const struct regmap_range dio48e_volatile_ranges[] = {
	i8255_volatile_regmap_range(0x0), i8255_volatile_regmap_range(0x4),
	regmap_reg_range(0xB, 0xB), regmap_reg_range(0xD, 0xD),
	regmap_reg_range(0xF, 0xF),
};
static const struct regmap_range dio48e_precious_ranges[] = {
	regmap_reg_range(0xB, 0xB), regmap_reg_range(0xD, 0xD),
	regmap_reg_range(0xF, 0xF),
};
static const struct regmap_access_table dio48e_wr_table = {
	.yes_ranges = dio48e_wr_ranges,
	.n_yes_ranges = ARRAY_SIZE(dio48e_wr_ranges),
};
static const struct regmap_access_table dio48e_rd_table = {
	.yes_ranges = dio48e_rd_ranges,
	.n_yes_ranges = ARRAY_SIZE(dio48e_rd_ranges),
};
static const struct regmap_access_table dio48e_volatile_table = {
	.yes_ranges = dio48e_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(dio48e_volatile_ranges),
};
static const struct regmap_access_table dio48e_precious_table = {
	.yes_ranges = dio48e_precious_ranges,
	.n_yes_ranges = ARRAY_SIZE(dio48e_precious_ranges),
};

static const struct regmap_range pit_wr_ranges[] = {
	regmap_reg_range(0x0, 0x3),
};
static const struct regmap_range pit_rd_ranges[] = {
	regmap_reg_range(0x0, 0x2),
};
static const struct regmap_access_table pit_wr_table = {
	.yes_ranges = pit_wr_ranges,
	.n_yes_ranges = ARRAY_SIZE(pit_wr_ranges),
};
static const struct regmap_access_table pit_rd_table = {
	.yes_ranges = pit_rd_ranges,
	.n_yes_ranges = ARRAY_SIZE(pit_rd_ranges),
};

/* only bit 3 on each respective Port C supports interrupts */
#define DIO48E_REGMAP_IRQ(_ppi)						\
	[19 + (_ppi) * 24] = {						\
		.mask = BIT(_ppi),					\
		.type = { .types_supported = IRQ_TYPE_EDGE_RISING },	\
	}

static const struct regmap_irq dio48e_regmap_irqs[] = {
	DIO48E_REGMAP_IRQ(0), DIO48E_REGMAP_IRQ(1),
};

/**
 * struct dio48e_gpio - GPIO device private data structure
 * @lock:	synchronization lock to prevent I/O race conditions
 * @map:	Regmap for the device
 * @regs:	virtual mapping for device registers
 * @flags:	IRQ flags saved during locking
 * @irq_mask:	Current IRQ mask state on the device
 */
struct dio48e_gpio {
	raw_spinlock_t lock;
	struct regmap *map;
	void __iomem *regs;
	unsigned long flags;
	unsigned int irq_mask;
};

static void dio48e_regmap_lock(void *lock_arg) __acquires(&dio48egpio->lock)
{
	struct dio48e_gpio *const dio48egpio = lock_arg;
	unsigned long flags;

	raw_spin_lock_irqsave(&dio48egpio->lock, flags);
	dio48egpio->flags = flags;
}

static void dio48e_regmap_unlock(void *lock_arg) __releases(&dio48egpio->lock)
{
	struct dio48e_gpio *const dio48egpio = lock_arg;

	raw_spin_unlock_irqrestore(&dio48egpio->lock, dio48egpio->flags);
}

static void pit_regmap_lock(void *lock_arg) __acquires(&dio48egpio->lock)
{
	struct dio48e_gpio *const dio48egpio = lock_arg;
	unsigned long flags;

	raw_spin_lock_irqsave(&dio48egpio->lock, flags);
	dio48egpio->flags = flags;

	iowrite8(0x00, dio48egpio->regs + DIO48E_ENABLE_COUNTER_TIMER_ADDRESSING);
}

static void pit_regmap_unlock(void *lock_arg) __releases(&dio48egpio->lock)
{
	struct dio48e_gpio *const dio48egpio = lock_arg;

	ioread8(dio48egpio->regs + DIO48E_DISABLE_COUNTER_TIMER_ADDRESSING);

	raw_spin_unlock_irqrestore(&dio48egpio->lock, dio48egpio->flags);
}

static int dio48e_handle_mask_sync(const int index,
				   const unsigned int mask_buf_def,
				   const unsigned int mask_buf,
				   void *const irq_drv_data)
{
	struct dio48e_gpio *const dio48egpio = irq_drv_data;
	const unsigned int prev_mask = dio48egpio->irq_mask;
	int err;
	unsigned int val;

	/* exit early if no change since the previous mask */
	if (mask_buf == prev_mask)
		return 0;

	/* remember the current mask for the next mask sync */
	dio48egpio->irq_mask = mask_buf;

	/* if all previously masked, enable interrupts when unmasking */
	if (prev_mask == mask_buf_def) {
		err = regmap_write(dio48egpio->map, DIO48E_CLEAR_INTERRUPT, 0x00);
		if (err)
			return err;
		return regmap_write(dio48egpio->map, DIO48E_ENABLE_INTERRUPT, 0x00);
	}

	/* if all are currently masked, disable interrupts */
	if (mask_buf == mask_buf_def)
		return regmap_read(dio48egpio->map, DIO48E_DISABLE_INTERRUPT, &val);

	return 0;
}

#define DIO48E_NGPIO 48
static const char *dio48e_names[DIO48E_NGPIO] = {
	"PPI Group 0 Port A 0", "PPI Group 0 Port A 1", "PPI Group 0 Port A 2",
	"PPI Group 0 Port A 3", "PPI Group 0 Port A 4", "PPI Group 0 Port A 5",
	"PPI Group 0 Port A 6", "PPI Group 0 Port A 7",	"PPI Group 0 Port B 0",
	"PPI Group 0 Port B 1", "PPI Group 0 Port B 2", "PPI Group 0 Port B 3",
	"PPI Group 0 Port B 4", "PPI Group 0 Port B 5", "PPI Group 0 Port B 6",
	"PPI Group 0 Port B 7", "PPI Group 0 Port C 0", "PPI Group 0 Port C 1",
	"PPI Group 0 Port C 2", "PPI Group 0 Port C 3", "PPI Group 0 Port C 4",
	"PPI Group 0 Port C 5", "PPI Group 0 Port C 6", "PPI Group 0 Port C 7",
	"PPI Group 1 Port A 0", "PPI Group 1 Port A 1", "PPI Group 1 Port A 2",
	"PPI Group 1 Port A 3", "PPI Group 1 Port A 4", "PPI Group 1 Port A 5",
	"PPI Group 1 Port A 6", "PPI Group 1 Port A 7",	"PPI Group 1 Port B 0",
	"PPI Group 1 Port B 1", "PPI Group 1 Port B 2", "PPI Group 1 Port B 3",
	"PPI Group 1 Port B 4", "PPI Group 1 Port B 5", "PPI Group 1 Port B 6",
	"PPI Group 1 Port B 7", "PPI Group 1 Port C 0", "PPI Group 1 Port C 1",
	"PPI Group 1 Port C 2", "PPI Group 1 Port C 3", "PPI Group 1 Port C 4",
	"PPI Group 1 Port C 5", "PPI Group 1 Port C 6", "PPI Group 1 Port C 7"
};

static int dio48e_irq_init_hw(struct regmap *const map)
{
	unsigned int val;

	/* Disable IRQ by default */
	return regmap_read(map, DIO48E_DISABLE_INTERRUPT, &val);
}

static int dio48e_probe(struct device *dev, unsigned int id)
{
	const char *const name = dev_name(dev);
	struct i8255_regmap_config config = {};
	void __iomem *regs;
	struct regmap *map;
	struct regmap_config dio48e_regmap_config;
	struct regmap_config pit_regmap_config;
	struct i8254_regmap_config pit_config;
	int err;
	struct regmap_irq_chip *chip;
	struct dio48e_gpio *dio48egpio;
	struct regmap_irq_chip_data *chip_data;

	if (!devm_request_region(dev, base[id], DIO48E_EXTENT, name)) {
		dev_err(dev, "Unable to lock port addresses (0x%X-0x%X)\n",
			base[id], base[id] + DIO48E_EXTENT);
		return -EBUSY;
	}

	dio48egpio = devm_kzalloc(dev, sizeof(*dio48egpio), GFP_KERNEL);
	if (!dio48egpio)
		return -ENOMEM;

	regs = devm_ioport_map(dev, base[id], DIO48E_EXTENT);
	if (!regs)
		return -ENOMEM;

	dio48egpio->regs = regs;

	raw_spin_lock_init(&dio48egpio->lock);

	dio48e_regmap_config = (struct regmap_config) {
		.reg_bits = 8,
		.reg_stride = 1,
		.val_bits = 8,
		.lock = dio48e_regmap_lock,
		.unlock = dio48e_regmap_unlock,
		.lock_arg = dio48egpio,
		.io_port = true,
		.wr_table = &dio48e_wr_table,
		.rd_table = &dio48e_rd_table,
		.volatile_table = &dio48e_volatile_table,
		.precious_table = &dio48e_precious_table,
		.cache_type = REGCACHE_FLAT,
	};

	map = devm_regmap_init_mmio(dev, regs, &dio48e_regmap_config);
	if (IS_ERR(map))
		return dev_err_probe(dev, PTR_ERR(map),
				     "Unable to initialize register map\n");

	dio48egpio->map = map;

	pit_regmap_config = (struct regmap_config) {
		.name = "i8254",
		.reg_bits = 8,
		.reg_stride = 1,
		.val_bits = 8,
		.lock = pit_regmap_lock,
		.unlock = pit_regmap_unlock,
		.lock_arg = dio48egpio,
		.io_port = true,
		.wr_table = &pit_wr_table,
		.rd_table = &pit_rd_table,
	};

	pit_config.map = devm_regmap_init_mmio(dev, regs, &pit_regmap_config);
	if (IS_ERR(pit_config.map))
		return dev_err_probe(dev, PTR_ERR(pit_config.map),
				     "Unable to initialize i8254 register map\n");

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->name = name;
	chip->mask_base = DIO48E_ENABLE_INTERRUPT;
	chip->ack_base = DIO48E_CLEAR_INTERRUPT;
	chip->no_status = true;
	chip->num_regs = 1;
	chip->irqs = dio48e_regmap_irqs;
	chip->num_irqs = ARRAY_SIZE(dio48e_regmap_irqs);
	chip->handle_mask_sync = dio48e_handle_mask_sync;
	chip->irq_drv_data = dio48egpio;

	/* Initialize to prevent spurious interrupts before we're ready */
	err = dio48e_irq_init_hw(map);
	if (err)
		return err;

	err = devm_regmap_add_irq_chip(dev, map, irq[id], 0, 0, chip, &chip_data);
	if (err)
		return dev_err_probe(dev, err, "IRQ registration failed\n");

	pit_config.parent = dev;

	err = devm_i8254_regmap_register(dev, &pit_config);
	if (err)
		return err;

	config.parent = dev;
	config.map = map;
	config.num_ppi = DIO48E_NUM_PPI;
	config.names = dio48e_names;
	config.domain = regmap_irq_get_domain(chip_data);

	return devm_i8255_regmap_register(dev, &config);
}

static struct isa_driver dio48e_driver = {
	.probe = dio48e_probe,
	.driver = {
		.name = "104-dio-48e"
	},
};
module_isa_driver_with_irq(dio48e_driver, num_dio48e, num_irq);

MODULE_AUTHOR("William Breathitt Gray <vilhelm.gray@gmail.com>");
MODULE_DESCRIPTION("ACCES 104-DIO-48E GPIO driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS("I8254");
