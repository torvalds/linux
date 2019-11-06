// SPDX-License-Identifier: GPL-2.0
/*
 *  Turris Mox Moxtet GPIO expander
 *
 *  Copyright (C) 2018 Marek Behun <marek.behun@nic.cz>
 */

#include <linux/bitops.h>
#include <linux/gpio/driver.h>
#include <linux/moxtet.h>
#include <linux/module.h>

#define MOXTET_GPIO_NGPIOS	12
#define MOXTET_GPIO_INPUTS	4

struct moxtet_gpio_desc {
	u16 in_mask;
	u16 out_mask;
};

static const struct moxtet_gpio_desc descs[] = {
	[TURRIS_MOX_MODULE_SFP] = {
		.in_mask = GENMASK(2, 0),
		.out_mask = GENMASK(5, 4),
	},
};

struct moxtet_gpio_chip {
	struct device			*dev;
	struct gpio_chip		gpio_chip;
	const struct moxtet_gpio_desc	*desc;
};

static int moxtet_gpio_get_value(struct gpio_chip *gc, unsigned int offset)
{
	struct moxtet_gpio_chip *chip = gpiochip_get_data(gc);
	int ret;

	if (chip->desc->in_mask & BIT(offset)) {
		ret = moxtet_device_read(chip->dev);
	} else if (chip->desc->out_mask & BIT(offset)) {
		ret = moxtet_device_written(chip->dev);
		if (ret >= 0)
			ret <<= MOXTET_GPIO_INPUTS;
	} else {
		return -EINVAL;
	}

	if (ret < 0)
		return ret;

	return !!(ret & BIT(offset));
}

static void moxtet_gpio_set_value(struct gpio_chip *gc, unsigned int offset,
				  int val)
{
	struct moxtet_gpio_chip *chip = gpiochip_get_data(gc);
	int state;

	state = moxtet_device_written(chip->dev);
	if (state < 0)
		return;

	offset -= MOXTET_GPIO_INPUTS;

	if (val)
		state |= BIT(offset);
	else
		state &= ~BIT(offset);

	moxtet_device_write(chip->dev, state);
}

static int moxtet_gpio_get_direction(struct gpio_chip *gc, unsigned int offset)
{
	struct moxtet_gpio_chip *chip = gpiochip_get_data(gc);

	/* All lines are hard wired to be either input or output, not both. */
	if (chip->desc->in_mask & BIT(offset))
		return GPIO_LINE_DIRECTION_IN;
	else if (chip->desc->out_mask & BIT(offset))
		return GPIO_LINE_DIRECTION_OUT;
	else
		return -EINVAL;
}

static int moxtet_gpio_direction_input(struct gpio_chip *gc,
				       unsigned int offset)
{
	struct moxtet_gpio_chip *chip = gpiochip_get_data(gc);

	if (chip->desc->in_mask & BIT(offset))
		return 0;
	else if (chip->desc->out_mask & BIT(offset))
		return -ENOTSUPP;
	else
		return -EINVAL;
}

static int moxtet_gpio_direction_output(struct gpio_chip *gc,
					unsigned int offset, int val)
{
	struct moxtet_gpio_chip *chip = gpiochip_get_data(gc);

	if (chip->desc->out_mask & BIT(offset))
		moxtet_gpio_set_value(gc, offset, val);
	else if (chip->desc->in_mask & BIT(offset))
		return -ENOTSUPP;
	else
		return -EINVAL;

	return 0;
}

static int moxtet_gpio_probe(struct device *dev)
{
	struct moxtet_gpio_chip *chip;
	struct device_node *nc = dev->of_node;
	int id;

	id = to_moxtet_device(dev)->id;

	if (id >= ARRAY_SIZE(descs)) {
		dev_err(dev, "%pOF Moxtet device id 0x%x is not supported by gpio-moxtet driver\n",
			nc, id);
		return -ENOTSUPP;
	}

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = dev;
	chip->gpio_chip.parent = dev;
	chip->desc = &descs[id];

	dev_set_drvdata(dev, chip);

	chip->gpio_chip.label = dev_name(dev);
	chip->gpio_chip.get_direction = moxtet_gpio_get_direction;
	chip->gpio_chip.direction_input = moxtet_gpio_direction_input;
	chip->gpio_chip.direction_output = moxtet_gpio_direction_output;
	chip->gpio_chip.get = moxtet_gpio_get_value;
	chip->gpio_chip.set = moxtet_gpio_set_value;
	chip->gpio_chip.base = -1;

	chip->gpio_chip.ngpio = MOXTET_GPIO_NGPIOS;

	chip->gpio_chip.can_sleep = true;
	chip->gpio_chip.owner = THIS_MODULE;

	return devm_gpiochip_add_data(dev, &chip->gpio_chip, chip);
}

static const struct of_device_id moxtet_gpio_dt_ids[] = {
	{ .compatible = "cznic,moxtet-gpio", },
	{},
};
MODULE_DEVICE_TABLE(of, moxtet_gpio_dt_ids);

static const enum turris_mox_module_id moxtet_gpio_module_table[] = {
	TURRIS_MOX_MODULE_SFP,
	0,
};

static struct moxtet_driver moxtet_gpio_driver = {
	.driver = {
		.name		= "moxtet-gpio",
		.of_match_table	= moxtet_gpio_dt_ids,
		.probe		= moxtet_gpio_probe,
	},
	.id_table = moxtet_gpio_module_table,
};
module_moxtet_driver(moxtet_gpio_driver);

MODULE_AUTHOR("Marek Behun <marek.behun@nic.cz>");
MODULE_DESCRIPTION("Turris Mox Moxtet GPIO expander");
MODULE_LICENSE("GPL v2");
