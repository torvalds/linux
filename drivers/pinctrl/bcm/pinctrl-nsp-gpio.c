// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2014-2017 Broadcom

/*
 * This file contains the Broadcom Northstar Plus (NSP) GPIO driver that
 * supports the chipCommonA GPIO controller. Basic PINCONF such as bias,
 * pull up/down, slew and drive strength are also supported in this driver.
 *
 * Pins from the chipCommonA  GPIO can be individually muxed to GPIO function,
 * through the interaction with the NSP IOMUX controller.
 */

#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string_choices.h>

#include "../pinctrl-utils.h"

#define NSP_CHIP_A_INT_STATUS		0x00
#define NSP_CHIP_A_INT_MASK		0x04
#define NSP_GPIO_DATA_IN		0x40
#define NSP_GPIO_DATA_OUT		0x44
#define NSP_GPIO_OUT_EN			0x48
#define NSP_GPIO_INT_POLARITY		0x50
#define NSP_GPIO_INT_MASK		0x54
#define NSP_GPIO_EVENT			0x58
#define NSP_GPIO_EVENT_INT_MASK		0x5c
#define NSP_GPIO_EVENT_INT_POLARITY	0x64
#define NSP_CHIP_A_GPIO_INT_BIT		0x01

/* I/O parameters offset for chipcommon A GPIO */
#define NSP_GPIO_DRV_CTRL		0x00
#define NSP_GPIO_HYSTERESIS_EN		0x10
#define NSP_GPIO_SLEW_RATE_EN		0x14
#define NSP_PULL_UP_EN			0x18
#define NSP_PULL_DOWN_EN		0x1c
#define GPIO_DRV_STRENGTH_BITS		0x03

/*
 * nsp GPIO core
 *
 * @dev: pointer to device
 * @base: I/O register base for nsp GPIO controller
 * @io_ctrl: I/O register base for PINCONF support outside the GPIO block
 * @gc: GPIO chip
 * @pctl: pointer to pinctrl_dev
 * @pctldesc: pinctrl descriptor
 * @lock: lock to protect access to I/O registers
 */
struct nsp_gpio {
	struct device *dev;
	void __iomem *base;
	void __iomem *io_ctrl;
	struct gpio_chip gc;
	struct pinctrl_dev *pctl;
	struct pinctrl_desc pctldesc;
	raw_spinlock_t lock;
};

enum base_type {
	REG,
	IO_CTRL
};

/*
 * Mapping from PINCONF pins to GPIO pins is 1-to-1
 */
static inline unsigned nsp_pin_to_gpio(unsigned pin)
{
	return pin;
}

/*
 *  nsp_set_bit - set or clear one bit (corresponding to the GPIO pin) in a
 *  nsp GPIO register
 *
 *  @nsp_gpio: nsp GPIO device
 *  @base_type: reg base to modify
 *  @reg: register offset
 *  @gpio: GPIO pin
 *  @set: set or clear
 */
static inline void nsp_set_bit(struct nsp_gpio *chip, enum base_type address,
			       unsigned int reg, unsigned gpio, bool set)
{
	u32 val;
	void __iomem *base_address;

	if (address == IO_CTRL)
		base_address = chip->io_ctrl;
	else
		base_address = chip->base;

	val = readl(base_address + reg);
	if (set)
		val |= BIT(gpio);
	else
		val &= ~BIT(gpio);

	writel(val, base_address + reg);
}

/*
 *  nsp_get_bit - get one bit (corresponding to the GPIO pin) in a
 *  nsp GPIO register
 */
static inline bool nsp_get_bit(struct nsp_gpio *chip, enum base_type address,
			       unsigned int reg, unsigned gpio)
{
	if (address == IO_CTRL)
		return !!(readl(chip->io_ctrl + reg) & BIT(gpio));
	else
		return !!(readl(chip->base + reg) & BIT(gpio));
}

static irqreturn_t nsp_gpio_irq_handler(int irq, void *data)
{
	struct gpio_chip *gc = (struct gpio_chip *)data;
	struct nsp_gpio *chip = gpiochip_get_data(gc);
	int bit;
	unsigned long int_bits = 0;
	u32 int_status;

	/* go through the entire GPIOs and handle all interrupts */
	int_status = readl(chip->base + NSP_CHIP_A_INT_STATUS);
	if (int_status & NSP_CHIP_A_GPIO_INT_BIT) {
		unsigned int event, level;

		/* Get level and edge interrupts */
		event = readl(chip->base + NSP_GPIO_EVENT_INT_MASK) &
			      readl(chip->base + NSP_GPIO_EVENT);
		level = readl(chip->base + NSP_GPIO_DATA_IN) ^
			      readl(chip->base + NSP_GPIO_INT_POLARITY);
		level &= readl(chip->base + NSP_GPIO_INT_MASK);
		int_bits = level | event;

		for_each_set_bit(bit, &int_bits, gc->ngpio)
			generic_handle_domain_irq(gc->irq.domain, bit);
	}

	return  int_bits ? IRQ_HANDLED : IRQ_NONE;
}

static void nsp_gpio_irq_ack(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct nsp_gpio *chip = gpiochip_get_data(gc);
	unsigned gpio = d->hwirq;
	u32 val = BIT(gpio);
	u32 trigger_type;

	trigger_type = irq_get_trigger_type(d->irq);
	if (trigger_type & (IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_EDGE_RISING))
		writel(val, chip->base + NSP_GPIO_EVENT);
}

/*
 *  nsp_gpio_irq_set_mask - mask/unmask a GPIO interrupt
 *
 *  @d: IRQ chip data
 *  @unmask: mask/unmask GPIO interrupt
 */
static void nsp_gpio_irq_set_mask(struct irq_data *d, bool unmask)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct nsp_gpio *chip = gpiochip_get_data(gc);
	unsigned gpio = d->hwirq;
	u32 trigger_type;

	trigger_type = irq_get_trigger_type(d->irq);
	if (trigger_type & (IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_EDGE_RISING))
		nsp_set_bit(chip, REG, NSP_GPIO_EVENT_INT_MASK, gpio, unmask);
	else
		nsp_set_bit(chip, REG, NSP_GPIO_INT_MASK, gpio, unmask);
}

static void nsp_gpio_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct nsp_gpio *chip = gpiochip_get_data(gc);
	unsigned long flags;

	raw_spin_lock_irqsave(&chip->lock, flags);
	nsp_gpio_irq_set_mask(d, false);
	raw_spin_unlock_irqrestore(&chip->lock, flags);
	gpiochip_disable_irq(gc, irqd_to_hwirq(d));
}

static void nsp_gpio_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct nsp_gpio *chip = gpiochip_get_data(gc);
	unsigned long flags;

	gpiochip_enable_irq(gc, irqd_to_hwirq(d));
	raw_spin_lock_irqsave(&chip->lock, flags);
	nsp_gpio_irq_set_mask(d, true);
	raw_spin_unlock_irqrestore(&chip->lock, flags);
}

static int nsp_gpio_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct nsp_gpio *chip = gpiochip_get_data(gc);
	unsigned gpio = d->hwirq;
	bool level_low;
	bool falling;
	unsigned long flags;

	raw_spin_lock_irqsave(&chip->lock, flags);
	falling = nsp_get_bit(chip, REG, NSP_GPIO_EVENT_INT_POLARITY, gpio);
	level_low = nsp_get_bit(chip, REG, NSP_GPIO_INT_POLARITY, gpio);

	switch (type & IRQ_TYPE_SENSE_MASK) {
	case IRQ_TYPE_EDGE_RISING:
		falling = false;
		break;

	case IRQ_TYPE_EDGE_FALLING:
		falling = true;
		break;

	case IRQ_TYPE_LEVEL_HIGH:
		level_low = false;
		break;

	case IRQ_TYPE_LEVEL_LOW:
		level_low = true;
		break;

	default:
		dev_err(chip->dev, "invalid GPIO IRQ type 0x%x\n",
			type);
		raw_spin_unlock_irqrestore(&chip->lock, flags);
		return -EINVAL;
	}

	nsp_set_bit(chip, REG, NSP_GPIO_EVENT_INT_POLARITY, gpio, falling);
	nsp_set_bit(chip, REG, NSP_GPIO_INT_POLARITY, gpio, level_low);

	if (type & IRQ_TYPE_EDGE_BOTH)
		irq_set_handler_locked(d, handle_edge_irq);
	else
		irq_set_handler_locked(d, handle_level_irq);

	raw_spin_unlock_irqrestore(&chip->lock, flags);

	dev_dbg(chip->dev, "gpio:%u level_low:%s falling:%s\n", gpio,
		str_true_false(level_low), str_true_false(falling));
	return 0;
}

static const struct irq_chip nsp_gpio_irq_chip = {
	.name = "gpio-a",
	.irq_ack = nsp_gpio_irq_ack,
	.irq_mask = nsp_gpio_irq_mask,
	.irq_unmask = nsp_gpio_irq_unmask,
	.irq_set_type = nsp_gpio_irq_set_type,
	.flags = IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static int nsp_gpio_direction_input(struct gpio_chip *gc, unsigned gpio)
{
	struct nsp_gpio *chip = gpiochip_get_data(gc);
	unsigned long flags;

	raw_spin_lock_irqsave(&chip->lock, flags);
	nsp_set_bit(chip, REG, NSP_GPIO_OUT_EN, gpio, false);
	raw_spin_unlock_irqrestore(&chip->lock, flags);

	dev_dbg(chip->dev, "gpio:%u set input\n", gpio);
	return 0;
}

static int nsp_gpio_direction_output(struct gpio_chip *gc, unsigned gpio,
				     int val)
{
	struct nsp_gpio *chip = gpiochip_get_data(gc);
	unsigned long flags;

	raw_spin_lock_irqsave(&chip->lock, flags);
	nsp_set_bit(chip, REG, NSP_GPIO_OUT_EN, gpio, true);
	nsp_set_bit(chip, REG, NSP_GPIO_DATA_OUT, gpio, !!(val));
	raw_spin_unlock_irqrestore(&chip->lock, flags);

	dev_dbg(chip->dev, "gpio:%u set output, value:%d\n", gpio, val);
	return 0;
}

static int nsp_gpio_get_direction(struct gpio_chip *gc, unsigned gpio)
{
	struct nsp_gpio *chip = gpiochip_get_data(gc);
	unsigned long flags;
	int val;

	raw_spin_lock_irqsave(&chip->lock, flags);
	val = nsp_get_bit(chip, REG, NSP_GPIO_OUT_EN, gpio);
	raw_spin_unlock_irqrestore(&chip->lock, flags);

	return !val;
}

static int nsp_gpio_set(struct gpio_chip *gc, unsigned int gpio, int val)
{
	struct nsp_gpio *chip = gpiochip_get_data(gc);
	unsigned long flags;

	raw_spin_lock_irqsave(&chip->lock, flags);
	nsp_set_bit(chip, REG, NSP_GPIO_DATA_OUT, gpio, !!(val));
	raw_spin_unlock_irqrestore(&chip->lock, flags);

	dev_dbg(chip->dev, "gpio:%u set, value:%d\n", gpio, val);

	return 0;
}

static int nsp_gpio_get(struct gpio_chip *gc, unsigned gpio)
{
	struct nsp_gpio *chip = gpiochip_get_data(gc);

	return !!(readl(chip->base + NSP_GPIO_DATA_IN) & BIT(gpio));
}

static int nsp_get_groups_count(struct pinctrl_dev *pctldev)
{
	return 1;
}

/*
 * Only one group: "gpio_grp", since this local pinctrl device only performs
 * GPIO specific PINCONF configurations
 */
static const char *nsp_get_group_name(struct pinctrl_dev *pctldev,
				      unsigned selector)
{
	return "gpio_grp";
}

static const struct pinctrl_ops nsp_pctrl_ops = {
	.get_groups_count = nsp_get_groups_count,
	.get_group_name = nsp_get_group_name,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_pin,
	.dt_free_map = pinctrl_utils_free_map,
};

static int nsp_gpio_set_slew(struct nsp_gpio *chip, unsigned gpio, u32 slew)
{
	if (slew)
		nsp_set_bit(chip, IO_CTRL, NSP_GPIO_SLEW_RATE_EN, gpio, true);
	else
		nsp_set_bit(chip, IO_CTRL, NSP_GPIO_SLEW_RATE_EN, gpio, false);

	return 0;
}

static int nsp_gpio_set_pull(struct nsp_gpio *chip, unsigned gpio,
			     bool pull_up, bool pull_down)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&chip->lock, flags);
	nsp_set_bit(chip, IO_CTRL, NSP_PULL_DOWN_EN, gpio, pull_down);
	nsp_set_bit(chip, IO_CTRL, NSP_PULL_UP_EN, gpio, pull_up);
	raw_spin_unlock_irqrestore(&chip->lock, flags);

	dev_dbg(chip->dev, "gpio:%u set pullup:%d pulldown: %d\n",
		gpio, pull_up, pull_down);
	return 0;
}

static void nsp_gpio_get_pull(struct nsp_gpio *chip, unsigned gpio,
			      bool *pull_up, bool *pull_down)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&chip->lock, flags);
	*pull_up = nsp_get_bit(chip, IO_CTRL, NSP_PULL_UP_EN, gpio);
	*pull_down = nsp_get_bit(chip, IO_CTRL, NSP_PULL_DOWN_EN, gpio);
	raw_spin_unlock_irqrestore(&chip->lock, flags);
}

static int nsp_gpio_set_strength(struct nsp_gpio *chip, unsigned gpio,
				 u32 strength)
{
	u32 offset, shift, i;
	u32 val;
	unsigned long flags;

	/* make sure drive strength is supported */
	if (strength < 2 || strength > 16 || (strength % 2))
		return -ENOTSUPP;

	shift = gpio;
	offset = NSP_GPIO_DRV_CTRL;
	dev_dbg(chip->dev, "gpio:%u set drive strength:%d mA\n", gpio,
		strength);
	raw_spin_lock_irqsave(&chip->lock, flags);
	strength = (strength / 2) - 1;
	for (i = GPIO_DRV_STRENGTH_BITS; i > 0; i--) {
		val = readl(chip->io_ctrl + offset);
		val &= ~BIT(shift);
		val |= ((strength >> (i-1)) & 0x1) << shift;
		writel(val, chip->io_ctrl + offset);
		offset += 4;
	}
	raw_spin_unlock_irqrestore(&chip->lock, flags);

	return 0;
}

static int nsp_gpio_get_strength(struct nsp_gpio *chip, unsigned gpio,
				 u16 *strength)
{
	unsigned int offset, shift;
	u32 val;
	unsigned long flags;
	int i;

	offset = NSP_GPIO_DRV_CTRL;
	shift = gpio;

	raw_spin_lock_irqsave(&chip->lock, flags);
	*strength = 0;
	for (i = (GPIO_DRV_STRENGTH_BITS - 1); i >= 0; i--) {
		val = readl(chip->io_ctrl + offset) & BIT(shift);
		val >>= shift;
		*strength += (val << i);
		offset += 4;
	}

	/* convert to mA */
	*strength = (*strength + 1) * 2;
	raw_spin_unlock_irqrestore(&chip->lock, flags);

	return 0;
}

static int nsp_pin_config_group_get(struct pinctrl_dev *pctldev,
				    unsigned selector,
			     unsigned long *config)
{
	return 0;
}

static int nsp_pin_config_group_set(struct pinctrl_dev *pctldev,
				    unsigned selector,
			     unsigned long *configs, unsigned num_configs)
{
	return 0;
}

static int nsp_pin_config_get(struct pinctrl_dev *pctldev, unsigned pin,
			      unsigned long *config)
{
	struct nsp_gpio *chip = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param = pinconf_to_config_param(*config);
	unsigned int gpio;
	u16 arg = 0;
	bool pull_up, pull_down;
	int ret;

	gpio = nsp_pin_to_gpio(pin);
	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		nsp_gpio_get_pull(chip, gpio, &pull_up, &pull_down);
		if ((pull_up == false) && (pull_down == false))
			return 0;
		else
			return -EINVAL;

	case PIN_CONFIG_BIAS_PULL_UP:
		nsp_gpio_get_pull(chip, gpio, &pull_up, &pull_down);
		if (pull_up)
			return 0;
		else
			return -EINVAL;

	case PIN_CONFIG_BIAS_PULL_DOWN:
		nsp_gpio_get_pull(chip, gpio, &pull_up, &pull_down);
		if (pull_down)
			return 0;
		else
			return -EINVAL;

	case PIN_CONFIG_DRIVE_STRENGTH:
		ret = nsp_gpio_get_strength(chip, gpio, &arg);
		if (ret)
			return ret;
		*config = pinconf_to_config_packed(param, arg);
		return 0;

	default:
		return -ENOTSUPP;
	}
}

static int nsp_pin_config_set(struct pinctrl_dev *pctldev, unsigned pin,
			      unsigned long *configs, unsigned num_configs)
{
	struct nsp_gpio *chip = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param;
	u32 arg;
	unsigned int i, gpio;
	int ret = -ENOTSUPP;

	gpio = nsp_pin_to_gpio(pin);
	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_BIAS_DISABLE:
			ret = nsp_gpio_set_pull(chip, gpio, false, false);
			if (ret < 0)
				goto out;
			break;

		case PIN_CONFIG_BIAS_PULL_UP:
			ret = nsp_gpio_set_pull(chip, gpio, true, false);
			if (ret < 0)
				goto out;
			break;

		case PIN_CONFIG_BIAS_PULL_DOWN:
			ret = nsp_gpio_set_pull(chip, gpio, false, true);
			if (ret < 0)
				goto out;
			break;

		case PIN_CONFIG_DRIVE_STRENGTH:
			ret = nsp_gpio_set_strength(chip, gpio, arg);
			if (ret < 0)
				goto out;
			break;

		case PIN_CONFIG_SLEW_RATE:
			ret = nsp_gpio_set_slew(chip, gpio, arg);
			if (ret < 0)
				goto out;
			break;

		default:
			dev_err(chip->dev, "invalid configuration\n");
			return -ENOTSUPP;
		}
	}

out:
	return ret;
}

static const struct pinconf_ops nsp_pconf_ops = {
	.is_generic = true,
	.pin_config_get = nsp_pin_config_get,
	.pin_config_set = nsp_pin_config_set,
	.pin_config_group_get = nsp_pin_config_group_get,
	.pin_config_group_set = nsp_pin_config_group_set,
};

/*
 * NSP GPIO controller supports some PINCONF related configurations such as
 * pull up, pull down, slew and drive strength, when the pin is configured
 * to GPIO.
 *
 * Here a local pinctrl device is created with simple 1-to-1 pin mapping to the
 * local GPIO pins
 */
static int nsp_gpio_register_pinconf(struct nsp_gpio *chip)
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
	pctldesc->pctlops = &nsp_pctrl_ops;
	pctldesc->pins = pins;
	pctldesc->npins = gc->ngpio;
	pctldesc->confops = &nsp_pconf_ops;

	chip->pctl = devm_pinctrl_register(chip->dev, pctldesc, chip);
	if (IS_ERR(chip->pctl)) {
		dev_err(chip->dev, "unable to register pinctrl device\n");
		return PTR_ERR(chip->pctl);
	}

	return 0;
}

static const struct of_device_id nsp_gpio_of_match[] = {
	{.compatible = "brcm,nsp-gpio-a",},
	{}
};

static int nsp_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct nsp_gpio *chip;
	struct gpio_chip *gc;
	u32 val;
	int irq, ret;

	if (of_property_read_u32(pdev->dev.of_node, "ngpios", &val)) {
		dev_err(&pdev->dev, "Missing ngpios OF property\n");
		return -ENODEV;
	}

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = dev;
	platform_set_drvdata(pdev, chip);

	chip->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(chip->base)) {
		dev_err(dev, "unable to map I/O memory\n");
		return PTR_ERR(chip->base);
	}

	chip->io_ctrl = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(chip->io_ctrl)) {
		dev_err(dev, "unable to map I/O memory\n");
		return PTR_ERR(chip->io_ctrl);
	}

	raw_spin_lock_init(&chip->lock);
	gc = &chip->gc;
	gc->base = -1;
	gc->can_sleep = false;
	gc->ngpio = val;
	gc->label = dev_name(dev);
	gc->parent = dev;
	gc->request = gpiochip_generic_request;
	gc->free = gpiochip_generic_free;
	gc->direction_input = nsp_gpio_direction_input;
	gc->direction_output = nsp_gpio_direction_output;
	gc->get_direction = nsp_gpio_get_direction;
	gc->set = nsp_gpio_set;
	gc->get = nsp_gpio_get;

	/* optional GPIO interrupt support */
	irq = platform_get_irq(pdev, 0);
	if (irq > 0) {
		struct gpio_irq_chip *girq;

		val = readl(chip->base + NSP_CHIP_A_INT_MASK);
		val = val | NSP_CHIP_A_GPIO_INT_BIT;
		writel(val, (chip->base + NSP_CHIP_A_INT_MASK));

		/* Install ISR for this GPIO controller. */
		ret = devm_request_irq(dev, irq, nsp_gpio_irq_handler,
				       IRQF_SHARED, "gpio-a", &chip->gc);
		if (ret) {
			dev_err(&pdev->dev, "Unable to request IRQ%d: %d\n",
				irq, ret);
			return ret;
		}

		girq = &chip->gc.irq;
		gpio_irq_chip_set_chip(girq, &nsp_gpio_irq_chip);
		/* This will let us handle the parent IRQ in the driver */
		girq->parent_handler = NULL;
		girq->num_parents = 0;
		girq->parents = NULL;
		girq->default_type = IRQ_TYPE_NONE;
		girq->handler = handle_bad_irq;
	}

	ret = devm_gpiochip_add_data(dev, gc, chip);
	if (ret < 0)
		return dev_err_probe(dev, ret, "unable to add GPIO chip\n");

	ret = nsp_gpio_register_pinconf(chip);
	if (ret) {
		dev_err(dev, "unable to register pinconf\n");
		return ret;
	}

	return 0;
}

static struct platform_driver nsp_gpio_driver = {
	.driver = {
		.name = "nsp-gpio-a",
		.of_match_table = nsp_gpio_of_match,
	},
	.probe = nsp_gpio_probe,
};

static int __init nsp_gpio_init(void)
{
	return platform_driver_register(&nsp_gpio_driver);
}
arch_initcall_sync(nsp_gpio_init);
