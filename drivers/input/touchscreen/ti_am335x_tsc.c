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


#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/sort.h>
#include <linux/pm_wakeirq.h>

#include <linux/mfd/ti_am335x_tscadc.h>

#define ADCFSM_STEPID		0x10
#define SEQ_SETTLE		275
#define MAX_12BIT		((1 << 12) - 1)

#define TSC_IRQENB_MASK		(IRQENB_FIFO0THRES | IRQENB_EOS | IRQENB_HW_PEN)

static const int config_pins[] = {
	STEPCONFIG_XPP,
	STEPCONFIG_XNN,
	STEPCONFIG_YPP,
	STEPCONFIG_YNN,
};

struct titsc {
	struct input_dev	*input;
	struct ti_tscadc_dev	*mfd_tscadc;
	struct device		*dev;
	unsigned int		irq;
	unsigned int		wires;
	unsigned int		x_plate_resistance;
	bool			pen_down;
	int			coordinate_readouts;
	u32			config_inp[4];
	u32			bit_xp, bit_xn, bit_yp, bit_yn;
	u32			inp_xp, inp_xn, inp_yp, inp_yn;
	u32			step_mask;
	u32			charge_delay;
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
	int i, n;
	int end_step, first_step, tsc_steps;
	u32 stepenable;

	config = STEPCONFIG_MODE_HWSYNC |
			STEPCONFIG_AVG_16 | ts_dev->bit_xp |
			STEPCONFIG_INM_ADCREFM;
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

	tsc_steps = ts_dev->coordinate_readouts * 2 + 2;
	first_step = TOTAL_STEPS - tsc_steps;
	/* Steps 16 to 16-coordinate_readouts is for X */
	end_step = first_step + tsc_steps;
	n = 0;
	for (i = end_step - ts_dev->coordinate_readouts; i < end_step; i++) {
		titsc_writel(ts_dev, REG_STEPCONFIG(i), config);
		titsc_writel(ts_dev, REG_STEPDELAY(i),
			     n++ == 0 ? STEPCONFIG_OPENDLY : 0);
	}

	config = 0;
	config = STEPCONFIG_MODE_HWSYNC |
			STEPCONFIG_AVG_16 | ts_dev->bit_yn |
			STEPCONFIG_INM_ADCREFM;
	switch (ts_dev->wires) {
	case 4:
		config |= ts_dev->bit_yp | STEPCONFIG_INP(ts_dev->inp_xp);
		break;
	case 5:
		config |= ts_dev->bit_xp | STEPCONFIG_INP_AN4 |
				STEPCONFIG_XNP | STEPCONFIG_YPN;
		break;
	case 8:
		config |= ts_dev->bit_yp | STEPCONFIG_INP(ts_dev->inp_xp);
		break;
	}

	/* 1 ... coordinate_readouts is for Y */
	end_step = first_step + ts_dev->coordinate_readouts;
	n = 0;
	for (i = first_step; i < end_step; i++) {
		titsc_writel(ts_dev, REG_STEPCONFIG(i), config);
		titsc_writel(ts_dev, REG_STEPDELAY(i),
			     n++ == 0 ? STEPCONFIG_OPENDLY : 0);
	}

	/* Make CHARGECONFIG same as IDLECONFIG */

	config = titsc_readl(ts_dev, REG_IDLECONFIG);
	titsc_writel(ts_dev, REG_CHARGECONFIG, config);
	titsc_writel(ts_dev, REG_CHARGEDELAY, ts_dev->charge_delay);

	/* coordinate_readouts + 1 ... coordinate_readouts + 2 is for Z */
	config = STEPCONFIG_MODE_HWSYNC |
			STEPCONFIG_AVG_16 | ts_dev->bit_yp |
			ts_dev->bit_xn | STEPCONFIG_INM_ADCREFM |
			STEPCONFIG_INP(ts_dev->inp_xp);
	titsc_writel(ts_dev, REG_STEPCONFIG(end_step), config);
	titsc_writel(ts_dev, REG_STEPDELAY(end_step),
			STEPCONFIG_OPENDLY);

	end_step++;
	config = STEPCONFIG_MODE_HWSYNC |
			STEPCONFIG_AVG_16 | ts_dev->bit_yp |
			ts_dev->bit_xn | STEPCONFIG_INM_ADCREFM |
			STEPCONFIG_INP(ts_dev->inp_yn);
	titsc_writel(ts_dev, REG_STEPCONFIG(end_step), config);
	titsc_writel(ts_dev, REG_STEPDELAY(end_step),
			STEPCONFIG_OPENDLY);

	/* The steps end ... end - readouts * 2 + 2 and bit 0 for TS_Charge */
	stepenable = 1;
	for (i = 0; i < tsc_steps; i++)
		stepenable |= 1 << (first_step + i + 1);

	ts_dev->step_mask = stepenable;
	am335x_tsc_se_set_cache(ts_dev->mfd_tscadc, ts_dev->step_mask);
}

static int titsc_cmp_coord(const void *a, const void *b)
{
	return *(int *)a - *(int *)b;
}

static void titsc_read_coordinates(struct titsc *ts_dev,
		u32 *x, u32 *y, u32 *z1, u32 *z2)
{
	unsigned int yvals[7], xvals[7];
	unsigned int i, xsum = 0, ysum = 0;
	unsigned int creads = ts_dev->coordinate_readouts;

	for (i = 0; i < creads; i++) {
		yvals[i] = titsc_readl(ts_dev, REG_FIFO0);
		yvals[i] &= 0xfff;
	}

	*z1 = titsc_readl(ts_dev, REG_FIFO0);
	*z1 &= 0xfff;
	*z2 = titsc_readl(ts_dev, REG_FIFO0);
	*z2 &= 0xfff;

	for (i = 0; i < creads; i++) {
		xvals[i] = titsc_readl(ts_dev, REG_FIFO0);
		xvals[i] &= 0xfff;
	}

	/*
	 * If co-ordinates readouts is less than 4 then
	 * report the average. In case of 4 or more
	 * readouts, sort the co-ordinate samples, drop
	 * min and max values and report the average of
	 * remaining values.
	 */
	if (creads <=  3) {
		for (i = 0; i < creads; i++) {
			ysum += yvals[i];
			xsum += xvals[i];
		}
		ysum /= creads;
		xsum /= creads;
	} else {
		sort(yvals, creads, sizeof(unsigned int),
		     titsc_cmp_coord, NULL);
		sort(xvals, creads, sizeof(unsigned int),
		     titsc_cmp_coord, NULL);
		for (i = 1; i < creads - 1; i++) {
			ysum += yvals[i];
			xsum += xvals[i];
		}
		ysum /= creads - 2;
		xsum /= creads - 2;
	}
	*y = ysum;
	*x = xsum;
}

static irqreturn_t titsc_irq(int irq, void *dev)
{
	struct titsc *ts_dev = dev;
	struct input_dev *input_dev = ts_dev->input;
	unsigned int fsm, status, irqclr = 0;
	unsigned int x = 0, y = 0;
	unsigned int z1, z2, z;

	status = titsc_readl(ts_dev, REG_RAWIRQSTATUS);
	if (status & IRQENB_HW_PEN) {
		ts_dev->pen_down = true;
		irqclr |= IRQENB_HW_PEN;
		pm_stay_awake(ts_dev->dev);
	}

	if (status & IRQENB_PENUP) {
		fsm = titsc_readl(ts_dev, REG_ADCFSM);
		if (fsm == ADCFSM_STEPID) {
			ts_dev->pen_down = false;
			input_report_key(input_dev, BTN_TOUCH, 0);
			input_report_abs(input_dev, ABS_PRESSURE, 0);
			input_sync(input_dev);
			pm_relax(ts_dev->dev);
		} else {
			ts_dev->pen_down = true;
		}
		irqclr |= IRQENB_PENUP;
	}

	if (status & IRQENB_EOS)
		irqclr |= IRQENB_EOS;

	/*
	 * ADC and touchscreen share the IRQ line.
	 * FIFO1 interrupts are used by ADC. Handle FIFO0 IRQs here only
	 */
	if (status & IRQENB_FIFO0THRES) {

		titsc_read_coordinates(ts_dev, &x, &y, &z1, &z2);

		if (ts_dev->pen_down && z1 != 0 && z2 != 0) {
			/*
			 * Calculate pressure using formula
			 * Resistance(touch) = x plate resistance *
			 * x position/4096 * ((z2 / z1) - 1)
			 */
			z = z1 - z2;
			z *= x;
			z *= ts_dev->x_plate_resistance;
			z /= z2;
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
	if (irqclr) {
		titsc_writel(ts_dev, REG_IRQSTATUS, irqclr);
		if (status & IRQENB_EOS)
			am335x_tsc_se_set_cache(ts_dev->mfd_tscadc,
						ts_dev->step_mask);
		return IRQ_HANDLED;
	}
	return IRQ_NONE;
}

static int titsc_parse_dt(struct platform_device *pdev,
					struct titsc *ts_dev)
{
	struct device_node *node = pdev->dev.of_node;
	int err;

	if (!node)
		return -EINVAL;

	err = of_property_read_u32(node, "ti,wires", &ts_dev->wires);
	if (err < 0)
		return err;
	switch (ts_dev->wires) {
	case 4:
	case 5:
	case 8:
		break;
	default:
		return -EINVAL;
	}

	err = of_property_read_u32(node, "ti,x-plate-resistance",
			&ts_dev->x_plate_resistance);
	if (err < 0)
		return err;

	/*
	 * Try with the new binding first. If it fails, try again with
	 * bogus, miss-spelled version.
	 */
	err = of_property_read_u32(node, "ti,coordinate-readouts",
			&ts_dev->coordinate_readouts);
	if (err < 0) {
		dev_warn(&pdev->dev, "please use 'ti,coordinate-readouts' instead\n");
		err = of_property_read_u32(node, "ti,coordiante-readouts",
				&ts_dev->coordinate_readouts);
	}

	if (err < 0)
		return err;

	if (ts_dev->coordinate_readouts <= 0) {
		dev_warn(&pdev->dev,
			 "invalid co-ordinate readouts, resetting it to 5\n");
		ts_dev->coordinate_readouts = 5;
	}

	err = of_property_read_u32(node, "ti,charge-delay",
				   &ts_dev->charge_delay);
	/*
	 * If ti,charge-delay value is not specified, then use
	 * CHARGEDLY_OPENDLY as the default value.
	 */
	if (err < 0) {
		ts_dev->charge_delay = CHARGEDLY_OPENDLY;
		dev_warn(&pdev->dev, "ti,charge-delay not specified\n");
	}

	return of_property_read_u32_array(node, "ti,wire-config",
			ts_dev->config_inp, ARRAY_SIZE(ts_dev->config_inp));
}

/*
 * The functions for inserting/removing driver as a module.
 */

static int titsc_probe(struct platform_device *pdev)
{
	struct titsc *ts_dev;
	struct input_dev *input_dev;
	struct ti_tscadc_dev *tscadc_dev = ti_tscadc_dev_get(pdev);
	int err;

	/* Allocate memory for device */
	ts_dev = kzalloc(sizeof(*ts_dev), GFP_KERNEL);
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
	ts_dev->dev = &pdev->dev;

	err = titsc_parse_dt(pdev, ts_dev);
	if (err) {
		dev_err(&pdev->dev, "Could not find valid DT data.\n");
		goto err_free_mem;
	}

	err = request_irq(ts_dev->irq, titsc_irq,
			  IRQF_SHARED, pdev->dev.driver->name, ts_dev);
	if (err) {
		dev_err(&pdev->dev, "failed to allocate irq.\n");
		goto err_free_mem;
	}

	device_init_wakeup(&pdev->dev, true);
	err = dev_pm_set_wake_irq(&pdev->dev, ts_dev->irq);
	if (err)
		dev_err(&pdev->dev, "irq wake enable failed.\n");

	titsc_writel(ts_dev, REG_IRQSTATUS, TSC_IRQENB_MASK);
	titsc_writel(ts_dev, REG_IRQENABLE, IRQENB_FIFO0THRES);
	titsc_writel(ts_dev, REG_IRQENABLE, IRQENB_EOS);
	err = titsc_config_wires(ts_dev);
	if (err) {
		dev_err(&pdev->dev, "wrong i/p wire configuration\n");
		goto err_free_irq;
	}
	titsc_step_config(ts_dev);
	titsc_writel(ts_dev, REG_FIFO0THR,
			ts_dev->coordinate_readouts * 2 + 2 - 1);

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
	dev_pm_clear_wake_irq(&pdev->dev);
	device_init_wakeup(&pdev->dev, false);
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

	dev_pm_clear_wake_irq(&pdev->dev);
	device_init_wakeup(&pdev->dev, false);
	free_irq(ts_dev->irq, ts_dev);

	/* total steps followed by the enable mask */
	steps = 2 * ts_dev->coordinate_readouts + 2;
	steps = (1 << steps) - 1;
	am335x_tsc_se_clr(ts_dev->mfd_tscadc, steps);

	input_unregister_device(ts_dev->input);

	kfree(ts_dev);
	return 0;
}

static int titsc_suspend(struct device *dev)
{
	struct titsc *ts_dev = dev_get_drvdata(dev);
	unsigned int idle;

	if (device_may_wakeup(dev)) {
		titsc_writel(ts_dev, REG_IRQSTATUS, TSC_IRQENB_MASK);
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

	if (device_may_wakeup(dev)) {
		titsc_writel(ts_dev, REG_IRQWAKEUP,
				0x00);
		titsc_writel(ts_dev, REG_IRQCLR, IRQENB_HW_PEN);
		pm_relax(dev);
	}
	titsc_step_config(ts_dev);
	titsc_writel(ts_dev, REG_FIFO0THR,
			ts_dev->coordinate_readouts * 2 + 2 - 1);
	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(titsc_pm_ops, titsc_suspend, titsc_resume);

static const struct of_device_id ti_tsc_dt_ids[] = {
	{ .compatible = "ti,am3359-tsc", },
	{ }
};
MODULE_DEVICE_TABLE(of, ti_tsc_dt_ids);

static struct platform_driver ti_tsc_driver = {
	.probe	= titsc_probe,
	.remove	= titsc_remove,
	.driver	= {
		.name   = "TI-am335x-tsc",
		.pm	= pm_sleep_ptr(&titsc_pm_ops),
		.of_match_table = ti_tsc_dt_ids,
	},
};
module_platform_driver(ti_tsc_driver);

MODULE_DESCRIPTION("TI touchscreen controller driver");
MODULE_AUTHOR("Rachna Patil <rachna@ti.com>");
MODULE_LICENSE("GPL");
