// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * APDS-9306/APDS-9306-065 Ambient Light Sensor
 * I2C Address: 0x52
 * Datasheet: https://docs.broadcom.com/doc/AV02-4755EN
 *
 * Copyright (C) 2024 Subhajit Ghosh <subhajit.ghosh@tweaklogic.com>
 */

#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/types.h>
#include <linux/units.h>

#include <linux/iio/iio.h>
#include <linux/iio/iio-gts-helper.h>
#include <linux/iio/events.h>
#include <linux/iio/sysfs.h>

#include <linux/unaligned.h>

#define APDS9306_MAIN_CTRL_REG		0x00
#define APDS9306_ALS_MEAS_RATE_REG	0x04
#define APDS9306_ALS_GAIN_REG		0x05
#define APDS9306_PART_ID_REG		0x06
#define APDS9306_MAIN_STATUS_REG	0x07
#define APDS9306_CLEAR_DATA_0_REG	0x0A
#define APDS9306_CLEAR_DATA_1_REG	0x0B
#define APDS9306_CLEAR_DATA_2_REG	0x0C
#define APDS9306_ALS_DATA_0_REG		0x0D
#define APDS9306_ALS_DATA_1_REG		0x0E
#define APDS9306_ALS_DATA_2_REG		0x0F
#define APDS9306_INT_CFG_REG		0x19
#define APDS9306_INT_PERSISTENCE_REG	0x1A
#define APDS9306_ALS_THRES_UP_0_REG	0x21
#define APDS9306_ALS_THRES_UP_1_REG	0x22
#define APDS9306_ALS_THRES_UP_2_REG	0x23
#define APDS9306_ALS_THRES_LOW_0_REG	0x24
#define APDS9306_ALS_THRES_LOW_1_REG	0x25
#define APDS9306_ALS_THRES_LOW_2_REG	0x26
#define APDS9306_ALS_THRES_VAR_REG	0x27

#define APDS9306_ALS_INT_STAT_MASK	BIT(4)
#define APDS9306_ALS_DATA_STAT_MASK	BIT(3)

#define APDS9306_ALS_THRES_VAL_MAX	(BIT(20) - 1)
#define APDS9306_ALS_THRES_VAR_NUM_VALS	8
#define APDS9306_ALS_PERSIST_NUM_VALS	16
#define APDS9306_ALS_READ_DATA_DELAY_US	(20 * USEC_PER_MSEC)
#define APDS9306_NUM_REPEAT_RATES	7
#define APDS9306_INT_SRC_CLEAR	0
#define APDS9306_INT_SRC_ALS	1
#define APDS9306_SAMP_FREQ_10HZ	0

/**
 * struct part_id_gts_multiplier - Part no. and corresponding gts multiplier
 *
 * GTS (Gain Time Scale) are helper functions for Light sensors which along
 * with hardware gains also has gains associated with Integration times.
 *
 * There are two variants of the device with slightly different characteristics,
 * they have same ADC count for different Lux levels as mentioned in the
 * datasheet. This multiplier array is used to store the derived Lux per count
 * value for the two variants to be used by the GTS helper functions.
 *
 * @part_id: Part ID of the device
 * @max_scale_int: Multiplier for iio_init_iio_gts()
 * @max_scale_nano: Multiplier for iio_init_iio_gts()
 */
struct part_id_gts_multiplier {
	int part_id;
	int max_scale_int;
	int max_scale_nano;
};

/*
 * As per the datasheet, at HW Gain = 3x, Integration time 100mS (32x),
 * typical 2000 ADC counts are observed for 49.8 uW per sq cm (340.134 lux)
 * for apds9306 and 43 uW per sq cm (293.69 lux) for apds9306-065.
 * Assuming lux per count is linear across all integration time ranges.
 *
 * Lux = (raw + offset) * scale; offset can be any value by userspace.
 * HG = Hardware Gain; ITG = Gain by changing integration time.
 * Scale table by IIO GTS Helpers = (1 / HG) * (1 / ITG) * Multiplier.
 *
 * The Lux values provided in the datasheet are at ITG=32x and HG=3x,
 * at typical 2000 count for both variants of the device.
 *
 * Lux per ADC count at 3x and 32x for apds9306 = 340.134 / 2000
 * Lux per ADC count at 3x and 32x for apds9306-065 = 293.69 / 2000
 *
 * The Multiplier for the scale table provided to userspace:
 * IIO GTS scale Multiplier for apds9306 = (340.134 / 2000) * 32 * 3 = 16.326432
 * and for apds9306-065 = (293.69 / 2000) * 32 * 3 = 14.09712
 */
static const struct part_id_gts_multiplier apds9306_gts_mul[] = {
	{
		.part_id = 0xB1,
		.max_scale_int = 16,
		.max_scale_nano = 326432000,
	}, {
		.part_id = 0xB3,
		.max_scale_int = 14,
		.max_scale_nano = 97120000,
	},
};

static const int apds9306_repeat_rate_freq[APDS9306_NUM_REPEAT_RATES][2] = {
	{ 40, 0 },
	{ 20, 0 },
	{ 10, 0 },
	{ 5,  0 },
	{ 2,  0 },
	{ 1,  0 },
	{ 0,  500000 },
};

static const int apds9306_repeat_rate_period[APDS9306_NUM_REPEAT_RATES] = {
	25000, 50000, 100000, 200000, 500000, 1000000, 2000000,
};

/**
 * struct apds9306_regfields - apds9306 regmap fields definitions
 *
 * @sw_reset: Software reset regfield
 * @en: Enable regfield
 * @intg_time: Resolution regfield
 * @repeat_rate: Measurement Rate regfield
 * @gain: Hardware gain regfield
 * @int_src: Interrupt channel regfield
 * @int_thresh_var_en: Interrupt variance threshold regfield
 * @int_en: Interrupt enable regfield
 * @int_persist_val: Interrupt persistence regfield
 * @int_thresh_var_val: Interrupt threshold variance value regfield
 */
struct apds9306_regfields {
	struct regmap_field *sw_reset;
	struct regmap_field *en;
	struct regmap_field *intg_time;
	struct regmap_field *repeat_rate;
	struct regmap_field *gain;
	struct regmap_field *int_src;
	struct regmap_field *int_thresh_var_en;
	struct regmap_field *int_en;
	struct regmap_field *int_persist_val;
	struct regmap_field *int_thresh_var_val;
};

/**
 * struct apds9306_data - apds9306 private data and registers definitions
 *
 * @dev: Pointer to the device structure
 * @gts: IIO Gain Time Scale structure
 * @mutex: Lock for protecting adc reads, device settings changes where
 *         some calculations are required before or after setting or
 *         getting the raw settings values from regmap writes or reads
 *         respectively.
 * @regmap: Regmap structure pointer
 * @rf: Regmap register fields structure
 * @nlux_per_count: Nano lux per ADC count for a particular model
 * @read_data_available: Flag set by IRQ handler for ADC data available
 */
struct apds9306_data {
	struct device *dev;
	struct iio_gts gts;

	struct mutex mutex;

	struct regmap *regmap;
	struct apds9306_regfields rf;

	int nlux_per_count;
	int read_data_available;
};

/*
 * Available scales with gain 1x - 18x, timings 3.125, 25, 50, 100, 200, 400 ms
 * Time impacts to gain: 1x, 8x, 16x, 32x, 64x, 128x
 */
#define APDS9306_GSEL_1X	0x00
#define APDS9306_GSEL_3X	0x01
#define APDS9306_GSEL_6X	0x02
#define APDS9306_GSEL_9X	0x03
#define APDS9306_GSEL_18X	0x04

static const struct iio_gain_sel_pair apds9306_gains[] = {
	GAIN_SCALE_GAIN(1, APDS9306_GSEL_1X),
	GAIN_SCALE_GAIN(3, APDS9306_GSEL_3X),
	GAIN_SCALE_GAIN(6, APDS9306_GSEL_6X),
	GAIN_SCALE_GAIN(9, APDS9306_GSEL_9X),
	GAIN_SCALE_GAIN(18, APDS9306_GSEL_18X),
};

#define APDS9306_MEAS_MODE_400MS	0x00
#define APDS9306_MEAS_MODE_200MS	0x01
#define APDS9306_MEAS_MODE_100MS	0x02
#define APDS9306_MEAS_MODE_50MS		0x03
#define APDS9306_MEAS_MODE_25MS		0x04
#define APDS9306_MEAS_MODE_3125US	0x05

static const struct iio_itime_sel_mul apds9306_itimes[] = {
	GAIN_SCALE_ITIME_US(400000, APDS9306_MEAS_MODE_400MS, BIT(7)),
	GAIN_SCALE_ITIME_US(200000, APDS9306_MEAS_MODE_200MS, BIT(6)),
	GAIN_SCALE_ITIME_US(100000, APDS9306_MEAS_MODE_100MS, BIT(5)),
	GAIN_SCALE_ITIME_US(50000, APDS9306_MEAS_MODE_50MS, BIT(4)),
	GAIN_SCALE_ITIME_US(25000, APDS9306_MEAS_MODE_25MS, BIT(3)),
	GAIN_SCALE_ITIME_US(3125, APDS9306_MEAS_MODE_3125US, BIT(0)),
};

static const struct iio_event_spec apds9306_event_spec[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_shared_by_all = BIT(IIO_EV_INFO_VALUE),
	}, {
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_FALLING,
		.mask_shared_by_all = BIT(IIO_EV_INFO_VALUE),
	}, {
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_EITHER,
		.mask_shared_by_all = BIT(IIO_EV_INFO_PERIOD),
		.mask_separate = BIT(IIO_EV_INFO_ENABLE),
	}, {
		.type = IIO_EV_TYPE_THRESH_ADAPTIVE,
		.mask_shared_by_all = BIT(IIO_EV_INFO_VALUE) |
			BIT(IIO_EV_INFO_ENABLE),
	},
};

static const struct iio_chan_spec apds9306_channels_with_events[] = {
	{
		.type = IIO_LIGHT,
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_INT_TIME) |
					   BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_INT_TIME) |
						     BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_separate_available = BIT(IIO_CHAN_INFO_SCALE),
		.event_spec = apds9306_event_spec,
		.num_event_specs = ARRAY_SIZE(apds9306_event_spec),
	}, {
		.type = IIO_INTENSITY,
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_INT_TIME) |
					   BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_INT_TIME) |
						     BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.channel2 = IIO_MOD_LIGHT_CLEAR,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.modified = 1,
		.event_spec = apds9306_event_spec,
		.num_event_specs = ARRAY_SIZE(apds9306_event_spec),
	},
};

static const struct iio_chan_spec apds9306_channels_without_events[] = {
	{
		.type = IIO_LIGHT,
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_INT_TIME) |
					   BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_INT_TIME) |
						     BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_separate_available = BIT(IIO_CHAN_INFO_SCALE),
	}, {
		.type = IIO_INTENSITY,
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_INT_TIME) |
					   BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_INT_TIME) |
						     BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.modified = 1,
		.channel2 = IIO_MOD_LIGHT_CLEAR,
	},
};

/* INT_PERSISTENCE available */
static IIO_CONST_ATTR(thresh_either_period_available, "[0 1 15]");

/* ALS_THRESH_VAR available */
static IIO_CONST_ATTR(thresh_adaptive_either_values_available, "[0 1 7]");

static struct attribute *apds9306_event_attributes[] = {
	&iio_const_attr_thresh_either_period_available.dev_attr.attr,
	&iio_const_attr_thresh_adaptive_either_values_available.dev_attr.attr,
	NULL
};

static const struct attribute_group apds9306_event_attr_group = {
	.attrs = apds9306_event_attributes,
};

static const struct regmap_range apds9306_readable_ranges[] = {
	regmap_reg_range(APDS9306_MAIN_CTRL_REG, APDS9306_ALS_THRES_VAR_REG)
};

static const struct regmap_range apds9306_writable_ranges[] = {
	regmap_reg_range(APDS9306_MAIN_CTRL_REG, APDS9306_ALS_GAIN_REG),
	regmap_reg_range(APDS9306_INT_CFG_REG, APDS9306_ALS_THRES_VAR_REG)
};

static const struct regmap_range apds9306_volatile_ranges[] = {
	regmap_reg_range(APDS9306_MAIN_STATUS_REG, APDS9306_MAIN_STATUS_REG),
	regmap_reg_range(APDS9306_CLEAR_DATA_0_REG, APDS9306_ALS_DATA_2_REG)
};

static const struct regmap_range apds9306_precious_ranges[] = {
	regmap_reg_range(APDS9306_MAIN_STATUS_REG, APDS9306_MAIN_STATUS_REG)
};

static const struct regmap_access_table apds9306_readable_table = {
	.yes_ranges = apds9306_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(apds9306_readable_ranges)
};

static const struct regmap_access_table apds9306_writable_table = {
	.yes_ranges = apds9306_writable_ranges,
	.n_yes_ranges = ARRAY_SIZE(apds9306_writable_ranges)
};

static const struct regmap_access_table apds9306_volatile_table = {
	.yes_ranges = apds9306_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(apds9306_volatile_ranges)
};

static const struct regmap_access_table apds9306_precious_table = {
	.yes_ranges = apds9306_precious_ranges,
	.n_yes_ranges = ARRAY_SIZE(apds9306_precious_ranges)
};

static const struct regmap_config apds9306_regmap = {
	.name = "apds9306_regmap",
	.reg_bits = 8,
	.val_bits = 8,
	.rd_table = &apds9306_readable_table,
	.wr_table = &apds9306_writable_table,
	.volatile_table = &apds9306_volatile_table,
	.precious_table = &apds9306_precious_table,
	.max_register = APDS9306_ALS_THRES_VAR_REG,
	.cache_type = REGCACHE_RBTREE,
};

static const struct reg_field apds9306_rf_sw_reset =
	REG_FIELD(APDS9306_MAIN_CTRL_REG, 4, 4);

static const struct reg_field apds9306_rf_en =
	REG_FIELD(APDS9306_MAIN_CTRL_REG, 1, 1);

static const struct reg_field apds9306_rf_intg_time =
	REG_FIELD(APDS9306_ALS_MEAS_RATE_REG, 4, 6);

static const struct reg_field apds9306_rf_repeat_rate =
	REG_FIELD(APDS9306_ALS_MEAS_RATE_REG, 0, 2);

static const struct reg_field apds9306_rf_gain =
	REG_FIELD(APDS9306_ALS_GAIN_REG, 0, 2);

static const struct reg_field apds9306_rf_int_src =
	REG_FIELD(APDS9306_INT_CFG_REG, 4, 5);

static const struct reg_field apds9306_rf_int_thresh_var_en =
	REG_FIELD(APDS9306_INT_CFG_REG, 3, 3);

static const struct reg_field apds9306_rf_int_en =
	REG_FIELD(APDS9306_INT_CFG_REG, 2, 2);

static const struct reg_field apds9306_rf_int_persist_val =
	REG_FIELD(APDS9306_INT_PERSISTENCE_REG, 4, 7);

static const struct reg_field apds9306_rf_int_thresh_var_val =
	REG_FIELD(APDS9306_ALS_THRES_VAR_REG, 0, 2);

static int apds9306_regfield_init(struct apds9306_data *data)
{
	struct device *dev = data->dev;
	struct regmap *regmap = data->regmap;
	struct regmap_field *tmp;
	struct apds9306_regfields *rf = &data->rf;

	tmp = devm_regmap_field_alloc(dev, regmap, apds9306_rf_sw_reset);
	if (IS_ERR(tmp))
		return PTR_ERR(tmp);
	rf->sw_reset = tmp;

	tmp = devm_regmap_field_alloc(dev, regmap, apds9306_rf_en);
	if (IS_ERR(tmp))
		return PTR_ERR(tmp);
	rf->en = tmp;

	tmp = devm_regmap_field_alloc(dev, regmap, apds9306_rf_intg_time);
	if (IS_ERR(tmp))
		return PTR_ERR(tmp);
	rf->intg_time = tmp;

	tmp = devm_regmap_field_alloc(dev, regmap, apds9306_rf_repeat_rate);
	if (IS_ERR(tmp))
		return PTR_ERR(tmp);
	rf->repeat_rate = tmp;

	tmp = devm_regmap_field_alloc(dev, regmap, apds9306_rf_gain);
	if (IS_ERR(tmp))
		return PTR_ERR(tmp);
	rf->gain = tmp;

	tmp = devm_regmap_field_alloc(dev, regmap, apds9306_rf_int_src);
	if (IS_ERR(tmp))
		return PTR_ERR(tmp);
	rf->int_src = tmp;

	tmp = devm_regmap_field_alloc(dev, regmap, apds9306_rf_int_thresh_var_en);
	if (IS_ERR(tmp))
		return PTR_ERR(tmp);
	rf->int_thresh_var_en = tmp;

	tmp = devm_regmap_field_alloc(dev, regmap, apds9306_rf_int_en);
	if (IS_ERR(tmp))
		return PTR_ERR(tmp);
	rf->int_en = tmp;

	tmp = devm_regmap_field_alloc(dev, regmap, apds9306_rf_int_persist_val);
	if (IS_ERR(tmp))
		return PTR_ERR(tmp);
	rf->int_persist_val = tmp;

	tmp = devm_regmap_field_alloc(dev, regmap, apds9306_rf_int_thresh_var_val);
	if (IS_ERR(tmp))
		return PTR_ERR(tmp);
	rf->int_thresh_var_val = tmp;

	return 0;
}

static int apds9306_power_state(struct apds9306_data *data, bool state)
{
	struct apds9306_regfields *rf = &data->rf;
	int ret;

	/* Reset not included as it causes ugly I2C bus error */
	if (state) {
		ret = regmap_field_write(rf->en, 1);
		if (ret)
			return ret;
		/* 5ms wake up time */
		fsleep(5000);
		return 0;
	}

	return regmap_field_write(rf->en, 0);
}

static int apds9306_read_data(struct apds9306_data *data, int *val, int reg)
{
	struct device *dev = data->dev;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct apds9306_regfields *rf = &data->rf;
	u64 ev_code;
	int ret, delay, intg_time, intg_time_idx, repeat_rate_idx, int_src;
	int status = 0;
	u8 buff[3];

	ret = pm_runtime_resume_and_get(data->dev);
	if (ret)
		return ret;

	ret = regmap_field_read(rf->intg_time, &intg_time_idx);
	if (ret)
		return ret;

	ret = regmap_field_read(rf->repeat_rate, &repeat_rate_idx);
	if (ret)
		return ret;

	ret = regmap_field_read(rf->int_src, &int_src);
	if (ret)
		return ret;

	intg_time = iio_gts_find_int_time_by_sel(&data->gts, intg_time_idx);
	if (intg_time < 0)
		return intg_time;

	/* Whichever is greater - integration time period or sampling period. */
	delay = max(intg_time, apds9306_repeat_rate_period[repeat_rate_idx]);

	/*
	 * Clear stale data flag that might have been set by the interrupt
	 * handler if it got data available flag set in the status reg.
	 */
	data->read_data_available = 0;

	/*
	 * If this function runs parallel with the interrupt handler, either
	 * this reads and clears the status registers or the interrupt handler
	 * does. The interrupt handler sets a flag for read data available
	 * in our private structure which we read here.
	 */
	ret = regmap_read_poll_timeout(data->regmap, APDS9306_MAIN_STATUS_REG,
				       status, data->read_data_available ||
				       (status & (APDS9306_ALS_DATA_STAT_MASK |
						  APDS9306_ALS_INT_STAT_MASK)),
				       APDS9306_ALS_READ_DATA_DELAY_US, delay * 2);
	if (ret)
		return ret;

	/* If we reach here before the interrupt handler we push an event */
	if ((status & APDS9306_ALS_INT_STAT_MASK)) {
		if (int_src == APDS9306_INT_SRC_ALS)
			ev_code = IIO_UNMOD_EVENT_CODE(IIO_LIGHT, 0,
						       IIO_EV_TYPE_THRESH,
						       IIO_EV_DIR_EITHER);
		else
			ev_code = IIO_MOD_EVENT_CODE(IIO_INTENSITY, 0,
						     IIO_MOD_LIGHT_CLEAR,
						     IIO_EV_TYPE_THRESH,
						     IIO_EV_DIR_EITHER);

		iio_push_event(indio_dev, ev_code, iio_get_time_ns(indio_dev));
	}

	ret = regmap_bulk_read(data->regmap, reg, buff, sizeof(buff));
	if (ret) {
		dev_err_ratelimited(dev, "read data failed\n");
		return ret;
	}

	*val = get_unaligned_le24(&buff);

	pm_runtime_mark_last_busy(data->dev);
	pm_runtime_put_autosuspend(data->dev);

	return 0;
}

static int apds9306_intg_time_get(struct apds9306_data *data, int *val2)
{
	struct apds9306_regfields *rf = &data->rf;
	int ret, intg_time_idx;

	ret = regmap_field_read(rf->intg_time, &intg_time_idx);
	if (ret)
		return ret;

	ret = iio_gts_find_int_time_by_sel(&data->gts, intg_time_idx);
	if (ret < 0)
		return ret;

	*val2 = ret;

	return 0;
}

static int apds9306_intg_time_set(struct apds9306_data *data, int val2)
{
	struct device *dev = data->dev;
	struct apds9306_regfields *rf = &data->rf;
	int ret, intg_old, gain_old, gain_new, gain_new_closest, intg_time_idx;
	int gain_idx;
	bool ok;

	if (!iio_gts_valid_time(&data->gts, val2)) {
		dev_err_ratelimited(dev, "Unsupported integration time %u\n", val2);
		return -EINVAL;
	}

	ret = regmap_field_read(rf->intg_time, &intg_time_idx);
	if (ret)
		return ret;

	ret = regmap_field_read(rf->gain, &gain_idx);
	if (ret)
		return ret;

	intg_old = iio_gts_find_int_time_by_sel(&data->gts, intg_time_idx);
	if (intg_old < 0)
		return intg_old;

	if (intg_old == val2)
		return 0;

	gain_old = iio_gts_find_gain_by_sel(&data->gts, gain_idx);
	if (gain_old < 0)
		return gain_old;

	iio_gts_find_new_gain_by_old_gain_time(&data->gts, gain_old, intg_old,
					       val2, &gain_new);

	if (gain_new < 0) {
		dev_err_ratelimited(dev, "Unsupported gain with time\n");
		return gain_new;
	}

	gain_new_closest = iio_find_closest_gain_low(&data->gts, gain_new, &ok);
	if (gain_new_closest < 0) {
		gain_new_closest = iio_gts_get_min_gain(&data->gts);
		if (gain_new_closest < 0)
			return gain_new_closest;
	}
	if (!ok)
		dev_dbg(dev, "Unable to find optimum gain, setting minimum");

	ret = iio_gts_find_sel_by_int_time(&data->gts, val2);
	if (ret < 0)
		return ret;

	ret = regmap_field_write(rf->intg_time, ret);
	if (ret)
		return ret;

	ret = iio_gts_find_sel_by_gain(&data->gts, gain_new_closest);
	if (ret < 0)
		return ret;

	return regmap_field_write(rf->gain, ret);
}

static int apds9306_sampling_freq_get(struct apds9306_data *data, int *val,
				      int *val2)
{
	struct apds9306_regfields *rf = &data->rf;
	int ret, repeat_rate_idx;

	ret = regmap_field_read(rf->repeat_rate, &repeat_rate_idx);
	if (ret)
		return ret;

	if (repeat_rate_idx >= ARRAY_SIZE(apds9306_repeat_rate_freq))
		return -EINVAL;

	*val = apds9306_repeat_rate_freq[repeat_rate_idx][0];
	*val2 = apds9306_repeat_rate_freq[repeat_rate_idx][1];

	return 0;
}

static int apds9306_sampling_freq_set(struct apds9306_data *data, int val,
				      int val2)
{
	struct apds9306_regfields *rf = &data->rf;
	int i;

	for (i = 0; i < ARRAY_SIZE(apds9306_repeat_rate_freq); i++) {
		if (apds9306_repeat_rate_freq[i][0] == val &&
		    apds9306_repeat_rate_freq[i][1] == val2)
			return regmap_field_write(rf->repeat_rate, i);
	}

	return -EINVAL;
}

static int apds9306_scale_get(struct apds9306_data *data, int *val, int *val2)
{
	struct apds9306_regfields *rf = &data->rf;
	int gain, intg, ret, intg_time_idx, gain_idx;

	ret = regmap_field_read(rf->gain, &gain_idx);
	if (ret)
		return ret;

	ret = regmap_field_read(rf->intg_time, &intg_time_idx);
	if (ret)
		return ret;

	gain = iio_gts_find_gain_by_sel(&data->gts, gain_idx);
	if (gain < 0)
		return gain;

	intg = iio_gts_find_int_time_by_sel(&data->gts, intg_time_idx);
	if (intg < 0)
		return intg;

	return iio_gts_get_scale(&data->gts, gain, intg, val, val2);
}

static int apds9306_scale_set(struct apds9306_data *data, int val, int val2)
{
	struct apds9306_regfields *rf = &data->rf;
	int i, ret, time_sel, gain_sel, intg_time_idx;

	ret = regmap_field_read(rf->intg_time, &intg_time_idx);
	if (ret)
		return ret;

	ret = iio_gts_find_gain_sel_for_scale_using_time(&data->gts,
					intg_time_idx, val, val2, &gain_sel);
	if (ret) {
		for (i = 0; i < data->gts.num_itime; i++) {
			time_sel = data->gts.itime_table[i].sel;

			if (time_sel == intg_time_idx)
				continue;

			ret = iio_gts_find_gain_sel_for_scale_using_time(&data->gts,
						time_sel, val, val2, &gain_sel);
			if (!ret)
				break;
		}
		if (ret)
			return -EINVAL;

		ret = regmap_field_write(rf->intg_time, time_sel);
		if (ret)
			return ret;
	}

	return regmap_field_write(rf->gain, gain_sel);
}

static int apds9306_event_period_get(struct apds9306_data *data, int *val)
{
	struct apds9306_regfields *rf = &data->rf;
	int period, ret;

	ret = regmap_field_read(rf->int_persist_val, &period);
	if (ret)
		return ret;

	if (!in_range(period, 0, APDS9306_ALS_PERSIST_NUM_VALS))
		return -EINVAL;

	*val = period;

	return ret;
}

static int apds9306_event_period_set(struct apds9306_data *data, int val)
{
	struct apds9306_regfields *rf = &data->rf;

	if (!in_range(val, 0, APDS9306_ALS_PERSIST_NUM_VALS))
		return -EINVAL;

	return regmap_field_write(rf->int_persist_val, val);
}

static int apds9306_event_thresh_get(struct apds9306_data *data, int dir,
				     int *val)
{
	int var, ret;
	u8 buff[3];

	if (dir == IIO_EV_DIR_RISING)
		var = APDS9306_ALS_THRES_UP_0_REG;
	else if (dir == IIO_EV_DIR_FALLING)
		var = APDS9306_ALS_THRES_LOW_0_REG;
	else
		return -EINVAL;

	ret = regmap_bulk_read(data->regmap, var, buff, sizeof(buff));
	if (ret)
		return ret;

	*val = get_unaligned_le24(&buff);

	return 0;
}

static int apds9306_event_thresh_set(struct apds9306_data *data, int dir,
				     int val)
{
	int var;
	u8 buff[3];

	if (dir == IIO_EV_DIR_RISING)
		var = APDS9306_ALS_THRES_UP_0_REG;
	else if (dir == IIO_EV_DIR_FALLING)
		var = APDS9306_ALS_THRES_LOW_0_REG;
	else
		return -EINVAL;

	if (!in_range(val, 0, APDS9306_ALS_THRES_VAL_MAX))
		return -EINVAL;

	put_unaligned_le24(val, buff);

	return regmap_bulk_write(data->regmap, var, buff, sizeof(buff));
}

static int apds9306_event_thresh_adaptive_get(struct apds9306_data *data, int *val)
{
	struct apds9306_regfields *rf = &data->rf;
	int thr_adpt, ret;

	ret = regmap_field_read(rf->int_thresh_var_val, &thr_adpt);
	if (ret)
		return ret;

	if (!in_range(thr_adpt, 0, APDS9306_ALS_THRES_VAR_NUM_VALS))
		return -EINVAL;

	*val = thr_adpt;

	return ret;
}

static int apds9306_event_thresh_adaptive_set(struct apds9306_data *data, int val)
{
	struct apds9306_regfields *rf = &data->rf;

	if (!in_range(val, 0, APDS9306_ALS_THRES_VAR_NUM_VALS))
		return -EINVAL;

	return regmap_field_write(rf->int_thresh_var_val, val);
}

static int apds9306_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan, int *val,
			     int *val2, long mask)
{
	struct apds9306_data *data = iio_priv(indio_dev);
	int ret, reg;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (chan->channel2 == IIO_MOD_LIGHT_CLEAR)
			reg = APDS9306_CLEAR_DATA_0_REG;
		else
			reg = APDS9306_ALS_DATA_0_REG;
		/*
		 * Changing device parameters during adc operation, resets
		 * the ADC which has to avoided.
		 */
		if (!iio_device_claim_direct(indio_dev))
			return -EBUSY;
		ret = apds9306_read_data(data, val, reg);
		iio_device_release_direct(indio_dev);
		if (ret)
			return ret;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_INT_TIME:
		ret = apds9306_intg_time_get(data, val2);
		if (ret)
			return ret;
		*val = 0;

		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_SAMP_FREQ:
		ret = apds9306_sampling_freq_get(data, val, val2);
		if (ret)
			return ret;

		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_SCALE:
		ret = apds9306_scale_get(data, val, val2);
		if (ret)
			return ret;

		return IIO_VAL_INT_PLUS_NANO;
	default:
		return -EINVAL;
	}
};

static int apds9306_read_avail(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       const int **vals, int *type, int *length,
			       long mask)
{
	struct apds9306_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_INT_TIME:
		return iio_gts_avail_times(&data->gts, vals, type, length);
	case IIO_CHAN_INFO_SCALE:
		return iio_gts_all_avail_scales(&data->gts, vals, type, length);
	case IIO_CHAN_INFO_SAMP_FREQ:
		*length = ARRAY_SIZE(apds9306_repeat_rate_freq) * 2;
		*vals = (const int *)apds9306_repeat_rate_freq;
		*type = IIO_VAL_INT_PLUS_MICRO;

		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
}

static int apds9306_write_raw_get_fmt(struct iio_dev *indio_dev,
				      struct iio_chan_spec const *chan,
				      long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		return IIO_VAL_INT_PLUS_NANO;
	case IIO_CHAN_INFO_INT_TIME:
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_SAMP_FREQ:
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static int apds9306_write_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan, int val,
			      int val2, long mask)
{
	struct apds9306_data *data = iio_priv(indio_dev);

	guard(mutex)(&data->mutex);

	switch (mask) {
	case IIO_CHAN_INFO_INT_TIME:
		if (val)
			return -EINVAL;
		return apds9306_intg_time_set(data, val2);
	case IIO_CHAN_INFO_SCALE:
		return apds9306_scale_set(data, val, val2);
	case IIO_CHAN_INFO_SAMP_FREQ:
		return apds9306_sampling_freq_set(data, val, val2);
	default:
		return -EINVAL;
	}
}

static irqreturn_t apds9306_irq_handler(int irq, void *priv)
{
	struct iio_dev *indio_dev = priv;
	struct apds9306_data *data = iio_priv(indio_dev);
	struct apds9306_regfields *rf = &data->rf;
	u64 ev_code;
	int ret, status, int_src;

	/*
	 * The interrupt line is released and the interrupt flag is
	 * cleared as a result of reading the status register. All the
	 * status flags are cleared as a result of this read.
	 */
	ret = regmap_read(data->regmap, APDS9306_MAIN_STATUS_REG, &status);
	if (ret < 0) {
		dev_err_ratelimited(data->dev, "status reg read failed\n");
		return IRQ_HANDLED;
	}

	ret = regmap_field_read(rf->int_src, &int_src);
	if (ret)
		return ret;

	if ((status & APDS9306_ALS_INT_STAT_MASK)) {
		if (int_src == APDS9306_INT_SRC_ALS)
			ev_code = IIO_UNMOD_EVENT_CODE(IIO_LIGHT, 0,
						       IIO_EV_TYPE_THRESH,
						       IIO_EV_DIR_EITHER);
		else
			ev_code = IIO_MOD_EVENT_CODE(IIO_INTENSITY, 0,
						     IIO_MOD_LIGHT_CLEAR,
						     IIO_EV_TYPE_THRESH,
						     IIO_EV_DIR_EITHER);

		iio_push_event(indio_dev, ev_code, iio_get_time_ns(indio_dev));
	}

	/*
	 * If a one-shot read through sysfs is underway at the same time
	 * as this interrupt handler is executing and a read data available
	 * flag was set, this flag is set to inform read_poll_timeout()
	 * to exit.
	 */
	if ((status & APDS9306_ALS_DATA_STAT_MASK))
		data->read_data_available = 1;

	return IRQ_HANDLED;
}

static int apds9306_read_event(struct iio_dev *indio_dev,
			       const struct iio_chan_spec *chan,
			       enum iio_event_type type,
			       enum iio_event_direction dir,
			       enum iio_event_info info,
			       int *val, int *val2)
{
	struct apds9306_data *data = iio_priv(indio_dev);
	int ret;

	switch (type) {
	case IIO_EV_TYPE_THRESH:
		if (dir == IIO_EV_DIR_EITHER && info == IIO_EV_INFO_PERIOD)
			ret = apds9306_event_period_get(data, val);
		else
			ret = apds9306_event_thresh_get(data, dir, val);
		if (ret)
			return ret;

		return IIO_VAL_INT;
	case IIO_EV_TYPE_THRESH_ADAPTIVE:
		ret = apds9306_event_thresh_adaptive_get(data, val);
		if (ret)
			return ret;

		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int apds9306_write_event(struct iio_dev *indio_dev,
				const struct iio_chan_spec *chan,
				enum iio_event_type type,
				enum iio_event_direction dir,
				enum iio_event_info info,
				int val, int val2)
{
	struct apds9306_data *data = iio_priv(indio_dev);

	switch (type) {
	case IIO_EV_TYPE_THRESH:
		if (dir == IIO_EV_DIR_EITHER && info == IIO_EV_INFO_PERIOD)
			return apds9306_event_period_set(data, val);

		return apds9306_event_thresh_set(data, dir, val);
	case IIO_EV_TYPE_THRESH_ADAPTIVE:
		return apds9306_event_thresh_adaptive_set(data, val);
	default:
		return -EINVAL;
	}
}

static int apds9306_read_event_config(struct iio_dev *indio_dev,
				      const struct iio_chan_spec *chan,
				      enum iio_event_type type,
				      enum iio_event_direction dir)
{
	struct apds9306_data *data = iio_priv(indio_dev);
	struct apds9306_regfields *rf = &data->rf;
	int int_en, int_src, ret;

	switch (type) {
	case IIO_EV_TYPE_THRESH: {
		guard(mutex)(&data->mutex);

		ret = regmap_field_read(rf->int_src, &int_src);
		if (ret)
			return ret;

		ret = regmap_field_read(rf->int_en, &int_en);
		if (ret)
			return ret;

		if (chan->type == IIO_LIGHT)
			return int_en && (int_src == APDS9306_INT_SRC_ALS);

		if (chan->type == IIO_INTENSITY)
			return int_en && (int_src == APDS9306_INT_SRC_CLEAR);

		return -EINVAL;
	}
	case IIO_EV_TYPE_THRESH_ADAPTIVE:
		ret = regmap_field_read(rf->int_thresh_var_en, &int_en);
		if (ret)
			return ret;

		return int_en;
	default:
		return -EINVAL;
	}
}

static int apds9306_write_event_config(struct iio_dev *indio_dev,
				       const struct iio_chan_spec *chan,
				       enum iio_event_type type,
				       enum iio_event_direction dir,
				       bool state)
{
	struct apds9306_data *data = iio_priv(indio_dev);
	struct apds9306_regfields *rf = &data->rf;
	int ret, enabled;

	switch (type) {
	case IIO_EV_TYPE_THRESH: {
		guard(mutex)(&data->mutex);

		ret = regmap_field_read(rf->int_en, &enabled);
		if (ret)
			return ret;

		/*
		 * If interrupt is enabled, the channel is set before enabling
		 * the interrupt. In case of disable, no need to switch
		 * channels. In case of different channel is selected while
		 * interrupt in on, just change the channel.
		 */
		if (state) {
			if (chan->type == IIO_LIGHT)
				ret = regmap_field_write(rf->int_src, 1);
			else if (chan->type == IIO_INTENSITY)
				ret = regmap_field_write(rf->int_src, 0);
			else
				return -EINVAL;

			if (ret)
				return ret;

			if (enabled)
				return 0;

			ret = regmap_field_write(rf->int_en, 1);
			if (ret)
				return ret;

			return pm_runtime_resume_and_get(data->dev);
		} else {
			if (!enabled)
				return 0;

			ret = regmap_field_write(rf->int_en, 0);
			if (ret)
				return ret;

			pm_runtime_mark_last_busy(data->dev);
			pm_runtime_put_autosuspend(data->dev);

			return 0;
		}
	}
	case IIO_EV_TYPE_THRESH_ADAPTIVE:
		return regmap_field_write(rf->int_thresh_var_en, state);
	default:
		return -EINVAL;
	}
}

static const struct iio_info apds9306_info_no_events = {
	.read_avail = apds9306_read_avail,
	.read_raw = apds9306_read_raw,
	.write_raw = apds9306_write_raw,
	.write_raw_get_fmt = apds9306_write_raw_get_fmt,
};

static const struct iio_info apds9306_info = {
	.read_avail = apds9306_read_avail,
	.read_raw = apds9306_read_raw,
	.write_raw = apds9306_write_raw,
	.write_raw_get_fmt = apds9306_write_raw_get_fmt,
	.read_event_value = apds9306_read_event,
	.write_event_value = apds9306_write_event,
	.read_event_config = apds9306_read_event_config,
	.write_event_config = apds9306_write_event_config,
	.event_attrs = &apds9306_event_attr_group,
};

static int apds9306_init_iio_gts(struct apds9306_data *data)
{
	int i, ret, part_id;

	ret = regmap_read(data->regmap, APDS9306_PART_ID_REG, &part_id);
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(apds9306_gts_mul); i++)
		if (part_id == apds9306_gts_mul[i].part_id)
			break;

	if (i == ARRAY_SIZE(apds9306_gts_mul))
		return -ENOENT;

	return devm_iio_init_iio_gts(data->dev,
				     apds9306_gts_mul[i].max_scale_int,
				     apds9306_gts_mul[i].max_scale_nano,
				     apds9306_gains, ARRAY_SIZE(apds9306_gains),
				     apds9306_itimes, ARRAY_SIZE(apds9306_itimes),
				     &data->gts);
}

static void apds9306_powerdown(void *ptr)
{
	struct apds9306_data *data = (struct apds9306_data *)ptr;
	struct apds9306_regfields *rf = &data->rf;
	int ret;

	ret = regmap_field_write(rf->int_thresh_var_en, 0);
	if (ret)
		return;

	ret = regmap_field_write(rf->int_en, 0);
	if (ret)
		return;

	apds9306_power_state(data, false);
}

static int apds9306_device_init(struct apds9306_data *data)
{
	struct apds9306_regfields *rf = &data->rf;
	int ret;

	ret = apds9306_init_iio_gts(data);
	if (ret)
		return ret;

	ret = regmap_field_write(rf->intg_time, APDS9306_MEAS_MODE_100MS);
	if (ret)
		return ret;

	ret = regmap_field_write(rf->repeat_rate, APDS9306_SAMP_FREQ_10HZ);
	if (ret)
		return ret;

	ret = regmap_field_write(rf->gain, APDS9306_GSEL_3X);
	if (ret)
		return ret;

	ret = regmap_field_write(rf->int_src, APDS9306_INT_SRC_ALS);
	if (ret)
		return ret;

	ret = regmap_field_write(rf->int_en, 0);
	if (ret)
		return ret;

	return regmap_field_write(rf->int_thresh_var_en, 0);
}

static int apds9306_pm_init(struct apds9306_data *data)
{
	struct device *dev = data->dev;
	int ret;

	ret = apds9306_power_state(data, true);
	if (ret)
		return ret;

	ret = pm_runtime_set_active(dev);
	if (ret)
		return ret;

	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return ret;

	pm_runtime_set_autosuspend_delay(dev, 5000);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_get(dev);

	return 0;
}

static int apds9306_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct apds9306_data *data;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);

	mutex_init(&data->mutex);

	data->regmap = devm_regmap_init_i2c(client, &apds9306_regmap);
	if (IS_ERR(data->regmap))
		return dev_err_probe(dev, PTR_ERR(data->regmap),
				     "regmap initialization failed\n");

	data->dev = dev;
	i2c_set_clientdata(client, indio_dev);

	ret = apds9306_regfield_init(data);
	if (ret)
		return dev_err_probe(dev, ret, "regfield initialization failed\n");

	ret = devm_regulator_get_enable(dev, "vdd");
	if (ret)
		return dev_err_probe(dev, ret, "Failed to enable regulator\n");

	indio_dev->name = "apds9306";
	indio_dev->modes = INDIO_DIRECT_MODE;
	if (client->irq) {
		indio_dev->info = &apds9306_info;
		indio_dev->channels = apds9306_channels_with_events;
		indio_dev->num_channels = ARRAY_SIZE(apds9306_channels_with_events);
		ret = devm_request_threaded_irq(dev, client->irq, NULL,
						apds9306_irq_handler, IRQF_ONESHOT,
						"apds9306_event", indio_dev);
		if (ret)
			return dev_err_probe(dev, ret,
					     "failed to assign interrupt.\n");
	} else {
		indio_dev->info = &apds9306_info_no_events;
		indio_dev->channels = apds9306_channels_without_events;
		indio_dev->num_channels =
				ARRAY_SIZE(apds9306_channels_without_events);
	}

	ret = apds9306_pm_init(data);
	if (ret)
		return dev_err_probe(dev, ret, "failed pm init\n");

	ret = apds9306_device_init(data);
	if (ret)
		return dev_err_probe(dev, ret, "failed to init device\n");

	ret = devm_add_action_or_reset(dev, apds9306_powerdown, data);
	if (ret)
		return dev_err_probe(dev, ret, "failed to add action or reset\n");

	ret = devm_iio_device_register(dev, indio_dev);
	if (ret)
		return dev_err_probe(dev, ret, "failed iio device registration\n");

	pm_runtime_put_autosuspend(dev);

	return 0;
}

static int apds9306_runtime_suspend(struct device *dev)
{
	struct apds9306_data *data = iio_priv(dev_get_drvdata(dev));

	return apds9306_power_state(data, false);
}

static int apds9306_runtime_resume(struct device *dev)
{
	struct apds9306_data *data = iio_priv(dev_get_drvdata(dev));

	return apds9306_power_state(data, true);
}

static DEFINE_RUNTIME_DEV_PM_OPS(apds9306_pm_ops,
				 apds9306_runtime_suspend,
				 apds9306_runtime_resume,
				 NULL);

static const struct of_device_id apds9306_of_match[] = {
	{ .compatible = "avago,apds9306" },
	{ }
};
MODULE_DEVICE_TABLE(of, apds9306_of_match);

static struct i2c_driver apds9306_driver = {
	.driver = {
		.name = "apds9306",
		.pm = pm_ptr(&apds9306_pm_ops),
		.of_match_table = apds9306_of_match,
	},
	.probe = apds9306_probe,
};
module_i2c_driver(apds9306_driver);

MODULE_AUTHOR("Subhajit Ghosh <subhajit.ghosh@tweaklogic.com>");
MODULE_DESCRIPTION("APDS9306 Ambient Light Sensor driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_GTS_HELPER");
