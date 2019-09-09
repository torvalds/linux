// SPDX-License-Identifier: GPL-2.0-only
/*
 * MEN 16Z127 GPIO driver
 *
 * Copyright (C) 2016 MEN Mikroelektronik GmbH (www.men.de)
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/mcb.h>
#include <linux/bitops.h>
#include <linux/gpio/driver.h>

#define MEN_Z127_CTRL	0x00
#define MEN_Z127_PSR	0x04
#define MEN_Z127_IRQR	0x08
#define MEN_Z127_GPIODR	0x0c
#define MEN_Z127_IER1	0x10
#define MEN_Z127_IER2	0x14
#define MEN_Z127_DBER	0x18
#define MEN_Z127_ODER	0x1C
#define GPIO_TO_DBCNT_REG(gpio)	((gpio * 4) + 0x80)

#define MEN_Z127_DB_MIN_US	50
/* 16 bit compare register. Each bit represents 50us */
#define MEN_Z127_DB_MAX_US	(0xffff * MEN_Z127_DB_MIN_US)
#define MEN_Z127_DB_IN_RANGE(db)	((db >= MEN_Z127_DB_MIN_US) && \
					 (db <= MEN_Z127_DB_MAX_US))

struct men_z127_gpio {
	struct gpio_chip gc;
	void __iomem *reg_base;
	struct resource *mem;
};

static int men_z127_debounce(struct gpio_chip *gc, unsigned gpio,
			     unsigned debounce)
{
	struct men_z127_gpio *priv = gpiochip_get_data(gc);
	struct device *dev = gc->parent;
	unsigned int rnd;
	u32 db_en, db_cnt;

	if (!MEN_Z127_DB_IN_RANGE(debounce)) {
		dev_err(dev, "debounce value %u out of range", debounce);
		return -EINVAL;
	}

	if (debounce > 0) {
		/* round up or down depending on MSB-1 */
		rnd = fls(debounce) - 1;

		if (rnd && (debounce & BIT(rnd - 1)))
			debounce = roundup(debounce, MEN_Z127_DB_MIN_US);
		else
			debounce = rounddown(debounce, MEN_Z127_DB_MIN_US);

		if (debounce > MEN_Z127_DB_MAX_US)
			debounce = MEN_Z127_DB_MAX_US;

		/* 50us per register unit */
		debounce /= 50;
	}

	spin_lock(&gc->bgpio_lock);

	db_en = readl(priv->reg_base + MEN_Z127_DBER);

	if (debounce == 0) {
		db_en &= ~BIT(gpio);
		db_cnt = 0;
	} else {
		db_en |= BIT(gpio);
		db_cnt = debounce;
	}

	writel(db_en, priv->reg_base + MEN_Z127_DBER);
	writel(db_cnt, priv->reg_base + GPIO_TO_DBCNT_REG(gpio));

	spin_unlock(&gc->bgpio_lock);

	return 0;
}

static int men_z127_set_single_ended(struct gpio_chip *gc,
				     unsigned offset,
				     enum pin_config_param param)
{
	struct men_z127_gpio *priv = gpiochip_get_data(gc);
	u32 od_en;

	spin_lock(&gc->bgpio_lock);
	od_en = readl(priv->reg_base + MEN_Z127_ODER);

	if (param == PIN_CONFIG_DRIVE_OPEN_DRAIN)
		od_en |= BIT(offset);
	else
		/* Implicitly PIN_CONFIG_DRIVE_PUSH_PULL */
		od_en &= ~BIT(offset);

	writel(od_en, priv->reg_base + MEN_Z127_ODER);
	spin_unlock(&gc->bgpio_lock);

	return 0;
}

static int men_z127_set_config(struct gpio_chip *gc, unsigned offset,
			       unsigned long config)
{
	enum pin_config_param param = pinconf_to_config_param(config);

	switch (param) {
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
	case PIN_CONFIG_DRIVE_PUSH_PULL:
		return men_z127_set_single_ended(gc, offset, param);

	case PIN_CONFIG_INPUT_DEBOUNCE:
		return men_z127_debounce(gc, offset,
			pinconf_to_config_argument(config));

	default:
		break;
	}

	return -ENOTSUPP;
}

static int men_z127_probe(struct mcb_device *mdev,
			  const struct mcb_device_id *id)
{
	struct men_z127_gpio *men_z127_gpio;
	struct device *dev = &mdev->dev;
	int ret;

	men_z127_gpio = devm_kzalloc(dev, sizeof(struct men_z127_gpio),
				     GFP_KERNEL);
	if (!men_z127_gpio)
		return -ENOMEM;

	men_z127_gpio->mem = mcb_request_mem(mdev, dev_name(dev));
	if (IS_ERR(men_z127_gpio->mem)) {
		dev_err(dev, "failed to request device memory");
		return PTR_ERR(men_z127_gpio->mem);
	}

	men_z127_gpio->reg_base = ioremap(men_z127_gpio->mem->start,
					  resource_size(men_z127_gpio->mem));
	if (men_z127_gpio->reg_base == NULL) {
		ret = -ENXIO;
		goto err_release;
	}

	mcb_set_drvdata(mdev, men_z127_gpio);

	ret = bgpio_init(&men_z127_gpio->gc, &mdev->dev, 4,
			 men_z127_gpio->reg_base + MEN_Z127_PSR,
			 men_z127_gpio->reg_base + MEN_Z127_CTRL,
			 NULL,
			 men_z127_gpio->reg_base + MEN_Z127_GPIODR,
			 NULL, 0);
	if (ret)
		goto err_unmap;

	men_z127_gpio->gc.set_config = men_z127_set_config;

	ret = gpiochip_add_data(&men_z127_gpio->gc, men_z127_gpio);
	if (ret) {
		dev_err(dev, "failed to register MEN 16Z127 GPIO controller");
		goto err_unmap;
	}

	dev_info(dev, "MEN 16Z127 GPIO driver registered");

	return 0;

err_unmap:
	iounmap(men_z127_gpio->reg_base);
err_release:
	mcb_release_mem(men_z127_gpio->mem);
	return ret;
}

static void men_z127_remove(struct mcb_device *mdev)
{
	struct men_z127_gpio *men_z127_gpio = mcb_get_drvdata(mdev);

	gpiochip_remove(&men_z127_gpio->gc);
	iounmap(men_z127_gpio->reg_base);
	mcb_release_mem(men_z127_gpio->mem);
}

static const struct mcb_device_id men_z127_ids[] = {
	{ .device = 0x7f },
	{ }
};
MODULE_DEVICE_TABLE(mcb, men_z127_ids);

static struct mcb_driver men_z127_driver = {
	.driver = {
		.name = "z127-gpio",
	},
	.probe = men_z127_probe,
	.remove = men_z127_remove,
	.id_table = men_z127_ids,
};
module_mcb_driver(men_z127_driver);

MODULE_AUTHOR("Andreas Werner <andreas.werner@men.de>");
MODULE_DESCRIPTION("MEN 16z127 GPIO Controller");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("mcb:16z127");
