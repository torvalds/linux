// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 TOSHIBA CORPORATION
 * Copyright (c) 2020 Toshiba Electronic Devices & Storage Corporation
 * Copyright (c) 2020 Nobuhiro Iwamatsu <nobuhiro1.iwamatsu@toshiba.co.jp>
 */

#include <linux/init.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include "pinctrl-common.h"
#include "../core.h"
#include "../pinconf.h"
#include "../pinctrl-utils.h"

#define DSEL_MASK GENMASK(3, 0)

/* private data */
struct visconti_pinctrl {
	void __iomem *base;
	struct device *dev;
	struct pinctrl_dev *pctl;
	struct pinctrl_desc pctl_desc;

	const struct visconti_pinctrl_devdata  *devdata;

	spinlock_t lock; /* protect pinctrl register */
};

/* pinconf */
static int visconti_pin_config_set(struct pinctrl_dev *pctldev,
				  unsigned int _pin,
				  unsigned long *configs,
				  unsigned int num_configs)
{
	struct visconti_pinctrl *priv = pinctrl_dev_get_drvdata(pctldev);
	const struct visconti_desc_pin *pin = &priv->devdata->pins[_pin];
	enum pin_config_param param;
	unsigned int arg;
	int i, ret = 0;
	unsigned int val, set_val, pude_val;
	unsigned long flags;

	dev_dbg(priv->dev, "%s: pin = %d (%s)\n", __func__, _pin, pin->pin.name);

	spin_lock_irqsave(&priv->lock, flags);

	for (i = 0; i < num_configs; i++) {
		set_val = 0;
		pude_val = 0;

		param = pinconf_to_config_param(configs[i]);
		switch (param) {
		case PIN_CONFIG_BIAS_PULL_UP:
			set_val = 1;
			fallthrough;
		case PIN_CONFIG_BIAS_PULL_DOWN:
			/* update pudsel setting */
			val = readl(priv->base + pin->pudsel_offset);
			val &= ~BIT(pin->pud_shift);
			val |= set_val << pin->pud_shift;
			writel(val, priv->base + pin->pudsel_offset);
			pude_val = 1;
			fallthrough;
		case PIN_CONFIG_BIAS_DISABLE:
			/* update pude setting */
			val = readl(priv->base + pin->pude_offset);
			val &= ~BIT(pin->pud_shift);
			val |= pude_val << pin->pud_shift;
			writel(val, priv->base + pin->pude_offset);
			dev_dbg(priv->dev, "BIAS(%d): off = 0x%x val = 0x%x\n",
				param, pin->pude_offset, val);
			break;

		case PIN_CONFIG_DRIVE_STRENGTH:
			arg = pinconf_to_config_argument(configs[i]);
			dev_dbg(priv->dev, "DRV_STR arg = %d\n", arg);
			switch (arg) {
			case 2:
			case 4:
			case 8:
			case 16:
			case 24:
			case 32:
				/*
				 * I/O drive capacity setting:
				 * 2mA: 0
				 * 4mA: 1
				 * 8mA: 3
				 * 16mA: 7
				 * 24mA: 11
				 * 32mA: 15
				 */
				set_val = DIV_ROUND_CLOSEST(arg, 2) - 1;
				break;
			default:
				ret = -EINVAL;
				goto err;
			}
			/* update drive setting */
			val = readl(priv->base + pin->dsel_offset);
			val &= ~(DSEL_MASK << pin->dsel_shift);
			val |= set_val << pin->dsel_shift;
			writel(val, priv->base + pin->dsel_offset);
			break;

		default:
			ret = -EOPNOTSUPP;
			goto err;
		}
	}
err:
	spin_unlock_irqrestore(&priv->lock, flags);
	return ret;
}

static int visconti_pin_config_group_set(struct pinctrl_dev *pctldev,
					unsigned int selector,
					unsigned long *configs,
					unsigned int num_configs)
{
	struct visconti_pinctrl *priv = pinctrl_dev_get_drvdata(pctldev);
	const unsigned int *pins;
	unsigned int num_pins;
	int i, ret;

	pins = priv->devdata->groups[selector].pins;
	num_pins = priv->devdata->groups[selector].nr_pins;

	dev_dbg(priv->dev, "%s: select = %d, n_pin = %d, n_config = %d\n",
		__func__, selector, num_pins, num_configs);

	for (i = 0; i < num_pins; i++) {
		ret = visconti_pin_config_set(pctldev, pins[i],
					     configs, num_configs);
		if (ret)
			return ret;
	}

	return 0;
}
static const struct pinconf_ops visconti_pinconf_ops = {
	.is_generic			= true,
	.pin_config_set			= visconti_pin_config_set,
	.pin_config_group_set		= visconti_pin_config_group_set,
	.pin_config_config_dbg_show	= pinconf_generic_dump_config,
};

/* pinctrl */
static int visconti_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct visconti_pinctrl *priv = pinctrl_dev_get_drvdata(pctldev);

	return priv->devdata->nr_groups;
}

static const char *visconti_get_group_name(struct pinctrl_dev *pctldev,
					      unsigned int selector)
{
	struct visconti_pinctrl *priv = pinctrl_dev_get_drvdata(pctldev);

	return priv->devdata->groups[selector].name;
}

static int visconti_get_group_pins(struct pinctrl_dev *pctldev,
				      unsigned int selector,
				      const unsigned int **pins,
				      unsigned int *num_pins)
{
	struct visconti_pinctrl *priv = pinctrl_dev_get_drvdata(pctldev);

	*pins = priv->devdata->groups[selector].pins;
	*num_pins = priv->devdata->groups[selector].nr_pins;

	return 0;
}

static const struct pinctrl_ops visconti_pinctrl_ops = {
	.get_groups_count	= visconti_get_groups_count,
	.get_group_name		= visconti_get_group_name,
	.get_group_pins		= visconti_get_group_pins,
	.dt_node_to_map		= pinconf_generic_dt_node_to_map_group,
	.dt_free_map		= pinctrl_utils_free_map,
};

/* pinmux */
static int visconti_get_functions_count(struct pinctrl_dev *pctldev)
{
	struct visconti_pinctrl *priv = pinctrl_dev_get_drvdata(pctldev);

	return priv->devdata->nr_functions;
}

static const char *visconti_get_function_name(struct pinctrl_dev *pctldev,
					     unsigned int selector)
{
	struct visconti_pinctrl *priv = pinctrl_dev_get_drvdata(pctldev);

	return priv->devdata->functions[selector].name;
}

static int visconti_get_function_groups(struct pinctrl_dev *pctldev,
				       unsigned int selector,
				       const char * const **groups,
				       unsigned * const num_groups)
{
	struct visconti_pinctrl *priv = pinctrl_dev_get_drvdata(pctldev);

	*groups = priv->devdata->functions[selector].groups;
	*num_groups = priv->devdata->functions[selector].nr_groups;

	return 0;
}

static int visconti_set_mux(struct pinctrl_dev *pctldev,
			   unsigned int function, unsigned int group)
{
	struct visconti_pinctrl *priv = pinctrl_dev_get_drvdata(pctldev);
	const struct visconti_pin_function *func = &priv->devdata->functions[function];
	const struct visconti_pin_group *grp = &priv->devdata->groups[group];
	const struct visconti_mux *mux = &grp->mux;
	unsigned int val;
	unsigned long flags;

	dev_dbg(priv->dev, "%s: function = %d(%s) group = %d(%s)\n", __func__,
		function, func->name, group, grp->name);

	spin_lock_irqsave(&priv->lock, flags);

	/* update mux */
	val = readl(priv->base + mux->offset);
	val &= ~mux->mask;
	val |= mux->val;
	writel(val, priv->base + mux->offset);

	spin_unlock_irqrestore(&priv->lock, flags);

	dev_dbg(priv->dev, "[%x]: 0x%x\n", mux->offset, val);

	return 0;
}

static int visconti_gpio_request_enable(struct pinctrl_dev *pctldev,
				      struct pinctrl_gpio_range *range,
				      unsigned int pin)
{
	struct visconti_pinctrl *priv = pinctrl_dev_get_drvdata(pctldev);
	const struct visconti_mux *gpio_mux = &priv->devdata->gpio_mux[pin];
	unsigned long flags;
	unsigned int val;

	dev_dbg(priv->dev, "%s: pin = %d\n", __func__, pin);

	/* update mux */
	spin_lock_irqsave(&priv->lock, flags);
	val = readl(priv->base + gpio_mux->offset);
	val &= ~gpio_mux->mask;
	val |= gpio_mux->val;
	writel(val, priv->base + gpio_mux->offset);
	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static const struct pinmux_ops visconti_pinmux_ops = {
	.get_functions_count	= visconti_get_functions_count,
	.get_function_name	= visconti_get_function_name,
	.get_function_groups	= visconti_get_function_groups,
	.set_mux		= visconti_set_mux,
	.gpio_request_enable	= visconti_gpio_request_enable,
	.strict			= true,
};

int visconti_pinctrl_probe(struct platform_device *pdev,
			  const struct visconti_pinctrl_devdata *devdata)
{
	struct device *dev = &pdev->dev;
	struct visconti_pinctrl *priv;
	struct pinctrl_pin_desc *pins;
	int i, ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	priv->devdata = devdata;
	spin_lock_init(&priv->lock);

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base)) {
		dev_err(dev, "unable to map I/O space\n");
		return PTR_ERR(priv->base);
	}

	pins = devm_kcalloc(dev, devdata->nr_pins,
			    sizeof(*pins), GFP_KERNEL);
	if (!pins)
		return -ENOMEM;

	for (i = 0; i < devdata->nr_pins; i++)
		pins[i] = devdata->pins[i].pin;

	priv->pctl_desc.name = dev_name(dev);
	priv->pctl_desc.owner = THIS_MODULE;
	priv->pctl_desc.pins = pins;
	priv->pctl_desc.npins = devdata->nr_pins;
	priv->pctl_desc.confops = &visconti_pinconf_ops;
	priv->pctl_desc.pctlops = &visconti_pinctrl_ops;
	priv->pctl_desc.pmxops = &visconti_pinmux_ops;

	ret = devm_pinctrl_register_and_init(dev, &priv->pctl_desc,
					     priv, &priv->pctl);
	if (ret) {
		dev_err(dev, "couldn't register pinctrl: %d\n", ret);
		return ret;
	}

	if (devdata->unlock)
		devdata->unlock(priv->base);

	return pinctrl_enable(priv->pctl);
}
