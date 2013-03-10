/*
 * SuperH Pin Function Controller pinmux support.
 *
 * Copyright (C) 2012  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#define DRV_NAME "sh-pfc"
#define pr_fmt(fmt) KBUILD_MODNAME " pinctrl: " fmt

#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "core.h"

struct sh_pfc_pin_config {
	u32 type;
};

struct sh_pfc_pinctrl {
	struct pinctrl_dev *pctl;
	struct pinctrl_desc pctl_desc;

	struct sh_pfc *pfc;

	struct pinctrl_pin_desc *pins;
	struct sh_pfc_pin_config *configs;
};

static int sh_pfc_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);

	return pmx->pfc->info->nr_groups;
}

static const char *sh_pfc_get_group_name(struct pinctrl_dev *pctldev,
					 unsigned selector)
{
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);

	return pmx->pfc->info->groups[selector].name;
}

static int sh_pfc_get_group_pins(struct pinctrl_dev *pctldev, unsigned selector,
				 const unsigned **pins, unsigned *num_pins)
{
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);

	*pins = pmx->pfc->info->groups[selector].pins;
	*num_pins = pmx->pfc->info->groups[selector].nr_pins;

	return 0;
}

static void sh_pfc_pin_dbg_show(struct pinctrl_dev *pctldev, struct seq_file *s,
				unsigned offset)
{
	seq_printf(s, "%s", DRV_NAME);
}

static const struct pinctrl_ops sh_pfc_pinctrl_ops = {
	.get_groups_count	= sh_pfc_get_groups_count,
	.get_group_name		= sh_pfc_get_group_name,
	.get_group_pins		= sh_pfc_get_group_pins,
	.pin_dbg_show		= sh_pfc_pin_dbg_show,
};

static int sh_pfc_get_functions_count(struct pinctrl_dev *pctldev)
{
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);

	return pmx->pfc->info->nr_functions;
}

static const char *sh_pfc_get_function_name(struct pinctrl_dev *pctldev,
					    unsigned selector)
{
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);

	return pmx->pfc->info->functions[selector].name;
}

static int sh_pfc_get_function_groups(struct pinctrl_dev *pctldev,
				      unsigned selector,
				      const char * const **groups,
				      unsigned * const num_groups)
{
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);

	*groups = pmx->pfc->info->functions[selector].groups;
	*num_groups = pmx->pfc->info->functions[selector].nr_groups;

	return 0;
}

static int sh_pfc_func_enable(struct pinctrl_dev *pctldev, unsigned selector,
			      unsigned group)
{
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);
	struct sh_pfc *pfc = pmx->pfc;
	const struct sh_pfc_pin_group *grp = &pfc->info->groups[group];
	unsigned long flags;
	unsigned int i;
	int ret = -EINVAL;

	spin_lock_irqsave(&pfc->lock, flags);

	for (i = 0; i < grp->nr_pins; ++i) {
		if (sh_pfc_config_mux(pfc, grp->mux[i], PINMUX_TYPE_FUNCTION))
			goto done;
	}

	ret = 0;

done:
	spin_unlock_irqrestore(&pfc->lock, flags);
	return ret;
}

static void sh_pfc_func_disable(struct pinctrl_dev *pctldev, unsigned selector,
				unsigned group)
{
}

static int sh_pfc_reconfig_pin(struct sh_pfc_pinctrl *pmx, unsigned offset,
			       int new_type)
{
	struct sh_pfc *pfc = pmx->pfc;
	int idx = sh_pfc_get_pin_index(pfc, offset);
	struct sh_pfc_pin_config *cfg = &pmx->configs[idx];
	struct sh_pfc_pin *pin = &pfc->info->pins[idx];
	unsigned int mark = pin->enum_id;
	unsigned long flags;
	int ret = -EINVAL;

	spin_lock_irqsave(&pfc->lock, flags);

	switch (cfg->type) {
	case PINMUX_TYPE_GPIO:
	case PINMUX_TYPE_OUTPUT:
	case PINMUX_TYPE_INPUT:
	case PINMUX_TYPE_INPUT_PULLUP:
	case PINMUX_TYPE_INPUT_PULLDOWN:
		break;
	default:
		goto err;
	}

	if (sh_pfc_config_mux(pfc, mark, new_type) != 0)
		goto err;

	cfg->type = new_type;

	ret = 0;

err:
	spin_unlock_irqrestore(&pfc->lock, flags);

	return ret;
}

static int sh_pfc_gpio_request_enable(struct pinctrl_dev *pctldev,
				      struct pinctrl_gpio_range *range,
				      unsigned offset)
{
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);
	struct sh_pfc *pfc = pmx->pfc;
	int idx = sh_pfc_get_pin_index(pfc, offset);
	struct sh_pfc_pin_config *cfg = &pmx->configs[idx];
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&pfc->lock, flags);

	switch (cfg->type) {
	case PINMUX_TYPE_GPIO:
	case PINMUX_TYPE_INPUT:
	case PINMUX_TYPE_OUTPUT:
		break;
	case PINMUX_TYPE_FUNCTION:
	default:
		pr_err("Unsupported mux type (%d), bailing...\n", cfg->type);
		ret = -ENOTSUPP;
		goto err;
	}

	ret = 0;

err:
	spin_unlock_irqrestore(&pfc->lock, flags);

	return ret;
}

static void sh_pfc_gpio_disable_free(struct pinctrl_dev *pctldev,
				     struct pinctrl_gpio_range *range,
				     unsigned offset)
{
}

static int sh_pfc_gpio_set_direction(struct pinctrl_dev *pctldev,
				     struct pinctrl_gpio_range *range,
				     unsigned offset, bool input)
{
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);
	int type = input ? PINMUX_TYPE_INPUT : PINMUX_TYPE_OUTPUT;

	return sh_pfc_reconfig_pin(pmx, offset, type);
}

static const struct pinmux_ops sh_pfc_pinmux_ops = {
	.get_functions_count	= sh_pfc_get_functions_count,
	.get_function_name	= sh_pfc_get_function_name,
	.get_function_groups	= sh_pfc_get_function_groups,
	.enable			= sh_pfc_func_enable,
	.disable		= sh_pfc_func_disable,
	.gpio_request_enable	= sh_pfc_gpio_request_enable,
	.gpio_disable_free	= sh_pfc_gpio_disable_free,
	.gpio_set_direction	= sh_pfc_gpio_set_direction,
};

static int sh_pfc_pinconf_get(struct pinctrl_dev *pctldev, unsigned _pin,
			      unsigned long *config)
{
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);
	struct sh_pfc *pfc = pmx->pfc;
	int idx = sh_pfc_get_pin_index(pfc, _pin);
	struct sh_pfc_pin_config *cfg = &pmx->configs[idx];

	*config = cfg->type;

	return 0;
}

static int sh_pfc_pinconf_set(struct pinctrl_dev *pctldev, unsigned pin,
			      unsigned long config)
{
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);

	/* Validate the new type */
	if (config >= PINMUX_FLAG_TYPE)
		return -EINVAL;

	return sh_pfc_reconfig_pin(pmx, pin, config);
}

static void sh_pfc_pinconf_dbg_show(struct pinctrl_dev *pctldev,
				    struct seq_file *s, unsigned pin)
{
	const char *pinmux_type_str[] = {
		[PINMUX_TYPE_NONE]		= "none",
		[PINMUX_TYPE_FUNCTION]		= "function",
		[PINMUX_TYPE_GPIO]		= "gpio",
		[PINMUX_TYPE_OUTPUT]		= "output",
		[PINMUX_TYPE_INPUT]		= "input",
		[PINMUX_TYPE_INPUT_PULLUP]	= "input bias pull up",
		[PINMUX_TYPE_INPUT_PULLDOWN]	= "input bias pull down",
	};
	unsigned long config;
	int rc;

	rc = sh_pfc_pinconf_get(pctldev, pin, &config);
	if (unlikely(rc != 0))
		return;

	seq_printf(s, " %s", pinmux_type_str[config]);
}

static const struct pinconf_ops sh_pfc_pinconf_ops = {
	.pin_config_get		= sh_pfc_pinconf_get,
	.pin_config_set		= sh_pfc_pinconf_set,
	.pin_config_dbg_show	= sh_pfc_pinconf_dbg_show,
};

/* PFC ranges -> pinctrl pin descs */
static int sh_pfc_map_pins(struct sh_pfc *pfc, struct sh_pfc_pinctrl *pmx)
{
	const struct pinmux_range *ranges;
	struct pinmux_range def_range;
	unsigned int nr_ranges;
	unsigned int nr_pins;
	unsigned int i;

	if (pfc->info->ranges == NULL) {
		def_range.begin = 0;
		def_range.end = pfc->info->nr_pins - 1;
		ranges = &def_range;
		nr_ranges = 1;
	} else {
		ranges = pfc->info->ranges;
		nr_ranges = pfc->info->nr_ranges;
	}

	pmx->pins = devm_kzalloc(pfc->dev,
				 sizeof(*pmx->pins) * pfc->info->nr_pins,
				 GFP_KERNEL);
	if (unlikely(!pmx->pins))
		return -ENOMEM;

	pmx->configs = devm_kzalloc(pfc->dev,
				    sizeof(*pmx->configs) * pfc->info->nr_pins,
				    GFP_KERNEL);
	if (unlikely(!pmx->configs))
		return -ENOMEM;

	for (i = 0, nr_pins = 0; i < nr_ranges; ++i) {
		const struct pinmux_range *range = &ranges[i];
		unsigned int number;

		for (number = range->begin; number <= range->end;
		     number++, nr_pins++) {
			struct sh_pfc_pin_config *cfg = &pmx->configs[nr_pins];
			struct pinctrl_pin_desc *pin = &pmx->pins[nr_pins];
			struct sh_pfc_pin *info = &pfc->info->pins[nr_pins];

			pin->number = number;
			pin->name = info->name;
			cfg->type = PINMUX_TYPE_GPIO;
		}
	}

	pfc->nr_pins = ranges[nr_ranges-1].end + 1;

	return nr_ranges;
}

int sh_pfc_register_pinctrl(struct sh_pfc *pfc)
{
	struct sh_pfc_pinctrl *pmx;
	int nr_ranges;

	pmx = devm_kzalloc(pfc->dev, sizeof(*pmx), GFP_KERNEL);
	if (unlikely(!pmx))
		return -ENOMEM;

	pmx->pfc = pfc;
	pfc->pinctrl = pmx;

	nr_ranges = sh_pfc_map_pins(pfc, pmx);
	if (unlikely(nr_ranges < 0))
		return nr_ranges;

	pmx->pctl_desc.name = DRV_NAME;
	pmx->pctl_desc.owner = THIS_MODULE;
	pmx->pctl_desc.pctlops = &sh_pfc_pinctrl_ops;
	pmx->pctl_desc.pmxops = &sh_pfc_pinmux_ops;
	pmx->pctl_desc.confops = &sh_pfc_pinconf_ops;
	pmx->pctl_desc.pins = pmx->pins;
	pmx->pctl_desc.npins = pfc->info->nr_pins;

	pmx->pctl = pinctrl_register(&pmx->pctl_desc, pfc->dev, pmx);
	if (pmx->pctl == NULL)
		return -EINVAL;

	return 0;
}

int sh_pfc_unregister_pinctrl(struct sh_pfc *pfc)
{
	struct sh_pfc_pinctrl *pmx = pfc->pinctrl;

	pinctrl_unregister(pmx->pctl);

	pfc->pinctrl = NULL;
	return 0;
}
