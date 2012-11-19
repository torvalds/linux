/*
 * AD7291 8-Channel, I2C, 12-Bit SAR ADC with Temperature Sensor
 *
 * Copyright 2010-2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regulator/consumer.h>
#include <linux/err.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/events.h>

/*
 * Simplified handling
 *
 * If no events enabled - single polled channel read
 * If event enabled direct reads disable unless channel
 * is in the read mask.
 *
 * The noise-delayed bit as per datasheet suggestion is always enabled.
 *
 */

/*
 * AD7291 registers definition
 */
#define AD7291_COMMAND			0x00
#define AD7291_VOLTAGE			0x01
#define AD7291_T_SENSE			0x02
#define AD7291_T_AVERAGE		0x03
#define AD7291_CH0_DATA_HIGH		0x04
#define AD7291_CH0_DATA_LOW		0x05
#define AD7291_CH0_HYST			0x06
#define AD7291_CH1_DATA_HIGH		0x07
#define AD7291_CH1_DATA_LOW		0x08
#define AD7291_CH1_HYST			0x09
#define AD7291_CH2_DATA_HIGH		0x0A
#define AD7291_CH2_DATA_LOW		0x0B
#define AD7291_CH2_HYST			0x0C
#define AD7291_CH3_DATA_HIGH		0x0D
#define AD7291_CH3_DATA_LOW		0x0E
#define AD7291_CH3_HYST			0x0F
#define AD7291_CH4_DATA_HIGH		0x10
#define AD7291_CH4_DATA_LOW		0x11
#define AD7291_CH4_HYST			0x12
#define AD7291_CH5_DATA_HIGH		0x13
#define AD7291_CH5_DATA_LOW		0x14
#define AD7291_CH5_HYST			0x15
#define AD7291_CH6_DATA_HIGH		0x16
#define AD7291_CH6_DATA_LOW		0x17
#define AD7291_CH6_HYST			0x18
#define AD7291_CH7_DATA_HIGH		0x19
#define AD7291_CH7_DATA_LOW		0x1A
#define AD7291_CH7_HYST			0x2B
#define AD7291_T_SENSE_HIGH		0x1C
#define AD7291_T_SENSE_LOW		0x1D
#define AD7291_T_SENSE_HYST		0x1E
#define AD7291_VOLTAGE_ALERT_STATUS	0x1F
#define AD7291_T_ALERT_STATUS		0x20

#define AD7291_VOLTAGE_LIMIT_COUNT	8


/*
 * AD7291 command
 */
#define AD7291_AUTOCYCLE		(1 << 0)
#define AD7291_RESET			(1 << 1)
#define AD7291_ALERT_CLEAR		(1 << 2)
#define AD7291_ALERT_POLARITY		(1 << 3)
#define AD7291_EXT_REF			(1 << 4)
#define AD7291_NOISE_DELAY		(1 << 5)
#define AD7291_T_SENSE_MASK		(1 << 7)
#define AD7291_VOLTAGE_MASK		0xFF00
#define AD7291_VOLTAGE_OFFSET		0x8

/*
 * AD7291 value masks
 */
#define AD7291_CHANNEL_MASK		0xF000
#define AD7291_BITS			12
#define AD7291_VALUE_MASK		0xFFF
#define AD7291_T_VALUE_SIGN		0x400
#define AD7291_T_VALUE_FLOAT_OFFSET	2
#define AD7291_T_VALUE_FLOAT_MASK	0x2

#define AD7291_BITS			12

struct ad7291_chip_info {
	struct i2c_client	*client;
	struct regulator	*reg;
	u16			int_vref_mv;
	u16			command;
	u16			c_mask;	/* Active voltage channels for events */
	struct mutex		state_lock;
};

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
	return i2c_smbus_write_word_data(chip->client, reg, swab16(data));
}

static ssize_t ad7291_store_reset(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad7291_chip_info *chip = iio_priv(indio_dev);

	return ad7291_i2c_write(chip, AD7291_COMMAND,
				chip->command | AD7291_RESET);
}

static IIO_DEVICE_ATTR(reset, S_IWUSR, NULL, ad7291_store_reset, 0);

static struct attribute *ad7291_attributes[] = {
	&iio_dev_attr_reset.dev_attr.attr,
	NULL,
};

static const struct attribute_group ad7291_attribute_group = {
	.attrs = ad7291_attributes,
};

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

	command = chip->command | AD7291_ALERT_CLEAR;
	ad7291_i2c_write(chip, AD7291_COMMAND, command);

	command = chip->command & ~AD7291_ALERT_CLEAR;
	ad7291_i2c_write(chip, AD7291_COMMAND, command);

	/* For now treat t_sense and t_sense_average the same */
	if ((t_status & (1 << 0)) || (t_status & (1 << 2)))
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(IIO_TEMP,
						    0,
						    IIO_EV_TYPE_THRESH,
						    IIO_EV_DIR_FALLING),
			       timestamp);
	if ((t_status & (1 << 1)) || (t_status & (1 << 3)))
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(IIO_TEMP,
						    0,
						    IIO_EV_TYPE_THRESH,
						    IIO_EV_DIR_RISING),
			       timestamp);

	for (i = 0; i < AD7291_VOLTAGE_LIMIT_COUNT*2; i += 2) {
		if (v_status & (1 << i))
			iio_push_event(indio_dev,
				       IIO_UNMOD_EVENT_CODE(IIO_VOLTAGE,
							    i/2,
							    IIO_EV_TYPE_THRESH,
							    IIO_EV_DIR_FALLING),
				       timestamp);
		if (v_status & (1 << (i + 1)))
			iio_push_event(indio_dev,
				       IIO_UNMOD_EVENT_CODE(IIO_VOLTAGE,
							    i/2,
							    IIO_EV_TYPE_THRESH,
							    IIO_EV_DIR_RISING),
				       timestamp);
	}

	return IRQ_HANDLED;
}

static inline ssize_t ad7291_show_hyst(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad7291_chip_info *chip = iio_priv(indio_dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	u16 data;
	int ret;

	ret = ad7291_i2c_read(chip, this_attr->address, &data);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", data & AD7291_VALUE_MASK);
}

static inline ssize_t ad7291_set_hyst(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf,
				      size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad7291_chip_info *chip = iio_priv(indio_dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	u16 data;
	int ret;

	ret = kstrtou16(buf, 10, &data);

	if (ret < 0)
		return ret;
	if (data > AD7291_VALUE_MASK)
		return -EINVAL;

	ret = ad7291_i2c_write(chip, this_attr->address, data);
	if (ret < 0)
		return ret;

	return len;
}

static IIO_DEVICE_ATTR(in_temp0_thresh_both_hyst_raw,
		       S_IRUGO | S_IWUSR,
		       ad7291_show_hyst, ad7291_set_hyst,
		       AD7291_T_SENSE_HYST);
static IIO_DEVICE_ATTR(in_voltage0_thresh_both_hyst_raw,
		       S_IRUGO | S_IWUSR,
		       ad7291_show_hyst, ad7291_set_hyst, AD7291_CH0_HYST);
static IIO_DEVICE_ATTR(in_voltage1_thresh_both_hyst_raw,
		       S_IRUGO | S_IWUSR,
		       ad7291_show_hyst, ad7291_set_hyst, AD7291_CH1_HYST);
static IIO_DEVICE_ATTR(in_voltage2_thresh_both_hyst_raw,
		       S_IRUGO | S_IWUSR,
		       ad7291_show_hyst, ad7291_set_hyst, AD7291_CH2_HYST);
static IIO_DEVICE_ATTR(in_voltage3_thresh_both_hyst_raw,
		       S_IRUGO | S_IWUSR,
		       ad7291_show_hyst, ad7291_set_hyst, AD7291_CH3_HYST);
static IIO_DEVICE_ATTR(in_voltage4_thresh_both_hyst_raw,
		       S_IRUGO | S_IWUSR,
		       ad7291_show_hyst, ad7291_set_hyst, AD7291_CH4_HYST);
static IIO_DEVICE_ATTR(in_voltage5_thresh_both_hyst_raw,
		       S_IRUGO | S_IWUSR,
		       ad7291_show_hyst, ad7291_set_hyst, AD7291_CH5_HYST);
static IIO_DEVICE_ATTR(in_voltage6_thresh_both_hyst_raw,
		       S_IRUGO | S_IWUSR,
		       ad7291_show_hyst, ad7291_set_hyst, AD7291_CH6_HYST);
static IIO_DEVICE_ATTR(in_voltage7_thresh_both_hyst_raw,
		       S_IRUGO | S_IWUSR,
		       ad7291_show_hyst, ad7291_set_hyst, AD7291_CH7_HYST);

static struct attribute *ad7291_event_attributes[] = {
	&iio_dev_attr_in_temp0_thresh_both_hyst_raw.dev_attr.attr,
	&iio_dev_attr_in_voltage0_thresh_both_hyst_raw.dev_attr.attr,
	&iio_dev_attr_in_voltage1_thresh_both_hyst_raw.dev_attr.attr,
	&iio_dev_attr_in_voltage2_thresh_both_hyst_raw.dev_attr.attr,
	&iio_dev_attr_in_voltage3_thresh_both_hyst_raw.dev_attr.attr,
	&iio_dev_attr_in_voltage4_thresh_both_hyst_raw.dev_attr.attr,
	&iio_dev_attr_in_voltage5_thresh_both_hyst_raw.dev_attr.attr,
	&iio_dev_attr_in_voltage6_thresh_both_hyst_raw.dev_attr.attr,
	&iio_dev_attr_in_voltage7_thresh_both_hyst_raw.dev_attr.attr,
	NULL,
};

/* high / low */
static u8 ad7291_limit_regs[9][2] = {
	{ AD7291_CH0_DATA_HIGH, AD7291_CH0_DATA_LOW },
	{ AD7291_CH1_DATA_HIGH, AD7291_CH1_DATA_LOW },
	{ AD7291_CH2_DATA_HIGH, AD7291_CH2_DATA_LOW },
	{ AD7291_CH3_DATA_HIGH, AD7291_CH3_DATA_LOW }, /* FIXME: ? */
	{ AD7291_CH4_DATA_HIGH, AD7291_CH4_DATA_LOW },
	{ AD7291_CH5_DATA_HIGH, AD7291_CH5_DATA_LOW },
	{ AD7291_CH6_DATA_HIGH, AD7291_CH6_DATA_LOW },
	{ AD7291_CH7_DATA_HIGH, AD7291_CH7_DATA_LOW },
	/* temp */
	{ AD7291_T_SENSE_HIGH, AD7291_T_SENSE_LOW },
};

static int ad7291_read_event_value(struct iio_dev *indio_dev,
				   u64 event_code,
				   int *val)
{
	struct ad7291_chip_info *chip = iio_priv(indio_dev);

	int ret;
	u8 reg;
	u16 uval;
	s16 signval;

	switch (IIO_EVENT_CODE_EXTRACT_CHAN_TYPE(event_code)) {
	case IIO_VOLTAGE:
		reg = ad7291_limit_regs[IIO_EVENT_CODE_EXTRACT_CHAN(event_code)]
			[!(IIO_EVENT_CODE_EXTRACT_DIR(event_code) ==
			   IIO_EV_DIR_RISING)];

		ret = ad7291_i2c_read(chip, reg, &uval);
		if (ret < 0)
			return ret;
		*val = uval & AD7291_VALUE_MASK;
		return 0;

	case IIO_TEMP:
		reg = ad7291_limit_regs[8]
			[!(IIO_EVENT_CODE_EXTRACT_DIR(event_code) ==
			   IIO_EV_DIR_RISING)];

		ret = ad7291_i2c_read(chip, reg, &signval);
		if (ret < 0)
			return ret;
		signval = (s16)((signval & AD7291_VALUE_MASK) << 4) >> 4;
		*val = signval;
		return 0;
	default:
		return -EINVAL;
	};
}

static int ad7291_write_event_value(struct iio_dev *indio_dev,
				    u64 event_code,
				    int val)
{
	struct ad7291_chip_info *chip = iio_priv(indio_dev);
	u8 reg;
	s16 signval;

	switch (IIO_EVENT_CODE_EXTRACT_CHAN_TYPE(event_code)) {
	case IIO_VOLTAGE:
		if (val > AD7291_VALUE_MASK || val < 0)
			return -EINVAL;
		reg = ad7291_limit_regs[IIO_EVENT_CODE_EXTRACT_CHAN(event_code)]
			[!(IIO_EVENT_CODE_EXTRACT_DIR(event_code) ==
			   IIO_EV_DIR_RISING)];
		return ad7291_i2c_write(chip, reg, val);
	case IIO_TEMP:
		if (val > 2047 || val < -2048)
			return -EINVAL;
		reg = ad7291_limit_regs[8]
			[!(IIO_EVENT_CODE_EXTRACT_DIR(event_code) ==
			   IIO_EV_DIR_RISING)];
		signval = val;
		return ad7291_i2c_write(chip, reg, *(u16 *)&signval);
	default:
		return -EINVAL;
	};
}

static int ad7291_read_event_config(struct iio_dev *indio_dev,
				    u64 event_code)
{
	struct ad7291_chip_info *chip = iio_priv(indio_dev);
	/* To be enabled the channel must simply be on. If any are enabled
	   we are in continuous sampling mode */

	switch (IIO_EVENT_CODE_EXTRACT_CHAN_TYPE(event_code)) {
	case IIO_VOLTAGE:
		if (chip->c_mask &
		    (1 << (15 - IIO_EVENT_CODE_EXTRACT_CHAN(event_code))))
			return 1;
		else
			return 0;
	case IIO_TEMP:
		/* always on */
		return 1;
	default:
		return -EINVAL;
	}

}

static int ad7291_write_event_config(struct iio_dev *indio_dev,
				     u64 event_code,
				     int state)
{
	int ret = 0;
	struct ad7291_chip_info *chip = iio_priv(indio_dev);
	u16 regval;

	mutex_lock(&chip->state_lock);
	regval = chip->command;
	/*
	 * To be enabled the channel must simply be on. If any are enabled
	 * use continuous sampling mode.
	 * Possible to disable temp as well but that makes single read tricky.
	 */

	switch (IIO_EVENT_CODE_EXTRACT_TYPE(event_code)) {
	case IIO_VOLTAGE:
		if ((!state) && (chip->c_mask & (1 << (15 -
				IIO_EVENT_CODE_EXTRACT_CHAN(event_code)))))
			chip->c_mask &= ~(1 << (15 - IIO_EVENT_CODE_EXTRACT_CHAN
							(event_code)));
		else if (state && (!(chip->c_mask & (1 << (15 -
				IIO_EVENT_CODE_EXTRACT_CHAN(event_code))))))
			chip->c_mask |= (1 << (15 - IIO_EVENT_CODE_EXTRACT_CHAN
							(event_code)));
		else
			break;

		regval &= ~AD7291_AUTOCYCLE;
		regval |= chip->c_mask;
		if (chip->c_mask) /* Enable autocycle? */
			regval |= AD7291_AUTOCYCLE;

		ret = ad7291_i2c_write(chip, AD7291_COMMAND, regval);
		if (ret < 0)
			goto error_ret;

		chip->command = regval;
		break;
	default:
		ret = -EINVAL;
	}

error_ret:
	mutex_unlock(&chip->state_lock);
	return ret;
}

static int ad7291_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val,
			   int *val2,
			   long mask)
{
	int ret;
	struct ad7291_chip_info *chip = iio_priv(indio_dev);
	unsigned int scale_uv;
	u16 regval;
	s16 signval;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		switch (chan->type) {
		case IIO_VOLTAGE:
			mutex_lock(&chip->state_lock);
			/* If in autocycle mode drop through */
			if (chip->command & AD7291_AUTOCYCLE) {
				mutex_unlock(&chip->state_lock);
				return -EBUSY;
			}
			/* Enable this channel alone */
			regval = chip->command & (~AD7291_VOLTAGE_MASK);
			regval |= 1 << (15 - chan->channel);
			ret = ad7291_i2c_write(chip, AD7291_COMMAND, regval);
			if (ret < 0) {
				mutex_unlock(&chip->state_lock);
				return ret;
			}
			/* Read voltage */
			ret = i2c_smbus_read_word_data(chip->client,
						       AD7291_VOLTAGE);
			if (ret < 0) {
				mutex_unlock(&chip->state_lock);
				return ret;
			}
			*val = swab16((u16)ret) & AD7291_VALUE_MASK;
			mutex_unlock(&chip->state_lock);
			return IIO_VAL_INT;
		case IIO_TEMP:
			/* Assumes tsense bit of command register always set */
			ret = i2c_smbus_read_word_data(chip->client,
						       AD7291_T_SENSE);
			if (ret < 0)
				return ret;
			signval = (s16)((swab16((u16)ret) &
				AD7291_VALUE_MASK) << 4) >> 4;
			*val = signval;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_AVERAGE_RAW:
		ret = i2c_smbus_read_word_data(chip->client,
					       AD7291_T_AVERAGE);
			if (ret < 0)
				return ret;
			signval = (s16)((swab16((u16)ret) &
				AD7291_VALUE_MASK) << 4) >> 4;
			*val = signval;
			return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_VOLTAGE:
			scale_uv = (chip->int_vref_mv * 1000) >> AD7291_BITS;
			*val =  scale_uv / 1000;
			*val2 = (scale_uv % 1000) * 1000;
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_TEMP:
			/*
			 * One LSB of the ADC corresponds to 0.25 deg C.
			 * The temperature reading is in 12-bit twos
			 * complement format
			 */
			*val = 250;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

#define AD7291_VOLTAGE_CHAN(_chan)					\
{									\
	.type = IIO_VOLTAGE,						\
	.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |			\
	IIO_CHAN_INFO_SCALE_SHARED_BIT,					\
	.indexed = 1,							\
	.channel = _chan,						\
	.event_mask = IIO_EV_BIT(IIO_EV_TYPE_THRESH, IIO_EV_DIR_RISING)|\
	IIO_EV_BIT(IIO_EV_TYPE_THRESH, IIO_EV_DIR_FALLING)		\
}

static const struct iio_chan_spec ad7291_channels[] = {
	AD7291_VOLTAGE_CHAN(0),
	AD7291_VOLTAGE_CHAN(1),
	AD7291_VOLTAGE_CHAN(2),
	AD7291_VOLTAGE_CHAN(3),
	AD7291_VOLTAGE_CHAN(4),
	AD7291_VOLTAGE_CHAN(5),
	AD7291_VOLTAGE_CHAN(6),
	AD7291_VOLTAGE_CHAN(7),
	{
		.type = IIO_TEMP,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
				IIO_CHAN_INFO_AVERAGE_RAW_SEPARATE_BIT |
				IIO_CHAN_INFO_SCALE_SEPARATE_BIT,
		.indexed = 1,
		.channel = 0,
		.event_mask =
		IIO_EV_BIT(IIO_EV_TYPE_THRESH, IIO_EV_DIR_RISING)|
		IIO_EV_BIT(IIO_EV_TYPE_THRESH, IIO_EV_DIR_FALLING)
	}
};

static struct attribute_group ad7291_event_attribute_group = {
	.attrs = ad7291_event_attributes,
};

static const struct iio_info ad7291_info = {
	.attrs = &ad7291_attribute_group,
	.read_raw = &ad7291_read_raw,
	.read_event_config = &ad7291_read_event_config,
	.write_event_config = &ad7291_write_event_config,
	.read_event_value = &ad7291_read_event_value,
	.write_event_value = &ad7291_write_event_value,
	.event_attrs = &ad7291_event_attribute_group,
};

static int ad7291_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct ad7291_chip_info *chip;
	struct iio_dev *indio_dev;
	int ret = 0, voltage_uv = 0;

	indio_dev = iio_device_alloc(sizeof(*chip));
	if (indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}
	chip = iio_priv(indio_dev);

	chip->reg = regulator_get(&client->dev, "vcc");
	if (!IS_ERR(chip->reg)) {
		ret = regulator_enable(chip->reg);
		if (ret)
			goto error_put_reg;
		voltage_uv = regulator_get_voltage(chip->reg);
	}

	mutex_init(&chip->state_lock);
	/* this is only used for device removal purposes */
	i2c_set_clientdata(client, indio_dev);

	chip->client = client;

	chip->command = AD7291_NOISE_DELAY |
			AD7291_T_SENSE_MASK | /* Tsense always enabled */
			AD7291_ALERT_POLARITY; /* set irq polarity low level */

	if (voltage_uv) {
		chip->int_vref_mv = voltage_uv / 1000;
		chip->command |= AD7291_EXT_REF;
	} else {
		chip->int_vref_mv = 2500; /* Build-in ref */
	}

	indio_dev->name = id->name;
	indio_dev->channels = ad7291_channels;
	indio_dev->num_channels = ARRAY_SIZE(ad7291_channels);

	indio_dev->dev.parent = &client->dev;
	indio_dev->info = &ad7291_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = ad7291_i2c_write(chip, AD7291_COMMAND, AD7291_RESET);
	if (ret) {
		ret = -EIO;
		goto error_disable_reg;
	}

	ret = ad7291_i2c_write(chip, AD7291_COMMAND, chip->command);
	if (ret) {
		ret = -EIO;
		goto error_disable_reg;
	}

	if (client->irq > 0) {
		ret = request_threaded_irq(client->irq,
					   NULL,
					   &ad7291_event_handler,
					   IRQF_TRIGGER_LOW | IRQF_ONESHOT,
					   id->name,
					   indio_dev);
		if (ret)
			goto error_disable_reg;
	}

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_unreg_irq;

	dev_info(&client->dev, "%s ADC registered.\n",
			 id->name);

	return 0;

error_unreg_irq:
	if (client->irq)
		free_irq(client->irq, indio_dev);
error_disable_reg:
	if (!IS_ERR(chip->reg))
		regulator_disable(chip->reg);
error_put_reg:
	if (!IS_ERR(chip->reg))
		regulator_put(chip->reg);

	iio_device_free(indio_dev);
error_ret:
	return ret;
}

static int ad7291_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct ad7291_chip_info *chip = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);

	if (client->irq)
		free_irq(client->irq, indio_dev);

	if (!IS_ERR(chip->reg)) {
		regulator_disable(chip->reg);
		regulator_put(chip->reg);
	}

	iio_device_free(indio_dev);

	return 0;
}

static const struct i2c_device_id ad7291_id[] = {
	{ "ad7291", 0 },
	{}
};

MODULE_DEVICE_TABLE(i2c, ad7291_id);

static struct i2c_driver ad7291_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
	},
	.probe = ad7291_probe,
	.remove = ad7291_remove,
	.id_table = ad7291_id,
};
module_i2c_driver(ad7291_driver);

MODULE_AUTHOR("Sonic Zhang <sonic.zhang@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD7291 ADC driver");
MODULE_LICENSE("GPL v2");
