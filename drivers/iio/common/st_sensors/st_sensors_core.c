// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics sensors core library driver
 *
 * Copyright 2012-2013 STMicroelectronics Inc.
 *
 * Denis Ciocca <denis.ciocca@st.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/iio/iio.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/regmap.h>
#include <asm/unaligned.h>
#include <linux/iio/common/st_sensors.h>

#include "st_sensors_core.h"

static inline u32 st_sensors_get_unaligned_le24(const u8 *p)
{
	return (s32)((p[0] | p[1] << 8 | p[2] << 16) << 8) >> 8;
}

int st_sensors_write_data_with_mask(struct iio_dev *indio_dev,
				    u8 reg_addr, u8 mask, u8 data)
{
	struct st_sensor_data *sdata = iio_priv(indio_dev);

	return regmap_update_bits(sdata->regmap,
				  reg_addr, mask, data << __ffs(mask));
}

int st_sensors_debugfs_reg_access(struct iio_dev *indio_dev,
				  unsigned reg, unsigned writeval,
				  unsigned *readval)
{
	struct st_sensor_data *sdata = iio_priv(indio_dev);
	int err;

	if (!readval)
		return regmap_write(sdata->regmap, reg, writeval);

	err = regmap_read(sdata->regmap, reg, readval);
	if (err < 0)
		return err;

	return 0;
}
EXPORT_SYMBOL(st_sensors_debugfs_reg_access);

static int st_sensors_match_odr(struct st_sensor_settings *sensor_settings,
			unsigned int odr, struct st_sensor_odr_avl *odr_out)
{
	int i, ret = -EINVAL;

	for (i = 0; i < ST_SENSORS_ODR_LIST_MAX; i++) {
		if (sensor_settings->odr.odr_avl[i].hz == 0)
			goto st_sensors_match_odr_error;

		if (sensor_settings->odr.odr_avl[i].hz == odr) {
			odr_out->hz = sensor_settings->odr.odr_avl[i].hz;
			odr_out->value = sensor_settings->odr.odr_avl[i].value;
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

	if (!sdata->sensor_settings->odr.addr)
		return 0;

	err = st_sensors_match_odr(sdata->sensor_settings, odr, &odr_out);
	if (err < 0)
		goto st_sensors_match_odr_error;

	if ((sdata->sensor_settings->odr.addr ==
					sdata->sensor_settings->pw.addr) &&
				(sdata->sensor_settings->odr.mask ==
					sdata->sensor_settings->pw.mask)) {
		if (sdata->enabled == true) {
			err = st_sensors_write_data_with_mask(indio_dev,
				sdata->sensor_settings->odr.addr,
				sdata->sensor_settings->odr.mask,
				odr_out.value);
		} else {
			err = 0;
		}
	} else {
		err = st_sensors_write_data_with_mask(indio_dev,
			sdata->sensor_settings->odr.addr,
			sdata->sensor_settings->odr.mask,
			odr_out.value);
	}
	if (err >= 0)
		sdata->odr = odr_out.hz;

st_sensors_match_odr_error:
	return err;
}
EXPORT_SYMBOL(st_sensors_set_odr);

static int st_sensors_match_fs(struct st_sensor_settings *sensor_settings,
					unsigned int fs, int *index_fs_avl)
{
	int i, ret = -EINVAL;

	for (i = 0; i < ST_SENSORS_FULLSCALE_AVL_MAX; i++) {
		if (sensor_settings->fs.fs_avl[i].num == 0)
			return ret;

		if (sensor_settings->fs.fs_avl[i].num == fs) {
			*index_fs_avl = i;
			ret = 0;
			break;
		}
	}

	return ret;
}

static int st_sensors_set_fullscale(struct iio_dev *indio_dev, unsigned int fs)
{
	int err, i = 0;
	struct st_sensor_data *sdata = iio_priv(indio_dev);

	if (sdata->sensor_settings->fs.addr == 0)
		return 0;

	err = st_sensors_match_fs(sdata->sensor_settings, fs, &i);
	if (err < 0)
		goto st_accel_set_fullscale_error;

	err = st_sensors_write_data_with_mask(indio_dev,
				sdata->sensor_settings->fs.addr,
				sdata->sensor_settings->fs.mask,
				sdata->sensor_settings->fs.fs_avl[i].value);
	if (err < 0)
		goto st_accel_set_fullscale_error;

	sdata->current_fullscale = (struct st_sensor_fullscale_avl *)
					&sdata->sensor_settings->fs.fs_avl[i];
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
		tmp_value = sdata->sensor_settings->pw.value_on;
		if ((sdata->sensor_settings->odr.addr ==
					sdata->sensor_settings->pw.addr) &&
				(sdata->sensor_settings->odr.mask ==
					sdata->sensor_settings->pw.mask)) {
			err = st_sensors_match_odr(sdata->sensor_settings,
							sdata->odr, &odr_out);
			if (err < 0)
				goto set_enable_error;
			tmp_value = odr_out.value;
			found = true;
		}
		err = st_sensors_write_data_with_mask(indio_dev,
				sdata->sensor_settings->pw.addr,
				sdata->sensor_settings->pw.mask, tmp_value);
		if (err < 0)
			goto set_enable_error;

		sdata->enabled = true;

		if (found)
			sdata->odr = odr_out.hz;
	} else {
		err = st_sensors_write_data_with_mask(indio_dev,
				sdata->sensor_settings->pw.addr,
				sdata->sensor_settings->pw.mask,
				sdata->sensor_settings->pw.value_off);
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
	int err = 0;

	if (sdata->sensor_settings->enable_axis.addr)
		err = st_sensors_write_data_with_mask(indio_dev,
				sdata->sensor_settings->enable_axis.addr,
				sdata->sensor_settings->enable_axis.mask,
				axis_enable);
	return err;
}
EXPORT_SYMBOL(st_sensors_set_axis_enable);

int st_sensors_power_enable(struct iio_dev *indio_dev)
{
	struct st_sensor_data *pdata = iio_priv(indio_dev);
	int err;

	/* Regulators not mandatory, but if requested we should enable them. */
	pdata->vdd = devm_regulator_get(indio_dev->dev.parent, "vdd");
	if (IS_ERR(pdata->vdd)) {
		dev_err(&indio_dev->dev, "unable to get Vdd supply\n");
		return PTR_ERR(pdata->vdd);
	}
	err = regulator_enable(pdata->vdd);
	if (err != 0) {
		dev_warn(&indio_dev->dev,
			 "Failed to enable specified Vdd supply\n");
		return err;
	}

	pdata->vdd_io = devm_regulator_get(indio_dev->dev.parent, "vddio");
	if (IS_ERR(pdata->vdd_io)) {
		dev_err(&indio_dev->dev, "unable to get Vdd_IO supply\n");
		err = PTR_ERR(pdata->vdd_io);
		goto st_sensors_disable_vdd;
	}
	err = regulator_enable(pdata->vdd_io);
	if (err != 0) {
		dev_warn(&indio_dev->dev,
			 "Failed to enable specified Vdd_IO supply\n");
		goto st_sensors_disable_vdd;
	}

	return 0;

st_sensors_disable_vdd:
	regulator_disable(pdata->vdd);
	return err;
}
EXPORT_SYMBOL(st_sensors_power_enable);

void st_sensors_power_disable(struct iio_dev *indio_dev)
{
	struct st_sensor_data *pdata = iio_priv(indio_dev);

	regulator_disable(pdata->vdd);
	regulator_disable(pdata->vdd_io);
}
EXPORT_SYMBOL(st_sensors_power_disable);

static int st_sensors_set_drdy_int_pin(struct iio_dev *indio_dev,
					struct st_sensors_platform_data *pdata)
{
	struct st_sensor_data *sdata = iio_priv(indio_dev);

	/* Sensor does not support interrupts */
	if (!sdata->sensor_settings->drdy_irq.int1.addr &&
	    !sdata->sensor_settings->drdy_irq.int2.addr) {
		if (pdata->drdy_int_pin)
			dev_info(&indio_dev->dev,
				 "DRDY on pin INT%d specified, but sensor "
				 "does not support interrupts\n",
				 pdata->drdy_int_pin);
		return 0;
	}

	switch (pdata->drdy_int_pin) {
	case 1:
		if (!sdata->sensor_settings->drdy_irq.int1.mask) {
			dev_err(&indio_dev->dev,
					"DRDY on INT1 not available.\n");
			return -EINVAL;
		}
		sdata->drdy_int_pin = 1;
		break;
	case 2:
		if (!sdata->sensor_settings->drdy_irq.int2.mask) {
			dev_err(&indio_dev->dev,
					"DRDY on INT2 not available.\n");
			return -EINVAL;
		}
		sdata->drdy_int_pin = 2;
		break;
	default:
		dev_err(&indio_dev->dev, "DRDY on pdata not valid.\n");
		return -EINVAL;
	}

	if (pdata->open_drain) {
		if (!sdata->sensor_settings->drdy_irq.int1.addr_od &&
		    !sdata->sensor_settings->drdy_irq.int2.addr_od)
			dev_err(&indio_dev->dev,
				"open drain requested but unsupported.\n");
		else
			sdata->int_pin_open_drain = true;
	}

	return 0;
}

static struct st_sensors_platform_data *st_sensors_dev_probe(struct device *dev,
		struct st_sensors_platform_data *defdata)
{
	struct st_sensors_platform_data *pdata;
	u32 val;

	if (!dev_fwnode(dev))
		return NULL;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);
	if (!device_property_read_u32(dev, "st,drdy-int-pin", &val) && (val <= 2))
		pdata->drdy_int_pin = (u8) val;
	else
		pdata->drdy_int_pin = defdata ? defdata->drdy_int_pin : 0;

	pdata->open_drain = device_property_read_bool(dev, "drive-open-drain");

	return pdata;
}

/**
 * st_sensors_dev_name_probe() - device probe for ST sensor name
 * @dev: driver model representation of the device.
 * @name: device name buffer reference.
 * @len: device name buffer length.
 *
 * In effect this function matches an ID to an internal kernel
 * name for a certain sensor device, so that the rest of the autodetection can
 * rely on that name from this point on. I2C/SPI devices will be renamed
 * to match the internal kernel convention.
 */
void st_sensors_dev_name_probe(struct device *dev, char *name, int len)
{
	const void *match;

	match = device_get_match_data(dev);
	if (!match)
		return;

	/* The name from the match takes precedence if present */
	strlcpy(name, match, len);
}
EXPORT_SYMBOL(st_sensors_dev_name_probe);

int st_sensors_init_sensor(struct iio_dev *indio_dev,
					struct st_sensors_platform_data *pdata)
{
	struct st_sensor_data *sdata = iio_priv(indio_dev);
	struct st_sensors_platform_data *of_pdata;
	int err = 0;

	/* If OF/DT pdata exists, it will take precedence of anything else */
	of_pdata = st_sensors_dev_probe(indio_dev->dev.parent, pdata);
	if (IS_ERR(of_pdata))
		return PTR_ERR(of_pdata);
	if (of_pdata)
		pdata = of_pdata;

	if (pdata) {
		err = st_sensors_set_drdy_int_pin(indio_dev, pdata);
		if (err < 0)
			return err;
	}

	err = st_sensors_set_enable(indio_dev, false);
	if (err < 0)
		return err;

	/* Disable DRDY, this might be still be enabled after reboot. */
	err = st_sensors_set_dataready_irq(indio_dev, false);
	if (err < 0)
		return err;

	if (sdata->current_fullscale) {
		err = st_sensors_set_fullscale(indio_dev,
						sdata->current_fullscale->num);
		if (err < 0)
			return err;
	} else
		dev_info(&indio_dev->dev, "Full-scale not possible\n");

	err = st_sensors_set_odr(indio_dev, sdata->odr);
	if (err < 0)
		return err;

	/* set BDU */
	if (sdata->sensor_settings->bdu.addr) {
		err = st_sensors_write_data_with_mask(indio_dev,
					sdata->sensor_settings->bdu.addr,
					sdata->sensor_settings->bdu.mask, true);
		if (err < 0)
			return err;
	}

	/* set DAS */
	if (sdata->sensor_settings->das.addr) {
		err = st_sensors_write_data_with_mask(indio_dev,
					sdata->sensor_settings->das.addr,
					sdata->sensor_settings->das.mask, 1);
		if (err < 0)
			return err;
	}

	if (sdata->int_pin_open_drain) {
		u8 addr, mask;

		if (sdata->drdy_int_pin == 1) {
			addr = sdata->sensor_settings->drdy_irq.int1.addr_od;
			mask = sdata->sensor_settings->drdy_irq.int1.mask_od;
		} else {
			addr = sdata->sensor_settings->drdy_irq.int2.addr_od;
			mask = sdata->sensor_settings->drdy_irq.int2.mask_od;
		}

		dev_info(&indio_dev->dev,
			 "set interrupt line to open drain mode on pin %d\n",
			 sdata->drdy_int_pin);
		err = st_sensors_write_data_with_mask(indio_dev, addr,
						      mask, 1);
		if (err < 0)
			return err;
	}

	err = st_sensors_set_axis_enable(indio_dev, ST_SENSORS_ENABLE_ALL_AXIS);

	return err;
}
EXPORT_SYMBOL(st_sensors_init_sensor);

int st_sensors_set_dataready_irq(struct iio_dev *indio_dev, bool enable)
{
	int err;
	u8 drdy_addr, drdy_mask;
	struct st_sensor_data *sdata = iio_priv(indio_dev);

	if (!sdata->sensor_settings->drdy_irq.int1.addr &&
	    !sdata->sensor_settings->drdy_irq.int2.addr) {
		/*
		 * there are some devices (e.g. LIS3MDL) where drdy line is
		 * routed to a given pin and it is not possible to select a
		 * different one. Take into account irq status register
		 * to understand if irq trigger can be properly supported
		 */
		if (sdata->sensor_settings->drdy_irq.stat_drdy.addr)
			sdata->hw_irq_trigger = enable;
		return 0;
	}

	/* Enable/Disable the interrupt generator 1. */
	if (sdata->sensor_settings->drdy_irq.ig1.en_addr > 0) {
		err = st_sensors_write_data_with_mask(indio_dev,
				sdata->sensor_settings->drdy_irq.ig1.en_addr,
				sdata->sensor_settings->drdy_irq.ig1.en_mask,
				(int)enable);
		if (err < 0)
			goto st_accel_set_dataready_irq_error;
	}

	if (sdata->drdy_int_pin == 1) {
		drdy_addr = sdata->sensor_settings->drdy_irq.int1.addr;
		drdy_mask = sdata->sensor_settings->drdy_irq.int1.mask;
	} else {
		drdy_addr = sdata->sensor_settings->drdy_irq.int2.addr;
		drdy_mask = sdata->sensor_settings->drdy_irq.int2.mask;
	}

	/* Flag to the poll function that the hardware trigger is in use */
	sdata->hw_irq_trigger = enable;

	/* Enable/Disable the interrupt generator for data ready. */
	err = st_sensors_write_data_with_mask(indio_dev, drdy_addr,
					      drdy_mask, (int)enable);

st_accel_set_dataready_irq_error:
	return err;
}
EXPORT_SYMBOL(st_sensors_set_dataready_irq);

int st_sensors_set_fullscale_by_gain(struct iio_dev *indio_dev, int scale)
{
	int err = -EINVAL, i;
	struct st_sensor_data *sdata = iio_priv(indio_dev);

	for (i = 0; i < ST_SENSORS_FULLSCALE_AVL_MAX; i++) {
		if ((sdata->sensor_settings->fs.fs_avl[i].gain == scale) &&
				(sdata->sensor_settings->fs.fs_avl[i].gain != 0)) {
			err = 0;
			break;
		}
	}
	if (err < 0)
		goto st_sensors_match_scale_error;

	err = st_sensors_set_fullscale(indio_dev,
				sdata->sensor_settings->fs.fs_avl[i].num);

st_sensors_match_scale_error:
	return err;
}
EXPORT_SYMBOL(st_sensors_set_fullscale_by_gain);

static int st_sensors_read_axis_data(struct iio_dev *indio_dev,
				     struct iio_chan_spec const *ch, int *data)
{
	int err;
	u8 *outdata;
	struct st_sensor_data *sdata = iio_priv(indio_dev);
	unsigned int byte_for_channel;

	byte_for_channel = DIV_ROUND_UP(ch->scan_type.realbits +
					ch->scan_type.shift, 8);
	outdata = kmalloc(byte_for_channel, GFP_DMA | GFP_KERNEL);
	if (!outdata)
		return -ENOMEM;

	err = regmap_bulk_read(sdata->regmap, ch->address,
			       outdata, byte_for_channel);
	if (err < 0)
		goto st_sensors_free_memory;

	if (byte_for_channel == 1)
		*data = (s8)*outdata;
	else if (byte_for_channel == 2)
		*data = (s16)get_unaligned_le16(outdata);
	else if (byte_for_channel == 3)
		*data = (s32)st_sensors_get_unaligned_le24(outdata);

st_sensors_free_memory:
	kfree(outdata);

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
		goto out;
	} else {
		err = st_sensors_set_enable(indio_dev, true);
		if (err < 0)
			goto out;

		msleep((sdata->sensor_settings->bootime * 1000) / sdata->odr);
		err = st_sensors_read_axis_data(indio_dev, ch, val);
		if (err < 0)
			goto out;

		*val = *val >> ch->scan_type.shift;

		err = st_sensors_set_enable(indio_dev, false);
	}
out:
	mutex_unlock(&indio_dev->mlock);

	return err;
}
EXPORT_SYMBOL(st_sensors_read_info_raw);

/*
 * st_sensors_get_settings_index() - get index of the sensor settings for a
 *				     specific device from list of settings
 * @name: device name buffer reference.
 * @list: sensor settings list.
 * @list_length: length of sensor settings list.
 *
 * Return: non negative number on success (valid index),
 *	   negative error code otherwise.
 */
int st_sensors_get_settings_index(const char *name,
				  const struct st_sensor_settings *list,
				  const int list_length)
{
	int i, n;

	for (i = 0; i < list_length; i++) {
		for (n = 0; n < ST_SENSORS_MAX_4WAI; n++) {
			if (strcmp(name, list[i].sensors_supported[n]) == 0)
				return i;
		}
	}

	return -ENODEV;
}
EXPORT_SYMBOL(st_sensors_get_settings_index);

/*
 * st_sensors_verify_id() - verify sensor ID (WhoAmI) is matching with the
 *			    expected value
 * @indio_dev: IIO device reference.
 *
 * Return: 0 on success (valid sensor ID), else a negative error code.
 */
int st_sensors_verify_id(struct iio_dev *indio_dev)
{
	struct st_sensor_data *sdata = iio_priv(indio_dev);
	int wai, err;

	if (sdata->sensor_settings->wai_addr) {
		err = regmap_read(sdata->regmap,
				  sdata->sensor_settings->wai_addr, &wai);
		if (err < 0) {
			dev_err(&indio_dev->dev,
				"failed to read Who-Am-I register.\n");
			return err;
		}

		if (sdata->sensor_settings->wai != wai) {
			dev_err(&indio_dev->dev,
				"%s: WhoAmI mismatch (0x%x).\n",
				indio_dev->name, wai);
			return -EINVAL;
		}
	}

	return 0;
}
EXPORT_SYMBOL(st_sensors_verify_id);

ssize_t st_sensors_sysfs_sampling_frequency_avail(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int i, len = 0;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct st_sensor_data *sdata = iio_priv(indio_dev);

	mutex_lock(&indio_dev->mlock);
	for (i = 0; i < ST_SENSORS_ODR_LIST_MAX; i++) {
		if (sdata->sensor_settings->odr.odr_avl[i].hz == 0)
			break;

		len += scnprintf(buf + len, PAGE_SIZE - len, "%d ",
				sdata->sensor_settings->odr.odr_avl[i].hz);
	}
	mutex_unlock(&indio_dev->mlock);
	buf[len - 1] = '\n';

	return len;
}
EXPORT_SYMBOL(st_sensors_sysfs_sampling_frequency_avail);

ssize_t st_sensors_sysfs_scale_avail(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int i, len = 0, q, r;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct st_sensor_data *sdata = iio_priv(indio_dev);

	mutex_lock(&indio_dev->mlock);
	for (i = 0; i < ST_SENSORS_FULLSCALE_AVL_MAX; i++) {
		if (sdata->sensor_settings->fs.fs_avl[i].num == 0)
			break;

		q = sdata->sensor_settings->fs.fs_avl[i].gain / 1000000;
		r = sdata->sensor_settings->fs.fs_avl[i].gain % 1000000;

		len += scnprintf(buf + len, PAGE_SIZE - len, "%u.%06u ", q, r);
	}
	mutex_unlock(&indio_dev->mlock);
	buf[len - 1] = '\n';

	return len;
}
EXPORT_SYMBOL(st_sensors_sysfs_scale_avail);

MODULE_AUTHOR("Denis Ciocca <denis.ciocca@st.com>");
MODULE_DESCRIPTION("STMicroelectronics ST-sensors core");
MODULE_LICENSE("GPL v2");
