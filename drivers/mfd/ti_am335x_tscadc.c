/*
 * TI Touch Screen / ADC MFD driver
 *
 * Copyright (C) 2012 Texas Instruments Incorporated - http://www.ti.com/
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

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/regmap.h>
#include <linux/mfd/core.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/sched.h>

#include <linux/mfd/ti_am335x_tscadc.h>

static unsigned int tscadc_readl(struct ti_tscadc_dev *tsadc, unsigned int reg)
{
	unsigned int val;

	regmap_read(tsadc->regmap_tscadc, reg, &val);
	return val;
}

static void tscadc_writel(struct ti_tscadc_dev *tsadc, unsigned int reg,
					unsigned int val)
{
	regmap_write(tsadc->regmap_tscadc, reg, val);
}

static const struct regmap_config tscadc_regmap_config = {
	.name = "ti_tscadc",
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
};

void am335x_tsc_se_set_cache(struct ti_tscadc_dev *tsadc, u32 val)
{
	unsigned long flags;

	spin_lock_irqsave(&tsadc->reg_lock, flags);
	tsadc->reg_se_cache |= val;
	if (tsadc->adc_waiting)
		wake_up(&tsadc->reg_se_wait);
	else if (!tsadc->adc_in_use)
		tscadc_writel(tsadc, REG_SE, tsadc->reg_se_cache);

	spin_unlock_irqrestore(&tsadc->reg_lock, flags);
}
EXPORT_SYMBOL_GPL(am335x_tsc_se_set_cache);

static void am335x_tscadc_need_adc(struct ti_tscadc_dev *tsadc)
{
	DEFINE_WAIT(wait);
	u32 reg;

	/*
	 * disable TSC steps so it does not run while the ADC is using it. If
	 * write 0 while it is running (it just started or was already running)
	 * then it completes all steps that were enabled and stops then.
	 */
	tscadc_writel(tsadc, REG_SE, 0);
	reg = tscadc_readl(tsadc, REG_ADCFSM);
	if (reg & SEQ_STATUS) {
		tsadc->adc_waiting = true;
		prepare_to_wait(&tsadc->reg_se_wait, &wait,
				TASK_UNINTERRUPTIBLE);
		spin_unlock_irq(&tsadc->reg_lock);

		schedule();

		spin_lock_irq(&tsadc->reg_lock);
		finish_wait(&tsadc->reg_se_wait, &wait);

		reg = tscadc_readl(tsadc, REG_ADCFSM);
		WARN_ON(reg & SEQ_STATUS);
		tsadc->adc_waiting = false;
	}
	tsadc->adc_in_use = true;
}

void am335x_tsc_se_set_once(struct ti_tscadc_dev *tsadc, u32 val)
{
	spin_lock_irq(&tsadc->reg_lock);
	tsadc->reg_se_cache |= val;
	am335x_tscadc_need_adc(tsadc);

	tscadc_writel(tsadc, REG_SE, val);
	spin_unlock_irq(&tsadc->reg_lock);
}
EXPORT_SYMBOL_GPL(am335x_tsc_se_set_once);

void am335x_tsc_se_adc_done(struct ti_tscadc_dev *tsadc)
{
	unsigned long flags;

	spin_lock_irqsave(&tsadc->reg_lock, flags);
	tsadc->adc_in_use = false;
	tscadc_writel(tsadc, REG_SE, tsadc->reg_se_cache);
	spin_unlock_irqrestore(&tsadc->reg_lock, flags);
}
EXPORT_SYMBOL_GPL(am335x_tsc_se_adc_done);

void am335x_tsc_se_clr(struct ti_tscadc_dev *tsadc, u32 val)
{
	unsigned long flags;

	spin_lock_irqsave(&tsadc->reg_lock, flags);
	tsadc->reg_se_cache &= ~val;
	tscadc_writel(tsadc, REG_SE, tsadc->reg_se_cache);
	spin_unlock_irqrestore(&tsadc->reg_lock, flags);
}
EXPORT_SYMBOL_GPL(am335x_tsc_se_clr);

static void tscadc_idle_config(struct ti_tscadc_dev *config)
{
	unsigned int idleconfig;

	idleconfig = STEPCONFIG_YNN | STEPCONFIG_INM_ADCREFM |
			STEPCONFIG_INP_ADCREFM | STEPCONFIG_YPN;

	tscadc_writel(config, REG_IDLECONFIG, idleconfig);
}

static	int ti_tscadc_probe(struct platform_device *pdev)
{
	struct ti_tscadc_dev	*tscadc;
	struct resource		*res;
	struct clk		*clk;
	struct device_node	*node = pdev->dev.of_node;
	struct mfd_cell		*cell;
	struct property         *prop;
	const __be32            *cur;
	u32			val;
	int			err, ctrl;
	int			clock_rate;
	int			tsc_wires = 0, adc_channels = 0, total_channels;
	int			readouts = 0;

	if (!pdev->dev.of_node) {
		dev_err(&pdev->dev, "Could not find valid DT data.\n");
		return -EINVAL;
	}

	node = of_get_child_by_name(pdev->dev.of_node, "tsc");
	of_property_read_u32(node, "ti,wires", &tsc_wires);
	of_property_read_u32(node, "ti,coordiante-readouts", &readouts);

	node = of_get_child_by_name(pdev->dev.of_node, "adc");
	of_property_for_each_u32(node, "ti,adc-channels", prop, cur, val) {
		adc_channels++;
		if (val > 7) {
			dev_err(&pdev->dev, " PIN numbers are 0..7 (not %d)\n",
					val);
			return -EINVAL;
		}
	}
	total_channels = tsc_wires + adc_channels;
	if (total_channels > 8) {
		dev_err(&pdev->dev, "Number of i/p channels more than 8\n");
		return -EINVAL;
	}
	if (total_channels == 0) {
		dev_err(&pdev->dev, "Need atleast one channel.\n");
		return -EINVAL;
	}

	if (readouts * 2 + 2 + adc_channels > 16) {
		dev_err(&pdev->dev, "Too many step configurations requested\n");
		return -EINVAL;
	}

	/* Allocate memory for device */
	tscadc = devm_kzalloc(&pdev->dev,
			sizeof(struct ti_tscadc_dev), GFP_KERNEL);
	if (!tscadc) {
		dev_err(&pdev->dev, "failed to allocate memory.\n");
		return -ENOMEM;
	}
	tscadc->dev = &pdev->dev;

	err = platform_get_irq(pdev, 0);
	if (err < 0) {
		dev_err(&pdev->dev, "no irq ID is specified.\n");
		goto ret;
	} else
		tscadc->irq = err;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	tscadc->tscadc_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(tscadc->tscadc_base))
		return PTR_ERR(tscadc->tscadc_base);

	tscadc->regmap_tscadc = devm_regmap_init_mmio(&pdev->dev,
			tscadc->tscadc_base, &tscadc_regmap_config);
	if (IS_ERR(tscadc->regmap_tscadc)) {
		dev_err(&pdev->dev, "regmap init failed\n");
		err = PTR_ERR(tscadc->regmap_tscadc);
		goto ret;
	}

	spin_lock_init(&tscadc->reg_lock);
	init_waitqueue_head(&tscadc->reg_se_wait);

	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	/*
	 * The TSC_ADC_Subsystem has 2 clock domains
	 * OCP_CLK and ADC_CLK.
	 * The ADC clock is expected to run at target of 3MHz,
	 * and expected to capture 12-bit data at a rate of 200 KSPS.
	 * The TSC_ADC_SS controller design assumes the OCP clock is
	 * at least 6x faster than the ADC clock.
	 */
	clk = clk_get(&pdev->dev, "adc_tsc_fck");
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "failed to get TSC fck\n");
		err = PTR_ERR(clk);
		goto err_disable_clk;
	}
	clock_rate = clk_get_rate(clk);
	clk_put(clk);
	tscadc->clk_div = clock_rate / ADC_CLK;

	/* TSCADC_CLKDIV needs to be configured to the value minus 1 */
	tscadc->clk_div--;
	tscadc_writel(tscadc, REG_CLKDIV, tscadc->clk_div);

	/* Set the control register bits */
	ctrl = CNTRLREG_STEPCONFIGWRT |	CNTRLREG_STEPID;
	tscadc_writel(tscadc, REG_CTRL, ctrl);

	/* Set register bits for Idle Config Mode */
	if (tsc_wires > 0) {
		tscadc->tsc_wires = tsc_wires;
		if (tsc_wires == 5)
			ctrl |= CNTRLREG_5WIRE | CNTRLREG_TSCENB;
		else
			ctrl |= CNTRLREG_4WIRE | CNTRLREG_TSCENB;
		tscadc_idle_config(tscadc);
	}

	/* Enable the TSC module enable bit */
	ctrl |= CNTRLREG_TSCSSENB;
	tscadc_writel(tscadc, REG_CTRL, ctrl);

	tscadc->used_cells = 0;
	tscadc->tsc_cell = -1;
	tscadc->adc_cell = -1;

	/* TSC Cell */
	if (tsc_wires > 0) {
		tscadc->tsc_cell = tscadc->used_cells;
		cell = &tscadc->cells[tscadc->used_cells++];
		cell->name = "TI-am335x-tsc";
		cell->of_compatible = "ti,am3359-tsc";
		cell->platform_data = &tscadc;
		cell->pdata_size = sizeof(tscadc);
	}

	/* ADC Cell */
	if (adc_channels > 0) {
		tscadc->adc_cell = tscadc->used_cells;
		cell = &tscadc->cells[tscadc->used_cells++];
		cell->name = "TI-am335x-adc";
		cell->of_compatible = "ti,am3359-adc";
		cell->platform_data = &tscadc;
		cell->pdata_size = sizeof(tscadc);
	}

	err = mfd_add_devices(&pdev->dev, pdev->id, tscadc->cells,
			tscadc->used_cells, NULL, 0, NULL);
	if (err < 0)
		goto err_disable_clk;

	device_init_wakeup(&pdev->dev, true);
	platform_set_drvdata(pdev, tscadc);
	return 0;

err_disable_clk:
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
ret:
	return err;
}

static int ti_tscadc_remove(struct platform_device *pdev)
{
	struct ti_tscadc_dev	*tscadc = platform_get_drvdata(pdev);

	tscadc_writel(tscadc, REG_SE, 0x00);

	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	mfd_remove_devices(tscadc->dev);

	return 0;
}

#ifdef CONFIG_PM
static int tscadc_suspend(struct device *dev)
{
	struct ti_tscadc_dev	*tscadc_dev = dev_get_drvdata(dev);

	tscadc_writel(tscadc_dev, REG_SE, 0x00);
	pm_runtime_put_sync(dev);

	return 0;
}

static int tscadc_resume(struct device *dev)
{
	struct ti_tscadc_dev	*tscadc_dev = dev_get_drvdata(dev);
	u32 ctrl;

	pm_runtime_get_sync(dev);

	/* context restore */
	ctrl = CNTRLREG_STEPCONFIGWRT |	CNTRLREG_STEPID;
	tscadc_writel(tscadc_dev, REG_CTRL, ctrl);

	if (tscadc_dev->tsc_cell != -1) {
		if (tscadc_dev->tsc_wires == 5)
			ctrl |= CNTRLREG_5WIRE | CNTRLREG_TSCENB;
		else
			ctrl |= CNTRLREG_4WIRE | CNTRLREG_TSCENB;
		tscadc_idle_config(tscadc_dev);
	}
	ctrl |= CNTRLREG_TSCSSENB;
	tscadc_writel(tscadc_dev, REG_CTRL, ctrl);

	tscadc_writel(tscadc_dev, REG_CLKDIV, tscadc_dev->clk_div);

	return 0;
}

static const struct dev_pm_ops tscadc_pm_ops = {
	.suspend = tscadc_suspend,
	.resume = tscadc_resume,
};
#define TSCADC_PM_OPS (&tscadc_pm_ops)
#else
#define TSCADC_PM_OPS NULL
#endif

static const struct of_device_id ti_tscadc_dt_ids[] = {
	{ .compatible = "ti,am3359-tscadc", },
	{ }
};
MODULE_DEVICE_TABLE(of, ti_tscadc_dt_ids);

static struct platform_driver ti_tscadc_driver = {
	.driver = {
		.name   = "ti_am3359-tscadc",
		.pm	= TSCADC_PM_OPS,
		.of_match_table = ti_tscadc_dt_ids,
	},
	.probe	= ti_tscadc_probe,
	.remove	= ti_tscadc_remove,

};

module_platform_driver(ti_tscadc_driver);

MODULE_DESCRIPTION("TI touchscreen / ADC MFD controller driver");
MODULE_AUTHOR("Rachna Patil <rachna@ti.com>");
MODULE_LICENSE("GPL");
