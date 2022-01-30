// SPDX-License-Identifier: GPL-2.0-only
/*
 * AD7785/AD7792/AD7793/AD7794/AD7795 SPI ADC driver
 *
 * Copyright 2011-2012 Analog Devices Inc.
 */

#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/spi/spi.h>
#include <linux/regulator/consumer.h>
#include <linux/err.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/module.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/adc/ad_sigma_delta.h>
#include <linux/platform_data/ad7793.h>

/* Registers */
#define AD7793_REG_COMM		0 /* Communications Register (WO, 8-bit) */
#define AD7793_REG_STAT		0 /* Status Register	     (RO, 8-bit) */
#define AD7793_REG_MODE		1 /* Mode Register	     (RW, 16-bit */
#define AD7793_REG_CONF		2 /* Configuration Register  (RW, 16-bit) */
#define AD7793_REG_DATA		3 /* Data Register	     (RO, 16-/24-bit) */
#define AD7793_REG_ID		4 /* ID Register	     (RO, 8-bit) */
#define AD7793_REG_IO		5 /* IO Register	     (RO, 8-bit) */
#define AD7793_REG_OFFSET	6 /* Offset Register	     (RW, 16-bit
				   * (AD7792)/24-bit (AD7793)) */
#define AD7793_REG_FULLSALE	7 /* Full-Scale Register
				   * (RW, 16-bit (AD7792)/24-bit (AD7793)) */

/* Communications Register Bit Designations (AD7793_REG_COMM) */
#define AD7793_COMM_WEN		(1 << 7) /* Write Enable */
#define AD7793_COMM_WRITE	(0 << 6) /* Write Operation */
#define AD7793_COMM_READ	(1 << 6) /* Read Operation */
#define AD7793_COMM_ADDR(x)	(((x) & 0x7) << 3) /* Register Address */
#define AD7793_COMM_CREAD	(1 << 2) /* Continuous Read of Data Register */

/* Status Register Bit Designations (AD7793_REG_STAT) */
#define AD7793_STAT_RDY		(1 << 7) /* Ready */
#define AD7793_STAT_ERR		(1 << 6) /* Error (Overrange, Underrange) */
#define AD7793_STAT_CH3		(1 << 2) /* Channel 3 */
#define AD7793_STAT_CH2		(1 << 1) /* Channel 2 */
#define AD7793_STAT_CH1		(1 << 0) /* Channel 1 */

/* Mode Register Bit Designations (AD7793_REG_MODE) */
#define AD7793_MODE_SEL(x)	(((x) & 0x7) << 13) /* Operation Mode Select */
#define AD7793_MODE_SEL_MASK	(0x7 << 13) /* Operation Mode Select mask */
#define AD7793_MODE_CLKSRC(x)	(((x) & 0x3) << 6) /* ADC Clock Source Select */
#define AD7793_MODE_RATE(x)	((x) & 0xF) /* Filter Update Rate Select */

#define AD7793_MODE_CONT		0 /* Continuous Conversion Mode */
#define AD7793_MODE_SINGLE		1 /* Single Conversion Mode */
#define AD7793_MODE_IDLE		2 /* Idle Mode */
#define AD7793_MODE_PWRDN		3 /* Power-Down Mode */
#define AD7793_MODE_CAL_INT_ZERO	4 /* Internal Zero-Scale Calibration */
#define AD7793_MODE_CAL_INT_FULL	5 /* Internal Full-Scale Calibration */
#define AD7793_MODE_CAL_SYS_ZERO	6 /* System Zero-Scale Calibration */
#define AD7793_MODE_CAL_SYS_FULL	7 /* System Full-Scale Calibration */

#define AD7793_CLK_INT		0 /* Internal 64 kHz Clock not
				   * available at the CLK pin */
#define AD7793_CLK_INT_CO	1 /* Internal 64 kHz Clock available
				   * at the CLK pin */
#define AD7793_CLK_EXT		2 /* External 64 kHz Clock */
#define AD7793_CLK_EXT_DIV2	3 /* External Clock divided by 2 */

/* Configuration Register Bit Designations (AD7793_REG_CONF) */
#define AD7793_CONF_VBIAS(x)	(((x) & 0x3) << 14) /* Bias Voltage
						     * Generator Enable */
#define AD7793_CONF_BO_EN	(1 << 13) /* Burnout Current Enable */
#define AD7793_CONF_UNIPOLAR	(1 << 12) /* Unipolar/Bipolar Enable */
#define AD7793_CONF_BOOST	(1 << 11) /* Boost Enable */
#define AD7793_CONF_GAIN(x)	(((x) & 0x7) << 8) /* Gain Select */
#define AD7793_CONF_REFSEL(x)	((x) << 6) /* INT/EXT Reference Select */
#define AD7793_CONF_BUF		(1 << 4) /* Buffered Mode Enable */
#define AD7793_CONF_CHAN(x)	((x) & 0xf) /* Channel select */
#define AD7793_CONF_CHAN_MASK	0xf /* Channel select mask */

#define AD7793_CH_AIN1P_AIN1M	0 /* AIN1(+) - AIN1(-) */
#define AD7793_CH_AIN2P_AIN2M	1 /* AIN2(+) - AIN2(-) */
#define AD7793_CH_AIN3P_AIN3M	2 /* AIN3(+) - AIN3(-) */
#define AD7793_CH_AIN1M_AIN1M	3 /* AIN1(-) - AIN1(-) */
#define AD7793_CH_TEMP		6 /* Temp Sensor */
#define AD7793_CH_AVDD_MONITOR	7 /* AVDD Monitor */

#define AD7795_CH_AIN4P_AIN4M	4 /* AIN4(+) - AIN4(-) */
#define AD7795_CH_AIN5P_AIN5M	5 /* AIN5(+) - AIN5(-) */
#define AD7795_CH_AIN6P_AIN6M	6 /* AIN6(+) - AIN6(-) */
#define AD7795_CH_AIN1M_AIN1M	8 /* AIN1(-) - AIN1(-) */

/* ID Register Bit Designations (AD7793_REG_ID) */
#define AD7785_ID		0x3
#define AD7792_ID		0xA
#define AD7793_ID		0xB
#define AD7794_ID		0xF
#define AD7795_ID		0xF
#define AD7796_ID		0xA
#define AD7797_ID		0xB
#define AD7798_ID		0x8
#define AD7799_ID		0x9
#define AD7793_ID_MASK		0xF

/* IO (Excitation Current Sources) Register Bit Designations (AD7793_REG_IO) */
#define AD7793_IO_IEXC1_IOUT1_IEXC2_IOUT2	0 /* IEXC1 connect to IOUT1,
						   * IEXC2 connect to IOUT2 */
#define AD7793_IO_IEXC1_IOUT2_IEXC2_IOUT1	1 /* IEXC1 connect to IOUT2,
						   * IEXC2 connect to IOUT1 */
#define AD7793_IO_IEXC1_IEXC2_IOUT1		2 /* Both current sources
						   * IEXC1,2 connect to IOUT1 */
#define AD7793_IO_IEXC1_IEXC2_IOUT2		3 /* Both current sources
						   * IEXC1,2 connect to IOUT2 */

#define AD7793_IO_IXCEN_10uA	(1 << 0) /* Excitation Current 10uA */
#define AD7793_IO_IXCEN_210uA	(2 << 0) /* Excitation Current 210uA */
#define AD7793_IO_IXCEN_1mA	(3 << 0) /* Excitation Current 1mA */

/* NOTE:
 * The AD7792/AD7793 features a dual use data out ready DOUT/RDY output.
 * In order to avoid contentions on the SPI bus, it's therefore necessary
 * to use spi bus locking.
 *
 * The DOUT/RDY output must also be wired to an interrupt capable GPIO.
 */

#define AD7793_FLAG_HAS_CLKSEL		BIT(0)
#define AD7793_FLAG_HAS_REFSEL		BIT(1)
#define AD7793_FLAG_HAS_VBIAS		BIT(2)
#define AD7793_HAS_EXITATION_CURRENT	BIT(3)
#define AD7793_FLAG_HAS_GAIN		BIT(4)
#define AD7793_FLAG_HAS_BUFFER		BIT(5)

struct ad7793_chip_info {
	unsigned int id;
	const struct iio_chan_spec *channels;
	unsigned int num_channels;
	unsigned int flags;

	const struct iio_info *iio_info;
	const u16 *sample_freq_avail;
};

struct ad7793_state {
	const struct ad7793_chip_info	*chip_info;
	struct regulator		*reg;
	u16				int_vref_mv;
	u16				mode;
	u16				conf;
	u32				scale_avail[8][2];

	struct ad_sigma_delta		sd;

};

enum ad7793_supported_device_ids {
	ID_AD7785,
	ID_AD7792,
	ID_AD7793,
	ID_AD7794,
	ID_AD7795,
	ID_AD7796,
	ID_AD7797,
	ID_AD7798,
	ID_AD7799,
};

static struct ad7793_state *ad_sigma_delta_to_ad7793(struct ad_sigma_delta *sd)
{
	return container_of(sd, struct ad7793_state, sd);
}

static int ad7793_set_channel(struct ad_sigma_delta *sd, unsigned int channel)
{
	struct ad7793_state *st = ad_sigma_delta_to_ad7793(sd);

	st->conf &= ~AD7793_CONF_CHAN_MASK;
	st->conf |= AD7793_CONF_CHAN(channel);

	return ad_sd_write_reg(&st->sd, AD7793_REG_CONF, 2, st->conf);
}

static int ad7793_set_mode(struct ad_sigma_delta *sd,
			   enum ad_sigma_delta_mode mode)
{
	struct ad7793_state *st = ad_sigma_delta_to_ad7793(sd);

	st->mode &= ~AD7793_MODE_SEL_MASK;
	st->mode |= AD7793_MODE_SEL(mode);

	return ad_sd_write_reg(&st->sd, AD7793_REG_MODE, 2, st->mode);
}

static const struct ad_sigma_delta_info ad7793_sigma_delta_info = {
	.set_channel = ad7793_set_channel,
	.set_mode = ad7793_set_mode,
	.has_registers = true,
	.addr_shift = 3,
	.read_mask = BIT(6),
	.irq_flags = IRQF_TRIGGER_FALLING,
};

static const struct ad_sd_calib_data ad7793_calib_arr[6] = {
	{AD7793_MODE_CAL_INT_ZERO, AD7793_CH_AIN1P_AIN1M},
	{AD7793_MODE_CAL_INT_FULL, AD7793_CH_AIN1P_AIN1M},
	{AD7793_MODE_CAL_INT_ZERO, AD7793_CH_AIN2P_AIN2M},
	{AD7793_MODE_CAL_INT_FULL, AD7793_CH_AIN2P_AIN2M},
	{AD7793_MODE_CAL_INT_ZERO, AD7793_CH_AIN3P_AIN3M},
	{AD7793_MODE_CAL_INT_FULL, AD7793_CH_AIN3P_AIN3M}
};

static int ad7793_calibrate_all(struct ad7793_state *st)
{
	return ad_sd_calibrate_all(&st->sd, ad7793_calib_arr,
				   ARRAY_SIZE(ad7793_calib_arr));
}

static int ad7793_check_platform_data(struct ad7793_state *st,
	const struct ad7793_platform_data *pdata)
{
	if ((pdata->current_source_direction == AD7793_IEXEC1_IEXEC2_IOUT1 ||
		pdata->current_source_direction == AD7793_IEXEC1_IEXEC2_IOUT2) &&
		((pdata->exitation_current != AD7793_IX_10uA) &&
		(pdata->exitation_current != AD7793_IX_210uA)))
		return -EINVAL;

	if (!(st->chip_info->flags & AD7793_FLAG_HAS_CLKSEL) &&
		pdata->clock_src != AD7793_CLK_SRC_INT)
		return -EINVAL;

	if (!(st->chip_info->flags & AD7793_FLAG_HAS_REFSEL) &&
		pdata->refsel != AD7793_REFSEL_REFIN1)
		return -EINVAL;

	if (!(st->chip_info->flags & AD7793_FLAG_HAS_VBIAS) &&
		pdata->bias_voltage != AD7793_BIAS_VOLTAGE_DISABLED)
		return -EINVAL;

	if (!(st->chip_info->flags & AD7793_HAS_EXITATION_CURRENT) &&
		pdata->exitation_current != AD7793_IX_DISABLED)
		return -EINVAL;

	return 0;
}

static int ad7793_setup(struct iio_dev *indio_dev,
	const struct ad7793_platform_data *pdata,
	unsigned int vref_mv)
{
	struct ad7793_state *st = iio_priv(indio_dev);
	int i, ret;
	unsigned long long scale_uv;
	u32 id;

	ret = ad7793_check_platform_data(st, pdata);
	if (ret)
		return ret;

	/* reset the serial interface */
	ret = ad_sd_reset(&st->sd, 32);
	if (ret < 0)
		goto out;
	usleep_range(500, 2000); /* Wait for at least 500us */

	/* write/read test for device presence */
	ret = ad_sd_read_reg(&st->sd, AD7793_REG_ID, 1, &id);
	if (ret)
		goto out;

	id &= AD7793_ID_MASK;

	if (id != st->chip_info->id) {
		ret = -ENODEV;
		dev_err(&st->sd.spi->dev, "device ID query failed\n");
		goto out;
	}

	st->mode = AD7793_MODE_RATE(1);
	st->conf = 0;

	if (st->chip_info->flags & AD7793_FLAG_HAS_CLKSEL)
		st->mode |= AD7793_MODE_CLKSRC(pdata->clock_src);
	if (st->chip_info->flags & AD7793_FLAG_HAS_REFSEL)
		st->conf |= AD7793_CONF_REFSEL(pdata->refsel);
	if (st->chip_info->flags & AD7793_FLAG_HAS_VBIAS)
		st->conf |= AD7793_CONF_VBIAS(pdata->bias_voltage);
	if (pdata->buffered || !(st->chip_info->flags & AD7793_FLAG_HAS_BUFFER))
		st->conf |= AD7793_CONF_BUF;
	if (pdata->boost_enable &&
		(st->chip_info->flags & AD7793_FLAG_HAS_VBIAS))
		st->conf |= AD7793_CONF_BOOST;
	if (pdata->burnout_current)
		st->conf |= AD7793_CONF_BO_EN;
	if (pdata->unipolar)
		st->conf |= AD7793_CONF_UNIPOLAR;

	if (!(st->chip_info->flags & AD7793_FLAG_HAS_GAIN))
		st->conf |= AD7793_CONF_GAIN(7);

	ret = ad7793_set_mode(&st->sd, AD_SD_MODE_IDLE);
	if (ret)
		goto out;

	ret = ad7793_set_channel(&st->sd, 0);
	if (ret)
		goto out;

	if (st->chip_info->flags & AD7793_HAS_EXITATION_CURRENT) {
		ret = ad_sd_write_reg(&st->sd, AD7793_REG_IO, 1,
				pdata->exitation_current |
				(pdata->current_source_direction << 2));
		if (ret)
			goto out;
	}

	ret = ad7793_calibrate_all(st);
	if (ret)
		goto out;

	/* Populate available ADC input ranges */
	for (i = 0; i < ARRAY_SIZE(st->scale_avail); i++) {
		scale_uv = ((u64)vref_mv * 100000000)
			>> (st->chip_info->channels[0].scan_type.realbits -
			(!!(st->conf & AD7793_CONF_UNIPOLAR) ? 0 : 1));
		scale_uv >>= i;

		st->scale_avail[i][1] = do_div(scale_uv, 100000000) * 10;
		st->scale_avail[i][0] = scale_uv;
	}

	return 0;
out:
	dev_err(&st->sd.spi->dev, "setup failed\n");
	return ret;
}

static const u16 ad7793_sample_freq_avail[16] = {0, 470, 242, 123, 62, 50, 39,
					33, 19, 17, 16, 12, 10, 8, 6, 4};

static const u16 ad7797_sample_freq_avail[16] = {0, 0, 0, 123, 62, 50, 0,
					33, 0, 17, 16, 12, 10, 8, 6, 4};

static IIO_CONST_ATTR_SAMP_FREQ_AVAIL(
	"470 242 123 62 50 39 33 19 17 16 12 10 8 6 4");

static IIO_CONST_ATTR_NAMED(sampling_frequency_available_ad7797,
	sampling_frequency_available, "123 62 50 33 17 16 12 10 8 6 4");

static int ad7793_read_avail(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     const int **vals, int *type, int *length,
			     long mask)
{
	struct ad7793_state *st = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		*vals = (int *)st->scale_avail;
		*type = IIO_VAL_INT_PLUS_NANO;
		/* Values are stored in a 2D matrix  */
		*length = ARRAY_SIZE(st->scale_avail) * 2;

		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
}

static struct attribute *ad7793_attributes[] = {
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	NULL
};

static const struct attribute_group ad7793_attribute_group = {
	.attrs = ad7793_attributes,
};

static struct attribute *ad7797_attributes[] = {
	&iio_const_attr_sampling_frequency_available_ad7797.dev_attr.attr,
	NULL
};

static const struct attribute_group ad7797_attribute_group = {
	.attrs = ad7797_attributes,
};

static int ad7793_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val,
			   int *val2,
			   long m)
{
	struct ad7793_state *st = iio_priv(indio_dev);
	int ret;
	unsigned long long scale_uv;
	bool unipolar = !!(st->conf & AD7793_CONF_UNIPOLAR);

	switch (m) {
	case IIO_CHAN_INFO_RAW:
		ret = ad_sigma_delta_single_conversion(indio_dev, chan, val);
		if (ret < 0)
			return ret;

		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_VOLTAGE:
			if (chan->differential) {
				*val = st->
					scale_avail[(st->conf >> 8) & 0x7][0];
				*val2 = st->
					scale_avail[(st->conf >> 8) & 0x7][1];
				return IIO_VAL_INT_PLUS_NANO;
			}
			/* 1170mV / 2^23 * 6 */
			scale_uv = (1170ULL * 1000000000ULL * 6ULL);
			break;
		case IIO_TEMP:
				/* 1170mV / 0.81 mV/C / 2^23 */
				scale_uv = 1444444444444444ULL;
			break;
		default:
			return -EINVAL;
		}

		scale_uv >>= (chan->scan_type.realbits - (unipolar ? 0 : 1));
		*val = 0;
		*val2 = scale_uv;
		return IIO_VAL_INT_PLUS_NANO;
	case IIO_CHAN_INFO_OFFSET:
		if (!unipolar)
			*val = -(1 << (chan->scan_type.realbits - 1));
		else
			*val = 0;

		/* Kelvin to Celsius */
		if (chan->type == IIO_TEMP) {
			unsigned long long offset;
			unsigned int shift;

			shift = chan->scan_type.realbits - (unipolar ? 0 : 1);
			offset = 273ULL << shift;
			do_div(offset, 1444);
			*val -= offset;
		}
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = st->chip_info
			       ->sample_freq_avail[AD7793_MODE_RATE(st->mode)];
		return IIO_VAL_INT;
	}
	return -EINVAL;
}

static int ad7793_write_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int val,
			       int val2,
			       long mask)
{
	struct ad7793_state *st = iio_priv(indio_dev);
	int ret, i;
	unsigned int tmp;

	ret = iio_device_claim_direct_mode(indio_dev);
	if (ret)
		return ret;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		ret = -EINVAL;
		for (i = 0; i < ARRAY_SIZE(st->scale_avail); i++)
			if (val2 == st->scale_avail[i][1]) {
				ret = 0;
				tmp = st->conf;
				st->conf &= ~AD7793_CONF_GAIN(-1);
				st->conf |= AD7793_CONF_GAIN(i);

				if (tmp == st->conf)
					break;

				ad_sd_write_reg(&st->sd, AD7793_REG_CONF,
						sizeof(st->conf), st->conf);
				ad7793_calibrate_all(st);
				break;
			}
		break;
	case IIO_CHAN_INFO_SAMP_FREQ:
		if (!val) {
			ret = -EINVAL;
			break;
		}

		for (i = 0; i < 16; i++)
			if (val == st->chip_info->sample_freq_avail[i])
				break;

		if (i == 16) {
			ret = -EINVAL;
			break;
		}

		st->mode &= ~AD7793_MODE_RATE(-1);
		st->mode |= AD7793_MODE_RATE(i);
		ad_sd_write_reg(&st->sd, AD7793_REG_MODE, sizeof(st->mode),
				st->mode);
		break;
	default:
		ret = -EINVAL;
	}

	iio_device_release_direct_mode(indio_dev);
	return ret;
}

static int ad7793_write_raw_get_fmt(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       long mask)
{
	return IIO_VAL_INT_PLUS_NANO;
}

static const struct iio_info ad7793_info = {
	.read_raw = &ad7793_read_raw,
	.write_raw = &ad7793_write_raw,
	.write_raw_get_fmt = &ad7793_write_raw_get_fmt,
	.read_avail = ad7793_read_avail,
	.attrs = &ad7793_attribute_group,
	.validate_trigger = ad_sd_validate_trigger,
};

static const struct iio_info ad7797_info = {
	.read_raw = &ad7793_read_raw,
	.write_raw = &ad7793_write_raw,
	.write_raw_get_fmt = &ad7793_write_raw_get_fmt,
	.attrs = &ad7797_attribute_group,
	.validate_trigger = ad_sd_validate_trigger,
};

#define __AD7793_CHANNEL(_si, _channel1, _channel2, _address, _bits, \
	_storagebits, _shift, _extend_name, _type, _mask_type_av, _mask_all) \
	{ \
		.type = (_type), \
		.differential = (_channel2 == -1 ? 0 : 1), \
		.indexed = 1, \
		.channel = (_channel1), \
		.channel2 = (_channel2), \
		.address = (_address), \
		.extend_name = (_extend_name), \
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
			BIT(IIO_CHAN_INFO_OFFSET), \
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE), \
		.info_mask_shared_by_type_available = (_mask_type_av), \
		.info_mask_shared_by_all = _mask_all, \
		.scan_index = (_si), \
		.scan_type = { \
			.sign = 'u', \
			.realbits = (_bits), \
			.storagebits = (_storagebits), \
			.shift = (_shift), \
			.endianness = IIO_BE, \
		}, \
	}

#define AD7793_DIFF_CHANNEL(_si, _channel1, _channel2, _address, _bits, \
	_storagebits, _shift) \
	__AD7793_CHANNEL(_si, _channel1, _channel2, _address, _bits, \
		_storagebits, _shift, NULL, IIO_VOLTAGE, \
		BIT(IIO_CHAN_INFO_SCALE), \
		BIT(IIO_CHAN_INFO_SAMP_FREQ))

#define AD7793_SHORTED_CHANNEL(_si, _channel, _address, _bits, \
	_storagebits, _shift) \
	__AD7793_CHANNEL(_si, _channel, _channel, _address, _bits, \
		_storagebits, _shift, "shorted", IIO_VOLTAGE, \
		BIT(IIO_CHAN_INFO_SCALE), \
		BIT(IIO_CHAN_INFO_SAMP_FREQ))

#define AD7793_TEMP_CHANNEL(_si, _address, _bits, _storagebits, _shift) \
	__AD7793_CHANNEL(_si, 0, -1, _address, _bits, \
		_storagebits, _shift, NULL, IIO_TEMP, \
		0, \
		BIT(IIO_CHAN_INFO_SAMP_FREQ))

#define AD7793_SUPPLY_CHANNEL(_si, _channel, _address, _bits, _storagebits, \
	_shift) \
	__AD7793_CHANNEL(_si, _channel, -1, _address, _bits, \
		_storagebits, _shift, "supply", IIO_VOLTAGE, \
		0, \
		BIT(IIO_CHAN_INFO_SAMP_FREQ))

#define AD7797_DIFF_CHANNEL(_si, _channel1, _channel2, _address, _bits, \
	_storagebits, _shift) \
	__AD7793_CHANNEL(_si, _channel1, _channel2, _address, _bits, \
		_storagebits, _shift, NULL, IIO_VOLTAGE, \
		0, \
		BIT(IIO_CHAN_INFO_SAMP_FREQ))

#define AD7797_SHORTED_CHANNEL(_si, _channel, _address, _bits, \
	_storagebits, _shift) \
	__AD7793_CHANNEL(_si, _channel, _channel, _address, _bits, \
		_storagebits, _shift, "shorted", IIO_VOLTAGE, \
		0, \
		BIT(IIO_CHAN_INFO_SAMP_FREQ))

#define DECLARE_AD7793_CHANNELS(_name, _b, _sb, _s) \
const struct iio_chan_spec _name##_channels[] = { \
	AD7793_DIFF_CHANNEL(0, 0, 0, AD7793_CH_AIN1P_AIN1M, (_b), (_sb), (_s)), \
	AD7793_DIFF_CHANNEL(1, 1, 1, AD7793_CH_AIN2P_AIN2M, (_b), (_sb), (_s)), \
	AD7793_DIFF_CHANNEL(2, 2, 2, AD7793_CH_AIN3P_AIN3M, (_b), (_sb), (_s)), \
	AD7793_SHORTED_CHANNEL(3, 0, AD7793_CH_AIN1M_AIN1M, (_b), (_sb), (_s)), \
	AD7793_TEMP_CHANNEL(4, AD7793_CH_TEMP, (_b), (_sb), (_s)), \
	AD7793_SUPPLY_CHANNEL(5, 3, AD7793_CH_AVDD_MONITOR, (_b), (_sb), (_s)), \
	IIO_CHAN_SOFT_TIMESTAMP(6), \
}

#define DECLARE_AD7795_CHANNELS(_name, _b, _sb) \
const struct iio_chan_spec _name##_channels[] = { \
	AD7793_DIFF_CHANNEL(0, 0, 0, AD7793_CH_AIN1P_AIN1M, (_b), (_sb), 0), \
	AD7793_DIFF_CHANNEL(1, 1, 1, AD7793_CH_AIN2P_AIN2M, (_b), (_sb), 0), \
	AD7793_DIFF_CHANNEL(2, 2, 2, AD7793_CH_AIN3P_AIN3M, (_b), (_sb), 0), \
	AD7793_DIFF_CHANNEL(3, 3, 3, AD7795_CH_AIN4P_AIN4M, (_b), (_sb), 0), \
	AD7793_DIFF_CHANNEL(4, 4, 4, AD7795_CH_AIN5P_AIN5M, (_b), (_sb), 0), \
	AD7793_DIFF_CHANNEL(5, 5, 5, AD7795_CH_AIN6P_AIN6M, (_b), (_sb), 0), \
	AD7793_SHORTED_CHANNEL(6, 0, AD7795_CH_AIN1M_AIN1M, (_b), (_sb), 0), \
	AD7793_TEMP_CHANNEL(7, AD7793_CH_TEMP, (_b), (_sb), 0), \
	AD7793_SUPPLY_CHANNEL(8, 3, AD7793_CH_AVDD_MONITOR, (_b), (_sb), 0), \
	IIO_CHAN_SOFT_TIMESTAMP(9), \
}

#define DECLARE_AD7797_CHANNELS(_name, _b, _sb) \
const struct iio_chan_spec _name##_channels[] = { \
	AD7797_DIFF_CHANNEL(0, 0, 0, AD7793_CH_AIN1P_AIN1M, (_b), (_sb), 0), \
	AD7797_SHORTED_CHANNEL(1, 0, AD7793_CH_AIN1M_AIN1M, (_b), (_sb), 0), \
	AD7793_TEMP_CHANNEL(2, AD7793_CH_TEMP, (_b), (_sb), 0), \
	AD7793_SUPPLY_CHANNEL(3, 3, AD7793_CH_AVDD_MONITOR, (_b), (_sb), 0), \
	IIO_CHAN_SOFT_TIMESTAMP(4), \
}

#define DECLARE_AD7799_CHANNELS(_name, _b, _sb) \
const struct iio_chan_spec _name##_channels[] = { \
	AD7793_DIFF_CHANNEL(0, 0, 0, AD7793_CH_AIN1P_AIN1M, (_b), (_sb), 0), \
	AD7793_DIFF_CHANNEL(1, 1, 1, AD7793_CH_AIN2P_AIN2M, (_b), (_sb), 0), \
	AD7793_DIFF_CHANNEL(2, 2, 2, AD7793_CH_AIN3P_AIN3M, (_b), (_sb), 0), \
	AD7793_SHORTED_CHANNEL(3, 0, AD7793_CH_AIN1M_AIN1M, (_b), (_sb), 0), \
	AD7793_SUPPLY_CHANNEL(4, 3, AD7793_CH_AVDD_MONITOR, (_b), (_sb), 0), \
	IIO_CHAN_SOFT_TIMESTAMP(5), \
}

static DECLARE_AD7793_CHANNELS(ad7785, 20, 32, 4);
static DECLARE_AD7793_CHANNELS(ad7792, 16, 32, 0);
static DECLARE_AD7793_CHANNELS(ad7793, 24, 32, 0);
static DECLARE_AD7795_CHANNELS(ad7794, 16, 32);
static DECLARE_AD7795_CHANNELS(ad7795, 24, 32);
static DECLARE_AD7797_CHANNELS(ad7796, 16, 16);
static DECLARE_AD7797_CHANNELS(ad7797, 24, 32);
static DECLARE_AD7799_CHANNELS(ad7798, 16, 16);
static DECLARE_AD7799_CHANNELS(ad7799, 24, 32);

static const struct ad7793_chip_info ad7793_chip_info_tbl[] = {
	[ID_AD7785] = {
		.id = AD7785_ID,
		.channels = ad7785_channels,
		.num_channels = ARRAY_SIZE(ad7785_channels),
		.iio_info = &ad7793_info,
		.sample_freq_avail = ad7793_sample_freq_avail,
		.flags = AD7793_FLAG_HAS_CLKSEL |
			AD7793_FLAG_HAS_REFSEL |
			AD7793_FLAG_HAS_VBIAS |
			AD7793_HAS_EXITATION_CURRENT |
			AD7793_FLAG_HAS_GAIN |
			AD7793_FLAG_HAS_BUFFER,
	},
	[ID_AD7792] = {
		.id = AD7792_ID,
		.channels = ad7792_channels,
		.num_channels = ARRAY_SIZE(ad7792_channels),
		.iio_info = &ad7793_info,
		.sample_freq_avail = ad7793_sample_freq_avail,
		.flags = AD7793_FLAG_HAS_CLKSEL |
			AD7793_FLAG_HAS_REFSEL |
			AD7793_FLAG_HAS_VBIAS |
			AD7793_HAS_EXITATION_CURRENT |
			AD7793_FLAG_HAS_GAIN |
			AD7793_FLAG_HAS_BUFFER,
	},
	[ID_AD7793] = {
		.id = AD7793_ID,
		.channels = ad7793_channels,
		.num_channels = ARRAY_SIZE(ad7793_channels),
		.iio_info = &ad7793_info,
		.sample_freq_avail = ad7793_sample_freq_avail,
		.flags = AD7793_FLAG_HAS_CLKSEL |
			AD7793_FLAG_HAS_REFSEL |
			AD7793_FLAG_HAS_VBIAS |
			AD7793_HAS_EXITATION_CURRENT |
			AD7793_FLAG_HAS_GAIN |
			AD7793_FLAG_HAS_BUFFER,
	},
	[ID_AD7794] = {
		.id = AD7794_ID,
		.channels = ad7794_channels,
		.num_channels = ARRAY_SIZE(ad7794_channels),
		.iio_info = &ad7793_info,
		.sample_freq_avail = ad7793_sample_freq_avail,
		.flags = AD7793_FLAG_HAS_CLKSEL |
			AD7793_FLAG_HAS_REFSEL |
			AD7793_FLAG_HAS_VBIAS |
			AD7793_HAS_EXITATION_CURRENT |
			AD7793_FLAG_HAS_GAIN |
			AD7793_FLAG_HAS_BUFFER,
	},
	[ID_AD7795] = {
		.id = AD7795_ID,
		.channels = ad7795_channels,
		.num_channels = ARRAY_SIZE(ad7795_channels),
		.iio_info = &ad7793_info,
		.sample_freq_avail = ad7793_sample_freq_avail,
		.flags = AD7793_FLAG_HAS_CLKSEL |
			AD7793_FLAG_HAS_REFSEL |
			AD7793_FLAG_HAS_VBIAS |
			AD7793_HAS_EXITATION_CURRENT |
			AD7793_FLAG_HAS_GAIN |
			AD7793_FLAG_HAS_BUFFER,
	},
	[ID_AD7796] = {
		.id = AD7796_ID,
		.channels = ad7796_channels,
		.num_channels = ARRAY_SIZE(ad7796_channels),
		.iio_info = &ad7797_info,
		.sample_freq_avail = ad7797_sample_freq_avail,
		.flags = AD7793_FLAG_HAS_CLKSEL,
	},
	[ID_AD7797] = {
		.id = AD7797_ID,
		.channels = ad7797_channels,
		.num_channels = ARRAY_SIZE(ad7797_channels),
		.iio_info = &ad7797_info,
		.sample_freq_avail = ad7797_sample_freq_avail,
		.flags = AD7793_FLAG_HAS_CLKSEL,
	},
	[ID_AD7798] = {
		.id = AD7798_ID,
		.channels = ad7798_channels,
		.num_channels = ARRAY_SIZE(ad7798_channels),
		.iio_info = &ad7793_info,
		.sample_freq_avail = ad7793_sample_freq_avail,
		.flags = AD7793_FLAG_HAS_GAIN |
			AD7793_FLAG_HAS_BUFFER,
	},
	[ID_AD7799] = {
		.id = AD7799_ID,
		.channels = ad7799_channels,
		.num_channels = ARRAY_SIZE(ad7799_channels),
		.iio_info = &ad7793_info,
		.sample_freq_avail = ad7793_sample_freq_avail,
		.flags = AD7793_FLAG_HAS_GAIN |
			AD7793_FLAG_HAS_BUFFER,
	},
};

static void ad7793_reg_disable(void *reg)
{
	regulator_disable(reg);
}

static int ad7793_probe(struct spi_device *spi)
{
	const struct ad7793_platform_data *pdata = spi->dev.platform_data;
	struct ad7793_state *st;
	struct iio_dev *indio_dev;
	int ret, vref_mv = 0;

	if (!pdata) {
		dev_err(&spi->dev, "no platform data?\n");
		return -ENODEV;
	}

	if (!spi->irq) {
		dev_err(&spi->dev, "no IRQ?\n");
		return -ENODEV;
	}

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (indio_dev == NULL)
		return -ENOMEM;

	st = iio_priv(indio_dev);

	ad_sd_init(&st->sd, indio_dev, spi, &ad7793_sigma_delta_info);

	if (pdata->refsel != AD7793_REFSEL_INTERNAL) {
		st->reg = devm_regulator_get(&spi->dev, "refin");
		if (IS_ERR(st->reg))
			return PTR_ERR(st->reg);

		ret = regulator_enable(st->reg);
		if (ret)
			return ret;

		ret = devm_add_action_or_reset(&spi->dev, ad7793_reg_disable, st->reg);
		if (ret)
			return ret;

		vref_mv = regulator_get_voltage(st->reg);
		if (vref_mv < 0)
			return vref_mv;

		vref_mv /= 1000;
	} else {
		vref_mv = 1170; /* Build-in ref */
	}

	st->chip_info =
		&ad7793_chip_info_tbl[spi_get_device_id(spi)->driver_data];

	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = st->chip_info->channels;
	indio_dev->num_channels = st->chip_info->num_channels;
	indio_dev->info = st->chip_info->iio_info;

	ret = devm_ad_sd_setup_buffer_and_trigger(&spi->dev, indio_dev);
	if (ret)
		return ret;

	ret = ad7793_setup(indio_dev, pdata, vref_mv);
	if (ret)
		return ret;

	return devm_iio_device_register(&spi->dev, indio_dev);
}

static const struct spi_device_id ad7793_id[] = {
	{"ad7785", ID_AD7785},
	{"ad7792", ID_AD7792},
	{"ad7793", ID_AD7793},
	{"ad7794", ID_AD7794},
	{"ad7795", ID_AD7795},
	{"ad7796", ID_AD7796},
	{"ad7797", ID_AD7797},
	{"ad7798", ID_AD7798},
	{"ad7799", ID_AD7799},
	{}
};
MODULE_DEVICE_TABLE(spi, ad7793_id);

static struct spi_driver ad7793_driver = {
	.driver = {
		.name	= "ad7793",
	},
	.probe		= ad7793_probe,
	.id_table	= ad7793_id,
};
module_spi_driver(ad7793_driver);

MODULE_AUTHOR("Michael Hennerich <michael.hennerich@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD7793 and similar ADCs");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(IIO_AD_SIGMA_DELTA);
