/*
 * Copyright (c) 2014-2016, Fuzhou Rockchip Electronics Co., Ltd
 * Caesar Wang <wxt@rock-chips.com>
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
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/thermal.h>
#include <linux/mfd/syscon.h>
#include <linux/pinctrl/consumer.h>

/**
 * If the temperature over a period of time High,
 * the resulting TSHUT gave CRU module,let it reset the entire chip,
 * or via GPIO give PMIC.
 */
enum tshut_mode {
	TSHUT_MODE_CRU = 0,
	TSHUT_MODE_GPIO,
};

/**
 * The system Temperature Sensors tshut(tshut) polarity
 * the bit 8 is tshut polarity.
 * 0: low active, 1: high active
 */
enum tshut_polarity {
	TSHUT_LOW_ACTIVE = 0,
	TSHUT_HIGH_ACTIVE,
};

/**
 * The system has two Temperature Sensors.
 * sensor0 is for CPU, and sensor1 is for GPU.
 */
enum sensor_id {
	SENSOR_CPU = 0,
	SENSOR_GPU,
};

/**
 * The conversion table has the adc value and temperature.
 * ADC_DECREMENT: the adc value is of diminishing.(e.g. rk3288_code_table)
 * ADC_INCREMENT: the adc value is incremental.(e.g. rk3368_code_table)
 */
enum adc_sort_mode {
	ADC_DECREMENT = 0,
	ADC_INCREMENT,
};

/**
 * The max sensors is two in rockchip SoCs.
 * Two sensors: CPU and GPU sensor.
 */
#define SOC_MAX_SENSORS	2

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
 * struct rockchip_tsadc_chip - hold the private data of tsadc chip
 * @chn_id[SOC_MAX_SENSORS]: the sensor id of chip correspond to the channel
 * @chn_num: the channel number of tsadc chip
 * @tshut_temp: the hardware-controlled shutdown temperature value
 * @tshut_mode: the hardware-controlled shutdown mode (0:CRU 1:GPIO)
 * @tshut_polarity: the hardware-controlled active polarity (0:LOW 1:HIGH)
 * @initialize: SoC special initialize tsadc controller method
 * @irq_ack: clear the interrupt
 * @get_temp: get the temperature
 * @set_alarm_temp: set the high temperature interrupt
 * @set_tshut_temp: set the hardware-controlled shutdown temperature
 * @set_tshut_mode: set the hardware-controlled shutdown mode
 * @table: the chip-specific conversion table
 */
struct rockchip_tsadc_chip {
	/* The sensor id of chip correspond to the ADC channel */
	int chn_id[SOC_MAX_SENSORS];
	int chn_num;

	/* The hardware-controlled tshut property */
	int tshut_temp;
	enum tshut_mode tshut_mode;
	enum tshut_polarity tshut_polarity;

	/* Chip-wide methods */
	void (*initialize)(struct regmap *grf,
			   void __iomem *reg, enum tshut_polarity p);
	void (*irq_ack)(void __iomem *reg);
	void (*control)(void __iomem *reg, bool on);

	/* Per-sensor methods */
	int (*get_temp)(struct chip_tsadc_table table,
			int chn, void __iomem *reg, int *temp);
	void (*set_alarm_temp)(struct chip_tsadc_table table,
			       int chn, void __iomem *reg, int temp);
	void (*set_tshut_temp)(struct chip_tsadc_table table,
			       int chn, void __iomem *reg, int temp);
	void (*set_tshut_mode)(int chn, void __iomem *reg, enum tshut_mode m);

	/* Per-table methods */
	struct chip_tsadc_table table;
};

/**
 * struct rockchip_thermal_sensor - hold the information of thermal sensor
 * @thermal:  pointer to the platform/configuration data
 * @tzd: pointer to a thermal zone
 * @id: identifier of the thermal sensor
 */
struct rockchip_thermal_sensor {
	struct rockchip_thermal_data *thermal;
	struct thermal_zone_device *tzd;
	int id;
};

/**
 * struct rockchip_thermal_data - hold the private data of thermal driver
 * @chip: pointer to the platform/configuration data
 * @pdev: platform device of thermal
 * @reset: the reset controller of tsadc
 * @sensors[SOC_MAX_SENSORS]: the thermal sensor
 * @clk: the controller clock is divided by the exteral 24MHz
 * @pclk: the advanced peripherals bus clock
 * @grf: the general register file will be used to do static set by software
 * @regs: the base address of tsadc controller
 * @tshut_temp: the hardware-controlled shutdown temperature value
 * @tshut_mode: the hardware-controlled shutdown mode (0:CRU 1:GPIO)
 * @tshut_polarity: the hardware-controlled active polarity (0:LOW 1:HIGH)
 */
struct rockchip_thermal_data {
	const struct rockchip_tsadc_chip *chip;
	struct platform_device *pdev;
	struct reset_control *reset;

	struct rockchip_thermal_sensor sensors[SOC_MAX_SENSORS];

	struct clk *clk;
	struct clk *pclk;

	struct regmap *grf;
	void __iomem *regs;

	int tshut_temp;
	enum tshut_mode tshut_mode;
	enum tshut_polarity tshut_polarity;
};

/**
 * TSADC Sensor Register description:
 *
 * TSADCV2_* are used for RK3288 SoCs, the other chips can reuse it.
 * TSADCV3_* are used for newer SoCs than RK3288. (e.g: RK3228, RK3399)
 *
 */
#define TSADCV2_USER_CON			0x00
#define TSADCV2_AUTO_CON			0x04
#define TSADCV2_INT_EN				0x08
#define TSADCV2_INT_PD				0x0c
#define TSADCV2_DATA(chn)			(0x20 + (chn) * 0x04)
#define TSADCV2_COMP_INT(chn)		        (0x30 + (chn) * 0x04)
#define TSADCV2_COMP_SHUT(chn)		        (0x40 + (chn) * 0x04)
#define TSADCV2_HIGHT_INT_DEBOUNCE		0x60
#define TSADCV2_HIGHT_TSHUT_DEBOUNCE		0x64
#define TSADCV2_AUTO_PERIOD			0x68
#define TSADCV2_AUTO_PERIOD_HT			0x6c

#define TSADCV2_AUTO_EN				BIT(0)
#define TSADCV2_AUTO_SRC_EN(chn)		BIT(4 + (chn))
#define TSADCV2_AUTO_TSHUT_POLARITY_HIGH	BIT(8)

#define TSADCV3_AUTO_Q_SEL_EN			BIT(1)

#define TSADCV2_INT_SRC_EN(chn)			BIT(chn)
#define TSADCV2_SHUT_2GPIO_SRC_EN(chn)		BIT(4 + (chn))
#define TSADCV2_SHUT_2CRU_SRC_EN(chn)		BIT(8 + (chn))

#define TSADCV2_INT_PD_CLEAR_MASK		~BIT(8)
#define TSADCV3_INT_PD_CLEAR_MASK		~BIT(16)

#define TSADCV2_DATA_MASK			0xfff
#define TSADCV3_DATA_MASK			0x3ff

#define TSADCV2_HIGHT_INT_DEBOUNCE_COUNT	4
#define TSADCV2_HIGHT_TSHUT_DEBOUNCE_COUNT	4
#define TSADCV2_AUTO_PERIOD_TIME		250 /* 250ms */
#define TSADCV2_AUTO_PERIOD_HT_TIME		50  /* 50ms */
#define TSADCV3_AUTO_PERIOD_TIME		187500 /* 250ms */
#define TSADCV3_AUTO_PERIOD_HT_TIME		37500  /* 50ms */

#define TSADCV2_USER_INTER_PD_SOC		0x340 /* 13 clocks */

#define GRF_SARADC_TESTBIT			0x0e644
#define GRF_TSADC_TESTBIT_L			0x0e648
#define GRF_TSADC_TESTBIT_H			0x0e64c

#define GRF_SARADC_TESTBIT_ON			(0x10001 << 2)
#define GRF_TSADC_TESTBIT_H_ON			(0x10001 << 2)
#define GRF_TSADC_VCM_EN_L			(0x10001 << 7)
#define GRF_TSADC_VCM_EN_H			(0x10001 << 7)

/**
 * struct tsadc_table - code to temperature conversion table
 * @code: the value of adc channel
 * @temp: the temperature
 * Note:
 * code to temperature mapping of the temperature sensor is a piece wise linear
 * curve.Any temperature, code faling between to 2 give temperatures can be
 * linearly interpolated.
 * Code to Temperature mapping should be updated based on manufacturer results.
 */
struct tsadc_table {
	u32 code;
	int temp;
};

static const struct tsadc_table rk3228_code_table[] = {
	{0, -40000},
	{588, -40000},
	{593, -35000},
	{598, -30000},
	{603, -25000},
	{608, -20000},
	{613, -15000},
	{618, -10000},
	{623, -5000},
	{629, 0},
	{634, 5000},
	{639, 10000},
	{644, 15000},
	{649, 20000},
	{654, 25000},
	{660, 30000},
	{665, 35000},
	{670, 40000},
	{675, 45000},
	{681, 50000},
	{686, 55000},
	{691, 60000},
	{696, 65000},
	{702, 70000},
	{707, 75000},
	{712, 80000},
	{717, 85000},
	{723, 90000},
	{728, 95000},
	{733, 100000},
	{738, 105000},
	{744, 110000},
	{749, 115000},
	{754, 120000},
	{760, 125000},
	{TSADCV2_DATA_MASK, 125000},
};

static const struct tsadc_table rk3288_code_table[] = {
	{TSADCV2_DATA_MASK, -40000},
	{3800, -40000},
	{3792, -35000},
	{3783, -30000},
	{3774, -25000},
	{3765, -20000},
	{3756, -15000},
	{3747, -10000},
	{3737, -5000},
	{3728, 0},
	{3718, 5000},
	{3708, 10000},
	{3698, 15000},
	{3688, 20000},
	{3678, 25000},
	{3667, 30000},
	{3656, 35000},
	{3645, 40000},
	{3634, 45000},
	{3623, 50000},
	{3611, 55000},
	{3600, 60000},
	{3588, 65000},
	{3575, 70000},
	{3563, 75000},
	{3550, 80000},
	{3537, 85000},
	{3524, 90000},
	{3510, 95000},
	{3496, 100000},
	{3482, 105000},
	{3467, 110000},
	{3452, 115000},
	{3437, 120000},
	{3421, 125000},
};

static const struct tsadc_table rk3368_code_table[] = {
	{0, -40000},
	{106, -40000},
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
	{171, 125000},
	{TSADCV3_DATA_MASK, 125000},
};

static const struct tsadc_table rk3399_code_table[] = {
	{0, -40000},
	{402, -40000},
	{410, -35000},
	{419, -30000},
	{427, -25000},
	{436, -20000},
	{444, -15000},
	{453, -10000},
	{461, -5000},
	{470, 0},
	{478, 5000},
	{487, 10000},
	{496, 15000},
	{504, 20000},
	{513, 25000},
	{521, 30000},
	{530, 35000},
	{538, 40000},
	{547, 45000},
	{555, 50000},
	{564, 55000},
	{573, 60000},
	{581, 65000},
	{590, 70000},
	{599, 75000},
	{607, 80000},
	{616, 85000},
	{624, 90000},
	{633, 95000},
	{642, 100000},
	{650, 105000},
	{659, 110000},
	{668, 115000},
	{677, 120000},
	{685, 125000},
	{TSADCV3_DATA_MASK, 125000},
};

static u32 rk_tsadcv2_temp_to_code(struct chip_tsadc_table table,
				   int temp)
{
	int high, low, mid;
	u32 error = 0;

	low = 0;
	high = table.length - 1;
	mid = (high + low) / 2;

	/* Return mask code data when the temp is over table range */
	if (temp < table.id[low].temp || temp > table.id[high].temp) {
		error = table.data_mask;
		goto exit;
	}

	while (low <= high) {
		if (temp == table.id[mid].temp)
			return table.id[mid].code;
		else if (temp < table.id[mid].temp)
			high = mid - 1;
		else
			low = mid + 1;
		mid = (low + high) / 2;
	}

exit:
	pr_err("Invalid the conversion, error=%d\n", error);
	return error;
}

static int rk_tsadcv2_code_to_temp(struct chip_tsadc_table table, u32 code,
				   int *temp)
{
	unsigned int low = 1;
	unsigned int high = table.length - 1;
	unsigned int mid = (low + high) / 2;
	unsigned int num;
	unsigned long denom;

	WARN_ON(table.length < 2);

	switch (table.mode) {
	case ADC_DECREMENT:
		code &= table.data_mask;
		if (code < table.id[high].code)
			return -EAGAIN;		/* Incorrect reading */

		while (low <= high) {
			if (code >= table.id[mid].code &&
			    code < table.id[mid - 1].code)
				break;
			else if (code < table.id[mid].code)
				low = mid + 1;
			else
				high = mid - 1;

			mid = (low + high) / 2;
		}
		break;
	case ADC_INCREMENT:
		code &= table.data_mask;
		if (code < table.id[low].code)
			return -EAGAIN;		/* Incorrect reading */

		while (low <= high) {
			if (code <= table.id[mid].code &&
			    code > table.id[mid - 1].code)
				break;
			else if (code > table.id[mid].code)
				low = mid + 1;
			else
				high = mid - 1;

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
	num = table.id[mid].temp - table.id[mid - 1].temp;
	num *= abs(table.id[mid - 1].code - code);
	denom = abs(table.id[mid - 1].code - table.id[mid].code);
	*temp = table.id[mid - 1].temp + (num / denom);

	return 0;
}

/**
 * rk_tsadcv2_initialize - initialize TASDC Controller.
 *
 * (1) Set TSADC_V2_AUTO_PERIOD:
 *     Configure the interleave between every two accessing of
 *     TSADC in normal operation.
 *
 * (2) Set TSADCV2_AUTO_PERIOD_HT:
 *     Configure the interleave between every two accessing of
 *     TSADC after the temperature is higher than COM_SHUT or COM_INT.
 *
 * (3) Set TSADCV2_HIGH_INT_DEBOUNCE and TSADC_HIGHT_TSHUT_DEBOUNCE:
 *     If the temperature is higher than COMP_INT or COMP_SHUT for
 *     "debounce" times, TSADC controller will generate interrupt or TSHUT.
 */
static void rk_tsadcv2_initialize(struct regmap *grf, void __iomem *regs,
				  enum tshut_polarity tshut_polarity)
{
	if (tshut_polarity == TSHUT_HIGH_ACTIVE)
		writel_relaxed(0U | TSADCV2_AUTO_TSHUT_POLARITY_HIGH,
			       regs + TSADCV2_AUTO_CON);
	else
		writel_relaxed(0U & ~TSADCV2_AUTO_TSHUT_POLARITY_HIGH,
			       regs + TSADCV2_AUTO_CON);

	writel_relaxed(TSADCV2_AUTO_PERIOD_TIME, regs + TSADCV2_AUTO_PERIOD);
	writel_relaxed(TSADCV2_HIGHT_INT_DEBOUNCE_COUNT,
		       regs + TSADCV2_HIGHT_INT_DEBOUNCE);
	writel_relaxed(TSADCV2_AUTO_PERIOD_HT_TIME,
		       regs + TSADCV2_AUTO_PERIOD_HT);
	writel_relaxed(TSADCV2_HIGHT_TSHUT_DEBOUNCE_COUNT,
		       regs + TSADCV2_HIGHT_TSHUT_DEBOUNCE);

	if (IS_ERR(grf)) {
		pr_warn("%s: Missing rockchip,grf property\n", __func__);
		return;
	}
}

/**
 * rk_tsadcv3_initialize - initialize TASDC Controller.
 *
 * (1) The tsadc control power sequence.
 *
 * (2) Set TSADC_V2_AUTO_PERIOD:
 *     Configure the interleave between every two accessing of
 *     TSADC in normal operation.
 *
 * (2) Set TSADCV2_AUTO_PERIOD_HT:
 *     Configure the interleave between every two accessing of
 *     TSADC after the temperature is higher than COM_SHUT or COM_INT.
 *
 * (3) Set TSADCV2_HIGH_INT_DEBOUNCE and TSADC_HIGHT_TSHUT_DEBOUNCE:
 *     If the temperature is higher than COMP_INT or COMP_SHUT for
 *     "debounce" times, TSADC controller will generate interrupt or TSHUT.
 */
static void rk_tsadcv3_initialize(struct regmap *grf, void __iomem *regs,
				  enum tshut_polarity tshut_polarity)
{
	/* The tsadc control power sequence */
	if (IS_ERR(grf)) {
		/* Set interleave value to workround ic time sync issue */
		writel_relaxed(TSADCV2_USER_INTER_PD_SOC, regs +
			       TSADCV2_USER_CON);

		writel_relaxed(TSADCV2_AUTO_PERIOD_TIME,
			       regs + TSADCV2_AUTO_PERIOD);
		writel_relaxed(TSADCV2_HIGHT_INT_DEBOUNCE_COUNT,
			       regs + TSADCV2_HIGHT_INT_DEBOUNCE);
		writel_relaxed(TSADCV2_AUTO_PERIOD_HT_TIME,
			       regs + TSADCV2_AUTO_PERIOD_HT);
		writel_relaxed(TSADCV2_HIGHT_TSHUT_DEBOUNCE_COUNT,
			       regs + TSADCV2_HIGHT_TSHUT_DEBOUNCE);

	} else {
		/* Enable the voltage common mode feature */
		regmap_write(grf, GRF_TSADC_TESTBIT_L, GRF_TSADC_VCM_EN_L);
		regmap_write(grf, GRF_TSADC_TESTBIT_H, GRF_TSADC_VCM_EN_H);

		usleep_range(15, 100); /* The spec note says at least 15 us */
		regmap_write(grf, GRF_SARADC_TESTBIT, GRF_SARADC_TESTBIT_ON);
		regmap_write(grf, GRF_TSADC_TESTBIT_H, GRF_TSADC_TESTBIT_H_ON);
		usleep_range(90, 200); /* The spec note says at least 90 us */

		writel_relaxed(TSADCV3_AUTO_PERIOD_TIME,
			       regs + TSADCV2_AUTO_PERIOD);
		writel_relaxed(TSADCV2_HIGHT_INT_DEBOUNCE_COUNT,
			       regs + TSADCV2_HIGHT_INT_DEBOUNCE);
		writel_relaxed(TSADCV3_AUTO_PERIOD_HT_TIME,
			       regs + TSADCV2_AUTO_PERIOD_HT);
		writel_relaxed(TSADCV2_HIGHT_TSHUT_DEBOUNCE_COUNT,
			       regs + TSADCV2_HIGHT_TSHUT_DEBOUNCE);
	}

	if (tshut_polarity == TSHUT_HIGH_ACTIVE)
		writel_relaxed(0U | TSADCV2_AUTO_TSHUT_POLARITY_HIGH,
			       regs + TSADCV2_AUTO_CON);
	else
		writel_relaxed(0U & ~TSADCV2_AUTO_TSHUT_POLARITY_HIGH,
			       regs + TSADCV2_AUTO_CON);
}

static void rk_tsadcv2_irq_ack(void __iomem *regs)
{
	u32 val;

	val = readl_relaxed(regs + TSADCV2_INT_PD);
	writel_relaxed(val & TSADCV2_INT_PD_CLEAR_MASK, regs + TSADCV2_INT_PD);
}

static void rk_tsadcv3_irq_ack(void __iomem *regs)
{
	u32 val;

	val = readl_relaxed(regs + TSADCV2_INT_PD);
	writel_relaxed(val & TSADCV3_INT_PD_CLEAR_MASK, regs + TSADCV2_INT_PD);
}

static void rk_tsadcv2_control(void __iomem *regs, bool enable)
{
	u32 val;

	val = readl_relaxed(regs + TSADCV2_AUTO_CON);
	if (enable)
		val |= TSADCV2_AUTO_EN;
	else
		val &= ~TSADCV2_AUTO_EN;

	writel_relaxed(val, regs + TSADCV2_AUTO_CON);
}

/**
 * rk_tsadcv3_control - the tsadc controller is enabled or disabled.
 *
 * NOTE: TSADC controller works at auto mode, and some SoCs need set the
 * tsadc_q_sel bit on TSADCV2_AUTO_CON[1]. The (1024 - tsadc_q) as output
 * adc value if setting this bit to enable.
 */
static void rk_tsadcv3_control(void __iomem *regs, bool enable)
{
	u32 val;

	val = readl_relaxed(regs + TSADCV2_AUTO_CON);
	if (enable)
		val |= TSADCV2_AUTO_EN | TSADCV3_AUTO_Q_SEL_EN;
	else
		val &= ~TSADCV2_AUTO_EN;

	writel_relaxed(val, regs + TSADCV2_AUTO_CON);
}

static int rk_tsadcv2_get_temp(struct chip_tsadc_table table,
			       int chn, void __iomem *regs, int *temp)
{
	u32 val;

	val = readl_relaxed(regs + TSADCV2_DATA(chn));

	return rk_tsadcv2_code_to_temp(table, val, temp);
}

static void rk_tsadcv2_alarm_temp(struct chip_tsadc_table table,
				  int chn, void __iomem *regs, int temp)
{
	u32 alarm_value, int_en;

	/* Make sure the value is valid */
	alarm_value = rk_tsadcv2_temp_to_code(table, temp);
	if (alarm_value == table.data_mask)
		return;

	writel_relaxed(alarm_value & table.data_mask,
		       regs + TSADCV2_COMP_INT(chn));

	int_en = readl_relaxed(regs + TSADCV2_INT_EN);
	int_en |= TSADCV2_INT_SRC_EN(chn);
	writel_relaxed(int_en, regs + TSADCV2_INT_EN);
}

static void rk_tsadcv2_tshut_temp(struct chip_tsadc_table table,
				  int chn, void __iomem *regs, int temp)
{
	u32 tshut_value, val;

	/* Make sure the value is valid */
	tshut_value = rk_tsadcv2_temp_to_code(table, temp);
	if (tshut_value == table.data_mask)
		return;

	writel_relaxed(tshut_value, regs + TSADCV2_COMP_SHUT(chn));

	/* TSHUT will be valid */
	val = readl_relaxed(regs + TSADCV2_AUTO_CON);
	writel_relaxed(val | TSADCV2_AUTO_SRC_EN(chn), regs + TSADCV2_AUTO_CON);
}

static void rk_tsadcv2_tshut_mode(int chn, void __iomem *regs,
				  enum tshut_mode mode)
{
	u32 val;

	val = readl_relaxed(regs + TSADCV2_INT_EN);
	if (mode == TSHUT_MODE_GPIO) {
		val &= ~TSADCV2_SHUT_2CRU_SRC_EN(chn);
		val |= TSADCV2_SHUT_2GPIO_SRC_EN(chn);
	} else {
		val &= ~TSADCV2_SHUT_2GPIO_SRC_EN(chn);
		val |= TSADCV2_SHUT_2CRU_SRC_EN(chn);
	}

	writel_relaxed(val, regs + TSADCV2_INT_EN);
}

static const struct rockchip_tsadc_chip rk3228_tsadc_data = {
	.chn_id[SENSOR_CPU] = 0, /* cpu sensor is channel 0 */
	.chn_num = 1, /* one channel for tsadc */

	.tshut_mode = TSHUT_MODE_GPIO, /* default TSHUT via GPIO give PMIC */
	.tshut_polarity = TSHUT_LOW_ACTIVE, /* default TSHUT LOW ACTIVE */
	.tshut_temp = 95000,

	.initialize = rk_tsadcv2_initialize,
	.irq_ack = rk_tsadcv3_irq_ack,
	.control = rk_tsadcv3_control,
	.get_temp = rk_tsadcv2_get_temp,
	.set_alarm_temp = rk_tsadcv2_alarm_temp,
	.set_tshut_temp = rk_tsadcv2_tshut_temp,
	.set_tshut_mode = rk_tsadcv2_tshut_mode,

	.table = {
		.id = rk3228_code_table,
		.length = ARRAY_SIZE(rk3228_code_table),
		.data_mask = TSADCV3_DATA_MASK,
		.mode = ADC_INCREMENT,
	},
};

static const struct rockchip_tsadc_chip rk3288_tsadc_data = {
	.chn_id[SENSOR_CPU] = 1, /* cpu sensor is channel 1 */
	.chn_id[SENSOR_GPU] = 2, /* gpu sensor is channel 2 */
	.chn_num = 2, /* two channels for tsadc */

	.tshut_mode = TSHUT_MODE_GPIO, /* default TSHUT via GPIO give PMIC */
	.tshut_polarity = TSHUT_LOW_ACTIVE, /* default TSHUT LOW ACTIVE */
	.tshut_temp = 95000,

	.initialize = rk_tsadcv2_initialize,
	.irq_ack = rk_tsadcv2_irq_ack,
	.control = rk_tsadcv2_control,
	.get_temp = rk_tsadcv2_get_temp,
	.set_alarm_temp = rk_tsadcv2_alarm_temp,
	.set_tshut_temp = rk_tsadcv2_tshut_temp,
	.set_tshut_mode = rk_tsadcv2_tshut_mode,

	.table = {
		.id = rk3288_code_table,
		.length = ARRAY_SIZE(rk3288_code_table),
		.data_mask = TSADCV2_DATA_MASK,
		.mode = ADC_DECREMENT,
	},
};

static const struct rockchip_tsadc_chip rk3366_tsadc_data = {
	.chn_id[SENSOR_CPU] = 0, /* cpu sensor is channel 0 */
	.chn_id[SENSOR_GPU] = 1, /* gpu sensor is channel 1 */
	.chn_num = 2, /* two channels for tsadc */

	.tshut_mode = TSHUT_MODE_GPIO, /* default TSHUT via GPIO give PMIC */
	.tshut_polarity = TSHUT_LOW_ACTIVE, /* default TSHUT LOW ACTIVE */
	.tshut_temp = 95000,

	.initialize = rk_tsadcv3_initialize,
	.irq_ack = rk_tsadcv3_irq_ack,
	.control = rk_tsadcv3_control,
	.get_temp = rk_tsadcv2_get_temp,
	.set_alarm_temp = rk_tsadcv2_alarm_temp,
	.set_tshut_temp = rk_tsadcv2_tshut_temp,
	.set_tshut_mode = rk_tsadcv2_tshut_mode,

	.table = {
		.id = rk3228_code_table,
		.length = ARRAY_SIZE(rk3228_code_table),
		.data_mask = TSADCV3_DATA_MASK,
		.mode = ADC_INCREMENT,
	},
};

static const struct rockchip_tsadc_chip rk3368_tsadc_data = {
	.chn_id[SENSOR_CPU] = 0, /* cpu sensor is channel 0 */
	.chn_id[SENSOR_GPU] = 1, /* gpu sensor is channel 1 */
	.chn_num = 2, /* two channels for tsadc */

	.tshut_mode = TSHUT_MODE_GPIO, /* default TSHUT via GPIO give PMIC */
	.tshut_polarity = TSHUT_LOW_ACTIVE, /* default TSHUT LOW ACTIVE */
	.tshut_temp = 95000,

	.initialize = rk_tsadcv2_initialize,
	.irq_ack = rk_tsadcv2_irq_ack,
	.control = rk_tsadcv2_control,
	.get_temp = rk_tsadcv2_get_temp,
	.set_alarm_temp = rk_tsadcv2_alarm_temp,
	.set_tshut_temp = rk_tsadcv2_tshut_temp,
	.set_tshut_mode = rk_tsadcv2_tshut_mode,

	.table = {
		.id = rk3368_code_table,
		.length = ARRAY_SIZE(rk3368_code_table),
		.data_mask = TSADCV3_DATA_MASK,
		.mode = ADC_INCREMENT,
	},
};

static const struct rockchip_tsadc_chip rk3399_tsadc_data = {
	.chn_id[SENSOR_CPU] = 0, /* cpu sensor is channel 0 */
	.chn_id[SENSOR_GPU] = 1, /* gpu sensor is channel 1 */
	.chn_num = 2, /* two channels for tsadc */

	.tshut_mode = TSHUT_MODE_GPIO, /* default TSHUT via GPIO give PMIC */
	.tshut_polarity = TSHUT_LOW_ACTIVE, /* default TSHUT LOW ACTIVE */
	.tshut_temp = 95000,

	.initialize = rk_tsadcv3_initialize,
	.irq_ack = rk_tsadcv3_irq_ack,
	.control = rk_tsadcv3_control,
	.get_temp = rk_tsadcv2_get_temp,
	.set_alarm_temp = rk_tsadcv2_alarm_temp,
	.set_tshut_temp = rk_tsadcv2_tshut_temp,
	.set_tshut_mode = rk_tsadcv2_tshut_mode,

	.table = {
		.id = rk3399_code_table,
		.length = ARRAY_SIZE(rk3399_code_table),
		.data_mask = TSADCV3_DATA_MASK,
		.mode = ADC_INCREMENT,
	},
};

static const struct of_device_id of_rockchip_thermal_match[] = {
	{
		.compatible = "rockchip,rk3228-tsadc",
		.data = (void *)&rk3228_tsadc_data,
	},
	{
		.compatible = "rockchip,rk3288-tsadc",
		.data = (void *)&rk3288_tsadc_data,
	},
	{
		.compatible = "rockchip,rk3366-tsadc",
		.data = (void *)&rk3366_tsadc_data,
	},
	{
		.compatible = "rockchip,rk3368-tsadc",
		.data = (void *)&rk3368_tsadc_data,
	},
	{
		.compatible = "rockchip,rk3399-tsadc",
		.data = (void *)&rk3399_tsadc_data,
	},
	{ /* end */ },
};
MODULE_DEVICE_TABLE(of, of_rockchip_thermal_match);

static void
rockchip_thermal_toggle_sensor(struct rockchip_thermal_sensor *sensor, bool on)
{
	struct thermal_zone_device *tzd = sensor->tzd;

	tzd->ops->set_mode(tzd,
		on ? THERMAL_DEVICE_ENABLED : THERMAL_DEVICE_DISABLED);
}

static irqreturn_t rockchip_thermal_alarm_irq_thread(int irq, void *dev)
{
	struct rockchip_thermal_data *thermal = dev;
	int i;

	dev_dbg(&thermal->pdev->dev, "thermal alarm\n");

	thermal->chip->irq_ack(thermal->regs);

	for (i = 0; i < thermal->chip->chn_num; i++)
		thermal_zone_device_update(thermal->sensors[i].tzd);

	return IRQ_HANDLED;
}

static int rockchip_thermal_set_trips(void *_sensor, int low, int high)
{
	struct rockchip_thermal_sensor *sensor = _sensor;
	struct rockchip_thermal_data *thermal = sensor->thermal;
	const struct rockchip_tsadc_chip *tsadc = thermal->chip;

	dev_dbg(&thermal->pdev->dev, "%s: sensor %d: low: %d, high %d\n",
		__func__, sensor->id, low, high);

	tsadc->set_alarm_temp(tsadc->table,
			      sensor->id, thermal->regs, high);

	return 0;
}

static int rockchip_thermal_get_temp(void *_sensor, int *out_temp)
{
	struct rockchip_thermal_sensor *sensor = _sensor;
	struct rockchip_thermal_data *thermal = sensor->thermal;
	const struct rockchip_tsadc_chip *tsadc = sensor->thermal->chip;
	int retval;

	retval = tsadc->get_temp(tsadc->table,
				 sensor->id, thermal->regs, out_temp);
	dev_dbg(&thermal->pdev->dev, "sensor %d - temp: %d, retval: %d\n",
		sensor->id, *out_temp, retval);

	return retval;
}

static const struct thermal_zone_of_device_ops rockchip_of_thermal_ops = {
	.get_temp = rockchip_thermal_get_temp,
	.set_trips = rockchip_thermal_set_trips,
};

static int rockchip_configure_from_dt(struct device *dev,
				      struct device_node *np,
				      struct rockchip_thermal_data *thermal)
{
	u32 shut_temp, tshut_mode, tshut_polarity;

	if (of_property_read_u32(np, "rockchip,hw-tshut-temp", &shut_temp)) {
		dev_warn(dev,
			 "Missing tshut temp property, using default %d\n",
			 thermal->chip->tshut_temp);
		thermal->tshut_temp = thermal->chip->tshut_temp;
	} else {
		if (shut_temp > INT_MAX) {
			dev_err(dev, "Invalid tshut temperature specified: %d\n",
				shut_temp);
			return -ERANGE;
		}
		thermal->tshut_temp = shut_temp;
	}

	if (of_property_read_u32(np, "rockchip,hw-tshut-mode", &tshut_mode)) {
		dev_warn(dev,
			 "Missing tshut mode property, using default (%s)\n",
			 thermal->chip->tshut_mode == TSHUT_MODE_GPIO ?
				"gpio" : "cru");
		thermal->tshut_mode = thermal->chip->tshut_mode;
	} else {
		thermal->tshut_mode = tshut_mode;
	}

	if (thermal->tshut_mode > 1) {
		dev_err(dev, "Invalid tshut mode specified: %d\n",
			thermal->tshut_mode);
		return -EINVAL;
	}

	if (of_property_read_u32(np, "rockchip,hw-tshut-polarity",
				 &tshut_polarity)) {
		dev_warn(dev,
			 "Missing tshut-polarity property, using default (%s)\n",
			 thermal->chip->tshut_polarity == TSHUT_LOW_ACTIVE ?
				"low" : "high");
		thermal->tshut_polarity = thermal->chip->tshut_polarity;
	} else {
		thermal->tshut_polarity = tshut_polarity;
	}

	if (thermal->tshut_polarity > 1) {
		dev_err(dev, "Invalid tshut-polarity specified: %d\n",
			thermal->tshut_polarity);
		return -EINVAL;
	}

	/* The tsadc wont to handle the error in here since some SoCs didn't
	 * need this property.
	 */
	thermal->grf = syscon_regmap_lookup_by_phandle(np, "rockchip,grf");

	return 0;
}

static int
rockchip_thermal_register_sensor(struct platform_device *pdev,
				 struct rockchip_thermal_data *thermal,
				 struct rockchip_thermal_sensor *sensor,
				 int id)
{
	const struct rockchip_tsadc_chip *tsadc = thermal->chip;
	int error;

	tsadc->set_tshut_mode(id, thermal->regs, thermal->tshut_mode);
	tsadc->set_tshut_temp(tsadc->table, id, thermal->regs,
			      thermal->tshut_temp);

	sensor->thermal = thermal;
	sensor->id = id;
	sensor->tzd = devm_thermal_zone_of_sensor_register(&pdev->dev, id,
					sensor, &rockchip_of_thermal_ops);
	if (IS_ERR(sensor->tzd)) {
		error = PTR_ERR(sensor->tzd);
		dev_err(&pdev->dev, "failed to register sensor %d: %d\n",
			id, error);
		return error;
	}

	return 0;
}

/**
 * Reset TSADC Controller, reset all tsadc registers.
 */
static void rockchip_thermal_reset_controller(struct reset_control *reset)
{
	reset_control_assert(reset);
	usleep_range(10, 20);
	reset_control_deassert(reset);
}

static int rockchip_thermal_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct rockchip_thermal_data *thermal;
	const struct of_device_id *match;
	struct resource *res;
	int irq;
	int i;
	int error;

	match = of_match_node(of_rockchip_thermal_match, np);
	if (!match)
		return -ENXIO;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "no irq resource?\n");
		return -EINVAL;
	}

	thermal = devm_kzalloc(&pdev->dev, sizeof(struct rockchip_thermal_data),
			       GFP_KERNEL);
	if (!thermal)
		return -ENOMEM;

	thermal->pdev = pdev;

	thermal->chip = (const struct rockchip_tsadc_chip *)match->data;
	if (!thermal->chip)
		return -EINVAL;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	thermal->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(thermal->regs))
		return PTR_ERR(thermal->regs);

	thermal->reset = devm_reset_control_get(&pdev->dev, "tsadc-apb");
	if (IS_ERR(thermal->reset)) {
		error = PTR_ERR(thermal->reset);
		dev_err(&pdev->dev, "failed to get tsadc reset: %d\n", error);
		return error;
	}

	thermal->clk = devm_clk_get(&pdev->dev, "tsadc");
	if (IS_ERR(thermal->clk)) {
		error = PTR_ERR(thermal->clk);
		dev_err(&pdev->dev, "failed to get tsadc clock: %d\n", error);
		return error;
	}

	thermal->pclk = devm_clk_get(&pdev->dev, "apb_pclk");
	if (IS_ERR(thermal->pclk)) {
		error = PTR_ERR(thermal->pclk);
		dev_err(&pdev->dev, "failed to get apb_pclk clock: %d\n",
			error);
		return error;
	}

	error = clk_prepare_enable(thermal->clk);
	if (error) {
		dev_err(&pdev->dev, "failed to enable converter clock: %d\n",
			error);
		return error;
	}

	error = clk_prepare_enable(thermal->pclk);
	if (error) {
		dev_err(&pdev->dev, "failed to enable pclk: %d\n", error);
		goto err_disable_clk;
	}

	rockchip_thermal_reset_controller(thermal->reset);

	error = rockchip_configure_from_dt(&pdev->dev, np, thermal);
	if (error) {
		dev_err(&pdev->dev, "failed to parse device tree data: %d\n",
			error);
		goto err_disable_pclk;
	}

	thermal->chip->initialize(thermal->grf, thermal->regs,
				  thermal->tshut_polarity);

	for (i = 0; i < thermal->chip->chn_num; i++) {
		error = rockchip_thermal_register_sensor(pdev, thermal,
						&thermal->sensors[i],
						thermal->chip->chn_id[i]);
		if (error) {
			dev_err(&pdev->dev,
				"failed to register sensor[%d] : error = %d\n",
				i, error);
			goto err_disable_pclk;
		}
	}

	error = devm_request_threaded_irq(&pdev->dev, irq, NULL,
					  &rockchip_thermal_alarm_irq_thread,
					  IRQF_ONESHOT,
					  "rockchip_thermal", thermal);
	if (error) {
		dev_err(&pdev->dev,
			"failed to request tsadc irq: %d\n", error);
		goto err_disable_pclk;
	}

	thermal->chip->control(thermal->regs, true);

	for (i = 0; i < thermal->chip->chn_num; i++)
		rockchip_thermal_toggle_sensor(&thermal->sensors[i], true);

	platform_set_drvdata(pdev, thermal);

	return 0;

err_disable_pclk:
	clk_disable_unprepare(thermal->pclk);
err_disable_clk:
	clk_disable_unprepare(thermal->clk);

	return error;
}

static int rockchip_thermal_remove(struct platform_device *pdev)
{
	struct rockchip_thermal_data *thermal = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < thermal->chip->chn_num; i++) {
		struct rockchip_thermal_sensor *sensor = &thermal->sensors[i];

		rockchip_thermal_toggle_sensor(sensor, false);
	}

	thermal->chip->control(thermal->regs, false);

	clk_disable_unprepare(thermal->pclk);
	clk_disable_unprepare(thermal->clk);

	return 0;
}

static int __maybe_unused rockchip_thermal_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct rockchip_thermal_data *thermal = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < thermal->chip->chn_num; i++)
		rockchip_thermal_toggle_sensor(&thermal->sensors[i], false);

	thermal->chip->control(thermal->regs, false);

	clk_disable(thermal->pclk);
	clk_disable(thermal->clk);

	pinctrl_pm_select_sleep_state(dev);

	return 0;
}

static int __maybe_unused rockchip_thermal_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct rockchip_thermal_data *thermal = platform_get_drvdata(pdev);
	int i;
	int error;

	error = clk_enable(thermal->clk);
	if (error)
		return error;

	error = clk_enable(thermal->pclk);
	if (error) {
		clk_disable(thermal->clk);
		return error;
	}

	rockchip_thermal_reset_controller(thermal->reset);

	thermal->chip->initialize(thermal->grf, thermal->regs,
				  thermal->tshut_polarity);

	for (i = 0; i < thermal->chip->chn_num; i++) {
		int id = thermal->sensors[i].id;

		thermal->chip->set_tshut_mode(id, thermal->regs,
					      thermal->tshut_mode);
		thermal->chip->set_tshut_temp(thermal->chip->table,
					      id, thermal->regs,
					      thermal->tshut_temp);
	}

	thermal->chip->control(thermal->regs, true);

	for (i = 0; i < thermal->chip->chn_num; i++)
		rockchip_thermal_toggle_sensor(&thermal->sensors[i], true);

	pinctrl_pm_select_default_state(dev);

	return 0;
}

static SIMPLE_DEV_PM_OPS(rockchip_thermal_pm_ops,
			 rockchip_thermal_suspend, rockchip_thermal_resume);

static struct platform_driver rockchip_thermal_driver = {
	.driver = {
		.name = "rockchip-thermal",
		.pm = &rockchip_thermal_pm_ops,
		.of_match_table = of_rockchip_thermal_match,
	},
	.probe = rockchip_thermal_probe,
	.remove = rockchip_thermal_remove,
};

module_platform_driver(rockchip_thermal_driver);

MODULE_DESCRIPTION("ROCKCHIP THERMAL Driver");
MODULE_AUTHOR("Rockchip, Inc.");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:rockchip-thermal");
