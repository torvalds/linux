// SPDX-License-Identifier: GPL-2.0-only
/*
 * Helpers for parsing common ADC information from a firmware node.
 *
 * Copyright (c) 2025 Matti Vaittinen <mazziesaccount@gmail.com>
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/types.h>

#include <linux/iio/adc-helpers.h>
#include <linux/iio/iio.h>

/**
 * devm_iio_adc_device_alloc_chaninfo_se - allocate and fill iio_chan_spec for ADC
 *
 * Scan the device node for single-ended ADC channel information. Channel ID is
 * expected to be found from the "reg" property. Allocate and populate the
 * iio_chan_spec structure corresponding to channels that are found. The memory
 * for iio_chan_spec structure will be freed upon device detach.
 *
 * @dev:		Pointer to the ADC device.
 * @template:		Template iio_chan_spec from which the fields of all
 *			found and allocated channels are initialized.
 * @max_chan_id:	Maximum value of a channel ID. Use negative value if no
 *			checking is required.
 * @cs:			Location where pointer to allocated iio_chan_spec
 *			should be stored.
 *
 * Return:	Number of found channels on success. Negative value to indicate
 *		failure. Specifically, -ENOENT if no channel nodes were found.
 */
int devm_iio_adc_device_alloc_chaninfo_se(struct device *dev,
					  const struct iio_chan_spec *template,
					  int max_chan_id,
					  struct iio_chan_spec **cs)
{
	struct iio_chan_spec *chan_array, *chan;
	int num_chan, ret;

	num_chan = iio_adc_device_num_channels(dev);
	if (num_chan < 0)
		return num_chan;

	if (!num_chan)
		return -ENOENT;

	chan_array = devm_kcalloc(dev, num_chan, sizeof(*chan_array),
				  GFP_KERNEL);
	if (!chan_array)
		return -ENOMEM;

	chan = &chan_array[0];

	device_for_each_named_child_node_scoped(dev, child, "channel") {
		u32 ch;

		ret = fwnode_property_read_u32(child, "reg", &ch);
		if (ret)
			return ret;

		if (max_chan_id >= 0 && ch > max_chan_id)
			return -ERANGE;

		*chan = *template;
		chan->channel = ch;
		chan++;
	}

	*cs = chan_array;

	return num_chan;
}
EXPORT_SYMBOL_NS_GPL(devm_iio_adc_device_alloc_chaninfo_se, "IIO_DRIVER");

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Matti Vaittinen <mazziesaccount@gmail.com>");
MODULE_DESCRIPTION("IIO ADC fwnode parsing helpers");
