// SPDX-License-Identifier: GPL-2.0-only
/* The industrial I/O core in kernel channel mapping
 *
 * Copyright (c) 2011 Jonathan Cameron
 */
#include <linux/cleanup.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/minmax.h>
#include <linux/mutex.h>
#include <linux/property.h>
#include <linux/slab.h>

#include <linux/iio/iio.h>
#include <linux/iio/iio-opaque.h>
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

static int iio_map_array_unregister_locked(struct iio_dev *indio_dev)
{
	int ret = -ENODEV;
	struct iio_map_internal *mapi, *next;

	list_for_each_entry_safe(mapi, next, &iio_map_list, l) {
		if (indio_dev == mapi->indio_dev) {
			list_del(&mapi->l);
			kfree(mapi);
			ret = 0;
		}
	}
	return ret;
}

int iio_map_array_register(struct iio_dev *indio_dev, struct iio_map *maps)
{
	struct iio_map_internal *mapi;
	int i = 0;
	int ret;

	if (!maps)
		return 0;

	guard(mutex)(&iio_map_list_lock);
	while (maps[i].consumer_dev_name) {
		mapi = kzalloc(sizeof(*mapi), GFP_KERNEL);
		if (!mapi) {
			ret = -ENOMEM;
			goto error_ret;
		}
		mapi->map = &maps[i];
		mapi->indio_dev = indio_dev;
		list_add_tail(&mapi->l, &iio_map_list);
		i++;
	}

	return 0;
error_ret:
	iio_map_array_unregister_locked(indio_dev);
	return ret;
}
EXPORT_SYMBOL_GPL(iio_map_array_register);

/*
 * Remove all map entries associated with the given iio device
 */
int iio_map_array_unregister(struct iio_dev *indio_dev)
{
	guard(mutex)(&iio_map_list_lock);
	return iio_map_array_unregister_locked(indio_dev);
}
EXPORT_SYMBOL_GPL(iio_map_array_unregister);

static void iio_map_array_unregister_cb(void *indio_dev)
{
	iio_map_array_unregister(indio_dev);
}

int devm_iio_map_array_register(struct device *dev, struct iio_dev *indio_dev, struct iio_map *maps)
{
	int ret;

	ret = iio_map_array_register(indio_dev, maps);
	if (ret)
		return ret;

	return devm_add_action_or_reset(dev, iio_map_array_unregister_cb, indio_dev);
}
EXPORT_SYMBOL_GPL(devm_iio_map_array_register);

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

/**
 * __fwnode_iio_simple_xlate - translate iiospec to the IIO channel index
 * @indio_dev:	pointer to the iio_dev structure
 * @iiospec:	IIO specifier as found in the device tree
 *
 * This is simple translation function, suitable for the most 1:1 mapped
 * channels in IIO chips. This function performs only one sanity check:
 * whether IIO index is less than num_channels (that is specified in the
 * iio_dev).
 */
static int __fwnode_iio_simple_xlate(struct iio_dev *indio_dev,
				     const struct fwnode_reference_args *iiospec)
{
	if (!iiospec->nargs)
		return 0;

	if (iiospec->args[0] >= indio_dev->num_channels) {
		dev_err(&indio_dev->dev, "invalid channel index %llu\n",
			iiospec->args[0]);
		return -EINVAL;
	}

	return iiospec->args[0];
}

static int __fwnode_iio_channel_get(struct iio_channel *channel,
				    struct fwnode_handle *fwnode, int index)
{
	struct fwnode_reference_args iiospec;
	struct device *idev;
	struct iio_dev *indio_dev;
	int err;

	err = fwnode_property_get_reference_args(fwnode, "io-channels",
						 "#io-channel-cells", 0,
						 index, &iiospec);
	if (err)
		return err;

	idev = bus_find_device_by_fwnode(&iio_bus_type, iiospec.fwnode);
	if (!idev) {
		fwnode_handle_put(iiospec.fwnode);
		return -EPROBE_DEFER;
	}

	indio_dev = dev_to_iio_dev(idev);
	channel->indio_dev = indio_dev;
	if (indio_dev->info->fwnode_xlate)
		index = indio_dev->info->fwnode_xlate(indio_dev, &iiospec);
	else
		index = __fwnode_iio_simple_xlate(indio_dev, &iiospec);
	fwnode_handle_put(iiospec.fwnode);
	if (index < 0)
		goto err_put;
	channel->channel = &indio_dev->channels[index];

	return 0;

err_put:
	iio_device_put(indio_dev);
	return index;
}

static struct iio_channel *fwnode_iio_channel_get(struct fwnode_handle *fwnode,
						  int index)
{
	int err;

	if (index < 0)
		return ERR_PTR(-EINVAL);

	struct iio_channel *channel __free(kfree) =
		kzalloc(sizeof(*channel), GFP_KERNEL);
	if (!channel)
		return ERR_PTR(-ENOMEM);

	err = __fwnode_iio_channel_get(channel, fwnode, index);
	if (err)
		return ERR_PTR(err);

	return_ptr(channel);
}

static struct iio_channel *
__fwnode_iio_channel_get_by_name(struct fwnode_handle *fwnode, const char *name)
{
	struct iio_channel *chan;
	int index = 0;

	/*
	 * For named iio channels, first look up the name in the
	 * "io-channel-names" property.  If it cannot be found, the
	 * index will be an error code, and fwnode_iio_channel_get()
	 * will fail.
	 */
	if (name)
		index = fwnode_property_match_string(fwnode, "io-channel-names",
						     name);

	chan = fwnode_iio_channel_get(fwnode, index);
	if (!IS_ERR(chan) || PTR_ERR(chan) == -EPROBE_DEFER)
		return chan;
	if (name) {
		if (index >= 0) {
			pr_err("ERROR: could not get IIO channel %pfw:%s(%i)\n",
			       fwnode, name, index);
			/*
			 * In this case, we found 'name' in 'io-channel-names'
			 * but somehow we still fail so that we should not proceed
			 * with any other lookup. Hence, explicitly return -EINVAL
			 * (maybe not the better error code) so that the caller
			 * won't do a system lookup.
			 */
			return ERR_PTR(-EINVAL);
		}
		/*
		 * If index < 0, then fwnode_property_get_reference_args() fails
		 * with -EINVAL or -ENOENT (ACPI case) which is expected. We
		 * should not proceed if we get any other error.
		 */
		if (PTR_ERR(chan) != -EINVAL && PTR_ERR(chan) != -ENOENT)
			return chan;
	} else if (PTR_ERR(chan) != -ENOENT) {
		/*
		 * if !name, then we should only proceed the lookup if
		 * fwnode_property_get_reference_args() returns -ENOENT.
		 */
		return chan;
	}

	/* so we continue the lookup */
	return ERR_PTR(-ENODEV);
}

struct iio_channel *fwnode_iio_channel_get_by_name(struct fwnode_handle *fwnode,
						   const char *name)
{
	struct fwnode_handle *parent;
	struct iio_channel *chan;

	/* Walk up the tree of devices looking for a matching iio channel */
	chan = __fwnode_iio_channel_get_by_name(fwnode, name);
	if (!IS_ERR(chan) || PTR_ERR(chan) != -ENODEV)
		return chan;

	/*
	 * No matching IIO channel found on this node.
	 * If the parent node has a "io-channel-ranges" property,
	 * then we can try one of its channels.
	 */
	fwnode_for_each_parent_node(fwnode, parent) {
		if (!fwnode_property_present(parent, "io-channel-ranges")) {
			fwnode_handle_put(parent);
			return ERR_PTR(-ENODEV);
		}

		chan = __fwnode_iio_channel_get_by_name(fwnode, name);
		if (!IS_ERR(chan) || PTR_ERR(chan) != -ENODEV) {
			fwnode_handle_put(parent);
 			return chan;
		}
	}

	return ERR_PTR(-ENODEV);
}
EXPORT_SYMBOL_GPL(fwnode_iio_channel_get_by_name);

static struct iio_channel *fwnode_iio_channel_get_all(struct device *dev)
{
	struct fwnode_handle *fwnode = dev_fwnode(dev);
	int i, mapind, nummaps = 0;
	int ret;

	do {
		ret = fwnode_property_get_reference_args(fwnode, "io-channels",
							 "#io-channel-cells", 0,
							 nummaps, NULL);
		if (ret < 0)
			break;
	} while (++nummaps);

	if (nummaps == 0)
		return ERR_PTR(-ENODEV);

	/* NULL terminated array to save passing size */
	struct iio_channel *chans __free(kfree) =
		kcalloc(nummaps + 1, sizeof(*chans), GFP_KERNEL);
	if (!chans)
		return ERR_PTR(-ENOMEM);

	/* Search for FW matches */
	for (mapind = 0; mapind < nummaps; mapind++) {
		ret = __fwnode_iio_channel_get(&chans[mapind], fwnode, mapind);
		if (ret)
			goto error_free_chans;
	}
	return_ptr(chans);

error_free_chans:
	for (i = 0; i < mapind; i++)
		iio_device_put(chans[i].indio_dev);
	return ERR_PTR(ret);
}

static struct iio_channel *iio_channel_get_sys(const char *name,
					       const char *channel_name)
{
	struct iio_map_internal *c_i = NULL, *c = NULL;
	int err;

	if (!(name || channel_name))
		return ERR_PTR(-ENODEV);

	/* first find matching entry the channel map */
	scoped_guard(mutex, &iio_map_list_lock) {
		list_for_each_entry(c_i, &iio_map_list, l) {
			if ((name && strcmp(name, c_i->map->consumer_dev_name) != 0) ||
			    (channel_name &&
			     strcmp(channel_name, c_i->map->consumer_channel) != 0))
				continue;
			c = c_i;
			iio_device_get(c->indio_dev);
			break;
		}
	}
	if (!c)
		return ERR_PTR(-ENODEV);

	struct iio_channel *channel __free(kfree) =
		kzalloc(sizeof(*channel), GFP_KERNEL);
	if (!channel) {
		err = -ENOMEM;
		goto error_no_mem;
	}

	channel->indio_dev = c->indio_dev;

	if (c->map->adc_channel_label) {
		channel->channel =
			iio_chan_spec_from_name(channel->indio_dev,
						c->map->adc_channel_label);

		if (!channel->channel) {
			err = -EINVAL;
			goto error_no_mem;
		}
	}

	return_ptr(channel);

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
		channel = fwnode_iio_channel_get_by_name(dev_fwnode(dev),
							 channel_name);
		if (!IS_ERR(channel) || PTR_ERR(channel) != -ENODEV)
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

static void devm_iio_channel_free(void *iio_channel)
{
	iio_channel_release(iio_channel);
}

struct iio_channel *devm_iio_channel_get(struct device *dev,
					 const char *channel_name)
{
	struct iio_channel *channel;
	int ret;

	channel = iio_channel_get(dev, channel_name);
	if (IS_ERR(channel))
		return channel;

	ret = devm_add_action_or_reset(dev, devm_iio_channel_free, channel);
	if (ret)
		return ERR_PTR(ret);

	return channel;
}
EXPORT_SYMBOL_GPL(devm_iio_channel_get);

struct iio_channel *devm_fwnode_iio_channel_get_by_name(struct device *dev,
							struct fwnode_handle *fwnode,
							const char *channel_name)
{
	struct iio_channel *channel;
	int ret;

	channel = fwnode_iio_channel_get_by_name(fwnode, channel_name);
	if (IS_ERR(channel))
		return channel;

	ret = devm_add_action_or_reset(dev, devm_iio_channel_free, channel);
	if (ret)
		return ERR_PTR(ret);

	return channel;
}
EXPORT_SYMBOL_GPL(devm_fwnode_iio_channel_get_by_name);

struct iio_channel *iio_channel_get_all(struct device *dev)
{
	const char *name;
	struct iio_map_internal *c = NULL;
	struct iio_channel *fw_chans;
	int nummaps = 0;
	int mapind = 0;
	int i, ret;

	if (!dev)
		return ERR_PTR(-EINVAL);

	fw_chans = fwnode_iio_channel_get_all(dev);
	/*
	 * We only want to carry on if the error is -ENODEV.  Anything else
	 * should be reported up the stack.
	 */
	if (!IS_ERR(fw_chans) || PTR_ERR(fw_chans) != -ENODEV)
		return fw_chans;

	name = dev_name(dev);

	guard(mutex)(&iio_map_list_lock);
	/* first count the matching maps */
	list_for_each_entry(c, &iio_map_list, l)
		if (name && strcmp(name, c->map->consumer_dev_name) != 0)
			continue;
		else
			nummaps++;

	if (nummaps == 0)
		return ERR_PTR(-ENODEV);

	/* NULL terminated array to save passing size */
	struct iio_channel *chans __free(kfree) =
		kcalloc(nummaps + 1, sizeof(*chans), GFP_KERNEL);
	if (!chans)
		return ERR_PTR(-ENOMEM);

	/* for each map fill in the chans element */
	list_for_each_entry(c, &iio_map_list, l) {
		if (name && strcmp(name, c->map->consumer_dev_name) != 0)
			continue;
		chans[mapind].indio_dev = c->indio_dev;
		chans[mapind].data = c->map->consumer_data;
		chans[mapind].channel =
			iio_chan_spec_from_name(chans[mapind].indio_dev,
						c->map->adc_channel_label);
		if (!chans[mapind].channel) {
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

	return_ptr(chans);

error_free_chans:
	for (i = 0; i < nummaps; i++)
		iio_device_put(chans[i].indio_dev);
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

static void devm_iio_channel_free_all(void *iio_channels)
{
	iio_channel_release_all(iio_channels);
}

struct iio_channel *devm_iio_channel_get_all(struct device *dev)
{
	struct iio_channel *channels;
	int ret;

	channels = iio_channel_get_all(dev);
	if (IS_ERR(channels))
		return channels;

	ret = devm_add_action_or_reset(dev, devm_iio_channel_free_all,
				       channels);
	if (ret)
		return ERR_PTR(ret);

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

	if (!val2)
		val2 = &unused;

	if (!iio_channel_has_info(chan->channel, info))
		return -EINVAL;

	if (chan->indio_dev->info->read_raw_multi) {
		ret = chan->indio_dev->info->read_raw_multi(chan->indio_dev,
					chan->channel, INDIO_MAX_RAW_ELEMENTS,
					vals, &val_len, info);
		*val = vals[0];
		*val2 = vals[1];
	} else {
		ret = chan->indio_dev->info->read_raw(chan->indio_dev,
					chan->channel, val, val2, info);
	}

	return ret;
}

int iio_read_channel_raw(struct iio_channel *chan, int *val)
{
	struct iio_dev_opaque *iio_dev_opaque = to_iio_dev_opaque(chan->indio_dev);

	guard(mutex)(&iio_dev_opaque->info_exist_lock);
	if (!chan->indio_dev->info)
		return -ENODEV;

	return iio_channel_read(chan, val, NULL, IIO_CHAN_INFO_RAW);
}
EXPORT_SYMBOL_GPL(iio_read_channel_raw);

int iio_read_channel_average_raw(struct iio_channel *chan, int *val)
{
	struct iio_dev_opaque *iio_dev_opaque = to_iio_dev_opaque(chan->indio_dev);

	guard(mutex)(&iio_dev_opaque->info_exist_lock);
	if (!chan->indio_dev->info)
		return -ENODEV;

	return iio_channel_read(chan, val, NULL, IIO_CHAN_INFO_AVERAGE_RAW);
}
EXPORT_SYMBOL_GPL(iio_read_channel_average_raw);

static int iio_convert_raw_to_processed_unlocked(struct iio_channel *chan,
						 int raw, int *processed,
						 unsigned int scale)
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
	struct iio_dev_opaque *iio_dev_opaque = to_iio_dev_opaque(chan->indio_dev);

	guard(mutex)(&iio_dev_opaque->info_exist_lock);
	if (!chan->indio_dev->info)
		return -ENODEV;

	return iio_convert_raw_to_processed_unlocked(chan, raw, processed,
						     scale);
}
EXPORT_SYMBOL_GPL(iio_convert_raw_to_processed);

int iio_read_channel_attribute(struct iio_channel *chan, int *val, int *val2,
			       enum iio_chan_info_enum attribute)
{
	struct iio_dev_opaque *iio_dev_opaque = to_iio_dev_opaque(chan->indio_dev);

	guard(mutex)(&iio_dev_opaque->info_exist_lock);
	if (!chan->indio_dev->info)
		return -ENODEV;

	return iio_channel_read(chan, val, val2, attribute);
}
EXPORT_SYMBOL_GPL(iio_read_channel_attribute);

int iio_read_channel_offset(struct iio_channel *chan, int *val, int *val2)
{
	return iio_read_channel_attribute(chan, val, val2, IIO_CHAN_INFO_OFFSET);
}
EXPORT_SYMBOL_GPL(iio_read_channel_offset);

int iio_read_channel_processed_scale(struct iio_channel *chan, int *val,
				     unsigned int scale)
{
	struct iio_dev_opaque *iio_dev_opaque = to_iio_dev_opaque(chan->indio_dev);
	int ret;

	guard(mutex)(&iio_dev_opaque->info_exist_lock);
	if (!chan->indio_dev->info)
		return -ENODEV;

	if (iio_channel_has_info(chan->channel, IIO_CHAN_INFO_PROCESSED)) {
		ret = iio_channel_read(chan, val, NULL,
				       IIO_CHAN_INFO_PROCESSED);
		if (ret < 0)
			return ret;
		*val *= scale;

		return 0;
	} else {
		ret = iio_channel_read(chan, val, NULL, IIO_CHAN_INFO_RAW);
		if (ret < 0)
			return ret;

		return iio_convert_raw_to_processed_unlocked(chan, *val, val,
							     scale);
	}
}
EXPORT_SYMBOL_GPL(iio_read_channel_processed_scale);

int iio_read_channel_processed(struct iio_channel *chan, int *val)
{
	/* This is just a special case with scale factor 1 */
	return iio_read_channel_processed_scale(chan, val, 1);
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
	struct iio_dev_opaque *iio_dev_opaque = to_iio_dev_opaque(chan->indio_dev);

	guard(mutex)(&iio_dev_opaque->info_exist_lock);
	if (!chan->indio_dev->info)
		return -ENODEV;

	return iio_channel_read_avail(chan, vals, type, length, attribute);
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
	const int *vals;
	int length;
	int ret;

	ret = iio_channel_read_avail(chan, &vals, type, &length, info);
	if (ret < 0)
		return ret;

	switch (ret) {
	case IIO_AVAIL_RANGE:
		switch (*type) {
		case IIO_VAL_INT:
			*val = vals[2];
			break;
		default:
			*val = vals[4];
			if (val2)
				*val2 = vals[5];
		}
		return 0;

	case IIO_AVAIL_LIST:
		if (length <= 0)
			return -EINVAL;
		switch (*type) {
		case IIO_VAL_INT:
			*val = max_array(vals, length);
			break;
		default:
			/* TODO: learn about max for other iio values */
			return -EINVAL;
		}
		return 0;

	default:
		return -EINVAL;
	}
}

int iio_read_max_channel_raw(struct iio_channel *chan, int *val)
{
	struct iio_dev_opaque *iio_dev_opaque = to_iio_dev_opaque(chan->indio_dev);
	int type;

	guard(mutex)(&iio_dev_opaque->info_exist_lock);
	if (!chan->indio_dev->info)
		return -ENODEV;

	return iio_channel_read_max(chan, val, NULL, &type, IIO_CHAN_INFO_RAW);
}
EXPORT_SYMBOL_GPL(iio_read_max_channel_raw);

static int iio_channel_read_min(struct iio_channel *chan,
				int *val, int *val2, int *type,
				enum iio_chan_info_enum info)
{
	const int *vals;
	int length;
	int ret;

	ret = iio_channel_read_avail(chan, &vals, type, &length, info);
	if (ret < 0)
		return ret;

	switch (ret) {
	case IIO_AVAIL_RANGE:
		switch (*type) {
		case IIO_VAL_INT:
			*val = vals[0];
			break;
		default:
			*val = vals[0];
			if (val2)
				*val2 = vals[1];
		}
		return 0;

	case IIO_AVAIL_LIST:
		if (length <= 0)
			return -EINVAL;
		switch (*type) {
		case IIO_VAL_INT:
			*val = min_array(vals, length);
			break;
		default:
			/* TODO: learn about min for other iio values */
			return -EINVAL;
		}
		return 0;

	default:
		return -EINVAL;
	}
}

int iio_read_min_channel_raw(struct iio_channel *chan, int *val)
{
	struct iio_dev_opaque *iio_dev_opaque = to_iio_dev_opaque(chan->indio_dev);
	int type;

	guard(mutex)(&iio_dev_opaque->info_exist_lock);
	if (!chan->indio_dev->info)
		return -ENODEV;

	return iio_channel_read_min(chan, val, NULL, &type, IIO_CHAN_INFO_RAW);
}
EXPORT_SYMBOL_GPL(iio_read_min_channel_raw);

int iio_get_channel_type(struct iio_channel *chan, enum iio_chan_type *type)
{
	struct iio_dev_opaque *iio_dev_opaque = to_iio_dev_opaque(chan->indio_dev);

	guard(mutex)(&iio_dev_opaque->info_exist_lock);
	if (!chan->indio_dev->info)
		return -ENODEV;

	*type = chan->channel->type;

	return 0;
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
	struct iio_dev_opaque *iio_dev_opaque = to_iio_dev_opaque(chan->indio_dev);

	guard(mutex)(&iio_dev_opaque->info_exist_lock);
	if (!chan->indio_dev->info)
		return -ENODEV;

	return iio_channel_write(chan, val, val2, attribute);
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

static const struct iio_chan_spec_ext_info *
iio_lookup_ext_info(const struct iio_channel *chan, const char *attr)
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
