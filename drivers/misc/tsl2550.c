/*
 *  tsl2550.c - Linux kernel modules for ambient light sensor
 *
 *  Copyright (C) 2007 Rodolfo Giometti <giometti@linux.it>
 *  Copyright (C) 2007 Eurotech S.p.A. <info@eurotech.it>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/mutex.h>

#define TSL2550_DRV_NAME	"tsl2550"
#define DRIVER_VERSION		"1.2"

/*
 * Defines
 */

#define TSL2550_POWER_DOWN		0x00
#define TSL2550_POWER_UP		0x03
#define TSL2550_STANDARD_RANGE		0x18
#define TSL2550_EXTENDED_RANGE		0x1d
#define TSL2550_READ_ADC0		0x43
#define TSL2550_READ_ADC1		0x83

/*
 * Structs
 */

struct tsl2550_data {
	struct i2c_client *client;
	struct mutex update_lock;

	unsigned int power_state:1;
	unsigned int operating_mode:1;
};

/*
 * Global data
 */

static const u8 TSL2550_MODE_RANGE[2] = {
	TSL2550_STANDARD_RANGE, TSL2550_EXTENDED_RANGE,
};

/*
 * Management functions
 */

static int tsl2550_set_operating_mode(struct i2c_client *client, int mode)
{
	struct tsl2550_data *data = i2c_get_clientdata(client);

	int ret = i2c_smbus_write_byte(client, TSL2550_MODE_RANGE[mode]);

	data->operating_mode = mode;

	return ret;
}

static int tsl2550_set_power_state(struct i2c_client *client, int state)
{
	struct tsl2550_data *data = i2c_get_clientdata(client);
	int ret;

	if (state == 0)
		ret = i2c_smbus_write_byte(client, TSL2550_POWER_DOWN);
	else {
		ret = i2c_smbus_write_byte(client, TSL2550_POWER_UP);

		/* On power up we should reset operating mode also... */
		tsl2550_set_operating_mode(client, data->operating_mode);
	}

	data->power_state = state;

	return ret;
}

static int tsl2550_get_adc_value(struct i2c_client *client, u8 cmd)
{
	int ret;

	ret = i2c_smbus_read_byte_data(client, cmd);
	if (ret < 0)
		return ret;
	if (!(ret & 0x80))
		return -EAGAIN;
	return ret & 0x7f;	/* remove the "valid" bit */
}

/*
 * LUX calculation
 */

#define	TSL2550_MAX_LUX		1846

static const u8 ratio_lut[] = {
	100, 100, 100, 100, 100, 100, 100, 100,
	100, 100, 100, 100, 100, 100, 99, 99,
	99, 99, 99, 99, 99, 99, 99, 99,
	99, 99, 99, 98, 98, 98, 98, 98,
	98, 98, 97, 97, 97, 97, 97, 96,
	96, 96, 96, 95, 95, 95, 94, 94,
	93, 93, 93, 92, 92, 91, 91, 90,
	89, 89, 88, 87, 87, 86, 85, 84,
	83, 82, 81, 80, 79, 78, 77, 75,
	74, 73, 71, 69, 68, 66, 64, 62,
	60, 58, 56, 54, 52, 49, 47, 44,
	42, 41, 40, 40, 39, 39, 38, 38,
	37, 37, 37, 36, 36, 36, 35, 35,
	35, 35, 34, 34, 34, 34, 33, 33,
	33, 33, 32, 32, 32, 32, 32, 31,
	31, 31, 31, 31, 30, 30, 30, 30,
	30,
};

static const u16 count_lut[] = {
	0, 1, 2, 3, 4, 5, 6, 7,
	8, 9, 10, 11, 12, 13, 14, 15,
	16, 18, 20, 22, 24, 26, 28, 30,
	32, 34, 36, 38, 40, 42, 44, 46,
	49, 53, 57, 61, 65, 69, 73, 77,
	81, 85, 89, 93, 97, 101, 105, 109,
	115, 123, 131, 139, 147, 155, 163, 171,
	179, 187, 195, 203, 211, 219, 227, 235,
	247, 263, 279, 295, 311, 327, 343, 359,
	375, 391, 407, 423, 439, 455, 471, 487,
	511, 543, 575, 607, 639, 671, 703, 735,
	767, 799, 831, 863, 895, 927, 959, 991,
	1039, 1103, 1167, 1231, 1295, 1359, 1423, 1487,
	1551, 1615, 1679, 1743, 1807, 1871, 1935, 1999,
	2095, 2223, 2351, 2479, 2607, 2735, 2863, 2991,
	3119, 3247, 3375, 3503, 3631, 3759, 3887, 4015,
};

/*
 * This function is described into Taos TSL2550 Designer's Notebook
 * pages 2, 3.
 */
static int tsl2550_calculate_lux(u8 ch0, u8 ch1)
{
	unsigned int lux;

	/* Look up count from channel values */
	u16 c0 = count_lut[ch0];
	u16 c1 = count_lut[ch1];

	/*
	 * Calculate ratio.
	 * Note: the "128" is a scaling factor
	 */
	u8 r = 128;

	/* Avoid division by 0 and count 1 cannot be greater than count 0 */
	if (c1 <= c0)
		if (c0) {
			r = c1 * 128 / c0;

			/* Calculate LUX */
			lux = ((c0 - c1) * ratio_lut[r]) / 256;
		} else
			lux = 0;
	else
		return -EAGAIN;

	/* LUX range check */
	return lux > TSL2550_MAX_LUX ? TSL2550_MAX_LUX : lux;
}

/*
 * SysFS support
 */

static ssize_t tsl2550_show_power_state(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tsl2550_data *data = i2c_get_clientdata(to_i2c_client(dev));

	return sprintf(buf, "%u\n", data->power_state);
}

static ssize_t tsl2550_store_power_state(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct tsl2550_data *data = i2c_get_clientdata(client);
	unsigned long val = simple_strtoul(buf, NULL, 10);
	int ret;

	if (val < 0 || val > 1)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	ret = tsl2550_set_power_state(client, val);
	mutex_unlock(&data->update_lock);

	if (ret < 0)
		return ret;

	return count;
}

static DEVICE_ATTR(power_state, S_IWUSR | S_IRUGO,
		   tsl2550_show_power_state, tsl2550_store_power_state);

static ssize_t tsl2550_show_operating_mode(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tsl2550_data *data = i2c_get_clientdata(to_i2c_client(dev));

	return sprintf(buf, "%u\n", data->operating_mode);
}

static ssize_t tsl2550_store_operating_mode(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct tsl2550_data *data = i2c_get_clientdata(client);
	unsigned long val = simple_strtoul(buf, NULL, 10);
	int ret;

	if (val < 0 || val > 1)
		return -EINVAL;

	if (data->power_state == 0)
		return -EBUSY;

	mutex_lock(&data->update_lock);
	ret = tsl2550_set_operating_mode(client, val);
	mutex_unlock(&data->update_lock);

	if (ret < 0)
		return ret;

	return count;
}

static DEVICE_ATTR(operating_mode, S_IWUSR | S_IRUGO,
		   tsl2550_show_operating_mode, tsl2550_store_operating_mode);

static ssize_t __tsl2550_show_lux(struct i2c_client *client, char *buf)
{
	struct tsl2550_data *data = i2c_get_clientdata(client);
	u8 ch0, ch1;
	int ret;

	ret = tsl2550_get_adc_value(client, TSL2550_READ_ADC0);
	if (ret < 0)
		return ret;
	ch0 = ret;

	ret = tsl2550_get_adc_value(client, TSL2550_READ_ADC1);
	if (ret < 0)
		return ret;
	ch1 = ret;

	/* Do the job */
	ret = tsl2550_calculate_lux(ch0, ch1);
	if (ret < 0)
		return ret;
	if (data->operating_mode == 1)
		ret *= 5;

	return sprintf(buf, "%d\n", ret);
}

static ssize_t tsl2550_show_lux1_input(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct tsl2550_data *data = i2c_get_clientdata(client);
	int ret;

	/* No LUX data if not operational */
	if (!data->power_state)
		return -EBUSY;

	mutex_lock(&data->update_lock);
	ret = __tsl2550_show_lux(client, buf);
	mutex_unlock(&data->update_lock);

	return ret;
}

static DEVICE_ATTR(lux1_input, S_IRUGO,
		   tsl2550_show_lux1_input, NULL);

static struct attribute *tsl2550_attributes[] = {
	&dev_attr_power_state.attr,
	&dev_attr_operating_mode.attr,
	&dev_attr_lux1_input.attr,
	NULL
};

static const struct attribute_group tsl2550_attr_group = {
	.attrs = tsl2550_attributes,
};

/*
 * Initialization function
 */

static int tsl2550_init_client(struct i2c_client *client)
{
	struct tsl2550_data *data = i2c_get_clientdata(client);
	int err;

	/*
	 * Probe the chip. To do so we try to power up the device and then to
	 * read back the 0x03 code
	 */
	err = i2c_smbus_read_byte_data(client, TSL2550_POWER_UP);
	if (err < 0)
		return err;
	if (err != TSL2550_POWER_UP)
		return -ENODEV;
	data->power_state = 1;

	/* Set the default operating mode */
	err = i2c_smbus_write_byte(client,
				   TSL2550_MODE_RANGE[data->operating_mode]);
	if (err < 0)
		return err;

	return 0;
}

/*
 * I2C init/probing/exit functions
 */

static struct i2c_driver tsl2550_driver;
static int __devinit tsl2550_probe(struct i2c_client *client,
				   const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct tsl2550_data *data;
	int *opmode, err = 0;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_WRITE_BYTE
					    | I2C_FUNC_SMBUS_READ_BYTE_DATA)) {
		err = -EIO;
		goto exit;
	}

	data = kzalloc(sizeof(struct tsl2550_data), GFP_KERNEL);
	if (!data) {
		err = -ENOMEM;
		goto exit;
	}
	data->client = client;
	i2c_set_clientdata(client, data);

	/* Check platform data */
	opmode = client->dev.platform_data;
	if (opmode) {
		if (*opmode < 0 || *opmode > 1) {
			dev_err(&client->dev, "invalid operating_mode (%d)\n",
					*opmode);
			err = -EINVAL;
			goto exit_kfree;
		}
		data->operating_mode = *opmode;
	} else
		data->operating_mode = 0;	/* default mode is standard */
	dev_info(&client->dev, "%s operating mode\n",
			data->operating_mode ? "extended" : "standard");

	mutex_init(&data->update_lock);

	/* Initialize the TSL2550 chip */
	err = tsl2550_init_client(client);
	if (err)
		goto exit_kfree;

	/* Register sysfs hooks */
	err = sysfs_create_group(&client->dev.kobj, &tsl2550_attr_group);
	if (err)
		goto exit_kfree;

	dev_info(&client->dev, "support ver. %s enabled\n", DRIVER_VERSION);

	return 0;

exit_kfree:
	kfree(data);
exit:
	return err;
}

static int __devexit tsl2550_remove(struct i2c_client *client)
{
	sysfs_remove_group(&client->dev.kobj, &tsl2550_attr_group);

	/* Power down the device */
	tsl2550_set_power_state(client, 0);

	kfree(i2c_get_clientdata(client));

	return 0;
}

#ifdef CONFIG_PM

static int tsl2550_suspend(struct i2c_client *client, pm_message_t mesg)
{
	return tsl2550_set_power_state(client, 0);
}

static int tsl2550_resume(struct i2c_client *client)
{
	return tsl2550_set_power_state(client, 1);
}

#else

#define tsl2550_suspend		NULL
#define tsl2550_resume		NULL

#endif /* CONFIG_PM */

static const struct i2c_device_id tsl2550_id[] = {
	{ "tsl2550", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tsl2550_id);

static struct i2c_driver tsl2550_driver = {
	.driver = {
		.name	= TSL2550_DRV_NAME,
		.owner	= THIS_MODULE,
	},
	.suspend = tsl2550_suspend,
	.resume	= tsl2550_resume,
	.probe	= tsl2550_probe,
	.remove	= __devexit_p(tsl2550_remove),
	.id_table = tsl2550_id,
};

static int __init tsl2550_init(void)
{
	return i2c_add_driver(&tsl2550_driver);
}

static void __exit tsl2550_exit(void)
{
	i2c_del_driver(&tsl2550_driver);
}

MODULE_AUTHOR("Rodolfo Giometti <giometti@linux.it>");
MODULE_DESCRIPTION("TSL2550 ambient light sensor driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRIVER_VERSION);

module_init(tsl2550_init);
module_exit(tsl2550_exit);
