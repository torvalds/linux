/*
 * STMicroelectronics st_lsm6dsx i2c controller driver
 *
 * i2c controller embedded in lsm6dx series can connect up to four
 * slave devices using accelerometer sensor as trigger for i2c
 * read/write operations. Current implementation relies on SLV0 channel
 * for slave configuration and SLV{1,2,3} to read data and push them into
 * the hw FIFO
 *
 * Copyright (C) 2018 Lorenzo Bianconi <lorenzo.bianconi83@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/bitfield.h>

#include "st_lsm6dsx.h"

#define ST_LSM6DSX_MAX_SLV_NUM			3
#define ST_LSM6DSX_SLV_ADDR(n, base)		((base) + (n) * 3)
#define ST_LSM6DSX_SLV_SUB_ADDR(n, base)	((base) + 1 + (n) * 3)
#define ST_LSM6DSX_SLV_CONFIG(n, base)		((base) + 2 + (n) * 3)

#define ST_LS6DSX_READ_OP_MASK			GENMASK(2, 0)

static const struct st_lsm6dsx_ext_dev_settings st_lsm6dsx_ext_dev_table[] = {
	/* LIS2MDL */
	{
		.i2c_addr = { 0x1e },
		.wai = {
			.addr = 0x4f,
			.val = 0x40,
		},
		.id = ST_LSM6DSX_ID_MAGN,
		.odr_table = {
			.reg = {
				.addr = 0x60,
				.mask = GENMASK(3, 2),
			},
			.odr_avl[0] = {  10, 0x0 },
			.odr_avl[1] = {  20, 0x1 },
			.odr_avl[2] = {  50, 0x2 },
			.odr_avl[3] = { 100, 0x3 },
		},
		.fs_table = {
			.fs_avl[0] = {
				.gain = 1500,
				.val = 0x0,
			}, /* 1500 uG/LSB */
		},
		.temp_comp = {
			.addr = 0x60,
			.mask = BIT(7),
		},
		.pwr_table = {
			.reg = {
				.addr = 0x60,
				.mask = GENMASK(1, 0),
			},
			.off_val = 0x2,
			.on_val = 0x0,
		},
		.off_canc = {
			.addr = 0x61,
			.mask = BIT(1),
		},
		.bdu = {
			.addr = 0x62,
			.mask = BIT(4),
		},
		.out = {
			.addr = 0x68,
			.len = 6,
		},
	},
};

static void st_lsm6dsx_shub_wait_complete(struct st_lsm6dsx_hw *hw)
{
	struct st_lsm6dsx_sensor *sensor;

	sensor = iio_priv(hw->iio_devs[ST_LSM6DSX_ID_ACC]);
	msleep((2000U / sensor->odr) + 1);
}

/**
 * st_lsm6dsx_shub_read_reg - read i2c controller register
 *
 * Read st_lsm6dsx i2c controller register
 */
static int st_lsm6dsx_shub_read_reg(struct st_lsm6dsx_hw *hw, u8 addr,
				    u8 *data, int len)
{
	int err;

	mutex_lock(&hw->page_lock);

	err = st_lsm6dsx_set_page(hw, true);
	if (err < 0)
		goto out;

	err = regmap_bulk_read(hw->regmap, addr, data, len);

	st_lsm6dsx_set_page(hw, false);
out:
	mutex_unlock(&hw->page_lock);

	return err;
}

/**
 * st_lsm6dsx_shub_write_reg - write i2c controller register
 *
 * Write st_lsm6dsx i2c controller register
 */
static int st_lsm6dsx_shub_write_reg(struct st_lsm6dsx_hw *hw, u8 addr,
				     u8 *data, int len)
{
	int err;

	mutex_lock(&hw->page_lock);
	err = st_lsm6dsx_set_page(hw, true);
	if (err < 0)
		goto out;

	err = regmap_bulk_write(hw->regmap, addr, data, len);

	st_lsm6dsx_set_page(hw, false);
out:
	mutex_unlock(&hw->page_lock);

	return err;
}

static int
st_lsm6dsx_shub_write_reg_with_mask(struct st_lsm6dsx_hw *hw, u8 addr,
				    u8 mask, u8 val)
{
	int err;

	mutex_lock(&hw->page_lock);
	err = st_lsm6dsx_set_page(hw, true);
	if (err < 0)
		goto out;

	err = regmap_update_bits(hw->regmap, addr, mask, val);

	st_lsm6dsx_set_page(hw, false);
out:
	mutex_unlock(&hw->page_lock);

	return err;
}

static int st_lsm6dsx_shub_master_enable(struct st_lsm6dsx_sensor *sensor,
					 bool enable)
{
	const struct st_lsm6dsx_shub_settings *hub_settings;
	struct st_lsm6dsx_hw *hw = sensor->hw;
	unsigned int data;
	int err;

	/* enable acc sensor as trigger */
	err = st_lsm6dsx_sensor_set_enable(sensor, enable);
	if (err < 0)
		return err;

	mutex_lock(&hw->page_lock);

	hub_settings = &hw->settings->shub_settings;
	err = st_lsm6dsx_set_page(hw, true);
	if (err < 0)
		goto out;

	data = ST_LSM6DSX_SHIFT_VAL(enable, hub_settings->master_en.mask);
	err = regmap_update_bits(hw->regmap, hub_settings->master_en.addr,
				 hub_settings->master_en.mask, data);

	st_lsm6dsx_set_page(hw, false);
out:
	mutex_unlock(&hw->page_lock);

	return err;
}

/**
 * st_lsm6dsx_shub_read - read data from slave device register
 *
 * Read data from slave device register. SLV0 is used for
 * one-shot read operation
 */
static int
st_lsm6dsx_shub_read(struct st_lsm6dsx_sensor *sensor, u8 addr,
		     u8 *data, int len)
{
	const struct st_lsm6dsx_shub_settings *hub_settings;
	struct st_lsm6dsx_hw *hw = sensor->hw;
	u8 config[3], slv_addr;
	int err;

	hub_settings = &hw->settings->shub_settings;
	slv_addr = ST_LSM6DSX_SLV_ADDR(0, hub_settings->slv0_addr);

	config[0] = (sensor->ext_info.addr << 1) | 1;
	config[1] = addr;
	config[2] = len & ST_LS6DSX_READ_OP_MASK;

	err = st_lsm6dsx_shub_write_reg(hw, slv_addr, config,
					sizeof(config));
	if (err < 0)
		return err;

	err = st_lsm6dsx_shub_master_enable(sensor, true);
	if (err < 0)
		return err;

	st_lsm6dsx_shub_wait_complete(hw);

	err = st_lsm6dsx_shub_read_reg(hw, hub_settings->shub_out, data,
				       len & ST_LS6DSX_READ_OP_MASK);

	st_lsm6dsx_shub_master_enable(sensor, false);

	memset(config, 0, sizeof(config));
	return st_lsm6dsx_shub_write_reg(hw, slv_addr, config,
					 sizeof(config));
}

/**
 * st_lsm6dsx_shub_write - write data to slave device register
 *
 * Write data from slave device register. SLV0 is used for
 * one-shot write operation
 */
static int
st_lsm6dsx_shub_write(struct st_lsm6dsx_sensor *sensor, u8 addr,
		      u8 *data, int len)
{
	const struct st_lsm6dsx_shub_settings *hub_settings;
	struct st_lsm6dsx_hw *hw = sensor->hw;
	u8 config[2], slv_addr;
	int err, i;

	hub_settings = &hw->settings->shub_settings;
	if (hub_settings->wr_once.addr) {
		unsigned int data;

		data = ST_LSM6DSX_SHIFT_VAL(1, hub_settings->wr_once.mask);
		err = st_lsm6dsx_shub_write_reg_with_mask(hw,
			hub_settings->wr_once.addr,
			hub_settings->wr_once.mask,
			data);
		if (err < 0)
			return err;
	}

	slv_addr = ST_LSM6DSX_SLV_ADDR(0, hub_settings->slv0_addr);
	config[0] = sensor->ext_info.addr << 1;
	for (i = 0 ; i < len; i++) {
		config[1] = addr + i;

		err = st_lsm6dsx_shub_write_reg(hw, slv_addr, config,
						sizeof(config));
		if (err < 0)
			return err;

		err = st_lsm6dsx_shub_write_reg(hw, hub_settings->dw_slv0_addr,
						&data[i], 1);
		if (err < 0)
			return err;

		err = st_lsm6dsx_shub_master_enable(sensor, true);
		if (err < 0)
			return err;

		st_lsm6dsx_shub_wait_complete(hw);

		st_lsm6dsx_shub_master_enable(sensor, false);
	}

	memset(config, 0, sizeof(config));
	return st_lsm6dsx_shub_write_reg(hw, slv_addr, config, sizeof(config));
}

static int
st_lsm6dsx_shub_write_with_mask(struct st_lsm6dsx_sensor *sensor,
				u8 addr, u8 mask, u8 val)
{
	int err;
	u8 data;

	err = st_lsm6dsx_shub_read(sensor, addr, &data, sizeof(data));
	if (err < 0)
		return err;

	data = ((data & ~mask) | (val << __ffs(mask) & mask));

	return st_lsm6dsx_shub_write(sensor, addr, &data, sizeof(data));
}

static int
st_lsm6dsx_shub_get_odr_val(struct st_lsm6dsx_sensor *sensor,
			    u16 odr, u16 *val)
{
	const struct st_lsm6dsx_ext_dev_settings *settings;
	int i;

	settings = sensor->ext_info.settings;
	for (i = 0; i < ST_LSM6DSX_ODR_LIST_SIZE; i++)
		if (settings->odr_table.odr_avl[i].hz == odr)
			break;

	if (i == ST_LSM6DSX_ODR_LIST_SIZE)
		return -EINVAL;

	*val = settings->odr_table.odr_avl[i].val;
	return 0;
}

static int
st_lsm6dsx_shub_set_odr(struct st_lsm6dsx_sensor *sensor, u16 odr)
{
	const struct st_lsm6dsx_ext_dev_settings *settings;
	u16 val;
	int err;

	err = st_lsm6dsx_shub_get_odr_val(sensor, odr, &val);
	if (err < 0)
		return err;

	settings = sensor->ext_info.settings;
	return st_lsm6dsx_shub_write_with_mask(sensor,
					       settings->odr_table.reg.addr,
					       settings->odr_table.reg.mask,
					       val);
}

/* use SLV{1,2,3} for FIFO read operations */
static int
st_lsm6dsx_shub_config_channels(struct st_lsm6dsx_sensor *sensor,
				bool enable)
{
	const struct st_lsm6dsx_shub_settings *hub_settings;
	const struct st_lsm6dsx_ext_dev_settings *settings;
	u8 config[9] = {}, enable_mask, slv_addr;
	struct st_lsm6dsx_hw *hw = sensor->hw;
	struct st_lsm6dsx_sensor *cur_sensor;
	int i, j = 0;

	hub_settings = &hw->settings->shub_settings;
	if (enable)
		enable_mask = hw->enable_mask | BIT(sensor->id);
	else
		enable_mask = hw->enable_mask & ~BIT(sensor->id);

	for (i = ST_LSM6DSX_ID_EXT0; i <= ST_LSM6DSX_ID_EXT2; i++) {
		if (!hw->iio_devs[i])
			continue;

		cur_sensor = iio_priv(hw->iio_devs[i]);
		if (!(enable_mask & BIT(cur_sensor->id)))
			continue;

		settings = cur_sensor->ext_info.settings;
		config[j] = (sensor->ext_info.addr << 1) | 1;
		config[j + 1] = settings->out.addr;
		config[j + 2] = (settings->out.len & ST_LS6DSX_READ_OP_MASK) |
				hub_settings->batch_en;
		j += 3;
	}

	slv_addr = ST_LSM6DSX_SLV_ADDR(1, hub_settings->slv0_addr);
	return st_lsm6dsx_shub_write_reg(hw, slv_addr, config,
					 sizeof(config));
}

int st_lsm6dsx_shub_set_enable(struct st_lsm6dsx_sensor *sensor, bool enable)
{
	const struct st_lsm6dsx_ext_dev_settings *settings;
	int err;

	err = st_lsm6dsx_shub_config_channels(sensor, enable);
	if (err < 0)
		return err;

	settings = sensor->ext_info.settings;
	if (enable) {
		err = st_lsm6dsx_shub_set_odr(sensor, sensor->odr);
		if (err < 0)
			return err;
	} else {
		err = st_lsm6dsx_shub_write_with_mask(sensor,
					settings->odr_table.reg.addr,
					settings->odr_table.reg.mask, 0);
		if (err < 0)
			return err;
	}

	if (settings->pwr_table.reg.addr) {
		u8 val;

		val = enable ? settings->pwr_table.on_val
			     : settings->pwr_table.off_val;
		err = st_lsm6dsx_shub_write_with_mask(sensor,
					settings->pwr_table.reg.addr,
					settings->pwr_table.reg.mask, val);
		if (err < 0)
			return err;
	}

	return st_lsm6dsx_shub_master_enable(sensor, enable);
}

static int
st_lsm6dsx_shub_read_oneshot(struct st_lsm6dsx_sensor *sensor,
			     struct iio_chan_spec const *ch,
			     int *val)
{
	int err, delay, len;
	u8 data[4];

	err = st_lsm6dsx_shub_set_enable(sensor, true);
	if (err < 0)
		return err;

	delay = 1000000 / sensor->odr;
	usleep_range(delay, 2 * delay);

	len = min_t(int, sizeof(data), ch->scan_type.realbits >> 3);
	err = st_lsm6dsx_shub_read(sensor, ch->address, data, len);

	st_lsm6dsx_shub_set_enable(sensor, false);

	if (err < 0)
		return err;

	switch (len) {
	case 2:
		*val = (s16)le16_to_cpu(*((__le16 *)data));
		break;
	default:
		return -EINVAL;
	}

	return IIO_VAL_INT;
}

static int
st_lsm6dsx_shub_read_raw(struct iio_dev *iio_dev,
			 struct iio_chan_spec const *ch,
			 int *val, int *val2, long mask)
{
	struct st_lsm6dsx_sensor *sensor = iio_priv(iio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = iio_device_claim_direct_mode(iio_dev);
		if (ret)
			break;

		ret = st_lsm6dsx_shub_read_oneshot(sensor, ch, val);
		iio_device_release_direct_mode(iio_dev);
		break;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = sensor->odr;
		ret = IIO_VAL_INT;
		break;
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		*val2 = sensor->gain;
		ret = IIO_VAL_INT_PLUS_MICRO;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int
st_lsm6dsx_shub_write_raw(struct iio_dev *iio_dev,
			  struct iio_chan_spec const *chan,
			  int val, int val2, long mask)
{
	struct st_lsm6dsx_sensor *sensor = iio_priv(iio_dev);
	int err;

	err = iio_device_claim_direct_mode(iio_dev);
	if (err)
		return err;

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ: {
		u16 data;

		err = st_lsm6dsx_shub_get_odr_val(sensor, val, &data);
		if (!err)
			sensor->odr = val;
		break;
	}
	default:
		err = -EINVAL;
		break;
	}

	iio_device_release_direct_mode(iio_dev);

	return err;
}

static ssize_t
st_lsm6dsx_shub_sampling_freq_avail(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct st_lsm6dsx_sensor *sensor = iio_priv(dev_get_drvdata(dev));
	const struct st_lsm6dsx_ext_dev_settings *settings;
	int i, len = 0;

	settings = sensor->ext_info.settings;
	for (i = 0; i < ST_LSM6DSX_ODR_LIST_SIZE; i++) {
		u16 val = settings->odr_table.odr_avl[i].hz;

		if (val > 0)
			len += scnprintf(buf + len, PAGE_SIZE - len, "%d ",
					 val);
	}
	buf[len - 1] = '\n';

	return len;
}

static ssize_t st_lsm6dsx_shub_scale_avail(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct st_lsm6dsx_sensor *sensor = iio_priv(dev_get_drvdata(dev));
	const struct st_lsm6dsx_ext_dev_settings *settings;
	int i, len = 0;

	settings = sensor->ext_info.settings;
	for (i = 0; i < ST_LSM6DSX_FS_LIST_SIZE; i++) {
		u16 val = settings->fs_table.fs_avl[i].gain;

		if (val > 0)
			len += scnprintf(buf + len, PAGE_SIZE - len, "0.%06u ",
					 val);
	}
	buf[len - 1] = '\n';

	return len;
}

static IIO_DEV_ATTR_SAMP_FREQ_AVAIL(st_lsm6dsx_shub_sampling_freq_avail);
static IIO_DEVICE_ATTR(in_scale_available, 0444,
		       st_lsm6dsx_shub_scale_avail, NULL, 0);
static struct attribute *st_lsm6dsx_ext_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_in_scale_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_lsm6dsx_ext_attribute_group = {
	.attrs = st_lsm6dsx_ext_attributes,
};

static const struct iio_info st_lsm6dsx_ext_info = {
	.attrs = &st_lsm6dsx_ext_attribute_group,
	.read_raw = st_lsm6dsx_shub_read_raw,
	.write_raw = st_lsm6dsx_shub_write_raw,
	.hwfifo_set_watermark = st_lsm6dsx_set_watermark,
};

static struct iio_dev *
st_lsm6dsx_shub_alloc_iiodev(struct st_lsm6dsx_hw *hw,
			     enum st_lsm6dsx_sensor_id id,
			     const struct st_lsm6dsx_ext_dev_settings *info,
			     u8 i2c_addr, const char *name)
{
	struct iio_chan_spec *ext_channels;
	struct st_lsm6dsx_sensor *sensor;
	struct iio_dev *iio_dev;

	iio_dev = devm_iio_device_alloc(hw->dev, sizeof(*sensor));
	if (!iio_dev)
		return NULL;

	iio_dev->modes = INDIO_DIRECT_MODE;
	iio_dev->dev.parent = hw->dev;
	iio_dev->info = &st_lsm6dsx_ext_info;

	sensor = iio_priv(iio_dev);
	sensor->id = id;
	sensor->hw = hw;
	sensor->odr = info->odr_table.odr_avl[0].hz;
	sensor->gain = info->fs_table.fs_avl[0].gain;
	sensor->ext_info.settings = info;
	sensor->ext_info.addr = i2c_addr;
	sensor->watermark = 1;

	switch (info->id) {
	case ST_LSM6DSX_ID_MAGN: {
		const struct iio_chan_spec magn_channels[] = {
			ST_LSM6DSX_CHANNEL(IIO_MAGN, info->out.addr,
					   IIO_MOD_X, 0),
			ST_LSM6DSX_CHANNEL(IIO_MAGN, info->out.addr + 2,
					   IIO_MOD_Y, 1),
			ST_LSM6DSX_CHANNEL(IIO_MAGN, info->out.addr + 4,
					   IIO_MOD_Z, 2),
			IIO_CHAN_SOFT_TIMESTAMP(3),
		};

		ext_channels = devm_kzalloc(hw->dev, sizeof(magn_channels),
					    GFP_KERNEL);
		if (!ext_channels)
			return NULL;

		memcpy(ext_channels, magn_channels, sizeof(magn_channels));
		iio_dev->available_scan_masks = st_lsm6dsx_available_scan_masks;
		iio_dev->channels = ext_channels;
		iio_dev->num_channels = ARRAY_SIZE(magn_channels);

		scnprintf(sensor->name, sizeof(sensor->name), "%s_magn",
			  name);
		break;
	}
	default:
		return NULL;
	}
	iio_dev->name = sensor->name;

	return iio_dev;
}

static int st_lsm6dsx_shub_init_device(struct st_lsm6dsx_sensor *sensor)
{
	const struct st_lsm6dsx_ext_dev_settings *settings;
	int err;

	settings = sensor->ext_info.settings;
	if (settings->bdu.addr) {
		err = st_lsm6dsx_shub_write_with_mask(sensor,
						      settings->bdu.addr,
						      settings->bdu.mask, 1);
		if (err < 0)
			return err;
	}

	if (settings->temp_comp.addr) {
		err = st_lsm6dsx_shub_write_with_mask(sensor,
					settings->temp_comp.addr,
					settings->temp_comp.mask, 1);
		if (err < 0)
			return err;
	}

	if (settings->off_canc.addr) {
		err = st_lsm6dsx_shub_write_with_mask(sensor,
					settings->off_canc.addr,
					settings->off_canc.mask, 1);
		if (err < 0)
			return err;
	}

	return 0;
}

static int
st_lsm6dsx_shub_check_wai(struct st_lsm6dsx_hw *hw, u8 *i2c_addr,
			  const struct st_lsm6dsx_ext_dev_settings *settings)
{
	const struct st_lsm6dsx_shub_settings *hub_settings;
	struct st_lsm6dsx_sensor *sensor;
	u8 config[3], data, slv_addr;
	bool found = false;
	int i, err;

	hub_settings = &hw->settings->shub_settings;
	slv_addr = ST_LSM6DSX_SLV_ADDR(0, hub_settings->slv0_addr);
	sensor = iio_priv(hw->iio_devs[ST_LSM6DSX_ID_ACC]);

	for (i = 0; i < ARRAY_SIZE(settings->i2c_addr); i++) {
		if (!settings->i2c_addr[i])
			continue;

		/* read wai slave register */
		config[0] = (settings->i2c_addr[i] << 1) | 0x1;
		config[1] = settings->wai.addr;
		config[2] = 0x1;

		err = st_lsm6dsx_shub_write_reg(hw, slv_addr, config,
						sizeof(config));
		if (err < 0)
			return err;

		err = st_lsm6dsx_shub_master_enable(sensor, true);
		if (err < 0)
			return err;

		st_lsm6dsx_shub_wait_complete(hw);

		err = st_lsm6dsx_shub_read_reg(hw,
					       hub_settings->shub_out,
					       &data, sizeof(data));

		st_lsm6dsx_shub_master_enable(sensor, false);

		if (err < 0)
			return err;

		if (data != settings->wai.val)
			continue;

		*i2c_addr = settings->i2c_addr[i];
		found = true;
		break;
	}

	/* reset SLV0 channel */
	memset(config, 0, sizeof(config));
	err = st_lsm6dsx_shub_write_reg(hw, slv_addr, config,
					sizeof(config));
	if (err < 0)
		return err;

	return found ? 0 : -ENODEV;
}

int st_lsm6dsx_shub_probe(struct st_lsm6dsx_hw *hw, const char *name)
{
	enum st_lsm6dsx_sensor_id id = ST_LSM6DSX_ID_EXT0;
	struct st_lsm6dsx_sensor *sensor;
	int err, i, num_ext_dev = 0;
	u8 i2c_addr = 0;

	for (i = 0; i < ARRAY_SIZE(st_lsm6dsx_ext_dev_table); i++) {
		err = st_lsm6dsx_shub_check_wai(hw, &i2c_addr,
					&st_lsm6dsx_ext_dev_table[i]);
		if (err == -ENODEV)
			continue;
		else if (err < 0)
			return err;

		hw->iio_devs[id] = st_lsm6dsx_shub_alloc_iiodev(hw, id,
						&st_lsm6dsx_ext_dev_table[i],
						i2c_addr, name);
		if (!hw->iio_devs[id])
			return -ENOMEM;

		sensor = iio_priv(hw->iio_devs[id]);
		err = st_lsm6dsx_shub_init_device(sensor);
		if (err < 0)
			return err;

		if (++num_ext_dev >= ST_LSM6DSX_MAX_SLV_NUM)
			break;
		id++;
	}

	return 0;
}
