/*
 * Copyright (c) 2015, 2017-2018, The Linux Foundation. All rights reserved.
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

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/pm_domain.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>
#include <linux/slab.h>
#include "gdsc.h"

#define PWR_ON_MASK		BIT(31)
#define EN_REST_WAIT_MASK	GENMASK_ULL(23, 20)
#define EN_FEW_WAIT_MASK	GENMASK_ULL(19, 16)
#define CLK_DIS_WAIT_MASK	GENMASK_ULL(15, 12)
#define SW_OVERRIDE_MASK	BIT(2)
#define HW_CONTROL_MASK		BIT(1)
#define SW_COLLAPSE_MASK	BIT(0)
#define GMEM_CLAMP_IO_MASK	BIT(0)
#define GMEM_RESET_MASK		BIT(4)

/* CFG_GDSCR */
#define GDSC_POWER_UP_COMPLETE		BIT(16)
#define GDSC_POWER_DOWN_COMPLETE	BIT(15)
#define CFG_GDSCR_OFFSET		0x4

/* Wait 2^n CXO cycles between all states. Here, n=2 (4 cycles). */
#define EN_REST_WAIT_VAL	(0x2 << 20)
#define EN_FEW_WAIT_VAL		(0x8 << 16)
#define CLK_DIS_WAIT_VAL	(0x2 << 12)

#define RETAIN_MEM		BIT(14)
#define RETAIN_PERIPH		BIT(13)

#define TIMEOUT_US		500

#define domain_to_gdsc(domain) container_of(domain, struct gdsc, pd)

enum gdsc_status {
	GDSC_OFF,
	GDSC_ON
};

/* Returns 1 if GDSC status is status, 0 if not, and < 0 on error */
static int gdsc_check_status(struct gdsc *sc, enum gdsc_status status)
{
	unsigned int reg;
	u32 val;
	int ret;

	if (sc->flags & POLL_CFG_GDSCR)
		reg = sc->gdscr + CFG_GDSCR_OFFSET;
	else if (sc->gds_hw_ctrl)
		reg = sc->gds_hw_ctrl;
	else
		reg = sc->gdscr;

	ret = regmap_read(sc->regmap, reg, &val);
	if (ret)
		return ret;

	if (sc->flags & POLL_CFG_GDSCR) {
		switch (status) {
		case GDSC_ON:
			return !!(val & GDSC_POWER_UP_COMPLETE);
		case GDSC_OFF:
			return !!(val & GDSC_POWER_DOWN_COMPLETE);
		}
	}

	switch (status) {
	case GDSC_ON:
		return !!(val & PWR_ON_MASK);
	case GDSC_OFF:
		return !(val & PWR_ON_MASK);
	}

	return -EINVAL;
}

static int gdsc_hwctrl(struct gdsc *sc, bool en)
{
	u32 val = en ? HW_CONTROL_MASK : 0;

	return regmap_update_bits(sc->regmap, sc->gdscr, HW_CONTROL_MASK, val);
}

static int gdsc_poll_status(struct gdsc *sc, enum gdsc_status status)
{
	ktime_t start;

	start = ktime_get();
	do {
		if (gdsc_check_status(sc, status))
			return 0;
	} while (ktime_us_delta(ktime_get(), start) < TIMEOUT_US);

	if (gdsc_check_status(sc, status))
		return 0;

	return -ETIMEDOUT;
}

static int gdsc_toggle_logic(struct gdsc *sc, enum gdsc_status status)
{
	int ret;
	u32 val = (status == GDSC_ON) ? 0 : SW_COLLAPSE_MASK;

	ret = regmap_update_bits(sc->regmap, sc->gdscr, SW_COLLAPSE_MASK, val);
	if (ret)
		return ret;

	/* If disabling votable gdscs, don't poll on status */
	if ((sc->flags & VOTABLE) && status == GDSC_OFF) {
		/*
		 * Add a short delay here to ensure that an enable
		 * right after it was disabled does not put it in an
		 * unknown state
		 */
		udelay(TIMEOUT_US);
		return 0;
	}

	if (sc->gds_hw_ctrl) {
		/*
		 * The gds hw controller asserts/de-asserts the status bit soon
		 * after it receives a power on/off request from a master.
		 * The controller then takes around 8 xo cycles to start its
		 * internal state machine and update the status bit. During
		 * this time, the status bit does not reflect the true status
		 * of the core.
		 * Add a delay of 1 us between writing to the SW_COLLAPSE bit
		 * and polling the status bit.
		 */
		udelay(1);
	}

	return gdsc_poll_status(sc, status);
}

static inline int gdsc_deassert_reset(struct gdsc *sc)
{
	int i;

	for (i = 0; i < sc->reset_count; i++)
		sc->rcdev->ops->deassert(sc->rcdev, sc->resets[i]);
	return 0;
}

static inline int gdsc_assert_reset(struct gdsc *sc)
{
	int i;

	for (i = 0; i < sc->reset_count; i++)
		sc->rcdev->ops->assert(sc->rcdev, sc->resets[i]);
	return 0;
}

static inline void gdsc_force_mem_on(struct gdsc *sc)
{
	int i;
	u32 mask = RETAIN_MEM | RETAIN_PERIPH;

	for (i = 0; i < sc->cxc_count; i++)
		regmap_update_bits(sc->regmap, sc->cxcs[i], mask, mask);
}

static inline void gdsc_clear_mem_on(struct gdsc *sc)
{
	int i;
	u32 mask = RETAIN_MEM | RETAIN_PERIPH;

	for (i = 0; i < sc->cxc_count; i++)
		regmap_update_bits(sc->regmap, sc->cxcs[i], mask, 0);
}

static inline void gdsc_deassert_clamp_io(struct gdsc *sc)
{
	regmap_update_bits(sc->regmap, sc->clamp_io_ctrl,
			   GMEM_CLAMP_IO_MASK, 0);
}

static inline void gdsc_assert_clamp_io(struct gdsc *sc)
{
	regmap_update_bits(sc->regmap, sc->clamp_io_ctrl,
			   GMEM_CLAMP_IO_MASK, 1);
}

static inline void gdsc_assert_reset_aon(struct gdsc *sc)
{
	regmap_update_bits(sc->regmap, sc->clamp_io_ctrl,
			   GMEM_RESET_MASK, 1);
	udelay(1);
	regmap_update_bits(sc->regmap, sc->clamp_io_ctrl,
			   GMEM_RESET_MASK, 0);
}
static int gdsc_enable(struct generic_pm_domain *domain)
{
	struct gdsc *sc = domain_to_gdsc(domain);
	int ret;

	if (sc->pwrsts == PWRSTS_ON)
		return gdsc_deassert_reset(sc);

	if (sc->flags & SW_RESET) {
		gdsc_assert_reset(sc);
		udelay(1);
		gdsc_deassert_reset(sc);
	}

	if (sc->flags & CLAMP_IO) {
		if (sc->flags & AON_RESET)
			gdsc_assert_reset_aon(sc);
		gdsc_deassert_clamp_io(sc);
	}

	ret = gdsc_toggle_logic(sc, GDSC_ON);
	if (ret)
		return ret;

	if (sc->pwrsts & PWRSTS_OFF)
		gdsc_force_mem_on(sc);

	/*
	 * If clocks to this power domain were already on, they will take an
	 * additional 4 clock cycles to re-enable after the power domain is
	 * enabled. Delay to account for this. A delay is also needed to ensure
	 * clocks are not enabled within 400ns of enabling power to the
	 * memories.
	 */
	udelay(1);

	/* Turn on HW trigger mode if supported */
	if (sc->flags & HW_CTRL) {
		ret = gdsc_hwctrl(sc, true);
		if (ret)
			return ret;
		/*
		 * Wait for the GDSC to go through a power down and
		 * up cycle.  In case a firmware ends up polling status
		 * bits for the gdsc, it might read an 'on' status before
		 * the GDSC can finish the power cycle.
		 * We wait 1us before returning to ensure the firmware
		 * can't immediately poll the status bits.
		 */
		udelay(1);
	}

	return 0;
}

static int gdsc_disable(struct generic_pm_domain *domain)
{
	struct gdsc *sc = domain_to_gdsc(domain);
	int ret;

	if (sc->pwrsts == PWRSTS_ON)
		return gdsc_assert_reset(sc);

	/* Turn off HW trigger mode if supported */
	if (sc->flags & HW_CTRL) {
		ret = gdsc_hwctrl(sc, false);
		if (ret < 0)
			return ret;
		/*
		 * Wait for the GDSC to go through a power down and
		 * up cycle.  In case we end up polling status
		 * bits for the gdsc before the power cycle is completed
		 * it might read an 'on' status wrongly.
		 */
		udelay(1);

		ret = gdsc_poll_status(sc, GDSC_ON);
		if (ret)
			return ret;
	}

	if (sc->pwrsts & PWRSTS_OFF)
		gdsc_clear_mem_on(sc);

	ret = gdsc_toggle_logic(sc, GDSC_OFF);
	if (ret)
		return ret;

	if (sc->flags & CLAMP_IO)
		gdsc_assert_clamp_io(sc);

	return 0;
}

static int gdsc_init(struct gdsc *sc)
{
	u32 mask, val;
	int on, ret;

	/*
	 * Disable HW trigger: collapse/restore occur based on registers writes.
	 * Disable SW override: Use hardware state-machine for sequencing.
	 * Configure wait time between states.
	 */
	mask = HW_CONTROL_MASK | SW_OVERRIDE_MASK |
	       EN_REST_WAIT_MASK | EN_FEW_WAIT_MASK | CLK_DIS_WAIT_MASK;
	val = EN_REST_WAIT_VAL | EN_FEW_WAIT_VAL | CLK_DIS_WAIT_VAL;
	ret = regmap_update_bits(sc->regmap, sc->gdscr, mask, val);
	if (ret)
		return ret;

	/* Force gdsc ON if only ON state is supported */
	if (sc->pwrsts == PWRSTS_ON) {
		ret = gdsc_toggle_logic(sc, GDSC_ON);
		if (ret)
			return ret;
	}

	on = gdsc_check_status(sc, GDSC_ON);
	if (on < 0)
		return on;

	/*
	 * Votable GDSCs can be ON due to Vote from other masters.
	 * If a Votable GDSC is ON, make sure we have a Vote.
	 */
	if ((sc->flags & VOTABLE) && on)
		gdsc_enable(&sc->pd);

	if (on || (sc->pwrsts & PWRSTS_RET))
		gdsc_force_mem_on(sc);
	else
		gdsc_clear_mem_on(sc);

	sc->pd.power_off = gdsc_disable;
	sc->pd.power_on = gdsc_enable;
	pm_genpd_init(&sc->pd, NULL, !on);

	return 0;
}

int gdsc_register(struct gdsc_desc *desc,
		  struct reset_controller_dev *rcdev, struct regmap *regmap)
{
	int i, ret;
	struct genpd_onecell_data *data;
	struct device *dev = desc->dev;
	struct gdsc **scs = desc->scs;
	size_t num = desc->num;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->domains = devm_kcalloc(dev, num, sizeof(*data->domains),
				     GFP_KERNEL);
	if (!data->domains)
		return -ENOMEM;

	data->num_domains = num;
	for (i = 0; i < num; i++) {
		if (!scs[i])
			continue;
		scs[i]->regmap = regmap;
		scs[i]->rcdev = rcdev;
		ret = gdsc_init(scs[i]);
		if (ret)
			return ret;
		data->domains[i] = &scs[i]->pd;
	}

	/* Add subdomains */
	for (i = 0; i < num; i++) {
		if (!scs[i])
			continue;
		if (scs[i]->parent)
			pm_genpd_add_subdomain(scs[i]->parent, &scs[i]->pd);
	}

	return of_genpd_add_provider_onecell(dev->of_node, data);
}

void gdsc_unregister(struct gdsc_desc *desc)
{
	int i;
	struct device *dev = desc->dev;
	struct gdsc **scs = desc->scs;
	size_t num = desc->num;

	/* Remove subdomains */
	for (i = 0; i < num; i++) {
		if (!scs[i])
			continue;
		if (scs[i]->parent)
			pm_genpd_remove_subdomain(scs[i]->parent, &scs[i]->pd);
	}
	of_genpd_del_provider(dev->of_node);
}
