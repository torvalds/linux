// SPDX-License-Identifier: GPL-2.0-only
/*
 * ADS1015 - Texas Instruments Analog-to-Digital Converter
 *
 * Copyright (c) 2016, Intel Corporation.
 *
 * IIO driver for ADS1015 ADC 7-bit I2C slave address:
 *	* 0x48 - ADDR connected to Ground
 *	* 0x49 - ADDR connected to Vdd
 *	* 0x4A - ADDR connected to SDA
 *	* 0x4B - ADDR connected to SCL
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/i2c.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/pm_runtime.h>
#include <linux/mutex.h>
#include <linux/delay.h>

#include <linux/iio/iio.h>
#include <linux/iio/types.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/events.h>
#include <linux/iio/buffer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>

#define ADS1015_DRV_NAME "ads1015"

#define ADS1015_CHANNELS 8

#define ADS1015_CONV_REG	0x00
#define ADS1015_CFG_REG		0x01
#define ADS1015_LO_THRESH_REG	0x02
#define ADS1015_HI_THRESH_REG	0x03

#define ADS1015_CFG_COMP_QUE_SHIFT	0
#define ADS1015_CFG_COMP_LAT_SHIFT	2
#define ADS1015_CFG_COMP_POL_SHIFT	3
#define ADS1015_CFG_COMP_MODE_SHIFT	4
#define ADS1015_CFG_DR_SHIFT	5
#define ADS1015_CFG_MOD_SHIFT	8
#define ADS1015_CFG_PGA_SHIFT	9
#define ADS1015_CFG_MUX_SHIFT	12

#define ADS1015_CFG_COMP_QUE_MASK	GENMASK(1, 0)
#define ADS1015_CFG_COMP_LAT_MASK	BIT(2)
#define ADS1015_CFG_COMP_POL_MASK	BIT(3)
#define ADS1015_CFG_COMP_MODE_MASK	BIT(4)
#define ADS1015_CFG_DR_MASK	GENMASK(7, 5)
#define ADS1015_CFG_MOD_MASK	BIT(8)
#define ADS1015_CFG_PGA_MASK	GENMASK(11, 9)
#define ADS1015_CFG_MUX_MASK	GENMASK(14, 12)

/* Comparator queue and disable field */
#define ADS1015_CFG_COMP_DISABLE	3

/* Comparator polarity field */
#define ADS1015_CFG_COMP_POL_LOW	0
#define ADS1015_CFG_COMP_POL_HIGH	1

/* Comparator mode field */
#define ADS1015_CFG_COMP_MODE_TRAD	0
#define ADS1015_CFG_COMP_MODE_WINDOW	1

/* device operating modes */
#define ADS1015_CONTINUOUS	0
#define ADS1015_SINGLESHOT	1

#define ADS1015_SLEEP_DELAY_MS		2000
#define ADS1015_DEFAULT_PGA		2
#define ADS1015_DEFAULT_DATA_RATE	4
#define ADS1015_DEFAULT_CHAN		0

struct ads1015_chip_data {
	struct iio_chan_spec const	*channels;
	int				num_channels;
	const struct iio_info		*info;
	const int			*data_rate;
	const int			data_rate_len;
	const int			*scale;
	const int			scale_len;
	bool				has_comparator;
};

enum ads1015_channels {
	ADS1015_AIN0_AIN1 = 0,
	ADS1015_AIN0_AIN3,
	ADS1015_AIN1_AIN3,
	ADS1015_AIN2_AIN3,
	ADS1015_AIN0,
	ADS1015_AIN1,
	ADS1015_AIN2,
	ADS1015_AIN3,
	ADS1015_TIMESTAMP,
};

static const int ads1015_data_rate[] = {
	128, 250, 490, 920, 1600, 2400, 3300, 3300
};

static const int ads1115_data_rate[] = {
	8, 16, 32, 64, 128, 250, 475, 860
};

/*
 * Translation from PGA bits to full-scale positive and negative input voltage
 * range in mV
 */
static const int ads1015_fullscale_range[] = {
	6144, 4096, 2048, 1024, 512, 256, 256, 256
};

static const int ads1015_scale[] = {	/* 12bit ADC */
	256, 11,
	512, 11,
	1024, 11,
	2048, 11,
	4096, 11,
	6144, 11
};

static const int ads1115_scale[] = {	/* 16bit ADC */
	256, 15,
	512, 15,
	1024, 15,
	2048, 15,
	4096, 15,
	6144, 15
};

/*
 * Translation from COMP_QUE field value to the number of successive readings
 * exceed the threshold values before an interrupt is generated
 */
static const int ads1015_comp_queue[] = { 1, 2, 4 };

static const struct iio_event_spec ads1015_events[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE) |
				BIT(IIO_EV_INFO_ENABLE),
	}, {
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_FALLING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	}, {
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_EITHER,
		.mask_separate = BIT(IIO_EV_INFO_ENABLE) |
				BIT(IIO_EV_INFO_PERIOD),
	},
};

/*
 * Compile-time check whether _fitbits can accommodate up to _testbits
 * bits. Returns _fitbits on success, fails to compile otherwise.
 *
 * The test works such that it multiplies constant _fitbits by constant
 * double-negation of size of a non-empty structure, i.e. it multiplies
 * constant _fitbits by constant 1 in each successful compilation case.
 * The non-empty structure may contain C11 _Static_assert(), make use of
 * this and place the kernel variant of static assert in there, so that
 * it performs the compile-time check for _testbits <= _fitbits. Note
 * that it is not possible to directly use static_assert in compound
 * statements, hence this convoluted construct.
 */
#define FIT_CHECK(_testbits, _fitbits)					\
	(								\
		(_fitbits) *						\
		!!sizeof(struct {					\
			static_assert((_testbits) <= (_fitbits));	\
			int pad;					\
		})							\
	)

#define ADS1015_V_CHAN(_chan, _addr, _realbits, _shift, _event_spec, _num_event_specs) { \
	.type = IIO_VOLTAGE,					\
	.indexed = 1,						\
	.address = _addr,					\
	.channel = _chan,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |		\
				BIT(IIO_CHAN_INFO_SCALE) |	\
				BIT(IIO_CHAN_INFO_SAMP_FREQ),	\
	.info_mask_shared_by_all_available =			\
				BIT(IIO_CHAN_INFO_SCALE) |	\
				BIT(IIO_CHAN_INFO_SAMP_FREQ),	\
	.scan_index = _addr,					\
	.scan_type = {						\
		.sign = 's',					\
		.realbits = (_realbits),			\
		.storagebits = FIT_CHECK((_realbits) + (_shift), 16),	\
		.shift = (_shift),				\
		.endianness = IIO_CPU,				\
	},							\
	.event_spec = (_event_spec),				\
	.num_event_specs = (_num_event_specs),			\
	.datasheet_name = "AIN"#_chan,				\
}

#define ADS1015_V_DIFF_CHAN(_chan, _chan2, _addr, _realbits, _shift, _event_spec, _num_event_specs) { \
	.type = IIO_VOLTAGE,					\
	.differential = 1,					\
	.indexed = 1,						\
	.address = _addr,					\
	.channel = _chan,					\
	.channel2 = _chan2,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |		\
				BIT(IIO_CHAN_INFO_SCALE) |	\
				BIT(IIO_CHAN_INFO_SAMP_FREQ),	\
	.info_mask_shared_by_all_available =			\
				BIT(IIO_CHAN_INFO_SCALE) |	\
				BIT(IIO_CHAN_INFO_SAMP_FREQ),	\
	.scan_index = _addr,					\
	.scan_type = {						\
		.sign = 's',					\
		.realbits = (_realbits),			\
		.storagebits = FIT_CHECK((_realbits) + (_shift), 16),	\
		.shift = (_shift),				\
		.endianness = IIO_CPU,				\
	},							\
	.event_spec = (_event_spec),				\
	.num_event_specs = (_num_event_specs),			\
	.datasheet_name = "AIN"#_chan"-AIN"#_chan2,		\
}

struct ads1015_channel_data {
	bool enabled;
	unsigned int pga;
	unsigned int data_rate;
};

struct ads1015_thresh_data {
	unsigned int comp_queue;
	int high_thresh;
	int low_thresh;
};

struct ads1015_data {
	struct regmap *regmap;
	/*
	 * Protects ADC ops, e.g: concurrent sysfs/buffered
	 * data reads, configuration updates
	 */
	struct mutex lock;
	struct ads1015_channel_data channel_data[ADS1015_CHANNELS];

	unsigned int event_channel;
	unsigned int comp_mode;
	struct ads1015_thresh_data thresh_data[ADS1015_CHANNELS];

	const struct ads1015_chip_data *chip;
	/*
	 * Set to true when the ADC is switched to the continuous-conversion
	 * mode and exits from a power-down state.  This flag is used to avoid
	 * getting the stale result from the conversion register.
	 */
	bool conv_invalid;
};

static bool ads1015_event_channel_enabled(struct ads1015_data *data)
{
	return (data->event_channel != ADS1015_CHANNELS);
}

static void ads1015_event_channel_enable(struct ads1015_data *data, int chan,
					 int comp_mode)
{
	WARN_ON(ads1015_event_channel_enabled(data));

	data->event_channel = chan;
	data->comp_mode = comp_mode;
}

static void ads1015_event_channel_disable(struct ads1015_data *data, int chan)
{
	data->event_channel = ADS1015_CHANNELS;
}

static const struct regmap_range ads1015_writeable_ranges[] = {
	regmap_reg_range(ADS1015_CFG_REG, ADS1015_HI_THRESH_REG),
};

static const struct regmap_access_table ads1015_writeable_table = {
	.yes_ranges = ads1015_writeable_ranges,
	.n_yes_ranges = ARRAY_SIZE(ads1015_writeable_ranges),
};

static const struct regmap_config ads1015_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,
	.max_register = ADS1015_HI_THRESH_REG,
	.wr_table = &ads1015_writeable_table,
};

static const struct regmap_range tla2024_writeable_ranges[] = {
	regmap_reg_range(ADS1015_CFG_REG, ADS1015_CFG_REG),
};

static const struct regmap_access_table tla2024_writeable_table = {
	.yes_ranges = tla2024_writeable_ranges,
	.n_yes_ranges = ARRAY_SIZE(tla2024_writeable_ranges),
};

static const struct regmap_config tla2024_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,
	.max_register = ADS1015_CFG_REG,
	.wr_table = &tla2024_writeable_table,
};

static const struct iio_chan_spec ads1015_channels[] = {
	ADS1015_V_DIFF_CHAN(0, 1, ADS1015_AIN0_AIN1, 12, 4,
			    ads1015_events, ARRAY_SIZE(ads1015_events)),
	ADS1015_V_DIFF_CHAN(0, 3, ADS1015_AIN0_AIN3, 12, 4,
			    ads1015_events, ARRAY_SIZE(ads1015_events)),
	ADS1015_V_DIFF_CHAN(1, 3, ADS1015_AIN1_AIN3, 12, 4,
			    ads1015_events, ARRAY_SIZE(ads1015_events)),
	ADS1015_V_DIFF_CHAN(2, 3, ADS1015_AIN2_AIN3, 12, 4,
			    ads1015_events, ARRAY_SIZE(ads1015_events)),
	ADS1015_V_CHAN(0, ADS1015_AIN0, 12, 4,
		       ads1015_events, ARRAY_SIZE(ads1015_events)),
	ADS1015_V_CHAN(1, ADS1015_AIN1, 12, 4,
		       ads1015_events, ARRAY_SIZE(ads1015_events)),
	ADS1015_V_CHAN(2, ADS1015_AIN2, 12, 4,
		       ads1015_events, ARRAY_SIZE(ads1015_events)),
	ADS1015_V_CHAN(3, ADS1015_AIN3, 12, 4,
		       ads1015_events, ARRAY_SIZE(ads1015_events)),
	IIO_CHAN_SOFT_TIMESTAMP(ADS1015_TIMESTAMP),
};

static const struct iio_chan_spec ads1115_channels[] = {
	ADS1015_V_DIFF_CHAN(0, 1, ADS1015_AIN0_AIN1, 16, 0,
			    ads1015_events, ARRAY_SIZE(ads1015_events)),
	ADS1015_V_DIFF_CHAN(0, 3, ADS1015_AIN0_AIN3, 16, 0,
			    ads1015_events, ARRAY_SIZE(ads1015_events)),
	ADS1015_V_DIFF_CHAN(1, 3, ADS1015_AIN1_AIN3, 16, 0,
			    ads1015_events, ARRAY_SIZE(ads1015_events)),
	ADS1015_V_DIFF_CHAN(2, 3, ADS1015_AIN2_AIN3, 16, 0,
			    ads1015_events, ARRAY_SIZE(ads1015_events)),
	ADS1015_V_CHAN(0, ADS1015_AIN0, 16, 0,
		       ads1015_events, ARRAY_SIZE(ads1015_events)),
	ADS1015_V_CHAN(1, ADS1015_AIN1, 16, 0,
		       ads1015_events, ARRAY_SIZE(ads1015_events)),
	ADS1015_V_CHAN(2, ADS1015_AIN2, 16, 0,
		       ads1015_events, ARRAY_SIZE(ads1015_events)),
	ADS1015_V_CHAN(3, ADS1015_AIN3, 16, 0,
		       ads1015_events, ARRAY_SIZE(ads1015_events)),
	IIO_CHAN_SOFT_TIMESTAMP(ADS1015_TIMESTAMP),
};

static const struct iio_chan_spec tla2024_channels[] = {
	ADS1015_V_DIFF_CHAN(0, 1, ADS1015_AIN0_AIN1, 12, 4, NULL, 0),
	ADS1015_V_DIFF_CHAN(0, 3, ADS1015_AIN0_AIN3, 12, 4, NULL, 0),
	ADS1015_V_DIFF_CHAN(1, 3, ADS1015_AIN1_AIN3, 12, 4, NULL, 0),
	ADS1015_V_DIFF_CHAN(2, 3, ADS1015_AIN2_AIN3, 12, 4, NULL, 0),
	ADS1015_V_CHAN(0, ADS1015_AIN0, 12, 4, NULL, 0),
	ADS1015_V_CHAN(1, ADS1015_AIN1, 12, 4, NULL, 0),
	ADS1015_V_CHAN(2, ADS1015_AIN2, 12, 4, NULL, 0),
	ADS1015_V_CHAN(3, ADS1015_AIN3, 12, 4, NULL, 0),
	IIO_CHAN_SOFT_TIMESTAMP(ADS1015_TIMESTAMP),
};


#ifdef CONFIG_PM
static int ads1015_set_power_state(struct ads1015_data *data, bool on)
{
	int ret;
	struct device *dev = regmap_get_device(data->regmap);

	if (on) {
		ret = pm_runtime_resume_and_get(dev);
	} else {
		pm_runtime_mark_last_busy(dev);
		ret = pm_runtime_put_autosuspend(dev);
	}

	return ret < 0 ? ret : 0;
}

#else /* !CONFIG_PM */

static int ads1015_set_power_state(struct ads1015_data *data, bool on)
{
	return 0;
}

#endif /* !CONFIG_PM */

static
int ads1015_get_adc_result(struct ads1015_data *data, int chan, int *val)
{
	const int *data_rate = data->chip->data_rate;
	int ret, pga, dr, dr_old, conv_time;
	unsigned int old, mask, cfg;

	if (chan < 0 || chan >= ADS1015_CHANNELS)
		return -EINVAL;

	ret = regmap_read(data->regmap, ADS1015_CFG_REG, &old);
	if (ret)
		return ret;

	pga = data->channel_data[chan].pga;
	dr = data->channel_data[chan].data_rate;
	mask = ADS1015_CFG_MUX_MASK | ADS1015_CFG_PGA_MASK |
		ADS1015_CFG_DR_MASK;
	cfg = chan << ADS1015_CFG_MUX_SHIFT | pga << ADS1015_CFG_PGA_SHIFT |
		dr << ADS1015_CFG_DR_SHIFT;

	if (ads1015_event_channel_enabled(data)) {
		mask |= ADS1015_CFG_COMP_QUE_MASK | ADS1015_CFG_COMP_MODE_MASK;
		cfg |= data->thresh_data[chan].comp_queue <<
				ADS1015_CFG_COMP_QUE_SHIFT |
			data->comp_mode <<
				ADS1015_CFG_COMP_MODE_SHIFT;
	}

	cfg = (old & ~mask) | (cfg & mask);
	if (old != cfg) {
		ret = regmap_write(data->regmap, ADS1015_CFG_REG, cfg);
		if (ret)
			return ret;
		data->conv_invalid = true;
	}
	if (data->conv_invalid) {
		dr_old = (old & ADS1015_CFG_DR_MASK) >> ADS1015_CFG_DR_SHIFT;
		conv_time = DIV_ROUND_UP(USEC_PER_SEC, data_rate[dr_old]);
		conv_time += DIV_ROUND_UP(USEC_PER_SEC, data_rate[dr]);
		conv_time += conv_time / 10; /* 10% internal clock inaccuracy */
		usleep_range(conv_time, conv_time + 1);
		data->conv_invalid = false;
	}

	return regmap_read(data->regmap, ADS1015_CONV_REG, val);
}

static irqreturn_t ads1015_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct ads1015_data *data = iio_priv(indio_dev);
	/* Ensure natural alignment of timestamp */
	struct {
		s16 chan;
		s64 timestamp __aligned(8);
	} scan;
	int chan, ret, res;

	memset(&scan, 0, sizeof(scan));

	mutex_lock(&data->lock);
	chan = find_first_bit(indio_dev->active_scan_mask,
			      indio_dev->masklength);
	ret = ads1015_get_adc_result(data, chan, &res);
	if (ret < 0) {
		mutex_unlock(&data->lock);
		goto err;
	}

	scan.chan = res;
	mutex_unlock(&data->lock);

	iio_push_to_buffers_with_timestamp(indio_dev, &scan,
					   iio_get_time_ns(indio_dev));

err:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static int ads1015_set_scale(struct ads1015_data *data,
			     struct iio_chan_spec const *chan,
			     int scale, int uscale)
{
	int i;
	int fullscale = div_s64((scale * 1000000LL + uscale) <<
				(chan->scan_type.realbits - 1), 1000000);

	for (i = 0; i < ARRAY_SIZE(ads1015_fullscale_range); i++) {
		if (ads1015_fullscale_range[i] == fullscale) {
			data->channel_data[chan->address].pga = i;
			return 0;
		}
	}

	return -EINVAL;
}

static int ads1015_set_data_rate(struct ads1015_data *data, int chan, int rate)
{
	int i;

	for (i = 0; i < data->chip->data_rate_len; i++) {
		if (data->chip->data_rate[i] == rate) {
			data->channel_data[chan].data_rate = i;
			return 0;
		}
	}

	return -EINVAL;
}

static int ads1015_read_avail(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      const int **vals, int *type, int *length,
			      long mask)
{
	struct ads1015_data *data = iio_priv(indio_dev);

	if (chan->type != IIO_VOLTAGE)
		return -EINVAL;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		*type = IIO_VAL_FRACTIONAL_LOG2;
		*vals =  data->chip->scale;
		*length = data->chip->scale_len;
		return IIO_AVAIL_LIST;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*type = IIO_VAL_INT;
		*vals = data->chip->data_rate;
		*length = data->chip->data_rate_len;
		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
}

static int ads1015_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan, int *val,
			    int *val2, long mask)
{
	int ret, idx;
	struct ads1015_data *data = iio_priv(indio_dev);

	mutex_lock(&data->lock);
	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			break;

		if (ads1015_event_channel_enabled(data) &&
				data->event_channel != chan->address) {
			ret = -EBUSY;
			goto release_direct;
		}

		ret = ads1015_set_power_state(data, true);
		if (ret < 0)
			goto release_direct;

		ret = ads1015_get_adc_result(data, chan->address, val);
		if (ret < 0) {
			ads1015_set_power_state(data, false);
			goto release_direct;
		}

		*val = sign_extend32(*val >> chan->scan_type.shift,
				     chan->scan_type.realbits - 1);

		ret = ads1015_set_power_state(data, false);
		if (ret < 0)
			goto release_direct;

		ret = IIO_VAL_INT;
release_direct:
		iio_device_release_direct_mode(indio_dev);
		break;
	case IIO_CHAN_INFO_SCALE:
		idx = data->channel_data[chan->address].pga;
		*val = ads1015_fullscale_range[idx];
		*val2 = chan->scan_type.realbits - 1;
		ret = IIO_VAL_FRACTIONAL_LOG2;
		break;
	case IIO_CHAN_INFO_SAMP_FREQ:
		idx = data->channel_data[chan->address].data_rate;
		*val = data->chip->data_rate[idx];
		ret = IIO_VAL_INT;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	mutex_unlock(&data->lock);

	return ret;
}

static int ads1015_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan, int val,
			     int val2, long mask)
{
	struct ads1015_data *data = iio_priv(indio_dev);
	int ret;

	mutex_lock(&data->lock);
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		ret = ads1015_set_scale(data, chan, val, val2);
		break;
	case IIO_CHAN_INFO_SAMP_FREQ:
		ret = ads1015_set_data_rate(data, chan->address, val);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	mutex_unlock(&data->lock);

	return ret;
}

static int ads1015_read_event(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, enum iio_event_type type,
	enum iio_event_direction dir, enum iio_event_info info, int *val,
	int *val2)
{
	struct ads1015_data *data = iio_priv(indio_dev);
	int ret;
	unsigned int comp_queue;
	int period;
	int dr;

	mutex_lock(&data->lock);

	switch (info) {
	case IIO_EV_INFO_VALUE:
		*val = (dir == IIO_EV_DIR_RISING) ?
			data->thresh_data[chan->address].high_thresh :
			data->thresh_data[chan->address].low_thresh;
		ret = IIO_VAL_INT;
		break;
	case IIO_EV_INFO_PERIOD:
		dr = data->channel_data[chan->address].data_rate;
		comp_queue = data->thresh_data[chan->address].comp_queue;
		period = ads1015_comp_queue[comp_queue] *
			USEC_PER_SEC / data->chip->data_rate[dr];

		*val = period / USEC_PER_SEC;
		*val2 = period % USEC_PER_SEC;
		ret = IIO_VAL_INT_PLUS_MICRO;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	mutex_unlock(&data->lock);

	return ret;
}

static int ads1015_write_event(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, enum iio_event_type type,
	enum iio_event_direction dir, enum iio_event_info info, int val,
	int val2)
{
	struct ads1015_data *data = iio_priv(indio_dev);
	const int *data_rate = data->chip->data_rate;
	int realbits = chan->scan_type.realbits;
	int ret = 0;
	long long period;
	int i;
	int dr;

	mutex_lock(&data->lock);

	switch (info) {
	case IIO_EV_INFO_VALUE:
		if (val >= 1 << (realbits - 1) || val < -1 << (realbits - 1)) {
			ret = -EINVAL;
			break;
		}
		if (dir == IIO_EV_DIR_RISING)
			data->thresh_data[chan->address].high_thresh = val;
		else
			data->thresh_data[chan->address].low_thresh = val;
		break;
	case IIO_EV_INFO_PERIOD:
		dr = data->channel_data[chan->address].data_rate;
		period = val * USEC_PER_SEC + val2;

		for (i = 0; i < ARRAY_SIZE(ads1015_comp_queue) - 1; i++) {
			if (period <= ads1015_comp_queue[i] *
					USEC_PER_SEC / data_rate[dr])
				break;
		}
		data->thresh_data[chan->address].comp_queue = i;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	mutex_unlock(&data->lock);

	return ret;
}

static int ads1015_read_event_config(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, enum iio_event_type type,
	enum iio_event_direction dir)
{
	struct ads1015_data *data = iio_priv(indio_dev);
	int ret = 0;

	mutex_lock(&data->lock);
	if (data->event_channel == chan->address) {
		switch (dir) {
		case IIO_EV_DIR_RISING:
			ret = 1;
			break;
		case IIO_EV_DIR_EITHER:
			ret = (data->comp_mode == ADS1015_CFG_COMP_MODE_WINDOW);
			break;
		default:
			ret = -EINVAL;
			break;
		}
	}
	mutex_unlock(&data->lock);

	return ret;
}

static int ads1015_enable_event_config(struct ads1015_data *data,
	const struct iio_chan_spec *chan, int comp_mode)
{
	int low_thresh = data->thresh_data[chan->address].low_thresh;
	int high_thresh = data->thresh_data[chan->address].high_thresh;
	int ret;
	unsigned int val;

	if (ads1015_event_channel_enabled(data)) {
		if (data->event_channel != chan->address ||
			(data->comp_mode == ADS1015_CFG_COMP_MODE_TRAD &&
				comp_mode == ADS1015_CFG_COMP_MODE_WINDOW))
			return -EBUSY;

		return 0;
	}

	if (comp_mode == ADS1015_CFG_COMP_MODE_TRAD) {
		low_thresh = max(-1 << (chan->scan_type.realbits - 1),
				high_thresh - 1);
	}
	ret = regmap_write(data->regmap, ADS1015_LO_THRESH_REG,
			low_thresh << chan->scan_type.shift);
	if (ret)
		return ret;

	ret = regmap_write(data->regmap, ADS1015_HI_THRESH_REG,
			high_thresh << chan->scan_type.shift);
	if (ret)
		return ret;

	ret = ads1015_set_power_state(data, true);
	if (ret < 0)
		return ret;

	ads1015_event_channel_enable(data, chan->address, comp_mode);

	ret = ads1015_get_adc_result(data, chan->address, &val);
	if (ret) {
		ads1015_event_channel_disable(data, chan->address);
		ads1015_set_power_state(data, false);
	}

	return ret;
}

static int ads1015_disable_event_config(struct ads1015_data *data,
	const struct iio_chan_spec *chan, int comp_mode)
{
	int ret;

	if (!ads1015_event_channel_enabled(data))
		return 0;

	if (data->event_channel != chan->address)
		return 0;

	if (data->comp_mode == ADS1015_CFG_COMP_MODE_TRAD &&
			comp_mode == ADS1015_CFG_COMP_MODE_WINDOW)
		return 0;

	ret = regmap_update_bits(data->regmap, ADS1015_CFG_REG,
				ADS1015_CFG_COMP_QUE_MASK,
				ADS1015_CFG_COMP_DISABLE <<
					ADS1015_CFG_COMP_QUE_SHIFT);
	if (ret)
		return ret;

	ads1015_event_channel_disable(data, chan->address);

	return ads1015_set_power_state(data, false);
}

static int ads1015_write_event_config(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, enum iio_event_type type,
	enum iio_event_direction dir, int state)
{
	struct ads1015_data *data = iio_priv(indio_dev);
	int ret;
	int comp_mode = (dir == IIO_EV_DIR_EITHER) ?
		ADS1015_CFG_COMP_MODE_WINDOW : ADS1015_CFG_COMP_MODE_TRAD;

	mutex_lock(&data->lock);

	/* Prevent from enabling both buffer and event at a time */
	ret = iio_device_claim_direct_mode(indio_dev);
	if (ret) {
		mutex_unlock(&data->lock);
		return ret;
	}

	if (state)
		ret = ads1015_enable_event_config(data, chan, comp_mode);
	else
		ret = ads1015_disable_event_config(data, chan, comp_mode);

	iio_device_release_direct_mode(indio_dev);
	mutex_unlock(&data->lock);

	return ret;
}

static irqreturn_t ads1015_event_handler(int irq, void *priv)
{
	struct iio_dev *indio_dev = priv;
	struct ads1015_data *data = iio_priv(indio_dev);
	int val;
	int ret;

	/* Clear the latched ALERT/RDY pin */
	ret = regmap_read(data->regmap, ADS1015_CONV_REG, &val);
	if (ret)
		return IRQ_HANDLED;

	if (ads1015_event_channel_enabled(data)) {
		enum iio_event_direction dir;
		u64 code;

		dir = data->comp_mode == ADS1015_CFG_COMP_MODE_TRAD ?
					IIO_EV_DIR_RISING : IIO_EV_DIR_EITHER;
		code = IIO_UNMOD_EVENT_CODE(IIO_VOLTAGE, data->event_channel,
					IIO_EV_TYPE_THRESH, dir);
		iio_push_event(indio_dev, code, iio_get_time_ns(indio_dev));
	}

	return IRQ_HANDLED;
}

static int ads1015_buffer_preenable(struct iio_dev *indio_dev)
{
	struct ads1015_data *data = iio_priv(indio_dev);

	/* Prevent from enabling both buffer and event at a time */
	if (ads1015_event_channel_enabled(data))
		return -EBUSY;

	return ads1015_set_power_state(iio_priv(indio_dev), true);
}

static int ads1015_buffer_postdisable(struct iio_dev *indio_dev)
{
	return ads1015_set_power_state(iio_priv(indio_dev), false);
}

static const struct iio_buffer_setup_ops ads1015_buffer_setup_ops = {
	.preenable	= ads1015_buffer_preenable,
	.postdisable	= ads1015_buffer_postdisable,
	.validate_scan_mask = &iio_validate_scan_mask_onehot,
};

static const struct iio_info ads1015_info = {
	.read_avail	= ads1015_read_avail,
	.read_raw	= ads1015_read_raw,
	.write_raw	= ads1015_write_raw,
	.read_event_value = ads1015_read_event,
	.write_event_value = ads1015_write_event,
	.read_event_config = ads1015_read_event_config,
	.write_event_config = ads1015_write_event_config,
};

static const struct iio_info tla2024_info = {
	.read_avail	= ads1015_read_avail,
	.read_raw	= ads1015_read_raw,
	.write_raw	= ads1015_write_raw,
};

static int ads1015_client_get_channels_config(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct ads1015_data *data = iio_priv(indio_dev);
	struct device *dev = &client->dev;
	int i = -1;

	device_for_each_child_node_scoped(dev, node) {
		u32 pval;
		unsigned int channel;
		unsigned int pga = ADS1015_DEFAULT_PGA;
		unsigned int data_rate = ADS1015_DEFAULT_DATA_RATE;

		if (fwnode_property_read_u32(node, "reg", &pval)) {
			dev_err(dev, "invalid reg on %pfw\n", node);
			continue;
		}

		channel = pval;
		if (channel >= ADS1015_CHANNELS) {
			dev_err(dev, "invalid channel index %d on %pfw\n",
				channel, node);
			continue;
		}

		if (!fwnode_property_read_u32(node, "ti,gain", &pval)) {
			pga = pval;
			if (pga > 5) {
				dev_err(dev, "invalid gain on %pfw\n", node);
				return -EINVAL;
			}
		}

		if (!fwnode_property_read_u32(node, "ti,datarate", &pval)) {
			data_rate = pval;
			if (data_rate > 7) {
				dev_err(dev, "invalid data_rate on %pfw\n", node);
				return -EINVAL;
			}
		}

		data->channel_data[channel].pga = pga;
		data->channel_data[channel].data_rate = data_rate;

		i++;
	}

	return i < 0 ? -EINVAL : 0;
}

static void ads1015_get_channels_config(struct i2c_client *client)
{
	unsigned int k;

	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct ads1015_data *data = iio_priv(indio_dev);

	if (!ads1015_client_get_channels_config(client))
		return;

	/* fallback on default configuration */
	for (k = 0; k < ADS1015_CHANNELS; ++k) {
		data->channel_data[k].pga = ADS1015_DEFAULT_PGA;
		data->channel_data[k].data_rate = ADS1015_DEFAULT_DATA_RATE;
	}
}

static int ads1015_set_conv_mode(struct ads1015_data *data, int mode)
{
	return regmap_update_bits(data->regmap, ADS1015_CFG_REG,
				  ADS1015_CFG_MOD_MASK,
				  mode << ADS1015_CFG_MOD_SHIFT);
}

static int ads1015_probe(struct i2c_client *client)
{
	const struct ads1015_chip_data *chip;
	struct iio_dev *indio_dev;
	struct ads1015_data *data;
	int ret;
	int i;

	chip = i2c_get_match_data(client);
	if (!chip)
		return dev_err_probe(&client->dev, -EINVAL, "Unknown chip\n");

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);

	mutex_init(&data->lock);

	indio_dev->name = ADS1015_DRV_NAME;
	indio_dev->modes = INDIO_DIRECT_MODE;

	indio_dev->channels = chip->channels;
	indio_dev->num_channels = chip->num_channels;
	indio_dev->info = chip->info;
	data->chip = chip;
	data->event_channel = ADS1015_CHANNELS;

	/*
	 * Set default lower and upper threshold to min and max value
	 * respectively.
	 */
	for (i = 0; i < ADS1015_CHANNELS; i++) {
		int realbits = indio_dev->channels[i].scan_type.realbits;

		data->thresh_data[i].low_thresh = -1 << (realbits - 1);
		data->thresh_data[i].high_thresh = (1 << (realbits - 1)) - 1;
	}

	/* we need to keep this ABI the same as used by hwmon ADS1015 driver */
	ads1015_get_channels_config(client);

	data->regmap = devm_regmap_init_i2c(client, chip->has_comparator ?
					    &ads1015_regmap_config :
					    &tla2024_regmap_config);
	if (IS_ERR(data->regmap)) {
		dev_err(&client->dev, "Failed to allocate register map\n");
		return PTR_ERR(data->regmap);
	}

	ret = devm_iio_triggered_buffer_setup(&client->dev, indio_dev, NULL,
					      ads1015_trigger_handler,
					      &ads1015_buffer_setup_ops);
	if (ret < 0) {
		dev_err(&client->dev, "iio triggered buffer setup failed\n");
		return ret;
	}

	if (client->irq && chip->has_comparator) {
		unsigned long irq_trig =
			irqd_get_trigger_type(irq_get_irq_data(client->irq));
		unsigned int cfg_comp_mask = ADS1015_CFG_COMP_QUE_MASK |
			ADS1015_CFG_COMP_LAT_MASK | ADS1015_CFG_COMP_POL_MASK;
		unsigned int cfg_comp =
			ADS1015_CFG_COMP_DISABLE << ADS1015_CFG_COMP_QUE_SHIFT |
			1 << ADS1015_CFG_COMP_LAT_SHIFT;

		switch (irq_trig) {
		case IRQF_TRIGGER_FALLING:
		case IRQF_TRIGGER_LOW:
			cfg_comp |= ADS1015_CFG_COMP_POL_LOW <<
					ADS1015_CFG_COMP_POL_SHIFT;
			break;
		case IRQF_TRIGGER_HIGH:
		case IRQF_TRIGGER_RISING:
			cfg_comp |= ADS1015_CFG_COMP_POL_HIGH <<
					ADS1015_CFG_COMP_POL_SHIFT;
			break;
		default:
			return -EINVAL;
		}

		ret = regmap_update_bits(data->regmap, ADS1015_CFG_REG,
					cfg_comp_mask, cfg_comp);
		if (ret)
			return ret;

		ret = devm_request_threaded_irq(&client->dev, client->irq,
						NULL, ads1015_event_handler,
						irq_trig | IRQF_ONESHOT,
						client->name, indio_dev);
		if (ret)
			return ret;
	}

	ret = ads1015_set_conv_mode(data, ADS1015_CONTINUOUS);
	if (ret)
		return ret;

	data->conv_invalid = true;

	ret = pm_runtime_set_active(&client->dev);
	if (ret)
		return ret;
	pm_runtime_set_autosuspend_delay(&client->dev, ADS1015_SLEEP_DELAY_MS);
	pm_runtime_use_autosuspend(&client->dev);
	pm_runtime_enable(&client->dev);

	ret = iio_device_register(indio_dev);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to register IIO device\n");
		return ret;
	}

	return 0;
}

static void ads1015_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct ads1015_data *data = iio_priv(indio_dev);
	int ret;

	iio_device_unregister(indio_dev);

	pm_runtime_disable(&client->dev);
	pm_runtime_set_suspended(&client->dev);

	/* power down single shot mode */
	ret = ads1015_set_conv_mode(data, ADS1015_SINGLESHOT);
	if (ret)
		dev_warn(&client->dev, "Failed to power down (%pe)\n",
			 ERR_PTR(ret));
}

#ifdef CONFIG_PM
static int ads1015_runtime_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct ads1015_data *data = iio_priv(indio_dev);

	return ads1015_set_conv_mode(data, ADS1015_SINGLESHOT);
}

static int ads1015_runtime_resume(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct ads1015_data *data = iio_priv(indio_dev);
	int ret;

	ret = ads1015_set_conv_mode(data, ADS1015_CONTINUOUS);
	if (!ret)
		data->conv_invalid = true;

	return ret;
}
#endif

static const struct dev_pm_ops ads1015_pm_ops = {
	SET_RUNTIME_PM_OPS(ads1015_runtime_suspend,
			   ads1015_runtime_resume, NULL)
};

static const struct ads1015_chip_data ads1015_data = {
	.channels	= ads1015_channels,
	.num_channels	= ARRAY_SIZE(ads1015_channels),
	.info		= &ads1015_info,
	.data_rate	= ads1015_data_rate,
	.data_rate_len	= ARRAY_SIZE(ads1015_data_rate),
	.scale		= ads1015_scale,
	.scale_len	= ARRAY_SIZE(ads1015_scale),
	.has_comparator	= true,
};

static const struct ads1015_chip_data ads1115_data = {
	.channels	= ads1115_channels,
	.num_channels	= ARRAY_SIZE(ads1115_channels),
	.info		= &ads1015_info,
	.data_rate	= ads1115_data_rate,
	.data_rate_len	= ARRAY_SIZE(ads1115_data_rate),
	.scale		= ads1115_scale,
	.scale_len	= ARRAY_SIZE(ads1115_scale),
	.has_comparator	= true,
};

static const struct ads1015_chip_data tla2024_data = {
	.channels	= tla2024_channels,
	.num_channels	= ARRAY_SIZE(tla2024_channels),
	.info		= &tla2024_info,
	.data_rate	= ads1015_data_rate,
	.data_rate_len	= ARRAY_SIZE(ads1015_data_rate),
	.scale		= ads1015_scale,
	.scale_len	= ARRAY_SIZE(ads1015_scale),
	.has_comparator	= false,
};

static const struct i2c_device_id ads1015_id[] = {
	{ "ads1015", (kernel_ulong_t)&ads1015_data },
	{ "ads1115", (kernel_ulong_t)&ads1115_data },
	{ "tla2024", (kernel_ulong_t)&tla2024_data },
	{}
};
MODULE_DEVICE_TABLE(i2c, ads1015_id);

static const struct of_device_id ads1015_of_match[] = {
	{ .compatible = "ti,ads1015", .data = &ads1015_data },
	{ .compatible = "ti,ads1115", .data = &ads1115_data },
	{ .compatible = "ti,tla2024", .data = &tla2024_data },
	{}
};
MODULE_DEVICE_TABLE(of, ads1015_of_match);

static struct i2c_driver ads1015_driver = {
	.driver = {
		.name = ADS1015_DRV_NAME,
		.of_match_table = ads1015_of_match,
		.pm = &ads1015_pm_ops,
	},
	.probe		= ads1015_probe,
	.remove		= ads1015_remove,
	.id_table	= ads1015_id,
};

module_i2c_driver(ads1015_driver);

MODULE_AUTHOR("Daniel Baluta <daniel.baluta@intel.com>");
MODULE_DESCRIPTION("Texas Instruments ADS1015 ADC driver");
MODULE_LICENSE("GPL v2");
