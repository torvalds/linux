/* arch/arm/plat-s3c24xx/pwm.c
 *
 * Copyright (c) 2007 Ben Dooks
 * Copyright (c) 2008 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>, <ben-linux@fluff.org>
 *
 * S3C24XX PWM device core
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/pwm.h>

#include <asm/plat-s3c24xx/devs.h>
#include <asm/plat-s3c/regs-timer.h>

struct pwm_device {
	struct list_head	 list;
	struct platform_device	*pdev;

	struct clk		*clk_div;
	struct clk		*clk;
	const char		*label;

	unsigned int		 period_ns;
	unsigned int		 duty_ns;

	unsigned char		 tcon_base;
	unsigned char		 running;
	unsigned char		 use_count;
	unsigned char		 pwm_id;
};

#define pwm_dbg(_pwm, msg...) dev_info(&(_pwm)->pdev->dev, msg)

static struct clk *clk_scaler[2];

/* Standard setup for a timer block. */

#define TIMER_RESOURCE_SIZE (1)

#define TIMER_RESOURCE(_tmr, _irq)			\
	(struct resource [TIMER_RESOURCE_SIZE]) {	\
		[0] = {					\
			.start	= _irq,			\
			.end	= _irq,			\
			.flags	= IORESOURCE_IRQ	\
		}					\
	}

#define DEFINE_TIMER(_tmr_no, _irq)			\
	.name		= "s3c24xx-pwm",		\
	.id		= _tmr_no,			\
	.num_resources	= TIMER_RESOURCE_SIZE,		\
	.resource	= TIMER_RESOURCE(_tmr_no, _irq),	\

/* since we already have an static mapping for the timer, we do not
 * bother setting any IO resource for the base.
 */

struct platform_device s3c_device_timer[] = {
	[0] = { DEFINE_TIMER(0, IRQ_TIMER0) },
	[1] = { DEFINE_TIMER(1, IRQ_TIMER1) },
	[2] = { DEFINE_TIMER(2, IRQ_TIMER2) },
	[3] = { DEFINE_TIMER(3, IRQ_TIMER3) },
	[4] = { DEFINE_TIMER(4, IRQ_TIMER4) },
};

static inline int pwm_is_tdiv(struct pwm_device *pwm)
{
	return clk_get_parent(pwm->clk) == pwm->clk_div;
}

static DEFINE_MUTEX(pwm_lock);
static LIST_HEAD(pwm_list);

struct pwm_device *pwm_request(int pwm_id, const char *label)
{
	struct pwm_device *pwm;
	int found = 0;

	mutex_lock(&pwm_lock);

	list_for_each_entry(pwm, &pwm_list, list) {
		if (pwm->pwm_id == pwm_id) {
			found = 1;
			break;
		}
	}

	if (found) {
		if (pwm->use_count == 0) {
			pwm->use_count = 1;
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
		printk(KERN_ERR "PWM%d device already freed\n", pwm->pwm_id);

	mutex_unlock(&pwm_lock);
}

EXPORT_SYMBOL(pwm_free);

#define pwm_tcon_start(pwm) (1 << (pwm->tcon_base + 0))
#define pwm_tcon_invert(pwm) (1 << (pwm->tcon_base + 2))
#define pwm_tcon_autoreload(pwm) (1 << (pwm->tcon_base + 3))
#define pwm_tcon_manulupdate(pwm) (1 << (pwm->tcon_base + 1))

int pwm_enable(struct pwm_device *pwm)
{
	unsigned long flags;
	unsigned long tcon;

	local_irq_save(flags);

	tcon = __raw_readl(S3C2410_TCON);
	tcon |= pwm_tcon_start(pwm);
	__raw_writel(tcon, S3C2410_TCON);

	local_irq_restore(flags);

	pwm->running = 1;
	return 0;
}

EXPORT_SYMBOL(pwm_enable);

void pwm_disable(struct pwm_device *pwm)
{
	unsigned long flags;
	unsigned long tcon;

	local_irq_save(flags);

	tcon = __raw_readl(S3C2410_TCON);
	tcon &= ~pwm_tcon_start(pwm);
	__raw_writel(tcon, S3C2410_TCON);

	local_irq_restore(flags);

	pwm->running = 0;
}

EXPORT_SYMBOL(pwm_disable);

static unsigned long pwm_calc_tin(struct pwm_device *pwm, unsigned long freq)
{
	unsigned long tin_parent_rate;
	unsigned int div;

	tin_parent_rate = clk_get_rate(clk_get_parent(pwm->clk_div));
	pwm_dbg(pwm, "tin parent at %lu\n", tin_parent_rate);

	for (div = 2; div <= 16; div *= 2) {
		if ((tin_parent_rate / (div << 16)) < freq)
			return tin_parent_rate / div;
	}

	return tin_parent_rate / 16;
}

#define NS_IN_HZ (1000000000UL)

int pwm_config(struct pwm_device *pwm, int duty_ns, int period_ns)
{
	unsigned long tin_rate;
	unsigned long tin_ns;
	unsigned long period;
	unsigned long flags;
	unsigned long tcon;
	unsigned long tcnt;
	long tcmp;

	/* We currently avoid using 64bit arithmetic by using the
	 * fact that anything faster than 1Hz is easily representable
	 * by 32bits. */

	if (period_ns > NS_IN_HZ || duty_ns > NS_IN_HZ)
		return -ERANGE;

	if (duty_ns > period_ns)
		return -EINVAL;

	if (period_ns == pwm->period_ns &&
	    duty_ns == pwm->duty_ns)
		return 0;

	/* The TCMP and TCNT can be read without a lock, they're not
	 * shared between the timers. */

	tcmp = __raw_readl(S3C2410_TCMPB(pwm->pwm_id));
	tcnt = __raw_readl(S3C2410_TCNTB(pwm->pwm_id));

	period = NS_IN_HZ / period_ns;

	pwm_dbg(pwm, "duty_ns=%d, period_ns=%d (%lu)\n",
		duty_ns, period_ns, period);

	/* Check to see if we are changing the clock rate of the PWM */

	if (pwm->period_ns != period_ns) {
		if (pwm_is_tdiv(pwm)) {
			tin_rate = pwm_calc_tin(pwm, period);
			clk_set_rate(pwm->clk_div, tin_rate);
		} else
			tin_rate = clk_get_rate(pwm->clk);

		pwm->period_ns = period_ns;

		pwm_dbg(pwm, "tin_rate=%lu\n", tin_rate);

		tin_ns = NS_IN_HZ / tin_rate;
		tcnt = period_ns / tin_ns;
	} else
		tin_ns = NS_IN_HZ / clk_get_rate(pwm->clk);

	/* Note, counters count down */

	tcmp = duty_ns / tin_ns;
	tcmp = tcnt - tcmp;

	pwm_dbg(pwm, "tin_ns=%lu, tcmp=%ld/%lu\n", tin_ns, tcmp, tcnt);

	if (tcmp < 0)
		tcmp = 0;

	/* Update the PWM register block. */

	local_irq_save(flags);

	__raw_writel(tcmp, S3C2410_TCMPB(pwm->pwm_id));
	__raw_writel(tcnt, S3C2410_TCNTB(pwm->pwm_id));

	tcon = __raw_readl(S3C2410_TCON);
	tcon |= pwm_tcon_manulupdate(pwm);
	tcon |= pwm_tcon_autoreload(pwm);
	__raw_writel(tcon, S3C2410_TCON);

	tcon &= ~pwm_tcon_manulupdate(pwm);
	__raw_writel(tcon, S3C2410_TCON);

	local_irq_restore(flags);

	return 0;
}

EXPORT_SYMBOL(pwm_config);

static int pwm_register(struct pwm_device *pwm)
{
	pwm->duty_ns = -1;
	pwm->period_ns = -1;

	mutex_lock(&pwm_lock);
	list_add_tail(&pwm->list, &pwm_list);
	mutex_unlock(&pwm_lock);

	return 0;
}

static int s3c_pwm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pwm_device *pwm;
	unsigned long flags;
	unsigned long tcon;
	unsigned int id = pdev->id;
	int ret;

	if (id == 4) {
		dev_err(dev, "TIMER4 is currently not supported\n");
		return -ENXIO;
	}

	pwm = kzalloc(sizeof(struct pwm_device), GFP_KERNEL);
	if (pwm == NULL) {
		dev_err(dev, "failed to allocate pwm_device\n");
		return -ENOMEM;
	}

	pwm->pdev = pdev;
	pwm->pwm_id = id;

	/* calculate base of control bits in TCON */
	pwm->tcon_base = id == 0 ? 0 : (id * 4) + 4;

	pwm->clk = clk_get(dev, "pwm-tin");
	if (IS_ERR(pwm->clk)) {
		dev_err(dev, "failed to get pwm tin clk\n");
		ret = PTR_ERR(pwm->clk);
		goto err_alloc;
	}

	pwm->clk_div = clk_get(dev, "pwm-tdiv");
	if (IS_ERR(pwm->clk_div)) {
		dev_err(dev, "failed to get pwm tdiv clk\n");
		ret = PTR_ERR(pwm->clk_div);
		goto err_clk_tin;
	}

	local_irq_save(flags);

	tcon = __raw_readl(S3C2410_TCON);
	tcon |= pwm_tcon_invert(pwm);
	__raw_writel(tcon, S3C2410_TCON);

	local_irq_restore(flags);


	ret = pwm_register(pwm);
	if (ret) {
		dev_err(dev, "failed to register pwm\n");
		goto err_clk_tdiv;
	}

	pwm_dbg(pwm, "config bits %02x\n",
		(__raw_readl(S3C2410_TCON) >> pwm->tcon_base) & 0x0f);

	dev_info(dev, "tin at %lu, tdiv at %lu, tin=%sclk, base %d\n",
		 clk_get_rate(pwm->clk),
		 clk_get_rate(pwm->clk_div),
		 pwm_is_tdiv(pwm) ? "div" : "ext", pwm->tcon_base);

	platform_set_drvdata(pdev, pwm);
	return 0;

 err_clk_tdiv:
	clk_put(pwm->clk_div);

 err_clk_tin:
	clk_put(pwm->clk);

 err_alloc:
	kfree(pwm);
	return ret;
}

static int s3c_pwm_remove(struct platform_device *pdev)
{
	struct pwm_device *pwm = platform_get_drvdata(pdev);

	clk_put(pwm->clk_div);
	clk_put(pwm->clk);
	kfree(pwm);

	return 0;
}

static struct platform_driver s3c_pwm_driver = {
	.driver		= {
		.name	= "s3c24xx-pwm",
		.owner	= THIS_MODULE,
	},
	.probe		= s3c_pwm_probe,
	.remove		= __devexit_p(s3c_pwm_remove),
};

static int __init pwm_init(void)
{
	int ret;

	clk_scaler[0] = clk_get(NULL, "pwm-scaler0");
	clk_scaler[1] = clk_get(NULL, "pwm-scaler1");

	if (IS_ERR(clk_scaler[0]) || IS_ERR(clk_scaler[1])) {
		printk(KERN_ERR "%s: failed to get scaler clocks\n", __func__);
		return -EINVAL;
	}

	ret = platform_driver_register(&s3c_pwm_driver);
	if (ret)
		printk(KERN_ERR "%s: failed to add pwm driver\n", __func__);

	return ret;
}

arch_initcall(pwm_init);
