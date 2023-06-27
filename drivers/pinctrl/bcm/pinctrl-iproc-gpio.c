// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014-2017 Broadcom
 */

/*
 * This file contains the Broadcom Iproc GPIO driver that supports 3
 * GPIO controllers on Iproc including the ASIU GPIO controller, the
 * chipCommonG GPIO controller, and the always-on GPIO controller. Basic
 * PINCONF such as bias pull up/down, and drive strength are also supported
 * in this driver.
 *
 * It provides the functionality where pins from the GPIO can be
 * individually muxed to GPIO function, if individual pad
 * configuration is supported, through the interaction with respective
 * SoCs IOMUX controller.
 */

#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>

#include "../pinctrl-utils.h"

#define IPROC_GPIO_DATA_IN_OFFSET   0x00
#define IPROC_GPIO_DATA_OUT_OFFSET  0x04
#define IPROC_GPIO_OUT_EN_OFFSET    0x08
#define IPROC_GPIO_INT_TYPE_OFFSET  0x0c
#define IPROC_GPIO_INT_DE_OFFSET    0x10
#define IPROC_GPIO_INT_EDGE_OFFSET  0x14
#define IPROC_GPIO_INT_MSK_OFFSET   0x18
#define IPROC_GPIO_INT_STAT_OFFSET  0x1c
#define IPROC_GPIO_INT_MSTAT_OFFSET 0x20
#define IPROC_GPIO_INT_CLR_OFFSET   0x24
#define IPROC_GPIO_PAD_RES_OFFSET   0x34
#define IPROC_GPIO_RES_EN_OFFSET    0x38

/* drive strength control for ASIU GPIO */
#define IPROC_GPIO_ASIU_DRV0_CTRL_OFFSET 0x58

/* pinconf for CCM GPIO */
#define IPROC_GPIO_PULL_DN_OFFSET   0x10
#define IPROC_GPIO_PULL_UP_OFFSET   0x14

/* pinconf for CRMU(aon) GPIO and CCM GPIO*/
#define IPROC_GPIO_DRV_CTRL_OFFSET  0x00

#define GPIO_BANK_SIZE 0x200
#define NGPIOS_PER_BANK 32
#define GPIO_BANK(pin) ((pin) / NGPIOS_PER_BANK)

#define IPROC_GPIO_REG(pin, reg) (GPIO_BANK(pin) * GPIO_BANK_SIZE + (reg))
#define IPROC_GPIO_SHIFT(pin) ((pin) % NGPIOS_PER_BANK)

#define GPIO_DRV_STRENGTH_BIT_SHIFT  20
#define GPIO_DRV_STRENGTH_BITS       3
#define GPIO_DRV_STRENGTH_BIT_MASK   ((1 << GPIO_DRV_STRENGTH_BITS) - 1)

enum iproc_pinconf_param {
	IPROC_PINCONF_DRIVE_STRENGTH = 0,
	IPROC_PINCONF_BIAS_DISABLE,
	IPROC_PINCONF_BIAS_PULL_UP,
	IPROC_PINCONF_BIAS_PULL_DOWN,
	IPROC_PINCON_MAX,
};

enum iproc_pinconf_ctrl_type {
	IOCTRL_TYPE_AON = 1,
	IOCTRL_TYPE_CDRU,
	IOCTRL_TYPE_INVALID,
};

/*
 * Iproc GPIO core
 *
 * @dev: pointer to device
 * @base: I/O register base for Iproc GPIO controller
 * @io_ctrl: I/O register base for certain type of Iproc GPIO controller that
 * has the PINCONF support implemented outside of the GPIO block
 * @lock: lock to protect access to I/O registers
 * @gc: GPIO chip
 * @num_banks: number of GPIO banks, each bank supports up to 32 GPIOs
 * @pinmux_is_supported: flag to indicate this GPIO controller contains pins
 * that can be individually muxed to GPIO
 * @pinconf_disable: contains a list of PINCONF parameters that need to be
 * disabled
 * @nr_pinconf_disable: total number of PINCONF parameters that need to be
 * disabled
 * @pctl: pointer to pinctrl_dev
 * @pctldesc: pinctrl descriptor
 */
struct iproc_gpio {
	struct device *dev;

	void __iomem *base;
	void __iomem *io_ctrl;
	enum iproc_pinconf_ctrl_type io_ctrl_type;

	raw_spinlock_t lock;

	struct gpio_chip gc;
	unsigned num_banks;

	bool pinmux_is_supported;

	enum pin_config_param *pinconf_disable;
	unsigned int nr_pinconf_disable;

	struct pinctrl_dev *pctl;
	struct pinctrl_desc pctldesc;
};

/*
 * Mapping from PINCONF pins to GPIO pins is 1-to-1
 */
static inline unsigned iproc_pin_to_gpio(unsigned pin)
{
	return pin;
}

/**
 *  iproc_set_bit - set or clear one bit (corresponding to the GPIO pin) in a
 *  Iproc GPIO register
 *
 *  @chip: Iproc GPIO device
 *  @reg: register offset
 *  @gpio: GPIO pin
 *  @set: set or clear
 */
static inline void iproc_set_bit(struct iproc_gpio *chip, unsigned int reg,
				  unsigned gpio, bool set)
{
	unsigned int offset = IPROC_GPIO_REG(gpio, reg);
	unsigned int shift = IPROC_GPIO_SHIFT(gpio);
	u32 val;

	val = readl(chip->base + offset);
	if (set)
		val |= BIT(shift);
	else
		val &= ~BIT(shift);
	writel(val, chip->base + offset);
}

static inline bool iproc_get_bit(struct iproc_gpio *chip, unsigned int reg,
				  unsigned gpio)
{
	unsigned int offset = IPROC_GPIO_REG(gpio, reg);
	unsigned int shift = IPROC_GPIO_SHIFT(gpio);

	return !!(readl(chip->base + offset) & BIT(shift));
}

static void iproc_gpio_irq_handler(struct irq_desc *desc)
{
	struct gpio_chip *gc = irq_desc_get_handler_data(desc);
	struct iproc_gpio *chip = gpiochip_get_data(gc);
	struct irq_chip *irq_chip = irq_desc_get_chip(desc);
	int i, bit;

	chained_irq_enter(irq_chip, desc);

	/* go through the entire GPIO banks and handle all interrupts */
	for (i = 0; i < chip->num_banks; i++) {
		unsigned long val = readl(chip->base + (i * GPIO_BANK_SIZE) +
					  IPROC_GPIO_INT_MSTAT_OFFSET);

		for_each_set_bit(bit, &val, NGPIOS_PER_BANK) {
			unsigned pin = NGPIOS_PER_BANK * i + bit;

			/*
			 * Clear the interrupt before invoking the
			 * handler, so we do not leave any window
			 */
			writel(BIT(bit), chip->base + (i * GPIO_BANK_SIZE) +
			       IPROC_GPIO_INT_CLR_OFFSET);

			generic_handle_domain_irq(gc->irq.domain, pin);
		}
	}

	chained_irq_exit(irq_chip, desc);
}


static void iproc_gpio_irq_ack(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct iproc_gpio *chip = gpiochip_get_data(gc);
	unsigned gpio = d->hwirq;
	unsigned int offset = IPROC_GPIO_REG(gpio,
			IPROC_GPIO_INT_CLR_OFFSET);
	unsigned int shift = IPROC_GPIO_SHIFT(gpio);
	u32 val = BIT(shift);

	writel(val, chip->base + offset);
}

/**
 *  iproc_gpio_irq_set_mask - mask/unmask a GPIO interrupt
 *
 *  @d: IRQ chip data
 *  @unmask: mask/unmask GPIO interrupt
 */
static void iproc_gpio_irq_set_mask(struct irq_data *d, bool unmask)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct iproc_gpio *chip = gpiochip_get_data(gc);
	unsigned gpio = irqd_to_hwirq(d);

	iproc_set_bit(chip, IPROC_GPIO_INT_MSK_OFFSET, gpio, unmask);
}

static void iproc_gpio_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct iproc_gpio *chip = gpiochip_get_data(gc);
	unsigned long flags;

	raw_spin_lock_irqsave(&chip->lock, flags);
	iproc_gpio_irq_set_mask(d, false);
	raw_spin_unlock_irqrestore(&chip->lock, flags);
	gpiochip_disable_irq(gc, irqd_to_hwirq(d));
}

static void iproc_gpio_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct iproc_gpio *chip = gpiochip_get_data(gc);
	unsigned long flags;

	gpiochip_enable_irq(gc, irqd_to_hwirq(d));
	raw_spin_lock_irqsave(&chip->lock, flags);
	iproc_gpio_irq_set_mask(d, true);
	raw_spin_unlock_irqrestore(&chip->lock, flags);
}

static int iproc_gpio_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct iproc_gpio *chip = gpiochip_get_data(gc);
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

	raw_spin_lock_irqsave(&chip->lock, flags);
	iproc_set_bit(chip, IPROC_GPIO_INT_TYPE_OFFSET, gpio,
		       level_triggered);
	iproc_set_bit(chip, IPROC_GPIO_INT_DE_OFFSET, gpio, dual_edge);
	iproc_set_bit(chip, IPROC_GPIO_INT_EDGE_OFFSET, gpio,
		       rising_or_high);

	if (type & IRQ_TYPE_EDGE_BOTH)
		irq_set_handler_locked(d, handle_edge_irq);
	else
		irq_set_handler_locked(d, handle_level_irq);

	raw_spin_unlock_irqrestore(&chip->lock, flags);

	dev_dbg(chip->dev,
		"gpio:%u level_triggered:%d dual_edge:%d rising_or_high:%d\n",
		gpio, level_triggered, dual_edge, rising_or_high);

	return 0;
}

static void iproc_gpio_irq_print_chip(struct irq_data *d, struct seq_file *p)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct iproc_gpio *chip = gpiochip_get_data(gc);

	seq_printf(p, dev_name(chip->dev));
}

static const struct irq_chip iproc_gpio_irq_chip = {
	.irq_ack = iproc_gpio_irq_ack,
	.irq_mask = iproc_gpio_irq_mask,
	.irq_unmask = iproc_gpio_irq_unmask,
	.irq_set_type = iproc_gpio_irq_set_type,
	.irq_enable = iproc_gpio_irq_unmask,
	.irq_disable = iproc_gpio_irq_mask,
	.irq_print_chip = iproc_gpio_irq_print_chip,
	.flags = IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

/*
 * Request the Iproc IOMUX pinmux controller to mux individual pins to GPIO
 */
static int iproc_gpio_request(struct gpio_chip *gc, unsigned offset)
{
	struct iproc_gpio *chip = gpiochip_get_data(gc);
	unsigned gpio = gc->base + offset;

	/* not all Iproc GPIO pins can be muxed individually */
	if (!chip->pinmux_is_supported)
		return 0;

	return pinctrl_gpio_request(gpio);
}

static void iproc_gpio_free(struct gpio_chip *gc, unsigned offset)
{
	struct iproc_gpio *chip = gpiochip_get_data(gc);
	unsigned gpio = gc->base + offset;

	if (!chip->pinmux_is_supported)
		return;

	pinctrl_gpio_free(gpio);
}

static int iproc_gpio_direction_input(struct gpio_chip *gc, unsigned gpio)
{
	struct iproc_gpio *chip = gpiochip_get_data(gc);
	unsigned long flags;

	raw_spin_lock_irqsave(&chip->lock, flags);
	iproc_set_bit(chip, IPROC_GPIO_OUT_EN_OFFSET, gpio, false);
	raw_spin_unlock_irqrestore(&chip->lock, flags);

	dev_dbg(chip->dev, "gpio:%u set input\n", gpio);

	return 0;
}

static int iproc_gpio_direction_output(struct gpio_chip *gc, unsigned gpio,
					int val)
{
	struct iproc_gpio *chip = gpiochip_get_data(gc);
	unsigned long flags;

	raw_spin_lock_irqsave(&chip->lock, flags);
	iproc_set_bit(chip, IPROC_GPIO_OUT_EN_OFFSET, gpio, true);
	iproc_set_bit(chip, IPROC_GPIO_DATA_OUT_OFFSET, gpio, !!(val));
	raw_spin_unlock_irqrestore(&chip->lock, flags);

	dev_dbg(chip->dev, "gpio:%u set output, value:%d\n", gpio, val);

	return 0;
}

static int iproc_gpio_get_direction(struct gpio_chip *gc, unsigned int gpio)
{
	struct iproc_gpio *chip = gpiochip_get_data(gc);
	unsigned int offset = IPROC_GPIO_REG(gpio, IPROC_GPIO_OUT_EN_OFFSET);
	unsigned int shift = IPROC_GPIO_SHIFT(gpio);

	if (readl(chip->base + offset) & BIT(shift))
		return GPIO_LINE_DIRECTION_OUT;

	return GPIO_LINE_DIRECTION_IN;
}

static void iproc_gpio_set(struct gpio_chip *gc, unsigned gpio, int val)
{
	struct iproc_gpio *chip = gpiochip_get_data(gc);
	unsigned long flags;

	raw_spin_lock_irqsave(&chip->lock, flags);
	iproc_set_bit(chip, IPROC_GPIO_DATA_OUT_OFFSET, gpio, !!(val));
	raw_spin_unlock_irqrestore(&chip->lock, flags);

	dev_dbg(chip->dev, "gpio:%u set, value:%d\n", gpio, val);
}

static int iproc_gpio_get(struct gpio_chip *gc, unsigned gpio)
{
	struct iproc_gpio *chip = gpiochip_get_data(gc);
	unsigned int offset = IPROC_GPIO_REG(gpio,
					      IPROC_GPIO_DATA_IN_OFFSET);
	unsigned int shift = IPROC_GPIO_SHIFT(gpio);

	return !!(readl(chip->base + offset) & BIT(shift));
}

/*
 * Mapping of the iProc PINCONF parameters to the generic pin configuration
 * parameters
 */
static const enum pin_config_param iproc_pinconf_disable_map[] = {
	[IPROC_PINCONF_DRIVE_STRENGTH] = PIN_CONFIG_DRIVE_STRENGTH,
	[IPROC_PINCONF_BIAS_DISABLE] = PIN_CONFIG_BIAS_DISABLE,
	[IPROC_PINCONF_BIAS_PULL_UP] = PIN_CONFIG_BIAS_PULL_UP,
	[IPROC_PINCONF_BIAS_PULL_DOWN] = PIN_CONFIG_BIAS_PULL_DOWN,
};

static bool iproc_pinconf_param_is_disabled(struct iproc_gpio *chip,
					    enum pin_config_param param)
{
	unsigned int i;

	if (!chip->nr_pinconf_disable)
		return false;

	for (i = 0; i < chip->nr_pinconf_disable; i++)
		if (chip->pinconf_disable[i] == param)
			return true;

	return false;
}

static int iproc_pinconf_disable_map_create(struct iproc_gpio *chip,
					    unsigned long disable_mask)
{
	unsigned int map_size = ARRAY_SIZE(iproc_pinconf_disable_map);
	unsigned int bit, nbits = 0;

	/* figure out total number of PINCONF parameters to disable */
	for_each_set_bit(bit, &disable_mask, map_size)
		nbits++;

	if (!nbits)
		return 0;

	/*
	 * Allocate an array to store PINCONF parameters that need to be
	 * disabled
	 */
	chip->pinconf_disable = devm_kcalloc(chip->dev, nbits,
					     sizeof(*chip->pinconf_disable),
					     GFP_KERNEL);
	if (!chip->pinconf_disable)
		return -ENOMEM;

	chip->nr_pinconf_disable = nbits;

	/* now store these parameters */
	nbits = 0;
	for_each_set_bit(bit, &disable_mask, map_size)
		chip->pinconf_disable[nbits++] = iproc_pinconf_disable_map[bit];

	return 0;
}

static int iproc_get_groups_count(struct pinctrl_dev *pctldev)
{
	return 1;
}

/*
 * Only one group: "gpio_grp", since this local pinctrl device only performs
 * GPIO specific PINCONF configurations
 */
static const char *iproc_get_group_name(struct pinctrl_dev *pctldev,
					 unsigned selector)
{
	return "gpio_grp";
}

static const struct pinctrl_ops iproc_pctrl_ops = {
	.get_groups_count = iproc_get_groups_count,
	.get_group_name = iproc_get_group_name,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_pin,
	.dt_free_map = pinctrl_utils_free_map,
};

static int iproc_gpio_set_pull(struct iproc_gpio *chip, unsigned gpio,
				bool disable, bool pull_up)
{
	void __iomem *base;
	unsigned long flags;
	unsigned int shift;
	u32 val_1, val_2;

	raw_spin_lock_irqsave(&chip->lock, flags);
	if (chip->io_ctrl_type == IOCTRL_TYPE_CDRU) {
		base = chip->io_ctrl;
		shift = IPROC_GPIO_SHIFT(gpio);

		val_1 = readl(base + IPROC_GPIO_PULL_UP_OFFSET);
		val_2 = readl(base + IPROC_GPIO_PULL_DN_OFFSET);
		if (disable) {
			/* no pull-up or pull-down */
			val_1 &= ~BIT(shift);
			val_2 &= ~BIT(shift);
		} else if (pull_up) {
			val_1 |= BIT(shift);
			val_2 &= ~BIT(shift);
		} else {
			val_1 &= ~BIT(shift);
			val_2 |= BIT(shift);
		}
		writel(val_1, base + IPROC_GPIO_PULL_UP_OFFSET);
		writel(val_2, base + IPROC_GPIO_PULL_DN_OFFSET);
	} else {
		if (disable) {
			iproc_set_bit(chip, IPROC_GPIO_RES_EN_OFFSET, gpio,
				      false);
		} else {
			iproc_set_bit(chip, IPROC_GPIO_PAD_RES_OFFSET, gpio,
				      pull_up);
			iproc_set_bit(chip, IPROC_GPIO_RES_EN_OFFSET, gpio,
				      true);
		}
	}

	raw_spin_unlock_irqrestore(&chip->lock, flags);
	dev_dbg(chip->dev, "gpio:%u set pullup:%d\n", gpio, pull_up);

	return 0;
}

static void iproc_gpio_get_pull(struct iproc_gpio *chip, unsigned gpio,
				 bool *disable, bool *pull_up)
{
	void __iomem *base;
	unsigned long flags;
	unsigned int shift;
	u32 val_1, val_2;

	raw_spin_lock_irqsave(&chip->lock, flags);
	if (chip->io_ctrl_type == IOCTRL_TYPE_CDRU) {
		base = chip->io_ctrl;
		shift = IPROC_GPIO_SHIFT(gpio);

		val_1 = readl(base + IPROC_GPIO_PULL_UP_OFFSET) & BIT(shift);
		val_2 = readl(base + IPROC_GPIO_PULL_DN_OFFSET) & BIT(shift);

		*pull_up = val_1 ? true : false;
		*disable = (val_1 | val_2) ? false : true;

	} else {
		*disable = !iproc_get_bit(chip, IPROC_GPIO_RES_EN_OFFSET, gpio);
		*pull_up = iproc_get_bit(chip, IPROC_GPIO_PAD_RES_OFFSET, gpio);
	}
	raw_spin_unlock_irqrestore(&chip->lock, flags);
}

#define DRV_STRENGTH_OFFSET(gpio, bit, type)  ((type) == IOCTRL_TYPE_AON ? \
	((2 - (bit)) * 4 + IPROC_GPIO_DRV_CTRL_OFFSET) : \
	((type) == IOCTRL_TYPE_CDRU) ? \
	((bit) * 4 + IPROC_GPIO_DRV_CTRL_OFFSET) : \
	((bit) * 4 + IPROC_GPIO_REG(gpio, IPROC_GPIO_ASIU_DRV0_CTRL_OFFSET)))

static int iproc_gpio_set_strength(struct iproc_gpio *chip, unsigned gpio,
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
	} else {
		base = chip->base;
	}

	shift = IPROC_GPIO_SHIFT(gpio);

	dev_dbg(chip->dev, "gpio:%u set drive strength:%d mA\n", gpio,
		strength);

	raw_spin_lock_irqsave(&chip->lock, flags);
	strength = (strength / 2) - 1;
	for (i = 0; i < GPIO_DRV_STRENGTH_BITS; i++) {
		offset = DRV_STRENGTH_OFFSET(gpio, i, chip->io_ctrl_type);
		val = readl(base + offset);
		val &= ~BIT(shift);
		val |= ((strength >> i) & 0x1) << shift;
		writel(val, base + offset);
	}
	raw_spin_unlock_irqrestore(&chip->lock, flags);

	return 0;
}

static int iproc_gpio_get_strength(struct iproc_gpio *chip, unsigned gpio,
				    u16 *strength)
{
	void __iomem *base;
	unsigned int i, offset, shift;
	u32 val;
	unsigned long flags;

	if (chip->io_ctrl) {
		base = chip->io_ctrl;
	} else {
		base = chip->base;
	}

	shift = IPROC_GPIO_SHIFT(gpio);

	raw_spin_lock_irqsave(&chip->lock, flags);
	*strength = 0;
	for (i = 0; i < GPIO_DRV_STRENGTH_BITS; i++) {
		offset = DRV_STRENGTH_OFFSET(gpio, i, chip->io_ctrl_type);
		val = readl(base + offset) & BIT(shift);
		val >>= shift;
		*strength += (val << i);
	}

	/* convert to mA */
	*strength = (*strength + 1) * 2;
	raw_spin_unlock_irqrestore(&chip->lock, flags);

	return 0;
}

static int iproc_pin_config_get(struct pinctrl_dev *pctldev, unsigned pin,
				 unsigned long *config)
{
	struct iproc_gpio *chip = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param = pinconf_to_config_param(*config);
	unsigned gpio = iproc_pin_to_gpio(pin);
	u16 arg;
	bool disable, pull_up;
	int ret;

	if (iproc_pinconf_param_is_disabled(chip, param))
		return -ENOTSUPP;

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		iproc_gpio_get_pull(chip, gpio, &disable, &pull_up);
		if (disable)
			return 0;
		else
			return -EINVAL;

	case PIN_CONFIG_BIAS_PULL_UP:
		iproc_gpio_get_pull(chip, gpio, &disable, &pull_up);
		if (!disable && pull_up)
			return 0;
		else
			return -EINVAL;

	case PIN_CONFIG_BIAS_PULL_DOWN:
		iproc_gpio_get_pull(chip, gpio, &disable, &pull_up);
		if (!disable && !pull_up)
			return 0;
		else
			return -EINVAL;

	case PIN_CONFIG_DRIVE_STRENGTH:
		ret = iproc_gpio_get_strength(chip, gpio, &arg);
		if (ret)
			return ret;
		*config = pinconf_to_config_packed(param, arg);

		return 0;

	default:
		return -ENOTSUPP;
	}

	return -ENOTSUPP;
}

static int iproc_pin_config_set(struct pinctrl_dev *pctldev, unsigned pin,
				 unsigned long *configs, unsigned num_configs)
{
	struct iproc_gpio *chip = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param;
	u32 arg;
	unsigned i, gpio = iproc_pin_to_gpio(pin);
	int ret = -ENOTSUPP;

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);

		if (iproc_pinconf_param_is_disabled(chip, param))
			return -ENOTSUPP;

		arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_BIAS_DISABLE:
			ret = iproc_gpio_set_pull(chip, gpio, true, false);
			if (ret < 0)
				goto out;
			break;

		case PIN_CONFIG_BIAS_PULL_UP:
			ret = iproc_gpio_set_pull(chip, gpio, false, true);
			if (ret < 0)
				goto out;
			break;

		case PIN_CONFIG_BIAS_PULL_DOWN:
			ret = iproc_gpio_set_pull(chip, gpio, false, false);
			if (ret < 0)
				goto out;
			break;

		case PIN_CONFIG_DRIVE_STRENGTH:
			ret = iproc_gpio_set_strength(chip, gpio, arg);
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

static const struct pinconf_ops iproc_pconf_ops = {
	.is_generic = true,
	.pin_config_get = iproc_pin_config_get,
	.pin_config_set = iproc_pin_config_set,
};

/*
 * Iproc GPIO controller supports some PINCONF related configurations such as
 * pull up, pull down, and drive strength, when the pin is configured to GPIO
 *
 * Here a local pinctrl device is created with simple 1-to-1 pin mapping to the
 * local GPIO pins
 */
static int iproc_gpio_register_pinconf(struct iproc_gpio *chip)
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
	pctldesc->pctlops = &iproc_pctrl_ops;
	pctldesc->pins = pins;
	pctldesc->npins = gc->ngpio;
	pctldesc->confops = &iproc_pconf_ops;

	chip->pctl = devm_pinctrl_register(chip->dev, pctldesc, chip);
	if (IS_ERR(chip->pctl)) {
		dev_err(chip->dev, "unable to register pinctrl device\n");
		return PTR_ERR(chip->pctl);
	}

	return 0;
}

static const struct of_device_id iproc_gpio_of_match[] = {
	{ .compatible = "brcm,iproc-gpio" },
	{ .compatible = "brcm,cygnus-ccm-gpio" },
	{ .compatible = "brcm,cygnus-asiu-gpio" },
	{ .compatible = "brcm,cygnus-crmu-gpio" },
	{ .compatible = "brcm,iproc-nsp-gpio" },
	{ .compatible = "brcm,iproc-stingray-gpio" },
	{ /* sentinel */ }
};

static int iproc_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct iproc_gpio *chip;
	struct gpio_chip *gc;
	u32 ngpios, pinconf_disable_mask = 0;
	int irq, ret;
	bool no_pinconf = false;
	enum iproc_pinconf_ctrl_type io_ctrl_type = IOCTRL_TYPE_INVALID;

	/* NSP does not support drive strength config */
	if (of_device_is_compatible(dev->of_node, "brcm,iproc-nsp-gpio"))
		pinconf_disable_mask = BIT(IPROC_PINCONF_DRIVE_STRENGTH);
	/* Stingray does not support pinconf in this controller */
	else if (of_device_is_compatible(dev->of_node,
					 "brcm,iproc-stingray-gpio"))
		no_pinconf = true;

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

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (res) {
		chip->io_ctrl = devm_ioremap_resource(dev, res);
		if (IS_ERR(chip->io_ctrl))
			return PTR_ERR(chip->io_ctrl);
		if (of_device_is_compatible(dev->of_node,
					    "brcm,cygnus-ccm-gpio"))
			io_ctrl_type = IOCTRL_TYPE_CDRU;
		else
			io_ctrl_type = IOCTRL_TYPE_AON;
	}

	chip->io_ctrl_type = io_ctrl_type;

	if (of_property_read_u32(dev->of_node, "ngpios", &ngpios)) {
		dev_err(&pdev->dev, "missing ngpios DT property\n");
		return -ENODEV;
	}

	raw_spin_lock_init(&chip->lock);

	gc = &chip->gc;
	gc->base = -1;
	gc->ngpio = ngpios;
	chip->num_banks = (ngpios + NGPIOS_PER_BANK - 1) / NGPIOS_PER_BANK;
	gc->label = dev_name(dev);
	gc->parent = dev;
	gc->request = iproc_gpio_request;
	gc->free = iproc_gpio_free;
	gc->direction_input = iproc_gpio_direction_input;
	gc->direction_output = iproc_gpio_direction_output;
	gc->get_direction = iproc_gpio_get_direction;
	gc->set = iproc_gpio_set;
	gc->get = iproc_gpio_get;

	chip->pinmux_is_supported = of_property_read_bool(dev->of_node,
							"gpio-ranges");

	/* optional GPIO interrupt support */
	irq = platform_get_irq_optional(pdev, 0);
	if (irq > 0) {
		struct gpio_irq_chip *girq;

		girq = &gc->irq;
		gpio_irq_chip_set_chip(girq, &iproc_gpio_irq_chip);
		girq->parent_handler = iproc_gpio_irq_handler;
		girq->num_parents = 1;
		girq->parents = devm_kcalloc(dev, 1,
					     sizeof(*girq->parents),
					     GFP_KERNEL);
		if (!girq->parents)
			return -ENOMEM;
		girq->parents[0] = irq;
		girq->default_type = IRQ_TYPE_NONE;
		girq->handler = handle_bad_irq;
	}

	ret = gpiochip_add_data(gc, chip);
	if (ret < 0) {
		dev_err(dev, "unable to add GPIO chip\n");
		return ret;
	}

	if (!no_pinconf) {
		ret = iproc_gpio_register_pinconf(chip);
		if (ret) {
			dev_err(dev, "unable to register pinconf\n");
			goto err_rm_gpiochip;
		}

		if (pinconf_disable_mask) {
			ret = iproc_pinconf_disable_map_create(chip,
							 pinconf_disable_mask);
			if (ret) {
				dev_err(dev,
					"unable to create pinconf disable map\n");
				goto err_rm_gpiochip;
			}
		}
	}

	return 0;

err_rm_gpiochip:
	gpiochip_remove(gc);

	return ret;
}

static struct platform_driver iproc_gpio_driver = {
	.driver = {
		.name = "iproc-gpio",
		.of_match_table = iproc_gpio_of_match,
	},
	.probe = iproc_gpio_probe,
};

static int __init iproc_gpio_init(void)
{
	return platform_driver_register(&iproc_gpio_driver);
}
arch_initcall_sync(iproc_gpio_init);
