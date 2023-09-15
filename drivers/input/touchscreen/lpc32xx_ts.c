// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * LPC32xx built-in touchscreen driver
 *
 * Copyright (C) 2010 NXP Semiconductors
 */

#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/of.h>

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

#define LPC32XX_TSC_STAT_FIFO_OVRRN		BIT(8)
#define LPC32XX_TSC_STAT_FIFO_EMPTY		BIT(7)

#define LPC32XX_TSC_SEL_DEFVAL			0x0284

#define LPC32XX_TSC_ADCCON_IRQ_TO_FIFO_4	(0x1 << 11)
#define LPC32XX_TSC_ADCCON_X_SAMPLE_SIZE(s)	((10 - (s)) << 7)
#define LPC32XX_TSC_ADCCON_Y_SAMPLE_SIZE(s)	((10 - (s)) << 4)
#define LPC32XX_TSC_ADCCON_POWER_UP		BIT(2)
#define LPC32XX_TSC_ADCCON_AUTO_EN		BIT(0)

#define LPC32XX_TSC_FIFO_TS_P_LEVEL		BIT(31)
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

	clk_disable_unprepare(tsc->clk);
}

static int lpc32xx_setup_tsc(struct lpc32xx_tsc *tsc)
{
	u32 tmp;
	int err;

	err = clk_prepare_enable(tsc->clk);
	if (err)
		return err;

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

	return 0;
}

static int lpc32xx_ts_open(struct input_dev *dev)
{
	struct lpc32xx_tsc *tsc = input_get_drvdata(dev);

	return lpc32xx_setup_tsc(tsc);
}

static void lpc32xx_ts_close(struct input_dev *dev)
{
	struct lpc32xx_tsc *tsc = input_get_drvdata(dev);

	lpc32xx_stop_tsc(tsc);
}

static int lpc32xx_ts_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct lpc32xx_tsc *tsc;
	struct input_dev *input;
	int irq;
	int error;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	tsc = devm_kzalloc(dev, sizeof(*tsc), GFP_KERNEL);
	if (!tsc)
		return -ENOMEM;

	tsc->irq = irq;

	tsc->tsc_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(tsc->tsc_base))
		return PTR_ERR(tsc->tsc_base);

	tsc->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(tsc->clk)) {
		dev_err(&pdev->dev, "failed getting clock\n");
		return PTR_ERR(tsc->clk);
	}

	input = devm_input_allocate_device(dev);
	if (!input) {
		dev_err(&pdev->dev, "failed allocating input device\n");
		return -ENOMEM;
	}

	input->name = MOD_NAME;
	input->phys = "lpc32xx/input0";
	input->id.bustype = BUS_HOST;
	input->id.vendor = 0x0001;
	input->id.product = 0x0002;
	input->id.version = 0x0100;
	input->open = lpc32xx_ts_open;
	input->close = lpc32xx_ts_close;

	input_set_capability(input, EV_KEY, BTN_TOUCH);
	input_set_abs_params(input, ABS_X, LPC32XX_TSC_MIN_XY_VAL,
			     LPC32XX_TSC_MAX_XY_VAL, 0, 0);
	input_set_abs_params(input, ABS_Y, LPC32XX_TSC_MIN_XY_VAL,
			     LPC32XX_TSC_MAX_XY_VAL, 0, 0);

	input_set_drvdata(input, tsc);
	tsc->dev = input;

	error = devm_request_irq(dev, tsc->irq, lpc32xx_ts_interrupt,
				 0, pdev->name, tsc);
	if (error) {
		dev_err(&pdev->dev, "failed requesting interrupt\n");
		return error;
	}

	error = input_register_device(input);
	if (error) {
		dev_err(&pdev->dev, "failed registering input device\n");
		return error;
	}

	platform_set_drvdata(pdev, tsc);
	device_init_wakeup(&pdev->dev, true);

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

	if (input_device_enabled(input)) {
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

	if (input_device_enabled(input)) {
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

#ifdef CONFIG_OF
static const struct of_device_id lpc32xx_tsc_of_match[] = {
	{ .compatible = "nxp,lpc3220-tsc", },
	{ },
};
MODULE_DEVICE_TABLE(of, lpc32xx_tsc_of_match);
#endif

static struct platform_driver lpc32xx_ts_driver = {
	.probe		= lpc32xx_ts_probe,
	.driver		= {
		.name	= MOD_NAME,
		.pm	= LPC32XX_TS_PM_OPS,
		.of_match_table = of_match_ptr(lpc32xx_tsc_of_match),
	},
};
module_platform_driver(lpc32xx_ts_driver);

MODULE_AUTHOR("Kevin Wells <kevin.wells@nxp.com");
MODULE_DESCRIPTION("LPC32XX TSC Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:lpc32xx_ts");
