// SPDX-License-Identifier: GPL-2.0
/*
 * Intel pinctrl/GPIO core driver.
 *
 * Copyright (C) 2015, Intel Corporation
 * Authors: Mathias Nyman <mathias.nyman@linux.intel.com>
 *          Mika Westerberg <mika.westerberg@linux.intel.com>
 */

#include <linux/acpi.h>
#include <linux/cleanup.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/log2.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/seq_file.h>
#include <linux/string_helpers.h>
#include <linux/time.h>

#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include <linux/platform_data/x86/pwm-lpss.h>

#include "../core.h"
#include "pinctrl-intel.h"

/* Offset from regs */
#define REVID				0x000
#define REVID_SHIFT			16
#define REVID_MASK			GENMASK(31, 16)

#define CAPLIST				0x004
#define CAPLIST_ID_SHIFT		16
#define CAPLIST_ID_MASK			GENMASK(23, 16)
#define CAPLIST_ID_GPIO_HW_INFO		1
#define CAPLIST_ID_PWM			2
#define CAPLIST_ID_BLINK		3
#define CAPLIST_ID_EXP			4
#define CAPLIST_NEXT_SHIFT		0
#define CAPLIST_NEXT_MASK		GENMASK(15, 0)

#define PADBAR				0x00c

#define PADOWN_BITS			4
#define PADOWN_SHIFT(p)			((p) % 8 * PADOWN_BITS)
#define PADOWN_MASK(p)			(GENMASK(3, 0) << PADOWN_SHIFT(p))
#define PADOWN_GPP(p)			((p) / 8)

#define PWMC				0x204

/* Offset from pad_regs */
#define PADCFG0				0x000
#define PADCFG0_RXEVCFG_MASK		GENMASK(26, 25)
#define PADCFG0_RXEVCFG_LEVEL		(0 << 25)
#define PADCFG0_RXEVCFG_EDGE		(1 << 25)
#define PADCFG0_RXEVCFG_DISABLED	(2 << 25)
#define PADCFG0_RXEVCFG_EDGE_BOTH	(3 << 25)
#define PADCFG0_PREGFRXSEL		BIT(24)
#define PADCFG0_RXINV			BIT(23)
#define PADCFG0_GPIROUTIOXAPIC		BIT(20)
#define PADCFG0_GPIROUTSCI		BIT(19)
#define PADCFG0_GPIROUTSMI		BIT(18)
#define PADCFG0_GPIROUTNMI		BIT(17)
#define PADCFG0_PMODE_SHIFT		10
#define PADCFG0_PMODE_MASK		GENMASK(13, 10)
#define PADCFG0_PMODE_GPIO		0
#define PADCFG0_GPIODIS_SHIFT		8
#define PADCFG0_GPIODIS_MASK		GENMASK(9, 8)
#define PADCFG0_GPIODIS_NONE		0
#define PADCFG0_GPIODIS_OUTPUT		1
#define PADCFG0_GPIODIS_INPUT		2
#define PADCFG0_GPIODIS_FULL		3
#define PADCFG0_GPIORXDIS		BIT(9)
#define PADCFG0_GPIOTXDIS		BIT(8)
#define PADCFG0_GPIORXSTATE		BIT(1)
#define PADCFG0_GPIOTXSTATE		BIT(0)

#define PADCFG1				0x004
#define PADCFG1_TERM_UP			BIT(13)
#define PADCFG1_TERM_SHIFT		10
#define PADCFG1_TERM_MASK		GENMASK(12, 10)
/*
 * Bit 0  Bit 1  Bit 2			Value, Ohms
 *
 *   0      0      0			-
 *   0      0      1			20000
 *   0      1      0			5000
 *   0      1      1			~4000
 *   1      0      0			1000 (if supported)
 *   1      0      1			~952 (if supported)
 *   1      1      0			~833 (if supported)
 *   1      1      1			~800 (if supported)
 */
#define PADCFG1_TERM_20K		BIT(2)
#define PADCFG1_TERM_5K			BIT(1)
#define PADCFG1_TERM_4K			(BIT(2) | BIT(1))
#define PADCFG1_TERM_1K			BIT(0)
#define PADCFG1_TERM_952		(BIT(2) | BIT(0))
#define PADCFG1_TERM_833		(BIT(1) | BIT(0))
#define PADCFG1_TERM_800		(BIT(2) | BIT(1) | BIT(0))

#define PADCFG2				0x008
#define PADCFG2_DEBOUNCE_SHIFT		1
#define PADCFG2_DEBOUNCE_MASK		GENMASK(4, 1)
#define PADCFG2_DEBEN			BIT(0)

#define DEBOUNCE_PERIOD_NSEC		31250

struct intel_pad_context {
	u32 padcfg0;
	u32 padcfg1;
	u32 padcfg2;
};

struct intel_community_context {
	u32 *intmask;
	u32 *hostown;
};

#define pin_to_padno(c, p)	((p) - (c)->pin_base)
#define padgroup_offset(g, p)	((p) - (g)->base)

#define for_each_intel_pin_community(pctrl, community)					\
	for (unsigned int __ci = 0;							\
	     __ci < pctrl->ncommunities && (community = &pctrl->communities[__ci]);	\
	     __ci++)									\

#define for_each_intel_community_pad_group(community, grp)			\
	for (unsigned int __gi = 0;						\
	     __gi < community->ngpps && (grp = &community->gpps[__gi]);		\
	     __gi++)								\

#define for_each_intel_pad_group(pctrl, community, grp)			\
	for_each_intel_pin_community(pctrl, community)			\
		for_each_intel_community_pad_group(community, grp)

#define for_each_intel_gpio_group(pctrl, community, grp)		\
	for_each_intel_pad_group(pctrl, community, grp)			\
		if (grp->gpio_base == INTEL_GPIO_BASE_NOMAP) {} else

const struct intel_community *intel_get_community(const struct intel_pinctrl *pctrl,
						  unsigned int pin)
{
	const struct intel_community *community;

	for_each_intel_pin_community(pctrl, community) {
		if (pin >= community->pin_base &&
		    pin < community->pin_base + community->npins)
			return community;
	}

	dev_warn(pctrl->dev, "failed to find community for pin %u\n", pin);
	return NULL;
}
EXPORT_SYMBOL_NS_GPL(intel_get_community, "PINCTRL_INTEL");

static const struct intel_padgroup *
intel_community_get_padgroup(const struct intel_community *community,
			     unsigned int pin)
{
	const struct intel_padgroup *padgrp;

	for_each_intel_community_pad_group(community, padgrp) {
		if (pin >= padgrp->base && pin < padgrp->base + padgrp->size)
			return padgrp;
	}

	return NULL;
}

static void __iomem *intel_get_padcfg(struct intel_pinctrl *pctrl,
				      unsigned int pin, unsigned int reg)
{
	const struct intel_community *community;
	unsigned int padno;
	size_t nregs;

	community = intel_get_community(pctrl, pin);
	if (!community)
		return NULL;

	padno = pin_to_padno(community, pin);
	nregs = (community->features & PINCTRL_FEATURE_DEBOUNCE) ? 4 : 2;

	if (reg >= nregs * 4)
		return NULL;

	return community->pad_regs + reg + padno * nregs * 4;
}

static bool intel_pad_owned_by_host(const struct intel_pinctrl *pctrl, unsigned int pin)
{
	const struct intel_community *community;
	const struct intel_padgroup *padgrp;
	unsigned int gpp, offset, gpp_offset;
	void __iomem *padown;

	community = intel_get_community(pctrl, pin);
	if (!community)
		return false;
	if (!community->padown_offset)
		return true;

	padgrp = intel_community_get_padgroup(community, pin);
	if (!padgrp)
		return false;

	gpp_offset = padgroup_offset(padgrp, pin);
	gpp = PADOWN_GPP(gpp_offset);
	offset = community->padown_offset + padgrp->padown_num * 4 + gpp * 4;
	padown = community->regs + offset;

	return !(readl(padown) & PADOWN_MASK(gpp_offset));
}

static bool intel_pad_acpi_mode(const struct intel_pinctrl *pctrl, unsigned int pin)
{
	const struct intel_community *community;
	const struct intel_padgroup *padgrp;
	unsigned int offset, gpp_offset;
	void __iomem *hostown;

	community = intel_get_community(pctrl, pin);
	if (!community)
		return true;
	if (!community->hostown_offset)
		return false;

	padgrp = intel_community_get_padgroup(community, pin);
	if (!padgrp)
		return true;

	gpp_offset = padgroup_offset(padgrp, pin);
	offset = community->hostown_offset + padgrp->reg_num * 4;
	hostown = community->regs + offset;

	return !(readl(hostown) & BIT(gpp_offset));
}

/**
 * enum - Locking variants of the pad configuration
 * @PAD_UNLOCKED:	pad is fully controlled by the configuration registers
 * @PAD_LOCKED:		pad configuration registers, except TX state, are locked
 * @PAD_LOCKED_TX:	pad configuration TX state is locked
 * @PAD_LOCKED_FULL:	pad configuration registers are locked completely
 *
 * Locking is considered as read-only mode for corresponding registers and
 * their respective fields. That said, TX state bit is locked separately from
 * the main locking scheme.
 */
enum {
	PAD_UNLOCKED	= 0,
	PAD_LOCKED	= 1,
	PAD_LOCKED_TX	= 2,
	PAD_LOCKED_FULL	= PAD_LOCKED | PAD_LOCKED_TX,
};

static int intel_pad_locked(const struct intel_pinctrl *pctrl, unsigned int pin)
{
	const struct intel_community *community;
	const struct intel_padgroup *padgrp;
	unsigned int offset, gpp_offset;
	u32 value;
	int ret = PAD_UNLOCKED;

	community = intel_get_community(pctrl, pin);
	if (!community)
		return PAD_LOCKED_FULL;
	if (!community->padcfglock_offset)
		return PAD_UNLOCKED;

	padgrp = intel_community_get_padgroup(community, pin);
	if (!padgrp)
		return PAD_LOCKED_FULL;

	gpp_offset = padgroup_offset(padgrp, pin);

	/*
	 * If PADCFGLOCK and PADCFGLOCKTX bits are both clear for this pad,
	 * the pad is considered unlocked. Any other case means that it is
	 * either fully or partially locked.
	 */
	offset = community->padcfglock_offset + 0 + padgrp->reg_num * 8;
	value = readl(community->regs + offset);
	if (value & BIT(gpp_offset))
		ret |= PAD_LOCKED;

	offset = community->padcfglock_offset + 4 + padgrp->reg_num * 8;
	value = readl(community->regs + offset);
	if (value & BIT(gpp_offset))
		ret |= PAD_LOCKED_TX;

	return ret;
}

static bool intel_pad_is_unlocked(const struct intel_pinctrl *pctrl, unsigned int pin)
{
	return (intel_pad_locked(pctrl, pin) & PAD_LOCKED) == PAD_UNLOCKED;
}

static bool intel_pad_usable(const struct intel_pinctrl *pctrl, unsigned int pin)
{
	return intel_pad_owned_by_host(pctrl, pin) && intel_pad_is_unlocked(pctrl, pin);
}

int intel_get_groups_count(struct pinctrl_dev *pctldev)
{
	const struct intel_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	return pctrl->soc->ngroups;
}
EXPORT_SYMBOL_NS_GPL(intel_get_groups_count, "PINCTRL_INTEL");

const char *intel_get_group_name(struct pinctrl_dev *pctldev, unsigned int group)
{
	const struct intel_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	return pctrl->soc->groups[group].grp.name;
}
EXPORT_SYMBOL_NS_GPL(intel_get_group_name, "PINCTRL_INTEL");

int intel_get_group_pins(struct pinctrl_dev *pctldev, unsigned int group,
			 const unsigned int **pins, unsigned int *npins)
{
	const struct intel_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	*pins = pctrl->soc->groups[group].grp.pins;
	*npins = pctrl->soc->groups[group].grp.npins;
	return 0;
}
EXPORT_SYMBOL_NS_GPL(intel_get_group_pins, "PINCTRL_INTEL");

static void intel_pin_dbg_show(struct pinctrl_dev *pctldev, struct seq_file *s,
			       unsigned int pin)
{
	struct intel_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	void __iomem *padcfg;
	u32 cfg0, cfg1, mode;
	int locked;
	bool acpi;

	if (!intel_pad_owned_by_host(pctrl, pin)) {
		seq_puts(s, "not available");
		return;
	}

	cfg0 = readl(intel_get_padcfg(pctrl, pin, PADCFG0));
	cfg1 = readl(intel_get_padcfg(pctrl, pin, PADCFG1));

	mode = (cfg0 & PADCFG0_PMODE_MASK) >> PADCFG0_PMODE_SHIFT;
	if (mode == PADCFG0_PMODE_GPIO)
		seq_puts(s, "GPIO ");
	else
		seq_printf(s, "mode %d ", mode);

	seq_printf(s, "0x%08x 0x%08x", cfg0, cfg1);

	/* Dump the additional PADCFG registers if available */
	padcfg = intel_get_padcfg(pctrl, pin, PADCFG2);
	if (padcfg)
		seq_printf(s, " 0x%08x", readl(padcfg));

	locked = intel_pad_locked(pctrl, pin);
	acpi = intel_pad_acpi_mode(pctrl, pin);

	if (locked || acpi) {
		seq_puts(s, " [");
		if (locked)
			seq_puts(s, "LOCKED");
		if ((locked & PAD_LOCKED_FULL) == PAD_LOCKED_TX)
			seq_puts(s, " tx");
		else if ((locked & PAD_LOCKED_FULL) == PAD_LOCKED_FULL)
			seq_puts(s, " full");

		if (locked && acpi)
			seq_puts(s, ", ");

		if (acpi)
			seq_puts(s, "ACPI");
		seq_puts(s, "]");
	}
}

static const struct pinctrl_ops intel_pinctrl_ops = {
	.get_groups_count = intel_get_groups_count,
	.get_group_name = intel_get_group_name,
	.get_group_pins = intel_get_group_pins,
	.pin_dbg_show = intel_pin_dbg_show,
};

int intel_get_functions_count(struct pinctrl_dev *pctldev)
{
	const struct intel_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	return pctrl->soc->nfunctions;
}
EXPORT_SYMBOL_NS_GPL(intel_get_functions_count, "PINCTRL_INTEL");

const char *intel_get_function_name(struct pinctrl_dev *pctldev, unsigned int function)
{
	const struct intel_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	return pctrl->soc->functions[function].func.name;
}
EXPORT_SYMBOL_NS_GPL(intel_get_function_name, "PINCTRL_INTEL");

int intel_get_function_groups(struct pinctrl_dev *pctldev, unsigned int function,
			      const char * const **groups, unsigned int * const ngroups)
{
	const struct intel_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	*groups = pctrl->soc->functions[function].func.groups;
	*ngroups = pctrl->soc->functions[function].func.ngroups;
	return 0;
}
EXPORT_SYMBOL_NS_GPL(intel_get_function_groups, "PINCTRL_INTEL");

static int intel_pinmux_set_mux(struct pinctrl_dev *pctldev,
				unsigned int function, unsigned int group)
{
	struct intel_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	const struct intel_pingroup *grp = &pctrl->soc->groups[group];
	int i;

	guard(raw_spinlock_irqsave)(&pctrl->lock);

	/*
	 * All pins in the groups needs to be accessible and writable
	 * before we can enable the mux for this group.
	 */
	for (i = 0; i < grp->grp.npins; i++) {
		if (!intel_pad_usable(pctrl, grp->grp.pins[i]))
			return -EBUSY;
	}

	/* Now enable the mux setting for each pin in the group */
	for (i = 0; i < grp->grp.npins; i++) {
		void __iomem *padcfg0;
		u32 value, pmode;

		padcfg0 = intel_get_padcfg(pctrl, grp->grp.pins[i], PADCFG0);

		value = readl(padcfg0);
		value &= ~PADCFG0_PMODE_MASK;

		if (grp->modes)
			pmode = grp->modes[i];
		else
			pmode = grp->mode;

		value |= pmode << PADCFG0_PMODE_SHIFT;
		writel(value, padcfg0);
	}

	return 0;
}

/**
 * enum - Possible pad physical connections
 * @PAD_CONNECT_NONE:	pad is fully disconnected
 * @PAD_CONNECT_INPUT:	pad is in input only mode
 * @PAD_CONNECT_OUTPUT:	pad is in output only mode
 * @PAD_CONNECT_FULL:	pad is fully connected
 */
enum {
	PAD_CONNECT_NONE	= 0,
	PAD_CONNECT_INPUT	= 1,
	PAD_CONNECT_OUTPUT	= 2,
	PAD_CONNECT_FULL	= PAD_CONNECT_INPUT | PAD_CONNECT_OUTPUT,
};

static int __intel_gpio_get_direction(u32 value)
{
	switch ((value & PADCFG0_GPIODIS_MASK) >> PADCFG0_GPIODIS_SHIFT) {
	case PADCFG0_GPIODIS_FULL:
		return PAD_CONNECT_NONE;
	case PADCFG0_GPIODIS_OUTPUT:
		return PAD_CONNECT_INPUT;
	case PADCFG0_GPIODIS_INPUT:
		return PAD_CONNECT_OUTPUT;
	case PADCFG0_GPIODIS_NONE:
		return PAD_CONNECT_FULL;
	default:
		return -ENOTSUPP;
	};
}

static u32 __intel_gpio_set_direction(u32 value, bool input, bool output)
{
	if (input)
		value &= ~PADCFG0_GPIORXDIS;
	else
		value |= PADCFG0_GPIORXDIS;

	if (output)
		value &= ~PADCFG0_GPIOTXDIS;
	else
		value |= PADCFG0_GPIOTXDIS;

	return value;
}

static int __intel_gpio_get_gpio_mode(u32 value)
{
	return (value & PADCFG0_PMODE_MASK) >> PADCFG0_PMODE_SHIFT;
}

static int intel_gpio_get_gpio_mode(void __iomem *padcfg0)
{
	return __intel_gpio_get_gpio_mode(readl(padcfg0));
}

static void intel_gpio_set_gpio_mode(void __iomem *padcfg0)
{
	u32 value;

	value = readl(padcfg0);

	/* Put the pad into GPIO mode */
	value &= ~PADCFG0_PMODE_MASK;
	value |= PADCFG0_PMODE_GPIO;

	/* Disable TX buffer and enable RX (this will be input) */
	value = __intel_gpio_set_direction(value, true, false);

	/* Disable SCI/SMI/NMI generation */
	value &= ~(PADCFG0_GPIROUTIOXAPIC | PADCFG0_GPIROUTSCI);
	value &= ~(PADCFG0_GPIROUTSMI | PADCFG0_GPIROUTNMI);

	writel(value, padcfg0);
}

static int intel_gpio_request_enable(struct pinctrl_dev *pctldev,
				     struct pinctrl_gpio_range *range,
				     unsigned int pin)
{
	struct intel_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	void __iomem *padcfg0;

	padcfg0 = intel_get_padcfg(pctrl, pin, PADCFG0);

	guard(raw_spinlock_irqsave)(&pctrl->lock);

	if (!intel_pad_owned_by_host(pctrl, pin))
		return -EBUSY;

	if (!intel_pad_is_unlocked(pctrl, pin))
		return 0;

	/*
	 * If pin is already configured in GPIO mode, we assume that
	 * firmware provides correct settings. In such case we avoid
	 * potential glitches on the pin. Otherwise, for the pin in
	 * alternative mode, consumer has to supply respective flags.
	 */
	if (intel_gpio_get_gpio_mode(padcfg0) == PADCFG0_PMODE_GPIO)
		return 0;

	intel_gpio_set_gpio_mode(padcfg0);

	return 0;
}

static int intel_gpio_set_direction(struct pinctrl_dev *pctldev,
				    struct pinctrl_gpio_range *range,
				    unsigned int pin, bool input)
{
	struct intel_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	void __iomem *padcfg0;
	u32 value;

	padcfg0 = intel_get_padcfg(pctrl, pin, PADCFG0);

	guard(raw_spinlock_irqsave)(&pctrl->lock);

	value = readl(padcfg0);
	if (input)
		value = __intel_gpio_set_direction(value, true, false);
	else
		value = __intel_gpio_set_direction(value, false, true);
	writel(value, padcfg0);

	return 0;
}

static const struct pinmux_ops intel_pinmux_ops = {
	.get_functions_count = intel_get_functions_count,
	.get_function_name = intel_get_function_name,
	.get_function_groups = intel_get_function_groups,
	.set_mux = intel_pinmux_set_mux,
	.gpio_request_enable = intel_gpio_request_enable,
	.gpio_set_direction = intel_gpio_set_direction,
};

static int intel_config_get_pull(struct intel_pinctrl *pctrl, unsigned int pin,
				 enum pin_config_param param, u32 *arg)
{
	void __iomem *padcfg1;
	u32 value, term;

	padcfg1 = intel_get_padcfg(pctrl, pin, PADCFG1);

	scoped_guard(raw_spinlock_irqsave, &pctrl->lock)
		value = readl(padcfg1);

	term = (value & PADCFG1_TERM_MASK) >> PADCFG1_TERM_SHIFT;

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		if (term)
			return -EINVAL;
		break;

	case PIN_CONFIG_BIAS_PULL_UP:
		if (!term || !(value & PADCFG1_TERM_UP))
			return -EINVAL;

		switch (term) {
		case PADCFG1_TERM_833:
			*arg = 833;
			break;
		case PADCFG1_TERM_1K:
			*arg = 1000;
			break;
		case PADCFG1_TERM_4K:
			*arg = 4000;
			break;
		case PADCFG1_TERM_5K:
			*arg = 5000;
			break;
		case PADCFG1_TERM_20K:
			*arg = 20000;
			break;
		}

		break;

	case PIN_CONFIG_BIAS_PULL_DOWN: {
		const struct intel_community *community = intel_get_community(pctrl, pin);

		if (!term || value & PADCFG1_TERM_UP)
			return -EINVAL;

		switch (term) {
		case PADCFG1_TERM_833:
			if (!(community->features & PINCTRL_FEATURE_1K_PD))
				return -EINVAL;
			*arg = 833;
			break;
		case PADCFG1_TERM_1K:
			if (!(community->features & PINCTRL_FEATURE_1K_PD))
				return -EINVAL;
			*arg = 1000;
			break;
		case PADCFG1_TERM_4K:
			*arg = 4000;
			break;
		case PADCFG1_TERM_5K:
			*arg = 5000;
			break;
		case PADCFG1_TERM_20K:
			*arg = 20000;
			break;
		}

		break;
	}

	default:
		return -EINVAL;
	}

	return 0;
}

static int intel_config_get_high_impedance(struct intel_pinctrl *pctrl, unsigned int pin,
					   enum pin_config_param param, u32 *arg)
{
	void __iomem *padcfg0;
	u32 value;

	padcfg0 = intel_get_padcfg(pctrl, pin, PADCFG0);

	scoped_guard(raw_spinlock_irqsave, &pctrl->lock)
		value = readl(padcfg0);

	if (__intel_gpio_get_direction(value) != PAD_CONNECT_NONE)
		return -EINVAL;

	return 0;
}

static int intel_config_get_debounce(struct intel_pinctrl *pctrl, unsigned int pin,
				     enum pin_config_param param, u32 *arg)
{
	void __iomem *padcfg2;
	unsigned long v;
	u32 value2;

	padcfg2 = intel_get_padcfg(pctrl, pin, PADCFG2);
	if (!padcfg2)
		return -ENOTSUPP;

	scoped_guard(raw_spinlock_irqsave, &pctrl->lock)
		value2 = readl(padcfg2);

	if (!(value2 & PADCFG2_DEBEN))
		return -EINVAL;

	v = (value2 & PADCFG2_DEBOUNCE_MASK) >> PADCFG2_DEBOUNCE_SHIFT;
	*arg = BIT(v) * DEBOUNCE_PERIOD_NSEC / NSEC_PER_USEC;

	return 0;
}

static int intel_config_get(struct pinctrl_dev *pctldev, unsigned int pin,
			    unsigned long *config)
{
	struct intel_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param = pinconf_to_config_param(*config);
	u32 arg = 0;
	int ret;

	if (!intel_pad_owned_by_host(pctrl, pin))
		return -ENOTSUPP;

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
	case PIN_CONFIG_BIAS_PULL_UP:
	case PIN_CONFIG_BIAS_PULL_DOWN:
		ret = intel_config_get_pull(pctrl, pin, param, &arg);
		if (ret)
			return ret;
		break;

	case PIN_CONFIG_BIAS_HIGH_IMPEDANCE:
		ret = intel_config_get_high_impedance(pctrl, pin, param, &arg);
		if (ret)
			return ret;
		break;

	case PIN_CONFIG_INPUT_DEBOUNCE:
		ret = intel_config_get_debounce(pctrl, pin, param, &arg);
		if (ret)
			return ret;
		break;

	default:
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);
	return 0;
}

static int intel_config_set_pull(struct intel_pinctrl *pctrl, unsigned int pin,
				 unsigned long config)
{
	unsigned int param = pinconf_to_config_param(config);
	unsigned int arg = pinconf_to_config_argument(config);
	u32 term = 0, up = 0, value;
	void __iomem *padcfg1;

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		break;

	case PIN_CONFIG_BIAS_PULL_UP:
		switch (arg) {
		case 20000:
			term = PADCFG1_TERM_20K;
			break;
		case 1: /* Set default strength value in case none is given */
		case 5000:
			term = PADCFG1_TERM_5K;
			break;
		case 4000:
			term = PADCFG1_TERM_4K;
			break;
		case 1000:
			term = PADCFG1_TERM_1K;
			break;
		case 833:
			term = PADCFG1_TERM_833;
			break;
		default:
			return -EINVAL;
		}

		up = PADCFG1_TERM_UP;
		break;

	case PIN_CONFIG_BIAS_PULL_DOWN: {
		const struct intel_community *community = intel_get_community(pctrl, pin);

		switch (arg) {
		case 20000:
			term = PADCFG1_TERM_20K;
			break;
		case 1: /* Set default strength value in case none is given */
		case 5000:
			term = PADCFG1_TERM_5K;
			break;
		case 4000:
			term = PADCFG1_TERM_4K;
			break;
		case 1000:
			if (!(community->features & PINCTRL_FEATURE_1K_PD))
				return -EINVAL;
			term = PADCFG1_TERM_1K;
			break;
		case 833:
			if (!(community->features & PINCTRL_FEATURE_1K_PD))
				return -EINVAL;
			term = PADCFG1_TERM_833;
			break;
		default:
			return -EINVAL;
		}

		break;
	}

	default:
		return -EINVAL;
	}

	padcfg1 = intel_get_padcfg(pctrl, pin, PADCFG1);

	guard(raw_spinlock_irqsave)(&pctrl->lock);

	value = readl(padcfg1);
	value = (value & ~PADCFG1_TERM_MASK) | (term << PADCFG1_TERM_SHIFT);
	value = (value & ~PADCFG1_TERM_UP) | up;
	writel(value, padcfg1);

	return 0;
}

static void intel_gpio_set_high_impedance(struct intel_pinctrl *pctrl, unsigned int pin)
{
	void __iomem *padcfg0;
	u32 value;

	padcfg0 = intel_get_padcfg(pctrl, pin, PADCFG0);

	guard(raw_spinlock_irqsave)(&pctrl->lock);

	value = readl(padcfg0);
	value = __intel_gpio_set_direction(value, false, false);
	writel(value, padcfg0);
}

static int intel_config_set_debounce(struct intel_pinctrl *pctrl,
				     unsigned int pin, unsigned int debounce)
{
	void __iomem *padcfg0, *padcfg2;
	u32 value0, value2;
	unsigned long v;

	if (debounce) {
		v = order_base_2(debounce * NSEC_PER_USEC / DEBOUNCE_PERIOD_NSEC);
		if (v < 3 || v > 15)
			return -EINVAL;
	} else {
		v = 0;
	}

	padcfg2 = intel_get_padcfg(pctrl, pin, PADCFG2);
	if (!padcfg2)
		return -ENOTSUPP;

	padcfg0 = intel_get_padcfg(pctrl, pin, PADCFG0);

	guard(raw_spinlock_irqsave)(&pctrl->lock);

	value0 = readl(padcfg0);
	value2 = readl(padcfg2);

	value2 = (value2 & ~PADCFG2_DEBOUNCE_MASK) | (v << PADCFG2_DEBOUNCE_SHIFT);
	if (v) {
		/* Enable glitch filter and debouncer */
		value0 |= PADCFG0_PREGFRXSEL;
		value2 |= PADCFG2_DEBEN;
	} else {
		/* Disable glitch filter and debouncer */
		value0 &= ~PADCFG0_PREGFRXSEL;
		value2 &= ~PADCFG2_DEBEN;
	}

	writel(value0, padcfg0);
	writel(value2, padcfg2);

	return 0;
}

static int intel_config_set(struct pinctrl_dev *pctldev, unsigned int pin,
			  unsigned long *configs, unsigned int nconfigs)
{
	struct intel_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	int i, ret;

	if (!intel_pad_usable(pctrl, pin))
		return -ENOTSUPP;

	for (i = 0; i < nconfigs; i++) {
		switch (pinconf_to_config_param(configs[i])) {
		case PIN_CONFIG_BIAS_DISABLE:
		case PIN_CONFIG_BIAS_PULL_UP:
		case PIN_CONFIG_BIAS_PULL_DOWN:
			ret = intel_config_set_pull(pctrl, pin, configs[i]);
			if (ret)
				return ret;
			break;

		case PIN_CONFIG_BIAS_HIGH_IMPEDANCE:
			intel_gpio_set_high_impedance(pctrl, pin);
			break;

		case PIN_CONFIG_INPUT_DEBOUNCE:
			ret = intel_config_set_debounce(pctrl, pin,
				pinconf_to_config_argument(configs[i]));
			if (ret)
				return ret;
			break;

		default:
			return -ENOTSUPP;
		}
	}

	return 0;
}

static const struct pinconf_ops intel_pinconf_ops = {
	.is_generic = true,
	.pin_config_get = intel_config_get,
	.pin_config_set = intel_config_set,
};

static const struct pinctrl_desc intel_pinctrl_desc = {
	.pctlops = &intel_pinctrl_ops,
	.pmxops = &intel_pinmux_ops,
	.confops = &intel_pinconf_ops,
	.owner = THIS_MODULE,
};

/**
 * intel_gpio_to_pin() - Translate from GPIO offset to pin number
 * @pctrl: Pinctrl structure
 * @offset: GPIO offset from gpiolib
 * @community: Community is filled here if not %NULL
 * @padgrp: Pad group is filled here if not %NULL
 *
 * When coming through gpiolib irqchip, the GPIO offset is not
 * automatically translated to pinctrl pin number. This function can be
 * used to find out the corresponding pinctrl pin.
 *
 * Return: a pin number and pointers to the community and pad group, which
 * the pin belongs to, or negative error code if translation can't be done.
 */
static int intel_gpio_to_pin(const struct intel_pinctrl *pctrl, unsigned int offset,
			     const struct intel_community **community,
			     const struct intel_padgroup **padgrp)
{
	const struct intel_community *comm;
	const struct intel_padgroup *grp;

	for_each_intel_gpio_group(pctrl, comm, grp) {
		if (offset >= grp->gpio_base && offset < grp->gpio_base + grp->size) {
			if (community)
				*community = comm;
			if (padgrp)
				*padgrp = grp;

			return grp->base + offset - grp->gpio_base;
		}
	}

	return -EINVAL;
}

/**
 * intel_pin_to_gpio() - Translate from pin number to GPIO offset
 * @pctrl: Pinctrl structure
 * @pin: pin number
 *
 * Translate the pin number of pinctrl to GPIO offset
 *
 * Return: a GPIO offset, or negative error code if translation can't be done.
 */
static int intel_pin_to_gpio(const struct intel_pinctrl *pctrl, int pin)
{
	const struct intel_community *community;
	const struct intel_padgroup *padgrp;

	community = intel_get_community(pctrl, pin);
	if (!community)
		return -EINVAL;

	padgrp = intel_community_get_padgroup(community, pin);
	if (!padgrp)
		return -EINVAL;

	return pin - padgrp->base + padgrp->gpio_base;
}

static int intel_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct intel_pinctrl *pctrl = gpiochip_get_data(chip);
	void __iomem *reg;
	u32 padcfg0;
	int pin;

	pin = intel_gpio_to_pin(pctrl, offset, NULL, NULL);
	if (pin < 0)
		return -EINVAL;

	reg = intel_get_padcfg(pctrl, pin, PADCFG0);
	if (!reg)
		return -EINVAL;

	padcfg0 = readl(reg);
	if (__intel_gpio_get_direction(padcfg0) & PAD_CONNECT_OUTPUT)
		return !!(padcfg0 & PADCFG0_GPIOTXSTATE);

	return !!(padcfg0 & PADCFG0_GPIORXSTATE);
}

static void intel_gpio_set(struct gpio_chip *chip, unsigned int offset,
			   int value)
{
	struct intel_pinctrl *pctrl = gpiochip_get_data(chip);
	void __iomem *reg;
	u32 padcfg0;
	int pin;

	pin = intel_gpio_to_pin(pctrl, offset, NULL, NULL);
	if (pin < 0)
		return;

	reg = intel_get_padcfg(pctrl, pin, PADCFG0);
	if (!reg)
		return;

	guard(raw_spinlock_irqsave)(&pctrl->lock);

	padcfg0 = readl(reg);
	if (value)
		padcfg0 |= PADCFG0_GPIOTXSTATE;
	else
		padcfg0 &= ~PADCFG0_GPIOTXSTATE;
	writel(padcfg0, reg);
}

static int intel_gpio_get_direction(struct gpio_chip *chip, unsigned int offset)
{
	struct intel_pinctrl *pctrl = gpiochip_get_data(chip);
	void __iomem *reg;
	u32 padcfg0;
	int pin;

	pin = intel_gpio_to_pin(pctrl, offset, NULL, NULL);
	if (pin < 0)
		return -EINVAL;

	reg = intel_get_padcfg(pctrl, pin, PADCFG0);
	if (!reg)
		return -EINVAL;

	scoped_guard(raw_spinlock_irqsave, &pctrl->lock)
		padcfg0 = readl(reg);

	if (padcfg0 & PADCFG0_PMODE_MASK)
		return -EINVAL;

	if (__intel_gpio_get_direction(padcfg0) & PAD_CONNECT_OUTPUT)
		return GPIO_LINE_DIRECTION_OUT;

	return GPIO_LINE_DIRECTION_IN;
}

static int intel_gpio_direction_input(struct gpio_chip *chip, unsigned int offset)
{
	return pinctrl_gpio_direction_input(chip, offset);
}

static int intel_gpio_direction_output(struct gpio_chip *chip, unsigned int offset,
				       int value)
{
	intel_gpio_set(chip, offset, value);
	return pinctrl_gpio_direction_output(chip, offset);
}

static const struct gpio_chip intel_gpio_chip = {
	.owner = THIS_MODULE,
	.request = gpiochip_generic_request,
	.free = gpiochip_generic_free,
	.get_direction = intel_gpio_get_direction,
	.direction_input = intel_gpio_direction_input,
	.direction_output = intel_gpio_direction_output,
	.get = intel_gpio_get,
	.set = intel_gpio_set,
	.set_config = gpiochip_generic_config,
};

static void intel_gpio_irq_ack(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct intel_pinctrl *pctrl = gpiochip_get_data(gc);
	const struct intel_community *community;
	const struct intel_padgroup *padgrp;
	int pin;

	pin = intel_gpio_to_pin(pctrl, irqd_to_hwirq(d), &community, &padgrp);
	if (pin >= 0) {
		unsigned int gpp, gpp_offset;
		void __iomem *is;

		gpp = padgrp->reg_num;
		gpp_offset = padgroup_offset(padgrp, pin);

		is = community->regs + community->is_offset + gpp * 4;

		guard(raw_spinlock)(&pctrl->lock);

		writel(BIT(gpp_offset), is);
	}
}

static void intel_gpio_irq_mask_unmask(struct gpio_chip *gc, irq_hw_number_t hwirq, bool mask)
{
	struct intel_pinctrl *pctrl = gpiochip_get_data(gc);
	const struct intel_community *community;
	const struct intel_padgroup *padgrp;
	int pin;

	pin = intel_gpio_to_pin(pctrl, hwirq, &community, &padgrp);
	if (pin >= 0) {
		unsigned int gpp, gpp_offset;
		void __iomem *reg, *is;
		u32 value;

		gpp = padgrp->reg_num;
		gpp_offset = padgroup_offset(padgrp, pin);

		reg = community->regs + community->ie_offset + gpp * 4;
		is = community->regs + community->is_offset + gpp * 4;

		guard(raw_spinlock_irqsave)(&pctrl->lock);

		/* Clear interrupt status first to avoid unexpected interrupt */
		writel(BIT(gpp_offset), is);

		value = readl(reg);
		if (mask)
			value &= ~BIT(gpp_offset);
		else
			value |= BIT(gpp_offset);
		writel(value, reg);
	}
}

static void intel_gpio_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);

	intel_gpio_irq_mask_unmask(gc, hwirq, true);
	gpiochip_disable_irq(gc, hwirq);
}

static void intel_gpio_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);

	gpiochip_enable_irq(gc, hwirq);
	intel_gpio_irq_mask_unmask(gc, hwirq, false);
}

static int intel_gpio_irq_type(struct irq_data *d, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct intel_pinctrl *pctrl = gpiochip_get_data(gc);
	unsigned int pin = intel_gpio_to_pin(pctrl, irqd_to_hwirq(d), NULL, NULL);
	u32 rxevcfg, rxinv, value;
	void __iomem *reg;

	reg = intel_get_padcfg(pctrl, pin, PADCFG0);
	if (!reg)
		return -EINVAL;

	/*
	 * If the pin is in ACPI mode it is still usable as a GPIO but it
	 * cannot be used as IRQ because GPI_IS status bit will not be
	 * updated by the host controller hardware.
	 */
	if (intel_pad_acpi_mode(pctrl, pin)) {
		dev_warn(pctrl->dev, "pin %u cannot be used as IRQ\n", pin);
		return -EPERM;
	}

	if ((type & IRQ_TYPE_EDGE_BOTH) == IRQ_TYPE_EDGE_BOTH) {
		rxevcfg = PADCFG0_RXEVCFG_EDGE_BOTH;
	} else if (type & IRQ_TYPE_EDGE_FALLING) {
		rxevcfg = PADCFG0_RXEVCFG_EDGE;
	} else if (type & IRQ_TYPE_EDGE_RISING) {
		rxevcfg = PADCFG0_RXEVCFG_EDGE;
	} else if (type & IRQ_TYPE_LEVEL_MASK) {
		rxevcfg = PADCFG0_RXEVCFG_LEVEL;
	} else {
		rxevcfg = PADCFG0_RXEVCFG_DISABLED;
	}

	if (type == IRQ_TYPE_EDGE_FALLING || type == IRQ_TYPE_LEVEL_LOW)
		rxinv = PADCFG0_RXINV;
	else
		rxinv = 0;

	guard(raw_spinlock_irqsave)(&pctrl->lock);

	intel_gpio_set_gpio_mode(reg);

	value = readl(reg);

	value = (value & ~PADCFG0_RXEVCFG_MASK) | rxevcfg;
	value = (value & ~PADCFG0_RXINV) | rxinv;

	writel(value, reg);

	if (type & IRQ_TYPE_EDGE_BOTH)
		irq_set_handler_locked(d, handle_edge_irq);
	else if (type & IRQ_TYPE_LEVEL_MASK)
		irq_set_handler_locked(d, handle_level_irq);

	return 0;
}

static int intel_gpio_irq_wake(struct irq_data *d, unsigned int on)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct intel_pinctrl *pctrl = gpiochip_get_data(gc);
	unsigned int pin = intel_gpio_to_pin(pctrl, irqd_to_hwirq(d), NULL, NULL);

	if (on)
		enable_irq_wake(pctrl->irq);
	else
		disable_irq_wake(pctrl->irq);

	dev_dbg(pctrl->dev, "%s wake for pin %u\n", str_enable_disable(on), pin);
	return 0;
}

static const struct irq_chip intel_gpio_irq_chip = {
	.name = "intel-gpio",
	.irq_ack = intel_gpio_irq_ack,
	.irq_mask = intel_gpio_irq_mask,
	.irq_unmask = intel_gpio_irq_unmask,
	.irq_set_type = intel_gpio_irq_type,
	.irq_set_wake = intel_gpio_irq_wake,
	.flags = IRQCHIP_MASK_ON_SUSPEND | IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static irqreturn_t intel_gpio_irq(int irq, void *data)
{
	const struct intel_community *community;
	const struct intel_padgroup *padgrp;
	struct intel_pinctrl *pctrl = data;
	int ret = 0;

	/* Need to check all communities for pending interrupts */
	for_each_intel_pad_group(pctrl, community, padgrp) {
		struct gpio_chip *gc = &pctrl->chip;
		unsigned long pending, enabled;
		unsigned int gpp, gpp_offset;
		void __iomem *reg, *is;

		gpp = padgrp->reg_num;

		reg = community->regs + community->ie_offset + gpp * 4;
		is = community->regs + community->is_offset + gpp * 4;

		scoped_guard(raw_spinlock, &pctrl->lock) {
			pending = readl(is);
			enabled = readl(reg);
		}

		/* Only interrupts that are enabled */
		pending &= enabled;

		for_each_set_bit(gpp_offset, &pending, padgrp->size)
			generic_handle_domain_irq(gc->irq.domain, padgrp->gpio_base + gpp_offset);

		ret += pending ? 1 : 0;
	}

	return IRQ_RETVAL(ret);
}

static void intel_gpio_irq_init(struct intel_pinctrl *pctrl)
{
	const struct intel_community *community;

	for_each_intel_pin_community(pctrl, community) {
		void __iomem *reg, *is;
		unsigned int gpp;

		for (gpp = 0; gpp < community->ngpps; gpp++) {
			reg = community->regs + community->ie_offset + gpp * 4;
			is = community->regs + community->is_offset + gpp * 4;

			/* Mask and clear all interrupts */
			writel(0, reg);
			writel(0xffff, is);
		}
	}
}

static int intel_gpio_irq_init_hw(struct gpio_chip *gc)
{
	struct intel_pinctrl *pctrl = gpiochip_get_data(gc);

	/*
	 * Make sure the interrupt lines are in a proper state before
	 * further configuration.
	 */
	intel_gpio_irq_init(pctrl);

	return 0;
}

static int intel_gpio_add_pin_ranges(struct gpio_chip *gc)
{
	struct intel_pinctrl *pctrl = gpiochip_get_data(gc);
	const struct intel_community *community;
	const struct intel_padgroup *grp;
	int ret;

	for_each_intel_gpio_group(pctrl, community, grp) {
		ret = gpiochip_add_pin_range(&pctrl->chip, dev_name(pctrl->dev),
					     grp->gpio_base, grp->base,
					     grp->size);
		if (ret) {
			dev_err(pctrl->dev, "failed to add GPIO pin range\n");
			return ret;
		}
	}

	return 0;
}

static unsigned int intel_gpio_ngpio(const struct intel_pinctrl *pctrl)
{
	const struct intel_community *community;
	const struct intel_padgroup *grp;
	unsigned int ngpio = 0;

	for_each_intel_gpio_group(pctrl, community, grp) {
		if (grp->gpio_base + grp->size > ngpio)
			ngpio = grp->gpio_base + grp->size;
	}

	return ngpio;
}

static int intel_gpio_probe(struct intel_pinctrl *pctrl, int irq)
{
	int ret;
	struct gpio_irq_chip *girq;

	pctrl->chip = intel_gpio_chip;

	/* Setup GPIO chip */
	pctrl->chip.ngpio = intel_gpio_ngpio(pctrl);
	pctrl->chip.label = dev_name(pctrl->dev);
	pctrl->chip.parent = pctrl->dev;
	pctrl->chip.base = -1;
	pctrl->chip.add_pin_ranges = intel_gpio_add_pin_ranges;
	pctrl->irq = irq;

	/*
	 * On some platforms several GPIO controllers share the same interrupt
	 * line.
	 */
	ret = devm_request_irq(pctrl->dev, irq, intel_gpio_irq,
			       IRQF_SHARED | IRQF_NO_THREAD,
			       dev_name(pctrl->dev), pctrl);
	if (ret) {
		dev_err(pctrl->dev, "failed to request interrupt\n");
		return ret;
	}

	/* Setup IRQ chip */
	girq = &pctrl->chip.irq;
	gpio_irq_chip_set_chip(girq, &intel_gpio_irq_chip);
	/* This will let us handle the IRQ in the driver */
	girq->parent_handler = NULL;
	girq->num_parents = 0;
	girq->default_type = IRQ_TYPE_NONE;
	girq->handler = handle_bad_irq;
	girq->init_hw = intel_gpio_irq_init_hw;

	ret = devm_gpiochip_add_data(pctrl->dev, &pctrl->chip, pctrl);
	if (ret) {
		dev_err(pctrl->dev, "failed to register gpiochip\n");
		return ret;
	}

	return 0;
}

static int intel_pinctrl_add_padgroups_by_gpps(struct intel_pinctrl *pctrl,
					       struct intel_community *community)
{
	struct intel_padgroup *gpps;
	unsigned int padown_num = 0;
	size_t i, ngpps = community->ngpps;

	gpps = devm_kcalloc(pctrl->dev, ngpps, sizeof(*gpps), GFP_KERNEL);
	if (!gpps)
		return -ENOMEM;

	for (i = 0; i < ngpps; i++) {
		gpps[i] = community->gpps[i];

		if (gpps[i].size > INTEL_PINCTRL_MAX_GPP_SIZE)
			return -EINVAL;

		/* Special treatment for GPIO base */
		switch (gpps[i].gpio_base) {
			case INTEL_GPIO_BASE_MATCH:
				gpps[i].gpio_base = gpps[i].base;
				break;
			case INTEL_GPIO_BASE_ZERO:
				gpps[i].gpio_base = 0;
				break;
			case INTEL_GPIO_BASE_NOMAP:
				break;
			default:
				break;
		}

		gpps[i].padown_num = padown_num;
		padown_num += DIV_ROUND_UP(gpps[i].size * 4, INTEL_PINCTRL_MAX_GPP_SIZE);
	}

	community->gpps = gpps;

	return 0;
}

static int intel_pinctrl_add_padgroups_by_size(struct intel_pinctrl *pctrl,
					       struct intel_community *community)
{
	struct intel_padgroup *gpps;
	unsigned int npins = community->npins;
	unsigned int padown_num = 0;
	size_t i, ngpps = DIV_ROUND_UP(npins, community->gpp_size);

	if (community->gpp_size > INTEL_PINCTRL_MAX_GPP_SIZE)
		return -EINVAL;

	gpps = devm_kcalloc(pctrl->dev, ngpps, sizeof(*gpps), GFP_KERNEL);
	if (!gpps)
		return -ENOMEM;

	for (i = 0; i < ngpps; i++) {
		unsigned int gpp_size = community->gpp_size;

		gpps[i].reg_num = i;
		gpps[i].base = community->pin_base + i * gpp_size;
		gpps[i].size = min(gpp_size, npins);
		npins -= gpps[i].size;

		gpps[i].gpio_base = gpps[i].base;
		gpps[i].padown_num = padown_num;

		padown_num += community->gpp_num_padown_regs;
	}

	community->ngpps = ngpps;
	community->gpps = gpps;

	return 0;
}

static int intel_pinctrl_pm_init(struct intel_pinctrl *pctrl)
{
#ifdef CONFIG_PM_SLEEP
	const struct intel_pinctrl_soc_data *soc = pctrl->soc;
	struct intel_community_context *communities;
	struct intel_pad_context *pads;
	int i;

	pads = devm_kcalloc(pctrl->dev, soc->npins, sizeof(*pads), GFP_KERNEL);
	if (!pads)
		return -ENOMEM;

	communities = devm_kcalloc(pctrl->dev, pctrl->ncommunities,
				   sizeof(*communities), GFP_KERNEL);
	if (!communities)
		return -ENOMEM;


	for (i = 0; i < pctrl->ncommunities; i++) {
		struct intel_community *community = &pctrl->communities[i];
		u32 *intmask, *hostown;

		intmask = devm_kcalloc(pctrl->dev, community->ngpps,
				       sizeof(*intmask), GFP_KERNEL);
		if (!intmask)
			return -ENOMEM;

		communities[i].intmask = intmask;

		hostown = devm_kcalloc(pctrl->dev, community->ngpps,
				       sizeof(*hostown), GFP_KERNEL);
		if (!hostown)
			return -ENOMEM;

		communities[i].hostown = hostown;
	}

	pctrl->context.pads = pads;
	pctrl->context.communities = communities;
#endif

	return 0;
}

static int intel_pinctrl_probe_pwm(struct intel_pinctrl *pctrl,
				   struct intel_community *community)
{
	static const struct pwm_lpss_boardinfo info = {
		.clk_rate = 19200000,
		.npwm = 1,
		.base_unit_bits = 22,
	};
	struct pwm_chip *chip;

	if (!(community->features & PINCTRL_FEATURE_PWM))
		return 0;

	if (!IS_REACHABLE(CONFIG_PWM_LPSS))
		return 0;

	chip = devm_pwm_lpss_probe(pctrl->dev, community->regs + PWMC, &info);
	return PTR_ERR_OR_ZERO(chip);
}

int intel_pinctrl_probe(struct platform_device *pdev,
			const struct intel_pinctrl_soc_data *soc_data)
{
	struct device *dev = &pdev->dev;
	struct intel_pinctrl *pctrl;
	int i, ret, irq;

	pctrl = devm_kzalloc(dev, sizeof(*pctrl), GFP_KERNEL);
	if (!pctrl)
		return -ENOMEM;

	pctrl->dev = dev;
	pctrl->soc = soc_data;
	raw_spin_lock_init(&pctrl->lock);

	/*
	 * Make a copy of the communities which we can use to hold pointers
	 * to the registers.
	 */
	pctrl->ncommunities = pctrl->soc->ncommunities;
	pctrl->communities = devm_kmemdup_array(dev, pctrl->soc->communities, pctrl->ncommunities,
						sizeof(*pctrl->soc->communities), GFP_KERNEL);
	if (!pctrl->communities)
		return -ENOMEM;

	for (i = 0; i < pctrl->ncommunities; i++) {
		struct intel_community *community = &pctrl->communities[i];
		void __iomem *regs;
		u32 offset;
		u32 value;

		regs = devm_platform_ioremap_resource(pdev, community->barno);
		if (IS_ERR(regs))
			return PTR_ERR(regs);

		/*
		 * Determine community features based on the revision.
		 * A value of all ones means the device is not present.
		 */
		value = readl(regs + REVID);
		if (value == ~0u)
			return -ENODEV;
		if (((value & REVID_MASK) >> REVID_SHIFT) >= 0x94) {
			community->features |= PINCTRL_FEATURE_DEBOUNCE;
			community->features |= PINCTRL_FEATURE_1K_PD;
		}

		/* Determine community features based on the capabilities */
		offset = CAPLIST;
		do {
			value = readl(regs + offset);
			switch ((value & CAPLIST_ID_MASK) >> CAPLIST_ID_SHIFT) {
			case CAPLIST_ID_GPIO_HW_INFO:
				community->features |= PINCTRL_FEATURE_GPIO_HW_INFO;
				break;
			case CAPLIST_ID_PWM:
				community->features |= PINCTRL_FEATURE_PWM;
				break;
			case CAPLIST_ID_BLINK:
				community->features |= PINCTRL_FEATURE_BLINK;
				break;
			case CAPLIST_ID_EXP:
				community->features |= PINCTRL_FEATURE_EXP;
				break;
			default:
				break;
			}
			offset = (value & CAPLIST_NEXT_MASK) >> CAPLIST_NEXT_SHIFT;
		} while (offset);

		dev_dbg(dev, "Community%d features: %#08x\n", i, community->features);

		/* Read offset of the pad configuration registers */
		offset = readl(regs + PADBAR);

		community->regs = regs;
		community->pad_regs = regs + offset;

		if (community->gpps)
			ret = intel_pinctrl_add_padgroups_by_gpps(pctrl, community);
		else
			ret = intel_pinctrl_add_padgroups_by_size(pctrl, community);
		if (ret)
			return ret;

		ret = intel_pinctrl_probe_pwm(pctrl, community);
		if (ret)
			return ret;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = intel_pinctrl_pm_init(pctrl);
	if (ret)
		return ret;

	pctrl->pctldesc = intel_pinctrl_desc;
	pctrl->pctldesc.name = dev_name(dev);
	pctrl->pctldesc.pins = pctrl->soc->pins;
	pctrl->pctldesc.npins = pctrl->soc->npins;

	pctrl->pctldev = devm_pinctrl_register(dev, &pctrl->pctldesc, pctrl);
	if (IS_ERR(pctrl->pctldev)) {
		dev_err(dev, "failed to register pinctrl driver\n");
		return PTR_ERR(pctrl->pctldev);
	}

	ret = intel_gpio_probe(pctrl, irq);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, pctrl);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(intel_pinctrl_probe, "PINCTRL_INTEL");

int intel_pinctrl_probe_by_hid(struct platform_device *pdev)
{
	const struct intel_pinctrl_soc_data *data;

	data = device_get_match_data(&pdev->dev);
	if (!data)
		return -ENODATA;

	return intel_pinctrl_probe(pdev, data);
}
EXPORT_SYMBOL_NS_GPL(intel_pinctrl_probe_by_hid, "PINCTRL_INTEL");

int intel_pinctrl_probe_by_uid(struct platform_device *pdev)
{
	const struct intel_pinctrl_soc_data *data;

	data = intel_pinctrl_get_soc_data(pdev);
	if (IS_ERR(data))
		return PTR_ERR(data);

	return intel_pinctrl_probe(pdev, data);
}
EXPORT_SYMBOL_NS_GPL(intel_pinctrl_probe_by_uid, "PINCTRL_INTEL");

const struct intel_pinctrl_soc_data *intel_pinctrl_get_soc_data(struct platform_device *pdev)
{
	const struct intel_pinctrl_soc_data * const *table;
	const struct intel_pinctrl_soc_data *data;
	struct device *dev = &pdev->dev;

	table = device_get_match_data(dev);
	if (table) {
		struct acpi_device *adev = ACPI_COMPANION(dev);
		unsigned int i;

		for (i = 0; table[i]; i++) {
			if (acpi_dev_uid_match(adev, table[i]->uid))
				break;
		}
		data = table[i];
	} else {
		const struct platform_device_id *id;

		id = platform_get_device_id(pdev);
		if (!id)
			return ERR_PTR(-ENODEV);

		table = (const struct intel_pinctrl_soc_data * const *)id->driver_data;
		data = table[pdev->id];
	}

	return data ?: ERR_PTR(-ENODATA);
}
EXPORT_SYMBOL_NS_GPL(intel_pinctrl_get_soc_data, "PINCTRL_INTEL");

static bool __intel_gpio_is_direct_irq(u32 value)
{
	return (value & PADCFG0_GPIROUTIOXAPIC) &&
	       (__intel_gpio_get_direction(value) == PAD_CONNECT_INPUT) &&
	       (__intel_gpio_get_gpio_mode(value) == PADCFG0_PMODE_GPIO);
}

static bool intel_pinctrl_should_save(struct intel_pinctrl *pctrl, unsigned int pin)
{
	const struct pin_desc *pd = pin_desc_get(pctrl->pctldev, pin);
	u32 value;

	if (!pd || !intel_pad_usable(pctrl, pin))
		return false;

	/*
	 * Only restore the pin if it is actually in use by the kernel (or
	 * by userspace). It is possible that some pins are used by the
	 * BIOS during resume and those are not always locked down so leave
	 * them alone.
	 */
	if (pd->mux_owner || pd->gpio_owner ||
	    gpiochip_line_is_irq(&pctrl->chip, intel_pin_to_gpio(pctrl, pin)))
		return true;

	/*
	 * The firmware on some systems may configure GPIO pins to be
	 * an interrupt source in so called "direct IRQ" mode. In such
	 * cases the GPIO controller driver has no idea if those pins
	 * are being used or not. At the same time, there is a known bug
	 * in the firmwares that don't restore the pin settings correctly
	 * after suspend, i.e. by an unknown reason the Rx value becomes
	 * inverted.
	 *
	 * Hence, let's save and restore the pins that are configured
	 * as GPIOs in the input mode with GPIROUTIOXAPIC bit set.
	 *
	 * See https://bugzilla.kernel.org/show_bug.cgi?id=214749.
	 */
	value = readl(intel_get_padcfg(pctrl, pin, PADCFG0));
	if (__intel_gpio_is_direct_irq(value))
		return true;

	return false;
}

static int intel_pinctrl_suspend_noirq(struct device *dev)
{
	struct intel_pinctrl *pctrl = dev_get_drvdata(dev);
	struct intel_community_context *communities;
	struct intel_pad_context *pads;
	int i;

	pads = pctrl->context.pads;
	for (i = 0; i < pctrl->soc->npins; i++) {
		const struct pinctrl_pin_desc *desc = &pctrl->soc->pins[i];
		void __iomem *padcfg;
		u32 val;

		if (!intel_pinctrl_should_save(pctrl, desc->number))
			continue;

		val = readl(intel_get_padcfg(pctrl, desc->number, PADCFG0));
		pads[i].padcfg0 = val & ~PADCFG0_GPIORXSTATE;
		val = readl(intel_get_padcfg(pctrl, desc->number, PADCFG1));
		pads[i].padcfg1 = val;

		padcfg = intel_get_padcfg(pctrl, desc->number, PADCFG2);
		if (padcfg)
			pads[i].padcfg2 = readl(padcfg);
	}

	communities = pctrl->context.communities;
	for (i = 0; i < pctrl->ncommunities; i++) {
		struct intel_community *community = &pctrl->communities[i];
		void __iomem *base;
		unsigned int gpp;

		base = community->regs + community->ie_offset;
		for (gpp = 0; gpp < community->ngpps; gpp++)
			communities[i].intmask[gpp] = readl(base + gpp * 4);

		base = community->regs + community->hostown_offset;
		for (gpp = 0; gpp < community->ngpps; gpp++)
			communities[i].hostown[gpp] = readl(base + gpp * 4);
	}

	return 0;
}

static bool intel_gpio_update_reg(void __iomem *reg, u32 mask, u32 value)
{
	u32 curr, updated;

	curr = readl(reg);

	updated = (curr & ~mask) | (value & mask);
	if (curr == updated)
		return false;

	writel(updated, reg);
	return true;
}

static void intel_restore_hostown(struct intel_pinctrl *pctrl, unsigned int c,
				  void __iomem *base, unsigned int gpp, u32 saved)
{
	const struct intel_community *community = &pctrl->communities[c];
	const struct intel_padgroup *padgrp = &community->gpps[gpp];
	struct device *dev = pctrl->dev;
	const char *dummy;
	u32 requested = 0;
	unsigned int i;

	if (padgrp->gpio_base == INTEL_GPIO_BASE_NOMAP)
		return;

	for_each_requested_gpio_in_range(&pctrl->chip, i, padgrp->gpio_base, padgrp->size, dummy)
		requested |= BIT(i);

	if (!intel_gpio_update_reg(base + gpp * 4, requested, saved))
		return;

	dev_dbg(dev, "restored hostown %u/%u %#08x\n", c, gpp, readl(base + gpp * 4));
}

static void intel_restore_intmask(struct intel_pinctrl *pctrl, unsigned int c,
				  void __iomem *base, unsigned int gpp, u32 saved)
{
	struct device *dev = pctrl->dev;

	if (!intel_gpio_update_reg(base + gpp * 4, ~0U, saved))
		return;

	dev_dbg(dev, "restored mask %u/%u %#08x\n", c, gpp, readl(base + gpp * 4));
}

static void intel_restore_padcfg(struct intel_pinctrl *pctrl, unsigned int pin,
				 unsigned int reg, u32 saved)
{
	u32 mask = (reg == PADCFG0) ? PADCFG0_GPIORXSTATE : 0;
	unsigned int n = reg / sizeof(u32);
	struct device *dev = pctrl->dev;
	void __iomem *padcfg;

	padcfg = intel_get_padcfg(pctrl, pin, reg);
	if (!padcfg)
		return;

	if (!intel_gpio_update_reg(padcfg, ~mask, saved))
		return;

	dev_dbg(dev, "restored pin %u padcfg%u %#08x\n", pin, n, readl(padcfg));
}

static int intel_pinctrl_resume_noirq(struct device *dev)
{
	struct intel_pinctrl *pctrl = dev_get_drvdata(dev);
	const struct intel_community_context *communities;
	const struct intel_pad_context *pads;
	int i;

	/* Mask all interrupts */
	intel_gpio_irq_init(pctrl);

	pads = pctrl->context.pads;
	for (i = 0; i < pctrl->soc->npins; i++) {
		const struct pinctrl_pin_desc *desc = &pctrl->soc->pins[i];

		if (!(intel_pinctrl_should_save(pctrl, desc->number) ||
		      /*
		       * If the firmware mangled the register contents too much,
		       * check the saved value for the Direct IRQ mode.
		       */
		      __intel_gpio_is_direct_irq(pads[i].padcfg0)))
			continue;

		intel_restore_padcfg(pctrl, desc->number, PADCFG0, pads[i].padcfg0);
		intel_restore_padcfg(pctrl, desc->number, PADCFG1, pads[i].padcfg1);
		intel_restore_padcfg(pctrl, desc->number, PADCFG2, pads[i].padcfg2);
	}

	communities = pctrl->context.communities;
	for (i = 0; i < pctrl->ncommunities; i++) {
		struct intel_community *community = &pctrl->communities[i];
		void __iomem *base;
		unsigned int gpp;

		base = community->regs + community->ie_offset;
		for (gpp = 0; gpp < community->ngpps; gpp++)
			intel_restore_intmask(pctrl, i, base, gpp, communities[i].intmask[gpp]);

		base = community->regs + community->hostown_offset;
		for (gpp = 0; gpp < community->ngpps; gpp++)
			intel_restore_hostown(pctrl, i, base, gpp, communities[i].hostown[gpp]);
	}

	return 0;
}

EXPORT_NS_GPL_DEV_SLEEP_PM_OPS(intel_pinctrl_pm_ops, PINCTRL_INTEL) = {
	NOIRQ_SYSTEM_SLEEP_PM_OPS(intel_pinctrl_suspend_noirq, intel_pinctrl_resume_noirq)
};

MODULE_AUTHOR("Mathias Nyman <mathias.nyman@linux.intel.com>");
MODULE_AUTHOR("Mika Westerberg <mika.westerberg@linux.intel.com>");
MODULE_DESCRIPTION("Intel pinctrl/GPIO core driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS("PWM_LPSS");
