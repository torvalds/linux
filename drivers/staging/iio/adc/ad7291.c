/*
 * AD7291 digital temperature sensor driver supporting AD7291
 *
 * Copyright 2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/workqueue.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/list.h>
#include <linux/i2c.h>
#include <linux/rtc.h>

#include "../iio.h"
#include "../sysfs.h"

/*
 * AD7291 registers definition
 */
#define AD7291_COMMAND			0
#define AD7291_VOLTAGE			1
#define AD7291_T_SENSE			2
#define AD7291_T_AVERAGE		3
#define AD7291_VOLTAGE_LIMIT_BASE	4
#define AD7291_VOLTAGE_LIMIT_COUNT	8
#define AD7291_T_SENSE_HIGH		0x1c
#define AD7291_T_SENSE_LOW		0x1d
#define AD7291_T_SENSE_HYST		0x1e
#define AD7291_VOLTAGE_ALERT_STATUS	0x1f
#define AD7291_T_ALERT_STATUS		0x20

/*
 * AD7291 command
 */
#define AD7291_AUTOCYCLE		0x1
#define AD7291_RESET			0x2
#define AD7291_ALART_CLEAR		0x4
#define AD7291_ALART_POLARITY		0x8
#define AD7291_EXT_REF			0x10
#define AD7291_NOISE_DELAY		0x20
#define AD7291_T_SENSE_MASK		0x40
#define AD7291_VOLTAGE_MASK		0xff00
#define AD7291_VOLTAGE_OFFSET		0x8

/*
 * AD7291 value masks
 */
#define AD7291_CHANNEL_MASK		0xf000
#define AD7291_VALUE_MASK		0xfff
#define AD7291_T_VALUE_SIGN		0x400
#define AD7291_T_VALUE_FLOAT_OFFSET	2
#define AD7291_T_VALUE_FLOAT_MASK	0x2

/*
 * struct ad7291_chip_info - chip specifc information
 */

struct ad7291_chip_info {
	const char *name;
	struct i2c_client *client;
	struct iio_dev *indio_dev;
	struct work_struct thresh_work;
	s64 last_timestamp;
	u16 command;
	u8  channels;	/* Active voltage channels */
};

/*
 * struct ad7291_chip_info - chip specifc information
 */

struct ad7291_limit_regs {
	u16	data_high;
	u16	data_low;
	u16	hysteresis;
};

/*
 * ad7291 register access by I2C
 */
static int ad7291_i2c_read(struct ad7291_chip_info *chip, u8 reg, u16 *data)
{
	struct i2c_client *client = chip->client;
	int ret = 0;

	ret = i2c_smbus_read_word_data(client, reg);
	if (ret < 0) {
		dev_err(&client->dev, "I2C read error\n");
		return ret;
	}

	*data = swab16((u16)ret);

	return 0;
}

static int ad7291_i2c_write(struct ad7291_chip_info *chip, u8 reg, u16 data)
{
	struct i2c_client *client = chip->client;
	int ret = 0;

	ret = i2c_smbus_write_word_data(client, reg, swab16(data));
	if (ret < 0)
		dev_err(&client->dev, "I2C write error\n");

	return ret;
}

/* Returns negative errno, or else the number of words read. */
static int ad7291_i2c_read_data(struct ad7291_chip_info *chip, u8 reg, u16 *data)
{
	struct i2c_client *client = chip->client;
	u8 commands[4];
	int ret = 0;
	int i, count;

	if (reg == AD7291_T_SENSE || reg == AD7291_T_AVERAGE)
		count = 2;
	else if (reg == AD7291_VOLTAGE) {
		if (!chip->channels) {
			dev_err(&client->dev, "No voltage channel is selected.\n");
			return -EINVAL;
		}
		count = 2 + chip->channels * 2;
	} else {
		dev_err(&client->dev, "I2C wrong data register\n");
		return -EINVAL;
	}

	commands[0] = 0;
	commands[1] = (chip->command >> 8) & 0xff;
	commands[2] = chip->command & 0xff;
	commands[3] = reg;

	ret = i2c_master_send(client, commands, 4);
	if (ret < 0) {
		dev_err(&client->dev, "I2C master send error\n");
		return ret;
	}

	ret = i2c_master_recv(client, (u8 *)data, count);
	if (ret < 0) {
		dev_err(&client->dev, "I2C master receive error\n");
		return ret;
	}
	ret >>= 2;

	for (i = 0; i < ret; i++)
		data[i] = swab16(data[i]);

	return ret;
}

static ssize_t ad7291_show_mode(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7291_chip_info *chip = dev_info->dev_data;

	if (chip->command & AD7291_AUTOCYCLE)
		return sprintf(buf, "autocycle\n");
	else
		return sprintf(buf, "command\n");
}

static ssize_t ad7291_store_mode(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7291_chip_info *chip = dev_info->dev_data;
	u16 command;
	int ret;

	command = chip->command & (~AD7291_AUTOCYCLE);
	if (strcmp(buf, "autocycle"))
		command |= AD7291_AUTOCYCLE;

	ret = ad7291_i2c_write(chip, AD7291_COMMAND, command);
	if (ret)
		return -EIO;

	chip->command = command;

	return ret;
}

static IIO_DEVICE_ATTR(mode, S_IRUGO | S_IWUSR,
		ad7291_show_mode,
		ad7291_store_mode,
		0);

static ssize_t ad7291_show_available_modes(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return sprintf(buf, "command\nautocycle\n");
}

static IIO_DEVICE_ATTR(available_modes, S_IRUGO, ad7291_show_available_modes, NULL, 0);

static ssize_t ad7291_store_reset(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7291_chip_info *chip = dev_info->dev_data;
	u16 command;
	int ret;

	command = chip->command | AD7291_RESET;

	ret = ad7291_i2c_write(chip, AD7291_COMMAND, command);
	if (ret)
		return -EIO;

	return ret;
}

static IIO_DEVICE_ATTR(reset, S_IWUSR,
		NULL,
		ad7291_store_reset,
		0);

static ssize_t ad7291_show_ext_ref(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7291_chip_info *chip = dev_info->dev_data;

	return sprintf(buf, "%d\n", !!(chip->command & AD7291_EXT_REF));
}

static ssize_t ad7291_store_ext_ref(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7291_chip_info *chip = dev_info->dev_data;
	u16 command;
	int ret;

	command = chip->command & (~AD7291_EXT_REF);
	if (strcmp(buf, "1"))
		command |= AD7291_EXT_REF;

	ret = ad7291_i2c_write(chip, AD7291_COMMAND, command);
	if (ret)
		return -EIO;

	chip->command = command;

	return ret;
}

static IIO_DEVICE_ATTR(ext_ref, S_IRUGO | S_IWUSR,
		ad7291_show_ext_ref,
		ad7291_store_ext_ref,
		0);

static ssize_t ad7291_show_noise_delay(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7291_chip_info *chip = dev_info->dev_data;

	return sprintf(buf, "%d\n", !!(chip->command & AD7291_NOISE_DELAY));
}

static ssize_t ad7291_store_noise_delay(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7291_chip_info *chip = dev_info->dev_data;
	u16 command;
	int ret;

	command = chip->command & (~AD7291_NOISE_DELAY);
	if (strcmp(buf, "1"))
		command |= AD7291_NOISE_DELAY;

	ret = ad7291_i2c_write(chip, AD7291_COMMAND, command);
	if (ret)
		return -EIO;

	chip->command = command;

	return ret;
}

static IIO_DEVICE_ATTR(noise_delay, S_IRUGO | S_IWUSR,
		ad7291_show_noise_delay,
		ad7291_store_noise_delay,
		0);

static ssize_t ad7291_show_t_sense(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7291_chip_info *chip = dev_info->dev_data;
	u16 data;
	char sign = ' ';
	int ret;

	ret = ad7291_i2c_read_data(chip, AD7291_T_SENSE, &data);
	if (ret)
		return -EIO;

	if (data & AD7291_T_VALUE_SIGN) {
		/* convert supplement to positive value */
		data = (AD7291_T_VALUE_SIGN << 1) - data;
		sign = '-';
	}

	return sprintf(buf, "%c%d.%.2d\n", sign,
		(data >> AD7291_T_VALUE_FLOAT_OFFSET),
		(data & AD7291_T_VALUE_FLOAT_MASK) * 25);
}

static IIO_DEVICE_ATTR(t_sense, S_IRUGO, ad7291_show_t_sense, NULL, 0);

static ssize_t ad7291_show_t_average(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7291_chip_info *chip = dev_info->dev_data;
	u16 data;
	char sign = ' ';
	int ret;

	ret = ad7291_i2c_read_data(chip, AD7291_T_AVERAGE, &data);
	if (ret)
		return -EIO;

	if (data & AD7291_T_VALUE_SIGN) {
		/* convert supplement to positive value */
		data = (AD7291_T_VALUE_SIGN << 1) - data;
		sign = '-';
	}

	return sprintf(buf, "%c%d.%.2d\n", sign,
		(data >> AD7291_T_VALUE_FLOAT_OFFSET),
		(data & AD7291_T_VALUE_FLOAT_MASK) * 25);
}

static IIO_DEVICE_ATTR(t_average, S_IRUGO, ad7291_show_t_average, NULL, 0);

static ssize_t ad7291_show_voltage(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7291_chip_info *chip = dev_info->dev_data;
	u16 data[AD7291_VOLTAGE_LIMIT_COUNT];
	int i, size, ret;

	ret = ad7291_i2c_read_data(chip, AD7291_VOLTAGE, data);
	if (ret)
		return -EIO;

	for (i = 0; i < AD7291_VOLTAGE_LIMIT_COUNT; i++) {
		if (chip->command & (AD7291_T_SENSE_MASK << i)) {
			ret = sprintf(buf, "channel[%d]=%d\n", i,
					data[i] & AD7291_VALUE_MASK);
			if (ret < 0)
				break;
			buf += ret;
			size += ret;
		}
	}

	return size;
}

static IIO_DEVICE_ATTR(voltage, S_IRUGO, ad7291_show_voltage, NULL, 0);

static ssize_t ad7291_show_channel_mask(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7291_chip_info *chip = dev_info->dev_data;

	return sprintf(buf, "0x%x\n", (chip->command & AD7291_VOLTAGE_MASK) >>
			AD7291_VOLTAGE_OFFSET);
}

static ssize_t ad7291_store_channel_mask(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7291_chip_info *chip = dev_info->dev_data;
	u16 command;
	unsigned long data;
	int i, ret;

	ret = strict_strtoul(buf, 16, &data);
	if (ret || data > 0xff)
		return -EINVAL;

	command = chip->command & (~AD7291_VOLTAGE_MASK);
	command |= data << AD7291_VOLTAGE_OFFSET;

	ret = ad7291_i2c_write(chip, AD7291_COMMAND, command);
	if (ret)
		return -EIO;

	chip->command = command;

	for (i = 0, chip->channels = 0; i < AD7291_VOLTAGE_LIMIT_COUNT; i++) {
		if (chip->command & (AD7291_T_SENSE_MASK << i))
			chip->channels++;
	}

	return ret;
}

static IIO_DEVICE_ATTR(channel_mask, S_IRUGO | S_IWUSR,
		ad7291_show_channel_mask,
		ad7291_store_channel_mask,
		0);

static ssize_t ad7291_show_name(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7291_chip_info *chip = dev_info->dev_data;
	return sprintf(buf, "%s\n", chip->name);
}

static IIO_DEVICE_ATTR(name, S_IRUGO, ad7291_show_name, NULL, 0);

static struct attribute *ad7291_attributes[] = {
	&iio_dev_attr_available_modes.dev_attr.attr,
	&iio_dev_attr_mode.dev_attr.attr,
	&iio_dev_attr_reset.dev_attr.attr,
	&iio_dev_attr_ext_ref.dev_attr.attr,
	&iio_dev_attr_noise_delay.dev_attr.attr,
	&iio_dev_attr_t_sense.dev_attr.attr,
	&iio_dev_attr_t_average.dev_attr.attr,
	&iio_dev_attr_voltage.dev_attr.attr,
	&iio_dev_attr_channel_mask.dev_attr.attr,
	&iio_dev_attr_name.dev_attr.attr,
	NULL,
};

static const struct attribute_group ad7291_attribute_group = {
	.attrs = ad7291_attributes,
};

/*
 * temperature bound events
 */

#define IIO_EVENT_CODE_AD7291_T_SENSE_HIGH  IIO_BUFFER_EVENT_CODE(0)
#define IIO_EVENT_CODE_AD7291_T_SENSE_LOW   IIO_BUFFER_EVENT_CODE(1)
#define IIO_EVENT_CODE_AD7291_T_AVG_HIGH    IIO_BUFFER_EVENT_CODE(2)
#define IIO_EVENT_CODE_AD7291_T_AVG_LOW     IIO_BUFFER_EVENT_CODE(3)
#define IIO_EVENT_CODE_AD7291_VOLTAGE_BASE  IIO_BUFFER_EVENT_CODE(4)

static void ad7291_interrupt_bh(struct work_struct *work_s)
{
	struct ad7291_chip_info *chip =
		container_of(work_s, struct ad7291_chip_info, thresh_work);
	u16 t_status, v_status;
	u16 command;
	int i;

	if (ad7291_i2c_read(chip, AD7291_T_ALERT_STATUS, &t_status))
		return;

	if (ad7291_i2c_read(chip, AD7291_VOLTAGE_ALERT_STATUS, &v_status))
		return;

	if (!(t_status || v_status))
		return;

	command = chip->command | AD7291_ALART_CLEAR;
	ad7291_i2c_write(chip, AD7291_COMMAND, command);

	command = chip->command & ~AD7291_ALART_CLEAR;
	ad7291_i2c_write(chip, AD7291_COMMAND, command);

	enable_irq(chip->client->irq);

	for (i = 0; i < 4; i++) {
		if (t_status & (1 << i))
			iio_push_event(chip->indio_dev, 0,
				IIO_EVENT_CODE_AD7291_T_SENSE_HIGH + i,
				chip->last_timestamp);
	}

	for (i = 0; i < AD7291_VOLTAGE_LIMIT_COUNT*2; i++) {
		if (v_status & (1 << i))
			iio_push_event(chip->indio_dev, 0,
				IIO_EVENT_CODE_AD7291_VOLTAGE_BASE + i,
				chip->last_timestamp);
	}
}

static int ad7291_interrupt(struct iio_dev *dev_info,
		int index,
		s64 timestamp,
		int no_test)
{
	struct ad7291_chip_info *chip = dev_info->dev_data;

	chip->last_timestamp = timestamp;
	schedule_work(&chip->thresh_work);

	return 0;
}

IIO_EVENT_SH(ad7291, &ad7291_interrupt);

static inline ssize_t ad7291_show_t_bound(struct device *dev,
		struct device_attribute *attr,
		u8 bound_reg,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7291_chip_info *chip = dev_info->dev_data;
	u16 data;
	char sign = ' ';
	int ret;

	ret = ad7291_i2c_read(chip, bound_reg, &data);
	if (ret)
		return -EIO;

	data &= AD7291_VALUE_MASK;
	if (data & AD7291_T_VALUE_SIGN) {
		/* convert supplement to positive value */
		data = (AD7291_T_VALUE_SIGN << 1) - data;
		sign = '-';
	}

	return sprintf(buf, "%c%d.%.2d\n", sign,
			data >> AD7291_T_VALUE_FLOAT_OFFSET,
			(data & AD7291_T_VALUE_FLOAT_MASK) * 25);
}

static inline ssize_t ad7291_set_t_bound(struct device *dev,
		struct device_attribute *attr,
		u8 bound_reg,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7291_chip_info *chip = dev_info->dev_data;
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
		if (len > AD7291_T_VALUE_FLOAT_OFFSET)
			len = AD7291_T_VALUE_FLOAT_OFFSET;
		pos[len] = 0;
		ret = strict_strtol(pos, 10, &tmp2);

		if (!ret)
			tmp2 = (tmp2 / 25) * 25;
	}

	if (tmp1 < 0)
		data = (u16)(-tmp1);
	else
		data = (u16)tmp1;
	data = (data << AD7291_T_VALUE_FLOAT_OFFSET) |
		(tmp2 & AD7291_T_VALUE_FLOAT_MASK);
	if (tmp1 < 0)
		/* convert positive value to supplyment */
		data = (AD7291_T_VALUE_SIGN << 1) - data;

	ret = ad7291_i2c_write(chip, bound_reg, data);
	if (ret)
		return -EIO;

	return ret;
}

static ssize_t ad7291_show_t_sense_high(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return ad7291_show_t_bound(dev, attr,
			AD7291_T_SENSE_HIGH, buf);
}

static inline ssize_t ad7291_set_t_sense_high(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	return ad7291_set_t_bound(dev, attr,
			AD7291_T_SENSE_HIGH, buf, len);
}

static ssize_t ad7291_show_t_sense_low(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return ad7291_show_t_bound(dev, attr,
			AD7291_T_SENSE_LOW, buf);
}

static inline ssize_t ad7291_set_t_sense_low(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	return ad7291_set_t_bound(dev, attr,
			AD7291_T_SENSE_LOW, buf, len);
}

static ssize_t ad7291_show_t_sense_hyst(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return ad7291_show_t_bound(dev, attr,
			AD7291_T_SENSE_HYST, buf);
}

static inline ssize_t ad7291_set_t_sense_hyst(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	return ad7291_set_t_bound(dev, attr,
			AD7291_T_SENSE_HYST, buf, len);
}

static inline ssize_t ad7291_show_v_bound(struct device *dev,
		struct device_attribute *attr,
		u8 bound_reg,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7291_chip_info *chip = dev_info->dev_data;
	u16 data;
	int ret;

	if (bound_reg < AD7291_VOLTAGE_LIMIT_BASE ||
		bound_reg >= AD7291_VOLTAGE_LIMIT_BASE +
		AD7291_VOLTAGE_LIMIT_COUNT)
		return -EINVAL;

	ret = ad7291_i2c_read(chip, bound_reg, &data);
	if (ret)
		return -EIO;

	data &= AD7291_VALUE_MASK;

	return sprintf(buf, "%d\n", data);
}

static inline ssize_t ad7291_set_v_bound(struct device *dev,
		struct device_attribute *attr,
		u8 bound_reg,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7291_chip_info *chip = dev_info->dev_data;
	unsigned long value;
	u16 data;
	int ret;

	if (bound_reg < AD7291_VOLTAGE_LIMIT_BASE ||
		bound_reg >= AD7291_VOLTAGE_LIMIT_BASE +
		AD7291_VOLTAGE_LIMIT_COUNT)
		return -EINVAL;

	ret = strict_strtoul(buf, 10, &value);

	if (ret || value >= 4096)
		return -EINVAL;

	data = (u16)value;
	ret = ad7291_i2c_write(chip, bound_reg, data);
	if (ret)
		return -EIO;

	return ret;
}

static int ad7291_get_voltage_limit_regs(const char *channel)
{
	int index;

	if (strlen(channel) < 3 && channel[0] != 'v')
		return -EINVAL;

	index = channel[1] - '0';
	if (index >= AD7291_VOLTAGE_LIMIT_COUNT)
		return -EINVAL;

	return index;
}

static ssize_t ad7291_show_voltage_high(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	int regs;

	regs = ad7291_get_voltage_limit_regs(attr->attr.name);

	if (regs < 0)
		return regs;

	return ad7291_show_t_bound(dev, attr, regs, buf);
}

static inline ssize_t ad7291_set_voltage_high(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	int regs;

	regs = ad7291_get_voltage_limit_regs(attr->attr.name);

	if (regs < 0)
		return regs;

	return ad7291_set_t_bound(dev, attr, regs, buf, len);
}

static ssize_t ad7291_show_voltage_low(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	int regs;

	regs = ad7291_get_voltage_limit_regs(attr->attr.name);

	if (regs < 0)
		return regs;

	return ad7291_show_t_bound(dev, attr, regs+1, buf);
}

static inline ssize_t ad7291_set_voltage_low(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	int regs;

	regs = ad7291_get_voltage_limit_regs(attr->attr.name);

	if (regs < 0)
		return regs;

	return ad7291_set_t_bound(dev, attr, regs+1, buf, len);
}

static ssize_t ad7291_show_voltage_hyst(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	int regs;

	regs = ad7291_get_voltage_limit_regs(attr->attr.name);

	if (regs < 0)
		return regs;

	return ad7291_show_t_bound(dev, attr, regs+2, buf);
}

static inline ssize_t ad7291_set_voltage_hyst(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	int regs;

	regs = ad7291_get_voltage_limit_regs(attr->attr.name);

	if (regs < 0)
		return regs;

	return ad7291_set_t_bound(dev, attr, regs+2, buf, len);
}

IIO_EVENT_ATTR_SH(t_sense_high, iio_event_ad7291,
		ad7291_show_t_sense_high, ad7291_set_t_sense_high, 0);
IIO_EVENT_ATTR_SH(t_sense_low, iio_event_ad7291,
		ad7291_show_t_sense_low, ad7291_set_t_sense_low, 0);
IIO_EVENT_ATTR_SH(t_sense_hyst, iio_event_ad7291,
		ad7291_show_t_sense_hyst, ad7291_set_t_sense_hyst, 0);

IIO_EVENT_ATTR_SH(v0_high, iio_event_ad7291,
		ad7291_show_voltage_high, ad7291_set_voltage_high, 0);
IIO_EVENT_ATTR_SH(v0_low, iio_event_ad7291,
		ad7291_show_voltage_low, ad7291_set_voltage_low, 0);
IIO_EVENT_ATTR_SH(v0_hyst, iio_event_ad7291,
		ad7291_show_voltage_hyst, ad7291_set_voltage_hyst, 0);
IIO_EVENT_ATTR_SH(v1_high, iio_event_ad7291,
		ad7291_show_voltage_high, ad7291_set_voltage_high, 0);
IIO_EVENT_ATTR_SH(v1_low, iio_event_ad7291,
		ad7291_show_voltage_low, ad7291_set_voltage_low, 0);
IIO_EVENT_ATTR_SH(v1_hyst, iio_event_ad7291,
		ad7291_show_voltage_hyst, ad7291_set_voltage_hyst, 0);
IIO_EVENT_ATTR_SH(v2_high, iio_event_ad7291,
		ad7291_show_voltage_high, ad7291_set_voltage_high, 0);
IIO_EVENT_ATTR_SH(v2_low, iio_event_ad7291,
		ad7291_show_voltage_low, ad7291_set_voltage_low, 0);
IIO_EVENT_ATTR_SH(v2_hyst, iio_event_ad7291,
		ad7291_show_voltage_hyst, ad7291_set_voltage_hyst, 0);
IIO_EVENT_ATTR_SH(v3_high, iio_event_ad7291,
		ad7291_show_voltage_high, ad7291_set_voltage_high, 0);
IIO_EVENT_ATTR_SH(v3_low, iio_event_ad7291,
		ad7291_show_voltage_low, ad7291_set_voltage_low, 0);
IIO_EVENT_ATTR_SH(v3_hyst, iio_event_ad7291,
		ad7291_show_voltage_hyst, ad7291_set_voltage_hyst, 0);
IIO_EVENT_ATTR_SH(v4_high, iio_event_ad7291,
		ad7291_show_voltage_high, ad7291_set_voltage_high, 0);
IIO_EVENT_ATTR_SH(v4_low, iio_event_ad7291,
		ad7291_show_voltage_low, ad7291_set_voltage_low, 0);
IIO_EVENT_ATTR_SH(v4_hyst, iio_event_ad7291,
		ad7291_show_voltage_hyst, ad7291_set_voltage_hyst, 0);
IIO_EVENT_ATTR_SH(v5_high, iio_event_ad7291,
		ad7291_show_voltage_high, ad7291_set_voltage_high, 0);
IIO_EVENT_ATTR_SH(v5_low, iio_event_ad7291,
		ad7291_show_voltage_low, ad7291_set_voltage_low, 0);
IIO_EVENT_ATTR_SH(v5_hyst, iio_event_ad7291,
		ad7291_show_voltage_hyst, ad7291_set_voltage_hyst, 0);
IIO_EVENT_ATTR_SH(v6_high, iio_event_ad7291,
		ad7291_show_voltage_high, ad7291_set_voltage_high, 0);
IIO_EVENT_ATTR_SH(v6_low, iio_event_ad7291,
		ad7291_show_voltage_low, ad7291_set_voltage_low, 0);
IIO_EVENT_ATTR_SH(v6_hyst, iio_event_ad7291,
		ad7291_show_voltage_hyst, ad7291_set_voltage_hyst, 0);
IIO_EVENT_ATTR_SH(v7_high, iio_event_ad7291,
		ad7291_show_voltage_high, ad7291_set_voltage_high, 0);
IIO_EVENT_ATTR_SH(v7_low, iio_event_ad7291,
		ad7291_show_voltage_low, ad7291_set_voltage_low, 0);
IIO_EVENT_ATTR_SH(v7_hyst, iio_event_ad7291,
		ad7291_show_voltage_hyst, ad7291_set_voltage_hyst, 0);

static struct attribute *ad7291_event_attributes[] = {
	&iio_event_attr_t_sense_high.dev_attr.attr,
	&iio_event_attr_t_sense_low.dev_attr.attr,
	&iio_event_attr_t_sense_hyst.dev_attr.attr,
	&iio_event_attr_v0_high.dev_attr.attr,
	&iio_event_attr_v0_low.dev_attr.attr,
	&iio_event_attr_v0_hyst.dev_attr.attr,
	&iio_event_attr_v1_high.dev_attr.attr,
	&iio_event_attr_v1_low.dev_attr.attr,
	&iio_event_attr_v1_hyst.dev_attr.attr,
	&iio_event_attr_v2_high.dev_attr.attr,
	&iio_event_attr_v2_low.dev_attr.attr,
	&iio_event_attr_v2_hyst.dev_attr.attr,
	&iio_event_attr_v3_high.dev_attr.attr,
	&iio_event_attr_v3_low.dev_attr.attr,
	&iio_event_attr_v3_hyst.dev_attr.attr,
	&iio_event_attr_v4_high.dev_attr.attr,
	&iio_event_attr_v4_low.dev_attr.attr,
	&iio_event_attr_v4_hyst.dev_attr.attr,
	&iio_event_attr_v5_high.dev_attr.attr,
	&iio_event_attr_v5_low.dev_attr.attr,
	&iio_event_attr_v5_hyst.dev_attr.attr,
	&iio_event_attr_v6_high.dev_attr.attr,
	&iio_event_attr_v6_low.dev_attr.attr,
	&iio_event_attr_v6_hyst.dev_attr.attr,
	&iio_event_attr_v7_high.dev_attr.attr,
	&iio_event_attr_v7_low.dev_attr.attr,
	&iio_event_attr_v7_hyst.dev_attr.attr,
	NULL,
};

static struct attribute_group ad7291_event_attribute_group = {
	.attrs = ad7291_event_attributes,
};

/*
 * device probe and remove
 */

static int __devinit ad7291_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct ad7291_chip_info *chip;
	int ret = 0;

	chip = kzalloc(sizeof(struct ad7291_chip_info), GFP_KERNEL);

	if (chip == NULL)
		return -ENOMEM;

	/* this is only used for device removal purposes */
	i2c_set_clientdata(client, chip);

	chip->client = client;
	chip->name = id->name;
	chip->command = AD7291_NOISE_DELAY | AD7291_T_SENSE_MASK;

	chip->indio_dev = iio_allocate_device();
	if (chip->indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_free_chip;
	}

	chip->indio_dev->dev.parent = &client->dev;
	chip->indio_dev->attrs = &ad7291_attribute_group;
	chip->indio_dev->event_attrs = &ad7291_event_attribute_group;
	chip->indio_dev->dev_data = (void *)chip;
	chip->indio_dev->driver_module = THIS_MODULE;
	chip->indio_dev->num_interrupt_lines = 1;
	chip->indio_dev->modes = INDIO_DIRECT_MODE;

	ret = iio_device_register(chip->indio_dev);
	if (ret)
		goto error_free_dev;

	if (client->irq > 0) {
		ret = iio_register_interrupt_line(client->irq,
				chip->indio_dev,
				0,
				IRQF_TRIGGER_LOW,
				chip->name);
		if (ret)
			goto error_unreg_dev;

		/*
		 * The event handler list element refer to iio_event_ad7291.
		 * All event attributes bind to the same event handler.
		 * So, only register event handler once.
		 */
		iio_add_event_to_list(&iio_event_ad7291,
				&chip->indio_dev->interrupts[0]->ev_list);

		INIT_WORK(&chip->thresh_work, ad7291_interrupt_bh);

		/* set irq polarity low level */
		chip->command |= AD7291_ALART_POLARITY;
	}

	ret = ad7291_i2c_write(chip, AD7291_COMMAND, chip->command);
	if (ret) {
		ret = -EIO;
		goto error_unreg_irq;
	}

	dev_info(&client->dev, "%s temperature sensor registered.\n",
			 id->name);

	return 0;

error_unreg_irq:
	iio_unregister_interrupt_line(chip->indio_dev, 0);
error_unreg_dev:
	iio_device_unregister(chip->indio_dev);
error_free_dev:
	iio_free_device(chip->indio_dev);
error_free_chip:
	kfree(chip);

	return ret;
}

static int __devexit ad7291_remove(struct i2c_client *client)
{
	struct ad7291_chip_info *chip = i2c_get_clientdata(client);
	struct iio_dev *indio_dev = chip->indio_dev;

	if (client->irq)
		iio_unregister_interrupt_line(indio_dev, 0);
	iio_device_unregister(indio_dev);
	iio_free_device(chip->indio_dev);
	kfree(chip);

	return 0;
}

static const struct i2c_device_id ad7291_id[] = {
	{ "ad7291", 0 },
	{}
};

MODULE_DEVICE_TABLE(i2c, ad7291_id);

static struct i2c_driver ad7291_driver = {
	.driver = {
		.name = "ad7291",
	},
	.probe = ad7291_probe,
	.remove = __devexit_p(ad7291_remove),
	.id_table = ad7291_id,
};

static __init int ad7291_init(void)
{
	return i2c_add_driver(&ad7291_driver);
}

static __exit void ad7291_exit(void)
{
	i2c_del_driver(&ad7291_driver);
}

MODULE_AUTHOR("Sonic Zhang <sonic.zhang@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD7291 digital"
			" temperature sensor driver");
MODULE_LICENSE("GPL v2");

module_init(ad7291_init);
module_exit(ad7291_exit);
