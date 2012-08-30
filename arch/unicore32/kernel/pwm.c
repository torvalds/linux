/*
 * linux/arch/unicore32/kernel/pwm.c
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 *	Maintained by GUAN Xue-tao <gxt@mprc.pku.edu.cn>
 *	Copyright (C) 2001-2010 Guan Xuetao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/pwm.h>

#include <asm/div64.h>
#include <mach/hardware.h>

struct pwm_device {
	struct list_head	node;
	struct platform_device *pdev;

	void __iomem	*base;

	const char	*label;
	struct clk	*clk;
	int		clk_enabled;

	unsigned int	use_count;
	unsigned int	pwm_id;
};

/*
 * period_ns = 10^9 * (PRESCALE + 1) * (PV + 1) / PWM_CLK_RATE
 * duty_ns   = 10^9 * (PRESCALE + 1) * DC / PWM_CLK_RATE
 */
int pwm_config(struct pwm_device *pwm, int duty_ns, int period_ns)
{
	unsigned long long c;
	unsigned long period_cycles, prescale, pv, dc;

	if (pwm == NULL || period_ns == 0 || duty_ns > period_ns)
		return -EINVAL;

	c = clk_get_rate(pwm->clk);
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
		dc = OST_PWMDCCR_FDCYCLE;
	else
		dc = (pv + 1) * duty_ns / period_ns;

	/* NOTE: the clock to PWM has to be enabled first
	 * before writing to the registers
	 */
	clk_enable(pwm->clk);

	writel(prescale, pwm->base + OST_PWM_PWCR);
	writel(pv - dc, pwm->base + OST_PWM_DCCR);
	writel(pv, pwm->base + OST_PWM_PCR);

	clk_disable(pwm->clk);

	return 0;
}
EXPORT_SYMBOL(pwm_config);

int pwm_enable(struct pwm_device *pwm)
{
	int rc = 0;

	if (!pwm->clk_enabled) {
		rc = clk_enable(pwm->clk);
		if (!rc)
			pwm->clk_enabled = 1;
	}
	return rc;
}
EXPORT_SYMBOL(pwm_enable);

void pwm_disable(struct pwm_device *pwm)
{
	if (pwm->clk_enabled) {
		clk_disable(pwm->clk);
		pwm->clk_enabled = 0;
	}
}
EXPORT_SYMBOL(pwm_disable);

static DEFINE_MUTEX(pwm_lock);
static LIST_HEAD(pwm_list);

struct pwm_device *pwm_request(int pwm_id, const char *label)
{
	struct pwm_device *pwm;
	int found = 0;

	mutex_lock(&pwm_lock);

	list_for_each_entry(pwm, &pwm_list, node) {
		if (pwm->pwm_id == pwm_id) {
			found = 1;
			break;
		}
	}

	if (found) {
		if (pwm->use_count == 0) {
			pwm->use_count++;
			pwm->label = label;
		} else
			pwm = ERR_PTR(-EBUSY);
	} else
		pwm = ERR_PTR(-ENOENT);

	mutex_unlock(&pwm_lock);
	return pwm;
}
EXPORT_SYMBOL(pwm_request);

void pwm_free(struct pwm_device *pwm)
{
	mutex_lock(&pwm_lock);

	if (pwm->use_count) {
		pwm->use_count--;
		pwm->label = NULL;
	} else
		pr_warning("PWM device already freed\n");

	mutex_unlock(&pwm_lock);
}
EXPORT_SYMBOL(pwm_free);

static inline void __add_pwm(struct pwm_device *pwm)
{
	mutex_lock(&pwm_lock);
	list_add_tail(&pwm->node, &pwm_list);
	mutex_unlock(&pwm_lock);
}

static int __devinit pwm_probe(struct platform_device *pdev)
{
	struct pwm_device *pwm;
	struct resource *r;

	pwm = devm_kzalloc(&pdev->dev, sizeof(struct pwm_device), GFP_KERNEL);
	if (pwm == NULL) {
		dev_err(&pdev->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	pwm->clk = devm_clk_get(&pdev->dev, "OST_CLK");
	if (IS_ERR(pwm->clk))
		return PTR_ERR(pwm->clk);

	pwm->clk_enabled = 0;

	pwm->use_count = 0;
	pwm->pwm_id = pdev->id;
	pwm->pdev = pdev;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (r == NULL) {
		dev_err(&pdev->dev, "no memory resource defined\n");
		return -ENODEV;
	}

	pwm->base = devm_request_and_ioremap(&pdev->dev, r);
	if (pwm->base == NULL)
		return -EADDRNOTAVAIL;

	__add_pwm(pwm);
	platform_set_drvdata(pdev, pwm);
	return 0;
}

static int __devexit pwm_remove(struct platform_device *pdev)
{
	struct pwm_device *pwm;

	pwm = platform_get_drvdata(pdev);
	if (pwm == NULL)
		return -ENODEV;

	mutex_lock(&pwm_lock);
	list_del(&pwm->node);
	mutex_unlock(&pwm_lock);

	return 0;
}

static struct platform_driver puv3_pwm_driver = {
	.driver		= {
		.name	= "PKUnity-v3-PWM",
	},
	.probe		= pwm_probe,
	.remove		= __devexit_p(pwm_remove),
};
module_platform_driver(puv3_pwm_driver);

MODULE_LICENSE("GPL v2");
