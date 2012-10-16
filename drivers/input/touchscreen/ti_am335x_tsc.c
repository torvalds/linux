/*
 * TI Touch Screen driver
 *
 * Copyright (C) 2011 Texas Instruments Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */


#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/input/ti_am335x_tsc.h>
#include <linux/delay.h>

#define REG_RAWIRQSTATUS	0x024
#define REG_IRQSTATUS		0x028
#define REG_IRQENABLE		0x02C
#define REG_IRQWAKEUP		0x034
#define REG_CTRL		0x040
#define REG_ADCFSM		0x044
#define REG_CLKDIV		0x04C
#define REG_SE			0x054
#define REG_IDLECONFIG		0x058
#define REG_CHARGECONFIG	0x05C
#define REG_CHARGEDELAY		0x060
#define REG_STEPCONFIG(n)	(0x64 + ((n - 1) * 8))
#define REG_STEPDELAY(n)	(0x68 + ((n - 1) * 8))
#define REG_FIFO0CNT		0xE4
#define REG_FIFO0THR		0xE8
#define REG_FIFO1THR		0xF4
#define REG_FIFO0		0x100
#define REG_FIFO1		0x200

/*	Register Bitfields	*/
#define IRQWKUP_ENB		BIT(0)

/* Step Enable */
#define STEPENB_MASK		(0x1FFFF << 0)
#define STEPENB(val)		(val << 0)
#define STPENB_STEPENB		STEPENB(0x7FFF)

/* IRQ enable */
#define IRQENB_FIFO0THRES	BIT(2)
#define IRQENB_FIFO1THRES	BIT(5)
#define IRQENB_PENUP		BIT(9)

/* Step Configuration */
#define STEPCONFIG_MODE_MASK	(3 << 0)
#define STEPCONFIG_MODE(val)	(val << 0)
#define STEPCONFIG_MODE_HWSYNC	STEPCONFIG_MODE(2)
#define STEPCONFIG_AVG_MASK	(7 << 2)
#define STEPCONFIG_AVG(val)	(val << 2)
#define STEPCONFIG_AVG_16	STEPCONFIG_AVG(4)
#define STEPCONFIG_XPP		BIT(5)
#define STEPCONFIG_XNN		BIT(6)
#define STEPCONFIG_YPP		BIT(7)
#define STEPCONFIG_YNN		BIT(8)
#define STEPCONFIG_XNP		BIT(9)
#define STEPCONFIG_YPN		BIT(10)
#define STEPCONFIG_INM_MASK	(0xF << 15)
#define STEPCONFIG_INM(val)	(val << 15)
#define STEPCONFIG_INM_ADCREFM	STEPCONFIG_INM(8)
#define STEPCONFIG_INP_MASK	(0xF << 19)
#define STEPCONFIG_INP(val)	(val << 19)
#define STEPCONFIG_INP_AN2	STEPCONFIG_INP(2)
#define STEPCONFIG_INP_AN3	STEPCONFIG_INP(3)
#define STEPCONFIG_INP_AN4	STEPCONFIG_INP(4)
#define STEPCONFIG_INP_ADCREFM	STEPCONFIG_INP(8)
#define STEPCONFIG_FIFO1	BIT(26)

/* Delay register */
#define STEPDELAY_OPEN_MASK	(0x3FFFF << 0)
#define STEPDELAY_OPEN(val)	(val << 0)
#define STEPCONFIG_OPENDLY	STEPDELAY_OPEN(0x098)

/* Charge Config */
#define STEPCHARGE_RFP_MASK	(7 << 12)
#define STEPCHARGE_RFP(val)	(val << 12)
#define STEPCHARGE_RFP_XPUL	STEPCHARGE_RFP(1)
#define STEPCHARGE_INM_MASK	(0xF << 15)
#define STEPCHARGE_INM(val)	(val << 15)
#define STEPCHARGE_INM_AN1	STEPCHARGE_INM(1)
#define STEPCHARGE_INP_MASK	(0xF << 19)
#define STEPCHARGE_INP(val)	(val << 19)
#define STEPCHARGE_INP_AN1	STEPCHARGE_INP(1)
#define STEPCHARGE_RFM_MASK	(3 << 23)
#define STEPCHARGE_RFM(val)	(val << 23)
#define STEPCHARGE_RFM_XNUR	STEPCHARGE_RFM(1)

/* Charge delay */
#define CHARGEDLY_OPEN_MASK	(0x3FFFF << 0)
#define CHARGEDLY_OPEN(val)	(val << 0)
#define CHARGEDLY_OPENDLY	CHARGEDLY_OPEN(1)

/* Control register */
#define CNTRLREG_TSCSSENB	BIT(0)
#define CNTRLREG_STEPID		BIT(1)
#define CNTRLREG_STEPCONFIGWRT	BIT(2)
#define CNTRLREG_AFE_CTRL_MASK	(3 << 5)
#define CNTRLREG_AFE_CTRL(val)	(val << 5)
#define CNTRLREG_4WIRE		CNTRLREG_AFE_CTRL(1)
#define CNTRLREG_5WIRE		CNTRLREG_AFE_CTRL(2)
#define CNTRLREG_8WIRE		CNTRLREG_AFE_CTRL(3)
#define CNTRLREG_TSCENB		BIT(7)

#define ADCFSM_STEPID		0x10
#define SEQ_SETTLE		275
#define ADC_CLK			3000000
#define MAX_12BIT		((1 << 12) - 1)

struct titsc {
	struct input_dev	*input;
	struct clk		*tsc_ick;
	void __iomem		*tsc_base;
	unsigned int		irq;
	unsigned int		wires;
	unsigned int		x_plate_resistance;
	bool			pen_down;
	int			steps_to_configure;
};

static unsigned int titsc_readl(struct titsc *ts, unsigned int reg)
{
	return readl(ts->tsc_base + reg);
}

static void titsc_writel(struct titsc *tsc, unsigned int reg,
					unsigned int val)
{
	writel(val, tsc->tsc_base + reg);
}

static void titsc_step_config(struct titsc *ts_dev)
{
	unsigned int	config;
	int i, total_steps;

	/* Configure the Step registers */
	total_steps = 2 * ts_dev->steps_to_configure;

	config = STEPCONFIG_MODE_HWSYNC |
			STEPCONFIG_AVG_16 | STEPCONFIG_XPP;
	switch (ts_dev->wires) {
	case 4:
		config |= STEPCONFIG_INP_AN2 | STEPCONFIG_XNN;
		break;
	case 5:
		config |= STEPCONFIG_YNN |
				STEPCONFIG_INP_AN4 | STEPCONFIG_XNN |
				STEPCONFIG_YPP;
		break;
	case 8:
		config |= STEPCONFIG_INP_AN2 | STEPCONFIG_XNN;
		break;
	}

	for (i = 1; i <= ts_dev->steps_to_configure; i++) {
		titsc_writel(ts_dev, REG_STEPCONFIG(i), config);
		titsc_writel(ts_dev, REG_STEPDELAY(i), STEPCONFIG_OPENDLY);
	}

	config = 0;
	config = STEPCONFIG_MODE_HWSYNC |
			STEPCONFIG_AVG_16 | STEPCONFIG_YNN |
			STEPCONFIG_INM_ADCREFM | STEPCONFIG_FIFO1;
	switch (ts_dev->wires) {
	case 4:
		config |= STEPCONFIG_YPP;
		break;
	case 5:
		config |= STEPCONFIG_XPP | STEPCONFIG_INP_AN4 |
				STEPCONFIG_XNP | STEPCONFIG_YPN;
		break;
	case 8:
		config |= STEPCONFIG_YPP;
		break;
	}

	for (i = (ts_dev->steps_to_configure + 1); i <= total_steps; i++) {
		titsc_writel(ts_dev, REG_STEPCONFIG(i), config);
		titsc_writel(ts_dev, REG_STEPDELAY(i), STEPCONFIG_OPENDLY);
	}

	config = 0;
	/* Charge step configuration */
	config = STEPCONFIG_XPP | STEPCONFIG_YNN |
			STEPCHARGE_RFP_XPUL | STEPCHARGE_RFM_XNUR |
			STEPCHARGE_INM_AN1 | STEPCHARGE_INP_AN1;

	titsc_writel(ts_dev, REG_CHARGECONFIG, config);
	titsc_writel(ts_dev, REG_CHARGEDELAY, CHARGEDLY_OPENDLY);

	config = 0;
	/* Configure to calculate pressure */
	config = STEPCONFIG_MODE_HWSYNC |
			STEPCONFIG_AVG_16 | STEPCONFIG_YPP |
			STEPCONFIG_XNN | STEPCONFIG_INM_ADCREFM;
	titsc_writel(ts_dev, REG_STEPCONFIG(total_steps + 1), config);
	titsc_writel(ts_dev, REG_STEPDELAY(total_steps + 1),
			STEPCONFIG_OPENDLY);

	config |= STEPCONFIG_INP_AN3 | STEPCONFIG_FIFO1;
	titsc_writel(ts_dev, REG_STEPCONFIG(total_steps + 2), config);
	titsc_writel(ts_dev, REG_STEPDELAY(total_steps + 2),
			STEPCONFIG_OPENDLY);

	titsc_writel(ts_dev, REG_SE, STPENB_STEPENB);
}

static void titsc_idle_config(struct titsc *ts_config)
{
	unsigned int idleconfig;

	idleconfig = STEPCONFIG_YNN |
			STEPCONFIG_INM_ADCREFM |
			STEPCONFIG_YPN | STEPCONFIG_INP_ADCREFM;
	titsc_writel(ts_config, REG_IDLECONFIG, idleconfig);
}

static void titsc_read_coordinates(struct titsc *ts_dev,
				    unsigned int *x, unsigned int *y)
{
	unsigned int fifocount = titsc_readl(ts_dev, REG_FIFO0CNT);
	unsigned int prev_val_x = ~0, prev_val_y = ~0;
	unsigned int prev_diff_x = ~0, prev_diff_y = ~0;
	unsigned int read, diff;
	unsigned int i;

	/*
	 * Delta filter is used to remove large variations in sampled
	 * values from ADC. The filter tries to predict where the next
	 * coordinate could be. This is done by taking a previous
	 * coordinate and subtracting it form current one. Further the
	 * algorithm compares the difference with that of a present value,
	 * if true the value is reported to the sub system.
	 */
	for (i = 0; i < fifocount - 1; i++) {
		read = titsc_readl(ts_dev, REG_FIFO0) & 0xfff;
		diff = abs(read - prev_val_x);
		if (diff < prev_diff_x) {
			prev_diff_x = diff;
			*x = read;
		}
		prev_val_x = read;

		read = titsc_readl(ts_dev, REG_FIFO1) & 0xfff;
		diff = abs(read - prev_val_y);
		if (diff < prev_diff_y) {
			prev_diff_y = diff;
			*y = read;
		}
		prev_val_y = read;
	}
}

static irqreturn_t titsc_irq(int irq, void *dev)
{
	struct titsc *ts_dev = dev;
	struct input_dev *input_dev = ts_dev->input;
	unsigned int status, irqclr = 0;
	unsigned int x = 0, y = 0;
	unsigned int z1, z2, z;
	unsigned int fsm;

	status = titsc_readl(ts_dev, REG_IRQSTATUS);
	if (status & IRQENB_FIFO0THRES) {
		titsc_read_coordinates(ts_dev, &x, &y);

		z1 = titsc_readl(ts_dev, REG_FIFO0) & 0xfff;
		z2 = titsc_readl(ts_dev, REG_FIFO1) & 0xfff;

		if (ts_dev->pen_down && z1 != 0 && z2 != 0) {
			/*
			 * Calculate pressure using formula
			 * Resistance(touch) = x plate resistance *
			 * x postion/4096 * ((z2 / z1) - 1)
			 */
			z = z2 - z1;
			z *= x;
			z *= ts_dev->x_plate_resistance;
			z /= z1;
			z = (z + 2047) >> 12;

			if (z <= MAX_12BIT) {
				input_report_abs(input_dev, ABS_X, x);
				input_report_abs(input_dev, ABS_Y, y);
				input_report_abs(input_dev, ABS_PRESSURE, z);
				input_report_key(input_dev, BTN_TOUCH, 1);
				input_sync(input_dev);
			}
		}
		irqclr |= IRQENB_FIFO0THRES;
	}

	/*
	 * Time for sequencer to settle, to read
	 * correct state of the sequencer.
	 */
	udelay(SEQ_SETTLE);

	status = titsc_readl(ts_dev, REG_RAWIRQSTATUS);
	if (status & IRQENB_PENUP) {
		/* Pen up event */
		fsm = titsc_readl(ts_dev, REG_ADCFSM);
		if (fsm == ADCFSM_STEPID) {
			ts_dev->pen_down = false;
			input_report_key(input_dev, BTN_TOUCH, 0);
			input_report_abs(input_dev, ABS_PRESSURE, 0);
			input_sync(input_dev);
		} else {
			ts_dev->pen_down = true;
		}
		irqclr |= IRQENB_PENUP;
	}

	titsc_writel(ts_dev, REG_IRQSTATUS, irqclr);

	titsc_writel(ts_dev, REG_SE, STPENB_STEPENB);
	return IRQ_HANDLED;
}

/*
 * The functions for inserting/removing driver as a module.
 */

static int __devinit titsc_probe(struct platform_device *pdev)
{
	const struct tsc_data *pdata = pdev->dev.platform_data;
	struct resource *res;
	struct titsc *ts_dev;
	struct input_dev *input_dev;
	struct clk *clk;
	int err;
	int clk_value, ctrl, irq;

	if (!pdata) {
		dev_err(&pdev->dev, "missing platform data.\n");
		return -EINVAL;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "no memory resource defined.\n");
		return -EINVAL;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "no irq ID is specified.\n");
		return -EINVAL;
	}

	/* Allocate memory for device */
	ts_dev = kzalloc(sizeof(struct titsc), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!ts_dev || !input_dev) {
		dev_err(&pdev->dev, "failed to allocate memory.\n");
		err = -ENOMEM;
		goto err_free_mem;
	}

	ts_dev->input = input_dev;
	ts_dev->irq = irq;
	ts_dev->wires = pdata->wires;
	ts_dev->x_plate_resistance = pdata->x_plate_resistance;
	ts_dev->steps_to_configure = pdata->steps_to_configure;

	res = request_mem_region(res->start, resource_size(res), pdev->name);
	if (!res) {
		dev_err(&pdev->dev, "failed to reserve registers.\n");
		err = -EBUSY;
		goto err_free_mem;
	}

	ts_dev->tsc_base = ioremap(res->start, resource_size(res));
	if (!ts_dev->tsc_base) {
		dev_err(&pdev->dev, "failed to map registers.\n");
		err = -ENOMEM;
		goto err_release_mem_region;
	}

	err = request_irq(ts_dev->irq, titsc_irq,
			  0, pdev->dev.driver->name, ts_dev);
	if (err) {
		dev_err(&pdev->dev, "failed to allocate irq.\n");
		goto err_unmap_regs;
	}

	ts_dev->tsc_ick = clk_get(&pdev->dev, "adc_tsc_ick");
	if (IS_ERR(ts_dev->tsc_ick)) {
		dev_err(&pdev->dev, "failed to get TSC ick\n");
		goto err_free_irq;
	}
	clk_enable(ts_dev->tsc_ick);

	clk = clk_get(&pdev->dev, "adc_tsc_fck");
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "failed to get TSC fck\n");
		err = PTR_ERR(clk);
		goto err_disable_clk;
	}

	clk_value = clk_get_rate(clk) / ADC_CLK;
	clk_put(clk);

	if (clk_value < 7) {
		dev_err(&pdev->dev, "clock input less than min clock requirement\n");
		goto err_disable_clk;
	}
	/* CLKDIV needs to be configured to the value minus 1 */
	titsc_writel(ts_dev, REG_CLKDIV, clk_value - 1);

	 /* Enable wake-up of the SoC using touchscreen */
	titsc_writel(ts_dev, REG_IRQWAKEUP, IRQWKUP_ENB);

	ctrl = CNTRLREG_STEPCONFIGWRT |
			CNTRLREG_TSCENB |
			CNTRLREG_STEPID;
	switch (ts_dev->wires) {
	case 4:
		ctrl |= CNTRLREG_4WIRE;
		break;
	case 5:
		ctrl |= CNTRLREG_5WIRE;
		break;
	case 8:
		ctrl |= CNTRLREG_8WIRE;
		break;
	}
	titsc_writel(ts_dev, REG_CTRL, ctrl);

	titsc_idle_config(ts_dev);
	titsc_writel(ts_dev, REG_IRQENABLE, IRQENB_FIFO0THRES);
	titsc_step_config(ts_dev);
	titsc_writel(ts_dev, REG_FIFO0THR, ts_dev->steps_to_configure);

	ctrl |= CNTRLREG_TSCSSENB;
	titsc_writel(ts_dev, REG_CTRL, ctrl);

	input_dev->name = "ti-tsc-adc";
	input_dev->dev.parent = &pdev->dev;

	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);

	input_set_abs_params(input_dev, ABS_X, 0, MAX_12BIT, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, MAX_12BIT, 0, 0);
	input_set_abs_params(input_dev, ABS_PRESSURE, 0, MAX_12BIT, 0, 0);

	/* register to the input system */
	err = input_register_device(input_dev);
	if (err)
		goto err_disable_clk;

	platform_set_drvdata(pdev, ts_dev);
	return 0;

err_disable_clk:
	clk_disable(ts_dev->tsc_ick);
	clk_put(ts_dev->tsc_ick);
err_free_irq:
	free_irq(ts_dev->irq, ts_dev);
err_unmap_regs:
	iounmap(ts_dev->tsc_base);
err_release_mem_region:
	release_mem_region(res->start, resource_size(res));
err_free_mem:
	input_free_device(input_dev);
	kfree(ts_dev);
	return err;
}

static int __devexit titsc_remove(struct platform_device *pdev)
{
	struct titsc *ts_dev = platform_get_drvdata(pdev);
	struct resource *res;

	free_irq(ts_dev->irq, ts_dev);

	input_unregister_device(ts_dev->input);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	iounmap(ts_dev->tsc_base);
	release_mem_region(res->start, resource_size(res));

	clk_disable(ts_dev->tsc_ick);
	clk_put(ts_dev->tsc_ick);

	kfree(ts_dev);

	platform_set_drvdata(pdev, NULL);
	return 0;
}

static struct platform_driver ti_tsc_driver = {
	.probe	= titsc_probe,
	.remove	= __devexit_p(titsc_remove),
	.driver	= {
		.name   = "tsc",
		.owner	= THIS_MODULE,
	},
};
module_platform_driver(ti_tsc_driver);

MODULE_DESCRIPTION("TI touchscreen controller driver");
MODULE_AUTHOR("Rachna Patil <rachna@ti.com>");
MODULE_LICENSE("GPL");
