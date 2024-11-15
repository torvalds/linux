// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013, Sony Mobile Communications AB.
 * Copyright (c) 2013-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/delay.h>
#include <linux/err.h>
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
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/reboot.h>
#include <linux/pm.h>
#include <linux/log2.h>
#include <linux/qcom_scm.h>
#include <linux/soc/qcom/irq.h>
#include <linux/bitmap.h>
#include <linux/sizes.h>
#include <linux/suspend.h>
#include <linux/notifier.h>
#include <linux/syscore_ops.h>

#include "../core.h"
#include "../pinconf.h"
#include "pinctrl-msm.h"
#include "../pinctrl-utils.h"

#define MAX_NR_GPIO 300
#define MAX_NR_TILES 4
#define DEFAULT_REG_SIZE_4K  1
#define PS_HOLD_OFFSET 0x820
#define QUP_MASK       GENMASK(5, 0)
#define SPARE_MASK     GENMASK(15, 8)

/**
 * struct msm_pinctrl - state for a pinctrl-msm device
 * @dev:            device handle.
 * @pctrl:          pinctrl handle.
 * @chip:           gpiochip handle.
 * @desc:           pin controller descriptor
 * @restart_nb:     restart notifier block.
 * @irq:            parent irq for the TLMM irq_chip.
 * @n_dir_conns:    The number of pins directly connected to GIC.
 * @intr_target_use_scm: route irq to application cpu using scm calls
 * @lock:           Spinlock to protect register resources as well
 *                  as msm_pinctrl data structures.
 * @enabled_irqs:   Bitmap of currently enabled irqs.
 * @dual_edge_irqs: Bitmap of irqs that need sw emulated dual edge
 *                  detection.
 * @skip_wake_irqs: Skip IRQs that are handled by wakeup interrupt controller
 * @disabled_for_mux: These IRQs were disabled because we muxed away.
 * @ever_gpio:      This bit is set the first time we mux a pin to gpio_func.
 * @soc:            Reference to soc_data of platform specific data.
 * @regs:           Base addresses for the TLMM tiles.
 * @phys_base:      Physical base address
 */
struct msm_pinctrl {
	struct device *dev;
	struct pinctrl_dev *pctrl;
	struct gpio_chip chip;
	struct pinctrl_desc desc;
	struct notifier_block restart_nb;

	int irq;
	int n_dir_conns;

	bool intr_target_use_scm;

	raw_spinlock_t lock;

	DECLARE_BITMAP(dual_edge_irqs, MAX_NR_GPIO);
	DECLARE_BITMAP(enabled_irqs, MAX_NR_GPIO);
	DECLARE_BITMAP(skip_wake_irqs, MAX_NR_GPIO);
	DECLARE_BITMAP(disabled_for_mux, MAX_NR_GPIO);
	DECLARE_BITMAP(ever_gpio, MAX_NR_GPIO);

	const struct msm_pinctrl_soc_data *soc;
	void __iomem *regs[MAX_NR_TILES];
	u32 phys_base[MAX_NR_TILES];

	struct msm_gpio_regs *gpio_regs;
	struct msm_tile *msm_tile_regs;
	bool hibernation;
};

static struct msm_pinctrl *msm_pinctrl_data;
static bool pinctrl_msm_log_mask;

#define EGPIO_PRESENT			11
#define EGPIO_ENABLE			12
#define I2C_PULL			13
#define MSM_APPS_OWNER			1
#define MSM_REMOTE_OWNER		0

/* custom pinconf parameters for msm pinictrl*/
#define MSM_PIN_CONFIG_APPS           (PIN_CONFIG_END + 1)
#define MSM_PIN_CONFIG_REMOTE         (PIN_CONFIG_END + 2)
#define MSM_PIN_CONFIG_I2C_PULL       (PIN_CONFIG_END + 3)

static const struct pinconf_generic_params msm_gpio_bindings[] = {
	{"qcom,apps",        MSM_PIN_CONFIG_APPS,     0},
	{"qcom,remote",      MSM_PIN_CONFIG_REMOTE,   0},
	{"qcom,i2c_pull",    MSM_PIN_CONFIG_I2C_PULL, 0},
};

static const char * const pulls_keeper[] = {
	"no pull",
	"pull down",
	"keeper",
	"pull up"
};

static const char * const pulls_no_keeper[] = {
	"no pull",
	"pull down",
	"pull up",
};

#define MSM_ACCESSOR(name) \
static u32 msm_readl_##name(struct msm_pinctrl *pctrl, \
			    const struct msm_pingroup *g) \
{ \
	return readl(pctrl->regs[g->tile] + g->name##_reg); \
} \
static void msm_writel_##name(u32 val, struct msm_pinctrl *pctrl, \
			      const struct msm_pingroup *g) \
{ \
	writel(val, pctrl->regs[g->tile] + g->name##_reg); \
}

MSM_ACCESSOR(ctl)
MSM_ACCESSOR(io)
MSM_ACCESSOR(intr_cfg)
MSM_ACCESSOR(intr_status)
MSM_ACCESSOR(intr_target)

struct msm_gpio_regs {
	u32 ctl_reg;
	u32 io_reg;
	u32 intr_cfg_reg;
	u32 intr_status_reg;
};

struct msm_tile {
	u32 dir_con_regs[8];
};

static void msm_ack_intr_status(struct msm_pinctrl *pctrl,
				const struct msm_pingroup *g)
{
	u32 val = g->intr_ack_high ? BIT(g->intr_status_bit) : 0;

	msm_writel_intr_status(val, pctrl, g);
}

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
	.dt_free_map		= pinctrl_utils_free_map,
};

static int msm_pinmux_request(struct pinctrl_dev *pctldev, unsigned offset)
{
	struct msm_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	struct gpio_chip *chip = &pctrl->chip;

	return gpiochip_line_is_valid(chip, offset) ? 0 : -EINVAL;
}

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

static int msm_pinmux_set_mux(struct pinctrl_dev *pctldev,
			      unsigned function,
			      unsigned group)
{
	struct msm_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	struct gpio_chip *gc = &pctrl->chip;
	const struct  msm_pinctrl_soc_data *ps = pctrl->soc;
	unsigned int irq = irq_find_mapping(gc->irq.domain, group);
	struct irq_data *d = irq_get_irq_data(irq);
	unsigned int gpio_func = pctrl->soc->gpio_func;
	unsigned int egpio_func = pctrl->soc->egpio_func;
	const struct msm_pingroup *g;
	unsigned long flags;
	u32 val, mask;
	int i;

	g = &pctrl->soc->groups[group];
	mask = GENMASK(g->mux_bit + order_base_2(g->nfuncs) - 1, g->mux_bit);

	for (i = 0; i < g->nfuncs; i++) {
		if (g->funcs[i] == function)
			break;
	}

	if (WARN_ON(i == g->nfuncs))
		return -EINVAL;

	/*
	 * If an GPIO interrupt is setup on this pin then we need special
	 * handling.  Specifically interrupt detection logic will still see
	 * the pin twiddle even when we're muxed away.
	 *
	 * When we see a pin with an interrupt setup on it then we'll disable
	 * (mask) interrupts on it when we mux away until we mux back.  Note
	 * that disable_irq() refcounts and interrupts are disabled as long as
	 * at least one disable_irq() has been called.
	 */
	if (d && i != gpio_func &&
	    !test_and_set_bit(d->hwirq, pctrl->disabled_for_mux))
		disable_irq(irq);

	raw_spin_lock_irqsave(&pctrl->lock, flags);

	val = msm_readl_ctl(pctrl, g);

	/*
	 * If this is the first time muxing to GPIO and the direction is
	 * output, make sure that we're not going to be glitching the pin
	 * by reading the current state of the pin and setting it as the
	 * output.
	 */
	if (i == gpio_func && (val & BIT(g->oe_bit)) &&
	    !test_and_set_bit(group, pctrl->ever_gpio)) {
		u32 io_val = msm_readl_io(pctrl, g);

		if (io_val & BIT(g->in_bit)) {
			if (!(io_val & BIT(g->out_bit)))
				msm_writel_io(io_val | BIT(g->out_bit), pctrl, g);
		} else {
			if (io_val & BIT(g->out_bit))
				msm_writel_io(io_val & ~BIT(g->out_bit), pctrl, g);
		}
	}

	if (egpio_func && i == egpio_func) {
		if (val & BIT(g->egpio_present))
			val &= ~BIT(g->egpio_enable);
	} else {
		val &= ~mask;
		val |= i << g->mux_bit;
		/* Claim ownership of pin if egpio capable and
		 * also need check if setting NA in funcs.
		 */
		if (egpio_func && val & BIT(g->egpio_present)) {
			if (g->funcs[egpio_func] != ps->nfunctions)
				val |= BIT(g->egpio_enable);
		}
	}

	msm_writel_ctl(val, pctrl, g);

	raw_spin_unlock_irqrestore(&pctrl->lock, flags);

	if (d && i == gpio_func &&
	    test_and_clear_bit(d->hwirq, pctrl->disabled_for_mux)) {
		/*
		 * Clear interrupts detected while not GPIO since we only
		 * masked things.
		 */
		if (d->parent_data && test_bit(d->hwirq, pctrl->skip_wake_irqs))
			irq_chip_set_parent_state(d, IRQCHIP_STATE_PENDING, false);
		else
			msm_ack_intr_status(pctrl, g);

		enable_irq(irq);
	}

	return 0;
}

static int msm_pinmux_request_gpio(struct pinctrl_dev *pctldev,
				   struct pinctrl_gpio_range *range,
				   unsigned offset)
{
	struct msm_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	const struct msm_pingroup *g = &pctrl->soc->groups[offset];

	/* No funcs? Probably ACPI so can't do anything here */
	if (!g->nfuncs)
		return 0;

	return msm_pinmux_set_mux(pctldev, g->funcs[pctrl->soc->gpio_func], offset);
}

static const struct pinmux_ops msm_pinmux_ops = {
	.request		= msm_pinmux_request,
	.get_functions_count	= msm_get_functions_count,
	.get_function_name	= msm_get_function_name,
	.get_function_groups	= msm_get_function_groups,
	.gpio_request_enable	= msm_pinmux_request_gpio,
	.set_mux		= msm_pinmux_set_mux,
};

static int msm_config_reg(struct msm_pinctrl *pctrl,
			  const struct msm_pingroup *g,
			  unsigned param,
			  unsigned *mask,
			  unsigned *bit)
{
	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
	case PIN_CONFIG_BIAS_PULL_DOWN:
	case PIN_CONFIG_BIAS_BUS_HOLD:
	case PIN_CONFIG_BIAS_PULL_UP:
		*bit = g->pull_bit;
		*mask = 3;
		break;
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		*bit = g->od_bit;
		*mask = 1;
		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		*bit = g->drv_bit;
		*mask = 7;
		break;
	case PIN_CONFIG_OUTPUT:
	case PIN_CONFIG_INPUT_ENABLE:
		*bit = g->oe_bit;
		*mask = 1;
		break;
	case MSM_PIN_CONFIG_APPS:
	case MSM_PIN_CONFIG_REMOTE:
		*bit = EGPIO_ENABLE;
		*mask = 1;
		break;
	case MSM_PIN_CONFIG_I2C_PULL:
		*bit = I2C_PULL;
		*mask = 1;
		break;
	default:
		return -ENOTSUPP;
	}

	return 0;
}

#define MSM_NO_PULL		0
#define MSM_PULL_DOWN		1
#define MSM_KEEPER		2
#define MSM_PULL_UP_NO_KEEPER	2
#define MSM_PULL_UP		3

static unsigned msm_regval_to_drive(u32 val)
{
	return (val + 1) * 2;
}

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
	int ret;
	u32 val;

	if (!gpiochip_line_is_valid(&msm_pinctrl_data->chip, group))
		return -EINVAL;

	g = &pctrl->soc->groups[group];

	ret = msm_config_reg(pctrl, g, param, &mask, &bit);
	if (ret < 0)
		return ret;

	val = msm_readl_ctl(pctrl, g);
	arg = (val >> bit) & mask;

	/* Convert register value to pinconf value */
	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		if (arg != MSM_NO_PULL)
			return -EINVAL;
		arg = 1;
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		if (arg != MSM_PULL_DOWN)
			return -EINVAL;
		arg = 1;
		break;
	case PIN_CONFIG_BIAS_BUS_HOLD:
		if (pctrl->soc->pull_no_keeper)
			return -ENOTSUPP;

		if (arg != MSM_KEEPER)
			return -EINVAL;
		arg = 1;
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		if (pctrl->soc->pull_no_keeper)
			arg = arg == MSM_PULL_UP_NO_KEEPER;
		else
			arg = arg == MSM_PULL_UP;
		if (!arg)
			return -EINVAL;
		break;
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		/* Pin is not open-drain */
		if (!arg)
			return -EINVAL;
		arg = 1;
		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		arg = msm_regval_to_drive(arg);
		break;
	case PIN_CONFIG_OUTPUT:
		/* Pin is not output */
		if (!arg)
			return -EINVAL;

		val = msm_readl_io(pctrl, g);
		arg = !!(val & BIT(g->in_bit));
		break;
	case PIN_CONFIG_INPUT_ENABLE:
		/* Pin is output */
		if (arg)
			return -EINVAL;
		arg = 1;
		break;
	case MSM_PIN_CONFIG_APPS:
		/* Pin do not support egpio or remote owner */
		if (!(val & BIT(EGPIO_PRESENT)) || !arg)
			return -EINVAL;

		arg = 1;
		break;
	case MSM_PIN_CONFIG_REMOTE:
		/* Pin do not support epgio or APPS owner */
		if (!(val & BIT(EGPIO_PRESENT)) || arg)
			return -EINVAL;

		arg = 1;
		break;
	case MSM_PIN_CONFIG_I2C_PULL:
		arg = 1;
		break;
	default:
		return -ENOTSUPP;
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
	u32 owner_bit, owner_update = 0;
	int ret;
	u32 val;
	int i;

	g = &pctrl->soc->groups[group];

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		ret = msm_config_reg(pctrl, g, param, &mask, &bit);
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
		case PIN_CONFIG_BIAS_BUS_HOLD:
			if (pctrl->soc->pull_no_keeper)
				return -ENOTSUPP;

			arg = MSM_KEEPER;
			break;
		case PIN_CONFIG_BIAS_PULL_UP:
			if (pctrl->soc->pull_no_keeper)
				arg = MSM_PULL_UP_NO_KEEPER;
			else
				arg = MSM_PULL_UP;
			break;
		case PIN_CONFIG_DRIVE_OPEN_DRAIN:
			arg = 1;
			break;
		case PIN_CONFIG_DRIVE_STRENGTH:
			/* Check for invalid values */
			if (arg > 16 || arg < 2 || (arg % 2) != 0)
				arg = -1;
			else
				arg = (arg / 2) - 1;
			break;
		case PIN_CONFIG_OUTPUT:
			/* set output value */
			raw_spin_lock_irqsave(&pctrl->lock, flags);
			val = msm_readl_io(pctrl, g);
			if (arg)
				val |= BIT(g->out_bit);
			else
				val &= ~BIT(g->out_bit);
			msm_writel_io(val, pctrl, g);
			raw_spin_unlock_irqrestore(&pctrl->lock, flags);

			/* enable output */
			arg = 1;
			break;
		case PIN_CONFIG_INPUT_ENABLE:
			/* disable output */
			arg = 0;
			break;
		case MSM_PIN_CONFIG_APPS:
			owner_update = 1;
			owner_bit = MSM_APPS_OWNER;
			break;
		case MSM_PIN_CONFIG_REMOTE:
			owner_update = 1;
			owner_bit = MSM_REMOTE_OWNER;
			break;
		case MSM_PIN_CONFIG_I2C_PULL:
			arg = 1;
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
		/* Update the owner bits as final config update */
		if (param == MSM_PIN_CONFIG_APPS || param == MSM_PIN_CONFIG_REMOTE)
			continue;

		raw_spin_lock_irqsave(&pctrl->lock, flags);
		val = msm_readl_ctl(pctrl, g);
		val &= ~(mask << bit);
		val |= arg << bit;
		msm_writel_ctl(val, pctrl, g);
		raw_spin_unlock_irqrestore(&pctrl->lock, flags);
	}

	if (owner_update) {
		ret = msm_config_reg(pctrl, g,
					MSM_PIN_CONFIG_APPS, &mask, &bit);
		if (ret < 0)
			return ret;

		raw_spin_lock_irqsave(&pctrl->lock, flags);
		val = msm_readl_ctl(pctrl, g);
		if (val & BIT(EGPIO_PRESENT)) {
			val &= ~(mask << bit);
			val |= owner_bit << bit;
			msm_writel_ctl(val, pctrl, g);
		}
		raw_spin_unlock_irqrestore(&pctrl->lock, flags);
	}

	return 0;
}

static const struct pinconf_ops msm_pinconf_ops = {
	.is_generic		= true,
	.pin_config_group_get	= msm_config_group_get,
	.pin_config_group_set	= msm_config_group_set,
};

static int msm_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	const struct msm_pingroup *g;
	struct msm_pinctrl *pctrl = gpiochip_get_data(chip);
	unsigned long flags;
	u32 val;

	g = &pctrl->soc->groups[offset];

	raw_spin_lock_irqsave(&pctrl->lock, flags);

	val = msm_readl_ctl(pctrl, g);
	val &= ~BIT(g->oe_bit);
	msm_writel_ctl(val, pctrl, g);

	raw_spin_unlock_irqrestore(&pctrl->lock, flags);

	return 0;
}

static int msm_gpio_direction_output(struct gpio_chip *chip, unsigned offset, int value)
{
	const struct msm_pingroup *g;
	struct msm_pinctrl *pctrl = gpiochip_get_data(chip);
	unsigned long flags;
	u32 val;

	g = &pctrl->soc->groups[offset];

	raw_spin_lock_irqsave(&pctrl->lock, flags);

	val = msm_readl_io(pctrl, g);
	if (value)
		val |= BIT(g->out_bit);
	else
		val &= ~BIT(g->out_bit);
	msm_writel_io(val, pctrl, g);

	val = msm_readl_ctl(pctrl, g);
	val |= BIT(g->oe_bit);
	msm_writel_ctl(val, pctrl, g);

	raw_spin_unlock_irqrestore(&pctrl->lock, flags);

	return 0;
}

static int msm_gpio_get_direction(struct gpio_chip *chip, unsigned int offset)
{
	struct msm_pinctrl *pctrl = gpiochip_get_data(chip);
	const struct msm_pingroup *g;
	u32 val;

	g = &pctrl->soc->groups[offset];

	val = msm_readl_ctl(pctrl, g);

	return val & BIT(g->oe_bit) ? GPIO_LINE_DIRECTION_OUT :
				      GPIO_LINE_DIRECTION_IN;
}

static int msm_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	const struct msm_pingroup *g;
	struct msm_pinctrl *pctrl = gpiochip_get_data(chip);
	u32 val;

	g = &pctrl->soc->groups[offset];

	val = msm_readl_io(pctrl, g);
	return !!(val & BIT(g->in_bit));
}

static void msm_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	const struct msm_pingroup *g;
	struct msm_pinctrl *pctrl = gpiochip_get_data(chip);
	unsigned long flags;
	u32 val;

	g = &pctrl->soc->groups[offset];

	raw_spin_lock_irqsave(&pctrl->lock, flags);

	val = msm_readl_io(pctrl, g);
	if (value)
		val |= BIT(g->out_bit);
	else
		val &= ~BIT(g->out_bit);
	msm_writel_io(val, pctrl, g);

	raw_spin_unlock_irqrestore(&pctrl->lock, flags);
}

static void msm_gpio_pin_status_get(struct msm_pinctrl *pctrl, const struct msm_pingroup *g,
				    unsigned int offset, int *is_out, unsigned int *func,
				    int *drive, int *pull, int *egpio_enable, int *val)
{
	u32 ctl_reg, io_reg;

	ctl_reg = msm_readl_ctl(pctrl, g);
	io_reg = msm_readl_io(pctrl, g);

	*is_out = !!(ctl_reg & BIT(g->oe_bit));
	*func = (ctl_reg >> g->mux_bit) & 7;
	*drive = (ctl_reg >> g->drv_bit) & 7;
	*pull = (ctl_reg >> g->pull_bit) & 3;
	*egpio_enable = 0;
	if (pctrl->soc->egpio_func && ctl_reg & BIT(g->egpio_present))
		*egpio_enable = !(ctl_reg & BIT(g->egpio_enable));

	if (*is_out)
		*val = !!(io_reg & BIT(g->out_bit));
	else
		*val = !!(io_reg & BIT(g->in_bit));
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
	struct msm_pinctrl *pctrl = gpiochip_get_data(chip);
	unsigned func;
	int is_out;
	int drive;
	int pull;
	int val;
	int egpio_enable;

	if (!gpiochip_line_is_valid(chip, offset))
		return;

	g = &pctrl->soc->groups[offset];
	msm_gpio_pin_status_get(pctrl, g, offset, &is_out, &func,
					&drive, &pull, &egpio_enable, &val);

	if (egpio_enable) {
		seq_printf(s, " %-8s: egpio\n", g->name);
		return;
	}

	seq_printf(s, " %-8s: %-3s", g->name, is_out ? "out" : "in");
	seq_printf(s, " %-4s func%d", val ? "high" : "low", func);
	seq_printf(s, " %dmA", msm_regval_to_drive(drive));
	if (pctrl->soc->pull_no_keeper)
		seq_printf(s, " %s", pulls_no_keeper[pull]);
	else
		seq_printf(s, " %s", pulls_keeper[pull]);
	seq_puts(s, "\n");
}

static void msm_gpio_dbg_show(struct seq_file *s, struct gpio_chip *chip)
{
	unsigned gpio = chip->base;
	unsigned i;

	for (i = 0; i < chip->ngpio; i++, gpio++)
		msm_gpio_dbg_show_one(s, NULL, chip, i, gpio);
}

#else
#define msm_gpio_dbg_show NULL
#endif

static void msm_gpio_log_pin_status(struct gpio_chip *chip, unsigned int offset)
{
	const struct msm_pingroup *g;
	struct msm_pinctrl *pctrl = gpiochip_get_data(chip);
	unsigned int func;
	int is_out;
	int drive;
	int pull;
	int val;
	int egpio_enable;

	if (!gpiochip_line_is_valid(chip, offset))
		return;

	g = &pctrl->soc->groups[offset];
	msm_gpio_pin_status_get(pctrl, g, offset, &is_out, &func,
					&drive, &pull, &egpio_enable, &val);

	printk_deferred("%s: %s, %s, func%d, %dmA, %s\n",
			g->name, is_out ? "out" : "in",
			val ? "high" : "low", func,
			msm_regval_to_drive(drive),
			pctrl->soc->pull_no_keeper ? pulls_no_keeper[pull] : pulls_keeper[pull]);
}

static void msm_gpios_status(struct gpio_chip *chip)
{
	unsigned int i;

	for (i = 0; i < chip->ngpio; i++)
		msm_gpio_log_pin_status(chip, i);
}

static int msm_gpio_init_valid_mask(struct gpio_chip *gc,
				    unsigned long *valid_mask,
				    unsigned int ngpios)
{
	struct msm_pinctrl *pctrl = gpiochip_get_data(gc);
	int ret;
	unsigned int len, i;
	const int *reserved = pctrl->soc->reserved_gpios;
	struct property *prop;
	const __be32 *p;
	u16 *tmp;

	/* Driver provided reserved list overrides DT and ACPI */
	if (reserved) {
		bitmap_fill(valid_mask, ngpios);
		for (i = 0; reserved[i] >= 0; i++) {
			if (i >= ngpios || reserved[i] >= ngpios) {
				dev_err(pctrl->dev, "invalid list of reserved GPIOs\n");
				return -EINVAL;
			}
			clear_bit(reserved[i], valid_mask);
		}

		return 0;
	}

	if (of_property_count_u32_elems(pctrl->dev->of_node, "qcom,gpios-reserved") > 0) {
		bitmap_fill(valid_mask, ngpios);
		of_property_for_each_u32(pctrl->dev->of_node, "qcom,gpios-reserved", prop, p, i) {
			if (i >= ngpios) {
				dev_err(pctrl->dev, "invalid list of reserved GPIOs\n");
				return -EINVAL;
			}
			clear_bit(i, valid_mask);
		}
	}

	/* The number of GPIOs in the ACPI tables */
	len = ret = device_property_count_u16(pctrl->dev, "gpios");
	if (ret < 0)
		return 0;

	if (ret > ngpios)
		return -EINVAL;

	tmp = kmalloc_array(len, sizeof(*tmp), GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	ret = device_property_read_u16_array(pctrl->dev, "gpios", tmp, len);
	if (ret < 0) {
		dev_err(pctrl->dev, "could not read list of GPIOs\n");
		goto out;
	}

	bitmap_zero(valid_mask, ngpios);
	for (i = 0; i < len; i++)
		set_bit(tmp[i], valid_mask);

out:
	kfree(tmp);
	return ret;
}

static const struct gpio_chip msm_gpio_template = {
	.direction_input  = msm_gpio_direction_input,
	.direction_output = msm_gpio_direction_output,
	.get_direction    = msm_gpio_get_direction,
	.get              = msm_gpio_get,
	.set              = msm_gpio_set,
	.request          = gpiochip_generic_request,
	.free             = gpiochip_generic_free,
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
		val = msm_readl_io(pctrl, g) & BIT(g->in_bit);

		pol = msm_readl_intr_cfg(pctrl, g);
		pol ^= BIT(g->intr_polarity_bit);
		msm_writel_intr_cfg(pol, pctrl, g);

		val2 = msm_readl_io(pctrl, g) & BIT(g->in_bit);
		intstat = msm_readl_intr_status(pctrl, g);
		if (intstat || (val == val2))
			return;
	} while (loop_limit-- > 0);
	dev_err(pctrl->dev, "dual-edge irq failed to stabilize, %#08x != %#08x\n",
		val, val2);
}

static void msm_gpio_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct msm_pinctrl *pctrl = gpiochip_get_data(gc);
	const struct msm_pingroup *g;
	unsigned long flags;
	u32 val;

	if (d->parent_data)
		irq_chip_mask_parent(d);

	if (test_bit(d->hwirq, pctrl->skip_wake_irqs))
		return;

	g = &pctrl->soc->groups[d->hwirq];

	raw_spin_lock_irqsave(&pctrl->lock, flags);

	val = msm_readl_intr_cfg(pctrl, g);
	/*
	 * There are two bits that control interrupt forwarding to the CPU. The
	 * RAW_STATUS_EN bit causes the level or edge sensed on the line to be
	 * latched into the interrupt status register when the hardware detects
	 * an irq that it's configured for (either edge for edge type or level
	 * for level type irq). The 'non-raw' status enable bit causes the
	 * hardware to assert the summary interrupt to the CPU if the latched
	 * status bit is set. There's a bug though, the edge detection logic
	 * seems to have a problem where toggling the RAW_STATUS_EN bit may
	 * cause the status bit to latch spuriously when there isn't any edge
	 * so we can't touch that bit for edge type irqs and we have to keep
	 * the bit set anyway so that edges are latched while the line is masked.
	 *
	 * To make matters more complicated, leaving the RAW_STATUS_EN bit
	 * enabled all the time causes level interrupts to re-latch into the
	 * status register because the level is still present on the line after
	 * we ack it. We clear the raw status enable bit during mask here and
	 * set the bit on unmask so the interrupt can't latch into the hardware
	 * while it's masked.
	 */
	if (irqd_get_trigger_type(d) & IRQ_TYPE_LEVEL_MASK)
		val &= ~BIT(g->intr_raw_status_bit);

	val &= ~BIT(g->intr_enable_bit);
	msm_writel_intr_cfg(val, pctrl, g);

	clear_bit(d->hwirq, pctrl->enabled_irqs);

	raw_spin_unlock_irqrestore(&pctrl->lock, flags);
}

static void msm_gpio_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct msm_pinctrl *pctrl = gpiochip_get_data(gc);
	const struct msm_pingroup *g;
	unsigned long flags;
	u32 val;

	if (d->parent_data)
		irq_chip_unmask_parent(d);

	if (test_bit(d->hwirq, pctrl->skip_wake_irqs))
		return;

	g = &pctrl->soc->groups[d->hwirq];

	raw_spin_lock_irqsave(&pctrl->lock, flags);

	val = msm_readl_intr_cfg(pctrl, g);
	val |= BIT(g->intr_raw_status_bit);
	val |= BIT(g->intr_enable_bit);
	msm_writel_intr_cfg(val, pctrl, g);

	set_bit(d->hwirq, pctrl->enabled_irqs);

	raw_spin_unlock_irqrestore(&pctrl->lock, flags);
}

static void msm_gpio_dirconn_handler(struct irq_desc *desc)
{
	struct irq_data *irqd = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);

	if (!irqd)
		return;

	chained_irq_enter(chip, desc);
	generic_handle_irq(irqd->irq);
	chained_irq_exit(chip, desc);
	irq_set_irqchip_state(irq_desc_get_irq_data(desc)->irq,
				IRQCHIP_STATE_ACTIVE, 0);
}

static void configure_tlmm_dc_polarity(struct irq_data *d,
				unsigned int type, u32 offset)
{
	const struct msm_pingroup *g;
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct msm_pinctrl *pctrl = gpiochip_get_data(gc);
	unsigned int polarity = 1, val;
	unsigned long flags;

	if (type & (IRQ_TYPE_EDGE_RISING | IRQ_TYPE_LEVEL_HIGH)) {
		/*
		 * Since the default polarity is set to 0, change it to 1 for
		 * Rising edge and active high interrupt type such that the line
		 * is not inverted.
		 */
		raw_spin_lock_irqsave(&pctrl->lock, flags);
		g = &pctrl->soc->groups[d->hwirq];
		val = readl_relaxed(msm_pinctrl_data->regs[g->tile] +
						g->dir_conn_reg + (offset * 4));
		val |= polarity << 8;
		writel_relaxed(val, msm_pinctrl_data->regs[g->tile] +
						g->dir_conn_reg + (offset * 4));
		raw_spin_unlock_irqrestore(&pctrl->lock, flags);
	}
}

static void enable_tlmm_dc(irq_hw_number_t irq)
{
	struct irq_data *dir_conn_data = irq_get_irq_data(irq);

	if (!dir_conn_data || !(dir_conn_data->chip))
		return;

	dir_conn_data->chip->irq_unmask(dir_conn_data);
}

static bool is_gpio_tlmm_dc(struct irq_data *d, u32 *offset,
				irq_hw_number_t *irq)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct msm_pinctrl *pctrl = gpiochip_get_data(gc);
	struct msm_dir_conn *dc;
	int i;

	for (i = pctrl->n_dir_conns; i > 0; i--) {
		dc = &pctrl->soc->dir_conn[i];
		*offset = pctrl->n_dir_conns - i;
		*irq = dc->irq;

		if (dc->gpio == d->hwirq)
			return true;
	}

	return false;
}

static void msm_gpio_irq_enable(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct msm_pinctrl *pctrl = gpiochip_get_data(gc);

	gpiochip_enable_irq(gc, d->hwirq);

	if (d->parent_data)
		irq_chip_enable_parent(d);

	if (!test_bit(d->hwirq, pctrl->skip_wake_irqs))
		msm_gpio_irq_unmask(d);
}

static void msm_gpio_irq_disable(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct msm_pinctrl *pctrl = gpiochip_get_data(gc);

	if (d->parent_data)
		irq_chip_disable_parent(d);

	if (!test_bit(d->hwirq, pctrl->skip_wake_irqs))
		msm_gpio_irq_mask(d);

	gpiochip_disable_irq(gc, d->hwirq);
}

/**
 * msm_gpio_update_dual_edge_parent() - Prime next edge for IRQs handled by parent.
 * @d: The irq dta.
 *
 * This is much like msm_gpio_update_dual_edge_pos() but for IRQs that are
 * normally handled by the parent irqchip.  The logic here is slightly
 * different due to what's easy to do with our parent, but in principle it's
 * the same.
 */
static void msm_gpio_update_dual_edge_parent(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct msm_pinctrl *pctrl = gpiochip_get_data(gc);
	const struct msm_pingroup *g = &pctrl->soc->groups[d->hwirq];
	int loop_limit = 100;
	unsigned int val;
	unsigned int type;

	/* Read the value and make a guess about what edge we need to catch */
	val = msm_readl_io(pctrl, g) & BIT(g->in_bit);
	type = val ? IRQ_TYPE_EDGE_FALLING : IRQ_TYPE_EDGE_RISING;

	do {
		/* Set the parent to catch the next edge */
		irq_chip_set_type_parent(d, type);

		/*
		 * Possibly the line changed between when we last read "val"
		 * (and decided what edge we needed) and when set the edge.
		 * If the value didn't change (or changed and then changed
		 * back) then we're done.
		 */
		val = msm_readl_io(pctrl, g) & BIT(g->in_bit);
		if (type == IRQ_TYPE_EDGE_RISING) {
			if (!val)
				return;
			type = IRQ_TYPE_EDGE_FALLING;
		} else if (type == IRQ_TYPE_EDGE_FALLING) {
			if (val)
				return;
			type = IRQ_TYPE_EDGE_RISING;
		}
	} while (loop_limit-- > 0);
	dev_warn_once(pctrl->dev, "dual-edge irq failed to stabilize\n");
}

static void msm_gpio_irq_ack(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct msm_pinctrl *pctrl = gpiochip_get_data(gc);
	const struct msm_pingroup *g;
	unsigned long flags;

	if (test_bit(d->hwirq, pctrl->skip_wake_irqs)) {
		if (test_bit(d->hwirq, pctrl->dual_edge_irqs))
			msm_gpio_update_dual_edge_parent(d);
		return;
	}

	g = &pctrl->soc->groups[d->hwirq];

	raw_spin_lock_irqsave(&pctrl->lock, flags);

	msm_ack_intr_status(pctrl, g);

	if (test_bit(d->hwirq, pctrl->dual_edge_irqs))
		msm_gpio_update_dual_edge_pos(pctrl, g, d);

	raw_spin_unlock_irqrestore(&pctrl->lock, flags);
}

static void msm_gpio_irq_eoi(struct irq_data *d)
{
	d = d->parent_data;

	if (d)
		d->chip->irq_eoi(d);
}

static bool msm_gpio_needs_dual_edge_parent_workaround(struct irq_data *d,
						       unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct msm_pinctrl *pctrl = gpiochip_get_data(gc);

	return type == IRQ_TYPE_EDGE_BOTH &&
	       pctrl->soc->wakeirq_dual_edge_errata && d->parent_data &&
	       test_bit(d->hwirq, pctrl->skip_wake_irqs);
}

static void msm_dirconn_cfg_reg(struct irq_data *d, u32 offset)
{
	u32 val;
	const struct msm_pingroup *g;
	unsigned long flags;
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct msm_pinctrl *pctrl = gpiochip_get_data(gc);

	raw_spin_lock_irqsave(&pctrl->lock, flags);
	g = &pctrl->soc->groups[d->hwirq];

	val = (d->hwirq) & 0xFF;

	writel_relaxed(val, pctrl->regs[g->tile] + g->dir_conn_reg
					+ (offset * 4));

	val = msm_readl_intr_cfg(pctrl, g);
	val |= BIT(g->dir_conn_en_bit);

	msm_writel_intr_cfg(val, pctrl, g);
	raw_spin_unlock_irqrestore(&pctrl->lock, flags);
}

static int msm_gpio_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct msm_pinctrl *pctrl = gpiochip_get_data(gc);
	const struct msm_pingroup *g;
	irq_hw_number_t irq = 0;
	u32 intr_target_mask = GENMASK(2, 0);
	unsigned long flags;
	u32 offset = 0;
	bool was_enabled;
	u32 val;

	if (msm_gpio_needs_dual_edge_parent_workaround(d, type)) {
		set_bit(d->hwirq, pctrl->dual_edge_irqs);
		irq_set_handler_locked(d, handle_fasteoi_ack_irq);
		msm_gpio_update_dual_edge_parent(d);
		return 0;
	}

	if (d->parent_data)
		irq_chip_set_type_parent(d, type);

	if (test_bit(d->hwirq, pctrl->skip_wake_irqs)) {
		clear_bit(d->hwirq, pctrl->dual_edge_irqs);
		irq_set_handler_locked(d, handle_fasteoi_irq);
		return 0;
	}

	if (is_gpio_tlmm_dc(d, &offset, &irq) && type != IRQ_TYPE_EDGE_BOTH) {
		configure_tlmm_dc_polarity(d, type, offset);
		enable_tlmm_dc(irq);
		if (type & (IRQ_TYPE_LEVEL_LOW | IRQ_TYPE_LEVEL_HIGH))
			irq_set_handler_locked(d, handle_level_irq);
		else if (type & (IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_EDGE_RISING))
			irq_set_handler_locked(d, handle_edge_irq);
		return 0;
	}

	g = &pctrl->soc->groups[d->hwirq];

	raw_spin_lock_irqsave(&pctrl->lock, flags);

	/*
	 * For hw without possibility of detecting both edges
	 */
	if (g->intr_detection_width == 1 && type == IRQ_TYPE_EDGE_BOTH)
		set_bit(d->hwirq, pctrl->dual_edge_irqs);
	else
		clear_bit(d->hwirq, pctrl->dual_edge_irqs);

	/* Route interrupts to application cpu.
	 * With intr_target_use_scm interrupts are routed to
	 * application cpu using scm calls.
	 */
	if (g->intr_target_width)
		intr_target_mask = GENMASK(g->intr_target_width - 1, 0);

	if (pctrl->intr_target_use_scm) {
		u32 addr = pctrl->phys_base[0] + g->intr_target_reg;
		int ret;
		qcom_scm_io_readl(addr, &val);
		val &= ~(intr_target_mask << g->intr_target_bit);
		val |= g->intr_target_kpss_val << g->intr_target_bit;
		ret = qcom_scm_io_writel(addr, val);
		if (ret)
			dev_err(pctrl->dev,
				"Failed routing %lu interrupt to Apps proc",
				d->hwirq);
	} else {
		val = msm_readl_intr_target(pctrl, g);
		val &= ~(intr_target_mask << g->intr_target_bit);
		val |= g->intr_target_kpss_val << g->intr_target_bit;
		msm_writel_intr_target(val, pctrl, g);
	}

	/* Update configuration for gpio.
	 * RAW_STATUS_EN is left on for all gpio irqs. Due to the
	 * internal circuitry of TLMM, toggling the RAW_STATUS
	 * could cause the INTR_STATUS to be set for EDGE interrupts.
	 */
	val = msm_readl_intr_cfg(pctrl, g);
	was_enabled = val & BIT(g->intr_raw_status_bit);
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
			val |= BIT(g->intr_polarity_bit);
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
	msm_writel_intr_cfg(val, pctrl, g);

	/*
	 * The first time we set RAW_STATUS_EN it could trigger an interrupt.
	 * Clear the interrupt.  This is safe because we have
	 * IRQCHIP_SET_TYPE_MASKED.
	 */
	if (!was_enabled)
		msm_ack_intr_status(pctrl, g);

	if (test_bit(d->hwirq, pctrl->dual_edge_irqs))
		msm_gpio_update_dual_edge_pos(pctrl, g, d);

	raw_spin_unlock_irqrestore(&pctrl->lock, flags);

	if (type & (IRQ_TYPE_LEVEL_LOW | IRQ_TYPE_LEVEL_HIGH))
		irq_set_handler_locked(d, handle_level_irq);
	else if (type & (IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_EDGE_RISING))
		irq_set_handler_locked(d, handle_edge_irq);

	return 0;
}

static int msm_gpio_irq_set_wake(struct irq_data *d, unsigned int on)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct msm_pinctrl *pctrl = gpiochip_get_data(gc);

	/*
	 * While they may not wake up when the TLMM is powered off,
	 * some GPIOs would like to wakeup the system from suspend
	 * when TLMM is powered on. To allow that, enable the GPIO
	 * summary line to be wakeup capable at GIC.
	 */
	if (d->parent_data && test_bit(d->hwirq, pctrl->skip_wake_irqs))
		return irq_chip_set_wake_parent(d, on);

	return irq_set_irq_wake(pctrl->irq, on);
}

static int msm_gpio_irq_reqres(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct msm_pinctrl *pctrl = gpiochip_get_data(gc);
	int ret;

	if (!try_module_get(gc->owner))
		return -ENODEV;

	ret = msm_pinmux_request_gpio(pctrl->pctrl, NULL, d->hwirq);
	if (ret)
		goto out;
	msm_gpio_direction_input(gc, d->hwirq);

	if (gpiochip_lock_as_irq(gc, d->hwirq)) {
		dev_err(gc->parent,
			"unable to lock HW IRQ %lu for IRQ\n",
			d->hwirq);
		ret = -EINVAL;
		goto out;
	}

	/*
	 * The disable / clear-enable workaround we do in msm_pinmux_set_mux()
	 * only works if disable is not lazy since we only clear any bogus
	 * interrupt in hardware. Explicitly mark the interrupt as UNLAZY.
	 */
	irq_set_status_flags(d->irq, IRQ_DISABLE_UNLAZY);

	return 0;
out:
	module_put(gc->owner);
	return ret;
}

static void msm_gpio_irq_relres(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);

	gpiochip_unlock_as_irq(gc, d->hwirq);
	module_put(gc->owner);
}

static int msm_gpio_irq_set_affinity(struct irq_data *d,
				const struct cpumask *dest, bool force)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct msm_pinctrl *pctrl = gpiochip_get_data(gc);
	unsigned int i, n_dir_conns = pctrl->n_dir_conns;
	struct irq_data *gpio_irq_data;
	struct msm_dir_conn *dc = NULL;

	for (i = n_dir_conns; i > 0; i--) {
		dc = &pctrl->soc->dir_conn[i];
		gpio_irq_data = irq_get_irq_data(dc->irq);

		if (!gpio_irq_data || !(gpio_irq_data->chip) ||
				!(gpio_irq_data->chip->irq_set_affinity))
			continue;

		if (d->hwirq == dc->gpio)
			return gpio_irq_data->chip->irq_set_affinity(gpio_irq_data, dest, force);
	}

	if (d->parent_data && test_bit(d->hwirq, pctrl->skip_wake_irqs))
		return irq_chip_set_affinity_parent(d, dest, force);

	return -EINVAL;
}

static int msm_gpio_irq_set_irqchip_state(struct irq_data *d, enum irqchip_irq_state which,
					  bool val)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct msm_pinctrl *pctrl = gpiochip_get_data(gc);
	const struct msm_pingroup *g = &pctrl->soc->groups[d->hwirq];

	if (which != IRQCHIP_STATE_PENDING)
		return -EINVAL;

	if (test_bit(d->hwirq, pctrl->skip_wake_irqs))
		return -EINVAL;

	msm_writel_intr_status(val, pctrl, g);

	return 0;
}

static int msm_gpio_irq_get_irqchip_state(struct irq_data *d, enum irqchip_irq_state which,
					  bool *val)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct msm_pinctrl *pctrl = gpiochip_get_data(gc);
	const struct msm_pingroup *g = &pctrl->soc->groups[d->hwirq];

	if (which != IRQCHIP_STATE_PENDING)
		return -EINVAL;

	if (test_bit(d->hwirq, pctrl->skip_wake_irqs))
		return -EINVAL;

	g = &pctrl->soc->groups[d->hwirq];
	*val = msm_readl_intr_status(pctrl, g);

	return 0;
}

static int msm_gpio_irq_set_vcpu_affinity(struct irq_data *d, void *vcpu_info)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct msm_pinctrl *pctrl = gpiochip_get_data(gc);

	if (d->parent_data && test_bit(d->hwirq, pctrl->skip_wake_irqs))
		return irq_chip_set_vcpu_affinity_parent(d, vcpu_info);

	return -EINVAL;
}

static void msm_gpio_irq_handler(struct irq_desc *desc)
{
	struct gpio_chip *gc = irq_desc_get_handler_data(desc);
	const struct msm_pingroup *g;
	struct msm_pinctrl *pctrl = gpiochip_get_data(gc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
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
		val = msm_readl_intr_status(pctrl, g);
		if (val & BIT(g->intr_status_bit)) {
			generic_handle_domain_irq(gc->irq.domain, i);
			handled++;
		}
	}

	/* No interrupts were flagged */
	if (handled == 0)
		handle_bad_irq(desc);

	chained_irq_exit(chip, desc);
}

static int msm_gpio_wakeirq(struct gpio_chip *gc,
			    unsigned int child,
			    unsigned int child_type,
			    unsigned int *parent,
			    unsigned int *parent_type)
{
	struct msm_pinctrl *pctrl = gpiochip_get_data(gc);
	const struct msm_gpio_wakeirq_map *map;
	const struct msm_pingroup *g;
	u32 intr_cfg;
	int i;

	*parent = GPIO_NO_WAKE_IRQ;
	*parent_type = IRQ_TYPE_EDGE_RISING;

	for (i = 0; i < pctrl->soc->nwakeirq_map; i++) {
		map = &pctrl->soc->wakeirq_map[i];
		if (map->gpio == child) {
			*parent = map->wakeirq;
			break;
		}
	}

	/*
	 * Additionally set intr_wakeup_enable_bit for the GPIOs Routed
	 * to parent irqchip depending on intr_wakeup_present_bit.
	 */
	if (*parent != GPIO_NO_WAKE_IRQ) {
		g = &pctrl->soc->groups[child];

		if (!g->intr_wakeup_present_bit)
			return 0;

		intr_cfg = msm_readl_intr_cfg(pctrl, g);
		if (intr_cfg & BIT(g->intr_wakeup_present_bit)) {
			intr_cfg |= BIT(g->intr_wakeup_enable_bit);
			msm_writel_intr_cfg(intr_cfg, pctrl, g);
		}
	}

	return 0;
}

static bool msm_gpio_needs_valid_mask(struct msm_pinctrl *pctrl)
{
	bool have_reserved, have_gpios;

	if (pctrl->soc->reserved_gpios)
		return true;

	have_reserved = of_property_count_u32_elems(pctrl->dev->of_node, "qcom,gpios-reserved") > 0;
	have_gpios = device_property_count_u16(pctrl->dev, "gpios") > 0;

	if (have_reserved && have_gpios)
		dev_warn(pctrl->dev, "qcom,gpios-reserved and gpios are both defined. Only one should be used.\n");

	return have_reserved || have_gpios;
}

static const struct irq_chip msm_gpio_irq_chip = {
	.name			= "msmgpio",
	.irq_enable		= msm_gpio_irq_enable,
	.irq_disable		= msm_gpio_irq_disable,
	.irq_mask		= msm_gpio_irq_mask,
	.irq_unmask		= msm_gpio_irq_unmask,
	.irq_ack		= msm_gpio_irq_ack,
	.irq_eoi		= msm_gpio_irq_eoi,
	.irq_set_type		= msm_gpio_irq_set_type,
	.irq_set_wake		= msm_gpio_irq_set_wake,
	.irq_request_resources	= msm_gpio_irq_reqres,
	.irq_release_resources	= msm_gpio_irq_relres,
	.irq_set_affinity	= msm_gpio_irq_set_affinity,
	.irq_set_irqchip_state = msm_gpio_irq_set_irqchip_state,
	.irq_get_irqchip_state = msm_gpio_irq_get_irqchip_state,
	.irq_set_vcpu_affinity	= msm_gpio_irq_set_vcpu_affinity,
	.flags			= (IRQCHIP_MASK_ON_SUSPEND |
				   IRQCHIP_SET_TYPE_MASKED |
				   IRQCHIP_ENABLE_WAKEUP_ON_SUSPEND |
				   IRQCHIP_IMMUTABLE),
};

static int msm_gpio_init(struct msm_pinctrl *pctrl)
{
	struct gpio_chip *chip;
	struct gpio_irq_chip *girq;
	int i, ret;
	unsigned gpio, ngpio = pctrl->soc->ngpios;
	struct device_node *np;
	bool skip;

	if (WARN_ON(ngpio > MAX_NR_GPIO))
		return -EINVAL;

	chip = &pctrl->chip;
	chip->base = -1;
	chip->ngpio = ngpio;
	chip->label = dev_name(pctrl->dev);
	chip->parent = pctrl->dev;
	chip->owner = THIS_MODULE;
	if (msm_gpio_needs_valid_mask(pctrl))
		chip->init_valid_mask = msm_gpio_init_valid_mask;

	np = of_parse_phandle(pctrl->dev->of_node, "wakeup-parent", 0);
	if (np) {
		chip->irq.parent_domain = irq_find_matching_host(np,
						 DOMAIN_BUS_WAKEUP);
		of_node_put(np);
		if (!chip->irq.parent_domain)
			return -EPROBE_DEFER;
		chip->irq.child_to_parent_hwirq = msm_gpio_wakeirq;
		/*
		 * Let's skip handling the GPIOs, if the parent irqchip
		 * is handling the direct connect IRQ of the GPIO.
		 */
		skip = irq_domain_qcom_handle_wakeup(chip->irq.parent_domain);
		for (i = 0; skip && i < pctrl->soc->nwakeirq_map; i++) {
			gpio = pctrl->soc->wakeirq_map[i].gpio;
			set_bit(gpio, pctrl->skip_wake_irqs);
		}
	}

	girq = &chip->irq;
	gpio_irq_chip_set_chip(girq, &msm_gpio_irq_chip);
	girq->parent_handler = msm_gpio_irq_handler;
	girq->fwnode = pctrl->dev->fwnode;
	girq->num_parents = 1;
	girq->parents = devm_kcalloc(pctrl->dev, 1, sizeof(*girq->parents),
				     GFP_KERNEL);
	if (!girq->parents)
		return -ENOMEM;
	girq->default_type = IRQ_TYPE_NONE;
	girq->handler = handle_bad_irq;
	girq->parents[0] = pctrl->irq;

	ret = gpiochip_add_data(&pctrl->chip, pctrl);
	if (ret) {
		dev_err(pctrl->dev, "Failed register gpiochip\n");
		return ret;
	}

	/*
	 * For DeviceTree-supported systems, the gpio core checks the
	 * pinctrl's device node for the "gpio-ranges" property.
	 * If it is present, it takes care of adding the pin ranges
	 * for the driver. In this case the driver can skip ahead.
	 *
	 * In order to remain compatible with older, existing DeviceTree
	 * files which don't set the "gpio-ranges" property or systems that
	 * utilize ACPI the driver has to call gpiochip_add_pin_range().
	 */
	if (!of_property_read_bool(pctrl->dev->of_node, "gpio-ranges")) {
		ret = gpiochip_add_pin_range(&pctrl->chip,
			dev_name(pctrl->dev), 0, 0, chip->ngpio);
		if (ret) {
			dev_err(pctrl->dev, "Failed to add pin range\n");
			gpiochip_remove(&pctrl->chip);
			return ret;
		}
	}

	return 0;
}

static void msm_gpio_setup_dir_connects(struct msm_pinctrl *pctrl)
{
	struct msm_dir_conn *dc = NULL;
	unsigned int i, n_dir_conns = pctrl->n_dir_conns, offset = 0;
	irq_hw_number_t dirconn_irq = 0, gpio_irq = 0;
	struct irq_data *gpio_irq_data;
	struct irq_domain *child_domain = pctrl->chip.irq.domain;

	for (i = n_dir_conns; i > 0; i--) {
		dc = &pctrl->soc->dir_conn[i];
		dirconn_irq = dc->irq;
		offset = n_dir_conns - i;

		if (dc->gpio == -1)
			continue;

		gpio_irq = irq_create_mapping(child_domain, dc->gpio);
		irq_set_parent(gpio_irq, dirconn_irq);
		irq_set_chip_data(gpio_irq, &(pctrl->chip));
		irq_set_chip_and_handler_name(gpio_irq, &msm_gpio_irq_chip, NULL, NULL);

		gpio_irq_data = irq_get_irq_data(gpio_irq);
		if (!gpio_irq_data)
			continue;

		irq_set_handler_data(dirconn_irq, gpio_irq_data);
		msm_dirconn_cfg_reg(gpio_irq_data, offset);
	}
}

static int msm_ps_hold_restart(struct notifier_block *nb, unsigned long action,
			       void *data)
{
	struct msm_pinctrl *pctrl = container_of(nb, struct msm_pinctrl, restart_nb);

	writel(0, pctrl->regs[0] + PS_HOLD_OFFSET);
	mdelay(1000);
	return NOTIFY_DONE;
}

static struct msm_pinctrl *poweroff_pctrl;

static void msm_ps_hold_poweroff(void)
{
	msm_ps_hold_restart(&poweroff_pctrl->restart_nb, 0, NULL);
}

static void msm_pinctrl_setup_pm_reset(struct msm_pinctrl *pctrl)
{
	int i;
	const struct msm_function *func = pctrl->soc->functions;

	for (i = 0; i < pctrl->soc->nfunctions; i++)
		if (!strcmp(func[i].name, "ps_hold")) {
			pctrl->restart_nb.notifier_call = msm_ps_hold_restart;
			pctrl->restart_nb.priority = 128;
			if (register_restart_handler(&pctrl->restart_nb))
				dev_err(pctrl->dev,
					"failed to setup restart handler.\n");
			poweroff_pctrl = pctrl;
			pm_power_off = msm_ps_hold_poweroff;
			break;
		}
}

#ifdef CONFIG_HIBERNATION
static int pinctrl_hibernation_notifier(struct notifier_block *nb,
								unsigned long event, void *dummy)
{
	struct msm_pinctrl *pctrl = msm_pinctrl_data;
	const struct msm_pinctrl_soc_data *soc = pctrl->soc;

	if (event == PM_HIBERNATION_PREPARE) {
		pctrl->gpio_regs = kcalloc(soc->ngroups,
			sizeof(*pctrl->gpio_regs), GFP_KERNEL);
		if (pctrl->gpio_regs == NULL)
			return -ENOMEM;
		if (soc->ntiles) {
			pctrl->msm_tile_regs = kcalloc(soc->ntiles,
				sizeof(*pctrl->msm_tile_regs), GFP_KERNEL);
			if (pctrl->msm_tile_regs == NULL) {
				kfree(pctrl->gpio_regs);
				return -ENOMEM;
			}
		}
		pctrl->hibernation = true;
	} else if (event == PM_POST_HIBERNATION) {
		kfree(pctrl->gpio_regs);
		kfree(pctrl->msm_tile_regs);
		pctrl->gpio_regs = NULL;
		pctrl->msm_tile_regs = NULL;
		pctrl->hibernation = false;
	}
	return NOTIFY_OK;
}

static struct notifier_block pinctrl_notif_block = {
	.notifier_call = pinctrl_hibernation_notifier,
};

static int msm_pinctrl_hibernation_suspend(void)
{
	const struct msm_pingroup *pgroup;
	struct msm_pinctrl *pctrl = msm_pinctrl_data;
	const struct msm_pinctrl_soc_data *soc = pctrl->soc;
	struct gpio_chip *chip = &pctrl->chip;
	void __iomem *tile_addr = NULL;
	u32 i, j;

	if (likely(!pctrl->hibernation))
		return 0;

    /* Save direction conn registers for hmss */

	for (i = 0; i < soc->ntiles; i++) {
		if (soc->tiles)
			tile_addr = pctrl->regs[i] + soc->dir_conn_addr[i];

		else
			tile_addr = pctrl->regs[0] + soc->dir_conn_addr[i];

		pr_err("The tile addr generated is 0x%lx\n", (u64)tile_addr);
		for (j = 0; j < 8; j++)
			pctrl->msm_tile_regs[i].dir_con_regs[j] =
				readl_relaxed(tile_addr + j*4);
	}

	/* All normal gpios will have common registers, first save them */
	for (i = 0; i < soc->ngpios; i++) {
		if (!test_bit(i, chip->valid_mask))
			continue;

		pgroup = &soc->groups[i];
		pctrl->gpio_regs[i].ctl_reg =
				msm_readl_ctl(pctrl, pgroup);
		pctrl->gpio_regs[i].io_reg =
				msm_readl_io(pctrl, pgroup);

		if (pgroup->intr_cfg_reg)
			pctrl->gpio_regs[i].intr_cfg_reg =
				msm_readl_intr_cfg(pctrl, pgroup);

		if (pgroup->intr_status_reg)
			pctrl->gpio_regs[i].intr_status_reg =
				msm_readl_intr_status(pctrl, pgroup);
	}

	for ( ; i < soc->ngroups; i++) {
		if (!test_bit(i, chip->valid_mask))
			continue;

		pgroup = &soc->groups[i];
		if (pgroup->ctl_reg)
			pctrl->gpio_regs[i].ctl_reg =
					msm_readl_ctl(pctrl, pgroup);
		if (pgroup->io_reg)
			pctrl->gpio_regs[i].io_reg =
					msm_readl_io(pctrl, pgroup);
	}
	return 0;
}

static void msm_pinctrl_hibernation_resume(void)
{
	u32 i, j;
	const struct msm_pingroup *pgroup;
	struct msm_pinctrl *pctrl = msm_pinctrl_data;
	const struct msm_pinctrl_soc_data *soc = pctrl->soc;
	struct gpio_chip *chip = &pctrl->chip;
	void __iomem *tile_addr = NULL;

	if (likely(!pctrl->hibernation) || !pctrl->gpio_regs)
		return;

	if (soc->ntiles && !pctrl->msm_tile_regs)
		return;

	for (i = 0; i < soc->ntiles; i++) {
		if (soc->tiles)
			tile_addr = pctrl->regs[i] + soc->dir_conn_addr[i];
		else
			tile_addr = pctrl->regs[0] + soc->dir_conn_addr[i];
		pr_err("The tile addr generated is 0x%lx\n", (u64)tile_addr);
		for (j = 0; j < 8; j++)
			writel_relaxed(pctrl->msm_tile_regs[i].dir_con_regs[j],
					tile_addr + j*4);
	}

    /* Restore normal gpios */
	for (i = 0; i < soc->ngpios; i++) {
		if (!test_bit(i, chip->valid_mask))
			continue;

		pgroup = &soc->groups[i];
		msm_writel_ctl(pctrl->gpio_regs[i].ctl_reg, pctrl, pgroup);
		msm_writel_io(pctrl->gpio_regs[i].io_reg, pctrl, pgroup);
		if (pgroup->intr_cfg_reg)
			msm_writel_intr_cfg(pctrl->gpio_regs[i].intr_cfg_reg,
				pctrl, pgroup);
		if (pgroup->intr_status_reg)
			msm_writel_intr_status(pctrl->gpio_regs[i].intr_status_reg,
					pctrl, pgroup);
	}

	for ( ; i < soc->ngroups; i++) {
		if (!test_bit(i, chip->valid_mask))
			continue;

		pgroup = &soc->groups[i];
		if (pgroup->ctl_reg)
			msm_writel_ctl(pctrl->gpio_regs[i].ctl_reg,
					pctrl, pgroup);
		if (pgroup->io_reg)
			msm_writel_io(pctrl->gpio_regs[i].io_reg,
					pctrl, pgroup);
	}
}

static struct syscore_ops msm_pinctrl_pm_ops = {
	.suspend = msm_pinctrl_hibernation_suspend,
	.resume = msm_pinctrl_hibernation_resume,
};
#endif

static __maybe_unused int msm_pinctrl_suspend(struct device *dev)
{
	struct msm_pinctrl *pctrl = dev_get_drvdata(dev);

	return pinctrl_force_sleep(pctrl->pctrl);
}

static __maybe_unused int msm_pinctrl_resume(struct device *dev)
{
	struct msm_pinctrl *pctrl = dev_get_drvdata(dev);

	return pinctrl_force_default(pctrl->pctrl);
}

SIMPLE_DEV_PM_OPS(msm_pinctrl_dev_pm_ops, msm_pinctrl_suspend,
		  msm_pinctrl_resume);

EXPORT_SYMBOL(msm_pinctrl_dev_pm_ops);

void debug_pintctrl_msm_enable(void)
{
	pinctrl_msm_log_mask = true;
}
EXPORT_SYMBOL(debug_pintctrl_msm_enable);

void debug_pintctrl_msm_disable(void)
{
	pinctrl_msm_log_mask = false;
}
EXPORT_SYMBOL(debug_pintctrl_msm_disable);

static __maybe_unused int noirq_msm_pinctrl_suspend(struct device *dev)
{
	struct msm_pinctrl *pctrl = dev_get_drvdata(dev);

	if (pinctrl_msm_log_mask) {
		printk_deferred("%s\n", pctrl->chip.label);
		msm_gpios_status(&pctrl->chip);
	}

	return 0;
}

const struct dev_pm_ops noirq_msm_pinctrl_dev_pm_ops = {
	.suspend_noirq = noirq_msm_pinctrl_suspend,
};
EXPORT_SYMBOL(noirq_msm_pinctrl_dev_pm_ops);

/*
 * msm_gpio_mpm_wake_set - API to make interrupt wakeup capable
 * @dev:        Device corrsponding to pinctrl
 * @gpio:       Gpio number to make interrupt wakeup capable
 * @enable:     Enable/Disable wakeup capability
 */
int msm_gpio_mpm_wake_set(unsigned int gpio, bool enable)
{
	const struct msm_pingroup *g;
	unsigned long flags;
	u32 val, intr_cfg;

	g = &msm_pinctrl_data->soc->groups[gpio];
	if (g->wake_bit == -1 && !g->intr_wakeup_present_bit)
		return -ENOENT;

	raw_spin_lock_irqsave(&msm_pinctrl_data->lock, flags);

	intr_cfg = msm_readl_intr_cfg(msm_pinctrl_data, g);
	if ((intr_cfg & BIT(g->intr_wakeup_present_bit)) &&
	     test_bit(gpio, msm_pinctrl_data->skip_wake_irqs)) {
		if (enable)
			intr_cfg |= BIT(g->intr_wakeup_enable_bit);
		else
			intr_cfg &= ~BIT(g->intr_wakeup_enable_bit);

		msm_writel_intr_cfg(intr_cfg, msm_pinctrl_data, g);

		goto exit;
	}

	val = readl_relaxed(msm_pinctrl_data->regs[g->tile] + g->wake_reg);
	if (enable)
		val |= BIT(g->wake_bit);
	else
		val &= ~BIT(g->wake_bit);

	writel_relaxed(val, msm_pinctrl_data->regs[g->tile] + g->wake_reg);

exit:
	raw_spin_unlock_irqrestore(&msm_pinctrl_data->lock, flags);

	return 0;
}
EXPORT_SYMBOL(msm_gpio_mpm_wake_set);

/*
 * msm_gpio_get_pin_address - API to get GPIO physical address range
 * @gpio_num:   global GPIO num, as returned from gpio apis - desc_to_gpio().
 * @res:        Out param. Resource struct with start/end addr populated.
 * @ret:        true if the pin is valid, false otherwise.
 */
bool msm_gpio_get_pin_address(unsigned int gpio_num, struct resource *res)
{
	struct gpio_chip *chip;
	const struct msm_pingroup *g;
	struct resource *tile_res;
	struct platform_device *pdev;
	unsigned int gpio;

	if (!msm_pinctrl_data) {
		pr_err("pinctrl data is not available\n");
		return false;
	}

	chip = &msm_pinctrl_data->chip;
	pdev = to_platform_device(msm_pinctrl_data->dev);
	gpio = gpio_num - chip->base;

	if (!gpiochip_line_is_valid(chip, gpio))
		return false;
	if (!res)
		return false;

	g = &msm_pinctrl_data->soc->groups[gpio];
	if (msm_pinctrl_data->soc->tiles)
		tile_res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					msm_pinctrl_data->soc->tiles[g->tile]);
	else
		tile_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!tile_res) {
		dev_err(&pdev->dev, "failed to get resource\n");
		return false;
	}

	res->start = tile_res->start + g->ctl_reg;
	res->end = res->start + (g->reg_size_4k ? : DEFAULT_REG_SIZE_4K) * SZ_4K - 1;

	return true;
}
EXPORT_SYMBOL(msm_gpio_get_pin_address);

int msm_qup_write(u32 mode, u32 val)
{
	int i;
	struct pinctrl_qup *regs = msm_pinctrl_data->soc->qup_regs;
	int num_regs =  msm_pinctrl_data->soc->nqup_regs;

	/*Iterate over modes*/
	for (i = 0; i < num_regs; i++) {
		if (regs[i].mode == mode) {
			writel_relaxed(val & QUP_MASK,
				 msm_pinctrl_data->regs[0] + regs[i].offset);
			return 0;
		}
	}

	return -ENOENT;
}
EXPORT_SYMBOL(msm_qup_write);

int msm_qup_read(unsigned int mode)
{
	int i, val;
	struct pinctrl_qup *regs = msm_pinctrl_data->soc->qup_regs;
	int num_regs =  msm_pinctrl_data->soc->nqup_regs;

	/*Iterate over modes*/
	for (i = 0; i < num_regs; i++) {
		if (regs[i].mode == mode) {
			val = readl_relaxed(msm_pinctrl_data->regs[0] +
								regs[i].offset);
			return val & QUP_MASK;
		}
	}

	return -ENOENT;
}
EXPORT_SYMBOL(msm_qup_read);

int msm_spare_write(int spare_reg, u32 val)
{
	u32 offset;
	const struct msm_spare_tlmm *regs = msm_pinctrl_data->soc->spare_regs;
	int num_regs =  msm_pinctrl_data->soc->nspare_regs;

	if (!regs || spare_reg >= num_regs)
		return -ENOENT;

	offset = regs[spare_reg].offset;
	if (offset != 0) {
		writel_relaxed(val & SPARE_MASK,
				msm_pinctrl_data->regs[0] + offset);
		return 0;
	}

	return -ENOENT;
}
EXPORT_SYMBOL(msm_spare_write);

int msm_spare_read(int spare_reg)
{
	u32 offset, val;
	const struct msm_spare_tlmm *regs = msm_pinctrl_data->soc->spare_regs;
	int num_regs =  msm_pinctrl_data->soc->nspare_regs;

	if (!regs || spare_reg >= num_regs)
		return -ENOENT;

	offset = regs[spare_reg].offset;
	if (offset != 0) {
		val = readl_relaxed(msm_pinctrl_data->regs[0] + offset);
		return val & SPARE_MASK;
	}

	return -ENOENT;
}
EXPORT_SYMBOL(msm_spare_read);

int msm_pinctrl_probe(struct platform_device *pdev,
		      const struct msm_pinctrl_soc_data *soc_data)
{
	struct msm_pinctrl *pctrl;
	struct resource *res;
	int ret, i, num_irq, irq;

	msm_pinctrl_data = pctrl = devm_kzalloc(&pdev->dev, sizeof(*pctrl), GFP_KERNEL);
	if (!pctrl)
		return -ENOMEM;

	pctrl->hibernation = false;
	pctrl->dev = &pdev->dev;
	pctrl->soc = soc_data;
	pctrl->chip = msm_gpio_template;
	pctrl->intr_target_use_scm = of_device_is_compatible(
					pctrl->dev->of_node,
					"qcom,ipq8064-pinctrl");

	raw_spin_lock_init(&pctrl->lock);

	if (soc_data->tiles) {
		for (i = 0; i < soc_data->ntiles; i++) {
			res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
							   soc_data->tiles[i]);
			pctrl->regs[i] = devm_ioremap_resource(&pdev->dev, res);
			if (IS_ERR(pctrl->regs[i]))
				return PTR_ERR(pctrl->regs[i]);
		}
	} else {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		if (!res)
			return -ENOENT;

		pctrl->regs[0] = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(pctrl->regs[0]))
			return PTR_ERR(pctrl->regs[0]);

		pctrl->phys_base[0] = res->start;
	}

	msm_pinctrl_setup_pm_reset(pctrl);

	pctrl->irq = platform_get_irq(pdev, 0);
	if (pctrl->irq < 0)
		return pctrl->irq;

	pctrl->desc.owner = THIS_MODULE;
	pctrl->desc.pctlops = &msm_pinctrl_ops;
	pctrl->desc.pmxops = &msm_pinmux_ops;
	pctrl->desc.confops = &msm_pinconf_ops;
	pctrl->desc.name = dev_name(&pdev->dev);
	pctrl->desc.pins = pctrl->soc->pins;
	pctrl->desc.npins = pctrl->soc->npins;
	pctrl->desc.num_custom_params = ARRAY_SIZE(msm_gpio_bindings);
	pctrl->desc.custom_params = msm_gpio_bindings;

	pctrl->pctrl = devm_pinctrl_register(&pdev->dev, &pctrl->desc, pctrl);
	if (IS_ERR(pctrl->pctrl)) {
		dev_err(&pdev->dev, "Couldn't register pinctrl driver\n");
		return PTR_ERR(pctrl->pctrl);
	}

	ret = msm_gpio_init(pctrl);
	if (ret)
		return ret;

	num_irq = platform_irq_count(pdev);

	for (i = 1; i < num_irq; i++) {
		struct msm_dir_conn *dc = &soc_data->dir_conn[i];

		irq = platform_get_irq(pdev, i);
		dc->irq = irq;
		__irq_set_handler(irq, msm_gpio_dirconn_handler, false, NULL);
		irq_set_irq_type(irq, IRQ_TYPE_EDGE_RISING);
		pctrl->n_dir_conns++;
	}

	msm_gpio_setup_dir_connects(pctrl);

	pinctrl_msm_log_mask = false;

	platform_set_drvdata(pdev, pctrl);

#ifdef CONFIG_HIBERNATION
	register_syscore_ops(&msm_pinctrl_pm_ops);
	ret = register_pm_notifier(&pinctrl_notif_block);
	if (ret)
		return ret;
#endif

	dev_dbg(&pdev->dev, "Probed Qualcomm pinctrl driver\n");

	return 0;
}
EXPORT_SYMBOL(msm_pinctrl_probe);

int msm_pinctrl_remove(struct platform_device *pdev)
{
	struct msm_pinctrl *pctrl = platform_get_drvdata(pdev);

	gpiochip_remove(&pctrl->chip);

	unregister_restart_handler(&pctrl->restart_nb);

	return 0;
}
EXPORT_SYMBOL(msm_pinctrl_remove);

MODULE_SOFTDEP("pre: qcom-pdc");
MODULE_DESCRIPTION("Qualcomm Technologies, Inc. pinctrl-msm driver");
MODULE_LICENSE("GPL v2");
