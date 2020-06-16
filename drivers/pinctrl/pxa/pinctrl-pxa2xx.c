// SPDX-License-Identifier: GPL-2.0-only
/*
 * Marvell PXA2xx family pin control
 *
 * Copyright (C) 2015 Robert Jarzmik
 */

#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/module.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "../pinctrl-utils.h"
#include "pinctrl-pxa2xx.h"

static int pxa2xx_pctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct pxa_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	return pctl->ngroups;
}

static const char *pxa2xx_pctrl_get_group_name(struct pinctrl_dev *pctldev,
					       unsigned tgroup)
{
	struct pxa_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct pxa_pinctrl_group *group = pctl->groups + tgroup;

	return group->name;
}

static int pxa2xx_pctrl_get_group_pins(struct pinctrl_dev *pctldev,
				       unsigned tgroup,
				       const unsigned **pins,
				       unsigned *num_pins)
{
	struct pxa_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct pxa_pinctrl_group *group = pctl->groups + tgroup;

	*pins = (unsigned *)&group->pin;
	*num_pins = 1;

	return 0;
}

static const struct pinctrl_ops pxa2xx_pctl_ops = {
#ifdef CONFIG_OF
	.dt_node_to_map		= pinconf_generic_dt_node_to_map_all,
	.dt_free_map		= pinctrl_utils_free_map,
#endif
	.get_groups_count	= pxa2xx_pctrl_get_groups_count,
	.get_group_name		= pxa2xx_pctrl_get_group_name,
	.get_group_pins		= pxa2xx_pctrl_get_group_pins,
};

static struct pxa_desc_function *
pxa_desc_by_func_group(struct pxa_pinctrl *pctl, const char *pin_name,
		       const char *func_name)
{
	int i;
	struct pxa_desc_function *df;

	for (i = 0; i < pctl->npins; i++) {
		const struct pxa_desc_pin *pin = pctl->ppins + i;

		if (!strcmp(pin->pin.name, pin_name))
			for (df = pin->functions; df->name; df++)
				if (!strcmp(df->name, func_name))
					return df;
	}

	return NULL;
}

static int pxa2xx_pmx_gpio_set_direction(struct pinctrl_dev *pctldev,
					 struct pinctrl_gpio_range *range,
					 unsigned pin,
					 bool input)
{
	struct pxa_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	unsigned long flags;
	uint32_t val;
	void __iomem *gpdr;

	gpdr = pctl->base_gpdr[pin / 32];
	dev_dbg(pctl->dev, "set_direction(pin=%d): dir=%d\n",
		pin, !input);

	spin_lock_irqsave(&pctl->lock, flags);

	val = readl_relaxed(gpdr);
	val = (val & ~BIT(pin % 32)) | (input ? 0 : BIT(pin % 32));
	writel_relaxed(val, gpdr);

	spin_unlock_irqrestore(&pctl->lock, flags);

	return 0;
}

static const char *pxa2xx_pmx_get_func_name(struct pinctrl_dev *pctldev,
					    unsigned function)
{
	struct pxa_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct pxa_pinctrl_function *pf = pctl->functions + function;

	return pf->name;
}

static int pxa2xx_get_functions_count(struct pinctrl_dev *pctldev)
{
	struct pxa_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	return pctl->nfuncs;
}

static int pxa2xx_pmx_get_func_groups(struct pinctrl_dev *pctldev,
				      unsigned function,
				      const char * const **groups,
				      unsigned * const num_groups)
{
	struct pxa_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct pxa_pinctrl_function *pf = pctl->functions + function;

	*groups = pf->groups;
	*num_groups = pf->ngroups;

	return 0;
}

static int pxa2xx_pmx_set_mux(struct pinctrl_dev *pctldev, unsigned function,
			      unsigned tgroup)
{
	struct pxa_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct pxa_pinctrl_group *group = pctl->groups + tgroup;
	struct pxa_desc_function *df;
	int pin, shift;
	unsigned long flags;
	void __iomem *gafr, *gpdr;
	u32 val;


	df = pxa_desc_by_func_group(pctl, group->name,
				    (pctl->functions + function)->name);
	if (!df)
		return -EINVAL;

	pin = group->pin;
	gafr = pctl->base_gafr[pin / 16];
	gpdr = pctl->base_gpdr[pin / 32];
	shift = (pin % 16) << 1;
	dev_dbg(pctl->dev, "set_mux(pin=%d): af=%d dir=%d\n",
		pin, df->muxval >> 1, df->muxval & 0x1);

	spin_lock_irqsave(&pctl->lock, flags);

	val = readl_relaxed(gafr);
	val = (val & ~(0x3 << shift)) | ((df->muxval >> 1) << shift);
	writel_relaxed(val, gafr);

	val = readl_relaxed(gpdr);
	val = (val & ~BIT(pin % 32)) | ((df->muxval & 1) ? BIT(pin % 32) : 0);
	writel_relaxed(val, gpdr);

	spin_unlock_irqrestore(&pctl->lock, flags);

	return 0;
}
static const struct pinmux_ops pxa2xx_pinmux_ops = {
	.get_functions_count = pxa2xx_get_functions_count,
	.get_function_name = pxa2xx_pmx_get_func_name,
	.get_function_groups = pxa2xx_pmx_get_func_groups,
	.set_mux = pxa2xx_pmx_set_mux,
	.gpio_set_direction = pxa2xx_pmx_gpio_set_direction,
};

static int pxa2xx_pconf_group_get(struct pinctrl_dev *pctldev,
				  unsigned group,
				  unsigned long *config)
{
	struct pxa_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct pxa_pinctrl_group *g = pctl->groups + group;
	unsigned long flags;
	unsigned pin = g->pin;
	void __iomem *pgsr = pctl->base_pgsr[pin / 32];
	u32 val;

	spin_lock_irqsave(&pctl->lock, flags);
	val = readl_relaxed(pgsr) & BIT(pin % 32);
	*config = val ? PIN_CONFIG_LOW_POWER_MODE : 0;
	spin_unlock_irqrestore(&pctl->lock, flags);

	dev_dbg(pctl->dev, "get sleep gpio state(pin=%d) %d\n",
		pin, !!val);
	return 0;
}

static int pxa2xx_pconf_group_set(struct pinctrl_dev *pctldev,
				  unsigned group,
				  unsigned long *configs,
				  unsigned num_configs)
{
	struct pxa_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct pxa_pinctrl_group *g = pctl->groups + group;
	unsigned long flags;
	unsigned pin = g->pin;
	void __iomem *pgsr = pctl->base_pgsr[pin / 32];
	int i, is_set = 0;
	u32 val;

	for (i = 0; i < num_configs; i++) {
		switch (pinconf_to_config_param(configs[i])) {
		case PIN_CONFIG_LOW_POWER_MODE:
			is_set = pinconf_to_config_argument(configs[i]);
			break;
		default:
			return -EINVAL;
		}
	}

	dev_dbg(pctl->dev, "set sleep gpio state(pin=%d) %d\n",
		pin, is_set);

	spin_lock_irqsave(&pctl->lock, flags);
	val = readl_relaxed(pgsr);
	val = (val & ~BIT(pin % 32)) | (is_set ? BIT(pin % 32) : 0);
	writel_relaxed(val, pgsr);
	spin_unlock_irqrestore(&pctl->lock, flags);

	return 0;
}

static const struct pinconf_ops pxa2xx_pconf_ops = {
	.pin_config_group_get	= pxa2xx_pconf_group_get,
	.pin_config_group_set	= pxa2xx_pconf_group_set,
	.is_generic		= true,
};

static struct pinctrl_desc pxa2xx_pinctrl_desc = {
	.confops	= &pxa2xx_pconf_ops,
	.pctlops	= &pxa2xx_pctl_ops,
	.pmxops		= &pxa2xx_pinmux_ops,
};

static const struct pxa_pinctrl_function *
pxa2xx_find_function(struct pxa_pinctrl *pctl, const char *fname,
		     const struct pxa_pinctrl_function *functions)
{
	const struct pxa_pinctrl_function *func;

	for (func = functions; func->name; func++)
		if (!strcmp(fname, func->name))
			return func;

	return NULL;
}

static int pxa2xx_build_functions(struct pxa_pinctrl *pctl)
{
	int i;
	struct pxa_pinctrl_function *functions;
	struct pxa_desc_function *df;

	/*
	 * Each pin can have at most 6 alternate functions, and 2 gpio functions
	 * which are common to each pin. As there are more than 2 pins without
	 * alternate function, 6 * npins is an absolute high limit of the number
	 * of functions.
	 */
	functions = devm_kcalloc(pctl->dev, pctl->npins * 6,
				 sizeof(*functions), GFP_KERNEL);
	if (!functions)
		return -ENOMEM;

	for (i = 0; i < pctl->npins; i++)
		for (df = pctl->ppins[i].functions; df->name; df++)
			if (!pxa2xx_find_function(pctl, df->name, functions))
				(functions + pctl->nfuncs++)->name = df->name;
	pctl->functions = devm_kmemdup(pctl->dev, functions,
				       pctl->nfuncs * sizeof(*functions),
				       GFP_KERNEL);
	if (!pctl->functions)
		return -ENOMEM;

	devm_kfree(pctl->dev, functions);
	return 0;
}

static int pxa2xx_build_groups(struct pxa_pinctrl *pctl)
{
	int i, j, ngroups;
	struct pxa_pinctrl_function *func;
	struct pxa_desc_function *df;
	char **gtmp;

	gtmp = devm_kmalloc_array(pctl->dev, pctl->npins, sizeof(*gtmp),
				  GFP_KERNEL);
	if (!gtmp)
		return -ENOMEM;

	for (i = 0; i < pctl->nfuncs; i++) {
		ngroups = 0;
		for (j = 0; j < pctl->npins; j++)
			for (df = pctl->ppins[j].functions; df->name;
			     df++)
				if (!strcmp(pctl->functions[i].name,
					    df->name))
					gtmp[ngroups++] = (char *)
						pctl->ppins[j].pin.name;
		func = pctl->functions + i;
		func->ngroups = ngroups;
		func->groups =
			devm_kmalloc_array(pctl->dev, ngroups,
					   sizeof(char *), GFP_KERNEL);
		if (!func->groups)
			return -ENOMEM;

		memcpy(func->groups, gtmp, ngroups * sizeof(*gtmp));
	}

	devm_kfree(pctl->dev, gtmp);
	return 0;
}

static int pxa2xx_build_state(struct pxa_pinctrl *pctl,
			      const struct pxa_desc_pin *ppins, int npins)
{
	struct pxa_pinctrl_group *group;
	struct pinctrl_pin_desc *pins;
	int ret, i;

	pctl->npins = npins;
	pctl->ppins = ppins;
	pctl->ngroups = npins;

	pctl->desc.npins = npins;
	pins = devm_kcalloc(pctl->dev, npins, sizeof(*pins), GFP_KERNEL);
	if (!pins)
		return -ENOMEM;

	pctl->desc.pins = pins;
	for (i = 0; i < npins; i++)
		pins[i] = ppins[i].pin;

	pctl->groups = devm_kmalloc_array(pctl->dev, pctl->ngroups,
					  sizeof(*pctl->groups), GFP_KERNEL);
	if (!pctl->groups)
		return -ENOMEM;

	for (i = 0; i < npins; i++) {
		group = pctl->groups + i;
		group->name = ppins[i].pin.name;
		group->pin = ppins[i].pin.number;
	}

	ret = pxa2xx_build_functions(pctl);
	if (ret)
		return ret;

	ret = pxa2xx_build_groups(pctl);
	if (ret)
		return ret;

	return 0;
}

int pxa2xx_pinctrl_init(struct platform_device *pdev,
			const struct pxa_desc_pin *ppins, int npins,
			void __iomem *base_gafr[], void __iomem *base_gpdr[],
			void __iomem *base_pgsr[])
{
	struct pxa_pinctrl *pctl;
	int ret, i, maxpin = 0;

	for (i = 0; i < npins; i++)
		maxpin = max_t(int, ppins[i].pin.number, maxpin);

	pctl = devm_kzalloc(&pdev->dev, sizeof(*pctl), GFP_KERNEL);
	if (!pctl)
		return -ENOMEM;
	pctl->base_gafr = devm_kcalloc(&pdev->dev, roundup(maxpin, 16),
				       sizeof(*pctl->base_gafr), GFP_KERNEL);
	pctl->base_gpdr = devm_kcalloc(&pdev->dev, roundup(maxpin, 32),
				       sizeof(*pctl->base_gpdr), GFP_KERNEL);
	pctl->base_pgsr = devm_kcalloc(&pdev->dev, roundup(maxpin, 32),
				       sizeof(*pctl->base_pgsr), GFP_KERNEL);
	if (!pctl->base_gafr || !pctl->base_gpdr || !pctl->base_pgsr)
		return -ENOMEM;

	platform_set_drvdata(pdev, pctl);
	spin_lock_init(&pctl->lock);

	pctl->dev = &pdev->dev;
	pctl->desc = pxa2xx_pinctrl_desc;
	pctl->desc.name = dev_name(&pdev->dev);
	pctl->desc.owner = THIS_MODULE;

	for (i = 0; i < roundup(maxpin, 16); i += 16)
		pctl->base_gafr[i / 16] = base_gafr[i / 16];
	for (i = 0; i < roundup(maxpin, 32); i += 32) {
		pctl->base_gpdr[i / 32] = base_gpdr[i / 32];
		pctl->base_pgsr[i / 32] = base_pgsr[i / 32];
	}

	ret = pxa2xx_build_state(pctl, ppins, npins);
	if (ret)
		return ret;

	pctl->pctl_dev = devm_pinctrl_register(&pdev->dev, &pctl->desc, pctl);
	if (IS_ERR(pctl->pctl_dev)) {
		dev_err(&pdev->dev, "couldn't register pinctrl driver\n");
		return PTR_ERR(pctl->pctl_dev);
	}

	dev_info(&pdev->dev, "initialized pxa2xx pinctrl driver\n");

	return 0;
}
EXPORT_SYMBOL_GPL(pxa2xx_pinctrl_init);

MODULE_AUTHOR("Robert Jarzmik <robert.jarzmik@free.fr>");
MODULE_DESCRIPTION("Marvell PXA2xx pinctrl driver");
MODULE_LICENSE("GPL v2");
