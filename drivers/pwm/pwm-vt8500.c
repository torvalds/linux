// SPDX-License-Identifier: GPL-2.0-only
/*
 * drivers/pwm/pwm-vt8500.c
 *
 * Copyright (C) 2012 Tony Prisk <linux@prisktech.co.nz>
 * Copyright (C) 2010 Alexey Charkov <alchark@gmail.com>
 */

#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/pwm.h>
#include <linux/delay.h>
#include <linux/clk.h>

#include <asm/div64.h>

/*
 * SoC architecture allocates register space for 4 PWMs but only
 * 2 are currently implemented.
 */
#define VT8500_NR_PWMS	2

#define REG_CTRL(pwm)		(((pwm) << 4) + 0x00)
#define REG_SCALAR(pwm)		(((pwm) << 4) + 0x04)
#define REG_PERIOD(pwm)		(((pwm) << 4) + 0x08)
#define REG_DUTY(pwm)		(((pwm) << 4) + 0x0C)
#define REG_STATUS		0x40

#define CTRL_ENABLE		BIT(0)
#define CTRL_INVERT		BIT(1)
#define CTRL_AUTOLOAD		BIT(2)
#define CTRL_STOP_IMM		BIT(3)
#define CTRL_LOAD_PRESCALE	BIT(4)
#define CTRL_LOAD_PERIOD	BIT(5)

#define STATUS_CTRL_UPDATE	BIT(0)
#define STATUS_SCALAR_UPDATE	BIT(1)
#define STATUS_PERIOD_UPDATE	BIT(2)
#define STATUS_DUTY_UPDATE	BIT(3)
#define STATUS_ALL_UPDATE	0x0F

struct vt8500_chip {
	struct pwm_chip chip;
	void __iomem *base;
	struct clk *clk;
};

#define to_vt8500_chip(chip)	container_of(chip, struct vt8500_chip, chip)

#define msecs_to_loops(t) (loops_per_jiffy / 1000 * HZ * t)
static inline void vt8500_pwm_busy_wait(struct vt8500_chip *vt8500, int nr, u8 bitmask)
{
	int loops = msecs_to_loops(10);
	u32 mask = bitmask << (nr << 8);

	while ((readl(vt8500->base + REG_STATUS) & mask) && --loops)
		cpu_relax();

	if (unlikely(!loops))
		dev_warn(vt8500->chip.dev, "Waiting for status bits 0x%x to clear timed out\n",
			 mask);
}

static int vt8500_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
		u64 duty_ns, u64 period_ns)
{
	struct vt8500_chip *vt8500 = to_vt8500_chip(chip);
	unsigned long long c;
	unsigned long period_cycles, prescale, pv, dc;
	int err;
	u32 val;

	err = clk_enable(vt8500->clk);
	if (err < 0) {
		dev_err(chip->dev, "failed to enable clock\n");
		return err;
	}

	c = clk_get_rate(vt8500->clk);
	c = c * period_ns;
	do_div(c, 1000000000);
	period_cycles = c;

	if (period_cycles < 1)
		period_cycles = 1;
	prescale = (period_cycles - 1) / 4096;
	pv = period_cycles / (prescale + 1) - 1;
	if (pv > 4095)
		pv = 4095;

	if (prescale > 1023) {
		clk_disable(vt8500->clk);
		return -EINVAL;
	}

	c = (unsigned long long)pv * duty_ns;

	dc = div64_u64(c, period_ns);

	writel(prescale, vt8500->base + REG_SCALAR(pwm->hwpwm));
	vt8500_pwm_busy_wait(vt8500, pwm->hwpwm, STATUS_SCALAR_UPDATE);

	writel(pv, vt8500->base + REG_PERIOD(pwm->hwpwm));
	vt8500_pwm_busy_wait(vt8500, pwm->hwpwm, STATUS_PERIOD_UPDATE);

	writel(dc, vt8500->base + REG_DUTY(pwm->hwpwm));
	vt8500_pwm_busy_wait(vt8500, pwm->hwpwm, STATUS_DUTY_UPDATE);

	val = readl(vt8500->base + REG_CTRL(pwm->hwpwm));
	val |= CTRL_AUTOLOAD;
	writel(val, vt8500->base + REG_CTRL(pwm->hwpwm));
	vt8500_pwm_busy_wait(vt8500, pwm->hwpwm, STATUS_CTRL_UPDATE);

	clk_disable(vt8500->clk);
	return 0;
}

static int vt8500_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct vt8500_chip *vt8500 = to_vt8500_chip(chip);
	int err;
	u32 val;

	err = clk_enable(vt8500->clk);
	if (err < 0) {
		dev_err(chip->dev, "failed to enable clock\n");
		return err;
	}

	val = readl(vt8500->base + REG_CTRL(pwm->hwpwm));
	val |= CTRL_ENABLE;
	writel(val, vt8500->base + REG_CTRL(pwm->hwpwm));
	vt8500_pwm_busy_wait(vt8500, pwm->hwpwm, STATUS_CTRL_UPDATE);

	return 0;
}

static void vt8500_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct vt8500_chip *vt8500 = to_vt8500_chip(chip);
	u32 val;

	val = readl(vt8500->base + REG_CTRL(pwm->hwpwm));
	val &= ~CTRL_ENABLE;
	writel(val, vt8500->base + REG_CTRL(pwm->hwpwm));
	vt8500_pwm_busy_wait(vt8500, pwm->hwpwm, STATUS_CTRL_UPDATE);

	clk_disable(vt8500->clk);
}

static int vt8500_pwm_set_polarity(struct pwm_chip *chip,
				   struct pwm_device *pwm,
				   enum pwm_polarity polarity)
{
	struct vt8500_chip *vt8500 = to_vt8500_chip(chip);
	u32 val;

	val = readl(vt8500->base + REG_CTRL(pwm->hwpwm));

	if (polarity == PWM_POLARITY_INVERSED)
		val |= CTRL_INVERT;
	else
		val &= ~CTRL_INVERT;

	writel(val, vt8500->base + REG_CTRL(pwm->hwpwm));
	vt8500_pwm_busy_wait(vt8500, pwm->hwpwm, STATUS_CTRL_UPDATE);

	return 0;
}

static int vt8500_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			    const struct pwm_state *state)
{
	int err;
	bool enabled = pwm->state.enabled;

	if (state->polarity != pwm->state.polarity) {
		/*
		 * Changing the polarity of a running PWM is only allowed when
		 * the PWM driver implements ->apply().
		 */
		if (enabled) {
			vt8500_pwm_disable(chip, pwm);

			enabled = false;
		}

		err = vt8500_pwm_set_polarity(chip, pwm, state->polarity);
		if (err)
			return err;
	}

	if (!state->enabled) {
		if (enabled)
			vt8500_pwm_disable(chip, pwm);

		return 0;
	}

	/*
	 * We cannot skip calling ->config even if state->period ==
	 * pwm->state.period && state->duty_cycle == pwm->state.duty_cycle
	 * because we might have exited early in the last call to
	 * pwm_apply_might_sleep because of !state->enabled and so the two values in
	 * pwm->state might not be configured in hardware.
	 */
	err = vt8500_pwm_config(chip, pwm, state->duty_cycle, state->period);
	if (err)
		return err;

	if (!enabled)
		err = vt8500_pwm_enable(chip, pwm);

	return err;
}

static const struct pwm_ops vt8500_pwm_ops = {
	.apply = vt8500_pwm_apply,
};

static const struct of_device_id vt8500_pwm_dt_ids[] = {
	{ .compatible = "via,vt8500-pwm", },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, vt8500_pwm_dt_ids);

static int vt8500_pwm_probe(struct platform_device *pdev)
{
	struct vt8500_chip *vt8500;
	struct device_node *np = pdev->dev.of_node;
	int ret;

	if (!np)
		return dev_err_probe(&pdev->dev, -EINVAL, "invalid devicetree node\n");

	vt8500 = devm_kzalloc(&pdev->dev, sizeof(*vt8500), GFP_KERNEL);
	if (vt8500 == NULL)
		return -ENOMEM;

	vt8500->chip.dev = &pdev->dev;
	vt8500->chip.ops = &vt8500_pwm_ops;
	vt8500->chip.npwm = VT8500_NR_PWMS;

	vt8500->clk = devm_clk_get_prepared(&pdev->dev, NULL);
	if (IS_ERR(vt8500->clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(vt8500->clk), "clock source not specified\n");

	vt8500->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(vt8500->base))
		return PTR_ERR(vt8500->base);

	ret = devm_pwmchip_add(&pdev->dev, &vt8500->chip);
	if (ret < 0)
		return dev_err_probe(&pdev->dev, ret, "failed to add PWM chip\n");

	return 0;
}

static struct platform_driver vt8500_pwm_driver = {
	.probe		= vt8500_pwm_probe,
	.driver		= {
		.name	= "vt8500-pwm",
		.of_match_table = vt8500_pwm_dt_ids,
	},
};
module_platform_driver(vt8500_pwm_driver);

MODULE_DESCRIPTION("VT8500 PWM Driver");
MODULE_AUTHOR("Tony Prisk <linux@prisktech.co.nz>");
MODULE_LICENSE("GPL v2");
