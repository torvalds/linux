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
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/interrupt.h>
#include <linux/bitops.h>
#include <linux/mfd/abx500.h>
#include <linux/mfd/abx500/ab8500.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/machine.h>

#include "pinctrl-abx500.h"
#include "../core.h"
#include "../pinconf.h"
#include "../pinctrl-utils.h"

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
#define AB8540_GPIO_PULL_UPDOWN_MASK	0x03
#define AB8540_GPIO_VINSEL_MASK	0x03
#define AB8540_GPIOX_VBAT_START	51
#define AB8540_GPIOX_VBAT_END	54

#define ABX500_GPIO_INPUT	0
#define ABX500_GPIO_OUTPUT	1

struct abx500_pinctrl {
	struct device *dev;
	struct pinctrl_dev *pctldev;
	struct abx500_pinctrl_soc_data *soc;
	struct gpio_chip chip;
	struct ab8500 *parent;
	struct abx500_gpio_irq_cluster *irq_cluster;
	int irq_cluster_size;
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
			"%s read reg =%x, offset=%x failed (%d)\n",
			__func__, reg, offset, ret);

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
		dev_err(pct->dev, "%s write reg, %x offset %x failed (%d)\n",
				__func__, reg, offset, ret);

	return ret;
}

/**
 * abx500_gpio_get() - Get the particular GPIO value
 * @chip:	Gpio device
 * @offset:	GPIO number to read
 */
static int abx500_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct abx500_pinctrl *pct = to_abx500_pinctrl(chip);
	bool bit;
	bool is_out;
	u8 gpio_offset = offset - 1;
	int ret;

	ret = abx500_gpio_get_bit(chip, AB8500_GPIO_DIR1_REG,
			gpio_offset, &is_out);
	if (ret < 0)
		goto out;

	if (is_out)
		ret = abx500_gpio_get_bit(chip, AB8500_GPIO_OUT1_REG,
				gpio_offset, &bit);
	else
		ret = abx500_gpio_get_bit(chip, AB8500_GPIO_IN1_REG,
				gpio_offset, &bit);
out:
	if (ret < 0) {
		dev_err(pct->dev, "%s failed (%d)\n", __func__, ret);
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
		dev_err(pct->dev, "%s write failed (%d)\n", __func__, ret);
}

static int abx500_get_pull_updown(struct abx500_pinctrl *pct, int offset,
				  enum abx500_gpio_pull_updown *pull_updown)
{
	u8 pos;
	u8 val;
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

	ret = abx500_get_register_interruptible(pct->dev,
			AB8500_MISC, AB8540_GPIO_PULL_UPDOWN_REG, &val);

	pos = (offset - pullud->first_pin) << 1;
	*pull_updown = (val >> pos) & AB8540_GPIO_PULL_UPDOWN_MASK;

out:
	if (ret < 0)
		dev_err(pct->dev, "%s failed (%d)\n", __func__, ret);

	return ret;
}

static int abx500_set_pull_updown(struct abx500_pinctrl *pct,
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
	pos = (offset - pullud->first_pin) << 1;

	ret = abx500_mask_and_set_register_interruptible(pct->dev,
			AB8500_MISC, AB8540_GPIO_PULL_UPDOWN_REG,
			AB8540_GPIO_PULL_UPDOWN_MASK << pos, val << pos);

out:
	if (ret < 0)
		dev_err(pct->dev, "%s failed (%d)\n", __func__, ret);

	return ret;
}

static bool abx500_pullud_supported(struct gpio_chip *chip, unsigned gpio)
{
	struct abx500_pinctrl *pct = to_abx500_pinctrl(chip);
	struct pullud *pullud = pct->soc->pullud;

	return (pullud &&
		gpio >= pullud->first_pin &&
		gpio <= pullud->last_pin);
}

static int abx500_gpio_direction_output(struct gpio_chip *chip,
					unsigned offset,
					int val)
{
	struct abx500_pinctrl *pct = to_abx500_pinctrl(chip);
	unsigned gpio;
	int ret;

	/* set direction as output */
	ret = abx500_gpio_set_bits(chip,
				AB8500_GPIO_DIR1_REG,
				offset,
				ABX500_GPIO_OUTPUT);
	if (ret < 0)
		goto out;

	/* disable pull down */
	ret = abx500_gpio_set_bits(chip,
				AB8500_GPIO_PUD1_REG,
				offset,
				ABX500_GPIO_PULL_NONE);
	if (ret < 0)
		goto out;

	/* if supported, disable both pull down and pull up */
	gpio = offset + 1;
	if (abx500_pullud_supported(chip, gpio)) {
		ret = abx500_set_pull_updown(pct,
				gpio,
				ABX500_GPIO_PULL_NONE);
	}
out:
	if (ret < 0) {
		dev_err(pct->dev, "%s failed (%d)\n", __func__, ret);
		return ret;
	}

	/* set the output as 1 or 0 */
	return abx500_gpio_set_bits(chip, AB8500_GPIO_OUT1_REG, offset, val);
}

static int abx500_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	/* set the register as input */
	return abx500_gpio_set_bits(chip,
				AB8500_GPIO_DIR1_REG,
				offset,
				ABX500_GPIO_INPUT);
}

static int abx500_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	struct abx500_pinctrl *pct = to_abx500_pinctrl(chip);
	/* The AB8500 GPIO numbers are off by one */
	int gpio = offset + 1;
	int hwirq;
	int i;

	for (i = 0; i < pct->irq_cluster_size; i++) {
		struct abx500_gpio_irq_cluster *cluster =
			&pct->irq_cluster[i];

		if (gpio >= cluster->start && gpio <= cluster->end) {
			/*
			 * The ABx500 GPIO's associated IRQs are clustered together
			 * throughout the interrupt numbers at irregular intervals.
			 * To solve this quandry, we have placed the read-in values
			 * into the cluster information table.
			 */
			hwirq = gpio - cluster->start + cluster->to_irq;
			return irq_create_mapping(pct->parent->domain, hwirq);
		}
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
			if (ret < 0)
				goto out;

			ret = abx500_gpio_set_bits(chip,
					AB8500_GPIO_ALTFUN_REG,
					af.alt_bit1,
					!!(af.alta_val & BIT(0)));
			if (ret < 0)
				goto out;

			if (af.alt_bit2 != UNUSED)
				ret = abx500_gpio_set_bits(chip,
					AB8500_GPIO_ALTFUN_REG,
					af.alt_bit2,
					!!(af.alta_val & BIT(1)));
		} else
			ret = abx500_gpio_set_bits(chip, AB8500_GPIO_SEL1_REG,
					offset, 1);
		break;

	case ABX500_ALT_B:
		ret = abx500_gpio_set_bits(chip, AB8500_GPIO_SEL1_REG,
				offset, 0);
		if (ret < 0)
			goto out;

		ret = abx500_gpio_set_bits(chip, AB8500_GPIO_ALTFUN_REG,
				af.alt_bit1, !!(af.altb_val & BIT(0)));
		if (ret < 0)
			goto out;

		if (af.alt_bit2 != UNUSED)
			ret = abx500_gpio_set_bits(chip,
					AB8500_GPIO_ALTFUN_REG,
					af.alt_bit2,
					!!(af.altb_val & BIT(1)));
		break;

	case ABX500_ALT_C:
		ret = abx500_gpio_set_bits(chip, AB8500_GPIO_SEL1_REG,
				offset, 0);
		if (ret < 0)
			goto out;

		ret = abx500_gpio_set_bits(chip, AB8500_GPIO_ALTFUN_REG,
				af.alt_bit2, !!(af.altc_val & BIT(0)));
		if (ret < 0)
			goto out;

		ret = abx500_gpio_set_bits(chip, AB8500_GPIO_ALTFUN_REG,
				af.alt_bit2, !!(af.altc_val & BIT(1)));
		break;

	default:
		dev_dbg(pct->dev, "unknow alt_setting %d\n", alt_setting);

		return -EINVAL;
	}
out:
	if (ret < 0)
		dev_err(pct->dev, "%s failed (%d)\n", __func__, ret);

	return ret;
}

static int abx500_get_mode(struct pinctrl_dev *pctldev, struct gpio_chip *chip,
			  unsigned gpio)
{
	u8 mode;
	bool bit_mode;
	bool alt_bit1;
	bool alt_bit2;
	struct abx500_pinctrl *pct = pinctrl_dev_get_drvdata(pctldev);
	struct alternate_functions af = pct->soc->alternate_functions[gpio];
	/* on ABx5xx, there is no GPIO0, so adjust the offset */
	unsigned offset = gpio - 1;
	int ret;

	/*
	 * if gpiosel_bit is set to unused,
	 * it means no GPIO or special case
	 */
	if (af.gpiosel_bit == UNUSED)
		return ABX500_DEFAULT;

	/* read GpioSelx register */
	ret = abx500_gpio_get_bit(chip, AB8500_GPIO_SEL1_REG + (offset / 8),
			af.gpiosel_bit, &bit_mode);
	if (ret < 0)
		goto out;

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
	if ((af.alt_bit1 == UNUSED) && (af.alt_bit2 == UNUSED))
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
	ret = abx500_gpio_get_bit(chip, AB8500_GPIO_ALTFUN_REG,
			    af.alt_bit1, &alt_bit1);
	if (ret < 0)
		goto out;

	if (af.alt_bit2 != UNUSED) {
		/* read alt_bit2 value */
		ret = abx500_gpio_get_bit(chip, AB8500_GPIO_ALTFUN_REG,
				af.alt_bit2,
				&alt_bit2);
		if (ret < 0)
			goto out;
	} else
		alt_bit2 = 0;

	mode = (alt_bit2 << 1) + alt_bit1;
	if (mode == af.alta_val)
		return ABX500_ALT_A;
	else if (mode == af.altb_val)
		return ABX500_ALT_B;
	else
		return ABX500_ALT_C;

out:
	dev_err(pct->dev, "%s failed (%d)\n", __func__, ret);
	return ret;
}

#ifdef CONFIG_DEBUG_FS

#include <linux/seq_file.h>

static void abx500_gpio_dbg_show_one(struct seq_file *s,
				     struct pinctrl_dev *pctldev,
				     struct gpio_chip *chip,
				     unsigned offset, unsigned gpio)
{
	struct abx500_pinctrl *pct = pinctrl_dev_get_drvdata(pctldev);
	const char *label = gpiochip_is_requested(chip, offset - 1);
	u8 gpio_offset = offset - 1;
	int mode = -1;
	bool is_out;
	bool pd;
	enum abx500_gpio_pull_updown pud = 0;
	int ret;

	const char *modes[] = {
		[ABX500_DEFAULT]	= "default",
		[ABX500_ALT_A]		= "altA",
		[ABX500_ALT_B]		= "altB",
		[ABX500_ALT_C]		= "altC",
	};

	const char *pull_up_down[] = {
		[ABX500_GPIO_PULL_DOWN]		= "pull down",
		[ABX500_GPIO_PULL_NONE]		= "pull none",
		[ABX500_GPIO_PULL_NONE + 1]	= "pull none",
		[ABX500_GPIO_PULL_UP]		= "pull up",
	};

	ret = abx500_gpio_get_bit(chip, AB8500_GPIO_DIR1_REG,
			gpio_offset, &is_out);
	if (ret < 0)
		goto out;

	seq_printf(s, " gpio-%-3d (%-20.20s) %-3s",
		   gpio, label ?: "(none)",
		   is_out ? "out" : "in ");

	if (!is_out) {
		if (abx500_pullud_supported(chip, offset)) {
			ret = abx500_get_pull_updown(pct, offset, &pud);
			if (ret < 0)
				goto out;

			seq_printf(s, " %-9s", pull_up_down[pud]);
		} else {
			ret = abx500_gpio_get_bit(chip, AB8500_GPIO_PUD1_REG,
					gpio_offset, &pd);
			if (ret < 0)
				goto out;

			seq_printf(s, " %-9s", pull_up_down[pd]);
		}
	} else
		seq_printf(s, " %-9s", chip->get(chip, offset) ? "hi" : "lo");

	if (pctldev)
		mode = abx500_get_mode(pctldev, chip, offset);

	seq_printf(s, " %s", (mode < 0) ? "unknown" : modes[mode]);

out:
	if (ret < 0)
		dev_err(pct->dev, "%s failed (%d)\n", __func__, ret);
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

static int abx500_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	int gpio = chip->base + offset;

	return pinctrl_request_gpio(gpio);
}

static void abx500_gpio_free(struct gpio_chip *chip, unsigned offset)
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

static int abx500_pmx_set(struct pinctrl_dev *pctldev, unsigned function,
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

		ret = abx500_set_mode(pctldev, chip, g->pins[i], g->altsetting);
	}

	if (ret < 0)
		dev_err(pct->dev, "%s failed (%d)\n", __func__, ret);

	return ret;
}

static int abx500_gpio_request_enable(struct pinctrl_dev *pctldev,
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
	if (ret < 0)
		dev_err(pct->dev, "%s setting altfunc failed\n", __func__);

	return ret;
}

static void abx500_gpio_disable_free(struct pinctrl_dev *pctldev,
				     struct pinctrl_gpio_range *range,
				     unsigned offset)
{
}

static const struct pinmux_ops abx500_pinmux_ops = {
	.get_functions_count = abx500_pmx_get_funcs_cnt,
	.get_function_name = abx500_pmx_get_func_name,
	.get_function_groups = abx500_pmx_get_func_groups,
	.set_mux = abx500_pmx_set,
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

static int abx500_dt_add_map_mux(struct pinctrl_map **map,
		unsigned *reserved_maps,
		unsigned *num_maps, const char *group,
		const char *function)
{
	if (*num_maps == *reserved_maps)
		return -ENOSPC;

	(*map)[*num_maps].type = PIN_MAP_TYPE_MUX_GROUP;
	(*map)[*num_maps].data.mux.group = group;
	(*map)[*num_maps].data.mux.function = function;
	(*num_maps)++;

	return 0;
}

static int abx500_dt_add_map_configs(struct pinctrl_map **map,
		unsigned *reserved_maps,
		unsigned *num_maps, const char *group,
		unsigned long *configs, unsigned num_configs)
{
	unsigned long *dup_configs;

	if (*num_maps == *reserved_maps)
		return -ENOSPC;

	dup_configs = kmemdup(configs, num_configs * sizeof(*dup_configs),
			      GFP_KERNEL);
	if (!dup_configs)
		return -ENOMEM;

	(*map)[*num_maps].type = PIN_MAP_TYPE_CONFIGS_PIN;

	(*map)[*num_maps].data.configs.group_or_pin = group;
	(*map)[*num_maps].data.configs.configs = dup_configs;
	(*map)[*num_maps].data.configs.num_configs = num_configs;
	(*num_maps)++;

	return 0;
}

static const char *abx500_find_pin_name(struct pinctrl_dev *pctldev,
					const char *pin_name)
{
	int i, pin_number;
	struct abx500_pinctrl *npct = pinctrl_dev_get_drvdata(pctldev);

	if (sscanf((char *)pin_name, "GPIO%d", &pin_number) == 1)
		for (i = 0; i < npct->soc->npins; i++)
			if (npct->soc->pins[i].number == pin_number)
				return npct->soc->pins[i].name;
	return NULL;
}

static int abx500_dt_subnode_to_map(struct pinctrl_dev *pctldev,
		struct device_node *np,
		struct pinctrl_map **map,
		unsigned *reserved_maps,
		unsigned *num_maps)
{
	int ret;
	const char *function = NULL;
	unsigned long *configs;
	unsigned int nconfigs = 0;
	bool has_config = 0;
	struct property *prop;
	const char *group, *gpio_name;
	struct device_node *np_config;

	ret = of_property_read_string(np, "ste,function", &function);
	if (ret >= 0) {
		ret = of_property_count_strings(np, "ste,pins");
		if (ret < 0)
			goto exit;

		ret = pinctrl_utils_reserve_map(pctldev, map, reserved_maps,
						num_maps, ret);
		if (ret < 0)
			goto exit;

		of_property_for_each_string(np, "ste,pins", prop, group) {
			ret = abx500_dt_add_map_mux(map, reserved_maps,
					num_maps, group, function);
			if (ret < 0)
				goto exit;
		}
	}

	ret = pinconf_generic_parse_dt_config(np, &configs, &nconfigs);
	if (nconfigs)
		has_config = 1;
	np_config = of_parse_phandle(np, "ste,config", 0);
	if (np_config) {
		ret = pinconf_generic_parse_dt_config(np_config, &configs,
				&nconfigs);
		if (ret)
			goto exit;
		has_config |= nconfigs;
	}
	if (has_config) {
		ret = of_property_count_strings(np, "ste,pins");
		if (ret < 0)
			goto exit;

		ret = pinctrl_utils_reserve_map(pctldev, map,
						reserved_maps,
						num_maps, ret);
		if (ret < 0)
			goto exit;

		of_property_for_each_string(np, "ste,pins", prop, group) {
			gpio_name = abx500_find_pin_name(pctldev, group);

			ret = abx500_dt_add_map_configs(map, reserved_maps,
					num_maps, gpio_name, configs, 1);
			if (ret < 0)
				goto exit;
		}
	}

exit:
	return ret;
}

static int abx500_dt_node_to_map(struct pinctrl_dev *pctldev,
				 struct device_node *np_config,
				 struct pinctrl_map **map, unsigned *num_maps)
{
	unsigned reserved_maps;
	struct device_node *np;
	int ret;

	reserved_maps = 0;
	*map = NULL;
	*num_maps = 0;

	for_each_child_of_node(np_config, np) {
		ret = abx500_dt_subnode_to_map(pctldev, np, map,
				&reserved_maps, num_maps);
		if (ret < 0) {
			pinctrl_utils_dt_free_map(pctldev, *map, *num_maps);
			return ret;
		}
	}

	return 0;
}

static const struct pinctrl_ops abx500_pinctrl_ops = {
	.get_groups_count = abx500_get_groups_cnt,
	.get_group_name = abx500_get_group_name,
	.get_group_pins = abx500_get_group_pins,
	.pin_dbg_show = abx500_pin_dbg_show,
	.dt_node_to_map = abx500_dt_node_to_map,
	.dt_free_map = pinctrl_utils_dt_free_map,
};

static int abx500_pin_config_get(struct pinctrl_dev *pctldev,
			  unsigned pin,
			  unsigned long *config)
{
	return -ENOSYS;
}

static int abx500_pin_config_set(struct pinctrl_dev *pctldev,
			  unsigned pin,
			  unsigned long *configs,
			  unsigned num_configs)
{
	struct abx500_pinctrl *pct = pinctrl_dev_get_drvdata(pctldev);
	struct gpio_chip *chip = &pct->chip;
	unsigned offset;
	int ret = -EINVAL;
	int i;
	enum pin_config_param param;
	enum pin_config_param argument;

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		argument = pinconf_to_config_argument(configs[i]);

		dev_dbg(chip->dev, "pin %d [%#lx]: %s %s\n",
			pin, configs[i],
			(param == PIN_CONFIG_OUTPUT) ? "output " : "input",
			(param == PIN_CONFIG_OUTPUT) ?
			(argument ? "high" : "low") :
			(argument ? "pull up" : "pull down"));

		/* on ABx500, there is no GPIO0, so adjust the offset */
		offset = pin - 1;

		switch (param) {
		case PIN_CONFIG_BIAS_DISABLE:
			ret = abx500_gpio_direction_input(chip, offset);
			if (ret < 0)
				goto out;
			/*
			 * Some chips only support pull down, while some
			 * actually support both pull up and pull down. Such
			 * chips have a "pullud" range specified for the pins
			 * that support both features. If the pin is not
			 * within that range, we fall back to the old bit set
			 * that only support pull down.
			 */
			if (abx500_pullud_supported(chip, pin))
				ret = abx500_set_pull_updown(pct,
					pin,
					ABX500_GPIO_PULL_NONE);
			else
				/* Chip only supports pull down */
				ret = abx500_gpio_set_bits(chip,
					AB8500_GPIO_PUD1_REG, offset,
					ABX500_GPIO_PULL_NONE);
			break;

		case PIN_CONFIG_BIAS_PULL_DOWN:
			ret = abx500_gpio_direction_input(chip, offset);
			if (ret < 0)
				goto out;
			/*
			 * if argument = 1 set the pull down
			 * else clear the pull down
			 * Some chips only support pull down, while some
			 * actually support both pull up and pull down. Such
			 * chips have a "pullud" range specified for the pins
			 * that support both features. If the pin is not
			 * within that range, we fall back to the old bit set
			 * that only support pull down.
			 */
			if (abx500_pullud_supported(chip, pin))
				ret = abx500_set_pull_updown(pct,
					pin,
					argument ? ABX500_GPIO_PULL_DOWN :
					ABX500_GPIO_PULL_NONE);
			else
				/* Chip only supports pull down */
				ret = abx500_gpio_set_bits(chip,
				AB8500_GPIO_PUD1_REG,
					offset,
					argument ? ABX500_GPIO_PULL_DOWN :
					ABX500_GPIO_PULL_NONE);
			break;

		case PIN_CONFIG_BIAS_PULL_UP:
			ret = abx500_gpio_direction_input(chip, offset);
			if (ret < 0)
				goto out;
			/*
			 * if argument = 1 set the pull up
			 * else clear the pull up
			 */
			ret = abx500_gpio_direction_input(chip, offset);
			/*
			 * Some chips only support pull down, while some
			 * actually support both pull up and pull down. Such
			 * chips have a "pullud" range specified for the pins
			 * that support both features. If the pin is not
			 * within that range, do nothing
			 */
			if (abx500_pullud_supported(chip, pin))
				ret = abx500_set_pull_updown(pct,
					pin,
					argument ? ABX500_GPIO_PULL_UP :
					ABX500_GPIO_PULL_NONE);
			break;

		case PIN_CONFIG_OUTPUT:
			ret = abx500_gpio_direction_output(chip, offset,
				argument);
			break;

		default:
			dev_err(chip->dev, "illegal configuration requested\n");
		}
	} /* for each config */
out:
	if (ret < 0)
		dev_err(pct->dev, "%s failed (%d)\n", __func__, ret);

	return ret;
}

static const struct pinconf_ops abx500_pinconf_ops = {
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

static const struct of_device_id abx500_gpio_match[] = {
	{ .compatible = "stericsson,ab8500-gpio", .data = (void *)PINCTRL_AB8500, },
	{ .compatible = "stericsson,ab8505-gpio", .data = (void *)PINCTRL_AB8505, },
	{ .compatible = "stericsson,ab8540-gpio", .data = (void *)PINCTRL_AB8540, },
	{ .compatible = "stericsson,ab9540-gpio", .data = (void *)PINCTRL_AB9540, },
	{ }
};

static int abx500_gpio_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *match;
	struct abx500_pinctrl *pct;
	unsigned int id = -1;
	int ret, err;
	int i;

	if (!np) {
		dev_err(&pdev->dev, "gpio dt node missing\n");
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
	pct->chip.base = -1; /* Dynamic allocation */

	match = of_match_device(abx500_gpio_match, &pdev->dev);
	if (!match) {
		dev_err(&pdev->dev, "gpio dt not matching\n");
		return -ENODEV;
	}
	id = (unsigned long)match->data;

	/* Poke in other ASIC variants here */
	switch (id) {
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
		dev_err(&pdev->dev, "Unsupported pinctrl sub driver (%d)\n", id);
		return -EINVAL;
	}

	if (!pct->soc) {
		dev_err(&pdev->dev, "Invalid SOC data\n");
		return -EINVAL;
	}

	pct->chip.ngpio = abx500_get_gpio_num(pct->soc);
	pct->irq_cluster = pct->soc->gpio_irq_cluster;
	pct->irq_cluster_size = pct->soc->ngpio_irq_cluster;

	ret = gpiochip_add(&pct->chip);
	if (ret) {
		dev_err(&pdev->dev, "unable to add gpiochip: %d\n", ret);
		return ret;
	}
	dev_info(&pdev->dev, "added gpiochip\n");

	abx500_pinctrl_desc.pins = pct->soc->pins;
	abx500_pinctrl_desc.npins = pct->soc->npins;
	pct->pctldev = pinctrl_register(&abx500_pinctrl_desc, &pdev->dev, pct);
	if (!pct->pctldev) {
		dev_err(&pdev->dev,
			"could not register abx500 pinctrl driver\n");
		ret = -EINVAL;
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
			goto out_rem_chip;
	}

	platform_set_drvdata(pdev, pct);
	dev_info(&pdev->dev, "initialized abx500 pinctrl driver\n");

	return 0;

out_rem_chip:
	err = gpiochip_remove(&pct->chip);
	if (err)
		dev_info(&pdev->dev, "failed to remove gpiochip\n");

	return ret;
}

/**
 * abx500_gpio_remove() - remove Ab8500-gpio driver
 * @pdev:	Platform device registered
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

	return 0;
}

static struct platform_driver abx500_gpio_driver = {
	.driver = {
		.name = "abx500-gpio",
		.owner = THIS_MODULE,
		.of_match_table = abx500_gpio_match,
	},
	.probe = abx500_gpio_probe,
	.remove = abx500_gpio_remove,
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
