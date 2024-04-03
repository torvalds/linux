// SPDX-License-Identifier: GPL-2.0-only
/*
 * GPIO driver for the WinSystems WS16C48
 * Copyright (C) 2016 William Breathitt Gray
 */
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio/regmap.h>
#include <linux/irq.h>
#include <linux/isa.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/spinlock.h>
#include <linux/regmap.h>
#include <linux/types.h>

#define WS16C48_EXTENT 11
#define MAX_NUM_WS16C48 max_num_isa_dev(WS16C48_EXTENT)

static unsigned int base[MAX_NUM_WS16C48];
static unsigned int num_ws16c48;
module_param_hw_array(base, uint, ioport, &num_ws16c48, 0);
MODULE_PARM_DESC(base, "WinSystems WS16C48 base addresses");

static unsigned int irq[MAX_NUM_WS16C48];
static unsigned int num_irq;
module_param_hw_array(irq, uint, irq, &num_irq, 0);
MODULE_PARM_DESC(irq, "WinSystems WS16C48 interrupt line numbers");

#define WS16C48_DAT_BASE 0x0
#define WS16C48_PAGE_LOCK 0x7
#define WS16C48_PAGE_BASE 0x8
#define WS16C48_POL WS16C48_PAGE_BASE
#define WS16C48_ENAB WS16C48_PAGE_BASE
#define WS16C48_INT_ID WS16C48_PAGE_BASE

#define PAGE_LOCK_PAGE_FIELD GENMASK(7, 6)
#define POL_PAGE u8_encode_bits(1, PAGE_LOCK_PAGE_FIELD)
#define ENAB_PAGE u8_encode_bits(2, PAGE_LOCK_PAGE_FIELD)
#define INT_ID_PAGE u8_encode_bits(3, PAGE_LOCK_PAGE_FIELD)

static const struct regmap_range ws16c48_wr_ranges[] = {
	regmap_reg_range(0x0, 0x5), regmap_reg_range(0x7, 0xA),
};
static const struct regmap_range ws16c48_rd_ranges[] = {
	regmap_reg_range(0x0, 0xA),
};
static const struct regmap_range ws16c48_volatile_ranges[] = {
	regmap_reg_range(0x0, 0x6), regmap_reg_range(0x8, 0xA),
};
static const struct regmap_access_table ws16c48_wr_table = {
	.yes_ranges = ws16c48_wr_ranges,
	.n_yes_ranges = ARRAY_SIZE(ws16c48_wr_ranges),
};
static const struct regmap_access_table ws16c48_rd_table = {
	.yes_ranges = ws16c48_rd_ranges,
	.n_yes_ranges = ARRAY_SIZE(ws16c48_rd_ranges),
};
static const struct regmap_access_table ws16c48_volatile_table = {
	.yes_ranges = ws16c48_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(ws16c48_volatile_ranges),
};
static const struct regmap_config ws16c48_regmap_config = {
	.reg_bits = 8,
	.reg_stride = 1,
	.val_bits = 8,
	.io_port = true,
	.wr_table = &ws16c48_wr_table,
	.rd_table = &ws16c48_rd_table,
	.volatile_table = &ws16c48_volatile_table,
	.cache_type = REGCACHE_FLAT,
	.use_raw_spinlock = true,
};

#define WS16C48_NGPIO_PER_REG 8
#define WS16C48_REGMAP_IRQ(_id)							\
	[_id] = {								\
		.reg_offset = (_id) / WS16C48_NGPIO_PER_REG,			\
		.mask = BIT((_id) % WS16C48_NGPIO_PER_REG),			\
		.type = {							\
			.type_reg_offset = (_id) / WS16C48_NGPIO_PER_REG,	\
			.types_supported = IRQ_TYPE_EDGE_BOTH,			\
		},								\
	}

/* Only the first 24 lines (Port 0-2) support interrupts */
#define WS16C48_NUM_IRQS 24
static const struct regmap_irq ws16c48_regmap_irqs[WS16C48_NUM_IRQS] = {
	WS16C48_REGMAP_IRQ(0), WS16C48_REGMAP_IRQ(1), WS16C48_REGMAP_IRQ(2), /* 0-2 */
	WS16C48_REGMAP_IRQ(3), WS16C48_REGMAP_IRQ(4), WS16C48_REGMAP_IRQ(5), /* 3-5 */
	WS16C48_REGMAP_IRQ(6), WS16C48_REGMAP_IRQ(7), WS16C48_REGMAP_IRQ(8), /* 6-8 */
	WS16C48_REGMAP_IRQ(9), WS16C48_REGMAP_IRQ(10), WS16C48_REGMAP_IRQ(11), /* 9-11 */
	WS16C48_REGMAP_IRQ(12), WS16C48_REGMAP_IRQ(13), WS16C48_REGMAP_IRQ(14), /* 12-14 */
	WS16C48_REGMAP_IRQ(15), WS16C48_REGMAP_IRQ(16), WS16C48_REGMAP_IRQ(17), /* 15-17 */
	WS16C48_REGMAP_IRQ(18), WS16C48_REGMAP_IRQ(19), WS16C48_REGMAP_IRQ(20), /* 18-20 */
	WS16C48_REGMAP_IRQ(21), WS16C48_REGMAP_IRQ(22), WS16C48_REGMAP_IRQ(23), /* 21-23 */
};

/**
 * struct ws16c48_gpio - GPIO device private data structure
 * @map:	regmap for the device
 * @lock:	synchronization lock to prevent I/O race conditions
 * @irq_mask:	I/O bits affected by interrupts
 */
struct ws16c48_gpio {
	struct regmap *map;
	raw_spinlock_t lock;
	u8 irq_mask[WS16C48_NUM_IRQS / WS16C48_NGPIO_PER_REG];
};

static int ws16c48_handle_pre_irq(void *const irq_drv_data) __acquires(&ws16c48gpio->lock)
{
	struct ws16c48_gpio *const ws16c48gpio = irq_drv_data;

	/* Lock to prevent Page/Lock register change while we handle IRQ */
	raw_spin_lock(&ws16c48gpio->lock);

	return 0;
}

static int ws16c48_handle_post_irq(void *const irq_drv_data) __releases(&ws16c48gpio->lock)
{
	struct ws16c48_gpio *const ws16c48gpio = irq_drv_data;

	raw_spin_unlock(&ws16c48gpio->lock);

	return 0;
}

static int ws16c48_handle_mask_sync(const int index, const unsigned int mask_buf_def,
				    const unsigned int mask_buf, void *const irq_drv_data)
{
	struct ws16c48_gpio *const ws16c48gpio = irq_drv_data;
	unsigned long flags;
	int ret = 0;

	raw_spin_lock_irqsave(&ws16c48gpio->lock, flags);

	/* exit early if no change since the last mask sync */
	if (mask_buf == ws16c48gpio->irq_mask[index])
		goto exit_unlock;
	ws16c48gpio->irq_mask[index] = mask_buf;

	ret = regmap_write(ws16c48gpio->map, WS16C48_PAGE_LOCK, ENAB_PAGE);
	if (ret)
		goto exit_unlock;

	/* Update ENAB register (inverted mask) */
	ret = regmap_write(ws16c48gpio->map, WS16C48_ENAB + index, ~mask_buf);
	if (ret)
		goto exit_unlock;

	ret = regmap_write(ws16c48gpio->map, WS16C48_PAGE_LOCK, INT_ID_PAGE);
	if (ret)
		goto exit_unlock;

exit_unlock:
	raw_spin_unlock_irqrestore(&ws16c48gpio->lock, flags);

	return ret;
}

static int ws16c48_set_type_config(unsigned int **const buf, const unsigned int type,
				   const struct regmap_irq *const irq_data, const int idx,
				   void *const irq_drv_data)
{
	struct ws16c48_gpio *const ws16c48gpio = irq_drv_data;
	unsigned int polarity;
	unsigned long flags;
	int ret;

	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		polarity = irq_data->mask;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		polarity = 0;
		break;
	default:
		return -EINVAL;
	}

	raw_spin_lock_irqsave(&ws16c48gpio->lock, flags);

	ret = regmap_write(ws16c48gpio->map, WS16C48_PAGE_LOCK, POL_PAGE);
	if (ret)
		goto exit_unlock;

	/* Set interrupt polarity */
	ret = regmap_update_bits(ws16c48gpio->map, WS16C48_POL + idx, irq_data->mask, polarity);
	if (ret)
		goto exit_unlock;

	ret = regmap_write(ws16c48gpio->map, WS16C48_PAGE_LOCK, INT_ID_PAGE);
	if (ret)
		goto exit_unlock;

exit_unlock:
	raw_spin_unlock_irqrestore(&ws16c48gpio->lock, flags);

	return ret;
}

#define WS16C48_NGPIO 48
static const char *ws16c48_names[WS16C48_NGPIO] = {
	"Port 0 Bit 0", "Port 0 Bit 1", "Port 0 Bit 2", "Port 0 Bit 3",
	"Port 0 Bit 4", "Port 0 Bit 5", "Port 0 Bit 6", "Port 0 Bit 7",
	"Port 1 Bit 0", "Port 1 Bit 1", "Port 1 Bit 2", "Port 1 Bit 3",
	"Port 1 Bit 4", "Port 1 Bit 5", "Port 1 Bit 6", "Port 1 Bit 7",
	"Port 2 Bit 0", "Port 2 Bit 1", "Port 2 Bit 2", "Port 2 Bit 3",
	"Port 2 Bit 4", "Port 2 Bit 5", "Port 2 Bit 6", "Port 2 Bit 7",
	"Port 3 Bit 0", "Port 3 Bit 1", "Port 3 Bit 2", "Port 3 Bit 3",
	"Port 3 Bit 4", "Port 3 Bit 5", "Port 3 Bit 6", "Port 3 Bit 7",
	"Port 4 Bit 0", "Port 4 Bit 1", "Port 4 Bit 2", "Port 4 Bit 3",
	"Port 4 Bit 4", "Port 4 Bit 5", "Port 4 Bit 6", "Port 4 Bit 7",
	"Port 5 Bit 0", "Port 5 Bit 1", "Port 5 Bit 2", "Port 5 Bit 3",
	"Port 5 Bit 4", "Port 5 Bit 5", "Port 5 Bit 6", "Port 5 Bit 7"
};

static int ws16c48_irq_init_hw(struct regmap *const map)
{
	int err;

	err = regmap_write(map, WS16C48_PAGE_LOCK, ENAB_PAGE);
	if (err)
		return err;

	/* Disable interrupts for all lines */
	err = regmap_write(map, WS16C48_ENAB + 0, 0x00);
	if (err)
		return err;
	err = regmap_write(map, WS16C48_ENAB + 1, 0x00);
	if (err)
		return err;
	err = regmap_write(map, WS16C48_ENAB + 2, 0x00);
	if (err)
		return err;

	return regmap_write(map, WS16C48_PAGE_LOCK, INT_ID_PAGE);
}

static int ws16c48_probe(struct device *dev, unsigned int id)
{
	struct ws16c48_gpio *ws16c48gpio;
	const char *const name = dev_name(dev);
	int err;
	struct gpio_regmap_config gpio_config = {};
	void __iomem *regs;
	struct regmap_irq_chip *chip;
	struct regmap_irq_chip_data *chip_data;

	ws16c48gpio = devm_kzalloc(dev, sizeof(*ws16c48gpio), GFP_KERNEL);
	if (!ws16c48gpio)
		return -ENOMEM;

	if (!devm_request_region(dev, base[id], WS16C48_EXTENT, name)) {
		dev_err(dev, "Unable to lock port addresses (0x%X-0x%X)\n",
			base[id], base[id] + WS16C48_EXTENT);
		return -EBUSY;
	}

	regs = devm_ioport_map(dev, base[id], WS16C48_EXTENT);
	if (!regs)
		return -ENOMEM;

	ws16c48gpio->map = devm_regmap_init_mmio(dev, regs, &ws16c48_regmap_config);
	if (IS_ERR(ws16c48gpio->map))
		return dev_err_probe(dev, PTR_ERR(ws16c48gpio->map),
				     "Unable to initialize register map\n");

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->name = name;
	chip->status_base = WS16C48_INT_ID;
	chip->mask_base = WS16C48_ENAB;
	chip->ack_base = WS16C48_INT_ID;
	chip->num_regs = 3;
	chip->irqs = ws16c48_regmap_irqs;
	chip->num_irqs = ARRAY_SIZE(ws16c48_regmap_irqs);
	chip->handle_pre_irq = ws16c48_handle_pre_irq;
	chip->handle_post_irq = ws16c48_handle_post_irq;
	chip->handle_mask_sync = ws16c48_handle_mask_sync;
	chip->set_type_config = ws16c48_set_type_config;
	chip->irq_drv_data = ws16c48gpio;

	raw_spin_lock_init(&ws16c48gpio->lock);

	/* Initialize to prevent spurious interrupts before we're ready */
	err = ws16c48_irq_init_hw(ws16c48gpio->map);
	if (err)
		return err;

	err = devm_regmap_add_irq_chip(dev, ws16c48gpio->map, irq[id], 0, 0, chip, &chip_data);
	if (err)
		return dev_err_probe(dev, err, "IRQ registration failed\n");

	gpio_config.parent = dev;
	gpio_config.regmap = ws16c48gpio->map;
	gpio_config.ngpio = WS16C48_NGPIO;
	gpio_config.names = ws16c48_names;
	gpio_config.reg_dat_base = GPIO_REGMAP_ADDR(WS16C48_DAT_BASE);
	gpio_config.reg_set_base = GPIO_REGMAP_ADDR(WS16C48_DAT_BASE);
	/* Setting a GPIO to 0 allows it to be used as an input */
	gpio_config.reg_dir_out_base = GPIO_REGMAP_ADDR(WS16C48_DAT_BASE);
	gpio_config.ngpio_per_reg = WS16C48_NGPIO_PER_REG;
	gpio_config.irq_domain = regmap_irq_get_domain(chip_data);

	return PTR_ERR_OR_ZERO(devm_gpio_regmap_register(dev, &gpio_config));
}

static struct isa_driver ws16c48_driver = {
	.probe = ws16c48_probe,
	.driver = {
		.name = "ws16c48"
	},
};

module_isa_driver_with_irq(ws16c48_driver, num_ws16c48, num_irq);

MODULE_AUTHOR("William Breathitt Gray <vilhelm.gray@gmail.com>");
MODULE_DESCRIPTION("WinSystems WS16C48 GPIO driver");
MODULE_LICENSE("GPL v2");
