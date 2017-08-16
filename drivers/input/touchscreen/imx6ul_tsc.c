/*
 * Freescale i.MX6UL touchscreen controller driver
 *
 * Copyright (C) 2015 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/gpio/consumer.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/log2.h>

/* ADC configuration registers field define */
#define ADC_AIEN		(0x1 << 7)
#define ADC_CONV_DISABLE	0x1F
#define ADC_AVGE		(0x1 << 5)
#define ADC_CAL			(0x1 << 7)
#define ADC_CALF		0x2
#define ADC_12BIT_MODE		(0x2 << 2)
#define ADC_CONV_MODE_MASK	(0x3 << 2)
#define ADC_IPG_CLK		0x00
#define ADC_INPUT_CLK_MASK	0x3
#define ADC_CLK_DIV_8		(0x03 << 5)
#define ADC_CLK_DIV_MASK	(0x3 << 5)
#define ADC_SHORT_SAMPLE_MODE	(0x0 << 4)
#define ADC_SAMPLE_MODE_MASK	(0x1 << 4)
#define ADC_HARDWARE_TRIGGER	(0x1 << 13)
#define ADC_AVGS_SHIFT		14
#define ADC_AVGS_MASK		(0x3 << 14)
#define SELECT_CHANNEL_4	0x04
#define SELECT_CHANNEL_1	0x01
#define DISABLE_CONVERSION_INT	(0x0 << 7)

/* ADC registers */
#define REG_ADC_HC0		0x00
#define REG_ADC_HC1		0x04
#define REG_ADC_HC2		0x08
#define REG_ADC_HC3		0x0C
#define REG_ADC_HC4		0x10
#define REG_ADC_HS		0x14
#define REG_ADC_R0		0x18
#define REG_ADC_CFG		0x2C
#define REG_ADC_GC		0x30
#define REG_ADC_GS		0x34

#define ADC_TIMEOUT		msecs_to_jiffies(100)

/* TSC registers */
#define REG_TSC_BASIC_SETING	0x00
#define REG_TSC_PRE_CHARGE_TIME	0x10
#define REG_TSC_FLOW_CONTROL	0x20
#define REG_TSC_MEASURE_VALUE	0x30
#define REG_TSC_INT_EN		0x40
#define REG_TSC_INT_SIG_EN	0x50
#define REG_TSC_INT_STATUS	0x60
#define REG_TSC_DEBUG_MODE	0x70
#define REG_TSC_DEBUG_MODE2	0x80

/* TSC configuration registers field define */
#define DETECT_4_WIRE_MODE	(0x0 << 4)
#define AUTO_MEASURE		0x1
#define MEASURE_SIGNAL		0x1
#define DETECT_SIGNAL		(0x1 << 4)
#define VALID_SIGNAL		(0x1 << 8)
#define MEASURE_INT_EN		0x1
#define MEASURE_SIG_EN		0x1
#define VALID_SIG_EN		(0x1 << 8)
#define DE_GLITCH_2		(0x2 << 29)
#define START_SENSE		(0x1 << 12)
#define TSC_DISABLE		(0x1 << 16)
#define DETECT_MODE		0x2

struct imx6ul_tsc {
	struct device *dev;
	struct input_dev *input;
	void __iomem *tsc_regs;
	void __iomem *adc_regs;
	struct clk *tsc_clk;
	struct clk *adc_clk;
	struct gpio_desc *xnur_gpio;

	u32 measure_delay_time;
	u32 pre_charge_time;
	bool average_enable;
	u32 average_select;

	struct completion completion;
};

/*
 * TSC module need ADC to get the measure value. So
 * before config TSC, we should initialize ADC module.
 */
static int imx6ul_adc_init(struct imx6ul_tsc *tsc)
{
	u32 adc_hc = 0;
	u32 adc_gc;
	u32 adc_gs;
	u32 adc_cfg;
	unsigned long timeout;

	reinit_completion(&tsc->completion);

	adc_cfg = readl(tsc->adc_regs + REG_ADC_CFG);
	adc_cfg &= ~(ADC_CONV_MODE_MASK | ADC_INPUT_CLK_MASK);
	adc_cfg |= ADC_12BIT_MODE | ADC_IPG_CLK;
	adc_cfg &= ~(ADC_CLK_DIV_MASK | ADC_SAMPLE_MODE_MASK);
	adc_cfg |= ADC_CLK_DIV_8 | ADC_SHORT_SAMPLE_MODE;
	if (tsc->average_enable) {
		adc_cfg &= ~ADC_AVGS_MASK;
		adc_cfg |= (tsc->average_select) << ADC_AVGS_SHIFT;
	}
	adc_cfg &= ~ADC_HARDWARE_TRIGGER;
	writel(adc_cfg, tsc->adc_regs + REG_ADC_CFG);

	/* enable calibration interrupt */
	adc_hc |= ADC_AIEN;
	adc_hc |= ADC_CONV_DISABLE;
	writel(adc_hc, tsc->adc_regs + REG_ADC_HC0);

	/* start ADC calibration */
	adc_gc = readl(tsc->adc_regs + REG_ADC_GC);
	adc_gc |= ADC_CAL;
	if (tsc->average_enable)
		adc_gc |= ADC_AVGE;
	writel(adc_gc, tsc->adc_regs + REG_ADC_GC);

	timeout = wait_for_completion_timeout
			(&tsc->completion, ADC_TIMEOUT);
	if (timeout == 0) {
		dev_err(tsc->dev, "Timeout for adc calibration\n");
		return -ETIMEDOUT;
	}

	adc_gs = readl(tsc->adc_regs + REG_ADC_GS);
	if (adc_gs & ADC_CALF) {
		dev_err(tsc->dev, "ADC calibration failed\n");
		return -EINVAL;
	}

	/* TSC need the ADC work in hardware trigger */
	adc_cfg = readl(tsc->adc_regs + REG_ADC_CFG);
	adc_cfg |= ADC_HARDWARE_TRIGGER;
	writel(adc_cfg, tsc->adc_regs + REG_ADC_CFG);

	return 0;
}

/*
 * This is a TSC workaround. Currently TSC misconnect two
 * ADC channels, this function remap channel configure for
 * hardware trigger.
 */
static void imx6ul_tsc_channel_config(struct imx6ul_tsc *tsc)
{
	u32 adc_hc0, adc_hc1, adc_hc2, adc_hc3, adc_hc4;

	adc_hc0 = DISABLE_CONVERSION_INT;
	writel(adc_hc0, tsc->adc_regs + REG_ADC_HC0);

	adc_hc1 = DISABLE_CONVERSION_INT | SELECT_CHANNEL_4;
	writel(adc_hc1, tsc->adc_regs + REG_ADC_HC1);

	adc_hc2 = DISABLE_CONVERSION_INT;
	writel(adc_hc2, tsc->adc_regs + REG_ADC_HC2);

	adc_hc3 = DISABLE_CONVERSION_INT | SELECT_CHANNEL_1;
	writel(adc_hc3, tsc->adc_regs + REG_ADC_HC3);

	adc_hc4 = DISABLE_CONVERSION_INT;
	writel(adc_hc4, tsc->adc_regs + REG_ADC_HC4);
}

/*
 * TSC setting, confige the pre-charge time and measure delay time.
 * different touch screen may need different pre-charge time and
 * measure delay time.
 */
static void imx6ul_tsc_set(struct imx6ul_tsc *tsc)
{
	u32 basic_setting = 0;
	u32 start;

	basic_setting |= tsc->measure_delay_time << 8;
	basic_setting |= DETECT_4_WIRE_MODE | AUTO_MEASURE;
	writel(basic_setting, tsc->tsc_regs + REG_TSC_BASIC_SETING);

	writel(DE_GLITCH_2, tsc->tsc_regs + REG_TSC_DEBUG_MODE2);

	writel(tsc->pre_charge_time, tsc->tsc_regs + REG_TSC_PRE_CHARGE_TIME);
	writel(MEASURE_INT_EN, tsc->tsc_regs + REG_TSC_INT_EN);
	writel(MEASURE_SIG_EN | VALID_SIG_EN,
		tsc->tsc_regs + REG_TSC_INT_SIG_EN);

	/* start sense detection */
	start = readl(tsc->tsc_regs + REG_TSC_FLOW_CONTROL);
	start |= START_SENSE;
	start &= ~TSC_DISABLE;
	writel(start, tsc->tsc_regs + REG_TSC_FLOW_CONTROL);
}

static int imx6ul_tsc_init(struct imx6ul_tsc *tsc)
{
	int err;

	err = imx6ul_adc_init(tsc);
	if (err)
		return err;
	imx6ul_tsc_channel_config(tsc);
	imx6ul_tsc_set(tsc);

	return 0;
}

static void imx6ul_tsc_disable(struct imx6ul_tsc *tsc)
{
	u32 tsc_flow;
	u32 adc_cfg;

	/* TSC controller enters to idle status */
	tsc_flow = readl(tsc->tsc_regs + REG_TSC_FLOW_CONTROL);
	tsc_flow |= TSC_DISABLE;
	writel(tsc_flow, tsc->tsc_regs + REG_TSC_FLOW_CONTROL);

	/* ADC controller enters to stop mode */
	adc_cfg = readl(tsc->adc_regs + REG_ADC_HC0);
	adc_cfg |= ADC_CONV_DISABLE;
	writel(adc_cfg, tsc->adc_regs + REG_ADC_HC0);
}

/* Delay some time (max 2ms), wait the pre-charge done. */
static bool tsc_wait_detect_mode(struct imx6ul_tsc *tsc)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(2);
	u32 state_machine;
	u32 debug_mode2;

	do {
		if (time_after(jiffies, timeout))
			return false;

		usleep_range(200, 400);
		debug_mode2 = readl(tsc->tsc_regs + REG_TSC_DEBUG_MODE2);
		state_machine = (debug_mode2 >> 20) & 0x7;
	} while (state_machine != DETECT_MODE);

	usleep_range(200, 400);
	return true;
}

static irqreturn_t tsc_irq_fn(int irq, void *dev_id)
{
	struct imx6ul_tsc *tsc = dev_id;
	u32 status;
	u32 value;
	u32 x, y;
	u32 start;

	status = readl(tsc->tsc_regs + REG_TSC_INT_STATUS);

	/* write 1 to clear the bit measure-signal */
	writel(MEASURE_SIGNAL | DETECT_SIGNAL,
		tsc->tsc_regs + REG_TSC_INT_STATUS);

	/* It's a HW self-clean bit. Set this bit and start sense detection */
	start = readl(tsc->tsc_regs + REG_TSC_FLOW_CONTROL);
	start |= START_SENSE;
	writel(start, tsc->tsc_regs + REG_TSC_FLOW_CONTROL);

	if (status & MEASURE_SIGNAL) {
		value = readl(tsc->tsc_regs + REG_TSC_MEASURE_VALUE);
		x = (value >> 16) & 0x0fff;
		y = value & 0x0fff;

		/*
		 * In detect mode, we can get the xnur gpio value,
		 * otherwise assume contact is stiull active.
		 */
		if (!tsc_wait_detect_mode(tsc) ||
		    gpiod_get_value_cansleep(tsc->xnur_gpio)) {
			input_report_key(tsc->input, BTN_TOUCH, 1);
			input_report_abs(tsc->input, ABS_X, x);
			input_report_abs(tsc->input, ABS_Y, y);
		} else {
			input_report_key(tsc->input, BTN_TOUCH, 0);
		}

		input_sync(tsc->input);
	}

	return IRQ_HANDLED;
}

static irqreturn_t adc_irq_fn(int irq, void *dev_id)
{
	struct imx6ul_tsc *tsc = dev_id;
	u32 coco;
	u32 value;

	coco = readl(tsc->adc_regs + REG_ADC_HS);
	if (coco & 0x01) {
		value = readl(tsc->adc_regs + REG_ADC_R0);
		complete(&tsc->completion);
	}

	return IRQ_HANDLED;
}

static int imx6ul_tsc_open(struct input_dev *input_dev)
{
	struct imx6ul_tsc *tsc = input_get_drvdata(input_dev);
	int err;

	err = clk_prepare_enable(tsc->adc_clk);
	if (err) {
		dev_err(tsc->dev,
			"Could not prepare or enable the adc clock: %d\n",
			err);
		return err;
	}

	err = clk_prepare_enable(tsc->tsc_clk);
	if (err) {
		dev_err(tsc->dev,
			"Could not prepare or enable the tsc clock: %d\n",
			err);
		goto disable_adc_clk;
	}

	err = imx6ul_tsc_init(tsc);
	if (err)
		goto disable_tsc_clk;

	return 0;

disable_tsc_clk:
	clk_disable_unprepare(tsc->tsc_clk);
disable_adc_clk:
	clk_disable_unprepare(tsc->adc_clk);
	return err;
}

static void imx6ul_tsc_close(struct input_dev *input_dev)
{
	struct imx6ul_tsc *tsc = input_get_drvdata(input_dev);

	imx6ul_tsc_disable(tsc);

	clk_disable_unprepare(tsc->tsc_clk);
	clk_disable_unprepare(tsc->adc_clk);
}

static int imx6ul_tsc_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct imx6ul_tsc *tsc;
	struct input_dev *input_dev;
	struct resource *tsc_mem;
	struct resource *adc_mem;
	int err;
	int tsc_irq;
	int adc_irq;
	u32 average_samples;

	tsc = devm_kzalloc(&pdev->dev, sizeof(*tsc), GFP_KERNEL);
	if (!tsc)
		return -ENOMEM;

	input_dev = devm_input_allocate_device(&pdev->dev);
	if (!input_dev)
		return -ENOMEM;

	input_dev->name = "iMX6UL Touchscreen Controller";
	input_dev->id.bustype = BUS_HOST;

	input_dev->open = imx6ul_tsc_open;
	input_dev->close = imx6ul_tsc_close;

	input_set_capability(input_dev, EV_KEY, BTN_TOUCH);
	input_set_abs_params(input_dev, ABS_X, 0, 0xFFF, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, 0xFFF, 0, 0);

	input_set_drvdata(input_dev, tsc);

	tsc->dev = &pdev->dev;
	tsc->input = input_dev;
	init_completion(&tsc->completion);

	tsc->xnur_gpio = devm_gpiod_get(&pdev->dev, "xnur", GPIOD_IN);
	if (IS_ERR(tsc->xnur_gpio)) {
		err = PTR_ERR(tsc->xnur_gpio);
		dev_err(&pdev->dev,
			"failed to request GPIO tsc_X- (xnur): %d\n", err);
		return err;
	}

	tsc_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	tsc->tsc_regs = devm_ioremap_resource(&pdev->dev, tsc_mem);
	if (IS_ERR(tsc->tsc_regs)) {
		err = PTR_ERR(tsc->tsc_regs);
		dev_err(&pdev->dev, "failed to remap tsc memory: %d\n", err);
		return err;
	}

	adc_mem = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	tsc->adc_regs = devm_ioremap_resource(&pdev->dev, adc_mem);
	if (IS_ERR(tsc->adc_regs)) {
		err = PTR_ERR(tsc->adc_regs);
		dev_err(&pdev->dev, "failed to remap adc memory: %d\n", err);
		return err;
	}

	tsc->tsc_clk = devm_clk_get(&pdev->dev, "tsc");
	if (IS_ERR(tsc->tsc_clk)) {
		err = PTR_ERR(tsc->tsc_clk);
		dev_err(&pdev->dev, "failed getting tsc clock: %d\n", err);
		return err;
	}

	tsc->adc_clk = devm_clk_get(&pdev->dev, "adc");
	if (IS_ERR(tsc->adc_clk)) {
		err = PTR_ERR(tsc->adc_clk);
		dev_err(&pdev->dev, "failed getting adc clock: %d\n", err);
		return err;
	}

	tsc_irq = platform_get_irq(pdev, 0);
	if (tsc_irq < 0) {
		dev_err(&pdev->dev, "no tsc irq resource?\n");
		return tsc_irq;
	}

	adc_irq = platform_get_irq(pdev, 1);
	if (adc_irq < 0) {
		dev_err(&pdev->dev, "no adc irq resource?\n");
		return adc_irq;
	}

	err = devm_request_threaded_irq(tsc->dev, tsc_irq,
					NULL, tsc_irq_fn, IRQF_ONESHOT,
					dev_name(&pdev->dev), tsc);
	if (err) {
		dev_err(&pdev->dev,
			"failed requesting tsc irq %d: %d\n",
			tsc_irq, err);
		return err;
	}

	err = devm_request_irq(tsc->dev, adc_irq, adc_irq_fn, 0,
				dev_name(&pdev->dev), tsc);
	if (err) {
		dev_err(&pdev->dev,
			"failed requesting adc irq %d: %d\n",
			adc_irq, err);
		return err;
	}

	err = of_property_read_u32(np, "measure-delay-time",
				   &tsc->measure_delay_time);
	if (err)
		tsc->measure_delay_time = 0xffff;

	err = of_property_read_u32(np, "pre-charge-time",
				   &tsc->pre_charge_time);
	if (err)
		tsc->pre_charge_time = 0xfff;

	err = of_property_read_u32(np, "touchscreen-average-samples",
				   &average_samples);
	if (err)
		average_samples = 1;

	switch (average_samples) {
	case 1:
		tsc->average_enable = false;
		tsc->average_select = 0; /* value unused; initialize anyway */
		break;
	case 4:
	case 8:
	case 16:
	case 32:
		tsc->average_enable = true;
		tsc->average_select = ilog2(average_samples) - 2;
		break;
	default:
		dev_err(&pdev->dev,
			"touchscreen-average-samples (%u) must be 1, 4, 8, 16 or 32\n",
			average_samples);
		return -EINVAL;
	}

	err = input_register_device(tsc->input);
	if (err) {
		dev_err(&pdev->dev,
			"failed to register input device: %d\n", err);
		return err;
	}

	platform_set_drvdata(pdev, tsc);
	return 0;
}

static int __maybe_unused imx6ul_tsc_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct imx6ul_tsc *tsc = platform_get_drvdata(pdev);
	struct input_dev *input_dev = tsc->input;

	mutex_lock(&input_dev->mutex);

	if (input_dev->users) {
		imx6ul_tsc_disable(tsc);

		clk_disable_unprepare(tsc->tsc_clk);
		clk_disable_unprepare(tsc->adc_clk);
	}

	mutex_unlock(&input_dev->mutex);

	return 0;
}

static int __maybe_unused imx6ul_tsc_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct imx6ul_tsc *tsc = platform_get_drvdata(pdev);
	struct input_dev *input_dev = tsc->input;
	int retval = 0;

	mutex_lock(&input_dev->mutex);

	if (input_dev->users) {
		retval = clk_prepare_enable(tsc->adc_clk);
		if (retval)
			goto out;

		retval = clk_prepare_enable(tsc->tsc_clk);
		if (retval) {
			clk_disable_unprepare(tsc->adc_clk);
			goto out;
		}

		retval = imx6ul_tsc_init(tsc);
	}

out:
	mutex_unlock(&input_dev->mutex);
	return retval;
}

static SIMPLE_DEV_PM_OPS(imx6ul_tsc_pm_ops,
			 imx6ul_tsc_suspend, imx6ul_tsc_resume);

static const struct of_device_id imx6ul_tsc_match[] = {
	{ .compatible = "fsl,imx6ul-tsc", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx6ul_tsc_match);

static struct platform_driver imx6ul_tsc_driver = {
	.driver		= {
		.name	= "imx6ul-tsc",
		.of_match_table	= imx6ul_tsc_match,
		.pm	= &imx6ul_tsc_pm_ops,
	},
	.probe		= imx6ul_tsc_probe,
};
module_platform_driver(imx6ul_tsc_driver);

MODULE_AUTHOR("Haibo Chen <haibo.chen@freescale.com>");
MODULE_DESCRIPTION("Freescale i.MX6UL Touchscreen controller driver");
MODULE_LICENSE("GPL v2");
