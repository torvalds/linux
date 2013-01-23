/*
 * Copyright (C) ST-Ericsson SA 2013
 *
 * Author: Patrice Chotard <patrice.chotard@st.com>
 * License terms: GNU General Public License (GPL) version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/bitops.h>
#include <linux/mfd/abx500.h>
#include <linux/mfd/abx500/ab8500.h>
#include <linux/mfd/abx500/ab8500-gpio.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>

#include "pinctrl-abx500.h"

/*
 * The AB9540 and AB8540 GPIO support are extended versions
 * of the AB8500 GPIO support.
 * The AB9540 supports an additional (7th) register so that
 * more GPIO may be configured and used.
 * The AB8540 supports 4 new gpios (GPIOx_VBAT) that have
 * internal pull-up and pull-down capabilities.
 */

/*
 * GPIO registers offset
 * Bank: 0x10
 */
#define AB8500_GPIO_SEL1_REG	0x00
#define AB8500_GPIO_SEL2_REG	0x01
#define AB8500_GPIO_SEL3_REG	0x02
#define AB8500_GPIO_SEL4_REG	0x03
#define AB8500_GPIO_SEL5_REG	0x04
#define AB8500_GPIO_SEL6_REG	0x05
#define AB9540_GPIO_SEL7_REG	0x06

#define AB8500_GPIO_DIR1_REG	0x10
#define AB8500_GPIO_DIR2_REG	0x11
#define AB8500_GPIO_DIR3_REG	0x12
#define AB8500_GPIO_DIR4_REG	0x13
#define AB8500_GPIO_DIR5_REG	0x14
#define AB8500_GPIO_DIR6_REG	0x15
#define AB9540_GPIO_DIR7_REG	0x16

#define AB8500_GPIO_OUT1_REG	0x20
#define AB8500_GPIO_OUT2_REG	0x21
#define AB8500_GPIO_OUT3_REG	0x22
#define AB8500_GPIO_OUT4_REG	0x23
#define AB8500_GPIO_OUT5_REG	0x24
#define AB8500_GPIO_OUT6_REG	0x25
#define AB9540_GPIO_OUT7_REG	0x26

#define AB8500_GPIO_PUD1_REG	0x30
#define AB8500_GPIO_PUD2_REG	0x31
#define AB8500_GPIO_PUD3_REG	0x32
#define AB8500_GPIO_PUD4_REG	0x33
#define AB8500_GPIO_PUD5_REG	0x34
#define AB8500_GPIO_PUD6_REG	0x35
#define AB9540_GPIO_PUD7_REG	0x36

#define AB8500_GPIO_IN1_REG	0x40
#define AB8500_GPIO_IN2_REG	0x41
#define AB8500_GPIO_IN3_REG	0x42
#define AB8500_GPIO_IN4_REG	0x43
#define AB8500_GPIO_IN5_REG	0x44
#define AB8500_GPIO_IN6_REG	0x45
#define AB9540_GPIO_IN7_REG	0x46
#define AB8540_GPIO_VINSEL_REG	0x47
#define AB8540_GPIO_PULL_UPDOWN_REG	0x48
#define AB8500_GPIO_ALTFUN_REG	0x50
#define AB8500_NUM_VIR_GPIO_IRQ	16
#define AB8540_GPIO_PULL_UPDOWN_MASK	0x03
#define AB8540_GPIO_VINSEL_MASK	0x03
#define AB8540_GPIOX_VBAT_START	51
#define AB8540_GPIOX_VBAT_END	54

enum abx500_gpio_action {
	NONE,
	STARTUP,
	SHUTDOWN,
	MASK,
	UNMASK
};

struct abx500_pinctrl {
	struct device *dev;
	struct pinctrl_dev *pctldev;
	struct abx500_pinctrl_soc_data *soc;
	struct gpio_chip chip;
	struct ab8500 *parent;
	struct mutex lock;
	u32 irq_base;
	enum abx500_gpio_action irq_action;
	u16 rising;
	u16 falling;
	struct abx500_gpio_irq_cluster *irq_cluster;
	int irq_cluster_size;
	int irq_gpio_rising_offset;
	int irq_gpio_falling_offset;
	int irq_gpio_factor;
};

/**
 * to_abx500_pinctrl() - get the pointer to abx500_pinctrl
 * @chip:	Member of the structure abx500_pinctrl
 */
static inline struct abx500_pinctrl *to_abx500_pinctrl(struct gpio_chip *chip)
{
	return container_of(chip, struct abx500_pinctrl, chip);
}

static int abx500_gpio_get_bit(struct gpio_chip *chip, u8 reg,
					unsigned offset, bool *bit)
{
	struct abx500_pinctrl *pct = to_abx500_pinctrl(chip);
	u8 pos = offset % 8;
	u8 val;
	int ret;

	reg += offset / 8;
	ret = abx500_get_register_interruptible(pct->dev,
						AB8500_MISC, reg, &val);

	*bit = !!(val & BIT(pos));

	if (ret < 0)
		dev_err(pct->dev,
			"%s read reg =%x, offset=%x failed\n",
			__func__, reg, offset);

	return ret;
}

static int abx500_gpio_set_bits(struct gpio_chip *chip, u8 reg,
					unsigned offset, int val)
{
	struct abx500_pinctrl *pct = to_abx500_pinctrl(chip);
	u8 pos = offset % 8;
	int ret;

	reg += offset / 8;
	ret = abx500_mask_and_set_register_interruptible(pct->dev,
				AB8500_MISC, reg, BIT(pos), val << pos);
	if (ret < 0)
		dev_err(pct->dev, "%s write failed\n", __func__);
	return ret;
}
/**
 * abx500_gpio_get() - Get the particular GPIO value
 * @chip: Gpio device
 * @offset: GPIO number to read
 */
static int abx500_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct abx500_pinctrl *pct = to_abx500_pinctrl(chip);
	bool bit;
	int ret;

	ret = abx500_gpio_get_bit(chip, AB8500_GPIO_IN1_REG,
				  offset, &bit);
	if (ret < 0) {
		dev_err(pct->dev, "%s failed\n", __func__);
		return ret;
	}
	return bit;
}

static void abx500_gpio_set(struct gpio_chip *chip, unsigned offset, int val)
{
	struct abx500_pinctrl *pct = to_abx500_pinctrl(chip);
	int ret;

	ret = abx500_gpio_set_bits(chip, AB8500_GPIO_OUT1_REG, offset, val);
	if (ret < 0)
		dev_err(pct->dev, "%s write failed\n", __func__);
}

static int abx500_config_pull_updown(struct abx500_pinctrl *pct,
				int offset, enum abx500_gpio_pull_updown val)
{
	u8 pos;
	int ret;
	struct pullud *pullud;

	if (!pct->soc->pullud) {
		dev_err(pct->dev, "%s AB chip doesn't support pull up/down feature",
				__func__);
		ret = -EPERM;
		goto out;
	}

	pullud = pct->soc->pullud;

	if ((offset < pullud->first_pin)
		|| (offset > pullud->last_pin)) {
		ret = -EINVAL;
		goto out;
	}

	pos = offset << 1;

	ret = abx500_mask_and_set_register_interruptible(pct->dev,
			AB8500_MISC, AB8540_GPIO_PULL_UPDOWN_REG,
			AB8540_GPIO_PULL_UPDOWN_MASK << pos, val << pos);

out:
	if (ret < 0)
		dev_err(pct->dev, "%s failed (%d)\n", __func__, ret);
	return ret;
}

static int abx500_gpio_direction_output(struct gpio_chip *chip,
					unsigned offset,
					int val)
{
	struct abx500_pinctrl *pct = to_abx500_pinctrl(chip);
	struct pullud *pullud = pct->soc->pullud;
	unsigned gpio;
	int ret;
	/* set direction as output */
	ret = abx500_gpio_set_bits(chip, AB8500_GPIO_DIR1_REG, offset, 1);
	if (ret < 0)
		return ret;

	/* disable pull down */
	ret = abx500_gpio_set_bits(chip, AB8500_GPIO_PUD1_REG, offset, 1);
	if (ret < 0)
		return ret;

	/* if supported, disable both pull down and pull up */
	gpio = offset + 1;
	if (pullud && gpio >= pullud->first_pin && gpio <= pullud->last_pin) {
		ret = abx500_config_pull_updown(pct,
				gpio,
				ABX500_GPIO_PULL_NONE);
		if (ret < 0)
			return ret;
	}
	/* set the output as 1 or 0 */
	return abx500_gpio_set_bits(chip, AB8500_GPIO_OUT1_REG, offset, val);
}

static int abx500_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	/* set the register as input */
	return abx500_gpio_set_bits(chip, AB8500_GPIO_DIR1_REG, offset, 0);
}

static int abx500_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	struct abx500_pinctrl *pct = to_abx500_pinctrl(chip);
	int base = pct->irq_base;
	int i;

	for (i = 0; i < pct->irq_cluster_size; i++) {
		struct abx500_gpio_irq_cluster *cluster =
			&pct->irq_cluster[i];

		if (offset >= cluster->start && offset <= cluster->end)
			return base + offset - cluster->start;

		/* Advance by the number of gpios in this cluster */
		base += cluster->end + cluster->offset - cluster->start + 1;
	}

	return -EINVAL;
}

static int abx500_set_mode(struct pinctrl_dev *pctldev, struct gpio_chip *chip,
		unsigned gpio, int alt_setting)
{
	struct abx500_pinctrl *pct = pinctrl_dev_get_drvdata(pctldev);
	struct alternate_functions af = pct->soc->alternate_functions[gpio];
	int ret;
	int val;
	unsigned offset;
	const char *modes[] = {
		[ABX500_DEFAULT]	= "default",
		[ABX500_ALT_A]		= "altA",
		[ABX500_ALT_B]		= "altB",
		[ABX500_ALT_C]		= "altC",
	};

	/* sanity check */
	if (((alt_setting == ABX500_ALT_A) && (af.gpiosel_bit == UNUSED)) ||
	    ((alt_setting == ABX500_ALT_B) && (af.alt_bit1 == UNUSED)) ||
	    ((alt_setting == ABX500_ALT_C) && (af.alt_bit2 == UNUSED))) {
		dev_dbg(pct->dev, "pin %d doesn't support %s mode\n", gpio,
				modes[alt_setting]);
		return -EINVAL;
	}

	/* on ABx5xx, there is no GPIO0, so adjust the offset */
	offset = gpio - 1;
	switch (alt_setting) {
	case ABX500_DEFAULT:
		/*
		 * for ABx5xx family, default mode is always selected by
		 * writing 0 to GPIOSELx register, except for pins which
		 * support at least ALT_B mode, default mode is selected
		 * by writing 1 to GPIOSELx register
		 */
		val = 0;
		if (af.alt_bit1 != UNUSED)
			val++;

		ret = abx500_gpio_set_bits(chip, AB8500_GPIO_SEL1_REG,
					   offset, val);
		break;
	case ABX500_ALT_A:
		/*
		 * for ABx5xx family, alt_a mode is always selected by
		 * writing 1 to GPIOSELx register, except for pins which
		 * support at least ALT_B mode, alt_a mode is selected
		 * by writing 0 to GPIOSELx register and 0 in ALTFUNC
		 * register
		 */
		if (af.alt_bit1 != UNUSED) {
			ret = abx500_gpio_set_bits(chip, AB8500_GPIO_SEL1_REG,
					offset, 0);
			ret = abx500_gpio_set_bits(chip,
					AB8500_GPIO_ALTFUN_REG,
					af.alt_bit1,
					!!(af.alta_val && BIT(0)));
			if (af.alt_bit2 != UNUSED)
				ret = abx500_gpio_set_bits(chip,
					AB8500_GPIO_ALTFUN_REG,
					af.alt_bit2,
					!!(af.alta_val && BIT(1)));
		} else
			ret = abx500_gpio_set_bits(chip, AB8500_GPIO_SEL1_REG,
					offset, 1);
		break;
	case ABX500_ALT_B:
		ret = abx500_gpio_set_bits(chip, AB8500_GPIO_SEL1_REG,
				offset, 0);
		ret = abx500_gpio_set_bits(chip, AB8500_GPIO_ALTFUN_REG,
				af.alt_bit1, !!(af.altb_val && BIT(0)));
		if (af.alt_bit2 != UNUSED)
			ret = abx500_gpio_set_bits(chip,
					AB8500_GPIO_ALTFUN_REG,
					af.alt_bit2,
					!!(af.altb_val && BIT(1)));
		break;
	case ABX500_ALT_C:
		ret = abx500_gpio_set_bits(chip, AB8500_GPIO_SEL1_REG,
				offset, 0);
		ret = abx500_gpio_set_bits(chip, AB8500_GPIO_ALTFUN_REG,
				af.alt_bit2, !!(af.altc_val && BIT(0)));
		ret = abx500_gpio_set_bits(chip, AB8500_GPIO_ALTFUN_REG,
				af.alt_bit2, !!(af.altc_val && BIT(1)));
		break;

	default:
		dev_dbg(pct->dev, "unknow alt_setting %d\n", alt_setting);
		return -EINVAL;
	}
	return ret;
}

static u8 abx500_get_mode(struct pinctrl_dev *pctldev, struct gpio_chip *chip,
		unsigned gpio)
{
	u8 mode;
	bool bit_mode;
	bool alt_bit1;
	bool alt_bit2;
	struct abx500_pinctrl *pct = pinctrl_dev_get_drvdata(pctldev);
	struct alternate_functions af = pct->soc->alternate_functions[gpio];

	/*
	 * if gpiosel_bit is set to unused,
	 * it means no GPIO or special case
	 */
	if (af.gpiosel_bit == UNUSED)
		return ABX500_DEFAULT;

	/* read GpioSelx register */
	abx500_gpio_get_bit(chip, AB8500_GPIO_SEL1_REG + (gpio / 8),
			af.gpiosel_bit, &bit_mode);
	mode = bit_mode;

	/* sanity check */
	if ((af.alt_bit1 < UNUSED) || (af.alt_bit1 > 7) ||
	    (af.alt_bit2 < UNUSED) || (af.alt_bit2 > 7)) {
		dev_err(pct->dev,
			"alt_bitX value not in correct range (-1 to 7)\n");
		return -EINVAL;
	}
	/* if alt_bit2 is used, alt_bit1 must be used too */
	if ((af.alt_bit2 != UNUSED) && (af.alt_bit1 == UNUSED)) {
		dev_err(pct->dev,
			"if alt_bit2 is used, alt_bit1 can't be unused\n");
		return -EINVAL;
	}

	/* check if pin use AlternateFunction register */
	if ((af.alt_bit1 == UNUSED) && (af.alt_bit1 == UNUSED))
		return mode;
	/*
	 * if pin GPIOSEL bit is set and pin supports alternate function,
	 * it means DEFAULT mode
	 */
	if (mode)
		return ABX500_DEFAULT;
	/*
	 * pin use the AlternatFunction register
	 * read alt_bit1 value
	 */
	abx500_gpio_get_bit(chip, AB8500_GPIO_ALTFUN_REG,
			    af.alt_bit1, &alt_bit1);

	if (af.alt_bit2 != UNUSED)
		/* read alt_bit2 value */
		abx500_gpio_get_bit(chip, AB8500_GPIO_ALTFUN_REG, af.alt_bit2,
				&alt_bit2);
	else
		alt_bit2 = 0;

	mode = (alt_bit2 << 1) + alt_bit1;
	if (mode == af.alta_val)
		return ABX500_ALT_A;
	else if (mode == af.altb_val)
		return ABX500_ALT_B;
	else
		return ABX500_ALT_C;
}

#ifdef CONFIG_DEBUG_FS

#include <linux/seq_file.h>

static void abx500_gpio_dbg_show_one(struct seq_file *s,
	struct pinctrl_dev *pctldev, struct gpio_chip *chip,
	unsigned offset, unsigned gpio)
{
	struct abx500_pinctrl *pct = to_abx500_pinctrl(chip);
	const char *label = gpiochip_is_requested(chip, offset - 1);
	u8 gpio_offset = offset - 1;
	int mode = -1;
	bool is_out;
	bool pull;
	const char *modes[] = {
		[ABX500_DEFAULT]	= "default",
		[ABX500_ALT_A]		= "altA",
		[ABX500_ALT_B]		= "altB",
		[ABX500_ALT_C]		= "altC",
	};

	abx500_gpio_get_bit(chip, AB8500_GPIO_DIR1_REG, gpio_offset, &is_out);
	abx500_gpio_get_bit(chip, AB8500_GPIO_PUD1_REG, gpio_offset, &pull);

	if (pctldev)
		mode = abx500_get_mode(pctldev, chip, offset);

	seq_printf(s, " gpio-%-3d (%-20.20s) %-3s %-9s %s",
		   gpio, label ?: "(none)",
		   is_out ? "out" : "in ",
		   is_out ?
		   (chip->get
		   ? (chip->get(chip, offset) ? "hi" : "lo")
		   : "?  ")
		   : (pull ? "pull up" : "pull down"),
		   (mode < 0) ? "unknown" : modes[mode]);

	if (label && !is_out) {
		int irq = gpio_to_irq(gpio);
		struct irq_desc	*desc = irq_to_desc(irq);

		if (irq >= 0 && desc->action) {
			char *trigger;
			int irq_offset = irq - pct->irq_base;

			if (pct->rising & BIT(irq_offset))
				trigger = "edge-rising";
			else if (pct->falling & BIT(irq_offset))
				trigger = "edge-falling";
			else
				trigger = "edge-undefined";

			seq_printf(s, " irq-%d %s", irq, trigger);
		}
	}
}

static void abx500_gpio_dbg_show(struct seq_file *s, struct gpio_chip *chip)
{
	unsigned i;
	unsigned gpio = chip->base;
	struct abx500_pinctrl *pct = to_abx500_pinctrl(chip);
	struct pinctrl_dev *pctldev = pct->pctldev;

	for (i = 0; i < chip->ngpio; i++, gpio++) {
		/* On AB8500, there is no GPIO0, the first is the GPIO 1 */
		abx500_gpio_dbg_show_one(s, pctldev, chip, i + 1, gpio);
		seq_printf(s, "\n");
	}
}

#else
static inline void abx500_gpio_dbg_show_one(struct seq_file *s,
					 struct pinctrl_dev *pctldev,
					 struct gpio_chip *chip,
					 unsigned offset, unsigned gpio)
{
}
#define abx500_gpio_dbg_show	NULL
#endif

int abx500_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	int gpio = chip->base + offset;

	return pinctrl_request_gpio(gpio);
}

void abx500_gpio_free(struct gpio_chip *chip, unsigned offset)
{
	int gpio = chip->base + offset;

	pinctrl_free_gpio(gpio);
}

static struct gpio_chip abx500gpio_chip = {
	.label			= "abx500-gpio",
	.owner			= THIS_MODULE,
	.request		= abx500_gpio_request,
	.free			= abx500_gpio_free,
	.direction_input	= abx500_gpio_direction_input,
	.get			= abx500_gpio_get,
	.direction_output	= abx500_gpio_direction_output,
	.set			= abx500_gpio_set,
	.to_irq			= abx500_gpio_to_irq,
	.dbg_show		= abx500_gpio_dbg_show,
};

static unsigned int irq_to_rising(unsigned int irq)
{
	struct abx500_pinctrl *pct = irq_get_chip_data(irq);
	int offset = irq - pct->irq_base;
	int new_irq;

	new_irq = offset * pct->irq_gpio_factor
		+ pct->irq_gpio_rising_offset
		+ pct->parent->irq_base;

	return new_irq;
}

static unsigned int irq_to_falling(unsigned int irq)
{
	struct abx500_pinctrl *pct = irq_get_chip_data(irq);
	int offset = irq - pct->irq_base;
	int new_irq;

	new_irq = offset * pct->irq_gpio_factor
		+ pct->irq_gpio_falling_offset
		+ pct->parent->irq_base;
	return new_irq;

}

static unsigned int rising_to_irq(unsigned int irq, void *dev)
{
	struct abx500_pinctrl *pct = dev;
	int offset, new_irq;

	offset = irq - pct->irq_gpio_rising_offset
		- pct->parent->irq_base;
	new_irq = (offset / pct->irq_gpio_factor)
		+ pct->irq_base;

	return new_irq;
}

static unsigned int falling_to_irq(unsigned int irq, void *dev)
{
	struct abx500_pinctrl *pct = dev;
	int offset, new_irq;

	offset = irq - pct->irq_gpio_falling_offset
		- pct->parent->irq_base;
	new_irq = (offset / pct->irq_gpio_factor)
		+ pct->irq_base;

	return new_irq;
}

/*
 * IRQ handler
 */

static irqreturn_t handle_rising(int irq, void *dev)
{

	handle_nested_irq(rising_to_irq(irq , dev));
	return IRQ_HANDLED;
}

static irqreturn_t handle_falling(int irq, void *dev)
{

	handle_nested_irq(falling_to_irq(irq, dev));
	return IRQ_HANDLED;
}

static void abx500_gpio_irq_lock(struct irq_data *data)
{
	struct abx500_pinctrl *pct = irq_data_get_irq_chip_data(data);
	mutex_lock(&pct->lock);
}

static void abx500_gpio_irq_sync_unlock(struct irq_data *data)
{
	struct abx500_pinctrl *pct = irq_data_get_irq_chip_data(data);
	unsigned int irq = data->irq;
	int offset = irq - pct->irq_base;
	bool rising = pct->rising & BIT(offset);
	bool falling = pct->falling & BIT(offset);
	int ret;

	switch (pct->irq_action)	{
	case STARTUP:
		if (rising)
			ret = request_threaded_irq(irq_to_rising(irq),
					NULL, handle_rising,
					IRQF_TRIGGER_RISING | IRQF_NO_SUSPEND,
					"abx500-gpio-r", pct);
		if (falling)
			ret = request_threaded_irq(irq_to_falling(irq),
				       NULL, handle_falling,
				       IRQF_TRIGGER_FALLING | IRQF_NO_SUSPEND,
				       "abx500-gpio-f", pct);
		break;
	case SHUTDOWN:
		if (rising)
			free_irq(irq_to_rising(irq), pct);
		if (falling)
			free_irq(irq_to_falling(irq), pct);
		break;
	case MASK:
		if (rising)
			disable_irq(irq_to_rising(irq));
		if (falling)
			disable_irq(irq_to_falling(irq));
		break;
	case UNMASK:
		if (rising)
			enable_irq(irq_to_rising(irq));
		if (falling)
			enable_irq(irq_to_falling(irq));
		break;
	case NONE:
		break;
	}
	pct->irq_action = NONE;
	pct->rising &= ~(BIT(offset));
	pct->falling &= ~(BIT(offset));
	mutex_unlock(&pct->lock);
}


static void abx500_gpio_irq_mask(struct irq_data *data)
{
	struct abx500_pinctrl *pct = irq_data_get_irq_chip_data(data);
	pct->irq_action = MASK;
}

static void abx500_gpio_irq_unmask(struct irq_data *data)
{
	struct abx500_pinctrl *pct =  irq_data_get_irq_chip_data(data);
	pct->irq_action = UNMASK;
}

static int abx500_gpio_irq_set_type(struct irq_data *data, unsigned int type)
{
	struct abx500_pinctrl *pct = irq_data_get_irq_chip_data(data);
	unsigned int irq = data->irq;
	int offset = irq - pct->irq_base;

	if (type == IRQ_TYPE_EDGE_BOTH) {
		pct->rising =  BIT(offset);
		pct->falling = BIT(offset);
	} else if (type == IRQ_TYPE_EDGE_RISING) {
		pct->rising =  BIT(offset);
	} else  {
		pct->falling = BIT(offset);
	}
	return 0;
}

static unsigned int abx500_gpio_irq_startup(struct irq_data *data)
{
	struct abx500_pinctrl *pct = irq_data_get_irq_chip_data(data);
	pct->irq_action = STARTUP;
	return 0;
}

static void abx500_gpio_irq_shutdown(struct irq_data *data)
{
	struct abx500_pinctrl *pct = irq_data_get_irq_chip_data(data);
	pct->irq_action = SHUTDOWN;
}

static struct irq_chip abx500_gpio_irq_chip = {
	.name			= "abx500-gpio",
	.irq_startup		= abx500_gpio_irq_startup,
	.irq_shutdown		= abx500_gpio_irq_shutdown,
	.irq_bus_lock		= abx500_gpio_irq_lock,
	.irq_bus_sync_unlock	= abx500_gpio_irq_sync_unlock,
	.irq_mask		= abx500_gpio_irq_mask,
	.irq_unmask		= abx500_gpio_irq_unmask,
	.irq_set_type		= abx500_gpio_irq_set_type,
};

static int abx500_gpio_irq_init(struct abx500_pinctrl *pct)
{
	u32 base = pct->irq_base;
	int irq;

	for (irq = base; irq < base + AB8500_NUM_VIR_GPIO_IRQ ; irq++) {
		irq_set_chip_data(irq, pct);
		irq_set_chip_and_handler(irq, &abx500_gpio_irq_chip,
				handle_simple_irq);
		irq_set_nested_thread(irq, 1);
#ifdef CONFIG_ARM
		set_irq_flags(irq, IRQF_VALID);
#else
		irq_set_noprobe(irq);
#endif
	}

	return 0;
}

static void abx500_gpio_irq_remove(struct abx500_pinctrl *pct)
{
	int base = pct->irq_base;
	int irq;

	for (irq = base; irq < base + AB8500_NUM_VIR_GPIO_IRQ; irq++) {
#ifdef CONFIG_ARM
		set_irq_flags(irq, 0);
#endif
		irq_set_chip_and_handler(irq, NULL, NULL);
		irq_set_chip_data(irq, NULL);
	}
}

static int abx500_pmx_get_funcs_cnt(struct pinctrl_dev *pctldev)
{
	struct abx500_pinctrl *pct = pinctrl_dev_get_drvdata(pctldev);

	return pct->soc->nfunctions;
}

static const char *abx500_pmx_get_func_name(struct pinctrl_dev *pctldev,
					 unsigned function)
{
	struct abx500_pinctrl *pct = pinctrl_dev_get_drvdata(pctldev);

	return pct->soc->functions[function].name;
}

static int abx500_pmx_get_func_groups(struct pinctrl_dev *pctldev,
				   unsigned function,
				   const char * const **groups,
				   unsigned * const num_groups)
{
	struct abx500_pinctrl *pct = pinctrl_dev_get_drvdata(pctldev);

	*groups = pct->soc->functions[function].groups;
	*num_groups = pct->soc->functions[function].ngroups;

	return 0;
}

static void abx500_disable_lazy_irq(struct gpio_chip *chip, unsigned gpio)
{
	struct abx500_pinctrl *pct = to_abx500_pinctrl(chip);
	int irq;
	int offset;
	bool rising;
	bool falling;

	/*
	 * check if gpio has interrupt capability and convert
	 * gpio number to irq
	 * On ABx5xx, there is no GPIO0, GPIO1 is the
	 * first one, so adjust gpio number
	 */
	gpio--;
	irq =  gpio_to_irq(gpio + chip->base);
	if (irq < 0)
		return;

	offset = irq - pct->irq_base;
	rising = pct->rising & BIT(offset);
	falling = pct->falling & BIT(offset);

	/* nothing to do ?*/
	if (!rising && !falling)
		return;

	if (rising) {
		disable_irq(irq_to_rising(irq));
		free_irq(irq_to_rising(irq), pct);
	}
	if (falling) {
		disable_irq(irq_to_falling(irq));
		free_irq(irq_to_falling(irq), pct);
	}
}

static int abx500_pmx_enable(struct pinctrl_dev *pctldev, unsigned function,
			  unsigned group)
{
	struct abx500_pinctrl *pct = pinctrl_dev_get_drvdata(pctldev);
	struct gpio_chip *chip = &pct->chip;
	const struct abx500_pingroup *g;
	int i;
	int ret = 0;

	g = &pct->soc->groups[group];
	if (g->altsetting < 0)
		return -EINVAL;

	dev_dbg(pct->dev, "enable group %s, %u pins\n", g->name, g->npins);

	for (i = 0; i < g->npins; i++) {
		dev_dbg(pct->dev, "setting pin %d to altsetting %d\n",
			g->pins[i], g->altsetting);

		abx500_disable_lazy_irq(chip, g->pins[i]);
		ret = abx500_set_mode(pctldev, chip, g->pins[i], g->altsetting);
	}
	return ret;
}

static void abx500_pmx_disable(struct pinctrl_dev *pctldev,
			    unsigned function, unsigned group)
{
	struct abx500_pinctrl *pct = pinctrl_dev_get_drvdata(pctldev);
	const struct abx500_pingroup *g;

	g = &pct->soc->groups[group];
	if (g->altsetting < 0)
		return;

	/* FIXME: poke out the mux, set the pin to some default state? */
	dev_dbg(pct->dev, "disable group %s, %u pins\n", g->name, g->npins);
}

int abx500_gpio_request_enable(struct pinctrl_dev *pctldev,
			    struct pinctrl_gpio_range *range,
			    unsigned offset)
{
	struct abx500_pinctrl *pct = pinctrl_dev_get_drvdata(pctldev);
	const struct abx500_pinrange *p;
	int ret;
	int i;

	/*
	 * Different ranges have different ways to enable GPIO function on a
	 * pin, so refer back to our local range type, where we handily define
	 * what altfunc enables GPIO for a certain pin.
	 */
	for (i = 0; i < pct->soc->gpio_num_ranges; i++) {
		p = &pct->soc->gpio_ranges[i];
		if ((offset >= p->offset) &&
		    (offset < (p->offset + p->npins)))
		  break;
	}

	if (i == pct->soc->gpio_num_ranges) {
		dev_err(pct->dev, "%s failed to locate range\n", __func__);
		return -ENODEV;
	}

	dev_dbg(pct->dev, "enable GPIO by altfunc %d at gpio %d\n",
		p->altfunc, offset);

	ret = abx500_set_mode(pct->pctldev, &pct->chip,
			      offset, p->altfunc);
	if (ret < 0) {
		dev_err(pct->dev, "%s setting altfunc failed\n", __func__);
		return ret;
	}

	return ret;
}

static void abx500_gpio_disable_free(struct pinctrl_dev *pctldev,
			   struct pinctrl_gpio_range *range,
			   unsigned offset)
{
}

static struct pinmux_ops abx500_pinmux_ops = {
	.get_functions_count = abx500_pmx_get_funcs_cnt,
	.get_function_name = abx500_pmx_get_func_name,
	.get_function_groups = abx500_pmx_get_func_groups,
	.enable = abx500_pmx_enable,
	.disable = abx500_pmx_disable,
	.gpio_request_enable = abx500_gpio_request_enable,
	.gpio_disable_free = abx500_gpio_disable_free,
};

static int abx500_get_groups_cnt(struct pinctrl_dev *pctldev)
{
	struct abx500_pinctrl *pct = pinctrl_dev_get_drvdata(pctldev);

	return pct->soc->ngroups;
}

static const char *abx500_get_group_name(struct pinctrl_dev *pctldev,
				       unsigned selector)
{
	struct abx500_pinctrl *pct = pinctrl_dev_get_drvdata(pctldev);

	return pct->soc->groups[selector].name;
}

static int abx500_get_group_pins(struct pinctrl_dev *pctldev,
		unsigned selector,
		const unsigned **pins,
		unsigned *num_pins)
{
	struct abx500_pinctrl *pct = pinctrl_dev_get_drvdata(pctldev);

	*pins = pct->soc->groups[selector].pins;
	*num_pins = pct->soc->groups[selector].npins;
	return 0;
}

static void abx500_pin_dbg_show(struct pinctrl_dev *pctldev,
		struct seq_file *s, unsigned offset)
{
	struct abx500_pinctrl *pct = pinctrl_dev_get_drvdata(pctldev);
	struct gpio_chip *chip = &pct->chip;

	abx500_gpio_dbg_show_one(s, pctldev, chip, offset,
				 chip->base + offset - 1);
}

static struct pinctrl_ops abx500_pinctrl_ops = {
	.get_groups_count = abx500_get_groups_cnt,
	.get_group_name = abx500_get_group_name,
	.get_group_pins = abx500_get_group_pins,
	.pin_dbg_show = abx500_pin_dbg_show,
};

int abx500_pin_config_get(struct pinctrl_dev *pctldev,
		       unsigned pin,
		       unsigned long *config)
{
	/* Not implemented */
	return -EINVAL;
}

int abx500_pin_config_set(struct pinctrl_dev *pctldev,
		       unsigned pin,
		       unsigned long config)
{
	struct abx500_pinctrl *pct = pinctrl_dev_get_drvdata(pctldev);
	struct pullud *pullud = pct->soc->pullud;
	struct gpio_chip *chip = &pct->chip;
	unsigned offset;
	int ret;
	enum pin_config_param param = pinconf_to_config_param(config);
	enum pin_config_param argument = pinconf_to_config_argument(config);

	dev_dbg(chip->dev, "pin %d [%#lx]: %s %s\n",
		pin, config, (param == PIN_CONFIG_OUTPUT) ? "output " : "input",
		(param == PIN_CONFIG_OUTPUT) ? (argument ? "high" : "low") :
		(argument ? "pull up" : "pull down"));
	/* on ABx500, there is no GPIO0, so adjust the offset */
	offset = pin - 1;

	switch (param) {
	case PIN_CONFIG_BIAS_PULL_DOWN:
		/*
		 * if argument = 1 set the pull down
		 * else clear the pull down
		 */
		ret = abx500_gpio_direction_input(chip, offset);
		/*
		 * Some chips only support pull down, while some actually
		 * support both pull up and pull down. Such chips have
		 * a "pullud" range specified for the pins that support
		 * both features. If the pin is not within that range, we
		 * fall back to the old bit set that only support pull down.
		 */
		if (pullud &&
		    pin >= pullud->first_pin &&
		    pin <= pullud->last_pin)
			ret = abx500_config_pull_updown(pct,
				pin,
				argument ? ABX500_GPIO_PULL_DOWN : ABX500_GPIO_PULL_NONE);
		else
			/* Chip only supports pull down */
			ret = abx500_gpio_set_bits(chip, AB8500_GPIO_PUD1_REG,
				offset, argument ? 0 : 1);
		break;
	case PIN_CONFIG_OUTPUT:
		ret = abx500_gpio_direction_output(chip, offset, argument);
		break;
	default:
		dev_err(chip->dev, "illegal configuration requested\n");
		return -EINVAL;
	}
	return ret;
}

static struct pinconf_ops abx500_pinconf_ops = {
	.pin_config_get = abx500_pin_config_get,
	.pin_config_set = abx500_pin_config_set,
};

static struct pinctrl_desc abx500_pinctrl_desc = {
	.name = "pinctrl-abx500",
	.pctlops = &abx500_pinctrl_ops,
	.pmxops = &abx500_pinmux_ops,
	.confops = &abx500_pinconf_ops,
	.owner = THIS_MODULE,
};

static int abx500_get_gpio_num(struct abx500_pinctrl_soc_data *soc)
{
	unsigned int lowest = 0;
	unsigned int highest = 0;
	unsigned int npins = 0;
	int i;

	/*
	 * Compute number of GPIOs from the last SoC gpio range descriptors
	 * These ranges may include "holes" but the GPIO number space shall
	 * still be homogeneous, so we need to detect and account for any
	 * such holes so that these are included in the number of GPIO pins.
	 */
	for (i = 0; i < soc->gpio_num_ranges; i++) {
		unsigned gstart;
		unsigned gend;
		const struct abx500_pinrange *p;

		p = &soc->gpio_ranges[i];
		gstart = p->offset;
		gend = p->offset + p->npins - 1;

		if (i == 0) {
			/* First iteration, set start values */
			lowest = gstart;
			highest = gend;
		} else {
			if (gstart < lowest)
				lowest = gstart;
			if (gend > highest)
				highest = gend;
		}
	}
	/* this gives the absolute number of pins */
	npins = highest - lowest + 1;
	return npins;
}

static int abx500_gpio_probe(struct platform_device *pdev)
{
	struct ab8500_platform_data *abx500_pdata =
				dev_get_platdata(pdev->dev.parent);
	struct abx500_gpio_platform_data *pdata;
	struct abx500_pinctrl *pct;
	const struct platform_device_id *platid = platform_get_device_id(pdev);
	int ret;
	int i;

	pdata = abx500_pdata->gpio;
	if (!pdata)	{
		dev_err(&pdev->dev, "gpio platform data missing\n");
		return -ENODEV;
	}

	pct = devm_kzalloc(&pdev->dev, sizeof(struct abx500_pinctrl),
				   GFP_KERNEL);
	if (pct == NULL) {
		dev_err(&pdev->dev,
			"failed to allocate memory for pct\n");
		return -ENOMEM;
	}

	pct->dev = &pdev->dev;
	pct->parent = dev_get_drvdata(pdev->dev.parent);
	pct->chip = abx500gpio_chip;
	pct->chip.dev = &pdev->dev;
	pct->chip.base = pdata->gpio_base;
	pct->irq_base = pdata->irq_base;

	/* initialize the lock */
	mutex_init(&pct->lock);

	/* Poke in other ASIC variants here */
	switch (platid->driver_data) {
	case PINCTRL_AB8500:
		abx500_pinctrl_ab8500_init(&pct->soc);
		break;
	case PINCTRL_AB8540:
		abx500_pinctrl_ab8540_init(&pct->soc);
		break;
	case PINCTRL_AB9540:
		abx500_pinctrl_ab9540_init(&pct->soc);
		break;
	case PINCTRL_AB8505:
		abx500_pinctrl_ab8505_init(&pct->soc);
		break;
	default:
		dev_err(&pdev->dev, "Unsupported pinctrl sub driver (%d)\n",
				(int) platid->driver_data);
		return -EINVAL;
	}

	if (!pct->soc) {
		dev_err(&pdev->dev, "Invalid SOC data\n");
		return -EINVAL;
	}

	pct->chip.ngpio = abx500_get_gpio_num(pct->soc);
	pct->irq_cluster = pct->soc->gpio_irq_cluster;
	pct->irq_cluster_size = pct->soc->ngpio_irq_cluster;
	pct->irq_gpio_rising_offset = pct->soc->irq_gpio_rising_offset;
	pct->irq_gpio_falling_offset = pct->soc->irq_gpio_falling_offset;
	pct->irq_gpio_factor = pct->soc->irq_gpio_factor;

	ret = abx500_gpio_irq_init(pct);
	if (ret)
		goto out_free;
	ret = gpiochip_add(&pct->chip);
	if (ret) {
		dev_err(&pdev->dev, "unable to add gpiochip: %d\n",
				ret);
		goto out_rem_irq;
	}
	dev_info(&pdev->dev, "added gpiochip\n");

	abx500_pinctrl_desc.pins = pct->soc->pins;
	abx500_pinctrl_desc.npins = pct->soc->npins;
	pct->pctldev = pinctrl_register(&abx500_pinctrl_desc, &pdev->dev, pct);
	if (!pct->pctldev) {
		dev_err(&pdev->dev,
			"could not register abx500 pinctrl driver\n");
		goto out_rem_chip;
	}
	dev_info(&pdev->dev, "registered pin controller\n");

	/* We will handle a range of GPIO pins */
	for (i = 0; i < pct->soc->gpio_num_ranges; i++) {
		const struct abx500_pinrange *p = &pct->soc->gpio_ranges[i];

		ret = gpiochip_add_pin_range(&pct->chip,
					dev_name(&pdev->dev),
					p->offset - 1, p->offset, p->npins);
		if (ret < 0)
			return ret;
	}

	platform_set_drvdata(pdev, pct);
	dev_info(&pdev->dev, "initialized abx500 pinctrl driver\n");

	return 0;

out_rem_chip:
	ret = gpiochip_remove(&pct->chip);
	if (ret)
		dev_info(&pdev->dev, "failed to remove gpiochip\n");
out_rem_irq:
	abx500_gpio_irq_remove(pct);
out_free:
	mutex_destroy(&pct->lock);
	return ret;
}

/*
 * abx500_gpio_remove() - remove Ab8500-gpio driver
 * @pdev :	Platform device registered
 */
static int abx500_gpio_remove(struct platform_device *pdev)
{
	struct abx500_pinctrl *pct = platform_get_drvdata(pdev);
	int ret;

	ret = gpiochip_remove(&pct->chip);
	if (ret < 0) {
		dev_err(pct->dev, "unable to remove gpiochip: %d\n",
			ret);
		return ret;
	}

	mutex_destroy(&pct->lock);

	return 0;
}

static const struct platform_device_id abx500_pinctrl_id[] = {
	{ "pinctrl-ab8500", PINCTRL_AB8500 },
	{ "pinctrl-ab8540", PINCTRL_AB8540 },
	{ "pinctrl-ab9540", PINCTRL_AB9540 },
	{ "pinctrl-ab8505", PINCTRL_AB8505 },
	{ },
};

static struct platform_driver abx500_gpio_driver = {
	.driver = {
		.name = "abx500-gpio",
		.owner = THIS_MODULE,
	},
	.probe = abx500_gpio_probe,
	.remove = abx500_gpio_remove,
	.id_table = abx500_pinctrl_id,
};

static int __init abx500_gpio_init(void)
{
	return platform_driver_register(&abx500_gpio_driver);
}
core_initcall(abx500_gpio_init);

MODULE_AUTHOR("Patrice Chotard <patrice.chotard@st.com>");
MODULE_DESCRIPTION("Driver allows to use AxB5xx unused pins to be used as GPIO");
MODULE_ALIAS("platform:abx500-gpio");
MODULE_LICENSE("GPL v2");
