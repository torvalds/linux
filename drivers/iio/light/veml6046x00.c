// SPDX-License-Identifier: GPL-2.0+
/*
 * VEML6046X00 High Accuracy RGBIR Color Sensor
 *
 * Copyright (c) 2025 Andreas Klinger <ak@it-klinger.de>
 */

#include <linux/array_size.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/dev_printk.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/time.h>
#include <linux/types.h>
#include <linux/units.h>

#include <asm/byteorder.h>

#include <linux/iio/iio.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

/*
 * Device registers
 * Those which are accessed as bulk io are omitted
 */
#define VEML6046X00_REG_CONF0       0x00
#define VEML6046X00_REG_CONF1       0x01
#define VEML6046X00_REG_THDH        0x04
#define VEML6046X00_REG_THDL        0x06
#define VEML6046X00_REG_R           0x10
#define VEML6046X00_REG_G           0x12
#define VEML6046X00_REG_B           0x14
#define VEML6046X00_REG_IR          0x16
#define VEML6046X00_REG_ID          0x18
#define VEML6046X00_REG_INT         0x1A
#define VEML6046X00_REG_INT_H       0x1B

/* Bit masks for specific functionality */
#define VEML6046X00_CONF0_ON_0      BIT(0)
#define VEML6046X00_CONF0_INT       BIT(1)
#define VEML6046X00_CONF0_AF_TRIG   BIT(2)
#define VEML6046X00_CONF0_AF        BIT(3)
#define VEML6046X00_CONF0_IT        GENMASK(6, 4)
#define VEML6046X00_CONF1_CAL       BIT(0)
#define VEML6046X00_CONF1_PERS      GENMASK(2, 1)
#define VEML6046X00_CONF1_GAIN      GENMASK(4, 3)
#define VEML6046X00_CONF1_PD_D2     BIT(6)
#define VEML6046X00_CONF1_ON_1      BIT(7)
#define VEML6046X00_INT_TH_H        BIT(1)
#define VEML6046X00_INT_TH_L        BIT(2)
#define VEML6046X00_INT_DRDY        BIT(3)
#define VEML6046X00_INT_MASK						       \
	(VEML6046X00_INT_TH_H | VEML6046X00_INT_TH_L | VEML6046X00_INT_DRDY)

#define	VEML6046X00_GAIN_1          0x0
#define	VEML6046X00_GAIN_2          0x1
#define	VEML6046X00_GAIN_0_66       0x2
#define	VEML6046X00_GAIN_0_5        0x3

#define VEML6046X00_PD_2_2          0x0
#define VEML6046X00_PD_1_2          BIT(6)

/* Autosuspend delay */
#define VEML6046X00_AUTOSUSPEND_MS  (3 * MSEC_PER_SEC)

enum veml6046x00_scan {
	VEML6046X00_SCAN_R,
	VEML6046X00_SCAN_G,
	VEML6046X00_SCAN_B,
	VEML6046X00_SCAN_IR,
	VEML6046X00_SCAN_TIMESTAMP,
};

/**
 * struct veml6046x00_rf - Regmap field of configuration registers.
 * @int_en:	Interrupt enable of green channel.
 * @mode:	Mode of operation.
 *		Driver uses always Active force mode.
 * @trig:	Trigger to be set in active force mode for starting
 *		measurement.
 * @it:		Integration time.
 * @pers:	Persistense - Number of threshold crossing for triggering
 *		interrupt.
 */
struct veml6046x00_rf {
	struct regmap_field *int_en;
	struct regmap_field *mode;
	struct regmap_field *trig;
	struct regmap_field *it;
	struct regmap_field *pers;
};

/**
 * struct veml6046x00_data - Private data of driver.
 * @regmap:	Regmap definition of sensor.
 * @trig:	Industrial-IO trigger.
 * @rf:		Regmap field of configuration.
 */
struct veml6046x00_data {
	struct regmap *regmap;
	struct iio_trigger *trig;
	struct veml6046x00_rf rf;
};

/**
 * DOC: Valid integration times (IT)
 *
 * static const int veml6046x00_it contains the array with valid IT.
 *
 * Register value to be read or written in regmap_field it on veml6046x00 is
 * identical with array index.
 * This means there is no separate translation table between valid integration
 * times and register values needed. The index of the array is identical with
 * the register value.
 *
 * The array is in the form as expected by the callback of the sysfs attribute
 * integration_time_available (IIO_CHAN_INFO_INT_TIME). So there is no
 * additional conversion needed.
 */
static const int veml6046x00_it[][2] = {
	{ 0, 3125 },
	{ 0, 6250 },
	{ 0, 12500 },
	{ 0, 25000 },
	{ 0, 50000 },
	{ 0, 100000 },
	{ 0, 200000 },
	{ 0, 400000 },
};

/**
 * DOC: Handling of gain and photodiode size (PD)
 *
 * Gains here in the driver are not exactly the same as in the datasheet of the
 * sensor. The gain in the driver is a combination of the gain of the sensor
 * with the photodiode size (PD).
 * The following combinations are possible:
 *   gain(driver) = gain(sensor) * PD
 *           0.25 = x0.5  * 1/2
 *           0.33 = x0.66 * 1/2
 *           0.5  = x0.5  * 2/2
 *           0.66 = x0.66 * 2/2
 *           1    = x1    * 2/2
 *           2    = x2    * 2/2
 */

/**
 * struct veml6046x00_gain_pd - Translation of gain and photodiode size (PD).
 * @gain_sen:	Gain used in the sensor as described in the datasheet of the
 *		sensor
 * @pd:		Photodiode size in the sensor
 *
 * This is the translation table from the gain used in the driver (and also used
 * by the userspace interface in sysfs) to the gain and PD used in the sensor
 * hardware.
 *
 * There are six gain values visible to the user (0.25 .. 2) which translate to
 * two different gains in the sensor hardware (x0.5 .. x2) and two PD (1/2 and
 * 2/2). Theoretical are there eight combinations, but gain values 0.5 and 1 are
 * doubled and therefore the combination with the larger PD (2/2) is taken as
 * more photodiode cells are supposed to deliver a more precise result.
 */
struct veml6046x00_gain_pd {
	unsigned int gain_sen;
	unsigned int pd;
};

static const struct veml6046x00_gain_pd veml6046x00_gain_pd[] = {
	{ .gain_sen = VEML6046X00_GAIN_0_5, .pd = VEML6046X00_PD_1_2 },
	{ .gain_sen = VEML6046X00_GAIN_0_66, .pd = VEML6046X00_PD_1_2 },
	{ .gain_sen = VEML6046X00_GAIN_0_5, .pd = VEML6046X00_PD_2_2 },
	{ .gain_sen = VEML6046X00_GAIN_0_66, .pd = VEML6046X00_PD_2_2 },
	{ .gain_sen = VEML6046X00_GAIN_1, .pd = VEML6046X00_PD_2_2 },
	{ .gain_sen = VEML6046X00_GAIN_2, .pd = VEML6046X00_PD_2_2 },
};

/**
 * DOC: Factors for calculation of lux
 *
 * static const int veml6046x00_it_gains contains the factors for calculation of
 * lux.
 *
 * Depending on the set up integration time (IT), gain and photodiode size (PD)
 * the measured raw values are different if the light is constant. As the gain
 * and PD are already coupled in the driver (see &struct veml6046x00_gain_pd)
 * there are two dimensions remaining: IT and gain(driver).
 *
 * The array of available factors for a certain IT are grouped together in the
 * same form as expected by the callback of scale_available
 * (IIO_CHAN_INFO_SCALE).
 *
 * Factors for lux / raw count are taken directly from the datasheet.
 */
static const int veml6046x00_it_gains[][6][2] = {
	/* integration time: 3.125 ms */
	{
		{ 5, 376000 },	/* gain: x0.25 */
		{ 4,  72700 },	/* gain: x0.33 */
		{ 2, 688000 },	/* gain: x0.5 */
		{ 2,  36400 },	/* gain: x0.66 */
		{ 1, 344000 },	/* gain: x1 */
		{ 0, 672000 },	/* gain: x2 */
	},
	/* integration time: 6.25 ms */
	{
		{ 2, 688000 },	/* gain: x0.25 */
		{ 2,  36350 },	/* gain: x0.33 */
		{ 1, 344000 },	/* gain: x0.5 */
		{ 1,  18200 },	/* gain: x0.66 */
		{ 0, 672000 },	/* gain: x1 */
		{ 0, 336000 },	/* gain: x2 */
	},
	/* integration time: 12.5 ms */
	{
		{ 1, 344000 },	/* gain: x0.25 */
		{ 1,  18175 },	/* gain: x0.33 */
		{ 0, 672000 },	/* gain: x0.5 */
		{ 0, 509100 },	/* gain: x0.66 */
		{ 0, 336000 },	/* gain: x1 */
		{ 0, 168000 },	/* gain: x2 */
	},
	/* integration time: 25 ms */
	{
		{ 0, 672000 },	/* gain: x0.25 */
		{ 0, 509087 },	/* gain: x0.33 */
		{ 0, 336000 },	/* gain: x0.5 */
		{ 0, 254550 },	/* gain: x0.66 */
		{ 0, 168000 },	/* gain: x1 */
		{ 0,  84000 },	/* gain: x2 */
	},
	/* integration time: 50 ms */
	{
		{ 0, 336000 },	/* gain: x0.25 */
		{ 0, 254543 },	/* gain: x0.33 */
		{ 0, 168000 },	/* gain: x0.5 */
		{ 0, 127275 },	/* gain: x0.66 */
		{ 0,  84000 },	/* gain: x1 */
		{ 0,  42000 },	/* gain: x2 */
	},
	/* integration time: 100 ms */
	{
		{ 0, 168000 },	/* gain: x0.25 */
		{ 0, 127271 },	/* gain: x0.33 */
		{ 0,  84000 },	/* gain: x0.5 */
		{ 0,  63637 },	/* gain: x0.66 */
		{ 0,  42000 },	/* gain: x1 */
		{ 0,  21000 },	/* gain: x2 */
	},
	/* integration time: 200 ms */
	{
		{ 0,  84000 },	/* gain: x0.25 */
		{ 0,  63635 },	/* gain: x0.33 */
		{ 0,  42000 },	/* gain: x0.5 */
		{ 0,  31818 },	/* gain: x0.66 */
		{ 0,  21000 },	/* gain: x1 */
		{ 0,  10500 },	/* gain: x2 */
	},
	/* integration time: 400 ms */
	{
		{ 0,  42000 },	/* gain: x0.25 */
		{ 0,  31817 },	/* gain: x0.33 */
		{ 0,  21000 },	/* gain: x0.5 */
		{ 0,  15909 },	/* gain: x0.66 */
		{ 0,  10500 },	/* gain: x1 */
		{ 0,   5250 },	/* gain: x2 */
	},
};

/*
 * Two bits (RGB_ON_0 and RGB_ON_1) must be cleared to power on the device.
 */
static int veml6046x00_power_on(struct veml6046x00_data *data)
{
	int ret;
	struct device *dev = regmap_get_device(data->regmap);

	ret = regmap_clear_bits(data->regmap, VEML6046X00_REG_CONF0,
				VEML6046X00_CONF0_ON_0);
	if (ret) {
		dev_err(dev, "Failed to set bit for power on %d\n", ret);
		return ret;
	}

	return regmap_clear_bits(data->regmap, VEML6046X00_REG_CONF1,
				 VEML6046X00_CONF1_ON_1);
}

/*
 * Two bits (RGB_ON_0 and RGB_ON_1) must be set to power off the device.
 */
static int veml6046x00_shutdown(struct veml6046x00_data *data)
{
	int ret;
	struct device *dev = regmap_get_device(data->regmap);

	ret = regmap_set_bits(data->regmap, VEML6046X00_REG_CONF0,
			      VEML6046X00_CONF0_ON_0);
	if (ret) {
		dev_err(dev, "Failed to set bit for shutdown %d\n", ret);
		return ret;
	}

	return regmap_set_bits(data->regmap, VEML6046X00_REG_CONF1,
			       VEML6046X00_CONF1_ON_1);
}

static void veml6046x00_shutdown_action(void *data)
{
	veml6046x00_shutdown(data);
}

static const struct iio_chan_spec veml6046x00_channels[] = {
	{
		.type = IIO_INTENSITY,
		.address = VEML6046X00_REG_R,
		.modified = 1,
		.channel2 = IIO_MOD_LIGHT_RED,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_INT_TIME) |
					   BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_INT_TIME) |
						     BIT(IIO_CHAN_INFO_SCALE),
		.scan_index = VEML6046X00_SCAN_R,
		.scan_type = {
			.sign = 'u',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_LE,
		},
	},
	{
		.type = IIO_INTENSITY,
		.address = VEML6046X00_REG_G,
		.modified = 1,
		.channel2 = IIO_MOD_LIGHT_GREEN,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_INT_TIME) |
					   BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_INT_TIME) |
						     BIT(IIO_CHAN_INFO_SCALE),
		.scan_index = VEML6046X00_SCAN_G,
		.scan_type = {
			.sign = 'u',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_LE,
		},
	},
	{
		.type = IIO_INTENSITY,
		.address = VEML6046X00_REG_B,
		.modified = 1,
		.channel2 = IIO_MOD_LIGHT_BLUE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_INT_TIME) |
					   BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_INT_TIME) |
						     BIT(IIO_CHAN_INFO_SCALE),
		.scan_index = VEML6046X00_SCAN_B,
		.scan_type = {
			.sign = 'u',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_LE,
		},
	},
	{
		.type = IIO_INTENSITY,
		.address = VEML6046X00_REG_IR,
		.modified = 1,
		.channel2 = IIO_MOD_LIGHT_IR,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_INT_TIME) |
					   BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_INT_TIME) |
						     BIT(IIO_CHAN_INFO_SCALE),
		.scan_index = VEML6046X00_SCAN_IR,
		.scan_type = {
			.sign = 'u',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_LE,
		},
	},
	IIO_CHAN_SOFT_TIMESTAMP(VEML6046X00_SCAN_TIMESTAMP),
};

static const struct regmap_config veml6046x00_regmap_config = {
	.name = "veml6046x00_regm",
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = VEML6046X00_REG_INT_H,
};

static const struct reg_field veml6046x00_rf_int_en =
	REG_FIELD(VEML6046X00_REG_CONF0, 1, 1);

static const struct reg_field veml6046x00_rf_trig =
	REG_FIELD(VEML6046X00_REG_CONF0, 2, 2);

static const struct reg_field veml6046x00_rf_mode =
	REG_FIELD(VEML6046X00_REG_CONF0, 3, 3);

static const struct reg_field veml6046x00_rf_it =
	REG_FIELD(VEML6046X00_REG_CONF0, 4, 6);

static const struct reg_field veml6046x00_rf_pers =
	REG_FIELD(VEML6046X00_REG_CONF1, 1, 2);

static int veml6046x00_regfield_init(struct veml6046x00_data *data)
{
	struct regmap *regmap = data->regmap;
	struct device *dev = regmap_get_device(data->regmap);
	struct regmap_field *rm_field;
	struct veml6046x00_rf *rf = &data->rf;

	rm_field = devm_regmap_field_alloc(dev, regmap, veml6046x00_rf_int_en);
	if (IS_ERR(rm_field))
		return PTR_ERR(rm_field);
	rf->int_en = rm_field;

	rm_field = devm_regmap_field_alloc(dev, regmap, veml6046x00_rf_mode);
	if (IS_ERR(rm_field))
		return PTR_ERR(rm_field);
	rf->mode = rm_field;

	rm_field = devm_regmap_field_alloc(dev, regmap, veml6046x00_rf_trig);
	if (IS_ERR(rm_field))
		return PTR_ERR(rm_field);
	rf->trig = rm_field;

	rm_field = devm_regmap_field_alloc(dev, regmap, veml6046x00_rf_it);
	if (IS_ERR(rm_field))
		return PTR_ERR(rm_field);
	rf->it = rm_field;

	rm_field = devm_regmap_field_alloc(dev, regmap, veml6046x00_rf_pers);
	if (IS_ERR(rm_field))
		return PTR_ERR(rm_field);
	rf->pers = rm_field;

	return 0;
}

static int veml6046x00_get_it_index(struct veml6046x00_data *data)
{
	int ret;
	unsigned int reg;

	ret = regmap_field_read(data->rf.it, &reg);
	if (ret)
		return ret;

	/* register value is identical with index of array */
	if (reg >= ARRAY_SIZE(veml6046x00_it))
		return -EINVAL;

	return reg;
}

static int veml6046x00_get_it_usec(struct veml6046x00_data *data, unsigned int *it_usec)
{
	int ret;
	unsigned int reg;

	ret = regmap_field_read(data->rf.it, &reg);
	if (ret)
		return ret;

	if (reg >= ARRAY_SIZE(veml6046x00_it))
		return -EINVAL;

	*it_usec = veml6046x00_it[reg][1];

	return IIO_VAL_INT_PLUS_MICRO;
}

static int veml6046x00_set_it(struct iio_dev *iio, int val, int val2)
{
	struct veml6046x00_data *data = iio_priv(iio);
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(veml6046x00_it); i++) {
		if ((veml6046x00_it[i][0] == val) &&
		    (veml6046x00_it[i][1] == val2))
			return regmap_field_write(data->rf.it, i);
	}

	return -EINVAL;
}

static int veml6046x00_get_val_gain_idx(struct veml6046x00_data *data, int val,
					int val2)
{
	unsigned int i;
	int it_idx;

	it_idx = veml6046x00_get_it_index(data);
	if (it_idx < 0)
		return it_idx;

	for (i = 0; i < ARRAY_SIZE(veml6046x00_it_gains[it_idx]); i++) {
		if ((veml6046x00_it_gains[it_idx][i][0] == val) &&
		    (veml6046x00_it_gains[it_idx][i][1] == val2))
			return i;
	}

	return -EINVAL;
}

static int veml6046x00_get_gain_idx(struct veml6046x00_data *data)
{
	int ret;
	unsigned int i, reg, reg_gain, reg_pd;

	ret = regmap_read(data->regmap, VEML6046X00_REG_CONF1, &reg);
	if (ret)
		return ret;

	reg_gain = FIELD_GET(VEML6046X00_CONF1_GAIN, reg);
	reg_pd = reg & VEML6046X00_CONF1_PD_D2;

	for (i = 0; i < ARRAY_SIZE(veml6046x00_gain_pd); i++) {
		if ((veml6046x00_gain_pd[i].gain_sen == reg_gain) &&
		    (veml6046x00_gain_pd[i].pd == reg_pd))
			return i;
	}

	return -EINVAL;
}

static int veml6046x00_set_scale(struct iio_dev *iio, int val, int val2)
{
	struct veml6046x00_data *data = iio_priv(iio);
	unsigned int new_scale;
	int gain_idx;

	gain_idx = veml6046x00_get_val_gain_idx(data, val, val2);
	if (gain_idx < 0)
		return gain_idx;

	new_scale = FIELD_PREP(VEML6046X00_CONF1_GAIN,
			       veml6046x00_gain_pd[gain_idx].gain_sen) |
			       veml6046x00_gain_pd[gain_idx].pd;

	return regmap_update_bits(data->regmap, VEML6046X00_REG_CONF1,
				  VEML6046X00_CONF1_GAIN |
				  VEML6046X00_CONF1_PD_D2,
				  new_scale);
}

static int veml6046x00_get_scale(struct veml6046x00_data *data,
				 int *val, int *val2)
{
	int gain_idx, it_idx;

	gain_idx = veml6046x00_get_gain_idx(data);
	if (gain_idx < 0)
		return gain_idx;

	it_idx = veml6046x00_get_it_index(data);
	if (it_idx < 0)
		return it_idx;

	*val = veml6046x00_it_gains[it_idx][gain_idx][0];
	*val2 = veml6046x00_it_gains[it_idx][gain_idx][1];

	return IIO_VAL_INT_PLUS_MICRO;
}

/**
 * veml6046x00_read_data_ready() - Read data ready bit
 * @data:	Private data.
 *
 * Helper function for reading data ready bit from interrupt register.
 *
 * Return:
 * * %1		- Data is available (AF_DATA_READY is set)
 * * %0		- No data available
 * * %-EIO	- Error during bulk read
 */
static int veml6046x00_read_data_ready(struct veml6046x00_data *data)
{
	struct device *dev = regmap_get_device(data->regmap);
	int ret;
	u8 reg[2];

	/*
	 * Note from the vendor, but not explicitly in the datasheet: we
	 * should always read both registers together.
	 */
	ret = regmap_bulk_read(data->regmap, VEML6046X00_REG_INT,
			       &reg, sizeof(reg));
	if (ret) {
		dev_err(dev, "Failed to read interrupt register %d\n", ret);
		return -EIO;
	}

	if (reg[1] & VEML6046X00_INT_DRDY)
		return 1;

	return 0;
}

/**
 * veml6046x00_wait_data_available() - Wait until data is available
 * @iio:	Industrial IO.
 * @usecs:	Microseconds to wait for data.
 *
 * This function waits for a certain bit in the interrupt register which signals
 * that there is data to be read available.
 *
 * It tries it two times with a waiting time of usecs in between.
 *
 * Return:
 * * %1		- Data is available (AF_DATA_READY is set)
 * * %0		- Timeout, no data available after usecs timeout
 * * %-EIO	- Error during bulk read
 */
static int veml6046x00_wait_data_available(struct iio_dev *iio, unsigned int usecs)
{
	struct veml6046x00_data *data = iio_priv(iio);
	int ret;

	ret = veml6046x00_read_data_ready(data);
	if (ret)
		return ret;

	fsleep(usecs);
	return veml6046x00_read_data_ready(data);
}

static int veml6046x00_single_read(struct iio_dev *iio,
				   enum iio_modifier modifier, int *val)
{
	struct veml6046x00_data *data = iio_priv(iio);
	struct device *dev = regmap_get_device(data->regmap);
	unsigned int addr, it_usec;
	int ret;
	__le16 reg;

	switch (modifier) {
	case IIO_MOD_LIGHT_RED:
		addr = VEML6046X00_REG_R;
		break;
	case IIO_MOD_LIGHT_GREEN:
		addr = VEML6046X00_REG_G;
		break;
	case IIO_MOD_LIGHT_BLUE:
		addr = VEML6046X00_REG_B;
		break;
	case IIO_MOD_LIGHT_IR:
		addr = VEML6046X00_REG_IR;
		break;
	default:
		return -EINVAL;
	}
	ret = pm_runtime_resume_and_get(dev);
	if (ret)
		return ret;

	ret = veml6046x00_get_it_usec(data, &it_usec);
	if (ret < 0) {
		dev_err(dev, "Failed to get integration time ret: %d", ret);
		goto out;
	}

	ret = regmap_field_write(data->rf.mode, 1);
	if (ret) {
		dev_err(dev, "Failed to write mode ret: %d", ret);
		goto out;
	}

	ret = regmap_field_write(data->rf.trig, 1);
	if (ret) {
		dev_err(dev, "Failed to write trigger ret: %d", ret);
		goto out;
	}

	/* integration time + 12.5 % to ensure completion */
	fsleep(it_usec + it_usec / 8);

	ret = veml6046x00_wait_data_available(iio, it_usec * 4);
	if (ret < 0)
		goto out;
	if (ret == 0) {
		ret = -EAGAIN;
		goto out;
	}

	if (!iio_device_claim_direct(iio)) {
		ret = -EBUSY;
		goto out;
	}

	ret = regmap_bulk_read(data->regmap, addr, &reg, sizeof(reg));
	iio_device_release_direct(iio);
	if (ret)
		goto out;

	*val = le16_to_cpu(reg);

	ret = IIO_VAL_INT;

out:
	pm_runtime_put_autosuspend(dev);

	return ret;
}

static int veml6046x00_read_raw(struct iio_dev *iio,
				struct iio_chan_spec const *chan, int *val,
				int *val2, long mask)
{
	struct veml6046x00_data *data = iio_priv(iio);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (chan->type != IIO_INTENSITY)
			return -EINVAL;
		return veml6046x00_single_read(iio, chan->channel2, val);
	case IIO_CHAN_INFO_INT_TIME:
		*val = 0;
		return veml6046x00_get_it_usec(data, val2);
	case IIO_CHAN_INFO_SCALE:
		return veml6046x00_get_scale(data, val, val2);
	default:
		return -EINVAL;
	}
}

static int veml6046x00_read_avail(struct iio_dev *iio,
				  struct iio_chan_spec const *chan,
				  const int **vals, int *type, int *length,
				  long mask)
{
	struct veml6046x00_data *data = iio_priv(iio);
	int it_idx;

	switch (mask) {
	case IIO_CHAN_INFO_INT_TIME:
		*vals = (int *)&veml6046x00_it;
		*length = 2 * ARRAY_SIZE(veml6046x00_it);
		*type = IIO_VAL_INT_PLUS_MICRO;
		return IIO_AVAIL_LIST;
	case IIO_CHAN_INFO_SCALE:
		it_idx = veml6046x00_get_it_index(data);
		if (it_idx < 0)
			return it_idx;
		*vals = (int *)&veml6046x00_it_gains[it_idx];
		*length = 2 * ARRAY_SIZE(veml6046x00_it_gains[it_idx]);
		*type = IIO_VAL_INT_PLUS_MICRO;
		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
}

static int veml6046x00_write_raw(struct iio_dev *iio,
				 struct iio_chan_spec const *chan,
				 int val, int val2, long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_INT_TIME:
		return veml6046x00_set_it(iio, val, val2);
	case IIO_CHAN_INFO_SCALE:
		return veml6046x00_set_scale(iio, val, val2);
	default:
		return -EINVAL;
	}
}

static const struct iio_info veml6046x00_info_no_irq = {
	.read_raw = veml6046x00_read_raw,
	.read_avail = veml6046x00_read_avail,
	.write_raw = veml6046x00_write_raw,
};

static int veml6046x00_buffer_preenable(struct iio_dev *iio)
{
	struct veml6046x00_data *data = iio_priv(iio);
	struct device *dev = regmap_get_device(data->regmap);
	int ret;

	ret = regmap_field_write(data->rf.mode, 0);
	if (ret) {
		dev_err(dev, "Failed to set mode %d\n", ret);
		return ret;
	}

	ret = regmap_field_write(data->rf.trig, 0);
	if (ret) {
		/*
		 * no unrolling of mode as it is set appropriately with next
		 * single read.
		 */
		dev_err(dev, "Failed to set trigger %d\n", ret);
		return ret;
	}

	return pm_runtime_resume_and_get(dev);
}

static int veml6046x00_buffer_postdisable(struct iio_dev *iio)
{
	struct veml6046x00_data *data = iio_priv(iio);
	struct device *dev = regmap_get_device(data->regmap);
	int ret;

	ret = regmap_field_write(data->rf.mode, 1);
	if (ret) {
		dev_err(dev, "Failed to set mode %d\n", ret);
		return ret;
	}

	pm_runtime_put_autosuspend(dev);

	return 0;
}

static const struct iio_buffer_setup_ops veml6046x00_buffer_setup_ops = {
	.preenable = veml6046x00_buffer_preenable,
	.postdisable = veml6046x00_buffer_postdisable,
};

static irqreturn_t veml6046x00_trig_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *iio = pf->indio_dev;
	struct veml6046x00_data *data = iio_priv(iio);
	int ret;
	struct {
		__le16 chans[4];
		aligned_s64 timestamp;
	} scan;

	ret = regmap_bulk_read(data->regmap, VEML6046X00_REG_R,
			       &scan.chans, sizeof(scan.chans));
	if (ret)
		goto done;

	iio_push_to_buffers_with_ts(iio, &scan, sizeof(scan),
				    iio_get_time_ns(iio));

done:
	iio_trigger_notify_done(iio->trig);

	return IRQ_HANDLED;
}

static int veml6046x00_validate_part_id(struct veml6046x00_data *data)
{
	struct device *dev = regmap_get_device(data->regmap);
	unsigned int part_id;
	int ret;
	__le16 reg;

	ret = regmap_bulk_read(data->regmap, VEML6046X00_REG_ID,
			       &reg, sizeof(reg));
	if (ret)
		return dev_err_probe(dev, ret, "Failed to read ID\n");

	part_id = le16_to_cpu(reg);
	if (part_id != 0x01)
		dev_info(dev, "Unknown ID %#04x\n", part_id);

	return 0;
}

static int veml6046x00_setup_device(struct iio_dev *iio)
{
	struct veml6046x00_data *data = iio_priv(iio);
	struct device *dev = regmap_get_device(data->regmap);
	int ret;
	__le16 reg16;

	reg16 = cpu_to_le16(VEML6046X00_CONF0_AF);
	ret = regmap_bulk_write(data->regmap, VEML6046X00_REG_CONF0,
				&reg16, sizeof(reg16));
	if (ret)
		return dev_err_probe(dev, ret, "Failed to set configuration\n");

	reg16 = cpu_to_le16(0);
	ret = regmap_bulk_write(data->regmap, VEML6046X00_REG_THDL,
				&reg16, sizeof(reg16));
	if (ret)
		return dev_err_probe(dev, ret, "Failed to set low threshold\n");

	reg16 = cpu_to_le16(U16_MAX);
	ret = regmap_bulk_write(data->regmap, VEML6046X00_REG_THDH,
				&reg16, sizeof(reg16));
	if (ret)
		return dev_err_probe(dev, ret, "Failed to set high threshold\n");

	ret = regmap_bulk_read(data->regmap, VEML6046X00_REG_INT,
			       &reg16, sizeof(reg16));
	if (ret)
		return dev_err_probe(dev, ret, "Failed to clear interrupts\n");

	return 0;
}

static int veml6046x00_probe(struct i2c_client *i2c)
{
	struct device *dev = &i2c->dev;
	struct veml6046x00_data *data;
	struct iio_dev *iio;
	struct regmap *regmap;
	int ret;

	regmap = devm_regmap_init_i2c(i2c, &veml6046x00_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap), "Failed to set regmap\n");

	iio = devm_iio_device_alloc(dev, sizeof(*data));
	if (!iio)
		return -ENOMEM;

	data = iio_priv(iio);
	/* struct iio_dev is retrieved via dev_get_drvdata(). */
	i2c_set_clientdata(i2c, iio);
	data->regmap = regmap;

	ret = veml6046x00_regfield_init(data);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to init regfield\n");

	ret = devm_regulator_get_enable(dev, "vdd");
	if (ret)
		return dev_err_probe(dev, ret, "Failed to enable regulator\n");

	/* bring device in a known state and switch device on */
	ret = veml6046x00_setup_device(iio);
	if (ret < 0)
		return ret;

	ret = devm_add_action_or_reset(dev, veml6046x00_shutdown_action, data);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to add shut down action\n");

	ret = pm_runtime_set_active(dev);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to activate PM runtime\n");

	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to enable PM runtime\n");

	pm_runtime_get_noresume(dev);
	pm_runtime_set_autosuspend_delay(dev, VEML6046X00_AUTOSUSPEND_MS);
	pm_runtime_use_autosuspend(dev);

	ret = veml6046x00_validate_part_id(data);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to validate device ID\n");

	iio->name = "veml6046x00";
	iio->channels = veml6046x00_channels;
	iio->num_channels = ARRAY_SIZE(veml6046x00_channels);
	iio->modes = INDIO_DIRECT_MODE;

	iio->info = &veml6046x00_info_no_irq;

	ret = devm_iio_triggered_buffer_setup(dev, iio, NULL,
					      veml6046x00_trig_handler,
					      &veml6046x00_buffer_setup_ops);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to register triggered buffer");

	pm_runtime_put_autosuspend(dev);

	ret = devm_iio_device_register(dev, iio);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to register iio device");

	return 0;
}

static int veml6046x00_runtime_suspend(struct device *dev)
{
	struct veml6046x00_data *data = iio_priv(dev_get_drvdata(dev));

	return veml6046x00_shutdown(data);
}

static int veml6046x00_runtime_resume(struct device *dev)
{
	struct veml6046x00_data *data = iio_priv(dev_get_drvdata(dev));

	return veml6046x00_power_on(data);
}

static DEFINE_RUNTIME_DEV_PM_OPS(veml6046x00_pm_ops,
				 veml6046x00_runtime_suspend,
				 veml6046x00_runtime_resume, NULL);

static const struct of_device_id veml6046x00_of_match[] = {
	{ .compatible = "vishay,veml6046x00" },
	{ }
};
MODULE_DEVICE_TABLE(of, veml6046x00_of_match);

static const struct i2c_device_id veml6046x00_id[] = {
	{ "veml6046x00" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, veml6046x00_id);

static struct i2c_driver veml6046x00_driver = {
	.driver = {
		.name = "veml6046x00",
		.of_match_table = veml6046x00_of_match,
		.pm = pm_ptr(&veml6046x00_pm_ops),
	},
	.probe = veml6046x00_probe,
	.id_table = veml6046x00_id,
};
module_i2c_driver(veml6046x00_driver);

MODULE_AUTHOR("Andreas Klinger <ak@it-klinger.de>");
MODULE_DESCRIPTION("VEML6046X00 RGBIR Color Sensor");
MODULE_LICENSE("GPL");
