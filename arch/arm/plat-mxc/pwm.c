/*
 * simple driver for PWM (Pulse Width Modulator) controller
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Derived from pxa PWM driver by eric miao <eric.miao@marvell.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/pwm.h>

#if defined CONFIG_ARCH_MX1 || defined CONFIG_ARCH_MX21
#define PWM_VER_1

#define PWMCR	0x00	/* PWM Control Register		*/
#define PWMSR	0x04	/* PWM Sample Register		*/
#define PWMPR	0x08	/* PWM Period Register		*/
#define PWMCNR	0x0C	/* PWM Counter Register		*/

#define PWMCR_HCTR		(1 << 18)		/* Halfword FIFO Data Swapping	*/
#define PWMCR_BCTR		(1 << 17)		/* Byte FIFO Data Swapping	*/
#define PWMCR_SWR		(1 << 16)		/* Software Reset		*/
#define PWMCR_CLKSRC_PERCLK	(0 << 15)		/* PERCLK Clock Source		*/
#define PWMCR_CLKSRC_CLK32	(1 << 15)		/* 32KHz Clock Source		*/
#define PWMCR_PRESCALER(x)	(((x - 1) & 0x7F) << 8)	/* PRESCALER			*/
#define PWMCR_IRQ		(1 << 7)		/* Interrupt Request		*/
#define PWMCR_IRQEN		(1 << 6)		/* Interrupt Request Enable	*/
#define PWMCR_FIFOAV		(1 << 5)		/* FIFO Available		*/
#define PWMCR_EN		(1 << 4)		/* Enables/Disables the PWM	*/
#define PWMCR_REPEAT(x)		(((x) & 0x03) << 2)	/* Sample Repeats		*/
#define PWMCR_DIV(x)		(((x) & 0x03) << 0)	/* Clock divider 2/4/8/16	*/

#define MAX_DIV			(128 * 16)
#endif

#if defined CONFIG_MACH_MX27 || defined CONFIG_ARCH_MX31
#define PWM_VER_2

#define PWMCR	0x00	/* PWM Control Register		*/
#define PWMSR	0x04	/* PWM Status Register		*/
#define PWMIR	0x08	/* PWM Interrupt Register	*/
#define PWMSAR	0x0C	/* PWM Sample Register		*/
#define PWMPR	0x10	/* PWM Period Register		*/
#define PWMCNR	0x14	/* PWM Counter Register		*/

#define PWMCR_EN		(1 << 0)		/* Enables/Disables the PWM	*/
#define PWMCR_REPEAT(x)		(((x) & 0x03) << 1)	/* Sample Repeats		*/
#define PWMCR_SWR		(1 << 3)		/* Software Reset		*/
#define PWMCR_PRESCALER(x)	(((x - 1) & 0xFFF) << 4)/* PRESCALER			*/
#define PWMCR_CLKSRC(x)		(((x) & 0x3) << 16)
#define PWMCR_CLKSRC_OFF	(0 << 16)
#define PWMCR_CLKSRC_IPG	(1 << 16)
#define PWMCR_CLKSRC_IPG_HIGH	(2 << 16)
#define PWMCR_CLKSRC_CLK32	(3 << 16)
#define PWMCR_POUTC
#define PWMCR_HCTR		(1 << 20)		/* Halfword FIFO Data Swapping	*/
#define PWMCR_BCTR		(1 << 21)		/* Byte FIFO Data Swapping	*/
#define PWMCR_DBGEN		(1 << 22)		/* Debug Mode			*/
#define PWMCR_WAITEN		(1 << 23)		/* Wait Mode			*/
#define PWMCR_DOZEN		(1 << 24)		/* Doze Mode			*/
#define PWMCR_STOPEN		(1 << 25)		/* Stop Mode			*/
#define PWMCR_FWM(x)		(((x) & 0x3) << 26)	/* FIFO Water Mark		*/

#define MAX_DIV 4096
#endif

#define PWMS_SAMPLE(x)		((x) & 0xFFFF)		/* Contains a two-sample word	*/
#define PWMP_PERIOD(x)		((x) & 0xFFFF)		/* Represents the PWM's period	*/
#define PWMC_COUNTER(x)		((x) & 0xFFFF)		/* Represents the current count value	*/

struct pwm_device {
	struct list_head	node;
	struct platform_device *pdev;

	const char	*label;
	struct clk	*clk;

	int		clk_enabled;
	void __iomem	*mmio_base;

	unsigned int	use_count;
	unsigned int	pwm_id;
};

int pwm_config(struct pwm_device *pwm, int duty_ns, int period_ns)
{
	unsigned long long c;
	unsigned long period_cycles, duty_cycles, prescale;

	if (pwm == NULL || period_ns == 0 || duty_ns > period_ns)
		return -EINVAL;

	c = clk_get_rate(pwm->clk);
	c = c * period_ns;
	do_div(c, 1000000000);
	period_cycles = c;

	prescale = period_cycles / 0x10000 + 1;

	period_cycles /= prescale;
	c = (unsigned long long)period_cycles * duty_ns;
	do_div(c, period_ns);
	duty_cycles = c;

#ifdef PWM_VER_2
	writel(duty_cycles, pwm->mmio_base + PWMSAR);
	writel(period_cycles, pwm->mmio_base + PWMPR);
	writel(PWMCR_PRESCALER(prescale - 1) | PWMCR_CLKSRC_IPG_HIGH | PWMCR_EN,
			pwm->mmio_base + PWMCR);
#elif defined PWM_VER_1
#error PWM not yet working on MX1 / MX21
#endif

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

static int __devinit mxc_pwm_probe(struct platform_device *pdev)
{
	struct pwm_device *pwm;
	struct resource *r;
	int ret = 0;

	pwm = kzalloc(sizeof(struct pwm_device), GFP_KERNEL);
	if (pwm == NULL) {
		dev_err(&pdev->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	pwm->clk = clk_get(&pdev->dev, "pwm");

	if (IS_ERR(pwm->clk)) {
		ret = PTR_ERR(pwm->clk);
		goto err_free;
	}

	pwm->clk_enabled = 0;

	pwm->use_count = 0;
	pwm->pwm_id = pdev->id;
	pwm->pdev = pdev;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (r == NULL) {
		dev_err(&pdev->dev, "no memory resource defined\n");
		ret = -ENODEV;
		goto err_free_clk;
	}

	r = request_mem_region(r->start, r->end - r->start + 1, pdev->name);
	if (r == NULL) {
		dev_err(&pdev->dev, "failed to request memory resource\n");
		ret = -EBUSY;
		goto err_free_clk;
	}

	pwm->mmio_base = ioremap(r->start, r->end - r->start + 1);
	if (pwm->mmio_base == NULL) {
		dev_err(&pdev->dev, "failed to ioremap() registers\n");
		ret = -ENODEV;
		goto err_free_mem;
	}

	mutex_lock(&pwm_lock);
	list_add_tail(&pwm->node, &pwm_list);
	mutex_unlock(&pwm_lock);

	platform_set_drvdata(pdev, pwm);
	return 0;

err_free_mem:
	release_mem_region(r->start, r->end - r->start + 1);
err_free_clk:
	clk_put(pwm->clk);
err_free:
	kfree(pwm);
	return ret;
}

static int __devexit mxc_pwm_remove(struct platform_device *pdev)
{
	struct pwm_device *pwm;
	struct resource *r;

	pwm = platform_get_drvdata(pdev);
	if (pwm == NULL)
		return -ENODEV;

	mutex_lock(&pwm_lock);
	list_del(&pwm->node);
	mutex_unlock(&pwm_lock);

	iounmap(pwm->mmio_base);

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(r->start, r->end - r->start + 1);

	clk_put(pwm->clk);

	kfree(pwm);
	return 0;
}

static struct platform_driver mxc_pwm_driver = {
	.driver		= {
		.name	= "mxc_pwm",
	},
	.probe		= mxc_pwm_probe,
	.remove		= __devexit_p(mxc_pwm_remove),
};

static int __init mxc_pwm_init(void)
{
	return platform_driver_register(&mxc_pwm_driver);
}
arch_initcall(mxc_pwm_init);

static void __exit mxc_pwm_exit(void)
{
	platform_driver_unregister(&mxc_pwm_driver);
}
module_exit(mxc_pwm_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Sascha Hauer <s.hauer@pengutronix.de>");

