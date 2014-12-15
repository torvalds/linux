/* arch/arm/plat-samsung/adc.c
 *
 * Copyright (c) 2008 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>, <ben-linux@fluff.org>
 *
 * Samsung ADC device core
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/regulator/consumer.h>

#include <plat/regs-adc.h>
#include <plat/adc.h>

/* This driver is designed to control the usage of the ADC block between
 * the touchscreen and any other drivers that may need to use it, such as
 * the hwmon driver.
 *
 * Priority will be given to the touchscreen driver, but as this itself is
 * rate limited it should not starve other requests which are processed in
 * order that they are received.
 *
 * Each user registers to get a client block which uniquely identifies it
 * and stores information such as the necessary functions to callback when
 * action is required.
 */

enum s3c_cpu_type {
	TYPE_ADCV1, /* S3C24XX */
	TYPE_ADCV11, /* S3C2443 */
	TYPE_ADCV12, /* S3C2416, S3C2450 */
	TYPE_ADCV2, /* S3C64XX */
	TYPE_ADCV3, /* S5PV210, S5PC110, EXYNOS4210 */
};

struct s3c_adc_client {
	struct platform_device	*pdev;
	struct list_head	 pend;
	wait_queue_head_t	*wait;

	unsigned int		 nr_samples;
	int			 result;
	unsigned char		 is_ts;
	unsigned char		 channel;

	void	(*select_cb)(struct s3c_adc_client *c, unsigned selected);
	void	(*convert_cb)(struct s3c_adc_client *c,
			      unsigned val1, unsigned val2,
			      unsigned *samples_left);
};

struct adc_device {
	struct platform_device	*pdev;
	struct platform_device	*owner;
	struct clk		*clk;
	struct s3c_adc_client	*cur;
	struct s3c_adc_client	*ts_pend;
	void __iomem		*regs;
	spinlock_t		 lock;

	unsigned int		 prescale;

	int			 irq;
	struct regulator	*vdd;
};

static struct adc_device *adc_dev;

static LIST_HEAD(adc_pending);	/* protected by adc_device.lock */

#define adc_dbg(_adc, msg...) dev_dbg(&(_adc)->pdev->dev, msg)

static inline void s3c_adc_convert(struct adc_device *adc)
{
	unsigned con = readl(adc->regs + S3C2410_ADCCON);

	con |= S3C2410_ADCCON_ENABLE_START;
	writel(con, adc->regs + S3C2410_ADCCON);
}

static inline void s3c_adc_select(struct adc_device *adc,
				  struct s3c_adc_client *client)
{
	unsigned con = readl(adc->regs + S3C2410_ADCCON);
	enum s3c_cpu_type cpu = platform_get_device_id(adc->pdev)->driver_data;

	client->select_cb(client, 1);

	if (cpu == TYPE_ADCV1 || cpu == TYPE_ADCV2)
		con &= ~S3C2410_ADCCON_MUXMASK;
	con &= ~S3C2410_ADCCON_STDBM;
	con &= ~S3C2410_ADCCON_STARTMASK;

	if (!client->is_ts) {
		if (cpu == TYPE_ADCV3)
			writel(client->channel & 0xf, adc->regs + S5P_ADCMUX);
		else if (cpu == TYPE_ADCV11 || cpu == TYPE_ADCV12)
			writel(client->channel & 0xf,
						adc->regs + S3C2443_ADCMUX);
		else
			con |= S3C2410_ADCCON_SELMUX(client->channel);
	}

	writel(con, adc->regs + S3C2410_ADCCON);
}

static void s3c_adc_dbgshow(struct adc_device *adc)
{
	adc_dbg(adc, "CON=%08x, TSC=%08x, DLY=%08x\n",
		readl(adc->regs + S3C2410_ADCCON),
		readl(adc->regs + S3C2410_ADCTSC),
		readl(adc->regs + S3C2410_ADCDLY));
}

static void s3c_adc_try(struct adc_device *adc)
{
	struct s3c_adc_client *next = adc->ts_pend;

	if (!next && !list_empty(&adc_pending)) {
		next = list_first_entry(&adc_pending,
					struct s3c_adc_client, pend);
		list_del(&next->pend);
	} else
		adc->ts_pend = NULL;

	if (next) {
		adc_dbg(adc, "new client is %p\n", next);
		adc->cur = next;
		s3c_adc_select(adc, next);
		s3c_adc_convert(adc);
		s3c_adc_dbgshow(adc);
	}
}

int s3c_adc_start(struct s3c_adc_client *client,
		  unsigned int channel, unsigned int nr_samples)
{
	struct adc_device *adc = adc_dev;
	unsigned long flags;

	if (!adc) {
		printk(KERN_ERR "%s: failed to find adc\n", __func__);
		return -EINVAL;
	}

	spin_lock_irqsave(&adc->lock, flags);

	if (client->is_ts && adc->ts_pend) {
		spin_unlock_irqrestore(&adc->lock, flags);
		return -EAGAIN;
	}

	client->channel = channel;
	client->nr_samples = nr_samples;

	if (client->is_ts)
		adc->ts_pend = client;
	else
		list_add_tail(&client->pend, &adc_pending);

	if (!adc->cur)
		s3c_adc_try(adc);

	spin_unlock_irqrestore(&adc->lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(s3c_adc_start);

static void s3c_convert_done(struct s3c_adc_client *client,
			     unsigned v, unsigned u, unsigned *left)
{
	client->result = v;
	wake_up(client->wait);
}

int s3c_adc_read(struct s3c_adc_client *client, unsigned int ch)
{
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(wake);
	int ret;

	client->convert_cb = s3c_convert_done;
	client->wait = &wake;
	client->result = -1;

	ret = s3c_adc_start(client, ch, 1);
	if (ret < 0)
		goto err;

	ret = wait_event_timeout(wake, client->result >= 0, HZ / 2);
	if (client->result < 0) {
		ret = -ETIMEDOUT;
		goto err;
	}

	client->convert_cb = NULL;
	return client->result;

err:
	return ret;
}
EXPORT_SYMBOL_GPL(s3c_adc_read);

static void s3c_adc_default_select(struct s3c_adc_client *client,
				   unsigned select)
{
}

struct s3c_adc_client *s3c_adc_register(struct platform_device *pdev,
					void (*select)(struct s3c_adc_client *client,
						       unsigned int selected),
					void (*conv)(struct s3c_adc_client *client,
						     unsigned d0, unsigned d1,
						     unsigned *samples_left),
					unsigned int is_ts)
{
	struct s3c_adc_client *client;

	WARN_ON(!pdev);

	if (!select)
		select = s3c_adc_default_select;

	if (!pdev)
		return ERR_PTR(-EINVAL);

	client = kzalloc(sizeof(struct s3c_adc_client), GFP_KERNEL);
	if (!client) {
		dev_err(&pdev->dev, "no memory for adc client\n");
		return ERR_PTR(-ENOMEM);
	}

	client->pdev = pdev;
	client->is_ts = is_ts;
	client->select_cb = select;
	client->convert_cb = conv;

	return client;
}
EXPORT_SYMBOL_GPL(s3c_adc_register);

void s3c_adc_release(struct s3c_adc_client *client)
{
	unsigned long flags;

	spin_lock_irqsave(&adc_dev->lock, flags);

	/* We should really check that nothing is in progress. */
	if (adc_dev->cur == client)
		adc_dev->cur = NULL;
	if (adc_dev->ts_pend == client)
		adc_dev->ts_pend = NULL;
	else {
		struct list_head *p, *n;
		struct s3c_adc_client *tmp;

		list_for_each_safe(p, n, &adc_pending) {
			tmp = list_entry(p, struct s3c_adc_client, pend);
			if (tmp == client)
				list_del(&tmp->pend);
		}
	}

	if (adc_dev->cur == NULL)
		s3c_adc_try(adc_dev);

	spin_unlock_irqrestore(&adc_dev->lock, flags);
	kfree(client);
}
EXPORT_SYMBOL_GPL(s3c_adc_release);

static irqreturn_t s3c_adc_irq(int irq, void *pw)
{
	struct adc_device *adc = pw;
	struct s3c_adc_client *client = adc->cur;
	enum s3c_cpu_type cpu = platform_get_device_id(adc->pdev)->driver_data;
	unsigned data0, data1;

	if (!client) {
		dev_warn(&adc->pdev->dev, "%s: no adc pending\n", __func__);
		goto exit;
	}

	data0 = readl(adc->regs + S3C2410_ADCDAT0);
	data1 = readl(adc->regs + S3C2410_ADCDAT1);
	adc_dbg(adc, "read %d: 0x%04x, 0x%04x\n", client->nr_samples, data0, data1);

	client->nr_samples--;

	if (cpu == TYPE_ADCV1 || cpu == TYPE_ADCV11) {
		data0 &= 0x3ff;
		data1 &= 0x3ff;
	} else {
		/* S3C2416/S3C64XX/S5P ADC resolution is 12-bit */
		data0 &= 0xfff;
		data1 &= 0xfff;
	}

	if (client->convert_cb)
		(client->convert_cb)(client, data0, data1, &client->nr_samples);

	if (client->nr_samples > 0) {
		/* fire another conversion for this */

		client->select_cb(client, 1);
		s3c_adc_convert(adc);
	} else {
		spin_lock(&adc->lock);
		(client->select_cb)(client, 0);
		adc->cur = NULL;

		s3c_adc_try(adc);
		spin_unlock(&adc->lock);
	}

exit:
	if (cpu == TYPE_ADCV2 || cpu == TYPE_ADCV3) {
		/* Clear ADC interrupt */
		writel(0, adc->regs + S3C64XX_ADCCLRINT);
	}
	return IRQ_HANDLED;
}

static int s3c_adc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct adc_device *adc;
	struct resource *regs;
	enum s3c_cpu_type cpu = platform_get_device_id(pdev)->driver_data;
	int ret;
	unsigned tmp;

	adc = devm_kzalloc(dev, sizeof(struct adc_device), GFP_KERNEL);
	if (adc == NULL) {
		dev_err(dev, "failed to allocate adc_device\n");
		return -ENOMEM;
	}

	spin_lock_init(&adc->lock);

	adc->pdev = pdev;
	adc->prescale = S3C2410_ADCCON_PRSCVL(49);

	adc->vdd = devm_regulator_get(dev, "vdd");
	if (IS_ERR(adc->vdd)) {
		dev_err(dev, "operating without regulator \"vdd\" .\n");
		return PTR_ERR(adc->vdd);
	}

	adc->irq = platform_get_irq(pdev, 1);
	if (adc->irq <= 0) {
		dev_err(dev, "failed to get adc irq\n");
		return -ENOENT;
	}

	ret = devm_request_irq(dev, adc->irq, s3c_adc_irq, 0, dev_name(dev),
				adc);
	if (ret < 0) {
		dev_err(dev, "failed to attach adc irq\n");
		return ret;
	}

	adc->clk = devm_clk_get(dev, "adc");
	if (IS_ERR(adc->clk)) {
		dev_err(dev, "failed to get adc clock\n");
		return PTR_ERR(adc->clk);
	}

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	adc->regs = devm_ioremap_resource(dev, regs);
	if (IS_ERR(adc->regs))
		return PTR_ERR(adc->regs);

	ret = regulator_enable(adc->vdd);
	if (ret)
		return ret;

	clk_enable(adc->clk);

	tmp = adc->prescale | S3C2410_ADCCON_PRSCEN;

	/* Enable 12-bit ADC resolution */
	if (cpu == TYPE_ADCV12)
		tmp |= S3C2416_ADCCON_RESSEL;
	if (cpu == TYPE_ADCV2 || cpu == TYPE_ADCV3)
		tmp |= S3C64XX_ADCCON_RESSEL;

	writel(tmp, adc->regs + S3C2410_ADCCON);

	dev_info(dev, "attached adc driver\n");

	platform_set_drvdata(pdev, adc);
	adc_dev = adc;

	return 0;
}

static int s3c_adc_remove(struct platform_device *pdev)
{
	struct adc_device *adc = platform_get_drvdata(pdev);

	clk_disable(adc->clk);
	regulator_disable(adc->vdd);

	return 0;
}

#ifdef CONFIG_PM
static int s3c_adc_suspend(struct device *dev)
{
	struct platform_device *pdev = container_of(dev,
			struct platform_device, dev);
	struct adc_device *adc = platform_get_drvdata(pdev);
	unsigned long flags;
	u32 con;

	spin_lock_irqsave(&adc->lock, flags);

	con = readl(adc->regs + S3C2410_ADCCON);
	con |= S3C2410_ADCCON_STDBM;
	writel(con, adc->regs + S3C2410_ADCCON);

	disable_irq(adc->irq);
	spin_unlock_irqrestore(&adc->lock, flags);
	clk_disable(adc->clk);
	regulator_disable(adc->vdd);

	return 0;
}

static int s3c_adc_resume(struct device *dev)
{
	struct platform_device *pdev = container_of(dev,
			struct platform_device, dev);
	struct adc_device *adc = platform_get_drvdata(pdev);
	enum s3c_cpu_type cpu = platform_get_device_id(pdev)->driver_data;
	int ret;
	unsigned long tmp;

	ret = regulator_enable(adc->vdd);
	if (ret)
		return ret;
	clk_enable(adc->clk);
	enable_irq(adc->irq);

	tmp = adc->prescale | S3C2410_ADCCON_PRSCEN;

	/* Enable 12-bit ADC resolution */
	if (cpu == TYPE_ADCV12)
		tmp |= S3C2416_ADCCON_RESSEL;
	if (cpu == TYPE_ADCV2 || cpu == TYPE_ADCV3)
		tmp |= S3C64XX_ADCCON_RESSEL;

	writel(tmp, adc->regs + S3C2410_ADCCON);

	return 0;
}

#else
#define s3c_adc_suspend NULL
#define s3c_adc_resume NULL
#endif

static struct platform_device_id s3c_adc_driver_ids[] = {
	{
		.name           = "s3c24xx-adc",
		.driver_data    = TYPE_ADCV1,
	}, {
		.name		= "s3c2443-adc",
		.driver_data	= TYPE_ADCV11,
	}, {
		.name		= "s3c2416-adc",
		.driver_data	= TYPE_ADCV12,
	}, {
		.name           = "s3c64xx-adc",
		.driver_data    = TYPE_ADCV2,
	}, {
		.name		= "samsung-adc-v3",
		.driver_data	= TYPE_ADCV3,
	},
	{ }
};
MODULE_DEVICE_TABLE(platform, s3c_adc_driver_ids);

static const struct dev_pm_ops adc_pm_ops = {
	.suspend	= s3c_adc_suspend,
	.resume		= s3c_adc_resume,
};

static struct platform_driver s3c_adc_driver = {
	.id_table	= s3c_adc_driver_ids,
	.driver		= {
		.name	= "s3c-adc",
		.pm	= &adc_pm_ops,
	},
	.probe		= s3c_adc_probe,
	.remove		= s3c_adc_remove,
};

static int __init adc_init(void)
{
	int ret;

	ret = platform_driver_register(&s3c_adc_driver);
	if (ret)
		printk(KERN_ERR "%s: failed to add adc driver\n", __func__);

	return ret;
}

module_init(adc_init);
