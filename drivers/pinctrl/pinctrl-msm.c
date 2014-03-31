/*
 * Copyright (c) 2013, Sony Mobile Communications AB.
 * Copyright (c) 2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/err.h>
#include <linux/irqdomain.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/of_irq.h>
#include <linux/spinlock.h>

#include "core.h"
#include "pinconf.h"
#include "pinctrl-msm.h"
#include "pinctrl-utils.h"

#define MAX_NR_GPIO 300

/**
 * struct msm_pinctrl - state for a pinctrl-msm device
 * @dev:            device handle.
 * @pctrl:          pinctrl handle.
 * @domain:         irqdomain handle.
 * @chip:           gpiochip handle.
 * @irq:            parent irq for the TLMM irq_chip.
 * @lock:           Spinlock to protect register resources as well
 *                  as msm_pinctrl data structures.
 * @enabled_irqs:   Bitmap of currently enabled irqs.
 * @dual_edge_irqs: Bitmap of irqs that need sw emulated dual edge
 *                  detection.
 * @wake_irqs:      Bitmap of irqs with requested as wakeup source.
 * @soc;            Reference to soc_data of platform specific data.
 * @regs:           Base address for the TLMM register map.
 */
struct msm_pinctrl {
	struct device *dev;
	struct pinctrl_dev *pctrl;
	struct irq_domain *domain;
	struct gpio_chip chip;
	int irq;

	spinlock_t lock;

	DECLARE_BITMAP(dual_edge_irqs, MAX_NR_GPIO);
	DECLARE_BITMAP(enabled_irqs, MAX_NR_GPIO);
	DECLARE_BITMAP(wake_irqs, MAX_NR_GPIO);

	const struct msm_pinctrl_soc_data *soc;
	void __iomem *regs;
};

static int msm_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct msm_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	return pctrl->soc->ngroups;
}

static const char *msm_get_group_name(struct pinctrl_dev *pctldev,
				      unsigned group)
{
	struct msm_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	return pctrl->soc->groups[group].name;
}

static int msm_get_group_pins(struct pinctrl_dev *pctldev,
			      unsigned group,
			      const unsigned **pins,
			      unsigned *num_pins)
{
	struct msm_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	*pins = pctrl->soc->groups[group].pins;
	*num_pins = pctrl->soc->groups[group].npins;
	return 0;
}

static const struct pinctrl_ops msm_pinctrl_ops = {
	.get_groups_count	= msm_get_groups_count,
	.get_group_name		= msm_get_group_name,
	.get_group_pins		= msm_get_group_pins,
	.dt_node_to_map		= pinconf_generic_dt_node_to_map_group,
	.dt_free_map		= pinctrl_utils_dt_free_map,
};

static int msm_get_functions_count(struct pinctrl_dev *pctldev)
{
	struct msm_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	return pctrl->soc->nfunctions;
}

static const char *msm_get_function_name(struct pinctrl_dev *pctldev,
					 unsigned function)
{
	struct msm_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	return pctrl->soc->functions[function].name;
}

static int msm_get_function_groups(struct pinctrl_dev *pctldev,
				   unsigned function,
				   const char * const **groups,
				   unsigned * const num_groups)
{
	struct msm_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	*groups = pctrl->soc->functions[function].groups;
	*num_groups = pctrl->soc->functions[function].ngroups;
	return 0;
}

static int msm_pinmux_enable(struct pinctrl_dev *pctldev,
			     unsigned function,
			     unsigned group)
{
	struct msm_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	const struct msm_pingroup *g;
	unsigned long flags;
	u32 val;
	int i;

	g = &pctrl->soc->groups[group];

	if (WARN_ON(g->mux_bit < 0))
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(g->funcs); i++) {
		if (g->funcs[i] == function)
			break;
	}

	if (WARN_ON(i == ARRAY_SIZE(g->funcs)))
		return -EINVAL;

	spin_lock_irqsave(&pctrl->lock, flags);

	val = readl(pctrl->regs + g->ctl_reg);
	val &= ~(0x7 << g->mux_bit);
	val |= i << g->mux_bit;
	writel(val, pctrl->regs + g->ctl_reg);

	spin_unlock_irqrestore(&pctrl->lock, flags);

	return 0;
}

static void msm_pinmux_disable(struct pinctrl_dev *pctldev,
			       unsigned function,
			       unsigned group)
{
	struct msm_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	const struct msm_pingroup *g;
	unsigned long flags;
	u32 val;

	g = &pctrl->soc->groups[group];

	if (WARN_ON(g->mux_bit < 0))
		return;

	spin_lock_irqsave(&pctrl->lock, flags);

	/* Clear the mux bits to select gpio mode */
	val = readl(pctrl->regs + g->ctl_reg);
	val &= ~(0x7 << g->mux_bit);
	writel(val, pctrl->regs + g->ctl_reg);

	spin_unlock_irqrestore(&pctrl->lock, flags);
}

static const struct pinmux_ops msm_pinmux_ops = {
	.get_functions_count	= msm_get_functions_count,
	.get_function_name	= msm_get_function_name,
	.get_function_groups	= msm_get_function_groups,
	.enable			= msm_pinmux_enable,
	.disable		= msm_pinmux_disable,
};

static int msm_config_reg(struct msm_pinctrl *pctrl,
			  const struct msm_pingroup *g,
			  unsigned param,
			  s16 *reg,
			  unsigned *mask,
			  unsigned *bit)
{
	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		*reg = g->ctl_reg;
		*bit = g->pull_bit;
		*mask = 3;
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		*reg = g->ctl_reg;
		*bit = g->pull_bit;
		*mask = 3;
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		*reg = g->ctl_reg;
		*bit = g->pull_bit;
		*mask = 3;
		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		*reg = g->ctl_reg;
		*bit = g->drv_bit;
		*mask = 7;
		break;
	default:
		dev_err(pctrl->dev, "Invalid config param %04x\n", param);
		return -ENOTSUPP;
	}

	if (*reg < 0) {
		dev_err(pctrl->dev, "Config param %04x not supported on group %s\n",
			param, g->name);
		return -ENOTSUPP;
	}

	return 0;
}

static int msm_config_get(struct pinctrl_dev *pctldev,
			  unsigned int pin,
			  unsigned long *config)
{
	dev_err(pctldev->dev, "pin_config_set op not supported\n");
	return -ENOTSUPP;
}

static int msm_config_set(struct pinctrl_dev *pctldev, unsigned int pin,
				unsigned long *configs, unsigned num_configs)
{
	dev_err(pctldev->dev, "pin_config_set op not supported\n");
	return -ENOTSUPP;
}

#define MSM_NO_PULL	0
#define MSM_PULL_DOWN	1
#define MSM_PULL_UP	3

static const unsigned msm_regval_to_drive[] = { 2, 4, 6, 8, 10, 12, 14, 16 };
static const unsigned msm_drive_to_regval[] = { -1, -1, 0, -1, 1, -1, 2, -1, 3, -1, 4, -1, 5, -1, 6, -1, 7 };

static int msm_config_group_get(struct pinctrl_dev *pctldev,
				unsigned int group,
				unsigned long *config)
{
	const struct msm_pingroup *g;
	struct msm_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	unsigned param = pinconf_to_config_param(*config);
	unsigned mask;
	unsigned arg;
	unsigned bit;
	s16 reg;
	int ret;
	u32 val;

	g = &pctrl->soc->groups[group];

	ret = msm_config_reg(pctrl, g, param, &reg, &mask, &bit);
	if (ret < 0)
		return ret;

	val = readl(pctrl->regs + reg);
	arg = (val >> bit) & mask;

	/* Convert register value to pinconf value */
	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		arg = arg == MSM_NO_PULL;
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		arg = arg == MSM_PULL_DOWN;
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		arg = arg == MSM_PULL_UP;
		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		arg = msm_regval_to_drive[arg];
		break;
	default:
		dev_err(pctrl->dev, "Unsupported config parameter: %x\n",
			param);
		return -EINVAL;
	}

	*config = pinconf_to_config_packed(param, arg);

	return 0;
}

static int msm_config_group_set(struct pinctrl_dev *pctldev,
				unsigned group,
				unsigned long *configs,
				unsigned num_configs)
{
	const struct msm_pingroup *g;
	struct msm_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	unsigned long flags;
	unsigned param;
	unsigned mask;
	unsigned arg;
	unsigned bit;
	s16 reg;
	int ret;
	u32 val;
	int i;

	g = &pctrl->soc->groups[group];

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		ret = msm_config_reg(pctrl, g, param, &reg, &mask, &bit);
		if (ret < 0)
			return ret;

		/* Convert pinconf values to register values */
		switch (param) {
		case PIN_CONFIG_BIAS_DISABLE:
			arg = MSM_NO_PULL;
			break;
		case PIN_CONFIG_BIAS_PULL_DOWN:
			arg = MSM_PULL_DOWN;
			break;
		case PIN_CONFIG_BIAS_PULL_UP:
			arg = MSM_PULL_UP;
			break;
		case PIN_CONFIG_DRIVE_STRENGTH:
			/* Check for invalid values */
			if (arg >= ARRAY_SIZE(msm_drive_to_regval))
				arg = -1;
			else
				arg = msm_drive_to_regval[arg];
			break;
		default:
			dev_err(pctrl->dev, "Unsupported config parameter: %x\n",
				param);
			return -EINVAL;
		}

		/* Range-check user-supplied value */
		if (arg & ~mask) {
			dev_err(pctrl->dev, "config %x: %x is invalid\n", param, arg);
			return -EINVAL;
		}

		spin_lock_irqsave(&pctrl->lock, flags);
		val = readl(pctrl->regs + reg);
		val &= ~(mask << bit);
		val |= arg << bit;
		writel(val, pctrl->regs + reg);
		spin_unlock_irqrestore(&pctrl->lock, flags);
	}

	return 0;
}

static const struct pinconf_ops msm_pinconf_ops = {
	.pin_config_get		= msm_config_get,
	.pin_config_set		= msm_config_set,
	.pin_config_group_get	= msm_config_group_get,
	.pin_config_group_set	= msm_config_group_set,
};

static struct pinctrl_desc msm_pinctrl_desc = {
	.pctlops = &msm_pinctrl_ops,
	.pmxops = &msm_pinmux_ops,
	.confops = &msm_pinconf_ops,
	.owner = THIS_MODULE,
};

static int msm_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	const struct msm_pingroup *g;
	struct msm_pinctrl *pctrl = container_of(chip, struct msm_pinctrl, chip);
	unsigned long flags;
	u32 val;

	g = &pctrl->soc->groups[offset];
	if (WARN_ON(g->io_reg < 0))
		return -EINVAL;

	spin_lock_irqsave(&pctrl->lock, flags);

	val = readl(pctrl->regs + g->ctl_reg);
	val &= ~BIT(g->oe_bit);
	writel(val, pctrl->regs + g->ctl_reg);

	spin_unlock_irqrestore(&pctrl->lock, flags);

	return 0;
}

static int msm_gpio_direction_output(struct gpio_chip *chip, unsigned offset, int value)
{
	const struct msm_pingroup *g;
	struct msm_pinctrl *pctrl = container_of(chip, struct msm_pinctrl, chip);
	unsigned long flags;
	u32 val;

	g = &pctrl->soc->groups[offset];
	if (WARN_ON(g->io_reg < 0))
		return -EINVAL;

	spin_lock_irqsave(&pctrl->lock, flags);

	val = readl(pctrl->regs + g->io_reg);
	if (value)
		val |= BIT(g->out_bit);
	else
		val &= ~BIT(g->out_bit);
	writel(val, pctrl->regs + g->io_reg);

	val = readl(pctrl->regs + g->ctl_reg);
	val |= BIT(g->oe_bit);
	writel(val, pctrl->regs + g->ctl_reg);

	spin_unlock_irqrestore(&pctrl->lock, flags);

	return 0;
}

static int msm_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	const struct msm_pingroup *g;
	struct msm_pinctrl *pctrl = container_of(chip, struct msm_pinctrl, chip);
	u32 val;

	g = &pctrl->soc->groups[offset];
	if (WARN_ON(g->io_reg < 0))
		return -EINVAL;

	val = readl(pctrl->regs + g->io_reg);
	return !!(val & BIT(g->in_bit));
}

static void msm_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	const struct msm_pingroup *g;
	struct msm_pinctrl *pctrl = container_of(chip, struct msm_pinctrl, chip);
	unsigned long flags;
	u32 val;

	g = &pctrl->soc->groups[offset];
	if (WARN_ON(g->io_reg < 0))
		return;

	spin_lock_irqsave(&pctrl->lock, flags);

	val = readl(pctrl->regs + g->io_reg);
	if (value)
		val |= BIT(g->out_bit);
	else
		val &= ~BIT(g->out_bit);
	writel(val, pctrl->regs + g->io_reg);

	spin_unlock_irqrestore(&pctrl->lock, flags);
}

static int msm_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	struct msm_pinctrl *pctrl = container_of(chip, struct msm_pinctrl, chip);

	return irq_find_mapping(pctrl->domain, offset);
}

static int msm_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	int gpio = chip->base + offset;
	return pinctrl_request_gpio(gpio);
}

static void msm_gpio_free(struct gpio_chip *chip, unsigned offset)
{
	int gpio = chip->base + offset;
	return pinctrl_free_gpio(gpio);
}

#ifdef CONFIG_DEBUG_FS
#include <linux/seq_file.h>

static void msm_gpio_dbg_show_one(struct seq_file *s,
				  struct pinctrl_dev *pctldev,
				  struct gpio_chip *chip,
				  unsigned offset,
				  unsigned gpio)
{
	const struct msm_pingroup *g;
	struct msm_pinctrl *pctrl = container_of(chip, struct msm_pinctrl, chip);
	unsigned func;
	int is_out;
	int drive;
	int pull;
	u32 ctl_reg;

	static const char * const pulls[] = {
		"no pull",
		"pull down",
		"keeper",
		"pull up"
	};

	g = &pctrl->soc->groups[offset];
	ctl_reg = readl(pctrl->regs + g->ctl_reg);

	is_out = !!(ctl_reg & BIT(g->oe_bit));
	func = (ctl_reg >> g->mux_bit) & 7;
	drive = (ctl_reg >> g->drv_bit) & 7;
	pull = (ctl_reg >> g->pull_bit) & 3;

	seq_printf(s, " %-8s: %-3s %d", g->name, is_out ? "out" : "in", func);
	seq_printf(s, " %dmA", msm_regval_to_drive[drive]);
	seq_printf(s, " %s", pulls[pull]);
}

static void msm_gpio_dbg_show(struct seq_file *s, struct gpio_chip *chip)
{
	unsigned gpio = chip->base;
	unsigned i;

	for (i = 0; i < chip->ngpio; i++, gpio++) {
		msm_gpio_dbg_show_one(s, NULL, chip, i, gpio);
		seq_puts(s, "\n");
	}
}

#else
#define msm_gpio_dbg_show NULL
#endif

static struct gpio_chip msm_gpio_template = {
	.direction_input  = msm_gpio_direction_input,
	.direction_output = msm_gpio_direction_output,
	.get              = msm_gpio_get,
	.set              = msm_gpio_set,
	.to_irq           = msm_gpio_to_irq,
	.request          = msm_gpio_request,
	.free             = msm_gpio_free,
	.dbg_show         = msm_gpio_dbg_show,
};

/* For dual-edge interrupts in software, since some hardware has no
 * such support:
 *
 * At appropriate moments, this function may be called to flip the polarity
 * settings of both-edge irq lines to try and catch the next edge.
 *
 * The attempt is considered successful if:
 * - the status bit goes high, indicating that an edge was caught, or
 * - the input value of the gpio doesn't change during the attempt.
 * If the value changes twice during the process, that would cause the first
 * test to fail but would force the second, as two opposite
 * transitions would cause a detection no matter the polarity setting.
 *
 * The do-loop tries to sledge-hammer closed the timing hole between
 * the initial value-read and the polarity-write - if the line value changes
 * during that window, an interrupt is lost, the new polarity setting is
 * incorrect, and the first success test will fail, causing a retry.
 *
 * Algorithm comes from Google's msmgpio driver.
 */
static void msm_gpio_update_dual_edge_pos(struct msm_pinctrl *pctrl,
					  const struct msm_pingroup *g,
					  struct irq_data *d)
{
	int loop_limit = 100;
	unsigned val, val2, intstat;
	unsigned pol;

	do {
		val = readl(pctrl->regs + g->io_reg) & BIT(g->in_bit);

		pol = readl(pctrl->regs + g->intr_cfg_reg);
		pol ^= BIT(g->intr_polarity_bit);
		writel(pol, pctrl->regs + g->intr_cfg_reg);

		val2 = readl(pctrl->regs + g->io_reg) & BIT(g->in_bit);
		intstat = readl(pctrl->regs + g->intr_status_reg);
		if (intstat || (val == val2))
			return;
	} while (loop_limit-- > 0);
	dev_err(pctrl->dev, "dual-edge irq failed to stabilize, %#08x != %#08x\n",
		val, val2);
}

static void msm_gpio_irq_mask(struct irq_data *d)
{
	const struct msm_pingroup *g;
	struct msm_pinctrl *pctrl;
	unsigned long flags;
	u32 val;

	pctrl = irq_data_get_irq_chip_data(d);
	g = &pctrl->soc->groups[d->hwirq];
	if (WARN_ON(g->intr_cfg_reg < 0))
		return;

	spin_lock_irqsave(&pctrl->lock, flags);

	val = readl(pctrl->regs + g->intr_cfg_reg);
	val &= ~BIT(g->intr_enable_bit);
	writel(val, pctrl->regs + g->intr_cfg_reg);

	clear_bit(d->hwirq, pctrl->enabled_irqs);

	spin_unlock_irqrestore(&pctrl->lock, flags);
}

static void msm_gpio_irq_unmask(struct irq_data *d)
{
	const struct msm_pingroup *g;
	struct msm_pinctrl *pctrl;
	unsigned long flags;
	u32 val;

	pctrl = irq_data_get_irq_chip_data(d);
	g = &pctrl->soc->groups[d->hwirq];
	if (WARN_ON(g->intr_status_reg < 0))
		return;

	spin_lock_irqsave(&pctrl->lock, flags);

	val = readl(pctrl->regs + g->intr_status_reg);
	val &= ~BIT(g->intr_status_bit);
	writel(val, pctrl->regs + g->intr_status_reg);

	val = readl(pctrl->regs + g->intr_cfg_reg);
	val |= BIT(g->intr_enable_bit);
	writel(val, pctrl->regs + g->intr_cfg_reg);

	set_bit(d->hwirq, pctrl->enabled_irqs);

	spin_unlock_irqrestore(&pctrl->lock, flags);
}

static void msm_gpio_irq_ack(struct irq_data *d)
{
	const struct msm_pingroup *g;
	struct msm_pinctrl *pctrl;
	unsigned long flags;
	u32 val;

	pctrl = irq_data_get_irq_chip_data(d);
	g = &pctrl->soc->groups[d->hwirq];
	if (WARN_ON(g->intr_status_reg < 0))
		return;

	spin_lock_irqsave(&pctrl->lock, flags);

	val = readl(pctrl->regs + g->intr_status_reg);
	val &= ~BIT(g->intr_status_bit);
	writel(val, pctrl->regs + g->intr_status_reg);

	if (test_bit(d->hwirq, pctrl->dual_edge_irqs))
		msm_gpio_update_dual_edge_pos(pctrl, g, d);

	spin_unlock_irqrestore(&pctrl->lock, flags);
}

#define INTR_TARGET_PROC_APPS    4

static int msm_gpio_irq_set_type(struct irq_data *d, unsigned int type)
{
	const struct msm_pingroup *g;
	struct msm_pinctrl *pctrl;
	unsigned long flags;
	u32 val;

	pctrl = irq_data_get_irq_chip_data(d);
	g = &pctrl->soc->groups[d->hwirq];
	if (WARN_ON(g->intr_cfg_reg < 0))
		return -EINVAL;

	spin_lock_irqsave(&pctrl->lock, flags);

	/*
	 * For hw without possibility of detecting both edges
	 */
	if (g->intr_detection_width == 1 && type == IRQ_TYPE_EDGE_BOTH)
		set_bit(d->hwirq, pctrl->dual_edge_irqs);
	else
		clear_bit(d->hwirq, pctrl->dual_edge_irqs);

	/* Route interrupts to application cpu */
	val = readl(pctrl->regs + g->intr_target_reg);
	val &= ~(7 << g->intr_target_bit);
	val |= INTR_TARGET_PROC_APPS << g->intr_target_bit;
	writel(val, pctrl->regs + g->intr_target_reg);

	/* Update configuration for gpio.
	 * RAW_STATUS_EN is left on for all gpio irqs. Due to the
	 * internal circuitry of TLMM, toggling the RAW_STATUS
	 * could cause the INTR_STATUS to be set for EDGE interrupts.
	 */
	val = readl(pctrl->regs + g->intr_cfg_reg);
	val |= BIT(g->intr_raw_status_bit);
	if (g->intr_detection_width == 2) {
		val &= ~(3 << g->intr_detection_bit);
		val &= ~(1 << g->intr_polarity_bit);
		switch (type) {
		case IRQ_TYPE_EDGE_RISING:
			val |= 1 << g->intr_detection_bit;
			val |= BIT(g->intr_polarity_bit);
			break;
		case IRQ_TYPE_EDGE_FALLING:
			val |= 2 << g->intr_detection_bit;
			val |= BIT(g->intr_polarity_bit);
			break;
		case IRQ_TYPE_EDGE_BOTH:
			val |= 3 << g->intr_detection_bit;
			val |= BIT(g->intr_polarity_bit);
			break;
		case IRQ_TYPE_LEVEL_LOW:
			break;
		case IRQ_TYPE_LEVEL_HIGH:
			val |= BIT(g->intr_polarity_bit);
			break;
		}
	} else if (g->intr_detection_width == 1) {
		val &= ~(1 << g->intr_detection_bit);
		val &= ~(1 << g->intr_polarity_bit);
		switch (type) {
		case IRQ_TYPE_EDGE_RISING:
			val |= BIT(g->intr_detection_bit);
			val |= BIT(g->intr_polarity_bit);
			break;
		case IRQ_TYPE_EDGE_FALLING:
			val |= BIT(g->intr_detection_bit);
			break;
		case IRQ_TYPE_EDGE_BOTH:
			val |= BIT(g->intr_detection_bit);
			break;
		case IRQ_TYPE_LEVEL_LOW:
			break;
		case IRQ_TYPE_LEVEL_HIGH:
			val |= BIT(g->intr_polarity_bit);
			break;
		}
	} else {
		BUG();
	}
	writel(val, pctrl->regs + g->intr_cfg_reg);

	if (test_bit(d->hwirq, pctrl->dual_edge_irqs))
		msm_gpio_update_dual_edge_pos(pctrl, g, d);

	spin_unlock_irqrestore(&pctrl->lock, flags);

	if (type & (IRQ_TYPE_LEVEL_LOW | IRQ_TYPE_LEVEL_HIGH))
		__irq_set_handler_locked(d->irq, handle_level_irq);
	else if (type & (IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_EDGE_RISING))
		__irq_set_handler_locked(d->irq, handle_edge_irq);

	return 0;
}

static int msm_gpio_irq_set_wake(struct irq_data *d, unsigned int on)
{
	struct msm_pinctrl *pctrl;
	unsigned long flags;
	unsigned ngpio;

	pctrl = irq_data_get_irq_chip_data(d);
	ngpio = pctrl->chip.ngpio;

	spin_lock_irqsave(&pctrl->lock, flags);

	if (on) {
		if (bitmap_empty(pctrl->wake_irqs, ngpio))
			enable_irq_wake(pctrl->irq);
		set_bit(d->hwirq, pctrl->wake_irqs);
	} else {
		clear_bit(d->hwirq, pctrl->wake_irqs);
		if (bitmap_empty(pctrl->wake_irqs, ngpio))
			disable_irq_wake(pctrl->irq);
	}

	spin_unlock_irqrestore(&pctrl->lock, flags);

	return 0;
}

static unsigned int msm_gpio_irq_startup(struct irq_data *d)
{
	struct msm_pinctrl *pctrl = irq_data_get_irq_chip_data(d);

	if (gpio_lock_as_irq(&pctrl->chip, d->hwirq)) {
		dev_err(pctrl->dev, "unable to lock HW IRQ %lu for IRQ\n",
			d->hwirq);
	}
	msm_gpio_irq_unmask(d);
	return 0;
}

static void msm_gpio_irq_shutdown(struct irq_data *d)
{
	struct msm_pinctrl *pctrl = irq_data_get_irq_chip_data(d);

	msm_gpio_irq_mask(d);
	gpio_unlock_as_irq(&pctrl->chip, d->hwirq);
}

static struct irq_chip msm_gpio_irq_chip = {
	.name           = "msmgpio",
	.irq_mask       = msm_gpio_irq_mask,
	.irq_unmask     = msm_gpio_irq_unmask,
	.irq_ack        = msm_gpio_irq_ack,
	.irq_set_type   = msm_gpio_irq_set_type,
	.irq_set_wake   = msm_gpio_irq_set_wake,
	.irq_startup	= msm_gpio_irq_startup,
	.irq_shutdown	= msm_gpio_irq_shutdown,
};

static void msm_gpio_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	const struct msm_pingroup *g;
	struct msm_pinctrl *pctrl = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_get_chip(irq);
	int irq_pin;
	int handled = 0;
	u32 val;
	int i;

	chained_irq_enter(chip, desc);

	/*
	 * Each pin has it's own IRQ status register, so use
	 * enabled_irq bitmap to limit the number of reads.
	 */
	for_each_set_bit(i, pctrl->enabled_irqs, pctrl->chip.ngpio) {
		g = &pctrl->soc->groups[i];
		val = readl(pctrl->regs + g->intr_status_reg);
		if (val & BIT(g->intr_status_bit)) {
			irq_pin = irq_find_mapping(pctrl->domain, i);
			generic_handle_irq(irq_pin);
			handled++;
		}
	}

	/* No interrupts were flagged */
	if (handled == 0)
		handle_bad_irq(irq, desc);

	chained_irq_exit(chip, desc);
}

static int msm_gpio_init(struct msm_pinctrl *pctrl)
{
	struct gpio_chip *chip;
	int irq;
	int ret;
	int i;
	int r;

	chip = &pctrl->chip;
	chip->base = 0;
	chip->ngpio = pctrl->soc->ngpios;
	chip->label = dev_name(pctrl->dev);
	chip->dev = pctrl->dev;
	chip->owner = THIS_MODULE;
	chip->of_node = pctrl->dev->of_node;

	ret = gpiochip_add(&pctrl->chip);
	if (ret) {
		dev_err(pctrl->dev, "Failed register gpiochip\n");
		return ret;
	}

	ret = gpiochip_add_pin_range(&pctrl->chip, dev_name(pctrl->dev), 0, 0, chip->ngpio);
	if (ret) {
		dev_err(pctrl->dev, "Failed to add pin range\n");
		return ret;
	}

	pctrl->domain = irq_domain_add_linear(pctrl->dev->of_node, chip->ngpio,
					      &irq_domain_simple_ops, NULL);
	if (!pctrl->domain) {
		dev_err(pctrl->dev, "Failed to register irq domain\n");
		r = gpiochip_remove(&pctrl->chip);
		return -ENOSYS;
	}

	for (i = 0; i < chip->ngpio; i++) {
		irq = irq_create_mapping(pctrl->domain, i);
		irq_set_chip_and_handler(irq, &msm_gpio_irq_chip, handle_edge_irq);
		irq_set_chip_data(irq, pctrl);
	}

	irq_set_handler_data(pctrl->irq, pctrl);
	irq_set_chained_handler(pctrl->irq, msm_gpio_irq_handler);

	return 0;
}

int msm_pinctrl_probe(struct platform_device *pdev,
		      const struct msm_pinctrl_soc_data *soc_data)
{
	struct msm_pinctrl *pctrl;
	struct resource *res;
	int ret;

	pctrl = devm_kzalloc(&pdev->dev, sizeof(*pctrl), GFP_KERNEL);
	if (!pctrl) {
		dev_err(&pdev->dev, "Can't allocate msm_pinctrl\n");
		return -ENOMEM;
	}
	pctrl->dev = &pdev->dev;
	pctrl->soc = soc_data;
	pctrl->chip = msm_gpio_template;

	spin_lock_init(&pctrl->lock);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pctrl->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pctrl->regs))
		return PTR_ERR(pctrl->regs);

	pctrl->irq = platform_get_irq(pdev, 0);
	if (pctrl->irq < 0) {
		dev_err(&pdev->dev, "No interrupt defined for msmgpio\n");
		return pctrl->irq;
	}

	msm_pinctrl_desc.name = dev_name(&pdev->dev);
	msm_pinctrl_desc.pins = pctrl->soc->pins;
	msm_pinctrl_desc.npins = pctrl->soc->npins;
	pctrl->pctrl = pinctrl_register(&msm_pinctrl_desc, &pdev->dev, pctrl);
	if (!pctrl->pctrl) {
		dev_err(&pdev->dev, "Couldn't register pinctrl driver\n");
		return -ENODEV;
	}

	ret = msm_gpio_init(pctrl);
	if (ret) {
		pinctrl_unregister(pctrl->pctrl);
		return ret;
	}

	platform_set_drvdata(pdev, pctrl);

	dev_dbg(&pdev->dev, "Probed Qualcomm pinctrl driver\n");

	return 0;
}
EXPORT_SYMBOL(msm_pinctrl_probe);

int msm_pinctrl_remove(struct platform_device *pdev)
{
	struct msm_pinctrl *pctrl = platform_get_drvdata(pdev);
	int ret;

	ret = gpiochip_remove(&pctrl->chip);
	if (ret) {
		dev_err(&pdev->dev, "Failed to remove gpiochip\n");
		return ret;
	}

	irq_set_chained_handler(pctrl->irq, NULL);
	irq_domain_remove(pctrl->domain);
	pinctrl_unregister(pctrl->pctrl);

	return 0;
}
EXPORT_SYMBOL(msm_pinctrl_remove);

