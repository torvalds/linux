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

struct sh_pfc_pinctrl {
	struct pinctrl_dev *pctl;
	struct pinctrl_desc pctl_desc;

	struct sh_pfc *pfc;

	struct pinmux_func **functions;
	unsigned int nr_functions;

	struct pinctrl_pin_desc *pads;
	unsigned int nr_pads;
};

static int sh_pfc_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);

	return pmx->nr_pads;
}

static const char *sh_pfc_get_group_name(struct pinctrl_dev *pctldev,
					 unsigned selector)
{
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);

	return pmx->pads[selector].name;
}

static int sh_pfc_get_group_pins(struct pinctrl_dev *pctldev, unsigned group,
				 const unsigned **pins, unsigned *num_pins)
{
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);

	*pins = &pmx->pads[group].number;
	*num_pins = 1;

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

	return pmx->nr_functions;
}

static const char *sh_pfc_get_function_name(struct pinctrl_dev *pctldev,
					    unsigned selector)
{
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);

	return pmx->functions[selector]->name;
}

static int sh_pfc_get_function_groups(struct pinctrl_dev *pctldev, unsigned func,
				      const char * const **groups,
				      unsigned * const num_groups)
{
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);

	*groups = &pmx->functions[func]->name;
	*num_groups = 1;

	return 0;
}

static int sh_pfc_noop_enable(struct pinctrl_dev *pctldev, unsigned func,
			      unsigned group)
{
	return 0;
}

static void sh_pfc_noop_disable(struct pinctrl_dev *pctldev, unsigned func,
				unsigned group)
{
}

static int sh_pfc_reconfig_pin(struct sh_pfc *pfc, unsigned offset,
			       int new_type)
{
	struct sh_pfc_pin *pin = sh_pfc_get_pin(pfc, offset);
	unsigned int mark = pin->enum_id;
	unsigned long flags;
	int pinmux_type;
	int ret = -EINVAL;

	spin_lock_irqsave(&pfc->lock, flags);

	pinmux_type = pin->flags & PINMUX_FLAG_TYPE;

	/*
	 * See if the present config needs to first be de-configured.
	 */
	switch (pinmux_type) {
	case PINMUX_TYPE_GPIO:
		break;
	case PINMUX_TYPE_OUTPUT:
	case PINMUX_TYPE_INPUT:
	case PINMUX_TYPE_INPUT_PULLUP:
	case PINMUX_TYPE_INPUT_PULLDOWN:
		sh_pfc_config_mux(pfc, mark, pinmux_type, GPIO_CFG_FREE);
		break;
	default:
		goto err;
	}

	/*
	 * Dry run
	 */
	if (sh_pfc_config_mux(pfc, mark, new_type, GPIO_CFG_DRYRUN) != 0)
		goto err;

	/*
	 * Request
	 */
	if (sh_pfc_config_mux(pfc, mark, new_type, GPIO_CFG_REQ) != 0)
		goto err;

	pin->flags &= ~PINMUX_FLAG_TYPE;
	pin->flags |= new_type;

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
	struct sh_pfc_pin *pin = sh_pfc_get_pin(pfc, offset);
	unsigned long flags;
	int ret, pinmux_type;

	spin_lock_irqsave(&pfc->lock, flags);

	pinmux_type = pin->flags & PINMUX_FLAG_TYPE;

	switch (pinmux_type) {
	case PINMUX_TYPE_GPIO:
	case PINMUX_TYPE_INPUT:
	case PINMUX_TYPE_OUTPUT:
		break;
	case PINMUX_TYPE_FUNCTION:
	default:
		pr_err("Unsupported mux type (%d), bailing...\n", pinmux_type);
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
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);
	struct sh_pfc *pfc = pmx->pfc;
	struct sh_pfc_pin *pin = sh_pfc_get_pin(pfc, offset);
	unsigned long flags;
	int pinmux_type;

	spin_lock_irqsave(&pfc->lock, flags);

	pinmux_type = pin->flags & PINMUX_FLAG_TYPE;

	sh_pfc_config_mux(pfc, pin->enum_id, pinmux_type, GPIO_CFG_FREE);

	spin_unlock_irqrestore(&pfc->lock, flags);
}

static int sh_pfc_gpio_set_direction(struct pinctrl_dev *pctldev,
				     struct pinctrl_gpio_range *range,
				     unsigned offset, bool input)
{
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);
	int type = input ? PINMUX_TYPE_INPUT : PINMUX_TYPE_OUTPUT;

	return sh_pfc_reconfig_pin(pmx->pfc, offset, type);
}

static const struct pinmux_ops sh_pfc_pinmux_ops = {
	.get_functions_count	= sh_pfc_get_functions_count,
	.get_function_name	= sh_pfc_get_function_name,
	.get_function_groups	= sh_pfc_get_function_groups,
	.enable			= sh_pfc_noop_enable,
	.disable		= sh_pfc_noop_disable,
	.gpio_request_enable	= sh_pfc_gpio_request_enable,
	.gpio_disable_free	= sh_pfc_gpio_disable_free,
	.gpio_set_direction	= sh_pfc_gpio_set_direction,
};

static int sh_pfc_pinconf_get(struct pinctrl_dev *pctldev, unsigned _pin,
			      unsigned long *config)
{
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);
	struct sh_pfc *pfc = pmx->pfc;
	struct sh_pfc_pin *pin = sh_pfc_get_pin(pfc, _pin);

	*config = pin->flags & PINMUX_FLAG_TYPE;

	return 0;
}

static int sh_pfc_pinconf_set(struct pinctrl_dev *pctldev, unsigned pin,
			      unsigned long config)
{
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);

	/* Validate the new type */
	if (config >= PINMUX_FLAG_TYPE)
		return -EINVAL;

	return sh_pfc_reconfig_pin(pmx->pfc, pin, config);
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

	pmx->pads = devm_kzalloc(pfc->dev,
				 sizeof(*pmx->pads) * pfc->info->nr_pins,
				 GFP_KERNEL);
	if (unlikely(!pmx->pads))
		return -ENOMEM;

	for (i = 0, nr_pins = 0; i < nr_ranges; ++i) {
		const struct pinmux_range *range = &ranges[i];
		unsigned int number;

		for (number = range->begin; number <= range->end;
		     number++, nr_pins++) {
			struct pinctrl_pin_desc *pin = &pmx->pads[nr_pins];
			struct sh_pfc_pin *info = &pfc->info->pins[nr_pins];

			pin->number = number;
			pin->name = info->name;
		}
	}

	pfc->nr_pins = ranges[nr_ranges-1].end + 1;

	return nr_ranges;
}

static int sh_pfc_map_functions(struct sh_pfc *pfc, struct sh_pfc_pinctrl *pmx)
{
	int i, fn;

	for (i = 0; i < pfc->info->nr_func_gpios; i++) {
		struct pinmux_func *func = pfc->info->func_gpios + i;

		if (func->enum_id)
			pmx->nr_functions++;
	}

	pmx->functions = devm_kzalloc(pfc->dev, pmx->nr_functions *
				      sizeof(*pmx->functions), GFP_KERNEL);
	if (unlikely(!pmx->functions))
		return -ENOMEM;

	for (i = fn = 0; i < pfc->info->nr_func_gpios; i++) {
		struct pinmux_func *func = pfc->info->func_gpios + i;

		if (func->enum_id)
			pmx->functions[fn++] = func;
	}

	return 0;
}

int sh_pfc_register_pinctrl(struct sh_pfc *pfc)
{
	struct sh_pfc_pinctrl *pmx;
	int nr_ranges;
	int ret;

	pmx = devm_kzalloc(pfc->dev, sizeof(*pmx), GFP_KERNEL);
	if (unlikely(!pmx))
		return -ENOMEM;

	pmx->pfc = pfc;
	pfc->pinctrl = pmx;

	nr_ranges = sh_pfc_map_pins(pfc, pmx);
	if (unlikely(nr_ranges < 0))
		return nr_ranges;

	ret = sh_pfc_map_functions(pfc, pmx);
	if (unlikely(ret != 0))
		return ret;

	pmx->pctl_desc.name = DRV_NAME;
	pmx->pctl_desc.owner = THIS_MODULE;
	pmx->pctl_desc.pctlops = &sh_pfc_pinctrl_ops;
	pmx->pctl_desc.pmxops = &sh_pfc_pinmux_ops;
	pmx->pctl_desc.confops = &sh_pfc_pinconf_ops;
	pmx->pctl_desc.pins = pmx->pads;
	pmx->pctl_desc.npins = pfc->info->nr_pins;

	pmx->pctl = pinctrl_register(&pmx->pctl_desc, pfc->dev, pmx);
	if (IS_ERR(pmx->pctl))
		return PTR_ERR(pmx->pctl);

	return 0;
}

int sh_pfc_unregister_pinctrl(struct sh_pfc *pfc)
{
	struct sh_pfc_pinctrl *pmx = pfc->pinctrl;

	pinctrl_unregister(pmx->pctl);

	pfc->pinctrl = NULL;
	return 0;
}
