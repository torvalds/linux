// SPDX-License-Identifier: GPL-2.0
/*
 * IIO rescale driver
 *
 * Copyright (C) 2018 Axentia Technologies AB
 * Copyright (C) 2022 Liam Beguin <liambeguin@gmail.com>
 *
 * Author: Peter Rosin <peda@axentia.se>
 */

#include <linux/err.h>
#include <linux/gcd.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>

#include <linux/iio/afe/rescale.h>
#include <linux/iio/consumer.h>
#include <linux/iio/iio.h>

int rescale_process_scale(struct rescale *rescale, int scale_type,
			  int *val, int *val2)
{
	s64 tmp;
	int _val, _val2;
	s32 rem, rem2;
	u32 mult;
	u32 neg;

	switch (scale_type) {
	case IIO_VAL_INT:
		*val *= rescale->numerator;
		if (rescale->denominator == 1)
			return scale_type;
		*val2 = rescale->denominator;
		return IIO_VAL_FRACTIONAL;
	case IIO_VAL_FRACTIONAL:
		/*
		 * When the product of both scales doesn't overflow, avoid
		 * potential accuracy loss (for in kernel consumers) by
		 * keeping a fractional representation.
		 */
		if (!check_mul_overflow(*val, rescale->numerator, &_val) &&
		    !check_mul_overflow(*val2, rescale->denominator, &_val2)) {
			*val = _val;
			*val2 = _val2;
			return IIO_VAL_FRACTIONAL;
		}
		fallthrough;
	case IIO_VAL_FRACTIONAL_LOG2:
		tmp = (s64)*val * 1000000000LL;
		tmp = div_s64(tmp, rescale->denominator);
		tmp *= rescale->numerator;

		tmp = div_s64_rem(tmp, 1000000000LL, &rem);
		*val = tmp;

		if (!rem)
			return scale_type;

		if (scale_type == IIO_VAL_FRACTIONAL)
			tmp = *val2;
		else
			tmp = ULL(1) << *val2;

		rem2 = *val % (int)tmp;
		*val = *val / (int)tmp;

		*val2 = rem / (int)tmp;
		if (rem2)
			*val2 += div_s64((s64)rem2 * 1000000000LL, tmp);

		return IIO_VAL_INT_PLUS_NANO;
	case IIO_VAL_INT_PLUS_NANO:
	case IIO_VAL_INT_PLUS_MICRO:
		mult = scale_type == IIO_VAL_INT_PLUS_NANO ? 1000000000L : 1000000L;

		/*
		 * For IIO_VAL_INT_PLUS_{MICRO,NANO} scale types if either *val
		 * OR *val2 is negative the schan scale is negative, i.e.
		 * *val = 1 and *val2 = -0.5 yields -1.5 not -0.5.
		 */
		neg = *val < 0 || *val2 < 0;

		tmp = (s64)abs(*val) * abs(rescale->numerator);
		*val = div_s64_rem(tmp, abs(rescale->denominator), &rem);

		tmp = (s64)rem * mult + (s64)abs(*val2) * abs(rescale->numerator);
		tmp = div_s64(tmp, abs(rescale->denominator));

		*val += div_s64_rem(tmp, mult, val2);

		/*
		 * If only one of the rescaler elements or the schan scale is
		 * negative, the combined scale is negative.
		 */
		if (neg ^ ((rescale->numerator < 0) ^ (rescale->denominator < 0))) {
			if (*val)
				*val = -*val;
			else
				*val2 = -*val2;
		}

		return scale_type;
	default:
		return -EOPNOTSUPP;
	}
}
EXPORT_SYMBOL_NS_GPL(rescale_process_scale, "IIO_RESCALE");

int rescale_process_offset(struct rescale *rescale, int scale_type,
			   int scale, int scale2, int schan_off,
			   int *val, int *val2)
{
	s64 tmp, tmp2;

	switch (scale_type) {
	case IIO_VAL_FRACTIONAL:
		tmp = (s64)rescale->offset * scale2;
		*val = div_s64(tmp, scale) + schan_off;
		return IIO_VAL_INT;
	case IIO_VAL_INT:
		*val = div_s64(rescale->offset, scale) + schan_off;
		return IIO_VAL_INT;
	case IIO_VAL_FRACTIONAL_LOG2:
		tmp = (s64)rescale->offset * (1 << scale2);
		*val = div_s64(tmp, scale) + schan_off;
		return IIO_VAL_INT;
	case IIO_VAL_INT_PLUS_NANO:
		tmp = (s64)rescale->offset * 1000000000LL;
		tmp2 = ((s64)scale * 1000000000LL) + scale2;
		*val = div64_s64(tmp, tmp2) + schan_off;
		return IIO_VAL_INT;
	case IIO_VAL_INT_PLUS_MICRO:
		tmp = (s64)rescale->offset * 1000000LL;
		tmp2 = ((s64)scale * 1000000LL) + scale2;
		*val = div64_s64(tmp, tmp2) + schan_off;
		return IIO_VAL_INT;
	default:
		return -EOPNOTSUPP;
	}
}
EXPORT_SYMBOL_NS_GPL(rescale_process_offset, "IIO_RESCALE");

static int rescale_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	struct rescale *rescale = iio_priv(indio_dev);
	int scale, scale2;
	int schan_off = 0;
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (rescale->chan_processed)
			/*
			 * When only processed channels are supported, we
			 * read the processed data and scale it by 1/1
			 * augmented with whatever the rescaler has calculated.
			 */
			return iio_read_channel_processed(rescale->source, val);
		else
			return iio_read_channel_raw(rescale->source, val);

	case IIO_CHAN_INFO_SCALE:
		if (rescale->chan_processed) {
			/*
			 * Processed channels are scaled 1-to-1
			 */
			*val = 1;
			*val2 = 1;
			ret = IIO_VAL_FRACTIONAL;
		} else {
			ret = iio_read_channel_scale(rescale->source, val, val2);
		}
		return rescale_process_scale(rescale, ret, val, val2);
	case IIO_CHAN_INFO_OFFSET:
		/*
		 * Processed channels are scaled 1-to-1 and source offset is
		 * already taken into account.
		 *
		 * In other cases, real world measurement are expressed as:
		 *
		 *	schan_scale * (raw + schan_offset)
		 *
		 * Given that the rescaler parameters are applied recursively:
		 *
		 *	rescaler_scale * (schan_scale * (raw + schan_offset) +
		 *		rescaler_offset)
		 *
		 * Or,
		 *
		 *	(rescaler_scale * schan_scale) * (raw +
		 *		(schan_offset +	rescaler_offset / schan_scale)
		 *
		 * Thus, reusing the original expression the parameters exposed
		 * to userspace are:
		 *
		 *	scale = schan_scale * rescaler_scale
		 *	offset = schan_offset + rescaler_offset / schan_scale
		 */
		if (rescale->chan_processed) {
			*val = rescale->offset;
			return IIO_VAL_INT;
		}

		if (iio_channel_has_info(rescale->source->channel,
					 IIO_CHAN_INFO_OFFSET)) {
			ret = iio_read_channel_offset(rescale->source,
						      &schan_off, NULL);
			if (ret != IIO_VAL_INT)
				return ret < 0 ? ret : -EOPNOTSUPP;
		}

		if (iio_channel_has_info(rescale->source->channel,
					 IIO_CHAN_INFO_SCALE)) {
			ret = iio_read_channel_scale(rescale->source, &scale, &scale2);
			return rescale_process_offset(rescale, ret, scale, scale2,
						      schan_off, val, val2);
		}

		/*
		 * If we get here we have no scale so scale 1:1 but apply
		 * rescaler and offset, if any.
		 */
		return rescale_process_offset(rescale, IIO_VAL_FRACTIONAL, 1, 1,
					      schan_off, val, val2);
	default:
		return -EINVAL;
	}
}

static int rescale_read_avail(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      const int **vals, int *type, int *length,
			      long mask)
{
	struct rescale *rescale = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		*type = IIO_VAL_INT;
		return iio_read_avail_channel_raw(rescale->source,
						  vals, length);
	default:
		return -EINVAL;
	}
}

static const struct iio_info rescale_info = {
	.read_raw = rescale_read_raw,
	.read_avail = rescale_read_avail,
};

static ssize_t rescale_read_ext_info(struct iio_dev *indio_dev,
				     uintptr_t private,
				     struct iio_chan_spec const *chan,
				     char *buf)
{
	struct rescale *rescale = iio_priv(indio_dev);

	return iio_read_channel_ext_info(rescale->source,
					 rescale->ext_info[private].name,
					 buf);
}

static ssize_t rescale_write_ext_info(struct iio_dev *indio_dev,
				      uintptr_t private,
				      struct iio_chan_spec const *chan,
				      const char *buf, size_t len)
{
	struct rescale *rescale = iio_priv(indio_dev);

	return iio_write_channel_ext_info(rescale->source,
					  rescale->ext_info[private].name,
					  buf, len);
}

static int rescale_configure_channel(struct device *dev,
				     struct rescale *rescale)
{
	struct iio_chan_spec *chan = &rescale->chan;
	struct iio_chan_spec const *schan = rescale->source->channel;

	chan->indexed = 1;
	chan->output = schan->output;
	chan->ext_info = rescale->ext_info;
	chan->type = rescale->cfg->type;

	if (iio_channel_has_info(schan, IIO_CHAN_INFO_RAW) &&
	    (iio_channel_has_info(schan, IIO_CHAN_INFO_SCALE) ||
	     iio_channel_has_info(schan, IIO_CHAN_INFO_OFFSET))) {
		dev_info(dev, "using raw+scale/offset source channel\n");
	} else if (iio_channel_has_info(schan, IIO_CHAN_INFO_PROCESSED)) {
		dev_info(dev, "using processed channel\n");
		rescale->chan_processed = true;
	} else {
		dev_err(dev, "source channel is not supported\n");
		return -EINVAL;
	}

	chan->info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
		BIT(IIO_CHAN_INFO_SCALE);

	if (rescale->offset)
		chan->info_mask_separate |= BIT(IIO_CHAN_INFO_OFFSET);

	/*
	 * Using .read_avail() is fringe to begin with and makes no sense
	 * whatsoever for processed channels, so we make sure that this cannot
	 * be called on a processed channel.
	 */
	if (iio_channel_has_available(schan, IIO_CHAN_INFO_RAW) &&
	    !rescale->chan_processed)
		chan->info_mask_separate_available |= BIT(IIO_CHAN_INFO_RAW);

	return 0;
}

static int rescale_current_sense_amplifier_props(struct device *dev,
						 struct rescale *rescale)
{
	u32 sense;
	u32 gain_mult = 1;
	u32 gain_div = 1;
	u32 factor;
	int ret;

	ret = device_property_read_u32(dev, "sense-resistor-micro-ohms",
				       &sense);
	if (ret) {
		dev_err(dev, "failed to read the sense resistance: %d\n", ret);
		return ret;
	}

	device_property_read_u32(dev, "sense-gain-mult", &gain_mult);
	device_property_read_u32(dev, "sense-gain-div", &gain_div);

	/*
	 * Calculate the scaling factor, 1 / (gain * sense), or
	 * gain_div / (gain_mult * sense), while trying to keep the
	 * numerator/denominator from overflowing.
	 */
	factor = gcd(sense, 1000000);
	rescale->numerator = 1000000 / factor;
	rescale->denominator = sense / factor;

	factor = gcd(rescale->numerator, gain_mult);
	rescale->numerator /= factor;
	rescale->denominator *= gain_mult / factor;

	factor = gcd(rescale->denominator, gain_div);
	rescale->numerator *= gain_div / factor;
	rescale->denominator /= factor;

	return 0;
}

static int rescale_current_sense_shunt_props(struct device *dev,
					     struct rescale *rescale)
{
	u32 shunt;
	u32 factor;
	int ret;

	ret = device_property_read_u32(dev, "shunt-resistor-micro-ohms",
				       &shunt);
	if (ret) {
		dev_err(dev, "failed to read the shunt resistance: %d\n", ret);
		return ret;
	}

	factor = gcd(shunt, 1000000);
	rescale->numerator = 1000000 / factor;
	rescale->denominator = shunt / factor;

	return 0;
}

static int rescale_voltage_divider_props(struct device *dev,
					 struct rescale *rescale)
{
	int ret;
	u32 factor;

	ret = device_property_read_u32(dev, "output-ohms",
				       &rescale->denominator);
	if (ret) {
		dev_err(dev, "failed to read output-ohms: %d\n", ret);
		return ret;
	}

	ret = device_property_read_u32(dev, "full-ohms",
				       &rescale->numerator);
	if (ret) {
		dev_err(dev, "failed to read full-ohms: %d\n", ret);
		return ret;
	}

	factor = gcd(rescale->numerator, rescale->denominator);
	rescale->numerator /= factor;
	rescale->denominator /= factor;

	return 0;
}

static int rescale_temp_sense_rtd_props(struct device *dev,
					struct rescale *rescale)
{
	u32 factor;
	u32 alpha;
	u32 iexc;
	u32 tmp;
	int ret;
	u32 r0;

	ret = device_property_read_u32(dev, "excitation-current-microamp",
				       &iexc);
	if (ret) {
		dev_err(dev, "failed to read excitation-current-microamp: %d\n",
			ret);
		return ret;
	}

	ret = device_property_read_u32(dev, "alpha-ppm-per-celsius", &alpha);
	if (ret) {
		dev_err(dev, "failed to read alpha-ppm-per-celsius: %d\n",
			ret);
		return ret;
	}

	ret = device_property_read_u32(dev, "r-naught-ohms", &r0);
	if (ret) {
		dev_err(dev, "failed to read r-naught-ohms: %d\n", ret);
		return ret;
	}

	tmp = r0 * iexc * alpha / 1000000;
	factor = gcd(tmp, 1000000);
	rescale->numerator = 1000000 / factor;
	rescale->denominator = tmp / factor;

	rescale->offset = -1 * ((r0 * iexc) / 1000);

	return 0;
}

static int rescale_temp_transducer_props(struct device *dev,
					 struct rescale *rescale)
{
	s32 offset = 0;
	s32 sense = 1;
	s32 alpha;
	int ret;

	device_property_read_u32(dev, "sense-offset-millicelsius", &offset);
	device_property_read_u32(dev, "sense-resistor-ohms", &sense);
	ret = device_property_read_u32(dev, "alpha-ppm-per-celsius", &alpha);
	if (ret) {
		dev_err(dev, "failed to read alpha-ppm-per-celsius: %d\n", ret);
		return ret;
	}

	rescale->numerator = 1000000;
	rescale->denominator = alpha * sense;

	rescale->offset = div_s64((s64)offset * rescale->denominator,
				  rescale->numerator);

	return 0;
}

enum rescale_variant {
	CURRENT_SENSE_AMPLIFIER,
	CURRENT_SENSE_SHUNT,
	VOLTAGE_DIVIDER,
	TEMP_SENSE_RTD,
	TEMP_TRANSDUCER,
};

static const struct rescale_cfg rescale_cfg[] = {
	[CURRENT_SENSE_AMPLIFIER] = {
		.type = IIO_CURRENT,
		.props = rescale_current_sense_amplifier_props,
	},
	[CURRENT_SENSE_SHUNT] = {
		.type = IIO_CURRENT,
		.props = rescale_current_sense_shunt_props,
	},
	[VOLTAGE_DIVIDER] = {
		.type = IIO_VOLTAGE,
		.props = rescale_voltage_divider_props,
	},
	[TEMP_SENSE_RTD] = {
		.type = IIO_TEMP,
		.props = rescale_temp_sense_rtd_props,
	},
	[TEMP_TRANSDUCER] = {
		.type = IIO_TEMP,
		.props = rescale_temp_transducer_props,
	},
};

static const struct of_device_id rescale_match[] = {
	{ .compatible = "current-sense-amplifier",
	  .data = &rescale_cfg[CURRENT_SENSE_AMPLIFIER], },
	{ .compatible = "current-sense-shunt",
	  .data = &rescale_cfg[CURRENT_SENSE_SHUNT], },
	{ .compatible = "voltage-divider",
	  .data = &rescale_cfg[VOLTAGE_DIVIDER], },
	{ .compatible = "temperature-sense-rtd",
	  .data = &rescale_cfg[TEMP_SENSE_RTD], },
	{ .compatible = "temperature-transducer",
	  .data = &rescale_cfg[TEMP_TRANSDUCER], },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rescale_match);

static int rescale_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct iio_dev *indio_dev;
	struct iio_channel *source;
	struct rescale *rescale;
	int sizeof_ext_info;
	int sizeof_priv;
	int i;
	int ret;

	source = devm_iio_channel_get(dev, NULL);
	if (IS_ERR(source))
		return dev_err_probe(dev, PTR_ERR(source),
				     "failed to get source channel\n");

	sizeof_ext_info = iio_get_channel_ext_info_count(source);
	if (sizeof_ext_info) {
		sizeof_ext_info += 1; /* one extra entry for the sentinel */
		sizeof_ext_info *= sizeof(*rescale->ext_info);
	}

	sizeof_priv = sizeof(*rescale) + sizeof_ext_info;

	indio_dev = devm_iio_device_alloc(dev, sizeof_priv);
	if (!indio_dev)
		return -ENOMEM;

	rescale = iio_priv(indio_dev);

	rescale->cfg = device_get_match_data(dev);
	rescale->numerator = 1;
	rescale->denominator = 1;
	rescale->offset = 0;

	ret = rescale->cfg->props(dev, rescale);
	if (ret)
		return ret;

	if (!rescale->numerator || !rescale->denominator) {
		dev_err(dev, "invalid scaling factor.\n");
		return -EINVAL;
	}

	platform_set_drvdata(pdev, indio_dev);

	rescale->source = source;

	indio_dev->name = dev_name(dev);
	indio_dev->info = &rescale_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = &rescale->chan;
	indio_dev->num_channels = 1;
	if (sizeof_ext_info) {
		rescale->ext_info = devm_kmemdup(dev,
						 source->channel->ext_info,
						 sizeof_ext_info, GFP_KERNEL);
		if (!rescale->ext_info)
			return -ENOMEM;

		for (i = 0; rescale->ext_info[i].name; ++i) {
			struct iio_chan_spec_ext_info *ext_info =
				&rescale->ext_info[i];

			if (source->channel->ext_info[i].read)
				ext_info->read = rescale_read_ext_info;
			if (source->channel->ext_info[i].write)
				ext_info->write = rescale_write_ext_info;
			ext_info->private = i;
		}
	}

	ret = rescale_configure_channel(dev, rescale);
	if (ret)
		return ret;

	return devm_iio_device_register(dev, indio_dev);
}

static struct platform_driver rescale_driver = {
	.probe = rescale_probe,
	.driver = {
		.name = "iio-rescale",
		.of_match_table = rescale_match,
	},
};
module_platform_driver(rescale_driver);

MODULE_DESCRIPTION("IIO rescale driver");
MODULE_AUTHOR("Peter Rosin <peda@axentia.se>");
MODULE_LICENSE("GPL v2");
