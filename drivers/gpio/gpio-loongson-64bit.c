// SPDX-License-Identifier: GPL-2.0+
/*
 * Loongson GPIO Support
 *
 * Copyright (C) 2022-2023 Loongson Technology Corporation Limited
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/platform_device.h>
#include <linux/bitops.h>
#include <asm/types.h>

enum loongson_gpio_mode {
	BIT_CTRL_MODE,
	BYTE_CTRL_MODE,
};

struct loongson_gpio_chip_data {
	const char		*label;
	enum loongson_gpio_mode	mode;
	unsigned int		conf_offset;
	unsigned int		out_offset;
	unsigned int		in_offset;
	unsigned int		inten_offset;
};

struct loongson_gpio_chip {
	struct gpio_chip	chip;
	spinlock_t		lock;
	void __iomem		*reg_base;
	const struct loongson_gpio_chip_data *chip_data;
};

static inline struct loongson_gpio_chip *to_loongson_gpio_chip(struct gpio_chip *chip)
{
	return container_of(chip, struct loongson_gpio_chip, chip);
}

static inline void loongson_commit_direction(struct loongson_gpio_chip *lgpio, unsigned int pin,
					     int input)
{
	u8 bval = input ? 1 : 0;

	writeb(bval, lgpio->reg_base + lgpio->chip_data->conf_offset + pin);
}

static void loongson_commit_level(struct loongson_gpio_chip *lgpio, unsigned int pin, int high)
{
	u8 bval = high ? 1 : 0;

	writeb(bval, lgpio->reg_base + lgpio->chip_data->out_offset + pin);
}

static int loongson_gpio_direction_input(struct gpio_chip *chip, unsigned int pin)
{
	unsigned long flags;
	struct loongson_gpio_chip *lgpio = to_loongson_gpio_chip(chip);

	spin_lock_irqsave(&lgpio->lock, flags);
	loongson_commit_direction(lgpio, pin, 1);
	spin_unlock_irqrestore(&lgpio->lock, flags);

	return 0;
}

static int loongson_gpio_direction_output(struct gpio_chip *chip, unsigned int pin, int value)
{
	unsigned long flags;
	struct loongson_gpio_chip *lgpio = to_loongson_gpio_chip(chip);

	spin_lock_irqsave(&lgpio->lock, flags);
	loongson_commit_level(lgpio, pin, value);
	loongson_commit_direction(lgpio, pin, 0);
	spin_unlock_irqrestore(&lgpio->lock, flags);

	return 0;
}

static int loongson_gpio_get(struct gpio_chip *chip, unsigned int pin)
{
	u8  bval;
	int val;
	struct loongson_gpio_chip *lgpio = to_loongson_gpio_chip(chip);

	bval = readb(lgpio->reg_base + lgpio->chip_data->in_offset + pin);
	val = bval & 1;

	return val;
}

static int loongson_gpio_get_direction(struct gpio_chip *chip, unsigned int pin)
{
	u8  bval;
	struct loongson_gpio_chip *lgpio = to_loongson_gpio_chip(chip);

	bval = readb(lgpio->reg_base + lgpio->chip_data->conf_offset + pin);
	if (bval & 1)
		return GPIO_LINE_DIRECTION_IN;

	return GPIO_LINE_DIRECTION_OUT;
}

static int loongson_gpio_set(struct gpio_chip *chip, unsigned int pin, int value)
{
	unsigned long flags;
	struct loongson_gpio_chip *lgpio = to_loongson_gpio_chip(chip);

	spin_lock_irqsave(&lgpio->lock, flags);
	loongson_commit_level(lgpio, pin, value);
	spin_unlock_irqrestore(&lgpio->lock, flags);

	return 0;
}

static int loongson_gpio_to_irq(struct gpio_chip *chip, unsigned int offset)
{
	unsigned int u;
	struct platform_device *pdev = to_platform_device(chip->parent);
	struct loongson_gpio_chip *lgpio = to_loongson_gpio_chip(chip);

	if (lgpio->chip_data->mode == BIT_CTRL_MODE) {
		/* Get the register index from offset then multiply by bytes per register */
		u = readl(lgpio->reg_base + lgpio->chip_data->inten_offset + (offset / 32) * 4);
		u |= BIT(offset % 32);
		writel(u, lgpio->reg_base + lgpio->chip_data->inten_offset + (offset / 32) * 4);
	} else {
		writeb(1, lgpio->reg_base + lgpio->chip_data->inten_offset + offset);
	}

	return platform_get_irq(pdev, offset);
}

static int loongson_gpio_init(struct device *dev, struct loongson_gpio_chip *lgpio,
			      void __iomem *reg_base)
{
	int ret;

	lgpio->reg_base = reg_base;
	if (lgpio->chip_data->mode == BIT_CTRL_MODE) {
		ret = bgpio_init(&lgpio->chip, dev, 8,
				lgpio->reg_base + lgpio->chip_data->in_offset,
				lgpio->reg_base + lgpio->chip_data->out_offset,
				NULL, NULL,
				lgpio->reg_base + lgpio->chip_data->conf_offset,
				0);
		if (ret) {
			dev_err(dev, "unable to init generic GPIO\n");
			return ret;
		}
	} else {
		lgpio->chip.direction_input = loongson_gpio_direction_input;
		lgpio->chip.get = loongson_gpio_get;
		lgpio->chip.get_direction = loongson_gpio_get_direction;
		lgpio->chip.direction_output = loongson_gpio_direction_output;
		lgpio->chip.set_rv = loongson_gpio_set;
		lgpio->chip.parent = dev;
		spin_lock_init(&lgpio->lock);
	}

	lgpio->chip.label = lgpio->chip_data->label;
	lgpio->chip.can_sleep = false;
	if (lgpio->chip_data->inten_offset)
		lgpio->chip.to_irq = loongson_gpio_to_irq;

	return devm_gpiochip_add_data(dev, &lgpio->chip, lgpio);
}

static int loongson_gpio_probe(struct platform_device *pdev)
{
	void __iomem *reg_base;
	struct loongson_gpio_chip *lgpio;
	struct device *dev = &pdev->dev;

	lgpio = devm_kzalloc(dev, sizeof(*lgpio), GFP_KERNEL);
	if (!lgpio)
		return -ENOMEM;

	lgpio->chip_data = device_get_match_data(dev);

	reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(reg_base))
		return PTR_ERR(reg_base);

	return loongson_gpio_init(dev, lgpio, reg_base);
}

static const struct loongson_gpio_chip_data loongson_gpio_ls2k_data = {
	.label = "ls2k_gpio",
	.mode = BIT_CTRL_MODE,
	.conf_offset = 0x0,
	.in_offset = 0x20,
	.out_offset = 0x10,
	.inten_offset = 0x30,
};

static const struct loongson_gpio_chip_data loongson_gpio_ls2k0500_data0 = {
	.label = "ls2k0500_gpio",
	.mode = BIT_CTRL_MODE,
	.conf_offset = 0x0,
	.in_offset = 0x8,
	.out_offset = 0x10,
	.inten_offset = 0xb0,
};

static const struct loongson_gpio_chip_data loongson_gpio_ls2k0500_data1 = {
	.label = "ls2k0500_gpio",
	.mode = BIT_CTRL_MODE,
	.conf_offset = 0x0,
	.in_offset = 0x8,
	.out_offset = 0x10,
	.inten_offset = 0x98,
};

static const struct loongson_gpio_chip_data loongson_gpio_ls2k2000_data0 = {
	.label = "ls2k2000_gpio",
	.mode = BIT_CTRL_MODE,
	.conf_offset = 0x0,
	.in_offset = 0xc,
	.out_offset = 0x8,
};

static const struct loongson_gpio_chip_data loongson_gpio_ls2k2000_data1 = {
	.label = "ls2k2000_gpio",
	.mode = BIT_CTRL_MODE,
	.conf_offset = 0x0,
	.in_offset = 0x20,
	.out_offset = 0x10,
};

static const struct loongson_gpio_chip_data loongson_gpio_ls2k2000_data2 = {
	.label = "ls2k2000_gpio",
	.mode = BIT_CTRL_MODE,
	.conf_offset = 0x4,
	.in_offset = 0x8,
	.out_offset = 0x0,
};

static const struct loongson_gpio_chip_data loongson_gpio_ls3a5000_data = {
	.label = "ls3a5000_gpio",
	.mode = BIT_CTRL_MODE,
	.conf_offset = 0x0,
	.in_offset = 0xc,
	.out_offset = 0x8,
};

static const struct loongson_gpio_chip_data loongson_gpio_ls7a_data = {
	.label = "ls7a_gpio",
	.mode = BYTE_CTRL_MODE,
	.conf_offset = 0x800,
	.in_offset = 0xa00,
	.out_offset = 0x900,
};

/* LS7A2000 chipset GPIO */
static const struct loongson_gpio_chip_data loongson_gpio_ls7a2000_data0 = {
	.label = "ls7a2000_gpio",
	.mode = BYTE_CTRL_MODE,
	.conf_offset = 0x800,
	.in_offset = 0xa00,
	.out_offset = 0x900,
};

/* LS7A2000 ACPI GPIO */
static const struct loongson_gpio_chip_data loongson_gpio_ls7a2000_data1 = {
	.label = "ls7a2000_gpio",
	.mode = BIT_CTRL_MODE,
	.conf_offset = 0x4,
	.in_offset = 0x8,
	.out_offset = 0x0,
};

/* Loongson-3A6000 node GPIO */
static const struct loongson_gpio_chip_data loongson_gpio_ls3a6000_data = {
	.label = "ls3a6000_gpio",
	.mode = BIT_CTRL_MODE,
	.conf_offset = 0x0,
	.in_offset = 0xc,
	.out_offset = 0x8,
};

static const struct of_device_id loongson_gpio_of_match[] = {
	{
		.compatible = "loongson,ls2k-gpio",
		.data = &loongson_gpio_ls2k_data,
	},
	{
		.compatible = "loongson,ls2k0500-gpio0",
		.data = &loongson_gpio_ls2k0500_data0,
	},
	{
		.compatible = "loongson,ls2k0500-gpio1",
		.data = &loongson_gpio_ls2k0500_data1,
	},
	{
		.compatible = "loongson,ls2k2000-gpio0",
		.data = &loongson_gpio_ls2k2000_data0,
	},
	{
		.compatible = "loongson,ls2k2000-gpio1",
		.data = &loongson_gpio_ls2k2000_data1,
	},
	{
		.compatible = "loongson,ls2k2000-gpio2",
		.data = &loongson_gpio_ls2k2000_data2,
	},
	{
		.compatible = "loongson,ls3a5000-gpio",
		.data = &loongson_gpio_ls3a5000_data,
	},
	{
		.compatible = "loongson,ls7a-gpio",
		.data = &loongson_gpio_ls7a_data,
	},
	{
		.compatible = "loongson,ls7a2000-gpio1",
		.data = &loongson_gpio_ls7a2000_data0,
	},
	{
		.compatible = "loongson,ls7a2000-gpio2",
		.data = &loongson_gpio_ls7a2000_data1,
	},
	{
		.compatible = "loongson,ls3a6000-gpio",
		.data = &loongson_gpio_ls3a6000_data,
	},
	{}
};
MODULE_DEVICE_TABLE(of, loongson_gpio_of_match);

static const struct acpi_device_id loongson_gpio_acpi_match[] = {
	{
		.id = "LOON0002",
		.driver_data = (kernel_ulong_t)&loongson_gpio_ls7a_data,
	},
	{
		.id = "LOON0007",
		.driver_data = (kernel_ulong_t)&loongson_gpio_ls3a5000_data,
	},
	{
		.id = "LOON000A",
		.driver_data = (kernel_ulong_t)&loongson_gpio_ls2k2000_data0,
	},
	{
		.id = "LOON000B",
		.driver_data = (kernel_ulong_t)&loongson_gpio_ls2k2000_data1,
	},
	{
		.id = "LOON000C",
		.driver_data = (kernel_ulong_t)&loongson_gpio_ls2k2000_data2,
	},
	{
		.id = "LOON000D",
		.driver_data = (kernel_ulong_t)&loongson_gpio_ls7a2000_data0,
	},
	{
		.id = "LOON000E",
		.driver_data = (kernel_ulong_t)&loongson_gpio_ls7a2000_data1,
	},
	{
		.id = "LOON000F",
		.driver_data = (kernel_ulong_t)&loongson_gpio_ls3a6000_data,
	},
	{}
};
MODULE_DEVICE_TABLE(acpi, loongson_gpio_acpi_match);

static struct platform_driver loongson_gpio_driver = {
	.driver = {
		.name = "loongson-gpio",
		.of_match_table = loongson_gpio_of_match,
		.acpi_match_table = loongson_gpio_acpi_match,
	},
	.probe = loongson_gpio_probe,
};

static int __init loongson_gpio_setup(void)
{
	return platform_driver_register(&loongson_gpio_driver);
}
postcore_initcall(loongson_gpio_setup);

MODULE_DESCRIPTION("Loongson gpio driver");
MODULE_LICENSE("GPL");
