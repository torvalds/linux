// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel Tangier pinctrl driver
 *
 * Copyright (C) 2016, 2023 Intel Corporation
 *
 * Authors: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 *          Raag Jadav <raag.jadav@intel.com>
 */

#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/overflow.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include "../core.h"
#include "pinctrl-intel.h"
#include "pinctrl-tangier.h"

#define SLEW_OFFSET			0x000
#define BUFCFG_OFFSET			0x100
#define MISC_OFFSET			0x300

#define BUFCFG_PINMODE_SHIFT		0
#define BUFCFG_PINMODE_MASK		GENMASK(2, 0)
#define BUFCFG_PINMODE_GPIO		0
#define BUFCFG_PUPD_VAL_SHIFT		4
#define BUFCFG_PUPD_VAL_MASK		GENMASK(5, 4)
#define BUFCFG_PUPD_VAL_2K		0
#define BUFCFG_PUPD_VAL_20K		1
#define BUFCFG_PUPD_VAL_50K		2
#define BUFCFG_PUPD_VAL_910		3
#define BUFCFG_PU_EN			BIT(8)
#define BUFCFG_PD_EN			BIT(9)
#define BUFCFG_Px_EN_MASK		GENMASK(9, 8)
#define BUFCFG_SLEWSEL			BIT(10)
#define BUFCFG_OVINEN			BIT(12)
#define BUFCFG_OVINEN_EN		BIT(13)
#define BUFCFG_OVINEN_MASK		GENMASK(13, 12)
#define BUFCFG_OVOUTEN			BIT(14)
#define BUFCFG_OVOUTEN_EN		BIT(15)
#define BUFCFG_OVOUTEN_MASK		GENMASK(15, 14)
#define BUFCFG_INDATAOV_VAL		BIT(16)
#define BUFCFG_INDATAOV_EN		BIT(17)
#define BUFCFG_INDATAOV_MASK		GENMASK(17, 16)
#define BUFCFG_OUTDATAOV_VAL		BIT(18)
#define BUFCFG_OUTDATAOV_EN		BIT(19)
#define BUFCFG_OUTDATAOV_MASK		GENMASK(19, 18)
#define BUFCFG_OD_EN			BIT(21)

#define pin_to_bufno(f, p)		((p) - (f)->pin_base)

static const struct tng_family *tng_get_family(struct tng_pinctrl *tp,
					       unsigned int pin)
{
	const struct tng_family *family;
	unsigned int i;

	for (i = 0; i < tp->nfamilies; i++) {
		family = &tp->families[i];
		if (pin >= family->pin_base &&
		    pin < family->pin_base + family->npins)
			return family;
	}

	dev_warn(tp->dev, "failed to find family for pin %u\n", pin);
	return NULL;
}

static bool tng_buf_available(struct tng_pinctrl *tp, unsigned int pin)
{
	const struct tng_family *family;

	family = tng_get_family(tp, pin);
	if (!family)
		return false;

	return !family->protected;
}

static void __iomem *tng_get_bufcfg(struct tng_pinctrl *tp, unsigned int pin)
{
	const struct tng_family *family;
	unsigned int bufno;

	family = tng_get_family(tp, pin);
	if (!family)
		return NULL;

	bufno = pin_to_bufno(family, pin);
	return family->regs + BUFCFG_OFFSET + bufno * 4;
}

static int tng_read_bufcfg(struct tng_pinctrl *tp, unsigned int pin, u32 *value)
{
	void __iomem *bufcfg;

	if (!tng_buf_available(tp, pin))
		return -EBUSY;

	bufcfg = tng_get_bufcfg(tp, pin);
	*value = readl(bufcfg);

	return 0;
}

static void tng_update_bufcfg(struct tng_pinctrl *tp, unsigned int pin,
			      u32 bits, u32 mask)
{
	void __iomem *bufcfg;
	u32 value;

	bufcfg = tng_get_bufcfg(tp, pin);

	value = readl(bufcfg);
	value = (value & ~mask) | (bits & mask);
	writel(value, bufcfg);
}

static int tng_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct tng_pinctrl *tp = pinctrl_dev_get_drvdata(pctldev);

	return tp->ngroups;
}

static const char *tng_get_group_name(struct pinctrl_dev *pctldev,
				      unsigned int group)
{
	struct tng_pinctrl *tp = pinctrl_dev_get_drvdata(pctldev);

	return tp->groups[group].grp.name;
}

static int tng_get_group_pins(struct pinctrl_dev *pctldev, unsigned int group,
			      const unsigned int **pins, unsigned int *npins)
{
	struct tng_pinctrl *tp = pinctrl_dev_get_drvdata(pctldev);

	*pins = tp->groups[group].grp.pins;
	*npins = tp->groups[group].grp.npins;
	return 0;
}

static void tng_pin_dbg_show(struct pinctrl_dev *pctldev, struct seq_file *s,
			     unsigned int pin)
{
	struct tng_pinctrl *tp = pinctrl_dev_get_drvdata(pctldev);
	u32 value, mode;
	int ret;

	ret = tng_read_bufcfg(tp, pin, &value);
	if (ret) {
		seq_puts(s, "not available");
		return;
	}

	mode = (value & BUFCFG_PINMODE_MASK) >> BUFCFG_PINMODE_SHIFT;
	if (mode == BUFCFG_PINMODE_GPIO)
		seq_puts(s, "GPIO ");
	else
		seq_printf(s, "mode %d ", mode);

	seq_printf(s, "0x%08x", value);
}

static const struct pinctrl_ops tng_pinctrl_ops = {
	.get_groups_count = tng_get_groups_count,
	.get_group_name = tng_get_group_name,
	.get_group_pins = tng_get_group_pins,
	.pin_dbg_show = tng_pin_dbg_show,
};

static int tng_get_functions_count(struct pinctrl_dev *pctldev)
{
	struct tng_pinctrl *tp = pinctrl_dev_get_drvdata(pctldev);

	return tp->nfunctions;
}

static const char *tng_get_function_name(struct pinctrl_dev *pctldev,
					 unsigned int function)
{
	struct tng_pinctrl *tp = pinctrl_dev_get_drvdata(pctldev);

	return tp->functions[function].func.name;
}

static int tng_get_function_groups(struct pinctrl_dev *pctldev,
				   unsigned int function,
				   const char * const **groups,
				   unsigned int * const ngroups)
{
	struct tng_pinctrl *tp = pinctrl_dev_get_drvdata(pctldev);

	*groups = tp->functions[function].func.groups;
	*ngroups = tp->functions[function].func.ngroups;
	return 0;
}

static int tng_pinmux_set_mux(struct pinctrl_dev *pctldev,
			      unsigned int function,
			      unsigned int group)
{
	struct tng_pinctrl *tp = pinctrl_dev_get_drvdata(pctldev);
	const struct intel_pingroup *grp = &tp->groups[group];
	u32 bits = grp->mode << BUFCFG_PINMODE_SHIFT;
	u32 mask = BUFCFG_PINMODE_MASK;
	unsigned int i;

	/*
	 * All pins in the groups needs to be accessible and writable
	 * before we can enable the mux for this group.
	 */
	for (i = 0; i < grp->grp.npins; i++) {
		if (!tng_buf_available(tp, grp->grp.pins[i]))
			return -EBUSY;
	}

	guard(raw_spinlock_irqsave)(&tp->lock);

	/* Now enable the mux setting for each pin in the group */
	for (i = 0; i < grp->grp.npins; i++)
		tng_update_bufcfg(tp, grp->grp.pins[i], bits, mask);

	return 0;
}

static int tng_gpio_request_enable(struct pinctrl_dev *pctldev,
				   struct pinctrl_gpio_range *range,
				   unsigned int pin)
{
	struct tng_pinctrl *tp = pinctrl_dev_get_drvdata(pctldev);
	u32 bits = BUFCFG_PINMODE_GPIO << BUFCFG_PINMODE_SHIFT;
	u32 mask = BUFCFG_PINMODE_MASK;

	if (!tng_buf_available(tp, pin))
		return -EBUSY;

	guard(raw_spinlock_irqsave)(&tp->lock);

	tng_update_bufcfg(tp, pin, bits, mask);

	return 0;
}

static const struct pinmux_ops tng_pinmux_ops = {
	.get_functions_count = tng_get_functions_count,
	.get_function_name = tng_get_function_name,
	.get_function_groups = tng_get_function_groups,
	.set_mux = tng_pinmux_set_mux,
	.gpio_request_enable = tng_gpio_request_enable,
};

static int tng_config_get(struct pinctrl_dev *pctldev, unsigned int pin,
			  unsigned long *config)
{
	struct tng_pinctrl *tp = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param = pinconf_to_config_param(*config);
	u32 value, term;
	u16 arg = 0;
	int ret;

	ret = tng_read_bufcfg(tp, pin, &value);
	if (ret)
		return -ENOTSUPP;

	term = (value & BUFCFG_PUPD_VAL_MASK) >> BUFCFG_PUPD_VAL_SHIFT;

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		if (value & BUFCFG_Px_EN_MASK)
			return -EINVAL;
		break;

	case PIN_CONFIG_BIAS_PULL_UP:
		if ((value & BUFCFG_Px_EN_MASK) != BUFCFG_PU_EN)
			return -EINVAL;

		switch (term) {
		case BUFCFG_PUPD_VAL_910:
			arg = 910;
			break;
		case BUFCFG_PUPD_VAL_2K:
			arg = 2000;
			break;
		case BUFCFG_PUPD_VAL_20K:
			arg = 20000;
			break;
		case BUFCFG_PUPD_VAL_50K:
			arg = 50000;
			break;
		}

		break;

	case PIN_CONFIG_BIAS_PULL_DOWN:
		if ((value & BUFCFG_Px_EN_MASK) != BUFCFG_PD_EN)
			return -EINVAL;

		switch (term) {
		case BUFCFG_PUPD_VAL_910:
			arg = 910;
			break;
		case BUFCFG_PUPD_VAL_2K:
			arg = 2000;
			break;
		case BUFCFG_PUPD_VAL_20K:
			arg = 20000;
			break;
		case BUFCFG_PUPD_VAL_50K:
			arg = 50000;
			break;
		}

		break;

	case PIN_CONFIG_DRIVE_PUSH_PULL:
		if (value & BUFCFG_OD_EN)
			return -EINVAL;
		break;

	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		if (!(value & BUFCFG_OD_EN))
			return -EINVAL;
		break;

	case PIN_CONFIG_SLEW_RATE:
		if (value & BUFCFG_SLEWSEL)
			arg = 1;
		break;

	default:
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);
	return 0;
}

static int tng_config_set_pin(struct tng_pinctrl *tp, unsigned int pin,
			      unsigned long config)
{
	unsigned int param = pinconf_to_config_param(config);
	unsigned int arg = pinconf_to_config_argument(config);
	u32 mask, term, value = 0;

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		mask = BUFCFG_Px_EN_MASK | BUFCFG_PUPD_VAL_MASK;
		break;

	case PIN_CONFIG_BIAS_PULL_UP:
		switch (arg) {
		case 50000:
			term = BUFCFG_PUPD_VAL_50K;
			break;
		case 1: /* Set default strength value in case none is given */
		case 20000:
			term = BUFCFG_PUPD_VAL_20K;
			break;
		case 2000:
			term = BUFCFG_PUPD_VAL_2K;
			break;
		case 910:
			term = BUFCFG_PUPD_VAL_910;
			break;
		default:
			return -EINVAL;
		}

		mask = BUFCFG_Px_EN_MASK | BUFCFG_PUPD_VAL_MASK;
		value = BUFCFG_PU_EN | (term << BUFCFG_PUPD_VAL_SHIFT);
		break;

	case PIN_CONFIG_BIAS_PULL_DOWN:
		switch (arg) {
		case 50000:
			term = BUFCFG_PUPD_VAL_50K;
			break;
		case 1: /* Set default strength value in case none is given */
		case 20000:
			term = BUFCFG_PUPD_VAL_20K;
			break;
		case 2000:
			term = BUFCFG_PUPD_VAL_2K;
			break;
		case 910:
			term = BUFCFG_PUPD_VAL_910;
			break;
		default:
			return -EINVAL;
		}

		mask = BUFCFG_Px_EN_MASK | BUFCFG_PUPD_VAL_MASK;
		value = BUFCFG_PD_EN | (term << BUFCFG_PUPD_VAL_SHIFT);
		break;

	case PIN_CONFIG_DRIVE_PUSH_PULL:
		mask = BUFCFG_OD_EN;
		break;

	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		mask = BUFCFG_OD_EN;
		value = BUFCFG_OD_EN;
		break;

	case PIN_CONFIG_SLEW_RATE:
		mask = BUFCFG_SLEWSEL;
		if (arg)
			value = BUFCFG_SLEWSEL;
		break;

	default:
		return -EINVAL;
	}

	guard(raw_spinlock_irqsave)(&tp->lock);

	tng_update_bufcfg(tp, pin, value, mask);

	return 0;
}

static int tng_config_set(struct pinctrl_dev *pctldev, unsigned int pin,
			  unsigned long *configs, unsigned int nconfigs)
{
	struct tng_pinctrl *tp = pinctrl_dev_get_drvdata(pctldev);
	unsigned int i;
	int ret;

	if (!tng_buf_available(tp, pin))
		return -ENOTSUPP;

	for (i = 0; i < nconfigs; i++) {
		switch (pinconf_to_config_param(configs[i])) {
		case PIN_CONFIG_BIAS_DISABLE:
		case PIN_CONFIG_BIAS_PULL_UP:
		case PIN_CONFIG_BIAS_PULL_DOWN:
		case PIN_CONFIG_DRIVE_PUSH_PULL:
		case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		case PIN_CONFIG_SLEW_RATE:
			ret = tng_config_set_pin(tp, pin, configs[i]);
			if (ret)
				return ret;
			break;

		default:
			return -ENOTSUPP;
		}
	}

	return 0;
}

static int tng_config_group_get(struct pinctrl_dev *pctldev,
				unsigned int group, unsigned long *config)
{
	const unsigned int *pins;
	unsigned int npins;
	int ret;

	ret = tng_get_group_pins(pctldev, group, &pins, &npins);
	if (ret)
		return ret;

	return tng_config_get(pctldev, pins[0], config);
}

static int tng_config_group_set(struct pinctrl_dev *pctldev,
				unsigned int group, unsigned long *configs,
				unsigned int num_configs)
{
	const unsigned int *pins;
	unsigned int npins;
	int i, ret;

	ret = tng_get_group_pins(pctldev, group, &pins, &npins);
	if (ret)
		return ret;

	for (i = 0; i < npins; i++) {
		ret = tng_config_set(pctldev, pins[i], configs, num_configs);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct pinconf_ops tng_pinconf_ops = {
	.is_generic = true,
	.pin_config_get = tng_config_get,
	.pin_config_set = tng_config_set,
	.pin_config_group_get = tng_config_group_get,
	.pin_config_group_set = tng_config_group_set,
};

static const struct pinctrl_desc tng_pinctrl_desc = {
	.pctlops = &tng_pinctrl_ops,
	.pmxops = &tng_pinmux_ops,
	.confops = &tng_pinconf_ops,
	.owner = THIS_MODULE,
};

static int tng_pinctrl_probe(struct platform_device *pdev,
			     const struct tng_pinctrl *data)
{
	struct device *dev = &pdev->dev;
	struct tng_family *families;
	struct tng_pinctrl *tp;
	void __iomem *regs;
	unsigned int i;

	tp = devm_kmemdup(dev, data, sizeof(*data), GFP_KERNEL);
	if (!tp)
		return -ENOMEM;

	tp->dev = dev;
	raw_spin_lock_init(&tp->lock);

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	/*
	 * Make a copy of the families which we can use to hold pointers
	 * to the registers.
	 */
	families = devm_kmemdup_array(dev, tp->families, tp->nfamilies,
				      sizeof(*tp->families), GFP_KERNEL);
	if (!families)
		return -ENOMEM;

	/* Splice memory resource by chunk per family */
	for (i = 0; i < tp->nfamilies; i++) {
		struct tng_family *family = &families[i];

		family->regs = regs + family->barno * TNG_FAMILY_LEN;
	}

	tp->families = families;
	tp->pctldesc = tng_pinctrl_desc;
	tp->pctldesc.name = dev_name(dev);
	tp->pctldesc.pins = tp->pins;
	tp->pctldesc.npins = tp->npins;

	tp->pctldev = devm_pinctrl_register(dev, &tp->pctldesc, tp);
	if (IS_ERR(tp->pctldev))
		return dev_err_probe(dev, PTR_ERR(tp->pctldev),
				     "failed to register pinctrl driver\n");

	return 0;
}

int devm_tng_pinctrl_probe(struct platform_device *pdev)
{
	const struct tng_pinctrl *data;

	data = device_get_match_data(&pdev->dev);
	if (!data)
		return -ENODATA;

	return tng_pinctrl_probe(pdev, data);
}
EXPORT_SYMBOL_NS_GPL(devm_tng_pinctrl_probe, "PINCTRL_TANGIER");

MODULE_AUTHOR("Andy Shevchenko <andriy.shevchenko@linux.intel.com>");
MODULE_AUTHOR("Raag Jadav <raag.jadav@intel.com>");
MODULE_DESCRIPTION("Intel Tangier pinctrl driver");
MODULE_LICENSE("GPL");
