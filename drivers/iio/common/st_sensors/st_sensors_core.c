/*
 * STMicroelectronics sensors core library driver
 *
 * Copyright 2012-2013 STMicroelectronics Inc.
 *
 * Denis Ciocca <denis.ciocca@st.com>
 *
 * Licensed under the GPL-2.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/iio/iio.h>
#include <asm/unaligned.h>

#include <linux/iio/common/st_sensors.h>


#define ST_SENSORS_WAI_ADDRESS		0x0f

static int st_sensors_write_data_with_mask(struct iio_dev *indio_dev,
						u8 reg_addr, u8 mask, u8 data)
{
	int err;
	u8 new_data;
	struct st_sensor_data *sdata = iio_priv(indio_dev);

	err = sdata->tf->read_byte(&sdata->tb, sdata->dev, reg_addr, &new_data);
	if (err < 0)
		goto st_sensors_write_data_with_mask_error;

	new_data = ((new_data & (~mask)) | ((data << __ffs(mask)) & mask));
	err = sdata->tf->write_byte(&sdata->tb, sdata->dev, reg_addr, new_data);

st_sensors_write_data_with_mask_error:
	return err;
}

static int st_sensors_match_odr(struct st_sensors *sensor,
			unsigned int odr, struct st_sensor_odr_avl *odr_out)
{
	int i, ret = -EINVAL;

	for (i = 0; i < ST_SENSORS_ODR_LIST_MAX; i++) {
		if (sensor->odr.odr_avl[i].hz == 0)
			goto st_sensors_match_odr_error;

		if (sensor->odr.odr_avl[i].hz == odr) {
			odr_out->hz = sensor->odr.odr_avl[i].hz;
			odr_out->value = sensor->odr.odr_avl[i].value;
			ret = 0;
			break;
		}
	}

st_sensors_match_odr_error:
	return ret;
}

int st_sensors_set_odr(struct iio_dev *indio_dev, unsigned int odr)
{
	int err;
	struct st_sensor_odr_avl odr_out = {0, 0};
	struct st_sensor_data *sdata = iio_priv(indio_dev);

	err = st_sensors_match_odr(sdata->sensor, odr, &odr_out);
	if (err < 0)
		goto st_sensors_match_odr_error;

	if ((sdata->sensor->odr.addr == sdata->sensor->pw.addr) &&
			(sdata->sensor->odr.mask == sdata->sensor->pw.mask)) {
		if (sdata->enabled == true) {
			err = st_sensors_write_data_with_mask(indio_dev,
				sdata->sensor->odr.addr,
				sdata->sensor->odr.mask,
				odr_out.value);
		} else {
			err = 0;
		}
	} else {
		err = st_sensors_write_data_with_mask(indio_dev,
			sdata->sensor->odr.addr, sdata->sensor->odr.mask,
			odr_out.value);
	}
	if (err >= 0)
		sdata->odr = odr_out.hz;

st_sensors_match_odr_error:
	return err;
}
EXPORT_SYMBOL(st_sensors_set_odr);

static int st_sensors_match_fs(struct st_sensors *sensor,
					unsigned int fs, int *index_fs_avl)
{
	int i, ret = -EINVAL;

	for (i = 0; i < ST_SENSORS_FULLSCALE_AVL_MAX; i++) {
		if (sensor->fs.fs_avl[i].num == 0)
			goto st_sensors_match_odr_error;

		if (sensor->fs.fs_avl[i].num == fs) {
			*index_fs_avl = i;
			ret = 0;
			break;
		}
	}

st_sensors_match_odr_error:
	return ret;
}

static int st_sensors_set_fullscale(struct iio_dev *indio_dev, unsigned int fs)
{
	int err, i = 0;
	struct st_sensor_data *sdata = iio_priv(indio_dev);

	err = st_sensors_match_fs(sdata->sensor, fs, &i);
	if (err < 0)
		goto st_accel_set_fullscale_error;

	err = st_sensors_write_data_with_mask(indio_dev,
				sdata->sensor->fs.addr,
				sdata->sensor->fs.mask,
				sdata->sensor->fs.fs_avl[i].value);
	if (err < 0)
		goto st_accel_set_fullscale_error;

	sdata->current_fullscale = (struct st_sensor_fullscale_avl *)
						&sdata->sensor->fs.fs_avl[i];
	return err;

st_accel_set_fullscale_error:
	dev_err(&indio_dev->dev, "failed to set new fullscale.\n");
	return err;
}

int st_sensors_set_enable(struct iio_dev *indio_dev, bool enable)
{
	u8 tmp_value;
	int err = -EINVAL;
	bool found = false;
	struct st_sensor_odr_avl odr_out = {0, 0};
	struct st_sensor_data *sdata = iio_priv(indio_dev);

	if (enable) {
		tmp_value = sdata->sensor->pw.value_on;
		if ((sdata->sensor->odr.addr == sdata->sensor->pw.addr) &&
			(sdata->sensor->odr.mask == sdata->sensor->pw.mask)) {
			err = st_sensors_match_odr(sdata->sensor,
							sdata->odr, &odr_out);
			if (err < 0)
				goto set_enable_error;
			tmp_value = odr_out.value;
			found = true;
		}
		err = st_sensors_write_data_with_mask(indio_dev,
				sdata->sensor->pw.addr,
				sdata->sensor->pw.mask, tmp_value);
		if (err < 0)
			goto set_enable_error;

		sdata->enabled = true;

		if (found)
			sdata->odr = odr_out.hz;
	} else {
		err = st_sensors_write_data_with_mask(indio_dev,
				sdata->sensor->pw.addr,
				sdata->sensor->pw.mask,
				sdata->sensor->pw.value_off);
		if (err < 0)
			goto set_enable_error;

		sdata->enabled = false;
	}

set_enable_error:
	return err;
}
EXPORT_SYMBOL(st_sensors_set_enable);

int st_sensors_set_axis_enable(struct iio_dev *indio_dev, u8 axis_enable)
{
	struct st_sensor_data *sdata = iio_priv(indio_dev);

	return st_sensors_write_data_with_mask(indio_dev,
				sdata->sensor->enable_axis.addr,
				sdata->sensor->enable_axis.mask, axis_enable);
}
EXPORT_SYMBOL(st_sensors_set_axis_enable);

int st_sensors_init_sensor(struct iio_dev *indio_dev)
{
	int err;
	struct st_sensor_data *sdata = iio_priv(indio_dev);

	mutex_init(&sdata->tb.buf_lock);

	err = st_sensors_set_enable(indio_dev, false);
	if (err < 0)
		goto init_error;

	err = st_sensors_set_fullscale(indio_dev,
						sdata->current_fullscale->num);
	if (err < 0)
		goto init_error;

	err = st_sensors_set_odr(indio_dev, sdata->odr);
	if (err < 0)
		goto init_error;

	/* set BDU */
	err = st_sensors_write_data_with_mask(indio_dev,
			sdata->sensor->bdu.addr, sdata->sensor->bdu.mask, true);
	if (err < 0)
		goto init_error;

	err = st_sensors_set_axis_enable(indio_dev, ST_SENSORS_ENABLE_ALL_AXIS);

init_error:
	return err;
}
EXPORT_SYMBOL(st_sensors_init_sensor);

int st_sensors_set_dataready_irq(struct iio_dev *indio_dev, bool enable)
{
	int err;
	struct st_sensor_data *sdata = iio_priv(indio_dev);

	/* Enable/Disable the interrupt generator 1. */
	if (sdata->sensor->drdy_irq.ig1.en_addr > 0) {
		err = st_sensors_write_data_with_mask(indio_dev,
			sdata->sensor->drdy_irq.ig1.en_addr,
			sdata->sensor->drdy_irq.ig1.en_mask, (int)enable);
		if (err < 0)
			goto st_accel_set_dataready_irq_error;
	}

	/* Enable/Disable the interrupt generator for data ready. */
	err = st_sensors_write_data_with_mask(indio_dev,
			sdata->sensor->drdy_irq.addr,
			sdata->sensor->drdy_irq.mask, (int)enable);

st_accel_set_dataready_irq_error:
	return err;
}
EXPORT_SYMBOL(st_sensors_set_dataready_irq);

int st_sensors_set_fullscale_by_gain(struct iio_dev *indio_dev, int scale)
{
	int err = -EINVAL, i;
	struct st_sensor_data *sdata = iio_priv(indio_dev);

	for (i = 0; i < ST_SENSORS_FULLSCALE_AVL_MAX; i++) {
		if ((sdata->sensor->fs.fs_avl[i].gain == scale) &&
				(sdata->sensor->fs.fs_avl[i].gain != 0)) {
			err = 0;
			break;
		}
	}
	if (err < 0)
		goto st_sensors_match_scale_error;

	err = st_sensors_set_fullscale(indio_dev,
					sdata->sensor->fs.fs_avl[i].num);

st_sensors_match_scale_error:
	return err;
}
EXPORT_SYMBOL(st_sensors_set_fullscale_by_gain);

static int st_sensors_read_axis_data(struct iio_dev *indio_dev,
							u8 ch_addr, int *data)
{
	int err;
	u8 outdata[ST_SENSORS_BYTE_FOR_CHANNEL];
	struct st_sensor_data *sdata = iio_priv(indio_dev);

	err = sdata->tf->read_multiple_byte(&sdata->tb, sdata->dev,
				ch_addr, ST_SENSORS_BYTE_FOR_CHANNEL,
				outdata, sdata->multiread_bit);
	if (err < 0)
		goto read_error;

	*data = (s16)get_unaligned_le16(outdata);

read_error:
	return err;
}

int st_sensors_read_info_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *ch, int *val)
{
	int err;
	struct st_sensor_data *sdata = iio_priv(indio_dev);

	mutex_lock(&indio_dev->mlock);
	if (indio_dev->currentmode == INDIO_BUFFER_TRIGGERED) {
		err = -EBUSY;
		goto read_error;
	} else {
		err = st_sensors_set_enable(indio_dev, true);
		if (err < 0)
			goto read_error;

		msleep((sdata->sensor->bootime * 1000) / sdata->odr);
		err = st_sensors_read_axis_data(indio_dev, ch->address, val);
		if (err < 0)
			goto read_error;

		*val = *val >> ch->scan_type.shift;
	}
	mutex_unlock(&indio_dev->mlock);

	return err;

read_error:
	mutex_unlock(&indio_dev->mlock);
	return err;
}
EXPORT_SYMBOL(st_sensors_read_info_raw);

int st_sensors_check_device_support(struct iio_dev *indio_dev,
			int num_sensors_list, const struct st_sensors *sensors)
{
	u8 wai;
	int i, n, err;
	struct st_sensor_data *sdata = iio_priv(indio_dev);

	err = sdata->tf->read_byte(&sdata->tb, sdata->dev,
					ST_SENSORS_DEFAULT_WAI_ADDRESS, &wai);
	if (err < 0) {
		dev_err(&indio_dev->dev, "failed to read Who-Am-I register.\n");
		goto read_wai_error;
	}

	for (i = 0; i < num_sensors_list; i++) {
		if (sensors[i].wai == wai)
			break;
	}
	if (i == num_sensors_list)
		goto device_not_supported;

	for (n = 0; n < ARRAY_SIZE(sensors[i].sensors_supported); n++) {
		if (strcmp(indio_dev->name,
				&sensors[i].sensors_supported[n][0]) == 0)
			break;
	}
	if (n == ARRAY_SIZE(sensors[i].sensors_supported)) {
		dev_err(&indio_dev->dev, "device name and WhoAmI mismatch.\n");
		goto sensor_name_mismatch;
	}

	sdata->sensor = (struct st_sensors *)&sensors[i];

	return i;

device_not_supported:
	dev_err(&indio_dev->dev, "device not supported: WhoAmI (0x%x).\n", wai);
sensor_name_mismatch:
	err = -ENODEV;
read_wai_error:
	return err;
}
EXPORT_SYMBOL(st_sensors_check_device_support);

ssize_t st_sensors_sysfs_get_sampling_frequency(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct st_sensor_data *adata = iio_priv(dev_get_drvdata(dev));

	return sprintf(buf, "%d\n", adata->odr);
}
EXPORT_SYMBOL(st_sensors_sysfs_get_sampling_frequency);

ssize_t st_sensors_sysfs_set_sampling_frequency(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	int err;
	unsigned int odr;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);

	err = kstrtoint(buf, 10, &odr);
	if (err < 0)
		goto conversion_error;

	mutex_lock(&indio_dev->mlock);
	err = st_sensors_set_odr(indio_dev, odr);
	mutex_unlock(&indio_dev->mlock);

conversion_error:
	return err < 0 ? err : size;
}
EXPORT_SYMBOL(st_sensors_sysfs_set_sampling_frequency);

ssize_t st_sensors_sysfs_sampling_frequency_avail(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int i, len = 0;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct st_sensor_data *sdata = iio_priv(indio_dev);

	mutex_lock(&indio_dev->mlock);
	for (i = 0; i < ST_SENSORS_ODR_LIST_MAX; i++) {
		if (sdata->sensor->odr.odr_avl[i].hz == 0)
			break;

		len += scnprintf(buf + len, PAGE_SIZE - len, "%d ",
					sdata->sensor->odr.odr_avl[i].hz);
	}
	mutex_unlock(&indio_dev->mlock);
	buf[len - 1] = '\n';

	return len;
}
EXPORT_SYMBOL(st_sensors_sysfs_sampling_frequency_avail);

ssize_t st_sensors_sysfs_scale_avail(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int i, len = 0;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct st_sensor_data *sdata = iio_priv(indio_dev);

	mutex_lock(&indio_dev->mlock);
	for (i = 0; i < ST_SENSORS_FULLSCALE_AVL_MAX; i++) {
		if (sdata->sensor->fs.fs_avl[i].num == 0)
			break;

		len += scnprintf(buf + len, PAGE_SIZE - len, "0.%06u ",
					sdata->sensor->fs.fs_avl[i].gain);
	}
	mutex_unlock(&indio_dev->mlock);
	buf[len - 1] = '\n';

	return len;
}
EXPORT_SYMBOL(st_sensors_sysfs_scale_avail);

MODULE_AUTHOR("Denis Ciocca <denis.ciocca@st.com>");
MODULE_DESCRIPTION("STMicroelectronics ST-sensors core");
MODULE_LICENSE("GPL v2");
