/*
 * ADIS16133/ADIS16135/ADIS16136 gyroscope driver
 *
 * Copyright 2012 Analog Devices Inc.
 *   Author: Lars-Peter Clausen <lars@metafoo.de>
 *
 * Licensed under the GPL-2.
 */

#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/module.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/imu/adis.h>

#include <linux/debugfs.h>

#define ADIS16136_REG_FLASH_CNT		0x00
#define ADIS16136_REG_TEMP_OUT		0x02
#define ADIS16136_REG_GYRO_OUT2		0x04
#define ADIS16136_REG_GYRO_OUT		0x06
#define ADIS16136_REG_GYRO_OFF2		0x08
#define ADIS16136_REG_GYRO_OFF		0x0A
#define ADIS16136_REG_ALM_MAG1		0x10
#define ADIS16136_REG_ALM_MAG2		0x12
#define ADIS16136_REG_ALM_SAMPL1	0x14
#define ADIS16136_REG_ALM_SAMPL2	0x16
#define ADIS16136_REG_ALM_CTRL		0x18
#define ADIS16136_REG_GPIO_CTRL		0x1A
#define ADIS16136_REG_MSC_CTRL		0x1C
#define ADIS16136_REG_SMPL_PRD		0x1E
#define ADIS16136_REG_AVG_CNT		0x20
#define ADIS16136_REG_DEC_RATE		0x22
#define ADIS16136_REG_SLP_CTRL		0x24
#define ADIS16136_REG_DIAG_STAT		0x26
#define ADIS16136_REG_GLOB_CMD		0x28
#define ADIS16136_REG_LOT1		0x32
#define ADIS16136_REG_LOT2		0x34
#define ADIS16136_REG_LOT3		0x36
#define ADIS16136_REG_PROD_ID		0x38
#define ADIS16136_REG_SERIAL_NUM	0x3A

#define ADIS16136_DIAG_STAT_FLASH_UPDATE_FAIL	2
#define ADIS16136_DIAG_STAT_SPI_FAIL		3
#define ADIS16136_DIAG_STAT_SELF_TEST_FAIL	5
#define ADIS16136_DIAG_STAT_FLASH_CHKSUM_FAIL	6

#define ADIS16136_MSC_CTRL_MEMORY_TEST BIT(11)
#define ADIS16136_MSC_CTRL_SELF_TEST BIT(10)

struct adis16136_chip_info {
	unsigned int precision;
	unsigned int fullscale;
};

struct adis16136 {
	const struct adis16136_chip_info *chip_info;

	struct adis adis;
};

#ifdef CONFIG_DEBUG_FS

static ssize_t adis16136_show_serial(struct file *file,
		char __user *userbuf, size_t count, loff_t *ppos)
{
	struct adis16136 *adis16136 = file->private_data;
	uint16_t lot1, lot2, lot3, serial;
	char buf[20];
	size_t len;
	int ret;

	ret = adis_read_reg_16(&adis16136->adis, ADIS16136_REG_SERIAL_NUM,
		&serial);
	if (ret < 0)
		return ret;

	ret = adis_read_reg_16(&adis16136->adis, ADIS16136_REG_LOT1, &lot1);
	if (ret < 0)
		return ret;

	ret = adis_read_reg_16(&adis16136->adis, ADIS16136_REG_LOT2, &lot2);
	if (ret < 0)
		return ret;

	ret = adis_read_reg_16(&adis16136->adis, ADIS16136_REG_LOT3, &lot3);
	if (ret < 0)
		return ret;

	len = snprintf(buf, sizeof(buf), "%.4x%.4x%.4x-%.4x\n", lot1, lot2,
		lot3, serial);

	return simple_read_from_buffer(userbuf, count, ppos, buf, len);
}

static const struct file_operations adis16136_serial_fops = {
	.open = simple_open,
	.read = adis16136_show_serial,
	.llseek = default_llseek,
	.owner = THIS_MODULE,
};

static int adis16136_show_product_id(void *arg, u64 *val)
{
	struct adis16136 *adis16136 = arg;
	u16 prod_id;
	int ret;

	ret = adis_read_reg_16(&adis16136->adis, ADIS16136_REG_PROD_ID,
		&prod_id);
	if (ret < 0)
		return ret;

	*val = prod_id;

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(adis16136_product_id_fops,
	adis16136_show_product_id, NULL, "%llu\n");

static int adis16136_show_flash_count(void *arg, u64 *val)
{
	struct adis16136 *adis16136 = arg;
	uint16_t flash_count;
	int ret;

	ret = adis_read_reg_16(&adis16136->adis, ADIS16136_REG_FLASH_CNT,
		&flash_count);
	if (ret < 0)
		return ret;

	*val = flash_count;

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(adis16136_flash_count_fops,
	adis16136_show_flash_count, NULL, "%lld\n");

static int adis16136_debugfs_init(struct iio_dev *indio_dev)
{
	struct adis16136 *adis16136 = iio_priv(indio_dev);

	debugfs_create_file("serial_number", 0400, indio_dev->debugfs_dentry,
		adis16136, &adis16136_serial_fops);
	debugfs_create_file("product_id", 0400, indio_dev->debugfs_dentry,
		adis16136, &adis16136_product_id_fops);
	debugfs_create_file("flash_count", 0400, indio_dev->debugfs_dentry,
		adis16136, &adis16136_flash_count_fops);

	return 0;
}

#else

static int adis16136_debugfs_init(struct iio_dev *indio_dev)
{
	return 0;
}

#endif

static int adis16136_set_freq(struct adis16136 *adis16136, unsigned int freq)
{
	unsigned int t;

	t = 32768 / freq;
	if (t < 0xf)
		t = 0xf;
	else if (t > 0xffff)
		t = 0xffff;
	else
		t--;

	return adis_write_reg_16(&adis16136->adis, ADIS16136_REG_SMPL_PRD, t);
}

static int adis16136_get_freq(struct adis16136 *adis16136, unsigned int *freq)
{
	uint16_t t;
	int ret;

	ret = adis_read_reg_16(&adis16136->adis, ADIS16136_REG_SMPL_PRD, &t);
	if (ret < 0)
		return ret;

	*freq = 32768 / (t + 1);

	return 0;
}

static ssize_t adis16136_write_frequency(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct adis16136 *adis16136 = iio_priv(indio_dev);
	unsigned int val;
	int ret;

	ret = kstrtouint(buf, 10, &val);
	if (ret)
		return ret;

	if (val == 0)
		return -EINVAL;

	ret = adis16136_set_freq(adis16136, val);

	return ret ? ret : len;
}

static ssize_t adis16136_read_frequency(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct adis16136 *adis16136 = iio_priv(indio_dev);
	unsigned int freq;
	int ret;

	ret = adis16136_get_freq(adis16136, &freq);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", freq);
}

static IIO_DEV_ATTR_SAMP_FREQ(S_IWUSR | S_IRUGO,
				  adis16136_read_frequency,
				  adis16136_write_frequency);

static const unsigned adis16136_3db_divisors[] = {
	[0] = 2, /* Special case */
	[1] = 6,
	[2] = 12,
	[3] = 25,
	[4] = 50,
	[5] = 100,
	[6] = 200,
	[7] = 200, /* Not a valid setting */
};

static int adis16136_set_filter(struct iio_dev *indio_dev, int val)
{
	struct adis16136 *adis16136 = iio_priv(indio_dev);
	unsigned int freq;
	int i, ret;

	ret = adis16136_get_freq(adis16136, &freq);
	if (ret < 0)
		return ret;

	for (i = ARRAY_SIZE(adis16136_3db_divisors) - 1; i >= 1; i--) {
		if (freq / adis16136_3db_divisors[i] >= val)
			break;
	}

	return adis_write_reg_16(&adis16136->adis, ADIS16136_REG_AVG_CNT, i);
}

static int adis16136_get_filter(struct iio_dev *indio_dev, int *val)
{
	struct adis16136 *adis16136 = iio_priv(indio_dev);
	unsigned int freq;
	uint16_t val16;
	int ret;

	mutex_lock(&indio_dev->mlock);

	ret = adis_read_reg_16(&adis16136->adis, ADIS16136_REG_AVG_CNT, &val16);
	if (ret < 0)
		goto err_unlock;

	ret = adis16136_get_freq(adis16136, &freq);
	if (ret < 0)
		goto err_unlock;

	*val = freq / adis16136_3db_divisors[val16 & 0x07];

err_unlock:
	mutex_unlock(&indio_dev->mlock);

	return ret ? ret : IIO_VAL_INT;
}

static int adis16136_read_raw(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, int *val, int *val2, long info)
{
	struct adis16136 *adis16136 = iio_priv(indio_dev);
	uint32_t val32;
	int ret;

	switch (info) {
	case IIO_CHAN_INFO_RAW:
		return adis_single_conversion(indio_dev, chan, 0, val);
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_ANGL_VEL:
			*val = adis16136->chip_info->precision;
			*val2 = (adis16136->chip_info->fullscale << 16);
			return IIO_VAL_FRACTIONAL;
		case IIO_TEMP:
			*val = 10;
			*val2 = 697000; /* 0.010697 degree Celsius */
			return IIO_VAL_INT_PLUS_MICRO;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_CALIBBIAS:
		ret = adis_read_reg_32(&adis16136->adis,
			ADIS16136_REG_GYRO_OFF2, &val32);
		if (ret < 0)
			return ret;

		*val = sign_extend32(val32, 31);

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		return adis16136_get_filter(indio_dev, val);
	default:
		return -EINVAL;
	}
}

static int adis16136_write_raw(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, int val, int val2, long info)
{
	struct adis16136 *adis16136 = iio_priv(indio_dev);

	switch (info) {
	case IIO_CHAN_INFO_CALIBBIAS:
		return adis_write_reg_32(&adis16136->adis,
			ADIS16136_REG_GYRO_OFF2, val);
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		return adis16136_set_filter(indio_dev, val);
	default:
		break;
	}

	return -EINVAL;
}

enum {
	ADIS16136_SCAN_GYRO,
	ADIS16136_SCAN_TEMP,
};

static const struct iio_chan_spec adis16136_channels[] = {
	{
		.type = IIO_ANGL_VEL,
		.modified = 1,
		.channel2 = IIO_MOD_X,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_CALIBBIAS) |
			BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),

		.address = ADIS16136_REG_GYRO_OUT2,
		.scan_index = ADIS16136_SCAN_GYRO,
		.scan_type = {
			.sign = 's',
			.realbits = 32,
			.storagebits = 32,
			.endianness = IIO_BE,
		},
	}, {
		.type = IIO_TEMP,
		.indexed = 1,
		.channel = 0,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_SCALE),
		.address = ADIS16136_REG_TEMP_OUT,
		.scan_index = ADIS16136_SCAN_TEMP,
		.scan_type = {
			.sign = 's',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_BE,
		},
	},
	IIO_CHAN_SOFT_TIMESTAMP(2),
};

static struct attribute *adis16136_attributes[] = {
	&iio_dev_attr_sampling_frequency.dev_attr.attr,
	NULL
};

static const struct attribute_group adis16136_attribute_group = {
	.attrs = adis16136_attributes,
};

static const struct iio_info adis16136_info = {
	.driver_module = THIS_MODULE,
	.attrs = &adis16136_attribute_group,
	.read_raw = &adis16136_read_raw,
	.write_raw = &adis16136_write_raw,
	.update_scan_mode = adis_update_scan_mode,
	.debugfs_reg_access = adis_debugfs_reg_access,
};

static int adis16136_stop_device(struct iio_dev *indio_dev)
{
	struct adis16136 *adis16136 = iio_priv(indio_dev);
	int ret;

	ret = adis_write_reg_16(&adis16136->adis, ADIS16136_REG_SLP_CTRL, 0xff);
	if (ret)
		dev_err(&indio_dev->dev,
			"Could not power down device: %d\n", ret);

	return ret;
}

static int adis16136_initial_setup(struct iio_dev *indio_dev)
{
	struct adis16136 *adis16136 = iio_priv(indio_dev);
	unsigned int device_id;
	uint16_t prod_id;
	int ret;

	ret = adis_initial_startup(&adis16136->adis);
	if (ret)
		return ret;

	ret = adis_read_reg_16(&adis16136->adis, ADIS16136_REG_PROD_ID,
		&prod_id);
	if (ret)
		return ret;

	ret = sscanf(indio_dev->name, "adis%u\n", &device_id);
	if (ret != 1)
		return -EINVAL;

	if (prod_id != device_id)
		dev_warn(&indio_dev->dev, "Device ID(%u) and product ID(%u) do not match.",
				device_id, prod_id);

	return 0;
}

static const char * const adis16136_status_error_msgs[] = {
	[ADIS16136_DIAG_STAT_FLASH_UPDATE_FAIL] = "Flash update failed",
	[ADIS16136_DIAG_STAT_SPI_FAIL] = "SPI failure",
	[ADIS16136_DIAG_STAT_SELF_TEST_FAIL] = "Self test error",
	[ADIS16136_DIAG_STAT_FLASH_CHKSUM_FAIL] = "Flash checksum error",
};

static const struct adis_data adis16136_data = {
	.diag_stat_reg = ADIS16136_REG_DIAG_STAT,
	.glob_cmd_reg = ADIS16136_REG_GLOB_CMD,
	.msc_ctrl_reg = ADIS16136_REG_MSC_CTRL,

	.self_test_mask = ADIS16136_MSC_CTRL_SELF_TEST,
	.startup_delay = 80,

	.read_delay = 10,
	.write_delay = 10,

	.status_error_msgs = adis16136_status_error_msgs,
	.status_error_mask = BIT(ADIS16136_DIAG_STAT_FLASH_UPDATE_FAIL) |
		BIT(ADIS16136_DIAG_STAT_SPI_FAIL) |
		BIT(ADIS16136_DIAG_STAT_SELF_TEST_FAIL) |
		BIT(ADIS16136_DIAG_STAT_FLASH_CHKSUM_FAIL),
};

enum adis16136_id {
	ID_ADIS16133,
	ID_ADIS16135,
	ID_ADIS16136,
	ID_ADIS16137,
};

static const struct adis16136_chip_info adis16136_chip_info[] = {
	[ID_ADIS16133] = {
		.precision = IIO_DEGREE_TO_RAD(1200),
		.fullscale = 24000,
	},
	[ID_ADIS16135] = {
		.precision = IIO_DEGREE_TO_RAD(300),
		.fullscale = 24000,
	},
	[ID_ADIS16136] = {
		.precision = IIO_DEGREE_TO_RAD(450),
		.fullscale = 24623,
	},
	[ID_ADIS16137] = {
		.precision = IIO_DEGREE_TO_RAD(1000),
		.fullscale = 24609,
	},
};

static int adis16136_probe(struct spi_device *spi)
{
	const struct spi_device_id *id = spi_get_device_id(spi);
	struct adis16136 *adis16136;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*adis16136));
	if (indio_dev == NULL)
		return -ENOMEM;

	spi_set_drvdata(spi, indio_dev);

	adis16136 = iio_priv(indio_dev);

	adis16136->chip_info = &adis16136_chip_info[id->driver_data];
	indio_dev->dev.parent = &spi->dev;
	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->channels = adis16136_channels;
	indio_dev->num_channels = ARRAY_SIZE(adis16136_channels);
	indio_dev->info = &adis16136_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = adis_init(&adis16136->adis, indio_dev, spi, &adis16136_data);
	if (ret)
		return ret;

	ret = adis_setup_buffer_and_trigger(&adis16136->adis, indio_dev, NULL);
	if (ret)
		return ret;

	ret = adis16136_initial_setup(indio_dev);
	if (ret)
		goto error_cleanup_buffer;

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_stop_device;

	adis16136_debugfs_init(indio_dev);

	return 0;

error_stop_device:
	adis16136_stop_device(indio_dev);
error_cleanup_buffer:
	adis_cleanup_buffer_and_trigger(&adis16136->adis, indio_dev);
	return ret;
}

static int adis16136_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct adis16136 *adis16136 = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	adis16136_stop_device(indio_dev);

	adis_cleanup_buffer_and_trigger(&adis16136->adis, indio_dev);

	return 0;
}

static const struct spi_device_id adis16136_ids[] = {
	{ "adis16133", ID_ADIS16133 },
	{ "adis16135", ID_ADIS16135 },
	{ "adis16136", ID_ADIS16136 },
	{ "adis16137", ID_ADIS16137 },
	{ }
};
MODULE_DEVICE_TABLE(spi, adis16136_ids);

static struct spi_driver adis16136_driver = {
	.driver = {
		.name = "adis16136",
	},
	.id_table = adis16136_ids,
	.probe = adis16136_probe,
	.remove = adis16136_remove,
};
module_spi_driver(adis16136_driver);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("Analog Devices ADIS16133/ADIS16135/ADIS16136 gyroscope driver");
MODULE_LICENSE("GPL v2");
