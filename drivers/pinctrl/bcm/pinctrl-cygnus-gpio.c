/*
 * Copyright (C) 2014-2015 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * This file contains the Broadcom Cygnus GPIO driver that supports 3
 * GPIO controllers on Cygnus including the ASIU GPIO controller, the
 * chipCommonG GPIO controller, and the always-on GPIO controller. Basic
 * PINCONF such as bias pull up/down, and drive strength are also supported
 * in this driver.
 *
 * Pins from the ASIU GPIO can be individually muxed to GPIO function,
 * through the interaction with the Cygnus IOMUX controller
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/ioport.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>

#include "../pinctrl-utils.h"

#define CYGNUS_GPIO_DATA_IN_OFFSET   0x00
#define CYGNUS_GPIO_DATA_OUT_OFFSET  0x04
#define CYGNUS_GPIO_OUT_EN_OFFSET    0x08
#define CYGNUS_GPIO_IN_TYPE_OFFSET   0x0c
#define CYGNUS_GPIO_INT_DE_OFFSET    0x10
#define CYGNUS_GPIO_INT_EDGE_OFFSET  0x14
#define CYGNUS_GPIO_INT_MSK_OFFSET   0x18
#define CYGNUS_GPIO_INT_STAT_OFFSET  0x1c
#define CYGNUS_GPIO_INT_MSTAT_OFFSET 0x20
#define CYGNUS_GPIO_INT_CLR_OFFSET   0x24
#define CYGNUS_GPIO_PAD_RES_OFFSET   0x34
#define CYGNUS_GPIO_RES_EN_OFFSET    0x38

/* drive strength control for ASIU GPIO */
#define CYGNUS_GPIO_ASIU_DRV0_CTRL_OFFSET 0x58

/* drive strength control for CCM/CRMU (AON) GPIO */
#define CYGNUS_GPIO_DRV0_CTRL_OFFSET  0x00

#define GPIO_BANK_SIZE 0x200
#define NGPIOS_PER_BANK 32
#define GPIO_BANK(pin) ((pin) / NGPIOS_PER_BANK)

#define CYGNUS_GPIO_REG(pin, reg) (GPIO_BANK(pin) * GPIO_BANK_SIZE + (reg))
#define CYGNUS_GPIO_SHIFT(pin) ((pin) % NGPIOS_PER_BANK)

#define GPIO_DRV_STRENGTH_BIT_SHIFT  20
#define GPIO_DRV_STRENGTH_BITS       3
#define GPIO_DRV_STRENGTH_BIT_MASK   ((1 << GPIO_DRV_STRENGTH_BITS) - 1)

/*
 * Cygnus GPIO core
 *
 * @dev: pointer to device
 * @base: I/O register base for Cygnus GPIO controller
 * @io_ctrl: I/O register base for certain type of Cygnus GPIO controller that
 * has the PINCONF support implemented outside of the GPIO block
 * @lock: lock to protect access to I/O registers
 * @gc: GPIO chip
 * @num_banks: number of GPIO banks, each bank supports up to 32 GPIOs
 * @pinmux_is_supported: flag to indicate this GPIO controller contains pins
 * that can be individually muxed to GPIO
 * @pctl: pointer to pinctrl_dev
 * @pctldesc: pinctrl descriptor
 */
struct cygnus_gpio {
	struct device *dev;

	void __iomem *base;
	void __iomem *io_ctrl;

	spinlock_t lock;

	struct gpio_chip gc;
	unsigned num_banks;

	bool pinmux_is_supported;

	struct pinctrl_dev *pctl;
	struct pinctrl_desc pctldesc;
};

static inline struct cygnus_gpio *to_cygnus_gpio(struct gpio_chip *gc)
{
	return container_of(gc, struct cygnus_gpio, gc);
}

/*
 * Mapping from PINCONF pins to GPIO pins is 1-to-1
 */
static inline unsigned cygnus_pin_to_gpio(unsigned pin)
{
	return pin;
}

/**
 *  cygnus_set_bit - set or clear one bit (corresponding to the GPIO pin) in a
 *  Cygnus GPIO register
 *
 *  @cygnus_gpio: Cygnus GPIO device
 *  @reg: register offset
 *  @gpio: GPIO pin
 *  @set: set or clear
 */
static inline void cygnus_set_bit(struct cygnus_gpio *chip, unsigned int reg,
				  unsigned gpio, bool set)
{
	unsigned int offset = CYGNUS_GPIO_REG(gpio, reg);
	unsigned int shift = CYGNUS_GPIO_SHIFT(gpio);
	u32 val;

	val = readl(chip->base + offset);
	if (set)
		val |= BIT(shift);
	else
		val &= ~BIT(shift);
	writel(val, chip->base + offset);
}

static inline bool cygnus_get_bit(struct cygnus_gpio *chip, unsigned int reg,
				  unsigned gpio)
{
	unsigned int offset = CYGNUS_GPIO_REG(gpio, reg);
	unsigned int shift = CYGNUS_GPIO_SHIFT(gpio);

	return !!(readl(chip->base + offset) & BIT(shift));
}

static void cygnus_gpio_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	struct gpio_chip *gc = irq_desc_get_handler_data(desc);
	struct cygnus_gpio *chip = to_cygnus_gpio(gc);
	struct irq_chip *irq_chip = irq_desc_get_chip(desc);
	int i, bit;

	chained_irq_enter(irq_chip, desc);

	/* go through the entire GPIO banks and handle all interrupts */
	for (i = 0; i < chip->num_banks; i++) {
		unsigned long val = readl(chip->base + (i * GPIO_BANK_SIZE) +
					  CYGNUS_GPIO_INT_MSTAT_OFFSET);

		for_each_set_bit(bit, &val, NGPIOS_PER_BANK) {
			unsigned pin = NGPIOS_PER_BANK * i + bit;
			int child_irq = irq_find_mapping(gc->irqdomain, pin);

			/*
			 * Clear the interrupt before invoking the
			 * handler, so we do not leave any window
			 */
			writel(BIT(bit), chip->base + (i * GPIO_BANK_SIZE) +
			       CYGNUS_GPIO_INT_CLR_OFFSET);

			generic_handle_irq(child_irq);
		}
	}

	chained_irq_exit(irq_chip, desc);
}


static void cygnus_gpio_irq_ack(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct cygnus_gpio *chip = to_cygnus_gpio(gc);
	unsigned gpio = d->hwirq;
	unsigned int offset = CYGNUS_GPIO_REG(gpio,
			CYGNUS_GPIO_INT_CLR_OFFSET);
	unsigned int shift = CYGNUS_GPIO_SHIFT(gpio);
	u32 val = BIT(shift);

	writel(val, chip->base + offset);
}

/**
 *  cygnus_gpio_irq_set_mask - mask/unmask a GPIO interrupt
 *
 *  @d: IRQ chip data
 *  @unmask: mask/unmask GPIO interrupt
 */
static void cygnus_gpio_irq_set_mask(struct irq_data *d, bool unmask)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct cygnus_gpio *chip = to_cygnus_gpio(gc);
	unsigned gpio = d->hwirq;

	cygnus_set_bit(chip, CYGNUS_GPIO_INT_MSK_OFFSET, gpio, unmask);
}

static void cygnus_gpio_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct cygnus_gpio *chip = to_cygnus_gpio(gc);
	unsigned long flags;

	spin_lock_irqsave(&chip->lock, flags);
	cygnus_gpio_irq_set_mask(d, false);
	spin_unlock_irqrestore(&chip->lock, flags);
}

static void cygnus_gpio_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct cygnus_gpio *chip = to_cygnus_gpio(gc);
	unsigned long flags;

	spin_lock_irqsave(&chip->lock, flags);
	cygnus_gpio_irq_set_mask(d, true);
	spin_unlock_irqrestore(&chip->lock, flags);
}

static int cygnus_gpio_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct cygnus_gpio *chip = to_cygnus_gpio(gc);
	unsigned gpio = d->hwirq;
	bool level_triggered = false;
	bool dual_edge = false;
	bool rising_or_high = false;
	unsigned long flags;

	switch (type & IRQ_TYPE_SENSE_MASK) {
	case IRQ_TYPE_EDGE_RISING:
		rising_or_high = true;
		break;

	case IRQ_TYPE_EDGE_FALLING:
		break;

	case IRQ_TYPE_EDGE_BOTH:
		dual_edge = true;
		break;

	case IRQ_TYPE_LEVEL_HIGH:
		level_triggered = true;
		rising_or_high = true;
		break;

	case IRQ_TYPE_LEVEL_LOW:
		level_triggered = true;
		break;

	default:
		dev_err(chip->dev, "invalid GPIO IRQ type 0x%x\n",
			type);
		return -EINVAL;
	}

	spin_lock_irqsave(&chip->lock, flags);
	cygnus_set_bit(chip, CYGNUS_GPIO_IN_TYPE_OFFSET, gpio,
		       level_triggered);
	cygnus_set_bit(chip, CYGNUS_GPIO_INT_DE_OFFSET, gpio, dual_edge);
	cygnus_set_bit(chip, CYGNUS_GPIO_INT_EDGE_OFFSET, gpio,
		       rising_or_high);
	spin_unlock_irqrestore(&chip->lock, flags);

	dev_dbg(chip->dev,
		"gpio:%u level_triggered:%d dual_edge:%d rising_or_high:%d\n",
		gpio, level_triggered, dual_edge, rising_or_high);

	return 0;
}

static struct irq_chip cygnus_gpio_irq_chip = {
	.name = "bcm-cygnus-gpio",
	.irq_ack = cygnus_gpio_irq_ack,
	.irq_mask = cygnus_gpio_irq_mask,
	.irq_unmask = cygnus_gpio_irq_unmask,
	.irq_set_type = cygnus_gpio_irq_set_type,
};

/*
 * Request the Cygnus IOMUX pinmux controller to mux individual pins to GPIO
 */
static int cygnus_gpio_request(struct gpio_chip *gc, unsigned offset)
{
	struct cygnus_gpio *chip = to_cygnus_gpio(gc);
	unsigned gpio = gc->base + offset;

	/* not all Cygnus GPIO pins can be muxed individually */
	if (!chip->pinmux_is_supported)
		return 0;

	return pinctrl_request_gpio(gpio);
}

static void cygnus_gpio_free(struct gpio_chip *gc, unsigned offset)
{
	struct cygnus_gpio *chip = to_cygnus_gpio(gc);
	unsigned gpio = gc->base + offset;

	if (!chip->pinmux_is_supported)
		return;

	pinctrl_free_gpio(gpio);
}

static int cygnus_gpio_direction_input(struct gpio_chip *gc, unsigned gpio)
{
	struct cygnus_gpio *chip = to_cygnus_gpio(gc);
	unsigned long flags;

	spin_lock_irqsave(&chip->lock, flags);
	cygnus_set_bit(chip, CYGNUS_GPIO_OUT_EN_OFFSET, gpio, false);
	spin_unlock_irqrestore(&chip->lock, flags);

	dev_dbg(chip->dev, "gpio:%u set input\n", gpio);

	return 0;
}

static int cygnus_gpio_direction_output(struct gpio_chip *gc, unsigned gpio,
					int val)
{
	struct cygnus_gpio *chip = to_cygnus_gpio(gc);
	unsigned long flags;

	spin_lock_irqsave(&chip->lock, flags);
	cygnus_set_bit(chip, CYGNUS_GPIO_OUT_EN_OFFSET, gpio, true);
	cygnus_set_bit(chip, CYGNUS_GPIO_DATA_OUT_OFFSET, gpio, !!(val));
	spin_unlock_irqrestore(&chip->lock, flags);

	dev_dbg(chip->dev, "gpio:%u set output, value:%d\n", gpio, val);

	return 0;
}

static void cygnus_gpio_set(struct gpio_chip *gc, unsigned gpio, int val)
{
	struct cygnus_gpio *chip = to_cygnus_gpio(gc);
	unsigned long flags;

	spin_lock_irqsave(&chip->lock, flags);
	cygnus_set_bit(chip, CYGNUS_GPIO_DATA_OUT_OFFSET, gpio, !!(val));
	spin_unlock_irqrestore(&chip->lock, flags);

	dev_dbg(chip->dev, "gpio:%u set, value:%d\n", gpio, val);
}

static int cygnus_gpio_get(struct gpio_chip *gc, unsigned gpio)
{
	struct cygnus_gpio *chip = to_cygnus_gpio(gc);
	unsigned int offset = CYGNUS_GPIO_REG(gpio,
					      CYGNUS_GPIO_DATA_IN_OFFSET);
	unsigned int shift = CYGNUS_GPIO_SHIFT(gpio);

	return !!(readl(chip->base + offset) & BIT(shift));
}

static int cygnus_get_groups_count(struct pinctrl_dev *pctldev)
{
	return 1;
}

/*
 * Only one group: "gpio_grp", since this local pinctrl device only performs
 * GPIO specific PINCONF configurations
 */
static const char *cygnus_get_group_name(struct pinctrl_dev *pctldev,
					 unsigned selector)
{
	return "gpio_grp";
}

static const struct pinctrl_ops cygnus_pctrl_ops = {
	.get_groups_count = cygnus_get_groups_count,
	.get_group_name = cygnus_get_group_name,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_pin,
	.dt_free_map = pinctrl_utils_dt_free_map,
};

static int cygnus_gpio_set_pull(struct cygnus_gpio *chip, unsigned gpio,
				bool disable, bool pull_up)
{
	unsigned long flags;

	spin_lock_irqsave(&chip->lock, flags);

	if (disable) {
		cygnus_set_bit(chip, CYGNUS_GPIO_RES_EN_OFFSET, gpio, false);
	} else {
		cygnus_set_bit(chip, CYGNUS_GPIO_PAD_RES_OFFSET, gpio,
			       pull_up);
		cygnus_set_bit(chip, CYGNUS_GPIO_RES_EN_OFFSET, gpio, true);
	}

	spin_unlock_irqrestore(&chip->lock, flags);

	dev_dbg(chip->dev, "gpio:%u set pullup:%d\n", gpio, pull_up);

	return 0;
}

static void cygnus_gpio_get_pull(struct cygnus_gpio *chip, unsigned gpio,
				 bool *disable, bool *pull_up)
{
	unsigned long flags;

	spin_lock_irqsave(&chip->lock, flags);
	*disable = !cygnus_get_bit(chip, CYGNUS_GPIO_RES_EN_OFFSET, gpio);
	*pull_up = cygnus_get_bit(chip, CYGNUS_GPIO_PAD_RES_OFFSET, gpio);
	spin_unlock_irqrestore(&chip->lock, flags);
}

static int cygnus_gpio_set_strength(struct cygnus_gpio *chip, unsigned gpio,
				    unsigned strength)
{
	void __iomem *base;
	unsigned int i, offset, shift;
	u32 val;
	unsigned long flags;

	/* make sure drive strength is supported */
	if (strength < 2 ||  strength > 16 || (strength % 2))
		return -ENOTSUPP;

	if (chip->io_ctrl) {
		base = chip->io_ctrl;
		offset = CYGNUS_GPIO_DRV0_CTRL_OFFSET;
	} else {
		base = chip->base;
		offset = CYGNUS_GPIO_REG(gpio,
					 CYGNUS_GPIO_ASIU_DRV0_CTRL_OFFSET);
	}

	shift = CYGNUS_GPIO_SHIFT(gpio);

	dev_dbg(chip->dev, "gpio:%u set drive strength:%d mA\n", gpio,
		strength);

	spin_lock_irqsave(&chip->lock, flags);
	strength = (strength / 2) - 1;
	for (i = 0; i < GPIO_DRV_STRENGTH_BITS; i++) {
		val = readl(base + offset);
		val &= ~BIT(shift);
		val |= ((strength >> i) & 0x1) << shift;
		writel(val, base + offset);
		offset += 4;
	}
	spin_unlock_irqrestore(&chip->lock, flags);

	return 0;
}

static int cygnus_gpio_get_strength(struct cygnus_gpio *chip, unsigned gpio,
				    u16 *strength)
{
	void __iomem *base;
	unsigned int i, offset, shift;
	u32 val;
	unsigned long flags;

	if (chip->io_ctrl) {
		base = chip->io_ctrl;
		offset = CYGNUS_GPIO_DRV0_CTRL_OFFSET;
	} else {
		base = chip->base;
		offset = CYGNUS_GPIO_REG(gpio,
					 CYGNUS_GPIO_ASIU_DRV0_CTRL_OFFSET);
	}

	shift = CYGNUS_GPIO_SHIFT(gpio);

	spin_lock_irqsave(&chip->lock, flags);
	*strength = 0;
	for (i = 0; i < GPIO_DRV_STRENGTH_BITS; i++) {
		val = readl(base + offset) & BIT(shift);
		val >>= shift;
		*strength += (val << i);
		offset += 4;
	}

	/* convert to mA */
	*strength = (*strength + 1) * 2;
	spin_unlock_irqrestore(&chip->lock, flags);

	return 0;
}

static int cygnus_pin_config_get(struct pinctrl_dev *pctldev, unsigned pin,
				 unsigned long *config)
{
	struct cygnus_gpio *chip = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param = pinconf_to_config_param(*config);
	unsigned gpio = cygnus_pin_to_gpio(pin);
	u16 arg;
	bool disable, pull_up;
	int ret;

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		cygnus_gpio_get_pull(chip, gpio, &disable, &pull_up);
		if (disable)
			return 0;
		else
			return -EINVAL;

	case PIN_CONFIG_BIAS_PULL_UP:
		cygnus_gpio_get_pull(chip, gpio, &disable, &pull_up);
		if (!disable && pull_up)
			return 0;
		else
			return -EINVAL;

	case PIN_CONFIG_BIAS_PULL_DOWN:
		cygnus_gpio_get_pull(chip, gpio, &disable, &pull_up);
		if (!disable && !pull_up)
			return 0;
		else
			return -EINVAL;

	case PIN_CONFIG_DRIVE_STRENGTH:
		ret = cygnus_gpio_get_strength(chip, gpio, &arg);
		if (ret)
			return ret;
		else
			*config = pinconf_to_config_packed(param, arg);

		return 0;

	default:
		return -ENOTSUPP;
	}

	return -ENOTSUPP;
}

static int cygnus_pin_config_set(struct pinctrl_dev *pctldev, unsigned pin,
				 unsigned long *configs, unsigned num_configs)
{
	struct cygnus_gpio *chip = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param;
	u16 arg;
	unsigned i, gpio = cygnus_pin_to_gpio(pin);
	int ret = -ENOTSUPP;

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_BIAS_DISABLE:
			ret = cygnus_gpio_set_pull(chip, gpio, true, false);
			if (ret < 0)
				goto out;
			break;

		case PIN_CONFIG_BIAS_PULL_UP:
			ret = cygnus_gpio_set_pull(chip, gpio, false, true);
			if (ret < 0)
				goto out;
			break;

		case PIN_CONFIG_BIAS_PULL_DOWN:
			ret = cygnus_gpio_set_pull(chip, gpio, false, false);
			if (ret < 0)
				goto out;
			break;

		case PIN_CONFIG_DRIVE_STRENGTH:
			ret = cygnus_gpio_set_strength(chip, gpio, arg);
			if (ret < 0)
				goto out;
			break;

		default:
			dev_err(chip->dev, "invalid configuration\n");
			return -ENOTSUPP;
		}
	} /* for each config */

out:
	return ret;
}

static const struct pinconf_ops cygnus_pconf_ops = {
	.is_generic = true,
	.pin_config_get = cygnus_pin_config_get,
	.pin_config_set = cygnus_pin_config_set,
};

/*
 * Map a GPIO in the local gpio_chip pin space to a pin in the Cygnus IOMUX
 * pinctrl pin space
 */
struct cygnus_gpio_pin_range {
	unsigned offset;
	unsigned pin_base;
	unsigned num_pins;
};

#define CYGNUS_PINRANGE(o, p, n) { .offset = o, .pin_base = p, .num_pins = n }

/*
 * Pin mapping table for mapping local GPIO pins to Cygnus IOMUX pinctrl pins
 */
static const struct cygnus_gpio_pin_range cygnus_gpio_pintable[] = {
	CYGNUS_PINRANGE(0, 42, 1),
	CYGNUS_PINRANGE(1, 44, 3),
	CYGNUS_PINRANGE(4, 48, 1),
	CYGNUS_PINRANGE(5, 50, 3),
	CYGNUS_PINRANGE(8, 126, 1),
	CYGNUS_PINRANGE(9, 155, 1),
	CYGNUS_PINRANGE(10, 152, 1),
	CYGNUS_PINRANGE(11, 154, 1),
	CYGNUS_PINRANGE(12, 153, 1),
	CYGNUS_PINRANGE(13, 127, 3),
	CYGNUS_PINRANGE(16, 140, 1),
	CYGNUS_PINRANGE(17, 145, 7),
	CYGNUS_PINRANGE(24, 130, 10),
	CYGNUS_PINRANGE(34, 141, 4),
	CYGNUS_PINRANGE(38, 54, 1),
	CYGNUS_PINRANGE(39, 56, 3),
	CYGNUS_PINRANGE(42, 60, 3),
	CYGNUS_PINRANGE(45, 64, 3),
	CYGNUS_PINRANGE(48, 68, 2),
	CYGNUS_PINRANGE(50, 84, 6),
	CYGNUS_PINRANGE(56, 94, 6),
	CYGNUS_PINRANGE(62, 72, 1),
	CYGNUS_PINRANGE(63, 70, 1),
	CYGNUS_PINRANGE(64, 80, 1),
	CYGNUS_PINRANGE(65, 74, 3),
	CYGNUS_PINRANGE(68, 78, 1),
	CYGNUS_PINRANGE(69, 82, 1),
	CYGNUS_PINRANGE(70, 156, 17),
	CYGNUS_PINRANGE(87, 104, 12),
	CYGNUS_PINRANGE(99, 102, 2),
	CYGNUS_PINRANGE(101, 90, 4),
	CYGNUS_PINRANGE(105, 116, 10),
	CYGNUS_PINRANGE(123, 11, 1),
	CYGNUS_PINRANGE(124, 38, 4),
	CYGNUS_PINRANGE(128, 43, 1),
	CYGNUS_PINRANGE(129, 47, 1),
	CYGNUS_PINRANGE(130, 49, 1),
	CYGNUS_PINRANGE(131, 53, 1),
	CYGNUS_PINRANGE(132, 55, 1),
	CYGNUS_PINRANGE(133, 59, 1),
	CYGNUS_PINRANGE(134, 63, 1),
	CYGNUS_PINRANGE(135, 67, 1),
	CYGNUS_PINRANGE(136, 71, 1),
	CYGNUS_PINRANGE(137, 73, 1),
	CYGNUS_PINRANGE(138, 77, 1),
	CYGNUS_PINRANGE(139, 79, 1),
	CYGNUS_PINRANGE(140, 81, 1),
	CYGNUS_PINRANGE(141, 83, 1),
	CYGNUS_PINRANGE(142, 10, 1)
};

/*
 * The Cygnus IOMUX controller mainly supports group based mux configuration,
 * but certain pins can be muxed to GPIO individually. Only the ASIU GPIO
 * controller can support this, so it's an optional configuration
 *
 * Return -ENODEV means no support and that's fine
 */
static int cygnus_gpio_pinmux_add_range(struct cygnus_gpio *chip)
{
	struct device_node *node = chip->dev->of_node;
	struct device_node *pinmux_node;
	struct platform_device *pinmux_pdev;
	struct gpio_chip *gc = &chip->gc;
	int i, ret = 0;

	/* parse DT to find the phandle to the pinmux controller */
	pinmux_node = of_parse_phandle(node, "pinmux", 0);
	if (!pinmux_node)
		return -ENODEV;

	pinmux_pdev = of_find_device_by_node(pinmux_node);
	/* no longer need the pinmux node */
	of_node_put(pinmux_node);
	if (!pinmux_pdev) {
		dev_err(chip->dev, "failed to get pinmux device\n");
		return -EINVAL;
	}

	/* now need to create the mapping between local GPIO and PINMUX pins */
	for (i = 0; i < ARRAY_SIZE(cygnus_gpio_pintable); i++) {
		ret = gpiochip_add_pin_range(gc, dev_name(&pinmux_pdev->dev),
					     cygnus_gpio_pintable[i].offset,
					     cygnus_gpio_pintable[i].pin_base,
					     cygnus_gpio_pintable[i].num_pins);
		if (ret) {
			dev_err(chip->dev, "unable to add GPIO pin range\n");
			goto err_put_device;
		}
	}

	chip->pinmux_is_supported = true;

	/* no need for pinmux_pdev device reference anymore */
	put_device(&pinmux_pdev->dev);
	return 0;

err_put_device:
	put_device(&pinmux_pdev->dev);
	gpiochip_remove_pin_ranges(gc);
	return ret;
}

/*
 * Cygnus GPIO controller supports some PINCONF related configurations such as
 * pull up, pull down, and drive strength, when the pin is configured to GPIO
 *
 * Here a local pinctrl device is created with simple 1-to-1 pin mapping to the
 * local GPIO pins
 */
static int cygnus_gpio_register_pinconf(struct cygnus_gpio *chip)
{
	struct pinctrl_desc *pctldesc = &chip->pctldesc;
	struct pinctrl_pin_desc *pins;
	struct gpio_chip *gc = &chip->gc;
	int i;

	pins = devm_kcalloc(chip->dev, gc->ngpio, sizeof(*pins), GFP_KERNEL);
	if (!pins)
		return -ENOMEM;

	for (i = 0; i < gc->ngpio; i++) {
		pins[i].number = i;
		pins[i].name = devm_kasprintf(chip->dev, GFP_KERNEL,
					      "gpio-%d", i);
		if (!pins[i].name)
			return -ENOMEM;
	}

	pctldesc->name = dev_name(chip->dev);
	pctldesc->pctlops = &cygnus_pctrl_ops;
	pctldesc->pins = pins;
	pctldesc->npins = gc->ngpio;
	pctldesc->confops = &cygnus_pconf_ops;

	chip->pctl = pinctrl_register(pctldesc, chip->dev, chip);
	if (!chip->pctl) {
		dev_err(chip->dev, "unable to register pinctrl device\n");
		return -EINVAL;
	}

	return 0;
}

static void cygnus_gpio_unregister_pinconf(struct cygnus_gpio *chip)
{
	if (chip->pctl)
		pinctrl_unregister(chip->pctl);
}

struct cygnus_gpio_data {
	unsigned num_gpios;
};

static const struct cygnus_gpio_data cygnus_cmm_gpio_data = {
	.num_gpios = 24,
};

static const struct cygnus_gpio_data cygnus_asiu_gpio_data = {
	.num_gpios = 146,
};

static const struct cygnus_gpio_data cygnus_crmu_gpio_data = {
	.num_gpios = 6,
};

static const struct of_device_id cygnus_gpio_of_match[] = {
	{
		.compatible = "brcm,cygnus-ccm-gpio",
		.data = &cygnus_cmm_gpio_data,
	},
	{
		.compatible = "brcm,cygnus-asiu-gpio",
		.data = &cygnus_asiu_gpio_data,
	},
	{
		.compatible = "brcm,cygnus-crmu-gpio",
		.data = &cygnus_crmu_gpio_data,
	}
};

static int cygnus_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct cygnus_gpio *chip;
	struct gpio_chip *gc;
	u32 ngpios;
	int irq, ret;
	const struct of_device_id *match;
	const struct cygnus_gpio_data *gpio_data;

	match = of_match_device(cygnus_gpio_of_match, dev);
	if (!match)
		return -ENODEV;
	gpio_data = match->data;
	ngpios = gpio_data->num_gpios;

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = dev;
	platform_set_drvdata(pdev, chip);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	chip->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(chip->base)) {
		dev_err(dev, "unable to map I/O memory\n");
		return PTR_ERR(chip->base);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (res) {
		chip->io_ctrl = devm_ioremap_resource(dev, res);
		if (IS_ERR(chip->io_ctrl)) {
			dev_err(dev, "unable to map I/O memory\n");
			return PTR_ERR(chip->io_ctrl);
		}
	}

	spin_lock_init(&chip->lock);

	gc = &chip->gc;
	gc->base = -1;
	gc->ngpio = ngpios;
	chip->num_banks = (ngpios + NGPIOS_PER_BANK - 1) / NGPIOS_PER_BANK;
	gc->label = dev_name(dev);
	gc->dev = dev;
	gc->of_node = dev->of_node;
	gc->request = cygnus_gpio_request;
	gc->free = cygnus_gpio_free;
	gc->direction_input = cygnus_gpio_direction_input;
	gc->direction_output = cygnus_gpio_direction_output;
	gc->set = cygnus_gpio_set;
	gc->get = cygnus_gpio_get;

	ret = gpiochip_add(gc);
	if (ret < 0) {
		dev_err(dev, "unable to add GPIO chip\n");
		return ret;
	}

	ret = cygnus_gpio_pinmux_add_range(chip);
	if (ret && ret != -ENODEV) {
		dev_err(dev, "unable to add GPIO pin range\n");
		goto err_rm_gpiochip;
	}

	ret = cygnus_gpio_register_pinconf(chip);
	if (ret) {
		dev_err(dev, "unable to register pinconf\n");
		goto err_rm_gpiochip;
	}

	/* optional GPIO interrupt support */
	irq = platform_get_irq(pdev, 0);
	if (irq) {
		ret = gpiochip_irqchip_add(gc, &cygnus_gpio_irq_chip, 0,
					   handle_simple_irq, IRQ_TYPE_NONE);
		if (ret) {
			dev_err(dev, "no GPIO irqchip\n");
			goto err_unregister_pinconf;
		}

		gpiochip_set_chained_irqchip(gc, &cygnus_gpio_irq_chip, irq,
					     cygnus_gpio_irq_handler);
	}

	return 0;

err_unregister_pinconf:
	cygnus_gpio_unregister_pinconf(chip);

err_rm_gpiochip:
	gpiochip_remove(gc);

	return ret;
}

static struct platform_driver cygnus_gpio_driver = {
	.driver = {
		.name = "cygnus-gpio",
		.of_match_table = cygnus_gpio_of_match,
	},
	.probe = cygnus_gpio_probe,
};

static int __init cygnus_gpio_init(void)
{
	return platform_driver_probe(&cygnus_gpio_driver, cygnus_gpio_probe);
}
arch_initcall_sync(cygnus_gpio_init);
