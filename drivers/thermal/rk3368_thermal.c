/*
 * Copyright (c) 2017, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/thermal.h>
#include <linux/timer.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/reboot.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/nvmem-consumer.h>
#include <linux/pm_qos.h>
#include <soc/rockchip/scpi.h>

/**
 * If the temperature over a period of time High,
 * the resulting TSHUT gave CRU module,let it reset the entire chip,
 * or via GPIO give PMIC.
 */
enum tshut_mode {
	TSHUT_MODE_CRU = 0,
	TSHUT_MODE_GPIO,
};

enum tsadc_mode {
	TSADC_AUTO_MODE = 0,
	TSHUT_USER_MODE,
};

/**
 * the system Temperature Sensors tshut(tshut) polarity
 * the bit 8 is tshut polarity.
 * 0: low active, 1: high active
 */
enum tshut_polarity {
	TSHUT_LOW_ACTIVE = 0,
	TSHUT_HIGH_ACTIVE,
};

#define NUM_SENSORS	2

/* TSADC V2 Sensor info define: */
#define TSADCV2_USER_CON			0x00
#define TSADCV2_AUTO_CON			0x04
#define TSADCV2_INT_EN				0x08
#define TSADCV2_INT_PD				0x0c
#define TSADCV2_DATA(chn)			(0x20 + (chn) * 0x04)

#define TSADC_CLK_CYCLE_TIME        32	/* usec */
#define TSADCV3_DATA_MASK			0x3ff

/**
 * The conversion table has the adc value and temperature.
 * ADC_DECREMENT: the adc value is of diminishing.(e.g. rk3288_code_table)
 * ADC_INCREMENT: the adc value is incremental.(e.g. rk3368_code_table)
 */
enum adc_sort_mode {
	ADC_DECREMENT = 0,
	ADC_INCREMENT,
};

#define TIME_OUT_TOTAL 2000
#define INVALID_EFUSE_VALUE           0xff

enum {
	ACCESS_FORBIDDEN = 0,
};

#define MIN_TEMP (-40000)
#define MAX_TEMP (125000)
#define INVALID_TEMP INT_MAX

#define BASE (1024)
#define BASE_SHIFT (10)
#define START_BOUNDING_COUNT (100)
#define HIGHER_BOUNDING_TEMP (30)
#define LOWER_BOUNDING_TEMP (15)

/**
 * struct tsadc_table - hold information about code and temp mapping
 * @code: raw code from tsadc ip
 * @temp: the mapping temperature
 */

struct tsadc_table {
	unsigned long code;
	int temp;
};

/**
 * struct chip_tsadc_table - hold information about chip-specific differences
 * @id: conversion table
 * @length: size of conversion table
 * @data_mask: mask to apply on data inputs
 * @mode: sort mode of this adc variant (incrementing or decrementing)
 */
struct chip_tsadc_table {
	const struct tsadc_table *id;
	unsigned int length;
	u32 data_mask;
	enum adc_sort_mode mode;
};

/**
 * struct rk3368_tsadc_chip - hold the private data of tsadc chip
 * @chn_id[SOC_MAX_SENSORS]: the sensor id of chip correspond to the channel
 * @chn_num: the channel number of tsadc chip
 * @tshut_temp: the hardware-controlled shutdown temperature value
 * @tshut_mode: the hardware-controlled shutdown mode (0:CRU 1:GPIO)
 * @tshut_polarity: the hardware-controlled active polarity (0:LOW 1:HIGH)
 * @chip_tsadc_table: the chip-specific conversion table
 * @get_temp: get the temperature
 * @set_alarm_temp: set the high temperature interrupt
 * @set_tshut_temp: set the hardware-controlled shutdown temperature
 * @set_tshut_mode: set the hardware-controlled shutdown mode
 */
struct rk3368_tsadc_chip {
	int chn_id[NUM_SENSORS];
	int chn_num;
	long hw_shut_temp;
	enum tshut_mode tshut_mode;
	enum tsadc_mode mode;
	enum tshut_polarity tshut_polarity;
	int latency_bound;

	const struct chip_tsadc_table *temp_table;

	/* Per-sensor methods */
	int (*get_temp)(const struct chip_tsadc_table *table,
			int chn, void __iomem *reg, int *temp);
	void (*set_alarm_temp)(const struct chip_tsadc_table *table,
			       int chn, void __iomem *reg, int temp);
	void (*set_tshut_temp)(const struct chip_tsadc_table *table,
			       int chn, void __iomem *reg, int temp);
	void (*set_tshut_mode)(int chn, void __iomem *reg, enum tshut_mode m);
};

/**
 * struct rk3368_thermal_sensor - hold the information of thermal sensor
 * @ctx:  pointer to the platform/configuration data
 * @tzd: pointer to a thermal zone
 * @id: identifier of the thermal sensor
 */
struct rk3368_thermal_sensor {
	struct rk3368_thermal_data *ctx;
	struct thermal_zone_device *tzd;
	int id;
};

/**
 * struct rk3368_thermal_data - hold the private data of thermal driver
 * @chip: pointer to the platform/configuration data
 * @pdev: platform device of thermal
 * @reset: the reset controller of tsadc
 * @sensors[SOC_MAX_SENSORS]: the thermal sensor
 * @clk: the controller clock is divided by the external 24MHz
 * @pclk: the advanced peripherals bus clock
 * @regs: the base address of tsadc controller
 * @tshut_temp: the hardware-controlled shutdown temperature value
 * @tshut_mode: the hardware-controlled shutdown mode (0:CRU 1:GPIO)
 * @tshut_polarity: the hardware-controlled active polarity (0:LOW 1:HIGH)
 * @cpu_temp_adjust: efuse value used to ajust the temperature
 * @gpu_temp_adjust: efuse value used to ajust the temperature
 * @cpu_temp: the current cpu's temperature
 * @logout: switch to control log output or not
 * @rk3368_thermal_kobj: node in sys fs
 */
struct rk3368_thermal_data {
	const struct rk3368_tsadc_chip *chip;
	struct platform_device *pdev;
	struct reset_control *reset;

	struct rk3368_thermal_sensor sensors[NUM_SENSORS];

	struct clk *clk;
	struct clk *pclk;
	void __iomem *regs;

	long hw_shut_temp;
	enum tshut_mode tshut_mode;
	enum tshut_polarity tshut_polarity;

	int cpu_temp_adjust;
	int gpu_temp_adjust;
	int cpu_temp;
	bool logout;
	struct kobject *rk3368_thermal_kobj;
	struct regulator *ref_regulator;
	int regulator_uv;
	int latency_req;
	int latency_bound;
	struct notifier_block tsadc_nb;
};

static struct rk3368_thermal_data *thermal_ctx;

static DEFINE_MUTEX(thermal_reg_mutex);

static DEFINE_MUTEX(thermal_lat_mutex);

static const struct tsadc_table code_table_3368[] = {
	{0, MIN_TEMP},
	{106, MIN_TEMP},
	{108, -35000},
	{110, -30000},
	{112, -25000},
	{114, -20000},
	{116, -15000},
	{118, -10000},
	{120, -5000},
	{122, 0},
	{124, 5000},
	{126, 10000},
	{128, 15000},
	{130, 20000},
	{132, 25000},
	{134, 30000},
	{136, 35000},
	{138, 40000},
	{140, 45000},
	{142, 50000},
	{144, 55000},
	{146, 60000},
	{148, 65000},
	{150, 70000},
	{152, 75000},
	{154, 80000},
	{156, 85000},
	{158, 90000},
	{160, 95000},
	{162, 100000},
	{163, 105000},
	{165, 110000},
	{167, 115000},
	{169, 120000},
	{171, MAX_TEMP},
	{TSADCV3_DATA_MASK, MAX_TEMP},
};

static const struct chip_tsadc_table tsadc_table_3368 = {
	.id = code_table_3368,
	.length = ARRAY_SIZE(code_table_3368),
	.data_mask = TSADCV3_DATA_MASK,
	.mode = ADC_INCREMENT,
};

static int rk3368_get_ajust_code(struct device_node *np, int *ajust_code)
{
	struct nvmem_cell *cell;
	unsigned char *buf;
	size_t len;

	cell = of_nvmem_cell_get(np, "temp_adjust");
	if (IS_ERR(cell)) {
		pr_err("avs failed to get temp_adjust cell\n");
		return PTR_ERR(cell);
	}

	buf = (unsigned char *)nvmem_cell_read(cell, &len);

	nvmem_cell_put(cell);

	if (IS_ERR(buf))
		return PTR_ERR(buf);

	if (buf[0] == INVALID_EFUSE_VALUE)
		return -EINVAL;

	if (buf[0] & 0x80)
		*ajust_code = -(buf[0] & 0x7f);
	else
		*ajust_code = buf[0];

	kfree(buf);

	return 0;
}

static struct rk3368_thermal_data *rk3368_thermal_get_data(void)
{
	WARN_ON(!thermal_ctx);
	return thermal_ctx;
}

static int rk3368_temp_to_code(const struct chip_tsadc_table *tmp_table,
				 long temp, u32 *code)
{
	unsigned int low = 1;
	unsigned int high = tmp_table->length - 1;
	unsigned int mid = (low + high) / 2;
	unsigned int num;
	unsigned long denom;
	*code = tmp_table->data_mask;

	WARN_ON(tmp_table->length < 2);

	if (temp < tmp_table->id[low].temp)
		return -EAGAIN;	/* Incorrect reading */

	while (low <= high) {
		if (temp == tmp_table->id[mid].temp) {
			*code = tmp_table->id[mid].code;
			break;
		} else if (temp > tmp_table->id[mid].temp) {
			low = mid + 1;
		} else {
			high = mid - 1;
		}

		mid = (low + high) / 2;
	}
	/*
	 * The 5C granularity provided by the table is too much. Let's
	 * assume that the relationship between sensor readings and
	 * temperature between 2 table entries is linear and interpolate
	 * to produce less granular result.
	 */
	if (*code == tmp_table->data_mask) {
		num = abs(tmp_table->id[low].code - tmp_table->id[high].code);
		num *= abs(tmp_table->id[high].temp - temp);
		denom = abs(tmp_table->id[high].temp - tmp_table->id[low].temp);
		*code = tmp_table->id[high].code + (num / denom);
	}

	return 0;
}

static int rk3368_code_to_temp(const struct chip_tsadc_table *tmp_table,
				 u32 code, int *temp)
{
	unsigned int low = 1;
	unsigned int high = tmp_table->length - 1;
	unsigned int mid = (low + high) / 2;
	unsigned int num;
	unsigned long denom;
	*temp = INVALID_TEMP;

	WARN_ON(tmp_table->length < 2);

	switch (tmp_table->mode) {
	case ADC_DECREMENT:
		code &= tmp_table->data_mask;
		if (code < tmp_table->id[high].code)
			return -EAGAIN;	/* Incorrect reading */

		while (low <= high) {
			if (code == tmp_table->id[mid].code) {
				*temp = tmp_table->id[mid].temp;
				break;
			} else if (code < tmp_table->id[mid].code) {
				low = mid + 1;
			} else {
				high = mid - 1;
			}

			mid = (low + high) / 2;
		}
		break;
	case ADC_INCREMENT:
		code &= tmp_table->data_mask;
		if (code < tmp_table->id[low].code)
			return -EAGAIN;	/* Incorrect reading */

		while (low <= high) {
			if (code == tmp_table->id[mid].code) {
				*temp = tmp_table->id[mid].temp;
				break;
			} else if (code > tmp_table->id[mid].code) {
				low = mid + 1;
			} else {
				high = mid - 1;
			}

			mid = (low + high) / 2;
		}
		break;
	default:
		pr_err("Invalid the conversion table\n");
	}

	/*
	 * The 5C granularity provided by the table is too much. Let's
	 * assume that the relationship between sensor readings and
	 * temperature between 2 table entries is linear and interpolate
	 * to produce less granular result.
	 */
	if (*temp == INVALID_TEMP) {
		num = abs(tmp_table->id[low].temp - tmp_table->id[high].temp);
		num *= abs(tmp_table->id[high].code - code);
		denom = abs(tmp_table->id[high].code - tmp_table->id[low].code);
		*temp = tmp_table->id[high].temp + (num / denom);
	}

	return 0;
}

static const struct rk3368_tsadc_chip rk3368_tsadc_data = {
	.tshut_mode = TSHUT_MODE_GPIO,	/* default TSHUT via GPIO give PMIC */
	.tshut_polarity = TSHUT_LOW_ACTIVE,	/* default TSHUT LOW ACTIVE */
	.latency_bound = 50000,	/* default 50000 us */
	.hw_shut_temp = 125000,
	.mode = TSHUT_USER_MODE,
	.chn_num = 2,
	.chn_id[0] = 0,
	.chn_id[1] = 1,
	.temp_table = &tsadc_table_3368,
};

static int rk3368_configure_from_dt(struct device *dev,
				      struct device_node *np,
				      struct rk3368_thermal_data *thermal)
{
	u32 shut_temp;
	u32 rate;
	u32 cycle;
	int lat_bound;
	int ret;

	if (of_property_read_u32(np, "clock-frequency", &rate)) {
		dev_err(dev, "Missing clock-frequency property in the DT.\n");
		return -EINVAL;
	}
	ret = clk_set_rate(thermal->clk, rate);

	cycle = DIV_ROUND_UP(1000000000, rate) / 1000;

	if (scpi_thermal_set_clk_cycle(cycle)) {
		dev_err(dev, "scpi_thermal_set_clk_cycle error.\n");
		return -EINVAL;
	}

	if (of_property_read_u32(np, "hw-shut-temp", &shut_temp)) {
		dev_warn(dev,
			 "Missing tshut temp property, using default %ld\n",
			 thermal->chip->hw_shut_temp);
		thermal->hw_shut_temp = thermal->chip->hw_shut_temp;
	} else {
		thermal->hw_shut_temp = shut_temp;
	}

	if (of_property_read_u32(np, "latency-bound", &lat_bound)) {
		dev_warn(dev,
			 "Missing latency-bound property, using default %d\n",
			 thermal->chip->latency_bound);
		thermal->latency_bound = thermal->chip->latency_bound;
	} else {
		thermal->latency_bound = lat_bound;
	}

	if (thermal->hw_shut_temp > INT_MAX) {
		dev_err(dev, "Invalid tshut temperature specified: %ld\n",
			thermal->hw_shut_temp);
		return -ERANGE;
	}

	return 0;
}

static int predict_temp(int temp)
{
	int cov_q = 18;
	int cov_r = 542;

	int gain;
	int temp_mid;
	int temp_now;
	int prob_mid;
	int prob_now;
	static int temp_last = 25;
	static int prob_last = 20;
	static int bounding_cnt;

	struct rk3368_thermal_data *ctx = rk3368_thermal_get_data();

	if (!ctx)
		return INVALID_TEMP;

	if (bounding_cnt++ > START_BOUNDING_COUNT) {
		bounding_cnt = START_BOUNDING_COUNT;
		if (temp - temp_last > HIGHER_BOUNDING_TEMP)
			temp = temp_last + HIGHER_BOUNDING_TEMP / 3;
		if (temp_last - temp > LOWER_BOUNDING_TEMP)
			temp = temp_last - LOWER_BOUNDING_TEMP / 3;
	}

	temp_mid = temp_last;
	prob_mid = prob_last + cov_q;
	gain = (prob_mid * BASE) / (prob_mid + cov_r);

	temp_now = temp_mid + (gain * (temp - temp_mid) >> BASE_SHIFT);
	prob_now = ((BASE - gain) * prob_mid) >> BASE_SHIFT;

	prob_last = prob_now;
	temp_last = temp_now;

	if (ctx->logout)
		pr_info("prob_now %d, temp_last %d, temp %d gain %d", prob_now,
			temp_now, temp, gain);

	return temp_last;
}

static int get_raw_code_internal(void)
{
	u32 val_cpu_pd;
	int val_cpu = INVALID_TEMP;
	int i;
	struct rk3368_thermal_data *ctx = rk3368_thermal_get_data();

	if (!ctx)
		return INVALID_TEMP;

	/* power up, channel 0 */
	writel_relaxed(0x18, ctx->regs + TSADCV2_USER_CON);

	udelay(TSADC_CLK_CYCLE_TIME * 2);
	/* start working */
	writel_relaxed(0x38, ctx->regs + TSADCV2_USER_CON);
	udelay(TSADC_CLK_CYCLE_TIME * 13);

	/* try 50 times */
	for (i = 0; i < 50; i++) {
		udelay(TSADC_CLK_CYCLE_TIME);
		val_cpu_pd = readl_relaxed(ctx->regs + TSADCV2_INT_PD);

		if ((val_cpu_pd & 0x100) == 0x100) {
			udelay(1);
			/*clear eoc inter */
			writel_relaxed(0x100, ctx->regs + TSADCV2_INT_PD);
			/*read adc data */
			val_cpu = readl_relaxed(ctx->regs + TSADCV2_DATA(0));
			break;
		}
	}
	/*power down, channel 0 */
	writel_relaxed(0x0, ctx->regs + TSADCV2_USER_CON);

	return val_cpu;
}

#define RAW_CODE_MIN (50)
#define RAW_CODE_MAX (225)

static int rk3368_get_raw_code(struct rk3368_thermal_data *ctx)
{
	static int old_data = 130;
	int tsadc_data = 0;

	if (ctx->latency_req > ctx->latency_bound)
		tsadc_data = scpi_thermal_get_temperature();
	else
		tsadc_data = get_raw_code_internal();

	if ((tsadc_data < RAW_CODE_MIN) || (tsadc_data > RAW_CODE_MAX))
		tsadc_data = old_data;
	else
		old_data = tsadc_data;

	return tsadc_data;
}

static int rk3368_convert_code_2_temp(int tsadc_data, int voltage)
{
	struct rk3368_thermal_data *ctx = rk3368_thermal_get_data();
	const struct rk3368_tsadc_chip *tsadc;
	int out_temp;
	static int old_temp;
	int data_adjust;

	u32 code_temp;
	u32 tmp_code1;
	u32 tmp_code2;

	if (!ctx)
		return INVALID_TEMP;

	tsadc = ctx->chip;

	rk3368_temp_to_code(tsadc->temp_table,
			      ctx->cpu_temp_adjust * 1000, &tmp_code1);
	rk3368_temp_to_code(tsadc->temp_table, 0, &tmp_code2);
	data_adjust = tmp_code1 - tmp_code2;
	code_temp =
	    ((tsadc_data * voltage - data_adjust * 1000000) + 500000) / 1000000;
	rk3368_code_to_temp(tsadc->temp_table, code_temp, &out_temp);

	if (ctx->logout)
		pr_info("cpu code temp:[%d, %d], voltage: %d\n",
			tsadc_data, out_temp / 1000, voltage);

	if ((out_temp < MIN_TEMP) || (out_temp > MAX_TEMP))
		out_temp = old_temp;
	else
		old_temp = out_temp;

	ctx->cpu_temp = out_temp / 1000;
	return out_temp;
}

static int rk3368_thermal_set_trips(void *_sensor, int low, int high)
{
	return 0;
}

static int rk3368_thermal_get_temp(void *_sensor, int *out_temp)
{
	int raw_code;
	int temp;
	struct rk3368_thermal_data *ctx = rk3368_thermal_get_data();
	struct platform_device *pdev;

	if (!ctx)
		return INVALID_TEMP;

	pdev = ctx->pdev;

	mutex_lock(&thermal_reg_mutex);
	raw_code = rk3368_get_raw_code(ctx);
	temp = rk3368_convert_code_2_temp(raw_code, ctx->regulator_uv);
	*out_temp = predict_temp(temp / 1000) * 1000;
	mutex_unlock(&thermal_reg_mutex);

	return 0;
}

static const struct thermal_zone_of_device_ops rk3368_of_thermal_ops = {
	.get_temp = rk3368_thermal_get_temp,
	.set_trips = rk3368_thermal_set_trips,
};

static int
rk3368_thermal_register_sensor(struct platform_device *pdev,
				 struct rk3368_thermal_data *ctx,
				 struct rk3368_thermal_sensor *sensor, int id)
{
	int error;

	sensor->ctx = ctx;
	sensor->id = id;
	sensor->tzd = devm_thermal_zone_of_sensor_register(&pdev->dev, id,
							   sensor,
							   &rk3368_of_thermal_ops);
	if (IS_ERR(sensor->tzd)) {
		error = PTR_ERR(sensor->tzd);
		dev_err(&pdev->dev, "failed to register sensor %d: %d\n",
			id, error);
		return error;
	}

	return 0;
}

/*
 * Reset TSADC Controller, reset all tsadc registers.
 */
static void rk3368_thermal_reset_controller(struct reset_control *reset)
{
	reset_control_assert(reset);
	udelay(10);
	reset_control_deassert(reset);
}

static ssize_t rk3368_thermal_temp_adjust_test_store(struct kobject *kobj,
						       struct kobj_attribute
						       *attr, const char *buf,
						       size_t n)
{
	struct rk3368_thermal_data *ctx = rk3368_thermal_get_data();
	int getdata;
	char cmd;
	const char *buftmp = buf;
	int ret;

	if (!ctx)
		return n;

	ret = sscanf(buftmp, "%c ", &cmd);
	if (ret != 1)
		return -EINVAL;

	switch (cmd) {
	case 'c':
		ret = sscanf(buftmp, "%c %d", &cmd, &getdata);
		if (ret != 2)
			return -EINVAL;
		ctx->cpu_temp_adjust = getdata;
		pr_info("get cpu_temp_adjust value = %d\n", getdata);

		break;
	case 'g':
		ret = sscanf(buftmp, "%c %d", &cmd, &getdata);
		if (ret != 2)
			return -EINVAL;
		ctx->gpu_temp_adjust = getdata;
		pr_info("get gpu_temp_adjust value = %d\n", getdata);

		break;
	default:
		pr_info("Unknown command\n");
		break;
	}

	return n;
}

static ssize_t rk3368_thermal_temp_adjust_test_show(struct kobject *kobj,
						      struct kobj_attribute
						      *attr, char *buf)
{
	struct rk3368_thermal_data *ctx = rk3368_thermal_get_data();
	char *str = buf;

	if (!ctx)
		return 0;

	str +=
	    sprintf(str, "rk3368_thermal: cpu:%d, gpu:%d\n",
		    ctx->cpu_temp_adjust, ctx->gpu_temp_adjust);
	return (str - buf);
}

static ssize_t rk3368_thermal_temp_test_store(struct kobject *kobj,
						struct kobj_attribute *attr,
						const char *buf, size_t n)
{
	struct rk3368_thermal_data *ctx = rk3368_thermal_get_data();
	char cmd;
	const char *buftmp = buf;
	int ret;

	if (!ctx)
		return n;

	ret = sscanf(buftmp, "%c", &cmd);
	if (ret != 1)
		return -EINVAL;

	switch (cmd) {
	case 't':
		ctx->logout = true;
		break;
	case 'f':
		ctx->logout = false;
		break;
	default:
		pr_info("Unknown command\n");
		break;
	}

	return n;
}

static ssize_t rk3368_thermal_temp_test_show(struct kobject *kobj,
					       struct kobj_attribute *attr,
					       char *buf)
{
	struct rk3368_thermal_data *ctx = rk3368_thermal_get_data();
	char *str = buf;

	if (!ctx)
		return 0;

	str += sprintf(str, "current cpu_temp:%d\n", ctx->cpu_temp);
	return (str - buf);
}

struct rk3368_thermal_attribute {
	struct attribute attr;
	ssize_t (*show) (struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf);
	ssize_t (*store) (struct kobject *kobj, struct kobj_attribute *attr,
			  const char *buf, size_t n);
};

static struct rk3368_thermal_attribute rk3368_thermal_attrs[] = {
	/*node_name permission show_func store_func */
	__ATTR(temp_adjust, 0644,
	       rk3368_thermal_temp_adjust_test_show,
	       rk3368_thermal_temp_adjust_test_store),
	__ATTR(temp, 0644, rk3368_thermal_temp_test_show,
	       rk3368_thermal_temp_test_store),
};

static void rk3368_dump_temperature(void)
{
	struct rk3368_thermal_data *ctx = rk3368_thermal_get_data();
	struct platform_device *pdev;

	if (!ctx)
		return;

	pdev = ctx->pdev;

	if (ctx->cpu_temp != INVALID_TEMP)
		dev_warn(&pdev->dev, "cpu channal temperature(%d C)\n",
			 ctx->cpu_temp);

	if (ctx->regs) {
		pr_warn("THERMAL REGS:\n");
		print_hex_dump(KERN_WARNING, "", DUMP_PREFIX_OFFSET,
			       32, 4, ctx->regs, 0x88, false);
	}
}
EXPORT_SYMBOL_GPL(rk3368_dump_temperature);

static int rk3368_thermal_panic(struct notifier_block *this,
				  unsigned long ev, void *ptr)
{
	rk3368_dump_temperature();
	return NOTIFY_DONE;
}

static struct notifier_block rk3368_thermal_panic_block = {
	.notifier_call = rk3368_thermal_panic,
};

static int rk3368_thermal_notify(struct notifier_block *nb,
				   unsigned long event, void *data)
{
	struct rk3368_thermal_data *ctx = rk3368_thermal_get_data();
	struct platform_device *pdev;

	if (!ctx)
		return NOTIFY_OK;

	pdev = ctx->pdev;

	if (event & REGULATOR_EVENT_PRE_VOLTAGE_CHANGE) {
		mutex_lock(&thermal_reg_mutex);
	} else if (event & (REGULATOR_EVENT_VOLTAGE_CHANGE |
			    REGULATOR_EVENT_ABORT_VOLTAGE_CHANGE)) {
		ctx->regulator_uv = (unsigned long)data;
		if (mutex_is_locked(&thermal_reg_mutex))
			mutex_unlock(&thermal_reg_mutex);
	} else {
		return NOTIFY_OK;
	}
	return NOTIFY_OK;
}

/*
 * This function gets called when a part of the kernel has a new latency
 * requirement. We record this requirement to instruct us to get temperature.
 */
static int tsadc_latency_notify(struct notifier_block *b,
				unsigned long l, void *v)
{
	struct rk3368_thermal_data *ctx = rk3368_thermal_get_data();

	if (!ctx)
		return NOTIFY_OK;

	mutex_lock(&thermal_lat_mutex);
	ctx->latency_req = (int)l;
	mutex_unlock(&thermal_lat_mutex);

	return NOTIFY_OK;
}

static struct notifier_block tsadc_latency_notifier = {
	.notifier_call = tsadc_latency_notify,
};

static inline int tsadc_add_latency_notifier(struct notifier_block *n)
{
	return pm_qos_add_notifier(PM_QOS_CPU_DMA_LATENCY, n);
}

static inline int tsadc_remove_latency_notifier(struct notifier_block *n)
{
	return pm_qos_remove_notifier(PM_QOS_CPU_DMA_LATENCY, n);
}

static const struct of_device_id of_rk3368_thermal_match[] = {
	{
	 .compatible = "rockchip,rk3368-tsadc-legacy",
	 .data = (void *)&rk3368_tsadc_data,
	 },

	{ /* end */ },
};
MODULE_DEVICE_TABLE(of, of_rk3368_thermal_match);

static int rk3368_thermal_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct rk3368_thermal_data *ctx;
	const struct of_device_id *match;
	struct resource *res;
	int irq;
	int i, j;
	int error;
	int uv;
	int ajust_code = 0;
	int latency_req = 0;

	match = of_match_node(of_rk3368_thermal_match, np);
	if (!match)
		return -ENXIO;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "no irq resource?\n");
		return -EINVAL;
	}

	ctx = devm_kzalloc(&pdev->dev, sizeof(struct rk3368_thermal_data),
			   GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->pdev = pdev;

	ctx->chip = (const struct rk3368_tsadc_chip *)match->data;
	if (!ctx->chip)
		return -EINVAL;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ctx->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(ctx->regs))
		return PTR_ERR(ctx->regs);

	ctx->reset = devm_reset_control_get(&pdev->dev, "tsadc-apb");
	if (IS_ERR(ctx->reset)) {
		error = PTR_ERR(ctx->reset);
		dev_err(&pdev->dev, "failed to get tsadc reset: %d\n", error);
		return error;
	}

	ctx->clk = devm_clk_get(&pdev->dev, "tsadc");
	if (IS_ERR(ctx->clk)) {
		error = PTR_ERR(ctx->clk);
		dev_err(&pdev->dev, "failed to get tsadc clock: %d\n", error);
		return error;
	}

	ctx->pclk = devm_clk_get(&pdev->dev, "apb_pclk");
	if (IS_ERR(ctx->pclk)) {
		error = PTR_ERR(ctx->pclk);
		dev_err(&pdev->dev, "failed to get apb_pclk clock: %d\n",
			error);
		return error;
	}

	error = clk_prepare_enable(ctx->clk);
	if (error) {
		dev_err(&pdev->dev, "failed to enable converter clock: %d\n",
			error);
		return error;
	}

	error = clk_prepare_enable(ctx->pclk);
	if (error) {
		dev_err(&pdev->dev, "failed to enable pclk: %d\n", error);
		goto err_disable_clk;
	}

	rk3368_thermal_reset_controller(ctx->reset);

	error = rk3368_configure_from_dt(&pdev->dev, np, ctx);
	if (error) {
		dev_err(&pdev->dev, "failed to parse device tree data: %d\n",
			error);
		goto err_disable_pclk;
	}

	thermal_ctx = ctx;
	ctx->ref_regulator = devm_regulator_get_optional(&pdev->dev, "tsadc");

	if (IS_ERR(ctx->ref_regulator)) {
		error = PTR_ERR(ctx->ref_regulator);

		if (error != -EPROBE_DEFER)
			dev_err(&pdev->dev,
				"couldn't get regulator tsadc-supply\n");
		goto err_disable_pclk;
	}

	ctx->tsadc_nb.notifier_call = rk3368_thermal_notify;

	/* register regulator notifier */
	error =
	    regulator_register_notifier(ctx->ref_regulator, &ctx->tsadc_nb);
	if (error) {
		dev_err(&pdev->dev, "regulator notifier request failed\n");
		goto err_disable_pclk;
	}

	uv = regulator_get_voltage(ctx->ref_regulator);
	if (uv <= 0) {
		dev_WARN(&pdev->dev, "regulator get failed\n");
		uv = 1000000;
	}

	mutex_lock(&thermal_reg_mutex);
	if (!ctx->regulator_uv)
		ctx->regulator_uv = uv;
	mutex_unlock(&thermal_reg_mutex);

	error = tsadc_add_latency_notifier(&tsadc_latency_notifier);
	if (error) {
		dev_err(&pdev->dev, "latency notifier request failed\n");
		goto err_unreg_notifier;
	}

	latency_req = pm_qos_request(PM_QOS_CPU_DMA_LATENCY);

	mutex_lock(&thermal_lat_mutex);
	if (!ctx->latency_req)
		ctx->latency_req = latency_req;
	mutex_unlock(&thermal_lat_mutex);

	rk3368_get_ajust_code(np, &ajust_code);

	ctx->cpu_temp_adjust = (int)ajust_code;

	for (i = 0; i < ctx->chip->chn_num; i++) {
		error = rk3368_thermal_register_sensor(pdev, ctx,
							 &ctx->sensors[i],
							 ctx->chip->chn_id[i]);
		if (error) {
			dev_err(&pdev->dev,
				"failed to register thermal sensor %d : error= %d\n",
				i, error);
			for (j = 0; j < i; j++)
				thermal_zone_of_sensor_unregister(&pdev->dev,
								  ctx->sensors[j].tzd);
			goto err_remove_latancy_notifier;
		}
	}

	ctx->rk3368_thermal_kobj =
	    kobject_create_and_add("rk3368_thermal", NULL);
	if (!ctx->rk3368_thermal_kobj) {
		error = -ENOMEM;
		dev_err(&pdev->dev,
			"failed to creat debug node : error= %d\n", error);
		goto err_remove_latancy_notifier;
	}

	for (i = 0; i < ARRAY_SIZE(rk3368_thermal_attrs); i++) {
		error =
		    sysfs_create_file(ctx->rk3368_thermal_kobj,
				      &rk3368_thermal_attrs[i].attr);
		if (error) {
			dev_err(&pdev->dev,
				"failed to register thermal sensor %d : error= %d\n",
				i, error);
			for (j = 0; j < i; j++)
				sysfs_remove_file(ctx->rk3368_thermal_kobj,
						  &rk3368_thermal_attrs[j].attr);

			goto err_remove_latancy_notifier;
		}
	}

	platform_set_drvdata(pdev, ctx);

	atomic_notifier_chain_register(&panic_notifier_list,
				       &rk3368_thermal_panic_block);

	ctx->cpu_temp = INVALID_TEMP;

	pr_info("rk3368 tsadc probed successfully\n");

	return 0;

err_remove_latancy_notifier:
	tsadc_remove_latency_notifier(&tsadc_latency_notifier);
err_unreg_notifier:
	regulator_unregister_notifier(ctx->ref_regulator, &ctx->tsadc_nb);

err_disable_pclk:
	clk_disable_unprepare(ctx->pclk);
err_disable_clk:
	clk_disable_unprepare(ctx->clk);

	return error;
}

static int rk3368_thermal_remove(struct platform_device *pdev)
{
	struct rk3368_thermal_data *ctx = platform_get_drvdata(pdev);
	int i;

	atomic_notifier_chain_unregister(&panic_notifier_list,
					 &rk3368_thermal_panic_block);
	for (i = 0; i < ctx->chip->chn_num; i++) {
		struct rk3368_thermal_sensor *sensor = &ctx->sensors[i];

		thermal_zone_of_sensor_unregister(&pdev->dev, sensor->tzd);
	}
	tsadc_remove_latency_notifier(&tsadc_latency_notifier);
	regulator_unregister_notifier(ctx->ref_regulator, &ctx->tsadc_nb);
	clk_disable_unprepare(ctx->pclk);
	clk_disable_unprepare(ctx->clk);

	return 0;
}

static int __maybe_unused rk3368_thermal_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct rk3368_thermal_data *ctx = platform_get_drvdata(pdev);

	clk_disable(ctx->pclk);
	clk_disable(ctx->clk);
	return 0;
}

static int __maybe_unused rk3368_thermal_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct rk3368_thermal_data *ctx = platform_get_drvdata(pdev);
	int error;

	error = clk_enable(ctx->clk);
	if (error)
		return error;

	error = clk_enable(ctx->pclk);
	if (error) {
		clk_disable(ctx->clk);
		return error;
	}

	rk3368_thermal_reset_controller(ctx->reset);

	return 0;
}

static SIMPLE_DEV_PM_OPS(rk3368_thermal_pm_ops,
			 rk3368_thermal_suspend, rk3368_thermal_resume);

static struct platform_driver rk3368_thermal_driver = {
	.driver = {
		.name = "rk3368-thermal",
		.pm = &rk3368_thermal_pm_ops,
		.of_match_table = of_rk3368_thermal_match,
	},
	.probe = rk3368_thermal_probe,
	.remove = rk3368_thermal_remove,
};

/* rk3368 thermal needs a clock source of 32k from rk818, so this init process
 * is postponed
 */
static int __init rk3368_thermal_init_driver(void)
{
	return platform_driver_register(&rk3368_thermal_driver);
}
late_initcall(rk3368_thermal_init_driver);

MODULE_DESCRIPTION("ROCKCHIP THERMAL Driver");
MODULE_AUTHOR("Rockchip, Inc.");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:rk3368-thermal");
