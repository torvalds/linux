/*
 * ADT7410 digital temperature sensor driver supporting ADT7310/ADT7410
 *
 * Copyright 2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/list.h>
#include <linux/i2c.h>
#include <linux/spi/spi.h>
#include <linux/module.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/events.h>

/*
 * ADT7410 registers definition
 */

#define ADT7410_TEMPERATURE		0
#define ADT7410_STATUS			2
#define ADT7410_CONFIG			3
#define ADT7410_T_ALARM_HIGH		4
#define ADT7410_T_ALARM_LOW		6
#define ADT7410_T_CRIT			8
#define ADT7410_T_HYST			0xA
#define ADT7410_ID			0xB
#define ADT7410_RESET			0x2F

/*
 * ADT7310 registers definition
 */

#define ADT7310_STATUS			0
#define ADT7310_CONFIG			1
#define ADT7310_TEMPERATURE		2
#define ADT7310_ID			3
#define ADT7310_T_CRIT			4
#define ADT7310_T_HYST			5
#define ADT7310_T_ALARM_HIGH		6
#define ADT7310_T_ALARM_LOW		7

/*
 * ADT7410 status
 */
#define ADT7410_STAT_T_LOW		0x10
#define ADT7410_STAT_T_HIGH		0x20
#define ADT7410_STAT_T_CRIT		0x40
#define ADT7410_STAT_NOT_RDY		0x80

/*
 * ADT7410 config
 */
#define ADT7410_FAULT_QUEUE_MASK	0x3
#define ADT7410_CT_POLARITY		0x4
#define ADT7410_INT_POLARITY		0x8
#define ADT7410_EVENT_MODE		0x10
#define ADT7410_MODE_MASK		0x60
#define ADT7410_ONESHOT			0x20
#define ADT7410_SPS			0x40
#define ADT7410_PD			0x60
#define ADT7410_RESOLUTION		0x80

/*
 * ADT7410 masks
 */
#define ADT7410_T16_VALUE_SIGN			0x8000
#define ADT7410_T16_VALUE_FLOAT_OFFSET		7
#define ADT7410_T16_VALUE_FLOAT_MASK		0x7F
#define ADT7410_T13_VALUE_SIGN			0x1000
#define ADT7410_T13_VALUE_OFFSET		3
#define ADT7410_T13_VALUE_FLOAT_OFFSET		4
#define ADT7410_T13_VALUE_FLOAT_MASK		0xF
#define ADT7410_T_HYST_MASK			0xF
#define ADT7410_DEVICE_ID_MASK			0xF
#define ADT7410_MANUFACTORY_ID_MASK		0xF0
#define ADT7410_MANUFACTORY_ID_OFFSET		4


#define ADT7310_CMD_REG_MASK			0x28
#define ADT7310_CMD_REG_OFFSET			3
#define ADT7310_CMD_READ			0x40
#define ADT7310_CMD_CON_READ			0x4

#define ADT7410_IRQS				2

/*
 * struct adt7410_chip_info - chip specifc information
 */

struct adt7410_chip_info;

struct adt7410_ops {
	int (*read_word)(struct adt7410_chip_info *, u8 reg, u16 *data);
	int (*write_word)(struct adt7410_chip_info *, u8 reg, u16 data);
	int (*read_byte)(struct adt7410_chip_info *, u8 reg, u8 *data);
	int (*write_byte)(struct adt7410_chip_info *, u8 reg, u8 data);
};

struct adt7410_chip_info {
	struct device *dev;
	u8  config;

	const struct adt7410_ops *ops;
};

static int adt7410_read_word(struct adt7410_chip_info *chip, u8 reg, u16 *data)
{
	return chip->ops->read_word(chip, reg, data);
}

static int adt7410_write_word(struct adt7410_chip_info *chip, u8 reg, u16 data)
{
	return chip->ops->write_word(chip, reg, data);
}

static int adt7410_read_byte(struct adt7410_chip_info *chip, u8 reg, u8 *data)
{
	return chip->ops->read_byte(chip, reg, data);
}

static int adt7410_write_byte(struct adt7410_chip_info *chip, u8 reg, u8 data)
{
	return chip->ops->write_byte(chip, reg, data);
}

static ssize_t adt7410_show_mode(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7410_chip_info *chip = iio_priv(dev_info);
	u8 config;

	config = chip->config & ADT7410_MODE_MASK;

	switch (config) {
	case ADT7410_PD:
		return sprintf(buf, "power-down\n");
	case ADT7410_ONESHOT:
		return sprintf(buf, "one-shot\n");
	case ADT7410_SPS:
		return sprintf(buf, "sps\n");
	default:
		return sprintf(buf, "full\n");
	}
}

static ssize_t adt7410_store_mode(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7410_chip_info *chip = iio_priv(dev_info);
	u16 config;
	int ret;

	ret = adt7410_read_byte(chip, ADT7410_CONFIG, &chip->config);
	if (ret)
		return -EIO;

	config = chip->config & (~ADT7410_MODE_MASK);
	if (strcmp(buf, "power-down"))
		config |= ADT7410_PD;
	else if (strcmp(buf, "one-shot"))
		config |= ADT7410_ONESHOT;
	else if (strcmp(buf, "sps"))
		config |= ADT7410_SPS;

	ret = adt7410_write_byte(chip, ADT7410_CONFIG, config);
	if (ret)
		return -EIO;

	chip->config = config;

	return len;
}

static IIO_DEVICE_ATTR(mode, S_IRUGO | S_IWUSR,
		adt7410_show_mode,
		adt7410_store_mode,
		0);

static ssize_t adt7410_show_available_modes(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return sprintf(buf, "full\none-shot\nsps\npower-down\n");
}

static IIO_DEVICE_ATTR(available_modes, S_IRUGO, adt7410_show_available_modes, NULL, 0);

static ssize_t adt7410_show_resolution(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7410_chip_info *chip = iio_priv(dev_info);
	int ret;
	int bits;

	ret = adt7410_read_byte(chip, ADT7410_CONFIG, &chip->config);
	if (ret)
		return -EIO;

	if (chip->config & ADT7410_RESOLUTION)
		bits = 16;
	else
		bits = 13;

	return sprintf(buf, "%d bits\n", bits);
}

static ssize_t adt7410_store_resolution(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7410_chip_info *chip = iio_priv(dev_info);
	unsigned long data;
	u16 config;
	int ret;

	ret = strict_strtoul(buf, 10, &data);
	if (ret)
		return -EINVAL;

	ret = adt7410_read_byte(chip, ADT7410_CONFIG, &chip->config);
	if (ret)
		return -EIO;

	config = chip->config & (~ADT7410_RESOLUTION);
	if (data)
		config |= ADT7410_RESOLUTION;

	ret = adt7410_write_byte(chip, ADT7410_CONFIG, config);
	if (ret)
		return -EIO;

	chip->config = config;

	return len;
}

static IIO_DEVICE_ATTR(resolution, S_IRUGO | S_IWUSR,
		adt7410_show_resolution,
		adt7410_store_resolution,
		0);

static ssize_t adt7410_show_id(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7410_chip_info *chip = iio_priv(dev_info);
	u8 id;
	int ret;

	ret = adt7410_read_byte(chip, ADT7410_ID, &id);
	if (ret)
		return -EIO;

	return sprintf(buf, "device id: 0x%x\nmanufactory id: 0x%x\n",
			id & ADT7410_DEVICE_ID_MASK,
			(id & ADT7410_MANUFACTORY_ID_MASK) >> ADT7410_MANUFACTORY_ID_OFFSET);
}

static IIO_DEVICE_ATTR(id, S_IRUGO | S_IWUSR,
		adt7410_show_id,
		NULL,
		0);

static ssize_t adt7410_convert_temperature(struct adt7410_chip_info *chip,
		u16 data, char *buf)
{
	char sign = ' ';

	if (!(chip->config & ADT7410_RESOLUTION))
		data &= ~0x7;

	if (data & ADT7410_T16_VALUE_SIGN) {
		/* convert supplement to positive value */
		data = (u16)((ADT7410_T16_VALUE_SIGN << 1) - (u32)data);
		sign = '-';
	}
	return sprintf(buf, "%c%d.%.7d\n", sign,
			(data >> ADT7410_T16_VALUE_FLOAT_OFFSET),
			(data & ADT7410_T16_VALUE_FLOAT_MASK) * 78125);
}

static ssize_t adt7410_show_value(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7410_chip_info *chip = iio_priv(dev_info);
	u8 status;
	u16 data;
	int ret, i = 0;

	do {
		ret = adt7410_read_byte(chip, ADT7410_STATUS, &status);
		if (ret)
			return -EIO;
		i++;
		if (i == 10000)
			return -EIO;
	} while (status & ADT7410_STAT_NOT_RDY);

	ret = adt7410_read_word(chip, ADT7410_TEMPERATURE, &data);
	if (ret)
		return -EIO;

	return adt7410_convert_temperature(chip, data, buf);
}

static IIO_DEVICE_ATTR(value, S_IRUGO, adt7410_show_value, NULL, 0);

static struct attribute *adt7410_attributes[] = {
	&iio_dev_attr_available_modes.dev_attr.attr,
	&iio_dev_attr_mode.dev_attr.attr,
	&iio_dev_attr_resolution.dev_attr.attr,
	&iio_dev_attr_id.dev_attr.attr,
	&iio_dev_attr_value.dev_attr.attr,
	NULL,
};

static const struct attribute_group adt7410_attribute_group = {
	.attrs = adt7410_attributes,
};

static irqreturn_t adt7410_event_handler(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct adt7410_chip_info *chip = iio_priv(indio_dev);
	s64 timestamp = iio_get_time_ns();
	u8 status;

	if (adt7410_read_byte(chip, ADT7410_STATUS, &status))
		return IRQ_HANDLED;

	if (status & ADT7410_STAT_T_HIGH)
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(IIO_TEMP, 0,
						    IIO_EV_TYPE_THRESH,
						    IIO_EV_DIR_RISING),
			       timestamp);
	if (status & ADT7410_STAT_T_LOW)
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(IIO_TEMP, 0,
						    IIO_EV_TYPE_THRESH,
						    IIO_EV_DIR_FALLING),
			       timestamp);
	if (status & ADT7410_STAT_T_CRIT)
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(IIO_TEMP, 0,
						    IIO_EV_TYPE_THRESH,
						    IIO_EV_DIR_RISING),
			       timestamp);

	return IRQ_HANDLED;
}

static ssize_t adt7410_show_event_mode(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7410_chip_info *chip = iio_priv(dev_info);
	int ret;

	ret = adt7410_read_byte(chip, ADT7410_CONFIG, &chip->config);
	if (ret)
		return -EIO;

	if (chip->config & ADT7410_EVENT_MODE)
		return sprintf(buf, "interrupt\n");
	else
		return sprintf(buf, "comparator\n");
}

static ssize_t adt7410_set_event_mode(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7410_chip_info *chip = iio_priv(dev_info);
	u16 config;
	int ret;

	ret = adt7410_read_byte(chip, ADT7410_CONFIG, &chip->config);
	if (ret)
		return -EIO;

	config = chip->config &= ~ADT7410_EVENT_MODE;
	if (strcmp(buf, "comparator") != 0)
		config |= ADT7410_EVENT_MODE;

	ret = adt7410_write_byte(chip, ADT7410_CONFIG, config);
	if (ret)
		return -EIO;

	chip->config = config;

	return ret;
}

static ssize_t adt7410_show_available_event_modes(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return sprintf(buf, "comparator\ninterrupt\n");
}

static ssize_t adt7410_show_fault_queue(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7410_chip_info *chip = iio_priv(dev_info);
	int ret;

	ret = adt7410_read_byte(chip, ADT7410_CONFIG, &chip->config);
	if (ret)
		return -EIO;

	return sprintf(buf, "%d\n", chip->config & ADT7410_FAULT_QUEUE_MASK);
}

static ssize_t adt7410_set_fault_queue(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7410_chip_info *chip = iio_priv(dev_info);
	unsigned long data;
	int ret;
	u8 config;

	ret = strict_strtoul(buf, 10, &data);
	if (ret || data > 3)
		return -EINVAL;

	ret = adt7410_read_byte(chip, ADT7410_CONFIG, &chip->config);
	if (ret)
		return -EIO;

	config = chip->config & ~ADT7410_FAULT_QUEUE_MASK;
	config |= data;
	ret = adt7410_write_byte(chip, ADT7410_CONFIG, config);
	if (ret)
		return -EIO;

	chip->config = config;

	return ret;
}

static inline ssize_t adt7410_show_t_bound(struct device *dev,
		struct device_attribute *attr,
		u8 bound_reg,
		char *buf)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7410_chip_info *chip = iio_priv(dev_info);
	u16 data;
	int ret;

	ret = adt7410_read_word(chip, bound_reg, &data);
	if (ret)
		return -EIO;

	return adt7410_convert_temperature(chip, data, buf);
}

static inline ssize_t adt7410_set_t_bound(struct device *dev,
		struct device_attribute *attr,
		u8 bound_reg,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7410_chip_info *chip = iio_priv(dev_info);
	long tmp1, tmp2;
	u16 data;
	char *pos;
	int ret;

	pos = strchr(buf, '.');

	ret = strict_strtol(buf, 10, &tmp1);

	if (ret || tmp1 > 127 || tmp1 < -128)
		return -EINVAL;

	if (pos) {
		len = strlen(pos);

		if (chip->config & ADT7410_RESOLUTION) {
			if (len > ADT7410_T16_VALUE_FLOAT_OFFSET)
				len = ADT7410_T16_VALUE_FLOAT_OFFSET;
			pos[len] = 0;
			ret = strict_strtol(pos, 10, &tmp2);

			if (!ret)
				tmp2 = (tmp2 / 78125) * 78125;
		} else {
			if (len > ADT7410_T13_VALUE_FLOAT_OFFSET)
				len = ADT7410_T13_VALUE_FLOAT_OFFSET;
			pos[len] = 0;
			ret = strict_strtol(pos, 10, &tmp2);

			if (!ret)
				tmp2 = (tmp2 / 625) * 625;
		}
	}

	if (tmp1 < 0)
		data = (u16)(-tmp1);
	else
		data = (u16)tmp1;

	if (chip->config & ADT7410_RESOLUTION) {
		data = (data << ADT7410_T16_VALUE_FLOAT_OFFSET) |
			(tmp2 & ADT7410_T16_VALUE_FLOAT_MASK);

		if (tmp1 < 0)
			/* convert positive value to supplyment */
			data = (u16)((ADT7410_T16_VALUE_SIGN << 1) - (u32)data);
	} else {
		data = (data << ADT7410_T13_VALUE_FLOAT_OFFSET) |
			(tmp2 & ADT7410_T13_VALUE_FLOAT_MASK);

		if (tmp1 < 0)
			/* convert positive value to supplyment */
			data = (ADT7410_T13_VALUE_SIGN << 1) - data;
		data <<= ADT7410_T13_VALUE_OFFSET;
	}

	ret = adt7410_write_word(chip, bound_reg, data);
	if (ret)
		return -EIO;

	return ret;
}

static ssize_t adt7410_show_t_alarm_high(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return adt7410_show_t_bound(dev, attr,
			ADT7410_T_ALARM_HIGH, buf);
}

static inline ssize_t adt7410_set_t_alarm_high(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	return adt7410_set_t_bound(dev, attr,
			ADT7410_T_ALARM_HIGH, buf, len);
}

static ssize_t adt7410_show_t_alarm_low(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return adt7410_show_t_bound(dev, attr,
			ADT7410_T_ALARM_LOW, buf);
}

static inline ssize_t adt7410_set_t_alarm_low(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	return adt7410_set_t_bound(dev, attr,
			ADT7410_T_ALARM_LOW, buf, len);
}

static ssize_t adt7410_show_t_crit(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return adt7410_show_t_bound(dev, attr,
			ADT7410_T_CRIT, buf);
}

static inline ssize_t adt7410_set_t_crit(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	return adt7410_set_t_bound(dev, attr,
			ADT7410_T_CRIT, buf, len);
}

static ssize_t adt7410_show_t_hyst(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7410_chip_info *chip = iio_priv(dev_info);
	int ret;
	u8 t_hyst;

	ret = adt7410_read_byte(chip, ADT7410_T_HYST, &t_hyst);
	if (ret)
		return -EIO;

	return sprintf(buf, "%d\n", t_hyst & ADT7410_T_HYST_MASK);
}

static inline ssize_t adt7410_set_t_hyst(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7410_chip_info *chip = iio_priv(dev_info);
	int ret;
	unsigned long data;
	u8 t_hyst;

	ret = strict_strtol(buf, 10, &data);

	if (ret || data > ADT7410_T_HYST_MASK)
		return -EINVAL;

	t_hyst = (u8)data;

	ret = adt7410_write_byte(chip, ADT7410_T_HYST, t_hyst);
	if (ret)
		return -EIO;

	return ret;
}

static IIO_DEVICE_ATTR(event_mode,
		       S_IRUGO | S_IWUSR,
		       adt7410_show_event_mode, adt7410_set_event_mode, 0);
static IIO_DEVICE_ATTR(available_event_modes,
		       S_IRUGO,
		       adt7410_show_available_event_modes, NULL, 0);
static IIO_DEVICE_ATTR(fault_queue,
		       S_IRUGO | S_IWUSR,
		       adt7410_show_fault_queue, adt7410_set_fault_queue, 0);
static IIO_DEVICE_ATTR(t_alarm_high,
		       S_IRUGO | S_IWUSR,
		       adt7410_show_t_alarm_high, adt7410_set_t_alarm_high, 0);
static IIO_DEVICE_ATTR(t_alarm_low,
		       S_IRUGO | S_IWUSR,
		       adt7410_show_t_alarm_low, adt7410_set_t_alarm_low, 0);
static IIO_DEVICE_ATTR(t_crit,
		       S_IRUGO | S_IWUSR,
		       adt7410_show_t_crit, adt7410_set_t_crit, 0);
static IIO_DEVICE_ATTR(t_hyst,
		       S_IRUGO | S_IWUSR,
		       adt7410_show_t_hyst, adt7410_set_t_hyst, 0);

static struct attribute *adt7410_event_int_attributes[] = {
	&iio_dev_attr_event_mode.dev_attr.attr,
	&iio_dev_attr_available_event_modes.dev_attr.attr,
	&iio_dev_attr_fault_queue.dev_attr.attr,
	&iio_dev_attr_t_alarm_high.dev_attr.attr,
	&iio_dev_attr_t_alarm_low.dev_attr.attr,
	&iio_dev_attr_t_crit.dev_attr.attr,
	&iio_dev_attr_t_hyst.dev_attr.attr,
	NULL,
};

static struct attribute_group adt7410_event_attribute_group = {
	.attrs = adt7410_event_int_attributes,
	.name = "events",
};

static const struct iio_info adt7410_info = {
	.attrs = &adt7410_attribute_group,
	.event_attrs = &adt7410_event_attribute_group,
	.driver_module = THIS_MODULE,
};

/*
 * device probe and remove
 */

static int adt7410_probe(struct device *dev, int irq,
	const char *name, const struct adt7410_ops *ops)
{
	unsigned long *adt7410_platform_data = dev->platform_data;
	unsigned long local_pdata[] = {0, 0};
	struct adt7410_chip_info *chip;
	struct iio_dev *indio_dev;
	int ret = 0;

	indio_dev = iio_device_alloc(sizeof(*chip));
	if (indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}
	chip = iio_priv(indio_dev);
	/* this is only used for device removal purposes */
	dev_set_drvdata(dev, indio_dev);

	chip->dev = dev;
	chip->ops = ops;

	indio_dev->name = name;
	indio_dev->dev.parent = dev;
	indio_dev->info = &adt7410_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	if (!adt7410_platform_data)
		adt7410_platform_data = local_pdata;

	/* CT critcal temperature event. line 0 */
	if (irq) {
		ret = request_threaded_irq(irq,
					   NULL,
					   &adt7410_event_handler,
					   IRQF_TRIGGER_LOW | IRQF_ONESHOT,
					   name,
					   indio_dev);
		if (ret)
			goto error_free_dev;
	}

	/* INT bound temperature alarm event. line 1 */
	if (adt7410_platform_data[0]) {
		ret = request_threaded_irq(adt7410_platform_data[0],
					   NULL,
					   &adt7410_event_handler,
					   adt7410_platform_data[1] |
					   IRQF_ONESHOT,
					   name,
					   indio_dev);
		if (ret)
			goto error_unreg_ct_irq;
	}

	ret = adt7410_read_byte(chip, ADT7410_CONFIG, &chip->config);
	if (ret) {
		ret = -EIO;
		goto error_unreg_int_irq;
	}

	chip->config |= ADT7410_RESOLUTION;

	if (irq && adt7410_platform_data[0]) {

		/* set irq polarity low level */
		chip->config &= ~ADT7410_CT_POLARITY;

		if (adt7410_platform_data[1] & IRQF_TRIGGER_HIGH)
			chip->config |= ADT7410_INT_POLARITY;
		else
			chip->config &= ~ADT7410_INT_POLARITY;
	}

	ret = adt7410_write_byte(chip, ADT7410_CONFIG, chip->config);
	if (ret) {
		ret = -EIO;
		goto error_unreg_int_irq;
	}
	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_unreg_int_irq;

	dev_info(dev, "%s temperature sensor registered.\n",
			 name);

	return 0;

error_unreg_int_irq:
	free_irq(adt7410_platform_data[0], indio_dev);
error_unreg_ct_irq:
	free_irq(irq, indio_dev);
error_free_dev:
	iio_device_free(indio_dev);
error_ret:
	return ret;
}

static int adt7410_remove(struct device *dev, int irq)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	unsigned long *adt7410_platform_data = dev->platform_data;

	iio_device_unregister(indio_dev);
	if (adt7410_platform_data[0])
		free_irq(adt7410_platform_data[0], indio_dev);
	if (irq)
		free_irq(irq, indio_dev);
	iio_device_free(indio_dev);

	return 0;
}

#if IS_ENABLED(CONFIG_I2C)

static int adt7410_i2c_read_word(struct adt7410_chip_info *chip, u8 reg,
	u16 *data)
{
	struct i2c_client *client = to_i2c_client(chip->dev);
	int ret = 0;

	ret = i2c_smbus_read_word_data(client, reg);
	if (ret < 0) {
		dev_err(&client->dev, "I2C read error\n");
		return ret;
	}

	*data = swab16((u16)ret);

	return 0;
}

static int adt7410_i2c_write_word(struct adt7410_chip_info *chip, u8 reg,
	u16 data)
{
	struct i2c_client *client = to_i2c_client(chip->dev);
	int ret = 0;

	ret = i2c_smbus_write_word_data(client, reg, swab16(data));
	if (ret < 0)
		dev_err(&client->dev, "I2C write error\n");

	return ret;
}

static int adt7410_i2c_read_byte(struct adt7410_chip_info *chip, u8 reg,
	u8 *data)
{
	struct i2c_client *client = to_i2c_client(chip->dev);
	int ret = 0;

	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0) {
		dev_err(&client->dev, "I2C read error\n");
		return ret;
	}

	*data = (u8)ret;

	return 0;
}

static int adt7410_i2c_write_byte(struct adt7410_chip_info *chip, u8 reg,
	u8 data)
{
	struct i2c_client *client = to_i2c_client(chip->dev);
	int ret = 0;

	ret = i2c_smbus_write_byte_data(client, reg, data);
	if (ret < 0)
		dev_err(&client->dev, "I2C write error\n");

	return ret;
}

static const struct adt7410_ops adt7410_i2c_ops = {
	.read_word = adt7410_i2c_read_word,
	.write_word = adt7410_i2c_write_word,
	.read_byte = adt7410_i2c_read_byte,
	.write_byte = adt7410_i2c_write_byte,
};

static int adt7410_i2c_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	return adt7410_probe(&client->dev, client->irq, id->name,
		&adt7410_i2c_ops);
}

static int adt7410_i2c_remove(struct i2c_client *client)
{
	return adt7410_remove(&client->dev, client->irq);
}

static const struct i2c_device_id adt7410_id[] = {
	{ "adt7410", 0 },
	{}
};

MODULE_DEVICE_TABLE(i2c, adt7410_id);

static struct i2c_driver adt7410_driver = {
	.driver = {
		.name = "adt7410",
	},
	.probe = adt7410_i2c_probe,
	.remove = __devexit_p(adt7410_i2c_remove),
	.id_table = adt7410_id,
};

static int __init adt7410_i2c_init(void)
{
	return i2c_add_driver(&adt7410_driver);
}

static void __exit adt7410_i2c_exit(void)
{
	i2c_del_driver(&adt7410_driver);
}

#else

static int  __init adt7410_i2c_init(void) { return 0; };
static void __exit adt7410_i2c_exit(void) {};

#endif

#if IS_ENABLED(CONFIG_SPI_MASTER)

static const u8 adt7371_reg_table[] = {
	[ADT7410_TEMPERATURE]   = ADT7310_TEMPERATURE,
	[ADT7410_STATUS]	= ADT7310_STATUS,
	[ADT7410_CONFIG]	= ADT7310_CONFIG,
	[ADT7410_T_ALARM_HIGH]	= ADT7310_T_ALARM_HIGH,
	[ADT7410_T_ALARM_LOW]	= ADT7310_T_ALARM_LOW,
	[ADT7410_T_CRIT]	= ADT7310_T_CRIT,
	[ADT7410_T_HYST]	= ADT7310_T_HYST,
	[ADT7410_ID]		= ADT7310_ID,
};

#define AD7310_COMMAND(reg) (adt7371_reg_table[(reg)] << ADT7310_CMD_REG_OFFSET)

static int adt7310_spi_read_word(struct adt7410_chip_info *chip,
	u8 reg, u16 *data)
{
	struct spi_device *spi = to_spi_device(chip->dev);
	u8 command = AD7310_COMMAND(reg);
	int ret = 0;

	command |= ADT7310_CMD_READ;
	ret = spi_write(spi, &command, sizeof(command));
	if (ret < 0) {
		dev_err(&spi->dev, "SPI write command error\n");
		return ret;
	}

	ret = spi_read(spi, (u8 *)data, sizeof(*data));
	if (ret < 0) {
		dev_err(&spi->dev, "SPI read word error\n");
		return ret;
	}

	*data = be16_to_cpu(*data);

	return 0;
}

static int adt7310_spi_write_word(struct adt7410_chip_info *chip, u8 reg,
	u16 data)
{
	struct spi_device *spi = to_spi_device(chip->dev);
	u8 buf[3];
	int ret = 0;

	buf[0] = AD7310_COMMAND(reg);
	buf[1] = (u8)(data >> 8);
	buf[2] = (u8)(data & 0xFF);

	ret = spi_write(spi, buf, 3);
	if (ret < 0) {
		dev_err(&spi->dev, "SPI write word error\n");
		return ret;
	}

	return ret;
}

static int adt7310_spi_read_byte(struct adt7410_chip_info *chip, u8 reg,
	u8 *data)
{
	struct spi_device *spi = to_spi_device(chip->dev);
	u8 command = AD7310_COMMAND(reg);
	int ret = 0;

	command |= ADT7310_CMD_READ;
	ret = spi_write(spi, &command, sizeof(command));
	if (ret < 0) {
		dev_err(&spi->dev, "SPI write command error\n");
		return ret;
	}

	ret = spi_read(spi, data, sizeof(*data));
	if (ret < 0) {
		dev_err(&spi->dev, "SPI read byte error\n");
		return ret;
	}

	return 0;
}

static int adt7310_spi_write_byte(struct adt7410_chip_info *chip, u8 reg,
	u8 data)
{
	struct spi_device *spi = to_spi_device(chip->dev);
	u8 buf[2];
	int ret = 0;

	buf[0] = AD7310_COMMAND(reg);
	buf[1] = data;

	ret = spi_write(spi, buf, 2);
	if (ret < 0) {
		dev_err(&spi->dev, "SPI write byte error\n");
		return ret;
	}

	return ret;
}

static const struct adt7410_ops adt7310_spi_ops = {
	.read_word = adt7310_spi_read_word,
	.write_word = adt7310_spi_write_word,
	.read_byte = adt7310_spi_read_byte,
	.write_byte = adt7310_spi_write_byte,
};

static int adt7310_spi_probe(struct spi_device *spi)
{
	return adt7410_probe(&spi->dev, spi->irq,
		spi_get_device_id(spi)->name, &adt7310_spi_ops);
}

static int adt7310_spi_remove(struct spi_device *spi)
{
	return adt7410_remove(&spi->dev, spi->irq);
}

static const struct spi_device_id adt7310_id[] = {
	{ "adt7310", 0 },
	{}
};
MODULE_DEVICE_TABLE(spi, adt7310_id);

static struct spi_driver adt7310_driver = {
	.driver = {
		.name = "adt7310",
		.owner = THIS_MODULE,
	},
	.probe = adt7310_spi_probe,
	.remove = __devexit_p(adt7310_spi_remove),
	.id_table = adt7310_id,
};

static int __init adt7310_spi_init(void)
{
	return spi_register_driver(&adt7310_driver);
}

static void adt7310_spi_exit(void)
{
	spi_unregister_driver(&adt7310_driver);
}

#else

static int __init adt7310_spi_init(void) { return 0; };
static void adt7310_spi_exit(void) {};

#endif

static int __init adt7410_init(void)
{
	int ret;

	ret = adt7310_spi_init();
	if (ret)
		return ret;

	ret = adt7410_i2c_init();
	if (ret)
		adt7310_spi_exit();

	return ret;
}
module_init(adt7410_init);

static void __exit adt7410_exit(void)
{
	adt7410_i2c_exit();
	adt7310_spi_exit();
}
module_exit(adt7410_exit);

MODULE_AUTHOR("Sonic Zhang <sonic.zhang@analog.com>");
MODULE_DESCRIPTION("Analog Devices ADT7310/ADT7410 digital temperature sensor driver");
MODULE_LICENSE("GPL v2");
