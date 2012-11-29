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
	struct pinctrl_gpio_range range;

	struct sh_pfc *pfc;

	struct pinmux_gpio **functions;
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

static int sh_pfc_config_function(struct sh_pfc *pfc, unsigned offset)
{
	if (sh_pfc_config_gpio(pfc, offset,
			       PINMUX_TYPE_FUNCTION,
			       GPIO_CFG_DRYRUN) != 0)
		return -EINVAL;

	if (sh_pfc_config_gpio(pfc, offset,
			       PINMUX_TYPE_FUNCTION,
			       GPIO_CFG_REQ) != 0)
		return -EINVAL;

	return 0;
}

static int sh_pfc_reconfig_pin(struct sh_pfc *pfc, unsigned offset,
			       int new_type)
{
	unsigned long flags;
	int pinmux_type;
	int ret = -EINVAL;

	spin_lock_irqsave(&pfc->lock, flags);

	pinmux_type = pfc->info->gpios[offset].flags & PINMUX_FLAG_TYPE;

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
		sh_pfc_config_gpio(pfc, offset, pinmux_type, GPIO_CFG_FREE);
		break;
	default:
		goto err;
	}

	/*
	 * Dry run
	 */
	if (sh_pfc_config_gpio(pfc, offset, new_type,
			       GPIO_CFG_DRYRUN) != 0)
		goto err;

	/*
	 * Request
	 */
	if (sh_pfc_config_gpio(pfc, offset, new_type,
			       GPIO_CFG_REQ) != 0)
		goto err;

	pfc->info->gpios[offset].flags &= ~PINMUX_FLAG_TYPE;
	pfc->info->gpios[offset].flags |= new_type;

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
	unsigned long flags;
	int ret, pinmux_type;

	spin_lock_irqsave(&pfc->lock, flags);

	pinmux_type = pfc->info->gpios[offset].flags & PINMUX_FLAG_TYPE;

	switch (pinmux_type) {
	case PINMUX_TYPE_FUNCTION:
		pr_notice_once("Use of GPIO API for function requests is "
			       "deprecated, convert to pinctrl\n");
		/* handle for now */
		ret = sh_pfc_config_function(pfc, offset);
		if (unlikely(ret < 0))
			goto err;

		break;
	case PINMUX_TYPE_GPIO:
	case PINMUX_TYPE_INPUT:
	case PINMUX_TYPE_OUTPUT:
		break;
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
	unsigned long flags;
	int pinmux_type;

	spin_lock_irqsave(&pfc->lock, flags);

	pinmux_type = pfc->info->gpios[offset].flags & PINMUX_FLAG_TYPE;

	sh_pfc_config_gpio(pfc, offset, pinmux_type, GPIO_CFG_FREE);

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

static int sh_pfc_pinconf_get(struct pinctrl_dev *pctldev, unsigned pin,
			      unsigned long *config)
{
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);
	struct sh_pfc *pfc = pmx->pfc;

	*config = pfc->info->gpios[pin].flags & PINMUX_FLAG_TYPE;

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

/* pinmux ranges -> pinctrl pin descs */
static int sh_pfc_map_gpios(struct sh_pfc *pfc, struct sh_pfc_pinctrl *pmx)
{
	int i;

	pmx->nr_pads = pfc->info->nr_gpios;

	pmx->pads = devm_kzalloc(pfc->dev, sizeof(*pmx->pads) * pmx->nr_pads,
				 GFP_KERNEL);
	if (unlikely(!pmx->pads)) {
		pmx->nr_pads = 0;
		return -ENOMEM;
	}

	for (i = 0; i < pmx->nr_pads; i++) {
		struct pinctrl_pin_desc *pin = pmx->pads + i;
		struct pinmux_gpio *gpio = pfc->info->gpios + i;

		pin->number = i;
		pin->name = gpio->name;

		/* XXX */
		if (unlikely(!gpio->enum_id))
			continue;

		if ((gpio->flags & PINMUX_FLAG_TYPE) == PINMUX_TYPE_FUNCTION)
			pmx->nr_functions++;
	}

	return 0;
}

static int sh_pfc_map_functions(struct sh_pfc *pfc, struct sh_pfc_pinctrl *pmx)
{
	int i, fn;

	pmx->functions = devm_kzalloc(pfc->dev, pmx->nr_functions *
				      sizeof(*pmx->functions), GFP_KERNEL);
	if (unlikely(!pmx->functions))
		return -ENOMEM;

	for (i = fn = 0; i < pmx->nr_pads; i++) {
		struct pinmux_gpio *gpio = pfc->info->gpios + i;

		if ((gpio->flags & PINMUX_FLAG_TYPE) == PINMUX_TYPE_FUNCTION)
			pmx->functions[fn++] = gpio;
	}

	return 0;
}

int sh_pfc_register_pinctrl(struct sh_pfc *pfc)
{
	struct sh_pfc_pinctrl *pmx;
	int ret;

	pmx = devm_kzalloc(pfc->dev, sizeof(*pmx), GFP_KERNEL);
	if (unlikely(!pmx))
		return -ENOMEM;

	pmx->pfc = pfc;
	pfc->pinctrl = pmx;

	ret = sh_pfc_map_gpios(pfc, pmx);
	if (unlikely(ret != 0))
		return ret;

	ret = sh_pfc_map_functions(pfc, pmx);
	if (unlikely(ret != 0))
		return ret;

	pmx->pctl_desc.name = DRV_NAME;
	pmx->pctl_desc.owner = THIS_MODULE;
	pmx->pctl_desc.pctlops = &sh_pfc_pinctrl_ops;
	pmx->pctl_desc.pmxops = &sh_pfc_pinmux_ops;
	pmx->pctl_desc.confops = &sh_pfc_pinconf_ops;
	pmx->pctl_desc.pins = pmx->pads;
	pmx->pctl_desc.npins = pmx->nr_pads;

	pmx->pctl = pinctrl_register(&pmx->pctl_desc, pfc->dev, pmx);
	if (IS_ERR(pmx->pctl))
		return PTR_ERR(pmx->pctl);

	pmx->range.name = DRV_NAME,
	pmx->range.id = 0;
	pmx->range.npins = pfc->info->nr_gpios;
	pmx->range.base = 0;
	pmx->range.pin_base = 0;

	pinctrl_add_gpio_range(pmx->pctl, &pmx->range);

	return 0;
}

int sh_pfc_unregister_pinctrl(struct sh_pfc *pfc)
{
	struct sh_pfc_pinctrl *pmx = pfc->pinctrl;

	pinctrl_unregister(pmx->pctl);

	pfc->pinctrl = NULL;
	return 0;
}
