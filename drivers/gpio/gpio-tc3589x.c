/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License Terms: GNU General Public License, version 2
 * Author: Hanumath Prasad <hanumath.prasad@stericsson.com> for ST-Ericsson
 * Author: Rabin Vincent <rabin.vincent@stericsson.com> for ST-Ericsson
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/interrupt.h>
#include <linux/mfd/tc3589x.h>

/*
 * These registers are modified under the irq bus lock and cached to avoid
 * unnecessary writes in bus_sync_unlock.
 */
enum { REG_IBE, REG_IEV, REG_IS, REG_IE };

#define CACHE_NR_REGS	4
#define CACHE_NR_BANKS	3

struct tc3589x_gpio {
	struct gpio_chip chip;
	struct tc3589x *tc3589x;
	struct device *dev;
	struct mutex irq_lock;
	/* Caches of interrupt control registers for bus_lock */
	u8 regs[CACHE_NR_REGS][CACHE_NR_BANKS];
	u8 oldregs[CACHE_NR_REGS][CACHE_NR_BANKS];
};

static inline struct tc3589x_gpio *to_tc3589x_gpio(struct gpio_chip *chip)
{
	return container_of(chip, struct tc3589x_gpio, chip);
}

static int tc3589x_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct tc3589x_gpio *tc3589x_gpio = to_tc3589x_gpio(chip);
	struct tc3589x *tc3589x = tc3589x_gpio->tc3589x;
	u8 reg = TC3589x_GPIODATA0 + (offset / 8) * 2;
	u8 mask = 1 << (offset % 8);
	int ret;

	ret = tc3589x_reg_read(tc3589x, reg);
	if (ret < 0)
		return ret;

	return ret & mask;
}

static void tc3589x_gpio_set(struct gpio_chip *chip, unsigned offset, int val)
{
	struct tc3589x_gpio *tc3589x_gpio = to_tc3589x_gpio(chip);
	struct tc3589x *tc3589x = tc3589x_gpio->tc3589x;
	u8 reg = TC3589x_GPIODATA0 + (offset / 8) * 2;
	unsigned pos = offset % 8;
	u8 data[] = {!!val << pos, 1 << pos};

	tc3589x_block_write(tc3589x, reg, ARRAY_SIZE(data), data);
}

static int tc3589x_gpio_direction_output(struct gpio_chip *chip,
					 unsigned offset, int val)
{
	struct tc3589x_gpio *tc3589x_gpio = to_tc3589x_gpio(chip);
	struct tc3589x *tc3589x = tc3589x_gpio->tc3589x;
	u8 reg = TC3589x_GPIODIR0 + offset / 8;
	unsigned pos = offset % 8;

	tc3589x_gpio_set(chip, offset, val);

	return tc3589x_set_bits(tc3589x, reg, 1 << pos, 1 << pos);
}

static int tc3589x_gpio_direction_input(struct gpio_chip *chip,
					unsigned offset)
{
	struct tc3589x_gpio *tc3589x_gpio = to_tc3589x_gpio(chip);
	struct tc3589x *tc3589x = tc3589x_gpio->tc3589x;
	u8 reg = TC3589x_GPIODIR0 + offset / 8;
	unsigned pos = offset % 8;

	return tc3589x_set_bits(tc3589x, reg, 1 << pos, 0);
}

static struct gpio_chip template_chip = {
	.label			= "tc3589x",
	.owner			= THIS_MODULE,
	.direction_input	= tc3589x_gpio_direction_input,
	.get			= tc3589x_gpio_get,
	.direction_output	= tc3589x_gpio_direction_output,
	.set			= tc3589x_gpio_set,
	.can_sleep		= true,
};

static int tc3589x_gpio_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct tc3589x_gpio *tc3589x_gpio = to_tc3589x_gpio(gc);
	int offset = d->hwirq;
	int regoffset = offset / 8;
	int mask = 1 << (offset % 8);

	if (type == IRQ_TYPE_EDGE_BOTH) {
		tc3589x_gpio->regs[REG_IBE][regoffset] |= mask;
		return 0;
	}

	tc3589x_gpio->regs[REG_IBE][regoffset] &= ~mask;

	if (type == IRQ_TYPE_LEVEL_LOW || type == IRQ_TYPE_LEVEL_HIGH)
		tc3589x_gpio->regs[REG_IS][regoffset] |= mask;
	else
		tc3589x_gpio->regs[REG_IS][regoffset] &= ~mask;

	if (type == IRQ_TYPE_EDGE_RISING || type == IRQ_TYPE_LEVEL_HIGH)
		tc3589x_gpio->regs[REG_IEV][regoffset] |= mask;
	else
		tc3589x_gpio->regs[REG_IEV][regoffset] &= ~mask;

	return 0;
}

static void tc3589x_gpio_irq_lock(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct tc3589x_gpio *tc3589x_gpio = to_tc3589x_gpio(gc);

	mutex_lock(&tc3589x_gpio->irq_lock);
}

static void tc3589x_gpio_irq_sync_unlock(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct tc3589x_gpio *tc3589x_gpio = to_tc3589x_gpio(gc);
	struct tc3589x *tc3589x = tc3589x_gpio->tc3589x;
	static const u8 regmap[] = {
		[REG_IBE]	= TC3589x_GPIOIBE0,
		[REG_IEV]	= TC3589x_GPIOIEV0,
		[REG_IS]	= TC3589x_GPIOIS0,
		[REG_IE]	= TC3589x_GPIOIE0,
	};
	int i, j;

	for (i = 0; i < CACHE_NR_REGS; i++) {
		for (j = 0; j < CACHE_NR_BANKS; j++) {
			u8 old = tc3589x_gpio->oldregs[i][j];
			u8 new = tc3589x_gpio->regs[i][j];

			if (new == old)
				continue;

			tc3589x_gpio->oldregs[i][j] = new;
			tc3589x_reg_write(tc3589x, regmap[i] + j * 8, new);
		}
	}

	mutex_unlock(&tc3589x_gpio->irq_lock);
}

static void tc3589x_gpio_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct tc3589x_gpio *tc3589x_gpio = to_tc3589x_gpio(gc);
	int offset = d->hwirq;
	int regoffset = offset / 8;
	int mask = 1 << (offset % 8);

	tc3589x_gpio->regs[REG_IE][regoffset] &= ~mask;
}

static void tc3589x_gpio_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct tc3589x_gpio *tc3589x_gpio = to_tc3589x_gpio(gc);
	int offset = d->hwirq;
	int regoffset = offset / 8;
	int mask = 1 << (offset % 8);

	tc3589x_gpio->regs[REG_IE][regoffset] |= mask;
}

static struct irq_chip tc3589x_gpio_irq_chip = {
	.name			= "tc3589x-gpio",
	.irq_bus_lock		= tc3589x_gpio_irq_lock,
	.irq_bus_sync_unlock	= tc3589x_gpio_irq_sync_unlock,
	.irq_mask		= tc3589x_gpio_irq_mask,
	.irq_unmask		= tc3589x_gpio_irq_unmask,
	.irq_set_type		= tc3589x_gpio_irq_set_type,
};

static irqreturn_t tc3589x_gpio_irq(int irq, void *dev)
{
	struct tc3589x_gpio *tc3589x_gpio = dev;
	struct tc3589x *tc3589x = tc3589x_gpio->tc3589x;
	u8 status[CACHE_NR_BANKS];
	int ret;
	int i;

	ret = tc3589x_block_read(tc3589x, TC3589x_GPIOMIS0,
				 ARRAY_SIZE(status), status);
	if (ret < 0)
		return IRQ_NONE;

	for (i = 0; i < ARRAY_SIZE(status); i++) {
		unsigned int stat = status[i];
		if (!stat)
			continue;

		while (stat) {
			int bit = __ffs(stat);
			int line = i * 8 + bit;
			int irq = irq_find_mapping(tc3589x_gpio->chip.irqdomain,
						   line);

			handle_nested_irq(irq);
			stat &= ~(1 << bit);
		}

		tc3589x_reg_write(tc3589x, TC3589x_GPIOIC0 + i, status[i]);
	}

	return IRQ_HANDLED;
}

static int tc3589x_gpio_probe(struct platform_device *pdev)
{
	struct tc3589x *tc3589x = dev_get_drvdata(pdev->dev.parent);
	struct device_node *np = pdev->dev.of_node;
	struct tc3589x_gpio *tc3589x_gpio;
	int ret;
	int irq;

	if (!np) {
		dev_err(&pdev->dev, "No Device Tree node found\n");
		return -EINVAL;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	tc3589x_gpio = devm_kzalloc(&pdev->dev, sizeof(struct tc3589x_gpio),
				    GFP_KERNEL);
	if (!tc3589x_gpio)
		return -ENOMEM;

	mutex_init(&tc3589x_gpio->irq_lock);

	tc3589x_gpio->dev = &pdev->dev;
	tc3589x_gpio->tc3589x = tc3589x;

	tc3589x_gpio->chip = template_chip;
	tc3589x_gpio->chip.ngpio = tc3589x->num_gpio;
	tc3589x_gpio->chip.dev = &pdev->dev;
	tc3589x_gpio->chip.base = -1;
	tc3589x_gpio->chip.of_node = np;

	/* Bring the GPIO module out of reset */
	ret = tc3589x_set_bits(tc3589x, TC3589x_RSTCTRL,
			       TC3589x_RSTCTRL_GPIRST, 0);
	if (ret < 0)
		return ret;

	ret = devm_request_threaded_irq(&pdev->dev,
					irq, NULL, tc3589x_gpio_irq,
					IRQF_ONESHOT, "tc3589x-gpio",
					tc3589x_gpio);
	if (ret) {
		dev_err(&pdev->dev, "unable to get irq: %d\n", ret);
		return ret;
	}

	ret = gpiochip_add(&tc3589x_gpio->chip);
	if (ret) {
		dev_err(&pdev->dev, "unable to add gpiochip: %d\n", ret);
		return ret;
	}

	ret =  gpiochip_irqchip_add(&tc3589x_gpio->chip,
				    &tc3589x_gpio_irq_chip,
				    0,
				    handle_simple_irq,
				    IRQ_TYPE_NONE);
	if (ret) {
		dev_err(&pdev->dev,
			"could not connect irqchip to gpiochip\n");
		return ret;
	}

	gpiochip_set_chained_irqchip(&tc3589x_gpio->chip,
				     &tc3589x_gpio_irq_chip,
				     irq,
				     NULL);

	platform_set_drvdata(pdev, tc3589x_gpio);

	return 0;
}

static int tc3589x_gpio_remove(struct platform_device *pdev)
{
	struct tc3589x_gpio *tc3589x_gpio = platform_get_drvdata(pdev);

	gpiochip_remove(&tc3589x_gpio->chip);

	return 0;
}

static struct platform_driver tc3589x_gpio_driver = {
	.driver.name	= "tc3589x-gpio",
	.driver.owner	= THIS_MODULE,
	.probe		= tc3589x_gpio_probe,
	.remove		= tc3589x_gpio_remove,
};

static int __init tc3589x_gpio_init(void)
{
	return platform_driver_register(&tc3589x_gpio_driver);
}
subsys_initcall(tc3589x_gpio_init);

static void __exit tc3589x_gpio_exit(void)
{
	platform_driver_unregister(&tc3589x_gpio_driver);
}
module_exit(tc3589x_gpio_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("TC3589x GPIO driver");
MODULE_AUTHOR("Hanumath Prasad, Rabin Vincent");
