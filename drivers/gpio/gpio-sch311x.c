/*
 * GPIO driver for the SMSC SCH311x Super-I/O chips
 *
 * Copyright (C) 2013 Bruno Randolf <br1@einfach.org>
 *
 * SuperIO functions and chip detection:
 * (c) Copyright 2008 Wim Van Sebroeck <wim@iguana.be>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/bitops.h>
#include <linux/io.h>

#define DRV_NAME			"gpio-sch311x"

#define SCH311X_GPIO_CONF_OUT		0x00
#define SCH311X_GPIO_CONF_IN		0x01
#define SCH311X_GPIO_CONF_INVERT	0x02
#define SCH311X_GPIO_CONF_OPEN_DRAIN	0x80

#define SIO_CONFIG_KEY_ENTER		0x55
#define SIO_CONFIG_KEY_EXIT		0xaa

#define GP1				0x4b

static int sch311x_ioports[] = { 0x2e, 0x4e, 0x162e, 0x164e };

static struct platform_device *sch311x_gpio_pdev;

struct sch311x_pdev_data {		/* platform device data */
	unsigned short runtime_reg;	/* runtime register base address */
};

struct sch311x_gpio_block {		/* one GPIO block runtime data */
	struct gpio_chip chip;
	unsigned short data_reg;	/* from definition below */
	unsigned short *config_regs;	/* pointer to definition below */
	unsigned short runtime_reg;	/* runtime register */
	spinlock_t lock;		/* lock for this GPIO block */
};

struct sch311x_gpio_priv {		/* driver private data */
	struct sch311x_gpio_block blocks[6];
};

struct sch311x_gpio_block_def {		/* register address definitions */
	unsigned short data_reg;
	unsigned short config_regs[8];
	unsigned short base;
};

/* Note: some GPIOs are not available, these are marked with 0x00 */

static struct sch311x_gpio_block_def sch311x_gpio_blocks[] = {
	{
		.data_reg = 0x4b,	/* GP1 */
		.config_regs = {0x23, 0x24, 0x25, 0x26, 0x27, 0x29, 0x2a, 0x2b},
		.base = 10,
	},
	{
		.data_reg = 0x4c,	/* GP2 */
		.config_regs = {0x00, 0x2c, 0x2d, 0x00, 0x00, 0x00, 0x00, 0x32},
		.base = 20,
	},
	{
		.data_reg = 0x4d,	/* GP3 */
		.config_regs = {0x33, 0x34, 0x35, 0x36, 0x37, 0x00, 0x39, 0x3a},
		.base = 30,
	},
	{
		.data_reg = 0x4e,	/* GP4 */
		.config_regs = {0x3b, 0x00, 0x3d, 0x00, 0x6e, 0x6f, 0x72, 0x73},
		.base = 40,
	},
	{
		.data_reg = 0x4f,	/* GP5 */
		.config_regs = {0x3f, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46},
		.base = 50,
	},
	{
		.data_reg = 0x50,	/* GP6 */
		.config_regs = {0x47, 0x48, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59},
		.base = 60,
	},
};

static inline struct sch311x_gpio_block *
to_sch311x_gpio_block(struct gpio_chip *chip)
{
	return container_of(chip, struct sch311x_gpio_block, chip);
}


/*
 *	Super-IO functions
 */

static inline int sch311x_sio_enter(int sio_config_port)
{
	/* Don't step on other drivers' I/O space by accident. */
	if (!request_muxed_region(sio_config_port, 2, DRV_NAME)) {
		pr_err(DRV_NAME "I/O address 0x%04x already in use\n",
		       sio_config_port);
		return -EBUSY;
	}

	outb(SIO_CONFIG_KEY_ENTER, sio_config_port);
	return 0;
}

static inline void sch311x_sio_exit(int sio_config_port)
{
	outb(SIO_CONFIG_KEY_EXIT, sio_config_port);
	release_region(sio_config_port, 2);
}

static inline int sch311x_sio_inb(int sio_config_port, int reg)
{
	outb(reg, sio_config_port);
	return inb(sio_config_port + 1);
}

static inline void sch311x_sio_outb(int sio_config_port, int reg, int val)
{
	outb(reg, sio_config_port);
	outb(val, sio_config_port + 1);
}


/*
 *	GPIO functions
 */

static int sch311x_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	struct sch311x_gpio_block *block = to_sch311x_gpio_block(chip);

	if (block->config_regs[offset] == 0) /* GPIO is not available */
		return -ENODEV;

	if (!request_region(block->runtime_reg + block->config_regs[offset],
			    1, DRV_NAME)) {
		dev_err(chip->dev, "Failed to request region 0x%04x.\n",
			block->runtime_reg + block->config_regs[offset]);
		return -EBUSY;
	}
	return 0;
}

static void sch311x_gpio_free(struct gpio_chip *chip, unsigned offset)
{
	struct sch311x_gpio_block *block = to_sch311x_gpio_block(chip);

	if (block->config_regs[offset] == 0) /* GPIO is not available */
		return;

	release_region(block->runtime_reg + block->config_regs[offset], 1);
}

static int sch311x_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct sch311x_gpio_block *block = to_sch311x_gpio_block(chip);
	unsigned char data;

	spin_lock(&block->lock);
	data = inb(block->runtime_reg + block->data_reg);
	spin_unlock(&block->lock);

	return !!(data & BIT(offset));
}

static void __sch311x_gpio_set(struct sch311x_gpio_block *block,
			       unsigned offset, int value)
{
	unsigned char data = inb(block->runtime_reg + block->data_reg);
	if (value)
		data |= BIT(offset);
	else
		data &= ~BIT(offset);
	outb(data, block->runtime_reg + block->data_reg);
}

static void sch311x_gpio_set(struct gpio_chip *chip, unsigned offset,
			     int value)
{
	struct sch311x_gpio_block *block = to_sch311x_gpio_block(chip);

	spin_lock(&block->lock);
	 __sch311x_gpio_set(block, offset, value);
	spin_unlock(&block->lock);
}

static int sch311x_gpio_direction_in(struct gpio_chip *chip, unsigned offset)
{
	struct sch311x_gpio_block *block = to_sch311x_gpio_block(chip);

	spin_lock(&block->lock);
	outb(SCH311X_GPIO_CONF_IN, block->runtime_reg +
	     block->config_regs[offset]);
	spin_unlock(&block->lock);

	return 0;
}

static int sch311x_gpio_direction_out(struct gpio_chip *chip, unsigned offset,
				      int value)
{
	struct sch311x_gpio_block *block = to_sch311x_gpio_block(chip);

	spin_lock(&block->lock);

	outb(SCH311X_GPIO_CONF_OUT, block->runtime_reg +
	     block->config_regs[offset]);

	__sch311x_gpio_set(block, offset, value);

	spin_unlock(&block->lock);
	return 0;
}

static int sch311x_gpio_probe(struct platform_device *pdev)
{
	struct sch311x_pdev_data *pdata = pdev->dev.platform_data;
	struct sch311x_gpio_priv *priv;
	struct sch311x_gpio_block *block;
	int err, i;

	/* we can register all GPIO data registers at once */
	if (!request_region(pdata->runtime_reg + GP1, 6, DRV_NAME)) {
		dev_err(&pdev->dev, "Failed to request region 0x%04x-0x%04x.\n",
			pdata->runtime_reg + GP1, pdata->runtime_reg + GP1 + 5);
		return -EBUSY;
	}

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	platform_set_drvdata(pdev, priv);

	for (i = 0; i < ARRAY_SIZE(priv->blocks); i++) {
		block = &priv->blocks[i];

		spin_lock_init(&block->lock);

		block->chip.label = DRV_NAME;
		block->chip.owner = THIS_MODULE;
		block->chip.request = sch311x_gpio_request;
		block->chip.free = sch311x_gpio_free;
		block->chip.direction_input = sch311x_gpio_direction_in;
		block->chip.direction_output = sch311x_gpio_direction_out;
		block->chip.get = sch311x_gpio_get;
		block->chip.set = sch311x_gpio_set;
		block->chip.ngpio = 8;
		block->chip.dev = &pdev->dev;
		block->chip.base = sch311x_gpio_blocks[i].base;
		block->config_regs = sch311x_gpio_blocks[i].config_regs;
		block->data_reg = sch311x_gpio_blocks[i].data_reg;
		block->runtime_reg = pdata->runtime_reg;

		err = gpiochip_add(&block->chip);
		if (err < 0) {
			dev_err(&pdev->dev,
				"Could not register gpiochip, %d\n", err);
			goto exit_err;
		}
		dev_info(&pdev->dev,
			 "SMSC SCH311x GPIO block %d registered.\n", i);
	}

	return 0;

exit_err:
	release_region(pdata->runtime_reg + GP1, 6);
	/* release already registered chips */
	for (--i; i >= 0; i--)
		gpiochip_remove(&priv->blocks[i].chip);
	return err;
}

static int sch311x_gpio_remove(struct platform_device *pdev)
{
	struct sch311x_pdev_data *pdata = pdev->dev.platform_data;
	struct sch311x_gpio_priv *priv = platform_get_drvdata(pdev);
	int err, i;

	release_region(pdata->runtime_reg + GP1, 6);

	for (i = 0; i < ARRAY_SIZE(priv->blocks); i++) {
		err = gpiochip_remove(&priv->blocks[i].chip);
		if (err)
			return err;
		dev_info(&pdev->dev,
			 "SMSC SCH311x GPIO block %d unregistered.\n", i);
	}
	return 0;
}

static struct platform_driver sch311x_gpio_driver = {
	.driver.name	= DRV_NAME,
	.driver.owner	= THIS_MODULE,
	.probe		= sch311x_gpio_probe,
	.remove		= sch311x_gpio_remove,
};


/*
 *	Init & exit routines
 */

static int __init sch311x_detect(int sio_config_port, unsigned short *addr)
{
	int err = 0, reg;
	unsigned short base_addr;
	unsigned char dev_id;

	err = sch311x_sio_enter(sio_config_port);
	if (err)
		return err;

	/* Check device ID. */
	reg = sch311x_sio_inb(sio_config_port, 0x20);
	switch (reg) {
	case 0x7c: /* SCH3112 */
		dev_id = 2;
		break;
	case 0x7d: /* SCH3114 */
		dev_id = 4;
		break;
	case 0x7f: /* SCH3116 */
		dev_id = 6;
		break;
	default:
		err = -ENODEV;
		goto exit;
	}

	/* Select logical device A (runtime registers) */
	sch311x_sio_outb(sio_config_port, 0x07, 0x0a);

	/* Check if Logical Device Register is currently active */
	if ((sch311x_sio_inb(sio_config_port, 0x30) & 0x01) == 0)
		pr_info("Seems that LDN 0x0a is not active...\n");

	/* Get the base address of the runtime registers */
	base_addr = (sch311x_sio_inb(sio_config_port, 0x60) << 8) |
			   sch311x_sio_inb(sio_config_port, 0x61);
	if (!base_addr) {
		pr_err("Base address not set\n");
		err = -ENODEV;
		goto exit;
	}
	*addr = base_addr;

	pr_info("Found an SMSC SCH311%d chip at 0x%04x\n", dev_id, base_addr);

exit:
	sch311x_sio_exit(sio_config_port);
	return err;
}

static int __init sch311x_gpio_pdev_add(const unsigned short addr)
{
	struct sch311x_pdev_data pdata;
	int err;

	pdata.runtime_reg = addr;

	sch311x_gpio_pdev = platform_device_alloc(DRV_NAME, -1);
	if (!sch311x_gpio_pdev)
		return -ENOMEM;

	err = platform_device_add_data(sch311x_gpio_pdev,
				       &pdata, sizeof(pdata));
	if (err) {
		pr_err(DRV_NAME "Platform data allocation failed\n");
		goto err;
	}

	err = platform_device_add(sch311x_gpio_pdev);
	if (err) {
		pr_err(DRV_NAME "Device addition failed\n");
		goto err;
	}
	return 0;

err:
	platform_device_put(sch311x_gpio_pdev);
	return err;
}

static int __init sch311x_gpio_init(void)
{
	int err, i;
	unsigned short addr = 0;

	for (i = 0; i < ARRAY_SIZE(sch311x_ioports); i++)
		if (sch311x_detect(sch311x_ioports[i], &addr) == 0)
			break;

	if (!addr)
		return -ENODEV;

	err = platform_driver_register(&sch311x_gpio_driver);
	if (err)
		return err;

	err = sch311x_gpio_pdev_add(addr);
	if (err)
		goto unreg_platform_driver;

	return 0;

unreg_platform_driver:
	platform_driver_unregister(&sch311x_gpio_driver);
	return err;
}

static void __exit sch311x_gpio_exit(void)
{
	platform_device_unregister(sch311x_gpio_pdev);
	platform_driver_unregister(&sch311x_gpio_driver);
}

module_init(sch311x_gpio_init);
module_exit(sch311x_gpio_exit);

MODULE_AUTHOR("Bruno Randolf <br1@einfach.org>");
MODULE_DESCRIPTION("SMSC SCH311x GPIO Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:gpio-sch311x");
