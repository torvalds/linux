// SPDX-License-Identifier: GPL-2.0-only
/*
 * BM1390 ROHM pressure sensor
 *
 * Copyright (c) 2023, ROHM Semiconductor.
 * https://fscdn.rohm.com/en/products/databook/datasheet/ic/sensor/pressure/bm1390glv-z-e.pdf
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#include <linux/iio/iio.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#define BM1390_REG_MANUFACT_ID		0x0f
#define BM1390_REG_PART_ID		0x10
#define BM1390_REG_POWER		0x12
#define BM1390_MASK_POWER		BIT(0)
#define BM1390_POWER_ON			BM1390_MASK_POWER
#define BM1390_POWER_OFF		0x00
#define BM1390_REG_RESET		0x13
#define BM1390_MASK_RESET		BIT(0)
#define BM1390_RESET_RELEASE		BM1390_MASK_RESET
#define BM1390_RESET			0x00
#define BM1390_REG_MODE_CTRL		0x14
#define BM1390_MASK_MEAS_MODE		GENMASK(1, 0)
#define BM1390_MASK_DRDY_EN		BIT(4)
#define BM1390_MASK_WMI_EN		BIT(2)
#define BM1390_MASK_AVE_NUM		GENMASK(7, 5)

/*
 * Data-sheet states that when the IIR is used, the AVE_NUM must be set to
 * value 110b
 */
#define BM1390_IIR_AVE_NUM		0x06
#define BM1390_REG_FIFO_CTRL		0x15
#define BM1390_MASK_IIR_MODE		GENMASK(1, 0)
#define BM1390_IIR_MODE_OFF		0x0
#define BM1390_IIR_MODE_WEAK		0x1
#define BM1390_IIR_MODE_MID		0x2
#define BM1390_IIR_MODE_STRONG		0x3

#define BM1390_MASK_FIFO_LEN		BIT(6)
#define BM1390_MASK_FIFO_EN		BIT(7)
#define BM1390_WMI_MIN			2
#define BM1390_WMI_MAX			3

#define BM1390_REG_FIFO_LVL		0x18
#define BM1390_MASK_FIFO_LVL		GENMASK(2, 0)
#define BM1390_REG_STATUS		0x19
#define BM1390_REG_PRESSURE_BASE	0x1a
#define BM1390_REG_TEMP_HI		0x1d
#define BM1390_REG_TEMP_LO		0x1e
#define BM1390_MAX_REGISTER		BM1390_REG_TEMP_LO

#define BM1390_ID			0x34

/* Regmap configs */
static const struct regmap_range bm1390_volatile_ranges[] = {
	{
		.range_min = BM1390_REG_STATUS,
		.range_max = BM1390_REG_STATUS,
	},
	{
		.range_min = BM1390_REG_FIFO_LVL,
		.range_max = BM1390_REG_TEMP_LO,
	},
};

static const struct regmap_access_table bm1390_volatile_regs = {
	.yes_ranges = &bm1390_volatile_ranges[0],
	.n_yes_ranges = ARRAY_SIZE(bm1390_volatile_ranges),
};

static const struct regmap_range bm1390_precious_ranges[] = {
	{
		.range_min = BM1390_REG_STATUS,
		.range_max = BM1390_REG_STATUS,
	},
};

static const struct regmap_access_table bm1390_precious_regs = {
	.yes_ranges = &bm1390_precious_ranges[0],
	.n_yes_ranges = ARRAY_SIZE(bm1390_precious_ranges),
};

static const struct regmap_range bm1390_read_only_ranges[] = {
	{
		.range_min = BM1390_REG_MANUFACT_ID,
		.range_max = BM1390_REG_PART_ID,
	}, {
		.range_min = BM1390_REG_FIFO_LVL,
		.range_max = BM1390_REG_TEMP_LO,
	},
};

static const struct regmap_access_table bm1390_ro_regs = {
	.no_ranges = &bm1390_read_only_ranges[0],
	.n_no_ranges = ARRAY_SIZE(bm1390_read_only_ranges),
};

static const struct regmap_range bm1390_noinc_read_ranges[] = {
	{
		.range_min = BM1390_REG_PRESSURE_BASE,
		.range_max = BM1390_REG_TEMP_LO,
	},
};

static const struct regmap_access_table bm1390_nir_regs = {
	.yes_ranges = &bm1390_noinc_read_ranges[0],
	.n_yes_ranges = ARRAY_SIZE(bm1390_noinc_read_ranges),
};

static const struct regmap_config bm1390_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.volatile_table = &bm1390_volatile_regs,
	.wr_table = &bm1390_ro_regs,
	.rd_noinc_table = &bm1390_nir_regs,
	.precious_table = &bm1390_precious_regs,
	.max_register = BM1390_MAX_REGISTER,
	.cache_type = REGCACHE_RBTREE,
	.disable_locking = true,
};

enum {
	BM1390_STATE_SAMPLE,
	BM1390_STATE_FIFO,
};

struct bm1390_data_buf {
	u32 pressure;
	__be16 temp;
	aligned_s64 ts;
};

/* BM1390 has FIFO for 4 pressure samples */
#define BM1390_FIFO_LENGTH	4

struct bm1390_data {
	s64 timestamp, old_timestamp;
	struct iio_trigger *trig;
	struct regmap *regmap;
	struct device *dev;
	struct bm1390_data_buf buf;
	int irq;
	unsigned int state;
	bool trigger_enabled;
	u8 watermark;

	/* Prevent accessing sensor during FIFO read sequence */
	struct mutex mutex;
};

enum {
	BM1390_CHAN_PRESSURE,
	BM1390_CHAN_TEMP,
};

static const struct iio_chan_spec bm1390_channels[] = {
	{
		.type = IIO_PRESSURE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		/*
		 * When IIR is used, we must fix amount of averaged samples.
		 * Thus we don't allow setting oversampling ratio.
		 */
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
		.scan_index = BM1390_CHAN_PRESSURE,
		.scan_type = {
			.sign = 'u',
			.realbits = 22,
			.storagebits = 32,
			.endianness = IIO_LE,
		},
	},
	{
		.type = IIO_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
		.scan_index = BM1390_CHAN_TEMP,
		.scan_type = {
			.sign = 's',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_BE,
		},
	},
	IIO_CHAN_SOFT_TIMESTAMP(2),
};

/*
 * We can't skip reading the pressure because the watermark IRQ is acked
 * only when the pressure data is read from the FIFO.
 */
static const unsigned long bm1390_scan_masks[] = {
	BIT(BM1390_CHAN_PRESSURE),
	BIT(BM1390_CHAN_PRESSURE) | BIT(BM1390_CHAN_TEMP),
	0
};

static int bm1390_read_temp(struct bm1390_data *data, int *temp)
{
	__be16 temp_raw;
	int ret;

	ret = regmap_bulk_read(data->regmap, BM1390_REG_TEMP_HI, &temp_raw,
			       sizeof(temp_raw));
	if (ret)
		return ret;

	*temp = be16_to_cpu(temp_raw);

	return 0;
}

static int bm1390_pressure_read(struct bm1390_data *data, u32 *pressure)
{
	/* Pressure data is in 3 8-bit registers */
	u8 raw[3];
	int ret;

	ret = regmap_bulk_read(data->regmap, BM1390_REG_PRESSURE_BASE,
			       raw, sizeof(raw));
	if (ret < 0)
		return ret;

	*pressure = (u32)(raw[2] >> 2 | raw[1] << 6 | raw[0] << 14);

	return 0;
}

 /* The enum values map directly to register bits */
enum bm1390_meas_mode {
	BM1390_MEAS_MODE_STOP = 0x0,
	BM1390_MEAS_MODE_1SHOT = 0x1,
	BM1390_MEAS_MODE_CONTINUOUS = 0x2,
};

static int bm1390_meas_set(struct bm1390_data *data, enum bm1390_meas_mode mode)
{
	return regmap_update_bits(data->regmap, BM1390_REG_MODE_CTRL,
				  BM1390_MASK_MEAS_MODE, mode);
}

/*
 * If the trigger is not used we just wait until the measurement has
 * completed. The data-sheet says maximum measurement cycle (regardless
 * the AVE_NUM) is 200 mS so let's just sleep at least that long. If speed
 * is needed the trigger should be used.
 */
#define BM1390_MAX_MEAS_TIME_MS 205

static int bm1390_read_data(struct bm1390_data *data,
			struct iio_chan_spec const *chan, int *val, int *val2)
{
	int ret, warn;

	guard(mutex)(&data->mutex);
	/*
	 * We use 'continuous mode' even for raw read because according to the
	 * data-sheet an one-shot mode can't be used with IIR filter.
	 */
	ret = bm1390_meas_set(data, BM1390_MEAS_MODE_CONTINUOUS);
	if (ret)
		return ret;

	switch (chan->type) {
	case IIO_PRESSURE:
		msleep(BM1390_MAX_MEAS_TIME_MS);
		ret = bm1390_pressure_read(data, val);
		break;
	case IIO_TEMP:
		msleep(BM1390_MAX_MEAS_TIME_MS);
		ret = bm1390_read_temp(data, val);
		break;
	default:
		ret = -EINVAL;
	}
	warn = bm1390_meas_set(data, BM1390_MEAS_MODE_STOP);
	if (warn)
		dev_warn(data->dev, "Failed to stop measurement (%d)\n", warn);

	return 0;
}

static int bm1390_read_raw(struct iio_dev *idev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	struct bm1390_data *data = iio_priv(idev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		if (chan->type == IIO_TEMP) {
			*val = 31;
			*val2 = 250000;

			return IIO_VAL_INT_PLUS_MICRO;
		} else if (chan->type == IIO_PRESSURE) {
			/*
			 * pressure in hPa is register value divided by 2048.
			 * This means kPa is 1/20480 times the register value,
			 */
			*val = 1;
			*val2 = 2048;

			return IIO_VAL_FRACTIONAL;
		}

		return -EINVAL;
	case IIO_CHAN_INFO_RAW:
		if (!iio_device_claim_direct(idev))
			return -EBUSY;

		ret = bm1390_read_data(data, chan, val, val2);
		iio_device_release_direct(idev);
		if (ret)
			return ret;

		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int __bm1390_fifo_flush(struct iio_dev *idev, unsigned int samples,
			       s64 timestamp)
{
	/* BM1390_FIFO_LENGTH is small so we shouldn't run out of stack */
	struct bm1390_data_buf buffer[BM1390_FIFO_LENGTH];
	struct bm1390_data *data = iio_priv(idev);
	int smp_lvl, ret, i, warn, dummy;
	u64 sample_period;
	__be16 temp = 0;

	ret = regmap_read(data->regmap, BM1390_REG_FIFO_LVL, &smp_lvl);
	if (ret)
		return ret;

	smp_lvl = FIELD_GET(BM1390_MASK_FIFO_LVL, smp_lvl);
	if (!smp_lvl)
		return 0;

	if (smp_lvl > BM1390_FIFO_LENGTH) {
		/*
		 * The fifo holds maximum of 4 samples so valid values
		 * should be 0, 1, 2, 3, 4 - rest are probably bit errors
		 * in I2C line. Don't overflow if this happens.
		 */
		dev_err(data->dev, "bad FIFO level %d\n", smp_lvl);
		smp_lvl = BM1390_FIFO_LENGTH;
	}

	sample_period = timestamp - data->old_timestamp;
	do_div(sample_period, smp_lvl);

	if (samples && smp_lvl > samples)
		smp_lvl = samples;


	/*
	 * After some testing it appears that the temperature is not readable
	 * until the FIFO access has been done after the WMI. Thus, we need
	 * to read the all pressure values to memory and read the temperature
	 * only after that.
	 */
	for (i = 0; i < smp_lvl; i++) {
		/*
		 * When we start reading data from the FIFO the sensor goes to
		 * special FIFO reading mode. If any other register is accessed
		 * during the FIFO read, samples can be dropped. Prevent access
		 * until FIFO_LVL is read. We have mutex locked and we do also
		 * go performing reading of FIFO_LVL even if this read fails.
		 */
		if (test_bit(BM1390_CHAN_PRESSURE, idev->active_scan_mask)) {
			ret = bm1390_pressure_read(data, &buffer[i].pressure);
			if (ret)
				break;
		}

		/*
		 * Old timestamp is either the previous sample IRQ time,
		 * previous flush-time or, if this was first sample, the enable
		 * time. When we add a sample period to that we should get the
		 * best approximation of the time-stamp we are handling.
		 *
		 * Idea is to always keep the "old_timestamp" matching the
		 * timestamp which we are currently handling.
		 */
		data->old_timestamp += sample_period;
		buffer[i].ts = data->old_timestamp;
	}
	/* Reading the FIFO_LVL closes the FIFO access sequence */
	warn = regmap_read(data->regmap, BM1390_REG_FIFO_LVL, &dummy);
	if (warn)
		dev_warn(data->dev, "Closing FIFO sequence failed\n");

	if (ret)
		return ret;

	if (test_bit(BM1390_CHAN_TEMP, idev->active_scan_mask)) {
		ret = regmap_bulk_read(data->regmap, BM1390_REG_TEMP_HI, &temp,
				       sizeof(temp));
		if (ret)
			return ret;
	}

	for (i = 0; i < smp_lvl; i++) {
		buffer[i].temp = temp;
		iio_push_to_buffers(idev, &buffer[i]);
	}

	return smp_lvl;
}

static int bm1390_fifo_flush(struct iio_dev *idev, unsigned int samples)
{
	struct bm1390_data *data = iio_priv(idev);
	s64 timestamp;
	int ret;

	/*
	 * If fifo_flush is being called from IRQ handler we know the stored
	 * timestamp is fairly accurate for the last stored sample. If we are
	 * called as a result of a read operation from userspace and hence
	 * before the watermark interrupt was triggered, take a timestamp
	 * now. We can fall anywhere in between two samples so the error in this
	 * case is at most one sample period.
	 * We need to have the IRQ disabled or we risk of messing-up
	 * the timestamps. If we are ran from IRQ, then the
	 * IRQF_ONESHOT has us covered - but if we are ran by the
	 * user-space read we need to disable the IRQ to be on a safe
	 * side. We do this usng synchronous disable so that if the
	 * IRQ thread is being ran on other CPU we wait for it to be
	 * finished.
	 */

	timestamp = iio_get_time_ns(idev);
	mutex_lock(&data->mutex);
	ret = __bm1390_fifo_flush(idev, samples, timestamp);
	mutex_unlock(&data->mutex);

	return ret;
}

static int bm1390_set_watermark(struct iio_dev *idev, unsigned int val)
{
	struct bm1390_data *data = iio_priv(idev);

	if (val < BM1390_WMI_MIN || val > BM1390_WMI_MAX)
		return -EINVAL;

	mutex_lock(&data->mutex);
	data->watermark = val;
	mutex_unlock(&data->mutex);

	return 0;
}

static const struct iio_info bm1390_noirq_info = {
	.read_raw = &bm1390_read_raw,
};

static const struct iio_info bm1390_info = {
	.read_raw = &bm1390_read_raw,
	.hwfifo_set_watermark = bm1390_set_watermark,
	.hwfifo_flush_to_buffer = bm1390_fifo_flush,
};

static int bm1390_chip_init(struct bm1390_data *data)
{
	int ret;

	ret = regmap_write_bits(data->regmap, BM1390_REG_POWER,
				BM1390_MASK_POWER, BM1390_POWER_ON);
	if (ret)
		return ret;

	msleep(1);

	ret = regmap_write_bits(data->regmap, BM1390_REG_RESET,
				BM1390_MASK_RESET, BM1390_RESET);
	if (ret)
		return ret;

	msleep(1);

	ret = regmap_write_bits(data->regmap, BM1390_REG_RESET,
				BM1390_MASK_RESET, BM1390_RESET_RELEASE);
	if (ret)
		return ret;

	msleep(1);

	ret = regmap_reinit_cache(data->regmap, &bm1390_regmap);
	if (ret) {
		dev_err(data->dev, "Failed to reinit reg cache\n");
		return ret;
	}

	/*
	 * Default to use IIR filter in "middle" mode. Also the AVE_NUM must
	 * be fixed when IIR is in use.
	 */
	ret = regmap_update_bits(data->regmap, BM1390_REG_MODE_CTRL,
				 BM1390_MASK_AVE_NUM, BM1390_IIR_AVE_NUM);
	if (ret)
		return ret;

	return regmap_update_bits(data->regmap, BM1390_REG_FIFO_CTRL,
				  BM1390_MASK_IIR_MODE, BM1390_IIR_MODE_MID);
}

static int bm1390_fifo_set_wmi(struct bm1390_data *data)
{
	u8 regval;

	regval = FIELD_PREP(BM1390_MASK_FIFO_LEN,
			    data->watermark - BM1390_WMI_MIN);

	return regmap_update_bits(data->regmap, BM1390_REG_FIFO_CTRL,
				  BM1390_MASK_FIFO_LEN, regval);
}

static int bm1390_fifo_enable(struct iio_dev *idev)
{
	struct bm1390_data *data = iio_priv(idev);
	int ret;

	/* We can't do buffered stuff without IRQ as we never get WMI */
	if (data->irq <= 0)
		return -EINVAL;

	guard(mutex)(&data->mutex);

	if (data->trigger_enabled)
		return -EBUSY;

	/* Update watermark to HW */
	ret = bm1390_fifo_set_wmi(data);
	if (ret)
		return ret;

	/* Enable WMI_IRQ */
	ret = regmap_set_bits(data->regmap, BM1390_REG_MODE_CTRL,
			      BM1390_MASK_WMI_EN);
	if (ret)
		return ret;

	/* Enable FIFO */
	ret = regmap_set_bits(data->regmap, BM1390_REG_FIFO_CTRL,
			      BM1390_MASK_FIFO_EN);
	if (ret)
		return ret;

	data->state = BM1390_STATE_FIFO;

	data->old_timestamp = iio_get_time_ns(idev);

	return bm1390_meas_set(data, BM1390_MEAS_MODE_CONTINUOUS);
}

static int bm1390_fifo_disable(struct iio_dev *idev)
{
	struct bm1390_data *data = iio_priv(idev);
	int ret;

	msleep(1);

	guard(mutex)(&data->mutex);
	ret = bm1390_meas_set(data, BM1390_MEAS_MODE_STOP);
	if (ret)
		return ret;

	/* Disable FIFO */
	ret = regmap_clear_bits(data->regmap, BM1390_REG_FIFO_CTRL,
				BM1390_MASK_FIFO_EN);
	if (ret)
		return ret;

	data->state = BM1390_STATE_SAMPLE;

	/* Disable WMI_IRQ */
	return regmap_clear_bits(data->regmap, BM1390_REG_MODE_CTRL,
				 BM1390_MASK_WMI_EN);
}

static int bm1390_buffer_postenable(struct iio_dev *idev)
{
	/*
	 * If we use data-ready trigger, then the IRQ masks should be handled by
	 * trigger enable and the hardware buffer is not used but we just update
	 * results to the IIO FIFO when data-ready triggers.
	 */
	if (iio_device_get_current_mode(idev) == INDIO_BUFFER_TRIGGERED)
		return 0;

	return bm1390_fifo_enable(idev);
}

static int bm1390_buffer_predisable(struct iio_dev *idev)
{
	if (iio_device_get_current_mode(idev) == INDIO_BUFFER_TRIGGERED)
		return 0;

	return bm1390_fifo_disable(idev);
}

static const struct iio_buffer_setup_ops bm1390_buffer_ops = {
	.postenable = bm1390_buffer_postenable,
	.predisable = bm1390_buffer_predisable,
};

static irqreturn_t bm1390_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *idev = pf->indio_dev;
	struct bm1390_data *data = iio_priv(idev);
	int ret, status;

	/* DRDY is acked by reading status reg */
	ret = regmap_read(data->regmap, BM1390_REG_STATUS, &status);
	if (ret || !status)
		return IRQ_NONE;

	dev_dbg(data->dev, "DRDY trig status 0x%x\n", status);

	if (test_bit(BM1390_CHAN_PRESSURE, idev->active_scan_mask)) {
		ret = bm1390_pressure_read(data, &data->buf.pressure);
		if (ret) {
			dev_warn(data->dev, "sample read failed %d\n", ret);
			return IRQ_NONE;
		}
	}

	if (test_bit(BM1390_CHAN_TEMP, idev->active_scan_mask)) {
		ret = regmap_bulk_read(data->regmap, BM1390_REG_TEMP_HI,
				       &data->buf.temp, sizeof(data->buf.temp));
		if (ret) {
			dev_warn(data->dev, "temp read failed %d\n", ret);
			return IRQ_HANDLED;
		}
	}

	iio_push_to_buffers_with_ts(idev, &data->buf, sizeof(data->buf),
				    data->timestamp);
	iio_trigger_notify_done(idev->trig);

	return IRQ_HANDLED;
}

/* Get timestamps and wake the thread if we need to read data */
static irqreturn_t bm1390_irq_handler(int irq, void *private)
{
	struct iio_dev *idev = private;
	struct bm1390_data *data = iio_priv(idev);

	data->timestamp = iio_get_time_ns(idev);

	if (data->state == BM1390_STATE_FIFO || data->trigger_enabled)
		return IRQ_WAKE_THREAD;

	return IRQ_NONE;
}

static irqreturn_t bm1390_irq_thread_handler(int irq, void *private)
{
	struct iio_dev *idev = private;
	struct bm1390_data *data = iio_priv(idev);

	guard(mutex)(&data->mutex);

	if (data->trigger_enabled) {
		iio_trigger_poll_nested(data->trig);
		return IRQ_HANDLED;
	}

	if (data->state == BM1390_STATE_FIFO) {
		int ok;

		ok = __bm1390_fifo_flush(idev, BM1390_FIFO_LENGTH,
					 data->timestamp);
		if (ok > 0)
			return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static int bm1390_set_drdy_irq(struct bm1390_data *data, bool en)
{
	if (en)
		return regmap_set_bits(data->regmap, BM1390_REG_MODE_CTRL,
				       BM1390_MASK_DRDY_EN);
	return regmap_clear_bits(data->regmap, BM1390_REG_MODE_CTRL,
				 BM1390_MASK_DRDY_EN);
}

static int bm1390_trigger_set_state(struct iio_trigger *trig,
				    bool state)
{
	struct bm1390_data *data = iio_trigger_get_drvdata(trig);
	int ret;

	guard(mutex)(&data->mutex);

	if (data->trigger_enabled == state)
		return 0;

	if (data->state == BM1390_STATE_FIFO) {
		dev_warn(data->dev, "Can't set trigger when FIFO enabled\n");
		return -EBUSY;
	}

	data->trigger_enabled = state;

	if (state) {
		ret = bm1390_meas_set(data, BM1390_MEAS_MODE_CONTINUOUS);
		if (ret)
			return ret;
	} else {
		int dummy;

		ret = bm1390_meas_set(data, BM1390_MEAS_MODE_STOP);
		if (ret)
			return ret;

		/*
		 * We need to read the status register in order to ACK the
		 * data-ready which may have been generated just before we
		 * disabled the measurement.
		 */
		ret = regmap_read(data->regmap, BM1390_REG_STATUS, &dummy);
		if (ret)
			dev_warn(data->dev, "status read failed\n");
	}

	return bm1390_set_drdy_irq(data, state);
}

static const struct iio_trigger_ops bm1390_trigger_ops = {
	.set_trigger_state = bm1390_trigger_set_state,
};

static int bm1390_setup_buffer(struct bm1390_data *data, struct iio_dev *idev)
{
	int ret;

	ret = devm_iio_triggered_buffer_setup(data->dev, idev,
					      &iio_pollfunc_store_time,
					      &bm1390_trigger_handler,
					      &bm1390_buffer_ops);

	if (ret)
		return dev_err_probe(data->dev, ret,
				     "iio_triggered_buffer_setup FAIL\n");

	idev->available_scan_masks = bm1390_scan_masks;

	return 0;
}

static int bm1390_setup_trigger(struct bm1390_data *data, struct iio_dev *idev,
				int irq)
{
	struct iio_trigger *itrig;
	char *name;
	int ret;

	itrig = devm_iio_trigger_alloc(data->dev, "%sdata-rdy-dev%d", idev->name,
					    iio_device_id(idev));
	if (!itrig)
		return -ENOMEM;

	data->trig = itrig;

	itrig->ops = &bm1390_trigger_ops;
	iio_trigger_set_drvdata(itrig, data);

	name = devm_kasprintf(data->dev, GFP_KERNEL, "%s-bm1390",
			      dev_name(data->dev));
	if (name == NULL)
		return -ENOMEM;

	ret = devm_request_threaded_irq(data->dev, irq, bm1390_irq_handler,
					&bm1390_irq_thread_handler,
					IRQF_ONESHOT, name, idev);
	if (ret)
		return dev_err_probe(data->dev, ret, "Could not request IRQ\n");


	ret = devm_iio_trigger_register(data->dev, itrig);
	if (ret)
		return dev_err_probe(data->dev, ret,
				     "Trigger registration failed\n");

	return 0;
}

static int bm1390_probe(struct i2c_client *i2c)
{
	struct bm1390_data *data;
	struct regmap *regmap;
	struct iio_dev *idev;
	struct device *dev;
	unsigned int part_id;
	int ret;

	dev = &i2c->dev;

	regmap = devm_regmap_init_i2c(i2c, &bm1390_regmap);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap),
				     "Failed to initialize Regmap\n");

	ret = devm_regulator_get_enable(dev, "vdd");
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get regulator\n");

	ret = regmap_read(regmap, BM1390_REG_PART_ID, &part_id);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to access sensor\n");

	if (part_id != BM1390_ID)
		dev_warn(dev, "unknown device 0x%x\n", part_id);

	idev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!idev)
		return -ENOMEM;

	data = iio_priv(idev);
	data->regmap = regmap;
	data->dev = dev;
	data->irq = i2c->irq;
	/*
	 * For now we just allow BM1390_WMI_MIN to BM1390_WMI_MAX and
	 * discard every other configuration when triggered mode is not used.
	 */
	data->watermark = BM1390_WMI_MAX;
	mutex_init(&data->mutex);

	idev->channels = bm1390_channels;
	idev->num_channels = ARRAY_SIZE(bm1390_channels);
	idev->name = "bm1390";
	idev->modes = INDIO_DIRECT_MODE;

	ret = bm1390_chip_init(data);
	if (ret)
		return dev_err_probe(dev, ret, "sensor init failed\n");

	ret = bm1390_setup_buffer(data, idev);
	if (ret)
		return ret;

	/* No trigger if we don't have IRQ for data-ready and WMI */
	if (i2c->irq > 0) {
		idev->info = &bm1390_info;
		idev->modes |= INDIO_BUFFER_SOFTWARE;
		ret = bm1390_setup_trigger(data, idev, i2c->irq);
		if (ret)
			return ret;
	} else {
		idev->info = &bm1390_noirq_info;
	}

	ret = devm_iio_device_register(dev, idev);
	if (ret < 0)
		return dev_err_probe(dev, ret,
				     "Unable to register iio device\n");

	return 0;
}

static const struct of_device_id bm1390_of_match[] = {
	{ .compatible = "rohm,bm1390glv-z" },
	{ }
};
MODULE_DEVICE_TABLE(of, bm1390_of_match);

static const struct i2c_device_id bm1390_id[] = {
	{ "bm1390glv-z", },
	{ }
};
MODULE_DEVICE_TABLE(i2c, bm1390_id);

static struct i2c_driver bm1390_driver = {
	.driver = {
		.name = "bm1390",
		.of_match_table = bm1390_of_match,
		/*
		 * Probing explicitly requires a few millisecond of sleep.
		 * Enabling the VDD regulator may include ramp up rates.
		 */
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.probe = bm1390_probe,
	.id_table = bm1390_id,
};
module_i2c_driver(bm1390_driver);

MODULE_AUTHOR("Matti Vaittinen <mazziesaccount@gmail.com>");
MODULE_DESCRIPTION("Driver for ROHM BM1390 pressure sensor");
MODULE_LICENSE("GPL");
