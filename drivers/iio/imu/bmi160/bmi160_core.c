/*
 * BMI160 - Bosch IMU (accel, gyro plus external magnetometer)
 *
 * Copyright (c) 2016, Intel Corporation.
 *
 * This file is subject to the terms and conditions of version 2 of
 * the GNU General Public License.  See the file COPYING in the main
 * directory of this archive for more details.
 *
 * IIO core driver for BMI160, with support for I2C/SPI busses
 *
 * TODO: magnetometer, interrupts, hardware FIFO
 */
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/acpi.h>
#include <linux/delay.h>

#include <linux/iio/iio.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/buffer.h>
#include <linux/iio/sysfs.h>

#include "bmi160.h"

#define BMI160_REG_CHIP_ID	0x00
#define BMI160_CHIP_ID_VAL	0xD1

#define BMI160_REG_PMU_STATUS	0x03

/* X axis data low byte address, the rest can be obtained using axis offset */
#define BMI160_REG_DATA_MAGN_XOUT_L	0x04
#define BMI160_REG_DATA_GYRO_XOUT_L	0x0C
#define BMI160_REG_DATA_ACCEL_XOUT_L	0x12

#define BMI160_REG_ACCEL_CONFIG		0x40
#define BMI160_ACCEL_CONFIG_ODR_MASK	GENMASK(3, 0)
#define BMI160_ACCEL_CONFIG_BWP_MASK	GENMASK(6, 4)

#define BMI160_REG_ACCEL_RANGE		0x41
#define BMI160_ACCEL_RANGE_2G		0x03
#define BMI160_ACCEL_RANGE_4G		0x05
#define BMI160_ACCEL_RANGE_8G		0x08
#define BMI160_ACCEL_RANGE_16G		0x0C

#define BMI160_REG_GYRO_CONFIG		0x42
#define BMI160_GYRO_CONFIG_ODR_MASK	GENMASK(3, 0)
#define BMI160_GYRO_CONFIG_BWP_MASK	GENMASK(5, 4)

#define BMI160_REG_GYRO_RANGE		0x43
#define BMI160_GYRO_RANGE_2000DPS	0x00
#define BMI160_GYRO_RANGE_1000DPS	0x01
#define BMI160_GYRO_RANGE_500DPS	0x02
#define BMI160_GYRO_RANGE_250DPS	0x03
#define BMI160_GYRO_RANGE_125DPS	0x04

#define BMI160_REG_CMD			0x7E
#define BMI160_CMD_ACCEL_PM_SUSPEND	0x10
#define BMI160_CMD_ACCEL_PM_NORMAL	0x11
#define BMI160_CMD_ACCEL_PM_LOW_POWER	0x12
#define BMI160_CMD_GYRO_PM_SUSPEND	0x14
#define BMI160_CMD_GYRO_PM_NORMAL	0x15
#define BMI160_CMD_GYRO_PM_FAST_STARTUP	0x17
#define BMI160_CMD_SOFTRESET		0xB6

#define BMI160_REG_DUMMY		0x7F

#define BMI160_ACCEL_PMU_MIN_USLEEP	3200
#define BMI160_ACCEL_PMU_MAX_USLEEP	3800
#define BMI160_GYRO_PMU_MIN_USLEEP	55000
#define BMI160_GYRO_PMU_MAX_USLEEP	80000
#define BMI160_SOFTRESET_USLEEP		1000

#define BMI160_CHANNEL(_type, _axis, _index) {			\
	.type = _type,						\
	.modified = 1,						\
	.channel2 = IIO_MOD_##_axis,				\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |  \
		BIT(IIO_CHAN_INFO_SAMP_FREQ),			\
	.scan_index = _index,					\
	.scan_type = {						\
		.sign = 's',					\
		.realbits = 16,					\
		.storagebits = 16,				\
		.endianness = IIO_LE,				\
	},							\
}

/* scan indexes follow DATA register order */
enum bmi160_scan_axis {
	BMI160_SCAN_EXT_MAGN_X = 0,
	BMI160_SCAN_EXT_MAGN_Y,
	BMI160_SCAN_EXT_MAGN_Z,
	BMI160_SCAN_RHALL,
	BMI160_SCAN_GYRO_X,
	BMI160_SCAN_GYRO_Y,
	BMI160_SCAN_GYRO_Z,
	BMI160_SCAN_ACCEL_X,
	BMI160_SCAN_ACCEL_Y,
	BMI160_SCAN_ACCEL_Z,
	BMI160_SCAN_TIMESTAMP,
};

enum bmi160_sensor_type {
	BMI160_ACCEL	= 0,
	BMI160_GYRO,
	BMI160_EXT_MAGN,
	BMI160_NUM_SENSORS /* must be last */
};

struct bmi160_data {
	struct regmap *regmap;
};

const struct regmap_config bmi160_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};
EXPORT_SYMBOL(bmi160_regmap_config);

struct bmi160_regs {
	u8 data; /* LSB byte register for X-axis */
	u8 config;
	u8 config_odr_mask;
	u8 config_bwp_mask;
	u8 range;
	u8 pmu_cmd_normal;
	u8 pmu_cmd_suspend;
};

static struct bmi160_regs bmi160_regs[] = {
	[BMI160_ACCEL] = {
		.data	= BMI160_REG_DATA_ACCEL_XOUT_L,
		.config	= BMI160_REG_ACCEL_CONFIG,
		.config_odr_mask = BMI160_ACCEL_CONFIG_ODR_MASK,
		.config_bwp_mask = BMI160_ACCEL_CONFIG_BWP_MASK,
		.range	= BMI160_REG_ACCEL_RANGE,
		.pmu_cmd_normal = BMI160_CMD_ACCEL_PM_NORMAL,
		.pmu_cmd_suspend = BMI160_CMD_ACCEL_PM_SUSPEND,
	},
	[BMI160_GYRO] = {
		.data	= BMI160_REG_DATA_GYRO_XOUT_L,
		.config	= BMI160_REG_GYRO_CONFIG,
		.config_odr_mask = BMI160_GYRO_CONFIG_ODR_MASK,
		.config_bwp_mask = BMI160_GYRO_CONFIG_BWP_MASK,
		.range	= BMI160_REG_GYRO_RANGE,
		.pmu_cmd_normal = BMI160_CMD_GYRO_PM_NORMAL,
		.pmu_cmd_suspend = BMI160_CMD_GYRO_PM_SUSPEND,
	},
};

struct bmi160_pmu_time {
	unsigned long min;
	unsigned long max;
};

static struct bmi160_pmu_time bmi160_pmu_time[] = {
	[BMI160_ACCEL] = {
		.min = BMI160_ACCEL_PMU_MIN_USLEEP,
		.max = BMI160_ACCEL_PMU_MAX_USLEEP
	},
	[BMI160_GYRO] = {
		.min = BMI160_GYRO_PMU_MIN_USLEEP,
		.max = BMI160_GYRO_PMU_MIN_USLEEP,
	},
};

struct bmi160_scale {
	u8 bits;
	int uscale;
};

struct bmi160_odr {
	u8 bits;
	int odr;
	int uodr;
};

static const struct bmi160_scale bmi160_accel_scale[] = {
	{ BMI160_ACCEL_RANGE_2G, 598},
	{ BMI160_ACCEL_RANGE_4G, 1197},
	{ BMI160_ACCEL_RANGE_8G, 2394},
	{ BMI160_ACCEL_RANGE_16G, 4788},
};

static const struct bmi160_scale bmi160_gyro_scale[] = {
	{ BMI160_GYRO_RANGE_2000DPS, 1065},
	{ BMI160_GYRO_RANGE_1000DPS, 532},
	{ BMI160_GYRO_RANGE_500DPS, 266},
	{ BMI160_GYRO_RANGE_250DPS, 133},
	{ BMI160_GYRO_RANGE_125DPS, 66},
};

struct bmi160_scale_item {
	const struct bmi160_scale *tbl;
	int num;
};

static const struct  bmi160_scale_item bmi160_scale_table[] = {
	[BMI160_ACCEL] = {
		.tbl	= bmi160_accel_scale,
		.num	= ARRAY_SIZE(bmi160_accel_scale),
	},
	[BMI160_GYRO] = {
		.tbl	= bmi160_gyro_scale,
		.num	= ARRAY_SIZE(bmi160_gyro_scale),
	},
};

static const struct bmi160_odr bmi160_accel_odr[] = {
	{0x01, 0, 781250},
	{0x02, 1, 562500},
	{0x03, 3, 125000},
	{0x04, 6, 250000},
	{0x05, 12, 500000},
	{0x06, 25, 0},
	{0x07, 50, 0},
	{0x08, 100, 0},
	{0x09, 200, 0},
	{0x0A, 400, 0},
	{0x0B, 800, 0},
	{0x0C, 1600, 0},
};

static const struct bmi160_odr bmi160_gyro_odr[] = {
	{0x06, 25, 0},
	{0x07, 50, 0},
	{0x08, 100, 0},
	{0x09, 200, 0},
	{0x0A, 400, 0},
	{0x0B, 800, 0},
	{0x0C, 1600, 0},
	{0x0D, 3200, 0},
};

struct bmi160_odr_item {
	const struct bmi160_odr *tbl;
	int num;
};

static const struct  bmi160_odr_item bmi160_odr_table[] = {
	[BMI160_ACCEL] = {
		.tbl	= bmi160_accel_odr,
		.num	= ARRAY_SIZE(bmi160_accel_odr),
	},
	[BMI160_GYRO] = {
		.tbl	= bmi160_gyro_odr,
		.num	= ARRAY_SIZE(bmi160_gyro_odr),
	},
};

static const struct iio_chan_spec bmi160_channels[] = {
	BMI160_CHANNEL(IIO_ACCEL, X, BMI160_SCAN_ACCEL_X),
	BMI160_CHANNEL(IIO_ACCEL, Y, BMI160_SCAN_ACCEL_Y),
	BMI160_CHANNEL(IIO_ACCEL, Z, BMI160_SCAN_ACCEL_Z),
	BMI160_CHANNEL(IIO_ANGL_VEL, X, BMI160_SCAN_GYRO_X),
	BMI160_CHANNEL(IIO_ANGL_VEL, Y, BMI160_SCAN_GYRO_Y),
	BMI160_CHANNEL(IIO_ANGL_VEL, Z, BMI160_SCAN_GYRO_Z),
	IIO_CHAN_SOFT_TIMESTAMP(BMI160_SCAN_TIMESTAMP),
};

static enum bmi160_sensor_type bmi160_to_sensor(enum iio_chan_type iio_type)
{
	switch (iio_type) {
	case IIO_ACCEL:
		return BMI160_ACCEL;
	case IIO_ANGL_VEL:
		return BMI160_GYRO;
	default:
		return -EINVAL;
	}
}

static
int bmi160_set_mode(struct bmi160_data *data, enum bmi160_sensor_type t,
		    bool mode)
{
	int ret;
	u8 cmd;

	if (mode)
		cmd = bmi160_regs[t].pmu_cmd_normal;
	else
		cmd = bmi160_regs[t].pmu_cmd_suspend;

	ret = regmap_write(data->regmap, BMI160_REG_CMD, cmd);
	if (ret < 0)
		return ret;

	usleep_range(bmi160_pmu_time[t].min, bmi160_pmu_time[t].max);

	return 0;
}

static
int bmi160_set_scale(struct bmi160_data *data, enum bmi160_sensor_type t,
		     int uscale)
{
	int i;

	for (i = 0; i < bmi160_scale_table[t].num; i++)
		if (bmi160_scale_table[t].tbl[i].uscale == uscale)
			break;

	if (i == bmi160_scale_table[t].num)
		return -EINVAL;

	return regmap_write(data->regmap, bmi160_regs[t].range,
			    bmi160_scale_table[t].tbl[i].bits);
}

static
int bmi160_get_scale(struct bmi160_data *data, enum bmi160_sensor_type t,
		     int *uscale)
{
	int i, ret, val;

	ret = regmap_read(data->regmap, bmi160_regs[t].range, &val);
	if (ret < 0)
		return ret;

	for (i = 0; i < bmi160_scale_table[t].num; i++)
		if (bmi160_scale_table[t].tbl[i].bits == val) {
			*uscale = bmi160_scale_table[t].tbl[i].uscale;
			return 0;
		}

	return -EINVAL;
}

static int bmi160_get_data(struct bmi160_data *data, int chan_type,
			   int axis, int *val)
{
	u8 reg;
	int ret;
	__le16 sample;
	enum bmi160_sensor_type t = bmi160_to_sensor(chan_type);

	reg = bmi160_regs[t].data + (axis - IIO_MOD_X) * sizeof(__le16);

	ret = regmap_bulk_read(data->regmap, reg, &sample, sizeof(__le16));
	if (ret < 0)
		return ret;

	*val = sign_extend32(le16_to_cpu(sample), 15);

	return 0;
}

static
int bmi160_set_odr(struct bmi160_data *data, enum bmi160_sensor_type t,
		   int odr, int uodr)
{
	int i;

	for (i = 0; i < bmi160_odr_table[t].num; i++)
		if (bmi160_odr_table[t].tbl[i].odr == odr &&
		    bmi160_odr_table[t].tbl[i].uodr == uodr)
			break;

	if (i >= bmi160_odr_table[t].num)
		return -EINVAL;

	return regmap_update_bits(data->regmap,
				  bmi160_regs[t].config,
				  bmi160_regs[t].config_odr_mask,
				  bmi160_odr_table[t].tbl[i].bits);
}

static int bmi160_get_odr(struct bmi160_data *data, enum bmi160_sensor_type t,
			  int *odr, int *uodr)
{
	int i, val, ret;

	ret = regmap_read(data->regmap, bmi160_regs[t].config, &val);
	if (ret < 0)
		return ret;

	val &= bmi160_regs[t].config_odr_mask;

	for (i = 0; i < bmi160_odr_table[t].num; i++)
		if (val == bmi160_odr_table[t].tbl[i].bits)
			break;

	if (i >= bmi160_odr_table[t].num)
		return -EINVAL;

	*odr = bmi160_odr_table[t].tbl[i].odr;
	*uodr = bmi160_odr_table[t].tbl[i].uodr;

	return 0;
}

static irqreturn_t bmi160_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct bmi160_data *data = iio_priv(indio_dev);
	__le16 buf[16];
	/* 3 sens x 3 axis x __le16 + 3 x __le16 pad + 4 x __le16 tstamp */
	int i, ret, j = 0, base = BMI160_REG_DATA_MAGN_XOUT_L;
	__le16 sample;

	for_each_set_bit(i, indio_dev->active_scan_mask,
			 indio_dev->masklength) {
		ret = regmap_bulk_read(data->regmap, base + i * sizeof(__le16),
				       &sample, sizeof(__le16));
		if (ret < 0)
			goto done;
		buf[j++] = sample;
	}

	iio_push_to_buffers_with_timestamp(indio_dev, buf,
					   iio_get_time_ns(indio_dev));
done:
	iio_trigger_notify_done(indio_dev->trig);
	return IRQ_HANDLED;
}

static int bmi160_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	int ret;
	struct bmi160_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = bmi160_get_data(data, chan->type, chan->channel2, val);
		if (ret < 0)
			return ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		ret = bmi160_get_scale(data,
				       bmi160_to_sensor(chan->type), val2);
		return ret < 0 ? ret : IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_SAMP_FREQ:
		ret = bmi160_get_odr(data, bmi160_to_sensor(chan->type),
				     val, val2);
		return ret < 0 ? ret : IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}

	return 0;
}

static int bmi160_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long mask)
{
	struct bmi160_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		return bmi160_set_scale(data,
					bmi160_to_sensor(chan->type), val2);
		break;
	case IIO_CHAN_INFO_SAMP_FREQ:
		return bmi160_set_odr(data, bmi160_to_sensor(chan->type),
				      val, val2);
	default:
		return -EINVAL;
	}

	return 0;
}

static
IIO_CONST_ATTR(in_accel_sampling_frequency_available,
	       "0.78125 1.5625 3.125 6.25 12.5 25 50 100 200 400 800 1600");
static
IIO_CONST_ATTR(in_anglvel_sampling_frequency_available,
	       "25 50 100 200 400 800 1600 3200");
static
IIO_CONST_ATTR(in_accel_scale_available,
	       "0.000598 0.001197 0.002394 0.004788");
static
IIO_CONST_ATTR(in_anglvel_scale_available,
	       "0.001065 0.000532 0.000266 0.000133 0.000066");

static struct attribute *bmi160_attrs[] = {
	&iio_const_attr_in_accel_sampling_frequency_available.dev_attr.attr,
	&iio_const_attr_in_anglvel_sampling_frequency_available.dev_attr.attr,
	&iio_const_attr_in_accel_scale_available.dev_attr.attr,
	&iio_const_attr_in_anglvel_scale_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group bmi160_attrs_group = {
	.attrs = bmi160_attrs,
};

static const struct iio_info bmi160_info = {
	.driver_module = THIS_MODULE,
	.read_raw = bmi160_read_raw,
	.write_raw = bmi160_write_raw,
	.attrs = &bmi160_attrs_group,
};

static const char *bmi160_match_acpi_device(struct device *dev)
{
	const struct acpi_device_id *id;

	id = acpi_match_device(dev->driver->acpi_match_table, dev);
	if (!id)
		return NULL;

	return dev_name(dev);
}

static int bmi160_chip_init(struct bmi160_data *data, bool use_spi)
{
	int ret;
	unsigned int val;
	struct device *dev = regmap_get_device(data->regmap);

	ret = regmap_write(data->regmap, BMI160_REG_CMD, BMI160_CMD_SOFTRESET);
	if (ret < 0)
		return ret;

	usleep_range(BMI160_SOFTRESET_USLEEP, BMI160_SOFTRESET_USLEEP + 1);

	/*
	 * CS rising edge is needed before starting SPI, so do a dummy read
	 * See Section 3.2.1, page 86 of the datasheet
	 */
	if (use_spi) {
		ret = regmap_read(data->regmap, BMI160_REG_DUMMY, &val);
		if (ret < 0)
			return ret;
	}

	ret = regmap_read(data->regmap, BMI160_REG_CHIP_ID, &val);
	if (ret < 0) {
		dev_err(dev, "Error reading chip id\n");
		return ret;
	}
	if (val != BMI160_CHIP_ID_VAL) {
		dev_err(dev, "Wrong chip id, got %x expected %x\n",
			val, BMI160_CHIP_ID_VAL);
		return -ENODEV;
	}

	ret = bmi160_set_mode(data, BMI160_ACCEL, true);
	if (ret < 0)
		return ret;

	ret = bmi160_set_mode(data, BMI160_GYRO, true);
	if (ret < 0)
		return ret;

	return 0;
}

static void bmi160_chip_uninit(struct bmi160_data *data)
{
	bmi160_set_mode(data, BMI160_GYRO, false);
	bmi160_set_mode(data, BMI160_ACCEL, false);
}

int bmi160_core_probe(struct device *dev, struct regmap *regmap,
		      const char *name, bool use_spi)
{
	struct iio_dev *indio_dev;
	struct bmi160_data *data;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	dev_set_drvdata(dev, indio_dev);
	data->regmap = regmap;

	ret = bmi160_chip_init(data, use_spi);
	if (ret < 0)
		return ret;

	if (!name && ACPI_HANDLE(dev))
		name = bmi160_match_acpi_device(dev);

	indio_dev->dev.parent = dev;
	indio_dev->channels = bmi160_channels;
	indio_dev->num_channels = ARRAY_SIZE(bmi160_channels);
	indio_dev->name = name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &bmi160_info;

	ret = iio_triggered_buffer_setup(indio_dev, NULL,
					 bmi160_trigger_handler, NULL);
	if (ret < 0)
		goto uninit;

	ret = iio_device_register(indio_dev);
	if (ret < 0)
		goto buffer_cleanup;

	return 0;
buffer_cleanup:
	iio_triggered_buffer_cleanup(indio_dev);
uninit:
	bmi160_chip_uninit(data);
	return ret;
}
EXPORT_SYMBOL_GPL(bmi160_core_probe);

void bmi160_core_remove(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct bmi160_data *data = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	iio_triggered_buffer_cleanup(indio_dev);
	bmi160_chip_uninit(data);
}
EXPORT_SYMBOL_GPL(bmi160_core_remove);

MODULE_AUTHOR("Daniel Baluta <daniel.baluta@intel.com");
MODULE_DESCRIPTION("Bosch BMI160 driver");
MODULE_LICENSE("GPL v2");
