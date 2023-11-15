// SPDX-License-Identifier: GPL-2.0
/*
 * DesignWare PWM Controller driver core
 *
 * Copyright (C) 2018-2020 Intel Corporation
 *
 * Author: Felipe Balbi (Intel)
 * Author: Jarkko Nikula <jarkko.nikula@linux.intel.com>
 * Author: Raymond Tan <raymond.tan@intel.com>
 */

#define DEFAULT_SYMBOL_NAMESPACE dwc_pwm

#include <linux/bitops.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <linux/pwm.h>

#include "pwm-dwc.h"

static void __dwc_pwm_set_enable(struct dwc_pwm *dwc, int pwm, int enabled)
{
	u32 reg;

	reg = dwc_pwm_readl(dwc, DWC_TIM_CTRL(pwm));

	if (enabled)
		reg |= DWC_TIM_CTRL_EN;
	else
		reg &= ~DWC_TIM_CTRL_EN;

	dwc_pwm_writel(dwc, reg, DWC_TIM_CTRL(pwm));
}

static int __dwc_pwm_configure_timer(struct dwc_pwm *dwc,
				     struct pwm_device *pwm,
				     const struct pwm_state *state)
{
	u64 tmp;
	u32 ctrl;
	u32 high;
	u32 low;

	/*
	 * Calculate width of low and high period in terms of input clock
	 * periods and check are the result within HW limits between 1 and
	 * 2^32 periods.
	 */
	tmp = DIV_ROUND_CLOSEST_ULL(state->duty_cycle, dwc->clk_ns);
	if (tmp < 1 || tmp > (1ULL << 32))
		return -ERANGE;
	low = tmp - 1;

	tmp = DIV_ROUND_CLOSEST_ULL(state->period - state->duty_cycle,
				    dwc->clk_ns);
	if (tmp < 1 || tmp > (1ULL << 32))
		return -ERANGE;
	high = tmp - 1;

	/*
	 * Specification says timer usage flow is to disable timer, then
	 * program it followed by enable. It also says Load Count is loaded
	 * into timer after it is enabled - either after a disable or
	 * a reset. Based on measurements it happens also without disable
	 * whenever Load Count is updated. But follow the specification.
	 */
	__dwc_pwm_set_enable(dwc, pwm->hwpwm, false);

	/*
	 * Write Load Count and Load Count 2 registers. Former defines the
	 * width of low period and latter the width of high period in terms
	 * multiple of input clock periods:
	 * Width = ((Count + 1) * input clock period).
	 */
	dwc_pwm_writel(dwc, low, DWC_TIM_LD_CNT(pwm->hwpwm));
	dwc_pwm_writel(dwc, high, DWC_TIM_LD_CNT2(pwm->hwpwm));

	/*
	 * Set user-defined mode, timer reloads from Load Count registers
	 * when it counts down to 0.
	 * Set PWM mode, it makes output to toggle and width of low and high
	 * periods are set by Load Count registers.
	 */
	ctrl = DWC_TIM_CTRL_MODE_USER | DWC_TIM_CTRL_PWM;
	dwc_pwm_writel(dwc, ctrl, DWC_TIM_CTRL(pwm->hwpwm));

	/*
	 * Enable timer. Output starts from low period.
	 */
	__dwc_pwm_set_enable(dwc, pwm->hwpwm, state->enabled);

	return 0;
}

static int dwc_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			 const struct pwm_state *state)
{
	struct dwc_pwm *dwc = to_dwc_pwm(chip);

	if (state->polarity != PWM_POLARITY_INVERSED)
		return -EINVAL;

	if (state->enabled) {
		if (!pwm->state.enabled)
			pm_runtime_get_sync(chip->dev);
		return __dwc_pwm_configure_timer(dwc, pwm, state);
	} else {
		if (pwm->state.enabled) {
			__dwc_pwm_set_enable(dwc, pwm->hwpwm, false);
			pm_runtime_put_sync(chip->dev);
		}
	}

	return 0;
}

static int dwc_pwm_get_state(struct pwm_chip *chip, struct pwm_device *pwm,
			     struct pwm_state *state)
{
	struct dwc_pwm *dwc = to_dwc_pwm(chip);
	u64 duty, period;
	u32 ctrl, ld, ld2;

	pm_runtime_get_sync(chip->dev);

	ctrl = dwc_pwm_readl(dwc, DWC_TIM_CTRL(pwm->hwpwm));
	ld = dwc_pwm_readl(dwc, DWC_TIM_LD_CNT(pwm->hwpwm));
	ld2 = dwc_pwm_readl(dwc, DWC_TIM_LD_CNT2(pwm->hwpwm));

	state->enabled = !!(ctrl & DWC_TIM_CTRL_EN);

	/*
	 * If we're not in PWM, technically the output is a 50-50
	 * based on the timer load-count only.
	 */
	if (ctrl & DWC_TIM_CTRL_PWM) {
		duty = (ld + 1) * dwc->clk_ns;
		period = (ld2 + 1)  * dwc->clk_ns;
		period += duty;
	} else {
		duty = (ld + 1) * dwc->clk_ns;
		period = duty * 2;
	}

	state->polarity = PWM_POLARITY_INVERSED;
	state->period = period;
	state->duty_cycle = duty;

	pm_runtime_put_sync(chip->dev);

	return 0;
}

static const struct pwm_ops dwc_pwm_ops = {
	.apply = dwc_pwm_apply,
	.get_state = dwc_pwm_get_state,
};

struct dwc_pwm *dwc_pwm_alloc(struct device *dev)
{
	struct dwc_pwm *dwc;

	dwc = devm_kzalloc(dev, sizeof(*dwc), GFP_KERNEL);
	if (!dwc)
		return NULL;

	dwc->clk_ns = 10;
	dwc->chip.dev = dev;
	dwc->chip.ops = &dwc_pwm_ops;
	dwc->chip.npwm = DWC_TIMERS_TOTAL;

	dev_set_drvdata(dev, dwc);
	return dwc;
}
EXPORT_SYMBOL_GPL(dwc_pwm_alloc);

MODULE_AUTHOR("Felipe Balbi (Intel)");
MODULE_AUTHOR("Jarkko Nikula <jarkko.nikula@linux.intel.com>");
MODULE_AUTHOR("Raymond Tan <raymond.tan@intel.com>");
MODULE_DESCRIPTION("DesignWare PWM Controller");
MODULE_LICENSE("GPL");
