// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for Broadcom brcmstb GPIO units (pinctrl only)
 *
 * Copyright (C) 2024-2025 Ivan T. Ivanov, Andrea della Porta
 * Copyright (C) 2021-3 Raspberry Pi Ltd.
 * Copyright (C) 2012 Chris Boot, Simon Arlott, Stephen Warren
 *
 * Based heavily on the BCM2835 GPIO & pinctrl driver, which was inspired by:
 * pinctrl-nomadik.c, please see original file for copyright information
 * pinctrl-tegra.c, please see original file for copyright information
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/cleanup.h>

#include "pinctrl-brcmstb.h"

#define BRCMSTB_PULL_NONE	0
#define BRCMSTB_PULL_DOWN	1
#define BRCMSTB_PULL_UP		2
#define BRCMSTB_PULL_MASK	0x3

#define BIT_TO_REG(b)		(((b) >> 5) << 2)
#define BIT_TO_SHIFT(b)		((b) & 0x1f)

struct brcmstb_pinctrl {
	struct device *dev;
	void __iomem *base;
	struct pinctrl_dev *pctl_dev;
	struct pinctrl_desc pctl_desc;
	const struct pin_regs *pin_regs;
	const struct brcmstb_pin_funcs *pin_funcs;
	const char * const *func_names;
	unsigned int func_count;
	unsigned int func_gpio;
	const char *const *gpio_groups;
	struct pinctrl_gpio_range gpio_range;
	/* Protect FSEL registers */
	spinlock_t fsel_lock;
};

static unsigned int brcmstb_pinctrl_fsel_get(struct brcmstb_pinctrl *pc,
					     unsigned int pin)
{
	u32 bit = pc->pin_regs[pin].mux_bit;
	unsigned int func;
	int fsel;
	u32 val;

	if (!bit)
		return pc->func_gpio;

	bit &= ~MUX_BIT_VALID;

	val = readl(pc->base + BIT_TO_REG(bit));
	fsel = (val >> BIT_TO_SHIFT(bit)) & pc->pin_funcs[pin].func_mask;
	func = pc->pin_funcs[pin].funcs[fsel];

	if (func >= pc->func_count)
		func = fsel;

	dev_dbg(pc->dev, "get %04x: %08x (%u => %s)\n",
		BIT_TO_REG(bit), val, pin,
		pc->func_names[func]);

	return func;
}

static int brcmstb_pinctrl_fsel_set(struct brcmstb_pinctrl *pc,
				    unsigned int pin, unsigned int func)
{
	u32 bit = pc->pin_regs[pin].mux_bit, val, fsel_mask;
	const u8 *pin_funcs;
	int fsel;
	int cur;
	int i;

	if (!bit || func >= pc->func_count)
		return -EINVAL;

	bit &= ~MUX_BIT_VALID;

	fsel = pc->pin_funcs[pin].n_funcs + 1;
	fsel_mask = pc->pin_funcs[pin].func_mask;

	if (func >= fsel) {
		/* Convert to an fsel number */
		pin_funcs = pc->pin_funcs[pin].funcs;
		for (i = 1; i < fsel; i++) {
			if (pin_funcs[i - 1] == func) {
				fsel = i;
				break;
			}
		}
	} else {
		fsel = func;
	}

	if (fsel >= pc->pin_funcs[pin].n_funcs + 1)
		return -EINVAL;

	guard(spinlock_irqsave)(&pc->fsel_lock);

	val = readl(pc->base + BIT_TO_REG(bit));
	cur = (val >> BIT_TO_SHIFT(bit)) & fsel_mask;

	dev_dbg(pc->dev, "read %04x: %08x (%u => %s)\n",
		BIT_TO_REG(bit), val, pin,
		pc->func_names[cur]);

	if (cur != fsel) {
		val &= ~(fsel_mask << BIT_TO_SHIFT(bit));
		val |= fsel << BIT_TO_SHIFT(bit);

		dev_dbg(pc->dev, "write %04x: %08x (%u <= %s)\n",
			BIT_TO_REG(bit), val, pin,
			pc->func_names[fsel]);
		writel(val, pc->base + BIT_TO_REG(bit));
	}

	return 0;
}

static int brcmstb_pctl_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct brcmstb_pinctrl *pc = pinctrl_dev_get_drvdata(pctldev);

	return pc->pctl_desc.npins;
}

static const char *brcmstb_pctl_get_group_name(struct pinctrl_dev *pctldev,
					       unsigned int selector)
{
	struct brcmstb_pinctrl *pc = pinctrl_dev_get_drvdata(pctldev);

	return pc->gpio_groups[selector];
}

static int brcmstb_pctl_get_group_pins(struct pinctrl_dev *pctldev,
				       unsigned int selector,
				       const unsigned int **pins,
				       unsigned int *num_pins)
{
	struct brcmstb_pinctrl *pc = pinctrl_dev_get_drvdata(pctldev);

	*pins = &pc->pctl_desc.pins[selector].number;
	*num_pins = 1;

	return 0;
}

static void brcmstb_pctl_pin_dbg_show(struct pinctrl_dev *pctldev,
				      struct seq_file *s, unsigned int offset)
{
	struct brcmstb_pinctrl *pc = pinctrl_dev_get_drvdata(pctldev);
	unsigned int fsel = brcmstb_pinctrl_fsel_get(pc, offset);
	const char *fname = pc->func_names[fsel];

	seq_printf(s, "function %s", fname);
}

static void brcmstb_pctl_dt_free_map(struct pinctrl_dev *pctldev,
				     struct pinctrl_map *maps,
				     unsigned int num_maps)
{
	int i;

	for (i = 0; i < num_maps; i++)
		if (maps[i].type == PIN_MAP_TYPE_CONFIGS_PIN)
			kfree(maps[i].data.configs.configs);

	kfree(maps);
}

static const struct pinctrl_ops brcmstb_pctl_ops = {
	.get_groups_count = brcmstb_pctl_get_groups_count,
	.get_group_name = brcmstb_pctl_get_group_name,
	.get_group_pins = brcmstb_pctl_get_group_pins,
	.pin_dbg_show = brcmstb_pctl_pin_dbg_show,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_all,
	.dt_free_map = brcmstb_pctl_dt_free_map,
};

static int brcmstb_pmx_free(struct pinctrl_dev *pctldev, unsigned int offset)
{
	struct brcmstb_pinctrl *pc = pinctrl_dev_get_drvdata(pctldev);

	/* disable by setting to GPIO */
	return brcmstb_pinctrl_fsel_set(pc, offset, pc->func_gpio);
}

static int brcmstb_pmx_get_functions_count(struct pinctrl_dev *pctldev)
{
	struct brcmstb_pinctrl *pc = pinctrl_dev_get_drvdata(pctldev);

	return pc->func_count;
}

static const char *brcmstb_pmx_get_function_name(struct pinctrl_dev *pctldev,
						 unsigned int selector)
{
	struct brcmstb_pinctrl *pc = pinctrl_dev_get_drvdata(pctldev);

	return (selector < pc->func_count) ? pc->func_names[selector] : NULL;
}

static int brcmstb_pmx_get_function_groups(struct pinctrl_dev *pctldev,
					   unsigned int selector,
					   const char *const **groups,
					   unsigned *const num_groups)
{
	struct brcmstb_pinctrl *pc = pinctrl_dev_get_drvdata(pctldev);

	*groups = pc->gpio_groups;
	*num_groups = pc->pctl_desc.npins;

	return 0;
}

static int brcmstb_pmx_set(struct pinctrl_dev *pctldev,
			   unsigned int func_selector,
			   unsigned int group_selector)
{
	struct brcmstb_pinctrl *pc = pinctrl_dev_get_drvdata(pctldev);
	const struct pinctrl_desc *pctldesc = &pc->pctl_desc;
	const struct pinctrl_pin_desc *pindesc;

	if (group_selector >= pctldesc->npins)
		return -EINVAL;

	pindesc = &pctldesc->pins[group_selector];
	return brcmstb_pinctrl_fsel_set(pc, pindesc->number, func_selector);
}

static int brcmstb_pmx_gpio_request_enable(struct pinctrl_dev *pctldev,
					   struct pinctrl_gpio_range *range,
					   unsigned int pin)
{
	struct brcmstb_pinctrl *pc = pinctrl_dev_get_drvdata(pctldev);

	return brcmstb_pinctrl_fsel_set(pc, pin, pc->func_gpio);
}

static void brcmstb_pmx_gpio_disable_free(struct pinctrl_dev *pctldev,
					  struct pinctrl_gpio_range *range,
					  unsigned int offset)
{
	struct brcmstb_pinctrl *pc = pinctrl_dev_get_drvdata(pctldev);

	/* disable by setting to GPIO */
	(void)brcmstb_pinctrl_fsel_set(pc, offset, pc->func_gpio);
}

static bool brcmstb_pmx_function_is_gpio(struct pinctrl_dev *pctldev,
					 unsigned int selector)
{
	struct brcmstb_pinctrl *pc = pinctrl_dev_get_drvdata(pctldev);

	return pc->func_gpio == selector;
}

static const struct pinmux_ops brcmstb_pmx_ops = {
	.free = brcmstb_pmx_free,
	.get_functions_count = brcmstb_pmx_get_functions_count,
	.get_function_name = brcmstb_pmx_get_function_name,
	.get_function_groups = brcmstb_pmx_get_function_groups,
	.set_mux = brcmstb_pmx_set,
	.gpio_request_enable = brcmstb_pmx_gpio_request_enable,
	.gpio_disable_free = brcmstb_pmx_gpio_disable_free,
	.function_is_gpio = brcmstb_pmx_function_is_gpio,
	.strict = true,
};

static unsigned int brcmstb_pull_config_get(struct brcmstb_pinctrl *pc,
					    unsigned int pin)
{
	u32 bit = pc->pin_regs[pin].pad_bit, val;

	if (bit == PAD_BIT_INVALID)
		return BRCMSTB_PULL_NONE;

	val = readl(pc->base + BIT_TO_REG(bit));
	return (val >> BIT_TO_SHIFT(bit)) & BRCMSTB_PULL_MASK;
}

static int brcmstb_pull_config_set(struct brcmstb_pinctrl *pc,
				   unsigned int pin, unsigned int arg)
{
	u32 bit = pc->pin_regs[pin].pad_bit, val;

	if (bit == PAD_BIT_INVALID) {
		dev_warn(pc->dev, "Can't set pulls for %s\n",
			 pc->gpio_groups[pin]);
		return -EINVAL;
	}

	guard(spinlock_irqsave)(&pc->fsel_lock);

	val = readl(pc->base + BIT_TO_REG(bit));
	val &= ~(BRCMSTB_PULL_MASK << BIT_TO_SHIFT(bit));
	val |= (arg << BIT_TO_SHIFT(bit));
	writel(val, pc->base + BIT_TO_REG(bit));

	return 0;
}

static int brcmstb_pinconf_get(struct pinctrl_dev *pctldev, unsigned int pin,
			       unsigned long *config)
{
	struct brcmstb_pinctrl *pc = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param = pinconf_to_config_param(*config);
	u32 arg;

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		arg = (brcmstb_pull_config_get(pc, pin) == BRCMSTB_PULL_NONE);
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		arg = (brcmstb_pull_config_get(pc, pin) == BRCMSTB_PULL_DOWN);
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		arg = (brcmstb_pull_config_get(pc, pin) == BRCMSTB_PULL_UP);
		break;
	default:
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);

	return 0;
}

static int brcmstb_pinconf_set(struct pinctrl_dev *pctldev,
			       unsigned int pin, unsigned long *configs,
			       unsigned int num_configs)
{
	struct brcmstb_pinctrl *pc = pinctrl_dev_get_drvdata(pctldev);
	int ret = 0;
	u32 param;
	int i;

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);

		switch (param) {
		case PIN_CONFIG_BIAS_DISABLE:
			ret = brcmstb_pull_config_set(pc, pin, BRCMSTB_PULL_NONE);
			break;
		case PIN_CONFIG_BIAS_PULL_DOWN:
			ret = brcmstb_pull_config_set(pc, pin, BRCMSTB_PULL_DOWN);
			break;
		case PIN_CONFIG_BIAS_PULL_UP:
			ret = brcmstb_pull_config_set(pc, pin, BRCMSTB_PULL_UP);
			break;
		default:
			return -ENOTSUPP;
		}
	}

	return ret;
}

static const struct pinconf_ops brcmstb_pinconf_ops = {
	.is_generic = true,
	.pin_config_get = brcmstb_pinconf_get,
	.pin_config_set = brcmstb_pinconf_set,
};

int brcmstb_pinctrl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	const struct brcmstb_pdata *pdata;
	struct brcmstb_pinctrl *pc;
	const char **names;
	int num_pins, i;

	pdata = of_device_get_match_data(dev);

	pc = devm_kzalloc(dev, sizeof(*pc), GFP_KERNEL);
	if (!pc)
		return -ENOMEM;

	platform_set_drvdata(pdev, pc);
	pc->dev = dev;
	spin_lock_init(&pc->fsel_lock);

	pc->base = devm_of_iomap(dev, np, 0, NULL);
	if (IS_ERR(pc->base))
		return dev_err_probe(&pdev->dev, PTR_ERR(pc->base),
				     "Could not get IO memory\n");

	pc->pctl_desc = *pdata->pctl_desc;
	pc->pctl_desc.pctlops = &brcmstb_pctl_ops;
	pc->pctl_desc.pmxops = &brcmstb_pmx_ops;
	pc->pctl_desc.confops = &brcmstb_pinconf_ops;
	pc->pctl_desc.owner = THIS_MODULE;
	num_pins = pc->pctl_desc.npins;
	names = devm_kmalloc_array(dev, num_pins, sizeof(const char *),
				   GFP_KERNEL);
	if (!names)
		return -ENOMEM;

	for (i = 0; i < num_pins; i++)
		names[i] = pc->pctl_desc.pins[i].name;

	pc->gpio_groups = names;
	pc->pin_regs = pdata->pin_regs;
	pc->pin_funcs = pdata->pin_funcs;
	pc->func_count = pdata->func_count;
	pc->func_names = pdata->func_names;

	pc->pctl_dev = devm_pinctrl_register(dev, &pc->pctl_desc, pc);
	if (IS_ERR(pc->pctl_dev))
		return dev_err_probe(&pdev->dev, PTR_ERR(pc->pctl_dev),
				     "Failed to register pinctrl device\n");

	pc->gpio_range = *pdata->gpio_range;
	pinctrl_add_gpio_range(pc->pctl_dev, &pc->gpio_range);

	return 0;
}
EXPORT_SYMBOL(brcmstb_pinctrl_probe);

MODULE_AUTHOR("Phil Elwell");
MODULE_AUTHOR("Jonathan Bell");
MODULE_AUTHOR("Ivan T. Ivanov");
MODULE_AUTHOR("Andrea della Porta");
MODULE_DESCRIPTION("Broadcom brcmstb pinctrl driver");
MODULE_LICENSE("GPL");
