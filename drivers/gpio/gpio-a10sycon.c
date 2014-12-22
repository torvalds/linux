/*
 *  Copyright (C) 2014 Altera Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * GPIO driver for Altera MAX5 Arria10 System Control
 * Adapted from DA9052
 */

#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/mfd/a10sycon.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>

struct a10sycon_gpio {
	struct a10sycon *a10sc;
	struct gpio_chip gp;
};

static inline struct a10sycon_gpio *to_a10sycon_gpio(struct gpio_chip *chip)
{
	return container_of(chip, struct a10sycon_gpio, gp);
}

static int a10sycon_gpio_get(struct gpio_chip *gc, unsigned nr)
{
	struct a10sycon_gpio *gpio = to_a10sycon_gpio(gc);
	int ret;

	ret = a10sycon_reg_read(gpio->a10sc,
				A10SYCON_REG_OFFSET(nr >> 1));
	if (ret < 0)
		return ret;

	if (ret & (1 << A10SYCON_REG_BIT(nr)))
		return 1;

	return 0;
}

static void a10sycon_gpio_set(struct gpio_chip *gc, unsigned nr, int value)
{
	struct a10sycon_gpio *gpio = to_a10sycon_gpio(gc);
	int ret;
	unsigned char reg = A10SYCON_LED_WR_REG + A10SYCON_REG_OFFSET(nr);

	ret = a10sycon_reg_update(gpio->a10sc, reg,
				  A10SYCON_REG_BIT_MASK(nr),
				  A10SYCON_REG_BIT_CHG(value, nr));
	if (ret != 0)
		dev_err(gpio->a10sc->dev,
			"Failed to update gpio reg : %d", ret);
}

static int a10sycon_gpio_direction_input(struct gpio_chip *gc, unsigned nr)
{
	if ((nr >= A10SC_IN_VALID_RANGE_LO) &&
	    (nr <= A10SC_IN_VALID_RANGE_HI))
		return 0;
	return -EINVAL;
}

static int a10sycon_gpio_direction_output(struct gpio_chip *gc,
					  unsigned nr, int value)
{
	if ((nr >= A10SC_OUT_VALID_RANGE_LO) &&
	    (nr <= A10SC_OUT_VALID_RANGE_HI))
		return 0;
	return -EINVAL;
}

static int a10sycon_gpio_to_irq(struct gpio_chip *gc, u32 nr)
{
	struct a10sycon_gpio *gpio = to_a10sycon_gpio(gc);

	nr -= A10SC_IN_VALID_RANGE_LO;

	return a10sycon_map_irq(gpio->a10sc, nr);
}

static struct gpio_chip a10sycon_gc = {
	.label = "a10sycon-gpio",
	.owner = THIS_MODULE,
	.get = a10sycon_gpio_get,
	.set = a10sycon_gpio_set,
	.direction_input = a10sycon_gpio_direction_input,
	.direction_output = a10sycon_gpio_direction_output,
	.to_irq =  a10sycon_gpio_to_irq,
	.can_sleep = true,
	.ngpio = 16,
	.base = -1,
};

static const struct of_device_id a10sycon_gpio_of_match[];

static int a10sycon_gpio_probe(struct platform_device *pdev)
{
	struct a10sycon_gpio *gpio;
	int ret;

	gpio = devm_kzalloc(&pdev->dev, sizeof(*gpio), GFP_KERNEL);
	if (gpio == NULL)
		return -ENOMEM;

	gpio->a10sc = dev_get_drvdata(pdev->dev.parent);

	gpio->gp = a10sycon_gc;

	gpio->gp.of_node = pdev->dev.of_node;

	ret = gpiochip_add(&gpio->gp);
	if (ret < 0) {
		dev_err(&pdev->dev, "Could not register gpiochip, %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, gpio);

	return 0;
}

static int a10sycon_gpio_remove(struct platform_device *pdev)
{
	struct a10sycon_gpio *gpio = platform_get_drvdata(pdev);

	return gpiochip_remove(&gpio->gp);
}

static const struct of_device_id a10sycon_gpio_of_match[] = {
	{ .compatible = "altr,a10sycon-gpio" },
	{ },
};
MODULE_DEVICE_TABLE(of, a10sycon_gpio_of_match);

static struct platform_driver a10sycon_gpio_driver = {
	.probe = a10sycon_gpio_probe,
	.remove = a10sycon_gpio_remove,
	.driver = {
		.name	= "a10sycon-gpio",
		.of_match_table = a10sycon_gpio_of_match,
	},
};

module_platform_driver(a10sycon_gpio_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Thor Thayer");
MODULE_DESCRIPTION("Altera Arria10 System Control Chip GPIO");
