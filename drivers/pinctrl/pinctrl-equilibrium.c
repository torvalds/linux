// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2019 Intel Corporation */

#include <linux/gpio/driver.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>

#include "core.h"
#include "pinconf.h"
#include "pinmux.h"
#include "pinctrl-equilibrium.h"

#define PIN_NAME_FMT	"io-%d"
#define PIN_NAME_LEN	10
#define PAD_REG_OFF	0x100

static void eqbr_gpio_disable_irq(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct eqbr_gpio_ctrl *gctrl = gpiochip_get_data(gc);
	unsigned int offset = irqd_to_hwirq(d);
	unsigned long flags;

	raw_spin_lock_irqsave(&gctrl->lock, flags);
	writel(BIT(offset), gctrl->membase + GPIO_IRNENCLR);
	raw_spin_unlock_irqrestore(&gctrl->lock, flags);
}

static void eqbr_gpio_enable_irq(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct eqbr_gpio_ctrl *gctrl = gpiochip_get_data(gc);
	unsigned int offset = irqd_to_hwirq(d);
	unsigned long flags;

	gc->direction_input(gc, offset);
	raw_spin_lock_irqsave(&gctrl->lock, flags);
	writel(BIT(offset), gctrl->membase + GPIO_IRNRNSET);
	raw_spin_unlock_irqrestore(&gctrl->lock, flags);
}

static void eqbr_gpio_ack_irq(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct eqbr_gpio_ctrl *gctrl = gpiochip_get_data(gc);
	unsigned int offset = irqd_to_hwirq(d);
	unsigned long flags;

	raw_spin_lock_irqsave(&gctrl->lock, flags);
	writel(BIT(offset), gctrl->membase + GPIO_IRNCR);
	raw_spin_unlock_irqrestore(&gctrl->lock, flags);
}

static void eqbr_gpio_mask_ack_irq(struct irq_data *d)
{
	eqbr_gpio_disable_irq(d);
	eqbr_gpio_ack_irq(d);
}

static inline void eqbr_cfg_bit(void __iomem *addr,
				unsigned int offset, unsigned int set)
{
	if (set)
		writel(readl(addr) | BIT(offset), addr);
	else
		writel(readl(addr) & ~BIT(offset), addr);
}

static int eqbr_irq_type_cfg(struct gpio_irq_type *type,
			     struct eqbr_gpio_ctrl *gctrl,
			     unsigned int offset)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&gctrl->lock, flags);
	eqbr_cfg_bit(gctrl->membase + GPIO_IRNCFG, offset, type->trig_type);
	eqbr_cfg_bit(gctrl->membase + GPIO_EXINTCR1, offset, type->trig_type);
	eqbr_cfg_bit(gctrl->membase + GPIO_EXINTCR0, offset, type->logic_type);
	raw_spin_unlock_irqrestore(&gctrl->lock, flags);

	return 0;
}

static int eqbr_gpio_set_irq_type(struct irq_data *d, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct eqbr_gpio_ctrl *gctrl = gpiochip_get_data(gc);
	unsigned int offset = irqd_to_hwirq(d);
	struct gpio_irq_type it;

	memset(&it, 0, sizeof(it));

	if ((type & IRQ_TYPE_SENSE_MASK) == IRQ_TYPE_NONE)
		return 0;

	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		it.trig_type = GPIO_EDGE_TRIG;
		it.edge_type = GPIO_SINGLE_EDGE;
		it.logic_type = GPIO_POSITIVE_TRIG;
		break;

	case IRQ_TYPE_EDGE_FALLING:
		it.trig_type = GPIO_EDGE_TRIG;
		it.edge_type = GPIO_SINGLE_EDGE;
		it.logic_type = GPIO_NEGATIVE_TRIG;
		break;

	case IRQ_TYPE_EDGE_BOTH:
		it.trig_type = GPIO_EDGE_TRIG;
		it.edge_type = GPIO_BOTH_EDGE;
		it.logic_type = GPIO_POSITIVE_TRIG;
		break;

	case IRQ_TYPE_LEVEL_HIGH:
		it.trig_type = GPIO_LEVEL_TRIG;
		it.edge_type = GPIO_SINGLE_EDGE;
		it.logic_type = GPIO_POSITIVE_TRIG;
		break;

	case IRQ_TYPE_LEVEL_LOW:
		it.trig_type = GPIO_LEVEL_TRIG;
		it.edge_type = GPIO_SINGLE_EDGE;
		it.logic_type = GPIO_NEGATIVE_TRIG;
		break;

	default:
		return -EINVAL;
	}

	eqbr_irq_type_cfg(&it, gctrl, offset);
	if (it.trig_type == GPIO_EDGE_TRIG)
		irq_set_handler_locked(d, handle_edge_irq);
	else
		irq_set_handler_locked(d, handle_level_irq);

	return 0;
}

static void eqbr_irq_handler(struct irq_desc *desc)
{
	struct gpio_chip *gc = irq_desc_get_handler_data(desc);
	struct eqbr_gpio_ctrl *gctrl = gpiochip_get_data(gc);
	struct irq_chip *ic = irq_desc_get_chip(desc);
	unsigned long pins, offset;

	chained_irq_enter(ic, desc);
	pins = readl(gctrl->membase + GPIO_IRNCR);

	for_each_set_bit(offset, &pins, gc->ngpio)
		generic_handle_irq(irq_find_mapping(gc->irq.domain, offset));

	chained_irq_exit(ic, desc);
}

static int gpiochip_setup(struct device *dev, struct eqbr_gpio_ctrl *gctrl)
{
	struct gpio_irq_chip *girq;
	struct gpio_chip *gc;

	gc = &gctrl->chip;
	gc->label = gctrl->name;
#if defined(CONFIG_OF_GPIO)
	gc->of_node = gctrl->node;
#endif

	if (!of_property_read_bool(gctrl->node, "interrupt-controller")) {
		dev_dbg(dev, "gc %s: doesn't act as interrupt controller!\n",
			gctrl->name);
		return 0;
	}

	gctrl->ic.name = "gpio_irq";
	gctrl->ic.irq_mask = eqbr_gpio_disable_irq;
	gctrl->ic.irq_unmask = eqbr_gpio_enable_irq;
	gctrl->ic.irq_ack = eqbr_gpio_ack_irq;
	gctrl->ic.irq_mask_ack = eqbr_gpio_mask_ack_irq;
	gctrl->ic.irq_set_type = eqbr_gpio_set_irq_type;

	girq = &gctrl->chip.irq;
	girq->chip = &gctrl->ic;
	girq->parent_handler = eqbr_irq_handler;
	girq->num_parents = 1;
	girq->parents = devm_kcalloc(dev, 1, sizeof(*girq->parents), GFP_KERNEL);
	if (!girq->parents)
		return -ENOMEM;

	girq->default_type = IRQ_TYPE_NONE;
	girq->handler = handle_bad_irq;
	girq->parents[0] = gctrl->virq;

	return 0;
}

static int gpiolib_reg(struct eqbr_pinctrl_drv_data *drvdata)
{
	struct device *dev = drvdata->dev;
	struct eqbr_gpio_ctrl *gctrl;
	struct device_node *np;
	struct resource res;
	int i, ret;

	for (i = 0; i < drvdata->nr_gpio_ctrls; i++) {
		gctrl = drvdata->gpio_ctrls + i;
		np = gctrl->node;

		gctrl->name = devm_kasprintf(dev, GFP_KERNEL, "gpiochip%d", i);
		if (!gctrl->name)
			return -ENOMEM;

		if (of_address_to_resource(np, 0, &res)) {
			dev_err(dev, "Failed to get GPIO register address\n");
			return -ENXIO;
		}

		gctrl->membase = devm_ioremap_resource(dev, &res);
		if (IS_ERR(gctrl->membase))
			return PTR_ERR(gctrl->membase);

		gctrl->virq = irq_of_parse_and_map(np, 0);
		if (!gctrl->virq) {
			dev_err(dev, "%s: failed to parse and map irq\n",
				gctrl->name);
			return -ENXIO;
		}
		raw_spin_lock_init(&gctrl->lock);

		ret = bgpio_init(&gctrl->chip, dev, gctrl->bank->nr_pins / 8,
				 gctrl->membase + GPIO_IN,
				 gctrl->membase + GPIO_OUTSET,
				 gctrl->membase + GPIO_OUTCLR,
				 gctrl->membase + GPIO_DIR,
				 NULL, 0);
		if (ret) {
			dev_err(dev, "unable to init generic GPIO\n");
			return ret;
		}

		ret = gpiochip_setup(dev, gctrl);
		if (ret)
			return ret;

		ret = devm_gpiochip_add_data(dev, &gctrl->chip, gctrl);
		if (ret)
			return ret;
	}

	return 0;
}

static inline struct eqbr_pin_bank
*find_pinbank_via_pin(struct eqbr_pinctrl_drv_data *pctl, unsigned int pin)
{
	struct eqbr_pin_bank *bank;
	int i;

	for (i = 0; i < pctl->nr_banks; i++) {
		bank = &pctl->pin_banks[i];
		if (pin >= bank->pin_base &&
		    (pin - bank->pin_base) < bank->nr_pins)
			return bank;
	}

	return NULL;
}

static const struct pinctrl_ops eqbr_pctl_ops = {
	.get_groups_count	= pinctrl_generic_get_group_count,
	.get_group_name		= pinctrl_generic_get_group_name,
	.get_group_pins		= pinctrl_generic_get_group_pins,
	.dt_node_to_map		= pinconf_generic_dt_node_to_map_all,
	.dt_free_map		= pinconf_generic_dt_free_map,
};

static int eqbr_set_pin_mux(struct eqbr_pinctrl_drv_data *pctl,
			    unsigned int pmx, unsigned int pin)
{
	struct eqbr_pin_bank *bank;
	unsigned long flags;
	unsigned int offset;
	void __iomem *mem;

	bank = find_pinbank_via_pin(pctl, pin);
	if (!bank) {
		dev_err(pctl->dev, "Couldn't find pin bank for pin %u\n", pin);
		return -ENODEV;
	}
	mem = bank->membase;
	offset = pin - bank->pin_base;

	if (!(bank->aval_pinmap & BIT(offset))) {
		dev_err(pctl->dev,
			"PIN: %u is not valid, pinbase: %u, bitmap: %u\n",
			pin, bank->pin_base, bank->aval_pinmap);
		return -ENODEV;
	}

	raw_spin_lock_irqsave(&pctl->lock, flags);
	writel(pmx, mem + (offset * 4));
	raw_spin_unlock_irqrestore(&pctl->lock, flags);
	return 0;
}

static int eqbr_pinmux_set_mux(struct pinctrl_dev *pctldev,
			       unsigned int selector, unsigned int group)
{
	struct eqbr_pinctrl_drv_data *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct function_desc *func;
	struct group_desc *grp;
	unsigned int *pinmux;
	int i;

	func = pinmux_generic_get_function(pctldev, selector);
	if (!func)
		return -EINVAL;

	grp = pinctrl_generic_get_group(pctldev, group);
	if (!grp)
		return -EINVAL;

	pinmux = grp->data;
	for (i = 0; i < grp->num_pins; i++)
		eqbr_set_pin_mux(pctl, pinmux[i], grp->pins[i]);

	return 0;
}

static int eqbr_pinmux_gpio_request(struct pinctrl_dev *pctldev,
				    struct pinctrl_gpio_range *range,
				    unsigned int pin)
{
	struct eqbr_pinctrl_drv_data *pctl = pinctrl_dev_get_drvdata(pctldev);

	return eqbr_set_pin_mux(pctl, EQBR_GPIO_MODE, pin);
}

static const struct pinmux_ops eqbr_pinmux_ops = {
	.get_functions_count	= pinmux_generic_get_function_count,
	.get_function_name	= pinmux_generic_get_function_name,
	.get_function_groups	= pinmux_generic_get_function_groups,
	.set_mux		= eqbr_pinmux_set_mux,
	.gpio_request_enable	= eqbr_pinmux_gpio_request,
	.strict			= true,
};

static int get_drv_cur(void __iomem *mem, unsigned int offset)
{
	unsigned int idx = offset / DRV_CUR_PINS; /* 0-15, 16-31 per register*/
	unsigned int pin_offset = offset % DRV_CUR_PINS;

	return PARSE_DRV_CURRENT(readl(mem + REG_DRCC(idx)), pin_offset);
}

static struct eqbr_gpio_ctrl
*get_gpio_ctrls_via_bank(struct eqbr_pinctrl_drv_data *pctl,
			struct eqbr_pin_bank *bank)
{
	int i;

	for (i = 0; i < pctl->nr_gpio_ctrls; i++) {
		if (pctl->gpio_ctrls[i].bank == bank)
			return &pctl->gpio_ctrls[i];
	}

	return NULL;
}

static int eqbr_pinconf_get(struct pinctrl_dev *pctldev, unsigned int pin,
			    unsigned long *config)
{
	struct eqbr_pinctrl_drv_data *pctl = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param = pinconf_to_config_param(*config);
	struct eqbr_gpio_ctrl *gctrl;
	struct eqbr_pin_bank *bank;
	unsigned long flags;
	unsigned int offset;
	void __iomem *mem;
	u32 val;

	bank = find_pinbank_via_pin(pctl, pin);
	if (!bank) {
		dev_err(pctl->dev, "Couldn't find pin bank for pin %u\n", pin);
		return -ENODEV;
	}
	mem = bank->membase;
	offset = pin - bank->pin_base;

	if (!(bank->aval_pinmap & BIT(offset))) {
		dev_err(pctl->dev,
			"PIN: %u is not valid, pinbase: %u, bitmap: %u\n",
			pin, bank->pin_base, bank->aval_pinmap);
		return -ENODEV;
	}

	raw_spin_lock_irqsave(&pctl->lock, flags);
	switch (param) {
	case PIN_CONFIG_BIAS_PULL_UP:
		val = !!(readl(mem + REG_PUEN) & BIT(offset));
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		val = !!(readl(mem + REG_PDEN) & BIT(offset));
		break;
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		val = !!(readl(mem + REG_OD) & BIT(offset));
		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		val = get_drv_cur(mem, offset);
		break;
	case PIN_CONFIG_SLEW_RATE:
		val = !!(readl(mem + REG_SRC) & BIT(offset));
		break;
	case PIN_CONFIG_OUTPUT_ENABLE:
		gctrl = get_gpio_ctrls_via_bank(pctl, bank);
		if (!gctrl) {
			dev_err(pctl->dev, "Failed to find gpio via bank pinbase: %u, pin: %u\n",
				bank->pin_base, pin);
			raw_spin_unlock_irqrestore(&pctl->lock, flags);
			return -ENODEV;
		}
		val = !!(readl(gctrl->membase + GPIO_DIR) & BIT(offset));
		break;
	default:
		raw_spin_unlock_irqrestore(&pctl->lock, flags);
		return -ENOTSUPP;
	}
	raw_spin_unlock_irqrestore(&pctl->lock, flags);
	*config = pinconf_to_config_packed(param, val);
;
	return 0;
}

static int eqbr_pinconf_set(struct pinctrl_dev *pctldev, unsigned int pin,
			    unsigned long *configs, unsigned int num_configs)
{
	struct eqbr_pinctrl_drv_data *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct eqbr_gpio_ctrl *gctrl;
	enum pin_config_param param;
	struct eqbr_pin_bank *bank;
	unsigned int val, offset;
	struct gpio_chip *gc;
	unsigned long flags;
	void __iomem *mem;
	u32 regval, mask;
	int i;

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		val = pinconf_to_config_argument(configs[i]);

		bank = find_pinbank_via_pin(pctl, pin);
		if (!bank) {
			dev_err(pctl->dev,
				"Couldn't find pin bank for pin %u\n", pin);
			return -ENODEV;
		}
		mem = bank->membase;
		offset = pin - bank->pin_base;

		switch (param) {
		case PIN_CONFIG_BIAS_PULL_UP:
			mem += REG_PUEN;
			mask = BIT(offset);
			break;
		case PIN_CONFIG_BIAS_PULL_DOWN:
			mem += REG_PDEN;
			mask = BIT(offset);
			break;
		case PIN_CONFIG_DRIVE_OPEN_DRAIN:
			mem += REG_OD;
			mask = BIT(offset);
			break;
		case PIN_CONFIG_DRIVE_STRENGTH:
			mem += REG_DRCC(offset / DRV_CUR_PINS);
			offset = (offset % DRV_CUR_PINS) * 2;
			mask = GENMASK(1, 0) << offset;
			break;
		case PIN_CONFIG_SLEW_RATE:
			mem += REG_SRC;
			mask = BIT(offset);
			break;
		case PIN_CONFIG_OUTPUT_ENABLE:
			gctrl = get_gpio_ctrls_via_bank(pctl, bank);
			if (!gctrl) {
				dev_err(pctl->dev, "Failed to find gpio via bank pinbase: %u, pin: %u\n",
					bank->pin_base, pin);
				return -ENODEV;
			}
			gc = &gctrl->chip;
			gc->direction_output(gc, offset, 0);
			continue;
		default:
			return -ENOTSUPP;
		}

		raw_spin_lock_irqsave(&pctl->lock, flags);
		regval = readl(mem);
		regval = (regval & ~mask) | ((val << offset) & mask);
		writel(regval, mem);
		raw_spin_unlock_irqrestore(&pctl->lock, flags);
	}

	return 0;
}

static int eqbr_pinconf_group_get(struct pinctrl_dev *pctldev,
				  unsigned int group, unsigned long *config)
{
	unsigned int i, npins, old = 0;
	const unsigned int *pins;
	int ret;

	ret = pinctrl_generic_get_group_pins(pctldev, group, &pins, &npins);
	if (ret)
		return ret;

	for (i = 0; i < npins; i++) {
		if (eqbr_pinconf_get(pctldev, pins[i], config))
			return -ENOTSUPP;

		if (i && old != *config)
			return -ENOTSUPP;

		old = *config;
	}
	return 0;
}

static int eqbr_pinconf_group_set(struct pinctrl_dev *pctldev,
				  unsigned int group, unsigned long *configs,
				  unsigned int num_configs)
{
	const unsigned int *pins;
	unsigned int i, npins;
	int ret;

	ret = pinctrl_generic_get_group_pins(pctldev, group, &pins, &npins);
	if (ret)
		return ret;

	for (i = 0; i < npins; i++) {
		ret = eqbr_pinconf_set(pctldev, pins[i], configs, num_configs);
		if (ret)
			return ret;
	}
	return 0;
}

static const struct pinconf_ops eqbr_pinconf_ops = {
	.is_generic			= true,
	.pin_config_get			= eqbr_pinconf_get,
	.pin_config_set			= eqbr_pinconf_set,
	.pin_config_group_get		= eqbr_pinconf_group_get,
	.pin_config_group_set		= eqbr_pinconf_group_set,
	.pin_config_config_dbg_show	= pinconf_generic_dump_config,
};

static bool is_func_exist(struct eqbr_pmx_func *funcs, const char *name,
			 unsigned int nr_funcs, unsigned int *idx)
{
	int i;

	if (!funcs)
		return false;

	for (i = 0; i < nr_funcs; i++) {
		if (funcs[i].name && !strcmp(funcs[i].name, name)) {
			*idx = i;
			return true;
		}
	}

	return false;
}

static int funcs_utils(struct device *dev, struct eqbr_pmx_func *funcs,
		       unsigned int *nr_funcs, funcs_util_ops op)
{
	struct device_node *node = dev->of_node;
	struct device_node *np;
	struct property *prop;
	const char *fn_name;
	unsigned int fid;
	int i, j;

	i = 0;
	for_each_child_of_node(node, np) {
		prop = of_find_property(np, "groups", NULL);
		if (!prop)
			continue;

		if (of_property_read_string(np, "function", &fn_name)) {
			/* some groups may not have function, it's OK */
			dev_dbg(dev, "Group %s: not function binded!\n",
				(char *)prop->value);
			continue;
		}

		switch (op) {
		case OP_COUNT_NR_FUNCS:
			if (!is_func_exist(funcs, fn_name, *nr_funcs, &fid))
				*nr_funcs = *nr_funcs + 1;
			break;

		case OP_ADD_FUNCS:
			if (!is_func_exist(funcs, fn_name, *nr_funcs, &fid))
				funcs[i].name = fn_name;
			break;

		case OP_COUNT_NR_FUNC_GRPS:
			if (is_func_exist(funcs, fn_name, *nr_funcs, &fid))
				funcs[fid].nr_groups++;
			break;

		case OP_ADD_FUNC_GRPS:
			if (is_func_exist(funcs, fn_name, *nr_funcs, &fid)) {
				for (j = 0; j < funcs[fid].nr_groups; j++)
					if (!funcs[fid].groups[j])
						break;
				funcs[fid].groups[j] = prop->value;
			}
			break;

		default:
				return -EINVAL;
		}
		i++;
	}

	return 0;
}

static int eqbr_build_functions(struct eqbr_pinctrl_drv_data *drvdata)
{
	struct device *dev = drvdata->dev;
	struct eqbr_pmx_func *funcs = NULL;
	unsigned int nr_funcs = 0;
	int i, ret;

	ret = funcs_utils(dev, funcs, &nr_funcs, OP_COUNT_NR_FUNCS);
	if (ret)
		return ret;

	funcs = devm_kcalloc(dev, nr_funcs, sizeof(*funcs), GFP_KERNEL);
	if (!funcs)
		return -ENOMEM;

	ret = funcs_utils(dev, funcs, &nr_funcs, OP_ADD_FUNCS);
	if (ret)
		return ret;

	ret = funcs_utils(dev, funcs, &nr_funcs, OP_COUNT_NR_FUNC_GRPS);
	if (ret)
		return ret;

	for (i = 0; i < nr_funcs; i++) {
		if (!funcs[i].nr_groups)
			continue;
		funcs[i].groups = devm_kcalloc(dev, funcs[i].nr_groups,
					       sizeof(*(funcs[i].groups)),
					       GFP_KERNEL);
		if (!funcs[i].groups)
			return -ENOMEM;
	}

	ret = funcs_utils(dev, funcs, &nr_funcs, OP_ADD_FUNC_GRPS);
	if (ret)
		return ret;

	for (i = 0; i < nr_funcs; i++) {

		/* Ignore the same function with multiple groups */
		if (funcs[i].name == NULL)
			continue;

		ret = pinmux_generic_add_function(drvdata->pctl_dev,
						  funcs[i].name,
						  funcs[i].groups,
						  funcs[i].nr_groups,
						  drvdata);
		if (ret < 0) {
			dev_err(dev, "Failed to register function %s\n",
				funcs[i].name);
			return ret;
		}
	}

	return 0;
}

static int eqbr_build_groups(struct eqbr_pinctrl_drv_data *drvdata)
{
	struct device *dev = drvdata->dev;
	struct device_node *node = dev->of_node;
	unsigned int *pinmux, pin_id, pinmux_id;
	struct group_desc group;
	struct device_node *np;
	struct property *prop;
	int j, err;

	for_each_child_of_node(node, np) {
		prop = of_find_property(np, "groups", NULL);
		if (!prop)
			continue;

		group.num_pins = of_property_count_u32_elems(np, "pins");
		if (group.num_pins < 0) {
			dev_err(dev, "No pins in the group: %s\n", prop->name);
			return -EINVAL;
		}
		group.name = prop->value;
		group.pins = devm_kcalloc(dev, group.num_pins,
					  sizeof(*(group.pins)), GFP_KERNEL);
		if (!group.pins)
			return -ENOMEM;

		pinmux = devm_kcalloc(dev, group.num_pins, sizeof(*pinmux),
				      GFP_KERNEL);
		if (!pinmux)
			return -ENOMEM;

		for (j = 0; j < group.num_pins; j++) {
			if (of_property_read_u32_index(np, "pins", j, &pin_id)) {
				dev_err(dev, "Group %s: Read intel pins id failed\n",
					group.name);
				return -EINVAL;
			}
			if (pin_id >= drvdata->pctl_desc.npins) {
				dev_err(dev, "Group %s: Invalid pin ID, idx: %d, pin %u\n",
					group.name, j, pin_id);
				return -EINVAL;
			}
			group.pins[j] = pin_id;
			if (of_property_read_u32_index(np, "pinmux", j, &pinmux_id)) {
				dev_err(dev, "Group %s: Read intel pinmux id failed\n",
					group.name);
				return -EINVAL;
			}
			pinmux[j] = pinmux_id;
		}

		err = pinctrl_generic_add_group(drvdata->pctl_dev, group.name,
						group.pins, group.num_pins,
						pinmux);
		if (err < 0) {
			dev_err(dev, "Failed to register group %s\n", group.name);
			return err;
		}
		memset(&group, 0, sizeof(group));
		pinmux = NULL;
	}

	return 0;
}

static int pinctrl_reg(struct eqbr_pinctrl_drv_data *drvdata)
{
	struct pinctrl_desc *pctl_desc;
	struct pinctrl_pin_desc *pdesc;
	struct device *dev;
	unsigned int nr_pins;
	char *pin_names;
	int i, ret;

	dev = drvdata->dev;
	pctl_desc = &drvdata->pctl_desc;
	pctl_desc->name = "eqbr-pinctrl";
	pctl_desc->owner = THIS_MODULE;
	pctl_desc->pctlops = &eqbr_pctl_ops;
	pctl_desc->pmxops = &eqbr_pinmux_ops;
	pctl_desc->confops = &eqbr_pinconf_ops;
	raw_spin_lock_init(&drvdata->lock);

	for (i = 0, nr_pins = 0; i < drvdata->nr_banks; i++)
		nr_pins += drvdata->pin_banks[i].nr_pins;

	pdesc = devm_kcalloc(dev, nr_pins, sizeof(*pdesc), GFP_KERNEL);
	if (!pdesc)
		return -ENOMEM;
	pin_names = devm_kcalloc(dev, nr_pins, PIN_NAME_LEN, GFP_KERNEL);
	if (!pin_names)
		return -ENOMEM;

	for (i = 0; i < nr_pins; i++) {
		sprintf(pin_names, PIN_NAME_FMT, i);
		pdesc[i].number = i;
		pdesc[i].name = pin_names;
		pin_names += PIN_NAME_LEN;
	}
	pctl_desc->pins = pdesc;
	pctl_desc->npins = nr_pins;
	dev_dbg(dev, "pinctrl total pin number: %u\n", nr_pins);

	ret = devm_pinctrl_register_and_init(dev, pctl_desc, drvdata,
					     &drvdata->pctl_dev);
	if (ret)
		return ret;

	ret = eqbr_build_groups(drvdata);
	if (ret) {
		dev_err(dev, "Failed to build groups\n");
		return ret;
	}

	ret = eqbr_build_functions(drvdata);
	if (ret) {
		dev_err(dev, "Failed to build functions\n");
		return ret;
	}

	return pinctrl_enable(drvdata->pctl_dev);
}

static int pinbank_init(struct device_node *np,
			struct eqbr_pinctrl_drv_data *drvdata,
			struct eqbr_pin_bank *bank, unsigned int id)
{
	struct device *dev = drvdata->dev;
	struct of_phandle_args spec;
	int ret;

	bank->membase = drvdata->membase + id * PAD_REG_OFF;

	ret = of_parse_phandle_with_fixed_args(np, "gpio-ranges", 3, 0, &spec);
	if (ret) {
		dev_err(dev, "gpio-range not available!\n");
		return ret;
	}

	bank->pin_base = spec.args[1];
	bank->nr_pins = spec.args[2];

	bank->aval_pinmap = readl(bank->membase + REG_AVAIL);
	bank->id = id;

	dev_dbg(dev, "pinbank id: %d, reg: %px, pinbase: %u, pin number: %u, pinmap: 0x%x\n",
		id, bank->membase, bank->pin_base,
		bank->nr_pins, bank->aval_pinmap);

	return ret;
}

static int pinbank_probe(struct eqbr_pinctrl_drv_data *drvdata)
{
	struct device *dev = drvdata->dev;
	struct device_node *np_gpio;
	struct eqbr_gpio_ctrl *gctrls;
	struct eqbr_pin_bank *banks;
	int i, nr_gpio;

	/* Count gpio bank number */
	nr_gpio = 0;
	for_each_node_by_name(np_gpio, "gpio") {
		if (of_device_is_available(np_gpio))
			nr_gpio++;
	}

	if (!nr_gpio) {
		dev_err(dev, "NO pin bank available!\n");
		return -ENODEV;
	}

	/* Count pin bank number and gpio controller number */
	banks = devm_kcalloc(dev, nr_gpio, sizeof(*banks), GFP_KERNEL);
	if (!banks)
		return -ENOMEM;

	gctrls = devm_kcalloc(dev, nr_gpio, sizeof(*gctrls), GFP_KERNEL);
	if (!gctrls)
		return -ENOMEM;

	dev_dbg(dev, "found %d gpio controller!\n", nr_gpio);

	/* Initialize Pin bank */
	i = 0;
	for_each_node_by_name(np_gpio, "gpio") {
		if (!of_device_is_available(np_gpio))
			continue;

		pinbank_init(np_gpio, drvdata, banks + i, i);

		gctrls[i].node = np_gpio;
		gctrls[i].bank = banks + i;
		i++;
	}

	drvdata->pin_banks = banks;
	drvdata->nr_banks = nr_gpio;
	drvdata->gpio_ctrls = gctrls;
	drvdata->nr_gpio_ctrls = nr_gpio;

	return 0;
}

static int eqbr_pinctrl_probe(struct platform_device *pdev)
{
	struct eqbr_pinctrl_drv_data *drvdata;
	struct device *dev = &pdev->dev;
	int ret;

	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	drvdata->dev = dev;

	drvdata->membase = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(drvdata->membase))
		return PTR_ERR(drvdata->membase);

	ret = pinbank_probe(drvdata);
	if (ret)
		return ret;

	ret = pinctrl_reg(drvdata);
	if (ret)
		return ret;

	ret = gpiolib_reg(drvdata);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, drvdata);
	return 0;
}

static const struct of_device_id eqbr_pinctrl_dt_match[] = {
	{ .compatible = "intel,lgm-io" },
	{}
};
MODULE_DEVICE_TABLE(of, eqbr_pinctrl_dt_match);

static struct platform_driver eqbr_pinctrl_driver = {
	.probe	= eqbr_pinctrl_probe,
	.driver = {
		.name = "eqbr-pinctrl",
		.of_match_table = eqbr_pinctrl_dt_match,
	},
};

module_platform_driver(eqbr_pinctrl_driver);

MODULE_AUTHOR("Zhu Yixin <yixin.zhu@intel.com>, Rahul Tanwar <rahul.tanwar@intel.com>");
MODULE_DESCRIPTION("Pinctrl Driver for LGM SoC (Equilibrium)");
MODULE_LICENSE("GPL v2");
