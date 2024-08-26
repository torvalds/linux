// SPDX-License-Identifier: GPL-2.0-only
/*
 * drivers/pwm/pwm-pxa.c
 *
 * simple driver for PWM (Pulse Width Modulator) controller
 *
 * 2008-02-13	initial version
 *		eric miao <eric.miao@marvell.com>
 *
 * Links to reference manuals for some of the supported PWM chips can be found
 * in Documentation/arch/arm/marvell.rst.
 *
 * Limitations:
 * - When PWM is stopped, the current PWM period stops abruptly at the next
 *   input clock (PWMCR_SD is set) and the output is driven to inactive.
 */

#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/pwm.h>
#include <linux/of.h>

#include <asm/div64.h>

#define HAS_SECONDARY_PWM	0x10

static const struct platform_device_id pwm_id_table[] = {
	/*   PWM    has_secondary_pwm? */
	{ "pxa25x-pwm", 0 },
	{ "pxa27x-pwm", HAS_SECONDARY_PWM },
	{ "pxa168-pwm", 0 },
	{ "pxa910-pwm", 0 },
	{ },
};
MODULE_DEVICE_TABLE(platform, pwm_id_table);

/* PWM registers and bits definitions */
#define PWMCR		(0x00)
#define PWMDCR		(0x04)
#define PWMPCR		(0x08)

#define PWMCR_SD	(1 << 6)
#define PWMDCR_FD	(1 << 10)

struct pxa_pwm_chip {
	struct device	*dev;

	struct clk	*clk;
	void __iomem	*mmio_base;
};

static inline struct pxa_pwm_chip *to_pxa_pwm_chip(struct pwm_chip *chip)
{
	return pwmchip_get_drvdata(chip);
}

/*
 * period_ns = 10^9 * (PRESCALE + 1) * (PV + 1) / PWM_CLK_RATE
 * duty_ns   = 10^9 * (PRESCALE + 1) * DC / PWM_CLK_RATE
 */
static int pxa_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
			  u64 duty_ns, u64 period_ns)
{
	struct pxa_pwm_chip *pc = to_pxa_pwm_chip(chip);
	unsigned long long c;
	unsigned long period_cycles, prescale, pv, dc;
	unsigned long offset;

	offset = pwm->hwpwm ? 0x10 : 0;

	c = clk_get_rate(pc->clk);
	c = c * period_ns;
	do_div(c, 1000000000);
	period_cycles = c;

	if (period_cycles < 1)
		period_cycles = 1;
	prescale = (period_cycles - 1) / 1024;
	pv = period_cycles / (prescale + 1) - 1;

	if (prescale > 63)
		return -EINVAL;

	if (duty_ns == period_ns)
		dc = PWMDCR_FD;
	else
		dc = mul_u64_u64_div_u64(pv + 1, duty_ns, period_ns);

	writel(prescale | PWMCR_SD, pc->mmio_base + offset + PWMCR);
	writel(dc, pc->mmio_base + offset + PWMDCR);
	writel(pv, pc->mmio_base + offset + PWMPCR);

	return 0;
}

static int pxa_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			 const struct pwm_state *state)
{
	struct pxa_pwm_chip *pc = to_pxa_pwm_chip(chip);
	u64 duty_cycle;
	int err;

	if (state->polarity != PWM_POLARITY_NORMAL)
		return -EINVAL;

	err = clk_prepare_enable(pc->clk);
	if (err)
		return err;

	duty_cycle = state->enabled ? state->duty_cycle : 0;

	err = pxa_pwm_config(chip, pwm, duty_cycle, state->period);
	if (err) {
		clk_disable_unprepare(pc->clk);
		return err;
	}

	if (state->enabled && !pwm->state.enabled)
		return 0;

	clk_disable_unprepare(pc->clk);

	if (!state->enabled && pwm->state.enabled)
		clk_disable_unprepare(pc->clk);

	return 0;
}

static const struct pwm_ops pxa_pwm_ops = {
	.apply = pxa_pwm_apply,
};

#ifdef CONFIG_OF
/*
 * Device tree users must create one device instance for each PWM channel.
 * Hence we dispense with the HAS_SECONDARY_PWM and "tell" the original driver
 * code that this is a single channel pxa25x-pwm.  Currently all devices are
 * supported identically.
 */
static const struct of_device_id pwm_of_match[] = {
	{ .compatible = "marvell,pxa250-pwm", .data = &pwm_id_table[0]},
	{ .compatible = "marvell,pxa270-pwm", .data = &pwm_id_table[0]},
	{ .compatible = "marvell,pxa168-pwm", .data = &pwm_id_table[0]},
	{ .compatible = "marvell,pxa910-pwm", .data = &pwm_id_table[0]},
	{ }
};
MODULE_DEVICE_TABLE(of, pwm_of_match);
#else
#define pwm_of_match NULL
#endif

static int pwm_probe(struct platform_device *pdev)
{
	const struct platform_device_id *id = platform_get_device_id(pdev);
	struct pwm_chip *chip;
	struct pxa_pwm_chip *pc;
	int ret = 0;

	if (IS_ENABLED(CONFIG_OF) && id == NULL)
		id = of_device_get_match_data(&pdev->dev);

	if (id == NULL)
		return -EINVAL;

	chip = devm_pwmchip_alloc(&pdev->dev,
				  (id->driver_data & HAS_SECONDARY_PWM) ? 2 : 1,
				  sizeof(*pc));
	if (IS_ERR(chip))
		return PTR_ERR(chip);
	pc = to_pxa_pwm_chip(chip);

	pc->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(pc->clk))
		return PTR_ERR(pc->clk);

	chip->ops = &pxa_pwm_ops;

	if (IS_ENABLED(CONFIG_OF))
		chip->of_xlate = of_pwm_single_xlate;

	pc->mmio_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pc->mmio_base))
		return PTR_ERR(pc->mmio_base);

	ret = devm_pwmchip_add(&pdev->dev, chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "pwmchip_add() failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static struct platform_driver pwm_driver = {
	.driver		= {
		.name	= "pxa25x-pwm",
		.of_match_table = pwm_of_match,
	},
	.probe		= pwm_probe,
	.id_table	= pwm_id_table,
};

module_platform_driver(pwm_driver);

MODULE_DESCRIPTION("PXA Pulse Width Modulator driver");
MODULE_LICENSE("GPL v2");
