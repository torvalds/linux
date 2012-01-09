/*
 * ADT7310 digital temperature sensor driver supporting ADT7310
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
#include <linux/spi/spi.h>
#include <linux/module.h>

#include "../iio.h"
#include "../sysfs.h"

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
 * ADT7310 status
 */
#define ADT7310_STAT_T_LOW		0x10
#define ADT7310_STAT_T_HIGH		0x20
#define ADT7310_STAT_T_CRIT		0x40
#define ADT7310_STAT_NOT_RDY		0x80

/*
 * ADT7310 config
 */
#define ADT7310_FAULT_QUEUE_MASK	0x3
#define ADT7310_CT_POLARITY		0x4
#define ADT7310_INT_POLARITY		0x8
#define ADT7310_EVENT_MODE		0x10
#define ADT7310_MODE_MASK		0x60
#define ADT7310_ONESHOT			0x20
#define ADT7310_SPS			0x40
#define ADT7310_PD			0x60
#define ADT7310_RESOLUTION		0x80

/*
 * ADT7310 masks
 */
#define ADT7310_T16_VALUE_SIGN			0x8000
#define ADT7310_T16_VALUE_FLOAT_OFFSET		7
#define ADT7310_T16_VALUE_FLOAT_MASK		0x7F
#define ADT7310_T13_VALUE_SIGN			0x1000
#define ADT7310_T13_VALUE_OFFSET		3
#define ADT7310_T13_VALUE_FLOAT_OFFSET		4
#define ADT7310_T13_VALUE_FLOAT_MASK		0xF
#define ADT7310_T_HYST_MASK			0xF
#define ADT7310_DEVICE_ID_MASK			0x7
#define ADT7310_MANUFACTORY_ID_MASK		0xF8
#define ADT7310_MANUFACTORY_ID_OFFSET		3


#define ADT7310_CMD_REG_MASK			0x28
#define ADT7310_CMD_REG_OFFSET			3
#define ADT7310_CMD_READ			0x40
#define ADT7310_CMD_CON_READ			0x4

#define ADT7310_IRQS				2

/*
 * struct adt7310_chip_info - chip specifc information
 */

struct adt7310_chip_info {
	struct spi_device *spi_dev;
	u8  config;
};

/*
 * adt7310 register access by SPI
 */

static int adt7310_spi_read_word(struct adt7310_chip_info *chip, u8 reg, u16 *data)
{
	struct spi_device *spi_dev = chip->spi_dev;
	u8 command = (reg << ADT7310_CMD_REG_OFFSET) & ADT7310_CMD_REG_MASK;
	int ret = 0;

	command |= ADT7310_CMD_READ;
	ret = spi_write(spi_dev, &command, sizeof(command));
	if (ret < 0) {
		dev_err(&spi_dev->dev, "SPI write command error\n");
		return ret;
	}

	ret = spi_read(spi_dev, (u8 *)data, sizeof(*data));
	if (ret < 0) {
		dev_err(&spi_dev->dev, "SPI read word error\n");
		return ret;
	}

	*data = be16_to_cpu(*data);

	return 0;
}

static int adt7310_spi_write_word(struct adt7310_chip_info *chip, u8 reg, u16 data)
{
	struct spi_device *spi_dev = chip->spi_dev;
	u8 buf[3];
	int ret = 0;

	buf[0] = (reg << ADT7310_CMD_REG_OFFSET) & ADT7310_CMD_REG_MASK;
	buf[1] = (u8)(data >> 8);
	buf[2] = (u8)(data & 0xFF);

	ret = spi_write(spi_dev, buf, 3);
	if (ret < 0) {
		dev_err(&spi_dev->dev, "SPI write word error\n");
		return ret;
	}

	return ret;
}

static int adt7310_spi_read_byte(struct adt7310_chip_info *chip, u8 reg, u8 *data)
{
	struct spi_device *spi_dev = chip->spi_dev;
	u8 command = (reg << ADT7310_CMD_REG_OFFSET) & ADT7310_CMD_REG_MASK;
	int ret = 0;

	command |= ADT7310_CMD_READ;
	ret = spi_write(spi_dev, &command, sizeof(command));
	if (ret < 0) {
		dev_err(&spi_dev->dev, "SPI write command error\n");
		return ret;
	}

	ret = spi_read(spi_dev, data, sizeof(*data));
	if (ret < 0) {
		dev_err(&spi_dev->dev, "SPI read byte error\n");
		return ret;
	}

	return 0;
}

static int adt7310_spi_write_byte(struct adt7310_chip_info *chip, u8 reg, u8 data)
{
	struct spi_device *spi_dev = chip->spi_dev;
	u8 buf[2];
	int ret = 0;

	buf[0] = (reg << ADT7310_CMD_REG_OFFSET) & ADT7310_CMD_REG_MASK;
	buf[1] = data;

	ret = spi_write(spi_dev, buf, 2);
	if (ret < 0) {
		dev_err(&spi_dev->dev, "SPI write byte error\n");
		return ret;
	}

	return ret;
}

static ssize_t adt7310_show_mode(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7310_chip_info *chip = iio_priv(dev_info);
	u8 config;

	config = chip->config & ADT7310_MODE_MASK;

	switch (config) {
	case ADT7310_PD:
		return sprintf(buf, "power-down\n");
	case ADT7310_ONESHOT:
		return sprintf(buf, "one-shot\n");
	case ADT7310_SPS:
		return sprintf(buf, "sps\n");
	default:
		return sprintf(buf, "full\n");
	}
}

static ssize_t adt7310_store_mode(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7310_chip_info *chip = iio_priv(dev_info);
	u16 config;
	int ret;

	ret = adt7310_spi_read_byte(chip, ADT7310_CONFIG, &chip->config);
	if (ret)
		return -EIO;

	config = chip->config & (~ADT7310_MODE_MASK);
	if (strcmp(buf, "power-down"))
		config |= ADT7310_PD;
	else if (strcmp(buf, "one-shot"))
		config |= ADT7310_ONESHOT;
	else if (strcmp(buf, "sps"))
		config |= ADT7310_SPS;

	ret = adt7310_spi_write_byte(chip, ADT7310_CONFIG, config);
	if (ret)
		return -EIO;

	chip->config = config;

	return len;
}

static IIO_DEVICE_ATTR(mode, S_IRUGO | S_IWUSR,
		adt7310_show_mode,
		adt7310_store_mode,
		0);

static ssize_t adt7310_show_available_modes(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return sprintf(buf, "full\none-shot\nsps\npower-down\n");
}

static IIO_DEVICE_ATTR(available_modes, S_IRUGO, adt7310_show_available_modes, NULL, 0);

static ssize_t adt7310_show_resolution(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7310_chip_info *chip = iio_priv(dev_info);
	int ret;
	int bits;

	ret = adt7310_spi_read_byte(chip, ADT7310_CONFIG, &chip->config);
	if (ret)
		return -EIO;

	if (chip->config & ADT7310_RESOLUTION)
		bits = 16;
	else
		bits = 13;

	return sprintf(buf, "%d bits\n", bits);
}

static ssize_t adt7310_store_resolution(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7310_chip_info *chip = iio_priv(dev_info);
	unsigned long data;
	u16 config;
	int ret;

	ret = strict_strtoul(buf, 10, &data);
	if (ret)
		return -EINVAL;

	ret = adt7310_spi_read_byte(chip, ADT7310_CONFIG, &chip->config);
	if (ret)
		return -EIO;

	config = chip->config & (~ADT7310_RESOLUTION);
	if (data)
		config |= ADT7310_RESOLUTION;

	ret = adt7310_spi_write_byte(chip, ADT7310_CONFIG, config);
	if (ret)
		return -EIO;

	chip->config = config;

	return len;
}

static IIO_DEVICE_ATTR(resolution, S_IRUGO | S_IWUSR,
		adt7310_show_resolution,
		adt7310_store_resolution,
		0);

static ssize_t adt7310_show_id(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7310_chip_info *chip = iio_priv(dev_info);
	u8 id;
	int ret;

	ret = adt7310_spi_read_byte(chip, ADT7310_ID, &id);
	if (ret)
		return -EIO;

	return sprintf(buf, "device id: 0x%x\nmanufactory id: 0x%x\n",
			id & ADT7310_DEVICE_ID_MASK,
			(id & ADT7310_MANUFACTORY_ID_MASK) >> ADT7310_MANUFACTORY_ID_OFFSET);
}

static IIO_DEVICE_ATTR(id, S_IRUGO | S_IWUSR,
		adt7310_show_id,
		NULL,
		0);

static ssize_t adt7310_convert_temperature(struct adt7310_chip_info *chip,
		u16 data, char *buf)
{
	char sign = ' ';

	if (chip->config & ADT7310_RESOLUTION) {
		if (data & ADT7310_T16_VALUE_SIGN) {
			/* convert supplement to positive value */
			data = (u16)((ADT7310_T16_VALUE_SIGN << 1) - (u32)data);
			sign = '-';
		}
		return sprintf(buf, "%c%d.%.7d\n", sign,
				(data >> ADT7310_T16_VALUE_FLOAT_OFFSET),
				(data & ADT7310_T16_VALUE_FLOAT_MASK) * 78125);
	} else {
		if (data & ADT7310_T13_VALUE_SIGN) {
			/* convert supplement to positive value */
			data >>= ADT7310_T13_VALUE_OFFSET;
			data = (ADT7310_T13_VALUE_SIGN << 1) - data;
			sign = '-';
		}
		return sprintf(buf, "%c%d.%.4d\n", sign,
				(data >> ADT7310_T13_VALUE_FLOAT_OFFSET),
				(data & ADT7310_T13_VALUE_FLOAT_MASK) * 625);
	}
}

static ssize_t adt7310_show_value(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7310_chip_info *chip = iio_priv(dev_info);
	u8 status;
	u16 data;
	int ret, i = 0;

	do {
		ret = adt7310_spi_read_byte(chip, ADT7310_STATUS, &status);
		if (ret)
			return -EIO;
		i++;
		if (i == 10000)
			return -EIO;
	} while (status & ADT7310_STAT_NOT_RDY);

	ret = adt7310_spi_read_word(chip, ADT7310_TEMPERATURE, &data);
	if (ret)
		return -EIO;

	return adt7310_convert_temperature(chip, data, buf);
}

static IIO_DEVICE_ATTR(value, S_IRUGO, adt7310_show_value, NULL, 0);

static struct attribute *adt7310_attributes[] = {
	&iio_dev_attr_available_modes.dev_attr.attr,
	&iio_dev_attr_mode.dev_attr.attr,
	&iio_dev_attr_resolution.dev_attr.attr,
	&iio_dev_attr_id.dev_attr.attr,
	&iio_dev_attr_value.dev_attr.attr,
	NULL,
};

static const struct attribute_group adt7310_attribute_group = {
	.attrs = adt7310_attributes,
};

static irqreturn_t adt7310_event_handler(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct adt7310_chip_info *chip = iio_priv(indio_dev);
	s64 timestamp = iio_get_time_ns();
	u8 status;
	int ret;

	ret = adt7310_spi_read_byte(chip, ADT7310_STATUS, &status);
	if (ret)
		return ret;

	if (status & ADT7310_STAT_T_HIGH)
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(IIO_TEMP, 0,
						    IIO_EV_TYPE_THRESH,
						    IIO_EV_DIR_RISING),
			       timestamp);
	if (status & ADT7310_STAT_T_LOW)
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(IIO_TEMP, 0,
						    IIO_EV_TYPE_THRESH,
						    IIO_EV_DIR_FALLING),
			       timestamp);
	if (status & ADT7310_STAT_T_CRIT)
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(IIO_TEMP, 0,
						    IIO_EV_TYPE_THRESH,
						    IIO_EV_DIR_RISING),
			timestamp);
	return IRQ_HANDLED;
}

static ssize_t adt7310_show_event_mode(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7310_chip_info *chip = iio_priv(dev_info);
	int ret;

	ret = adt7310_spi_read_byte(chip, ADT7310_CONFIG, &chip->config);
	if (ret)
		return -EIO;

	if (chip->config & ADT7310_EVENT_MODE)
		return sprintf(buf, "interrupt\n");
	else
		return sprintf(buf, "comparator\n");
}

static ssize_t adt7310_set_event_mode(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7310_chip_info *chip = iio_priv(dev_info);
	u16 config;
	int ret;

	ret = adt7310_spi_read_byte(chip, ADT7310_CONFIG, &chip->config);
	if (ret)
		return -EIO;

	config = chip->config &= ~ADT7310_EVENT_MODE;
	if (strcmp(buf, "comparator") != 0)
		config |= ADT7310_EVENT_MODE;

	ret = adt7310_spi_write_byte(chip, ADT7310_CONFIG, config);
	if (ret)
		return -EIO;

	chip->config = config;

	return len;
}

static ssize_t adt7310_show_available_event_modes(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return sprintf(buf, "comparator\ninterrupt\n");
}

static ssize_t adt7310_show_fault_queue(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7310_chip_info *chip = iio_priv(dev_info);
	int ret;

	ret = adt7310_spi_read_byte(chip, ADT7310_CONFIG, &chip->config);
	if (ret)
		return -EIO;

	return sprintf(buf, "%d\n", chip->config & ADT7310_FAULT_QUEUE_MASK);
}

static ssize_t adt7310_set_fault_queue(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7310_chip_info *chip = iio_priv(dev_info);
	unsigned long data;
	int ret;
	u8 config;

	ret = strict_strtoul(buf, 10, &data);
	if (ret || data > 3)
		return -EINVAL;

	ret = adt7310_spi_read_byte(chip, ADT7310_CONFIG, &chip->config);
	if (ret)
		return -EIO;

	config = chip->config & ~ADT7310_FAULT_QUEUE_MASK;
	config |= data;
	ret = adt7310_spi_write_byte(chip, ADT7310_CONFIG, config);
	if (ret)
		return -EIO;

	chip->config = config;

	return len;
}

static inline ssize_t adt7310_show_t_bound(struct device *dev,
		struct device_attribute *attr,
		u8 bound_reg,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7310_chip_info *chip = iio_priv(dev_info);
	u16 data;
	int ret;

	ret = adt7310_spi_read_word(chip, bound_reg, &data);
	if (ret)
		return -EIO;

	return adt7310_convert_temperature(chip, data, buf);
}

static inline ssize_t adt7310_set_t_bound(struct device *dev,
		struct device_attribute *attr,
		u8 bound_reg,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7310_chip_info *chip = iio_priv(dev_info);
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

		if (chip->config & ADT7310_RESOLUTION) {
			if (len > ADT7310_T16_VALUE_FLOAT_OFFSET)
				len = ADT7310_T16_VALUE_FLOAT_OFFSET;
			pos[len] = 0;
			ret = strict_strtol(pos, 10, &tmp2);

			if (!ret)
				tmp2 = (tmp2 / 78125) * 78125;
		} else {
			if (len > ADT7310_T13_VALUE_FLOAT_OFFSET)
				len = ADT7310_T13_VALUE_FLOAT_OFFSET;
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

	if (chip->config & ADT7310_RESOLUTION) {
		data = (data << ADT7310_T16_VALUE_FLOAT_OFFSET) |
			(tmp2 & ADT7310_T16_VALUE_FLOAT_MASK);

		if (tmp1 < 0)
			/* convert positive value to supplyment */
			data = (u16)((ADT7310_T16_VALUE_SIGN << 1) - (u32)data);
	} else {
		data = (data << ADT7310_T13_VALUE_FLOAT_OFFSET) |
			(tmp2 & ADT7310_T13_VALUE_FLOAT_MASK);

		if (tmp1 < 0)
			/* convert positive value to supplyment */
			data = (ADT7310_T13_VALUE_SIGN << 1) - data;
		data <<= ADT7310_T13_VALUE_OFFSET;
	}

	ret = adt7310_spi_write_word(chip, bound_reg, data);
	if (ret)
		return -EIO;

	return len;
}

static ssize_t adt7310_show_t_alarm_high(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return adt7310_show_t_bound(dev, attr,
			ADT7310_T_ALARM_HIGH, buf);
}

static inline ssize_t adt7310_set_t_alarm_high(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	return adt7310_set_t_bound(dev, attr,
			ADT7310_T_ALARM_HIGH, buf, len);
}

static ssize_t adt7310_show_t_alarm_low(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return adt7310_show_t_bound(dev, attr,
			ADT7310_T_ALARM_LOW, buf);
}

static inline ssize_t adt7310_set_t_alarm_low(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	return adt7310_set_t_bound(dev, attr,
			ADT7310_T_ALARM_LOW, buf, len);
}

static ssize_t adt7310_show_t_crit(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return adt7310_show_t_bound(dev, attr,
			ADT7310_T_CRIT, buf);
}

static inline ssize_t adt7310_set_t_crit(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	return adt7310_set_t_bound(dev, attr,
			ADT7310_T_CRIT, buf, len);
}

static ssize_t adt7310_show_t_hyst(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7310_chip_info *chip = iio_priv(dev_info);
	int ret;
	u8 t_hyst;

	ret = adt7310_spi_read_byte(chip, ADT7310_T_HYST, &t_hyst);
	if (ret)
		return -EIO;

	return sprintf(buf, "%d\n", t_hyst & ADT7310_T_HYST_MASK);
}

static inline ssize_t adt7310_set_t_hyst(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7310_chip_info *chip = iio_priv(dev_info);
	int ret;
	unsigned long data;
	u8 t_hyst;

	ret = strict_strtol(buf, 10, &data);

	if (ret || data > ADT7310_T_HYST_MASK)
		return -EINVAL;

	t_hyst = (u8)data;

	ret = adt7310_spi_write_byte(chip, ADT7310_T_HYST, t_hyst);
	if (ret)
		return -EIO;

	return len;
}

static IIO_DEVICE_ATTR(event_mode,
		       S_IRUGO | S_IWUSR,
		       adt7310_show_event_mode, adt7310_set_event_mode, 0);
static IIO_DEVICE_ATTR(available_event_modes,
		       S_IRUGO | S_IWUSR,
		       adt7310_show_available_event_modes, NULL, 0);
static IIO_DEVICE_ATTR(fault_queue,
		       S_IRUGO | S_IWUSR,
		       adt7310_show_fault_queue, adt7310_set_fault_queue, 0);
static IIO_DEVICE_ATTR(t_alarm_high,
		       S_IRUGO | S_IWUSR,
		       adt7310_show_t_alarm_high, adt7310_set_t_alarm_high, 0);
static IIO_DEVICE_ATTR(t_alarm_low,
		       S_IRUGO | S_IWUSR,
		       adt7310_show_t_alarm_low, adt7310_set_t_alarm_low, 0);
static IIO_DEVICE_ATTR(t_crit,
		       S_IRUGO | S_IWUSR,
		       adt7310_show_t_crit, adt7310_set_t_crit, 0);
static IIO_DEVICE_ATTR(t_hyst,
		       S_IRUGO | S_IWUSR,
		       adt7310_show_t_hyst, adt7310_set_t_hyst, 0);

static struct attribute *adt7310_event_int_attributes[] = {
	&iio_dev_attr_event_mode.dev_attr.attr,
	&iio_dev_attr_available_event_modes.dev_attr.attr,
	&iio_dev_attr_fault_queue.dev_attr.attr,
	&iio_dev_attr_t_alarm_high.dev_attr.attr,
	&iio_dev_attr_t_alarm_low.dev_attr.attr,
	&iio_dev_attr_t_hyst.dev_attr.attr,
	NULL,
};

static struct attribute *adt7310_event_ct_attributes[] = {
	&iio_dev_attr_event_mode.dev_attr.attr,
	&iio_dev_attr_available_event_modes.dev_attr.attr,
	&iio_dev_attr_fault_queue.dev_attr.attr,
	&iio_dev_attr_t_crit.dev_attr.attr,
	&iio_dev_attr_t_hyst.dev_attr.attr,
	NULL,
};

static struct attribute_group adt7310_event_attribute_group[ADT7310_IRQS] = {
	{
		.attrs = adt7310_event_int_attributes,
		.name = "events",
	}, {
		.attrs = adt7310_event_ct_attributes,
		.name = "events",
	}
};

static const struct iio_info adt7310_info = {
	.attrs = &adt7310_attribute_group,
	.event_attrs = adt7310_event_attribute_group,
	.driver_module = THIS_MODULE,
};

/*
 * device probe and remove
 */

static int __devinit adt7310_probe(struct spi_device *spi_dev)
{
	struct adt7310_chip_info *chip;
	struct iio_dev *indio_dev;
	int ret = 0;
	unsigned long *adt7310_platform_data = spi_dev->dev.platform_data;
	unsigned long irq_flags;

	indio_dev = iio_allocate_device(sizeof(*chip));
	if (indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}
	chip = iio_priv(indio_dev);
	/* this is only used for device removal purposes */
	dev_set_drvdata(&spi_dev->dev, indio_dev);

	chip->spi_dev = spi_dev;

	indio_dev->dev.parent = &spi_dev->dev;
	indio_dev->name = spi_get_device_id(spi_dev)->name;
	indio_dev->info = &adt7310_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	/* CT critcal temperature event. line 0 */
	if (spi_dev->irq) {
		if (adt7310_platform_data[2])
			irq_flags = adt7310_platform_data[2];
		else
			irq_flags = IRQF_TRIGGER_LOW;
		ret = request_threaded_irq(spi_dev->irq,
					   NULL,
					   &adt7310_event_handler,
					   irq_flags,
					   indio_dev->name,
					   indio_dev);
		if (ret)
			goto error_free_dev;
	}

	/* INT bound temperature alarm event. line 1 */
	if (adt7310_platform_data[0]) {
		ret = request_threaded_irq(adt7310_platform_data[0],
					   NULL,
					   &adt7310_event_handler,
					   adt7310_platform_data[1],
					   indio_dev->name,
					   indio_dev);
		if (ret)
			goto error_unreg_ct_irq;
	}

	if (spi_dev->irq && adt7310_platform_data[0]) {
		ret = adt7310_spi_read_byte(chip, ADT7310_CONFIG, &chip->config);
		if (ret) {
			ret = -EIO;
			goto error_unreg_int_irq;
		}

		/* set irq polarity low level */
		chip->config &= ~ADT7310_CT_POLARITY;

		if (adt7310_platform_data[1] & IRQF_TRIGGER_HIGH)
			chip->config |= ADT7310_INT_POLARITY;
		else
			chip->config &= ~ADT7310_INT_POLARITY;

		ret = adt7310_spi_write_byte(chip, ADT7310_CONFIG, chip->config);
		if (ret) {
			ret = -EIO;
			goto error_unreg_int_irq;
		}
	}

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_unreg_int_irq;

	dev_info(&spi_dev->dev, "%s temperature sensor registered.\n",
			indio_dev->name);

	return 0;

error_unreg_int_irq:
	free_irq(adt7310_platform_data[0], indio_dev);
error_unreg_ct_irq:
	free_irq(spi_dev->irq, indio_dev);
error_free_dev:
	iio_free_device(indio_dev);
error_ret:
	return ret;
}

static int __devexit adt7310_remove(struct spi_device *spi_dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(&spi_dev->dev);
	unsigned long *adt7310_platform_data = spi_dev->dev.platform_data;

	iio_device_unregister(indio_dev);
	dev_set_drvdata(&spi_dev->dev, NULL);
	if (adt7310_platform_data[0])
		free_irq(adt7310_platform_data[0], indio_dev);
	if (spi_dev->irq)
		free_irq(spi_dev->irq, indio_dev);
	iio_free_device(indio_dev);

	return 0;
}

static const struct spi_device_id adt7310_id[] = {
	{ "adt7310", 0 },
	{}
};

MODULE_DEVICE_TABLE(spi, adt7310_id);

static struct spi_driver adt7310_driver = {
	.driver = {
		.name = "adt7310",
		.bus = &spi_bus_type,
		.owner = THIS_MODULE,
	},
	.probe = adt7310_probe,
	.remove = __devexit_p(adt7310_remove),
	.id_table = adt7310_id,
};

static __init int adt7310_init(void)
{
	return spi_register_driver(&adt7310_driver);
}

static __exit void adt7310_exit(void)
{
	spi_unregister_driver(&adt7310_driver);
}

MODULE_AUTHOR("Sonic Zhang <sonic.zhang@analog.com>");
MODULE_DESCRIPTION("Analog Devices ADT7310 digital"
			" temperature sensor driver");
MODULE_LICENSE("GPL v2");

module_init(adt7310_init);
module_exit(adt7310_exit);
