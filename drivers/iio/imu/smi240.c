// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/*
 * Copyright (c) 2024 Robert Bosch GmbH.
 */
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>
#include <linux/unaligned.h>
#include <linux/units.h>

#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#define SMI240_CHIP_ID 0x0024

#define SMI240_SOFT_CONFIG_EOC_MASK BIT(0)
#define SMI240_SOFT_CONFIG_GYR_BW_MASK BIT(1)
#define SMI240_SOFT_CONFIG_ACC_BW_MASK BIT(2)
#define SMI240_SOFT_CONFIG_BITE_AUTO_MASK BIT(3)
#define SMI240_SOFT_CONFIG_BITE_REP_MASK GENMASK(6, 4)

#define SMI240_CHIP_ID_REG 0x00
#define SMI240_SOFT_CONFIG_REG 0x0A
#define SMI240_TEMP_CUR_REG 0x10
#define SMI240_ACCEL_X_CUR_REG 0x11
#define SMI240_GYRO_X_CUR_REG 0x14
#define SMI240_DATA_CAP_FIRST_REG 0x17
#define SMI240_CMD_REG 0x2F

#define SMI240_SOFT_RESET_CMD 0xB6

#define SMI240_BITE_SEQUENCE_DELAY_US 140000
#define SMI240_FILTER_FLUSH_DELAY_US 60000
#define SMI240_DIGITAL_STARTUP_DELAY_US 120000
#define SMI240_MECH_STARTUP_DELAY_US 100000

#define SMI240_BUS_ID 0x00
#define SMI240_CRC_INIT 0x05
#define SMI240_CRC_POLY 0x0B
#define SMI240_CRC_MASK GENMASK(2, 0)

#define SMI240_READ_SD_BIT_MASK BIT(31)
#define SMI240_READ_DATA_MASK GENMASK(19, 4)
#define SMI240_READ_CS_BIT_MASK BIT(3)

#define SMI240_WRITE_BUS_ID_MASK GENMASK(31, 30)
#define SMI240_WRITE_ADDR_MASK GENMASK(29, 22)
#define SMI240_WRITE_BIT_MASK BIT(21)
#define SMI240_WRITE_CAP_BIT_MASK BIT(20)
#define SMI240_WRITE_DATA_MASK GENMASK(18, 3)

/* T°C = (temp / 256) + 25 */
#define SMI240_TEMP_OFFSET 6400   /* 25 * 256 */
#define SMI240_TEMP_SCALE 3906250 /* (1 / 256) * 1e9 */

#define SMI240_ACCEL_SCALE 500  /* (1 / 2000) * 1e6 */
#define SMI240_GYRO_SCALE 10000 /* (1 /  100) * 1e6 */

#define SMI240_LOW_BANDWIDTH_HZ 50
#define SMI240_HIGH_BANDWIDTH_HZ 400

#define SMI240_BUILT_IN_SELF_TEST_COUNT 3

#define SMI240_DATA_CHANNEL(_type, _axis, _index) {			\
	.type = _type,							\
	.modified = 1,							\
	.channel2 = IIO_MOD_##_axis,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),			\
	.info_mask_shared_by_type =					\
		BIT(IIO_CHAN_INFO_SCALE) |				\
		BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY),	\
	.info_mask_shared_by_type_available =				\
		BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY),	\
	.scan_index = _index,						\
	.scan_type = {							\
		.sign = 's',						\
		.realbits = 16,						\
		.storagebits = 16,					\
		.endianness = IIO_CPU,					\
	},								\
}

#define SMI240_TEMP_CHANNEL(_index) {			\
	.type = IIO_TEMP,				\
	.modified = 1,					\
	.channel2 = IIO_MOD_TEMP_OBJECT,		\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |	\
		BIT(IIO_CHAN_INFO_OFFSET) |		\
		BIT(IIO_CHAN_INFO_SCALE),		\
	.scan_index = _index,				\
	.scan_type = {					\
		.sign = 's',				\
		.realbits = 16,				\
		.storagebits = 16,			\
		.endianness = IIO_CPU,			\
	},						\
}

enum capture_mode { SMI240_CAPTURE_OFF = 0, SMI240_CAPTURE_ON = 1 };

struct smi240_data {
	struct regmap *regmap;
	u16 accel_filter_freq;
	u16 anglvel_filter_freq;
	u8 built_in_self_test_count;
	enum capture_mode capture;
	/*
	 * Ensure natural alignment for timestamp if present.
	 * Channel size: 2 bytes.
	 * Max length needed: 2 * 3 channels + temp channel + 2 bytes padding + 8 byte ts.
	 * If fewer channels are enabled, less space may be needed, as
	 * long as the timestamp is still aligned to 8 bytes.
	 */
	s16 buf[12] __aligned(8);

	__be32 spi_buf __aligned(IIO_DMA_MINALIGN);
};

enum {
	SMI240_TEMP_OBJECT,
	SMI240_SCAN_ACCEL_X,
	SMI240_SCAN_ACCEL_Y,
	SMI240_SCAN_ACCEL_Z,
	SMI240_SCAN_GYRO_X,
	SMI240_SCAN_GYRO_Y,
	SMI240_SCAN_GYRO_Z,
	SMI240_SCAN_TIMESTAMP,
};

static const struct iio_chan_spec smi240_channels[] = {
	SMI240_TEMP_CHANNEL(SMI240_TEMP_OBJECT),
	SMI240_DATA_CHANNEL(IIO_ACCEL, X, SMI240_SCAN_ACCEL_X),
	SMI240_DATA_CHANNEL(IIO_ACCEL, Y, SMI240_SCAN_ACCEL_Y),
	SMI240_DATA_CHANNEL(IIO_ACCEL, Z, SMI240_SCAN_ACCEL_Z),
	SMI240_DATA_CHANNEL(IIO_ANGL_VEL, X, SMI240_SCAN_GYRO_X),
	SMI240_DATA_CHANNEL(IIO_ANGL_VEL, Y, SMI240_SCAN_GYRO_Y),
	SMI240_DATA_CHANNEL(IIO_ANGL_VEL, Z, SMI240_SCAN_GYRO_Z),
	IIO_CHAN_SOFT_TIMESTAMP(SMI240_SCAN_TIMESTAMP),
};

static const int smi240_low_pass_freqs[] = { SMI240_LOW_BANDWIDTH_HZ,
					     SMI240_HIGH_BANDWIDTH_HZ };

static u8 smi240_crc3(u32 data, u8 init, u8 poly)
{
	u8 crc = init;
	u8 do_xor;
	s8 i = 31;

	do {
		do_xor = crc & 0x04;
		crc <<= 1;
		crc |= 0x01 & (data >> i);
		if (do_xor)
			crc ^= poly;

		crc &= SMI240_CRC_MASK;
	} while (--i >= 0);

	return crc;
}

static bool smi240_sensor_data_is_valid(u32 data)
{
	if (smi240_crc3(data, SMI240_CRC_INIT, SMI240_CRC_POLY) != 0)
		return false;

	if (FIELD_GET(SMI240_READ_SD_BIT_MASK, data) &
	    FIELD_GET(SMI240_READ_CS_BIT_MASK, data))
		return false;

	return true;
}

static int smi240_regmap_spi_read(void *context, const void *reg_buf,
				  size_t reg_size, void *val_buf,
				  size_t val_size)
{
	int ret;
	u32 request, response;
	u16 *val = val_buf;
	struct spi_device *spi = context;
	struct iio_dev *indio_dev = dev_get_drvdata(&spi->dev);
	struct smi240_data *iio_priv_data = iio_priv(indio_dev);

	if (reg_size != 1 || val_size != 2)
		return -EINVAL;

	request = FIELD_PREP(SMI240_WRITE_BUS_ID_MASK, SMI240_BUS_ID);
	request |= FIELD_PREP(SMI240_WRITE_CAP_BIT_MASK, iio_priv_data->capture);
	request |= FIELD_PREP(SMI240_WRITE_ADDR_MASK, *(u8 *)reg_buf);
	request |= smi240_crc3(request, SMI240_CRC_INIT, SMI240_CRC_POLY);

	iio_priv_data->spi_buf = cpu_to_be32(request);

	/*
	 * SMI240 module consists of a 32Bit Out Of Frame (OOF)
	 * SPI protocol, where the slave interface responds to
	 * the Master request in the next frame.
	 * CS signal must toggle (> 700 ns) between the frames.
	 */
	ret = spi_write(spi, &iio_priv_data->spi_buf, sizeof(request));
	if (ret)
		return ret;

	ret = spi_read(spi, &iio_priv_data->spi_buf, sizeof(response));
	if (ret)
		return ret;

	response = be32_to_cpu(iio_priv_data->spi_buf);

	if (!smi240_sensor_data_is_valid(response))
		return -EIO;

	*val = FIELD_GET(SMI240_READ_DATA_MASK, response);

	return 0;
}

static int smi240_regmap_spi_write(void *context, const void *data,
				   size_t count)
{
	u8 reg_addr;
	u16 reg_data;
	u32 request;
	const u8 *data_ptr = data;
	struct spi_device *spi = context;
	struct iio_dev *indio_dev = dev_get_drvdata(&spi->dev);
	struct smi240_data *iio_priv_data = iio_priv(indio_dev);

	if (count < 2)
		return -EINVAL;

	reg_addr = data_ptr[0];
	memcpy(&reg_data, &data_ptr[1], sizeof(reg_data));

	request = FIELD_PREP(SMI240_WRITE_BUS_ID_MASK, SMI240_BUS_ID);
	request |= FIELD_PREP(SMI240_WRITE_BIT_MASK, 1);
	request |= FIELD_PREP(SMI240_WRITE_ADDR_MASK, reg_addr);
	request |= FIELD_PREP(SMI240_WRITE_DATA_MASK, reg_data);
	request |= smi240_crc3(request, SMI240_CRC_INIT, SMI240_CRC_POLY);

	iio_priv_data->spi_buf = cpu_to_be32(request);

	return spi_write(spi, &iio_priv_data->spi_buf, sizeof(request));
}

static const struct regmap_bus smi240_regmap_bus = {
	.read = smi240_regmap_spi_read,
	.write = smi240_regmap_spi_write,
};

static const struct regmap_config smi240_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,
	.val_format_endian = REGMAP_ENDIAN_NATIVE,
};

static int smi240_soft_reset(struct smi240_data *data)
{
	int ret;

	ret = regmap_write(data->regmap, SMI240_CMD_REG, SMI240_SOFT_RESET_CMD);
	if (ret)
		return ret;
	fsleep(SMI240_DIGITAL_STARTUP_DELAY_US);

	return 0;
}

static int smi240_soft_config(struct smi240_data *data)
{
	int ret;
	u8 acc_bw, gyr_bw;
	u16 request;

	switch (data->accel_filter_freq) {
	case SMI240_LOW_BANDWIDTH_HZ:
		acc_bw = 0x1;
		break;
	case SMI240_HIGH_BANDWIDTH_HZ:
		acc_bw = 0x0;
		break;
	default:
		return -EINVAL;
	}

	switch (data->anglvel_filter_freq) {
	case SMI240_LOW_BANDWIDTH_HZ:
		gyr_bw = 0x1;
		break;
	case SMI240_HIGH_BANDWIDTH_HZ:
		gyr_bw = 0x0;
		break;
	default:
		return -EINVAL;
	}

	request = FIELD_PREP(SMI240_SOFT_CONFIG_EOC_MASK, 1);
	request |= FIELD_PREP(SMI240_SOFT_CONFIG_GYR_BW_MASK, gyr_bw);
	request |= FIELD_PREP(SMI240_SOFT_CONFIG_ACC_BW_MASK, acc_bw);
	request |= FIELD_PREP(SMI240_SOFT_CONFIG_BITE_AUTO_MASK, 1);
	request |= FIELD_PREP(SMI240_SOFT_CONFIG_BITE_REP_MASK,
			      data->built_in_self_test_count - 1);

	ret = regmap_write(data->regmap, SMI240_SOFT_CONFIG_REG, request);
	if (ret)
		return ret;

	fsleep(SMI240_MECH_STARTUP_DELAY_US +
	       data->built_in_self_test_count * SMI240_BITE_SEQUENCE_DELAY_US +
	       SMI240_FILTER_FLUSH_DELAY_US);

	return 0;
}

static int smi240_get_low_pass_filter_freq(struct smi240_data *data,
					   int chan_type, int *val)
{
	switch (chan_type) {
	case IIO_ACCEL:
		*val = data->accel_filter_freq;
		return 0;
	case IIO_ANGL_VEL:
		*val = data->anglvel_filter_freq;
		return 0;
	default:
		return -EINVAL;
	}
}

static int smi240_get_data(struct smi240_data *data, int chan_type, int axis,
			   int *val)
{
	u8 reg;
	int ret, sample;

	switch (chan_type) {
	case IIO_TEMP:
		reg = SMI240_TEMP_CUR_REG;
		break;
	case IIO_ACCEL:
		reg = SMI240_ACCEL_X_CUR_REG + (axis - IIO_MOD_X);
		break;
	case IIO_ANGL_VEL:
		reg = SMI240_GYRO_X_CUR_REG + (axis - IIO_MOD_X);
		break;
	default:
		return -EINVAL;
	}

	ret = regmap_read(data->regmap, reg, &sample);
	if (ret)
		return ret;

	*val = sign_extend32(sample, 15);

	return 0;
}

static irqreturn_t smi240_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct smi240_data *data = iio_priv(indio_dev);
	int base = SMI240_DATA_CAP_FIRST_REG, i = 0;
	int ret, chan, sample;

	data->capture = SMI240_CAPTURE_ON;

	iio_for_each_active_channel(indio_dev, chan) {
		ret = regmap_read(data->regmap, base + chan, &sample);
		data->capture = SMI240_CAPTURE_OFF;
		if (ret)
			goto out;
		data->buf[i++] = sample;
	}

	iio_push_to_buffers_with_timestamp(indio_dev, data->buf, pf->timestamp);

out:
	iio_trigger_notify_done(indio_dev->trig);
	return IRQ_HANDLED;
}

static int smi240_read_avail(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan, const int **vals,
			     int *type, int *length, long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		*vals = smi240_low_pass_freqs;
		*length = ARRAY_SIZE(smi240_low_pass_freqs);
		*type = IIO_VAL_INT;
		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
}

static int smi240_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan, int *val,
			   int *val2, long mask)
{
	int ret;
	struct smi240_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (!iio_device_claim_direct(indio_dev))
			return -EBUSY;
		ret = smi240_get_data(data, chan->type, chan->channel2, val);
		iio_device_release_direct(indio_dev);
		if (ret)
			return ret;
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		ret = smi240_get_low_pass_filter_freq(data, chan->type, val);
		if (ret)
			return ret;
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_TEMP:
			*val = SMI240_TEMP_SCALE / GIGA;
			*val2 = SMI240_TEMP_SCALE % GIGA;
			return IIO_VAL_INT_PLUS_NANO;
		case IIO_ACCEL:
			*val = 0;
			*val2 = SMI240_ACCEL_SCALE;
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_ANGL_VEL:
			*val = 0;
			*val2 = SMI240_GYRO_SCALE;
			return IIO_VAL_INT_PLUS_MICRO;
		default:
			return -EINVAL;
		}

	case IIO_CHAN_INFO_OFFSET:
		if (chan->type == IIO_TEMP) {
			*val = SMI240_TEMP_OFFSET;
			return IIO_VAL_INT;
		} else {
			return -EINVAL;
		}

	default:
		return -EINVAL;
	}
}

static int smi240_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan, int val, int val2,
			    long mask)
{
	int ret, i;
	struct smi240_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		for (i = 0; i < ARRAY_SIZE(smi240_low_pass_freqs); i++) {
			if (val == smi240_low_pass_freqs[i])
				break;
		}

		if (i == ARRAY_SIZE(smi240_low_pass_freqs))
			return -EINVAL;

		switch (chan->type) {
		case IIO_ACCEL:
			data->accel_filter_freq = val;
			break;
		case IIO_ANGL_VEL:
			data->anglvel_filter_freq = val;
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	/* Write access to soft config is locked until hard/soft reset */
	ret = smi240_soft_reset(data);
	if (ret)
		return ret;

	return smi240_soft_config(data);
}

static int smi240_write_raw_get_fmt(struct iio_dev *indio_dev,
				    struct iio_chan_spec const *chan, long info)
{
	switch (info) {
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_TEMP:
			return IIO_VAL_INT_PLUS_NANO;
		default:
			return IIO_VAL_INT_PLUS_MICRO;
		}
	default:
		return IIO_VAL_INT_PLUS_MICRO;
	}
}

static int smi240_init(struct smi240_data *data)
{
	int ret;

	data->accel_filter_freq = SMI240_HIGH_BANDWIDTH_HZ;
	data->anglvel_filter_freq = SMI240_HIGH_BANDWIDTH_HZ;
	data->built_in_self_test_count = SMI240_BUILT_IN_SELF_TEST_COUNT;

	ret = smi240_soft_reset(data);
	if (ret)
		return ret;

	return smi240_soft_config(data);
}

static const struct iio_info smi240_info = {
	.read_avail = smi240_read_avail,
	.read_raw = smi240_read_raw,
	.write_raw = smi240_write_raw,
	.write_raw_get_fmt = smi240_write_raw_get_fmt,
};

static int smi240_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct iio_dev *indio_dev;
	struct regmap *regmap;
	struct smi240_data *data;
	int ret, response;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	regmap = devm_regmap_init(dev, &smi240_regmap_bus, dev,
				  &smi240_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap),
				     "Failed to initialize SPI Regmap\n");

	data = iio_priv(indio_dev);
	dev_set_drvdata(dev, indio_dev);
	data->regmap = regmap;
	data->capture = SMI240_CAPTURE_OFF;

	ret = regmap_read(data->regmap, SMI240_CHIP_ID_REG, &response);
	if (ret)
		return dev_err_probe(dev, ret, "Read chip id failed\n");

	if (response != SMI240_CHIP_ID)
		dev_info(dev, "Unknown chip id: 0x%04x\n", response);

	ret = smi240_init(data);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Device initialization failed\n");

	indio_dev->channels = smi240_channels;
	indio_dev->num_channels = ARRAY_SIZE(smi240_channels);
	indio_dev->name = "smi240";
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &smi240_info;

	ret = devm_iio_triggered_buffer_setup(dev, indio_dev,
					      iio_pollfunc_store_time,
					      smi240_trigger_handler, NULL);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Setup triggered buffer failed\n");

	ret = devm_iio_device_register(dev, indio_dev);
	if (ret)
		return dev_err_probe(dev, ret, "Register IIO device failed\n");

	return 0;
}

static const struct spi_device_id smi240_spi_id[] = {
	{ "smi240" },
	{ }
};
MODULE_DEVICE_TABLE(spi, smi240_spi_id);

static const struct of_device_id smi240_of_match[] = {
	{ .compatible = "bosch,smi240" },
	{ }
};
MODULE_DEVICE_TABLE(of, smi240_of_match);

static struct spi_driver smi240_spi_driver = {
	.probe = smi240_probe,
	.id_table = smi240_spi_id,
	.driver = {
		.of_match_table = smi240_of_match,
		.name = "smi240",
	},
};
module_spi_driver(smi240_spi_driver);

MODULE_AUTHOR("Markus Lochmann <markus.lochmann@de.bosch.com>");
MODULE_AUTHOR("Stefan Gutmann <stefan.gutmann@de.bosch.com>");
MODULE_DESCRIPTION("Bosch SMI240 SPI driver");
MODULE_LICENSE("Dual BSD/GPL");
