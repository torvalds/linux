/*
 * STMicroelectronics st_lsm6dsx FIFO buffer library driver
 *
 * LSM6DS3/LSM6DS3H/LSM6DSL/LSM6DSM/ISM330DLC: The FIFO buffer can be
 * configured to store data from gyroscope and accelerometer. Samples are
 * queued without any tag according to a specific pattern based on
 * 'FIFO data sets' (6 bytes each):
 *  - 1st data set is reserved for gyroscope data
 *  - 2nd data set is reserved for accelerometer data
 * The FIFO pattern changes depending on the ODRs and decimation factors
 * assigned to the FIFO data sets. The first sequence of data stored in FIFO
 * buffer contains the data of all the enabled FIFO data sets
 * (e.g. Gx, Gy, Gz, Ax, Ay, Az), then data are repeated depending on the
 * value of the decimation factor and ODR set for each FIFO data set.
 *
 * LSM6DSO/LSM6DSOX/ASM330LHH/LSM6DSR: The FIFO buffer can be configured to
 * store data from gyroscope and accelerometer. Each sample is queued with
 * a tag (1B) indicating data source (gyroscope, accelerometer, hw timer).
 *
 * FIFO supported modes:
 *  - BYPASS: FIFO disabled
 *  - CONTINUOUS: FIFO enabled. When the buffer is full, the FIFO index
 *    restarts from the beginning and the oldest sample is overwritten
 *
 * Copyright 2016 STMicroelectronics Inc.
 *
 * Lorenzo Bianconi <lorenzo.bianconi@st.com>
 * Denis Ciocca <denis.ciocca@st.com>
 *
 * Licensed under the GPL-2.
 */
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/iio/kfifo_buf.h>
#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/regmap.h>
#include <linux/bitfield.h>

#include <linux/platform_data/st_sensors_pdata.h>

#include "st_lsm6dsx.h"

#define ST_LSM6DSX_REG_HLACTIVE_ADDR		0x12
#define ST_LSM6DSX_REG_HLACTIVE_MASK		BIT(5)
#define ST_LSM6DSX_REG_PP_OD_ADDR		0x12
#define ST_LSM6DSX_REG_PP_OD_MASK		BIT(4)
#define ST_LSM6DSX_REG_FIFO_MODE_ADDR		0x0a
#define ST_LSM6DSX_FIFO_MODE_MASK		GENMASK(2, 0)
#define ST_LSM6DSX_FIFO_ODR_MASK		GENMASK(6, 3)
#define ST_LSM6DSX_FIFO_EMPTY_MASK		BIT(12)
#define ST_LSM6DSX_REG_FIFO_OUTL_ADDR		0x3e
#define ST_LSM6DSX_REG_FIFO_OUT_TAG_ADDR	0x78
#define ST_LSM6DSX_REG_TS_RESET_ADDR		0x42

#define ST_LSM6DSX_MAX_FIFO_ODR_VAL		0x08

#define ST_LSM6DSX_TS_SENSITIVITY		25000UL /* 25us */
#define ST_LSM6DSX_TS_RESET_VAL			0xaa

struct st_lsm6dsx_decimator_entry {
	u8 decimator;
	u8 val;
};

enum st_lsm6dsx_fifo_tag {
	ST_LSM6DSX_GYRO_TAG = 0x01,
	ST_LSM6DSX_ACC_TAG = 0x02,
	ST_LSM6DSX_TS_TAG = 0x04,
	ST_LSM6DSX_EXT0_TAG = 0x0f,
	ST_LSM6DSX_EXT1_TAG = 0x10,
	ST_LSM6DSX_EXT2_TAG = 0x11,
};

static const
struct st_lsm6dsx_decimator_entry st_lsm6dsx_decimator_table[] = {
	{  0, 0x0 },
	{  1, 0x1 },
	{  2, 0x2 },
	{  3, 0x3 },
	{  4, 0x4 },
	{  8, 0x5 },
	{ 16, 0x6 },
	{ 32, 0x7 },
};

static int st_lsm6dsx_get_decimator_val(u8 val)
{
	const int max_size = ARRAY_SIZE(st_lsm6dsx_decimator_table);
	int i;

	for (i = 0; i < max_size; i++)
		if (st_lsm6dsx_decimator_table[i].decimator == val)
			break;

	return i == max_size ? 0 : st_lsm6dsx_decimator_table[i].val;
}

static void st_lsm6dsx_get_max_min_odr(struct st_lsm6dsx_hw *hw,
				       u16 *max_odr, u16 *min_odr)
{
	struct st_lsm6dsx_sensor *sensor;
	int i;

	*max_odr = 0, *min_odr = ~0;
	for (i = 0; i < ST_LSM6DSX_ID_MAX; i++) {
		if (!hw->iio_devs[i])
			continue;

		sensor = iio_priv(hw->iio_devs[i]);

		if (!(hw->enable_mask & BIT(sensor->id)))
			continue;

		*max_odr = max_t(u16, *max_odr, sensor->odr);
		*min_odr = min_t(u16, *min_odr, sensor->odr);
	}
}

static int st_lsm6dsx_update_decimators(struct st_lsm6dsx_hw *hw)
{
	u16 max_odr, min_odr, sip = 0, ts_sip = 0;
	const struct st_lsm6dsx_reg *ts_dec_reg;
	struct st_lsm6dsx_sensor *sensor;
	int err = 0, i;
	u8 data;

	st_lsm6dsx_get_max_min_odr(hw, &max_odr, &min_odr);

	for (i = 0; i < ST_LSM6DSX_ID_MAX; i++) {
		const struct st_lsm6dsx_reg *dec_reg;

		if (!hw->iio_devs[i])
			continue;

		sensor = iio_priv(hw->iio_devs[i]);
		/* update fifo decimators and sample in pattern */
		if (hw->enable_mask & BIT(sensor->id)) {
			sensor->sip = sensor->odr / min_odr;
			sensor->decimator = max_odr / sensor->odr;
			data = st_lsm6dsx_get_decimator_val(sensor->decimator);
		} else {
			sensor->sip = 0;
			sensor->decimator = 0;
			data = 0;
		}
		ts_sip = max_t(u16, ts_sip, sensor->sip);

		dec_reg = &hw->settings->decimator[sensor->id];
		if (dec_reg->addr) {
			int val = ST_LSM6DSX_SHIFT_VAL(data, dec_reg->mask);

			err = st_lsm6dsx_update_bits_locked(hw, dec_reg->addr,
							    dec_reg->mask,
							    val);
			if (err < 0)
				return err;
		}
		sip += sensor->sip;
	}
	hw->sip = sip + ts_sip;
	hw->ts_sip = ts_sip;

	/*
	 * update hw ts decimator if necessary. Decimator for hw timestamp
	 * is always 1 or 0 in order to have a ts sample for each data
	 * sample in FIFO
	 */
	ts_dec_reg = &hw->settings->ts_settings.decimator;
	if (ts_dec_reg->addr) {
		int val, ts_dec = !!hw->ts_sip;

		val = ST_LSM6DSX_SHIFT_VAL(ts_dec, ts_dec_reg->mask);
		err = st_lsm6dsx_update_bits_locked(hw, ts_dec_reg->addr,
						    ts_dec_reg->mask, val);
	}
	return err;
}

int st_lsm6dsx_set_fifo_mode(struct st_lsm6dsx_hw *hw,
			     enum st_lsm6dsx_fifo_mode fifo_mode)
{
	unsigned int data;
	int err;

	data = FIELD_PREP(ST_LSM6DSX_FIFO_MODE_MASK, fifo_mode);
	err = st_lsm6dsx_update_bits_locked(hw, ST_LSM6DSX_REG_FIFO_MODE_ADDR,
					    ST_LSM6DSX_FIFO_MODE_MASK, data);
	if (err < 0)
		return err;

	hw->fifo_mode = fifo_mode;

	return 0;
}

static int st_lsm6dsx_set_fifo_odr(struct st_lsm6dsx_sensor *sensor,
				   bool enable)
{
	struct st_lsm6dsx_hw *hw = sensor->hw;
	const struct st_lsm6dsx_reg *batch_reg;
	u8 data;

	batch_reg = &hw->settings->batch[sensor->id];
	if (batch_reg->addr) {
		int val;

		if (enable) {
			int err;

			err = st_lsm6dsx_check_odr(sensor, sensor->odr,
						   &data);
			if (err < 0)
				return err;
		} else {
			data = 0;
		}
		val = ST_LSM6DSX_SHIFT_VAL(data, batch_reg->mask);
		return st_lsm6dsx_update_bits_locked(hw, batch_reg->addr,
						     batch_reg->mask, val);
	} else {
		data = hw->enable_mask ? ST_LSM6DSX_MAX_FIFO_ODR_VAL : 0;
		return st_lsm6dsx_update_bits_locked(hw,
					ST_LSM6DSX_REG_FIFO_MODE_ADDR,
					ST_LSM6DSX_FIFO_ODR_MASK,
					FIELD_PREP(ST_LSM6DSX_FIFO_ODR_MASK,
						   data));
	}
}

int st_lsm6dsx_update_watermark(struct st_lsm6dsx_sensor *sensor, u16 watermark)
{
	u16 fifo_watermark = ~0, cur_watermark, fifo_th_mask;
	struct st_lsm6dsx_hw *hw = sensor->hw;
	struct st_lsm6dsx_sensor *cur_sensor;
	int i, err, data;
	__le16 wdata;

	if (!hw->sip)
		return 0;

	for (i = 0; i < ST_LSM6DSX_ID_MAX; i++) {
		if (!hw->iio_devs[i])
			continue;

		cur_sensor = iio_priv(hw->iio_devs[i]);

		if (!(hw->enable_mask & BIT(cur_sensor->id)))
			continue;

		cur_watermark = (cur_sensor == sensor) ? watermark
						       : cur_sensor->watermark;

		fifo_watermark = min_t(u16, fifo_watermark, cur_watermark);
	}

	fifo_watermark = max_t(u16, fifo_watermark, hw->sip);
	fifo_watermark = (fifo_watermark / hw->sip) * hw->sip;
	fifo_watermark = fifo_watermark * hw->settings->fifo_ops.th_wl;

	mutex_lock(&hw->page_lock);
	err = regmap_read(hw->regmap, hw->settings->fifo_ops.fifo_th.addr + 1,
			  &data);
	if (err < 0)
		goto out;

	fifo_th_mask = hw->settings->fifo_ops.fifo_th.mask;
	fifo_watermark = ((data << 8) & ~fifo_th_mask) |
			 (fifo_watermark & fifo_th_mask);

	wdata = cpu_to_le16(fifo_watermark);
	err = regmap_bulk_write(hw->regmap,
				hw->settings->fifo_ops.fifo_th.addr,
				&wdata, sizeof(wdata));
out:
	mutex_unlock(&hw->page_lock);
	return err;
}

static int st_lsm6dsx_reset_hw_ts(struct st_lsm6dsx_hw *hw)
{
	struct st_lsm6dsx_sensor *sensor;
	int i, err;

	/* reset hw ts counter */
	err = st_lsm6dsx_write_locked(hw, ST_LSM6DSX_REG_TS_RESET_ADDR,
				      ST_LSM6DSX_TS_RESET_VAL);
	if (err < 0)
		return err;

	for (i = 0; i < ST_LSM6DSX_ID_MAX; i++) {
		if (!hw->iio_devs[i])
			continue;

		sensor = iio_priv(hw->iio_devs[i]);
		/*
		 * store enable buffer timestamp as reference for
		 * hw timestamp
		 */
		sensor->ts_ref = iio_get_time_ns(hw->iio_devs[i]);
	}
	return 0;
}

/*
 * Set max bulk read to ST_LSM6DSX_MAX_WORD_LEN/ST_LSM6DSX_MAX_TAGGED_WORD_LEN
 * in order to avoid a kmalloc for each bus access
 */
static inline int st_lsm6dsx_read_block(struct st_lsm6dsx_hw *hw, u8 addr,
					u8 *data, unsigned int data_len,
					unsigned int max_word_len)
{
	unsigned int word_len, read_len = 0;
	int err;

	while (read_len < data_len) {
		word_len = min_t(unsigned int, data_len - read_len,
				 max_word_len);
		err = st_lsm6dsx_read_locked(hw, addr, data + read_len,
					     word_len);
		if (err < 0)
			return err;
		read_len += word_len;
	}
	return 0;
}

#define ST_LSM6DSX_IIO_BUFF_SIZE	(ALIGN(ST_LSM6DSX_SAMPLE_SIZE, \
					       sizeof(s64)) + sizeof(s64))
/**
 * st_lsm6dsx_read_fifo() - hw FIFO read routine
 * @hw: Pointer to instance of struct st_lsm6dsx_hw.
 *
 * Read samples from the hw FIFO and push them to IIO buffers.
 *
 * Return: Number of bytes read from the FIFO
 */
int st_lsm6dsx_read_fifo(struct st_lsm6dsx_hw *hw)
{
	u16 fifo_len, pattern_len = hw->sip * ST_LSM6DSX_SAMPLE_SIZE;
	u16 fifo_diff_mask = hw->settings->fifo_ops.fifo_diff.mask;
	int err, acc_sip, gyro_sip, ts_sip, read_len, offset;
	struct st_lsm6dsx_sensor *acc_sensor, *gyro_sensor;
	u8 gyro_buff[ST_LSM6DSX_IIO_BUFF_SIZE];
	u8 acc_buff[ST_LSM6DSX_IIO_BUFF_SIZE];
	bool reset_ts = false;
	__le16 fifo_status;
	s64 ts = 0;

	err = st_lsm6dsx_read_locked(hw,
				     hw->settings->fifo_ops.fifo_diff.addr,
				     &fifo_status, sizeof(fifo_status));
	if (err < 0) {
		dev_err(hw->dev, "failed to read fifo status (err=%d)\n",
			err);
		return err;
	}

	if (fifo_status & cpu_to_le16(ST_LSM6DSX_FIFO_EMPTY_MASK))
		return 0;

	fifo_len = (le16_to_cpu(fifo_status) & fifo_diff_mask) *
		   ST_LSM6DSX_CHAN_SIZE;
	fifo_len = (fifo_len / pattern_len) * pattern_len;

	acc_sensor = iio_priv(hw->iio_devs[ST_LSM6DSX_ID_ACC]);
	gyro_sensor = iio_priv(hw->iio_devs[ST_LSM6DSX_ID_GYRO]);

	for (read_len = 0; read_len < fifo_len; read_len += pattern_len) {
		err = st_lsm6dsx_read_block(hw, ST_LSM6DSX_REG_FIFO_OUTL_ADDR,
					    hw->buff, pattern_len,
					    ST_LSM6DSX_MAX_WORD_LEN);
		if (err < 0) {
			dev_err(hw->dev,
				"failed to read pattern from fifo (err=%d)\n",
				err);
			return err;
		}

		/*
		 * Data are written to the FIFO with a specific pattern
		 * depending on the configured ODRs. The first sequence of data
		 * stored in FIFO contains the data of all enabled sensors
		 * (e.g. Gx, Gy, Gz, Ax, Ay, Az, Ts), then data are repeated
		 * depending on the value of the decimation factor set for each
		 * sensor.
		 *
		 * Supposing the FIFO is storing data from gyroscope and
		 * accelerometer at different ODRs:
		 *   - gyroscope ODR = 208Hz, accelerometer ODR = 104Hz
		 * Since the gyroscope ODR is twice the accelerometer one, the
		 * following pattern is repeated every 9 samples:
		 *   - Gx, Gy, Gz, Ax, Ay, Az, Ts, Gx, Gy, Gz, Ts, Gx, ..
		 */
		gyro_sip = gyro_sensor->sip;
		acc_sip = acc_sensor->sip;
		ts_sip = hw->ts_sip;
		offset = 0;

		while (acc_sip > 0 || gyro_sip > 0) {
			if (gyro_sip > 0) {
				memcpy(gyro_buff, &hw->buff[offset],
				       ST_LSM6DSX_SAMPLE_SIZE);
				offset += ST_LSM6DSX_SAMPLE_SIZE;
			}
			if (acc_sip > 0) {
				memcpy(acc_buff, &hw->buff[offset],
				       ST_LSM6DSX_SAMPLE_SIZE);
				offset += ST_LSM6DSX_SAMPLE_SIZE;
			}

			if (ts_sip-- > 0) {
				u8 data[ST_LSM6DSX_SAMPLE_SIZE];

				memcpy(data, &hw->buff[offset], sizeof(data));
				/*
				 * hw timestamp is 3B long and it is stored
				 * in FIFO using 6B as 4th FIFO data set
				 * according to this schema:
				 * B0 = ts[15:8], B1 = ts[23:16], B3 = ts[7:0]
				 */
				ts = data[1] << 16 | data[0] << 8 | data[3];
				/*
				 * check if hw timestamp engine is going to
				 * reset (the sensor generates an interrupt
				 * to signal the hw timestamp will reset in
				 * 1.638s)
				 */
				if (!reset_ts && ts >= 0xff0000)
					reset_ts = true;
				ts *= ST_LSM6DSX_TS_SENSITIVITY;

				offset += ST_LSM6DSX_SAMPLE_SIZE;
			}

			if (gyro_sip-- > 0)
				iio_push_to_buffers_with_timestamp(
					hw->iio_devs[ST_LSM6DSX_ID_GYRO],
					gyro_buff, gyro_sensor->ts_ref + ts);
			if (acc_sip-- > 0)
				iio_push_to_buffers_with_timestamp(
					hw->iio_devs[ST_LSM6DSX_ID_ACC],
					acc_buff, acc_sensor->ts_ref + ts);
		}
	}

	if (unlikely(reset_ts)) {
		err = st_lsm6dsx_reset_hw_ts(hw);
		if (err < 0) {
			dev_err(hw->dev, "failed to reset hw ts (err=%d)\n",
				err);
			return err;
		}
	}
	return read_len;
}

static int
st_lsm6dsx_push_tagged_data(struct st_lsm6dsx_hw *hw, u8 tag,
			    u8 *data, s64 ts)
{
	struct st_lsm6dsx_sensor *sensor;
	struct iio_dev *iio_dev;

	/*
	 * EXT_TAG are managed in FIFO fashion so ST_LSM6DSX_EXT0_TAG
	 * corresponds to the first enabled channel, ST_LSM6DSX_EXT1_TAG
	 * to the second one and ST_LSM6DSX_EXT2_TAG to the last enabled
	 * channel
	 */
	switch (tag) {
	case ST_LSM6DSX_GYRO_TAG:
		iio_dev = hw->iio_devs[ST_LSM6DSX_ID_GYRO];
		break;
	case ST_LSM6DSX_ACC_TAG:
		iio_dev = hw->iio_devs[ST_LSM6DSX_ID_ACC];
		break;
	case ST_LSM6DSX_EXT0_TAG:
		if (hw->enable_mask & BIT(ST_LSM6DSX_ID_EXT0))
			iio_dev = hw->iio_devs[ST_LSM6DSX_ID_EXT0];
		else if (hw->enable_mask & BIT(ST_LSM6DSX_ID_EXT1))
			iio_dev = hw->iio_devs[ST_LSM6DSX_ID_EXT1];
		else
			iio_dev = hw->iio_devs[ST_LSM6DSX_ID_EXT2];
		break;
	case ST_LSM6DSX_EXT1_TAG:
		if ((hw->enable_mask & BIT(ST_LSM6DSX_ID_EXT0)) &&
		    (hw->enable_mask & BIT(ST_LSM6DSX_ID_EXT1)))
			iio_dev = hw->iio_devs[ST_LSM6DSX_ID_EXT1];
		else
			iio_dev = hw->iio_devs[ST_LSM6DSX_ID_EXT2];
		break;
	case ST_LSM6DSX_EXT2_TAG:
		iio_dev = hw->iio_devs[ST_LSM6DSX_ID_EXT2];
		break;
	default:
		return -EINVAL;
	}

	sensor = iio_priv(iio_dev);
	iio_push_to_buffers_with_timestamp(iio_dev, data,
					   ts + sensor->ts_ref);

	return 0;
}

/**
 * st_lsm6dsx_read_tagged_fifo() - tagged hw FIFO read routine
 * @hw: Pointer to instance of struct st_lsm6dsx_hw.
 *
 * Read samples from the hw FIFO and push them to IIO buffers.
 *
 * Return: Number of bytes read from the FIFO
 */
int st_lsm6dsx_read_tagged_fifo(struct st_lsm6dsx_hw *hw)
{
	u16 pattern_len = hw->sip * ST_LSM6DSX_TAGGED_SAMPLE_SIZE;
	u16 fifo_len, fifo_diff_mask;
	u8 iio_buff[ST_LSM6DSX_IIO_BUFF_SIZE], tag;
	bool reset_ts = false;
	int i, err, read_len;
	__le16 fifo_status;
	s64 ts = 0;

	err = st_lsm6dsx_read_locked(hw,
				     hw->settings->fifo_ops.fifo_diff.addr,
				     &fifo_status, sizeof(fifo_status));
	if (err < 0) {
		dev_err(hw->dev, "failed to read fifo status (err=%d)\n",
			err);
		return err;
	}

	fifo_diff_mask = hw->settings->fifo_ops.fifo_diff.mask;
	fifo_len = (le16_to_cpu(fifo_status) & fifo_diff_mask) *
		   ST_LSM6DSX_TAGGED_SAMPLE_SIZE;
	if (!fifo_len)
		return 0;

	for (read_len = 0; read_len < fifo_len; read_len += pattern_len) {
		err = st_lsm6dsx_read_block(hw,
					    ST_LSM6DSX_REG_FIFO_OUT_TAG_ADDR,
					    hw->buff, pattern_len,
					    ST_LSM6DSX_MAX_TAGGED_WORD_LEN);
		if (err < 0) {
			dev_err(hw->dev,
				"failed to read pattern from fifo (err=%d)\n",
				err);
			return err;
		}

		for (i = 0; i < pattern_len;
		     i += ST_LSM6DSX_TAGGED_SAMPLE_SIZE) {
			memcpy(iio_buff, &hw->buff[i + ST_LSM6DSX_TAG_SIZE],
			       ST_LSM6DSX_SAMPLE_SIZE);

			tag = hw->buff[i] >> 3;
			if (tag == ST_LSM6DSX_TS_TAG) {
				/*
				 * hw timestamp is 4B long and it is stored
				 * in FIFO according to this schema:
				 * B0 = ts[7:0], B1 = ts[15:8], B2 = ts[23:16],
				 * B3 = ts[31:24]
				 */
				ts = le32_to_cpu(*((__le32 *)iio_buff));
				/*
				 * check if hw timestamp engine is going to
				 * reset (the sensor generates an interrupt
				 * to signal the hw timestamp will reset in
				 * 1.638s)
				 */
				if (!reset_ts && ts >= 0xffff0000)
					reset_ts = true;
				ts *= ST_LSM6DSX_TS_SENSITIVITY;
			} else {
				st_lsm6dsx_push_tagged_data(hw, tag, iio_buff,
							    ts);
			}
		}
	}

	if (unlikely(reset_ts)) {
		err = st_lsm6dsx_reset_hw_ts(hw);
		if (err < 0)
			return err;
	}
	return read_len;
}

int st_lsm6dsx_flush_fifo(struct st_lsm6dsx_hw *hw)
{
	int err;

	mutex_lock(&hw->fifo_lock);

	hw->settings->fifo_ops.read_fifo(hw);
	err = st_lsm6dsx_set_fifo_mode(hw, ST_LSM6DSX_FIFO_BYPASS);

	mutex_unlock(&hw->fifo_lock);

	return err;
}

static int st_lsm6dsx_update_fifo(struct iio_dev *iio_dev, bool enable)
{
	struct st_lsm6dsx_sensor *sensor = iio_priv(iio_dev);
	struct st_lsm6dsx_hw *hw = sensor->hw;
	int err;

	mutex_lock(&hw->conf_lock);

	if (hw->fifo_mode != ST_LSM6DSX_FIFO_BYPASS) {
		err = st_lsm6dsx_flush_fifo(hw);
		if (err < 0)
			goto out;
	}

	if (sensor->id == ST_LSM6DSX_ID_EXT0 ||
	    sensor->id == ST_LSM6DSX_ID_EXT1 ||
	    sensor->id == ST_LSM6DSX_ID_EXT2) {
		err = st_lsm6dsx_shub_set_enable(sensor, enable);
		if (err < 0)
			goto out;
	} else {
		err = st_lsm6dsx_sensor_set_enable(sensor, enable);
		if (err < 0)
			goto out;

		err = st_lsm6dsx_set_fifo_odr(sensor, enable);
		if (err < 0)
			goto out;
	}

	err = st_lsm6dsx_update_decimators(hw);
	if (err < 0)
		goto out;

	err = st_lsm6dsx_update_watermark(sensor, sensor->watermark);
	if (err < 0)
		goto out;

	if (hw->enable_mask) {
		/* reset hw ts counter */
		err = st_lsm6dsx_reset_hw_ts(hw);
		if (err < 0)
			goto out;

		err = st_lsm6dsx_set_fifo_mode(hw, ST_LSM6DSX_FIFO_CONT);
	}

out:
	mutex_unlock(&hw->conf_lock);

	return err;
}

static irqreturn_t st_lsm6dsx_handler_irq(int irq, void *private)
{
	struct st_lsm6dsx_hw *hw = private;

	return hw->sip > 0 ? IRQ_WAKE_THREAD : IRQ_NONE;
}

static irqreturn_t st_lsm6dsx_handler_thread(int irq, void *private)
{
	struct st_lsm6dsx_hw *hw = private;
	int count;

	mutex_lock(&hw->fifo_lock);
	count = hw->settings->fifo_ops.read_fifo(hw);
	mutex_unlock(&hw->fifo_lock);

	return !count ? IRQ_NONE : IRQ_HANDLED;
}

static int st_lsm6dsx_buffer_preenable(struct iio_dev *iio_dev)
{
	return st_lsm6dsx_update_fifo(iio_dev, true);
}

static int st_lsm6dsx_buffer_postdisable(struct iio_dev *iio_dev)
{
	return st_lsm6dsx_update_fifo(iio_dev, false);
}

static const struct iio_buffer_setup_ops st_lsm6dsx_buffer_ops = {
	.preenable = st_lsm6dsx_buffer_preenable,
	.postdisable = st_lsm6dsx_buffer_postdisable,
};

int st_lsm6dsx_fifo_setup(struct st_lsm6dsx_hw *hw)
{
	struct device_node *np = hw->dev->of_node;
	struct st_sensors_platform_data *pdata;
	struct iio_buffer *buffer;
	unsigned long irq_type;
	bool irq_active_low;
	int i, err;

	irq_type = irqd_get_trigger_type(irq_get_irq_data(hw->irq));

	switch (irq_type) {
	case IRQF_TRIGGER_HIGH:
	case IRQF_TRIGGER_RISING:
		irq_active_low = false;
		break;
	case IRQF_TRIGGER_LOW:
	case IRQF_TRIGGER_FALLING:
		irq_active_low = true;
		break;
	default:
		dev_info(hw->dev, "mode %lx unsupported\n", irq_type);
		return -EINVAL;
	}

	err = regmap_update_bits(hw->regmap, ST_LSM6DSX_REG_HLACTIVE_ADDR,
				 ST_LSM6DSX_REG_HLACTIVE_MASK,
				 FIELD_PREP(ST_LSM6DSX_REG_HLACTIVE_MASK,
					    irq_active_low));
	if (err < 0)
		return err;

	pdata = (struct st_sensors_platform_data *)hw->dev->platform_data;
	if ((np && of_property_read_bool(np, "drive-open-drain")) ||
	    (pdata && pdata->open_drain)) {
		err = regmap_update_bits(hw->regmap, ST_LSM6DSX_REG_PP_OD_ADDR,
					 ST_LSM6DSX_REG_PP_OD_MASK,
					 FIELD_PREP(ST_LSM6DSX_REG_PP_OD_MASK,
						    1));
		if (err < 0)
			return err;

		irq_type |= IRQF_SHARED;
	}

	err = devm_request_threaded_irq(hw->dev, hw->irq,
					st_lsm6dsx_handler_irq,
					st_lsm6dsx_handler_thread,
					irq_type | IRQF_ONESHOT,
					"lsm6dsx", hw);
	if (err) {
		dev_err(hw->dev, "failed to request trigger irq %d\n",
			hw->irq);
		return err;
	}

	for (i = 0; i < ST_LSM6DSX_ID_MAX; i++) {
		if (!hw->iio_devs[i])
			continue;

		buffer = devm_iio_kfifo_allocate(hw->dev);
		if (!buffer)
			return -ENOMEM;

		iio_device_attach_buffer(hw->iio_devs[i], buffer);
		hw->iio_devs[i]->modes |= INDIO_BUFFER_SOFTWARE;
		hw->iio_devs[i]->setup_ops = &st_lsm6dsx_buffer_ops;
	}

	return 0;
}
