// SPDX-License-Identifier: GPL-2.0-only
/*
 * 3-axis accelerometer driver for MXC4005XC Memsic sensor
 *
 * Copyright (c) 2014, Intel Corporation.
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/mod_devicetable.h>
#include <linux/regmap.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#include <linux/iio/buffer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>

#define MXC4005_DRV_NAME		"mxc4005"
#define MXC4005_IRQ_NAME		"mxc4005_event"
#define MXC4005_REGMAP_NAME		"mxc4005_regmap"

#define MXC4005_REG_XOUT_UPPER		0x03
#define MXC4005_REG_XOUT_LOWER		0x04
#define MXC4005_REG_YOUT_UPPER		0x05
#define MXC4005_REG_YOUT_LOWER		0x06
#define MXC4005_REG_ZOUT_UPPER		0x07
#define MXC4005_REG_ZOUT_LOWER		0x08

#define MXC4005_REG_INT_MASK0		0x0A

#define MXC4005_REG_INT_MASK1		0x0B
#define MXC4005_REG_INT_MASK1_BIT_DRDYE	0x01

#define MXC4005_REG_INT_CLR0		0x00

#define MXC4005_REG_INT_CLR1		0x01
#define MXC4005_REG_INT_CLR1_BIT_DRDYC	0x01
#define MXC4005_REG_INT_CLR1_SW_RST	0x10

#define MXC4005_REG_CONTROL		0x0D
#define MXC4005_REG_CONTROL_MASK_FSR	GENMASK(6, 5)
#define MXC4005_CONTROL_FSR_SHIFT	5

#define MXC4005_REG_DEVICE_ID		0x0E

/* Datasheet does not specify a reset time, this is a conservative guess */
#define MXC4005_RESET_TIME_US		2000

enum mxc4005_axis {
	AXIS_X,
	AXIS_Y,
	AXIS_Z,
};

enum mxc4005_range {
	MXC4005_RANGE_2G,
	MXC4005_RANGE_4G,
	MXC4005_RANGE_8G,
};

struct mxc4005_data {
	struct device *dev;
	struct mutex mutex;
	struct regmap *regmap;
	struct iio_trigger *dready_trig;
	struct iio_mount_matrix orientation;
	/* Ensure timestamp is naturally aligned */
	struct {
		__be16 chans[3];
		s64 timestamp __aligned(8);
	} scan;
	bool trigger_enabled;
	unsigned int control;
	unsigned int int_mask1;
};

/*
 * MXC4005 can operate in the following ranges:
 * +/- 2G, 4G, 8G (the default +/-2G)
 *
 * (2 + 2) * 9.81 / (2^12 - 1) = 0.009582
 * (4 + 4) * 9.81 / (2^12 - 1) = 0.019164
 * (8 + 8) * 9.81 / (2^12 - 1) = 0.038329
 */
static const struct {
	u8 range;
	int scale;
} mxc4005_scale_table[] = {
	{MXC4005_RANGE_2G, 9582},
	{MXC4005_RANGE_4G, 19164},
	{MXC4005_RANGE_8G, 38329},
};


static IIO_CONST_ATTR(in_accel_scale_available, "0.009582 0.019164 0.038329");

static struct attribute *mxc4005_attributes[] = {
	&iio_const_attr_in_accel_scale_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group mxc4005_attrs_group = {
	.attrs = mxc4005_attributes,
};

static bool mxc4005_is_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MXC4005_REG_XOUT_UPPER:
	case MXC4005_REG_XOUT_LOWER:
	case MXC4005_REG_YOUT_UPPER:
	case MXC4005_REG_YOUT_LOWER:
	case MXC4005_REG_ZOUT_UPPER:
	case MXC4005_REG_ZOUT_LOWER:
	case MXC4005_REG_DEVICE_ID:
	case MXC4005_REG_CONTROL:
		return true;
	default:
		return false;
	}
}

static bool mxc4005_is_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MXC4005_REG_INT_CLR0:
	case MXC4005_REG_INT_CLR1:
	case MXC4005_REG_INT_MASK0:
	case MXC4005_REG_INT_MASK1:
	case MXC4005_REG_CONTROL:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config mxc4005_regmap_config = {
	.name = MXC4005_REGMAP_NAME,

	.reg_bits = 8,
	.val_bits = 8,

	.max_register = MXC4005_REG_DEVICE_ID,

	.readable_reg = mxc4005_is_readable_reg,
	.writeable_reg = mxc4005_is_writeable_reg,
};

static int mxc4005_read_xyz(struct mxc4005_data *data)
{
	int ret;

	ret = regmap_bulk_read(data->regmap, MXC4005_REG_XOUT_UPPER,
			       data->scan.chans, sizeof(data->scan.chans));
	if (ret < 0) {
		dev_err(data->dev, "failed to read axes\n");
		return ret;
	}

	return 0;
}

static int mxc4005_read_axis(struct mxc4005_data *data,
			     unsigned int addr)
{
	__be16 reg;
	int ret;

	ret = regmap_bulk_read(data->regmap, addr, &reg, sizeof(reg));
	if (ret < 0) {
		dev_err(data->dev, "failed to read reg %02x\n", addr);
		return ret;
	}

	return be16_to_cpu(reg);
}

static int mxc4005_read_scale(struct mxc4005_data *data)
{
	unsigned int reg;
	int ret;
	int i;

	ret = regmap_read(data->regmap, MXC4005_REG_CONTROL, &reg);
	if (ret < 0) {
		dev_err(data->dev, "failed to read reg_control\n");
		return ret;
	}

	i = reg >> MXC4005_CONTROL_FSR_SHIFT;

	if (i < 0 || i >= ARRAY_SIZE(mxc4005_scale_table))
		return -EINVAL;

	return mxc4005_scale_table[i].scale;
}

static int mxc4005_set_scale(struct mxc4005_data *data, int val)
{
	unsigned int reg;
	int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(mxc4005_scale_table); i++) {
		if (mxc4005_scale_table[i].scale == val) {
			reg = i << MXC4005_CONTROL_FSR_SHIFT;
			ret = regmap_update_bits(data->regmap,
						 MXC4005_REG_CONTROL,
						 MXC4005_REG_CONTROL_MASK_FSR,
						 reg);
			if (ret < 0)
				dev_err(data->dev,
					"failed to write reg_control\n");
			return ret;
		}
	}

	return -EINVAL;
}

static int mxc4005_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	struct mxc4005_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		switch (chan->type) {
		case IIO_ACCEL:
			if (iio_buffer_enabled(indio_dev))
				return -EBUSY;

			ret = mxc4005_read_axis(data, chan->address);
			if (ret < 0)
				return ret;
			*val = sign_extend32(ret >> chan->scan_type.shift,
					     chan->scan_type.realbits - 1);
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SCALE:
		ret = mxc4005_read_scale(data);
		if (ret < 0)
			return ret;

		*val = 0;
		*val2 = ret;
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static int mxc4005_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long mask)
{
	struct mxc4005_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		if (val != 0)
			return -EINVAL;

		return mxc4005_set_scale(data, val2);
	default:
		return -EINVAL;
	}
}

static const struct iio_mount_matrix *
mxc4005_get_mount_matrix(const struct iio_dev *indio_dev,
			   const struct iio_chan_spec *chan)
{
	struct mxc4005_data *data = iio_priv(indio_dev);

	return &data->orientation;
}

static const struct iio_chan_spec_ext_info mxc4005_ext_info[] = {
	IIO_MOUNT_MATRIX(IIO_SHARED_BY_TYPE, mxc4005_get_mount_matrix),
	{ }
};

static const struct iio_info mxc4005_info = {
	.read_raw	= mxc4005_read_raw,
	.write_raw	= mxc4005_write_raw,
	.attrs		= &mxc4005_attrs_group,
};

static const unsigned long mxc4005_scan_masks[] = {
	BIT(AXIS_X) | BIT(AXIS_Y) | BIT(AXIS_Z),
	0
};

#define MXC4005_CHANNEL(_axis, _addr) {				\
	.type = IIO_ACCEL,					\
	.modified = 1,						\
	.channel2 = IIO_MOD_##_axis,				\
	.address = _addr,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
	.scan_index = AXIS_##_axis,				\
	.scan_type = {						\
		.sign = 's',					\
		.realbits = 12,					\
		.storagebits = 16,				\
		.shift = 4,					\
		.endianness = IIO_BE,				\
	},							\
	.ext_info = mxc4005_ext_info,				\
}

static const struct iio_chan_spec mxc4005_channels[] = {
	MXC4005_CHANNEL(X, MXC4005_REG_XOUT_UPPER),
	MXC4005_CHANNEL(Y, MXC4005_REG_YOUT_UPPER),
	MXC4005_CHANNEL(Z, MXC4005_REG_ZOUT_UPPER),
	IIO_CHAN_SOFT_TIMESTAMP(3),
};

static irqreturn_t mxc4005_trigger_handler(int irq, void *private)
{
	struct iio_poll_func *pf = private;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct mxc4005_data *data = iio_priv(indio_dev);
	int ret;

	ret = mxc4005_read_xyz(data);
	if (ret < 0)
		goto err;

	iio_push_to_buffers_with_timestamp(indio_dev, &data->scan,
					   pf->timestamp);

err:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static void mxc4005_clr_intr(struct mxc4005_data *data)
{
	int ret;

	/* clear interrupt */
	ret = regmap_write(data->regmap, MXC4005_REG_INT_CLR1,
			   MXC4005_REG_INT_CLR1_BIT_DRDYC);
	if (ret < 0)
		dev_err(data->dev, "failed to write to reg_int_clr1\n");
}

static int mxc4005_set_trigger_state(struct iio_trigger *trig,
				     bool state)
{
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	struct mxc4005_data *data = iio_priv(indio_dev);
	unsigned int val;
	int ret;

	mutex_lock(&data->mutex);

	val = state ? MXC4005_REG_INT_MASK1_BIT_DRDYE : 0;
	ret = regmap_write(data->regmap, MXC4005_REG_INT_MASK1, val);
	if (ret < 0) {
		mutex_unlock(&data->mutex);
		dev_err(data->dev, "failed to update reg_int_mask1");
		return ret;
	}

	data->int_mask1 = val;
	data->trigger_enabled = state;
	mutex_unlock(&data->mutex);

	return 0;
}

static void mxc4005_trigger_reen(struct iio_trigger *trig)
{
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	struct mxc4005_data *data = iio_priv(indio_dev);

	if (!data->dready_trig)
		return;

	mxc4005_clr_intr(data);
}

static const struct iio_trigger_ops mxc4005_trigger_ops = {
	.set_trigger_state = mxc4005_set_trigger_state,
	.reenable = mxc4005_trigger_reen,
};

static int mxc4005_chip_init(struct mxc4005_data *data)
{
	int ret;
	unsigned int reg;

	ret = regmap_read(data->regmap, MXC4005_REG_DEVICE_ID, &reg);
	if (ret < 0) {
		dev_err(data->dev, "failed to read chip id\n");
		return ret;
	}

	dev_dbg(data->dev, "MXC4005 chip id %02x\n", reg);

	ret = regmap_write(data->regmap, MXC4005_REG_INT_CLR1,
			   MXC4005_REG_INT_CLR1_SW_RST);
	if (ret < 0)
		return dev_err_probe(data->dev, ret, "resetting chip\n");

	fsleep(MXC4005_RESET_TIME_US);

	ret = regmap_write(data->regmap, MXC4005_REG_INT_MASK0, 0);
	if (ret < 0)
		return dev_err_probe(data->dev, ret, "writing INT_MASK0\n");

	ret = regmap_write(data->regmap, MXC4005_REG_INT_MASK1, 0);
	if (ret < 0)
		return dev_err_probe(data->dev, ret, "writing INT_MASK1\n");

	return 0;
}

static int mxc4005_probe(struct i2c_client *client)
{
	struct mxc4005_data *data;
	struct iio_dev *indio_dev;
	struct regmap *regmap;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	regmap = devm_regmap_init_i2c(client, &mxc4005_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "failed to initialize regmap\n");
		return PTR_ERR(regmap);
	}

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->dev = &client->dev;
	data->regmap = regmap;

	ret = mxc4005_chip_init(data);
	if (ret < 0) {
		dev_err(&client->dev, "failed to initialize chip\n");
		return ret;
	}

	mutex_init(&data->mutex);

	if (!iio_read_acpi_mount_matrix(&client->dev, &data->orientation, "ROTM")) {
		ret = iio_read_mount_matrix(&client->dev, &data->orientation);
		if (ret)
			return ret;
	}

	indio_dev->channels = mxc4005_channels;
	indio_dev->num_channels = ARRAY_SIZE(mxc4005_channels);
	indio_dev->available_scan_masks = mxc4005_scan_masks;
	indio_dev->name = MXC4005_DRV_NAME;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &mxc4005_info;

	ret = devm_iio_triggered_buffer_setup(&client->dev, indio_dev,
					 iio_pollfunc_store_time,
					 mxc4005_trigger_handler,
					 NULL);
	if (ret < 0) {
		dev_err(&client->dev,
			"failed to setup iio triggered buffer\n");
		return ret;
	}

	if (client->irq > 0) {
		data->dready_trig = devm_iio_trigger_alloc(&client->dev,
							   "%s-dev%d",
							   indio_dev->name,
							   iio_device_id(indio_dev));
		if (!data->dready_trig)
			return -ENOMEM;

		ret = devm_request_threaded_irq(&client->dev, client->irq,
						iio_trigger_generic_data_rdy_poll,
						NULL,
						IRQF_TRIGGER_FALLING |
						IRQF_ONESHOT,
						MXC4005_IRQ_NAME,
						data->dready_trig);
		if (ret) {
			dev_err(&client->dev,
				"failed to init threaded irq\n");
			return ret;
		}

		data->dready_trig->ops = &mxc4005_trigger_ops;
		iio_trigger_set_drvdata(data->dready_trig, indio_dev);
		ret = devm_iio_trigger_register(&client->dev,
						data->dready_trig);
		if (ret) {
			dev_err(&client->dev,
				"failed to register trigger\n");
			return ret;
		}

		indio_dev->trig = iio_trigger_get(data->dready_trig);
	}

	return devm_iio_device_register(&client->dev, indio_dev);
}

static int mxc4005_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct mxc4005_data *data = iio_priv(indio_dev);
	int ret;

	/* Save control to restore it on resume */
	ret = regmap_read(data->regmap, MXC4005_REG_CONTROL, &data->control);
	if (ret < 0)
		dev_err(data->dev, "failed to read reg_control\n");

	return ret;
}

static int mxc4005_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct mxc4005_data *data = iio_priv(indio_dev);
	int ret;

	ret = regmap_write(data->regmap, MXC4005_REG_INT_CLR1,
			   MXC4005_REG_INT_CLR1_SW_RST);
	if (ret) {
		dev_err(data->dev, "failed to reset chip: %d\n", ret);
		return ret;
	}

	fsleep(MXC4005_RESET_TIME_US);

	ret = regmap_write(data->regmap, MXC4005_REG_CONTROL, data->control);
	if (ret) {
		dev_err(data->dev, "failed to restore control register\n");
		return ret;
	}

	ret = regmap_write(data->regmap, MXC4005_REG_INT_MASK0, 0);
	if (ret) {
		dev_err(data->dev, "failed to restore interrupt 0 mask\n");
		return ret;
	}

	ret = regmap_write(data->regmap, MXC4005_REG_INT_MASK1, data->int_mask1);
	if (ret) {
		dev_err(data->dev, "failed to restore interrupt 1 mask\n");
		return ret;
	}

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(mxc4005_pm_ops, mxc4005_suspend, mxc4005_resume);

static const struct acpi_device_id mxc4005_acpi_match[] = {
	{"MXC4005",	0},
	{"MXC6655",	0},
	{"MDA6655",	0},
	{ },
};
MODULE_DEVICE_TABLE(acpi, mxc4005_acpi_match);

static const struct of_device_id mxc4005_of_match[] = {
	{ .compatible = "memsic,mxc4005", },
	{ .compatible = "memsic,mxc6655", },
	{ },
};
MODULE_DEVICE_TABLE(of, mxc4005_of_match);

static const struct i2c_device_id mxc4005_id[] = {
	{ "mxc4005" },
	{ "mxc6655" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mxc4005_id);

static struct i2c_driver mxc4005_driver = {
	.driver = {
		.name = MXC4005_DRV_NAME,
		.acpi_match_table = mxc4005_acpi_match,
		.of_match_table = mxc4005_of_match,
		.pm = pm_sleep_ptr(&mxc4005_pm_ops),
	},
	.probe		= mxc4005_probe,
	.id_table	= mxc4005_id,
};

module_i2c_driver(mxc4005_driver);

MODULE_AUTHOR("Teodora Baluta <teodora.baluta@intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MXC4005 3-axis accelerometer driver");
