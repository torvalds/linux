// SPDX-License-Identifier: GPL-2.0-only
/* The industrial I/O core in kernel channel mapping
 *
 * Copyright (c) 2011 Jonathan Cameron
 */
#include <linux/err.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/of.h>

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
		list_add_tail(&mapi->l, &iio_map_list);
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
	struct iio_map_internal *mapi, *next;

	mutex_lock(&iio_map_list_lock);
	list_for_each_entry_safe(mapi, next, &iio_map_list, l) {
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

#ifdef CONFIG_OF

static int iio_dev_node_match(struct device *dev, const void *data)
{
	return dev->of_node == data && dev->type == &iio_device_type;
}

/**
 * __of_iio_simple_xlate - translate iiospec to the IIO channel index
 * @indio_dev:	pointer to the iio_dev structure
 * @iiospec:	IIO specifier as found in the device tree
 *
 * This is simple translation function, suitable for the most 1:1 mapped
 * channels in IIO chips. This function performs only one sanity check:
 * whether IIO index is less than num_channels (that is specified in the
 * iio_dev).
 */
static int __of_iio_simple_xlate(struct iio_dev *indio_dev,
				const struct of_phandle_args *iiospec)
{
	if (!iiospec->args_count)
		return 0;

	if (iiospec->args[0] >= indio_dev->num_channels) {
		dev_err(&indio_dev->dev, "invalid channel index %u\n",
			iiospec->args[0]);
		return -EINVAL;
	}

	return iiospec->args[0];
}

static int __of_iio_channel_get(struct iio_channel *channel,
				struct device_node *np, int index)
{
	struct device *idev;
	struct iio_dev *indio_dev;
	int err;
	struct of_phandle_args iiospec;

	err = of_parse_phandle_with_args(np, "io-channels",
					 "#io-channel-cells",
					 index, &iiospec);
	if (err)
		return err;

	idev = bus_find_device(&iio_bus_type, NULL, iiospec.np,
			       iio_dev_node_match);
	of_node_put(iiospec.np);
	if (idev == NULL)
		return -EPROBE_DEFER;

	indio_dev = dev_to_iio_dev(idev);
	channel->indio_dev = indio_dev;
	if (indio_dev->info->of_xlate)
		index = indio_dev->info->of_xlate(indio_dev, &iiospec);
	else
		index = __of_iio_simple_xlate(indio_dev, &iiospec);
	if (index < 0)
		goto err_put;
	channel->channel = &indio_dev->channels[index];

	return 0;

err_put:
	iio_device_put(indio_dev);
	return index;
}

static struct iio_channel *of_iio_channel_get(struct device_node *np, int index)
{
	struct iio_channel *channel;
	int err;

	if (index < 0)
		return ERR_PTR(-EINVAL);

	channel = kzalloc(sizeof(*channel), GFP_KERNEL);
	if (channel == NULL)
		return ERR_PTR(-ENOMEM);

	err = __of_iio_channel_get(channel, np, index);
	if (err)
		goto err_free_channel;

	return channel;

err_free_channel:
	kfree(channel);
	return ERR_PTR(err);
}

static struct iio_channel *of_iio_channel_get_by_name(struct device_node *np,
						      const char *name)
{
	struct iio_channel *chan = NULL;

	/* Walk up the tree of devices looking for a matching iio channel */
	while (np) {
		int index = 0;

		/*
		 * For named iio channels, first look up the name in the
		 * "io-channel-names" property.  If it cannot be found, the
		 * index will be an error code, and of_iio_channel_get()
		 * will fail.
		 */
		if (name)
			index = of_property_match_string(np, "io-channel-names",
							 name);
		chan = of_iio_channel_get(np, index);
		if (!IS_ERR(chan) || PTR_ERR(chan) == -EPROBE_DEFER)
			break;
		else if (name && index >= 0) {
			pr_err("ERROR: could not get IIO channel %pOF:%s(%i)\n",
				np, name ? name : "", index);
			return NULL;
		}

		/*
		 * No matching IIO channel found on this node.
		 * If the parent node has a "io-channel-ranges" property,
		 * then we can try one of its channels.
		 */
		np = np->parent;
		if (np && !of_get_property(np, "io-channel-ranges", NULL))
			return NULL;
	}

	return chan;
}

static struct iio_channel *of_iio_channel_get_all(struct device *dev)
{
	struct iio_channel *chans;
	int i, mapind, nummaps = 0;
	int ret;

	do {
		ret = of_parse_phandle_with_args(dev->of_node,
						 "io-channels",
						 "#io-channel-cells",
						 nummaps, NULL);
		if (ret < 0)
			break;
	} while (++nummaps);

	if (nummaps == 0)	/* no error, return NULL to search map table */
		return NULL;

	/* NULL terminated array to save passing size */
	chans = kcalloc(nummaps + 1, sizeof(*chans), GFP_KERNEL);
	if (chans == NULL)
		return ERR_PTR(-ENOMEM);

	/* Search for OF matches */
	for (mapind = 0; mapind < nummaps; mapind++) {
		ret = __of_iio_channel_get(&chans[mapind], dev->of_node,
					   mapind);
		if (ret)
			goto error_free_chans;
	}
	return chans;

error_free_chans:
	for (i = 0; i < mapind; i++)
		iio_device_put(chans[i].indio_dev);
	kfree(chans);
	return ERR_PTR(ret);
}

#else /* CONFIG_OF */

static inline struct iio_channel *
of_iio_channel_get_by_name(struct device_node *np, const char *name)
{
	return NULL;
}

static inline struct iio_channel *of_iio_channel_get_all(struct device *dev)
{
	return NULL;
}

#endif /* CONFIG_OF */

static struct iio_channel *iio_channel_get_sys(const char *name,
					       const char *channel_name)
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

struct iio_channel *iio_channel_get(struct device *dev,
				    const char *channel_name)
{
	const char *name = dev ? dev_name(dev) : NULL;
	struct iio_channel *channel;

	if (dev) {
		channel = of_iio_channel_get_by_name(dev->of_node,
						     channel_name);
		if (channel != NULL)
			return channel;
	}

	return iio_channel_get_sys(name, channel_name);
}
EXPORT_SYMBOL_GPL(iio_channel_get);

void iio_channel_release(struct iio_channel *channel)
{
	if (!channel)
		return;
	iio_device_put(channel->indio_dev);
	kfree(channel);
}
EXPORT_SYMBOL_GPL(iio_channel_release);

static void devm_iio_channel_free(struct device *dev, void *res)
{
	struct iio_channel *channel = *(struct iio_channel **)res;

	iio_channel_release(channel);
}

struct iio_channel *devm_iio_channel_get(struct device *dev,
					 const char *channel_name)
{
	struct iio_channel **ptr, *channel;

	ptr = devres_alloc(devm_iio_channel_free, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	channel = iio_channel_get(dev, channel_name);
	if (IS_ERR(channel)) {
		devres_free(ptr);
		return channel;
	}

	*ptr = channel;
	devres_add(dev, ptr);

	return channel;
}
EXPORT_SYMBOL_GPL(devm_iio_channel_get);

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

	chans = of_iio_channel_get_all(dev);
	if (chans)
		return chans;

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
	chans = kcalloc(nummaps + 1, sizeof(*chans), GFP_KERNEL);
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

static void devm_iio_channel_free_all(struct device *dev, void *res)
{
	struct iio_channel *channels = *(struct iio_channel **)res;

	iio_channel_release_all(channels);
}

struct iio_channel *devm_iio_channel_get_all(struct device *dev)
{
	struct iio_channel **ptr, *channels;

	ptr = devres_alloc(devm_iio_channel_free_all, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	channels = iio_channel_get_all(dev);
	if (IS_ERR(channels)) {
		devres_free(ptr);
		return channels;
	}

	*ptr = channels;
	devres_add(dev, ptr);

	return channels;
}
EXPORT_SYMBOL_GPL(devm_iio_channel_get_all);

static int iio_channel_read(struct iio_channel *chan, int *val, int *val2,
	enum iio_chan_info_enum info)
{
	int unused;
	int vals[INDIO_MAX_RAW_ELEMENTS];
	int ret;
	int val_len = 2;

	if (val2 == NULL)
		val2 = &unused;

	if (!iio_channel_has_info(chan->channel, info))
		return -EINVAL;

	if (chan->indio_dev->info->read_raw_multi) {
		ret = chan->indio_dev->info->read_raw_multi(chan->indio_dev,
					chan->channel, INDIO_MAX_RAW_ELEMENTS,
					vals, &val_len, info);
		*val = vals[0];
		*val2 = vals[1];
	} else
		ret = chan->indio_dev->info->read_raw(chan->indio_dev,
					chan->channel, val, val2, info);

	return ret;
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

int iio_read_channel_average_raw(struct iio_channel *chan, int *val)
{
	int ret;

	mutex_lock(&chan->indio_dev->info_exist_lock);
	if (chan->indio_dev->info == NULL) {
		ret = -ENODEV;
		goto err_unlock;
	}

	ret = iio_channel_read(chan, val, NULL, IIO_CHAN_INFO_AVERAGE_RAW);
err_unlock:
	mutex_unlock(&chan->indio_dev->info_exist_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(iio_read_channel_average_raw);

static int iio_convert_raw_to_processed_unlocked(struct iio_channel *chan,
	int raw, int *processed, unsigned int scale)
{
	int scale_type, scale_val, scale_val2;
	int offset_type, offset_val, offset_val2;
	s64 raw64 = raw;

	offset_type = iio_channel_read(chan, &offset_val, &offset_val2,
				       IIO_CHAN_INFO_OFFSET);
	if (offset_type >= 0) {
		switch (offset_type) {
		case IIO_VAL_INT:
			break;
		case IIO_VAL_INT_PLUS_MICRO:
		case IIO_VAL_INT_PLUS_NANO:
			/*
			 * Both IIO_VAL_INT_PLUS_MICRO and IIO_VAL_INT_PLUS_NANO
			 * implicitely truncate the offset to it's integer form.
			 */
			break;
		case IIO_VAL_FRACTIONAL:
			offset_val /= offset_val2;
			break;
		case IIO_VAL_FRACTIONAL_LOG2:
			offset_val >>= offset_val2;
			break;
		default:
			return -EINVAL;
		}

		raw64 += offset_val;
	}

	scale_type = iio_channel_read(chan, &scale_val, &scale_val2,
					IIO_CHAN_INFO_SCALE);
	if (scale_type < 0) {
		/*
		 * If no channel scaling is available apply consumer scale to
		 * raw value and return.
		 */
		*processed = raw * scale;
		return 0;
	}

	switch (scale_type) {
	case IIO_VAL_INT:
		*processed = raw64 * scale_val * scale;
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

int iio_read_channel_attribute(struct iio_channel *chan, int *val, int *val2,
			       enum iio_chan_info_enum attribute)
{
	int ret;

	mutex_lock(&chan->indio_dev->info_exist_lock);
	if (chan->indio_dev->info == NULL) {
		ret = -ENODEV;
		goto err_unlock;
	}

	ret = iio_channel_read(chan, val, val2, attribute);
err_unlock:
	mutex_unlock(&chan->indio_dev->info_exist_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(iio_read_channel_attribute);

int iio_read_channel_offset(struct iio_channel *chan, int *val, int *val2)
{
	return iio_read_channel_attribute(chan, val, val2, IIO_CHAN_INFO_OFFSET);
}
EXPORT_SYMBOL_GPL(iio_read_channel_offset);

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
	return iio_read_channel_attribute(chan, val, val2, IIO_CHAN_INFO_SCALE);
}
EXPORT_SYMBOL_GPL(iio_read_channel_scale);

static int iio_channel_read_avail(struct iio_channel *chan,
				  const int **vals, int *type, int *length,
				  enum iio_chan_info_enum info)
{
	if (!iio_channel_has_available(chan->channel, info))
		return -EINVAL;

	return chan->indio_dev->info->read_avail(chan->indio_dev, chan->channel,
						 vals, type, length, info);
}

int iio_read_avail_channel_attribute(struct iio_channel *chan,
				     const int **vals, int *type, int *length,
				     enum iio_chan_info_enum attribute)
{
	int ret;

	mutex_lock(&chan->indio_dev->info_exist_lock);
	if (!chan->indio_dev->info) {
		ret = -ENODEV;
		goto err_unlock;
	}

	ret = iio_channel_read_avail(chan, vals, type, length, attribute);
err_unlock:
	mutex_unlock(&chan->indio_dev->info_exist_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(iio_read_avail_channel_attribute);

int iio_read_avail_channel_raw(struct iio_channel *chan,
			       const int **vals, int *length)
{
	int ret;
	int type;

	ret = iio_read_avail_channel_attribute(chan, vals, &type, length,
					 IIO_CHAN_INFO_RAW);

	if (ret >= 0 && type != IIO_VAL_INT)
		/* raw values are assumed to be IIO_VAL_INT */
		ret = -EINVAL;

	return ret;
}
EXPORT_SYMBOL_GPL(iio_read_avail_channel_raw);

static int iio_channel_read_max(struct iio_channel *chan,
				int *val, int *val2, int *type,
				enum iio_chan_info_enum info)
{
	int unused;
	const int *vals;
	int length;
	int ret;

	if (!val2)
		val2 = &unused;

	ret = iio_channel_read_avail(chan, &vals, type, &length, info);
	switch (ret) {
	case IIO_AVAIL_RANGE:
		switch (*type) {
		case IIO_VAL_INT:
			*val = vals[2];
			break;
		default:
			*val = vals[4];
			*val2 = vals[5];
		}
		return 0;

	case IIO_AVAIL_LIST:
		if (length <= 0)
			return -EINVAL;
		switch (*type) {
		case IIO_VAL_INT:
			*val = vals[--length];
			while (length) {
				if (vals[--length] > *val)
					*val = vals[length];
			}
			break;
		default:
			/* FIXME: learn about max for other iio values */
			return -EINVAL;
		}
		return 0;

	default:
		return ret;
	}
}

int iio_read_max_channel_raw(struct iio_channel *chan, int *val)
{
	int ret;
	int type;

	mutex_lock(&chan->indio_dev->info_exist_lock);
	if (!chan->indio_dev->info) {
		ret = -ENODEV;
		goto err_unlock;
	}

	ret = iio_channel_read_max(chan, val, NULL, &type, IIO_CHAN_INFO_RAW);
err_unlock:
	mutex_unlock(&chan->indio_dev->info_exist_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(iio_read_max_channel_raw);

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

static int iio_channel_write(struct iio_channel *chan, int val, int val2,
			     enum iio_chan_info_enum info)
{
	return chan->indio_dev->info->write_raw(chan->indio_dev,
						chan->channel, val, val2, info);
}

int iio_write_channel_attribute(struct iio_channel *chan, int val, int val2,
				enum iio_chan_info_enum attribute)
{
	int ret;

	mutex_lock(&chan->indio_dev->info_exist_lock);
	if (chan->indio_dev->info == NULL) {
		ret = -ENODEV;
		goto err_unlock;
	}

	ret = iio_channel_write(chan, val, val2, attribute);
err_unlock:
	mutex_unlock(&chan->indio_dev->info_exist_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(iio_write_channel_attribute);

int iio_write_channel_raw(struct iio_channel *chan, int val)
{
	return iio_write_channel_attribute(chan, val, 0, IIO_CHAN_INFO_RAW);
}
EXPORT_SYMBOL_GPL(iio_write_channel_raw);

unsigned int iio_get_channel_ext_info_count(struct iio_channel *chan)
{
	const struct iio_chan_spec_ext_info *ext_info;
	unsigned int i = 0;

	if (!chan->channel->ext_info)
		return i;

	for (ext_info = chan->channel->ext_info; ext_info->name; ext_info++)
		++i;

	return i;
}
EXPORT_SYMBOL_GPL(iio_get_channel_ext_info_count);

static const struct iio_chan_spec_ext_info *iio_lookup_ext_info(
						const struct iio_channel *chan,
						const char *attr)
{
	const struct iio_chan_spec_ext_info *ext_info;

	if (!chan->channel->ext_info)
		return NULL;

	for (ext_info = chan->channel->ext_info; ext_info->name; ++ext_info) {
		if (!strcmp(attr, ext_info->name))
			return ext_info;
	}

	return NULL;
}

ssize_t iio_read_channel_ext_info(struct iio_channel *chan,
				  const char *attr, char *buf)
{
	const struct iio_chan_spec_ext_info *ext_info;

	ext_info = iio_lookup_ext_info(chan, attr);
	if (!ext_info)
		return -EINVAL;

	return ext_info->read(chan->indio_dev, ext_info->private,
			      chan->channel, buf);
}
EXPORT_SYMBOL_GPL(iio_read_channel_ext_info);

ssize_t iio_write_channel_ext_info(struct iio_channel *chan, const char *attr,
				   const char *buf, size_t len)
{
	const struct iio_chan_spec_ext_info *ext_info;

	ext_info = iio_lookup_ext_info(chan, attr);
	if (!ext_info)
		return -EINVAL;

	return ext_info->write(chan->indio_dev, ext_info->private,
			       chan->channel, buf, len);
}
EXPORT_SYMBOL_GPL(iio_write_channel_ext_info);
