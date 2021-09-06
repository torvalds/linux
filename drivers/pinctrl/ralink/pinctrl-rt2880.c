// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 2013 John Crispin <blogic@openwrt.org>
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/machine.h>

#include <asm/mach-ralink/ralink_regs.h>
#include <asm/mach-ralink/pinmux.h>
#include <asm/mach-ralink/mt7620.h>

#include "../core.h"
#include "../pinctrl-utils.h"

#define SYSC_REG_GPIO_MODE	0x60
#define SYSC_REG_GPIO_MODE2	0x64

struct rt2880_priv {
	struct device *dev;

	struct pinctrl_pin_desc *pads;
	struct pinctrl_desc *desc;

	struct rt2880_pmx_func **func;
	int func_count;

	struct rt2880_pmx_group *groups;
	const char **group_names;
	int group_count;

	u8 *gpio;
	int max_pins;
};

static int rt2880_get_group_count(struct pinctrl_dev *pctrldev)
{
	struct rt2880_priv *p = pinctrl_dev_get_drvdata(pctrldev);

	return p->group_count;
}

static const char *rt2880_get_group_name(struct pinctrl_dev *pctrldev,
					 unsigned int group)
{
	struct rt2880_priv *p = pinctrl_dev_get_drvdata(pctrldev);

	return (group >= p->group_count) ? NULL : p->group_names[group];
}

static int rt2880_get_group_pins(struct pinctrl_dev *pctrldev,
				 unsigned int group,
				 const unsigned int **pins,
				 unsigned int *num_pins)
{
	struct rt2880_priv *p = pinctrl_dev_get_drvdata(pctrldev);

	if (group >= p->group_count)
		return -EINVAL;

	*pins = p->groups[group].func[0].pins;
	*num_pins = p->groups[group].func[0].pin_count;

	return 0;
}

static const struct pinctrl_ops rt2880_pctrl_ops = {
	.get_groups_count	= rt2880_get_group_count,
	.get_group_name		= rt2880_get_group_name,
	.get_group_pins		= rt2880_get_group_pins,
	.dt_node_to_map		= pinconf_generic_dt_node_to_map_all,
	.dt_free_map		= pinconf_generic_dt_free_map,
};

static int rt2880_pmx_func_count(struct pinctrl_dev *pctrldev)
{
	struct rt2880_priv *p = pinctrl_dev_get_drvdata(pctrldev);

	return p->func_count;
}

static const char *rt2880_pmx_func_name(struct pinctrl_dev *pctrldev,
					unsigned int func)
{
	struct rt2880_priv *p = pinctrl_dev_get_drvdata(pctrldev);

	return p->func[func]->name;
}

static int rt2880_pmx_group_get_groups(struct pinctrl_dev *pctrldev,
				       unsigned int func,
				       const char * const **groups,
				       unsigned int * const num_groups)
{
	struct rt2880_priv *p = pinctrl_dev_get_drvdata(pctrldev);

	if (p->func[func]->group_count == 1)
		*groups = &p->group_names[p->func[func]->groups[0]];
	else
		*groups = p->group_names;

	*num_groups = p->func[func]->group_count;

	return 0;
}

static int rt2880_pmx_group_enable(struct pinctrl_dev *pctrldev,
				   unsigned int func, unsigned int group)
{
	struct rt2880_priv *p = pinctrl_dev_get_drvdata(pctrldev);
	u32 mode = 0;
	u32 reg = SYSC_REG_GPIO_MODE;
	int i;
	int shift;

	/* dont allow double use */
	if (p->groups[group].enabled) {
		dev_err(p->dev, "%s is already enabled\n",
			p->groups[group].name);
		return 0;
	}

	p->groups[group].enabled = 1;
	p->func[func]->enabled = 1;

	shift = p->groups[group].shift;
	if (shift >= 32) {
		shift -= 32;
		reg = SYSC_REG_GPIO_MODE2;
	}
	mode = rt_sysc_r32(reg);
	mode &= ~(p->groups[group].mask << shift);

	/* mark the pins as gpio */
	for (i = 0; i < p->groups[group].func[0].pin_count; i++)
		p->gpio[p->groups[group].func[0].pins[i]] = 1;

	/* function 0 is gpio and needs special handling */
	if (func == 0) {
		mode |= p->groups[group].gpio << shift;
	} else {
		for (i = 0; i < p->func[func]->pin_count; i++)
			p->gpio[p->func[func]->pins[i]] = 0;
		mode |= p->func[func]->value << shift;
	}
	rt_sysc_w32(mode, reg);

	return 0;
}

static int rt2880_pmx_group_gpio_request_enable(struct pinctrl_dev *pctrldev,
						struct pinctrl_gpio_range *range,
						unsigned int pin)
{
	struct rt2880_priv *p = pinctrl_dev_get_drvdata(pctrldev);

	if (!p->gpio[pin]) {
		dev_err(p->dev, "pin %d is not set to gpio mux\n", pin);
		return -EINVAL;
	}

	return 0;
}

static const struct pinmux_ops rt2880_pmx_group_ops = {
	.get_functions_count	= rt2880_pmx_func_count,
	.get_function_name	= rt2880_pmx_func_name,
	.get_function_groups	= rt2880_pmx_group_get_groups,
	.set_mux		= rt2880_pmx_group_enable,
	.gpio_request_enable	= rt2880_pmx_group_gpio_request_enable,
};

static struct pinctrl_desc rt2880_pctrl_desc = {
	.owner		= THIS_MODULE,
	.name		= "rt2880-pinmux",
	.pctlops	= &rt2880_pctrl_ops,
	.pmxops		= &rt2880_pmx_group_ops,
};

static struct rt2880_pmx_func gpio_func = {
	.name = "gpio",
};

static int rt2880_pinmux_index(struct rt2880_priv *p)
{
	struct rt2880_pmx_group *mux = p->groups;
	int i, j, c = 0;

	/* count the mux functions */
	while (mux->name) {
		p->group_count++;
		mux++;
	}

	/* allocate the group names array needed by the gpio function */
	p->group_names = devm_kcalloc(p->dev, p->group_count,
				      sizeof(char *), GFP_KERNEL);
	if (!p->group_names)
		return -ENOMEM;

	for (i = 0; i < p->group_count; i++) {
		p->group_names[i] = p->groups[i].name;
		p->func_count += p->groups[i].func_count;
	}

	/* we have a dummy function[0] for gpio */
	p->func_count++;

	/* allocate our function and group mapping index buffers */
	p->func = devm_kcalloc(p->dev, p->func_count,
			       sizeof(*p->func), GFP_KERNEL);
	gpio_func.groups = devm_kcalloc(p->dev, p->group_count, sizeof(int),
					GFP_KERNEL);
	if (!p->func || !gpio_func.groups)
		return -ENOMEM;

	/* add a backpointer to the function so it knows its group */
	gpio_func.group_count = p->group_count;
	for (i = 0; i < gpio_func.group_count; i++)
		gpio_func.groups[i] = i;

	p->func[c] = &gpio_func;
	c++;

	/* add remaining functions */
	for (i = 0; i < p->group_count; i++) {
		for (j = 0; j < p->groups[i].func_count; j++) {
			p->func[c] = &p->groups[i].func[j];
			p->func[c]->groups = devm_kzalloc(p->dev, sizeof(int),
						    GFP_KERNEL);
			if (!p->func[c]->groups)
				return -ENOMEM;
			p->func[c]->groups[0] = i;
			p->func[c]->group_count = 1;
			c++;
		}
	}
	return 0;
}

static int rt2880_pinmux_pins(struct rt2880_priv *p)
{
	int i, j;

	/*
	 * loop over the functions and initialize the pins array.
	 * also work out the highest pin used.
	 */
	for (i = 0; i < p->func_count; i++) {
		int pin;

		if (!p->func[i]->pin_count)
			continue;

		p->func[i]->pins = devm_kcalloc(p->dev,
						p->func[i]->pin_count,
						sizeof(int),
						GFP_KERNEL);
		for (j = 0; j < p->func[i]->pin_count; j++)
			p->func[i]->pins[j] = p->func[i]->pin_first + j;

		pin = p->func[i]->pin_first + p->func[i]->pin_count;
		if (pin > p->max_pins)
			p->max_pins = pin;
	}

	/* the buffer that tells us which pins are gpio */
	p->gpio = devm_kcalloc(p->dev, p->max_pins, sizeof(u8), GFP_KERNEL);
	/* the pads needed to tell pinctrl about our pins */
	p->pads = devm_kcalloc(p->dev, p->max_pins,
			       sizeof(struct pinctrl_pin_desc), GFP_KERNEL);
	if (!p->pads || !p->gpio)
		return -ENOMEM;

	memset(p->gpio, 1, sizeof(u8) * p->max_pins);
	for (i = 0; i < p->func_count; i++) {
		if (!p->func[i]->pin_count)
			continue;

		for (j = 0; j < p->func[i]->pin_count; j++)
			p->gpio[p->func[i]->pins[j]] = 0;
	}

	/* pin 0 is always a gpio */
	p->gpio[0] = 1;

	/* set the pads */
	for (i = 0; i < p->max_pins; i++) {
		/* strlen("ioXY") + 1 = 5 */
		char *name = devm_kzalloc(p->dev, 5, GFP_KERNEL);

		if (!name)
			return -ENOMEM;
		snprintf(name, 5, "io%d", i);
		p->pads[i].number = i;
		p->pads[i].name = name;
	}
	p->desc->pins = p->pads;
	p->desc->npins = p->max_pins;

	return 0;
}

static int rt2880_pinmux_probe(struct platform_device *pdev)
{
	struct rt2880_priv *p;
	struct pinctrl_dev *dev;
	int err;

	if (!rt2880_pinmux_data)
		return -ENOTSUPP;

	/* setup the private data */
	p = devm_kzalloc(&pdev->dev, sizeof(struct rt2880_priv), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	p->dev = &pdev->dev;
	p->desc = &rt2880_pctrl_desc;
	p->groups = rt2880_pinmux_data;
	platform_set_drvdata(pdev, p);

	/* init the device */
	err = rt2880_pinmux_index(p);
	if (err) {
		dev_err(&pdev->dev, "failed to load index\n");
		return err;
	}

	err = rt2880_pinmux_pins(p);
	if (err) {
		dev_err(&pdev->dev, "failed to load pins\n");
		return err;
	}
	dev = pinctrl_register(p->desc, &pdev->dev, p);

	return PTR_ERR_OR_ZERO(dev);
}

static const struct of_device_id rt2880_pinmux_match[] = {
	{ .compatible = "ralink,rt2880-pinmux" },
	{},
};
MODULE_DEVICE_TABLE(of, rt2880_pinmux_match);

static struct platform_driver rt2880_pinmux_driver = {
	.probe = rt2880_pinmux_probe,
	.driver = {
		.name = "rt2880-pinmux",
		.of_match_table = rt2880_pinmux_match,
	},
};

static int __init rt2880_pinmux_init(void)
{
	return platform_driver_register(&rt2880_pinmux_driver);
}

core_initcall_sync(rt2880_pinmux_init);
