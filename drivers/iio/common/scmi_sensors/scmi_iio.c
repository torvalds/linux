// SPDX-License-Identifier: GPL-2.0

/*
 * System Control and Management Interface(SCMI) based IIO sensor driver
 *
 * Copyright (C) 2021 Google LLC
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/kfifo_buf.h>
#include <linux/iio/sysfs.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/scmi_protocol.h>
#include <linux/time.h>
#include <linux/types.h>
#include <linux/units.h>

#define SCMI_IIO_NUM_OF_AXIS 3

struct scmi_iio_priv {
	const struct scmi_sensor_proto_ops *sensor_ops;
	struct scmi_protocol_handle *ph;
	const struct scmi_sensor_info *sensor_info;
	struct iio_dev *indio_dev;
	/* adding one additional channel for timestamp */
	s64 iio_buf[SCMI_IIO_NUM_OF_AXIS + 1];
	struct notifier_block sensor_update_nb;
	u32 *freq_avail;
};

static int scmi_iio_sensor_update_cb(struct notifier_block *nb,
				     unsigned long event, void *data)
{
	struct scmi_sensor_update_report *sensor_update = data;
	struct iio_dev *scmi_iio_dev;
	struct scmi_iio_priv *sensor;
	s8 tstamp_scale;
	u64 time, time_ns;
	int i;

	if (sensor_update->readings_count == 0)
		return NOTIFY_DONE;

	sensor = container_of(nb, struct scmi_iio_priv, sensor_update_nb);

	for (i = 0; i < sensor_update->readings_count; i++)
		sensor->iio_buf[i] = sensor_update->readings[i].value;

	if (!sensor->sensor_info->timestamped) {
		time_ns = ktime_to_ns(sensor_update->timestamp);
	} else {
		/*
		 *  All the axes are supposed to have the same value for timestamp.
		 *  We are just using the values from the Axis 0 here.
		 */
		time = sensor_update->readings[0].timestamp;

		/*
		 *  Timestamp returned by SCMI is in seconds and is equal to
		 *  time * power-of-10 multiplier(tstamp_scale) seconds.
		 *  Converting the timestamp to nanoseconds below.
		 */
		tstamp_scale = sensor->sensor_info->tstamp_scale +
			       const_ilog2(NSEC_PER_SEC) / const_ilog2(10);
		if (tstamp_scale < 0) {
			do_div(time, int_pow(10, abs(tstamp_scale)));
			time_ns = time;
		} else {
			time_ns = time * int_pow(10, tstamp_scale);
		}
	}

	scmi_iio_dev = sensor->indio_dev;
	iio_push_to_buffers_with_timestamp(scmi_iio_dev, sensor->iio_buf,
					   time_ns);
	return NOTIFY_OK;
}

static int scmi_iio_buffer_preenable(struct iio_dev *iio_dev)
{
	struct scmi_iio_priv *sensor = iio_priv(iio_dev);
	u32 sensor_config = 0;
	int err;

	if (sensor->sensor_info->timestamped)
		sensor_config |= FIELD_PREP(SCMI_SENS_CFG_TSTAMP_ENABLED_MASK,
					    SCMI_SENS_CFG_TSTAMP_ENABLE);

	sensor_config |= FIELD_PREP(SCMI_SENS_CFG_SENSOR_ENABLED_MASK,
				    SCMI_SENS_CFG_SENSOR_ENABLE);
	err = sensor->sensor_ops->config_set(sensor->ph,
					     sensor->sensor_info->id,
					     sensor_config);
	if (err)
		dev_err(&iio_dev->dev, "Error in enabling sensor %s err %d",
			sensor->sensor_info->name, err);

	return err;
}

static int scmi_iio_buffer_postdisable(struct iio_dev *iio_dev)
{
	struct scmi_iio_priv *sensor = iio_priv(iio_dev);
	u32 sensor_config = 0;
	int err;

	sensor_config |= FIELD_PREP(SCMI_SENS_CFG_SENSOR_ENABLED_MASK,
				    SCMI_SENS_CFG_SENSOR_DISABLE);
	err = sensor->sensor_ops->config_set(sensor->ph,
					     sensor->sensor_info->id,
					     sensor_config);
	if (err) {
		dev_err(&iio_dev->dev,
			"Error in disabling sensor %s with err %d",
			sensor->sensor_info->name, err);
	}

	return err;
}

static const struct iio_buffer_setup_ops scmi_iio_buffer_ops = {
	.preenable = scmi_iio_buffer_preenable,
	.postdisable = scmi_iio_buffer_postdisable,
};

static int scmi_iio_set_odr_val(struct iio_dev *iio_dev, int val, int val2)
{
	struct scmi_iio_priv *sensor = iio_priv(iio_dev);
	u64 sec, mult, uHz, sf;
	u32 sensor_config;
	char buf[32];

	int err = sensor->sensor_ops->config_get(sensor->ph,
						 sensor->sensor_info->id,
						 &sensor_config);
	if (err) {
		dev_err(&iio_dev->dev,
			"Error in getting sensor config for sensor %s err %d",
			sensor->sensor_info->name, err);
		return err;
	}

	uHz = val * MICROHZ_PER_HZ + val2;

	/*
	 * The seconds field in the sensor interval in SCMI is 16 bits long
	 * Therefore seconds  = 1/Hz <= 0xFFFF. As floating point calculations are
	 * discouraged in the kernel driver code, to calculate the scale factor (sf)
	 * (1* 1000000 * sf)/uHz <= 0xFFFF. Therefore, sf <= (uHz * 0xFFFF)/1000000
	 * To calculate the multiplier,we convert the sf into char string  and
	 * count the number of characters
	 */
	sf = (u64)uHz * 0xFFFF;
	do_div(sf,  MICROHZ_PER_HZ);
	mult = scnprintf(buf, sizeof(buf), "%llu", sf) - 1;

	sec = int_pow(10, mult) * MICROHZ_PER_HZ;
	do_div(sec, uHz);
	if (sec == 0) {
		dev_err(&iio_dev->dev,
			"Trying to set invalid sensor update value for sensor %s",
			sensor->sensor_info->name);
		return -EINVAL;
	}

	sensor_config &= ~SCMI_SENS_CFG_UPDATE_SECS_MASK;
	sensor_config |= FIELD_PREP(SCMI_SENS_CFG_UPDATE_SECS_MASK, sec);
	sensor_config &= ~SCMI_SENS_CFG_UPDATE_EXP_MASK;
	sensor_config |= FIELD_PREP(SCMI_SENS_CFG_UPDATE_EXP_MASK, -mult);

	if (sensor->sensor_info->timestamped) {
		sensor_config &= ~SCMI_SENS_CFG_TSTAMP_ENABLED_MASK;
		sensor_config |= FIELD_PREP(SCMI_SENS_CFG_TSTAMP_ENABLED_MASK,
					    SCMI_SENS_CFG_TSTAMP_ENABLE);
	}

	sensor_config &= ~SCMI_SENS_CFG_ROUND_MASK;
	sensor_config |=
		FIELD_PREP(SCMI_SENS_CFG_ROUND_MASK, SCMI_SENS_CFG_ROUND_AUTO);

	err = sensor->sensor_ops->config_set(sensor->ph,
					     sensor->sensor_info->id,
					     sensor_config);
	if (err)
		dev_err(&iio_dev->dev,
			"Error in setting sensor update interval for sensor %s value %u err %d",
			sensor->sensor_info->name, sensor_config, err);

	return err;
}

static int scmi_iio_write_raw(struct iio_dev *iio_dev,
			      struct iio_chan_spec const *chan, int val,
			      int val2, long mask)
{
	int err;

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		mutex_lock(&iio_dev->mlock);
		err = scmi_iio_set_odr_val(iio_dev, val, val2);
		mutex_unlock(&iio_dev->mlock);
		return err;
	default:
		return -EINVAL;
	}
}

static int scmi_iio_read_avail(struct iio_dev *iio_dev,
			       struct iio_chan_spec const *chan,
			       const int **vals, int *type, int *length,
			       long mask)
{
	struct scmi_iio_priv *sensor = iio_priv(iio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		*vals = sensor->freq_avail;
		*type = IIO_VAL_INT_PLUS_MICRO;
		*length = sensor->sensor_info->intervals.count * 2;
		if (sensor->sensor_info->intervals.segmented)
			return IIO_AVAIL_RANGE;
		else
			return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
}

static void convert_ns_to_freq(u64 interval_ns, u64 *hz, u64 *uhz)
{
	u64 rem, freq;

	freq = NSEC_PER_SEC;
	rem = do_div(freq, interval_ns);
	*hz = freq;
	*uhz = rem * 1000000UL;
	do_div(*uhz, interval_ns);
}

static int scmi_iio_get_odr_val(struct iio_dev *iio_dev, int *val, int *val2)
{
	u64 sensor_update_interval, sensor_interval_mult, hz, uhz;
	struct scmi_iio_priv *sensor = iio_priv(iio_dev);
	u32 sensor_config;
	int mult;

	int err = sensor->sensor_ops->config_get(sensor->ph,
						 sensor->sensor_info->id,
						 &sensor_config);
	if (err) {
		dev_err(&iio_dev->dev,
			"Error in getting sensor config for sensor %s err %d",
			sensor->sensor_info->name, err);
		return err;
	}

	sensor_update_interval =
		SCMI_SENS_CFG_GET_UPDATE_SECS(sensor_config) * NSEC_PER_SEC;

	mult = SCMI_SENS_CFG_GET_UPDATE_EXP(sensor_config);
	if (mult < 0) {
		sensor_interval_mult = int_pow(10, abs(mult));
		do_div(sensor_update_interval, sensor_interval_mult);
	} else {
		sensor_interval_mult = int_pow(10, mult);
		sensor_update_interval =
			sensor_update_interval * sensor_interval_mult;
	}

	convert_ns_to_freq(sensor_update_interval, &hz, &uhz);
	*val = hz;
	*val2 = uhz;
	return 0;
}

static int scmi_iio_read_channel_data(struct iio_dev *iio_dev,
			     struct iio_chan_spec const *ch, int *val, int *val2)
{
	struct scmi_iio_priv *sensor = iio_priv(iio_dev);
	u32 sensor_config;
	struct scmi_sensor_reading readings[SCMI_IIO_NUM_OF_AXIS];
	int err;

	sensor_config = FIELD_PREP(SCMI_SENS_CFG_SENSOR_ENABLED_MASK,
					SCMI_SENS_CFG_SENSOR_ENABLE);
	err = sensor->sensor_ops->config_set(
		sensor->ph, sensor->sensor_info->id, sensor_config);
	if (err) {
		dev_err(&iio_dev->dev,
			"Error in enabling sensor %s err %d",
			sensor->sensor_info->name, err);
		return err;
	}

	err = sensor->sensor_ops->reading_get_timestamped(
		sensor->ph, sensor->sensor_info->id,
		sensor->sensor_info->num_axis, readings);
	if (err) {
		dev_err(&iio_dev->dev,
			"Error in reading raw attribute for sensor %s err %d",
			sensor->sensor_info->name, err);
		return err;
	}

	sensor_config = FIELD_PREP(SCMI_SENS_CFG_SENSOR_ENABLED_MASK,
					SCMI_SENS_CFG_SENSOR_DISABLE);
	err = sensor->sensor_ops->config_set(
		sensor->ph, sensor->sensor_info->id, sensor_config);
	if (err) {
		dev_err(&iio_dev->dev,
			"Error in disabling sensor %s err %d",
			sensor->sensor_info->name, err);
		return err;
	}

	*val = lower_32_bits(readings[ch->scan_index].value);
	*val2 = upper_32_bits(readings[ch->scan_index].value);

	return IIO_VAL_INT_64;
}

static int scmi_iio_read_raw(struct iio_dev *iio_dev,
			     struct iio_chan_spec const *ch, int *val,
			     int *val2, long mask)
{
	struct scmi_iio_priv *sensor = iio_priv(iio_dev);
	s8 scale;
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		scale = sensor->sensor_info->axis[ch->scan_index].scale;
		if (scale < 0) {
			*val = 1;
			*val2 = int_pow(10, abs(scale));
			return IIO_VAL_FRACTIONAL;
		}
		*val = int_pow(10, scale);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SAMP_FREQ:
		ret = scmi_iio_get_odr_val(iio_dev, val, val2);
		return ret ? ret : IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_RAW:
		ret = iio_device_claim_direct_mode(iio_dev);
		if (ret)
			return ret;

		ret = scmi_iio_read_channel_data(iio_dev, ch, val, val2);
		iio_device_release_direct_mode(iio_dev);
		return ret;
	default:
		return -EINVAL;
	}
}

static const struct iio_info scmi_iio_info = {
	.read_raw = scmi_iio_read_raw,
	.read_avail = scmi_iio_read_avail,
	.write_raw = scmi_iio_write_raw,
};

static ssize_t scmi_iio_get_raw_available(struct iio_dev *iio_dev,
					  uintptr_t private,
					  const struct iio_chan_spec *chan,
					  char *buf)
{
	struct scmi_iio_priv *sensor = iio_priv(iio_dev);
	u64 resolution, rem;
	s64 min_range, max_range;
	s8 exponent, scale;
	int len = 0;

	/*
	 * All the axes are supposed to have the same value for range and resolution.
	 * We are just using the values from the Axis 0 here.
	 */
	if (sensor->sensor_info->axis[0].extended_attrs) {
		min_range = sensor->sensor_info->axis[0].attrs.min_range;
		max_range = sensor->sensor_info->axis[0].attrs.max_range;
		resolution = sensor->sensor_info->axis[0].resolution;
		exponent = sensor->sensor_info->axis[0].exponent;
		scale = sensor->sensor_info->axis[0].scale;

		/*
		 * To provide the raw value for the resolution to the userspace,
		 * need to divide the resolution exponent by the sensor scale
		 */
		exponent = exponent - scale;
		if (exponent < 0) {
			rem = do_div(resolution,
				     int_pow(10, abs(exponent))
				     );
			len = scnprintf(buf, PAGE_SIZE,
					"[%lld %llu.%llu %lld]\n", min_range,
					resolution, rem, max_range);
		} else {
			resolution = resolution * int_pow(10, exponent);
			len = scnprintf(buf, PAGE_SIZE, "[%lld %llu %lld]\n",
					min_range, resolution, max_range);
		}
	}
	return len;
}

static const struct iio_chan_spec_ext_info scmi_iio_ext_info[] = {
	{
		.name = "raw_available",
		.read = scmi_iio_get_raw_available,
		.shared = IIO_SHARED_BY_TYPE,
	},
	{},
};

static void scmi_iio_set_timestamp_channel(struct iio_chan_spec *iio_chan,
					   int scan_index)
{
	iio_chan->type = IIO_TIMESTAMP;
	iio_chan->channel = -1;
	iio_chan->scan_index = scan_index;
	iio_chan->scan_type.sign = 'u';
	iio_chan->scan_type.realbits = 64;
	iio_chan->scan_type.storagebits = 64;
}

static void scmi_iio_set_data_channel(struct iio_chan_spec *iio_chan,
				      enum iio_chan_type type,
				      enum iio_modifier mod, int scan_index)
{
	iio_chan->type = type;
	iio_chan->modified = 1;
	iio_chan->channel2 = mod;
	iio_chan->info_mask_separate =
		BIT(IIO_CHAN_INFO_SCALE) | BIT(IIO_CHAN_INFO_RAW);
	iio_chan->info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SAMP_FREQ);
	iio_chan->info_mask_shared_by_type_available =
		BIT(IIO_CHAN_INFO_SAMP_FREQ);
	iio_chan->scan_index = scan_index;
	iio_chan->scan_type.sign = 's';
	iio_chan->scan_type.realbits = 64;
	iio_chan->scan_type.storagebits = 64;
	iio_chan->scan_type.endianness = IIO_LE;
	iio_chan->ext_info = scmi_iio_ext_info;
}

static int scmi_iio_get_chan_modifier(const char *name,
				      enum iio_modifier *modifier)
{
	char *pch, mod;

	if (!name)
		return -EINVAL;

	pch = strrchr(name, '_');
	if (!pch)
		return -EINVAL;

	mod = *(pch + 1);
	switch (mod) {
	case 'X':
		*modifier = IIO_MOD_X;
		return 0;
	case 'Y':
		*modifier = IIO_MOD_Y;
		return 0;
	case 'Z':
		*modifier = IIO_MOD_Z;
		return 0;
	default:
		return -EINVAL;
	}
}

static int scmi_iio_get_chan_type(u8 scmi_type, enum iio_chan_type *iio_type)
{
	switch (scmi_type) {
	case METERS_SEC_SQUARED:
		*iio_type = IIO_ACCEL;
		return 0;
	case RADIANS_SEC:
		*iio_type = IIO_ANGL_VEL;
		return 0;
	default:
		return -EINVAL;
	}
}

static u64 scmi_iio_convert_interval_to_ns(u32 val)
{
	u64 sensor_update_interval =
		SCMI_SENS_INTVL_GET_SECS(val) * NSEC_PER_SEC;
	u64 sensor_interval_mult;
	int mult;

	mult = SCMI_SENS_INTVL_GET_EXP(val);
	if (mult < 0) {
		sensor_interval_mult = int_pow(10, abs(mult));
		do_div(sensor_update_interval, sensor_interval_mult);
	} else {
		sensor_interval_mult = int_pow(10, mult);
		sensor_update_interval =
			sensor_update_interval * sensor_interval_mult;
	}
	return sensor_update_interval;
}

static int scmi_iio_set_sampling_freq_avail(struct iio_dev *iio_dev)
{
	u64 cur_interval_ns, low_interval_ns, high_interval_ns, step_size_ns,
		hz, uhz;
	unsigned int cur_interval, low_interval, high_interval, step_size;
	struct scmi_iio_priv *sensor = iio_priv(iio_dev);
	int i;

	sensor->freq_avail =
		devm_kzalloc(&iio_dev->dev,
			     sizeof(*sensor->freq_avail) *
				     (sensor->sensor_info->intervals.count * 2),
			     GFP_KERNEL);
	if (!sensor->freq_avail)
		return -ENOMEM;

	if (sensor->sensor_info->intervals.segmented) {
		low_interval = sensor->sensor_info->intervals
				       .desc[SCMI_SENS_INTVL_SEGMENT_LOW];
		low_interval_ns = scmi_iio_convert_interval_to_ns(low_interval);
		convert_ns_to_freq(low_interval_ns, &hz, &uhz);
		sensor->freq_avail[0] = hz;
		sensor->freq_avail[1] = uhz;

		step_size = sensor->sensor_info->intervals
				    .desc[SCMI_SENS_INTVL_SEGMENT_STEP];
		step_size_ns = scmi_iio_convert_interval_to_ns(step_size);
		convert_ns_to_freq(step_size_ns, &hz, &uhz);
		sensor->freq_avail[2] = hz;
		sensor->freq_avail[3] = uhz;

		high_interval = sensor->sensor_info->intervals
					.desc[SCMI_SENS_INTVL_SEGMENT_HIGH];
		high_interval_ns =
			scmi_iio_convert_interval_to_ns(high_interval);
		convert_ns_to_freq(high_interval_ns, &hz, &uhz);
		sensor->freq_avail[4] = hz;
		sensor->freq_avail[5] = uhz;
	} else {
		for (i = 0; i < sensor->sensor_info->intervals.count; i++) {
			cur_interval = sensor->sensor_info->intervals.desc[i];
			cur_interval_ns =
				scmi_iio_convert_interval_to_ns(cur_interval);
			convert_ns_to_freq(cur_interval_ns, &hz, &uhz);
			sensor->freq_avail[i * 2] = hz;
			sensor->freq_avail[i * 2 + 1] = uhz;
		}
	}
	return 0;
}

static struct iio_dev *
scmi_alloc_iiodev(struct scmi_device *sdev,
		  const struct scmi_sensor_proto_ops *ops,
		  struct scmi_protocol_handle *ph,
		  const struct scmi_sensor_info *sensor_info)
{
	struct iio_chan_spec *iio_channels;
	struct scmi_iio_priv *sensor;
	enum iio_modifier modifier;
	enum iio_chan_type type;
	struct iio_dev *iiodev;
	struct device *dev = &sdev->dev;
	const struct scmi_handle *handle = sdev->handle;
	int i, ret;

	iiodev = devm_iio_device_alloc(dev, sizeof(*sensor));
	if (!iiodev)
		return ERR_PTR(-ENOMEM);

	iiodev->modes = INDIO_DIRECT_MODE;
	sensor = iio_priv(iiodev);
	sensor->sensor_ops = ops;
	sensor->ph = ph;
	sensor->sensor_info = sensor_info;
	sensor->sensor_update_nb.notifier_call = scmi_iio_sensor_update_cb;
	sensor->indio_dev = iiodev;

	/* adding one additional channel for timestamp */
	iiodev->num_channels = sensor_info->num_axis + 1;
	iiodev->name = sensor_info->name;
	iiodev->info = &scmi_iio_info;

	iio_channels =
		devm_kzalloc(dev,
			     sizeof(*iio_channels) * (iiodev->num_channels),
			     GFP_KERNEL);
	if (!iio_channels)
		return ERR_PTR(-ENOMEM);

	ret = scmi_iio_set_sampling_freq_avail(iiodev);
	if (ret < 0)
		return ERR_PTR(ret);

	for (i = 0; i < sensor_info->num_axis; i++) {
		ret = scmi_iio_get_chan_type(sensor_info->axis[i].type, &type);
		if (ret < 0)
			return ERR_PTR(ret);

		ret = scmi_iio_get_chan_modifier(sensor_info->axis[i].name,
						 &modifier);
		if (ret < 0)
			return ERR_PTR(ret);

		scmi_iio_set_data_channel(&iio_channels[i], type, modifier,
					  sensor_info->axis[i].id);
	}

	ret = handle->notify_ops->devm_event_notifier_register(sdev,
				SCMI_PROTOCOL_SENSOR, SCMI_EVENT_SENSOR_UPDATE,
				&sensor->sensor_info->id,
				&sensor->sensor_update_nb);
	if (ret) {
		dev_err(&iiodev->dev,
			"Error in registering sensor update notifier for sensor %s err %d",
			sensor->sensor_info->name, ret);
		return ERR_PTR(ret);
	}

	scmi_iio_set_timestamp_channel(&iio_channels[i], i);
	iiodev->channels = iio_channels;
	return iiodev;
}

static int scmi_iio_dev_probe(struct scmi_device *sdev)
{
	const struct scmi_sensor_info *sensor_info;
	struct scmi_handle *handle = sdev->handle;
	const struct scmi_sensor_proto_ops *sensor_ops;
	struct scmi_protocol_handle *ph;
	struct device *dev = &sdev->dev;
	struct iio_dev *scmi_iio_dev;
	u16 nr_sensors;
	int err = -ENODEV, i;

	if (!handle)
		return -ENODEV;

	sensor_ops = handle->devm_protocol_get(sdev, SCMI_PROTOCOL_SENSOR, &ph);
	if (IS_ERR(sensor_ops)) {
		dev_err(dev, "SCMI device has no sensor interface\n");
		return PTR_ERR(sensor_ops);
	}

	nr_sensors = sensor_ops->count_get(ph);
	if (!nr_sensors) {
		dev_dbg(dev, "0 sensors found via SCMI bus\n");
		return -ENODEV;
	}

	for (i = 0; i < nr_sensors; i++) {
		sensor_info = sensor_ops->info_get(ph, i);
		if (!sensor_info) {
			dev_err(dev, "SCMI sensor %d has missing info\n", i);
			return -EINVAL;
		}

		/* This driver only supports 3-axis accel and gyro, skipping other sensors */
		if (sensor_info->num_axis != SCMI_IIO_NUM_OF_AXIS)
			continue;

		/* This driver only supports 3-axis accel and gyro, skipping other sensors */
		if (sensor_info->axis[0].type != METERS_SEC_SQUARED &&
		    sensor_info->axis[0].type != RADIANS_SEC)
			continue;

		scmi_iio_dev = scmi_alloc_iiodev(sdev, sensor_ops, ph,
						 sensor_info);
		if (IS_ERR(scmi_iio_dev)) {
			dev_err(dev,
				"failed to allocate IIO device for sensor %s: %ld\n",
				sensor_info->name, PTR_ERR(scmi_iio_dev));
			return PTR_ERR(scmi_iio_dev);
		}

		err = devm_iio_kfifo_buffer_setup(&scmi_iio_dev->dev,
						  scmi_iio_dev,
						  &scmi_iio_buffer_ops);
		if (err < 0) {
			dev_err(dev,
				"IIO buffer setup error at sensor %s: %d\n",
				sensor_info->name, err);
			return err;
		}

		err = devm_iio_device_register(dev, scmi_iio_dev);
		if (err) {
			dev_err(dev,
				"IIO device registration failed at sensor %s: %d\n",
				sensor_info->name, err);
			return err;
		}
	}
	return err;
}

static const struct scmi_device_id scmi_id_table[] = {
	{ SCMI_PROTOCOL_SENSOR, "iiodev" },
	{},
};

MODULE_DEVICE_TABLE(scmi, scmi_id_table);

static struct scmi_driver scmi_iiodev_driver = {
	.name = "scmi-sensor-iiodev",
	.probe = scmi_iio_dev_probe,
	.id_table = scmi_id_table,
};

module_scmi_driver(scmi_iiodev_driver);

MODULE_AUTHOR("Jyoti Bhayana <jbhayana@google.com>");
MODULE_DESCRIPTION("SCMI IIO Driver");
MODULE_LICENSE("GPL v2");
