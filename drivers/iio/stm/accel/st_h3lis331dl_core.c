// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_h3lis331dl sensor driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2023 STMicroelectronics Inc.
 */

#include <linux/kernel.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pm.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/version.h>

#include "st_h3lis331dl.h"

static const struct st_h3lis331dl_odr_table_entry st_h3lis331dl_odr_table = {
	.size = ST_H3LIS331DL_ODR_LIST_SIZE,
	.reg = {
		.addr = ST_H3LIS331DL_CTRL_REG1_ADDR,
		.mask = ST_H3LIS331DL_DR_MASK | ST_H3LIS331DL_PM_MASK,
	},
	.odr_avl[0] = {   0, 0x00 },
	.odr_avl[1] = {   1, 0x0c },
	.odr_avl[2] = {   2, 0x10 },
	.odr_avl[3] = {   5, 0x14 },
	.odr_avl[4] = {  10, 0x18 },
	.odr_avl[5] = {  50, 0x04 },
	.odr_avl[6] = { 100, 0x05 },
	.odr_avl[7] = { 400, 0x06 },
};

static const struct st_h3lis331dl_fs_table_entry st_h3lis331dl_fs_table = {
	.size = ST_H3LIS331DL_FS_LIST_SIZE,
	.reg = {
		.addr = ST_H3LIS331DL_CTRL_REG4_ADDR,
		.mask = ST_H3LIS331DL_FS_MASK,
	},
	.fs_avl[0] = {
		.gain = ST_H3LIS331DL_ACC_FS_100G_GAIN,
		.val = 0x0,
	},
	.fs_avl[1] = {
		.gain = ST_H3LIS331DL_ACC_FS_200G_GAIN,
		.val = 0x1,
	},
	.fs_avl[2] = {
		.gain = ST_H3LIS331DL_ACC_FS_400G_GAIN,
		.val = 0x2,
	},
};

static const inline struct iio_mount_matrix *
st_h3lis331dl_get_mount_matrix(const struct iio_dev *iio_dev,
			      const struct iio_chan_spec *chan)
{
	struct st_h3lis331dl_sensor *sensor = iio_priv(iio_dev);
	struct st_h3lis331dl_hw *hw = sensor->hw;

	return &hw->orientation;
}

static const struct iio_chan_spec_ext_info st_h3lis331dl_ext_info[] = {
	IIO_MOUNT_MATRIX(IIO_SHARED_BY_ALL, st_h3lis331dl_get_mount_matrix),
	{},
};

static const struct iio_chan_spec st_h3lis331dl_acc_channels[] = {
	ST_H3LIS331DL_DATA_CHANNEL(IIO_ACCEL, ST_H3LIS331DL_REG_OUTX_L_ADDR,
				1, IIO_MOD_X, 0, 12, 16, 's', st_h3lis331dl_ext_info),
	ST_H3LIS331DL_DATA_CHANNEL(IIO_ACCEL, ST_H3LIS331DL_REG_OUTY_L_ADDR,
				1, IIO_MOD_Y, 1, 12, 16, 's', st_h3lis331dl_ext_info),
	ST_H3LIS331DL_DATA_CHANNEL(IIO_ACCEL, ST_H3LIS331DL_REG_OUTZ_L_ADDR,
				1, IIO_MOD_Z, 2, 12, 16, 's', st_h3lis331dl_ext_info),
	IIO_CHAN_SOFT_TIMESTAMP(3),
};

static __maybe_unused int st_h3lis331dl_reg_access(struct iio_dev *iio_dev,
				 unsigned int reg, unsigned int writeval,
				 unsigned int *readval)
{
	struct st_h3lis331dl_sensor *sensor = iio_priv(iio_dev);
	int ret;

	ret = iio_device_claim_direct_mode(iio_dev);
	if (ret)
		return ret;

	if (readval == NULL)
		ret = regmap_write(sensor->hw->regmap, reg, writeval);
	else
		ret = regmap_read(sensor->hw->regmap, reg, readval);

	iio_device_release_direct_mode(iio_dev);

	return (ret < 0) ? ret : 0;
}

static int st_h3lis331dl_check_whoami(struct st_h3lis331dl_hw *hw,
				      int id)
{
	int err, data;

	err = regmap_read(hw->regmap, ST_H3LIS331DL_REG_WHO_AM_I_ADDR, &data);
	if (err < 0) {
		dev_err(hw->dev, "failed to read whoami register\n");

		return err;
	}

	if (data != ST_H3LIS331DL_WHO_AM_I_VAL) {
		dev_err(hw->dev, "unsupported whoami [%02x]\n", data);

		return -ENODEV;
	}

	return 0;
}

static int st_h3lis331dl_set_full_scale(struct st_h3lis331dl_sensor *sensor,
					u32 gain)
{
	struct st_h3lis331dl_hw *hw = sensor->hw;
	int i, err;

	for (i = 0; i < st_h3lis331dl_fs_table.size; i++)
		if (st_h3lis331dl_fs_table.fs_avl[i].gain >= gain)
			break;

	if (i == st_h3lis331dl_fs_table.size)
		return -EINVAL;

	err = regmap_update_bits(hw->regmap,
			st_h3lis331dl_fs_table.reg.addr,
			st_h3lis331dl_fs_table.reg.mask,
			ST_H3LIS331DL_SHIFT_VAL(st_h3lis331dl_fs_table.fs_avl[i].val,
					      st_h3lis331dl_fs_table.reg.mask));
	if (err < 0)
		return err;

	sensor->gain = st_h3lis331dl_fs_table.fs_avl[i].gain;

	return 0;
}

int st_h3lis331dl_get_odr_val(int odr, u8 *val)
{
	int i;

	for (i = 0; i < st_h3lis331dl_odr_table.size; i++) {
		if (st_h3lis331dl_odr_table.odr_avl[i].hz >= odr)
			break;
	}

	if (i == st_h3lis331dl_odr_table.size)
		return -EINVAL;

	*val = st_h3lis331dl_odr_table.odr_avl[i].val;

	return st_h3lis331dl_odr_table.odr_avl[i].hz;
}

static int st_h3lis331dl_set_odr(struct st_h3lis331dl_sensor *sensor, int odr)
{
	struct st_h3lis331dl_hw *hw = sensor->hw;
	u8 val = 0;
	int ret;

	ret = st_h3lis331dl_get_odr_val(odr, &val);
	if (ret < 0)
		return ret;

	return regmap_update_bits(hw->regmap,
			st_h3lis331dl_odr_table.reg.addr,
			st_h3lis331dl_odr_table.reg.mask,
			ST_H3LIS331DL_SHIFT_VAL(val,
					     st_h3lis331dl_odr_table.reg.mask));
}

int st_h3lis331dl_sensor_set_enable(struct st_h3lis331dl_sensor *sensor,
				    bool enable)
{
	int odr = enable ? sensor->odr : 0;
	int err;

	err = st_h3lis331dl_set_odr(sensor, odr);
	if (err < 0)
		return err;

	if (enable)
		sensor->hw->enable_mask = BIT_ULL(sensor->id);
	else
		sensor->hw->enable_mask &= ~BIT_ULL(sensor->id);

	return 0;
}

static int st_h3lis331dl_read_oneshot(struct st_h3lis331dl_sensor *sensor,
				   u8 addr, int *val)
{
	struct st_h3lis331dl_hw *hw = sensor->hw;
	int err, delay;
	__le16 data;

	err = st_h3lis331dl_sensor_set_enable(sensor, true);
	if (err < 0)
		return err;

	/* use 10 odrs delay for data valid */
	delay = 10000000 / sensor->odr;
	usleep_range(delay, 2 * delay);

	err = regmap_bulk_read(hw->regmap, addr,
			       &data, sizeof(data));
	if (err < 0)
		return err;

	err = st_h3lis331dl_sensor_set_enable(sensor, false);

	*val = (s16)le16_to_cpu(data);

	return IIO_VAL_INT;
}

static int st_h3lis331dl_read_raw(struct iio_dev *iio_dev,
				  struct iio_chan_spec const *ch,
				  int *val, int *val2, long mask)
{
	struct st_h3lis331dl_sensor *sensor = iio_priv(iio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = iio_device_claim_direct_mode(iio_dev);
		if (ret)
			break;

		ret = st_h3lis331dl_read_oneshot(sensor, ch->address, val);
		iio_device_release_direct_mode(iio_dev);
		break;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = (int)sensor->odr;
		ret = IIO_VAL_INT;
		break;
	case IIO_CHAN_INFO_SCALE:
		switch (ch->type) {
		case IIO_ACCEL:
			*val = 0;
			*val2 = sensor->gain;
			ret = IIO_VAL_INT_PLUS_NANO;
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int st_h3lis331dl_write_raw(struct iio_dev *iio_dev,
				   struct iio_chan_spec const *chan,
				   int val, int val2, long mask)
{
	struct st_h3lis331dl_sensor *sensor = iio_priv(iio_dev);
	int err;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		err = iio_device_claim_direct_mode(iio_dev);
		if (err)
			return err;

		err = st_h3lis331dl_set_full_scale(sensor, val2);
		iio_device_release_direct_mode(iio_dev);
		break;
	case IIO_CHAN_INFO_SAMP_FREQ: {
		int todr;
		u8 data;

		todr = st_h3lis331dl_get_odr_val(val, &data);
		if (todr < 0)
			return todr;

		sensor->odr = todr;

		if (sensor->hw->enable_mask & BIT(sensor->id)) {
			err = st_h3lis331dl_set_odr(sensor, sensor->odr);
			if (err < 0)
				return err;
		}
		break;
	}
	default:
		err = -EINVAL;
		break;
	}

	return err;
}

static ssize_t
st_h3lis331dl_sysfs_sampling_freq_avail(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	int i, len = 0;

	for (i = 1; i < st_h3lis331dl_odr_table.size; i++) {
		len += scnprintf(buf + len, PAGE_SIZE - len, "%d ",
				 st_h3lis331dl_odr_table.odr_avl[i].hz);
	}

	buf[len - 1] = '\n';

	return len;
}

static ssize_t st_h3lis331dl_sysfs_scale_avail(struct device *dev,
					       struct device_attribute *attr,
					       char *buf)
{
	int i, len = 0;

	for (i = 0; i < st_h3lis331dl_fs_table.size; i++)
		len += scnprintf(buf + len, PAGE_SIZE - len, "0.%09u ",
				 st_h3lis331dl_fs_table.fs_avl[i].gain);
	buf[len - 1] = '\n';

	return len;
}

static IIO_DEV_ATTR_SAMP_FREQ_AVAIL(st_h3lis331dl_sysfs_sampling_freq_avail);
static IIO_DEVICE_ATTR(in_accel_scale_available, 0444,
		       st_h3lis331dl_sysfs_scale_avail, NULL, 0);

static int st_h3lis331dl_write_raw_get_fmt(struct iio_dev *indio_dev,
					   struct iio_chan_spec const *chan,
					   long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		if (chan->type == IIO_ACCEL)
			return IIO_VAL_INT_PLUS_NANO;
		break;
	case IIO_CHAN_INFO_SAMP_FREQ:
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}

	return -EINVAL;
}

static struct attribute *st_h3lis331dl_acc_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_in_accel_scale_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_h3lis331dl_acc_attribute_group = {
	.attrs = st_h3lis331dl_acc_attributes,
};

static const struct iio_info st_h3lis331dl_acc_info = {
	.attrs = &st_h3lis331dl_acc_attribute_group,
	.read_raw = st_h3lis331dl_read_raw,
	.write_raw = st_h3lis331dl_write_raw,
	.write_raw_get_fmt = &st_h3lis331dl_write_raw_get_fmt,
#ifdef CONFIG_DEBUG_FS
	.debugfs_reg_access = &st_h3lis331dl_reg_access,
#endif /* CONFIG_DEBUG_FS */
};

static const unsigned long st_h3lis331dl_available_scan_masks[] = {
	GENMASK(3, 0), 0x0
};

static int st_h3lis331dl_reset_device(struct st_h3lis331dl_hw *hw)
{
	int err;

	/* re-boot sensor */
	err = regmap_update_bits(hw->regmap,
				 ST_H3LIS331DL_CTRL_REG2_ADDR,
				 ST_H3LIS331DL_BOOT_MASK,
				 FIELD_PREP(ST_H3LIS331DL_BOOT_MASK, 1));
	if (err < 0)
		return err;

	usleep_range(5000, 5100);

	return 0;
}

static int st_h3lis331dl_init_device(struct st_h3lis331dl_hw *hw)
{
	/* enable Block Data Update */
	return regmap_update_bits(hw->regmap, ST_H3LIS331DL_CTRL_REG4_ADDR,
				  ST_H3LIS331DL_BDU_MASK,
				  FIELD_PREP(ST_H3LIS331DL_BDU_MASK, 1));
}

static struct iio_dev *st_h3lis331dl_alloc_iiodev(struct st_h3lis331dl_hw *hw)
{
	struct st_h3lis331dl_sensor *sensor;
	struct iio_dev *iio_dev;

	iio_dev = devm_iio_device_alloc(hw->dev, sizeof(*sensor));
	if (!iio_dev)
		return NULL;

	iio_dev->modes = INDIO_DIRECT_MODE;
	iio_dev->dev.parent = hw->dev;

	sensor = iio_priv(iio_dev);
	sensor->id = ST_H3LIS331DL_ACC_SENSOR_ID;
	sensor->hw = hw;

	sensor->decimator = 0;
	sensor->dec_counter = 0;

	iio_dev->channels = st_h3lis331dl_acc_channels;
	iio_dev->num_channels = ARRAY_SIZE(st_h3lis331dl_acc_channels);
	sprintf(sensor->name, "h3lis331dl_accel");
	iio_dev->info = &st_h3lis331dl_acc_info;
	iio_dev->available_scan_masks = st_h3lis331dl_available_scan_masks;
	sensor->gain = st_h3lis331dl_fs_table.fs_avl[0].gain;
	sensor->odr = st_h3lis331dl_odr_table.odr_avl[0].hz;

	st_h3lis331dl_set_full_scale(sensor, sensor->gain);
	iio_dev->name = sensor->name;

	return iio_dev;
}

static void st_h3lis331dl_disable_regulator_action(void *_data)
{
	struct st_h3lis331dl_hw *hw = _data;

	regulator_disable(hw->vddio_supply);
	regulator_disable(hw->vdd_supply);
}

static int st_h3lis331dl_power_enable(struct st_h3lis331dl_hw *hw)
{
	int err;

	hw->vdd_supply = devm_regulator_get(hw->dev, "vdd");
	if (IS_ERR(hw->vdd_supply)) {
		if (PTR_ERR(hw->vdd_supply) != -EPROBE_DEFER)
			dev_err(hw->dev, "Failed to get vdd regulator %d\n",
				(int)PTR_ERR(hw->vdd_supply));

		return PTR_ERR(hw->vdd_supply);
	}

	hw->vddio_supply = devm_regulator_get(hw->dev, "vddio");
	if (IS_ERR(hw->vddio_supply)) {
		if (PTR_ERR(hw->vddio_supply) != -EPROBE_DEFER)
			dev_err(hw->dev, "Failed to get vddio regulator %d\n",
				(int)PTR_ERR(hw->vddio_supply));

		return PTR_ERR(hw->vddio_supply);
	}

	err = regulator_enable(hw->vdd_supply);
	if (err) {
		dev_err(hw->dev, "Failed to enable vdd regulator: %d\n", err);
		return err;
	}

	err = regulator_enable(hw->vddio_supply);
	if (err) {
		regulator_disable(hw->vdd_supply);
		return err;
	}

	err = devm_add_action_or_reset(hw->dev,
				       st_h3lis331dl_disable_regulator_action,
				       hw);
	if (err) {
		dev_err(hw->dev,
			"Failed to setup regulator cleanup action %d\n", err);
		return err;
	}

	return 0;
}

int st_h3lis331dl_probe(struct device *dev, int irq, int hw_id,
			struct regmap *regmap)
{
	struct st_h3lis331dl_hw *hw;
	int err;

	hw = devm_kzalloc(dev, sizeof(*hw), GFP_KERNEL);
	if (!hw)
		return -ENOMEM;

	dev_set_drvdata(dev, (void *)hw);

	mutex_init(&hw->lock);

	hw->regmap = regmap;
	hw->dev = dev;
	hw->irq = irq;

	err = st_h3lis331dl_power_enable(hw);
	if (err != 0)
		return err;

	err = st_h3lis331dl_check_whoami(hw, hw_id);
	if (err < 0)
		return err;

	err = st_h3lis331dl_reset_device(hw);
	if (err < 0)
		return err;

	err = st_h3lis331dl_init_device(hw);
	if (err < 0)
		return err;

#if KERNEL_VERSION(5, 15, 0) <= LINUX_VERSION_CODE
	err = iio_read_mount_matrix(hw->dev, &hw->orientation);
#elif KERNEL_VERSION(5, 2, 0) <= LINUX_VERSION_CODE
	err = iio_read_mount_matrix(hw->dev, "mount-matrix", &hw->orientation);
#else /* LINUX_VERSION_CODE */
	err = of_iio_read_mount_matrix(hw->dev, "mount-matrix",
				       &hw->orientation);
#endif /* LINUX_VERSION_CODE */

	if (err) {
		dev_err(dev, "Failed to retrieve mounting matrix %d\n", err);

		return err;
	}

	hw->iio_devs = st_h3lis331dl_alloc_iiodev(hw);
	if (!hw->iio_devs)
		return -ENOMEM;

	err = st_h3lis331dl_allocate_triggered_buffer(hw);
	if (err < 0)
		return err;

	if (hw->irq) {
		err = st_h3lis331dl_trigger_setup(hw);
		if (err < 0)
			return err;
	}

	err = devm_iio_device_register(hw->dev, hw->iio_devs);
	if (err)
		return err;

	if (device_may_wakeup(dev))
		device_init_wakeup(dev, 1);

	return 0;
}
EXPORT_SYMBOL(st_h3lis331dl_probe);

static int __maybe_unused st_h3lis331dl_suspend(struct device *dev)
{
	struct st_h3lis331dl_hw *hw = dev_get_drvdata(dev);
	struct st_h3lis331dl_sensor *sensor;
	int err = 0;

	sensor = iio_priv(hw->iio_devs);
	if (!(hw->enable_mask & BIT_ULL(sensor->id)))
		return 0;

	err = st_h3lis331dl_set_odr(sensor, 0);
	if (err < 0)
		return err;

	if (device_may_wakeup(dev))
		enable_irq_wake(hw->irq);

	return err < 0 ? err : 0;
}

static int __maybe_unused st_h3lis331dl_resume(struct device *dev)
{
	struct st_h3lis331dl_hw *hw = dev_get_drvdata(dev);
	struct st_h3lis331dl_sensor *sensor;

	if (device_may_wakeup(dev))
		disable_irq_wake(hw->irq);

	sensor = iio_priv(hw->iio_devs);
	if (!(hw->enable_mask & BIT_ULL(sensor->id)))
		return 0;

	return st_h3lis331dl_set_odr(sensor, sensor->odr);
}

const struct dev_pm_ops st_h3lis331dl_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(st_h3lis331dl_suspend, st_h3lis331dl_resume)
};
EXPORT_SYMBOL(st_h3lis331dl_pm_ops);

MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_DESCRIPTION("STMicroelectronics st_h3lis331dl driver");
MODULE_LICENSE("GPL v2");
