// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020 Spreadtrum Communications Inc.

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/thermal.h>

#define SPRD_THM_CTL			0x0
#define SPRD_THM_INT_EN			0x4
#define SPRD_THM_INT_STS		0x8
#define SPRD_THM_INT_RAW_STS		0xc
#define SPRD_THM_DET_PERIOD		0x10
#define SPRD_THM_INT_CLR		0x14
#define SPRD_THM_INT_CLR_ST		0x18
#define SPRD_THM_MON_PERIOD		0x4c
#define SPRD_THM_MON_CTL		0x50
#define SPRD_THM_INTERNAL_STS1		0x54
#define SPRD_THM_RAW_READ_MSK		0x3ff

#define SPRD_THM_OFFSET(id)		((id) * 0x4)
#define SPRD_THM_TEMP(id)		(SPRD_THM_OFFSET(id) + 0x5c)
#define SPRD_THM_THRES(id)		(SPRD_THM_OFFSET(id) + 0x2c)

#define SPRD_THM_SEN(id)		BIT((id) + 2)
#define SPRD_THM_SEN_OVERHEAT_EN(id)	BIT((id) + 8)
#define SPRD_THM_SEN_OVERHEAT_ALARM_EN(id)	BIT((id) + 0)

/* bits definitions for register THM_CTL */
#define SPRD_THM_SET_RDY_ST		BIT(13)
#define SPRD_THM_SET_RDY		BIT(12)
#define SPRD_THM_MON_EN			BIT(1)
#define SPRD_THM_EN			BIT(0)

/* bits definitions for register THM_INT_CTL */
#define SPRD_THM_BIT_INT_EN		BIT(26)
#define SPRD_THM_OVERHEAT_EN		BIT(25)
#define SPRD_THM_OTP_TRIP_SHIFT		10

/* bits definitions for register SPRD_THM_INTERNAL_STS1 */
#define SPRD_THM_TEMPER_RDY		BIT(0)

#define SPRD_THM_DET_PERIOD_DATA	0x800
#define SPRD_THM_DET_PERIOD_MASK	GENMASK(19, 0)
#define SPRD_THM_MON_MODE		0x7
#define SPRD_THM_MON_MODE_MASK		GENMASK(3, 0)
#define SPRD_THM_MON_PERIOD_DATA	0x10
#define SPRD_THM_MON_PERIOD_MASK	GENMASK(15, 0)
#define SPRD_THM_THRES_MASK		GENMASK(19, 0)
#define SPRD_THM_INT_CLR_MASK		GENMASK(24, 0)

/* thermal sensor calibration parameters */
#define SPRD_THM_TEMP_LOW		-40000
#define SPRD_THM_TEMP_HIGH		120000
#define SPRD_THM_OTP_TEMP		120000
#define SPRD_THM_HOT_TEMP		75000
#define SPRD_THM_RAW_DATA_LOW		0
#define SPRD_THM_RAW_DATA_HIGH		1000
#define SPRD_THM_SEN_NUM		8
#define SPRD_THM_DT_OFFSET		24
#define SPRD_THM_RATION_OFFSET		17
#define SPRD_THM_RATION_SIGN		16

#define SPRD_THM_RDYST_POLLING_TIME	10
#define SPRD_THM_RDYST_TIMEOUT		700
#define SPRD_THM_TEMP_READY_POLL_TIME	10000
#define SPRD_THM_TEMP_READY_TIMEOUT	600000
#define SPRD_THM_MAX_SENSOR		8

struct sprd_thermal_sensor {
	struct thermal_zone_device *tzd;
	struct sprd_thermal_data *data;
	struct device *dev;
	int cal_slope;
	int cal_offset;
	int id;
};

struct sprd_thermal_data {
	const struct sprd_thm_variant_data *var_data;
	struct sprd_thermal_sensor *sensor[SPRD_THM_MAX_SENSOR];
	struct clk *clk;
	void __iomem *base;
	u32 ratio_off;
	int ratio_sign;
	int nr_sensors;
};

/*
 * The conversion between ADC and temperature is based on linear relationship,
 * and use idea_k to specify the slope and ideal_b to specify the offset.
 *
 * Since different Spreadtrum SoCs have different ideal_k and ideal_b,
 * we should save ideal_k and ideal_b in the device data structure.
 */
struct sprd_thm_variant_data {
	u32 ideal_k;
	u32 ideal_b;
};

static const struct sprd_thm_variant_data ums512_data = {
	.ideal_k = 262,
	.ideal_b = 66400,
};

static inline void sprd_thm_update_bits(void __iomem *reg, u32 mask, u32 val)
{
	u32 tmp, orig;

	orig = readl(reg);
	tmp = orig & ~mask;
	tmp |= val & mask;
	writel(tmp, reg);
}

static int sprd_thm_cal_read(struct device_node *np, const char *cell_id,
			     u32 *val)
{
	struct nvmem_cell *cell;
	void *buf;
	size_t len;

	cell = of_nvmem_cell_get(np, cell_id);
	if (IS_ERR(cell))
		return PTR_ERR(cell);

	buf = nvmem_cell_read(cell, &len);
	nvmem_cell_put(cell);
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	if (len > sizeof(u32)) {
		kfree(buf);
		return -EINVAL;
	}

	memcpy(val, buf, len);

	kfree(buf);
	return 0;
}

static int sprd_thm_sensor_calibration(struct device_node *np,
				       struct sprd_thermal_data *thm,
				       struct sprd_thermal_sensor *sen)
{
	int ret;
	/*
	 * According to thermal datasheet, the default calibration offset is 64,
	 * and the default ratio is 1000.
	 */
	int dt_offset = 64, ratio = 1000;

	ret = sprd_thm_cal_read(np, "sen_delta_cal", &dt_offset);
	if (ret)
		return ret;

	ratio += thm->ratio_sign * thm->ratio_off;

	/*
	 * According to the ideal slope K and ideal offset B, combined with
	 * calibration value of thermal from efuse, then calibrate the real
	 * slope k and offset b:
	 * k_cal = (k * ratio) / 1000.
	 * b_cal = b + (dt_offset - 64) * 500.
	 */
	sen->cal_slope = (thm->var_data->ideal_k * ratio) / 1000;
	sen->cal_offset = thm->var_data->ideal_b + (dt_offset - 128) * 250;

	return 0;
}

static int sprd_thm_rawdata_to_temp(struct sprd_thermal_sensor *sen,
				    u32 rawdata)
{
	clamp(rawdata, (u32)SPRD_THM_RAW_DATA_LOW, (u32)SPRD_THM_RAW_DATA_HIGH);

	/*
	 * According to the thermal datasheet, the formula of converting
	 * adc value to the temperature value should be:
	 * T_final = k_cal * x - b_cal.
	 */
	return sen->cal_slope * rawdata - sen->cal_offset;
}

static int sprd_thm_temp_to_rawdata(int temp, struct sprd_thermal_sensor *sen)
{
	u32 val;

	clamp(temp, (int)SPRD_THM_TEMP_LOW, (int)SPRD_THM_TEMP_HIGH);

	/*
	 * According to the thermal datasheet, the formula of converting
	 * adc value to the temperature value should be:
	 * T_final = k_cal * x - b_cal.
	 */
	val = (temp + sen->cal_offset) / sen->cal_slope;

	return clamp(val, val, (u32)(SPRD_THM_RAW_DATA_HIGH - 1));
}

static int sprd_thm_read_temp(void *devdata, int *temp)
{
	struct sprd_thermal_sensor *sen = devdata;
	u32 data;

	data = readl(sen->data->base + SPRD_THM_TEMP(sen->id)) &
		SPRD_THM_RAW_READ_MSK;

	*temp = sprd_thm_rawdata_to_temp(sen, data);

	return 0;
}

static const struct thermal_zone_of_device_ops sprd_thm_ops = {
	.get_temp = sprd_thm_read_temp,
};

static int sprd_thm_poll_ready_status(struct sprd_thermal_data *thm)
{
	u32 val;
	int ret;

	/*
	 * Wait for thermal ready status before configuring thermal parameters.
	 */
	ret = readl_poll_timeout(thm->base + SPRD_THM_CTL, val,
				 !(val & SPRD_THM_SET_RDY_ST),
				 SPRD_THM_RDYST_POLLING_TIME,
				 SPRD_THM_RDYST_TIMEOUT);
	if (ret)
		return ret;

	sprd_thm_update_bits(thm->base + SPRD_THM_CTL, SPRD_THM_MON_EN,
			     SPRD_THM_MON_EN);
	sprd_thm_update_bits(thm->base + SPRD_THM_CTL, SPRD_THM_SET_RDY,
			     SPRD_THM_SET_RDY);
	return 0;
}

static int sprd_thm_wait_temp_ready(struct sprd_thermal_data *thm)
{
	u32 val;

	/* Wait for first temperature data ready before reading temperature */
	return readl_poll_timeout(thm->base + SPRD_THM_INTERNAL_STS1, val,
				  !(val & SPRD_THM_TEMPER_RDY),
				  SPRD_THM_TEMP_READY_POLL_TIME,
				  SPRD_THM_TEMP_READY_TIMEOUT);
}

static int sprd_thm_set_ready(struct sprd_thermal_data *thm)
{
	int ret;

	ret = sprd_thm_poll_ready_status(thm);
	if (ret)
		return ret;

	/*
	 * Clear interrupt status, enable thermal interrupt and enable thermal.
	 *
	 * The SPRD thermal controller integrates a hardware interrupt signal,
	 * which means if the temperature is overheat, it will generate an
	 * interrupt and notify the event to PMIC automatically to shutdown the
	 * system. So here we should enable the interrupt bits, though we have
	 * not registered an irq handler.
	 */
	writel(SPRD_THM_INT_CLR_MASK, thm->base + SPRD_THM_INT_CLR);
	sprd_thm_update_bits(thm->base + SPRD_THM_INT_EN,
			     SPRD_THM_BIT_INT_EN, SPRD_THM_BIT_INT_EN);
	sprd_thm_update_bits(thm->base + SPRD_THM_CTL,
			     SPRD_THM_EN, SPRD_THM_EN);
	return 0;
}

static void sprd_thm_sensor_init(struct sprd_thermal_data *thm,
				 struct sprd_thermal_sensor *sen)
{
	u32 otp_rawdata, hot_rawdata;

	otp_rawdata = sprd_thm_temp_to_rawdata(SPRD_THM_OTP_TEMP, sen);
	hot_rawdata = sprd_thm_temp_to_rawdata(SPRD_THM_HOT_TEMP, sen);

	/* Enable the sensor' overheat temperature protection interrupt */
	sprd_thm_update_bits(thm->base + SPRD_THM_INT_EN,
			     SPRD_THM_SEN_OVERHEAT_ALARM_EN(sen->id),
			     SPRD_THM_SEN_OVERHEAT_ALARM_EN(sen->id));

	/* Set the sensor' overheat and hot threshold temperature */
	sprd_thm_update_bits(thm->base + SPRD_THM_THRES(sen->id),
			     SPRD_THM_THRES_MASK,
			     (otp_rawdata << SPRD_THM_OTP_TRIP_SHIFT) |
			     hot_rawdata);

	/* Enable the corresponding sensor */
	sprd_thm_update_bits(thm->base + SPRD_THM_CTL, SPRD_THM_SEN(sen->id),
			     SPRD_THM_SEN(sen->id));
}

static void sprd_thm_para_config(struct sprd_thermal_data *thm)
{
	/* Set the period of two valid temperature detection action */
	sprd_thm_update_bits(thm->base + SPRD_THM_DET_PERIOD,
			     SPRD_THM_DET_PERIOD_MASK, SPRD_THM_DET_PERIOD);

	/* Set the sensors' monitor mode */
	sprd_thm_update_bits(thm->base + SPRD_THM_MON_CTL,
			     SPRD_THM_MON_MODE_MASK, SPRD_THM_MON_MODE);

	/* Set the sensors' monitor period */
	sprd_thm_update_bits(thm->base + SPRD_THM_MON_PERIOD,
			     SPRD_THM_MON_PERIOD_MASK, SPRD_THM_MON_PERIOD);
}

static void sprd_thm_toggle_sensor(struct sprd_thermal_sensor *sen, bool on)
{
	struct thermal_zone_device *tzd = sen->tzd;

	if (on)
		thermal_zone_device_enable(tzd);
	else
		thermal_zone_device_disable(tzd);
}

static int sprd_thm_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *sen_child;
	struct sprd_thermal_data *thm;
	struct sprd_thermal_sensor *sen;
	const struct sprd_thm_variant_data *pdata;
	int ret, i;
	u32 val;

	pdata = of_device_get_match_data(&pdev->dev);
	if (!pdata) {
		dev_err(&pdev->dev, "No matching driver data found\n");
		return -EINVAL;
	}

	thm = devm_kzalloc(&pdev->dev, sizeof(*thm), GFP_KERNEL);
	if (!thm)
		return -ENOMEM;

	thm->var_data = pdata;
	thm->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(thm->base))
		return PTR_ERR(thm->base);

	thm->nr_sensors = of_get_child_count(np);
	if (thm->nr_sensors == 0 || thm->nr_sensors > SPRD_THM_MAX_SENSOR) {
		dev_err(&pdev->dev, "incorrect sensor count\n");
		return -EINVAL;
	}

	thm->clk = devm_clk_get(&pdev->dev, "enable");
	if (IS_ERR(thm->clk)) {
		dev_err(&pdev->dev, "failed to get enable clock\n");
		return PTR_ERR(thm->clk);
	}

	ret = clk_prepare_enable(thm->clk);
	if (ret)
		return ret;

	sprd_thm_para_config(thm);

	ret = sprd_thm_cal_read(np, "thm_sign_cal", &val);
	if (ret)
		goto disable_clk;

	if (val > 0)
		thm->ratio_sign = -1;
	else
		thm->ratio_sign = 1;

	ret = sprd_thm_cal_read(np, "thm_ratio_cal", &thm->ratio_off);
	if (ret)
		goto disable_clk;

	for_each_child_of_node(np, sen_child) {
		sen = devm_kzalloc(&pdev->dev, sizeof(*sen), GFP_KERNEL);
		if (!sen) {
			ret = -ENOMEM;
			goto of_put;
		}

		sen->data = thm;
		sen->dev = &pdev->dev;

		ret = of_property_read_u32(sen_child, "reg", &sen->id);
		if (ret) {
			dev_err(&pdev->dev, "get sensor reg failed");
			goto of_put;
		}

		ret = sprd_thm_sensor_calibration(sen_child, thm, sen);
		if (ret) {
			dev_err(&pdev->dev, "efuse cal analysis failed");
			goto of_put;
		}

		sprd_thm_sensor_init(thm, sen);

		sen->tzd = devm_thermal_zone_of_sensor_register(sen->dev,
								sen->id,
								sen,
								&sprd_thm_ops);
		if (IS_ERR(sen->tzd)) {
			dev_err(&pdev->dev, "register thermal zone failed %d\n",
				sen->id);
			ret = PTR_ERR(sen->tzd);
			goto of_put;
		}

		thm->sensor[sen->id] = sen;
	}
	/* sen_child set to NULL at this point */

	ret = sprd_thm_set_ready(thm);
	if (ret)
		goto of_put;

	ret = sprd_thm_wait_temp_ready(thm);
	if (ret)
		goto of_put;

	for (i = 0; i < thm->nr_sensors; i++)
		sprd_thm_toggle_sensor(thm->sensor[i], true);

	platform_set_drvdata(pdev, thm);
	return 0;

of_put:
	of_node_put(sen_child);
disable_clk:
	clk_disable_unprepare(thm->clk);
	return ret;
}

#ifdef CONFIG_PM_SLEEP
static void sprd_thm_hw_suspend(struct sprd_thermal_data *thm)
{
	int i;

	for (i = 0; i < thm->nr_sensors; i++) {
		sprd_thm_update_bits(thm->base + SPRD_THM_CTL,
				     SPRD_THM_SEN(thm->sensor[i]->id), 0);
	}

	sprd_thm_update_bits(thm->base + SPRD_THM_CTL,
			     SPRD_THM_EN, 0x0);
}

static int sprd_thm_suspend(struct device *dev)
{
	struct sprd_thermal_data *thm = dev_get_drvdata(dev);
	int i;

	for (i = 0; i < thm->nr_sensors; i++)
		sprd_thm_toggle_sensor(thm->sensor[i], false);

	sprd_thm_hw_suspend(thm);
	clk_disable_unprepare(thm->clk);

	return 0;
}

static int sprd_thm_hw_resume(struct sprd_thermal_data *thm)
{
	int ret, i;

	for (i = 0; i < thm->nr_sensors; i++) {
		sprd_thm_update_bits(thm->base + SPRD_THM_CTL,
				     SPRD_THM_SEN(thm->sensor[i]->id),
				     SPRD_THM_SEN(thm->sensor[i]->id));
	}

	ret = sprd_thm_poll_ready_status(thm);
	if (ret)
		return ret;

	writel(SPRD_THM_INT_CLR_MASK, thm->base + SPRD_THM_INT_CLR);
	sprd_thm_update_bits(thm->base + SPRD_THM_CTL,
			     SPRD_THM_EN, SPRD_THM_EN);
	return sprd_thm_wait_temp_ready(thm);
}

static int sprd_thm_resume(struct device *dev)
{
	struct sprd_thermal_data *thm = dev_get_drvdata(dev);
	int ret, i;

	ret = clk_prepare_enable(thm->clk);
	if (ret)
		return ret;

	ret = sprd_thm_hw_resume(thm);
	if (ret)
		goto disable_clk;

	for (i = 0; i < thm->nr_sensors; i++)
		sprd_thm_toggle_sensor(thm->sensor[i], true);

	return 0;

disable_clk:
	clk_disable_unprepare(thm->clk);
	return ret;
}
#endif

static int sprd_thm_remove(struct platform_device *pdev)
{
	struct sprd_thermal_data *thm = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < thm->nr_sensors; i++) {
		sprd_thm_toggle_sensor(thm->sensor[i], false);
		devm_thermal_zone_of_sensor_unregister(&pdev->dev,
						       thm->sensor[i]->tzd);
	}

	clk_disable_unprepare(thm->clk);
	return 0;
}

static const struct of_device_id sprd_thermal_of_match[] = {
	{ .compatible = "sprd,ums512-thermal", .data = &ums512_data },
	{ },
};
MODULE_DEVICE_TABLE(of, sprd_thermal_of_match);

static const struct dev_pm_ops sprd_thermal_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(sprd_thm_suspend, sprd_thm_resume)
};

static struct platform_driver sprd_thermal_driver = {
	.probe = sprd_thm_probe,
	.remove = sprd_thm_remove,
	.driver = {
		.name = "sprd-thermal",
		.pm = &sprd_thermal_pm_ops,
		.of_match_table = sprd_thermal_of_match,
	},
};

module_platform_driver(sprd_thermal_driver);

MODULE_AUTHOR("Freeman Liu <freeman.liu@unisoc.com>");
MODULE_DESCRIPTION("Spreadtrum thermal driver");
MODULE_LICENSE("GPL v2");
