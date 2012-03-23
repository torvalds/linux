/*
 * LPC32xx built-in touchscreen driver
 *
 * Copyright (C) 2010 NXP Semiconductors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/slab.h>

/*
 * Touchscreen controller register offsets
 */
#define LPC32XX_TSC_STAT			0x00
#define LPC32XX_TSC_SEL				0x04
#define LPC32XX_TSC_CON				0x08
#define LPC32XX_TSC_FIFO			0x0C
#define LPC32XX_TSC_DTR				0x10
#define LPC32XX_TSC_RTR				0x14
#define LPC32XX_TSC_UTR				0x18
#define LPC32XX_TSC_TTR				0x1C
#define LPC32XX_TSC_DXP				0x20
#define LPC32XX_TSC_MIN_X			0x24
#define LPC32XX_TSC_MAX_X			0x28
#define LPC32XX_TSC_MIN_Y			0x2C
#define LPC32XX_TSC_MAX_Y			0x30
#define LPC32XX_TSC_AUX_UTR			0x34
#define LPC32XX_TSC_AUX_MIN			0x38
#define LPC32XX_TSC_AUX_MAX			0x3C

#define LPC32XX_TSC_STAT_FIFO_OVRRN		(1 << 8)
#define LPC32XX_TSC_STAT_FIFO_EMPTY		(1 << 7)

#define LPC32XX_TSC_SEL_DEFVAL			0x0284

#define LPC32XX_TSC_ADCCON_IRQ_TO_FIFO_4	(0x1 << 11)
#define LPC32XX_TSC_ADCCON_X_SAMPLE_SIZE(s)	((10 - (s)) << 7)
#define LPC32XX_TSC_ADCCON_Y_SAMPLE_SIZE(s)	((10 - (s)) << 4)
#define LPC32XX_TSC_ADCCON_POWER_UP		(1 << 2)
#define LPC32XX_TSC_ADCCON_AUTO_EN		(1 << 0)

#define LPC32XX_TSC_FIFO_TS_P_LEVEL		(1 << 31)
#define LPC32XX_TSC_FIFO_NORMALIZE_X_VAL(x)	(((x) & 0x03FF0000) >> 16)
#define LPC32XX_TSC_FIFO_NORMALIZE_Y_VAL(y)	((y) & 0x000003FF)

#define LPC32XX_TSC_ADCDAT_VALUE_MASK		0x000003FF

#define LPC32XX_TSC_MIN_XY_VAL			0x0
#define LPC32XX_TSC_MAX_XY_VAL			0x3FF

#define MOD_NAME "ts-lpc32xx"

#define tsc_readl(dev, reg) \
	__raw_readl((dev)->tsc_base + (reg))
#define tsc_writel(dev, reg, val) \
	__raw_writel((val), (dev)->tsc_base + (reg))

struct lpc32xx_tsc {
	struct input_dev *dev;
	void __iomem *tsc_base;
	int irq;
	struct clk *clk;
};

static void lpc32xx_fifo_clear(struct lpc32xx_tsc *tsc)
{
	while (!(tsc_readl(tsc, LPC32XX_TSC_STAT) &
			LPC32XX_TSC_STAT_FIFO_EMPTY))
		tsc_readl(tsc, LPC32XX_TSC_FIFO);
}

static irqreturn_t lpc32xx_ts_interrupt(int irq, void *dev_id)
{
	u32 tmp, rv[4], xs[4], ys[4];
	int idx;
	struct lpc32xx_tsc *tsc = dev_id;
	struct input_dev *input = tsc->dev;

	tmp = tsc_readl(tsc, LPC32XX_TSC_STAT);

	if (tmp & LPC32XX_TSC_STAT_FIFO_OVRRN) {
		/* FIFO overflow - throw away samples */
		lpc32xx_fifo_clear(tsc);
		return IRQ_HANDLED;
	}

	/*
	 * Gather and normalize 4 samples. Pen-up events may have less
	 * than 4 samples, but its ok to pop 4 and let the last sample
	 * pen status check drop the samples.
	 */
	idx = 0;
	while (idx < 4 &&
	       !(tsc_readl(tsc, LPC32XX_TSC_STAT) &
			LPC32XX_TSC_STAT_FIFO_EMPTY)) {
		tmp = tsc_readl(tsc, LPC32XX_TSC_FIFO);
		xs[idx] = LPC32XX_TSC_ADCDAT_VALUE_MASK -
			LPC32XX_TSC_FIFO_NORMALIZE_X_VAL(tmp);
		ys[idx] = LPC32XX_TSC_ADCDAT_VALUE_MASK -
			LPC32XX_TSC_FIFO_NORMALIZE_Y_VAL(tmp);
		rv[idx] = tmp;
		idx++;
	}

	/* Data is only valid if pen is still down in last sample */
	if (!(rv[3] & LPC32XX_TSC_FIFO_TS_P_LEVEL) && idx == 4) {
		/* Use average of 2nd and 3rd sample for position */
		input_report_abs(input, ABS_X, (xs[1] + xs[2]) / 2);
		input_report_abs(input, ABS_Y, (ys[1] + ys[2]) / 2);
		input_report_key(input, BTN_TOUCH, 1);
	} else {
		input_report_key(input, BTN_TOUCH, 0);
	}

	input_sync(input);

	return IRQ_HANDLED;
}

static void lpc32xx_stop_tsc(struct lpc32xx_tsc *tsc)
{
	/* Disable auto mode */
	tsc_writel(tsc, LPC32XX_TSC_CON,
		   tsc_readl(tsc, LPC32XX_TSC_CON) &
			     ~LPC32XX_TSC_ADCCON_AUTO_EN);

	clk_disable(tsc->clk);
}

static void lpc32xx_setup_tsc(struct lpc32xx_tsc *tsc)
{
	u32 tmp;

	clk_enable(tsc->clk);

	tmp = tsc_readl(tsc, LPC32XX_TSC_CON) & ~LPC32XX_TSC_ADCCON_POWER_UP;

	/* Set the TSC FIFO depth to 4 samples @ 10-bits per sample (max) */
	tmp = LPC32XX_TSC_ADCCON_IRQ_TO_FIFO_4 |
	      LPC32XX_TSC_ADCCON_X_SAMPLE_SIZE(10) |
	      LPC32XX_TSC_ADCCON_Y_SAMPLE_SIZE(10);
	tsc_writel(tsc, LPC32XX_TSC_CON, tmp);

	/* These values are all preset */
	tsc_writel(tsc, LPC32XX_TSC_SEL, LPC32XX_TSC_SEL_DEFVAL);
	tsc_writel(tsc, LPC32XX_TSC_MIN_X, LPC32XX_TSC_MIN_XY_VAL);
	tsc_writel(tsc, LPC32XX_TSC_MAX_X, LPC32XX_TSC_MAX_XY_VAL);
	tsc_writel(tsc, LPC32XX_TSC_MIN_Y, LPC32XX_TSC_MIN_XY_VAL);
	tsc_writel(tsc, LPC32XX_TSC_MAX_Y, LPC32XX_TSC_MAX_XY_VAL);

	/* Aux support is not used */
	tsc_writel(tsc, LPC32XX_TSC_AUX_UTR, 0);
	tsc_writel(tsc, LPC32XX_TSC_AUX_MIN, 0);
	tsc_writel(tsc, LPC32XX_TSC_AUX_MAX, 0);

	/*
	 * Set sample rate to about 240Hz per X/Y pair. A single measurement
	 * consists of 4 pairs which gives about a 60Hz sample rate based on
	 * a stable 32768Hz clock source. Values are in clocks.
	 * Rate is (32768 / (RTR + XCONV + RTR + YCONV + DXP + TTR + UTR) / 4
	 */
	tsc_writel(tsc, LPC32XX_TSC_RTR, 0x2);
	tsc_writel(tsc, LPC32XX_TSC_DTR, 0x2);
	tsc_writel(tsc, LPC32XX_TSC_TTR, 0x10);
	tsc_writel(tsc, LPC32XX_TSC_DXP, 0x4);
	tsc_writel(tsc, LPC32XX_TSC_UTR, 88);

	lpc32xx_fifo_clear(tsc);

	/* Enable automatic ts event capture */
	tsc_writel(tsc, LPC32XX_TSC_CON, tmp | LPC32XX_TSC_ADCCON_AUTO_EN);
}

static int lpc32xx_ts_open(struct input_dev *dev)
{
	struct lpc32xx_tsc *tsc = input_get_drvdata(dev);

	lpc32xx_setup_tsc(tsc);

	return 0;
}

static void lpc32xx_ts_close(struct input_dev *dev)
{
	struct lpc32xx_tsc *tsc = input_get_drvdata(dev);

	lpc32xx_stop_tsc(tsc);
}

static int __devinit lpc32xx_ts_probe(struct platform_device *pdev)
{
	struct lpc32xx_tsc *tsc;
	struct input_dev *input;
	struct resource *res;
	resource_size_t size;
	int irq;
	int error;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Can't get memory resource\n");
		return -ENOENT;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "Can't get interrupt resource\n");
		return irq;
	}

	tsc = kzalloc(sizeof(*tsc), GFP_KERNEL);
	input = input_allocate_device();
	if (!tsc || !input) {
		dev_err(&pdev->dev, "failed allocating memory\n");
		error = -ENOMEM;
		goto err_free_mem;
	}

	tsc->dev = input;
	tsc->irq = irq;

	size = resource_size(res);

	if (!request_mem_region(res->start, size, pdev->name)) {
		dev_err(&pdev->dev, "TSC registers are not free\n");
		error = -EBUSY;
		goto err_free_mem;
	}

	tsc->tsc_base = ioremap(res->start, size);
	if (!tsc->tsc_base) {
		dev_err(&pdev->dev, "Can't map memory\n");
		error = -ENOMEM;
		goto err_release_mem;
	}

	tsc->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(tsc->clk)) {
		dev_err(&pdev->dev, "failed getting clock\n");
		error = PTR_ERR(tsc->clk);
		goto err_unmap;
	}

	input->name = MOD_NAME;
	input->phys = "lpc32xx/input0";
	input->id.bustype = BUS_HOST;
	input->id.vendor = 0x0001;
	input->id.product = 0x0002;
	input->id.version = 0x0100;
	input->dev.parent = &pdev->dev;
	input->open = lpc32xx_ts_open;
	input->close = lpc32xx_ts_close;

	input->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	input->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
	input_set_abs_params(input, ABS_X, LPC32XX_TSC_MIN_XY_VAL,
			     LPC32XX_TSC_MAX_XY_VAL, 0, 0);
	input_set_abs_params(input, ABS_Y, LPC32XX_TSC_MIN_XY_VAL,
			     LPC32XX_TSC_MAX_XY_VAL, 0, 0);

	input_set_drvdata(input, tsc);

	error = request_irq(tsc->irq, lpc32xx_ts_interrupt,
			    0, pdev->name, tsc);
	if (error) {
		dev_err(&pdev->dev, "failed requesting interrupt\n");
		goto err_put_clock;
	}

	error = input_register_device(input);
	if (error) {
		dev_err(&pdev->dev, "failed registering input device\n");
		goto err_free_irq;
	}

	platform_set_drvdata(pdev, tsc);
	device_init_wakeup(&pdev->dev, 1);

	return 0;

err_free_irq:
	free_irq(tsc->irq, tsc);
err_put_clock:
	clk_put(tsc->clk);
err_unmap:
	iounmap(tsc->tsc_base);
err_release_mem:
	release_mem_region(res->start, size);
err_free_mem:
	input_free_device(input);
	kfree(tsc);

	return error;
}

static int __devexit lpc32xx_ts_remove(struct platform_device *pdev)
{
	struct lpc32xx_tsc *tsc = platform_get_drvdata(pdev);
	struct resource *res;

	device_init_wakeup(&pdev->dev, 0);
	free_irq(tsc->irq, tsc);

	input_unregister_device(tsc->dev);

	clk_put(tsc->clk);

	iounmap(tsc->tsc_base);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(res->start, resource_size(res));

	kfree(tsc);

	return 0;
}

#ifdef CONFIG_PM
static int lpc32xx_ts_suspend(struct device *dev)
{
	struct lpc32xx_tsc *tsc = dev_get_drvdata(dev);
	struct input_dev *input = tsc->dev;

	/*
	 * Suspend and resume can be called when the device hasn't been
	 * enabled. If there are no users that have the device open, then
	 * avoid calling the TSC stop and start functions as the TSC
	 * isn't yet clocked.
	 */
	mutex_lock(&input->mutex);

	if (input->users) {
		if (device_may_wakeup(dev))
			enable_irq_wake(tsc->irq);
		else
			lpc32xx_stop_tsc(tsc);
	}

	mutex_unlock(&input->mutex);

	return 0;
}

static int lpc32xx_ts_resume(struct device *dev)
{
	struct lpc32xx_tsc *tsc = dev_get_drvdata(dev);
	struct input_dev *input = tsc->dev;

	mutex_lock(&input->mutex);

	if (input->users) {
		if (device_may_wakeup(dev))
			disable_irq_wake(tsc->irq);
		else
			lpc32xx_setup_tsc(tsc);
	}

	mutex_unlock(&input->mutex);

	return 0;
}

static const struct dev_pm_ops lpc32xx_ts_pm_ops = {
	.suspend	= lpc32xx_ts_suspend,
	.resume		= lpc32xx_ts_resume,
};
#define LPC32XX_TS_PM_OPS (&lpc32xx_ts_pm_ops)
#else
#define LPC32XX_TS_PM_OPS NULL
#endif

static struct platform_driver lpc32xx_ts_driver = {
	.probe		= lpc32xx_ts_probe,
	.remove		= __devexit_p(lpc32xx_ts_remove),
	.driver		= {
		.name	= MOD_NAME,
		.owner	= THIS_MODULE,
		.pm	= LPC32XX_TS_PM_OPS,
	},
};
module_platform_driver(lpc32xx_ts_driver);

MODULE_AUTHOR("Kevin Wells <kevin.wells@nxp.com");
MODULE_DESCRIPTION("LPC32XX TSC Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:lpc32xx_ts");
