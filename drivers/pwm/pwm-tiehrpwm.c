/*
 * EHRPWM PWM driver
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
#include <linux/pwm.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/pm_runtime.h>
#include <linux/of_device.h>

#include "pwm-tipwmss.h"

/* EHRPWM registers and bits definitions */

/* Time base module registers */
#define TBCTL			0x00
#define TBPRD			0x0A

#define TBCTL_RUN_MASK		(BIT(15) | BIT(14))
#define TBCTL_STOP_NEXT		0
#define TBCTL_STOP_ON_CYCLE	BIT(14)
#define TBCTL_FREE_RUN		(BIT(15) | BIT(14))
#define TBCTL_PRDLD_MASK	BIT(3)
#define TBCTL_PRDLD_SHDW	0
#define TBCTL_PRDLD_IMDT	BIT(3)
#define TBCTL_CLKDIV_MASK	(BIT(12) | BIT(11) | BIT(10) | BIT(9) | \
				BIT(8) | BIT(7))
#define TBCTL_CTRMODE_MASK	(BIT(1) | BIT(0))
#define TBCTL_CTRMODE_UP	0
#define TBCTL_CTRMODE_DOWN	BIT(0)
#define TBCTL_CTRMODE_UPDOWN	BIT(1)
#define TBCTL_CTRMODE_FREEZE	(BIT(1) | BIT(0))

#define TBCTL_HSPCLKDIV_SHIFT	7
#define TBCTL_CLKDIV_SHIFT	10

#define CLKDIV_MAX		7
#define HSPCLKDIV_MAX		7
#define PERIOD_MAX		0xFFFF

/* compare module registers */
#define CMPA			0x12
#define CMPB			0x14

/* Action qualifier module registers */
#define AQCTLA			0x16
#define AQCTLB			0x18
#define AQSFRC			0x1A
#define AQCSFRC			0x1C

#define AQCTL_CBU_MASK		(BIT(9) | BIT(8))
#define AQCTL_CBU_FRCLOW	BIT(8)
#define AQCTL_CBU_FRCHIGH	BIT(9)
#define AQCTL_CBU_FRCTOGGLE	(BIT(9) | BIT(8))
#define AQCTL_CAU_MASK		(BIT(5) | BIT(4))
#define AQCTL_CAU_FRCLOW	BIT(4)
#define AQCTL_CAU_FRCHIGH	BIT(5)
#define AQCTL_CAU_FRCTOGGLE	(BIT(5) | BIT(4))
#define AQCTL_PRD_MASK		(BIT(3) | BIT(2))
#define AQCTL_PRD_FRCLOW	BIT(2)
#define AQCTL_PRD_FRCHIGH	BIT(3)
#define AQCTL_PRD_FRCTOGGLE	(BIT(3) | BIT(2))
#define AQCTL_ZRO_MASK		(BIT(1) | BIT(0))
#define AQCTL_ZRO_FRCLOW	BIT(0)
#define AQCTL_ZRO_FRCHIGH	BIT(1)
#define AQCTL_ZRO_FRCTOGGLE	(BIT(1) | BIT(0))

#define AQCTL_CHANA_POLNORMAL	(AQCTL_CAU_FRCLOW | AQCTL_PRD_FRCHIGH | \
				AQCTL_ZRO_FRCHIGH)
#define AQCTL_CHANA_POLINVERSED	(AQCTL_CAU_FRCHIGH | AQCTL_PRD_FRCLOW | \
				AQCTL_ZRO_FRCLOW)
#define AQCTL_CHANB_POLNORMAL	(AQCTL_CBU_FRCLOW | AQCTL_PRD_FRCHIGH | \
				AQCTL_ZRO_FRCHIGH)
#define AQCTL_CHANB_POLINVERSED	(AQCTL_CBU_FRCHIGH | AQCTL_PRD_FRCLOW | \
				AQCTL_ZRO_FRCLOW)

#define AQSFRC_RLDCSF_MASK	(BIT(7) | BIT(6))
#define AQSFRC_RLDCSF_ZRO	0
#define AQSFRC_RLDCSF_PRD	BIT(6)
#define AQSFRC_RLDCSF_ZROPRD	BIT(7)
#define AQSFRC_RLDCSF_IMDT	(BIT(7) | BIT(6))

#define AQCSFRC_CSFB_MASK	(BIT(3) | BIT(2))
#define AQCSFRC_CSFB_FRCDIS	0
#define AQCSFRC_CSFB_FRCLOW	BIT(2)
#define AQCSFRC_CSFB_FRCHIGH	BIT(3)
#define AQCSFRC_CSFB_DISSWFRC	(BIT(3) | BIT(2))
#define AQCSFRC_CSFA_MASK	(BIT(1) | BIT(0))
#define AQCSFRC_CSFA_FRCDIS	0
#define AQCSFRC_CSFA_FRCLOW	BIT(0)
#define AQCSFRC_CSFA_FRCHIGH	BIT(1)
#define AQCSFRC_CSFA_DISSWFRC	(BIT(1) | BIT(0))

#define NUM_PWM_CHANNEL		2	/* EHRPWM channels */

struct ehrpwm_pwm_chip {
	struct pwm_chip	chip;
	unsigned int	clk_rate;
	void __iomem	*mmio_base;
	unsigned long period_cycles[NUM_PWM_CHANNEL];
	enum pwm_polarity polarity[NUM_PWM_CHANNEL];
	struct	clk	*tbclk;
};

static inline struct ehrpwm_pwm_chip *to_ehrpwm_pwm_chip(struct pwm_chip *chip)
{
	return container_of(chip, struct ehrpwm_pwm_chip, chip);
}

static void ehrpwm_write(void *base, int offset, unsigned int val)
{
	writew(val & 0xFFFF, base + offset);
}

static void ehrpwm_modify(void *base, int offset,
		unsigned short mask, unsigned short val)
{
	unsigned short regval;

	regval = readw(base + offset);
	regval &= ~mask;
	regval |= val & mask;
	writew(regval, base + offset);
}

/**
 * set_prescale_div -	Set up the prescaler divider function
 * @rqst_prescaler:	prescaler value min
 * @prescale_div:	prescaler value set
 * @tb_clk_div:		Time Base Control prescaler bits
 */
static int set_prescale_div(unsigned long rqst_prescaler,
		unsigned short *prescale_div, unsigned short *tb_clk_div)
{
	unsigned int clkdiv, hspclkdiv;

	for (clkdiv = 0; clkdiv <= CLKDIV_MAX; clkdiv++) {
		for (hspclkdiv = 0; hspclkdiv <= HSPCLKDIV_MAX; hspclkdiv++) {

			/*
			 * calculations for prescaler value :
			 * prescale_div = HSPCLKDIVIDER * CLKDIVIDER.
			 * HSPCLKDIVIDER =  2 ** hspclkdiv
			 * CLKDIVIDER = (1),		if clkdiv == 0 *OR*
			 *		(2 * clkdiv),	if clkdiv != 0
			 *
			 * Configure prescale_div value such that period
			 * register value is less than 65535.
			 */

			*prescale_div = (1 << clkdiv) *
					(hspclkdiv ? (hspclkdiv * 2) : 1);
			if (*prescale_div > rqst_prescaler) {
				*tb_clk_div = (clkdiv << TBCTL_CLKDIV_SHIFT) |
					(hspclkdiv << TBCTL_HSPCLKDIV_SHIFT);
				return 0;
			}
		}
	}
	return 1;
}

static void configure_polarity(struct ehrpwm_pwm_chip *pc, int chan)
{
	int aqctl_reg;
	unsigned short aqctl_val, aqctl_mask;

	/*
	 * Configure PWM output to HIGH/LOW level on counter
	 * reaches compare register value and LOW/HIGH level
	 * on counter value reaches period register value and
	 * zero value on counter
	 */
	if (chan == 1) {
		aqctl_reg = AQCTLB;
		aqctl_mask = AQCTL_CBU_MASK;

		if (pc->polarity[chan] == PWM_POLARITY_INVERSED)
			aqctl_val = AQCTL_CHANB_POLINVERSED;
		else
			aqctl_val = AQCTL_CHANB_POLNORMAL;
	} else {
		aqctl_reg = AQCTLA;
		aqctl_mask = AQCTL_CAU_MASK;

		if (pc->polarity[chan] == PWM_POLARITY_INVERSED)
			aqctl_val = AQCTL_CHANA_POLINVERSED;
		else
			aqctl_val = AQCTL_CHANA_POLNORMAL;
	}

	aqctl_mask |= AQCTL_PRD_MASK | AQCTL_ZRO_MASK;
	ehrpwm_modify(pc->mmio_base, aqctl_reg, aqctl_mask, aqctl_val);
}

/*
 * period_ns = 10^9 * (ps_divval * period_cycles) / PWM_CLK_RATE
 * duty_ns   = 10^9 * (ps_divval * duty_cycles) / PWM_CLK_RATE
 */
static int ehrpwm_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
		int duty_ns, int period_ns)
{
	struct ehrpwm_pwm_chip *pc = to_ehrpwm_pwm_chip(chip);
	unsigned long long c;
	unsigned long period_cycles, duty_cycles;
	unsigned short ps_divval, tb_divval;
	int i, cmp_reg;

	if (period_ns > NSEC_PER_SEC)
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

	/*
	 * Period values should be same for multiple PWM channels as IP uses
	 * same period register for multiple channels.
	 */
	for (i = 0; i < NUM_PWM_CHANNEL; i++) {
		if (pc->period_cycles[i] &&
				(pc->period_cycles[i] != period_cycles)) {
			/*
			 * Allow channel to reconfigure period if no other
			 * channels being configured.
			 */
			if (i == pwm->hwpwm)
				continue;

			dev_err(chip->dev, "Period value conflicts with channel %d\n",
					i);
			return -EINVAL;
		}
	}

	pc->period_cycles[pwm->hwpwm] = period_cycles;

	/* Configure clock prescaler to support Low frequency PWM wave */
	if (set_prescale_div(period_cycles/PERIOD_MAX, &ps_divval,
				&tb_divval)) {
		dev_err(chip->dev, "Unsupported values\n");
		return -EINVAL;
	}

	pm_runtime_get_sync(chip->dev);

	/* Update clock prescaler values */
	ehrpwm_modify(pc->mmio_base, TBCTL, TBCTL_CLKDIV_MASK, tb_divval);

	/* Update period & duty cycle with presacler division */
	period_cycles = period_cycles / ps_divval;
	duty_cycles = duty_cycles / ps_divval;

	/* Configure shadow loading on Period register */
	ehrpwm_modify(pc->mmio_base, TBCTL, TBCTL_PRDLD_MASK, TBCTL_PRDLD_SHDW);

	ehrpwm_write(pc->mmio_base, TBPRD, period_cycles);

	/* Configure ehrpwm counter for up-count mode */
	ehrpwm_modify(pc->mmio_base, TBCTL, TBCTL_CTRMODE_MASK,
			TBCTL_CTRMODE_UP);

	if (pwm->hwpwm == 1)
		/* Channel 1 configured with compare B register */
		cmp_reg = CMPB;
	else
		/* Channel 0 configured with compare A register */
		cmp_reg = CMPA;

	ehrpwm_write(pc->mmio_base, cmp_reg, duty_cycles);

	pm_runtime_put_sync(chip->dev);
	return 0;
}

static int ehrpwm_pwm_set_polarity(struct pwm_chip *chip,
		struct pwm_device *pwm,	enum pwm_polarity polarity)
{
	struct ehrpwm_pwm_chip *pc = to_ehrpwm_pwm_chip(chip);

	/* Configuration of polarity in hardware delayed, do at enable */
	pc->polarity[pwm->hwpwm] = polarity;
	return 0;
}

static int ehrpwm_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct ehrpwm_pwm_chip *pc = to_ehrpwm_pwm_chip(chip);
	unsigned short aqcsfrc_val, aqcsfrc_mask;

	/* Leave clock enabled on enabling PWM */
	pm_runtime_get_sync(chip->dev);

	/* Disabling Action Qualifier on PWM output */
	if (pwm->hwpwm) {
		aqcsfrc_val = AQCSFRC_CSFB_FRCDIS;
		aqcsfrc_mask = AQCSFRC_CSFB_MASK;
	} else {
		aqcsfrc_val = AQCSFRC_CSFA_FRCDIS;
		aqcsfrc_mask = AQCSFRC_CSFA_MASK;
	}

	/* Changes to shadow mode */
	ehrpwm_modify(pc->mmio_base, AQSFRC, AQSFRC_RLDCSF_MASK,
			AQSFRC_RLDCSF_ZRO);

	ehrpwm_modify(pc->mmio_base, AQCSFRC, aqcsfrc_mask, aqcsfrc_val);

	/* Channels polarity can be configured from action qualifier module */
	configure_polarity(pc, pwm->hwpwm);

	/* Enable TBCLK before enabling PWM device */
	clk_enable(pc->tbclk);

	/* Enable time counter for free_run */
	ehrpwm_modify(pc->mmio_base, TBCTL, TBCTL_RUN_MASK, TBCTL_FREE_RUN);
	return 0;
}

static void ehrpwm_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct ehrpwm_pwm_chip *pc = to_ehrpwm_pwm_chip(chip);
	unsigned short aqcsfrc_val, aqcsfrc_mask;

	/* Action Qualifier puts PWM output low forcefully */
	if (pwm->hwpwm) {
		aqcsfrc_val = AQCSFRC_CSFB_FRCLOW;
		aqcsfrc_mask = AQCSFRC_CSFB_MASK;
	} else {
		aqcsfrc_val = AQCSFRC_CSFA_FRCLOW;
		aqcsfrc_mask = AQCSFRC_CSFA_MASK;
	}

	/*
	 * Changes to immediate action on Action Qualifier. This puts
	 * Action Qualifier control on PWM output from next TBCLK
	 */
	ehrpwm_modify(pc->mmio_base, AQSFRC, AQSFRC_RLDCSF_MASK,
			AQSFRC_RLDCSF_IMDT);

	ehrpwm_modify(pc->mmio_base, AQCSFRC, aqcsfrc_mask, aqcsfrc_val);

	/* Disabling TBCLK on PWM disable */
	clk_disable(pc->tbclk);

	/* Stop Time base counter */
	ehrpwm_modify(pc->mmio_base, TBCTL, TBCTL_RUN_MASK, TBCTL_STOP_NEXT);

	/* Disable clock on PWM disable */
	pm_runtime_put_sync(chip->dev);
}

static void ehrpwm_pwm_free(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct ehrpwm_pwm_chip *pc = to_ehrpwm_pwm_chip(chip);

	if (test_bit(PWMF_ENABLED, &pwm->flags)) {
		dev_warn(chip->dev, "Removing PWM device without disabling\n");
		pm_runtime_put_sync(chip->dev);
	}

	/* set period value to zero on free */
	pc->period_cycles[pwm->hwpwm] = 0;
}

static const struct pwm_ops ehrpwm_pwm_ops = {
	.free		= ehrpwm_pwm_free,
	.config		= ehrpwm_pwm_config,
	.set_polarity	= ehrpwm_pwm_set_polarity,
	.enable		= ehrpwm_pwm_enable,
	.disable	= ehrpwm_pwm_disable,
	.owner		= THIS_MODULE,
};

static const struct of_device_id ehrpwm_of_match[] = {
	{ .compatible	= "ti,am33xx-ehrpwm" },
	{},
};
MODULE_DEVICE_TABLE(of, ehrpwm_of_match);

static int __devinit ehrpwm_pwm_probe(struct platform_device *pdev)
{
	int ret;
	struct resource *r;
	struct clk *clk;
	struct ehrpwm_pwm_chip *pc;
	u16 status;

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
	pc->chip.ops = &ehrpwm_pwm_ops;
	pc->chip.of_xlate = of_pwm_xlate_with_flags;
	pc->chip.of_pwm_n_cells = 3;
	pc->chip.base = -1;
	pc->chip.npwm = NUM_PWM_CHANNEL;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		dev_err(&pdev->dev, "no memory resource defined\n");
		return -ENODEV;
	}

	pc->mmio_base = devm_request_and_ioremap(&pdev->dev, r);
	if (!pc->mmio_base)
		return  -EADDRNOTAVAIL;

	/* Acquire tbclk for Time Base EHRPWM submodule */
	pc->tbclk = devm_clk_get(&pdev->dev, "tbclk");
	if (IS_ERR(pc->tbclk)) {
		dev_err(&pdev->dev, "Failed to get tbclk\n");
		return PTR_ERR(pc->tbclk);
	}

	ret = pwmchip_add(&pc->chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "pwmchip_add() failed: %d\n", ret);
		return ret;
	}

	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	status = pwmss_submodule_state_change(pdev->dev.parent,
			PWMSS_EPWMCLK_EN);
	if (!(status & PWMSS_EPWMCLK_EN_ACK)) {
		dev_err(&pdev->dev, "PWMSS config space clock enable failed\n");
		ret = -EINVAL;
		goto pwmss_clk_failure;
	}

	pm_runtime_put_sync(&pdev->dev);

	platform_set_drvdata(pdev, pc);
	return 0;

pwmss_clk_failure:
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	pwmchip_remove(&pc->chip);
	return ret;
}

static int __devexit ehrpwm_pwm_remove(struct platform_device *pdev)
{
	struct ehrpwm_pwm_chip *pc = platform_get_drvdata(pdev);

	pm_runtime_get_sync(&pdev->dev);
	/*
	 * Due to hardware misbehaviour, acknowledge of the stop_req
	 * is missing. Hence checking of the status bit skipped.
	 */
	pwmss_submodule_state_change(pdev->dev.parent, PWMSS_EPWMCLK_STOP_REQ);
	pm_runtime_put_sync(&pdev->dev);

	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	return pwmchip_remove(&pc->chip);
}

static struct platform_driver ehrpwm_pwm_driver = {
	.driver = {
		.name	= "ehrpwm",
		.owner	= THIS_MODULE,
		.of_match_table = ehrpwm_of_match,
	},
	.probe = ehrpwm_pwm_probe,
	.remove = __devexit_p(ehrpwm_pwm_remove),
};

module_platform_driver(ehrpwm_pwm_driver);

MODULE_DESCRIPTION("EHRPWM PWM driver");
MODULE_AUTHOR("Texas Instruments");
MODULE_LICENSE("GPL");
