/*
 * ECAP PWM driver
 *
 * Copyright (C) 2012 Texas Instruments, Inc. - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/pm_runtime.h>
#include <linux/pwm.h>

/* ECAP registers and bits definitions */
#define CAP1			0x08
#define CAP2			0x0C
#define CAP3			0x10
#define CAP4			0x14
#define ECCTL2			0x2A
#define ECCTL2_APWM_POL_LOW	BIT(10)
#define ECCTL2_APWM_MODE	BIT(9)
#define ECCTL2_SYNC_SEL_DISA	(BIT(7) | BIT(6))
#define ECCTL2_TSCTR_FREERUN	BIT(4)

struct ecap_pwm_chip {
	struct pwm_chip	chip;
	unsigned int	clk_rate;
	void __iomem	*mmio_base;
};

static inline struct ecap_pwm_chip *to_ecap_pwm_chip(struct pwm_chip *chip)
{
	return container_of(chip, struct ecap_pwm_chip, chip);
}

/*
 * period_ns = 10^9 * period_cycles / PWM_CLK_RATE
 * duty_ns   = 10^9 * duty_cycles / PWM_CLK_RATE
 */
static int ecap_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
		int duty_ns, int period_ns)
{
	struct ecap_pwm_chip *pc = to_ecap_pwm_chip(chip);
	unsigned long long c;
	unsigned long period_cycles, duty_cycles;
	unsigned int reg_val;

	if (period_ns < 0 || duty_ns < 0 || period_ns > NSEC_PER_SEC)
		return -ERANGE;

	c = pc->clk_rate;
	c = c * period_ns;
	do_div(c, NSEC_PER_SEC);
	period_cycles = (unsigned long)c;

	if (period_cycles < 1) {
		period_cycles = 1;
		duty_cycles = 1;
	} else {
		c = pc->clk_rate;
		c = c * duty_ns;
		do_div(c, NSEC_PER_SEC);
		duty_cycles = (unsigned long)c;
	}

	pm_runtime_get_sync(pc->chip.dev);

	reg_val = readw(pc->mmio_base + ECCTL2);

	/* Configure APWM mode & disable sync option */
	reg_val |= ECCTL2_APWM_MODE | ECCTL2_SYNC_SEL_DISA;

	writew(reg_val, pc->mmio_base + ECCTL2);

	if (!test_bit(PWMF_ENABLED, &pwm->flags)) {
		/* Update active registers if not running */
		writel(duty_cycles, pc->mmio_base + CAP2);
		writel(period_cycles, pc->mmio_base + CAP1);
	} else {
		/*
		 * Update shadow registers to configure period and
		 * compare values. This helps current PWM period to
		 * complete on reconfiguring
		 */
		writel(duty_cycles, pc->mmio_base + CAP4);
		writel(period_cycles, pc->mmio_base + CAP3);
	}

	if (!test_bit(PWMF_ENABLED, &pwm->flags)) {
		reg_val = readw(pc->mmio_base + ECCTL2);
		/* Disable APWM mode to put APWM output Low */
		reg_val &= ~ECCTL2_APWM_MODE;
		writew(reg_val, pc->mmio_base + ECCTL2);
	}

	pm_runtime_put_sync(pc->chip.dev);
	return 0;
}

static int ecap_pwm_set_polarity(struct pwm_chip *chip, struct pwm_device *pwm,
		enum pwm_polarity polarity)
{
	struct ecap_pwm_chip *pc = to_ecap_pwm_chip(chip);
	unsigned short reg_val;

	pm_runtime_get_sync(pc->chip.dev);
	reg_val = readw(pc->mmio_base + ECCTL2);
	if (polarity == PWM_POLARITY_INVERSED)
		/* Duty cycle defines LOW period of PWM */
		reg_val |= ECCTL2_APWM_POL_LOW;
	else
		/* Duty cycle defines HIGH period of PWM */
		reg_val &= ~ECCTL2_APWM_POL_LOW;

	writew(reg_val, pc->mmio_base + ECCTL2);
	pm_runtime_put_sync(pc->chip.dev);
	return 0;
}

static int ecap_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct ecap_pwm_chip *pc = to_ecap_pwm_chip(chip);
	unsigned int reg_val;

	/* Leave clock enabled on enabling PWM */
	pm_runtime_get_sync(pc->chip.dev);

	/*
	 * Enable 'Free run Time stamp counter mode' to start counter
	 * and  'APWM mode' to enable APWM output
	 */
	reg_val = readw(pc->mmio_base + ECCTL2);
	reg_val |= ECCTL2_TSCTR_FREERUN | ECCTL2_APWM_MODE;
	writew(reg_val, pc->mmio_base + ECCTL2);
	return 0;
}

static void ecap_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct ecap_pwm_chip *pc = to_ecap_pwm_chip(chip);
	unsigned int reg_val;

	/*
	 * Disable 'Free run Time stamp counter mode' to stop counter
	 * and 'APWM mode' to put APWM output to low
	 */
	reg_val = readw(pc->mmio_base + ECCTL2);
	reg_val &= ~(ECCTL2_TSCTR_FREERUN | ECCTL2_APWM_MODE);
	writew(reg_val, pc->mmio_base + ECCTL2);

	/* Disable clock on PWM disable */
	pm_runtime_put_sync(pc->chip.dev);
}

static void ecap_pwm_free(struct pwm_chip *chip, struct pwm_device *pwm)
{
	if (test_bit(PWMF_ENABLED, &pwm->flags)) {
		dev_warn(chip->dev, "Removing PWM device without disabling\n");
		pm_runtime_put_sync(chip->dev);
	}
}

static const struct pwm_ops ecap_pwm_ops = {
	.free		= ecap_pwm_free,
	.config		= ecap_pwm_config,
	.set_polarity	= ecap_pwm_set_polarity,
	.enable		= ecap_pwm_enable,
	.disable	= ecap_pwm_disable,
	.owner		= THIS_MODULE,
};

static int __devinit ecap_pwm_probe(struct platform_device *pdev)
{
	int ret;
	struct resource *r;
	struct clk *clk;
	struct ecap_pwm_chip *pc;

	pc = devm_kzalloc(&pdev->dev, sizeof(*pc), GFP_KERNEL);
	if (!pc) {
		dev_err(&pdev->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	clk = devm_clk_get(&pdev->dev, "fck");
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "failed to get clock\n");
		return PTR_ERR(clk);
	}

	pc->clk_rate = clk_get_rate(clk);
	if (!pc->clk_rate) {
		dev_err(&pdev->dev, "failed to get clock rate\n");
		return -EINVAL;
	}

	pc->chip.dev = &pdev->dev;
	pc->chip.ops = &ecap_pwm_ops;
	pc->chip.base = -1;
	pc->chip.npwm = 1;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		dev_err(&pdev->dev, "no memory resource defined\n");
		return -ENODEV;
	}

	pc->mmio_base = devm_request_and_ioremap(&pdev->dev, r);
	if (!pc->mmio_base)
		return -EADDRNOTAVAIL;

	ret = pwmchip_add(&pc->chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "pwmchip_add() failed: %d\n", ret);
		return ret;
	}

	pm_runtime_enable(&pdev->dev);
	platform_set_drvdata(pdev, pc);
	return 0;
}

static int __devexit ecap_pwm_remove(struct platform_device *pdev)
{
	struct ecap_pwm_chip *pc = platform_get_drvdata(pdev);

	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	return pwmchip_remove(&pc->chip);
}

static struct platform_driver ecap_pwm_driver = {
	.driver = {
		.name = "ecap",
	},
	.probe = ecap_pwm_probe,
	.remove = __devexit_p(ecap_pwm_remove),
};

module_platform_driver(ecap_pwm_driver);

MODULE_DESCRIPTION("ECAP PWM driver");
MODULE_AUTHOR("Texas Instruments");
MODULE_LICENSE("GPL");
