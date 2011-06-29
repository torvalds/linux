/*
 * AD7291 digital temperature sensor driver supporting AD7291
 *
 * Copyright 2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/list.h>
#include <linux/i2c.h>

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
	struct i2c_client *client;
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
	struct ad7291_chip_info *chip = iio_priv(dev_info);

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
	struct ad7291_chip_info *chip = iio_priv(dev_info);
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
	struct ad7291_chip_info *chip = iio_priv(dev_info);
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
	struct ad7291_chip_info *chip = iio_priv(dev_info);

	return sprintf(buf, "%d\n", !!(chip->command & AD7291_EXT_REF));
}

static ssize_t ad7291_store_ext_ref(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7291_chip_info *chip = iio_priv(dev_info);
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
	struct ad7291_chip_info *chip = iio_priv(dev_info);

	return sprintf(buf, "%d\n", !!(chip->command & AD7291_NOISE_DELAY));
}

static ssize_t ad7291_store_noise_delay(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7291_chip_info *chip = iio_priv(dev_info);
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
	struct ad7291_chip_info *chip = iio_priv(dev_info);
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
	struct ad7291_chip_info *chip = iio_priv(dev_info);
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
	struct ad7291_chip_info *chip = iio_priv(dev_info);
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
	struct ad7291_chip_info *chip = iio_priv(dev_info);

	return sprintf(buf, "0x%x\n", (chip->command & AD7291_VOLTAGE_MASK) >>
			AD7291_VOLTAGE_OFFSET);
}

static ssize_t ad7291_store_channel_mask(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7291_chip_info *chip = iio_priv(dev_info);
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
	NULL,
};

static const struct attribute_group ad7291_attribute_group = {
	.attrs = ad7291_attributes,
};

/*
 * temperature bound events
 */

static irqreturn_t ad7291_event_handler(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct ad7291_chip_info *chip = iio_priv(private);
	u16 t_status, v_status;
	u16 command;
	int i;
	s64 timestamp = iio_get_time_ns();

	if (ad7291_i2c_read(chip, AD7291_T_ALERT_STATUS, &t_status))
		return IRQ_HANDLED;

	if (ad7291_i2c_read(chip, AD7291_VOLTAGE_ALERT_STATUS, &v_status))
		return IRQ_HANDLED;

	if (!(t_status || v_status))
		return IRQ_HANDLED;

	command = chip->command | AD7291_ALART_CLEAR;
	ad7291_i2c_write(chip, AD7291_COMMAND, command);

	command = chip->command & ~AD7291_ALART_CLEAR;
	ad7291_i2c_write(chip, AD7291_COMMAND, command);

	if (t_status & (1 << 0))
		iio_push_event(indio_dev, 0,
			       IIO_UNMOD_EVENT_CODE(IIO_TEMP,
						    0,
						    IIO_EV_TYPE_THRESH,
						    IIO_EV_DIR_FALLING),
			       timestamp);
	if (t_status & (1 << 1))
		iio_push_event(indio_dev, 0,
			       IIO_UNMOD_EVENT_CODE(IIO_TEMP,
						    0,
						    IIO_EV_TYPE_THRESH,
						    IIO_EV_DIR_RISING),
			       timestamp);
	if (t_status & (1 << 2))
		iio_push_event(indio_dev, 0,
			       IIO_UNMOD_EVENT_CODE(IIO_TEMP,
						    0,
						    IIO_EV_TYPE_THRESH,
						    IIO_EV_DIR_FALLING),
			       timestamp);
	if (t_status & (1 << 3))
		iio_push_event(indio_dev, 0,
			       IIO_UNMOD_EVENT_CODE(IIO_TEMP,
						    0,
						    IIO_EV_TYPE_THRESH,
						    IIO_EV_DIR_RISING),
			       timestamp);

	for (i = 0; i < AD7291_VOLTAGE_LIMIT_COUNT*2; i += 2) {
		if (v_status & (1 << i))
			iio_push_event(indio_dev, 0,
				       IIO_UNMOD_EVENT_CODE(IIO_IN,
							    i/2,
							    IIO_EV_TYPE_THRESH,
							    IIO_EV_DIR_FALLING),
				       timestamp);
		if (v_status & (1 << (i + 1)))
			iio_push_event(indio_dev, 0,
				       IIO_UNMOD_EVENT_CODE(IIO_IN,
							    i/2,
							    IIO_EV_TYPE_THRESH,
							    IIO_EV_DIR_RISING),
				       timestamp);
	}

	return IRQ_HANDLED;
}

static inline ssize_t ad7291_show_t_bound(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7291_chip_info *chip = iio_priv(dev_info);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	u16 data;
	char sign = ' ';
	int ret;

	ret = ad7291_i2c_read(chip, this_attr->address, &data);
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
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7291_chip_info *chip = iio_priv(dev_info);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
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

	ret = ad7291_i2c_write(chip, this_attr->address, data);
	if (ret)
		return -EIO;

	return ret;
}

static inline ssize_t ad7291_show_v_bound(struct device *dev,
		struct device_attribute *attr,
		u8 bound_reg,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7291_chip_info *chip = iio_priv(dev_info);
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
	struct ad7291_chip_info *chip = iio_priv(dev_info);
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

static IIO_DEVICE_ATTR(t_sense_high_value,
		       S_IRUGO | S_IWUSR,
		       ad7291_show_t_bound, ad7291_set_t_bound,
		       AD7291_T_SENSE_HIGH);
static IIO_DEVICE_ATTR(t_sense_low_value,
		       S_IRUGO | S_IWUSR,
		       ad7291_show_t_bound, ad7291_set_t_bound,
		       AD7291_T_SENSE_LOW);
static IIO_DEVICE_ATTR(t_sense_hyst_value,
		       S_IRUGO | S_IWUSR,
		       ad7291_show_t_bound, ad7291_set_t_bound,
		       AD7291_T_SENSE_HYST);
static IIO_DEVICE_ATTR(v0_high,
		       S_IRUGO | S_IWUSR,
		       ad7291_show_t_bound, ad7291_set_t_bound, 0x04);
static IIO_DEVICE_ATTR(v0_low,
		       S_IRUGO | S_IWUSR,
		       ad7291_show_t_bound, ad7291_set_t_bound, 0x05);
static IIO_DEVICE_ATTR(v0_hyst,
		       S_IRUGO | S_IWUSR,
		       ad7291_show_t_bound, ad7291_set_t_bound, 0x06);
static IIO_DEVICE_ATTR(v1_high,
		       S_IRUGO | S_IWUSR,
		       ad7291_show_t_bound, ad7291_set_t_bound, 0x07);
static IIO_DEVICE_ATTR(v1_low,
		       S_IRUGO | S_IWUSR,
		       ad7291_show_t_bound, ad7291_set_t_bound, 0x08);
static IIO_DEVICE_ATTR(v1_hyst,
		       S_IRUGO | S_IWUSR,
		       ad7291_show_t_bound, ad7291_set_t_bound, 0x09);
static IIO_DEVICE_ATTR(v2_high,
		       S_IRUGO | S_IWUSR,
		       ad7291_show_t_bound, ad7291_set_t_bound, 0x0A);
static IIO_DEVICE_ATTR(v2_low,
		       S_IRUGO | S_IWUSR,
		       ad7291_show_t_bound, ad7291_set_t_bound, 0x0B);
static IIO_DEVICE_ATTR(v2_hyst,
		       S_IRUGO | S_IWUSR,
		       ad7291_show_t_bound, ad7291_set_t_bound, 0x0C);
static IIO_DEVICE_ATTR(v3_high,
		       S_IRUGO | S_IWUSR,
		       /* Datasheet suggests this one and this one only
			  has the registers in different order */
		       ad7291_show_t_bound, ad7291_set_t_bound, 0x0E);
static IIO_DEVICE_ATTR(v3_low,
		       S_IRUGO | S_IWUSR,
		       ad7291_show_t_bound, ad7291_set_t_bound, 0x0D);
static IIO_DEVICE_ATTR(v3_hyst,
		       S_IRUGO | S_IWUSR,
		       ad7291_show_t_bound, ad7291_set_t_bound, 0x0F);
static IIO_DEVICE_ATTR(v4_high,
		       S_IRUGO | S_IWUSR,
		       ad7291_show_t_bound, ad7291_set_t_bound, 0x10);
static IIO_DEVICE_ATTR(v4_low,
		       S_IRUGO | S_IWUSR,
		       ad7291_show_t_bound, ad7291_set_t_bound, 0x11);
static IIO_DEVICE_ATTR(v4_hyst,
		       S_IRUGO | S_IWUSR,
		       ad7291_show_t_bound, ad7291_set_t_bound, 0x12);
static IIO_DEVICE_ATTR(v5_high,
		       S_IRUGO | S_IWUSR,
		       ad7291_show_t_bound, ad7291_set_t_bound, 0x13);
static IIO_DEVICE_ATTR(v5_low,
		       S_IRUGO | S_IWUSR,
		       ad7291_show_t_bound, ad7291_set_t_bound, 0x14);
static IIO_DEVICE_ATTR(v5_hyst,
		       S_IRUGO | S_IWUSR,
		       ad7291_show_t_bound, ad7291_set_t_bound, 0x15);
static IIO_DEVICE_ATTR(v6_high,
		       S_IRUGO | S_IWUSR,
		       ad7291_show_t_bound, ad7291_set_t_bound, 0x16);
static IIO_DEVICE_ATTR(v6_low,
		       S_IRUGO | S_IWUSR,
		       ad7291_show_t_bound, ad7291_set_t_bound, 0x17);
static IIO_DEVICE_ATTR(v6_hyst,
		       S_IRUGO | S_IWUSR,
		       ad7291_show_t_bound, ad7291_set_t_bound, 0x18);
static IIO_DEVICE_ATTR(v7_high,
		       S_IRUGO | S_IWUSR,
		       ad7291_show_t_bound, ad7291_set_t_bound, 0x19);
static IIO_DEVICE_ATTR(v7_low,
		       S_IRUGO | S_IWUSR,
		       ad7291_show_t_bound, ad7291_set_t_bound, 0x1A);
static IIO_DEVICE_ATTR(v7_hyst,
		       S_IRUGO | S_IWUSR,
		       ad7291_show_t_bound, ad7291_set_t_bound, 0x1B);

static struct attribute *ad7291_event_attributes[] = {
	&iio_dev_attr_t_sense_high_value.dev_attr.attr,
	&iio_dev_attr_t_sense_low_value.dev_attr.attr,
	&iio_dev_attr_t_sense_hyst_value.dev_attr.attr,
	&iio_dev_attr_v0_high.dev_attr.attr,
	&iio_dev_attr_v0_low.dev_attr.attr,
	&iio_dev_attr_v0_hyst.dev_attr.attr,
	&iio_dev_attr_v1_high.dev_attr.attr,
	&iio_dev_attr_v1_low.dev_attr.attr,
	&iio_dev_attr_v1_hyst.dev_attr.attr,
	&iio_dev_attr_v2_high.dev_attr.attr,
	&iio_dev_attr_v2_low.dev_attr.attr,
	&iio_dev_attr_v2_hyst.dev_attr.attr,
	&iio_dev_attr_v3_high.dev_attr.attr,
	&iio_dev_attr_v3_low.dev_attr.attr,
	&iio_dev_attr_v3_hyst.dev_attr.attr,
	&iio_dev_attr_v4_high.dev_attr.attr,
	&iio_dev_attr_v4_low.dev_attr.attr,
	&iio_dev_attr_v4_hyst.dev_attr.attr,
	&iio_dev_attr_v5_high.dev_attr.attr,
	&iio_dev_attr_v5_low.dev_attr.attr,
	&iio_dev_attr_v5_hyst.dev_attr.attr,
	&iio_dev_attr_v6_high.dev_attr.attr,
	&iio_dev_attr_v6_low.dev_attr.attr,
	&iio_dev_attr_v6_hyst.dev_attr.attr,
	&iio_dev_attr_v7_high.dev_attr.attr,
	&iio_dev_attr_v7_low.dev_attr.attr,
	&iio_dev_attr_v7_hyst.dev_attr.attr,
	NULL,
};

static struct attribute_group ad7291_event_attribute_group = {
	.attrs = ad7291_event_attributes,
};

static const struct iio_info ad7291_info = {
	.attrs = &ad7291_attribute_group,
	.num_interrupt_lines = 1,
	.event_attrs = &ad7291_event_attribute_group,
};

/*
 * device probe and remove
 */

static int __devinit ad7291_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct ad7291_chip_info *chip;
	struct iio_dev *indio_dev;
	int ret = 0;

	indio_dev = iio_allocate_device(sizeof(*chip));
	if (indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}
	chip = iio_priv(indio_dev);
	/* this is only used for device removal purposes */
	i2c_set_clientdata(client, indio_dev);

	chip->client = client;
	chip->command = AD7291_NOISE_DELAY | AD7291_T_SENSE_MASK;

	indio_dev->name = id->name;
	indio_dev->dev.parent = &client->dev;
	indio_dev->info = &ad7291_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_free_dev;

	if (client->irq > 0) {
		ret = request_threaded_irq(client->irq,
					   NULL,
					   &ad7291_event_handler,
					   IRQF_TRIGGER_LOW | IRQF_ONESHOT,
					   id->name,
					   indio_dev);
		if (ret)
			goto error_unreg_dev;

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
	free_irq(client->irq, indio_dev);
error_unreg_dev:
	iio_device_unregister(indio_dev);
error_free_dev:
	iio_free_device(indio_dev);
error_ret:
	return ret;
}

static int __devexit ad7291_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);

	if (client->irq)
		free_irq(client->irq, indio_dev);
	iio_device_unregister(indio_dev);
	iio_free_device(indio_dev);

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
