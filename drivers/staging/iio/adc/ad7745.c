/*
 * AD774X capacitive sensor driver supporting AD7745/6/7
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
 * AD774X registers definition
 */

#define AD774X_STATUS		0
#define AD774X_STATUS_RDY	(1 << 2)
#define AD774X_STATUS_RDYVT	(1 << 1)
#define AD774X_STATUS_RDYCAP	(1 << 0)
#define AD774X_CAP_DATA_HIGH	1
#define AD774X_CAP_DATA_MID	2
#define AD774X_CAP_DATA_LOW	3
#define AD774X_VT_DATA_HIGH	4
#define AD774X_VT_DATA_MID	5
#define AD774X_VT_DATA_LOW	6
#define AD774X_CAP_SETUP	7
#define AD774X_VT_SETUP		8
#define AD774X_EXEC_SETUP	9
#define AD774X_CFG		10
#define AD774X_CAPDACA		11
#define AD774X_CAPDACB		12
#define AD774X_CAPDAC_EN	(1 << 7)
#define AD774X_CAP_OFFH		13
#define AD774X_CAP_OFFL		14
#define AD774X_CAP_GAINH	15
#define AD774X_CAP_GAINL	16
#define AD774X_VOLT_GAINH	17
#define AD774X_VOLT_GAINL	18

#define AD774X_MAX_CONV_MODE	6

/*
 * struct ad774x_chip_info - chip specifc information
 */

struct ad774x_chip_info {
	const char *name;
	struct i2c_client *client;
	struct iio_dev *indio_dev;
	struct work_struct thresh_work;
	bool inter;
	s64 last_timestamp;
	u16 cap_offs;                   /* Capacitive offset */
	u16 cap_gain;                   /* Capacitive gain calibration */
	u16 volt_gain;                  /* Voltage gain calibration */
	u8  cap_setup;
	u8  vt_setup;
	u8  exec_setup;

	char *conversion_mode;
};

struct ad774x_conversion_mode {
	char *name;
	u8 reg_cfg;
};

struct ad774x_conversion_mode ad774x_conv_mode_table[AD774X_MAX_CONV_MODE] = {
	{ "idle", 0 },
	{ "continuous-conversion", 1 },
	{ "single-conversion", 2 },
	{ "power-down", 3 },
	{ "offset-calibration", 5 },
	{ "gain-calibration", 6 },
};

/*
 * ad774x register access by I2C
 */

static int ad774x_i2c_read(struct ad774x_chip_info *chip, u8 reg, u8 *data, int len)
{
	struct i2c_client *client = chip->client;
	int ret;

	ret = i2c_master_send(client, &reg, 1);
	if (ret < 0) {
		dev_err(&client->dev, "I2C write error\n");
		return ret;
	}

	ret = i2c_master_recv(client, data, len);
	if (ret < 0) {
		dev_err(&client->dev, "I2C read error\n");
		return ret;
	}

	return ret;
}

static int ad774x_i2c_write(struct ad774x_chip_info *chip, u8 reg, u8 data)
{
	struct i2c_client *client = chip->client;
	int ret;

	u8 tx[2] = {
		reg,
		data,
	};

	ret = i2c_master_send(client, tx, 2);
	if (ret < 0)
		dev_err(&client->dev, "I2C write error\n");

	return ret;
}

/*
 * sysfs nodes
 */

#define IIO_DEV_ATTR_AVAIL_CONVERSION_MODES(_show)				\
	IIO_DEVICE_ATTR(available_conversion_modes, S_IRUGO, _show, NULL, 0)
#define IIO_DEV_ATTR_CONVERSION_MODE(_mode, _show, _store)              \
	IIO_DEVICE_ATTR(conversion_mode, _mode, _show, _store, 0)
#define IIO_DEV_ATTR_CAP_SETUP(_mode, _show, _store)		\
	IIO_DEVICE_ATTR(cap_setup, _mode, _show, _store, 0)
#define IIO_DEV_ATTR_VT_SETUP(_mode, _show, _store)              \
	IIO_DEVICE_ATTR(in0_setup, _mode, _show, _store, 0)
#define IIO_DEV_ATTR_EXEC_SETUP(_mode, _show, _store)              \
	IIO_DEVICE_ATTR(exec_setup, _mode, _show, _store, 0)
#define IIO_DEV_ATTR_VOLT_GAIN(_mode, _show, _store)		\
	IIO_DEVICE_ATTR(in0_gain, _mode, _show, _store, 0)
#define IIO_DEV_ATTR_CAP_OFFS(_mode, _show, _store)		\
	IIO_DEVICE_ATTR(cap_offs, _mode, _show, _store, 0)
#define IIO_DEV_ATTR_CAP_GAIN(_mode, _show, _store)		\
	IIO_DEVICE_ATTR(cap_gain, _mode, _show, _store, 0)
#define IIO_DEV_ATTR_CAP_DATA(_show)		\
	IIO_DEVICE_ATTR(cap0_raw, S_IRUGO, _show, NULL, 0)
#define IIO_DEV_ATTR_VT_DATA(_show)		\
	IIO_DEVICE_ATTR(in0_raw, S_IRUGO, _show, NULL, 0)

static ssize_t ad774x_show_conversion_modes(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	int i;
	int len = 0;

	for (i = 0; i < AD774X_MAX_CONV_MODE; i++)
		len += sprintf(buf + len, "%s ", ad774x_conv_mode_table[i].name);

	len += sprintf(buf + len, "\n");

	return len;
}

static IIO_DEV_ATTR_AVAIL_CONVERSION_MODES(ad774x_show_conversion_modes);

static ssize_t ad774x_show_conversion_mode(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad774x_chip_info *chip = dev_info->dev_data;

	return sprintf(buf, "%s\n", chip->conversion_mode);
}

static ssize_t ad774x_store_conversion_mode(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad774x_chip_info *chip = dev_info->dev_data;
	u8 cfg;
	int i;

	ad774x_i2c_read(chip, AD774X_CFG, &cfg, 1);

	for (i = 0; i < AD774X_MAX_CONV_MODE; i++) {
		if (strncmp(buf, ad774x_conv_mode_table[i].name,
				strlen(ad774x_conv_mode_table[i].name) - 1) == 0) {
			chip->conversion_mode = ad774x_conv_mode_table[i].name;
			cfg |= 0x18 | ad774x_conv_mode_table[i].reg_cfg;
			ad774x_i2c_write(chip, AD774X_CFG, cfg);
			return len;
		}
	}

	dev_err(dev, "not supported conversion mode\n");

	return -EINVAL;
}

static IIO_DEV_ATTR_CONVERSION_MODE(S_IRUGO | S_IWUSR,
		ad774x_show_conversion_mode,
		ad774x_store_conversion_mode);

static ssize_t ad774x_show_dac_value(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad774x_chip_info *chip = dev_info->dev_data;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	u8 data;

	ad774x_i2c_read(chip, this_attr->address, &data, 1);

	return sprintf(buf, "%02x\n", data & 0x7F);
}

static ssize_t ad774x_store_dac_value(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad774x_chip_info *chip = dev_info->dev_data;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	unsigned long data;
	int ret;

	ret = strict_strtoul(buf, 10, &data);

	if (!ret) {
		ad774x_i2c_write(chip, this_attr->address,
			(data ? AD774X_CAPDAC_EN : 0) | (data & 0x7F));
		return len;
	}

	return -EINVAL;
}

static IIO_DEVICE_ATTR(capdac0_raw, S_IRUGO | S_IWUSR,
			ad774x_show_dac_value,
			ad774x_store_dac_value,
			AD774X_CAPDACA);

static IIO_DEVICE_ATTR(capdac1_raw, S_IRUGO | S_IWUSR,
			ad774x_show_dac_value,
			ad774x_store_dac_value,
			AD774X_CAPDACB);

static ssize_t ad774x_show_cap_setup(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad774x_chip_info *chip = dev_info->dev_data;

	return sprintf(buf, "0x%02x\n", chip->cap_setup);
}

static ssize_t ad774x_store_cap_setup(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad774x_chip_info *chip = dev_info->dev_data;
	unsigned long data;
	int ret;

	ret = strict_strtoul(buf, 10, &data);

	if ((!ret) && (data < 0x100)) {
		ad774x_i2c_write(chip, AD774X_CAP_SETUP, data);
		chip->cap_setup = data;
		return len;
	}

	return -EINVAL;
}

static IIO_DEV_ATTR_CAP_SETUP(S_IRUGO | S_IWUSR,
		ad774x_show_cap_setup,
		ad774x_store_cap_setup);

static ssize_t ad774x_show_vt_setup(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad774x_chip_info *chip = dev_info->dev_data;

	return sprintf(buf, "0x%02x\n", chip->vt_setup);
}

static ssize_t ad774x_store_vt_setup(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad774x_chip_info *chip = dev_info->dev_data;
	unsigned long data;
	int ret;

	ret = strict_strtoul(buf, 10, &data);

	if ((!ret) && (data < 0x100)) {
		ad774x_i2c_write(chip, AD774X_VT_SETUP, data);
		chip->vt_setup = data;
		return len;
	}

	return -EINVAL;
}

static IIO_DEV_ATTR_VT_SETUP(S_IRUGO | S_IWUSR,
		ad774x_show_vt_setup,
		ad774x_store_vt_setup);

static ssize_t ad774x_show_exec_setup(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad774x_chip_info *chip = dev_info->dev_data;

	return sprintf(buf, "0x%02x\n", chip->exec_setup);
}

static ssize_t ad774x_store_exec_setup(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad774x_chip_info *chip = dev_info->dev_data;
	unsigned long data;
	int ret;

	ret = strict_strtoul(buf, 10, &data);

	if ((!ret) && (data < 0x100)) {
		ad774x_i2c_write(chip, AD774X_EXEC_SETUP, data);
		chip->exec_setup = data;
		return len;
	}

	return -EINVAL;
}

static IIO_DEV_ATTR_EXEC_SETUP(S_IRUGO | S_IWUSR,
		ad774x_show_exec_setup,
		ad774x_store_exec_setup);

static ssize_t ad774x_show_volt_gain(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad774x_chip_info *chip = dev_info->dev_data;

	return sprintf(buf, "%d\n", chip->volt_gain);
}

static ssize_t ad774x_store_volt_gain(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad774x_chip_info *chip = dev_info->dev_data;
	unsigned long data;
	int ret;

	ret = strict_strtoul(buf, 10, &data);

	if ((!ret) && (data < 0x10000)) {
		ad774x_i2c_write(chip, AD774X_VOLT_GAINH, data >> 8);
		ad774x_i2c_write(chip, AD774X_VOLT_GAINL, data);
		chip->volt_gain = data;
		return len;
	}

	return -EINVAL;
}

static IIO_DEV_ATTR_VOLT_GAIN(S_IRUGO | S_IWUSR,
		ad774x_show_volt_gain,
		ad774x_store_volt_gain);

static ssize_t ad774x_show_cap_data(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad774x_chip_info *chip = dev_info->dev_data;
	unsigned long data;
	char tmp[3];

	ad774x_i2c_read(chip, AD774X_CAP_DATA_HIGH, tmp, 3);
	data = ((int)tmp[0] << 16) | ((int)tmp[1] << 8) | (int)tmp[2];

	return sprintf(buf, "%ld\n", data);
}

static IIO_DEV_ATTR_CAP_DATA(ad774x_show_cap_data);

static ssize_t ad774x_show_vt_data(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad774x_chip_info *chip = dev_info->dev_data;
	unsigned long data;
	char tmp[3];

	ad774x_i2c_read(chip, AD774X_VT_DATA_HIGH, tmp, 3);
	data = ((int)tmp[0] << 16) | ((int)tmp[1] << 8) | (int)tmp[2];

	return sprintf(buf, "%ld\n", data);
}

static IIO_DEV_ATTR_VT_DATA(ad774x_show_vt_data);

static ssize_t ad774x_show_cap_offs(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad774x_chip_info *chip = dev_info->dev_data;

	return sprintf(buf, "%d\n", chip->cap_offs);
}

static ssize_t ad774x_store_cap_offs(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad774x_chip_info *chip = dev_info->dev_data;
	unsigned long data;
	int ret;

	ret = strict_strtoul(buf, 10, &data);

	if ((!ret) && (data < 0x10000)) {
		ad774x_i2c_write(chip, AD774X_CAP_OFFH, data >> 8);
		ad774x_i2c_write(chip, AD774X_CAP_OFFL, data);
		chip->cap_offs = data;
		return len;
	}

	return -EINVAL;
}

static IIO_DEV_ATTR_CAP_OFFS(S_IRUGO | S_IWUSR,
		ad774x_show_cap_offs,
		ad774x_store_cap_offs);

static ssize_t ad774x_show_cap_gain(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad774x_chip_info *chip = dev_info->dev_data;

	return sprintf(buf, "%d\n", chip->cap_gain);
}

static ssize_t ad774x_store_cap_gain(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad774x_chip_info *chip = dev_info->dev_data;
	unsigned long data;
	int ret;

	ret = strict_strtoul(buf, 10, &data);

	if ((!ret) && (data < 0x10000)) {
		ad774x_i2c_write(chip, AD774X_CAP_GAINH, data >> 8);
		ad774x_i2c_write(chip, AD774X_CAP_GAINL, data);
		chip->cap_gain = data;
		return len;
	}

	return -EINVAL;
}

static IIO_DEV_ATTR_CAP_GAIN(S_IRUGO | S_IWUSR,
		ad774x_show_cap_gain,
		ad774x_store_cap_gain);

static ssize_t ad774x_show_name(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad774x_chip_info *chip = dev_info->dev_data;
	return sprintf(buf, "%s\n", chip->name);
}

static IIO_DEVICE_ATTR(name, S_IRUGO, ad774x_show_name, NULL, 0);

static struct attribute *ad774x_attributes[] = {
	&iio_dev_attr_available_conversion_modes.dev_attr.attr,
	&iio_dev_attr_conversion_mode.dev_attr.attr,
	&iio_dev_attr_cap_setup.dev_attr.attr,
	&iio_dev_attr_in0_setup.dev_attr.attr,
	&iio_dev_attr_exec_setup.dev_attr.attr,
	&iio_dev_attr_cap_offs.dev_attr.attr,
	&iio_dev_attr_cap_gain.dev_attr.attr,
	&iio_dev_attr_in0_gain.dev_attr.attr,
	&iio_dev_attr_in0_raw.dev_attr.attr,
	&iio_dev_attr_cap0_raw.dev_attr.attr,
	&iio_dev_attr_capdac0_raw.dev_attr.attr,
	&iio_dev_attr_capdac1_raw.dev_attr.attr,
	&iio_dev_attr_name.dev_attr.attr,
	NULL,
};

static const struct attribute_group ad774x_attribute_group = {
	.attrs = ad774x_attributes,
};

/*
 * data ready events
 */

#define IIO_EVENT_CODE_CAP_RDY     IIO_BUFFER_EVENT_CODE(0)
#define IIO_EVENT_CODE_VT_RDY      IIO_BUFFER_EVENT_CODE(1)

#define IIO_EVENT_ATTR_CAP_RDY_SH(_evlist, _show, _store, _mask)	\
	IIO_EVENT_ATTR_SH(cap_rdy, _evlist, _show, _store, _mask)

#define IIO_EVENT_ATTR_VT_RDY_SH(_evlist, _show, _store, _mask)	\
	IIO_EVENT_ATTR_SH(vt_rdy, _evlist, _show, _store, _mask)

static void ad774x_interrupt_handler_bh(struct work_struct *work_s)
{
	struct ad774x_chip_info *chip =
		container_of(work_s, struct ad774x_chip_info, thresh_work);
	u8 int_status;

	enable_irq(chip->client->irq);

	ad774x_i2c_read(chip, AD774X_STATUS, &int_status, 1);

	if (int_status & AD774X_STATUS_RDYCAP)
		iio_push_event(chip->indio_dev, 0,
				IIO_EVENT_CODE_CAP_RDY,
				chip->last_timestamp);

	if (int_status & AD774X_STATUS_RDYVT)
		iio_push_event(chip->indio_dev, 0,
				IIO_EVENT_CODE_VT_RDY,
				chip->last_timestamp);
}

static int ad774x_interrupt_handler_th(struct iio_dev *dev_info,
		int index,
		s64 timestamp,
		int no_test)
{
	struct ad774x_chip_info *chip = dev_info->dev_data;

	chip->last_timestamp = timestamp;
	schedule_work(&chip->thresh_work);

	return 0;
}

IIO_EVENT_SH(data_rdy, &ad774x_interrupt_handler_th);

static ssize_t ad774x_query_out_mode(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	/*
	 * AD774X provides one /RDY pin, which can be used as interrupt
	 * but the pin is not configurable
	 */
	return sprintf(buf, "1\n");
}

static ssize_t ad774x_set_out_mode(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	return len;
}

IIO_EVENT_ATTR_CAP_RDY_SH(iio_event_data_rdy, ad774x_query_out_mode, ad774x_set_out_mode, 0);
IIO_EVENT_ATTR_VT_RDY_SH(iio_event_data_rdy, ad774x_query_out_mode, ad774x_set_out_mode, 0);

static struct attribute *ad774x_event_attributes[] = {
	&iio_event_attr_cap_rdy.dev_attr.attr,
	&iio_event_attr_vt_rdy.dev_attr.attr,
	NULL,
};

static struct attribute_group ad774x_event_attribute_group = {
	.attrs = ad774x_event_attributes,
};

/*
 * device probe and remove
 */

static int __devinit ad774x_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	int ret = 0, regdone = 0;
	struct ad774x_chip_info *chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (chip == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}

	/* this is only used for device removal purposes */
	i2c_set_clientdata(client, chip);

	chip->client = client;
	chip->name = id->name;

	chip->indio_dev = iio_allocate_device();
	if (chip->indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_free_chip;
	}

	/* Establish that the iio_dev is a child of the i2c device */
	chip->indio_dev->dev.parent = &client->dev;
	chip->indio_dev->attrs = &ad774x_attribute_group;
	chip->indio_dev->event_attrs = &ad774x_event_attribute_group;
	chip->indio_dev->dev_data = (void *)(chip);
	chip->indio_dev->driver_module = THIS_MODULE;
	chip->indio_dev->num_interrupt_lines = 1;
	chip->indio_dev->modes = INDIO_DIRECT_MODE;

	ret = iio_device_register(chip->indio_dev);
	if (ret)
		goto error_free_dev;
	regdone = 1;

	if (client->irq) {
		ret = iio_register_interrupt_line(client->irq,
				chip->indio_dev,
				0,
				IRQF_TRIGGER_FALLING,
				"ad774x");
		if (ret)
			goto error_free_dev;

		iio_add_event_to_list(iio_event_attr_cap_rdy.listel,
				&chip->indio_dev->interrupts[0]->ev_list);

		INIT_WORK(&chip->thresh_work, ad774x_interrupt_handler_bh);
	}

	dev_err(&client->dev, "%s capacitive sensor registered, irq: %d\n", id->name, client->irq);

	return 0;

error_free_dev:
	if (regdone)
		iio_device_unregister(chip->indio_dev);
	else
		iio_free_device(chip->indio_dev);
error_free_chip:
	kfree(chip);
error_ret:
	return ret;
}

static int __devexit ad774x_remove(struct i2c_client *client)
{
	struct ad774x_chip_info *chip = i2c_get_clientdata(client);
	struct iio_dev *indio_dev = chip->indio_dev;

	if (client->irq)
		iio_unregister_interrupt_line(indio_dev, 0);
	iio_device_unregister(indio_dev);
	kfree(chip);

	return 0;
}

static const struct i2c_device_id ad774x_id[] = {
	{ "ad7745", 0 },
	{ "ad7746", 0 },
	{ "ad7747", 0 },
	{}
};

MODULE_DEVICE_TABLE(i2c, ad774x_id);

static struct i2c_driver ad774x_driver = {
	.driver = {
		.name = "ad774x",
	},
	.probe = ad774x_probe,
	.remove = __devexit_p(ad774x_remove),
	.id_table = ad774x_id,
};

static __init int ad774x_init(void)
{
	return i2c_add_driver(&ad774x_driver);
}

static __exit void ad774x_exit(void)
{
	i2c_del_driver(&ad774x_driver);
}

MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_DESCRIPTION("Analog Devices ad7745/6/7 capacitive sensor driver");
MODULE_LICENSE("GPL v2");

module_init(ad774x_init);
module_exit(ad774x_exit);
