// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN EIC7700 Voltage, Temperature sensor driver
 *
 * Copyright 2026, Beijing ESWIN Computing Technology Co., Ltd.
 *
 * Authors:
 *   Yulin Lu <luyulin@eswincomputing.com>
 *   Huan He <hehuan1@eswincomputing.com>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/polynomial.h>
#include <linux/reset.h>
#include "eic7700-pvt.h"

static const struct pvt_sensor_info pvt_info[] = {
	PVT_SENSOR_INFO(0, "Temperature", hwmon_temp, TEMP),
	PVT_SENSOR_INFO(0, "Voltage", hwmon_in, VOLT),
};

static const char * const pvt_clk_names[PVT_CLK_NUM] = {"enable", "apb"};

/*
 * The original translation formulae of the temperature (in degrees of Celsius)
 * to PVT data and vice-versa are following:
 * N = 6.0818e-8*(T^4) +1.2873e-5*(T^3) + 7.2244e-3*(T^2) + 3.6484*(T^1) +
 *     1.6198e2,
 * T = -1.8439e-11*(N^4) + 8.0705e-8*(N^3) + -1.8501e-4*(N^2) +
 *     3.2843e-1*(N^1) - 4.8690e1,
 * where T = [-40, 125]C and N = [27, 771].
 * They must be accordingly altered to be suitable for the integer arithmetics.
 * The technique is called 'factor redistribution', which just makes sure the
 * multiplications and divisions are made so to have a result of the operations
 * within the integer numbers limit. In addition we need to translate the
 * formulae to accept millidegrees of Celsius. Here what they look like after
 * the alterations:
 * N = (60818e-20*(T^4) + 12873e-14*(T^3) + 72244e-9*(T^2) + 36484e-3*T +
 *     16198e2) / 1e4,
 * T = -18439e-12*(N^4) + 80705e-9*(N^3) - 185010e-6*(N^2) + 328430e-3*N -
 *     48690,
 * where T = [-40000, 125000] mC and N = [27, 771].
 */
static const struct polynomial poly_N_to_temp = {
	.total_divider = 1,
	.terms = {
		{4, -18439, 1000, 1},
		{3, 80705, 1000, 1},
		{2, -185010, 1000, 1},
		{1, 328430, 1000, 1},
		{0, -48690, 1, 1}
	}
};

/*
 * Similar alterations are performed for the voltage conversion equations.
 * The original formulae are:
 * N = 1.3905e3*V - 5.7685e2,
 * V = (N + 5.7685e2) / 1.3905e3,
 * where V = [0.72, 0.88] V and N = [424, 646].
 * After the optimization they looks as follows:
 * N = (13905e-3*V - 5768.5) / 10,
 * V = (N * 10^5 / 13905 + 57685 * 10^3 / 13905) / 10.
 * where V = [720, 880] mV and N = [424, 646].
 */
static const struct polynomial poly_N_to_volt = {
	.total_divider = 10,
	.terms = {
		{1, 100000, 13905, 1},
		{0, 57685000, 1, 13905}
	}
};

static inline u32 eic7700_pvt_update(void __iomem *reg, u32 mask, u32 data)
{
	u32 old;

	old = readl_relaxed(reg);
	writel((old & ~mask) | (data & mask), reg);

	return old & mask;
}

static inline void eic7700_pvt_set_mode(struct pvt_hwmon *pvt, u32 mode)
{
	u32 old;

	mode = FIELD_PREP(PVT_MODE_MASK, mode);

	old = eic7700_pvt_update(pvt->regs + PVT_ENA, PVT_ENA_EN, 0);
	eic7700_pvt_update(pvt->regs + PVT_MODE, PVT_MODE_MASK, mode);
	eic7700_pvt_update(pvt->regs + PVT_ENA, PVT_ENA_EN, old);
}

static inline void eic7700_pvt_set_trim(struct pvt_hwmon *pvt, u32 val)
{
	u32 old;

	old = eic7700_pvt_update(pvt->regs + PVT_ENA, PVT_ENA_EN, 0);
	writel(val, pvt->regs + PVT_TRIM);
	eic7700_pvt_update(pvt->regs + PVT_ENA, PVT_ENA_EN, old);
}

static irqreturn_t eic7700_pvt_hard_isr(int irq, void *data)
{
	struct pvt_hwmon *pvt = data;
	u32 stat, val;
	int active;

	if (IS_ENABLED(CONFIG_PM)) {
		active = pm_runtime_get_if_active(pvt->dev);
		if (active <= 0)
			return IRQ_NONE;
	}

	stat = readl(pvt->regs + PVT_INT);
	if (!(stat & PVT_INT_STAT)) {
		if (IS_ENABLED(CONFIG_PM))
			pm_runtime_put(pvt->dev);
		return IRQ_NONE;
	}

	eic7700_pvt_update(pvt->regs + PVT_INT, PVT_INT_CLR, PVT_INT_CLR);
	/*
	 * Read the data, update the cache and notify a waiter of this event.
	 */
	val = readl(pvt->regs + PVT_DATA);
	WRITE_ONCE(pvt->data_cache, FIELD_GET(PVT_DATA_OUT, val));
	complete(&pvt->conversion);

	if (IS_ENABLED(CONFIG_PM))
		pm_runtime_put(pvt->dev);

	return IRQ_HANDLED;
}

static int eic7700_pvt_read_data(struct pvt_hwmon *pvt,
				 enum pvt_sensor_type type, long *val)
{
	unsigned long timeout;
	u32 data;
	int ret;

	/*
	 * Wait for PVT conversion to complete and update the data cache. The
	 * data read procedure is following: set the requested PVT sensor mode,
	 * enable conversion, wait until conversion is finished, then disable
	 * conversion and IRQ, and read the cached data.
	 */
	reinit_completion(&pvt->conversion);

	eic7700_pvt_set_mode(pvt, pvt_info[type].mode);
	eic7700_pvt_update(pvt->regs + PVT_ENA, PVT_ENA_EN, PVT_ENA_EN);

	/*
	 * Wait with timeout since in case if the sensor is suddenly powered
	 * down the request won't be completed and the caller will hang up on
	 * this procedure until the power is back up again. Multiply the
	 * timeout by the factor of two to prevent a false timeout.
	 */
	timeout = 2 * usecs_to_jiffies(ktime_to_us(pvt->timeout));
	ret = wait_for_completion_timeout(&pvt->conversion, timeout);

	eic7700_pvt_update(pvt->regs + PVT_ENA, PVT_ENA_EN, 0);
	eic7700_pvt_update(pvt->regs + PVT_INT, PVT_INT_CLR, PVT_INT_CLR);

	if (!ret)
		synchronize_irq(pvt->irq);

	data = READ_ONCE(pvt->data_cache);

	if (!ret)
		return -ETIMEDOUT;

	if (type == PVT_TEMP)
		*val = polynomial_calc(&poly_N_to_temp, data);
	else
		*val = polynomial_calc(&poly_N_to_volt, data);

	return 0;
}

static const struct hwmon_channel_info *pvt_channel_info[] = {
	HWMON_CHANNEL_INFO(chip, HWMON_C_REGISTER_TZ),
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT | HWMON_T_LABEL),
	HWMON_CHANNEL_INFO(in, HWMON_I_INPUT | HWMON_I_LABEL),
	NULL
};

static umode_t eic7700_pvt_hwmon_is_visible(const void *data,
					    enum hwmon_sensor_types type,
					    u32 attr, int ch)
{
	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_input:
		case hwmon_temp_label:
			return 0444;
		}
		break;
	case hwmon_in:
		switch (attr) {
		case hwmon_in_input:
		case hwmon_in_label:
			return 0444;
		}
		break;
	default:
		break;
	}

	return 0;
}

static int eic7700_pvt_hwmon_read(struct device *dev,
				  enum hwmon_sensor_types type, u32 attr,
				  int ch, long *val)
{
	struct pvt_hwmon *pvt = dev_get_drvdata(dev);
	int ret;

	ret = pm_runtime_get_sync(pvt->dev);
	if (ret < 0) {
		dev_err(pvt->dev, "Failed to resume PVT device: %d\n", ret);
		pm_runtime_put_noidle(pvt->dev);
		return ret;
	}

	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_input:
			ret = eic7700_pvt_read_data(pvt, ch, val);
			break;
		default:
			ret = -EOPNOTSUPP;
		}
		break;
	case hwmon_in:
		if (attr == hwmon_in_input)
			ret = eic7700_pvt_read_data(pvt, PVT_VOLT + ch, val);
		else
			ret = -EOPNOTSUPP;
		break;
	default:
		ret = -EOPNOTSUPP;
	}

	pm_runtime_mark_last_busy(pvt->dev);
	pm_runtime_put_autosuspend(pvt->dev);
	return ret;
}

static int eic7700_pvt_hwmon_read_string(struct device *dev,
					 enum hwmon_sensor_types type, u32 attr,
					 int ch, const char **str)
{
	switch (type) {
	case hwmon_temp:
		if (attr == hwmon_temp_label) {
			*str = pvt_info[ch].label;
			return 0;
		}
		break;
	case hwmon_in:
		if (attr == hwmon_in_label) {
			*str = pvt_info[PVT_VOLT + ch].label;
			return 0;
		}
		break;
	default:
		break;
	}

	return -EOPNOTSUPP;
}

static const struct hwmon_ops pvt_hwmon_ops = {
	.is_visible = eic7700_pvt_hwmon_is_visible,
	.read = eic7700_pvt_hwmon_read,
	.read_string = eic7700_pvt_hwmon_read_string
};

static const struct hwmon_chip_info pvt_hwmon_info = {
	.ops = &pvt_hwmon_ops,
	.info = pvt_channel_info
};

static struct pvt_hwmon *eic7700_pvt_create_data(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pvt_hwmon *pvt;

	pvt = devm_kzalloc(dev, sizeof(*pvt), GFP_KERNEL);
	if (!pvt)
		return ERR_PTR(-ENOMEM);

	pvt->dev = dev;
	init_completion(&pvt->conversion);

	return pvt;
}

static int eic7700_pvt_init_iface(struct pvt_hwmon *pvt)
{
	/*
	 * Make sure controller are disabled so not to accidentally have ISR
	 * executed before the driver data is fully initialized. Clear the IRQ
	 * status as well.
	 */
	eic7700_pvt_update(pvt->regs + PVT_ENA, PVT_ENA_EN, 0);
	eic7700_pvt_update(pvt->regs + PVT_INT, PVT_INT_CLR, PVT_INT_CLR);
	readl(pvt->regs + PVT_INT);
	readl(pvt->regs + PVT_DATA);

	/* Setup default sensor mode and temperature trim. */
	eic7700_pvt_set_mode(pvt, pvt_info[PVT_TEMP].mode);

	/*
	 * Max conversion latency (~333 µs) derived from PVT spec:
	 * maximum sampling rate = 3000 samples/sec.
	 */
	pvt->timeout = ns_to_ktime(PVT_TOUT_MIN);

	eic7700_pvt_set_trim(pvt, PVT_TRIM_DEF);

	return 0;
}

static int eic7700_pvt_request_irq(struct pvt_hwmon *pvt)
{
	struct platform_device *pdev = to_platform_device(pvt->dev);
	int ret;

	pvt->irq = platform_get_irq(pdev, 0);
	if (pvt->irq < 0)
		return pvt->irq;

	ret = devm_request_threaded_irq(pvt->dev, pvt->irq,
					eic7700_pvt_hard_isr, NULL,
					IRQF_TRIGGER_HIGH, "pvt", pvt);
	if (ret) {
		dev_err(pvt->dev, "Couldn't request PVT IRQ\n");
		return ret;
	}

	return 0;
}

static int eic7700_pvt_create_hwmon(struct pvt_hwmon *pvt)
{
	pvt->hwmon = devm_hwmon_device_register_with_info(pvt->dev, "pvt",
							  pvt, &pvt_hwmon_info,
							  NULL);
	if (IS_ERR(pvt->hwmon)) {
		dev_err(pvt->dev, "Couldn't create hwmon device\n");
		return PTR_ERR(pvt->hwmon);
	}

	return 0;
}

static void eic7700_pvt_disable_pm_runtime(void *data)
{
	struct pvt_hwmon *pvt = data;

	pm_runtime_dont_use_autosuspend(pvt->dev);
	pm_runtime_disable(pvt->dev);

	if (!pm_runtime_status_suspended(pvt->dev)) {
		clk_bulk_disable_unprepare(PVT_CLK_NUM, pvt->clks);
		pm_runtime_set_suspended(pvt->dev);
	}
}

static int eic7700_pvt_probe(struct platform_device *pdev)
{
	struct reset_control *rst;
	struct pvt_hwmon *pvt;
	int i, ret;

	pvt = eic7700_pvt_create_data(pdev);
	if (IS_ERR(pvt))
		return PTR_ERR(pvt);

	platform_set_drvdata(pdev, pvt);

	pvt->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pvt->regs))
		return PTR_ERR(pvt->regs);

	for (i = 0; i < PVT_CLK_NUM; i++)
		pvt->clks[i].id = pvt_clk_names[i];

	ret = devm_clk_bulk_get(&pdev->dev, PVT_CLK_NUM, pvt->clks);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "Couldn't get clock descriptors\n");

	rst = devm_reset_control_get_exclusive_deasserted(&pdev->dev, NULL);
	if (IS_ERR(rst))
		return dev_err_probe(pvt->dev, PTR_ERR(rst),
				     "Couldn't get reset control\n");

	ret = clk_bulk_prepare_enable(PVT_CLK_NUM, pvt->clks);
	if (ret)
		return dev_err_probe(pvt->dev, ret,
				     "Failed to enable clocks\n");

	ret = eic7700_pvt_init_iface(pvt);
	if (ret) {
		clk_bulk_disable_unprepare(PVT_CLK_NUM, pvt->clks);
		return ret;
	}

	if (IS_ENABLED(CONFIG_PM))
		clk_bulk_disable_unprepare(PVT_CLK_NUM, pvt->clks);

	pm_runtime_enable(&pdev->dev);
	pm_runtime_set_autosuspend_delay(&pdev->dev, 3000);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_get_noresume(&pdev->dev);

	ret = devm_add_action_or_reset(pvt->dev, eic7700_pvt_disable_pm_runtime,
				       pvt);
	if (ret) {
		pm_runtime_put_noidle(&pdev->dev);
		return dev_err_probe(&pdev->dev, ret,
				     "Can't register PM cleanup\n");
	}

	ret = eic7700_pvt_request_irq(pvt);
	if (ret)
		goto err_put_pm_runtime;

	ret = eic7700_pvt_create_hwmon(pvt);
	if (ret)
		goto err_put_pm_runtime;

	pm_runtime_put_autosuspend(&pdev->dev);

	return 0;

err_put_pm_runtime:
	pm_runtime_put_noidle(&pdev->dev);
	return ret;
}

static int __maybe_unused eic7700_pvt_runtime_resume(struct device *dev)
{
	struct pvt_hwmon *pvt = dev_get_drvdata(dev);
	int ret;

	ret = clk_bulk_prepare_enable(PVT_CLK_NUM, pvt->clks);
	if (ret) {
		dev_err(dev, "Failed to enable clocks: %d\n", ret);
		return ret;
	}

	eic7700_pvt_set_trim(pvt, PVT_TRIM_DEF);

	return 0;
}

static int __maybe_unused eic7700_pvt_runtime_suspend(struct device *dev)
{
	struct pvt_hwmon *pvt = dev_get_drvdata(dev);

	clk_bulk_disable_unprepare(PVT_CLK_NUM, pvt->clks);

	return 0;
}

static const struct dev_pm_ops eic7700_pvt_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend, pm_runtime_force_resume)
	RUNTIME_PM_OPS(eic7700_pvt_runtime_suspend, eic7700_pvt_runtime_resume,
		       NULL)
};

static const struct of_device_id pvt_of_match[] = {
	{ .compatible = "eswin,eic7700-pvt"},
	{ }
};
MODULE_DEVICE_TABLE(of, pvt_of_match);

static struct platform_driver pvt_driver = {
	.probe = eic7700_pvt_probe,
	.driver = {
		.name = "eic7700-pvt",
		.of_match_table = pvt_of_match,
		.pm = pm_ptr(&eic7700_pvt_pm_ops),
	},
};
module_platform_driver(pvt_driver);

MODULE_AUTHOR("Yulin Lu <luyulin@eswincomputing.com>");
MODULE_AUTHOR("Huan He <hehuan1@eswincomputing.com>");
MODULE_DESCRIPTION("Eswin eic7700 PVT driver");
MODULE_LICENSE("GPL");
