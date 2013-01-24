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

#include <linux/mfd/ti_am335x_tscadc.h>

#define ADCFSM_STEPID		0x10
#define SEQ_SETTLE		275
#define MAX_12BIT		((1 << 12) - 1)

static const int config_pins[] = {
	STEPCONFIG_XPP,
	STEPCONFIG_XNN,
	STEPCONFIG_YPP,
	STEPCONFIG_YNN,
};

struct titsc {
	struct input_dev	*input;
	struct ti_tscadc_dev	*mfd_tscadc;
	unsigned int		irq;
	unsigned int		wires;
	unsigned int		x_plate_resistance;
	bool			pen_down;
	int			steps_to_configure;
	u32			config_inp[4];
	u32			bit_xp, bit_xn, bit_yp, bit_yn;
	u32			inp_xp, inp_xn, inp_yp, inp_yn;
};

static unsigned int titsc_readl(struct titsc *ts, unsigned int reg)
{
	return readl(ts->mfd_tscadc->tscadc_base + reg);
}

static void titsc_writel(struct titsc *tsc, unsigned int reg,
					unsigned int val)
{
	writel(val, tsc->mfd_tscadc->tscadc_base + reg);
}

static int titsc_config_wires(struct titsc *ts_dev)
{
	u32 analog_line[4];
	u32 wire_order[4];
	int i, bit_cfg;

	for (i = 0; i < 4; i++) {
		/*
		 * Get the order in which TSC wires are attached
		 * w.r.t. each of the analog input lines on the EVM.
		 */
		analog_line[i] = (ts_dev->config_inp[i] & 0xF0) >> 4;
		wire_order[i] = ts_dev->config_inp[i] & 0x0F;
		if (WARN_ON(analog_line[i] > 7))
			return -EINVAL;
		if (WARN_ON(wire_order[i] > ARRAY_SIZE(config_pins)))
			return -EINVAL;
	}

	for (i = 0; i < 4; i++) {
		int an_line;
		int wi_order;

		an_line = analog_line[i];
		wi_order = wire_order[i];
		bit_cfg = config_pins[wi_order];
		if (bit_cfg == 0)
			return -EINVAL;
		switch (wi_order) {
		case 0:
			ts_dev->bit_xp = bit_cfg;
			ts_dev->inp_xp = an_line;
			break;

		case 1:
			ts_dev->bit_xn = bit_cfg;
			ts_dev->inp_xn = an_line;
			break;

		case 2:
			ts_dev->bit_yp = bit_cfg;
			ts_dev->inp_yp = an_line;
			break;
		case 3:
			ts_dev->bit_yn = bit_cfg;
			ts_dev->inp_yn = an_line;
			break;
		}
	}
	return 0;
}

static void titsc_step_config(struct titsc *ts_dev)
{
	unsigned int	config;
	unsigned int	stepenable = 0;
	int i, total_steps;

	/* Configure the Step registers */
	total_steps = 2 * ts_dev->steps_to_configure;

	config = STEPCONFIG_MODE_HWSYNC |
			STEPCONFIG_AVG_16 | ts_dev->bit_xp;
	switch (ts_dev->wires) {
	case 4:
		config |= STEPCONFIG_INP(ts_dev->inp_yp) | ts_dev->bit_xn;
		break;
	case 5:
		config |= ts_dev->bit_yn |
				STEPCONFIG_INP_AN4 | ts_dev->bit_xn |
				ts_dev->bit_yp;
		break;
	case 8:
		config |= STEPCONFIG_INP(ts_dev->inp_yp) | ts_dev->bit_xn;
		break;
	}

	for (i = 1; i <= ts_dev->steps_to_configure; i++) {
		titsc_writel(ts_dev, REG_STEPCONFIG(i), config);
		titsc_writel(ts_dev, REG_STEPDELAY(i), STEPCONFIG_OPENDLY);
	}

	config = 0;
	config = STEPCONFIG_MODE_HWSYNC |
			STEPCONFIG_AVG_16 | ts_dev->bit_yn |
			STEPCONFIG_INM_ADCREFM | STEPCONFIG_FIFO1;
	switch (ts_dev->wires) {
	case 4:
		config |= ts_dev->bit_yp | STEPCONFIG_INP(ts_dev->inp_xp);
		break;
	case 5:
		config |= ts_dev->bit_xp | STEPCONFIG_INP_AN4 |
				ts_dev->bit_xn | ts_dev->bit_yp;
		break;
	case 8:
		config |= ts_dev->bit_yp | STEPCONFIG_INP(ts_dev->inp_xp);
		break;
	}

	for (i = (ts_dev->steps_to_configure + 1); i <= total_steps; i++) {
		titsc_writel(ts_dev, REG_STEPCONFIG(i), config);
		titsc_writel(ts_dev, REG_STEPDELAY(i), STEPCONFIG_OPENDLY);
	}

	config = 0;
	/* Charge step configuration */
	config = ts_dev->bit_xp | ts_dev->bit_yn |
			STEPCHARGE_RFP_XPUL | STEPCHARGE_RFM_XNUR |
			STEPCHARGE_INM_AN1 | STEPCHARGE_INP(ts_dev->inp_yp);

	titsc_writel(ts_dev, REG_CHARGECONFIG, config);
	titsc_writel(ts_dev, REG_CHARGEDELAY, CHARGEDLY_OPENDLY);

	config = 0;
	/* Configure to calculate pressure */
	config = STEPCONFIG_MODE_HWSYNC |
			STEPCONFIG_AVG_16 | ts_dev->bit_yp |
			ts_dev->bit_xn | STEPCONFIG_INM_ADCREFM |
			STEPCONFIG_INP(ts_dev->inp_xp);
	titsc_writel(ts_dev, REG_STEPCONFIG(total_steps + 1), config);
	titsc_writel(ts_dev, REG_STEPDELAY(total_steps + 1),
			STEPCONFIG_OPENDLY);

	config |= STEPCONFIG_INP(ts_dev->inp_yn) | STEPCONFIG_FIFO1;
	titsc_writel(ts_dev, REG_STEPCONFIG(total_steps + 2), config);
	titsc_writel(ts_dev, REG_STEPDELAY(total_steps + 2),
			STEPCONFIG_OPENDLY);

	/* The steps1 … end and bit 0 for TS_Charge */
	stepenable = (1 << (total_steps + 2)) - 1;
	am335x_tsc_se_set(ts_dev->mfd_tscadc, stepenable);
}

static void titsc_read_coordinates(struct titsc *ts_dev,
				    unsigned int *x, unsigned int *y)
{
	unsigned int fifocount = titsc_readl(ts_dev, REG_FIFO0CNT);
	unsigned int prev_val_x = ~0, prev_val_y = ~0;
	unsigned int prev_diff_x = ~0, prev_diff_y = ~0;
	unsigned int read, diff;
	unsigned int i, channel;

	/*
	 * Delta filter is used to remove large variations in sampled
	 * values from ADC. The filter tries to predict where the next
	 * coordinate could be. This is done by taking a previous
	 * coordinate and subtracting it form current one. Further the
	 * algorithm compares the difference with that of a present value,
	 * if true the value is reported to the sub system.
	 */
	for (i = 0; i < fifocount - 1; i++) {
		read = titsc_readl(ts_dev, REG_FIFO0);
		channel = read & 0xf0000;
		channel = channel >> 0x10;
		if ((channel >= 0) && (channel < ts_dev->steps_to_configure)) {
			read &= 0xfff;
			diff = abs(read - prev_val_x);
			if (diff < prev_diff_x) {
				prev_diff_x = diff;
				*x = read;
			}
			prev_val_x = read;
		}

		read = titsc_readl(ts_dev, REG_FIFO1);
		channel = read & 0xf0000;
		channel = channel >> 0x10;
		if ((channel >= ts_dev->steps_to_configure) &&
			(channel < (2 * ts_dev->steps_to_configure - 1))) {
			read &= 0xfff;
			diff = abs(read - prev_val_y);
			if (diff < prev_diff_y) {
				prev_diff_y = diff;
				*y = read;
			}
			prev_val_y = read;
		}
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
	unsigned int fifo1count, fifo0count;
	int i;

	status = titsc_readl(ts_dev, REG_IRQSTATUS);
	if (status & IRQENB_FIFO0THRES) {
		titsc_read_coordinates(ts_dev, &x, &y);

		z1 = titsc_readl(ts_dev, REG_FIFO0) & 0xfff;
		z2 = titsc_readl(ts_dev, REG_FIFO1) & 0xfff;

		fifo1count = titsc_readl(ts_dev, REG_FIFO1CNT);
		for (i = 0; i < fifo1count; i++)
			titsc_readl(ts_dev, REG_FIFO1);

		fifo0count = titsc_readl(ts_dev, REG_FIFO0CNT);
		for (i = 0; i < fifo0count; i++)
			titsc_readl(ts_dev, REG_FIFO0);

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

	am335x_tsc_se_update(ts_dev->mfd_tscadc);
	return IRQ_HANDLED;
}

/*
 * The functions for inserting/removing driver as a module.
 */

static int titsc_probe(struct platform_device *pdev)
{
	struct titsc *ts_dev;
	struct input_dev *input_dev;
	struct ti_tscadc_dev *tscadc_dev = ti_tscadc_dev_get(pdev);
	struct mfd_tscadc_board	*pdata;
	int err;

	pdata = tscadc_dev->dev->platform_data;

	if (!pdata) {
		dev_err(&pdev->dev, "Could not find platform data\n");
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

	tscadc_dev->tsc = ts_dev;
	ts_dev->mfd_tscadc = tscadc_dev;
	ts_dev->input = input_dev;
	ts_dev->irq = tscadc_dev->irq;
	ts_dev->wires = pdata->tsc_init->wires;
	ts_dev->x_plate_resistance = pdata->tsc_init->x_plate_resistance;
	ts_dev->steps_to_configure = pdata->tsc_init->steps_to_configure;
	memcpy(ts_dev->config_inp, pdata->tsc_init->wire_config,
			sizeof(pdata->tsc_init->wire_config));

	err = request_irq(ts_dev->irq, titsc_irq,
			  0, pdev->dev.driver->name, ts_dev);
	if (err) {
		dev_err(&pdev->dev, "failed to allocate irq.\n");
		goto err_free_mem;
	}

	titsc_writel(ts_dev, REG_IRQENABLE, IRQENB_FIFO0THRES);
	err = titsc_config_wires(ts_dev);
	if (err) {
		dev_err(&pdev->dev, "wrong i/p wire configuration\n");
		goto err_free_irq;
	}
	titsc_step_config(ts_dev);
	titsc_writel(ts_dev, REG_FIFO0THR, ts_dev->steps_to_configure);

	input_dev->name = "ti-tsc";
	input_dev->dev.parent = &pdev->dev;

	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);

	input_set_abs_params(input_dev, ABS_X, 0, MAX_12BIT, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, MAX_12BIT, 0, 0);
	input_set_abs_params(input_dev, ABS_PRESSURE, 0, MAX_12BIT, 0, 0);

	/* register to the input system */
	err = input_register_device(input_dev);
	if (err)
		goto err_free_irq;

	platform_set_drvdata(pdev, ts_dev);
	return 0;

err_free_irq:
	free_irq(ts_dev->irq, ts_dev);
err_free_mem:
	input_free_device(input_dev);
	kfree(ts_dev);
	return err;
}

static int titsc_remove(struct platform_device *pdev)
{
	struct titsc *ts_dev = platform_get_drvdata(pdev);
	u32 steps;

	free_irq(ts_dev->irq, ts_dev);

	/* total steps followed by the enable mask */
	steps = 2 * ts_dev->steps_to_configure + 2;
	steps = (1 << steps) - 1;
	am335x_tsc_se_clr(ts_dev->mfd_tscadc, steps);

	input_unregister_device(ts_dev->input);

	platform_set_drvdata(pdev, NULL);
	kfree(ts_dev);
	return 0;
}

#ifdef CONFIG_PM
static int titsc_suspend(struct device *dev)
{
	struct titsc *ts_dev = dev_get_drvdata(dev);
	struct ti_tscadc_dev *tscadc_dev;
	unsigned int idle;

	tscadc_dev = ti_tscadc_dev_get(to_platform_device(dev));
	if (device_may_wakeup(tscadc_dev->dev)) {
		idle = titsc_readl(ts_dev, REG_IRQENABLE);
		titsc_writel(ts_dev, REG_IRQENABLE,
				(idle | IRQENB_HW_PEN));
		titsc_writel(ts_dev, REG_IRQWAKEUP, IRQWKUP_ENB);
	}
	return 0;
}

static int titsc_resume(struct device *dev)
{
	struct titsc *ts_dev = dev_get_drvdata(dev);
	struct ti_tscadc_dev *tscadc_dev;

	tscadc_dev = ti_tscadc_dev_get(to_platform_device(dev));
	if (device_may_wakeup(tscadc_dev->dev)) {
		titsc_writel(ts_dev, REG_IRQWAKEUP,
				0x00);
		titsc_writel(ts_dev, REG_IRQCLR, IRQENB_HW_PEN);
	}
	titsc_step_config(ts_dev);
	titsc_writel(ts_dev, REG_FIFO0THR,
			ts_dev->steps_to_configure);
	return 0;
}

static const struct dev_pm_ops titsc_pm_ops = {
	.suspend = titsc_suspend,
	.resume  = titsc_resume,
};
#define TITSC_PM_OPS (&titsc_pm_ops)
#else
#define TITSC_PM_OPS NULL
#endif

static struct platform_driver ti_tsc_driver = {
	.probe	= titsc_probe,
	.remove	= titsc_remove,
	.driver	= {
		.name   = "tsc",
		.owner	= THIS_MODULE,
		.pm	= TITSC_PM_OPS,
	},
};
module_platform_driver(ti_tsc_driver);

MODULE_DESCRIPTION("TI touchscreen controller driver");
MODULE_AUTHOR("Rachna Patil <rachna@ti.com>");
MODULE_LICENSE("GPL");
