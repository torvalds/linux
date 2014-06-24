/*
 * ST Microelectronics SPEAr Pulse Width Modulator driver
 *
 * Copyright (C) 2012 ST Microelectronics
 * Shiraz Hashim <shiraz.linux.kernel@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/slab.h>
#include <linux/types.h>

#define NUM_PWM		4

/* PWM registers and bits definitions */
#define PWMCR			0x00	/* Control Register */
#define PWMCR_PWM_ENABLE	0x1
#define PWMCR_PRESCALE_SHIFT	2
#define PWMCR_MIN_PRESCALE	0x00
#define PWMCR_MAX_PRESCALE	0x3FFF

#define PWMDCR			0x04	/* Duty Cycle Register */
#define PWMDCR_MIN_DUTY		0x0001
#define PWMDCR_MAX_DUTY		0xFFFF

#define PWMPCR			0x08	/* Period Register */
#define PWMPCR_MIN_PERIOD	0x0001
#define PWMPCR_MAX_PERIOD	0xFFFF

/* Following only available on 13xx SoCs */
#define PWMMCR			0x3C	/* Master Control Register */
#define PWMMCR_PWM_ENABLE	0x1

/**
 * struct spear_pwm_chip - struct representing pwm chip
 *
 * @mmio_base: base address of pwm chip
 * @clk: pointer to clk structure of pwm chip
 * @chip: linux pwm chip representation
 */
struct spear_pwm_chip {
	void __iomem *mmio_base;
	struct clk *clk;
	struct pwm_chip chip;
};

static inline struct spear_pwm_chip *to_spear_pwm_chip(struct pwm_chip *chip)
{
	return container_of(chip, struct spear_pwm_chip, chip);
}

static inline u32 spear_pwm_readl(struct spear_pwm_chip *chip, unsigned int num,
				  unsigned long offset)
{
	return readl_relaxed(chip->mmio_base + (num << 4) + offset);
}

static inline void spear_pwm_writel(struct spear_pwm_chip *chip,
				    unsigned int num, unsigned long offset,
				    unsigned long val)
{
	writel_relaxed(val, chip->mmio_base + (num << 4) + offset);
}

static int spear_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
			    int duty_ns, int period_ns)
{
	struct spear_pwm_chip *pc = to_spear_pwm_chip(chip);
	u64 val, div, clk_rate;
	unsigned long prescale = PWMCR_MIN_PRESCALE, pv, dc;
	int ret;

	/*
	 * Find pv, dc and prescale to suit duty_ns and period_ns. This is done
	 * according to formulas described below:
	 *
	 * period_ns = 10^9 * (PRESCALE + 1) * PV / PWM_CLK_RATE
	 * duty_ns = 10^9 * (PRESCALE + 1) * DC / PWM_CLK_RATE
	 *
	 * PV = (PWM_CLK_RATE * period_ns) / (10^9 * (PRESCALE + 1))
	 * DC = (PWM_CLK_RATE * duty_ns) / (10^9 * (PRESCALE + 1))
	 */
	clk_rate = clk_get_rate(pc->clk);
	while (1) {
		div = 1000000000;
		div *= 1 + prescale;
		val = clk_rate * period_ns;
		pv = div64_u64(val, div);
		val = clk_rate * duty_ns;
		dc = div64_u64(val, div);

		/* if duty_ns and period_ns are not achievable then return */
		if (pv < PWMPCR_MIN_PERIOD || dc < PWMDCR_MIN_DUTY)
			return -EINVAL;

		/*
		 * if pv and dc have crossed their upper limit, then increase
		 * prescale and recalculate pv and dc.
		 */
		if (pv > PWMPCR_MAX_PERIOD || dc > PWMDCR_MAX_DUTY) {
			if (++prescale > PWMCR_MAX_PRESCALE)
				return -EINVAL;
			continue;
		}
		break;
	}

	/*
	 * NOTE: the clock to PWM has to be enabled first before writing to the
	 * registers.
	 */
	ret = clk_enable(pc->clk);
	if (ret)
		return ret;

	spear_pwm_writel(pc, pwm->hwpwm, PWMCR,
			prescale << PWMCR_PRESCALE_SHIFT);
	spear_pwm_writel(pc, pwm->hwpwm, PWMDCR, dc);
	spear_pwm_writel(pc, pwm->hwpwm, PWMPCR, pv);
	clk_disable(pc->clk);

	return 0;
}

static int spear_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct spear_pwm_chip *pc = to_spear_pwm_chip(chip);
	int rc = 0;
	u32 val;

	rc = clk_enable(pc->clk);
	if (rc)
		return rc;

	val = spear_pwm_readl(pc, pwm->hwpwm, PWMCR);
	val |= PWMCR_PWM_ENABLE;
	spear_pwm_writel(pc, pwm->hwpwm, PWMCR, val);

	return 0;
}

static void spear_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct spear_pwm_chip *pc = to_spear_pwm_chip(chip);
	u32 val;

	val = spear_pwm_readl(pc, pwm->hwpwm, PWMCR);
	val &= ~PWMCR_PWM_ENABLE;
	spear_pwm_writel(pc, pwm->hwpwm, PWMCR, val);

	clk_disable(pc->clk);
}

static const struct pwm_ops spear_pwm_ops = {
	.config = spear_pwm_config,
	.enable = spear_pwm_enable,
	.disable = spear_pwm_disable,
	.owner = THIS_MODULE,
};

static int spear_pwm_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct spear_pwm_chip *pc;
	struct resource *r;
	int ret;
	u32 val;

	pc = devm_kzalloc(&pdev->dev, sizeof(*pc), GFP_KERNEL);
	if (!pc) {
		dev_err(&pdev->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pc->mmio_base = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(pc->mmio_base))
		return PTR_ERR(pc->mmio_base);

	pc->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(pc->clk))
		return PTR_ERR(pc->clk);

	platform_set_drvdata(pdev, pc);

	pc->chip.dev = &pdev->dev;
	pc->chip.ops = &spear_pwm_ops;
	pc->chip.base = -1;
	pc->chip.npwm = NUM_PWM;

	ret = clk_prepare(pc->clk);
	if (ret)
		return ret;

	if (of_device_is_compatible(np, "st,spear1340-pwm")) {
		ret = clk_enable(pc->clk);
		if (ret) {
			clk_unprepare(pc->clk);
			return ret;
		}
		/*
		 * Following enables PWM chip, channels would still be
		 * enabled individually through their control register
		 */
		val = readl_relaxed(pc->mmio_base + PWMMCR);
		val |= PWMMCR_PWM_ENABLE;
		writel_relaxed(val, pc->mmio_base + PWMMCR);

		clk_disable(pc->clk);
	}

	ret = pwmchip_add(&pc->chip);
	if (!ret) {
		clk_unprepare(pc->clk);
		dev_err(&pdev->dev, "pwmchip_add() failed: %d\n", ret);
	}

	return ret;
}

static int spear_pwm_remove(struct platform_device *pdev)
{
	struct spear_pwm_chip *pc = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < NUM_PWM; i++)
		pwm_disable(&pc->chip.pwms[i]);

	/* clk was prepared in probe, hence unprepare it here */
	clk_unprepare(pc->clk);
	return pwmchip_remove(&pc->chip);
}

static const struct of_device_id spear_pwm_of_match[] = {
	{ .compatible = "st,spear320-pwm" },
	{ .compatible = "st,spear1340-pwm" },
	{ }
};

MODULE_DEVICE_TABLE(of, spear_pwm_of_match);

static struct platform_driver spear_pwm_driver = {
	.driver = {
		.name = "spear-pwm",
		.owner = THIS_MODULE,
		.of_match_table = spear_pwm_of_match,
	},
	.probe = spear_pwm_probe,
	.remove = spear_pwm_remove,
};

module_platform_driver(spear_pwm_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Shiraz Hashim <shiraz.linux.kernel@gmail.com>");
MODULE_AUTHOR("Viresh Kumar <viresh.kumar@linaro.com>");
MODULE_ALIAS("platform:spear-pwm");
