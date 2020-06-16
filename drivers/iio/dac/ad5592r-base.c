// SPDX-License-Identifier: GPL-2.0-only
/*
 * AD5592R Digital <-> Analog converters driver
 *
 * Copyright 2014-2016 Analog Devices Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/iio/iio.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/property.h>

#include <dt-bindings/iio/adi,ad5592r.h>

#include "ad5592r-base.h"

static int ad5592r_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct ad5592r_state *st = gpiochip_get_data(chip);
	int ret = 0;
	u8 val;

	mutex_lock(&st->gpio_lock);

	if (st->gpio_out & BIT(offset))
		val = st->gpio_val;
	else
		ret = st->ops->gpio_read(st, &val);

	mutex_unlock(&st->gpio_lock);

	if (ret < 0)
		return ret;

	return !!(val & BIT(offset));
}

static void ad5592r_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct ad5592r_state *st = gpiochip_get_data(chip);

	mutex_lock(&st->gpio_lock);

	if (value)
		st->gpio_val |= BIT(offset);
	else
		st->gpio_val &= ~BIT(offset);

	st->ops->reg_write(st, AD5592R_REG_GPIO_SET, st->gpio_val);

	mutex_unlock(&st->gpio_lock);
}

static int ad5592r_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct ad5592r_state *st = gpiochip_get_data(chip);
	int ret;

	mutex_lock(&st->gpio_lock);

	st->gpio_out &= ~BIT(offset);
	st->gpio_in |= BIT(offset);

	ret = st->ops->reg_write(st, AD5592R_REG_GPIO_OUT_EN, st->gpio_out);
	if (ret < 0)
		goto err_unlock;

	ret = st->ops->reg_write(st, AD5592R_REG_GPIO_IN_EN, st->gpio_in);

err_unlock:
	mutex_unlock(&st->gpio_lock);

	return ret;
}

static int ad5592r_gpio_direction_output(struct gpio_chip *chip,
					 unsigned offset, int value)
{
	struct ad5592r_state *st = gpiochip_get_data(chip);
	int ret;

	mutex_lock(&st->gpio_lock);

	if (value)
		st->gpio_val |= BIT(offset);
	else
		st->gpio_val &= ~BIT(offset);

	st->gpio_in &= ~BIT(offset);
	st->gpio_out |= BIT(offset);

	ret = st->ops->reg_write(st, AD5592R_REG_GPIO_SET, st->gpio_val);
	if (ret < 0)
		goto err_unlock;

	ret = st->ops->reg_write(st, AD5592R_REG_GPIO_OUT_EN, st->gpio_out);
	if (ret < 0)
		goto err_unlock;

	ret = st->ops->reg_write(st, AD5592R_REG_GPIO_IN_EN, st->gpio_in);

err_unlock:
	mutex_unlock(&st->gpio_lock);

	return ret;
}

static int ad5592r_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	struct ad5592r_state *st = gpiochip_get_data(chip);

	if (!(st->gpio_map & BIT(offset))) {
		dev_err(st->dev, "GPIO %d is reserved by alternate function\n",
			offset);
		return -ENODEV;
	}

	return 0;
}

static int ad5592r_gpio_init(struct ad5592r_state *st)
{
	if (!st->gpio_map)
		return 0;

	st->gpiochip.label = dev_name(st->dev);
	st->gpiochip.base = -1;
	st->gpiochip.ngpio = 8;
	st->gpiochip.parent = st->dev;
	st->gpiochip.can_sleep = true;
	st->gpiochip.direction_input = ad5592r_gpio_direction_input;
	st->gpiochip.direction_output = ad5592r_gpio_direction_output;
	st->gpiochip.get = ad5592r_gpio_get;
	st->gpiochip.set = ad5592r_gpio_set;
	st->gpiochip.request = ad5592r_gpio_request;
	st->gpiochip.owner = THIS_MODULE;

	mutex_init(&st->gpio_lock);

	return gpiochip_add_data(&st->gpiochip, st);
}

static void ad5592r_gpio_cleanup(struct ad5592r_state *st)
{
	if (st->gpio_map)
		gpiochip_remove(&st->gpiochip);
}

static int ad5592r_reset(struct ad5592r_state *st)
{
	struct gpio_desc *gpio;

	gpio = devm_gpiod_get_optional(st->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(gpio))
		return PTR_ERR(gpio);

	if (gpio) {
		udelay(1);
		gpiod_set_value(gpio, 1);
	} else {
		mutex_lock(&st->lock);
		/* Writing this magic value resets the device */
		st->ops->reg_write(st, AD5592R_REG_RESET, 0xdac);
		mutex_unlock(&st->lock);
	}

	udelay(250);

	return 0;
}

static int ad5592r_get_vref(struct ad5592r_state *st)
{
	int ret;

	if (st->reg) {
		ret = regulator_get_voltage(st->reg);
		if (ret < 0)
			return ret;

		return ret / 1000;
	} else {
		return 2500;
	}
}

static int ad5592r_set_channel_modes(struct ad5592r_state *st)
{
	const struct ad5592r_rw_ops *ops = st->ops;
	int ret;
	unsigned i;
	u8 pulldown = 0, tristate = 0, dac = 0, adc = 0;
	u16 read_back;

	for (i = 0; i < st->num_channels; i++) {
		switch (st->channel_modes[i]) {
		case CH_MODE_DAC:
			dac |= BIT(i);
			break;

		case CH_MODE_ADC:
			adc |= BIT(i);
			break;

		case CH_MODE_DAC_AND_ADC:
			dac |= BIT(i);
			adc |= BIT(i);
			break;

		case CH_MODE_GPIO:
			st->gpio_map |= BIT(i);
			st->gpio_in |= BIT(i); /* Default to input */
			break;

		case CH_MODE_UNUSED:
			/* fall-through */
		default:
			switch (st->channel_offstate[i]) {
			case CH_OFFSTATE_OUT_TRISTATE:
				tristate |= BIT(i);
				break;

			case CH_OFFSTATE_OUT_LOW:
				st->gpio_out |= BIT(i);
				break;

			case CH_OFFSTATE_OUT_HIGH:
				st->gpio_out |= BIT(i);
				st->gpio_val |= BIT(i);
				break;

			case CH_OFFSTATE_PULLDOWN:
				/* fall-through */
			default:
				pulldown |= BIT(i);
				break;
			}
		}
	}

	mutex_lock(&st->lock);

	/* Pull down unused pins to GND */
	ret = ops->reg_write(st, AD5592R_REG_PULLDOWN, pulldown);
	if (ret)
		goto err_unlock;

	ret = ops->reg_write(st, AD5592R_REG_TRISTATE, tristate);
	if (ret)
		goto err_unlock;

	/* Configure pins that we use */
	ret = ops->reg_write(st, AD5592R_REG_DAC_EN, dac);
	if (ret)
		goto err_unlock;

	ret = ops->reg_write(st, AD5592R_REG_ADC_EN, adc);
	if (ret)
		goto err_unlock;

	ret = ops->reg_write(st, AD5592R_REG_GPIO_SET, st->gpio_val);
	if (ret)
		goto err_unlock;

	ret = ops->reg_write(st, AD5592R_REG_GPIO_OUT_EN, st->gpio_out);
	if (ret)
		goto err_unlock;

	ret = ops->reg_write(st, AD5592R_REG_GPIO_IN_EN, st->gpio_in);
	if (ret)
		goto err_unlock;

	/* Verify that we can read back at least one register */
	ret = ops->reg_read(st, AD5592R_REG_ADC_EN, &read_back);
	if (!ret && (read_back & 0xff) != adc)
		ret = -EIO;

err_unlock:
	mutex_unlock(&st->lock);
	return ret;
}

static int ad5592r_reset_channel_modes(struct ad5592r_state *st)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(st->channel_modes); i++)
		st->channel_modes[i] = CH_MODE_UNUSED;

	return ad5592r_set_channel_modes(st);
}

static int ad5592r_write_raw(struct iio_dev *iio_dev,
	struct iio_chan_spec const *chan, int val, int val2, long mask)
{
	struct ad5592r_state *st = iio_priv(iio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:

		if (val >= (1 << chan->scan_type.realbits) || val < 0)
			return -EINVAL;

		if (!chan->output)
			return -EINVAL;

		mutex_lock(&st->lock);
		ret = st->ops->write_dac(st, chan->channel, val);
		if (!ret)
			st->cached_dac[chan->channel] = val;
		mutex_unlock(&st->lock);
		return ret;
	case IIO_CHAN_INFO_SCALE:
		if (chan->type == IIO_VOLTAGE) {
			bool gain;

			if (val == st->scale_avail[0][0] &&
				val2 == st->scale_avail[0][1])
				gain = false;
			else if (val == st->scale_avail[1][0] &&
				 val2 == st->scale_avail[1][1])
				gain = true;
			else
				return -EINVAL;

			mutex_lock(&st->lock);

			ret = st->ops->reg_read(st, AD5592R_REG_CTRL,
						&st->cached_gp_ctrl);
			if (ret < 0) {
				mutex_unlock(&st->lock);
				return ret;
			}

			if (chan->output) {
				if (gain)
					st->cached_gp_ctrl |=
						AD5592R_REG_CTRL_DAC_RANGE;
				else
					st->cached_gp_ctrl &=
						~AD5592R_REG_CTRL_DAC_RANGE;
			} else {
				if (gain)
					st->cached_gp_ctrl |=
						AD5592R_REG_CTRL_ADC_RANGE;
				else
					st->cached_gp_ctrl &=
						~AD5592R_REG_CTRL_ADC_RANGE;
			}

			ret = st->ops->reg_write(st, AD5592R_REG_CTRL,
						 st->cached_gp_ctrl);
			mutex_unlock(&st->lock);

			return ret;
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int ad5592r_read_raw(struct iio_dev *iio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long m)
{
	struct ad5592r_state *st = iio_priv(iio_dev);
	u16 read_val;
	int ret;

	switch (m) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&st->lock);

		if (!chan->output) {
			ret = st->ops->read_adc(st, chan->channel, &read_val);
			if (ret)
				goto unlock;

			if ((read_val >> 12 & 0x7) != (chan->channel & 0x7)) {
				dev_err(st->dev, "Error while reading channel %u\n",
						chan->channel);
				ret = -EIO;
				goto unlock;
			}

			read_val &= GENMASK(11, 0);

		} else {
			read_val = st->cached_dac[chan->channel];
		}

		dev_dbg(st->dev, "Channel %u read: 0x%04hX\n",
				chan->channel, read_val);

		*val = (int) read_val;
		ret = IIO_VAL_INT;
		break;
	case IIO_CHAN_INFO_SCALE:
		*val = ad5592r_get_vref(st);

		if (chan->type == IIO_TEMP) {
			s64 tmp = *val * (3767897513LL / 25LL);
			*val = div_s64_rem(tmp, 1000000000LL, val2);

			ret = IIO_VAL_INT_PLUS_MICRO;
		} else {
			int mult;

			mutex_lock(&st->lock);

			if (chan->output)
				mult = !!(st->cached_gp_ctrl &
					AD5592R_REG_CTRL_DAC_RANGE);
			else
				mult = !!(st->cached_gp_ctrl &
					AD5592R_REG_CTRL_ADC_RANGE);

			*val *= ++mult;

			*val2 = chan->scan_type.realbits;
			ret = IIO_VAL_FRACTIONAL_LOG2;
		}
		break;
	case IIO_CHAN_INFO_OFFSET:
		ret = ad5592r_get_vref(st);

		mutex_lock(&st->lock);

		if (st->cached_gp_ctrl & AD5592R_REG_CTRL_ADC_RANGE)
			*val = (-34365 * 25) / ret;
		else
			*val = (-75365 * 25) / ret;
		ret =  IIO_VAL_INT;
		break;
	default:
		ret = -EINVAL;
	}

unlock:
	mutex_unlock(&st->lock);
	return ret;
}

static int ad5592r_write_raw_get_fmt(struct iio_dev *indio_dev,
				 struct iio_chan_spec const *chan, long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		return IIO_VAL_INT_PLUS_NANO;

	default:
		return IIO_VAL_INT_PLUS_MICRO;
	}

	return -EINVAL;
}

static const struct iio_info ad5592r_info = {
	.read_raw = ad5592r_read_raw,
	.write_raw = ad5592r_write_raw,
	.write_raw_get_fmt = ad5592r_write_raw_get_fmt,
};

static ssize_t ad5592r_show_scale_available(struct iio_dev *iio_dev,
					   uintptr_t private,
					   const struct iio_chan_spec *chan,
					   char *buf)
{
	struct ad5592r_state *st = iio_priv(iio_dev);

	return sprintf(buf, "%d.%09u %d.%09u\n",
		st->scale_avail[0][0], st->scale_avail[0][1],
		st->scale_avail[1][0], st->scale_avail[1][1]);
}

static struct iio_chan_spec_ext_info ad5592r_ext_info[] = {
	{
	 .name = "scale_available",
	 .read = ad5592r_show_scale_available,
	 .shared = true,
	 },
	{},
};

static void ad5592r_setup_channel(struct iio_dev *iio_dev,
		struct iio_chan_spec *chan, bool output, unsigned id)
{
	chan->type = IIO_VOLTAGE;
	chan->indexed = 1;
	chan->output = output;
	chan->channel = id;
	chan->info_mask_separate = BIT(IIO_CHAN_INFO_RAW);
	chan->info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE);
	chan->scan_type.sign = 'u';
	chan->scan_type.realbits = 12;
	chan->scan_type.storagebits = 16;
	chan->ext_info = ad5592r_ext_info;
}

static int ad5592r_alloc_channels(struct ad5592r_state *st)
{
	unsigned i, curr_channel = 0,
		 num_channels = st->num_channels;
	struct iio_dev *iio_dev = iio_priv_to_dev(st);
	struct iio_chan_spec *channels;
	struct fwnode_handle *child;
	u32 reg, tmp;
	int ret;

	device_for_each_child_node(st->dev, child) {
		ret = fwnode_property_read_u32(child, "reg", &reg);
		if (ret || reg >= ARRAY_SIZE(st->channel_modes))
			continue;

		ret = fwnode_property_read_u32(child, "adi,mode", &tmp);
		if (!ret)
			st->channel_modes[reg] = tmp;

		fwnode_property_read_u32(child, "adi,off-state", &tmp);
		if (!ret)
			st->channel_offstate[reg] = tmp;
	}

	channels = devm_kcalloc(st->dev,
			1 + 2 * num_channels, sizeof(*channels),
			GFP_KERNEL);
	if (!channels)
		return -ENOMEM;

	for (i = 0; i < num_channels; i++) {
		switch (st->channel_modes[i]) {
		case CH_MODE_DAC:
			ad5592r_setup_channel(iio_dev, &channels[curr_channel],
					true, i);
			curr_channel++;
			break;

		case CH_MODE_ADC:
			ad5592r_setup_channel(iio_dev, &channels[curr_channel],
					false, i);
			curr_channel++;
			break;

		case CH_MODE_DAC_AND_ADC:
			ad5592r_setup_channel(iio_dev, &channels[curr_channel],
					true, i);
			curr_channel++;
			ad5592r_setup_channel(iio_dev, &channels[curr_channel],
					false, i);
			curr_channel++;
			break;

		default:
			continue;
		}
	}

	channels[curr_channel].type = IIO_TEMP;
	channels[curr_channel].channel = 8;
	channels[curr_channel].info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				   BIT(IIO_CHAN_INFO_SCALE) |
				   BIT(IIO_CHAN_INFO_OFFSET);
	curr_channel++;

	iio_dev->num_channels = curr_channel;
	iio_dev->channels = channels;

	return 0;
}

static void ad5592r_init_scales(struct ad5592r_state *st, int vref_mV)
{
	s64 tmp = (s64)vref_mV * 1000000000LL >> 12;

	st->scale_avail[0][0] =
		div_s64_rem(tmp, 1000000000LL, &st->scale_avail[0][1]);
	st->scale_avail[1][0] =
		div_s64_rem(tmp * 2, 1000000000LL, &st->scale_avail[1][1]);
}

int ad5592r_probe(struct device *dev, const char *name,
		const struct ad5592r_rw_ops *ops)
{
	struct iio_dev *iio_dev;
	struct ad5592r_state *st;
	int ret;

	iio_dev = devm_iio_device_alloc(dev, sizeof(*st));
	if (!iio_dev)
		return -ENOMEM;

	st = iio_priv(iio_dev);
	st->dev = dev;
	st->ops = ops;
	st->num_channels = 8;
	dev_set_drvdata(dev, iio_dev);

	st->reg = devm_regulator_get_optional(dev, "vref");
	if (IS_ERR(st->reg)) {
		if ((PTR_ERR(st->reg) != -ENODEV) && dev->of_node)
			return PTR_ERR(st->reg);

		st->reg = NULL;
	} else {
		ret = regulator_enable(st->reg);
		if (ret)
			return ret;
	}

	iio_dev->dev.parent = dev;
	iio_dev->name = name;
	iio_dev->info = &ad5592r_info;
	iio_dev->modes = INDIO_DIRECT_MODE;

	mutex_init(&st->lock);

	ad5592r_init_scales(st, ad5592r_get_vref(st));

	ret = ad5592r_reset(st);
	if (ret)
		goto error_disable_reg;

	ret = ops->reg_write(st, AD5592R_REG_PD,
		     (st->reg == NULL) ? AD5592R_REG_PD_EN_REF : 0);
	if (ret)
		goto error_disable_reg;

	ret = ad5592r_alloc_channels(st);
	if (ret)
		goto error_disable_reg;

	ret = ad5592r_set_channel_modes(st);
	if (ret)
		goto error_reset_ch_modes;

	ret = iio_device_register(iio_dev);
	if (ret)
		goto error_reset_ch_modes;

	ret = ad5592r_gpio_init(st);
	if (ret)
		goto error_dev_unregister;

	return 0;

error_dev_unregister:
	iio_device_unregister(iio_dev);

error_reset_ch_modes:
	ad5592r_reset_channel_modes(st);

error_disable_reg:
	if (st->reg)
		regulator_disable(st->reg);

	return ret;
}
EXPORT_SYMBOL_GPL(ad5592r_probe);

int ad5592r_remove(struct device *dev)
{
	struct iio_dev *iio_dev = dev_get_drvdata(dev);
	struct ad5592r_state *st = iio_priv(iio_dev);

	iio_device_unregister(iio_dev);
	ad5592r_reset_channel_modes(st);
	ad5592r_gpio_cleanup(st);

	if (st->reg)
		regulator_disable(st->reg);

	return 0;
}
EXPORT_SYMBOL_GPL(ad5592r_remove);

MODULE_AUTHOR("Paul Cercueil <paul.cercueil@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD5592R multi-channel converters");
MODULE_LICENSE("GPL v2");
