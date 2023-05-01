// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 * Copyright (c) 2019, 2020, Linaro Ltd.
 */

#include <linux/debugfs.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/mfd/syscon.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/thermal.h>
#include "../thermal_hwmon.h"
#include "tsens.h"

/**
 * struct tsens_irq_data - IRQ status and temperature violations
 * @up_viol:        upper threshold violated
 * @up_thresh:      upper threshold temperature value
 * @up_irq_mask:    mask register for upper threshold irqs
 * @up_irq_clear:   clear register for uppper threshold irqs
 * @low_viol:       lower threshold violated
 * @low_thresh:     lower threshold temperature value
 * @low_irq_mask:   mask register for lower threshold irqs
 * @low_irq_clear:  clear register for lower threshold irqs
 * @crit_viol:      critical threshold violated
 * @crit_thresh:    critical threshold temperature value
 * @crit_irq_mask:  mask register for critical threshold irqs
 * @crit_irq_clear: clear register for critical threshold irqs
 *
 * Structure containing data about temperature threshold settings and
 * irq status if they were violated.
 */
struct tsens_irq_data {
	u32 up_viol;
	int up_thresh;
	u32 up_irq_mask;
	u32 up_irq_clear;
	u32 low_viol;
	int low_thresh;
	u32 low_irq_mask;
	u32 low_irq_clear;
	u32 crit_viol;
	u32 crit_thresh;
	u32 crit_irq_mask;
	u32 crit_irq_clear;
};

char *qfprom_read(struct device *dev, const char *cname)
{
	struct nvmem_cell *cell;
	ssize_t data;
	char *ret;

	cell = nvmem_cell_get(dev, cname);
	if (IS_ERR(cell))
		return ERR_CAST(cell);

	ret = nvmem_cell_read(cell, &data);
	nvmem_cell_put(cell);

	return ret;
}

int tsens_read_calibration(struct tsens_priv *priv, int shift, u32 *p1, u32 *p2, bool backup)
{
	u32 mode;
	u32 base1, base2;
	char name[] = "sXX_pY_backup"; /* s10_p1_backup */
	int i, ret;

	if (priv->num_sensors > MAX_SENSORS)
		return -EINVAL;

	ret = snprintf(name, sizeof(name), "mode%s", backup ? "_backup" : "");
	if (ret < 0)
		return ret;

	ret = nvmem_cell_read_variable_le_u32(priv->dev, name, &mode);
	if (ret == -ENOENT)
		dev_warn(priv->dev, "Please migrate to separate nvmem cells for calibration data\n");
	if (ret < 0)
		return ret;

	dev_dbg(priv->dev, "calibration mode is %d\n", mode);

	ret = snprintf(name, sizeof(name), "base1%s", backup ? "_backup" : "");
	if (ret < 0)
		return ret;

	ret = nvmem_cell_read_variable_le_u32(priv->dev, name, &base1);
	if (ret < 0)
		return ret;

	ret = snprintf(name, sizeof(name), "base2%s", backup ? "_backup" : "");
	if (ret < 0)
		return ret;

	ret = nvmem_cell_read_variable_le_u32(priv->dev, name, &base2);
	if (ret < 0)
		return ret;

	for (i = 0; i < priv->num_sensors; i++) {
		ret = snprintf(name, sizeof(name), "s%d_p1%s", priv->sensor[i].hw_id,
			       backup ? "_backup" : "");
		if (ret < 0)
			return ret;

		ret = nvmem_cell_read_variable_le_u32(priv->dev, name, &p1[i]);
		if (ret)
			return ret;

		ret = snprintf(name, sizeof(name), "s%d_p2%s", priv->sensor[i].hw_id,
			       backup ? "_backup" : "");
		if (ret < 0)
			return ret;

		ret = nvmem_cell_read_variable_le_u32(priv->dev, name, &p2[i]);
		if (ret)
			return ret;
	}

	switch (mode) {
	case ONE_PT_CALIB:
		for (i = 0; i < priv->num_sensors; i++)
			p1[i] = p1[i] + (base1 << shift);
		break;
	case TWO_PT_CALIB:
		for (i = 0; i < priv->num_sensors; i++)
			p2[i] = (p2[i] + base2) << shift;
		fallthrough;
	case ONE_PT_CALIB2:
		for (i = 0; i < priv->num_sensors; i++)
			p1[i] = (p1[i] + base1) << shift;
		break;
	default:
		dev_dbg(priv->dev, "calibrationless mode\n");
		for (i = 0; i < priv->num_sensors; i++) {
			p1[i] = 500;
			p2[i] = 780;
		}
	}

	return mode;
}

int tsens_calibrate_nvmem(struct tsens_priv *priv, int shift)
{
	u32 p1[MAX_SENSORS], p2[MAX_SENSORS];
	int mode;

	mode = tsens_read_calibration(priv, shift, p1, p2, false);
	if (mode < 0)
		return mode;

	compute_intercept_slope(priv, p1, p2, mode);

	return 0;
}

int tsens_calibrate_common(struct tsens_priv *priv)
{
	return tsens_calibrate_nvmem(priv, 2);
}

static u32 tsens_read_cell(const struct tsens_single_value *cell, u8 len, u32 *data0, u32 *data1)
{
	u32 val;
	u32 *data = cell->blob ? data1 : data0;

	if (cell->shift + len <= 32) {
		val = data[cell->idx] >> cell->shift;
	} else {
		u8 part = 32 - cell->shift;

		val = data[cell->idx] >> cell->shift;
		val |= data[cell->idx + 1] << part;
	}

	return val & ((1 << len) - 1);
}

int tsens_read_calibration_legacy(struct tsens_priv *priv,
				  const struct tsens_legacy_calibration_format *format,
				  u32 *p1, u32 *p2,
				  u32 *cdata0, u32 *cdata1)
{
	u32 mode, invalid;
	u32 base1, base2;
	int i;

	mode = tsens_read_cell(&format->mode, 2, cdata0, cdata1);
	invalid = tsens_read_cell(&format->invalid, 1, cdata0, cdata1);
	if (invalid)
		mode = NO_PT_CALIB;
	dev_dbg(priv->dev, "calibration mode is %d\n", mode);

	base1 = tsens_read_cell(&format->base[0], format->base_len, cdata0, cdata1);
	base2 = tsens_read_cell(&format->base[1], format->base_len, cdata0, cdata1);

	for (i = 0; i < priv->num_sensors; i++) {
		p1[i] = tsens_read_cell(&format->sp[i][0], format->sp_len, cdata0, cdata1);
		p2[i] = tsens_read_cell(&format->sp[i][1], format->sp_len, cdata0, cdata1);
	}

	switch (mode) {
	case ONE_PT_CALIB:
		for (i = 0; i < priv->num_sensors; i++)
			p1[i] = p1[i] + (base1 << format->base_shift);
		break;
	case TWO_PT_CALIB:
		for (i = 0; i < priv->num_sensors; i++)
			p2[i] = (p2[i] + base2) << format->base_shift;
		fallthrough;
	case ONE_PT_CALIB2:
		for (i = 0; i < priv->num_sensors; i++)
			p1[i] = (p1[i] + base1) << format->base_shift;
		break;
	default:
		dev_dbg(priv->dev, "calibrationless mode\n");
		for (i = 0; i < priv->num_sensors; i++) {
			p1[i] = 500;
			p2[i] = 780;
		}
	}

	return mode;
}

/*
 * Use this function on devices where slope and offset calculations
 * depend on calibration data read from qfprom. On others the slope
 * and offset values are derived from tz->tzp->slope and tz->tzp->offset
 * resp.
 */
void compute_intercept_slope(struct tsens_priv *priv, u32 *p1,
			     u32 *p2, u32 mode)
{
	int i;
	int num, den;

	for (i = 0; i < priv->num_sensors; i++) {
		dev_dbg(priv->dev,
			"%s: sensor%d - data_point1:%#x data_point2:%#x\n",
			__func__, i, p1[i], p2[i]);

		if (!priv->sensor[i].slope)
			priv->sensor[i].slope = SLOPE_DEFAULT;
		if (mode == TWO_PT_CALIB) {
			/*
			 * slope (m) = adc_code2 - adc_code1 (y2 - y1)/
			 *	temp_120_degc - temp_30_degc (x2 - x1)
			 */
			num = p2[i] - p1[i];
			num *= SLOPE_FACTOR;
			den = CAL_DEGC_PT2 - CAL_DEGC_PT1;
			priv->sensor[i].slope = num / den;
		}

		priv->sensor[i].offset = (p1[i] * SLOPE_FACTOR) -
				(CAL_DEGC_PT1 *
				priv->sensor[i].slope);
		dev_dbg(priv->dev, "%s: offset:%d\n", __func__,
			priv->sensor[i].offset);
	}
}

static inline u32 degc_to_code(int degc, const struct tsens_sensor *s)
{
	u64 code = div_u64(((u64)degc * s->slope + s->offset), SLOPE_FACTOR);

	pr_debug("%s: raw_code: 0x%llx, degc:%d\n", __func__, code, degc);
	return clamp_val(code, THRESHOLD_MIN_ADC_CODE, THRESHOLD_MAX_ADC_CODE);
}

static inline int code_to_degc(u32 adc_code, const struct tsens_sensor *s)
{
	int degc, num, den;

	num = (adc_code * SLOPE_FACTOR) - s->offset;
	den = s->slope;

	if (num > 0)
		degc = num + (den / 2);
	else if (num < 0)
		degc = num - (den / 2);
	else
		degc = num;

	degc /= den;

	return degc;
}

/**
 * tsens_hw_to_mC - Return sign-extended temperature in mCelsius.
 * @s:     Pointer to sensor struct
 * @field: Index into regmap_field array pointing to temperature data
 *
 * This function handles temperature returned in ADC code or deciCelsius
 * depending on IP version.
 *
 * Return: Temperature in milliCelsius on success, a negative errno will
 * be returned in error cases
 */
static int tsens_hw_to_mC(const struct tsens_sensor *s, int field)
{
	struct tsens_priv *priv = s->priv;
	u32 resolution;
	u32 temp = 0;
	int ret;

	resolution = priv->fields[LAST_TEMP_0].msb -
		priv->fields[LAST_TEMP_0].lsb;

	ret = regmap_field_read(priv->rf[field], &temp);
	if (ret)
		return ret;

	/* Convert temperature from ADC code to milliCelsius */
	if (priv->feat->adc)
		return code_to_degc(temp, s) * 1000;

	/* deciCelsius -> milliCelsius along with sign extension */
	return sign_extend32(temp, resolution) * 100;
}

/**
 * tsens_mC_to_hw - Convert temperature to hardware register value
 * @s: Pointer to sensor struct
 * @temp: temperature in milliCelsius to be programmed to hardware
 *
 * This function outputs the value to be written to hardware in ADC code
 * or deciCelsius depending on IP version.
 *
 * Return: ADC code or temperature in deciCelsius.
 */
static int tsens_mC_to_hw(const struct tsens_sensor *s, int temp)
{
	struct tsens_priv *priv = s->priv;

	/* milliC to adc code */
	if (priv->feat->adc)
		return degc_to_code(temp / 1000, s);

	/* milliC to deciC */
	return temp / 100;
}

static inline enum tsens_ver tsens_version(struct tsens_priv *priv)
{
	return priv->feat->ver_major;
}

static void tsens_set_interrupt_v1(struct tsens_priv *priv, u32 hw_id,
				   enum tsens_irq_type irq_type, bool enable)
{
	u32 index = 0;

	switch (irq_type) {
	case UPPER:
		index = UP_INT_CLEAR_0 + hw_id;
		break;
	case LOWER:
		index = LOW_INT_CLEAR_0 + hw_id;
		break;
	case CRITICAL:
		/* No critical interrupts before v2 */
		return;
	}
	regmap_field_write(priv->rf[index], enable ? 0 : 1);
}

static void tsens_set_interrupt_v2(struct tsens_priv *priv, u32 hw_id,
				   enum tsens_irq_type irq_type, bool enable)
{
	u32 index_mask = 0, index_clear = 0;

	/*
	 * To enable the interrupt flag for a sensor:
	 *    - clear the mask bit
	 * To disable the interrupt flag for a sensor:
	 *    - Mask further interrupts for this sensor
	 *    - Write 1 followed by 0 to clear the interrupt
	 */
	switch (irq_type) {
	case UPPER:
		index_mask  = UP_INT_MASK_0 + hw_id;
		index_clear = UP_INT_CLEAR_0 + hw_id;
		break;
	case LOWER:
		index_mask  = LOW_INT_MASK_0 + hw_id;
		index_clear = LOW_INT_CLEAR_0 + hw_id;
		break;
	case CRITICAL:
		index_mask  = CRIT_INT_MASK_0 + hw_id;
		index_clear = CRIT_INT_CLEAR_0 + hw_id;
		break;
	}

	if (enable) {
		regmap_field_write(priv->rf[index_mask], 0);
	} else {
		regmap_field_write(priv->rf[index_mask],  1);
		regmap_field_write(priv->rf[index_clear], 1);
		regmap_field_write(priv->rf[index_clear], 0);
	}
}

/**
 * tsens_set_interrupt - Set state of an interrupt
 * @priv: Pointer to tsens controller private data
 * @hw_id: Hardware ID aka. sensor number
 * @irq_type: irq_type from enum tsens_irq_type
 * @enable: false = disable, true = enable
 *
 * Call IP-specific function to set state of an interrupt
 *
 * Return: void
 */
static void tsens_set_interrupt(struct tsens_priv *priv, u32 hw_id,
				enum tsens_irq_type irq_type, bool enable)
{
	dev_dbg(priv->dev, "[%u] %s: %s -> %s\n", hw_id, __func__,
		irq_type ? ((irq_type == 1) ? "UP" : "CRITICAL") : "LOW",
		enable ? "en" : "dis");
	if (tsens_version(priv) > VER_1_X)
		tsens_set_interrupt_v2(priv, hw_id, irq_type, enable);
	else
		tsens_set_interrupt_v1(priv, hw_id, irq_type, enable);
}

/**
 * tsens_threshold_violated - Check if a sensor temperature violated a preset threshold
 * @priv: Pointer to tsens controller private data
 * @hw_id: Hardware ID aka. sensor number
 * @d: Pointer to irq state data
 *
 * Return: 0 if threshold was not violated, 1 if it was violated and negative
 * errno in case of errors
 */
static int tsens_threshold_violated(struct tsens_priv *priv, u32 hw_id,
				    struct tsens_irq_data *d)
{
	int ret;

	ret = regmap_field_read(priv->rf[UPPER_STATUS_0 + hw_id], &d->up_viol);
	if (ret)
		return ret;
	ret = regmap_field_read(priv->rf[LOWER_STATUS_0 + hw_id], &d->low_viol);
	if (ret)
		return ret;

	if (priv->feat->crit_int) {
		ret = regmap_field_read(priv->rf[CRITICAL_STATUS_0 + hw_id],
					&d->crit_viol);
		if (ret)
			return ret;
	}

	if (d->up_viol || d->low_viol || d->crit_viol)
		return 1;

	return 0;
}

static int tsens_read_irq_state(struct tsens_priv *priv, u32 hw_id,
				const struct tsens_sensor *s,
				struct tsens_irq_data *d)
{
	int ret;

	ret = regmap_field_read(priv->rf[UP_INT_CLEAR_0 + hw_id], &d->up_irq_clear);
	if (ret)
		return ret;
	ret = regmap_field_read(priv->rf[LOW_INT_CLEAR_0 + hw_id], &d->low_irq_clear);
	if (ret)
		return ret;
	if (tsens_version(priv) > VER_1_X) {
		ret = regmap_field_read(priv->rf[UP_INT_MASK_0 + hw_id], &d->up_irq_mask);
		if (ret)
			return ret;
		ret = regmap_field_read(priv->rf[LOW_INT_MASK_0 + hw_id], &d->low_irq_mask);
		if (ret)
			return ret;
		ret = regmap_field_read(priv->rf[CRIT_INT_CLEAR_0 + hw_id],
					&d->crit_irq_clear);
		if (ret)
			return ret;
		ret = regmap_field_read(priv->rf[CRIT_INT_MASK_0 + hw_id],
					&d->crit_irq_mask);
		if (ret)
			return ret;

		d->crit_thresh = tsens_hw_to_mC(s, CRIT_THRESH_0 + hw_id);
	} else {
		/* No mask register on older TSENS */
		d->up_irq_mask = 0;
		d->low_irq_mask = 0;
		d->crit_irq_clear = 0;
		d->crit_irq_mask = 0;
		d->crit_thresh = 0;
	}

	d->up_thresh  = tsens_hw_to_mC(s, UP_THRESH_0 + hw_id);
	d->low_thresh = tsens_hw_to_mC(s, LOW_THRESH_0 + hw_id);

	dev_dbg(priv->dev, "[%u] %s%s: status(%u|%u|%u) | clr(%u|%u|%u) | mask(%u|%u|%u)\n",
		hw_id, __func__,
		(d->up_viol || d->low_viol || d->crit_viol) ? "(V)" : "",
		d->low_viol, d->up_viol, d->crit_viol,
		d->low_irq_clear, d->up_irq_clear, d->crit_irq_clear,
		d->low_irq_mask, d->up_irq_mask, d->crit_irq_mask);
	dev_dbg(priv->dev, "[%u] %s%s: thresh: (%d:%d:%d)\n", hw_id, __func__,
		(d->up_viol || d->low_viol || d->crit_viol) ? "(V)" : "",
		d->low_thresh, d->up_thresh, d->crit_thresh);

	return 0;
}

static inline u32 masked_irq(u32 hw_id, u32 mask, enum tsens_ver ver)
{
	if (ver > VER_1_X)
		return mask & (1 << hw_id);

	/* v1, v0.1 don't have a irq mask register */
	return 0;
}

/**
 * tsens_critical_irq_thread() - Threaded handler for critical interrupts
 * @irq: irq number
 * @data: tsens controller private data
 *
 * Check FSM watchdog bark status and clear if needed.
 * Check all sensors to find ones that violated their critical threshold limits.
 * Clear and then re-enable the interrupt.
 *
 * The level-triggered interrupt might deassert if the temperature returned to
 * within the threshold limits by the time the handler got scheduled. We
 * consider the irq to have been handled in that case.
 *
 * Return: IRQ_HANDLED
 */
static irqreturn_t tsens_critical_irq_thread(int irq, void *data)
{
	struct tsens_priv *priv = data;
	struct tsens_irq_data d;
	int temp, ret, i;
	u32 wdog_status, wdog_count;

	if (priv->feat->has_watchdog) {
		ret = regmap_field_read(priv->rf[WDOG_BARK_STATUS],
					&wdog_status);
		if (ret)
			return ret;

		if (wdog_status) {
			/* Clear WDOG interrupt */
			regmap_field_write(priv->rf[WDOG_BARK_CLEAR], 1);
			regmap_field_write(priv->rf[WDOG_BARK_CLEAR], 0);
			ret = regmap_field_read(priv->rf[WDOG_BARK_COUNT],
						&wdog_count);
			if (ret)
				return ret;
			if (wdog_count)
				dev_dbg(priv->dev, "%s: watchdog count: %d\n",
					__func__, wdog_count);

			/* Fall through to handle critical interrupts if any */
		}
	}

	for (i = 0; i < priv->num_sensors; i++) {
		const struct tsens_sensor *s = &priv->sensor[i];
		u32 hw_id = s->hw_id;

		if (!s->tzd)
			continue;
		if (!tsens_threshold_violated(priv, hw_id, &d))
			continue;
		ret = get_temp_tsens_valid(s, &temp);
		if (ret) {
			dev_err(priv->dev, "[%u] %s: error reading sensor\n",
				hw_id, __func__);
			continue;
		}

		tsens_read_irq_state(priv, hw_id, s, &d);
		if (d.crit_viol &&
		    !masked_irq(hw_id, d.crit_irq_mask, tsens_version(priv))) {
			/* Mask critical interrupts, unused on Linux */
			tsens_set_interrupt(priv, hw_id, CRITICAL, false);
		}
	}

	return IRQ_HANDLED;
}

/**
 * tsens_irq_thread - Threaded interrupt handler for uplow interrupts
 * @irq: irq number
 * @data: tsens controller private data
 *
 * Check all sensors to find ones that violated their threshold limits. If the
 * temperature is still outside the limits, call thermal_zone_device_update() to
 * update the thresholds, else re-enable the interrupts.
 *
 * The level-triggered interrupt might deassert if the temperature returned to
 * within the threshold limits by the time the handler got scheduled. We
 * consider the irq to have been handled in that case.
 *
 * Return: IRQ_HANDLED
 */
static irqreturn_t tsens_irq_thread(int irq, void *data)
{
	struct tsens_priv *priv = data;
	struct tsens_irq_data d;
	int i;

	for (i = 0; i < priv->num_sensors; i++) {
		const struct tsens_sensor *s = &priv->sensor[i];
		u32 hw_id = s->hw_id;

		if (!s->tzd)
			continue;
		if (!tsens_threshold_violated(priv, hw_id, &d))
			continue;

		thermal_zone_device_update(s->tzd, THERMAL_EVENT_UNSPECIFIED);

		if (tsens_version(priv) < VER_0_1) {
			/* Constraint: There is only 1 interrupt control register for all
			 * 11 temperature sensor. So monitoring more than 1 sensor based
			 * on interrupts will yield inconsistent result. To overcome this
			 * issue we will monitor only sensor 0 which is the master sensor.
			 */
			break;
		}
	}

	return IRQ_HANDLED;
}

/**
 * tsens_combined_irq_thread() - Threaded interrupt handler for combined interrupts
 * @irq: irq number
 * @data: tsens controller private data
 *
 * Handle the combined interrupt as if it were 2 separate interrupts, so call the
 * critical handler first and then the up/low one.
 *
 * Return: IRQ_HANDLED
 */
static irqreturn_t tsens_combined_irq_thread(int irq, void *data)
{
	irqreturn_t ret;

	ret = tsens_critical_irq_thread(irq, data);
	if (ret != IRQ_HANDLED)
		return ret;

	return tsens_irq_thread(irq, data);
}

static int tsens_set_trips(struct thermal_zone_device *tz, int low, int high)
{
	struct tsens_sensor *s = tz->devdata;
	struct tsens_priv *priv = s->priv;
	struct device *dev = priv->dev;
	struct tsens_irq_data d;
	unsigned long flags;
	int high_val, low_val, cl_high, cl_low;
	u32 hw_id = s->hw_id;

	if (tsens_version(priv) < VER_0_1) {
		/* Pre v0.1 IP had a single register for each type of interrupt
		 * and thresholds
		 */
		hw_id = 0;
	}

	dev_dbg(dev, "[%u] %s: proposed thresholds: (%d:%d)\n",
		hw_id, __func__, low, high);

	cl_high = clamp_val(high, priv->feat->trip_min_temp, priv->feat->trip_max_temp);
	cl_low  = clamp_val(low, priv->feat->trip_min_temp, priv->feat->trip_max_temp);

	high_val = tsens_mC_to_hw(s, cl_high);
	low_val  = tsens_mC_to_hw(s, cl_low);

	spin_lock_irqsave(&priv->ul_lock, flags);

	tsens_read_irq_state(priv, hw_id, s, &d);

	/* Write the new thresholds and clear the status */
	regmap_field_write(priv->rf[LOW_THRESH_0 + hw_id], low_val);
	regmap_field_write(priv->rf[UP_THRESH_0 + hw_id], high_val);
	tsens_set_interrupt(priv, hw_id, LOWER, true);
	tsens_set_interrupt(priv, hw_id, UPPER, true);

	spin_unlock_irqrestore(&priv->ul_lock, flags);

	dev_dbg(dev, "[%u] %s: (%d:%d)->(%d:%d)\n",
		hw_id, __func__, d.low_thresh, d.up_thresh, cl_low, cl_high);

	return 0;
}

static int tsens_enable_irq(struct tsens_priv *priv)
{
	int ret;
	int val = tsens_version(priv) > VER_1_X ? 7 : 1;

	ret = regmap_field_write(priv->rf[INT_EN], val);
	if (ret < 0)
		dev_err(priv->dev, "%s: failed to enable interrupts\n",
			__func__);

	return ret;
}

static void tsens_disable_irq(struct tsens_priv *priv)
{
	regmap_field_write(priv->rf[INT_EN], 0);
}

int get_temp_tsens_valid(const struct tsens_sensor *s, int *temp)
{
	struct tsens_priv *priv = s->priv;
	int hw_id = s->hw_id;
	u32 temp_idx = LAST_TEMP_0 + hw_id;
	u32 valid_idx = VALID_0 + hw_id;
	u32 valid;
	int ret;

	/* VER_0 doesn't have VALID bit */
	if (tsens_version(priv) == VER_0)
		goto get_temp;

	/* Valid bit is 0 for 6 AHB clock cycles.
	 * At 19.2MHz, 1 AHB clock is ~60ns.
	 * We should enter this loop very, very rarely.
	 * Wait 1 us since it's the min of poll_timeout macro.
	 * Old value was 400 ns.
	 */
	ret = regmap_field_read_poll_timeout(priv->rf[valid_idx], valid,
					     valid, 1, 20 * USEC_PER_MSEC);
	if (ret)
		return ret;

get_temp:
	/* Valid bit is set, OK to read the temperature */
	*temp = tsens_hw_to_mC(s, temp_idx);

	return 0;
}

int get_temp_common(const struct tsens_sensor *s, int *temp)
{
	struct tsens_priv *priv = s->priv;
	int hw_id = s->hw_id;
	int last_temp = 0, ret, trdy;
	unsigned long timeout;

	timeout = jiffies + usecs_to_jiffies(TIMEOUT_US);
	do {
		if (tsens_version(priv) == VER_0) {
			ret = regmap_field_read(priv->rf[TRDY], &trdy);
			if (ret)
				return ret;
			if (!trdy)
				continue;
		}

		ret = regmap_field_read(priv->rf[LAST_TEMP_0 + hw_id], &last_temp);
		if (ret)
			return ret;

		*temp = code_to_degc(last_temp, s) * 1000;

		return 0;
	} while (time_before(jiffies, timeout));

	return -ETIMEDOUT;
}

#ifdef CONFIG_DEBUG_FS
static int dbg_sensors_show(struct seq_file *s, void *data)
{
	struct platform_device *pdev = s->private;
	struct tsens_priv *priv = platform_get_drvdata(pdev);
	int i;

	seq_printf(s, "max: %2d\nnum: %2d\n\n",
		   priv->feat->max_sensors, priv->num_sensors);

	seq_puts(s, "      id    slope   offset\n--------------------------\n");
	for (i = 0;  i < priv->num_sensors; i++) {
		seq_printf(s, "%8d %8d %8d\n", priv->sensor[i].hw_id,
			   priv->sensor[i].slope, priv->sensor[i].offset);
	}

	return 0;
}

static int dbg_version_show(struct seq_file *s, void *data)
{
	struct platform_device *pdev = s->private;
	struct tsens_priv *priv = platform_get_drvdata(pdev);
	u32 maj_ver, min_ver, step_ver;
	int ret;

	if (tsens_version(priv) > VER_0_1) {
		ret = regmap_field_read(priv->rf[VER_MAJOR], &maj_ver);
		if (ret)
			return ret;
		ret = regmap_field_read(priv->rf[VER_MINOR], &min_ver);
		if (ret)
			return ret;
		ret = regmap_field_read(priv->rf[VER_STEP], &step_ver);
		if (ret)
			return ret;
		seq_printf(s, "%d.%d.%d\n", maj_ver, min_ver, step_ver);
	} else {
		seq_printf(s, "0.%d.0\n", priv->feat->ver_major);
	}

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(dbg_version);
DEFINE_SHOW_ATTRIBUTE(dbg_sensors);

static void tsens_debug_init(struct platform_device *pdev)
{
	struct tsens_priv *priv = platform_get_drvdata(pdev);

	priv->debug_root = debugfs_lookup("tsens", NULL);
	if (!priv->debug_root)
		priv->debug_root = debugfs_create_dir("tsens", NULL);

	/* A directory for each instance of the TSENS IP */
	priv->debug = debugfs_create_dir(dev_name(&pdev->dev), priv->debug_root);
	debugfs_create_file("version", 0444, priv->debug, pdev, &dbg_version_fops);
	debugfs_create_file("sensors", 0444, priv->debug, pdev, &dbg_sensors_fops);
}
#else
static inline void tsens_debug_init(struct platform_device *pdev) {}
#endif

static const struct regmap_config tsens_config = {
	.name		= "tm",
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
};

static const struct regmap_config tsens_srot_config = {
	.name		= "srot",
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
};

int __init init_common(struct tsens_priv *priv)
{
	void __iomem *tm_base, *srot_base;
	struct device *dev = priv->dev;
	u32 ver_minor;
	struct resource *res;
	u32 enabled;
	int ret, i, j;
	struct platform_device *op = of_find_device_by_node(priv->dev->of_node);

	if (!op)
		return -EINVAL;

	if (op->num_resources > 1) {
		/* DT with separate SROT and TM address space */
		priv->tm_offset = 0;
		res = platform_get_resource(op, IORESOURCE_MEM, 1);
		srot_base = devm_ioremap_resource(dev, res);
		if (IS_ERR(srot_base)) {
			ret = PTR_ERR(srot_base);
			goto err_put_device;
		}

		priv->srot_map = devm_regmap_init_mmio(dev, srot_base,
						       &tsens_srot_config);
		if (IS_ERR(priv->srot_map)) {
			ret = PTR_ERR(priv->srot_map);
			goto err_put_device;
		}
	} else {
		/* old DTs where SROT and TM were in a contiguous 2K block */
		priv->tm_offset = 0x1000;
	}

	if (tsens_version(priv) >= VER_0_1) {
		res = platform_get_resource(op, IORESOURCE_MEM, 0);
		tm_base = devm_ioremap_resource(dev, res);
		if (IS_ERR(tm_base)) {
			ret = PTR_ERR(tm_base);
			goto err_put_device;
		}

		priv->tm_map = devm_regmap_init_mmio(dev, tm_base, &tsens_config);
	} else { /* VER_0 share the same gcc regs using a syscon */
		struct device *parent = priv->dev->parent;

		if (parent)
			priv->tm_map = syscon_node_to_regmap(parent->of_node);
	}

	if (IS_ERR_OR_NULL(priv->tm_map)) {
		if (!priv->tm_map)
			ret = -ENODEV;
		else
			ret = PTR_ERR(priv->tm_map);
		goto err_put_device;
	}

	/* VER_0 have only tm_map */
	if (!priv->srot_map)
		priv->srot_map = priv->tm_map;

	if (tsens_version(priv) > VER_0_1) {
		for (i = VER_MAJOR; i <= VER_STEP; i++) {
			priv->rf[i] = devm_regmap_field_alloc(dev, priv->srot_map,
							      priv->fields[i]);
			if (IS_ERR(priv->rf[i])) {
				ret = PTR_ERR(priv->rf[i]);
				goto err_put_device;
			}
		}
		ret = regmap_field_read(priv->rf[VER_MINOR], &ver_minor);
		if (ret)
			goto err_put_device;
	}

	priv->rf[TSENS_EN] = devm_regmap_field_alloc(dev, priv->srot_map,
						     priv->fields[TSENS_EN]);
	if (IS_ERR(priv->rf[TSENS_EN])) {
		ret = PTR_ERR(priv->rf[TSENS_EN]);
		goto err_put_device;
	}
	/* in VER_0 TSENS need to be explicitly enabled */
	if (tsens_version(priv) == VER_0)
		regmap_field_write(priv->rf[TSENS_EN], 1);

	ret = regmap_field_read(priv->rf[TSENS_EN], &enabled);
	if (ret)
		goto err_put_device;
	if (!enabled) {
		dev_err(dev, "%s: device not enabled\n", __func__);
		ret = -ENODEV;
		goto err_put_device;
	}

	priv->rf[SENSOR_EN] = devm_regmap_field_alloc(dev, priv->srot_map,
						      priv->fields[SENSOR_EN]);
	if (IS_ERR(priv->rf[SENSOR_EN])) {
		ret = PTR_ERR(priv->rf[SENSOR_EN]);
		goto err_put_device;
	}
	priv->rf[INT_EN] = devm_regmap_field_alloc(dev, priv->tm_map,
						   priv->fields[INT_EN]);
	if (IS_ERR(priv->rf[INT_EN])) {
		ret = PTR_ERR(priv->rf[INT_EN]);
		goto err_put_device;
	}

	priv->rf[TSENS_SW_RST] =
		devm_regmap_field_alloc(dev, priv->srot_map, priv->fields[TSENS_SW_RST]);
	if (IS_ERR(priv->rf[TSENS_SW_RST])) {
		ret = PTR_ERR(priv->rf[TSENS_SW_RST]);
		goto err_put_device;
	}

	priv->rf[TRDY] = devm_regmap_field_alloc(dev, priv->tm_map, priv->fields[TRDY]);
	if (IS_ERR(priv->rf[TRDY])) {
		ret = PTR_ERR(priv->rf[TRDY]);
		goto err_put_device;
	}

	/* This loop might need changes if enum regfield_ids is reordered */
	for (j = LAST_TEMP_0; j <= UP_THRESH_15; j += 16) {
		for (i = 0; i < priv->feat->max_sensors; i++) {
			int idx = j + i;

			priv->rf[idx] = devm_regmap_field_alloc(dev,
								priv->tm_map,
								priv->fields[idx]);
			if (IS_ERR(priv->rf[idx])) {
				ret = PTR_ERR(priv->rf[idx]);
				goto err_put_device;
			}
		}
	}

	if (priv->feat->crit_int || tsens_version(priv) < VER_0_1) {
		/* Loop might need changes if enum regfield_ids is reordered */
		for (j = CRITICAL_STATUS_0; j <= CRIT_THRESH_15; j += 16) {
			for (i = 0; i < priv->feat->max_sensors; i++) {
				int idx = j + i;

				priv->rf[idx] =
					devm_regmap_field_alloc(dev,
								priv->tm_map,
								priv->fields[idx]);
				if (IS_ERR(priv->rf[idx])) {
					ret = PTR_ERR(priv->rf[idx]);
					goto err_put_device;
				}
			}
		}
	}

	if (tsens_version(priv) > VER_1_X &&  ver_minor > 2) {
		/* Watchdog is present only on v2.3+ */
		priv->feat->has_watchdog = 1;
		for (i = WDOG_BARK_STATUS; i <= CC_MON_MASK; i++) {
			priv->rf[i] = devm_regmap_field_alloc(dev, priv->tm_map,
							      priv->fields[i]);
			if (IS_ERR(priv->rf[i])) {
				ret = PTR_ERR(priv->rf[i]);
				goto err_put_device;
			}
		}
		/*
		 * Watchdog is already enabled, unmask the bark.
		 * Disable cycle completion monitoring
		 */
		regmap_field_write(priv->rf[WDOG_BARK_MASK], 0);
		regmap_field_write(priv->rf[CC_MON_MASK], 1);
	}

	spin_lock_init(&priv->ul_lock);

	/* VER_0 interrupt doesn't need to be enabled */
	if (tsens_version(priv) >= VER_0_1)
		tsens_enable_irq(priv);

err_put_device:
	put_device(&op->dev);
	return ret;
}

static int tsens_get_temp(struct thermal_zone_device *tz, int *temp)
{
	struct tsens_sensor *s = tz->devdata;
	struct tsens_priv *priv = s->priv;

	return priv->ops->get_temp(s, temp);
}

static int  __maybe_unused tsens_suspend(struct device *dev)
{
	struct tsens_priv *priv = dev_get_drvdata(dev);

	if (priv->ops && priv->ops->suspend)
		return priv->ops->suspend(priv);

	return 0;
}

static int __maybe_unused tsens_resume(struct device *dev)
{
	struct tsens_priv *priv = dev_get_drvdata(dev);

	if (priv->ops && priv->ops->resume)
		return priv->ops->resume(priv);

	return 0;
}

static SIMPLE_DEV_PM_OPS(tsens_pm_ops, tsens_suspend, tsens_resume);

static const struct of_device_id tsens_table[] = {
	{
		.compatible = "qcom,ipq8064-tsens",
		.data = &data_8960,
	}, {
		.compatible = "qcom,ipq8074-tsens",
		.data = &data_ipq8074,
	}, {
		.compatible = "qcom,mdm9607-tsens",
		.data = &data_9607,
	}, {
		.compatible = "qcom,msm8916-tsens",
		.data = &data_8916,
	}, {
		.compatible = "qcom,msm8939-tsens",
		.data = &data_8939,
	}, {
		.compatible = "qcom,msm8956-tsens",
		.data = &data_8956,
	}, {
		.compatible = "qcom,msm8960-tsens",
		.data = &data_8960,
	}, {
		.compatible = "qcom,msm8974-tsens",
		.data = &data_8974,
	}, {
		.compatible = "qcom,msm8976-tsens",
		.data = &data_8976,
	}, {
		.compatible = "qcom,msm8996-tsens",
		.data = &data_8996,
	}, {
		.compatible = "qcom,tsens-v1",
		.data = &data_tsens_v1,
	}, {
		.compatible = "qcom,tsens-v2",
		.data = &data_tsens_v2,
	},
	{}
};
MODULE_DEVICE_TABLE(of, tsens_table);

static const struct thermal_zone_device_ops tsens_of_ops = {
	.get_temp = tsens_get_temp,
	.set_trips = tsens_set_trips,
};

static int tsens_register_irq(struct tsens_priv *priv, char *irqname,
			      irq_handler_t thread_fn)
{
	struct platform_device *pdev;
	int ret, irq;

	pdev = of_find_device_by_node(priv->dev->of_node);
	if (!pdev)
		return -ENODEV;

	irq = platform_get_irq_byname(pdev, irqname);
	if (irq < 0) {
		ret = irq;
		/* For old DTs with no IRQ defined */
		if (irq == -ENXIO)
			ret = 0;
	} else {
		/* VER_0 interrupt is TRIGGER_RISING, VER_0_1 and up is ONESHOT */
		if (tsens_version(priv) == VER_0)
			ret = devm_request_threaded_irq(&pdev->dev, irq,
							thread_fn, NULL,
							IRQF_TRIGGER_RISING,
							dev_name(&pdev->dev),
							priv);
		else
			ret = devm_request_threaded_irq(&pdev->dev, irq, NULL,
							thread_fn, IRQF_ONESHOT,
							dev_name(&pdev->dev),
							priv);

		if (ret)
			dev_err(&pdev->dev, "%s: failed to get irq\n",
				__func__);
		else
			enable_irq_wake(irq);
	}

	put_device(&pdev->dev);
	return ret;
}

static int tsens_register(struct tsens_priv *priv)
{
	int i, ret;
	struct thermal_zone_device *tzd;

	for (i = 0;  i < priv->num_sensors; i++) {
		priv->sensor[i].priv = priv;
		tzd = devm_thermal_of_zone_register(priv->dev, priv->sensor[i].hw_id,
						    &priv->sensor[i],
						    &tsens_of_ops);
		if (IS_ERR(tzd))
			continue;
		priv->sensor[i].tzd = tzd;
		if (priv->ops->enable)
			priv->ops->enable(priv, i);

		if (devm_thermal_add_hwmon_sysfs(tzd))
			dev_warn(priv->dev,
				 "Failed to add hwmon sysfs attributes\n");
	}

	/* VER_0 require to set MIN and MAX THRESH
	 * These 2 regs are set using the:
	 * - CRIT_THRESH_0 for MAX THRESH hardcoded to 120°C
	 * - CRIT_THRESH_1 for MIN THRESH hardcoded to   0°C
	 */
	if (tsens_version(priv) < VER_0_1) {
		regmap_field_write(priv->rf[CRIT_THRESH_0],
				   tsens_mC_to_hw(priv->sensor, 120000));

		regmap_field_write(priv->rf[CRIT_THRESH_1],
				   tsens_mC_to_hw(priv->sensor, 0));
	}

	if (priv->feat->combo_int) {
		ret = tsens_register_irq(priv, "combined",
					 tsens_combined_irq_thread);
	} else {
		ret = tsens_register_irq(priv, "uplow", tsens_irq_thread);
		if (ret < 0)
			return ret;

		if (priv->feat->crit_int)
			ret = tsens_register_irq(priv, "critical",
						 tsens_critical_irq_thread);
	}

	return ret;
}

static int tsens_probe(struct platform_device *pdev)
{
	int ret, i;
	struct device *dev;
	struct device_node *np;
	struct tsens_priv *priv;
	const struct tsens_plat_data *data;
	const struct of_device_id *id;
	u32 num_sensors;

	if (pdev->dev.of_node)
		dev = &pdev->dev;
	else
		dev = pdev->dev.parent;

	np = dev->of_node;

	id = of_match_node(tsens_table, np);
	if (id)
		data = id->data;
	else
		data = &data_8960;

	num_sensors = data->num_sensors;

	if (np)
		of_property_read_u32(np, "#qcom,sensors", &num_sensors);

	if (num_sensors <= 0) {
		dev_err(dev, "%s: invalid number of sensors\n", __func__);
		return -EINVAL;
	}

	priv = devm_kzalloc(dev,
			     struct_size(priv, sensor, num_sensors),
			     GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	priv->num_sensors = num_sensors;
	priv->ops = data->ops;
	for (i = 0;  i < priv->num_sensors; i++) {
		if (data->hw_ids)
			priv->sensor[i].hw_id = data->hw_ids[i];
		else
			priv->sensor[i].hw_id = i;
	}
	priv->feat = data->feat;
	priv->fields = data->fields;

	platform_set_drvdata(pdev, priv);

	if (!priv->ops || !priv->ops->init || !priv->ops->get_temp)
		return -EINVAL;

	ret = priv->ops->init(priv);
	if (ret < 0) {
		dev_err(dev, "%s: init failed\n", __func__);
		return ret;
	}

	if (priv->ops->calibrate) {
		ret = priv->ops->calibrate(priv);
		if (ret < 0) {
			if (ret != -EPROBE_DEFER)
				dev_err(dev, "%s: calibration failed\n", __func__);
			return ret;
		}
	}

	ret = tsens_register(priv);
	if (!ret)
		tsens_debug_init(pdev);

	return ret;
}

static int tsens_remove(struct platform_device *pdev)
{
	struct tsens_priv *priv = platform_get_drvdata(pdev);

	debugfs_remove_recursive(priv->debug_root);
	tsens_disable_irq(priv);
	if (priv->ops->disable)
		priv->ops->disable(priv);

	return 0;
}

static struct platform_driver tsens_driver = {
	.probe = tsens_probe,
	.remove = tsens_remove,
	.driver = {
		.name = "qcom-tsens",
		.pm	= &tsens_pm_ops,
		.of_match_table = tsens_table,
	},
};
module_platform_driver(tsens_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("QCOM Temperature Sensor driver");
MODULE_ALIAS("platform:qcom-tsens");
