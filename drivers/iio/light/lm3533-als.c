/*
 * lm3533-als.c -- LM3533 Ambient Light Sensor driver
 *
 * Copyright (C) 2011-2012 Texas Instruments
 *
 * Author: Johan Hovold <jhovold@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under  the terms of the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/atomic.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iio/events.h>
#include <linux/iio/iio.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/mfd/core.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include <linux/mfd/lm3533.h>


#define LM3533_ALS_RESISTOR_MIN			1
#define LM3533_ALS_RESISTOR_MAX			127
#define LM3533_ALS_CHANNEL_CURRENT_MAX		2
#define LM3533_ALS_THRESH_MAX			3
#define LM3533_ALS_ZONE_MAX			4

#define LM3533_REG_ALS_RESISTOR_SELECT		0x30
#define LM3533_REG_ALS_CONF			0x31
#define LM3533_REG_ALS_ZONE_INFO		0x34
#define LM3533_REG_ALS_READ_ADC_RAW		0x37
#define LM3533_REG_ALS_READ_ADC_AVERAGE		0x38
#define LM3533_REG_ALS_BOUNDARY_BASE		0x50
#define LM3533_REG_ALS_TARGET_BASE		0x60

#define LM3533_ALS_ENABLE_MASK			0x01
#define LM3533_ALS_INPUT_MODE_MASK		0x02
#define LM3533_ALS_INT_ENABLE_MASK		0x01

#define LM3533_ALS_ZONE_SHIFT			2
#define LM3533_ALS_ZONE_MASK			0x1c

#define LM3533_ALS_FLAG_INT_ENABLED		1


struct lm3533_als {
	struct lm3533 *lm3533;
	struct platform_device *pdev;

	unsigned long flags;
	int irq;

	atomic_t zone;
	struct mutex thresh_mutex;
};


static int lm3533_als_get_adc(struct iio_dev *indio_dev, bool average,
								int *adc)
{
	struct lm3533_als *als = iio_priv(indio_dev);
	u8 reg;
	u8 val;
	int ret;

	if (average)
		reg = LM3533_REG_ALS_READ_ADC_AVERAGE;
	else
		reg = LM3533_REG_ALS_READ_ADC_RAW;

	ret = lm3533_read(als->lm3533, reg, &val);
	if (ret) {
		dev_err(&indio_dev->dev, "failed to read adc\n");
		return ret;
	}

	*adc = val;

	return 0;
}

static int _lm3533_als_get_zone(struct iio_dev *indio_dev, u8 *zone)
{
	struct lm3533_als *als = iio_priv(indio_dev);
	u8 val;
	int ret;

	ret = lm3533_read(als->lm3533, LM3533_REG_ALS_ZONE_INFO, &val);
	if (ret) {
		dev_err(&indio_dev->dev, "failed to read zone\n");
		return ret;
	}

	val = (val & LM3533_ALS_ZONE_MASK) >> LM3533_ALS_ZONE_SHIFT;
	*zone = min_t(u8, val, LM3533_ALS_ZONE_MAX);

	return 0;
}

static int lm3533_als_get_zone(struct iio_dev *indio_dev, u8 *zone)
{
	struct lm3533_als *als = iio_priv(indio_dev);
	int ret;

	if (test_bit(LM3533_ALS_FLAG_INT_ENABLED, &als->flags)) {
		*zone = atomic_read(&als->zone);
	} else {
		ret = _lm3533_als_get_zone(indio_dev, zone);
		if (ret)
			return ret;
	}

	return 0;
}

/*
 * channel	output channel 0..2
 * zone		zone 0..4
 */
static inline u8 lm3533_als_get_target_reg(unsigned channel, unsigned zone)
{
	return LM3533_REG_ALS_TARGET_BASE + 5 * channel + zone;
}

static int lm3533_als_get_target(struct iio_dev *indio_dev, unsigned channel,
							unsigned zone, u8 *val)
{
	struct lm3533_als *als = iio_priv(indio_dev);
	u8 reg;
	int ret;

	if (channel > LM3533_ALS_CHANNEL_CURRENT_MAX)
		return -EINVAL;

	if (zone > LM3533_ALS_ZONE_MAX)
		return -EINVAL;

	reg = lm3533_als_get_target_reg(channel, zone);
	ret = lm3533_read(als->lm3533, reg, val);
	if (ret)
		dev_err(&indio_dev->dev, "failed to get target current\n");

	return ret;
}

static int lm3533_als_set_target(struct iio_dev *indio_dev, unsigned channel,
							unsigned zone, u8 val)
{
	struct lm3533_als *als = iio_priv(indio_dev);
	u8 reg;
	int ret;

	if (channel > LM3533_ALS_CHANNEL_CURRENT_MAX)
		return -EINVAL;

	if (zone > LM3533_ALS_ZONE_MAX)
		return -EINVAL;

	reg = lm3533_als_get_target_reg(channel, zone);
	ret = lm3533_write(als->lm3533, reg, val);
	if (ret)
		dev_err(&indio_dev->dev, "failed to set target current\n");

	return ret;
}

static int lm3533_als_get_current(struct iio_dev *indio_dev, unsigned channel,
								int *val)
{
	u8 zone;
	u8 target;
	int ret;

	ret = lm3533_als_get_zone(indio_dev, &zone);
	if (ret)
		return ret;

	ret = lm3533_als_get_target(indio_dev, channel, zone, &target);
	if (ret)
		return ret;

	*val = target;

	return 0;
}

static int lm3533_als_read_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int *val, int *val2, long mask)
{
	int ret;

	switch (mask) {
	case 0:
		switch (chan->type) {
		case IIO_LIGHT:
			ret = lm3533_als_get_adc(indio_dev, false, val);
			break;
		case IIO_CURRENT:
			ret = lm3533_als_get_current(indio_dev, chan->channel,
									val);
			break;
		default:
			return -EINVAL;
		}
		break;
	case IIO_CHAN_INFO_AVERAGE_RAW:
		ret = lm3533_als_get_adc(indio_dev, true, val);
		break;
	default:
		return -EINVAL;
	}

	if (ret)
		return ret;

	return IIO_VAL_INT;
}

#define CHANNEL_CURRENT(_channel)					\
	{								\
		.type		= IIO_CURRENT,				\
		.channel	= _channel,				\
		.indexed	= true,					\
		.output		= true,					\
		.info_mask	= IIO_CHAN_INFO_RAW_SEPARATE_BIT,	\
	}

static const struct iio_chan_spec lm3533_als_channels[] = {
	{
		.type		= IIO_LIGHT,
		.channel	= 0,
		.indexed	= true,
		.info_mask	= (IIO_CHAN_INFO_AVERAGE_RAW_SEPARATE_BIT |
				   IIO_CHAN_INFO_RAW_SEPARATE_BIT),
	},
	CHANNEL_CURRENT(0),
	CHANNEL_CURRENT(1),
	CHANNEL_CURRENT(2),
};

static irqreturn_t lm3533_als_isr(int irq, void *dev_id)
{

	struct iio_dev *indio_dev = dev_id;
	struct lm3533_als *als = iio_priv(indio_dev);
	u8 zone;
	int ret;

	/* Clear interrupt by reading the ALS zone register. */
	ret = _lm3533_als_get_zone(indio_dev, &zone);
	if (ret)
		goto out;

	atomic_set(&als->zone, zone);

	iio_push_event(indio_dev,
		       IIO_UNMOD_EVENT_CODE(IIO_LIGHT,
					    0,
					    IIO_EV_TYPE_THRESH,
					    IIO_EV_DIR_EITHER),
		       iio_get_time_ns());
out:
	return IRQ_HANDLED;
}

static int lm3533_als_set_int_mode(struct iio_dev *indio_dev, int enable)
{
	struct lm3533_als *als = iio_priv(indio_dev);
	u8 mask = LM3533_ALS_INT_ENABLE_MASK;
	u8 val;
	int ret;

	if (enable)
		val = mask;
	else
		val = 0;

	ret = lm3533_update(als->lm3533, LM3533_REG_ALS_ZONE_INFO, val, mask);
	if (ret) {
		dev_err(&indio_dev->dev, "failed to set int mode %d\n",
								enable);
		return ret;
	}

	return 0;
}

static int lm3533_als_get_int_mode(struct iio_dev *indio_dev, int *enable)
{
	struct lm3533_als *als = iio_priv(indio_dev);
	u8 mask = LM3533_ALS_INT_ENABLE_MASK;
	u8 val;
	int ret;

	ret = lm3533_read(als->lm3533, LM3533_REG_ALS_ZONE_INFO, &val);
	if (ret) {
		dev_err(&indio_dev->dev, "failed to get int mode\n");
		return ret;
	}

	*enable = !!(val & mask);

	return 0;
}

static inline u8 lm3533_als_get_threshold_reg(unsigned nr, bool raising)
{
	u8 offset = !raising;

	return LM3533_REG_ALS_BOUNDARY_BASE + 2 * nr + offset;
}

static int lm3533_als_get_threshold(struct iio_dev *indio_dev, unsigned nr,
							bool raising, u8 *val)
{
	struct lm3533_als *als = iio_priv(indio_dev);
	u8 reg;
	int ret;

	if (nr > LM3533_ALS_THRESH_MAX)
		return -EINVAL;

	reg = lm3533_als_get_threshold_reg(nr, raising);
	ret = lm3533_read(als->lm3533, reg, val);
	if (ret)
		dev_err(&indio_dev->dev, "failed to get threshold\n");

	return ret;
}

static int lm3533_als_set_threshold(struct iio_dev *indio_dev, unsigned nr,
							bool raising, u8 val)
{
	struct lm3533_als *als = iio_priv(indio_dev);
	u8 val2;
	u8 reg, reg2;
	int ret;

	if (nr > LM3533_ALS_THRESH_MAX)
		return -EINVAL;

	reg = lm3533_als_get_threshold_reg(nr, raising);
	reg2 = lm3533_als_get_threshold_reg(nr, !raising);

	mutex_lock(&als->thresh_mutex);
	ret = lm3533_read(als->lm3533, reg2, &val2);
	if (ret) {
		dev_err(&indio_dev->dev, "failed to get threshold\n");
		goto out;
	}
	/*
	 * This device does not allow negative hysteresis (in fact, it uses
	 * whichever value is smaller as the lower bound) so we need to make
	 * sure that thresh_falling <= thresh_raising.
	 */
	if ((raising && (val < val2)) || (!raising && (val > val2))) {
		ret = -EINVAL;
		goto out;
	}

	ret = lm3533_write(als->lm3533, reg, val);
	if (ret) {
		dev_err(&indio_dev->dev, "failed to set threshold\n");
		goto out;
	}
out:
	mutex_unlock(&als->thresh_mutex);

	return ret;
}

static int lm3533_als_get_hysteresis(struct iio_dev *indio_dev, unsigned nr,
								u8 *val)
{
	struct lm3533_als *als = iio_priv(indio_dev);
	u8 falling;
	u8 raising;
	int ret;

	if (nr > LM3533_ALS_THRESH_MAX)
		return -EINVAL;

	mutex_lock(&als->thresh_mutex);
	ret = lm3533_als_get_threshold(indio_dev, nr, false, &falling);
	if (ret)
		goto out;
	ret = lm3533_als_get_threshold(indio_dev, nr, true, &raising);
	if (ret)
		goto out;

	*val = raising - falling;
out:
	mutex_unlock(&als->thresh_mutex);

	return ret;
}

static ssize_t show_thresh_either_en(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct lm3533_als *als = iio_priv(indio_dev);
	int enable;
	int ret;

	if (als->irq) {
		ret = lm3533_als_get_int_mode(indio_dev, &enable);
		if (ret)
			return ret;
	} else {
		enable = 0;
	}

	return scnprintf(buf, PAGE_SIZE, "%u\n", enable);
}

static ssize_t store_thresh_either_en(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct lm3533_als *als = iio_priv(indio_dev);
	unsigned long enable;
	bool int_enabled;
	u8 zone;
	int ret;

	if (!als->irq)
		return -EBUSY;

	if (kstrtoul(buf, 0, &enable))
		return -EINVAL;

	int_enabled = test_bit(LM3533_ALS_FLAG_INT_ENABLED, &als->flags);

	if (enable && !int_enabled) {
		ret = lm3533_als_get_zone(indio_dev, &zone);
		if (ret)
			return ret;

		atomic_set(&als->zone, zone);

		set_bit(LM3533_ALS_FLAG_INT_ENABLED, &als->flags);
	}

	ret = lm3533_als_set_int_mode(indio_dev, enable);
	if (ret) {
		if (!int_enabled)
			clear_bit(LM3533_ALS_FLAG_INT_ENABLED, &als->flags);

		return ret;
	}

	if (!enable)
		clear_bit(LM3533_ALS_FLAG_INT_ENABLED, &als->flags);

	return len;
}

static ssize_t show_zone(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	u8 zone;
	int ret;

	ret = lm3533_als_get_zone(indio_dev, &zone);
	if (ret)
		return ret;

	return scnprintf(buf, PAGE_SIZE, "%u\n", zone);
}

enum lm3533_als_attribute_type {
	LM3533_ATTR_TYPE_HYSTERESIS,
	LM3533_ATTR_TYPE_TARGET,
	LM3533_ATTR_TYPE_THRESH_FALLING,
	LM3533_ATTR_TYPE_THRESH_RAISING,
};

struct lm3533_als_attribute {
	struct device_attribute dev_attr;
	enum lm3533_als_attribute_type type;
	u8 val1;
	u8 val2;
};

static inline struct lm3533_als_attribute *
to_lm3533_als_attr(struct device_attribute *attr)
{
	return container_of(attr, struct lm3533_als_attribute, dev_attr);
}

static ssize_t show_als_attr(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct lm3533_als_attribute *als_attr = to_lm3533_als_attr(attr);
	u8 val;
	int ret;

	switch (als_attr->type) {
	case LM3533_ATTR_TYPE_HYSTERESIS:
		ret = lm3533_als_get_hysteresis(indio_dev, als_attr->val1,
									&val);
		break;
	case LM3533_ATTR_TYPE_TARGET:
		ret = lm3533_als_get_target(indio_dev, als_attr->val1,
							als_attr->val2, &val);
		break;
	case LM3533_ATTR_TYPE_THRESH_FALLING:
		ret = lm3533_als_get_threshold(indio_dev, als_attr->val1,
								false, &val);
		break;
	case LM3533_ATTR_TYPE_THRESH_RAISING:
		ret = lm3533_als_get_threshold(indio_dev, als_attr->val1,
								true, &val);
		break;
	default:
		ret = -ENXIO;
	}

	if (ret)
		return ret;

	return scnprintf(buf, PAGE_SIZE, "%u\n", val);
}

static ssize_t store_als_attr(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct lm3533_als_attribute *als_attr = to_lm3533_als_attr(attr);
	u8 val;
	int ret;

	if (kstrtou8(buf, 0, &val))
		return -EINVAL;

	switch (als_attr->type) {
	case LM3533_ATTR_TYPE_TARGET:
		ret = lm3533_als_set_target(indio_dev, als_attr->val1,
							als_attr->val2, val);
		break;
	case LM3533_ATTR_TYPE_THRESH_FALLING:
		ret = lm3533_als_set_threshold(indio_dev, als_attr->val1,
								false, val);
		break;
	case LM3533_ATTR_TYPE_THRESH_RAISING:
		ret = lm3533_als_set_threshold(indio_dev, als_attr->val1,
								true, val);
		break;
	default:
		ret = -ENXIO;
	}

	if (ret)
		return ret;

	return len;
}

#define ALS_ATTR(_name, _mode, _show, _store, _type, _val1, _val2)	\
	{ .dev_attr	= __ATTR(_name, _mode, _show, _store),		\
	  .type		= _type,					\
	  .val1		= _val1,					\
	  .val2		= _val2 }

#define LM3533_ALS_ATTR(_name, _mode, _show, _store, _type, _val1, _val2) \
	struct lm3533_als_attribute lm3533_als_attr_##_name =		  \
		ALS_ATTR(_name, _mode, _show, _store, _type, _val1, _val2)

#define ALS_TARGET_ATTR_RW(_channel, _zone)				\
	LM3533_ALS_ATTR(out_current##_channel##_current##_zone##_raw,	\
				S_IRUGO | S_IWUSR,			\
				show_als_attr, store_als_attr,		\
				LM3533_ATTR_TYPE_TARGET, _channel, _zone)
/*
 * ALS output current values (ALS mapper targets)
 *
 * out_current[0-2]_current[0-4]_raw		0-255
 */
static ALS_TARGET_ATTR_RW(0, 0);
static ALS_TARGET_ATTR_RW(0, 1);
static ALS_TARGET_ATTR_RW(0, 2);
static ALS_TARGET_ATTR_RW(0, 3);
static ALS_TARGET_ATTR_RW(0, 4);

static ALS_TARGET_ATTR_RW(1, 0);
static ALS_TARGET_ATTR_RW(1, 1);
static ALS_TARGET_ATTR_RW(1, 2);
static ALS_TARGET_ATTR_RW(1, 3);
static ALS_TARGET_ATTR_RW(1, 4);

static ALS_TARGET_ATTR_RW(2, 0);
static ALS_TARGET_ATTR_RW(2, 1);
static ALS_TARGET_ATTR_RW(2, 2);
static ALS_TARGET_ATTR_RW(2, 3);
static ALS_TARGET_ATTR_RW(2, 4);

#define ALS_THRESH_FALLING_ATTR_RW(_nr)					\
	LM3533_ALS_ATTR(in_illuminance0_thresh##_nr##_falling_value,	\
			S_IRUGO | S_IWUSR,				\
			show_als_attr, store_als_attr,		\
			LM3533_ATTR_TYPE_THRESH_FALLING, _nr, 0)

#define ALS_THRESH_RAISING_ATTR_RW(_nr)					\
	LM3533_ALS_ATTR(in_illuminance0_thresh##_nr##_raising_value,	\
			S_IRUGO | S_IWUSR,				\
			show_als_attr, store_als_attr,			\
			LM3533_ATTR_TYPE_THRESH_RAISING, _nr, 0)
/*
 * ALS Zone thresholds (boundaries)
 *
 * in_illuminance0_thresh[0-3]_falling_value	0-255
 * in_illuminance0_thresh[0-3]_raising_value	0-255
 */
static ALS_THRESH_FALLING_ATTR_RW(0);
static ALS_THRESH_FALLING_ATTR_RW(1);
static ALS_THRESH_FALLING_ATTR_RW(2);
static ALS_THRESH_FALLING_ATTR_RW(3);

static ALS_THRESH_RAISING_ATTR_RW(0);
static ALS_THRESH_RAISING_ATTR_RW(1);
static ALS_THRESH_RAISING_ATTR_RW(2);
static ALS_THRESH_RAISING_ATTR_RW(3);

#define ALS_HYSTERESIS_ATTR_RO(_nr)					\
	LM3533_ALS_ATTR(in_illuminance0_thresh##_nr##_hysteresis,	\
			S_IRUGO, show_als_attr, NULL,			\
			LM3533_ATTR_TYPE_HYSTERESIS, _nr, 0)
/*
 * ALS Zone threshold hysteresis
 *
 * threshY_hysteresis = threshY_raising - threshY_falling
 *
 * in_illuminance0_thresh[0-3]_hysteresis	0-255
 * in_illuminance0_thresh[0-3]_hysteresis	0-255
 */
static ALS_HYSTERESIS_ATTR_RO(0);
static ALS_HYSTERESIS_ATTR_RO(1);
static ALS_HYSTERESIS_ATTR_RO(2);
static ALS_HYSTERESIS_ATTR_RO(3);

#define ILLUMINANCE_ATTR_RO(_name) \
	DEVICE_ATTR(in_illuminance0_##_name, S_IRUGO, show_##_name, NULL)
#define ILLUMINANCE_ATTR_RW(_name) \
	DEVICE_ATTR(in_illuminance0_##_name, S_IRUGO | S_IWUSR , \
						show_##_name, store_##_name)
/*
 * ALS Zone threshold-event enable
 *
 * in_illuminance0_thresh_either_en		0,1
 */
static ILLUMINANCE_ATTR_RW(thresh_either_en);

/*
 * ALS Current Zone
 *
 * in_illuminance0_zone		0-4
 */
static ILLUMINANCE_ATTR_RO(zone);

static struct attribute *lm3533_als_event_attributes[] = {
	&dev_attr_in_illuminance0_thresh_either_en.attr,
	&lm3533_als_attr_in_illuminance0_thresh0_falling_value.dev_attr.attr,
	&lm3533_als_attr_in_illuminance0_thresh0_hysteresis.dev_attr.attr,
	&lm3533_als_attr_in_illuminance0_thresh0_raising_value.dev_attr.attr,
	&lm3533_als_attr_in_illuminance0_thresh1_falling_value.dev_attr.attr,
	&lm3533_als_attr_in_illuminance0_thresh1_hysteresis.dev_attr.attr,
	&lm3533_als_attr_in_illuminance0_thresh1_raising_value.dev_attr.attr,
	&lm3533_als_attr_in_illuminance0_thresh2_falling_value.dev_attr.attr,
	&lm3533_als_attr_in_illuminance0_thresh2_hysteresis.dev_attr.attr,
	&lm3533_als_attr_in_illuminance0_thresh2_raising_value.dev_attr.attr,
	&lm3533_als_attr_in_illuminance0_thresh3_falling_value.dev_attr.attr,
	&lm3533_als_attr_in_illuminance0_thresh3_hysteresis.dev_attr.attr,
	&lm3533_als_attr_in_illuminance0_thresh3_raising_value.dev_attr.attr,
	NULL
};

static struct attribute_group lm3533_als_event_attribute_group = {
	.attrs = lm3533_als_event_attributes
};

static struct attribute *lm3533_als_attributes[] = {
	&dev_attr_in_illuminance0_zone.attr,
	&lm3533_als_attr_out_current0_current0_raw.dev_attr.attr,
	&lm3533_als_attr_out_current0_current1_raw.dev_attr.attr,
	&lm3533_als_attr_out_current0_current2_raw.dev_attr.attr,
	&lm3533_als_attr_out_current0_current3_raw.dev_attr.attr,
	&lm3533_als_attr_out_current0_current4_raw.dev_attr.attr,
	&lm3533_als_attr_out_current1_current0_raw.dev_attr.attr,
	&lm3533_als_attr_out_current1_current1_raw.dev_attr.attr,
	&lm3533_als_attr_out_current1_current2_raw.dev_attr.attr,
	&lm3533_als_attr_out_current1_current3_raw.dev_attr.attr,
	&lm3533_als_attr_out_current1_current4_raw.dev_attr.attr,
	&lm3533_als_attr_out_current2_current0_raw.dev_attr.attr,
	&lm3533_als_attr_out_current2_current1_raw.dev_attr.attr,
	&lm3533_als_attr_out_current2_current2_raw.dev_attr.attr,
	&lm3533_als_attr_out_current2_current3_raw.dev_attr.attr,
	&lm3533_als_attr_out_current2_current4_raw.dev_attr.attr,
	NULL
};

static struct attribute_group lm3533_als_attribute_group = {
	.attrs = lm3533_als_attributes
};

static int __devinit lm3533_als_set_input_mode(struct lm3533_als *als,
								bool pwm_mode)
{
	u8 mask = LM3533_ALS_INPUT_MODE_MASK;
	u8 val;
	int ret;

	if (pwm_mode)
		val = mask;	/* pwm input */
	else
		val = 0;	/* analog input */

	ret = lm3533_update(als->lm3533, LM3533_REG_ALS_CONF, val, mask);
	if (ret) {
		dev_err(&als->pdev->dev, "failed to set input mode %d\n",
								pwm_mode);
		return ret;
	}

	return 0;
}

static int __devinit lm3533_als_set_resistor(struct lm3533_als *als, u8 val)
{
	int ret;

	if (val < LM3533_ALS_RESISTOR_MIN || val > LM3533_ALS_RESISTOR_MAX)
		return -EINVAL;

	ret = lm3533_write(als->lm3533, LM3533_REG_ALS_RESISTOR_SELECT, val);
	if (ret) {
		dev_err(&als->pdev->dev, "failed to set resistor\n");
		return ret;
	}

	return 0;
}

static int __devinit lm3533_als_setup(struct lm3533_als *als,
					struct lm3533_als_platform_data *pdata)
{
	int ret;

	ret = lm3533_als_set_input_mode(als, pdata->pwm_mode);
	if (ret)
		return ret;

	/* ALS input is always high impedance in PWM-mode. */
	if (!pdata->pwm_mode) {
		ret = lm3533_als_set_resistor(als, pdata->r_select);
		if (ret)
			return ret;
	}

	return 0;
}

static int __devinit lm3533_als_setup_irq(struct lm3533_als *als, void *dev)
{
	u8 mask = LM3533_ALS_INT_ENABLE_MASK;
	int ret;

	/* Make sure interrupts are disabled. */
	ret = lm3533_update(als->lm3533, LM3533_REG_ALS_ZONE_INFO, 0, mask);
	if (ret) {
		dev_err(&als->pdev->dev, "failed to disable interrupts\n");
		return ret;
	}

	ret = request_threaded_irq(als->irq, NULL, lm3533_als_isr,
					IRQF_TRIGGER_LOW | IRQF_ONESHOT,
					dev_name(&als->pdev->dev), dev);
	if (ret) {
		dev_err(&als->pdev->dev, "failed to request irq %d\n",
								als->irq);
		return ret;
	}

	return 0;
}

static int __devinit lm3533_als_enable(struct lm3533_als *als)
{
	u8 mask = LM3533_ALS_ENABLE_MASK;
	int ret;

	ret = lm3533_update(als->lm3533, LM3533_REG_ALS_CONF, mask, mask);
	if (ret)
		dev_err(&als->pdev->dev, "failed to enable ALS\n");

	return ret;
}

static int lm3533_als_disable(struct lm3533_als *als)
{
	u8 mask = LM3533_ALS_ENABLE_MASK;
	int ret;

	ret = lm3533_update(als->lm3533, LM3533_REG_ALS_CONF, 0, mask);
	if (ret)
		dev_err(&als->pdev->dev, "failed to disable ALS\n");

	return ret;
}

static const struct iio_info lm3533_als_info = {
	.attrs		= &lm3533_als_attribute_group,
	.event_attrs	= &lm3533_als_event_attribute_group,
	.driver_module	= THIS_MODULE,
	.read_raw	= &lm3533_als_read_raw,
};

static int __devinit lm3533_als_probe(struct platform_device *pdev)
{
	struct lm3533 *lm3533;
	struct lm3533_als_platform_data *pdata;
	struct lm3533_als *als;
	struct iio_dev *indio_dev;
	int ret;

	lm3533 = dev_get_drvdata(pdev->dev.parent);
	if (!lm3533)
		return -EINVAL;

	pdata = pdev->dev.platform_data;
	if (!pdata) {
		dev_err(&pdev->dev, "no platform data\n");
		return -EINVAL;
	}

	indio_dev = iio_device_alloc(sizeof(*als));
	if (!indio_dev)
		return -ENOMEM;

	indio_dev->info = &lm3533_als_info;
	indio_dev->channels = lm3533_als_channels;
	indio_dev->num_channels = ARRAY_SIZE(lm3533_als_channels);
	indio_dev->name = dev_name(&pdev->dev);
	indio_dev->dev.parent = pdev->dev.parent;
	indio_dev->modes = INDIO_DIRECT_MODE;

	als = iio_priv(indio_dev);
	als->lm3533 = lm3533;
	als->pdev = pdev;
	als->irq = lm3533->irq;
	atomic_set(&als->zone, 0);
	mutex_init(&als->thresh_mutex);

	platform_set_drvdata(pdev, indio_dev);

	if (als->irq) {
		ret = lm3533_als_setup_irq(als, indio_dev);
		if (ret)
			goto err_free_dev;
	}

	ret = lm3533_als_setup(als, pdata);
	if (ret)
		goto err_free_irq;

	ret = lm3533_als_enable(als);
	if (ret)
		goto err_free_irq;

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err(&pdev->dev, "failed to register ALS\n");
		goto err_disable;
	}

	return 0;

err_disable:
	lm3533_als_disable(als);
err_free_irq:
	if (als->irq)
		free_irq(als->irq, indio_dev);
err_free_dev:
	iio_device_free(indio_dev);

	return ret;
}

static int __devexit lm3533_als_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct lm3533_als *als = iio_priv(indio_dev);

	lm3533_als_set_int_mode(indio_dev, false);
	iio_device_unregister(indio_dev);
	lm3533_als_disable(als);
	if (als->irq)
		free_irq(als->irq, indio_dev);
	iio_device_free(indio_dev);

	return 0;
}

static struct platform_driver lm3533_als_driver = {
	.driver	= {
		.name	= "lm3533-als",
		.owner	= THIS_MODULE,
	},
	.probe		= lm3533_als_probe,
	.remove		= __devexit_p(lm3533_als_remove),
};
module_platform_driver(lm3533_als_driver);

MODULE_AUTHOR("Johan Hovold <jhovold@gmail.com>");
MODULE_DESCRIPTION("LM3533 Ambient Light Sensor driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:lm3533-als");
