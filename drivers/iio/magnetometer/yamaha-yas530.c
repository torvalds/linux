// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for the Yamaha YAS magnetic sensors, often used in Samsung
 * mobile phones. While all are not yet handled because of lacking
 * hardware, expand this driver to handle the different variants:
 *
 * YAS530 MS-3E (2011 Samsung Galaxy S Advance)
 * YAS532 MS-3R (2011 Samsung Galaxy S4)
 * YAS533 MS-3F (Vivo 1633, 1707, V3, Y21L)
 * (YAS534 is a magnetic switch, not handled)
 * YAS535 MS-6C
 * YAS536 MS-3W
 * YAS537 MS-3T (2015 Samsung Galaxy S6, Note 5, Galaxy S7)
 * YAS539 MS-3S (2018 Samsung Galaxy A7 SM-A750FN)
 *
 * Code functions found in the MPU3050 YAS530 and YAS532 drivers
 * named "inv_compass" in the Tegra Android kernel tree.
 * Copyright (C) 2012 InvenSense Corporation
 *
 * Code functions for YAS537 based on Yamaha Android kernel driver.
 * Copyright (c) 2014 Yamaha Corporation
 *
 * Author: Linus Walleij <linus.walleij@linaro.org>
 */
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/random.h>
#include <linux/units.h>

#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#include <linux/unaligned.h>

/* Commonly used registers */
#define YAS5XX_DEVICE_ID		0x80
#define YAS5XX_MEASURE_DATA		0xB0

/* These registers are used by YAS530, YAS532 and YAS533 */
#define YAS530_ACTUATE_INIT_COIL	0x81
#define YAS530_MEASURE			0x82
#define YAS530_CONFIG			0x83
#define YAS530_MEASURE_INTERVAL		0x84
#define YAS530_OFFSET_X			0x85 /* [-31 .. 31] */
#define YAS530_OFFSET_Y1		0x86 /* [-31 .. 31] */
#define YAS530_OFFSET_Y2		0x87 /* [-31 .. 31] */
#define YAS530_TEST1			0x88
#define YAS530_TEST2			0x89
#define YAS530_CAL			0x90

/* Registers used by YAS537 */
#define YAS537_MEASURE			0x81 /* Originally YAS537_REG_CMDR */
#define YAS537_CONFIG			0x82 /* Originally YAS537_REG_CONFR */
#define YAS537_MEASURE_INTERVAL		0x83 /* Originally YAS537_REG_INTRVLR */
#define YAS537_OFFSET_X			0x84 /* Originally YAS537_REG_OXR */
#define YAS537_OFFSET_Y1		0x85 /* Originally YAS537_REG_OY1R */
#define YAS537_OFFSET_Y2		0x86 /* Originally YAS537_REG_OY2R */
#define YAS537_AVR			0x87
#define YAS537_HCK			0x88
#define YAS537_LCK			0x89
#define YAS537_SRST			0x90
#define YAS537_ADCCAL			0x91
#define YAS537_MTC			0x93
#define YAS537_OC			0x9E
#define YAS537_TRM			0x9F
#define YAS537_CAL			0xC0

/* Bits in the YAS5xx config register */
#define YAS5XX_CONFIG_INTON		BIT(0) /* Interrupt on? */
#define YAS5XX_CONFIG_INTHACT		BIT(1) /* Interrupt active high? */
#define YAS5XX_CONFIG_CCK_MASK		GENMASK(4, 2)
#define YAS5XX_CONFIG_CCK_SHIFT		2

/* Bits in the measure command register */
#define YAS5XX_MEASURE_START		BIT(0)
#define YAS5XX_MEASURE_LDTC		BIT(1)
#define YAS5XX_MEASURE_FORS		BIT(2)
#define YAS5XX_MEASURE_DLYMES		BIT(4)
#define YAS5XX_MEASURE_CONT		BIT(5)

/* Bits in the measure data register */
#define YAS5XX_MEASURE_DATA_BUSY	BIT(7)

#define YAS530_DEVICE_ID		0x01 /* YAS530 (MS-3E) */
#define YAS530_VERSION_A		0 /* YAS530 (MS-3E A) */
#define YAS530_VERSION_B		1 /* YAS530B (MS-3E B) */
#define YAS530_VERSION_A_COEF		380
#define YAS530_VERSION_B_COEF		550
#define YAS530_DATA_BITS		12
#define YAS530_DATA_CENTER		BIT(YAS530_DATA_BITS - 1)
#define YAS530_DATA_OVERFLOW		(BIT(YAS530_DATA_BITS) - 1)

#define YAS532_DEVICE_ID		0x02 /* YAS532/YAS533 (MS-3R/F) */
#define YAS532_VERSION_AB		0 /* YAS532/533 AB (MS-3R/F AB) */
#define YAS532_VERSION_AC		1 /* YAS532/533 AC (MS-3R/F AC) */
#define YAS532_VERSION_AB_COEF		1800
#define YAS532_VERSION_AC_COEF_X	850
#define YAS532_VERSION_AC_COEF_Y1	750
#define YAS532_VERSION_AC_COEF_Y2	750
#define YAS532_DATA_BITS		13
#define YAS532_DATA_CENTER		BIT(YAS532_DATA_BITS - 1)
#define YAS532_DATA_OVERFLOW		(BIT(YAS532_DATA_BITS) - 1)

#define YAS537_DEVICE_ID		0x07 /* YAS537 (MS-3T) */
#define YAS537_VERSION_0		0 /* Version naming unknown */
#define YAS537_VERSION_1		1 /* Version naming unknown */
#define YAS537_MAG_AVERAGE_32_MASK	GENMASK(6, 4)
#define YAS537_MEASURE_TIME_WORST_US	1500
#define YAS537_DEFAULT_SENSOR_DELAY_MS	50
#define YAS537_MAG_RCOIL_TIME_US	65
#define YAS537_MTC3_MASK_PREP		GENMASK(7, 0)
#define YAS537_MTC3_MASK_GET		GENMASK(7, 5)
#define YAS537_MTC3_ADD_BIT		BIT(4)
#define YAS537_HCK_MASK_PREP		GENMASK(4, 0)
#define YAS537_HCK_MASK_GET		GENMASK(7, 4)
#define YAS537_LCK_MASK_PREP		GENMASK(4, 0)
#define YAS537_LCK_MASK_GET		GENMASK(3, 0)
#define YAS537_OC_MASK_GET		GENMASK(5, 0)

/* Turn off device regulators etc after 5 seconds of inactivity */
#define YAS5XX_AUTOSUSPEND_DELAY_MS	5000

enum chip_ids {
	yas530,
	yas532,
	yas533,
	yas537,
};

static const int yas530_volatile_reg[] = {
	YAS530_ACTUATE_INIT_COIL,
	YAS530_MEASURE,
};

static const int yas537_volatile_reg[] = {
	YAS537_MEASURE,
};

struct yas5xx_calibration {
	/* Linearization calibration x, y1, y2 */
	s32 r[3];
	u32 f[3];
	/* Temperature compensation calibration */
	s16 Cx, Cy1, Cy2;
	/* Misc calibration coefficients */
	s8  a2, a3, a4, a6, a7, a8;
	s16 a5, a9;
	u8  k;
	/* clock divider */
	u8 dck;
};

struct yas5xx;

/**
 * struct yas5xx_chip_info - device-specific data and function pointers
 * @devid: device ID number
 * @product_name: product name of the YAS variant
 * @version_names: version letters or namings
 * @volatile_reg: device-specific volatile registers
 * @volatile_reg_qty: quantity of device-specific volatile registers
 * @scaling_val2: scaling value for IIO_CHAN_INFO_SCALE
 * @t_ref: number of counts at reference temperature 20 °C
 * @min_temp_x10: starting point of temperature counting in 1/10:s degrees Celsius
 * @get_measure: function pointer to get a measurement
 * @get_calibration_data: function pointer to get calibration data
 * @dump_calibration: function pointer to dump calibration for debugging
 * @measure_offsets: function pointer to measure the offsets
 * @power_on: function pointer to power-on procedure
 *
 * The "t_ref" value for YAS532/533 is known from the Android driver.
 * For YAS530 and YAS537 it was approximately measured.
 *
 * The temperatures "min_temp_x10" are derived from the temperature resolutions
 * given in the data sheets.
 */
struct yas5xx_chip_info {
	unsigned int devid;
	const char *product_name;
	const char *version_names[2];
	const int *volatile_reg;
	int volatile_reg_qty;
	u32 scaling_val2;
	u16 t_ref;
	s16 min_temp_x10;
	int (*get_measure)(struct yas5xx *yas5xx, s32 *to, s32 *xo, s32 *yo, s32 *zo);
	int (*get_calibration_data)(struct yas5xx *yas5xx);
	void (*dump_calibration)(struct yas5xx *yas5xx);
	int (*measure_offsets)(struct yas5xx *yas5xx);
	int (*power_on)(struct yas5xx *yas5xx);
};

/**
 * struct yas5xx - state container for the YAS5xx driver
 * @dev: parent device pointer
 * @chip_info: device-specific data and function pointers
 * @version: device version
 * @calibration: calibration settings from the OTP storage
 * @hard_offsets: offsets for each axis measured with initcoil actuated
 * @orientation: mounting matrix, flipped axis etc
 * @map: regmap to access the YAX5xx registers over I2C
 * @regs: the vdd and vddio power regulators
 * @reset: optional GPIO line used for handling RESET
 * @lock: locks the magnetometer for exclusive use during a measurement (which
 * involves several register transactions so the regmap lock is not enough)
 * so that measurements get serialized in a first-come-first serve manner
 * @scan: naturally aligned measurements
 */
struct yas5xx {
	struct device *dev;
	const struct yas5xx_chip_info *chip_info;
	unsigned int version;
	struct yas5xx_calibration calibration;
	s8 hard_offsets[3];
	struct iio_mount_matrix orientation;
	struct regmap *map;
	struct regulator_bulk_data regs[2];
	struct gpio_desc *reset;
	struct mutex lock;
	/*
	 * The scanout is 4 x 32 bits in CPU endianness.
	 * Ensure timestamp is naturally aligned
	 */
	struct {
		s32 channels[4];
		aligned_s64 ts;
	} scan;
};

/* On YAS530 the x, y1 and y2 values are 12 bits */
static u16 yas530_extract_axis(u8 *data)
{
	u16 val;

	/*
	 * These are the bits used in a 16bit word:
	 * 15 14 13 12 11 10 9  8  7  6  5  4  3  2  1  0
	 *    x  x  x  x  x  x  x  x  x  x  x  x
	 */
	val = get_unaligned_be16(&data[0]);
	val = FIELD_GET(GENMASK(14, 3), val);
	return val;
}

/* On YAS532 the x, y1 and y2 values are 13 bits */
static u16 yas532_extract_axis(u8 *data)
{
	u16 val;

	/*
	 * These are the bits used in a 16bit word:
	 * 15 14 13 12 11 10 9  8  7  6  5  4  3  2  1  0
	 *    x  x  x  x  x  x  x  x  x  x  x  x  x
	 */
	val = get_unaligned_be16(&data[0]);
	val = FIELD_GET(GENMASK(14, 2), val);
	return val;
}

/**
 * yas530_measure() - Make a measure from the hardware
 * @yas5xx: The device state
 * @t: the raw temperature measurement
 * @x: the raw x axis measurement
 * @y1: the y1 axis measurement
 * @y2: the y2 axis measurement
 * @return: 0 on success or error code
 *
 * Used by YAS530, YAS532 and YAS533.
 */
static int yas530_measure(struct yas5xx *yas5xx, u16 *t, u16 *x, u16 *y1, u16 *y2)
{
	const struct yas5xx_chip_info *ci = yas5xx->chip_info;
	unsigned int busy;
	u8 data[8];
	int ret;
	u16 val;

	mutex_lock(&yas5xx->lock);
	ret = regmap_write(yas5xx->map, YAS530_MEASURE, YAS5XX_MEASURE_START);
	if (ret < 0)
		goto out_unlock;

	/*
	 * Typical time to measure 1500 us, max 2000 us so wait min 500 us
	 * and at most 20000 us (one magnitude more than the datsheet max)
	 * before timeout.
	 */
	ret = regmap_read_poll_timeout(yas5xx->map, YAS5XX_MEASURE_DATA, busy,
				       !(busy & YAS5XX_MEASURE_DATA_BUSY),
				       500, 20000);
	if (ret) {
		dev_err(yas5xx->dev, "timeout waiting for measurement\n");
		goto out_unlock;
	}

	ret = regmap_bulk_read(yas5xx->map, YAS5XX_MEASURE_DATA,
			       data, sizeof(data));
	if (ret)
		goto out_unlock;

	mutex_unlock(&yas5xx->lock);

	switch (ci->devid) {
	case YAS530_DEVICE_ID:
		/*
		 * The t value is 9 bits in big endian format
		 * These are the bits used in a 16bit word:
		 * 15 14 13 12 11 10 9  8  7  6  5  4  3  2  1  0
		 *    x  x  x  x  x  x  x  x  x
		 */
		val = get_unaligned_be16(&data[0]);
		val = FIELD_GET(GENMASK(14, 6), val);
		*t = val;
		*x = yas530_extract_axis(&data[2]);
		*y1 = yas530_extract_axis(&data[4]);
		*y2 = yas530_extract_axis(&data[6]);
		break;
	case YAS532_DEVICE_ID:
		/*
		 * The t value is 10 bits in big endian format
		 * These are the bits used in a 16bit word:
		 * 15 14 13 12 11 10 9  8  7  6  5  4  3  2  1  0
		 *    x  x  x  x  x  x  x  x  x  x
		 */
		val = get_unaligned_be16(&data[0]);
		val = FIELD_GET(GENMASK(14, 5), val);
		*t = val;
		*x = yas532_extract_axis(&data[2]);
		*y1 = yas532_extract_axis(&data[4]);
		*y2 = yas532_extract_axis(&data[6]);
		break;
	default:
		dev_err(yas5xx->dev, "unknown data format\n");
		ret = -EINVAL;
		break;
	}

	return ret;

out_unlock:
	mutex_unlock(&yas5xx->lock);
	return ret;
}

/**
 * yas537_measure() - Make a measure from the hardware
 * @yas5xx: The device state
 * @t: the raw temperature measurement
 * @x: the raw x axis measurement
 * @y1: the y1 axis measurement
 * @y2: the y2 axis measurement
 * @return: 0 on success or error code
 */
static int yas537_measure(struct yas5xx *yas5xx, u16 *t, u16 *x, u16 *y1, u16 *y2)
{
	struct yas5xx_calibration *c = &yas5xx->calibration;
	unsigned int busy;
	u8 data[8];
	u16 xy1y2[3];
	s32 h[3], s[3];
	int half_range = BIT(13);
	int i, ret;

	mutex_lock(&yas5xx->lock);

	/* Contrary to YAS530/532, also a "cont" bit is set, meaning unknown */
	ret = regmap_write(yas5xx->map, YAS537_MEASURE, YAS5XX_MEASURE_START |
			   YAS5XX_MEASURE_CONT);
	if (ret < 0)
		goto out_unlock;

	/* Use same timeout like YAS530/532 but the bit is in data row 2 */
	ret = regmap_read_poll_timeout(yas5xx->map, YAS5XX_MEASURE_DATA + 2, busy,
				       !(busy & YAS5XX_MEASURE_DATA_BUSY),
				       500, 20000);
	if (ret) {
		dev_err(yas5xx->dev, "timeout waiting for measurement\n");
		goto out_unlock;
	}

	ret = regmap_bulk_read(yas5xx->map, YAS5XX_MEASURE_DATA,
			       data, sizeof(data));
	if (ret)
		goto out_unlock;

	mutex_unlock(&yas5xx->lock);

	*t = get_unaligned_be16(&data[0]);
	xy1y2[0] = FIELD_GET(GENMASK(13, 0), get_unaligned_be16(&data[2]));
	xy1y2[1] = get_unaligned_be16(&data[4]);
	xy1y2[2] = get_unaligned_be16(&data[6]);

	/* The second version of YAS537 needs to include calibration coefficients */
	if (yas5xx->version == YAS537_VERSION_1) {
		for (i = 0; i < 3; i++)
			s[i] = xy1y2[i] - half_range;
		h[0] = (c->k *   (128 * s[0] + c->a2 * s[1] + c->a3 * s[2])) / half_range;
		h[1] = (c->k * (c->a4 * s[0] + c->a5 * s[1] + c->a6 * s[2])) / half_range;
		h[2] = (c->k * (c->a7 * s[0] + c->a8 * s[1] + c->a9 * s[2])) / half_range;
		for (i = 0; i < 3; i++) {
			h[i] = clamp(h[i], -half_range, half_range - 1);
			xy1y2[i] = h[i] + half_range;
		}
	}

	*x = xy1y2[0];
	*y1 = xy1y2[1];
	*y2 = xy1y2[2];

	return 0;

out_unlock:
	mutex_unlock(&yas5xx->lock);
	return ret;
}

/* Used by YAS530, YAS532 and YAS533 */
static s32 yas530_linearize(struct yas5xx *yas5xx, u16 val, int axis)
{
	const struct yas5xx_chip_info *ci = yas5xx->chip_info;
	struct yas5xx_calibration *c = &yas5xx->calibration;
	static const s32 yas532ac_coef[] = {
		YAS532_VERSION_AC_COEF_X,
		YAS532_VERSION_AC_COEF_Y1,
		YAS532_VERSION_AC_COEF_Y2,
	};
	s32 coef;

	/* Select coefficients */
	switch (ci->devid) {
	case YAS530_DEVICE_ID:
		if (yas5xx->version == YAS530_VERSION_A)
			coef = YAS530_VERSION_A_COEF;
		else
			coef = YAS530_VERSION_B_COEF;
		break;
	case YAS532_DEVICE_ID:
		if (yas5xx->version == YAS532_VERSION_AB)
			coef = YAS532_VERSION_AB_COEF;
		else
			/* Elaborate coefficients */
			coef = yas532ac_coef[axis];
		break;
	default:
		dev_err(yas5xx->dev, "unknown device type\n");
		return val;
	}
	/*
	 * Linearization formula:
	 *
	 * x' = x - (3721 + 50 * f) + (xoffset - r) * c
	 *
	 * Where f and r are calibration values, c is a per-device
	 * and sometimes per-axis coefficient.
	 */
	return val - (3721 + 50 * c->f[axis]) +
		(yas5xx->hard_offsets[axis] - c->r[axis]) * coef;
}

static s32 yas5xx_calc_temperature(struct yas5xx *yas5xx, u16 t)
{
	const struct yas5xx_chip_info *ci = yas5xx->chip_info;
	s32 to;
	u16 t_ref;
	s16 min_temp_x10;
	int ref_temp_x10;

	t_ref = ci->t_ref;
	min_temp_x10 = ci->min_temp_x10;
	ref_temp_x10 = 200;

	to = (min_temp_x10 + ((ref_temp_x10 - min_temp_x10) * t / t_ref)) * 100;
	return to;
}

/**
 * yas530_get_measure() - Measure a sample of all axis and process
 * @yas5xx: The device state
 * @to: Temperature out
 * @xo: X axis out
 * @yo: Y axis out
 * @zo: Z axis out
 * @return: 0 on success or error code
 *
 * Used by YAS530, YAS532 and YAS533.
 */
static int yas530_get_measure(struct yas5xx *yas5xx, s32 *to, s32 *xo, s32 *yo, s32 *zo)
{
	const struct yas5xx_chip_info *ci = yas5xx->chip_info;
	struct yas5xx_calibration *c = &yas5xx->calibration;
	u16 t_ref, t_comp, t, x, y1, y2;
	/* These are signed x, signed y1 etc */
	s32 sx, sy1, sy2, sy, sz;
	int ret;

	/* We first get raw data that needs to be translated to [x,y,z] */
	ret = yas530_measure(yas5xx, &t, &x, &y1, &y2);
	if (ret)
		return ret;

	/* Do some linearization if available */
	sx = yas530_linearize(yas5xx, x, 0);
	sy1 = yas530_linearize(yas5xx, y1, 1);
	sy2 = yas530_linearize(yas5xx, y2, 2);

	/*
	 * Set the temperature for compensation (unit: counts):
	 * YAS532/YAS533 version AC uses the temperature deviation as a
	 * multiplier. YAS530 and YAS532 version AB use solely the t value.
	 */
	t_ref = ci->t_ref;
	if (ci->devid == YAS532_DEVICE_ID &&
	    yas5xx->version == YAS532_VERSION_AC) {
		t_comp = t - t_ref;
	} else {
		t_comp = t;
	}

	/*
	 * Temperature compensation for x, y1, y2 respectively:
	 *
	 *          Cx * t_comp
	 * x' = x - -----------
	 *              100
	 */
	sx = sx - (c->Cx * t_comp) / 100;
	sy1 = sy1 - (c->Cy1 * t_comp) / 100;
	sy2 = sy2 - (c->Cy2 * t_comp) / 100;

	/*
	 * Break y1 and y2 into y and z, y1 and y2 are apparently encoding
	 * y and z.
	 */
	sy = sy1 - sy2;
	sz = -sy1 - sy2;

	/* Calculate temperature readout */
	*to = yas5xx_calc_temperature(yas5xx, t);

	/*
	 * Calibrate [x,y,z] with some formulas like this:
	 *
	 *            100 * x + a_2 * y + a_3 * z
	 *  x' = k *  ---------------------------
	 *                        10
	 *
	 *           a_4 * x + a_5 * y + a_6 * z
	 *  y' = k * ---------------------------
	 *                        10
	 *
	 *           a_7 * x + a_8 * y + a_9 * z
	 *  z' = k * ---------------------------
	 *                        10
	 */
	*xo = c->k * ((100 * sx + c->a2 * sy + c->a3 * sz) / 10);
	*yo = c->k * ((c->a4 * sx + c->a5 * sy + c->a6 * sz) / 10);
	*zo = c->k * ((c->a7 * sx + c->a8 * sy + c->a9 * sz) / 10);

	return 0;
}

/**
 * yas537_get_measure() - Measure a sample of all axis and process
 * @yas5xx: The device state
 * @to: Temperature out
 * @xo: X axis out
 * @yo: Y axis out
 * @zo: Z axis out
 * @return: 0 on success or error code
 */
static int yas537_get_measure(struct yas5xx *yas5xx, s32 *to, s32 *xo, s32 *yo, s32 *zo)
{
	u16 t, x, y1, y2;
	int ret;

	/* We first get raw data that needs to be translated to [x,y,z] */
	ret = yas537_measure(yas5xx, &t, &x, &y1, &y2);
	if (ret)
		return ret;

	/* Calculate temperature readout */
	*to = yas5xx_calc_temperature(yas5xx, t);

	/*
	 * Unfortunately, no linearization or temperature compensation formulas
	 * are known for YAS537.
	 */

	/* Calculate x, y, z from x, y1, y2 */
	*xo = (x - BIT(13)) * 300;
	*yo = (y1 - y2) * 1732 / 10;
	*zo = (-y1 - y2 + BIT(14)) * 300;

	return 0;
}

static int yas5xx_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2,
			   long mask)
{
	struct yas5xx *yas5xx = iio_priv(indio_dev);
	const struct yas5xx_chip_info *ci = yas5xx->chip_info;
	s32 t, x, y, z;
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_PROCESSED:
	case IIO_CHAN_INFO_RAW:
		pm_runtime_get_sync(yas5xx->dev);
		ret = ci->get_measure(yas5xx, &t, &x, &y, &z);
		pm_runtime_put_autosuspend(yas5xx->dev);
		if (ret)
			return ret;
		switch (chan->address) {
		case 0:
			*val = t;
			break;
		case 1:
			*val = x;
			break;
		case 2:
			*val = y;
			break;
		case 3:
			*val = z;
			break;
		default:
			dev_err(yas5xx->dev, "unknown channel\n");
			return -EINVAL;
		}
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = 1;
		*val2 = ci->scaling_val2;
		return IIO_VAL_FRACTIONAL;
	default:
		/* Unknown request */
		return -EINVAL;
	}
}

static void yas5xx_fill_buffer(struct iio_dev *indio_dev)
{
	struct yas5xx *yas5xx = iio_priv(indio_dev);
	const struct yas5xx_chip_info *ci = yas5xx->chip_info;
	s32 t, x, y, z;
	int ret;

	pm_runtime_get_sync(yas5xx->dev);
	ret = ci->get_measure(yas5xx, &t, &x, &y, &z);
	pm_runtime_put_autosuspend(yas5xx->dev);
	if (ret) {
		dev_err(yas5xx->dev, "error refilling buffer\n");
		return;
	}
	yas5xx->scan.channels[0] = t;
	yas5xx->scan.channels[1] = x;
	yas5xx->scan.channels[2] = y;
	yas5xx->scan.channels[3] = z;
	iio_push_to_buffers_with_ts(indio_dev, &yas5xx->scan, sizeof(yas5xx->scan),
				    iio_get_time_ns(indio_dev));
}

static irqreturn_t yas5xx_handle_trigger(int irq, void *p)
{
	const struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;

	yas5xx_fill_buffer(indio_dev);
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}


static const struct iio_mount_matrix *
yas5xx_get_mount_matrix(const struct iio_dev *indio_dev,
			const struct iio_chan_spec *chan)
{
	struct yas5xx *yas5xx = iio_priv(indio_dev);

	return &yas5xx->orientation;
}

static const struct iio_chan_spec_ext_info yas5xx_ext_info[] = {
	IIO_MOUNT_MATRIX(IIO_SHARED_BY_DIR, yas5xx_get_mount_matrix),
	{ }
};

#define YAS5XX_AXIS_CHANNEL(axis, index)				\
	{								\
		.type = IIO_MAGN,					\
		.modified = 1,						\
		.channel2 = IIO_MOD_##axis,				\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |		\
			BIT(IIO_CHAN_INFO_SCALE),			\
		.ext_info = yas5xx_ext_info,				\
		.address = index,					\
		.scan_index = index,					\
		.scan_type = {						\
			.sign = 's',					\
			.realbits = 32,					\
			.storagebits = 32,				\
			.endianness = IIO_CPU,				\
		},							\
	}

static const struct iio_chan_spec yas5xx_channels[] = {
	{
		.type = IIO_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
		.address = 0,
		.scan_index = 0,
		.scan_type = {
			.sign = 's',
			.realbits = 32,
			.storagebits = 32,
			.endianness = IIO_CPU,
		},
	},
	YAS5XX_AXIS_CHANNEL(X, 1),
	YAS5XX_AXIS_CHANNEL(Y, 2),
	YAS5XX_AXIS_CHANNEL(Z, 3),
	IIO_CHAN_SOFT_TIMESTAMP(4),
};

static const unsigned long yas5xx_scan_masks[] = { GENMASK(3, 0), 0 };

static const struct iio_info yas5xx_info = {
	.read_raw = &yas5xx_read_raw,
};

static bool yas5xx_volatile_reg(struct device *dev, unsigned int reg)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct yas5xx *yas5xx = iio_priv(indio_dev);
	const struct yas5xx_chip_info *ci = yas5xx->chip_info;
	int reg_qty;
	int i;

	if (reg >= YAS5XX_MEASURE_DATA && reg < YAS5XX_MEASURE_DATA + 8)
		return true;

	/*
	 * YAS versions share different registers on the same address,
	 * need to differentiate.
	 */
	reg_qty = ci->volatile_reg_qty;
	for (i = 0; i < reg_qty; i++) {
		if (reg == ci->volatile_reg[i])
			return true;
	}

	return false;
}

/* TODO: enable regmap cache, using mark dirty and sync at runtime resume */
static const struct regmap_config yas5xx_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xff,
	.volatile_reg = yas5xx_volatile_reg,
};

/**
 * yas530_extract_calibration() - extracts the a2-a9 and k calibration
 * @data: the bitfield to use
 * @c: the calibration to populate
 *
 * Used by YAS530, YAS532 and YAS533.
 */
static void yas530_extract_calibration(u8 *data, struct yas5xx_calibration *c)
{
	u64 val = get_unaligned_be64(data);

	/*
	 * Bitfield layout for the axis calibration data, for factor
	 * a2 = 2 etc, k = k, c = clock divider
	 *
	 * n   7 6 5 4 3 2 1 0
	 * 0 [ 2 2 2 2 2 2 3 3 ] bits 63 .. 56
	 * 1 [ 3 3 4 4 4 4 4 4 ] bits 55 .. 48
	 * 2 [ 5 5 5 5 5 5 6 6 ] bits 47 .. 40
	 * 3 [ 6 6 6 6 7 7 7 7 ] bits 39 .. 32
	 * 4 [ 7 7 7 8 8 8 8 8 ] bits 31 .. 24
	 * 5 [ 8 9 9 9 9 9 9 9 ] bits 23 .. 16
	 * 6 [ 9 k k k k k c c ] bits 15 .. 8
	 * 7 [ c x x x x x x x ] bits  7 .. 0
	 */
	c->a2 = FIELD_GET(GENMASK_ULL(63, 58), val) - 32;
	c->a3 = FIELD_GET(GENMASK_ULL(57, 54), val) - 8;
	c->a4 = FIELD_GET(GENMASK_ULL(53, 48), val) - 32;
	c->a5 = FIELD_GET(GENMASK_ULL(47, 42), val) + 38;
	c->a6 = FIELD_GET(GENMASK_ULL(41, 36), val) - 32;
	c->a7 = FIELD_GET(GENMASK_ULL(35, 29), val) - 64;
	c->a8 = FIELD_GET(GENMASK_ULL(28, 23), val) - 32;
	c->a9 = FIELD_GET(GENMASK_ULL(22, 15), val);
	c->k = FIELD_GET(GENMASK_ULL(14, 10), val) + 10;
	c->dck = FIELD_GET(GENMASK_ULL(9, 7), val);
}

static int yas530_get_calibration_data(struct yas5xx *yas5xx)
{
	struct yas5xx_calibration *c = &yas5xx->calibration;
	u8 data[16];
	u32 val;
	int ret;

	/* Dummy read, first read is ALWAYS wrong */
	ret = regmap_bulk_read(yas5xx->map, YAS530_CAL, data, sizeof(data));
	if (ret)
		return ret;

	/* Actual calibration readout */
	ret = regmap_bulk_read(yas5xx->map, YAS530_CAL, data, sizeof(data));
	if (ret)
		return ret;
	dev_dbg(yas5xx->dev, "calibration data: %16ph\n", data);

	/* Contribute calibration data to the input pool for kernel entropy */
	add_device_randomness(data, sizeof(data));

	/* Extract version */
	yas5xx->version = data[15] & GENMASK(1, 0);

	/* Extract the calibration from the bitfield */
	c->Cx = data[0] * 6 - 768;
	c->Cy1 = data[1] * 6 - 768;
	c->Cy2 = data[2] * 6 - 768;
	yas530_extract_calibration(&data[3], c);

	/*
	 * Extract linearization:
	 * Linearization layout in the 32 bits at byte 11:
	 * The r factors are 6 bit values where bit 5 is the sign
	 *
	 * n    7  6  5  4  3  2  1  0
	 * 0 [ xx xx xx r0 r0 r0 r0 r0 ] bits 31 .. 24
	 * 1 [ r0 f0 f0 r1 r1 r1 r1 r1 ] bits 23 .. 16
	 * 2 [ r1 f1 f1 r2 r2 r2 r2 r2 ] bits 15 .. 8
	 * 3 [ r2 f2 f2 xx xx xx xx xx ] bits  7 .. 0
	 */
	val = get_unaligned_be32(&data[11]);
	c->f[0] = FIELD_GET(GENMASK(22, 21), val);
	c->f[1] = FIELD_GET(GENMASK(14, 13), val);
	c->f[2] = FIELD_GET(GENMASK(6, 5), val);
	c->r[0] = sign_extend32(FIELD_GET(GENMASK(28, 23), val), 5);
	c->r[1] = sign_extend32(FIELD_GET(GENMASK(20, 15), val), 5);
	c->r[2] = sign_extend32(FIELD_GET(GENMASK(12, 7), val), 5);

	return 0;
}

static int yas532_get_calibration_data(struct yas5xx *yas5xx)
{
	struct yas5xx_calibration *c = &yas5xx->calibration;
	u8 data[14];
	u32 val;
	int ret;

	/* Dummy read, first read is ALWAYS wrong */
	ret = regmap_bulk_read(yas5xx->map, YAS530_CAL, data, sizeof(data));
	if (ret)
		return ret;
	/* Actual calibration readout */
	ret = regmap_bulk_read(yas5xx->map, YAS530_CAL, data, sizeof(data));
	if (ret)
		return ret;
	dev_dbg(yas5xx->dev, "calibration data: %14ph\n", data);

	/* Sanity check, is this all zeroes? */
	if (!memchr_inv(data, 0x00, 13) && !(data[13] & BIT(7)))
		dev_warn(yas5xx->dev, "calibration is blank!\n");

	/* Contribute calibration data to the input pool for kernel entropy */
	add_device_randomness(data, sizeof(data));

	/* Only one bit of version info reserved here as far as we know */
	yas5xx->version = data[13] & BIT(0);

	/* Extract calibration from the bitfield */
	c->Cx = data[0] * 10 - 1280;
	c->Cy1 = data[1] * 10 - 1280;
	c->Cy2 = data[2] * 10 - 1280;
	yas530_extract_calibration(&data[3], c);

	/*
	 * Extract linearization:
	 * Linearization layout in the 32 bits at byte 10:
	 * The r factors are 6 bit values where bit 5 is the sign
	 *
	 * n    7  6  5  4  3  2  1  0
	 * 0 [ xx r0 r0 r0 r0 r0 r0 f0 ] bits 31 .. 24
	 * 1 [ f0 r1 r1 r1 r1 r1 r1 f1 ] bits 23 .. 16
	 * 2 [ f1 r2 r2 r2 r2 r2 r2 f2 ] bits 15 .. 8
	 * 3 [ f2 xx xx xx xx xx xx xx ] bits  7 .. 0
	 */
	val = get_unaligned_be32(&data[10]);
	c->f[0] = FIELD_GET(GENMASK(24, 23), val);
	c->f[1] = FIELD_GET(GENMASK(16, 15), val);
	c->f[2] = FIELD_GET(GENMASK(8, 7), val);
	c->r[0] = sign_extend32(FIELD_GET(GENMASK(30, 25), val), 5);
	c->r[1] = sign_extend32(FIELD_GET(GENMASK(22, 17), val), 5);
	c->r[2] = sign_extend32(FIELD_GET(GENMASK(14, 7), val), 5);

	return 0;
}

static int yas537_get_calibration_data(struct yas5xx *yas5xx)
{
	struct yas5xx_calibration *c = &yas5xx->calibration;
	u8 data[17];
	u32 val1, val2, val3, val4;
	int i, ret;

	/* Writing SRST register */
	ret = regmap_write(yas5xx->map, YAS537_SRST, BIT(1));
	if (ret)
		return ret;

	/* Calibration readout, YAS537 needs one readout only */
	ret = regmap_bulk_read(yas5xx->map, YAS537_CAL, data, sizeof(data));
	if (ret)
		return ret;
	dev_dbg(yas5xx->dev, "calibration data: %17ph\n", data);

	/* Sanity check, is this all zeroes? */
	if (!memchr_inv(data, 0x00, 16) && !FIELD_GET(GENMASK(5, 0), data[16]))
		dev_warn(yas5xx->dev, "calibration is blank!\n");

	/* Contribute calibration data to the input pool for kernel entropy */
	add_device_randomness(data, sizeof(data));

	/* Extract version information */
	yas5xx->version = FIELD_GET(GENMASK(7, 6), data[16]);

	/* There are two versions of YAS537 behaving differently */
	switch (yas5xx->version) {
	case YAS537_VERSION_0:
		/*
		 * The first version simply writes data back into registers:
		 *
		 * data[0]  YAS537_MTC		0x93
		 * data[1]			0x94
		 * data[2]			0x95
		 * data[3]			0x96
		 * data[4]			0x97
		 * data[5]			0x98
		 * data[6]			0x99
		 * data[7]			0x9a
		 * data[8]			0x9b
		 * data[9]			0x9c
		 * data[10]			0x9d
		 * data[11] YAS537_OC		0x9e
		 *
		 * data[12] YAS537_OFFSET_X	0x84
		 * data[13] YAS537_OFFSET_Y1	0x85
		 * data[14] YAS537_OFFSET_Y2	0x86
		 *
		 * data[15] YAS537_HCK		0x88
		 * data[16] YAS537_LCK		0x89
		 */
		for (i = 0; i < 12; i++) {
			ret = regmap_write(yas5xx->map, YAS537_MTC + i,
					   data[i]);
			if (ret)
				return ret;
		}
		for (i = 0; i < 3; i++) {
			ret = regmap_write(yas5xx->map, YAS537_OFFSET_X + i,
					   data[i + 12]);
			if (ret)
				return ret;
			yas5xx->hard_offsets[i] = data[i + 12];
		}
		for (i = 0; i < 2; i++) {
			ret = regmap_write(yas5xx->map, YAS537_HCK + i,
					   data[i + 15]);
			if (ret)
				return ret;
		}
		break;
	case YAS537_VERSION_1:
		/*
		 * The second version writes some data into registers but also
		 * extracts calibration coefficients.
		 *
		 * Registers being written:
		 *
		 * data[0]  YAS537_MTC			0x93
		 * data[1]  YAS537_MTC+1		0x94
		 * data[2]  YAS537_MTC+2		0x95
		 * data[3]  YAS537_MTC+3 (partially)	0x96
		 *
		 * data[12] YAS537_OFFSET_X		0x84
		 * data[13] YAS537_OFFSET_Y1		0x85
		 * data[14] YAS537_OFFSET_Y2		0x86
		 *
		 * data[15] YAS537_HCK (partially)	0x88
		 *          YAS537_LCK (partially)	0x89
		 * data[16] YAS537_OC  (partially)	0x9e
		 */
		for (i = 0; i < 3; i++) {
			ret = regmap_write(yas5xx->map, YAS537_MTC + i,
					   data[i]);
			if (ret)
				return ret;
		}
		for (i = 0; i < 3; i++) {
			ret = regmap_write(yas5xx->map, YAS537_OFFSET_X + i,
					   data[i + 12]);
			if (ret)
				return ret;
			yas5xx->hard_offsets[i] = data[i + 12];
		}
		/*
		 * Visualization of partially taken data:
		 *
		 * data[3]       n 7 6 5 4 3 2 1 0
		 * YAS537_MTC+3    x x x 1 0 0 0 0
		 *
		 * data[15]      n 7 6 5 4 3 2 1 0
		 * YAS537_HCK      x x x x 0
		 *
		 * data[15]      n 7 6 5 4 3 2 1 0
		 * YAS537_LCK              x x x x 0
		 *
		 * data[16]      n 7 6 5 4 3 2 1 0
		 * YAS537_OC           x x x x x x
		 */
		ret = regmap_write(yas5xx->map, YAS537_MTC + 3,
				   FIELD_PREP(YAS537_MTC3_MASK_PREP,
				   FIELD_GET(YAS537_MTC3_MASK_GET, data[3])) |
				   YAS537_MTC3_ADD_BIT);
		if (ret)
			return ret;
		ret = regmap_write(yas5xx->map, YAS537_HCK,
				   FIELD_PREP(YAS537_HCK_MASK_PREP,
				   FIELD_GET(YAS537_HCK_MASK_GET, data[15])));
		if (ret)
			return ret;
		ret = regmap_write(yas5xx->map, YAS537_LCK,
				   FIELD_PREP(YAS537_LCK_MASK_PREP,
				   FIELD_GET(YAS537_LCK_MASK_GET, data[15])));
		if (ret)
			return ret;
		ret = regmap_write(yas5xx->map, YAS537_OC,
				   FIELD_GET(YAS537_OC_MASK_GET, data[16]));
		if (ret)
			return ret;
		/*
		 * For data extraction, build some blocks. Four 32-bit blocks
		 * look appropriate.
		 *
		 *            n    7  6  5  4  3  2  1  0
		 *  data[0]   0 [ Cx Cx Cx Cx Cx Cx Cx Cx ] bits 31 .. 24
		 *  data[1]   1 [ Cx C1 C1 C1 C1 C1 C1 C1 ] bits 23 .. 16
		 *  data[2]   2 [ C1 C1 C2 C2 C2 C2 C2 C2 ] bits 15 .. 8
		 *  data[3]   3 [ C2 C2 C2                ] bits  7 .. 0
		 *
		 *            n    7  6  5  4  3  2  1  0
		 *  data[3]   0 [          a2 a2 a2 a2 a2 ] bits 31 .. 24
		 *  data[4]   1 [ a2 a2 a3 a3 a3 a3 a3 a3 ] bits 23 .. 16
		 *  data[5]   2 [ a3 a4 a4 a4 a4 a4 a4 a4 ] bits 15 .. 8
		 *  data[6]   3 [ a4                      ] bits  7 .. 0
		 *
		 *            n    7  6  5  4  3  2  1  0
		 *  data[6]   0 [    a5 a5 a5 a5 a5 a5 a5 ] bits 31 .. 24
		 *  data[7]   1 [ a5 a5 a6 a6 a6 a6 a6 a6 ] bits 23 .. 16
		 *  data[8]   2 [ a6 a7 a7 a7 a7 a7 a7 a7 ] bits 15 .. 8
		 *  data[9]   3 [ a7                      ] bits  7 .. 0
		 *
		 *            n    7  6  5  4  3  2  1  0
		 *  data[9]   0 [    a8 a8 a8 a8 a8 a8 a8 ] bits 31 .. 24
		 *  data[10]  1 [ a9 a9 a9 a9 a9 a9 a9 a9 ] bits 23 .. 16
		 *  data[11]  2 [ a9  k  k  k  k  k  k  k ] bits 15 .. 8
		 *  data[12]  3 [                         ] bits  7 .. 0
		 */
		val1 = get_unaligned_be32(&data[0]);
		val2 = get_unaligned_be32(&data[3]);
		val3 = get_unaligned_be32(&data[6]);
		val4 = get_unaligned_be32(&data[9]);
		/* Extract calibration coefficients and modify */
		c->Cx  = FIELD_GET(GENMASK(31, 23), val1) - 256;
		c->Cy1 = FIELD_GET(GENMASK(22, 14), val1) - 256;
		c->Cy2 = FIELD_GET(GENMASK(13,  5), val1) - 256;
		c->a2  = FIELD_GET(GENMASK(28, 22), val2) -  64;
		c->a3  = FIELD_GET(GENMASK(21, 15), val2) -  64;
		c->a4  = FIELD_GET(GENMASK(14,  7), val2) - 128;
		c->a5  = FIELD_GET(GENMASK(30, 22), val3) - 112;
		c->a6  = FIELD_GET(GENMASK(21, 15), val3) -  64;
		c->a7  = FIELD_GET(GENMASK(14,  7), val3) - 128;
		c->a8  = FIELD_GET(GENMASK(30, 24), val4) -  64;
		c->a9  = FIELD_GET(GENMASK(23, 15), val4) - 112;
		c->k   = FIELD_GET(GENMASK(14,  8), val4);
		break;
	default:
		dev_err(yas5xx->dev, "unknown version of YAS537\n");
		return -EINVAL;
	}

	return 0;
}

/* Used by YAS530, YAS532 and YAS533 */
static void yas530_dump_calibration(struct yas5xx *yas5xx)
{
	struct yas5xx_calibration *c = &yas5xx->calibration;

	dev_dbg(yas5xx->dev, "f[] = [%d, %d, %d]\n",
		c->f[0], c->f[1], c->f[2]);
	dev_dbg(yas5xx->dev, "r[] = [%d, %d, %d]\n",
		c->r[0], c->r[1], c->r[2]);
	dev_dbg(yas5xx->dev, "Cx = %d\n", c->Cx);
	dev_dbg(yas5xx->dev, "Cy1 = %d\n", c->Cy1);
	dev_dbg(yas5xx->dev, "Cy2 = %d\n", c->Cy2);
	dev_dbg(yas5xx->dev, "a2 = %d\n", c->a2);
	dev_dbg(yas5xx->dev, "a3 = %d\n", c->a3);
	dev_dbg(yas5xx->dev, "a4 = %d\n", c->a4);
	dev_dbg(yas5xx->dev, "a5 = %d\n", c->a5);
	dev_dbg(yas5xx->dev, "a6 = %d\n", c->a6);
	dev_dbg(yas5xx->dev, "a7 = %d\n", c->a7);
	dev_dbg(yas5xx->dev, "a8 = %d\n", c->a8);
	dev_dbg(yas5xx->dev, "a9 = %d\n", c->a9);
	dev_dbg(yas5xx->dev, "k = %d\n", c->k);
	dev_dbg(yas5xx->dev, "dck = %d\n", c->dck);
}

static void yas537_dump_calibration(struct yas5xx *yas5xx)
{
	struct yas5xx_calibration *c = &yas5xx->calibration;

	if (yas5xx->version == YAS537_VERSION_1) {
		dev_dbg(yas5xx->dev, "Cx = %d\n", c->Cx);
		dev_dbg(yas5xx->dev, "Cy1 = %d\n", c->Cy1);
		dev_dbg(yas5xx->dev, "Cy2 = %d\n", c->Cy2);
		dev_dbg(yas5xx->dev, "a2 = %d\n", c->a2);
		dev_dbg(yas5xx->dev, "a3 = %d\n", c->a3);
		dev_dbg(yas5xx->dev, "a4 = %d\n", c->a4);
		dev_dbg(yas5xx->dev, "a5 = %d\n", c->a5);
		dev_dbg(yas5xx->dev, "a6 = %d\n", c->a6);
		dev_dbg(yas5xx->dev, "a7 = %d\n", c->a7);
		dev_dbg(yas5xx->dev, "a8 = %d\n", c->a8);
		dev_dbg(yas5xx->dev, "a9 = %d\n", c->a9);
		dev_dbg(yas5xx->dev, "k = %d\n", c->k);
	}
}

/* Used by YAS530, YAS532 and YAS533 */
static int yas530_set_offsets(struct yas5xx *yas5xx, s8 ox, s8 oy1, s8 oy2)
{
	int ret;

	ret = regmap_write(yas5xx->map, YAS530_OFFSET_X, ox);
	if (ret)
		return ret;
	ret = regmap_write(yas5xx->map, YAS530_OFFSET_Y1, oy1);
	if (ret)
		return ret;
	return regmap_write(yas5xx->map, YAS530_OFFSET_Y2, oy2);
}

/* Used by YAS530, YAS532 and YAS533 */
static s8 yas530_adjust_offset(s8 old, int bit, u16 center, u16 measure)
{
	if (measure > center)
		return old + BIT(bit);
	if (measure < center)
		return old - BIT(bit);
	return old;
}

/* Used by YAS530, YAS532 and YAS533 */
static int yas530_measure_offsets(struct yas5xx *yas5xx)
{
	const struct yas5xx_chip_info *ci = yas5xx->chip_info;
	int ret;
	u16 center;
	u16 t, x, y1, y2;
	s8 ox, oy1, oy2;
	int i;

	/* Actuate the init coil and measure offsets */
	ret = regmap_write(yas5xx->map, YAS530_ACTUATE_INIT_COIL, 0);
	if (ret)
		return ret;

	/* When the initcoil is active this should be around the center */
	switch (ci->devid) {
	case YAS530_DEVICE_ID:
		center = YAS530_DATA_CENTER;
		break;
	case YAS532_DEVICE_ID:
		center = YAS532_DATA_CENTER;
		break;
	default:
		dev_err(yas5xx->dev, "unknown device type\n");
		return -EINVAL;
	}

	/*
	 * We set offsets in the interval +-31 by iterating
	 * +-16, +-8, +-4, +-2, +-1 adjusting the offsets each
	 * time, then writing the final offsets into the
	 * registers.
	 *
	 * NOTE: these offsets are NOT in the same unit or magnitude
	 * as the values for [x, y1, y2]. The value is +/-31
	 * but the effect on the raw values is much larger.
	 * The effect of the offset is to bring the measure
	 * rougly to the center.
	 */
	ox = 0;
	oy1 = 0;
	oy2 = 0;

	for (i = 4; i >= 0; i--) {
		ret = yas530_set_offsets(yas5xx, ox, oy1, oy2);
		if (ret)
			return ret;

		ret = yas530_measure(yas5xx, &t, &x, &y1, &y2);
		if (ret)
			return ret;
		dev_dbg(yas5xx->dev, "measurement %d: x=%d, y1=%d, y2=%d\n",
			5-i, x, y1, y2);

		ox = yas530_adjust_offset(ox, i, center, x);
		oy1 = yas530_adjust_offset(oy1, i, center, y1);
		oy2 = yas530_adjust_offset(oy2, i, center, y2);
	}

	/* Needed for calibration algorithm */
	yas5xx->hard_offsets[0] = ox;
	yas5xx->hard_offsets[1] = oy1;
	yas5xx->hard_offsets[2] = oy2;
	ret = yas530_set_offsets(yas5xx, ox, oy1, oy2);
	if (ret)
		return ret;

	dev_info(yas5xx->dev, "discovered hard offsets: x=%d, y1=%d, y2=%d\n",
		 ox, oy1, oy2);
	return 0;
}

/* Used by YAS530, YAS532 and YAS533 */
static int yas530_power_on(struct yas5xx *yas5xx)
{
	unsigned int val;
	int ret;

	/* Zero the test registers */
	ret = regmap_write(yas5xx->map, YAS530_TEST1, 0);
	if (ret)
		return ret;
	ret = regmap_write(yas5xx->map, YAS530_TEST2, 0);
	if (ret)
		return ret;

	/* Set up for no interrupts, calibrated clock divider */
	val = FIELD_PREP(YAS5XX_CONFIG_CCK_MASK, yas5xx->calibration.dck);
	ret = regmap_write(yas5xx->map, YAS530_CONFIG, val);
	if (ret)
		return ret;

	/* Measure interval 0 (back-to-back?)  */
	return regmap_write(yas5xx->map, YAS530_MEASURE_INTERVAL, 0);
}

static int yas537_power_on(struct yas5xx *yas5xx)
{
	__be16 buf;
	int ret;
	u8 intrvl;

	/* Writing ADCCAL and TRM registers */
	buf = cpu_to_be16(GENMASK(9, 3));
	ret = regmap_bulk_write(yas5xx->map, YAS537_ADCCAL, &buf, sizeof(buf));
	if (ret)
		return ret;
	ret = regmap_write(yas5xx->map, YAS537_TRM, GENMASK(7, 0));
	if (ret)
		return ret;

	/* The interval value is static in regular operation */
	intrvl = (YAS537_DEFAULT_SENSOR_DELAY_MS * MILLI
		 - YAS537_MEASURE_TIME_WORST_US) / 4100;
	ret = regmap_write(yas5xx->map, YAS537_MEASURE_INTERVAL, intrvl);
	if (ret)
		return ret;

	/* The average value is also static in regular operation */
	ret = regmap_write(yas5xx->map, YAS537_AVR, YAS537_MAG_AVERAGE_32_MASK);
	if (ret)
		return ret;

	/* Perform the "rcoil" part but skip the "last_after_rcoil" read */
	ret = regmap_write(yas5xx->map, YAS537_CONFIG, BIT(3));
	if (ret)
		return ret;

	/* Wait until the coil has ramped up */
	usleep_range(YAS537_MAG_RCOIL_TIME_US, YAS537_MAG_RCOIL_TIME_US + 100);

	return 0;
}

static const struct yas5xx_chip_info yas5xx_chip_info_tbl[] = {
	[yas530] = {
		.devid = YAS530_DEVICE_ID,
		.product_name = "YAS530 MS-3E",
		.version_names = { "A", "B" },
		.volatile_reg = yas530_volatile_reg,
		.volatile_reg_qty = ARRAY_SIZE(yas530_volatile_reg),
		.scaling_val2 = 100000000, /* picotesla to Gauss */
		.t_ref = 182, /* counts */
		.min_temp_x10 = -620, /* 1/10:s degrees Celsius */
		.get_measure = yas530_get_measure,
		.get_calibration_data = yas530_get_calibration_data,
		.dump_calibration = yas530_dump_calibration,
		.measure_offsets = yas530_measure_offsets,
		.power_on = yas530_power_on,
	},
	[yas532] = {
		.devid = YAS532_DEVICE_ID,
		.product_name = "YAS532 MS-3R",
		.version_names = { "AB", "AC" },
		.volatile_reg = yas530_volatile_reg,
		.volatile_reg_qty = ARRAY_SIZE(yas530_volatile_reg),
		.scaling_val2 = 100000, /* nanotesla to Gauss */
		.t_ref = 390, /* counts */
		.min_temp_x10 = -500, /* 1/10:s degrees Celsius */
		.get_measure = yas530_get_measure,
		.get_calibration_data = yas532_get_calibration_data,
		.dump_calibration = yas530_dump_calibration,
		.measure_offsets = yas530_measure_offsets,
		.power_on = yas530_power_on,
	},
	[yas533] = {
		.devid = YAS532_DEVICE_ID,
		.product_name = "YAS533 MS-3F",
		.version_names = { "AB", "AC" },
		.volatile_reg = yas530_volatile_reg,
		.volatile_reg_qty = ARRAY_SIZE(yas530_volatile_reg),
		.scaling_val2 = 100000, /* nanotesla to Gauss */
		.t_ref = 390, /* counts */
		.min_temp_x10 = -500, /* 1/10:s degrees Celsius */
		.get_measure = yas530_get_measure,
		.get_calibration_data = yas532_get_calibration_data,
		.dump_calibration = yas530_dump_calibration,
		.measure_offsets = yas530_measure_offsets,
		.power_on = yas530_power_on,
	},
	[yas537] = {
		.devid = YAS537_DEVICE_ID,
		.product_name = "YAS537 MS-3T",
		.version_names = { "v0", "v1" }, /* version naming unknown */
		.volatile_reg = yas537_volatile_reg,
		.volatile_reg_qty = ARRAY_SIZE(yas537_volatile_reg),
		.scaling_val2 = 100000, /* nanotesla to Gauss */
		.t_ref = 8120, /* counts */
		.min_temp_x10 = -3860, /* 1/10:s degrees Celsius */
		.get_measure = yas537_get_measure,
		.get_calibration_data = yas537_get_calibration_data,
		.dump_calibration = yas537_dump_calibration,
		/* .measure_offets is not needed for yas537 */
		.power_on = yas537_power_on,
	},
};

static int yas5xx_probe(struct i2c_client *i2c)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(i2c);
	struct iio_dev *indio_dev;
	struct device *dev = &i2c->dev;
	struct yas5xx *yas5xx;
	const struct yas5xx_chip_info *ci;
	int id_check;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*yas5xx));
	if (!indio_dev)
		return -ENOMEM;

	yas5xx = iio_priv(indio_dev);
	i2c_set_clientdata(i2c, indio_dev);
	yas5xx->dev = dev;
	mutex_init(&yas5xx->lock);

	ret = iio_read_mount_matrix(dev, &yas5xx->orientation);
	if (ret)
		return ret;

	yas5xx->regs[0].supply = "vdd";
	yas5xx->regs[1].supply = "iovdd";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(yas5xx->regs),
				      yas5xx->regs);
	if (ret)
		return dev_err_probe(dev, ret, "cannot get regulators\n");

	ret = regulator_bulk_enable(ARRAY_SIZE(yas5xx->regs), yas5xx->regs);
	if (ret)
		return dev_err_probe(dev, ret, "cannot enable regulators\n");

	/* See comment in runtime resume callback */
	usleep_range(31000, 40000);

	/* This will take the device out of reset if need be */
	yas5xx->reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(yas5xx->reset)) {
		ret = dev_err_probe(dev, PTR_ERR(yas5xx->reset), "failed to get reset line\n");
		goto reg_off;
	}

	yas5xx->map = devm_regmap_init_i2c(i2c, &yas5xx_regmap_config);
	if (IS_ERR(yas5xx->map)) {
		ret = dev_err_probe(dev, PTR_ERR(yas5xx->map), "failed to allocate register map\n");
		goto assert_reset;
	}

	ci = i2c_get_match_data(i2c);
	yas5xx->chip_info = ci;

	ret = regmap_read(yas5xx->map, YAS5XX_DEVICE_ID, &id_check);
	if (ret)
		goto assert_reset;

	if (id_check != ci->devid) {
		ret = dev_err_probe(dev, -ENODEV,
				    "device ID %02x doesn't match %s\n",
				    id_check, id->name);
		goto assert_reset;
	}

	ret = ci->get_calibration_data(yas5xx);
	if (ret)
		goto assert_reset;

	dev_info(dev, "detected %s %s\n", ci->product_name,
		 ci->version_names[yas5xx->version]);

	ci->dump_calibration(yas5xx);

	ret = ci->power_on(yas5xx);
	if (ret)
		goto assert_reset;

	if (ci->measure_offsets) {
		ret = ci->measure_offsets(yas5xx);
		if (ret)
			goto assert_reset;
	}

	indio_dev->info = &yas5xx_info;
	indio_dev->available_scan_masks = yas5xx_scan_masks;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->name = id->name;
	indio_dev->channels = yas5xx_channels;
	indio_dev->num_channels = ARRAY_SIZE(yas5xx_channels);

	ret = iio_triggered_buffer_setup(indio_dev, NULL,
					 yas5xx_handle_trigger,
					 NULL);
	if (ret) {
		dev_err_probe(dev, ret, "triggered buffer setup failed\n");
		goto assert_reset;
	}

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err_probe(dev, ret, "device register failed\n");
		goto cleanup_buffer;
	}

	/* Take runtime PM online */
	pm_runtime_get_noresume(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	pm_runtime_set_autosuspend_delay(dev, YAS5XX_AUTOSUSPEND_DELAY_MS);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_put(dev);

	return 0;

cleanup_buffer:
	iio_triggered_buffer_cleanup(indio_dev);
assert_reset:
	gpiod_set_value_cansleep(yas5xx->reset, 1);
reg_off:
	regulator_bulk_disable(ARRAY_SIZE(yas5xx->regs), yas5xx->regs);

	return ret;
}

static void yas5xx_remove(struct i2c_client *i2c)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(i2c);
	struct yas5xx *yas5xx = iio_priv(indio_dev);
	struct device *dev = &i2c->dev;

	iio_device_unregister(indio_dev);
	iio_triggered_buffer_cleanup(indio_dev);
	/*
	 * Now we can't get any more reads from the device, which would
	 * also call pm_runtime* functions and race with our disable
	 * code. Disable PM runtime in orderly fashion and power down.
	 */
	pm_runtime_get_sync(dev);
	pm_runtime_put_noidle(dev);
	pm_runtime_disable(dev);
	gpiod_set_value_cansleep(yas5xx->reset, 1);
	regulator_bulk_disable(ARRAY_SIZE(yas5xx->regs), yas5xx->regs);
}

static int yas5xx_runtime_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct yas5xx *yas5xx = iio_priv(indio_dev);

	gpiod_set_value_cansleep(yas5xx->reset, 1);
	regulator_bulk_disable(ARRAY_SIZE(yas5xx->regs), yas5xx->regs);

	return 0;
}

static int yas5xx_runtime_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct yas5xx *yas5xx = iio_priv(indio_dev);
	const struct yas5xx_chip_info *ci = yas5xx->chip_info;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(yas5xx->regs), yas5xx->regs);
	if (ret) {
		dev_err(dev, "cannot enable regulators\n");
		return ret;
	}

	/*
	 * The YAS530 datasheet says TVSKW is up to 30 ms, after that 1 ms
	 * for all voltages to settle. The YAS532 is 10ms then 4ms for the
	 * I2C to come online. Let's keep it safe and put this at 31ms.
	 */
	usleep_range(31000, 40000);
	gpiod_set_value_cansleep(yas5xx->reset, 0);

	ret = ci->power_on(yas5xx);
	if (ret) {
		dev_err(dev, "cannot power on\n");
		goto out_reset;
	}

	return 0;

out_reset:
	gpiod_set_value_cansleep(yas5xx->reset, 1);
	regulator_bulk_disable(ARRAY_SIZE(yas5xx->regs), yas5xx->regs);

	return ret;
}

static DEFINE_RUNTIME_DEV_PM_OPS(yas5xx_dev_pm_ops, yas5xx_runtime_suspend,
				 yas5xx_runtime_resume, NULL);

static const struct i2c_device_id yas5xx_id[] = {
	{"yas530", (kernel_ulong_t)&yas5xx_chip_info_tbl[yas530] },
	{"yas532", (kernel_ulong_t)&yas5xx_chip_info_tbl[yas532] },
	{"yas533", (kernel_ulong_t)&yas5xx_chip_info_tbl[yas533] },
	{"yas537", (kernel_ulong_t)&yas5xx_chip_info_tbl[yas537] },
	{ }
};
MODULE_DEVICE_TABLE(i2c, yas5xx_id);

static const struct of_device_id yas5xx_of_match[] = {
	{ .compatible = "yamaha,yas530", &yas5xx_chip_info_tbl[yas530] },
	{ .compatible = "yamaha,yas532", &yas5xx_chip_info_tbl[yas532] },
	{ .compatible = "yamaha,yas533", &yas5xx_chip_info_tbl[yas533] },
	{ .compatible = "yamaha,yas537", &yas5xx_chip_info_tbl[yas537] },
	{ }
};
MODULE_DEVICE_TABLE(of, yas5xx_of_match);

static struct i2c_driver yas5xx_driver = {
	.driver	 = {
		.name	= "yas5xx",
		.of_match_table = yas5xx_of_match,
		.pm = pm_ptr(&yas5xx_dev_pm_ops),
	},
	.probe = yas5xx_probe,
	.remove	  = yas5xx_remove,
	.id_table = yas5xx_id,
};
module_i2c_driver(yas5xx_driver);

MODULE_DESCRIPTION("Yamaha YAS53x 3-axis magnetometer driver");
MODULE_AUTHOR("Linus Walleij");
MODULE_LICENSE("GPL v2");
