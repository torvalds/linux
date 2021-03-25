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
 * YAS537 MS-3T (2015 Samsung Galaxy S6, Note 5, Xiaomi)
 * YAS539 MS-3S (2018 Samsung Galaxy A7 SM-A750FN)
 *
 * Code functions found in the MPU3050 YAS530 and YAS532 drivers
 * named "inv_compass" in the Tegra Android kernel tree.
 * Copyright (C) 2012 InvenSense Corporation
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
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/random.h>
#include <linux/unaligned/be_byteshift.h>

#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

/* This register map covers YAS530 and YAS532 but differs in YAS 537 and YAS539 */
#define YAS5XX_DEVICE_ID		0x80
#define YAS5XX_ACTUATE_INIT_COIL	0x81
#define YAS5XX_MEASURE			0x82
#define YAS5XX_CONFIG			0x83
#define YAS5XX_MEASURE_INTERVAL		0x84
#define YAS5XX_OFFSET_X			0x85 /* [-31 .. 31] */
#define YAS5XX_OFFSET_Y1		0x86 /* [-31 .. 31] */
#define YAS5XX_OFFSET_Y2		0x87 /* [-31 .. 31] */
#define YAS5XX_TEST1			0x88
#define YAS5XX_TEST2			0x89
#define YAS5XX_CAL			0x90
#define YAS5XX_MEASURE_DATA		0xB0

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
#define YAS532_20DEGREES		390 /* Looks like Kelvin */

/* These variant IDs are known from code dumps */
#define YAS537_DEVICE_ID		0x07 /* YAS537 (MS-3T) */
#define YAS539_DEVICE_ID		0x08 /* YAS539 (MS-3S) */

/* Turn off device regulators etc after 5 seconds of inactivity */
#define YAS5XX_AUTOSUSPEND_DELAY_MS	5000

struct yas5xx_calibration {
	/* Linearization calibration x, y1, y2 */
	s32 r[3];
	u32 f[3];
	/* Temperature compensation calibration */
	s32 Cx, Cy1, Cy2;
	/* Misc calibration coefficients */
	s32 a2, a3, a4, a5, a6, a7, a8, a9, k;
	/* clock divider */
	u8 dck;
};

/**
 * struct yas5xx - state container for the YAS5xx driver
 * @dev: parent device pointer
 * @devid: device ID number
 * @version: device version
 * @name: device name
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
	unsigned int devid;
	unsigned int version;
	char name[16];
	struct yas5xx_calibration calibration;
	u8 hard_offsets[3];
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
		s64 ts __aligned(8);
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
 * yas5xx_measure() - Make a measure from the hardware
 * @yas5xx: The device state
 * @t: the raw temperature measurement
 * @x: the raw x axis measurement
 * @y1: the y1 axis measurement
 * @y2: the y2 axis measurement
 * @return: 0 on success or error code
 */
static int yas5xx_measure(struct yas5xx *yas5xx, u16 *t, u16 *x, u16 *y1, u16 *y2)
{
	unsigned int busy;
	u8 data[8];
	int ret;
	u16 val;

	mutex_lock(&yas5xx->lock);
	ret = regmap_write(yas5xx->map, YAS5XX_MEASURE, YAS5XX_MEASURE_START);
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

	switch (yas5xx->devid) {
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

static s32 yas5xx_linearize(struct yas5xx *yas5xx, u16 val, int axis)
{
	struct yas5xx_calibration *c = &yas5xx->calibration;
	static const s32 yas532ac_coef[] = {
		YAS532_VERSION_AC_COEF_X,
		YAS532_VERSION_AC_COEF_Y1,
		YAS532_VERSION_AC_COEF_Y2,
	};
	s32 coef;

	/* Select coefficients */
	switch (yas5xx->devid) {
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

/**
 * yas5xx_get_measure() - Measure a sample of all axis and process
 * @yas5xx: The device state
 * @to: Temperature out
 * @xo: X axis out
 * @yo: Y axis out
 * @zo: Z axis out
 * @return: 0 on success or error code
 *
 * Returned values are in nanotesla according to some code.
 */
static int yas5xx_get_measure(struct yas5xx *yas5xx, s32 *to, s32 *xo, s32 *yo, s32 *zo)
{
	struct yas5xx_calibration *c = &yas5xx->calibration;
	u16 t, x, y1, y2;
	/* These are "signed x, signed y1 etc */
	s32 sx, sy1, sy2, sy, sz;
	int ret;

	/* We first get raw data that needs to be translated to [x,y,z] */
	ret = yas5xx_measure(yas5xx, &t, &x, &y1, &y2);
	if (ret)
		return ret;

	/* Do some linearization if available */
	sx = yas5xx_linearize(yas5xx, x, 0);
	sy1 = yas5xx_linearize(yas5xx, y1, 1);
	sy2 = yas5xx_linearize(yas5xx, y2, 2);

	/*
	 * Temperature compensation for x, y1, y2 respectively:
	 *
	 *          Cx * t
	 * x' = x - ------
	 *           100
	 */
	sx = sx - (c->Cx * t) / 100;
	sy1 = sy1 - (c->Cy1 * t) / 100;
	sy2 = sy2 - (c->Cy2 * t) / 100;

	/*
	 * Break y1 and y2 into y and z, y1 and y2 are apparently encoding
	 * y and z.
	 */
	sy = sy1 - sy2;
	sz = -sy1 - sy2;

	/*
	 * FIXME: convert to Celsius? Just guessing this is given
	 * as 1/10:s of degrees so multiply by 100 to get millicentigrades.
	 */
	*to = t * 100;
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

static int yas5xx_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2,
			   long mask)
{
	struct yas5xx *yas5xx = iio_priv(indio_dev);
	s32 t, x, y, z;
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		pm_runtime_get_sync(yas5xx->dev);
		ret = yas5xx_get_measure(yas5xx, &t, &x, &y, &z);
		pm_runtime_mark_last_busy(yas5xx->dev);
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
		if (chan->address == 0) {
			/* Temperature is unscaled */
			*val = 1;
			return IIO_VAL_INT;
		}
		/*
		 * The axis values are in nanotesla according to the vendor
		 * drivers, but is clearly in microtesla according to
		 * experiments. Since 1 uT = 0.01 Gauss, we need to divide
		 * by 100000000 (10^8) to get to Gauss from the raw value.
		 */
		*val = 1;
		*val2 = 100000000;
		return IIO_VAL_FRACTIONAL;
	default:
		/* Unknown request */
		return -EINVAL;
	}
}

static void yas5xx_fill_buffer(struct iio_dev *indio_dev)
{
	struct yas5xx *yas5xx = iio_priv(indio_dev);
	s32 t, x, y, z;
	int ret;

	pm_runtime_get_sync(yas5xx->dev);
	ret = yas5xx_get_measure(yas5xx, &t, &x, &y, &z);
	pm_runtime_mark_last_busy(yas5xx->dev);
	pm_runtime_put_autosuspend(yas5xx->dev);
	if (ret) {
		dev_err(yas5xx->dev, "error refilling buffer\n");
		return;
	}
	yas5xx->scan.channels[0] = t;
	yas5xx->scan.channels[1] = x;
	yas5xx->scan.channels[2] = y;
	yas5xx->scan.channels[3] = z;
	iio_push_to_buffers_with_timestamp(indio_dev, &yas5xx->scan,
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
			.sign = 'u',
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
	return reg == YAS5XX_ACTUATE_INIT_COIL ||
		reg == YAS5XX_MEASURE ||
		(reg >= YAS5XX_MEASURE_DATA && reg <= YAS5XX_MEASURE_DATA + 8);
}

/* TODO: enable regmap cache, using mark dirty and sync at runtime resume */
static const struct regmap_config yas5xx_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xff,
	.volatile_reg = yas5xx_volatile_reg,
};

/**
 * yas53x_extract_calibration() - extracts the a2-a9 and k calibration
 * @data: the bitfield to use
 * @c: the calibration to populate
 */
static void yas53x_extract_calibration(u8 *data, struct yas5xx_calibration *c)
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
	ret = regmap_bulk_read(yas5xx->map, YAS5XX_CAL, data, sizeof(data));
	if (ret)
		return ret;

	/* Actual calibration readout */
	ret = regmap_bulk_read(yas5xx->map, YAS5XX_CAL, data, sizeof(data));
	if (ret)
		return ret;
	dev_dbg(yas5xx->dev, "calibration data: %*ph\n", 14, data);

	add_device_randomness(data, sizeof(data));
	yas5xx->version = data[15] & GENMASK(1, 0);

	/* Extract the calibration from the bitfield */
	c->Cx = data[0] * 6 - 768;
	c->Cy1 = data[1] * 6 - 768;
	c->Cy2 = data[2] * 6 - 768;
	yas53x_extract_calibration(&data[3], c);

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
	ret = regmap_bulk_read(yas5xx->map, YAS5XX_CAL, data, sizeof(data));
	if (ret)
		return ret;
	/* Actual calibration readout */
	ret = regmap_bulk_read(yas5xx->map, YAS5XX_CAL, data, sizeof(data));
	if (ret)
		return ret;
	dev_dbg(yas5xx->dev, "calibration data: %*ph\n", 14, data);

	/* Sanity check, is this all zeroes? */
	if (memchr_inv(data, 0x00, 13)) {
		if (!(data[13] & BIT(7)))
			dev_warn(yas5xx->dev, "calibration is blank!\n");
	}

	add_device_randomness(data, sizeof(data));
	/* Only one bit of version info reserved here as far as we know */
	yas5xx->version = data[13] & BIT(0);

	/* Extract calibration from the bitfield */
	c->Cx = data[0] * 10 - 1280;
	c->Cy1 = data[1] * 10 - 1280;
	c->Cy2 = data[2] * 10 - 1280;
	yas53x_extract_calibration(&data[3], c);
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

static void yas5xx_dump_calibration(struct yas5xx *yas5xx)
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

static int yas5xx_set_offsets(struct yas5xx *yas5xx, s8 ox, s8 oy1, s8 oy2)
{
	int ret;

	ret = regmap_write(yas5xx->map, YAS5XX_OFFSET_X, ox);
	if (ret)
		return ret;
	ret = regmap_write(yas5xx->map, YAS5XX_OFFSET_Y1, oy1);
	if (ret)
		return ret;
	return regmap_write(yas5xx->map, YAS5XX_OFFSET_Y2, oy2);
}

static s8 yas5xx_adjust_offset(s8 old, int bit, u16 center, u16 measure)
{
	if (measure > center)
		return old + BIT(bit);
	if (measure < center)
		return old - BIT(bit);
	return old;
}

static int yas5xx_meaure_offsets(struct yas5xx *yas5xx)
{
	int ret;
	u16 center;
	u16 t, x, y1, y2;
	s8 ox, oy1, oy2;
	int i;

	/* Actuate the init coil and measure offsets */
	ret = regmap_write(yas5xx->map, YAS5XX_ACTUATE_INIT_COIL, 0);
	if (ret)
		return ret;

	/* When the initcoil is active this should be around the center */
	switch (yas5xx->devid) {
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
		ret = yas5xx_set_offsets(yas5xx, ox, oy1, oy2);
		if (ret)
			return ret;

		ret = yas5xx_measure(yas5xx, &t, &x, &y1, &y2);
		if (ret)
			return ret;
		dev_dbg(yas5xx->dev, "measurement %d: x=%d, y1=%d, y2=%d\n",
			5-i, x, y1, y2);

		ox = yas5xx_adjust_offset(ox, i, center, x);
		oy1 = yas5xx_adjust_offset(oy1, i, center, y1);
		oy2 = yas5xx_adjust_offset(oy2, i, center, y2);
	}

	/* Needed for calibration algorithm */
	yas5xx->hard_offsets[0] = ox;
	yas5xx->hard_offsets[1] = oy1;
	yas5xx->hard_offsets[2] = oy2;
	ret = yas5xx_set_offsets(yas5xx, ox, oy1, oy2);
	if (ret)
		return ret;

	dev_info(yas5xx->dev, "discovered hard offsets: x=%d, y1=%d, y2=%d\n",
		 ox, oy1, oy2);
	return 0;
}

static int yas5xx_power_on(struct yas5xx *yas5xx)
{
	unsigned int val;
	int ret;

	/* Zero the test registers */
	ret = regmap_write(yas5xx->map, YAS5XX_TEST1, 0);
	if (ret)
		return ret;
	ret = regmap_write(yas5xx->map, YAS5XX_TEST2, 0);
	if (ret)
		return ret;

	/* Set up for no interrupts, calibrated clock divider */
	val = FIELD_PREP(YAS5XX_CONFIG_CCK_MASK, yas5xx->calibration.dck);
	ret = regmap_write(yas5xx->map, YAS5XX_CONFIG, val);
	if (ret)
		return ret;

	/* Measure interval 0 (back-to-back?)  */
	return regmap_write(yas5xx->map, YAS5XX_MEASURE_INTERVAL, 0);
}

static int yas5xx_probe(struct i2c_client *i2c,
			const struct i2c_device_id *id)
{
	struct iio_dev *indio_dev;
	struct device *dev = &i2c->dev;
	struct yas5xx *yas5xx;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*yas5xx));
	if (!indio_dev)
		return -ENOMEM;

	yas5xx = iio_priv(indio_dev);
	i2c_set_clientdata(i2c, indio_dev);
	yas5xx->dev = dev;
	mutex_init(&yas5xx->lock);

	ret = iio_read_mount_matrix(dev, "mount-matrix", &yas5xx->orientation);
	if (ret)
		return ret;

	yas5xx->regs[0].supply = "vdd";
	yas5xx->regs[1].supply = "iovdd";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(yas5xx->regs),
				      yas5xx->regs);
	if (ret)
		return dev_err_probe(dev, ret, "cannot get regulators\n");

	ret = regulator_bulk_enable(ARRAY_SIZE(yas5xx->regs), yas5xx->regs);
	if (ret) {
		dev_err(dev, "cannot enable regulators\n");
		return ret;
	}

	/* See comment in runtime resume callback */
	usleep_range(31000, 40000);

	/* This will take the device out of reset if need be */
	yas5xx->reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(yas5xx->reset)) {
		ret = dev_err_probe(dev, PTR_ERR(yas5xx->reset),
				    "failed to get reset line\n");
		goto reg_off;
	}

	yas5xx->map = devm_regmap_init_i2c(i2c, &yas5xx_regmap_config);
	if (IS_ERR(yas5xx->map)) {
		dev_err(dev, "failed to allocate register map\n");
		ret = PTR_ERR(yas5xx->map);
		goto assert_reset;
	}

	ret = regmap_read(yas5xx->map, YAS5XX_DEVICE_ID, &yas5xx->devid);
	if (ret)
		goto assert_reset;

	switch (yas5xx->devid) {
	case YAS530_DEVICE_ID:
		ret = yas530_get_calibration_data(yas5xx);
		if (ret)
			goto assert_reset;
		dev_info(dev, "detected YAS530 MS-3E %s",
			 yas5xx->version ? "B" : "A");
		strncpy(yas5xx->name, "yas530", sizeof(yas5xx->name));
		break;
	case YAS532_DEVICE_ID:
		ret = yas532_get_calibration_data(yas5xx);
		if (ret)
			goto assert_reset;
		dev_info(dev, "detected YAS532/YAS533 MS-3R/F %s",
			 yas5xx->version ? "AC" : "AB");
		strncpy(yas5xx->name, "yas532", sizeof(yas5xx->name));
		break;
	default:
		dev_err(dev, "unhandled device ID %02x\n", yas5xx->devid);
		goto assert_reset;
	}

	yas5xx_dump_calibration(yas5xx);
	ret = yas5xx_power_on(yas5xx);
	if (ret)
		goto assert_reset;
	ret = yas5xx_meaure_offsets(yas5xx);
	if (ret)
		goto assert_reset;

	indio_dev->info = &yas5xx_info;
	indio_dev->available_scan_masks = yas5xx_scan_masks;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->name = yas5xx->name;
	indio_dev->channels = yas5xx_channels;
	indio_dev->num_channels = ARRAY_SIZE(yas5xx_channels);

	ret = iio_triggered_buffer_setup(indio_dev, NULL,
					 yas5xx_handle_trigger,
					 NULL);
	if (ret) {
		dev_err(dev, "triggered buffer setup failed\n");
		goto assert_reset;
	}

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err(dev, "device register failed\n");
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

static int yas5xx_remove(struct i2c_client *i2c)
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

	return 0;
}

static int __maybe_unused yas5xx_runtime_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct yas5xx *yas5xx = iio_priv(indio_dev);

	gpiod_set_value_cansleep(yas5xx->reset, 1);
	regulator_bulk_disable(ARRAY_SIZE(yas5xx->regs), yas5xx->regs);

	return 0;
}

static int __maybe_unused yas5xx_runtime_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct yas5xx *yas5xx = iio_priv(indio_dev);
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

	ret = yas5xx_power_on(yas5xx);
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

static const struct dev_pm_ops yas5xx_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(yas5xx_runtime_suspend,
			   yas5xx_runtime_resume, NULL)
};

static const struct i2c_device_id yas5xx_id[] = {
	{"yas530", },
	{"yas532", },
	{"yas533", },
	{}
};
MODULE_DEVICE_TABLE(i2c, yas5xx_id);

static const struct of_device_id yas5xx_of_match[] = {
	{ .compatible = "yamaha,yas530", },
	{ .compatible = "yamaha,yas532", },
	{ .compatible = "yamaha,yas533", },
	{}
};
MODULE_DEVICE_TABLE(of, yas5xx_of_match);

static struct i2c_driver yas5xx_driver = {
	.driver	 = {
		.name	= "yas5xx",
		.of_match_table = yas5xx_of_match,
		.pm = &yas5xx_dev_pm_ops,
	},
	.probe	  = yas5xx_probe,
	.remove	  = yas5xx_remove,
	.id_table = yas5xx_id,
};
module_i2c_driver(yas5xx_driver);

MODULE_DESCRIPTION("Yamaha YAS53x 3-axis magnetometer driver");
MODULE_AUTHOR("Linus Walleij");
MODULE_LICENSE("GPL v2");
