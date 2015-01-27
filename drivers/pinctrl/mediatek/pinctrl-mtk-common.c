/*
 * mt65xx pinctrl driver based on Allwinner A1X pinctrl driver.
 * Copyright (c) 2014 MediaTek Inc.
 * Author: Hongzhou.Yang <hongzhou.yang@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <dt-bindings/pinctrl/mt65xx.h>

#include "../core.h"
#include "../pinconf.h"
#include "../pinctrl-utils.h"
#include "pinctrl-mtk-common.h"

#define MAX_GPIO_MODE_PER_REG 5
#define GPIO_MODE_BITS        3

static const char * const mtk_gpio_functions[] = {
	"func0", "func1", "func2", "func3",
	"func4", "func5", "func6", "func7",
};

/*
 * There are two base address for pull related configuration
 * in mt8135, and different GPIO pins use different base address.
 * When pin number greater than type1_start and less than type1_end,
 * should use the second base address.
 */
static struct regmap *mtk_get_regmap(struct mtk_pinctrl *pctl,
		unsigned long pin)
{
	if (pin >= pctl->devdata->type1_start && pin < pctl->devdata->type1_end)
		return pctl->regmap2;
	return pctl->regmap1;
}

static unsigned int mtk_get_port(struct mtk_pinctrl *pctl, unsigned long pin)
{
	/* Different SoC has different mask and port shift. */
	return ((pin >> 4) & pctl->devdata->port_mask)
			<< pctl->devdata->port_shf;
}

static int mtk_pmx_gpio_set_direction(struct pinctrl_dev *pctldev,
			struct pinctrl_gpio_range *range, unsigned offset,
			bool input)
{
	unsigned int reg_addr;
	unsigned int bit;
	struct mtk_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	reg_addr = mtk_get_port(pctl, offset) + pctl->devdata->dir_offset;
	bit = BIT(offset & 0xf);

	if (input)
		/* Different SoC has different alignment offset. */
		reg_addr = CLR_ADDR(reg_addr, pctl);
	else
		reg_addr = SET_ADDR(reg_addr, pctl);

	regmap_write(mtk_get_regmap(pctl, offset), reg_addr, bit);
	return 0;
}

static void mtk_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	unsigned int reg_addr;
	unsigned int bit;
	struct mtk_pinctrl *pctl = dev_get_drvdata(chip->dev);

	reg_addr = mtk_get_port(pctl, offset) + pctl->devdata->dout_offset;
	bit = BIT(offset & 0xf);

	if (value)
		reg_addr = SET_ADDR(reg_addr, pctl);
	else
		reg_addr = CLR_ADDR(reg_addr, pctl);

	regmap_write(mtk_get_regmap(pctl, offset), reg_addr, bit);
}

static void mtk_pconf_set_ies_smt(struct mtk_pinctrl *pctl, unsigned pin,
		int value, enum pin_config_param param)
{
	unsigned int reg_addr, offset;
	unsigned int bit;
	int ret;

	/*
	 * Due to some pins are irregular, their input enable and smt
	 * control register are discontinuous, but they are mapping together.
	 * So we need this special handle.
	 */
	if (pctl->devdata->spec_ies_smt_set) {
		ret = pctl->devdata->spec_ies_smt_set(mtk_get_regmap(pctl, pin),
			pin, pctl->devdata->port_align, value);
		if (!ret)
			return;
	}

	bit = BIT(pin & 0xf);

	if (param == PIN_CONFIG_INPUT_ENABLE)
		offset = pctl->devdata->ies_offset;
	else
		offset = pctl->devdata->smt_offset;

	if (value)
		reg_addr = SET_ADDR(mtk_get_port(pctl, pin) + offset, pctl);
	else
		reg_addr = CLR_ADDR(mtk_get_port(pctl, pin) + offset, pctl);

	regmap_write(mtk_get_regmap(pctl, pin), reg_addr, bit);
}

static const struct mtk_pin_drv_grp *mtk_find_pin_drv_grp_by_pin(
		struct mtk_pinctrl *pctl,  unsigned long pin) {
	int i;

	for (i = 0; i < pctl->devdata->n_pin_drv_grps; i++) {
		const struct mtk_pin_drv_grp *pin_drv =
				pctl->devdata->pin_drv_grp + i;
		if (pin == pin_drv->pin)
			return pin_drv;
	}

	return NULL;
}

static int mtk_pconf_set_driving(struct mtk_pinctrl *pctl,
		unsigned int pin, unsigned char driving)
{
	const struct mtk_pin_drv_grp *pin_drv;
	unsigned int val;
	unsigned int bits, mask, shift;
	const struct mtk_drv_group_desc *drv_grp;

	if (pin >= pctl->devdata->npins)
		return -EINVAL;

	pin_drv = mtk_find_pin_drv_grp_by_pin(pctl, pin);
	if (!pin_drv || pin_drv->grp > pctl->devdata->n_grp_cls)
		return -EINVAL;

	drv_grp = pctl->devdata->grp_desc + pin_drv->grp;
	if (driving >= drv_grp->min_drv && driving <= drv_grp->max_drv
		&& !(driving % drv_grp->step)) {
		val = driving / drv_grp->step - 1;
		bits = drv_grp->high_bit - drv_grp->low_bit + 1;
		mask = BIT(bits) - 1;
		shift = pin_drv->bit + drv_grp->low_bit;
		mask <<= shift;
		val <<= shift;
		return regmap_update_bits(mtk_get_regmap(pctl, pin),
				pin_drv->offset, mask, val);
	}

	return -EINVAL;
}

static int mtk_pconf_set_pull_select(struct mtk_pinctrl *pctl,
		unsigned int pin, bool enable, bool isup, unsigned int arg)
{
	unsigned int bit;
	unsigned int reg_pullen, reg_pullsel;
	int ret;

	/* Some pins' pull setting are very different,
	 * they have separate pull up/down bit, R0 and R1
	 * resistor bit, so we need this special handle.
	 */
	if (pctl->devdata->spec_pull_set) {
		ret = pctl->devdata->spec_pull_set(mtk_get_regmap(pctl, pin),
			pin, pctl->devdata->port_align, isup, arg);
		if (!ret)
			return 0;
	}

	/* For generic pull config, default arg value should be 0 or 1. */
	if (arg != 0 && arg != 1) {
		dev_err(pctl->dev, "invalid pull-up argument %d on pin %d .\n",
			arg, pin);
		return -EINVAL;
	}

	bit = BIT(pin & 0xf);
	if (enable)
		reg_pullen = SET_ADDR(mtk_get_port(pctl, pin) +
			pctl->devdata->pullen_offset, pctl);
	else
		reg_pullen = CLR_ADDR(mtk_get_port(pctl, pin) +
			pctl->devdata->pullen_offset, pctl);

	if (isup)
		reg_pullsel = SET_ADDR(mtk_get_port(pctl, pin) +
			pctl->devdata->pullsel_offset, pctl);
	else
		reg_pullsel = CLR_ADDR(mtk_get_port(pctl, pin) +
			pctl->devdata->pullsel_offset, pctl);

	regmap_write(mtk_get_regmap(pctl, pin), reg_pullen, bit);
	regmap_write(mtk_get_regmap(pctl, pin), reg_pullsel, bit);
	return 0;
}

static int mtk_pconf_parse_conf(struct pinctrl_dev *pctldev,
		unsigned int pin, enum pin_config_param param,
		enum pin_config_param arg)
{
	struct mtk_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		mtk_pconf_set_pull_select(pctl, pin, false, false, arg);
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		mtk_pconf_set_pull_select(pctl, pin, true, true, arg);
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		mtk_pconf_set_pull_select(pctl, pin, true, false, arg);
		break;
	case PIN_CONFIG_INPUT_ENABLE:
		mtk_pconf_set_ies_smt(pctl, pin, arg, param);
		break;
	case PIN_CONFIG_OUTPUT:
		mtk_gpio_set(pctl->chip, pin, arg);
		mtk_pmx_gpio_set_direction(pctldev, NULL, pin, false);
		break;
	case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
		mtk_pconf_set_ies_smt(pctl, pin, arg, param);
		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		mtk_pconf_set_driving(pctl, pin, arg);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int mtk_pconf_group_get(struct pinctrl_dev *pctldev,
				 unsigned group,
				 unsigned long *config)
{
	struct mtk_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	*config = pctl->groups[group].config;

	return 0;
}

static int mtk_pconf_group_set(struct pinctrl_dev *pctldev, unsigned group,
				 unsigned long *configs, unsigned num_configs)
{
	struct mtk_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct mtk_pinctrl_group *g = &pctl->groups[group];
	int i;

	for (i = 0; i < num_configs; i++) {
		mtk_pconf_parse_conf(pctldev, g->pin,
			pinconf_to_config_param(configs[i]),
			pinconf_to_config_argument(configs[i]));

		g->config = configs[i];
	}

	return 0;
}

static const struct pinconf_ops mtk_pconf_ops = {
	.pin_config_group_get	= mtk_pconf_group_get,
	.pin_config_group_set	= mtk_pconf_group_set,
};

static struct mtk_pinctrl_group *
mtk_pctrl_find_group_by_pin(struct mtk_pinctrl *pctl, u32 pin)
{
	int i;

	for (i = 0; i < pctl->ngroups; i++) {
		struct mtk_pinctrl_group *grp = pctl->groups + i;

		if (grp->pin == pin)
			return grp;
	}

	return NULL;
}

static const struct mtk_desc_function *mtk_pctrl_find_function_by_pin(
		struct mtk_pinctrl *pctl, u32 pin_num, u32 fnum)
{
	const struct mtk_desc_pin *pin = pctl->devdata->pins + pin_num;
	const struct mtk_desc_function *func = pin->functions;

	while (func && func->name) {
		if (func->muxval == fnum)
			return func;
		func++;
	}

	return NULL;
}

static bool mtk_pctrl_is_function_valid(struct mtk_pinctrl *pctl,
		u32 pin_num, u32 fnum)
{
	int i;

	for (i = 0; i < pctl->devdata->npins; i++) {
		const struct mtk_desc_pin *pin = pctl->devdata->pins + i;

		if (pin->pin.number == pin_num) {
			const struct mtk_desc_function *func =
					pin->functions;

			while (func && func->name) {
				if (func->muxval == fnum)
					return true;
				func++;
			}

			break;
		}
	}

	return false;
}

static int mtk_pctrl_dt_node_to_map_func(struct mtk_pinctrl *pctl,
		u32 pin, u32 fnum, struct mtk_pinctrl_group *grp,
		struct pinctrl_map **map, unsigned *reserved_maps,
		unsigned *num_maps)
{
	bool ret;

	if (*num_maps == *reserved_maps)
		return -ENOSPC;

	(*map)[*num_maps].type = PIN_MAP_TYPE_MUX_GROUP;
	(*map)[*num_maps].data.mux.group = grp->name;

	ret = mtk_pctrl_is_function_valid(pctl, pin, fnum);
	if (!ret) {
		dev_err(pctl->dev, "invalid function %d on pin %d .\n",
				fnum, pin);
		return -EINVAL;
	}

	(*map)[*num_maps].data.mux.function = mtk_gpio_functions[fnum];
	(*num_maps)++;

	return 0;
}

static int mtk_pctrl_dt_subnode_to_map(struct pinctrl_dev *pctldev,
				      struct device_node *node,
				      struct pinctrl_map **map,
				      unsigned *reserved_maps,
				      unsigned *num_maps)
{
	struct property *pins;
	u32 pinfunc, pin, func;
	int num_pins, num_funcs, maps_per_pin;
	unsigned long *configs;
	unsigned int num_configs;
	bool has_config = 0;
	int i, err;
	unsigned reserve = 0;
	struct mtk_pinctrl_group *grp;
	struct mtk_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	pins = of_find_property(node, "pinmux", NULL);
	if (!pins) {
		dev_err(pctl->dev, "missing pins property in node %s .\n",
				node->name);
		return -EINVAL;
	}

	err = pinconf_generic_parse_dt_config(node, &configs, &num_configs);
	if (num_configs)
		has_config = 1;

	num_pins = pins->length / sizeof(u32);
	num_funcs = num_pins;
	maps_per_pin = 0;
	if (num_funcs)
		maps_per_pin++;
	if (has_config && num_pins >= 1)
		maps_per_pin++;

	if (!num_pins || !maps_per_pin)
		return -EINVAL;

	reserve = num_pins * maps_per_pin;

	err = pinctrl_utils_reserve_map(pctldev, map,
			reserved_maps, num_maps, reserve);
	if (err < 0)
		goto fail;

	for (i = 0; i < num_pins; i++) {
		err = of_property_read_u32_index(node, "pinmux",
				i, &pinfunc);
		if (err)
			goto fail;

		pin = MTK_GET_PIN_NO(pinfunc);
		func = MTK_GET_PIN_FUNC(pinfunc);

		if (pin >= pctl->devdata->npins ||
				func >= ARRAY_SIZE(mtk_gpio_functions)) {
			dev_err(pctl->dev, "invalid pins value.\n");
			err = -EINVAL;
			goto fail;
		}

		grp = mtk_pctrl_find_group_by_pin(pctl, pin);
		if (!grp) {
			dev_err(pctl->dev, "unable to match pin %d to group\n",
					pin);
			return -EINVAL;
		}

		err = mtk_pctrl_dt_node_to_map_func(pctl, pin, func, grp, map,
				reserved_maps, num_maps);
		if (err < 0)
			goto fail;

		if (has_config) {
			err = pinctrl_utils_add_map_configs(pctldev, map,
					reserved_maps, num_maps, grp->name,
					configs, num_configs,
					PIN_MAP_TYPE_CONFIGS_GROUP);
			if (err < 0)
				goto fail;
		}
	}

	return 0;

fail:
	return err;
}

static int mtk_pctrl_dt_node_to_map(struct pinctrl_dev *pctldev,
				 struct device_node *np_config,
				 struct pinctrl_map **map, unsigned *num_maps)
{
	struct device_node *np;
	unsigned reserved_maps;
	int ret;

	*map = NULL;
	*num_maps = 0;
	reserved_maps = 0;

	for_each_child_of_node(np_config, np) {
		ret = mtk_pctrl_dt_subnode_to_map(pctldev, np, map,
				&reserved_maps, num_maps);
		if (ret < 0) {
			pinctrl_utils_dt_free_map(pctldev, *map, *num_maps);
			return ret;
		}
	}

	return 0;
}

static int mtk_pctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct mtk_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	return pctl->ngroups;
}

static const char *mtk_pctrl_get_group_name(struct pinctrl_dev *pctldev,
					      unsigned group)
{
	struct mtk_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	return pctl->groups[group].name;
}

static int mtk_pctrl_get_group_pins(struct pinctrl_dev *pctldev,
				      unsigned group,
				      const unsigned **pins,
				      unsigned *num_pins)
{
	struct mtk_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	*pins = (unsigned *)&pctl->groups[group].pin;
	*num_pins = 1;

	return 0;
}

static const struct pinctrl_ops mtk_pctrl_ops = {
	.dt_node_to_map		= mtk_pctrl_dt_node_to_map,
	.dt_free_map		= pinctrl_utils_dt_free_map,
	.get_groups_count	= mtk_pctrl_get_groups_count,
	.get_group_name		= mtk_pctrl_get_group_name,
	.get_group_pins		= mtk_pctrl_get_group_pins,
};

static int mtk_pmx_get_funcs_cnt(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(mtk_gpio_functions);
}

static const char *mtk_pmx_get_func_name(struct pinctrl_dev *pctldev,
					   unsigned selector)
{
	return mtk_gpio_functions[selector];
}

static int mtk_pmx_get_func_groups(struct pinctrl_dev *pctldev,
				     unsigned function,
				     const char * const **groups,
				     unsigned * const num_groups)
{
	struct mtk_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	*groups = pctl->grp_names;
	*num_groups = pctl->ngroups;

	return 0;
}

static int mtk_pmx_set_mode(struct pinctrl_dev *pctldev,
		unsigned long pin, unsigned long mode)
{
	unsigned int reg_addr;
	unsigned char bit;
	unsigned int val;
	unsigned int mask = (1L << GPIO_MODE_BITS) - 1;
	struct mtk_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	reg_addr = ((pin / MAX_GPIO_MODE_PER_REG) << pctl->devdata->port_shf)
			+ pctl->devdata->pinmux_offset;

	bit = pin % MAX_GPIO_MODE_PER_REG;
	mask <<= (GPIO_MODE_BITS * bit);
	val = (mode << (GPIO_MODE_BITS * bit));
	return regmap_update_bits(mtk_get_regmap(pctl, pin),
			reg_addr, mask, val);
}

static const struct mtk_desc_pin *
mtk_find_pin_by_eint_num(struct mtk_pinctrl *pctl, unsigned int eint_num)
{
	int i;
	const struct mtk_desc_pin *pin;

	for (i = 0; i < pctl->devdata->npins; i++) {
		pin = pctl->devdata->pins + i;
		if (pin->eint.eintnum == eint_num)
			return pin;
	}

	return NULL;
}

static int mtk_pmx_set_mux(struct pinctrl_dev *pctldev,
			    unsigned function,
			    unsigned group)
{
	bool ret;
	const struct mtk_desc_function *desc;
	struct mtk_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct mtk_pinctrl_group *g = pctl->groups + group;

	ret = mtk_pctrl_is_function_valid(pctl, g->pin, function);
	if (!ret) {
		dev_err(pctl->dev, "invaild function %d on group %d .\n",
				function, group);
		return -EINVAL;
	}

	desc = mtk_pctrl_find_function_by_pin(pctl, g->pin, function);
	if (!desc)
		return -EINVAL;
	mtk_pmx_set_mode(pctldev, g->pin, desc->muxval);
	return 0;
}

static const struct pinmux_ops mtk_pmx_ops = {
	.get_functions_count	= mtk_pmx_get_funcs_cnt,
	.get_function_name	= mtk_pmx_get_func_name,
	.get_function_groups	= mtk_pmx_get_func_groups,
	.set_mux		= mtk_pmx_set_mux,
	.gpio_set_direction	= mtk_pmx_gpio_set_direction,
};

static int mtk_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	return pinctrl_request_gpio(chip->base + offset);
}

static void mtk_gpio_free(struct gpio_chip *chip, unsigned offset)
{
	pinctrl_free_gpio(chip->base + offset);
}

static int mtk_gpio_direction_input(struct gpio_chip *chip,
					unsigned offset)
{
	return pinctrl_gpio_direction_input(chip->base + offset);
}

static int mtk_gpio_direction_output(struct gpio_chip *chip,
					unsigned offset, int value)
{
	mtk_gpio_set(chip, offset, value);
	return pinctrl_gpio_direction_output(chip->base + offset);
}

static int mtk_gpio_get_direction(struct gpio_chip *chip, unsigned offset)
{
	unsigned int reg_addr;
	unsigned int bit;
	unsigned int read_val = 0;

	struct mtk_pinctrl *pctl = dev_get_drvdata(chip->dev);

	reg_addr =  mtk_get_port(pctl, offset) + pctl->devdata->dir_offset;
	bit = BIT(offset & 0xf);
	regmap_read(pctl->regmap1, reg_addr, &read_val);
	return !!(read_val & bit);
}

static int mtk_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	unsigned int reg_addr;
	unsigned int bit;
	unsigned int read_val = 0;
	struct mtk_pinctrl *pctl = dev_get_drvdata(chip->dev);

	if (mtk_gpio_get_direction(chip, offset))
		reg_addr = mtk_get_port(pctl, offset) +
			pctl->devdata->dout_offset;
	else
		reg_addr = mtk_get_port(pctl, offset) +
			pctl->devdata->din_offset;

	bit = BIT(offset & 0xf);
	regmap_read(pctl->regmap1, reg_addr, &read_val);
	return !!(read_val & bit);
}

static int mtk_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	const struct mtk_desc_pin *pin;
	struct mtk_pinctrl *pctl = dev_get_drvdata(chip->dev);
	int irq;

	pin = pctl->devdata->pins + offset;
	if (pin->eint.eintnum == NO_EINT_SUPPORT)
		return -EINVAL;

	irq = irq_find_mapping(pctl->domain, pin->eint.eintnum);
	if (!irq)
		return -EINVAL;

	return irq;
}

static int mtk_pinctrl_irq_request_resources(struct irq_data *d)
{
	struct mtk_pinctrl *pctl = irq_data_get_irq_chip_data(d);
	const struct mtk_desc_pin *pin;
	int ret;

	pin = mtk_find_pin_by_eint_num(pctl, d->hwirq);

	if (!pin) {
		dev_err(pctl->dev, "Can not find pin\n");
		return -EINVAL;
	}

	ret = gpiochip_lock_as_irq(pctl->chip, pin->pin.number);
	if (ret) {
		dev_err(pctl->dev, "unable to lock HW IRQ %lu for IRQ\n",
			irqd_to_hwirq(d));
		return ret;
	}

	/* set mux to INT mode */
	mtk_pmx_set_mode(pctl->pctl_dev, pin->pin.number, pin->eint.eintmux);

	return 0;
}

static void mtk_pinctrl_irq_release_resources(struct irq_data *d)
{
	struct mtk_pinctrl *pctl = irq_data_get_irq_chip_data(d);
	const struct mtk_desc_pin *pin;

	pin = mtk_find_pin_by_eint_num(pctl, d->hwirq);

	if (!pin) {
		dev_err(pctl->dev, "Can not find pin\n");
		return;
	}

	gpiochip_unlock_as_irq(pctl->chip, pin->pin.number);
}

static void __iomem *mtk_eint_get_offset(struct mtk_pinctrl *pctl,
	unsigned int eint_num, unsigned int offset)
{
	unsigned int eint_base = 0;
	void __iomem *reg;

	if (eint_num >= pctl->devdata->ap_num)
		eint_base = pctl->devdata->ap_num;

	reg = pctl->eint_reg_base + offset + ((eint_num - eint_base) / 32) * 4;

	return reg;
}

/*
 * mtk_can_en_debounce: Check the EINT number is able to enable debounce or not
 * @eint_num: the EINT number to setmtk_pinctrl
 */
static unsigned int mtk_eint_can_en_debounce(struct mtk_pinctrl *pctl,
	unsigned int eint_num)
{
	unsigned int sens;
	unsigned int bit = BIT(eint_num % 32);
	const struct mtk_eint_offsets *eint_offsets =
		&pctl->devdata->eint_offsets;

	void __iomem *reg = mtk_eint_get_offset(pctl, eint_num,
			eint_offsets->sens);

	if (readl(reg) & bit)
		sens = MT_LEVEL_SENSITIVE;
	else
		sens = MT_EDGE_SENSITIVE;

	if ((eint_num < pctl->devdata->db_cnt) && (sens != MT_EDGE_SENSITIVE))
		return 1;
	else
		return 0;
}

/*
 * mtk_eint_get_mask: To get the eint mask
 * @eint_num: the EINT number to get
 */
static unsigned int mtk_eint_get_mask(struct mtk_pinctrl *pctl,
	unsigned int eint_num)
{
	unsigned int bit = BIT(eint_num % 32);
	const struct mtk_eint_offsets *eint_offsets =
		&pctl->devdata->eint_offsets;

	void __iomem *reg = mtk_eint_get_offset(pctl, eint_num,
			eint_offsets->mask);

	return !!(readl(reg) & bit);
}

static void mtk_eint_mask(struct irq_data *d)
{
	struct mtk_pinctrl *pctl = irq_data_get_irq_chip_data(d);
	const struct mtk_eint_offsets *eint_offsets =
			&pctl->devdata->eint_offsets;
	u32 mask = BIT(d->hwirq & 0x1f);
	void __iomem *reg = mtk_eint_get_offset(pctl, d->hwirq,
			eint_offsets->mask_set);

	writel(mask, reg);
}

static void mtk_eint_unmask(struct irq_data *d)
{
	struct mtk_pinctrl *pctl = irq_data_get_irq_chip_data(d);
	const struct mtk_eint_offsets *eint_offsets =
		&pctl->devdata->eint_offsets;
	u32 mask = BIT(d->hwirq & 0x1f);
	void __iomem *reg = mtk_eint_get_offset(pctl, d->hwirq,
			eint_offsets->mask_clr);

	writel(mask, reg);
}

static int mtk_gpio_set_debounce(struct gpio_chip *chip, unsigned offset,
	unsigned debounce)
{
	struct mtk_pinctrl *pctl = dev_get_drvdata(chip->dev);
	int eint_num, virq, eint_offset;
	unsigned int set_offset, bit, clr_bit, clr_offset, rst, i, unmask, dbnc;
	static const unsigned int dbnc_arr[] = {0 , 1, 16, 32, 64, 128, 256};
	const struct mtk_desc_pin *pin;
	struct irq_data *d;

	pin = pctl->devdata->pins + offset;
	if (pin->eint.eintnum == NO_EINT_SUPPORT)
		return -EINVAL;

	eint_num = pin->eint.eintnum;
	virq = irq_find_mapping(pctl->domain, eint_num);
	eint_offset = (eint_num % 4) * 8;
	d = irq_get_irq_data(virq);

	set_offset = (eint_num / 4) * 4 + pctl->devdata->eint_offsets.dbnc_set;
	clr_offset = (eint_num / 4) * 4 + pctl->devdata->eint_offsets.dbnc_clr;
	if (!mtk_eint_can_en_debounce(pctl, eint_num))
		return -ENOSYS;

	dbnc = ARRAY_SIZE(dbnc_arr);
	for (i = 0; i < ARRAY_SIZE(dbnc_arr); i++) {
		if (debounce <= dbnc_arr[i]) {
			dbnc = i;
			break;
		}
	}

	if (!mtk_eint_get_mask(pctl, eint_num)) {
		mtk_eint_mask(d);
		unmask = 1;
	}

	clr_bit = 0xff << eint_offset;
	writel(clr_bit, pctl->eint_reg_base + clr_offset);

	bit = ((dbnc << EINT_DBNC_SET_DBNC_BITS) | EINT_DBNC_SET_EN) <<
		eint_offset;
	rst = EINT_DBNC_RST_BIT << eint_offset;
	writel(rst | bit, pctl->eint_reg_base + set_offset);

	/* Delay a while (more than 2T) to wait for hw debounce counter reset
	work correctly */
	udelay(1);
	if (unmask == 1)
		mtk_eint_unmask(d);

	return 0;
}

static struct gpio_chip mtk_gpio_chip = {
	.owner			= THIS_MODULE,
	.request		= mtk_gpio_request,
	.free			= mtk_gpio_free,
	.direction_input	= mtk_gpio_direction_input,
	.direction_output	= mtk_gpio_direction_output,
	.get			= mtk_gpio_get,
	.set			= mtk_gpio_set,
	.to_irq			= mtk_gpio_to_irq,
	.set_debounce		= mtk_gpio_set_debounce,
	.of_gpio_n_cells	= 2,
};

static int mtk_eint_set_type(struct irq_data *d,
				      unsigned int type)
{
	struct mtk_pinctrl *pctl = irq_data_get_irq_chip_data(d);
	const struct mtk_eint_offsets *eint_offsets =
		&pctl->devdata->eint_offsets;
	u32 mask = BIT(d->hwirq & 0x1f);
	void __iomem *reg;

	if (((type & IRQ_TYPE_EDGE_BOTH) && (type & IRQ_TYPE_LEVEL_MASK)) ||
		((type & IRQ_TYPE_EDGE_BOTH) == IRQ_TYPE_EDGE_BOTH) ||
		((type & IRQ_TYPE_LEVEL_MASK) == IRQ_TYPE_LEVEL_MASK)) {
		dev_err(pctl->dev, "Can't configure IRQ%d (EINT%lu) for type 0x%X\n",
			d->irq, d->hwirq, type);
		return -EINVAL;
	}

	if (type & (IRQ_TYPE_LEVEL_LOW | IRQ_TYPE_EDGE_FALLING)) {
		reg = mtk_eint_get_offset(pctl, d->hwirq,
			eint_offsets->pol_clr);
		writel(mask, reg);
	} else {
		reg = mtk_eint_get_offset(pctl, d->hwirq,
			eint_offsets->pol_set);
		writel(mask, reg);
	}

	if (type & (IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING)) {
		reg = mtk_eint_get_offset(pctl, d->hwirq,
			eint_offsets->sens_clr);
		writel(mask, reg);
	} else {
		reg = mtk_eint_get_offset(pctl, d->hwirq,
			eint_offsets->sens_set);
		writel(mask, reg);
	}

	return 0;
}

static void mtk_eint_ack(struct irq_data *d)
{
	struct mtk_pinctrl *pctl = irq_data_get_irq_chip_data(d);
	const struct mtk_eint_offsets *eint_offsets =
		&pctl->devdata->eint_offsets;
	u32 mask = BIT(d->hwirq & 0x1f);
	void __iomem *reg = mtk_eint_get_offset(pctl, d->hwirq,
			eint_offsets->ack);

	writel(mask, reg);
}

static struct irq_chip mtk_pinctrl_irq_chip = {
	.name = "mt-eint",
	.irq_mask = mtk_eint_mask,
	.irq_unmask = mtk_eint_unmask,
	.irq_ack = mtk_eint_ack,
	.irq_set_type = mtk_eint_set_type,
	.irq_request_resources = mtk_pinctrl_irq_request_resources,
	.irq_release_resources = mtk_pinctrl_irq_release_resources,
};

static unsigned int mtk_eint_init(struct mtk_pinctrl *pctl)
{
	const struct mtk_eint_offsets *eint_offsets =
		&pctl->devdata->eint_offsets;
	void __iomem *reg = pctl->eint_reg_base + eint_offsets->dom_en;
	unsigned int i;

	for (i = 0; i < pctl->devdata->ap_num; i += 32) {
		writel(0xffffffff, reg);
		reg += 4;
	}
	return 0;
}

static inline void
mtk_eint_debounce_process(struct mtk_pinctrl *pctl, int index)
{
	unsigned int rst, ctrl_offset;
	unsigned int bit, dbnc;
	const struct mtk_eint_offsets *eint_offsets =
		&pctl->devdata->eint_offsets;

	ctrl_offset = (index / 4) * 4 + eint_offsets->dbnc_ctrl;
	dbnc = readl(pctl->eint_reg_base + ctrl_offset);
	bit = EINT_DBNC_SET_EN << ((index % 4) * 8);
	if ((bit & dbnc) > 0) {
		ctrl_offset = (index / 4) * 4 + eint_offsets->dbnc_set;
		rst = EINT_DBNC_RST_BIT << ((index % 4) * 8);
		writel(rst, pctl->eint_reg_base + ctrl_offset);
	}
}

static void mtk_eint_irq_handler(unsigned irq, struct irq_desc *desc)
{
	struct irq_chip *chip = irq_get_chip(irq);
	struct mtk_pinctrl *pctl = irq_get_handler_data(irq);
	unsigned int status, eint_num;
	int offset, index, virq;
	const struct mtk_eint_offsets *eint_offsets =
		&pctl->devdata->eint_offsets;
	void __iomem *reg =  mtk_eint_get_offset(pctl, 0, eint_offsets->stat);

	chained_irq_enter(chip, desc);
	for (eint_num = 0; eint_num < pctl->devdata->ap_num; eint_num += 32) {
		status = readl(reg);
		reg += 4;
		while (status) {
			offset = __ffs(status);
			index = eint_num + offset;
			virq = irq_find_mapping(pctl->domain, index);
			status &= ~BIT(offset);

			generic_handle_irq(virq);

			if (index < pctl->devdata->db_cnt)
				mtk_eint_debounce_process(pctl , index);
		}
	}
	chained_irq_exit(chip, desc);
}

static int mtk_pctrl_build_state(struct platform_device *pdev)
{
	struct mtk_pinctrl *pctl = platform_get_drvdata(pdev);
	int i;

	pctl->ngroups = pctl->devdata->npins;

	/* Allocate groups */
	pctl->groups = devm_kzalloc(&pdev->dev,
				    pctl->ngroups * sizeof(*pctl->groups),
				    GFP_KERNEL);
	if (!pctl->groups)
		return -ENOMEM;

	/* We assume that one pin is one group, use pin name as group name. */
	pctl->grp_names = devm_kzalloc(&pdev->dev,
				    pctl->ngroups * sizeof(*pctl->grp_names),
				    GFP_KERNEL);
	if (!pctl->grp_names)
		return -ENOMEM;

	for (i = 0; i < pctl->devdata->npins; i++) {
		const struct mtk_desc_pin *pin = pctl->devdata->pins + i;
		struct mtk_pinctrl_group *group = pctl->groups + i;

		group->name = pin->pin.name;
		group->pin = pin->pin.number;

		pctl->grp_names[i] = pin->pin.name;
	}

	return 0;
}

static struct pinctrl_desc mtk_pctrl_desc = {
	.confops	= &mtk_pconf_ops,
	.pctlops	= &mtk_pctrl_ops,
	.pmxops		= &mtk_pmx_ops,
};

int mtk_pctrl_init(struct platform_device *pdev,
		const struct mtk_pinctrl_devdata *data)
{
	struct pinctrl_pin_desc *pins;
	struct mtk_pinctrl *pctl;
	struct device_node *np = pdev->dev.of_node, *node;
	struct property *prop;
	struct resource *res;
	int i, ret, irq;

	pctl = devm_kzalloc(&pdev->dev, sizeof(*pctl), GFP_KERNEL);
	if (!pctl)
		return -ENOMEM;

	platform_set_drvdata(pdev, pctl);

	prop = of_find_property(np, "pins-are-numbered", NULL);
	if (!prop) {
		dev_err(&pdev->dev, "only support pins-are-numbered format\n", ret);
		return -EINVAL;
	}

	node = of_parse_phandle(np, "mediatek,pctl-regmap", 0);
	if (node) {
		pctl->regmap1 = syscon_node_to_regmap(node);
		if (IS_ERR(pctl->regmap1))
			return PTR_ERR(pctl->regmap1);
	}

	/* Only 8135 has two base addr, other SoCs have only one. */
	node = of_parse_phandle(np, "mediatek,pctl-regmap", 1);
	if (node) {
		pctl->regmap2 = syscon_node_to_regmap(node);
		if (IS_ERR(pctl->regmap2))
			return PTR_ERR(pctl->regmap2);
	}

	pctl->devdata = data;
	ret = mtk_pctrl_build_state(pdev);
	if (ret) {
		dev_err(&pdev->dev, "build state failed: %d\n", ret);
		return -EINVAL;
	}

	pins = devm_kzalloc(&pdev->dev,
			    pctl->devdata->npins * sizeof(*pins),
			    GFP_KERNEL);
	if (!pins)
		return -ENOMEM;

	for (i = 0; i < pctl->devdata->npins; i++)
		pins[i] = pctl->devdata->pins[i].pin;
	mtk_pctrl_desc.name = dev_name(&pdev->dev);
	mtk_pctrl_desc.owner = THIS_MODULE;
	mtk_pctrl_desc.pins = pins;
	mtk_pctrl_desc.npins = pctl->devdata->npins;
	pctl->dev = &pdev->dev;
	pctl->pctl_dev = pinctrl_register(&mtk_pctrl_desc, &pdev->dev, pctl);
	if (!pctl->pctl_dev) {
		dev_err(&pdev->dev, "couldn't register pinctrl driver\n");
		return -EINVAL;
	}

	pctl->chip = devm_kzalloc(&pdev->dev, sizeof(*pctl->chip), GFP_KERNEL);
	if (!pctl->chip) {
		ret = -ENOMEM;
		goto pctrl_error;
	}

	pctl->chip = &mtk_gpio_chip;
	pctl->chip->ngpio = pctl->devdata->npins;
	pctl->chip->label = dev_name(&pdev->dev);
	pctl->chip->dev = &pdev->dev;
	pctl->chip->base = 0;

	ret = gpiochip_add(pctl->chip);
	if (ret) {
		ret = -EINVAL;
		goto pctrl_error;
	}

	/* Register the GPIO to pin mappings. */
	ret = gpiochip_add_pin_range(pctl->chip, dev_name(&pdev->dev),
			0, 0, pctl->devdata->npins);
	if (ret) {
		ret = -EINVAL;
		goto chip_error;
	}

	/* Get EINT register base from dts. */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Unable to get Pinctrl resource\n");
		ret = -EINVAL;
		goto chip_error;
	}

	pctl->eint_reg_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pctl->eint_reg_base)) {
		ret = -EINVAL;
		goto chip_error;
	}

	irq = irq_of_parse_and_map(np, 0);
	if (!irq) {
		dev_err(&pdev->dev, "couldn't parse and map irq\n");
		ret = -EINVAL;
		goto chip_error;
	}

	pctl->domain = irq_domain_add_linear(np,
		pctl->devdata->ap_num, &irq_domain_simple_ops, NULL);
	if (!pctl->domain) {
		dev_err(&pdev->dev, "Couldn't register IRQ domain\n");
		ret = -ENOMEM;
		goto chip_error;
	}

	mtk_eint_init(pctl);
	for (i = 0; i < pctl->devdata->ap_num; i++) {
		int virq = irq_create_mapping(pctl->domain, i);

		irq_set_chip_and_handler(virq, &mtk_pinctrl_irq_chip,
			handle_level_irq);
		irq_set_chip_data(virq, pctl);
		set_irq_flags(virq, IRQF_VALID);
	};

	irq_set_chained_handler(irq, mtk_eint_irq_handler);
	irq_set_handler_data(irq, pctl);
	set_irq_flags(irq, IRQF_VALID);
	return 0;

chip_error:
	gpiochip_remove(pctl->chip);
pctrl_error:
	pinctrl_unregister(pctl->pctl_dev);
	return ret;
}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MediaTek Pinctrl Driver");
MODULE_AUTHOR("Hongzhou Yang <hongzhou.yang@mediatek.com>");
