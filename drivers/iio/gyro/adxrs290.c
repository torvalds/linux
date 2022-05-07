// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ADXRS290 SPI Gyroscope Driver
 *
 * Copyright (C) 2020 Nishant Malpani <nish.malpani25@gmail.com>
 * Copyright (C) 2020 Analog Devices, Inc.
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spi/spi.h>

#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>

#define ADXRS290_ADI_ID		0xAD
#define ADXRS290_MEMS_ID	0x1D
#define ADXRS290_DEV_ID		0x92

#define ADXRS290_REG_ADI_ID	0x00
#define ADXRS290_REG_MEMS_ID	0x01
#define ADXRS290_REG_DEV_ID	0x02
#define ADXRS290_REG_REV_ID	0x03
#define ADXRS290_REG_SN0	0x04 /* Serial Number Registers, 4 bytes */
#define ADXRS290_REG_DATAX0	0x08 /* Roll Rate o/p Data Regs, 2 bytes */
#define ADXRS290_REG_DATAY0	0x0A /* Pitch Rate o/p Data Regs, 2 bytes */
#define ADXRS290_REG_TEMP0	0x0C
#define ADXRS290_REG_POWER_CTL	0x10
#define ADXRS290_REG_FILTER	0x11
#define ADXRS290_REG_DATA_RDY	0x12

#define ADXRS290_READ		BIT(7)
#define ADXRS290_TSM		BIT(0)
#define ADXRS290_MEASUREMENT	BIT(1)
#define ADXRS290_DATA_RDY_OUT	BIT(0)
#define ADXRS290_SYNC_MASK	GENMASK(1, 0)
#define ADXRS290_SYNC(x)	FIELD_PREP(ADXRS290_SYNC_MASK, x)
#define ADXRS290_LPF_MASK	GENMASK(2, 0)
#define ADXRS290_LPF(x)		FIELD_PREP(ADXRS290_LPF_MASK, x)
#define ADXRS290_HPF_MASK	GENMASK(7, 4)
#define ADXRS290_HPF(x)		FIELD_PREP(ADXRS290_HPF_MASK, x)

#define ADXRS290_READ_REG(reg)	(ADXRS290_READ | (reg))

#define ADXRS290_MAX_TRANSITION_TIME_MS 100

enum adxrs290_mode {
	ADXRS290_MODE_STANDBY,
	ADXRS290_MODE_MEASUREMENT,
};

enum adxrs290_scan_index {
	ADXRS290_IDX_X,
	ADXRS290_IDX_Y,
	ADXRS290_IDX_TEMP,
	ADXRS290_IDX_TS,
};

struct adxrs290_state {
	struct spi_device	*spi;
	/* Serialize reads and their subsequent processing */
	struct mutex		lock;
	enum adxrs290_mode	mode;
	unsigned int		lpf_3db_freq_idx;
	unsigned int		hpf_3db_freq_idx;
	struct iio_trigger      *dready_trig;
	/* Ensure correct alignment of timestamp when present */
	struct {
		s16 channels[3];
		s64 ts __aligned(8);
	} buffer;
};

/*
 * Available cut-off frequencies of the low pass filter in Hz.
 * The integer part and fractional part are represented separately.
 */
static const int adxrs290_lpf_3db_freq_hz_table[][2] = {
	[0] = {480, 0},
	[1] = {320, 0},
	[2] = {160, 0},
	[3] = {80, 0},
	[4] = {56, 600000},
	[5] = {40, 0},
	[6] = {28, 300000},
	[7] = {20, 0},
};

/*
 * Available cut-off frequencies of the high pass filter in Hz.
 * The integer part and fractional part are represented separately.
 */
static const int adxrs290_hpf_3db_freq_hz_table[][2] = {
	[0] = {0, 0},
	[1] = {0, 11000},
	[2] = {0, 22000},
	[3] = {0, 44000},
	[4] = {0, 87000},
	[5] = {0, 175000},
	[6] = {0, 350000},
	[7] = {0, 700000},
	[8] = {1, 400000},
	[9] = {2, 800000},
	[10] = {11, 300000},
};

static int adxrs290_get_rate_data(struct iio_dev *indio_dev, const u8 cmd, int *val)
{
	struct adxrs290_state *st = iio_priv(indio_dev);
	int ret = 0;
	int temp;

	mutex_lock(&st->lock);
	temp = spi_w8r16(st->spi, cmd);
	if (temp < 0) {
		ret = temp;
		goto err_unlock;
	}

	*val = temp;

err_unlock:
	mutex_unlock(&st->lock);
	return ret;
}

static int adxrs290_get_temp_data(struct iio_dev *indio_dev, int *val)
{
	const u8 cmd = ADXRS290_READ_REG(ADXRS290_REG_TEMP0);
	struct adxrs290_state *st = iio_priv(indio_dev);
	int ret = 0;
	int temp;

	mutex_lock(&st->lock);
	temp = spi_w8r16(st->spi, cmd);
	if (temp < 0) {
		ret = temp;
		goto err_unlock;
	}

	/* extract lower 12 bits temperature reading */
	*val = temp & 0x0FFF;

err_unlock:
	mutex_unlock(&st->lock);
	return ret;
}

static int adxrs290_get_3db_freq(struct iio_dev *indio_dev, u8 *val, u8 *val2)
{
	const u8 cmd = ADXRS290_READ_REG(ADXRS290_REG_FILTER);
	struct adxrs290_state *st = iio_priv(indio_dev);
	int ret = 0;
	short temp;

	mutex_lock(&st->lock);
	temp = spi_w8r8(st->spi, cmd);
	if (temp < 0) {
		ret = temp;
		goto err_unlock;
	}

	*val = FIELD_GET(ADXRS290_LPF_MASK, temp);
	*val2 = FIELD_GET(ADXRS290_HPF_MASK, temp);

err_unlock:
	mutex_unlock(&st->lock);
	return ret;
}

static int adxrs290_spi_write_reg(struct spi_device *spi, const u8 reg,
				  const u8 val)
{
	u8 buf[2];

	buf[0] = reg;
	buf[1] = val;

	return spi_write_then_read(spi, buf, ARRAY_SIZE(buf), NULL, 0);
}

static int adxrs290_find_match(const int (*freq_tbl)[2], const int n,
			       const int val, const int val2)
{
	int i;

	for (i = 0; i < n; i++) {
		if (freq_tbl[i][0] == val && freq_tbl[i][1] == val2)
			return i;
	}

	return -EINVAL;
}

static int adxrs290_set_filter_freq(struct iio_dev *indio_dev,
				    const unsigned int lpf_idx,
				    const unsigned int hpf_idx)
{
	struct adxrs290_state *st = iio_priv(indio_dev);
	u8 val;

	val = ADXRS290_HPF(hpf_idx) | ADXRS290_LPF(lpf_idx);

	return adxrs290_spi_write_reg(st->spi, ADXRS290_REG_FILTER, val);
}

static int adxrs290_set_mode(struct iio_dev *indio_dev, enum adxrs290_mode mode)
{
	struct adxrs290_state *st = iio_priv(indio_dev);
	int val, ret;

	if (st->mode == mode)
		return 0;

	mutex_lock(&st->lock);

	ret = spi_w8r8(st->spi, ADXRS290_READ_REG(ADXRS290_REG_POWER_CTL));
	if (ret < 0)
		goto out_unlock;

	val = ret;

	switch (mode) {
	case ADXRS290_MODE_STANDBY:
		val &= ~ADXRS290_MEASUREMENT;
		break;
	case ADXRS290_MODE_MEASUREMENT:
		val |= ADXRS290_MEASUREMENT;
		break;
	default:
		ret = -EINVAL;
		goto out_unlock;
	}

	ret = adxrs290_spi_write_reg(st->spi, ADXRS290_REG_POWER_CTL, val);
	if (ret < 0) {
		dev_err(&st->spi->dev, "unable to set mode: %d\n", ret);
		goto out_unlock;
	}

	/* update cached mode */
	st->mode = mode;

out_unlock:
	mutex_unlock(&st->lock);
	return ret;
}

static void adxrs290_chip_off_action(void *data)
{
	struct iio_dev *indio_dev = data;

	adxrs290_set_mode(indio_dev, ADXRS290_MODE_STANDBY);
}

static int adxrs290_initial_setup(struct iio_dev *indio_dev)
{
	struct adxrs290_state *st = iio_priv(indio_dev);
	struct spi_device *spi = st->spi;
	int ret;

	ret = adxrs290_spi_write_reg(spi, ADXRS290_REG_POWER_CTL,
				     ADXRS290_MEASUREMENT | ADXRS290_TSM);
	if (ret < 0)
		return ret;

	st->mode = ADXRS290_MODE_MEASUREMENT;

	return devm_add_action_or_reset(&spi->dev, adxrs290_chip_off_action,
					indio_dev);
}

static int adxrs290_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int *val,
			     int *val2,
			     long mask)
{
	struct adxrs290_state *st = iio_priv(indio_dev);
	unsigned int t;
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;

		switch (chan->type) {
		case IIO_ANGL_VEL:
			ret = adxrs290_get_rate_data(indio_dev,
						     ADXRS290_READ_REG(chan->address),
						     val);
			if (ret < 0)
				break;

			ret = IIO_VAL_INT;
			break;
		case IIO_TEMP:
			ret = adxrs290_get_temp_data(indio_dev, val);
			if (ret < 0)
				break;

			ret = IIO_VAL_INT;
			break;
		default:
			ret = -EINVAL;
			break;
		}

		iio_device_release_direct_mode(indio_dev);
		return ret;
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_ANGL_VEL:
			/* 1 LSB = 0.005 degrees/sec */
			*val = 0;
			*val2 = 87266;
			return IIO_VAL_INT_PLUS_NANO;
		case IIO_TEMP:
			/* 1 LSB = 0.1 degrees Celsius */
			*val = 100;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		switch (chan->type) {
		case IIO_ANGL_VEL:
			t = st->lpf_3db_freq_idx;
			*val = adxrs290_lpf_3db_freq_hz_table[t][0];
			*val2 = adxrs290_lpf_3db_freq_hz_table[t][1];
			return IIO_VAL_INT_PLUS_MICRO;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_HIGH_PASS_FILTER_3DB_FREQUENCY:
		switch (chan->type) {
		case IIO_ANGL_VEL:
			t = st->hpf_3db_freq_idx;
			*val = adxrs290_hpf_3db_freq_hz_table[t][0];
			*val2 = adxrs290_hpf_3db_freq_hz_table[t][1];
			return IIO_VAL_INT_PLUS_MICRO;
		default:
			return -EINVAL;
		}
	}

	return -EINVAL;
}

static int adxrs290_write_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int val,
			      int val2,
			      long mask)
{
	struct adxrs290_state *st = iio_priv(indio_dev);
	int ret, lpf_idx, hpf_idx;

	ret = iio_device_claim_direct_mode(indio_dev);
	if (ret)
		return ret;

	switch (mask) {
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		lpf_idx = adxrs290_find_match(adxrs290_lpf_3db_freq_hz_table,
					      ARRAY_SIZE(adxrs290_lpf_3db_freq_hz_table),
					      val, val2);
		if (lpf_idx < 0) {
			ret = -EINVAL;
			break;
		}

		/* caching the updated state of the low-pass filter */
		st->lpf_3db_freq_idx = lpf_idx;
		/* retrieving the current state of the high-pass filter */
		hpf_idx = st->hpf_3db_freq_idx;
		ret = adxrs290_set_filter_freq(indio_dev, lpf_idx, hpf_idx);
		break;

	case IIO_CHAN_INFO_HIGH_PASS_FILTER_3DB_FREQUENCY:
		hpf_idx = adxrs290_find_match(adxrs290_hpf_3db_freq_hz_table,
					      ARRAY_SIZE(adxrs290_hpf_3db_freq_hz_table),
					      val, val2);
		if (hpf_idx < 0) {
			ret = -EINVAL;
			break;
		}

		/* caching the updated state of the high-pass filter */
		st->hpf_3db_freq_idx = hpf_idx;
		/* retrieving the current state of the low-pass filter */
		lpf_idx = st->lpf_3db_freq_idx;
		ret = adxrs290_set_filter_freq(indio_dev, lpf_idx, hpf_idx);
		break;

	default:
		ret = -EINVAL;
		break;
	}

	iio_device_release_direct_mode(indio_dev);
	return ret;
}

static int adxrs290_read_avail(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       const int **vals, int *type, int *length,
			       long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		*vals = (const int *)adxrs290_lpf_3db_freq_hz_table;
		*type = IIO_VAL_INT_PLUS_MICRO;
		/* Values are stored in a 2D matrix */
		*length = ARRAY_SIZE(adxrs290_lpf_3db_freq_hz_table) * 2;

		return IIO_AVAIL_LIST;
	case IIO_CHAN_INFO_HIGH_PASS_FILTER_3DB_FREQUENCY:
		*vals = (const int *)adxrs290_hpf_3db_freq_hz_table;
		*type = IIO_VAL_INT_PLUS_MICRO;
		/* Values are stored in a 2D matrix */
		*length = ARRAY_SIZE(adxrs290_hpf_3db_freq_hz_table) * 2;

		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
}

static int adxrs290_reg_access_rw(struct spi_device *spi, unsigned int reg,
				  unsigned int *readval)
{
	int ret;

	ret = spi_w8r8(spi, ADXRS290_READ_REG(reg));
	if (ret < 0)
		return ret;

	*readval = ret;

	return 0;
}

static int adxrs290_reg_access(struct iio_dev *indio_dev, unsigned int reg,
			       unsigned int writeval, unsigned int *readval)
{
	struct adxrs290_state *st = iio_priv(indio_dev);

	if (readval)
		return adxrs290_reg_access_rw(st->spi, reg, readval);
	else
		return adxrs290_spi_write_reg(st->spi, reg, writeval);
}

static int adxrs290_data_rdy_trigger_set_state(struct iio_trigger *trig,
					       bool state)
{
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	struct adxrs290_state *st = iio_priv(indio_dev);
	int ret;
	u8 val;

	val = state ? ADXRS290_SYNC(ADXRS290_DATA_RDY_OUT) : 0;

	ret = adxrs290_spi_write_reg(st->spi, ADXRS290_REG_DATA_RDY, val);
	if (ret < 0)
		dev_err(&st->spi->dev, "failed to start data rdy interrupt\n");

	return ret;
}

static int adxrs290_reset_trig(struct iio_trigger *trig)
{
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	int val;

	/*
	 * Data ready interrupt is reset after a read of the data registers.
	 * Here, we only read the 16b DATAY registers as that marks the end of
	 * a read of the data registers and initiates a reset for the interrupt
	 * line.
	 */
	adxrs290_get_rate_data(indio_dev,
			       ADXRS290_READ_REG(ADXRS290_REG_DATAY0), &val);

	return 0;
}

static const struct iio_trigger_ops adxrs290_trigger_ops = {
	.set_trigger_state = &adxrs290_data_rdy_trigger_set_state,
	.validate_device = &iio_trigger_validate_own_device,
	.try_reenable = &adxrs290_reset_trig,
};

static irqreturn_t adxrs290_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct adxrs290_state *st = iio_priv(indio_dev);
	u8 tx = ADXRS290_READ_REG(ADXRS290_REG_DATAX0);
	int ret;

	mutex_lock(&st->lock);

	/* exercise a bulk data capture starting from reg DATAX0... */
	ret = spi_write_then_read(st->spi, &tx, sizeof(tx), st->buffer.channels,
				  sizeof(st->buffer.channels));
	if (ret < 0)
		goto out_unlock_notify;

	iio_push_to_buffers_with_timestamp(indio_dev, &st->buffer,
					   pf->timestamp);

out_unlock_notify:
	mutex_unlock(&st->lock);
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

#define ADXRS290_ANGL_VEL_CHANNEL(reg, axis) {				\
	.type = IIO_ANGL_VEL,						\
	.address = reg,							\
	.modified = 1,							\
	.channel2 = IIO_MOD_##axis,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),			\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |		\
	BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY) |		\
	BIT(IIO_CHAN_INFO_HIGH_PASS_FILTER_3DB_FREQUENCY),		\
	.info_mask_shared_by_type_available =				\
	BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY) |		\
	BIT(IIO_CHAN_INFO_HIGH_PASS_FILTER_3DB_FREQUENCY),		\
	.scan_index = ADXRS290_IDX_##axis,				\
	.scan_type = {                                                  \
		.sign = 's',                                            \
		.realbits = 16,                                         \
		.storagebits = 16,                                      \
		.endianness = IIO_LE,					\
	},                                                              \
}

static const struct iio_chan_spec adxrs290_channels[] = {
	ADXRS290_ANGL_VEL_CHANNEL(ADXRS290_REG_DATAX0, X),
	ADXRS290_ANGL_VEL_CHANNEL(ADXRS290_REG_DATAY0, Y),
	{
		.type = IIO_TEMP,
		.address = ADXRS290_REG_TEMP0,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
		BIT(IIO_CHAN_INFO_SCALE),
		.scan_index = ADXRS290_IDX_TEMP,
		.scan_type = {
			.sign = 's',
			.realbits = 12,
			.storagebits = 16,
			.endianness = IIO_LE,
		},
	},
	IIO_CHAN_SOFT_TIMESTAMP(ADXRS290_IDX_TS),
};

static const unsigned long adxrs290_avail_scan_masks[] = {
	BIT(ADXRS290_IDX_X) | BIT(ADXRS290_IDX_Y) | BIT(ADXRS290_IDX_TEMP),
	0
};

static const struct iio_info adxrs290_info = {
	.read_raw = &adxrs290_read_raw,
	.write_raw = &adxrs290_write_raw,
	.read_avail = &adxrs290_read_avail,
	.debugfs_reg_access = &adxrs290_reg_access,
};

static int adxrs290_probe_trigger(struct iio_dev *indio_dev)
{
	struct adxrs290_state *st = iio_priv(indio_dev);
	int ret;

	if (!st->spi->irq) {
		dev_info(&st->spi->dev, "no irq, using polling\n");
		return 0;
	}

	st->dready_trig = devm_iio_trigger_alloc(&st->spi->dev, "%s-dev%d",
						 indio_dev->name,
						 indio_dev->id);
	if (!st->dready_trig)
		return -ENOMEM;

	st->dready_trig->dev.parent = &st->spi->dev;
	st->dready_trig->ops = &adxrs290_trigger_ops;
	iio_trigger_set_drvdata(st->dready_trig, indio_dev);

	ret = devm_request_irq(&st->spi->dev, st->spi->irq,
			       &iio_trigger_generic_data_rdy_poll,
			       IRQF_ONESHOT, "adxrs290_irq", st->dready_trig);
	if (ret < 0)
		return dev_err_probe(&st->spi->dev, ret,
				     "request irq %d failed\n", st->spi->irq);

	ret = devm_iio_trigger_register(&st->spi->dev, st->dready_trig);
	if (ret) {
		dev_err(&st->spi->dev, "iio trigger register failed\n");
		return ret;
	}

	indio_dev->trig = iio_trigger_get(st->dready_trig);

	return 0;
}

static int adxrs290_probe(struct spi_device *spi)
{
	struct iio_dev *indio_dev;
	struct adxrs290_state *st;
	u8 val, val2;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	st->spi = spi;

	indio_dev->name = "adxrs290";
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = adxrs290_channels;
	indio_dev->num_channels = ARRAY_SIZE(adxrs290_channels);
	indio_dev->info = &adxrs290_info;
	indio_dev->available_scan_masks = adxrs290_avail_scan_masks;

	mutex_init(&st->lock);

	val = spi_w8r8(spi, ADXRS290_READ_REG(ADXRS290_REG_ADI_ID));
	if (val != ADXRS290_ADI_ID) {
		dev_err(&spi->dev, "Wrong ADI ID 0x%02x\n", val);
		return -ENODEV;
	}

	val = spi_w8r8(spi, ADXRS290_READ_REG(ADXRS290_REG_MEMS_ID));
	if (val != ADXRS290_MEMS_ID) {
		dev_err(&spi->dev, "Wrong MEMS ID 0x%02x\n", val);
		return -ENODEV;
	}

	val = spi_w8r8(spi, ADXRS290_READ_REG(ADXRS290_REG_DEV_ID));
	if (val != ADXRS290_DEV_ID) {
		dev_err(&spi->dev, "Wrong DEV ID 0x%02x\n", val);
		return -ENODEV;
	}

	/* default mode the gyroscope starts in */
	st->mode = ADXRS290_MODE_STANDBY;

	/* switch to measurement mode and switch on the temperature sensor */
	ret = adxrs290_initial_setup(indio_dev);
	if (ret < 0)
		return ret;

	/* max transition time to measurement mode */
	msleep(ADXRS290_MAX_TRANSITION_TIME_MS);

	ret = adxrs290_get_3db_freq(indio_dev, &val, &val2);
	if (ret < 0)
		return ret;

	st->lpf_3db_freq_idx = val;
	st->hpf_3db_freq_idx = val2;

	ret = devm_iio_triggered_buffer_setup(&spi->dev, indio_dev,
					      &iio_pollfunc_store_time,
					      &adxrs290_trigger_handler, NULL);
	if (ret < 0)
		return dev_err_probe(&spi->dev, ret,
				     "iio triggered buffer setup failed\n");

	ret = adxrs290_probe_trigger(indio_dev);
	if (ret < 0)
		return ret;

	return devm_iio_device_register(&spi->dev, indio_dev);
}

static const struct of_device_id adxrs290_of_match[] = {
	{ .compatible = "adi,adxrs290" },
	{ }
};
MODULE_DEVICE_TABLE(of, adxrs290_of_match);

static struct spi_driver adxrs290_driver = {
	.driver = {
		.name = "adxrs290",
		.of_match_table = adxrs290_of_match,
	},
	.probe = adxrs290_probe,
};
module_spi_driver(adxrs290_driver);

MODULE_AUTHOR("Nishant Malpani <nish.malpani25@gmail.com>");
MODULE_DESCRIPTION("Analog Devices ADXRS290 Gyroscope SPI driver");
MODULE_LICENSE("GPL");
