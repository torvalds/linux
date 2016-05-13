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
#include <linux/regulator/consumer.h>
#include <linux/of.h>
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

int st_sensors_debugfs_reg_access(struct iio_dev *indio_dev,
				  unsigned reg, unsigned writeval,
				  unsigned *readval)
{
	struct st_sensor_data *sdata = iio_priv(indio_dev);
	u8 readdata;
	int err;

	if (!readval)
		return sdata->tf->write_byte(&sdata->tb, sdata->dev,
					     (u8)reg, (u8)writeval);

	err = sdata->tf->read_byte(&sdata->tb, sdata->dev, (u8)reg, &readdata);
	if (err < 0)
		return err;

	*readval = (unsigned)readdata;

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
			goto st_sensors_match_odr_error;

		if (sensor_settings->fs.fs_avl[i].num == fs) {
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

	return st_sensors_write_data_with_mask(indio_dev,
				sdata->sensor_settings->enable_axis.addr,
				sdata->sensor_settings->enable_axis.mask,
				axis_enable);
}
EXPORT_SYMBOL(st_sensors_set_axis_enable);

void st_sensors_power_enable(struct iio_dev *indio_dev)
{
	struct st_sensor_data *pdata = iio_priv(indio_dev);
	int err;

	/* Regulators not mandatory, but if requested we should enable them. */
	pdata->vdd = devm_regulator_get_optional(indio_dev->dev.parent, "vdd");
	if (!IS_ERR(pdata->vdd)) {
		err = regulator_enable(pdata->vdd);
		if (err != 0)
			dev_warn(&indio_dev->dev,
				 "Failed to enable specified Vdd supply\n");
	}

	pdata->vdd_io = devm_regulator_get_optional(indio_dev->dev.parent, "vddio");
	if (!IS_ERR(pdata->vdd_io)) {
		err = regulator_enable(pdata->vdd_io);
		if (err != 0)
			dev_warn(&indio_dev->dev,
				 "Failed to enable specified Vdd_IO supply\n");
	}
}
EXPORT_SYMBOL(st_sensors_power_enable);

void st_sensors_power_disable(struct iio_dev *indio_dev)
{
	struct st_sensor_data *pdata = iio_priv(indio_dev);

	if (!IS_ERR(pdata->vdd))
		regulator_disable(pdata->vdd);

	if (!IS_ERR(pdata->vdd_io))
		regulator_disable(pdata->vdd_io);
}
EXPORT_SYMBOL(st_sensors_power_disable);

static int st_sensors_set_drdy_int_pin(struct iio_dev *indio_dev,
					struct st_sensors_platform_data *pdata)
{
	struct st_sensor_data *sdata = iio_priv(indio_dev);

	/* Sensor does not support interrupts */
	if (sdata->sensor_settings->drdy_irq.addr == 0) {
		if (pdata->drdy_int_pin)
			dev_info(&indio_dev->dev,
				 "DRDY on pin INT%d specified, but sensor "
				 "does not support interrupts\n",
				 pdata->drdy_int_pin);
		return 0;
	}

	switch (pdata->drdy_int_pin) {
	case 1:
		if (sdata->sensor_settings->drdy_irq.mask_int1 == 0) {
			dev_err(&indio_dev->dev,
					"DRDY on INT1 not available.\n");
			return -EINVAL;
		}
		sdata->drdy_int_pin = 1;
		break;
	case 2:
		if (sdata->sensor_settings->drdy_irq.mask_int2 == 0) {
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
		if (!sdata->sensor_settings->drdy_irq.addr_od)
			dev_err(&indio_dev->dev,
				"open drain requested but unsupported.\n");
		else
			sdata->int_pin_open_drain = true;
	}

	return 0;
}

#ifdef CONFIG_OF
static struct st_sensors_platform_data *st_sensors_of_probe(struct device *dev,
		struct st_sensors_platform_data *defdata)
{
	struct st_sensors_platform_data *pdata;
	struct device_node *np = dev->of_node;
	u32 val;

	if (!np)
		return NULL;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!of_property_read_u32(np, "st,drdy-int-pin", &val) && (val <= 2))
		pdata->drdy_int_pin = (u8) val;
	else
		pdata->drdy_int_pin = defdata ? defdata->drdy_int_pin : 0;

	pdata->open_drain = of_property_read_bool(np, "drive-open-drain");

	return pdata;
}
#else
static struct st_sensors_platform_data *st_sensors_of_probe(struct device *dev,
		struct st_sensors_platform_data *defdata)
{
	return NULL;
}
#endif

int st_sensors_init_sensor(struct iio_dev *indio_dev,
					struct st_sensors_platform_data *pdata)
{
	struct st_sensor_data *sdata = iio_priv(indio_dev);
	struct st_sensors_platform_data *of_pdata;
	int err = 0;

	/* If OF/DT pdata exists, it will take precedence of anything else */
	of_pdata = st_sensors_of_probe(indio_dev->dev.parent, pdata);
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

	if (sdata->int_pin_open_drain) {
		dev_info(&indio_dev->dev,
			 "set interrupt line to open drain mode\n");
		err = st_sensors_write_data_with_mask(indio_dev,
				sdata->sensor_settings->drdy_irq.addr_od,
				sdata->sensor_settings->drdy_irq.mask_od, 1);
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
	u8 drdy_mask;
	struct st_sensor_data *sdata = iio_priv(indio_dev);

	if (!sdata->sensor_settings->drdy_irq.addr)
		return 0;

	/* Enable/Disable the interrupt generator 1. */
	if (sdata->sensor_settings->drdy_irq.ig1.en_addr > 0) {
		err = st_sensors_write_data_with_mask(indio_dev,
				sdata->sensor_settings->drdy_irq.ig1.en_addr,
				sdata->sensor_settings->drdy_irq.ig1.en_mask,
				(int)enable);
		if (err < 0)
			goto st_accel_set_dataready_irq_error;
	}

	if (sdata->drdy_int_pin == 1)
		drdy_mask = sdata->sensor_settings->drdy_irq.mask_int1;
	else
		drdy_mask = sdata->sensor_settings->drdy_irq.mask_int2;

	/* Flag to the poll function that the hardware trigger is in use */
	sdata->hw_irq_trigger = enable;

	/* Enable/Disable the interrupt generator for data ready. */
	err = st_sensors_write_data_with_mask(indio_dev,
					sdata->sensor_settings->drdy_irq.addr,
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
	unsigned int byte_for_channel = ch->scan_type.storagebits >> 3;

	outdata = kmalloc(byte_for_channel, GFP_KERNEL);
	if (!outdata)
		return -ENOMEM;

	err = sdata->tf->read_multiple_byte(&sdata->tb, sdata->dev,
				ch->address, byte_for_channel,
				outdata, sdata->multiread_bit);
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

int st_sensors_check_device_support(struct iio_dev *indio_dev,
			int num_sensors_list,
			const struct st_sensor_settings *sensor_settings)
{
	int i, n, err;
	u8 wai;
	struct st_sensor_data *sdata = iio_priv(indio_dev);

	for (i = 0; i < num_sensors_list; i++) {
		for (n = 0; n < ST_SENSORS_MAX_4WAI; n++) {
			if (strcmp(indio_dev->name,
				sensor_settings[i].sensors_supported[n]) == 0) {
				break;
			}
		}
		if (n < ST_SENSORS_MAX_4WAI)
			break;
	}
	if (i == num_sensors_list) {
		dev_err(&indio_dev->dev, "device name %s not recognized.\n",
							indio_dev->name);
		return -ENODEV;
	}

	err = sdata->tf->read_byte(&sdata->tb, sdata->dev,
					sensor_settings[i].wai_addr, &wai);
	if (err < 0) {
		dev_err(&indio_dev->dev, "failed to read Who-Am-I register.\n");
		return err;
	}

	if (sensor_settings[i].wai != wai) {
		dev_err(&indio_dev->dev, "%s: WhoAmI mismatch (0x%x).\n",
						indio_dev->name, wai);
		return -EINVAL;
	}

	sdata->sensor_settings =
			(struct st_sensor_settings *)&sensor_settings[i];

	return i;
}
EXPORT_SYMBOL(st_sensors_check_device_support);

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
	int i, len = 0;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct st_sensor_data *sdata = iio_priv(indio_dev);

	mutex_lock(&indio_dev->mlock);
	for (i = 0; i < ST_SENSORS_FULLSCALE_AVL_MAX; i++) {
		if (sdata->sensor_settings->fs.fs_avl[i].num == 0)
			break;

		len += scnprintf(buf + len, PAGE_SIZE - len, "0.%06u ",
				sdata->sensor_settings->fs.fs_avl[i].gain);
	}
	mutex_unlock(&indio_dev->mlock);
	buf[len - 1] = '\n';

	return len;
}
EXPORT_SYMBOL(st_sensors_sysfs_scale_avail);

MODULE_AUTHOR("Denis Ciocca <denis.ciocca@st.com>");
MODULE_DESCRIPTION("STMicroelectronics ST-sensors core");
MODULE_LICENSE("GPL v2");
