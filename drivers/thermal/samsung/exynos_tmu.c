/*
 * exynos_tmu.c - Samsung EXYNOS TMU (Thermal Management Unit)
 *
 *  Copyright (C) 2011 Samsung Electronics
 *  Donggeun Kim <dg77.kim@samsung.com>
 *  Amit Daniel Kachhap <amit.kachhap@linaro.org>
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "exynos_thermal_common.h"
#include "exynos_tmu.h"
#include "exynos_tmu_data.h"

struct exynos_tmu_data {
	struct exynos_tmu_platform_data *pdata;
	struct resource *mem;
	void __iomem *base;
	int irq;
	enum soc_type soc;
	struct work_struct irq_work;
	struct mutex lock;
	struct clk *clk;
	u8 temp_error1, temp_error2;
};

/*
 * TMU treats temperature as a mapped temperature code.
 * The temperature is converted differently depending on the calibration type.
 */
static int temp_to_code(struct exynos_tmu_data *data, u8 temp)
{
	struct exynos_tmu_platform_data *pdata = data->pdata;
	int temp_code;

	if (data->soc == SOC_ARCH_EXYNOS4210)
		/* temp should range between 25 and 125 */
		if (temp < 25 || temp > 125) {
			temp_code = -EINVAL;
			goto out;
		}

	switch (pdata->cal_type) {
	case TYPE_TWO_POINT_TRIMMING:
		temp_code = (temp - pdata->first_point_trim) *
			(data->temp_error2 - data->temp_error1) /
			(pdata->second_point_trim - pdata->first_point_trim) +
			data->temp_error1;
		break;
	case TYPE_ONE_POINT_TRIMMING:
		temp_code = temp + data->temp_error1 - pdata->first_point_trim;
		break;
	default:
		temp_code = temp + pdata->default_temp_offset;
		break;
	}
out:
	return temp_code;
}

/*
 * Calculate a temperature value from a temperature code.
 * The unit of the temperature is degree Celsius.
 */
static int code_to_temp(struct exynos_tmu_data *data, u8 temp_code)
{
	struct exynos_tmu_platform_data *pdata = data->pdata;
	int temp;

	if (data->soc == SOC_ARCH_EXYNOS4210)
		/* temp_code should range between 75 and 175 */
		if (temp_code < 75 || temp_code > 175) {
			temp = -ENODATA;
			goto out;
		}

	switch (pdata->cal_type) {
	case TYPE_TWO_POINT_TRIMMING:
		temp = (temp_code - data->temp_error1) *
			(pdata->second_point_trim - pdata->first_point_trim) /
			(data->temp_error2 - data->temp_error1) +
			pdata->first_point_trim;
		break;
	case TYPE_ONE_POINT_TRIMMING:
		temp = temp_code - data->temp_error1 + pdata->first_point_trim;
		break;
	default:
		temp = temp_code - pdata->default_temp_offset;
		break;
	}
out:
	return temp;
}

static int exynos_tmu_initialize(struct platform_device *pdev)
{
	struct exynos_tmu_data *data = platform_get_drvdata(pdev);
	struct exynos_tmu_platform_data *pdata = data->pdata;
	const struct exynos_tmu_registers *reg = pdata->registers;
	unsigned int status, trim_info = 0, con;
	unsigned int rising_threshold = 0, falling_threshold = 0;
	int ret = 0, threshold_code, i, trigger_levs = 0;

	mutex_lock(&data->lock);
	clk_enable(data->clk);

	status = readb(data->base + reg->tmu_status);
	if (!status) {
		ret = -EBUSY;
		goto out;
	}

	if (data->soc == SOC_ARCH_EXYNOS)
		__raw_writel(1, data->base + reg->triminfo_ctrl);

	/* Save trimming info in order to perform calibration */
	trim_info = readl(data->base + reg->triminfo_data);
	data->temp_error1 = trim_info & EXYNOS_TMU_TEMP_MASK;
	data->temp_error2 = ((trim_info >> reg->triminfo_85_shift) &
				EXYNOS_TMU_TEMP_MASK);

	if ((pdata->min_efuse_value > data->temp_error1) ||
			(data->temp_error1 > pdata->max_efuse_value) ||
			(data->temp_error2 != 0))
		data->temp_error1 = pdata->efuse_value;

	if (pdata->max_trigger_level > MAX_THRESHOLD_LEVS) {
		dev_err(&pdev->dev, "Invalid max trigger level\n");
		goto out;
	}

	for (i = 0; i < pdata->max_trigger_level; i++) {
		if (!pdata->trigger_levels[i])
			continue;

		if ((pdata->trigger_type[i] == HW_TRIP) &&
		(!pdata->trigger_levels[pdata->max_trigger_level - 1])) {
			dev_err(&pdev->dev, "Invalid hw trigger level\n");
			ret = -EINVAL;
			goto out;
		}

		/* Count trigger levels except the HW trip*/
		if (!(pdata->trigger_type[i] == HW_TRIP))
			trigger_levs++;
	}

	if (data->soc == SOC_ARCH_EXYNOS4210) {
		/* Write temperature code for threshold */
		threshold_code = temp_to_code(data, pdata->threshold);
		if (threshold_code < 0) {
			ret = threshold_code;
			goto out;
		}
		writeb(threshold_code,
			data->base + reg->threshold_temp);
		for (i = 0; i < trigger_levs; i++)
			writeb(pdata->trigger_levels[i], data->base +
			reg->threshold_th0 + i * sizeof(reg->threshold_th0));

		writel(reg->inten_rise_mask, data->base + reg->tmu_intclear);
	} else if (data->soc == SOC_ARCH_EXYNOS) {
		/* Write temperature code for rising and falling threshold */
		for (i = 0;
		i < trigger_levs && i < EXYNOS_MAX_TRIGGER_PER_REG; i++) {
			threshold_code = temp_to_code(data,
						pdata->trigger_levels[i]);
			if (threshold_code < 0) {
				ret = threshold_code;
				goto out;
			}
			rising_threshold |= threshold_code << 8 * i;
			if (pdata->threshold_falling) {
				threshold_code = temp_to_code(data,
						pdata->trigger_levels[i] -
						pdata->threshold_falling);
				if (threshold_code > 0)
					falling_threshold |=
						threshold_code << 8 * i;
			}
		}

		writel(rising_threshold,
				data->base + reg->threshold_th0);
		writel(falling_threshold,
				data->base + reg->threshold_th1);

		writel((reg->inten_rise_mask << reg->inten_rise_shift) |
			(reg->inten_fall_mask << reg->inten_fall_shift),
				data->base + reg->tmu_intclear);

		/* if last threshold limit is also present */
		i = pdata->max_trigger_level - 1;
		if (pdata->trigger_levels[i] &&
				(pdata->trigger_type[i] == HW_TRIP)) {
			threshold_code = temp_to_code(data,
						pdata->trigger_levels[i]);
			if (threshold_code < 0) {
				ret = threshold_code;
				goto out;
			}
			rising_threshold |= threshold_code << 8 * i;
			writel(rising_threshold,
				data->base + reg->threshold_th0);
			con = readl(data->base + reg->tmu_ctrl);
			con |= (1 << reg->therm_trip_en_shift);
			writel(con, data->base + reg->tmu_ctrl);
		}
	}
out:
	clk_disable(data->clk);
	mutex_unlock(&data->lock);

	return ret;
}

static void exynos_tmu_control(struct platform_device *pdev, bool on)
{
	struct exynos_tmu_data *data = platform_get_drvdata(pdev);
	struct exynos_tmu_platform_data *pdata = data->pdata;
	const struct exynos_tmu_registers *reg = pdata->registers;
	unsigned int con, interrupt_en;

	mutex_lock(&data->lock);
	clk_enable(data->clk);

	con = readl(data->base + reg->tmu_ctrl);

	if (pdata->reference_voltage) {
		con &= ~(reg->buf_vref_sel_mask << reg->buf_vref_sel_shift);
		con |= pdata->reference_voltage << reg->buf_vref_sel_shift;
	}

	if (pdata->gain) {
		con &= ~(reg->buf_slope_sel_mask << reg->buf_slope_sel_shift);
		con |= (pdata->gain << reg->buf_slope_sel_shift);
	}

	if (pdata->noise_cancel_mode) {
		con &= ~(reg->therm_trip_mode_mask <<
					reg->therm_trip_mode_shift);
		con |= (pdata->noise_cancel_mode << reg->therm_trip_mode_shift);
	}

	if (on) {
		con |= (1 << reg->core_en_shift);
		interrupt_en =
			pdata->trigger_enable[3] << reg->inten_rise3_shift |
			pdata->trigger_enable[2] << reg->inten_rise2_shift |
			pdata->trigger_enable[1] << reg->inten_rise1_shift |
			pdata->trigger_enable[0] << reg->inten_rise0_shift;
		if (pdata->threshold_falling)
			interrupt_en |=
				interrupt_en << reg->inten_fall0_shift;
	} else {
		con &= ~(1 << reg->core_en_shift);
		interrupt_en = 0; /* Disable all interrupts */
	}
	writel(interrupt_en, data->base + reg->tmu_inten);
	writel(con, data->base + reg->tmu_ctrl);

	clk_disable(data->clk);
	mutex_unlock(&data->lock);
}

static int exynos_tmu_read(struct exynos_tmu_data *data)
{
	struct exynos_tmu_platform_data *pdata = data->pdata;
	const struct exynos_tmu_registers *reg = pdata->registers;
	u8 temp_code;
	int temp;

	mutex_lock(&data->lock);
	clk_enable(data->clk);

	temp_code = readb(data->base + reg->tmu_cur_temp);
	temp = code_to_temp(data, temp_code);

	clk_disable(data->clk);
	mutex_unlock(&data->lock);

	return temp;
}

#ifdef CONFIG_THERMAL_EMULATION
static int exynos_tmu_set_emulation(void *drv_data, unsigned long temp)
{
	struct exynos_tmu_data *data = drv_data;
	struct exynos_tmu_platform_data *pdata = data->pdata;
	const struct exynos_tmu_registers *reg = pdata->registers;
	unsigned int val;
	int ret = -EINVAL;

	if (data->soc == SOC_ARCH_EXYNOS4210)
		goto out;

	if (temp && temp < MCELSIUS)
		goto out;

	mutex_lock(&data->lock);
	clk_enable(data->clk);

	val = readl(data->base + reg->emul_con);

	if (temp) {
		temp /= MCELSIUS;

		val = (EXYNOS_EMUL_TIME << reg->emul_time_shift) |
			(temp_to_code(data, temp)
			 << reg->emul_temp_shift) | EXYNOS_EMUL_ENABLE;
	} else {
		val &= ~EXYNOS_EMUL_ENABLE;
	}

	writel(val, data->base + reg->emul_con);

	clk_disable(data->clk);
	mutex_unlock(&data->lock);
	return 0;
out:
	return ret;
}
#else
static int exynos_tmu_set_emulation(void *drv_data,	unsigned long temp)
	{ return -EINVAL; }
#endif/*CONFIG_THERMAL_EMULATION*/

static struct thermal_sensor_conf exynos_sensor_conf = {
	.name			= "exynos-therm",
	.read_temperature	= (int (*)(void *))exynos_tmu_read,
	.write_emul_temp	= exynos_tmu_set_emulation,
};

static void exynos_tmu_work(struct work_struct *work)
{
	struct exynos_tmu_data *data = container_of(work,
			struct exynos_tmu_data, irq_work);
	struct exynos_tmu_platform_data *pdata = data->pdata;
	const struct exynos_tmu_registers *reg = pdata->registers;
	unsigned int val_irq;

	exynos_report_trigger(&exynos_sensor_conf);
	mutex_lock(&data->lock);
	clk_enable(data->clk);

	/* TODO: take action based on particular interrupt */
	val_irq = readl(data->base + reg->tmu_intstat);
	/* clear the interrupts */
	writel(val_irq, data->base + reg->tmu_intclear);

	clk_disable(data->clk);
	mutex_unlock(&data->lock);

	enable_irq(data->irq);
}

static irqreturn_t exynos_tmu_irq(int irq, void *id)
{
	struct exynos_tmu_data *data = id;

	disable_irq_nosync(irq);
	schedule_work(&data->irq_work);

	return IRQ_HANDLED;
}

#ifdef CONFIG_OF
static const struct of_device_id exynos_tmu_match[] = {
	{
		.compatible = "samsung,exynos4210-tmu",
		.data = (void *)EXYNOS4210_TMU_DRV_DATA,
	},
	{
		.compatible = "samsung,exynos4412-tmu",
		.data = (void *)EXYNOS5250_TMU_DRV_DATA,
	},
	{
		.compatible = "samsung,exynos5250-tmu",
		.data = (void *)EXYNOS5250_TMU_DRV_DATA,
	},
	{},
};
MODULE_DEVICE_TABLE(of, exynos_tmu_match);
#endif

static struct platform_device_id exynos_tmu_driver_ids[] = {
	{
		.name		= "exynos4210-tmu",
		.driver_data    = (kernel_ulong_t)EXYNOS4210_TMU_DRV_DATA,
	},
	{
		.name		= "exynos5250-tmu",
		.driver_data    = (kernel_ulong_t)EXYNOS5250_TMU_DRV_DATA,
	},
	{ },
};
MODULE_DEVICE_TABLE(platform, exynos_tmu_driver_ids);

static inline struct  exynos_tmu_platform_data *exynos_get_driver_data(
			struct platform_device *pdev)
{
#ifdef CONFIG_OF
	if (pdev->dev.of_node) {
		const struct of_device_id *match;
		match = of_match_node(exynos_tmu_match, pdev->dev.of_node);
		if (!match)
			return NULL;
		return (struct exynos_tmu_platform_data *) match->data;
	}
#endif
	return (struct exynos_tmu_platform_data *)
			platform_get_device_id(pdev)->driver_data;
}

static int exynos_tmu_probe(struct platform_device *pdev)
{
	struct exynos_tmu_data *data;
	struct exynos_tmu_platform_data *pdata = pdev->dev.platform_data;
	int ret, i;

	if (!pdata)
		pdata = exynos_get_driver_data(pdev);

	if (!pdata) {
		dev_err(&pdev->dev, "No platform init data supplied.\n");
		return -ENODEV;
	}
	data = devm_kzalloc(&pdev->dev, sizeof(struct exynos_tmu_data),
					GFP_KERNEL);
	if (!data) {
		dev_err(&pdev->dev, "Failed to allocate driver structure\n");
		return -ENOMEM;
	}

	data->irq = platform_get_irq(pdev, 0);
	if (data->irq < 0) {
		dev_err(&pdev->dev, "Failed to get platform irq\n");
		return data->irq;
	}

	INIT_WORK(&data->irq_work, exynos_tmu_work);

	data->mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	data->base = devm_ioremap_resource(&pdev->dev, data->mem);
	if (IS_ERR(data->base))
		return PTR_ERR(data->base);

	ret = devm_request_irq(&pdev->dev, data->irq, exynos_tmu_irq,
		IRQF_TRIGGER_RISING, "exynos-tmu", data);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request irq: %d\n", data->irq);
		return ret;
	}

	data->clk = devm_clk_get(&pdev->dev, "tmu_apbif");
	if (IS_ERR(data->clk)) {
		dev_err(&pdev->dev, "Failed to get clock\n");
		return  PTR_ERR(data->clk);
	}

	ret = clk_prepare(data->clk);
	if (ret)
		return ret;

	if (pdata->type == SOC_ARCH_EXYNOS ||
				pdata->type == SOC_ARCH_EXYNOS4210)
		data->soc = pdata->type;
	else {
		ret = -EINVAL;
		dev_err(&pdev->dev, "Platform not supported\n");
		goto err_clk;
	}

	data->pdata = pdata;
	platform_set_drvdata(pdev, data);
	mutex_init(&data->lock);

	ret = exynos_tmu_initialize(pdev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to initialize TMU\n");
		goto err_clk;
	}

	exynos_tmu_control(pdev, true);

	/* Register the sensor with thermal management interface */
	(&exynos_sensor_conf)->driver_data = data;
	exynos_sensor_conf.trip_data.trip_count = pdata->trigger_enable[0] +
			pdata->trigger_enable[1] + pdata->trigger_enable[2]+
			pdata->trigger_enable[3];

	for (i = 0; i < exynos_sensor_conf.trip_data.trip_count; i++) {
		exynos_sensor_conf.trip_data.trip_val[i] =
			pdata->threshold + pdata->trigger_levels[i];
		exynos_sensor_conf.trip_data.trip_type[i] =
					pdata->trigger_type[i];
	}

	exynos_sensor_conf.trip_data.trigger_falling = pdata->threshold_falling;

	exynos_sensor_conf.cooling_data.freq_clip_count =
						pdata->freq_tab_count;
	for (i = 0; i < pdata->freq_tab_count; i++) {
		exynos_sensor_conf.cooling_data.freq_data[i].freq_clip_max =
					pdata->freq_tab[i].freq_clip_max;
		exynos_sensor_conf.cooling_data.freq_data[i].temp_level =
					pdata->freq_tab[i].temp_level;
	}

	ret = exynos_register_thermal(&exynos_sensor_conf);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register thermal interface\n");
		goto err_clk;
	}

	return 0;
err_clk:
	clk_unprepare(data->clk);
	return ret;
}

static int exynos_tmu_remove(struct platform_device *pdev)
{
	struct exynos_tmu_data *data = platform_get_drvdata(pdev);

	exynos_tmu_control(pdev, false);

	exynos_unregister_thermal(&exynos_sensor_conf);

	clk_unprepare(data->clk);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int exynos_tmu_suspend(struct device *dev)
{
	exynos_tmu_control(to_platform_device(dev), false);

	return 0;
}

static int exynos_tmu_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);

	exynos_tmu_initialize(pdev);
	exynos_tmu_control(pdev, true);

	return 0;
}

static SIMPLE_DEV_PM_OPS(exynos_tmu_pm,
			 exynos_tmu_suspend, exynos_tmu_resume);
#define EXYNOS_TMU_PM	(&exynos_tmu_pm)
#else
#define EXYNOS_TMU_PM	NULL
#endif

static struct platform_driver exynos_tmu_driver = {
	.driver = {
		.name   = "exynos-tmu",
		.owner  = THIS_MODULE,
		.pm     = EXYNOS_TMU_PM,
		.of_match_table = of_match_ptr(exynos_tmu_match),
	},
	.probe = exynos_tmu_probe,
	.remove	= exynos_tmu_remove,
	.id_table = exynos_tmu_driver_ids,
};

module_platform_driver(exynos_tmu_driver);

MODULE_DESCRIPTION("EXYNOS TMU Driver");
MODULE_AUTHOR("Donggeun Kim <dg77.kim@samsung.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:exynos-tmu");
