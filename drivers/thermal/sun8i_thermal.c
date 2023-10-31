// SPDX-License-Identifier: GPL-2.0
/*
 * Thermal sensor driver for Allwinner SOC
 * Copyright (C) 2019 Yangtao Li
 *
 * Based on the work of Icenowy Zheng <icenowy@aosc.io>
 * Based on the work of Ondrej Jirman <megous@megous.com>
 * Based on the work of Josef Gajdusek <atx@atx.name>
 */

#include <linux/bitmap.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/thermal.h>

#include "thermal_hwmon.h"

#define MAX_SENSOR_NUM	4

#define FT_TEMP_MASK				GENMASK(11, 0)
#define TEMP_CALIB_MASK				GENMASK(11, 0)
#define CALIBRATE_DEFAULT			0x800

#define SUN8I_THS_CTRL0				0x00
#define SUN8I_THS_CTRL2				0x40
#define SUN8I_THS_IC				0x44
#define SUN8I_THS_IS				0x48
#define SUN8I_THS_MFC				0x70
#define SUN8I_THS_TEMP_CALIB			0x74
#define SUN8I_THS_TEMP_DATA			0x80

#define SUN50I_THS_CTRL0			0x00
#define SUN50I_H6_THS_ENABLE			0x04
#define SUN50I_H6_THS_PC			0x08
#define SUN50I_H6_THS_DIC			0x10
#define SUN50I_H6_THS_DIS			0x20
#define SUN50I_H6_THS_MFC			0x30
#define SUN50I_H6_THS_TEMP_CALIB		0xa0
#define SUN50I_H6_THS_TEMP_DATA			0xc0

#define SUN8I_THS_CTRL0_T_ACQ0(x)		(GENMASK(15, 0) & (x))
#define SUN8I_THS_CTRL2_T_ACQ1(x)		((GENMASK(15, 0) & (x)) << 16)
#define SUN8I_THS_DATA_IRQ_STS(x)		BIT(x + 8)

#define SUN50I_THS_CTRL0_T_ACQ(x)		((GENMASK(15, 0) & (x)) << 16)
#define SUN50I_THS_FILTER_EN			BIT(2)
#define SUN50I_THS_FILTER_TYPE(x)		(GENMASK(1, 0) & (x))
#define SUN50I_H6_THS_PC_TEMP_PERIOD(x)		((GENMASK(19, 0) & (x)) << 12)
#define SUN50I_H6_THS_DATA_IRQ_STS(x)		BIT(x)

struct tsensor {
	struct ths_device		*tmdev;
	struct thermal_zone_device	*tzd;
	int				id;
};

struct ths_thermal_chip {
	bool            has_mod_clk;
	bool            has_bus_clk_reset;
	int		sensor_num;
	int		offset;
	int		scale;
	int		ft_deviation;
	int		temp_data_base;
	int		(*calibrate)(struct ths_device *tmdev,
				     u16 *caldata, int callen);
	int		(*init)(struct ths_device *tmdev);
	unsigned long	(*irq_ack)(struct ths_device *tmdev);
	int		(*calc_temp)(struct ths_device *tmdev,
				     int id, int reg);
};

struct ths_device {
	const struct ths_thermal_chip		*chip;
	struct device				*dev;
	struct regmap				*regmap;
	struct reset_control			*reset;
	struct clk				*bus_clk;
	struct clk                              *mod_clk;
	struct tsensor				sensor[MAX_SENSOR_NUM];
};

/* Temp Unit: millidegree Celsius */
static int sun8i_ths_calc_temp(struct ths_device *tmdev,
			       int id, int reg)
{
	return tmdev->chip->offset - (reg * tmdev->chip->scale / 10);
}

static int sun50i_h5_calc_temp(struct ths_device *tmdev,
			       int id, int reg)
{
	if (reg >= 0x500)
		return -1191 * reg / 10 + 223000;
	else if (!id)
		return -1452 * reg / 10 + 259000;
	else
		return -1590 * reg / 10 + 276000;
}

static int sun8i_ths_get_temp(struct thermal_zone_device *tz, int *temp)
{
	struct tsensor *s = thermal_zone_device_priv(tz);
	struct ths_device *tmdev = s->tmdev;
	int val = 0;

	regmap_read(tmdev->regmap, tmdev->chip->temp_data_base +
		    0x4 * s->id, &val);

	/* ths have no data yet */
	if (!val)
		return -EAGAIN;

	*temp = tmdev->chip->calc_temp(tmdev, s->id, val);
	/*
	 * According to the original sdk, there are some platforms(rarely)
	 * that add a fixed offset value after calculating the temperature
	 * value. We can't simply put it on the formula for calculating the
	 * temperature above, because the formula for calculating the
	 * temperature above is also used when the sensor is calibrated. If
	 * do this, the correct calibration formula is hard to know.
	 */
	*temp += tmdev->chip->ft_deviation;

	return 0;
}

static const struct thermal_zone_device_ops ths_ops = {
	.get_temp = sun8i_ths_get_temp,
};

static const struct regmap_config config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.fast_io = true,
	.max_register = 0xfc,
};

static unsigned long sun8i_h3_irq_ack(struct ths_device *tmdev)
{
	unsigned long irq_bitmap = 0;
	int i, state;

	regmap_read(tmdev->regmap, SUN8I_THS_IS, &state);

	for (i = 0; i < tmdev->chip->sensor_num; i++) {
		if (state & SUN8I_THS_DATA_IRQ_STS(i)) {
			regmap_write(tmdev->regmap, SUN8I_THS_IS,
				     SUN8I_THS_DATA_IRQ_STS(i));
			bitmap_set(&irq_bitmap, i, 1);
		}
	}

	return irq_bitmap;
}

static unsigned long sun50i_h6_irq_ack(struct ths_device *tmdev)
{
	unsigned long irq_bitmap = 0;
	int i, state;

	regmap_read(tmdev->regmap, SUN50I_H6_THS_DIS, &state);

	for (i = 0; i < tmdev->chip->sensor_num; i++) {
		if (state & SUN50I_H6_THS_DATA_IRQ_STS(i)) {
			regmap_write(tmdev->regmap, SUN50I_H6_THS_DIS,
				     SUN50I_H6_THS_DATA_IRQ_STS(i));
			bitmap_set(&irq_bitmap, i, 1);
		}
	}

	return irq_bitmap;
}

static irqreturn_t sun8i_irq_thread(int irq, void *data)
{
	struct ths_device *tmdev = data;
	unsigned long irq_bitmap = tmdev->chip->irq_ack(tmdev);
	int i;

	for_each_set_bit(i, &irq_bitmap, tmdev->chip->sensor_num) {
		thermal_zone_device_update(tmdev->sensor[i].tzd,
					   THERMAL_EVENT_UNSPECIFIED);
	}

	return IRQ_HANDLED;
}

static int sun8i_h3_ths_calibrate(struct ths_device *tmdev,
				  u16 *caldata, int callen)
{
	int i;

	if (!caldata[0] || callen < 2 * tmdev->chip->sensor_num)
		return -EINVAL;

	for (i = 0; i < tmdev->chip->sensor_num; i++) {
		int offset = (i % 2) << 4;

		regmap_update_bits(tmdev->regmap,
				   SUN8I_THS_TEMP_CALIB + (4 * (i >> 1)),
				   TEMP_CALIB_MASK << offset,
				   caldata[i] << offset);
	}

	return 0;
}

static int sun50i_h6_ths_calibrate(struct ths_device *tmdev,
				   u16 *caldata, int callen)
{
	struct device *dev = tmdev->dev;
	int i, ft_temp;

	if (!caldata[0] || callen < 2 + 2 * tmdev->chip->sensor_num)
		return -EINVAL;

	/*
	 * efuse layout:
	 *
	 *	0   11  16	 32
	 *	+-------+-------+-------+
	 *	|temp|  |sensor0|sensor1|
	 *	+-------+-------+-------+
	 *
	 * The calibration data on the H6 is the ambient temperature and
	 * sensor values that are filled during the factory test stage.
	 *
	 * The unit of stored FT temperature is 0.1 degree celsius.
	 *
	 * We need to calculate a delta between measured and caluclated
	 * register values and this will become a calibration offset.
	 */
	ft_temp = (caldata[0] & FT_TEMP_MASK) * 100;

	for (i = 0; i < tmdev->chip->sensor_num; i++) {
		int sensor_reg = caldata[i + 1] & TEMP_CALIB_MASK;
		int cdata, offset;
		int sensor_temp = tmdev->chip->calc_temp(tmdev, i, sensor_reg);

		/*
		 * Calibration data is CALIBRATE_DEFAULT - (calculated
		 * temperature from sensor reading at factory temperature
		 * minus actual factory temperature) * 14.88 (scale from
		 * temperature to register values)
		 */
		cdata = CALIBRATE_DEFAULT -
			((sensor_temp - ft_temp) * 10 / tmdev->chip->scale);
		if (cdata & ~TEMP_CALIB_MASK) {
			/*
			 * Calibration value more than 12-bit, but calibration
			 * register is 12-bit. In this case, ths hardware can
			 * still work without calibration, although the data
			 * won't be so accurate.
			 */
			dev_warn(dev, "sensor%d is not calibrated.\n", i);
			continue;
		}

		offset = (i % 2) * 16;
		regmap_update_bits(tmdev->regmap,
				   SUN50I_H6_THS_TEMP_CALIB + (i / 2 * 4),
				   TEMP_CALIB_MASK << offset,
				   cdata << offset);
	}

	return 0;
}

static int sun8i_ths_calibrate(struct ths_device *tmdev)
{
	struct nvmem_cell *calcell;
	struct device *dev = tmdev->dev;
	u16 *caldata;
	size_t callen;
	int ret = 0;

	calcell = nvmem_cell_get(dev, "calibration");
	if (IS_ERR(calcell)) {
		if (PTR_ERR(calcell) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		/*
		 * Even if the external calibration data stored in sid is
		 * not accessible, the THS hardware can still work, although
		 * the data won't be so accurate.
		 *
		 * The default value of calibration register is 0x800 for
		 * every sensor, and the calibration value is usually 0x7xx
		 * or 0x8xx, so they won't be away from the default value
		 * for a lot.
		 *
		 * So here we do not return error if the calibration data is
		 * not available, except the probe needs deferring.
		 */
		goto out;
	}

	caldata = nvmem_cell_read(calcell, &callen);
	if (IS_ERR(caldata)) {
		ret = PTR_ERR(caldata);
		goto out;
	}

	tmdev->chip->calibrate(tmdev, caldata, callen);

	kfree(caldata);
out:
	if (!IS_ERR(calcell))
		nvmem_cell_put(calcell);
	return ret;
}

static void sun8i_ths_reset_control_assert(void *data)
{
	reset_control_assert(data);
}

static int sun8i_ths_resource_init(struct ths_device *tmdev)
{
	struct device *dev = tmdev->dev;
	struct platform_device *pdev = to_platform_device(dev);
	void __iomem *base;
	int ret;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	tmdev->regmap = devm_regmap_init_mmio(dev, base, &config);
	if (IS_ERR(tmdev->regmap))
		return PTR_ERR(tmdev->regmap);

	if (tmdev->chip->has_bus_clk_reset) {
		tmdev->reset = devm_reset_control_get(dev, NULL);
		if (IS_ERR(tmdev->reset))
			return PTR_ERR(tmdev->reset);

		ret = reset_control_deassert(tmdev->reset);
		if (ret)
			return ret;

		ret = devm_add_action_or_reset(dev, sun8i_ths_reset_control_assert,
					       tmdev->reset);
		if (ret)
			return ret;

		tmdev->bus_clk = devm_clk_get_enabled(&pdev->dev, "bus");
		if (IS_ERR(tmdev->bus_clk))
			return PTR_ERR(tmdev->bus_clk);
	}

	if (tmdev->chip->has_mod_clk) {
		tmdev->mod_clk = devm_clk_get_enabled(&pdev->dev, "mod");
		if (IS_ERR(tmdev->mod_clk))
			return PTR_ERR(tmdev->mod_clk);
	}

	ret = clk_set_rate(tmdev->mod_clk, 24000000);
	if (ret)
		return ret;

	ret = sun8i_ths_calibrate(tmdev);
	if (ret)
		return ret;

	return 0;
}

static int sun8i_h3_thermal_init(struct ths_device *tmdev)
{
	int val;

	/* average over 4 samples */
	regmap_write(tmdev->regmap, SUN8I_THS_MFC,
		     SUN50I_THS_FILTER_EN |
		     SUN50I_THS_FILTER_TYPE(1));
	/*
	 * clkin = 24MHz
	 * filter_samples = 4
	 * period = 0.25s
	 *
	 * x = period * clkin / 4096 / filter_samples - 1
	 *   = 365
	 */
	val = GENMASK(7 + tmdev->chip->sensor_num, 8);
	regmap_write(tmdev->regmap, SUN8I_THS_IC,
		     SUN50I_H6_THS_PC_TEMP_PERIOD(365) | val);
	/*
	 * T_acq = 20us
	 * clkin = 24MHz
	 *
	 * x = T_acq * clkin - 1
	 *   = 479
	 */
	regmap_write(tmdev->regmap, SUN8I_THS_CTRL0,
		     SUN8I_THS_CTRL0_T_ACQ0(479));
	val = GENMASK(tmdev->chip->sensor_num - 1, 0);
	regmap_write(tmdev->regmap, SUN8I_THS_CTRL2,
		     SUN8I_THS_CTRL2_T_ACQ1(479) | val);

	return 0;
}

/*
 * Without this undocumented value, the returned temperatures would
 * be higher than real ones by about 20C.
 */
#define SUN50I_H6_CTRL0_UNK 0x0000002f

static int sun50i_h6_thermal_init(struct ths_device *tmdev)
{
	int val;

	/*
	 * T_acq = 20us
	 * clkin = 24MHz
	 *
	 * x = T_acq * clkin - 1
	 *   = 479
	 */
	regmap_write(tmdev->regmap, SUN50I_THS_CTRL0,
		     SUN50I_H6_CTRL0_UNK | SUN50I_THS_CTRL0_T_ACQ(479));
	/* average over 4 samples */
	regmap_write(tmdev->regmap, SUN50I_H6_THS_MFC,
		     SUN50I_THS_FILTER_EN |
		     SUN50I_THS_FILTER_TYPE(1));
	/*
	 * clkin = 24MHz
	 * filter_samples = 4
	 * period = 0.25s
	 *
	 * x = period * clkin / 4096 / filter_samples - 1
	 *   = 365
	 */
	regmap_write(tmdev->regmap, SUN50I_H6_THS_PC,
		     SUN50I_H6_THS_PC_TEMP_PERIOD(365));
	/* enable sensor */
	val = GENMASK(tmdev->chip->sensor_num - 1, 0);
	regmap_write(tmdev->regmap, SUN50I_H6_THS_ENABLE, val);
	/* thermal data interrupt enable */
	val = GENMASK(tmdev->chip->sensor_num - 1, 0);
	regmap_write(tmdev->regmap, SUN50I_H6_THS_DIC, val);

	return 0;
}

static int sun8i_ths_register(struct ths_device *tmdev)
{
	int i;

	for (i = 0; i < tmdev->chip->sensor_num; i++) {
		tmdev->sensor[i].tmdev = tmdev;
		tmdev->sensor[i].id = i;
		tmdev->sensor[i].tzd =
			devm_thermal_of_zone_register(tmdev->dev,
						      i,
						      &tmdev->sensor[i],
						      &ths_ops);
		if (IS_ERR(tmdev->sensor[i].tzd))
			return PTR_ERR(tmdev->sensor[i].tzd);

		devm_thermal_add_hwmon_sysfs(tmdev->dev, tmdev->sensor[i].tzd);
	}

	return 0;
}

static int sun8i_ths_probe(struct platform_device *pdev)
{
	struct ths_device *tmdev;
	struct device *dev = &pdev->dev;
	int ret, irq;

	tmdev = devm_kzalloc(dev, sizeof(*tmdev), GFP_KERNEL);
	if (!tmdev)
		return -ENOMEM;

	tmdev->dev = dev;
	tmdev->chip = of_device_get_match_data(&pdev->dev);
	if (!tmdev->chip)
		return -EINVAL;

	ret = sun8i_ths_resource_init(tmdev);
	if (ret)
		return ret;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = tmdev->chip->init(tmdev);
	if (ret)
		return ret;

	ret = sun8i_ths_register(tmdev);
	if (ret)
		return ret;

	/*
	 * Avoid entering the interrupt handler, the thermal device is not
	 * registered yet, we deffer the registration of the interrupt to
	 * the end.
	 */
	ret = devm_request_threaded_irq(dev, irq, NULL,
					sun8i_irq_thread,
					IRQF_ONESHOT, "ths", tmdev);
	if (ret)
		return ret;

	return 0;
}

static const struct ths_thermal_chip sun8i_a83t_ths = {
	.sensor_num = 3,
	.scale = 705,
	.offset = 191668,
	.temp_data_base = SUN8I_THS_TEMP_DATA,
	.calibrate = sun8i_h3_ths_calibrate,
	.init = sun8i_h3_thermal_init,
	.irq_ack = sun8i_h3_irq_ack,
	.calc_temp = sun8i_ths_calc_temp,
};

static const struct ths_thermal_chip sun8i_h3_ths = {
	.sensor_num = 1,
	.scale = 1211,
	.offset = 217000,
	.has_mod_clk = true,
	.has_bus_clk_reset = true,
	.temp_data_base = SUN8I_THS_TEMP_DATA,
	.calibrate = sun8i_h3_ths_calibrate,
	.init = sun8i_h3_thermal_init,
	.irq_ack = sun8i_h3_irq_ack,
	.calc_temp = sun8i_ths_calc_temp,
};

static const struct ths_thermal_chip sun8i_r40_ths = {
	.sensor_num = 2,
	.offset = 251086,
	.scale = 1130,
	.has_mod_clk = true,
	.has_bus_clk_reset = true,
	.temp_data_base = SUN8I_THS_TEMP_DATA,
	.calibrate = sun8i_h3_ths_calibrate,
	.init = sun8i_h3_thermal_init,
	.irq_ack = sun8i_h3_irq_ack,
	.calc_temp = sun8i_ths_calc_temp,
};

static const struct ths_thermal_chip sun50i_a64_ths = {
	.sensor_num = 3,
	.offset = 260890,
	.scale = 1170,
	.has_mod_clk = true,
	.has_bus_clk_reset = true,
	.temp_data_base = SUN8I_THS_TEMP_DATA,
	.calibrate = sun8i_h3_ths_calibrate,
	.init = sun8i_h3_thermal_init,
	.irq_ack = sun8i_h3_irq_ack,
	.calc_temp = sun8i_ths_calc_temp,
};

static const struct ths_thermal_chip sun50i_a100_ths = {
	.sensor_num = 3,
	.has_bus_clk_reset = true,
	.ft_deviation = 8000,
	.offset = 187744,
	.scale = 672,
	.temp_data_base = SUN50I_H6_THS_TEMP_DATA,
	.calibrate = sun50i_h6_ths_calibrate,
	.init = sun50i_h6_thermal_init,
	.irq_ack = sun50i_h6_irq_ack,
	.calc_temp = sun8i_ths_calc_temp,
};

static const struct ths_thermal_chip sun50i_h5_ths = {
	.sensor_num = 2,
	.has_mod_clk = true,
	.has_bus_clk_reset = true,
	.temp_data_base = SUN8I_THS_TEMP_DATA,
	.calibrate = sun8i_h3_ths_calibrate,
	.init = sun8i_h3_thermal_init,
	.irq_ack = sun8i_h3_irq_ack,
	.calc_temp = sun50i_h5_calc_temp,
};

static const struct ths_thermal_chip sun50i_h6_ths = {
	.sensor_num = 2,
	.has_bus_clk_reset = true,
	.ft_deviation = 7000,
	.offset = 187744,
	.scale = 672,
	.temp_data_base = SUN50I_H6_THS_TEMP_DATA,
	.calibrate = sun50i_h6_ths_calibrate,
	.init = sun50i_h6_thermal_init,
	.irq_ack = sun50i_h6_irq_ack,
	.calc_temp = sun8i_ths_calc_temp,
};

static const struct of_device_id of_ths_match[] = {
	{ .compatible = "allwinner,sun8i-a83t-ths", .data = &sun8i_a83t_ths },
	{ .compatible = "allwinner,sun8i-h3-ths", .data = &sun8i_h3_ths },
	{ .compatible = "allwinner,sun8i-r40-ths", .data = &sun8i_r40_ths },
	{ .compatible = "allwinner,sun50i-a64-ths", .data = &sun50i_a64_ths },
	{ .compatible = "allwinner,sun50i-a100-ths", .data = &sun50i_a100_ths },
	{ .compatible = "allwinner,sun50i-h5-ths", .data = &sun50i_h5_ths },
	{ .compatible = "allwinner,sun50i-h6-ths", .data = &sun50i_h6_ths },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, of_ths_match);

static struct platform_driver ths_driver = {
	.probe = sun8i_ths_probe,
	.driver = {
		.name = "sun8i-thermal",
		.of_match_table = of_ths_match,
	},
};
module_platform_driver(ths_driver);

MODULE_DESCRIPTION("Thermal sensor driver for Allwinner SOC");
MODULE_LICENSE("GPL v2");
