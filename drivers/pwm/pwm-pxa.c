// SPDX-License-Identifier: GPL-2.0-only
/*
 * drivers/pwm/pwm-pxa.c
 *
 * simple driver for PWM (Pulse Width Modulator) controller
 *
 * 2008-02-13	initial version
 *		eric miao <eric.miao@marvell.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/pwm.h>
#include <linux/of_device.h>

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
	struct pwm_chip	chip;
	struct device	*dev;

	struct clk	*clk;
	void __iomem	*mmio_base;
};

static inline struct pxa_pwm_chip *to_pxa_pwm_chip(struct pwm_chip *chip)
{
	return container_of(chip, struct pxa_pwm_chip, chip);
}

/*
 * period_ns = 10^9 * (PRESCALE + 1) * (PV + 1) / PWM_CLK_RATE
 * duty_ns   = 10^9 * (PRESCALE + 1) * DC / PWM_CLK_RATE
 */
static int pxa_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
			  int duty_ns, int period_ns)
{
	struct pxa_pwm_chip *pc = to_pxa_pwm_chip(chip);
	unsigned long long c;
	unsigned long period_cycles, prescale, pv, dc;
	unsigned long offset;
	int rc;

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
		dc = (pv + 1) * duty_ns / period_ns;

	/* NOTE: the clock to PWM has to be enabled first
	 * before writing to the registers
	 */
	rc = clk_prepare_enable(pc->clk);
	if (rc < 0)
		return rc;

	writel(prescale, pc->mmio_base + offset + PWMCR);
	writel(dc, pc->mmio_base + offset + PWMDCR);
	writel(pv, pc->mmio_base + offset + PWMPCR);

	clk_disable_unprepare(pc->clk);
	return 0;
}

static int pxa_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct pxa_pwm_chip *pc = to_pxa_pwm_chip(chip);

	return clk_prepare_enable(pc->clk);
}

static void pxa_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct pxa_pwm_chip *pc = to_pxa_pwm_chip(chip);

	clk_disable_unprepare(pc->clk);
}

static const struct pwm_ops pxa_pwm_ops = {
	.config = pxa_pwm_config,
	.enable = pxa_pwm_enable,
	.disable = pxa_pwm_disable,
	.owner = THIS_MODULE,
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

static const struct platform_device_id *pxa_pwm_get_id_dt(struct device *dev)
{
	const struct of_device_id *id = of_match_device(pwm_of_match, dev);

	return id ? id->data : NULL;
}

static struct pwm_device *
pxa_pwm_of_xlate(struct pwm_chip *pc, const struct of_phandle_args *args)
{
	struct pwm_device *pwm;

	pwm = pwm_request_from_chip(pc, 0, NULL);
	if (IS_ERR(pwm))
		return pwm;

	pwm->args.period = args->args[0];

	return pwm;
}

static int pwm_probe(struct platform_device *pdev)
{
	const struct platform_device_id *id = platform_get_device_id(pdev);
	struct pxa_pwm_chip *pc;
	int ret = 0;

	if (IS_ENABLED(CONFIG_OF) && id == NULL)
		id = pxa_pwm_get_id_dt(&pdev->dev);

	if (id == NULL)
		return -EINVAL;

	pc = devm_kzalloc(&pdev->dev, sizeof(*pc), GFP_KERNEL);
	if (pc == NULL)
		return -ENOMEM;

	pc->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(pc->clk))
		return PTR_ERR(pc->clk);

	pc->chip.dev = &pdev->dev;
	pc->chip.ops = &pxa_pwm_ops;
	pc->chip.npwm = (id->driver_data & HAS_SECONDARY_PWM) ? 2 : 1;

	if (IS_ENABLED(CONFIG_OF)) {
		pc->chip.of_xlate = pxa_pwm_of_xlate;
		pc->chip.of_pwm_n_cells = 1;
	}

	pc->mmio_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pc->mmio_base))
		return PTR_ERR(pc->mmio_base);

	ret = devm_pwmchip_add(&pdev->dev, &pc->chip);
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

MODULE_LICENSE("GPL v2");
