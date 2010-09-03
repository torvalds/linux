/*
 * arch/arm/mach-tegra/pwm.c
 *
 * Tegra pulse-width-modulation controller driver
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 * Based on arch/arm/plat-mxc/pwm.c by Sascha Hauer <s.hauer@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/slab.h>

#define PWM_ENABLE	(1 << 31)
#define PWM_DUTY_WIDTH	8
#define PWM_DUTY_SHIFT	16
#define PWM_SCALE_WIDTH	13
#define PWM_SCALE_SHIFT	0

struct pwm_device {
	struct list_head	node;
	struct platform_device	*pdev;

	const char		*label;
	struct clk		*clk;

	int			clk_enb;
	void __iomem		*mmio_base;

	unsigned int		in_use;
	unsigned int		id;
};

static DEFINE_MUTEX(pwm_lock);
static LIST_HEAD(pwm_list);

static inline int pwm_writel(struct pwm_device *pwm, unsigned long val)
{
	int rc;

	rc = clk_enable(pwm->clk);
	if (WARN_ON(rc))
		return rc;
	writel(val, pwm->mmio_base);
	clk_disable(pwm->clk);
	return 0;
}

int pwm_config(struct pwm_device *pwm, int duty_ns, int period_ns)
{
	unsigned long long c;
	unsigned long rate, hz;
	u32 val = 0;

	/* convert from duty_ns / period_ns to a fixed number of duty
	 * ticks per (1 << PWM_DUTY_WIDTH) cycles. */
	c = duty_ns * ((1 << PWM_DUTY_WIDTH) - 1);
	do_div(c, period_ns);

	val = (u32)c << PWM_DUTY_SHIFT;

	/* compute the prescaler value for which (1 << PWM_DUTY_WIDTH)
	 * cycles at the PWM clock rate will take period_ns nanoseconds. */
	rate = clk_get_rate(pwm->clk) >> PWM_DUTY_WIDTH;
	hz = 1000000000ul / period_ns;

	rate = (rate + (hz / 2)) / hz;

	if (rate >> PWM_SCALE_WIDTH)
		return -EINVAL;

	val |= (rate << PWM_SCALE_SHIFT);

	/* the struct clk may be shared across multiple PWM devices, so
	 * only enable the PWM if this device has been enabled */
	if (pwm->clk_enb)
		val |= PWM_ENABLE;

	return pwm_writel(pwm, val);
}
EXPORT_SYMBOL(pwm_config);

int pwm_enable(struct pwm_device *pwm)
{
	int rc = 0;

	mutex_lock(&pwm_lock);
	if (!pwm->clk_enb) {
		rc = clk_enable(pwm->clk);
		if (!rc) {
			u32 val = readl(pwm->mmio_base);
			writel(val | PWM_ENABLE, pwm->mmio_base);
			pwm->clk_enb = 1;
		}
	}
	mutex_unlock(&pwm_lock);

	return rc;
}
EXPORT_SYMBOL(pwm_enable);

void pwm_disable(struct pwm_device *pwm)
{
	mutex_lock(&pwm_lock);
	if (pwm->clk_enb) {
		u32 val = readl(pwm->mmio_base);
		writel(val & ~PWM_ENABLE, pwm->mmio_base);
		clk_disable(pwm->clk);
		pwm->clk_enb = 0;
	} else
		dev_warn(&pwm->pdev->dev, "%s called on disabled PWM\n",
			 __func__);
	mutex_unlock(&pwm_lock);
}
EXPORT_SYMBOL(pwm_disable);

struct pwm_device *pwm_request(int pwm_id, const char *label)
{
	struct pwm_device *pwm;
	int found = 0;

	mutex_lock(&pwm_lock);

	list_for_each_entry(pwm, &pwm_list, node) {
		if (pwm->id == pwm_id) {
			found = 1;
			break;
		}
	}

	if (found) {
		if (!pwm->in_use) {
			pwm->in_use = 1;
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
	if (pwm->in_use) {
		pwm->in_use = 0;
		pwm->label = NULL;
	} else
		dev_warn(&pwm->pdev->dev, "PWM device already freed\n");

	mutex_unlock(&pwm_lock);
}
EXPORT_SYMBOL(pwm_free);

static int tegra_pwm_probe(struct platform_device *pdev)
{
	struct pwm_device *pwm;
	struct resource *r;
	int ret;

	pwm = kzalloc(sizeof(*pwm), GFP_KERNEL);
	if (!pwm) {
		dev_err(&pdev->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}
	pwm->clk = clk_get(&pdev->dev, NULL);

	if (IS_ERR(pwm->clk)) {
		ret = PTR_ERR(pwm->clk);
		goto err_free;
	}

	pwm->clk_enb = 0;
	pwm->in_use = 0;
	pwm->id = pdev->id;
	pwm->pdev = pdev;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		dev_err(&pdev->dev, "no memory resources defined\n");
		ret = -ENODEV;
		goto err_put_clk;
	}

	r = request_mem_region(r->start, resource_size(r), pdev->name);
	if (!r) {
		dev_err(&pdev->dev, "failed to request memory\n");
		ret = -EBUSY;
		goto err_put_clk;
	}

	pwm->mmio_base = ioremap(r->start, resource_size(r));
	if (!pwm->mmio_base) {
		dev_err(&pdev->dev, "failed to ioremap() region\n");
		ret = -ENODEV;
		goto err_free_mem;
	}

	platform_set_drvdata(pdev, pwm);

	mutex_lock(&pwm_lock);
	list_add_tail(&pwm->node, &pwm_list);
	mutex_unlock(&pwm_lock);

	return 0;

err_free_mem:
	release_mem_region(r->start, resource_size(r));
err_put_clk:
	clk_put(pwm->clk);
err_free:
	kfree(pwm);
	return ret;
}

static int __devexit tegra_pwm_remove(struct platform_device *pdev)
{
	struct pwm_device *pwm = platform_get_drvdata(pdev);
	struct resource *r;
	int rc;

	if (WARN_ON(!pwm))
		return -ENODEV;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	mutex_lock(&pwm_lock);
	if (pwm->in_use) {
		mutex_unlock(&pwm_lock);
		return -EBUSY;
	}
	list_del(&pwm->node);
	mutex_unlock(&pwm_lock);

	rc = pwm_writel(pwm, 0);

	iounmap(pwm->mmio_base);
	release_mem_region(r->start, resource_size(r));

	if (pwm->clk_enb)
		clk_disable(pwm->clk);

	clk_put(pwm->clk);

	kfree(pwm);
	return rc;
}

static struct platform_driver tegra_pwm_driver = {
	.driver		= {
		.name	= "tegra_pwm",
	},
	.probe		= tegra_pwm_probe,
	.remove		= __devexit_p(tegra_pwm_remove),
};

static int __init tegra_pwm_init(void)
{
	return platform_driver_register(&tegra_pwm_driver);
}
subsys_initcall(tegra_pwm_init);

static void __exit tegra_pwm_exit(void)
{
	platform_driver_unregister(&tegra_pwm_driver);
}
module_exit(tegra_pwm_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("NVIDIA Corporation");
