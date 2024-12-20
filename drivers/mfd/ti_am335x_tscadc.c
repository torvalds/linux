// SPDX-License-Identifier: GPL-2.0-only
/*
 * TI Touch Screen / ADC MFD driver
 *
 * Copyright (C) 2012 Texas Instruments Incorporated - https://www.ti.com/
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
#include <linux/platform_device.h>
#include <linux/sched.h>

#include <linux/mfd/ti_am335x_tscadc.h>

static const struct regmap_config tscadc_regmap_config = {
	.name = "ti_tscadc",
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
};

void am335x_tsc_se_set_cache(struct ti_tscadc_dev *tscadc, u32 val)
{
	unsigned long flags;

	spin_lock_irqsave(&tscadc->reg_lock, flags);
	tscadc->reg_se_cache |= val;
	if (tscadc->adc_waiting)
		wake_up(&tscadc->reg_se_wait);
	else if (!tscadc->adc_in_use)
		regmap_write(tscadc->regmap, REG_SE, tscadc->reg_se_cache);

	spin_unlock_irqrestore(&tscadc->reg_lock, flags);
}
EXPORT_SYMBOL_GPL(am335x_tsc_se_set_cache);

static void am335x_tscadc_need_adc(struct ti_tscadc_dev *tscadc)
{
	DEFINE_WAIT(wait);
	u32 reg;

	regmap_read(tscadc->regmap, REG_ADCFSM, &reg);
	if (reg & SEQ_STATUS) {
		tscadc->adc_waiting = true;
		prepare_to_wait(&tscadc->reg_se_wait, &wait,
				TASK_UNINTERRUPTIBLE);
		spin_unlock_irq(&tscadc->reg_lock);

		schedule();

		spin_lock_irq(&tscadc->reg_lock);
		finish_wait(&tscadc->reg_se_wait, &wait);

		/*
		 * Sequencer should either be idle or
		 * busy applying the charge step.
		 */
		regmap_read(tscadc->regmap, REG_ADCFSM, &reg);
		WARN_ON((reg & SEQ_STATUS) && !(reg & CHARGE_STEP));
		tscadc->adc_waiting = false;
	}
	tscadc->adc_in_use = true;
}

void am335x_tsc_se_set_once(struct ti_tscadc_dev *tscadc, u32 val)
{
	spin_lock_irq(&tscadc->reg_lock);
	am335x_tscadc_need_adc(tscadc);

	regmap_write(tscadc->regmap, REG_SE, val);
	spin_unlock_irq(&tscadc->reg_lock);
}
EXPORT_SYMBOL_GPL(am335x_tsc_se_set_once);

void am335x_tsc_se_adc_done(struct ti_tscadc_dev *tscadc)
{
	unsigned long flags;

	spin_lock_irqsave(&tscadc->reg_lock, flags);
	tscadc->adc_in_use = false;
	regmap_write(tscadc->regmap, REG_SE, tscadc->reg_se_cache);
	spin_unlock_irqrestore(&tscadc->reg_lock, flags);
}
EXPORT_SYMBOL_GPL(am335x_tsc_se_adc_done);

void am335x_tsc_se_clr(struct ti_tscadc_dev *tscadc, u32 val)
{
	unsigned long flags;

	spin_lock_irqsave(&tscadc->reg_lock, flags);
	tscadc->reg_se_cache &= ~val;
	regmap_write(tscadc->regmap, REG_SE, tscadc->reg_se_cache);
	spin_unlock_irqrestore(&tscadc->reg_lock, flags);
}
EXPORT_SYMBOL_GPL(am335x_tsc_se_clr);

static void tscadc_idle_config(struct ti_tscadc_dev *tscadc)
{
	unsigned int idleconfig;

	idleconfig = STEPCONFIG_INM_ADCREFM | STEPCONFIG_INP_ADCREFM;
	if (ti_adc_with_touchscreen(tscadc))
		idleconfig |= STEPCONFIG_YNN | STEPCONFIG_YPN;

	regmap_write(tscadc->regmap, REG_IDLECONFIG, idleconfig);
}

static	int ti_tscadc_probe(struct platform_device *pdev)
{
	struct ti_tscadc_dev *tscadc;
	struct resource *res;
	struct clk *clk;
	struct device_node *node;
	struct mfd_cell *cell;
	bool use_tsc = false, use_mag = false;
	u32 val;
	int err;
	int tscmag_wires = 0, adc_channels = 0, cell_idx = 0, total_channels;
	int readouts = 0, mag_tracks = 0;

	/* Allocate memory for device */
	tscadc = devm_kzalloc(&pdev->dev, sizeof(*tscadc), GFP_KERNEL);
	if (!tscadc)
		return -ENOMEM;

	tscadc->dev = &pdev->dev;

	if (!pdev->dev.of_node) {
		dev_err(&pdev->dev, "Could not find valid DT data.\n");
		return -EINVAL;
	}

	tscadc->data = of_device_get_match_data(&pdev->dev);

	if (ti_adc_with_touchscreen(tscadc)) {
		node = of_get_child_by_name(pdev->dev.of_node, "tsc");
		of_property_read_u32(node, "ti,wires", &tscmag_wires);
		err = of_property_read_u32(node, "ti,coordinate-readouts",
					   &readouts);
		if (err < 0)
			of_property_read_u32(node, "ti,coordiante-readouts",
					     &readouts);

		of_node_put(node);

		if (tscmag_wires)
			use_tsc = true;
	} else {
		/*
		 * When adding support for the magnetic stripe reader, here is
		 * the place to look for the number of tracks used from device
		 * tree. Let's default to 0 for now.
		 */
		mag_tracks = 0;
		tscmag_wires = mag_tracks * 2;
		if (tscmag_wires)
			use_mag = true;
	}

	node = of_get_child_by_name(pdev->dev.of_node, "adc");
	of_property_for_each_u32(node, "ti,adc-channels", val) {
		adc_channels++;
		if (val > 7) {
			dev_err(&pdev->dev, " PIN numbers are 0..7 (not %d)\n",
				val);
			of_node_put(node);
			return -EINVAL;
		}
	}

	of_node_put(node);

	total_channels = tscmag_wires + adc_channels;
	if (total_channels > 8) {
		dev_err(&pdev->dev, "Number of i/p channels more than 8\n");
		return -EINVAL;
	}

	if (total_channels == 0) {
		dev_err(&pdev->dev, "Need at least one channel.\n");
		return -EINVAL;
	}

	if (use_tsc && (readouts * 2 + 2 + adc_channels > 16)) {
		dev_err(&pdev->dev, "Too many step configurations requested\n");
		return -EINVAL;
	}

	err = platform_get_irq(pdev, 0);
	if (err < 0)
		return err;
	else
		tscadc->irq = err;

	tscadc->tscadc_base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(tscadc->tscadc_base))
		return PTR_ERR(tscadc->tscadc_base);

	tscadc->tscadc_phys_base = res->start;
	tscadc->regmap = devm_regmap_init_mmio(&pdev->dev,
					       tscadc->tscadc_base,
					       &tscadc_regmap_config);
	if (IS_ERR(tscadc->regmap)) {
		dev_err(&pdev->dev, "regmap init failed\n");
		return PTR_ERR(tscadc->regmap);
	}

	spin_lock_init(&tscadc->reg_lock);
	init_waitqueue_head(&tscadc->reg_se_wait);

	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	/*
	 * The TSC_ADC_Subsystem has 2 clock domains: OCP_CLK and ADC_CLK.
	 * ADCs produce a 12-bit sample every 15 ADC_CLK cycles.
	 * am33xx ADCs expect to capture 200ksps.
	 * am47xx ADCs expect to capture 867ksps.
	 * We need ADC clocks respectively running at 3MHz and 13MHz.
	 * These frequencies are valid since TSC_ADC_SS controller design
	 * assumes the OCP clock is at least 6x faster than the ADC clock.
	 */
	clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "failed to get fck\n");
		err = PTR_ERR(clk);
		goto err_disable_clk;
	}

	tscadc->clk_div = (clk_get_rate(clk) / tscadc->data->target_clk_rate) - 1;
	regmap_write(tscadc->regmap, REG_CLKDIV, tscadc->clk_div);

	/*
	 * Set the control register bits. tscadc->ctrl stores the configuration
	 * of the CTRL register but not the subsystem enable bit which must be
	 * added manually when timely.
	 */
	tscadc->ctrl = CNTRLREG_STEPID;
	if (ti_adc_with_touchscreen(tscadc)) {
		tscadc->ctrl |= CNTRLREG_TSC_STEPCONFIGWRT;
		if (use_tsc) {
			tscadc->ctrl |= CNTRLREG_TSC_ENB;
			if (tscmag_wires == 5)
				tscadc->ctrl |= CNTRLREG_TSC_5WIRE;
			else
				tscadc->ctrl |= CNTRLREG_TSC_4WIRE;
		}
	} else {
		tscadc->ctrl |= CNTRLREG_MAG_PREAMP_PWRDOWN |
				CNTRLREG_MAG_PREAMP_BYPASS;
	}
	regmap_write(tscadc->regmap, REG_CTRL, tscadc->ctrl);

	tscadc_idle_config(tscadc);

	/* Enable the TSC module enable bit */
	regmap_write(tscadc->regmap, REG_CTRL, tscadc->ctrl | CNTRLREG_SSENB);

	/* TSC or MAG Cell */
	if (use_tsc || use_mag) {
		cell = &tscadc->cells[cell_idx++];
		cell->name = tscadc->data->secondary_feature_name;
		cell->of_compatible = tscadc->data->secondary_feature_compatible;
		cell->platform_data = &tscadc;
		cell->pdata_size = sizeof(tscadc);
	}

	/* ADC Cell */
	if (adc_channels > 0) {
		cell = &tscadc->cells[cell_idx++];
		cell->name = tscadc->data->adc_feature_name;
		cell->of_compatible = tscadc->data->adc_feature_compatible;
		cell->platform_data = &tscadc;
		cell->pdata_size = sizeof(tscadc);
	}

	err = mfd_add_devices(&pdev->dev, PLATFORM_DEVID_AUTO,
			      tscadc->cells, cell_idx, NULL, 0, NULL);
	if (err < 0)
		goto err_disable_clk;

	platform_set_drvdata(pdev, tscadc);
	return 0;

err_disable_clk:
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	return err;
}

static void ti_tscadc_remove(struct platform_device *pdev)
{
	struct ti_tscadc_dev *tscadc = platform_get_drvdata(pdev);

	regmap_write(tscadc->regmap, REG_SE, 0x00);

	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	mfd_remove_devices(tscadc->dev);
}

static int __maybe_unused ti_tscadc_can_wakeup(struct device *dev, void *data)
{
	return device_may_wakeup(dev);
}

static int __maybe_unused tscadc_suspend(struct device *dev)
{
	struct ti_tscadc_dev *tscadc = dev_get_drvdata(dev);

	regmap_write(tscadc->regmap, REG_SE, 0x00);
	if (device_for_each_child(dev, NULL, ti_tscadc_can_wakeup)) {
		u32 ctrl;

		regmap_read(tscadc->regmap, REG_CTRL, &ctrl);
		ctrl &= ~(CNTRLREG_POWERDOWN);
		ctrl |= CNTRLREG_SSENB;
		regmap_write(tscadc->regmap, REG_CTRL, ctrl);
	}
	pm_runtime_put_sync(dev);

	return 0;
}

static int __maybe_unused tscadc_resume(struct device *dev)
{
	struct ti_tscadc_dev *tscadc = dev_get_drvdata(dev);

	pm_runtime_get_sync(dev);

	regmap_write(tscadc->regmap, REG_CLKDIV, tscadc->clk_div);
	regmap_write(tscadc->regmap, REG_CTRL, tscadc->ctrl);
	tscadc_idle_config(tscadc);
	regmap_write(tscadc->regmap, REG_CTRL, tscadc->ctrl | CNTRLREG_SSENB);

	return 0;
}

static SIMPLE_DEV_PM_OPS(tscadc_pm_ops, tscadc_suspend, tscadc_resume);

static const struct ti_tscadc_data tscdata = {
	.adc_feature_name = "TI-am335x-adc",
	.adc_feature_compatible = "ti,am3359-adc",
	.secondary_feature_name = "TI-am335x-tsc",
	.secondary_feature_compatible = "ti,am3359-tsc",
	.target_clk_rate = TSC_ADC_CLK,
};

static const struct ti_tscadc_data magdata = {
	.adc_feature_name = "TI-am43xx-adc",
	.adc_feature_compatible = "ti,am4372-adc",
	.secondary_feature_name = "TI-am43xx-mag",
	.secondary_feature_compatible = "ti,am4372-mag",
	.target_clk_rate = MAG_ADC_CLK,
};

static const struct of_device_id ti_tscadc_dt_ids[] = {
	{ .compatible = "ti,am3359-tscadc", .data = &tscdata },
	{ .compatible = "ti,am4372-magadc", .data = &magdata },
	{ }
};
MODULE_DEVICE_TABLE(of, ti_tscadc_dt_ids);

static struct platform_driver ti_tscadc_driver = {
	.driver = {
		.name   = "ti_am3359-tscadc",
		.pm	= &tscadc_pm_ops,
		.of_match_table = ti_tscadc_dt_ids,
	},
	.probe	= ti_tscadc_probe,
	.remove = ti_tscadc_remove,

};

module_platform_driver(ti_tscadc_driver);

MODULE_DESCRIPTION("TI touchscreen/magnetic stripe reader/ADC MFD controller driver");
MODULE_AUTHOR("Rachna Patil <rachna@ti.com>");
MODULE_LICENSE("GPL");
