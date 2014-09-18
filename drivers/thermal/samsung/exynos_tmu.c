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
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

#include "exynos_thermal_common.h"
#include "exynos_tmu.h"
#include "exynos_tmu_data.h"

/**
 * struct exynos_tmu_data : A structure to hold the private data of the TMU
	driver
 * @id: identifier of the one instance of the TMU controller.
 * @pdata: pointer to the tmu platform/configuration data
 * @base: base address of the single instance of the TMU controller.
 * @base_second: base address of the common registers of the TMU controller.
 * @irq: irq number of the TMU controller.
 * @soc: id of the SOC type.
 * @irq_work: pointer to the irq work structure.
 * @lock: lock to implement synchronization.
 * @clk: pointer to the clock structure.
 * @clk_sec: pointer to the clock structure for accessing the base_second.
 * @temp_error1: fused value of the first point trim.
 * @temp_error2: fused value of the second point trim.
 * @regulator: pointer to the TMU regulator structure.
 * @reg_conf: pointer to structure to register with core thermal.
 */
struct exynos_tmu_data {
	int id;
	struct exynos_tmu_platform_data *pdata;
	void __iomem *base;
	void __iomem *base_second;
	int irq;
	enum soc_type soc;
	struct work_struct irq_work;
	struct mutex lock;
	struct clk *clk, *clk_sec;
	u8 temp_error1, temp_error2;
	struct regulator *regulator;
	struct thermal_sensor_conf *reg_conf;
};

/*
 * TMU treats temperature as a mapped temperature code.
 * The temperature is converted differently depending on the calibration type.
 */
static int temp_to_code(struct exynos_tmu_data *data, u8 temp)
{
	struct exynos_tmu_platform_data *pdata = data->pdata;
	int temp_code;

	if (pdata->cal_mode == HW_MODE)
		return temp;

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

	if (pdata->cal_mode == HW_MODE)
		return temp_code;

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
	if (!IS_ERR(data->clk_sec))
		clk_enable(data->clk_sec);

	if (TMU_SUPPORTS(pdata, READY_STATUS)) {
		status = readb(data->base + reg->tmu_status);
		if (!status) {
			ret = -EBUSY;
			goto out;
		}
	}

	if (TMU_SUPPORTS(pdata, TRIM_RELOAD))
		__raw_writel(1, data->base + reg->triminfo_ctrl);

	if (pdata->cal_mode == HW_MODE)
		goto skip_calib_data;

	/* Save trimming info in order to perform calibration */
	if (data->soc == SOC_ARCH_EXYNOS5440) {
		/*
		 * For exynos5440 soc triminfo value is swapped between TMU0 and
		 * TMU2, so the below logic is needed.
		 */
		switch (data->id) {
		case 0:
			trim_info = readl(data->base +
			EXYNOS5440_EFUSE_SWAP_OFFSET + reg->triminfo_data);
			break;
		case 1:
			trim_info = readl(data->base + reg->triminfo_data);
			break;
		case 2:
			trim_info = readl(data->base -
			EXYNOS5440_EFUSE_SWAP_OFFSET + reg->triminfo_data);
		}
	} else {
		/* On exynos5420 the triminfo register is in the shared space */
		if (data->soc == SOC_ARCH_EXYNOS5420_TRIMINFO)
			trim_info = readl(data->base_second +
							reg->triminfo_data);
		else
			trim_info = readl(data->base + reg->triminfo_data);
	}
	data->temp_error1 = trim_info & EXYNOS_TMU_TEMP_MASK;
	data->temp_error2 = ((trim_info >> reg->triminfo_85_shift) &
				EXYNOS_TMU_TEMP_MASK);

	if (!data->temp_error1 ||
		(pdata->min_efuse_value > data->temp_error1) ||
		(data->temp_error1 > pdata->max_efuse_value))
		data->temp_error1 = pdata->efuse_value & EXYNOS_TMU_TEMP_MASK;

	if (!data->temp_error2)
		data->temp_error2 =
			(pdata->efuse_value >> reg->triminfo_85_shift) &
			EXYNOS_TMU_TEMP_MASK;

skip_calib_data:
	if (pdata->max_trigger_level > MAX_THRESHOLD_LEVS) {
		dev_err(&pdev->dev, "Invalid max trigger level\n");
		ret = -EINVAL;
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

	rising_threshold = readl(data->base + reg->threshold_th0);

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

		writel(reg->intclr_rise_mask, data->base + reg->tmu_intclear);
	} else {
		/* Write temperature code for rising and falling threshold */
		for (i = 0;
		i < trigger_levs && i < EXYNOS_MAX_TRIGGER_PER_REG; i++) {
			threshold_code = temp_to_code(data,
						pdata->trigger_levels[i]);
			if (threshold_code < 0) {
				ret = threshold_code;
				goto out;
			}
			rising_threshold &= ~(0xff << 8 * i);
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

		writel((reg->intclr_rise_mask << reg->intclr_rise_shift) |
			(reg->intclr_fall_mask << reg->intclr_fall_shift),
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
			if (i == EXYNOS_MAX_TRIGGER_PER_REG - 1) {
				/* 1-4 level to be assigned in th0 reg */
				rising_threshold &= ~(0xff << 8 * i);
				rising_threshold |= threshold_code << 8 * i;
				writel(rising_threshold,
					data->base + reg->threshold_th0);
			} else if (i == EXYNOS_MAX_TRIGGER_PER_REG) {
				/* 5th level to be assigned in th2 reg */
				rising_threshold =
				threshold_code << reg->threshold_th3_l0_shift;
				writel(rising_threshold,
					data->base + reg->threshold_th2);
			}
			con = readl(data->base + reg->tmu_ctrl);
			con |= (1 << reg->therm_trip_en_shift);
			writel(con, data->base + reg->tmu_ctrl);
		}
	}
	/*Clear the PMIN in the common TMU register*/
	if (reg->tmu_pmin && !data->id)
		writel(0, data->base_second + reg->tmu_pmin);
out:
	clk_disable(data->clk);
	mutex_unlock(&data->lock);
	if (!IS_ERR(data->clk_sec))
		clk_disable(data->clk_sec);

	return ret;
}

static void exynos_tmu_control(struct platform_device *pdev, bool on)
{
	struct exynos_tmu_data *data = platform_get_drvdata(pdev);
	struct exynos_tmu_platform_data *pdata = data->pdata;
	const struct exynos_tmu_registers *reg = pdata->registers;
	unsigned int con, interrupt_en, cal_val;

	mutex_lock(&data->lock);
	clk_enable(data->clk);

	con = readl(data->base + reg->tmu_ctrl);

	if (pdata->test_mux)
		con |= (pdata->test_mux << reg->test_mux_addr_shift);

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

	if (pdata->cal_mode == HW_MODE) {
		con &= ~(reg->calib_mode_mask << reg->calib_mode_shift);
		cal_val = 0;
		switch (pdata->cal_type) {
		case TYPE_TWO_POINT_TRIMMING:
			cal_val = 3;
			break;
		case TYPE_ONE_POINT_TRIMMING_85:
			cal_val = 2;
			break;
		case TYPE_ONE_POINT_TRIMMING_25:
			cal_val = 1;
			break;
		case TYPE_NONE:
			break;
		default:
			dev_err(&pdev->dev, "Invalid calibration type, using none\n");
		}
		con |= cal_val << reg->calib_mode_shift;
	}

	if (on) {
		con |= (1 << reg->core_en_shift);
		interrupt_en =
			pdata->trigger_enable[3] << reg->inten_rise3_shift |
			pdata->trigger_enable[2] << reg->inten_rise2_shift |
			pdata->trigger_enable[1] << reg->inten_rise1_shift |
			pdata->trigger_enable[0] << reg->inten_rise0_shift;
		if (TMU_SUPPORTS(pdata, FALLING_TRIP))
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

	if (!TMU_SUPPORTS(pdata, EMULATION))
		goto out;

	if (temp && temp < MCELSIUS)
		goto out;

	mutex_lock(&data->lock);
	clk_enable(data->clk);

	val = readl(data->base + reg->emul_con);

	if (temp) {
		temp /= MCELSIUS;

		if (TMU_SUPPORTS(pdata, EMUL_TIME)) {
			val &= ~(EXYNOS_EMUL_TIME_MASK << reg->emul_time_shift);
			val |= (EXYNOS_EMUL_TIME << reg->emul_time_shift);
		}
		val &= ~(EXYNOS_EMUL_DATA_MASK << reg->emul_temp_shift);
		val |= (temp_to_code(data, temp) << reg->emul_temp_shift) |
			EXYNOS_EMUL_ENABLE;
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

static void exynos_tmu_work(struct work_struct *work)
{
	struct exynos_tmu_data *data = container_of(work,
			struct exynos_tmu_data, irq_work);
	struct exynos_tmu_platform_data *pdata = data->pdata;
	const struct exynos_tmu_registers *reg = pdata->registers;
	unsigned int val_irq, val_type;

	if (!IS_ERR(data->clk_sec))
		clk_enable(data->clk_sec);
	/* Find which sensor generated this interrupt */
	if (reg->tmu_irqstatus) {
		val_type = readl(data->base_second + reg->tmu_irqstatus);
		if (!((val_type >> data->id) & 0x1))
			goto out;
	}
	if (!IS_ERR(data->clk_sec))
		clk_disable(data->clk_sec);

	exynos_report_trigger(data->reg_conf);
	mutex_lock(&data->lock);
	clk_enable(data->clk);

	/* TODO: take action based on particular interrupt */
	val_irq = readl(data->base + reg->tmu_intstat);
	/* clear the interrupts */
	writel(val_irq, data->base + reg->tmu_intclear);

	clk_disable(data->clk);
	mutex_unlock(&data->lock);
out:
	enable_irq(data->irq);
}

static irqreturn_t exynos_tmu_irq(int irq, void *id)
{
	struct exynos_tmu_data *data = id;

	disable_irq_nosync(irq);
	schedule_work(&data->irq_work);

	return IRQ_HANDLED;
}

static const struct of_device_id exynos_tmu_match[] = {
	{
		.compatible = "samsung,exynos3250-tmu",
		.data = (void *)EXYNOS3250_TMU_DRV_DATA,
	},
	{
		.compatible = "samsung,exynos4210-tmu",
		.data = (void *)EXYNOS4210_TMU_DRV_DATA,
	},
	{
		.compatible = "samsung,exynos4412-tmu",
		.data = (void *)EXYNOS4412_TMU_DRV_DATA,
	},
	{
		.compatible = "samsung,exynos5250-tmu",
		.data = (void *)EXYNOS5250_TMU_DRV_DATA,
	},
	{
		.compatible = "samsung,exynos5260-tmu",
		.data = (void *)EXYNOS5260_TMU_DRV_DATA,
	},
	{
		.compatible = "samsung,exynos5420-tmu",
		.data = (void *)EXYNOS5420_TMU_DRV_DATA,
	},
	{
		.compatible = "samsung,exynos5420-tmu-ext-triminfo",
		.data = (void *)EXYNOS5420_TMU_DRV_DATA,
	},
	{
		.compatible = "samsung,exynos5440-tmu",
		.data = (void *)EXYNOS5440_TMU_DRV_DATA,
	},
	{},
};
MODULE_DEVICE_TABLE(of, exynos_tmu_match);

static inline struct  exynos_tmu_platform_data *exynos_get_driver_data(
			struct platform_device *pdev, int id)
{
	struct  exynos_tmu_init_data *data_table;
	struct exynos_tmu_platform_data *tmu_data;
	const struct of_device_id *match;

	match = of_match_node(exynos_tmu_match, pdev->dev.of_node);
	if (!match)
		return NULL;
	data_table = (struct exynos_tmu_init_data *) match->data;
	if (!data_table || id >= data_table->tmu_count)
		return NULL;
	tmu_data = data_table->tmu_data;
	return (struct exynos_tmu_platform_data *) (tmu_data + id);
}

static int exynos_map_dt_data(struct platform_device *pdev)
{
	struct exynos_tmu_data *data = platform_get_drvdata(pdev);
	struct exynos_tmu_platform_data *pdata;
	struct resource res;
	int ret;

	if (!data || !pdev->dev.of_node)
		return -ENODEV;

	/*
	 * Try enabling the regulator if found
	 * TODO: Add regulator as an SOC feature, so that regulator enable
	 * is a compulsory call.
	 */
	data->regulator = devm_regulator_get(&pdev->dev, "vtmu");
	if (!IS_ERR(data->regulator)) {
		ret = regulator_enable(data->regulator);
		if (ret) {
			dev_err(&pdev->dev, "failed to enable vtmu\n");
			return ret;
		}
	} else {
		dev_info(&pdev->dev, "Regulator node (vtmu) not found\n");
	}

	data->id = of_alias_get_id(pdev->dev.of_node, "tmuctrl");
	if (data->id < 0)
		data->id = 0;

	data->irq = irq_of_parse_and_map(pdev->dev.of_node, 0);
	if (data->irq <= 0) {
		dev_err(&pdev->dev, "failed to get IRQ\n");
		return -ENODEV;
	}

	if (of_address_to_resource(pdev->dev.of_node, 0, &res)) {
		dev_err(&pdev->dev, "failed to get Resource 0\n");
		return -ENODEV;
	}

	data->base = devm_ioremap(&pdev->dev, res.start, resource_size(&res));
	if (!data->base) {
		dev_err(&pdev->dev, "Failed to ioremap memory\n");
		return -EADDRNOTAVAIL;
	}

	pdata = exynos_get_driver_data(pdev, data->id);
	if (!pdata) {
		dev_err(&pdev->dev, "No platform init data supplied.\n");
		return -ENODEV;
	}
	data->pdata = pdata;
	/*
	 * Check if the TMU shares some registers and then try to map the
	 * memory of common registers.
	 */
	if (!TMU_SUPPORTS(pdata, ADDRESS_MULTIPLE))
		return 0;

	if (of_address_to_resource(pdev->dev.of_node, 1, &res)) {
		dev_err(&pdev->dev, "failed to get Resource 1\n");
		return -ENODEV;
	}

	data->base_second = devm_ioremap(&pdev->dev, res.start,
					resource_size(&res));
	if (!data->base_second) {
		dev_err(&pdev->dev, "Failed to ioremap memory\n");
		return -ENOMEM;
	}

	return 0;
}

static int exynos_tmu_probe(struct platform_device *pdev)
{
	struct exynos_tmu_data *data;
	struct exynos_tmu_platform_data *pdata;
	struct thermal_sensor_conf *sensor_conf;
	int ret, i;

	data = devm_kzalloc(&pdev->dev, sizeof(struct exynos_tmu_data),
					GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	platform_set_drvdata(pdev, data);
	mutex_init(&data->lock);

	ret = exynos_map_dt_data(pdev);
	if (ret)
		return ret;

	pdata = data->pdata;

	INIT_WORK(&data->irq_work, exynos_tmu_work);

	data->clk = devm_clk_get(&pdev->dev, "tmu_apbif");
	if (IS_ERR(data->clk)) {
		dev_err(&pdev->dev, "Failed to get clock\n");
		return  PTR_ERR(data->clk);
	}

	data->clk_sec = devm_clk_get(&pdev->dev, "tmu_triminfo_apbif");
	if (IS_ERR(data->clk_sec)) {
		if (data->soc == SOC_ARCH_EXYNOS5420_TRIMINFO) {
			dev_err(&pdev->dev, "Failed to get triminfo clock\n");
			return PTR_ERR(data->clk_sec);
		}
	} else {
		ret = clk_prepare(data->clk_sec);
		if (ret) {
			dev_err(&pdev->dev, "Failed to get clock\n");
			return ret;
		}
	}

	ret = clk_prepare(data->clk);
	if (ret) {
		dev_err(&pdev->dev, "Failed to get clock\n");
		goto err_clk_sec;
	}

	if (pdata->type == SOC_ARCH_EXYNOS3250 ||
	    pdata->type == SOC_ARCH_EXYNOS4210 ||
	    pdata->type == SOC_ARCH_EXYNOS4412 ||
	    pdata->type == SOC_ARCH_EXYNOS5250 ||
	    pdata->type == SOC_ARCH_EXYNOS5260 ||
	    pdata->type == SOC_ARCH_EXYNOS5420_TRIMINFO ||
	    pdata->type == SOC_ARCH_EXYNOS5440)
		data->soc = pdata->type;
	else {
		ret = -EINVAL;
		dev_err(&pdev->dev, "Platform not supported\n");
		goto err_clk;
	}

	ret = exynos_tmu_initialize(pdev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to initialize TMU\n");
		goto err_clk;
	}

	exynos_tmu_control(pdev, true);

	/* Allocate a structure to register with the exynos core thermal */
	sensor_conf = devm_kzalloc(&pdev->dev,
				sizeof(struct thermal_sensor_conf), GFP_KERNEL);
	if (!sensor_conf) {
		ret = -ENOMEM;
		goto err_clk;
	}
	sprintf(sensor_conf->name, "therm_zone%d", data->id);
	sensor_conf->read_temperature = (int (*)(void *))exynos_tmu_read;
	sensor_conf->write_emul_temp =
		(int (*)(void *, unsigned long))exynos_tmu_set_emulation;
	sensor_conf->driver_data = data;
	sensor_conf->trip_data.trip_count = pdata->trigger_enable[0] +
			pdata->trigger_enable[1] + pdata->trigger_enable[2]+
			pdata->trigger_enable[3];

	for (i = 0; i < sensor_conf->trip_data.trip_count; i++) {
		sensor_conf->trip_data.trip_val[i] =
			pdata->threshold + pdata->trigger_levels[i];
		sensor_conf->trip_data.trip_type[i] =
					pdata->trigger_type[i];
	}

	sensor_conf->trip_data.trigger_falling = pdata->threshold_falling;

	sensor_conf->cooling_data.freq_clip_count = pdata->freq_tab_count;
	for (i = 0; i < pdata->freq_tab_count; i++) {
		sensor_conf->cooling_data.freq_data[i].freq_clip_max =
					pdata->freq_tab[i].freq_clip_max;
		sensor_conf->cooling_data.freq_data[i].temp_level =
					pdata->freq_tab[i].temp_level;
	}
	sensor_conf->dev = &pdev->dev;
	/* Register the sensor with thermal management interface */
	ret = exynos_register_thermal(sensor_conf);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register thermal interface\n");
		goto err_clk;
	}
	data->reg_conf = sensor_conf;

	ret = devm_request_irq(&pdev->dev, data->irq, exynos_tmu_irq,
		IRQF_TRIGGER_RISING | IRQF_SHARED, dev_name(&pdev->dev), data);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request irq: %d\n", data->irq);
		goto err_clk;
	}

	return 0;
err_clk:
	clk_unprepare(data->clk);
err_clk_sec:
	if (!IS_ERR(data->clk_sec))
		clk_unprepare(data->clk_sec);
	return ret;
}

static int exynos_tmu_remove(struct platform_device *pdev)
{
	struct exynos_tmu_data *data = platform_get_drvdata(pdev);

	exynos_unregister_thermal(data->reg_conf);

	exynos_tmu_control(pdev, false);

	clk_unprepare(data->clk);
	if (!IS_ERR(data->clk_sec))
		clk_unprepare(data->clk_sec);

	if (!IS_ERR(data->regulator))
		regulator_disable(data->regulator);

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
		.of_match_table = exynos_tmu_match,
	},
	.probe = exynos_tmu_probe,
	.remove	= exynos_tmu_remove,
};

module_platform_driver(exynos_tmu_driver);

MODULE_DESCRIPTION("EXYNOS TMU Driver");
MODULE_AUTHOR("Donggeun Kim <dg77.kim@samsung.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:exynos-tmu");
