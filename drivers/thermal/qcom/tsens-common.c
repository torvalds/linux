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

int get_temp_tsens_valid(struct tsens_sensor *s, int *temp)
{
	struct tsens_priv *priv = s->priv;
	int hw_id = s->hw_id;
	u32 temp_idx = LAST_TEMP_0 + hw_id;
	u32 valid_idx = VALID_0 + hw_id;
	u32 last_temp = 0, valid, mask;
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
	ret = regmap_field_read(priv->rf[temp_idx], &last_temp);
	if (ret)
		return ret;

	if (priv->feat->adc) {
		/* Convert temperature from ADC code to milliCelsius */
		*temp = code_to_degc(last_temp, s) * 1000;
	} else {
		mask = GENMASK(priv->fields[LAST_TEMP_0].msb,
			       priv->fields[LAST_TEMP_0].lsb);
		/* Convert temperature from deciCelsius to milliCelsius */
		*temp = sign_extend32(last_temp, fls(mask) - 1) * 100;
	}

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

	if (tsens_ver(priv) > VER_0_1) {
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

	if (tsens_ver(priv) > VER_0_1) {
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
	/* now alloc regmap_fields in tm_map */
	for (i = 0, j = LAST_TEMP_0; i < priv->feat->max_sensors; i++, j++) {
		priv->rf[j] = devm_regmap_field_alloc(dev, priv->tm_map,
						      priv->fields[j]);
		if (IS_ERR(priv->rf[j])) {
			ret = PTR_ERR(priv->rf[j]);
			goto err_put_device;
		}
	}
	for (i = 0, j = VALID_0; i < priv->feat->max_sensors; i++, j++) {
		priv->rf[j] = devm_regmap_field_alloc(dev, priv->tm_map,
						      priv->fields[j]);
		if (IS_ERR(priv->rf[j])) {
			ret = PTR_ERR(priv->rf[j]);
			goto err_put_device;
		}
	}

	tsens_debug_init(op);

	return 0;

err_put_device:
	put_device(&op->dev);
	return ret;
}
