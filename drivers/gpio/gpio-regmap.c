// SPDX-License-Identifier: GPL-2.0-only
/*
 * regmap based generic GPIO driver
 *
 * Copyright 2020 Michael Walle <michael@walle.cc>
 */

#include <linux/gpio/driver.h>
#include <linux/gpio/regmap.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/regmap.h>

struct gpio_regmap {
	struct device *parent;
	struct regmap *regmap;
	struct gpio_chip gpio_chip;

	int reg_stride;
	int ngpio_per_reg;
	unsigned int reg_dat_base;
	unsigned int reg_set_base;
	unsigned int reg_clr_base;
	unsigned int reg_dir_in_base;
	unsigned int reg_dir_out_base;

	int (*reg_mask_xlate)(struct gpio_regmap *gpio, unsigned int base,
			      unsigned int offset, unsigned int *reg,
			      unsigned int *mask);

	void *driver_data;
};

static unsigned int gpio_regmap_addr(unsigned int addr)
{
	if (addr == GPIO_REGMAP_ADDR_ZERO)
		return 0;

	return addr;
}

static int gpio_regmap_simple_xlate(struct gpio_regmap *gpio,
				    unsigned int base, unsigned int offset,
				    unsigned int *reg, unsigned int *mask)
{
	unsigned int line = offset % gpio->ngpio_per_reg;
	unsigned int stride = offset / gpio->ngpio_per_reg;

	*reg = base + stride * gpio->reg_stride;
	*mask = BIT(line);

	return 0;
}

static int gpio_regmap_get(struct gpio_chip *chip, unsigned int offset)
{
	struct gpio_regmap *gpio = gpiochip_get_data(chip);
	unsigned int base, val, reg, mask;
	int ret;

	/* we might not have an output register if we are input only */
	if (gpio->reg_dat_base)
		base = gpio_regmap_addr(gpio->reg_dat_base);
	else
		base = gpio_regmap_addr(gpio->reg_set_base);

	ret = gpio->reg_mask_xlate(gpio, base, offset, &reg, &mask);
	if (ret)
		return ret;

	ret = regmap_read(gpio->regmap, reg, &val);
	if (ret)
		return ret;

	return !!(val & mask);
}

static void gpio_regmap_set(struct gpio_chip *chip, unsigned int offset,
			    int val)
{
	struct gpio_regmap *gpio = gpiochip_get_data(chip);
	unsigned int base = gpio_regmap_addr(gpio->reg_set_base);
	unsigned int reg, mask;

	gpio->reg_mask_xlate(gpio, base, offset, &reg, &mask);
	if (val)
		regmap_update_bits(gpio->regmap, reg, mask, mask);
	else
		regmap_update_bits(gpio->regmap, reg, mask, 0);
}

static void gpio_regmap_set_with_clear(struct gpio_chip *chip,
				       unsigned int offset, int val)
{
	struct gpio_regmap *gpio = gpiochip_get_data(chip);
	unsigned int base, reg, mask;

	if (val)
		base = gpio_regmap_addr(gpio->reg_set_base);
	else
		base = gpio_regmap_addr(gpio->reg_clr_base);

	gpio->reg_mask_xlate(gpio, base, offset, &reg, &mask);
	regmap_write(gpio->regmap, reg, mask);
}

static int gpio_regmap_get_direction(struct gpio_chip *chip,
				     unsigned int offset)
{
	struct gpio_regmap *gpio = gpiochip_get_data(chip);
	unsigned int base, val, reg, mask;
	int invert, ret;

	if (gpio->reg_dir_out_base) {
		base = gpio_regmap_addr(gpio->reg_dir_out_base);
		invert = 0;
	} else if (gpio->reg_dir_in_base) {
		base = gpio_regmap_addr(gpio->reg_dir_in_base);
		invert = 1;
	} else {
		return -EOPNOTSUPP;
	}

	ret = gpio->reg_mask_xlate(gpio, base, offset, &reg, &mask);
	if (ret)
		return ret;

	ret = regmap_read(gpio->regmap, reg, &val);
	if (ret)
		return ret;

	if (!!(val & mask) ^ invert)
		return GPIO_LINE_DIRECTION_OUT;
	else
		return GPIO_LINE_DIRECTION_IN;
}

static int gpio_regmap_set_direction(struct gpio_chip *chip,
				     unsigned int offset, bool output)
{
	struct gpio_regmap *gpio = gpiochip_get_data(chip);
	unsigned int base, val, reg, mask;
	int invert, ret;

	if (gpio->reg_dir_out_base) {
		base = gpio_regmap_addr(gpio->reg_dir_out_base);
		invert = 0;
	} else if (gpio->reg_dir_in_base) {
		base = gpio_regmap_addr(gpio->reg_dir_in_base);
		invert = 1;
	} else {
		return -EOPNOTSUPP;
	}

	ret = gpio->reg_mask_xlate(gpio, base, offset, &reg, &mask);
	if (ret)
		return ret;

	if (invert)
		val = output ? 0 : mask;
	else
		val = output ? mask : 0;

	return regmap_update_bits(gpio->regmap, reg, mask, val);
}

static int gpio_regmap_direction_input(struct gpio_chip *chip,
				       unsigned int offset)
{
	return gpio_regmap_set_direction(chip, offset, false);
}

static int gpio_regmap_direction_output(struct gpio_chip *chip,
					unsigned int offset, int value)
{
	gpio_regmap_set(chip, offset, value);

	return gpio_regmap_set_direction(chip, offset, true);
}

void *gpio_regmap_get_drvdata(struct gpio_regmap *gpio)
{
	return gpio->driver_data;
}
EXPORT_SYMBOL_GPL(gpio_regmap_get_drvdata);

/**
 * gpio_regmap_register() - Register a generic regmap GPIO controller
 * @config: configuration for gpio_regmap
 *
 * Return: A pointer to the registered gpio_regmap or ERR_PTR error value.
 */
struct gpio_regmap *gpio_regmap_register(const struct gpio_regmap_config *config)
{
	struct gpio_regmap *gpio;
	struct gpio_chip *chip;
	int ret;

	if (!config->parent)
		return ERR_PTR(-EINVAL);

	if (!config->ngpio)
		return ERR_PTR(-EINVAL);

	/* we need at least one */
	if (!config->reg_dat_base && !config->reg_set_base)
		return ERR_PTR(-EINVAL);

	/* if we have a direction register we need both input and output */
	if ((config->reg_dir_out_base || config->reg_dir_in_base) &&
	    (!config->reg_dat_base || !config->reg_set_base))
		return ERR_PTR(-EINVAL);

	/* we don't support having both registers simultaneously for now */
	if (config->reg_dir_out_base && config->reg_dir_in_base)
		return ERR_PTR(-EINVAL);

	gpio = kzalloc(sizeof(*gpio), GFP_KERNEL);
	if (!gpio)
		return ERR_PTR(-ENOMEM);

	gpio->parent = config->parent;
	gpio->driver_data = config->drvdata;
	gpio->regmap = config->regmap;
	gpio->ngpio_per_reg = config->ngpio_per_reg;
	gpio->reg_stride = config->reg_stride;
	gpio->reg_mask_xlate = config->reg_mask_xlate;
	gpio->reg_dat_base = config->reg_dat_base;
	gpio->reg_set_base = config->reg_set_base;
	gpio->reg_clr_base = config->reg_clr_base;
	gpio->reg_dir_in_base = config->reg_dir_in_base;
	gpio->reg_dir_out_base = config->reg_dir_out_base;

	/* if not set, assume there is only one register */
	if (!gpio->ngpio_per_reg)
		gpio->ngpio_per_reg = config->ngpio;

	/* if not set, assume they are consecutive */
	if (!gpio->reg_stride)
		gpio->reg_stride = 1;

	if (!gpio->reg_mask_xlate)
		gpio->reg_mask_xlate = gpio_regmap_simple_xlate;

	chip = &gpio->gpio_chip;
	chip->parent = config->parent;
	chip->fwnode = config->fwnode;
	chip->base = -1;
	chip->ngpio = config->ngpio;
	chip->names = config->names;
	chip->label = config->label ?: dev_name(config->parent);

	/*
	 * If our regmap is fast_io we should probably set can_sleep to false.
	 * Right now, the regmap doesn't save this property, nor is there any
	 * access function for it.
	 * The only regmap type which uses fast_io is regmap-mmio. For now,
	 * assume a safe default of true here.
	 */
	chip->can_sleep = true;

	chip->get = gpio_regmap_get;
	if (gpio->reg_set_base && gpio->reg_clr_base)
		chip->set = gpio_regmap_set_with_clear;
	else if (gpio->reg_set_base)
		chip->set = gpio_regmap_set;

	if (gpio->reg_dir_in_base || gpio->reg_dir_out_base) {
		chip->get_direction = gpio_regmap_get_direction;
		chip->direction_input = gpio_regmap_direction_input;
		chip->direction_output = gpio_regmap_direction_output;
	}

	ret = gpiochip_add_data(chip, gpio);
	if (ret < 0)
		goto err_free_gpio;

	if (config->irq_domain) {
		ret = gpiochip_irqchip_add_domain(chip, config->irq_domain);
		if (ret)
			goto err_remove_gpiochip;
	}

	return gpio;

err_remove_gpiochip:
	gpiochip_remove(chip);
err_free_gpio:
	kfree(gpio);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(gpio_regmap_register);

/**
 * gpio_regmap_unregister() - Unregister a generic regmap GPIO controller
 * @gpio: gpio_regmap device to unregister
 */
void gpio_regmap_unregister(struct gpio_regmap *gpio)
{
	gpiochip_remove(&gpio->gpio_chip);
	kfree(gpio);
}
EXPORT_SYMBOL_GPL(gpio_regmap_unregister);

static void devm_gpio_regmap_unregister(void *res)
{
	gpio_regmap_unregister(res);
}

/**
 * devm_gpio_regmap_register() - resource managed gpio_regmap_register()
 * @dev: device that is registering this GPIO device
 * @config: configuration for gpio_regmap
 *
 * Managed gpio_regmap_register(). For generic regmap GPIO device registered by
 * this function, gpio_regmap_unregister() is automatically called on driver
 * detach. See gpio_regmap_register() for more information.
 *
 * Return: A pointer to the registered gpio_regmap or ERR_PTR error value.
 */
struct gpio_regmap *devm_gpio_regmap_register(struct device *dev,
					      const struct gpio_regmap_config *config)
{
	struct gpio_regmap *gpio;
	int ret;

	gpio = gpio_regmap_register(config);

	if (IS_ERR(gpio))
		return gpio;

	ret = devm_add_action_or_reset(dev, devm_gpio_regmap_unregister, gpio);
	if (ret)
		return ERR_PTR(ret);

	return gpio;
}
EXPORT_SYMBOL_GPL(devm_gpio_regmap_register);

MODULE_AUTHOR("Michael Walle <michael@walle.cc>");
MODULE_DESCRIPTION("GPIO generic regmap driver core");
MODULE_LICENSE("GPL");
