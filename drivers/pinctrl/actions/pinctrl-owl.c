// SPDX-License-Identifier: GPL-2.0+
/*
 * OWL SoC's Pinctrl driver
 *
 * Copyright (c) 2014 Actions Semi Inc.
 * Author: David Liu <liuwei@actions-semi.com>
 *
 * Copyright (c) 2018 Linaro Ltd.
 * Author: Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "../core.h"
#include "../pinctrl-utils.h"
#include "pinctrl-owl.h"

/**
 * struct owl_pinctrl - pinctrl state of the device
 * @dev: device handle
 * @pctrldev: pinctrl handle
 * @chip: gpio chip
 * @lock: spinlock to protect registers
 * @clk: clock control
 * @soc: reference to soc_data
 * @base: pinctrl register base address
 * @irq_chip: IRQ chip information
 * @num_irq: number of possible interrupts
 * @irq: interrupt numbers
 */
struct owl_pinctrl {
	struct device *dev;
	struct pinctrl_dev *pctrldev;
	struct gpio_chip chip;
	raw_spinlock_t lock;
	struct clk *clk;
	const struct owl_pinctrl_soc_data *soc;
	void __iomem *base;
	struct irq_chip irq_chip;
	unsigned int num_irq;
	unsigned int *irq;
};

static void owl_update_bits(void __iomem *base, u32 mask, u32 val)
{
	u32 reg_val;

	reg_val = readl_relaxed(base);

	reg_val = (reg_val & ~mask) | (val & mask);

	writel_relaxed(reg_val, base);
}

static u32 owl_read_field(struct owl_pinctrl *pctrl, u32 reg,
				u32 bit, u32 width)
{
	u32 tmp, mask;

	tmp = readl_relaxed(pctrl->base + reg);
	mask = (1 << width) - 1;

	return (tmp >> bit) & mask;
}

static void owl_write_field(struct owl_pinctrl *pctrl, u32 reg, u32 arg,
				u32 bit, u32 width)
{
	u32 mask;

	mask = (1 << width) - 1;
	mask = mask << bit;

	owl_update_bits(pctrl->base + reg, mask, (arg << bit));
}

static int owl_get_groups_count(struct pinctrl_dev *pctrldev)
{
	struct owl_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctrldev);

	return pctrl->soc->ngroups;
}

static const char *owl_get_group_name(struct pinctrl_dev *pctrldev,
				unsigned int group)
{
	struct owl_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctrldev);

	return pctrl->soc->groups[group].name;
}

static int owl_get_group_pins(struct pinctrl_dev *pctrldev,
				unsigned int group,
				const unsigned int **pins,
				unsigned int *num_pins)
{
	struct owl_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctrldev);

	*pins = pctrl->soc->groups[group].pads;
	*num_pins = pctrl->soc->groups[group].npads;

	return 0;
}

static void owl_pin_dbg_show(struct pinctrl_dev *pctrldev,
				struct seq_file *s,
				unsigned int offset)
{
	struct owl_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctrldev);

	seq_printf(s, "%s", dev_name(pctrl->dev));
}

static const struct pinctrl_ops owl_pinctrl_ops = {
	.get_groups_count = owl_get_groups_count,
	.get_group_name = owl_get_group_name,
	.get_group_pins = owl_get_group_pins,
	.pin_dbg_show = owl_pin_dbg_show,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_all,
	.dt_free_map = pinctrl_utils_free_map,
};

static int owl_get_funcs_count(struct pinctrl_dev *pctrldev)
{
	struct owl_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctrldev);

	return pctrl->soc->nfunctions;
}

static const char *owl_get_func_name(struct pinctrl_dev *pctrldev,
				unsigned int function)
{
	struct owl_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctrldev);

	return pctrl->soc->functions[function].name;
}

static int owl_get_func_groups(struct pinctrl_dev *pctrldev,
				unsigned int function,
				const char * const **groups,
				unsigned int * const num_groups)
{
	struct owl_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctrldev);

	*groups = pctrl->soc->functions[function].groups;
	*num_groups = pctrl->soc->functions[function].ngroups;

	return 0;
}

static inline int get_group_mfp_mask_val(const struct owl_pingroup *g,
				int function,
				u32 *mask,
				u32 *val)
{
	int id;
	u32 option_num;
	u32 option_mask;

	for (id = 0; id < g->nfuncs; id++) {
		if (g->funcs[id] == function)
			break;
	}
	if (WARN_ON(id == g->nfuncs))
		return -EINVAL;

	option_num = (1 << g->mfpctl_width);
	if (id > option_num)
		id -= option_num;

	option_mask = option_num - 1;
	*mask = (option_mask  << g->mfpctl_shift);
	*val = (id << g->mfpctl_shift);

	return 0;
}

static int owl_set_mux(struct pinctrl_dev *pctrldev,
				unsigned int function,
				unsigned int group)
{
	struct owl_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctrldev);
	const struct owl_pingroup *g;
	unsigned long flags;
	u32 val, mask;

	g = &pctrl->soc->groups[group];

	if (get_group_mfp_mask_val(g, function, &mask, &val))
		return -EINVAL;

	raw_spin_lock_irqsave(&pctrl->lock, flags);

	owl_update_bits(pctrl->base + g->mfpctl_reg, mask, val);

	raw_spin_unlock_irqrestore(&pctrl->lock, flags);

	return 0;
}

static const struct pinmux_ops owl_pinmux_ops = {
	.get_functions_count = owl_get_funcs_count,
	.get_function_name = owl_get_func_name,
	.get_function_groups = owl_get_func_groups,
	.set_mux = owl_set_mux,
};

static int owl_pad_pinconf_reg(const struct owl_padinfo *info,
				unsigned int param,
				u32 *reg,
				u32 *bit,
				u32 *width)
{
	switch (param) {
	case PIN_CONFIG_BIAS_BUS_HOLD:
	case PIN_CONFIG_BIAS_HIGH_IMPEDANCE:
	case PIN_CONFIG_BIAS_PULL_DOWN:
	case PIN_CONFIG_BIAS_PULL_UP:
		if (!info->pullctl)
			return -EINVAL;
		*reg = info->pullctl->reg;
		*bit = info->pullctl->shift;
		*width = info->pullctl->width;
		break;
	case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
		if (!info->st)
			return -EINVAL;
		*reg = info->st->reg;
		*bit = info->st->shift;
		*width = info->st->width;
		break;
	default:
		return -ENOTSUPP;
	}

	return 0;
}

static int owl_pin_config_get(struct pinctrl_dev *pctrldev,
				unsigned int pin,
				unsigned long *config)
{
	int ret = 0;
	struct owl_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctrldev);
	const struct owl_padinfo *info;
	unsigned int param = pinconf_to_config_param(*config);
	u32 reg, bit, width, arg;

	info = &pctrl->soc->padinfo[pin];

	ret = owl_pad_pinconf_reg(info, param, &reg, &bit, &width);
	if (ret)
		return ret;

	arg = owl_read_field(pctrl, reg, bit, width);

	if (!pctrl->soc->padctl_val2arg)
		return -ENOTSUPP;

	ret = pctrl->soc->padctl_val2arg(info, param, &arg);
	if (ret)
		return ret;

	*config = pinconf_to_config_packed(param, arg);

	return ret;
}

static int owl_pin_config_set(struct pinctrl_dev *pctrldev,
				unsigned int pin,
				unsigned long *configs,
				unsigned int num_configs)
{
	struct owl_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctrldev);
	const struct owl_padinfo *info;
	unsigned long flags;
	unsigned int param;
	u32 reg, bit, width, arg;
	int ret = 0, i;

	info = &pctrl->soc->padinfo[pin];

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		ret = owl_pad_pinconf_reg(info, param, &reg, &bit, &width);
		if (ret)
			return ret;

		if (!pctrl->soc->padctl_arg2val)
			return -ENOTSUPP;

		ret = pctrl->soc->padctl_arg2val(info, param, &arg);
		if (ret)
			return ret;

		raw_spin_lock_irqsave(&pctrl->lock, flags);

		owl_write_field(pctrl, reg, arg, bit, width);

		raw_spin_unlock_irqrestore(&pctrl->lock, flags);
	}

	return ret;
}

static int owl_group_pinconf_reg(const struct owl_pingroup *g,
				unsigned int param,
				u32 *reg,
				u32 *bit,
				u32 *width)
{
	switch (param) {
	case PIN_CONFIG_DRIVE_STRENGTH:
		if (g->drv_reg < 0)
			return -EINVAL;
		*reg = g->drv_reg;
		*bit = g->drv_shift;
		*width = g->drv_width;
		break;
	case PIN_CONFIG_SLEW_RATE:
		if (g->sr_reg < 0)
			return -EINVAL;
		*reg = g->sr_reg;
		*bit = g->sr_shift;
		*width = g->sr_width;
		break;
	default:
		return -ENOTSUPP;
	}

	return 0;
}

static int owl_group_pinconf_arg2val(const struct owl_pingroup *g,
				unsigned int param,
				u32 *arg)
{
	switch (param) {
	case PIN_CONFIG_DRIVE_STRENGTH:
		switch (*arg) {
		case 2:
			*arg = OWL_PINCONF_DRV_2MA;
			break;
		case 4:
			*arg = OWL_PINCONF_DRV_4MA;
			break;
		case 8:
			*arg = OWL_PINCONF_DRV_8MA;
			break;
		case 12:
			*arg = OWL_PINCONF_DRV_12MA;
			break;
		default:
			return -EINVAL;
		}
		break;
	case PIN_CONFIG_SLEW_RATE:
		if (*arg)
			*arg = OWL_PINCONF_SLEW_FAST;
		else
			*arg = OWL_PINCONF_SLEW_SLOW;
		break;
	default:
		return -ENOTSUPP;
	}

	return 0;
}

static int owl_group_pinconf_val2arg(const struct owl_pingroup *g,
				unsigned int param,
				u32 *arg)
{
	switch (param) {
	case PIN_CONFIG_DRIVE_STRENGTH:
		switch (*arg) {
		case OWL_PINCONF_DRV_2MA:
			*arg = 2;
			break;
		case OWL_PINCONF_DRV_4MA:
			*arg = 4;
			break;
		case OWL_PINCONF_DRV_8MA:
			*arg = 8;
			break;
		case OWL_PINCONF_DRV_12MA:
			*arg = 12;
			break;
		default:
			return -EINVAL;
		}
		break;
	case PIN_CONFIG_SLEW_RATE:
		if (*arg)
			*arg = 1;
		else
			*arg = 0;
		break;
	default:
		return -ENOTSUPP;
	}

	return 0;
}

static int owl_group_config_get(struct pinctrl_dev *pctrldev,
				unsigned int group,
				unsigned long *config)
{
	const struct owl_pingroup *g;
	struct owl_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctrldev);
	unsigned int param = pinconf_to_config_param(*config);
	u32 reg, bit, width, arg;
	int ret;

	g = &pctrl->soc->groups[group];

	ret = owl_group_pinconf_reg(g, param, &reg, &bit, &width);
	if (ret)
		return ret;

	arg = owl_read_field(pctrl, reg, bit, width);

	ret = owl_group_pinconf_val2arg(g, param, &arg);
	if (ret)
		return ret;

	*config = pinconf_to_config_packed(param, arg);

	return ret;
}

static int owl_group_config_set(struct pinctrl_dev *pctrldev,
				unsigned int group,
				unsigned long *configs,
				unsigned int num_configs)
{
	const struct owl_pingroup *g;
	struct owl_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctrldev);
	unsigned long flags;
	unsigned int param;
	u32 reg, bit, width, arg;
	int ret, i;

	g = &pctrl->soc->groups[group];

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		ret = owl_group_pinconf_reg(g, param, &reg, &bit, &width);
		if (ret)
			return ret;

		ret = owl_group_pinconf_arg2val(g, param, &arg);
		if (ret)
			return ret;

		/* Update register */
		raw_spin_lock_irqsave(&pctrl->lock, flags);

		owl_write_field(pctrl, reg, arg, bit, width);

		raw_spin_unlock_irqrestore(&pctrl->lock, flags);
	}

	return 0;
}

static const struct pinconf_ops owl_pinconf_ops = {
	.is_generic = true,
	.pin_config_get = owl_pin_config_get,
	.pin_config_set = owl_pin_config_set,
	.pin_config_group_get = owl_group_config_get,
	.pin_config_group_set = owl_group_config_set,
};

static struct pinctrl_desc owl_pinctrl_desc = {
	.pctlops = &owl_pinctrl_ops,
	.pmxops = &owl_pinmux_ops,
	.confops = &owl_pinconf_ops,
	.owner = THIS_MODULE,
};

static const struct owl_gpio_port *
owl_gpio_get_port(struct owl_pinctrl *pctrl, unsigned int *pin)
{
	unsigned int start = 0, i;

	for (i = 0; i < pctrl->soc->nports; i++) {
		const struct owl_gpio_port *port = &pctrl->soc->ports[i];

		if (*pin >= start && *pin < start + port->pins) {
			*pin -= start;
			return port;
		}

		start += port->pins;
	}

	return NULL;
}

static void owl_gpio_update_reg(void __iomem *base, unsigned int pin, int flag)
{
	u32 val;

	val = readl_relaxed(base);

	if (flag)
		val |= BIT(pin);
	else
		val &= ~BIT(pin);

	writel_relaxed(val, base);
}

static int owl_gpio_request(struct gpio_chip *chip, unsigned int offset)
{
	struct owl_pinctrl *pctrl = gpiochip_get_data(chip);
	const struct owl_gpio_port *port;
	void __iomem *gpio_base;
	unsigned long flags;

	port = owl_gpio_get_port(pctrl, &offset);
	if (WARN_ON(port == NULL))
		return -ENODEV;

	gpio_base = pctrl->base + port->offset;

	/*
	 * GPIOs have higher priority over other modules, so either setting
	 * them as OUT or IN is sufficient
	 */
	raw_spin_lock_irqsave(&pctrl->lock, flags);
	owl_gpio_update_reg(gpio_base + port->outen, offset, true);
	raw_spin_unlock_irqrestore(&pctrl->lock, flags);

	return 0;
}

static void owl_gpio_free(struct gpio_chip *chip, unsigned int offset)
{
	struct owl_pinctrl *pctrl = gpiochip_get_data(chip);
	const struct owl_gpio_port *port;
	void __iomem *gpio_base;
	unsigned long flags;

	port = owl_gpio_get_port(pctrl, &offset);
	if (WARN_ON(port == NULL))
		return;

	gpio_base = pctrl->base + port->offset;

	raw_spin_lock_irqsave(&pctrl->lock, flags);
	/* disable gpio output */
	owl_gpio_update_reg(gpio_base + port->outen, offset, false);

	/* disable gpio input */
	owl_gpio_update_reg(gpio_base + port->inen, offset, false);
	raw_spin_unlock_irqrestore(&pctrl->lock, flags);
}

static int owl_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct owl_pinctrl *pctrl = gpiochip_get_data(chip);
	const struct owl_gpio_port *port;
	void __iomem *gpio_base;
	unsigned long flags;
	u32 val;

	port = owl_gpio_get_port(pctrl, &offset);
	if (WARN_ON(port == NULL))
		return -ENODEV;

	gpio_base = pctrl->base + port->offset;

	raw_spin_lock_irqsave(&pctrl->lock, flags);
	val = readl_relaxed(gpio_base + port->dat);
	raw_spin_unlock_irqrestore(&pctrl->lock, flags);

	return !!(val & BIT(offset));
}

static void owl_gpio_set(struct gpio_chip *chip, unsigned int offset, int value)
{
	struct owl_pinctrl *pctrl = gpiochip_get_data(chip);
	const struct owl_gpio_port *port;
	void __iomem *gpio_base;
	unsigned long flags;

	port = owl_gpio_get_port(pctrl, &offset);
	if (WARN_ON(port == NULL))
		return;

	gpio_base = pctrl->base + port->offset;

	raw_spin_lock_irqsave(&pctrl->lock, flags);
	owl_gpio_update_reg(gpio_base + port->dat, offset, value);
	raw_spin_unlock_irqrestore(&pctrl->lock, flags);
}

static int owl_gpio_direction_input(struct gpio_chip *chip, unsigned int offset)
{
	struct owl_pinctrl *pctrl = gpiochip_get_data(chip);
	const struct owl_gpio_port *port;
	void __iomem *gpio_base;
	unsigned long flags;

	port = owl_gpio_get_port(pctrl, &offset);
	if (WARN_ON(port == NULL))
		return -ENODEV;

	gpio_base = pctrl->base + port->offset;

	raw_spin_lock_irqsave(&pctrl->lock, flags);
	owl_gpio_update_reg(gpio_base + port->outen, offset, false);
	owl_gpio_update_reg(gpio_base + port->inen, offset, true);
	raw_spin_unlock_irqrestore(&pctrl->lock, flags);

	return 0;
}

static int owl_gpio_direction_output(struct gpio_chip *chip,
				unsigned int offset, int value)
{
	struct owl_pinctrl *pctrl = gpiochip_get_data(chip);
	const struct owl_gpio_port *port;
	void __iomem *gpio_base;
	unsigned long flags;

	port = owl_gpio_get_port(pctrl, &offset);
	if (WARN_ON(port == NULL))
		return -ENODEV;

	gpio_base = pctrl->base + port->offset;

	raw_spin_lock_irqsave(&pctrl->lock, flags);
	owl_gpio_update_reg(gpio_base + port->inen, offset, false);
	owl_gpio_update_reg(gpio_base + port->outen, offset, true);
	owl_gpio_update_reg(gpio_base + port->dat, offset, value);
	raw_spin_unlock_irqrestore(&pctrl->lock, flags);

	return 0;
}

static void irq_set_type(struct owl_pinctrl *pctrl, int gpio, unsigned int type)
{
	const struct owl_gpio_port *port;
	void __iomem *gpio_base;
	unsigned long flags;
	unsigned int offset, value, irq_type = 0;

	switch (type) {
	case IRQ_TYPE_EDGE_BOTH:
		/*
		 * Since the hardware doesn't support interrupts on both edges,
		 * emulate it in the software by setting the single edge
		 * interrupt and switching to the opposite edge while ACKing
		 * the interrupt
		 */
		if (owl_gpio_get(&pctrl->chip, gpio))
			irq_type = OWL_GPIO_INT_EDGE_FALLING;
		else
			irq_type = OWL_GPIO_INT_EDGE_RISING;
		break;

	case IRQ_TYPE_EDGE_RISING:
		irq_type = OWL_GPIO_INT_EDGE_RISING;
		break;

	case IRQ_TYPE_EDGE_FALLING:
		irq_type = OWL_GPIO_INT_EDGE_FALLING;
		break;

	case IRQ_TYPE_LEVEL_HIGH:
		irq_type = OWL_GPIO_INT_LEVEL_HIGH;
		break;

	case IRQ_TYPE_LEVEL_LOW:
		irq_type = OWL_GPIO_INT_LEVEL_LOW;
		break;

	default:
		break;
	}

	port = owl_gpio_get_port(pctrl, &gpio);
	if (WARN_ON(port == NULL))
		return;

	gpio_base = pctrl->base + port->offset;

	raw_spin_lock_irqsave(&pctrl->lock, flags);

	offset = (gpio < 16) ? 4 : 0;
	value = readl_relaxed(gpio_base + port->intc_type + offset);
	value &= ~(OWL_GPIO_INT_MASK << ((gpio % 16) * 2));
	value |= irq_type << ((gpio % 16) * 2);
	writel_relaxed(value, gpio_base + port->intc_type + offset);

	raw_spin_unlock_irqrestore(&pctrl->lock, flags);
}

static void owl_gpio_irq_mask(struct irq_data *data)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(data);
	struct owl_pinctrl *pctrl = gpiochip_get_data(gc);
	const struct owl_gpio_port *port;
	void __iomem *gpio_base;
	unsigned long flags;
	unsigned int gpio = data->hwirq;
	u32 val;

	port = owl_gpio_get_port(pctrl, &gpio);
	if (WARN_ON(port == NULL))
		return;

	gpio_base = pctrl->base + port->offset;

	raw_spin_lock_irqsave(&pctrl->lock, flags);

	owl_gpio_update_reg(gpio_base + port->intc_msk, gpio, false);

	/* disable port interrupt if no interrupt pending bit is active */
	val = readl_relaxed(gpio_base + port->intc_msk);
	if (val == 0)
		owl_gpio_update_reg(gpio_base + port->intc_ctl,
					OWL_GPIO_CTLR_ENABLE + port->shared_ctl_offset * 5, false);

	raw_spin_unlock_irqrestore(&pctrl->lock, flags);
}

static void owl_gpio_irq_unmask(struct irq_data *data)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(data);
	struct owl_pinctrl *pctrl = gpiochip_get_data(gc);
	const struct owl_gpio_port *port;
	void __iomem *gpio_base;
	unsigned long flags;
	unsigned int gpio = data->hwirq;
	u32 value;

	port = owl_gpio_get_port(pctrl, &gpio);
	if (WARN_ON(port == NULL))
		return;

	gpio_base = pctrl->base + port->offset;
	raw_spin_lock_irqsave(&pctrl->lock, flags);

	/* enable port interrupt */
	value = readl_relaxed(gpio_base + port->intc_ctl);
	value |= ((BIT(OWL_GPIO_CTLR_ENABLE) | BIT(OWL_GPIO_CTLR_SAMPLE_CLK_24M))
			<< port->shared_ctl_offset * 5);
	writel_relaxed(value, gpio_base + port->intc_ctl);

	/* enable GPIO interrupt */
	owl_gpio_update_reg(gpio_base + port->intc_msk, gpio, true);

	raw_spin_unlock_irqrestore(&pctrl->lock, flags);
}

static void owl_gpio_irq_ack(struct irq_data *data)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(data);
	struct owl_pinctrl *pctrl = gpiochip_get_data(gc);
	const struct owl_gpio_port *port;
	void __iomem *gpio_base;
	unsigned long flags;
	unsigned int gpio = data->hwirq;

	/*
	 * Switch the interrupt edge to the opposite edge of the interrupt
	 * which got triggered for the case of emulating both edges
	 */
	if (irqd_get_trigger_type(data) == IRQ_TYPE_EDGE_BOTH) {
		if (owl_gpio_get(gc, gpio))
			irq_set_type(pctrl, gpio, IRQ_TYPE_EDGE_FALLING);
		else
			irq_set_type(pctrl, gpio, IRQ_TYPE_EDGE_RISING);
	}

	port = owl_gpio_get_port(pctrl, &gpio);
	if (WARN_ON(port == NULL))
		return;

	gpio_base = pctrl->base + port->offset;

	raw_spin_lock_irqsave(&pctrl->lock, flags);

	owl_gpio_update_reg(gpio_base + port->intc_ctl,
				OWL_GPIO_CTLR_PENDING + port->shared_ctl_offset * 5, true);

	raw_spin_unlock_irqrestore(&pctrl->lock, flags);
}

static int owl_gpio_irq_set_type(struct irq_data *data, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(data);
	struct owl_pinctrl *pctrl = gpiochip_get_data(gc);

	if (type & (IRQ_TYPE_LEVEL_LOW | IRQ_TYPE_LEVEL_HIGH))
		irq_set_handler_locked(data, handle_level_irq);
	else
		irq_set_handler_locked(data, handle_edge_irq);

	irq_set_type(pctrl, data->hwirq, type);

	return 0;
}

static void owl_gpio_irq_handler(struct irq_desc *desc)
{
	struct owl_pinctrl *pctrl = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct irq_domain *domain = pctrl->chip.irq.domain;
	unsigned int parent = irq_desc_get_irq(desc);
	const struct owl_gpio_port *port;
	void __iomem *base;
	unsigned int pin, offset = 0, i;
	unsigned long pending_irq;

	chained_irq_enter(chip, desc);

	for (i = 0; i < pctrl->soc->nports; i++) {
		port = &pctrl->soc->ports[i];
		base = pctrl->base + port->offset;

		/* skip ports that are not associated with this irq */
		if (parent != pctrl->irq[i])
			goto skip;

		pending_irq = readl_relaxed(base + port->intc_pd);

		for_each_set_bit(pin, &pending_irq, port->pins) {
			generic_handle_domain_irq(domain, offset + pin);

			/* clear pending interrupt */
			owl_gpio_update_reg(base + port->intc_pd, pin, true);
		}

skip:
		offset += port->pins;
	}

	chained_irq_exit(chip, desc);
}

static int owl_gpio_init(struct owl_pinctrl *pctrl)
{
	struct gpio_chip *chip;
	struct gpio_irq_chip *gpio_irq;
	int ret, i, j, offset;

	chip = &pctrl->chip;
	chip->base = -1;
	chip->ngpio = pctrl->soc->ngpios;
	chip->label = dev_name(pctrl->dev);
	chip->parent = pctrl->dev;
	chip->owner = THIS_MODULE;

	pctrl->irq_chip.name = chip->of_node->name;
	pctrl->irq_chip.irq_ack = owl_gpio_irq_ack;
	pctrl->irq_chip.irq_mask = owl_gpio_irq_mask;
	pctrl->irq_chip.irq_unmask = owl_gpio_irq_unmask;
	pctrl->irq_chip.irq_set_type = owl_gpio_irq_set_type;

	gpio_irq = &chip->irq;
	gpio_irq->chip = &pctrl->irq_chip;
	gpio_irq->handler = handle_simple_irq;
	gpio_irq->default_type = IRQ_TYPE_NONE;
	gpio_irq->parent_handler = owl_gpio_irq_handler;
	gpio_irq->parent_handler_data = pctrl;
	gpio_irq->num_parents = pctrl->num_irq;
	gpio_irq->parents = pctrl->irq;

	gpio_irq->map = devm_kcalloc(pctrl->dev, chip->ngpio,
				sizeof(*gpio_irq->map), GFP_KERNEL);
	if (!gpio_irq->map)
		return -ENOMEM;

	for (i = 0, offset = 0; i < pctrl->soc->nports; i++) {
		const struct owl_gpio_port *port = &pctrl->soc->ports[i];

		for (j = 0; j < port->pins; j++)
			gpio_irq->map[offset + j] = gpio_irq->parents[i];

		offset += port->pins;
	}

	ret = gpiochip_add_data(&pctrl->chip, pctrl);
	if (ret) {
		dev_err(pctrl->dev, "failed to register gpiochip\n");
		return ret;
	}

	return 0;
}

int owl_pinctrl_probe(struct platform_device *pdev,
				struct owl_pinctrl_soc_data *soc_data)
{
	struct owl_pinctrl *pctrl;
	int ret, i;

	pctrl = devm_kzalloc(&pdev->dev, sizeof(*pctrl), GFP_KERNEL);
	if (!pctrl)
		return -ENOMEM;

	pctrl->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pctrl->base))
		return PTR_ERR(pctrl->base);

	/* enable GPIO/MFP clock */
	pctrl->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(pctrl->clk)) {
		dev_err(&pdev->dev, "no clock defined\n");
		return PTR_ERR(pctrl->clk);
	}

	ret = clk_prepare_enable(pctrl->clk);
	if (ret) {
		dev_err(&pdev->dev, "clk enable failed\n");
		return ret;
	}

	raw_spin_lock_init(&pctrl->lock);

	owl_pinctrl_desc.name = dev_name(&pdev->dev);
	owl_pinctrl_desc.pins = soc_data->pins;
	owl_pinctrl_desc.npins = soc_data->npins;

	pctrl->chip.direction_input  = owl_gpio_direction_input;
	pctrl->chip.direction_output = owl_gpio_direction_output;
	pctrl->chip.get = owl_gpio_get;
	pctrl->chip.set = owl_gpio_set;
	pctrl->chip.request = owl_gpio_request;
	pctrl->chip.free = owl_gpio_free;

	pctrl->soc = soc_data;
	pctrl->dev = &pdev->dev;

	pctrl->pctrldev = devm_pinctrl_register(&pdev->dev,
					&owl_pinctrl_desc, pctrl);
	if (IS_ERR(pctrl->pctrldev)) {
		dev_err(&pdev->dev, "could not register Actions OWL pinmux driver\n");
		ret = PTR_ERR(pctrl->pctrldev);
		goto err_exit;
	}

	ret = platform_irq_count(pdev);
	if (ret < 0)
		goto err_exit;

	pctrl->num_irq = ret;

	pctrl->irq = devm_kcalloc(&pdev->dev, pctrl->num_irq,
					sizeof(*pctrl->irq), GFP_KERNEL);
	if (!pctrl->irq) {
		ret = -ENOMEM;
		goto err_exit;
	}

	for (i = 0; i < pctrl->num_irq ; i++) {
		ret = platform_get_irq(pdev, i);
		if (ret < 0)
			goto err_exit;
		pctrl->irq[i] = ret;
	}

	ret = owl_gpio_init(pctrl);
	if (ret)
		goto err_exit;

	platform_set_drvdata(pdev, pctrl);

	return 0;

err_exit:
	clk_disable_unprepare(pctrl->clk);

	return ret;
}
