/*
 * ADT75 digital temperature sensor driver supporting ADT75
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
 * ADT75 registers definition
 */

#define ADT75_TEMPERATURE		0
#define ADT75_CONFIG			1
#define ADT75_T_HYST			2
#define ADT75_T_OS			3
#define ADT75_ONESHOT			4

/*
 * ADT75 config
 */
#define ADT75_PD			0x1
#define ADT75_OS_INT			0x2
#define ADT75_OS_POLARITY		0x4
#define ADT75_FAULT_QUEUE_MASK		0x18
#define ADT75_FAULT_QUEUE_OFFSET	3
#define ADT75_SMBUS_ALART		0x8

/*
 * ADT75 masks
 */
#define ADT75_VALUE_SIGN		0x800
#define ADT75_VALUE_OFFSET		4
#define ADT75_VALUE_FLOAT_OFFSET	4
#define ADT75_VALUE_FLOAT_MASK		0xF


/*
 * struct adt75_chip_info - chip specifc information
 */

struct adt75_chip_info {
	const char *name;
	struct i2c_client *client;
	struct iio_dev *indio_dev;
	struct work_struct thresh_work;
	s64 last_timestamp;
	u8  config;
};

/*
 * adt75 register access by I2C
 */

static int adt75_i2c_read(struct adt75_chip_info *chip, u8 reg, u8 *data)
{
	struct i2c_client *client = chip->client;
	int ret = 0, len;

	ret = i2c_smbus_write_byte(client, reg);
	if (ret < 0) {
		dev_err(&client->dev, "I2C read register address error\n");
		return ret;
	}

	if (reg == ADT75_CONFIG || reg == ADT75_ONESHOT)
		len = 1;
	else
		len = 2;

	ret = i2c_master_recv(client, data, len);
	if (ret < 0) {
		dev_err(&client->dev, "I2C read error\n");
		return ret;
	}

	return ret;
}

static int adt75_i2c_write(struct adt75_chip_info *chip, u8 reg, u8 data)
{
	struct i2c_client *client = chip->client;
	int ret = 0;

	if (reg == ADT75_CONFIG || reg == ADT75_ONESHOT)
		ret = i2c_smbus_write_byte_data(client, reg, data);
	else
		ret = i2c_smbus_write_word_data(client, reg, data);

	if (ret < 0)
		dev_err(&client->dev, "I2C write error\n");

	return ret;
}

static ssize_t adt75_show_mode(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt75_chip_info *chip = dev_info->dev_data;

	if (chip->config & ADT75_PD)
		return sprintf(buf, "power-save\n");
	else
		return sprintf(buf, "full\n");
}

static ssize_t adt75_store_mode(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt75_chip_info *chip = dev_info->dev_data;
	int ret;
	u8 config;

	ret = adt75_i2c_read(chip, ADT75_CONFIG, &chip->config);
	if (ret)
		return -EIO;

	config = chip->config & ~ADT75_PD;
	if (!strcmp(buf, "full"))
		config |= ADT75_PD;

	ret = adt75_i2c_write(chip, ADT75_CONFIG, config);
	if (ret)
		return -EIO;

	chip->config = config;

	return ret;
}

static IIO_DEVICE_ATTR(mode, S_IRUGO | S_IWUSR,
		adt75_show_mode,
		adt75_store_mode,
		0);

static ssize_t adt75_show_available_modes(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return sprintf(buf, "full\npower-down\n");
}

static IIO_DEVICE_ATTR(available_modes, S_IRUGO, adt75_show_available_modes, NULL, 0);

static ssize_t adt75_show_oneshot(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt75_chip_info *chip = dev_info->dev_data;

	return sprintf(buf, "%d\n", !!(chip->config & ADT75_ONESHOT));
}

static ssize_t adt75_store_oneshot(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt75_chip_info *chip = dev_info->dev_data;
	unsigned long data = 0;
	int ret;
	u8 config;

	ret = strict_strtoul(buf, 10, &data);
	if (ret)
		return -EINVAL;


	ret = adt75_i2c_read(chip, ADT75_CONFIG, &chip->config);
	if (ret)
		return -EIO;

	config = chip->config & ~ADT75_ONESHOT;
	if (data)
		config |= ADT75_ONESHOT;

	ret = adt75_i2c_write(chip, ADT75_CONFIG, config);
	if (ret)
		return -EIO;

	chip->config = config;

	return ret;
}

static IIO_DEVICE_ATTR(oneshot, S_IRUGO | S_IWUSR,
		adt75_show_oneshot,
		adt75_store_oneshot,
		0);

static ssize_t adt75_show_value(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt75_chip_info *chip = dev_info->dev_data;
	u16 data;
	char sign = ' ';
	int ret;

	if (chip->config & ADT75_PD) {
		dev_err(dev, "Can't read value in power-down mode.\n");
		return -EIO;
	}

	if (chip->config & ADT75_ONESHOT) {
		/* write to active converter */
		ret = i2c_smbus_write_byte(chip->client, ADT75_ONESHOT);
		if (ret)
			return -EIO;
	}

	ret = adt75_i2c_read(chip, ADT75_TEMPERATURE, (u8 *)&data);
	if (ret)
		return -EIO;

	data = swab16(data) >> ADT75_VALUE_OFFSET;
	if (data & ADT75_VALUE_SIGN) {
		/* convert supplement to positive value */
		data = (ADT75_VALUE_SIGN << 1) - data;
		sign = '-';
	}

	return sprintf(buf, "%c%d.%.4d\n", sign,
		(data >> ADT75_VALUE_FLOAT_OFFSET),
		(data & ADT75_VALUE_FLOAT_MASK) * 625);
}

static IIO_DEVICE_ATTR(value, S_IRUGO, adt75_show_value, NULL, 0);

static ssize_t adt75_show_name(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt75_chip_info *chip = dev_info->dev_data;
	return sprintf(buf, "%s\n", chip->name);
}

static IIO_DEVICE_ATTR(name, S_IRUGO, adt75_show_name, NULL, 0);

static struct attribute *adt75_attributes[] = {
	&iio_dev_attr_available_modes.dev_attr.attr,
	&iio_dev_attr_mode.dev_attr.attr,
	&iio_dev_attr_oneshot.dev_attr.attr,
	&iio_dev_attr_value.dev_attr.attr,
	&iio_dev_attr_name.dev_attr.attr,
	NULL,
};

static const struct attribute_group adt75_attribute_group = {
	.attrs = adt75_attributes,
};

/*
 * temperature bound events
 */

#define IIO_EVENT_CODE_ADT75_OTI    IIO_BUFFER_EVENT_CODE(0)

static void adt75_interrupt_bh(struct work_struct *work_s)
{
	struct adt75_chip_info *chip =
		container_of(work_s, struct adt75_chip_info, thresh_work);

	enable_irq(chip->client->irq);

	iio_push_event(chip->indio_dev, 0,
			IIO_EVENT_CODE_ADT75_OTI,
			chip->last_timestamp);
}

static int adt75_interrupt(struct iio_dev *dev_info,
		int index,
		s64 timestamp,
		int no_test)
{
	struct adt75_chip_info *chip = dev_info->dev_data;

	chip->last_timestamp = timestamp;
	schedule_work(&chip->thresh_work);

	return 0;
}

IIO_EVENT_SH(adt75, &adt75_interrupt);

static ssize_t adt75_show_oti_mode(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt75_chip_info *chip = dev_info->dev_data;
	int ret;

	/* retrive ALART status */
	ret = adt75_i2c_read(chip, ADT75_CONFIG, &chip->config);
	if (ret)
		return -EIO;

	if (chip->config & ADT75_OS_INT)
		return sprintf(buf, "interrupt\n");
	else
		return sprintf(buf, "comparator\n");
}

static ssize_t adt75_set_oti_mode(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt75_chip_info *chip = dev_info->dev_data;
	int ret;
	u8 config;

	/* retrive ALART status */
	ret = adt75_i2c_read(chip, ADT75_CONFIG, &chip->config);
	if (ret)
		return -EIO;

	config = chip->config & ~ADT75_OS_INT;
	if (strcmp(buf, "comparator") != 0)
		config |= ADT75_OS_INT;

	ret = adt75_i2c_write(chip, ADT75_CONFIG, config);
	if (ret)
		return -EIO;

	chip->config = config;

	return ret;
}

static ssize_t adt75_show_available_oti_modes(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return sprintf(buf, "comparator\ninterrupt\n");
}

static ssize_t adt75_show_smbus_alart(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt75_chip_info *chip = dev_info->dev_data;
	int ret;

	/* retrive ALART status */
	ret = adt75_i2c_read(chip, ADT75_CONFIG, &chip->config);
	if (ret)
		return -EIO;

	return sprintf(buf, "%d\n", !!(chip->config & ADT75_SMBUS_ALART));
}

static ssize_t adt75_set_smbus_alart(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt75_chip_info *chip = dev_info->dev_data;
	unsigned long data = 0;
	int ret;
	u8 config;

	ret = strict_strtoul(buf, 10, &data);
	if (ret)
		return -EINVAL;

	/* retrive ALART status */
	ret = adt75_i2c_read(chip, ADT75_CONFIG, &chip->config);
	if (ret)
		return -EIO;

	config = chip->config & ~ADT75_SMBUS_ALART;
	if (data)
		config |= ADT75_SMBUS_ALART;

	ret = adt75_i2c_write(chip, ADT75_CONFIG, config);
	if (ret)
		return -EIO;

	chip->config = config;

	return ret;
}

static ssize_t adt75_show_fault_queue(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt75_chip_info *chip = dev_info->dev_data;
	int ret;

	/* retrive ALART status */
	ret = adt75_i2c_read(chip, ADT75_CONFIG, &chip->config);
	if (ret)
		return -EIO;

	return sprintf(buf, "%d\n", (chip->config & ADT75_FAULT_QUEUE_MASK) >>
				ADT75_FAULT_QUEUE_OFFSET);
}

static ssize_t adt75_set_fault_queue(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt75_chip_info *chip = dev_info->dev_data;
	unsigned long data;
	int ret;
	u8 config;

	ret = strict_strtoul(buf, 10, &data);
	if (ret || data > 3)
		return -EINVAL;

	/* retrive ALART status */
	ret = adt75_i2c_read(chip, ADT75_CONFIG, &chip->config);
	if (ret)
		return -EIO;

	config = chip->config & ~ADT75_FAULT_QUEUE_MASK;
	config |= (data << ADT75_FAULT_QUEUE_OFFSET);
	ret = adt75_i2c_write(chip, ADT75_CONFIG, config);
	if (ret)
		return -EIO;

	chip->config = config;

	return ret;
}
static inline ssize_t adt75_show_t_bound(struct device *dev,
		struct device_attribute *attr,
		u8 bound_reg,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt75_chip_info *chip = dev_info->dev_data;
	u16 data;
	char sign = ' ';
	int ret;

	ret = adt75_i2c_read(chip, bound_reg, (u8 *)&data);
	if (ret)
		return -EIO;

	data = swab16(data) >> ADT75_VALUE_OFFSET;
	if (data & ADT75_VALUE_SIGN) {
		/* convert supplement to positive value */
		data = (ADT75_VALUE_SIGN << 1) - data;
		sign = '-';
	}

	return sprintf(buf, "%c%d.%.4d\n", sign,
		(data >> ADT75_VALUE_FLOAT_OFFSET),
		(data & ADT75_VALUE_FLOAT_MASK) * 625);
}

static inline ssize_t adt75_set_t_bound(struct device *dev,
		struct device_attribute *attr,
		u8 bound_reg,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt75_chip_info *chip = dev_info->dev_data;
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
		if (len > ADT75_VALUE_FLOAT_OFFSET)
			len = ADT75_VALUE_FLOAT_OFFSET;
		pos[len] = 0;
		ret = strict_strtol(pos, 10, &tmp2);

		if (!ret)
			tmp2 = (tmp2 / 625) * 625;
	}

	if (tmp1 < 0)
		data = (u16)(-tmp1);
	else
		data = (u16)tmp1;
	data = (data << ADT75_VALUE_FLOAT_OFFSET) | (tmp2 & ADT75_VALUE_FLOAT_MASK);
	if (tmp1 < 0)
		/* convert positive value to supplyment */
		data = (ADT75_VALUE_SIGN << 1) - data;
	data <<= ADT75_VALUE_OFFSET;
	data = swab16(data);

	ret = adt75_i2c_write(chip, bound_reg, (u8)data);
	if (ret)
		return -EIO;

	return ret;
}

static ssize_t adt75_show_t_os(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return adt75_show_t_bound(dev, attr,
			ADT75_T_OS, buf);
}

static inline ssize_t adt75_set_t_os(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	return adt75_set_t_bound(dev, attr,
			ADT75_T_OS, buf, len);
}

static ssize_t adt75_show_t_hyst(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return adt75_show_t_bound(dev, attr,
			ADT75_T_HYST, buf);
}

static inline ssize_t adt75_set_t_hyst(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	return adt75_set_t_bound(dev, attr,
			ADT75_T_HYST, buf, len);
}

IIO_EVENT_ATTR_SH(oti_mode, iio_event_adt75,
		adt75_show_oti_mode, adt75_set_oti_mode, 0);
IIO_EVENT_ATTR_SH(available_oti_modes, iio_event_adt75,
		adt75_show_available_oti_modes, NULL, 0);
IIO_EVENT_ATTR_SH(smbus_alart, iio_event_adt75,
		adt75_show_smbus_alart, adt75_set_smbus_alart, 0);
IIO_EVENT_ATTR_SH(fault_queue, iio_event_adt75,
		adt75_show_fault_queue, adt75_set_fault_queue, 0);
IIO_EVENT_ATTR_SH(t_os, iio_event_adt75,
		adt75_show_t_os, adt75_set_t_os, 0);
IIO_EVENT_ATTR_SH(t_hyst, iio_event_adt75,
		adt75_show_t_hyst, adt75_set_t_hyst, 0);

static struct attribute *adt75_event_attributes[] = {
	&iio_event_attr_oti_mode.dev_attr.attr,
	&iio_event_attr_available_oti_modes.dev_attr.attr,
	&iio_event_attr_smbus_alart.dev_attr.attr,
	&iio_event_attr_fault_queue.dev_attr.attr,
	&iio_event_attr_t_os.dev_attr.attr,
	&iio_event_attr_t_hyst.dev_attr.attr,
	NULL,
};

static struct attribute_group adt75_event_attribute_group = {
	.attrs = adt75_event_attributes,
};

/*
 * device probe and remove
 */

static int __devinit adt75_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct adt75_chip_info *chip;
	int ret = 0;

	chip = kzalloc(sizeof(struct adt75_chip_info), GFP_KERNEL);

	if (chip == NULL)
		return -ENOMEM;

	/* this is only used for device removal purposes */
	i2c_set_clientdata(client, chip);

	chip->client = client;
	chip->name = id->name;

	chip->indio_dev = iio_allocate_device();
	if (chip->indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_free_chip;
	}

	chip->indio_dev->dev.parent = &client->dev;
	chip->indio_dev->attrs = &adt75_attribute_group;
	chip->indio_dev->event_attrs = &adt75_event_attribute_group;
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
		 * The event handler list element refer to iio_event_adt75.
		 * All event attributes bind to the same event handler.
		 * So, only register event handler once.
		 */
		iio_add_event_to_list(&iio_event_adt75,
				&chip->indio_dev->interrupts[0]->ev_list);

		INIT_WORK(&chip->thresh_work, adt75_interrupt_bh);

		ret = adt75_i2c_read(chip, ADT75_CONFIG, &chip->config);
		if (ret) {
			ret = -EIO;
			goto error_unreg_irq;
		}

		/* set irq polarity low level */
		chip->config &= ~ADT75_OS_POLARITY;

		ret = adt75_i2c_write(chip, ADT75_CONFIG, chip->config);
		if (ret) {
			ret = -EIO;
			goto error_unreg_irq;
		}
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

static int __devexit adt75_remove(struct i2c_client *client)
{
	struct adt75_chip_info *chip = i2c_get_clientdata(client);
	struct iio_dev *indio_dev = chip->indio_dev;

	if (client->irq)
		iio_unregister_interrupt_line(indio_dev, 0);
	iio_device_unregister(indio_dev);
	iio_free_device(chip->indio_dev);
	kfree(chip);

	return 0;
}

static const struct i2c_device_id adt75_id[] = {
	{ "adt75", 0 },
	{}
};

MODULE_DEVICE_TABLE(i2c, adt75_id);

static struct i2c_driver adt75_driver = {
	.driver = {
		.name = "adt75",
	},
	.probe = adt75_probe,
	.remove = __devexit_p(adt75_remove),
	.id_table = adt75_id,
};

static __init int adt75_init(void)
{
	return i2c_add_driver(&adt75_driver);
}

static __exit void adt75_exit(void)
{
	i2c_del_driver(&adt75_driver);
}

MODULE_AUTHOR("Sonic Zhang <sonic.zhang@analog.com>");
MODULE_DESCRIPTION("Analog Devices ADT75 digital"
			" temperature sensor driver");
MODULE_LICENSE("GPL v2");

module_init(adt75_init);
module_exit(adt75_exit);
