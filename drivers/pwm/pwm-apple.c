// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Driver for the Apple SoC PWM controller
 *
 * Copyright The Asahi Linux Contributors
 *
 * Limitations:
 * - The writes to cycle registers are shadowed until a write to
 *   the control register.
 * - If both OFF_CYCLES and ON_CYCLES are set to 0, the output
 *   is a constant off signal.
 * - When APPLE_PWM_CTRL is set to 0, the output is constant low
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/math64.h>

#define APPLE_PWM_CTRL        0x00
#define APPLE_PWM_ON_CYCLES   0x1c
#define APPLE_PWM_OFF_CYCLES  0x18

#define APPLE_PWM_CTRL_ENABLE        BIT(0)
#define APPLE_PWM_CTRL_MODE          BIT(2)
#define APPLE_PWM_CTRL_UPDATE        BIT(5)
#define APPLE_PWM_CTRL_TRIGGER       BIT(9)
#define APPLE_PWM_CTRL_INVERT        BIT(10)
#define APPLE_PWM_CTRL_OUTPUT_ENABLE BIT(14)

struct apple_pwm {
	struct pwm_chip chip;
	void __iomem *base;
	u64 clkrate;
};

static inline struct apple_pwm *to_apple_pwm(struct pwm_chip *chip)
{
	return container_of(chip, struct apple_pwm, chip);
}

static int apple_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			   const struct pwm_state *state)
{
	struct apple_pwm *fpwm;

	if (state->polarity == PWM_POLARITY_INVERSED)
		return -EINVAL;

	fpwm = to_apple_pwm(chip);
	if (state->enabled) {
		u64 on_cycles, off_cycles;

		on_cycles = mul_u64_u64_div_u64(fpwm->clkrate,
						state->duty_cycle, NSEC_PER_SEC);
		if (on_cycles > 0xFFFFFFFF)
			on_cycles = 0xFFFFFFFF;

		off_cycles = mul_u64_u64_div_u64(fpwm->clkrate,
						 state->period, NSEC_PER_SEC) - on_cycles;
		if (off_cycles > 0xFFFFFFFF)
			off_cycles = 0xFFFFFFFF;

		writel(on_cycles, fpwm->base + APPLE_PWM_ON_CYCLES);
		writel(off_cycles, fpwm->base + APPLE_PWM_OFF_CYCLES);
		writel(APPLE_PWM_CTRL_ENABLE | APPLE_PWM_CTRL_OUTPUT_ENABLE | APPLE_PWM_CTRL_UPDATE,
		       fpwm->base + APPLE_PWM_CTRL);
	} else {
		writel(0, fpwm->base + APPLE_PWM_CTRL);
	}
	return 0;
}

static int apple_pwm_get_state(struct pwm_chip *chip, struct pwm_device *pwm,
			   struct pwm_state *state)
{
	struct apple_pwm *fpwm;
	u32 on_cycles, off_cycles, ctrl;

	fpwm = to_apple_pwm(chip);

	ctrl = readl(fpwm->base + APPLE_PWM_CTRL);
	on_cycles = readl(fpwm->base + APPLE_PWM_ON_CYCLES);
	off_cycles = readl(fpwm->base + APPLE_PWM_OFF_CYCLES);

	state->enabled = (ctrl & APPLE_PWM_CTRL_ENABLE) && (ctrl & APPLE_PWM_CTRL_OUTPUT_ENABLE);
	state->polarity = PWM_POLARITY_NORMAL;
	// on_cycles + off_cycles is 33 bits, NSEC_PER_SEC is 30, there is no overflow
	state->duty_cycle = DIV64_U64_ROUND_UP((u64)on_cycles * NSEC_PER_SEC, fpwm->clkrate);
	state->period = DIV64_U64_ROUND_UP(((u64)off_cycles + (u64)on_cycles) *
					    NSEC_PER_SEC, fpwm->clkrate);

	return 0;
}

static const struct pwm_ops apple_pwm_ops = {
	.apply = apple_pwm_apply,
	.get_state = apple_pwm_get_state,
	.owner = THIS_MODULE,
};

static int apple_pwm_probe(struct platform_device *pdev)
{
	struct apple_pwm *fpwm;
	struct clk *clk;
	int ret;

	fpwm = devm_kzalloc(&pdev->dev, sizeof(*fpwm), GFP_KERNEL);
	if (!fpwm)
		return -ENOMEM;

	fpwm->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(fpwm->base))
		return PTR_ERR(fpwm->base);

	clk = devm_clk_get_enabled(&pdev->dev, NULL);
	if (IS_ERR(clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(clk), "unable to get the clock");

	/*
	 * Uses the 24MHz system clock on all existing devices, can only
	 * happen if the device tree is broken
	 *
	 * This check is done to prevent an overflow in .apply
	 */
	fpwm->clkrate = clk_get_rate(clk);
	if (fpwm->clkrate > NSEC_PER_SEC)
		return dev_err_probe(&pdev->dev, -EINVAL, "pwm clock out of range");

	fpwm->chip.dev = &pdev->dev;
	fpwm->chip.npwm = 1;
	fpwm->chip.ops = &apple_pwm_ops;

	ret = devm_pwmchip_add(&pdev->dev, &fpwm->chip);
	if (ret < 0)
		return dev_err_probe(&pdev->dev, ret, "unable to add pwm chip");

	return 0;
}

static const struct of_device_id apple_pwm_of_match[] = {
	{ .compatible = "apple,s5l-fpwm" },
	{}
};
MODULE_DEVICE_TABLE(of, apple_pwm_of_match);

static struct platform_driver apple_pwm_driver = {
	.probe = apple_pwm_probe,
	.driver = {
		.name = "apple-pwm",
		.of_match_table = apple_pwm_of_match,
	},
};
module_platform_driver(apple_pwm_driver);

MODULE_DESCRIPTION("Apple SoC PWM driver");
MODULE_LICENSE("Dual MIT/GPL");
