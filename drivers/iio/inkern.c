/* The industrial I/O core in kernel channel mapping
 *
 * Copyright (c) 2011 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#include <linux/err.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/mutex.h>

#include <linux/iio/iio.h>
#include "iio_core.h"
#include <linux/iio/machine.h>
#include <linux/iio/driver.h>
#include <linux/iio/consumer.h>

struct iio_map_internal {
	struct iio_dev *indio_dev;
	struct iio_map *map;
	struct list_head l;
};

static LIST_HEAD(iio_map_list);
static DEFINE_MUTEX(iio_map_list_lock);

int iio_map_array_register(struct iio_dev *indio_dev, struct iio_map *maps)
{
	int i = 0, ret = 0;
	struct iio_map_internal *mapi;

	if (maps == NULL)
		return 0;

	mutex_lock(&iio_map_list_lock);
	while (maps[i].consumer_dev_name != NULL) {
		mapi = kzalloc(sizeof(*mapi), GFP_KERNEL);
		if (mapi == NULL) {
			ret = -ENOMEM;
			goto error_ret;
		}
		mapi->map = &maps[i];
		mapi->indio_dev = indio_dev;
		list_add(&mapi->l, &iio_map_list);
		i++;
	}
error_ret:
	mutex_unlock(&iio_map_list_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(iio_map_array_register);


/*
 * Remove all map entries associated with the given iio device
 */
int iio_map_array_unregister(struct iio_dev *indio_dev)
{
	int ret = -ENODEV;
	struct iio_map_internal *mapi;
	struct list_head *pos, *tmp;

	mutex_lock(&iio_map_list_lock);
	list_for_each_safe(pos, tmp, &iio_map_list) {
		mapi = list_entry(pos, struct iio_map_internal, l);
		if (indio_dev == mapi->indio_dev) {
			list_del(&mapi->l);
			kfree(mapi);
			ret = 0;
		}
	}
	mutex_unlock(&iio_map_list_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(iio_map_array_unregister);

static const struct iio_chan_spec
*iio_chan_spec_from_name(const struct iio_dev *indio_dev, const char *name)
{
	int i;
	const struct iio_chan_spec *chan = NULL;

	for (i = 0; i < indio_dev->num_channels; i++)
		if (indio_dev->channels[i].datasheet_name &&
		    strcmp(name, indio_dev->channels[i].datasheet_name) == 0) {
			chan = &indio_dev->channels[i];
			break;
		}
	return chan;
}


struct iio_channel *iio_channel_get(const char *name, const char *channel_name)
{
	struct iio_map_internal *c_i = NULL, *c = NULL;
	struct iio_channel *channel;
	int err;

	if (name == NULL && channel_name == NULL)
		return ERR_PTR(-ENODEV);

	/* first find matching entry the channel map */
	mutex_lock(&iio_map_list_lock);
	list_for_each_entry(c_i, &iio_map_list, l) {
		if ((name && strcmp(name, c_i->map->consumer_dev_name) != 0) ||
		    (channel_name &&
		     strcmp(channel_name, c_i->map->consumer_channel) != 0))
			continue;
		c = c_i;
		iio_device_get(c->indio_dev);
		break;
	}
	mutex_unlock(&iio_map_list_lock);
	if (c == NULL)
		return ERR_PTR(-ENODEV);

	channel = kzalloc(sizeof(*channel), GFP_KERNEL);
	if (channel == NULL) {
		err = -ENOMEM;
		goto error_no_mem;
	}

	channel->indio_dev = c->indio_dev;

	if (c->map->adc_channel_label) {
		channel->channel =
			iio_chan_spec_from_name(channel->indio_dev,
						c->map->adc_channel_label);

		if (channel->channel == NULL) {
			err = -EINVAL;
			goto error_no_chan;
		}
	}

	return channel;

error_no_chan:
	kfree(channel);
error_no_mem:
	iio_device_put(c->indio_dev);
	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(iio_channel_get);

void iio_channel_release(struct iio_channel *channel)
{
	iio_device_put(channel->indio_dev);
	kfree(channel);
}
EXPORT_SYMBOL_GPL(iio_channel_release);

struct iio_channel *iio_channel_get_all(struct device *dev)
{
	const char *name;
	struct iio_channel *chans;
	struct iio_map_internal *c = NULL;
	int nummaps = 0;
	int mapind = 0;
	int i, ret;

	if (dev == NULL)
		return ERR_PTR(-EINVAL);
	name = dev_name(dev);

	mutex_lock(&iio_map_list_lock);
	/* first count the matching maps */
	list_for_each_entry(c, &iio_map_list, l)
		if (name && strcmp(name, c->map->consumer_dev_name) != 0)
			continue;
		else
			nummaps++;

	if (nummaps == 0) {
		ret = -ENODEV;
		goto error_ret;
	}

	/* NULL terminated array to save passing size */
	chans = kzalloc(sizeof(*chans)*(nummaps + 1), GFP_KERNEL);
	if (chans == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}

	/* for each map fill in the chans element */
	list_for_each_entry(c, &iio_map_list, l) {
		if (name && strcmp(name, c->map->consumer_dev_name) != 0)
			continue;
		chans[mapind].indio_dev = c->indio_dev;
		chans[mapind].data = c->map->consumer_data;
		chans[mapind].channel =
			iio_chan_spec_from_name(chans[mapind].indio_dev,
						c->map->adc_channel_label);
		if (chans[mapind].channel == NULL) {
			ret = -EINVAL;
			goto error_free_chans;
		}
		iio_device_get(chans[mapind].indio_dev);
		mapind++;
	}
	if (mapind == 0) {
		ret = -ENODEV;
		goto error_free_chans;
	}
	mutex_unlock(&iio_map_list_lock);

	return chans;

error_free_chans:
	for (i = 0; i < nummaps; i++)
		iio_device_put(chans[i].indio_dev);
	kfree(chans);
error_ret:
	mutex_unlock(&iio_map_list_lock);

	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(iio_channel_get_all);

void iio_channel_release_all(struct iio_channel *channels)
{
	struct iio_channel *chan = &channels[0];

	while (chan->indio_dev) {
		iio_device_put(chan->indio_dev);
		chan++;
	}
	kfree(channels);
}
EXPORT_SYMBOL_GPL(iio_channel_release_all);

static int iio_channel_read(struct iio_channel *chan, int *val, int *val2,
	enum iio_chan_info_enum info)
{
	int unused;

	if (val2 == NULL)
		val2 = &unused;

	return chan->indio_dev->info->read_raw(chan->indio_dev, chan->channel,
						val, val2, info);
}

int iio_read_channel_raw(struct iio_channel *chan, int *val)
{
	int ret;

	mutex_lock(&chan->indio_dev->info_exist_lock);
	if (chan->indio_dev->info == NULL) {
		ret = -ENODEV;
		goto err_unlock;
	}

	ret = iio_channel_read(chan, val, NULL, IIO_CHAN_INFO_RAW);
err_unlock:
	mutex_unlock(&chan->indio_dev->info_exist_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(iio_read_channel_raw);

static int iio_convert_raw_to_processed_unlocked(struct iio_channel *chan,
	int raw, int *processed, unsigned int scale)
{
	int scale_type, scale_val, scale_val2, offset;
	s64 raw64 = raw;
	int ret;

	ret = iio_channel_read(chan, &offset, NULL, IIO_CHAN_INFO_SCALE);
	if (ret == 0)
		raw64 += offset;

	scale_type = iio_channel_read(chan, &scale_val, &scale_val2,
					IIO_CHAN_INFO_SCALE);
	if (scale_type < 0)
		return scale_type;

	switch (scale_type) {
	case IIO_VAL_INT:
		*processed = raw64 * scale_val;
		break;
	case IIO_VAL_INT_PLUS_MICRO:
		if (scale_val2 < 0)
			*processed = -raw64 * scale_val;
		else
			*processed = raw64 * scale_val;
		*processed += div_s64(raw64 * (s64)scale_val2 * scale,
				      1000000LL);
		break;
	case IIO_VAL_INT_PLUS_NANO:
		if (scale_val2 < 0)
			*processed = -raw64 * scale_val;
		else
			*processed = raw64 * scale_val;
		*processed += div_s64(raw64 * (s64)scale_val2 * scale,
				      1000000000LL);
		break;
	case IIO_VAL_FRACTIONAL:
		*processed = div_s64(raw64 * (s64)scale_val * scale,
				     scale_val2);
		break;
	case IIO_VAL_FRACTIONAL_LOG2:
		*processed = (raw64 * (s64)scale_val * scale) >> scale_val2;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

int iio_convert_raw_to_processed(struct iio_channel *chan, int raw,
	int *processed, unsigned int scale)
{
	int ret;

	mutex_lock(&chan->indio_dev->info_exist_lock);
	if (chan->indio_dev->info == NULL) {
		ret = -ENODEV;
		goto err_unlock;
	}

	ret = iio_convert_raw_to_processed_unlocked(chan, raw, processed,
							scale);
err_unlock:
	mutex_unlock(&chan->indio_dev->info_exist_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(iio_convert_raw_to_processed);

int iio_read_channel_processed(struct iio_channel *chan, int *val)
{
	int ret;

	mutex_lock(&chan->indio_dev->info_exist_lock);
	if (chan->indio_dev->info == NULL) {
		ret = -ENODEV;
		goto err_unlock;
	}

	if (iio_channel_has_info(chan->channel, IIO_CHAN_INFO_PROCESSED)) {
		ret = iio_channel_read(chan, val, NULL,
				       IIO_CHAN_INFO_PROCESSED);
	} else {
		ret = iio_channel_read(chan, val, NULL, IIO_CHAN_INFO_RAW);
		if (ret < 0)
			goto err_unlock;
		ret = iio_convert_raw_to_processed_unlocked(chan, *val, val, 1);
	}

err_unlock:
	mutex_unlock(&chan->indio_dev->info_exist_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(iio_read_channel_processed);

int iio_read_channel_scale(struct iio_channel *chan, int *val, int *val2)
{
	int ret;

	mutex_lock(&chan->indio_dev->info_exist_lock);
	if (chan->indio_dev->info == NULL) {
		ret = -ENODEV;
		goto err_unlock;
	}

	ret = iio_channel_read(chan, val, val2, IIO_CHAN_INFO_SCALE);
err_unlock:
	mutex_unlock(&chan->indio_dev->info_exist_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(iio_read_channel_scale);

int iio_get_channel_type(struct iio_channel *chan, enum iio_chan_type *type)
{
	int ret = 0;
	/* Need to verify underlying driver has not gone away */

	mutex_lock(&chan->indio_dev->info_exist_lock);
	if (chan->indio_dev->info == NULL) {
		ret = -ENODEV;
		goto err_unlock;
	}

	*type = chan->channel->type;
err_unlock:
	mutex_unlock(&chan->indio_dev->info_exist_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(iio_get_channel_type);
