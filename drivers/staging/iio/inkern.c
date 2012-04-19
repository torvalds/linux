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

#include "iio.h"
#include "iio_core.h"
#include "machine.h"
#include "driver.h"
#include "consumer.h"

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


/* Assumes the exact same array (e.g. memory locations)
 * used at unregistration as used at registration rather than
 * more complex checking of contents.
 */
int iio_map_array_unregister(struct iio_dev *indio_dev,
			     struct iio_map *maps)
{
	int i = 0, ret = 0;
	bool found_it;
	struct iio_map_internal *mapi;

	if (maps == NULL)
		return 0;

	mutex_lock(&iio_map_list_lock);
	while (maps[i].consumer_dev_name != NULL) {
		found_it = false;
		list_for_each_entry(mapi, &iio_map_list, l)
			if (&maps[i] == mapi->map) {
				list_del(&mapi->l);
				kfree(mapi);
				found_it = true;
				break;
			}
		if (found_it == false) {
			ret = -ENODEV;
			goto error_ret;
		}
		i++;
	}
error_ret:
	mutex_unlock(&iio_map_list_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(iio_map_array_unregister);

static const struct iio_chan_spec
*iio_chan_spec_from_name(const struct iio_dev *indio_dev,
			 const char *name)
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


struct iio_channel *iio_st_channel_get(const char *name,
				       const char *channel_name)
{
	struct iio_map_internal *c_i = NULL, *c = NULL;
	struct iio_channel *channel;

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
		get_device(&c->indio_dev->dev);
		break;
	}
	mutex_unlock(&iio_map_list_lock);
	if (c == NULL)
		return ERR_PTR(-ENODEV);

	channel = kmalloc(sizeof(*channel), GFP_KERNEL);
	if (channel == NULL)
		return ERR_PTR(-ENOMEM);

	channel->indio_dev = c->indio_dev;

	if (c->map->adc_channel_label)
		channel->channel =
			iio_chan_spec_from_name(channel->indio_dev,
						c->map->adc_channel_label);

	return channel;
}
EXPORT_SYMBOL_GPL(iio_st_channel_get);

void iio_st_channel_release(struct iio_channel *channel)
{
	put_device(&channel->indio_dev->dev);
	kfree(channel);
}
EXPORT_SYMBOL_GPL(iio_st_channel_release);

struct iio_channel *iio_st_channel_get_all(const char *name)
{
	struct iio_channel *chans;
	struct iio_map_internal *c = NULL;
	int nummaps = 0;
	int mapind = 0;
	int i, ret;

	if (name == NULL)
		return ERR_PTR(-EINVAL);

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
		chans[mapind].channel =
			iio_chan_spec_from_name(chans[mapind].indio_dev,
						c->map->adc_channel_label);
		if (chans[mapind].channel == NULL) {
			ret = -EINVAL;
			put_device(&chans[mapind].indio_dev->dev);
			goto error_free_chans;
		}
		get_device(&chans[mapind].indio_dev->dev);
		mapind++;
	}
	mutex_unlock(&iio_map_list_lock);
	if (mapind == 0) {
		ret = -ENODEV;
		goto error_free_chans;
	}
	return chans;

error_free_chans:
	for (i = 0; i < nummaps; i++)
		if (chans[i].indio_dev)
			put_device(&chans[i].indio_dev->dev);
	kfree(chans);
error_ret:
	mutex_unlock(&iio_map_list_lock);

	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(iio_st_channel_get_all);

void iio_st_channel_release_all(struct iio_channel *channels)
{
	struct iio_channel *chan = &channels[0];

	while (chan->indio_dev) {
		put_device(&chan->indio_dev->dev);
		chan++;
	}
	kfree(channels);
}
EXPORT_SYMBOL_GPL(iio_st_channel_release_all);

int iio_st_read_channel_raw(struct iio_channel *chan, int *val)
{
	int val2, ret;

	mutex_lock(&chan->indio_dev->info_exist_lock);
	if (chan->indio_dev->info == NULL) {
		ret = -ENODEV;
		goto err_unlock;
	}

	ret = chan->indio_dev->info->read_raw(chan->indio_dev, chan->channel,
					      val, &val2, 0);
err_unlock:
	mutex_unlock(&chan->indio_dev->info_exist_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(iio_st_read_channel_raw);

int iio_st_read_channel_scale(struct iio_channel *chan, int *val, int *val2)
{
	int ret;

	mutex_lock(&chan->indio_dev->info_exist_lock);
	if (chan->indio_dev->info == NULL) {
		ret = -ENODEV;
		goto err_unlock;
	}

	ret = chan->indio_dev->info->read_raw(chan->indio_dev,
					      chan->channel,
					      val, val2,
					      IIO_CHAN_INFO_SCALE);
err_unlock:
	mutex_unlock(&chan->indio_dev->info_exist_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(iio_st_read_channel_scale);

int iio_st_get_channel_type(struct iio_channel *chan,
			    enum iio_chan_type *type)
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
EXPORT_SYMBOL_GPL(iio_st_get_channel_type);
