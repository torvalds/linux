// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 */

#include <linux/debugfs.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/nvmem-consumer.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
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
		dev_dbg(priv->dev, "%s: offset:%d\n", __func__, priv->sensor[i].offset);
	}
}

static inline u32 degc_to_code(int degc, const struct tsens_sensor *s)
{
	u64 code = (degc * s->slope + s->offset) / SLOPE_FACTOR;

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
static int tsens_hw_to_mC(struct tsens_sensor *s, int field)
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
static int tsens_mC_to_hw(struct tsens_sensor *s, int temp)
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
	if (d->up_viol || d->low_viol)
		return 1;

	return 0;
}

static int tsens_read_irq_state(struct tsens_priv *priv, u32 hw_id,
				struct tsens_sensor *s, struct tsens_irq_data *d)
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
	} else {
		/* No mask register on older TSENS */
		d->up_irq_mask = 0;
		d->low_irq_mask = 0;
	}

	d->up_thresh  = tsens_hw_to_mC(s, UP_THRESH_0 + hw_id);
	d->low_thresh = tsens_hw_to_mC(s, LOW_THRESH_0 + hw_id);

	dev_dbg(priv->dev, "[%u] %s%s: status(%u|%u) | clr(%u|%u) | mask(%u|%u)\n",
		hw_id, __func__, (d->up_viol || d->low_viol) ? "(V)" : "",
		d->low_viol, d->up_viol, d->low_irq_clear, d->up_irq_clear,
		d->low_irq_mask, d->up_irq_mask);
	dev_dbg(priv->dev, "[%u] %s%s: thresh: (%d:%d)\n", hw_id, __func__,
		(d->up_viol || d->low_viol) ? "(violation)" : "",
		d->low_thresh, d->up_thresh);

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
irqreturn_t tsens_irq_thread(int irq, void *data)
{
	struct tsens_priv *priv = data;
	struct tsens_irq_data d;
	bool enable = true, disable = false;
	unsigned long flags;
	int temp, ret, i;

	for (i = 0; i < priv->num_sensors; i++) {
		bool trigger = false;
		struct tsens_sensor *s = &priv->sensor[i];
		u32 hw_id = s->hw_id;

		if (IS_ERR(priv->sensor[i].tzd))
			continue;
		if (!tsens_threshold_violated(priv, hw_id, &d))
			continue;
		ret = get_temp_tsens_valid(s, &temp);
		if (ret) {
			dev_err(priv->dev, "[%u] %s: error reading sensor\n", hw_id, __func__);
			continue;
		}

		spin_lock_irqsave(&priv->ul_lock, flags);

		tsens_read_irq_state(priv, hw_id, s, &d);

		if (d.up_viol &&
		    !masked_irq(hw_id, d.up_irq_mask, tsens_version(priv))) {
			tsens_set_interrupt(priv, hw_id, UPPER, disable);
			if (d.up_thresh > temp) {
				dev_dbg(priv->dev, "[%u] %s: re-arm upper\n",
					priv->sensor[i].hw_id, __func__);
				tsens_set_interrupt(priv, hw_id, UPPER, enable);
			} else {
				trigger = true;
				/* Keep irq masked */
			}
		} else if (d.low_viol &&
			   !masked_irq(hw_id, d.low_irq_mask, tsens_version(priv))) {
			tsens_set_interrupt(priv, hw_id, LOWER, disable);
			if (d.low_thresh < temp) {
				dev_dbg(priv->dev, "[%u] %s: re-arm low\n",
					priv->sensor[i].hw_id, __func__);
				tsens_set_interrupt(priv, hw_id, LOWER, enable);
			} else {
				trigger = true;
				/* Keep irq masked */
			}
		}

		spin_unlock_irqrestore(&priv->ul_lock, flags);

		if (trigger) {
			dev_dbg(priv->dev, "[%u] %s: TZ update trigger (%d mC)\n",
				hw_id, __func__, temp);
			thermal_zone_device_update(priv->sensor[i].tzd,
						   THERMAL_EVENT_UNSPECIFIED);
		} else {
			dev_dbg(priv->dev, "[%u] %s: no violation:  %d\n",
				hw_id, __func__, temp);
		}
	}

	return IRQ_HANDLED;
}

int tsens_set_trips(void *_sensor, int low, int high)
{
	struct tsens_sensor *s = _sensor;
	struct tsens_priv *priv = s->priv;
	struct device *dev = priv->dev;
	struct tsens_irq_data d;
	unsigned long flags;
	int high_val, low_val, cl_high, cl_low;
	u32 hw_id = s->hw_id;

	dev_dbg(dev, "[%u] %s: proposed thresholds: (%d:%d)\n",
		hw_id, __func__, low, high);

	cl_high = clamp_val(high, -40000, 120000);
	cl_low  = clamp_val(low, -40000, 120000);

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
		s->hw_id, __func__, d.low_thresh, d.up_thresh, cl_low, cl_high);

	return 0;
}

int tsens_enable_irq(struct tsens_priv *priv)
{
	int ret;
	int val = tsens_version(priv) > VER_1_X ? 7 : 1;

	ret = regmap_field_write(priv->rf[INT_EN], val);
	if (ret < 0)
		dev_err(priv->dev, "%s: failed to enable interrupts\n", __func__);

	return ret;
}

void tsens_disable_irq(struct tsens_priv *priv)
{
	regmap_field_write(priv->rf[INT_EN], 0);
}

int get_temp_tsens_valid(struct tsens_sensor *s, int *temp)
{
	struct tsens_priv *priv = s->priv;
	int hw_id = s->hw_id;
	u32 temp_idx = LAST_TEMP_0 + hw_id;
	u32 valid_idx = VALID_0 + hw_id;
	u32 valid;
	int ret;

	ret = regmap_field_read(priv->rf[valid_idx], &valid);
	if (ret)
		return ret;
	while (!valid) {
		/* Valid bit is 0 for 6 AHB clock cycles.
		 * At 19.2MHz, 1 AHB clock is ~60ns.
		 * We should enter this loop very, very rarely.
		 */
		ndelay(400);
		ret = regmap_field_read(priv->rf[valid_idx], &valid);
		if (ret)
			return ret;
	}

	/* Valid bit is set, OK to read the temperature */
	*temp = tsens_hw_to_mC(s, temp_idx);

	return 0;
}

int get_temp_common(struct tsens_sensor *s, int *temp)
{
	struct tsens_priv *priv = s->priv;
	int hw_id = s->hw_id;
	int last_temp = 0, ret;

	ret = regmap_field_read(priv->rf[LAST_TEMP_0 + hw_id], &last_temp);
	if (ret)
		return ret;

	*temp = code_to_degc(last_temp, s) * 1000;

	return 0;
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
		seq_puts(s, "0.1.0\n");
	}

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(dbg_version);
DEFINE_SHOW_ATTRIBUTE(dbg_sensors);

static void tsens_debug_init(struct platform_device *pdev)
{
	struct tsens_priv *priv = platform_get_drvdata(pdev);
	struct dentry *root, *file;

	root = debugfs_lookup("tsens", NULL);
	if (!root)
		priv->debug_root = debugfs_create_dir("tsens", NULL);
	else
		priv->debug_root = root;

	file = debugfs_lookup("version", priv->debug_root);
	if (!file)
		debugfs_create_file("version", 0444, priv->debug_root,
				    pdev, &dbg_version_fops);

	/* A directory for each instance of the TSENS IP */
	priv->debug = debugfs_create_dir(dev_name(&pdev->dev), priv->debug_root);
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
		srot_base = devm_ioremap_resource(&op->dev, res);
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

	res = platform_get_resource(op, IORESOURCE_MEM, 0);
	tm_base = devm_ioremap_resource(&op->dev, res);
	if (IS_ERR(tm_base)) {
		ret = PTR_ERR(tm_base);
		goto err_put_device;
	}

	priv->tm_map = devm_regmap_init_mmio(dev, tm_base, &tsens_config);
	if (IS_ERR(priv->tm_map)) {
		ret = PTR_ERR(priv->tm_map);
		goto err_put_device;
	}

	if (tsens_version(priv) > VER_0_1) {
		for (i = VER_MAJOR; i <= VER_STEP; i++) {
			priv->rf[i] = devm_regmap_field_alloc(dev, priv->srot_map,
							      priv->fields[i]);
			if (IS_ERR(priv->rf[i]))
				return PTR_ERR(priv->rf[i]);
		}
	}

	priv->rf[TSENS_EN] = devm_regmap_field_alloc(dev, priv->srot_map,
						     priv->fields[TSENS_EN]);
	if (IS_ERR(priv->rf[TSENS_EN])) {
		ret = PTR_ERR(priv->rf[TSENS_EN]);
		goto err_put_device;
	}
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

	/* This loop might need changes if enum regfield_ids is reordered */
	for (j = LAST_TEMP_0; j <= UP_THRESH_15; j += 16) {
		for (i = 0; i < priv->feat->max_sensors; i++) {
			int idx = j + i;

			priv->rf[idx] = devm_regmap_field_alloc(dev, priv->tm_map,
								priv->fields[idx]);
			if (IS_ERR(priv->rf[idx])) {
				ret = PTR_ERR(priv->rf[idx]);
				goto err_put_device;
			}
		}
	}

	spin_lock_init(&priv->ul_lock);
	tsens_enable_irq(priv);
	tsens_debug_init(op);

	return 0;

err_put_device:
	put_device(&op->dev);
	return ret;
}
