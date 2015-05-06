/*
 *
 * Copyright (C) 2011-2014 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/input-polldev.h>
#include <linux/hwmon.h>
#include <linux/input.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#define MAG3110_DRV_NAME       "mag3110"
#define MAG3110_ID		(0xC4)
#define MAG3110_XYZ_DATA_LEN	(6)
#define MAG3110_STATUS_ZYXDR	(0x08)
#define MAG3110_AC_MASK		(0x01)
#define MAG3110_AC_OFFSET	(0)
#define MAG3110_DR_MODE_MASK	(0x7 << 5)
#define MAG3110_DR_MODE_OFFSET	(5)

#define POLL_INTERVAL_MAX	(500)
#define POLL_INTERVAL		(100)
#define INT_TIMEOUT		(1000)
#define DEFAULT_POSITION	(2)

/* register enum for mag3110 registers */
enum {
	MAG3110_DR_STATUS = 0x00,
	MAG3110_OUT_X_MSB,
	MAG3110_OUT_X_LSB,
	MAG3110_OUT_Y_MSB,
	MAG3110_OUT_Y_LSB,
	MAG3110_OUT_Z_MSB,
	MAG3110_OUT_Z_LSB,
	MAG3110_WHO_AM_I,

	MAG3110_OFF_X_MSB,
	MAG3110_OFF_X_LSB,
	MAG3110_OFF_Y_MSB,
	MAG3110_OFF_Y_LSB,
	MAG3110_OFF_Z_MSB,
	MAG3110_OFF_Z_LSB,

	MAG3110_DIE_TEMP,

	MAG3110_CTRL_REG1 = 0x10,
	MAG3110_CTRL_REG2,
};

enum {
	MAG_STANDBY,
	MAG_ACTIVED
};

struct mag3110_data {
	struct i2c_client *client;
	struct input_polled_dev *poll_dev;
	struct device *hwmon_dev;
	wait_queue_head_t waitq;
	bool data_ready;
	u8 ctl_reg1;
	int active;
	int position;
	int use_irq;
};

static short MAGHAL[8][3][3] = {
	{ {0, 1, 0}, {-1, 0, 0}, {0, 0, 1} },
	{ {1, 0, 0}, {0, 1, 0}, {0, 0, 1} },
	{ {0, -1, 0}, {1, 0, 0}, {0, 0, 1} },
	{ {-1, 0, 0}, {0, -1, 0}, {0, 0, 1} },

	{ {0, 1, 0}, {1, 0, 0}, {0, 0, -1} },
	{ {1, 0, 0}, {0, -1, 0}, {0, 0, -1} },
	{ {0, -1, 0}, {-1, 0, 0}, {0, 0, -1} },
	{ {-1, 0, 0}, {0, 1, 0}, {0, 0, -1} },
};

static struct mag3110_data *mag3110_pdata;
static DEFINE_MUTEX(mag3110_lock);

/*
 * This function do one mag3110 register read.
 */
static int mag3110_adjust_position(short *x, short *y, short *z)
{
	short rawdata[3], data[3];
	int i, j;
	int position = mag3110_pdata->position;
	if (position < 0 || position > 7)
		position = 0;
	rawdata[0] = *x;
	rawdata[1] = *y;
	rawdata[2] = *z;
	for (i = 0; i < 3; i++) {
		data[i] = 0;
		for (j = 0; j < 3; j++)
			data[i] += rawdata[j] * MAGHAL[position][i][j];
	}
	*x = data[0];
	*y = data[1];
	*z = data[2];
	return 0;
}

static int mag3110_read_reg(struct i2c_client *client, u8 reg)
{
	return i2c_smbus_read_byte_data(client, reg);
}

/*
 * This function do one mag3110 register write.
 */
static int mag3110_write_reg(struct i2c_client *client, u8 reg, char value)
{
	int ret;

	ret = i2c_smbus_write_byte_data(client, reg, value);
	if (ret < 0)
		dev_err(&client->dev, "i2c write failed\n");
	return ret;
}

/*
 * This function do multiple mag3110 registers read.
 */
static int mag3110_read_block_data(struct i2c_client *client, u8 reg,
				   int count, u8 *addr)
{
	if (i2c_smbus_read_i2c_block_data(client, reg, count, addr) < count) {
		dev_err(&client->dev, "i2c block read failed\n");
		return -1;
	}

	return count;
}

/*
 * Initialization function
 */
static int mag3110_init_client(struct i2c_client *client)
{
	int val, ret;

	/* enable automatic resets */
	val = 0x80;
	ret = mag3110_write_reg(client, MAG3110_CTRL_REG2, val);

	/* set default data rate to 10HZ */
	val = mag3110_read_reg(client, MAG3110_CTRL_REG1);
	val |= (0x0 << MAG3110_DR_MODE_OFFSET);
	ret = mag3110_write_reg(client, MAG3110_CTRL_REG1, val);

	return ret;
}

/*
 * read sensor data from mag3110
 */
static int mag3110_read_data(short *x, short *y, short *z)
{
	struct mag3110_data *data;
	u8 tmp_data[MAG3110_XYZ_DATA_LEN];
	int retry = 3;
	int result;

	if (!mag3110_pdata || mag3110_pdata->active == MAG_STANDBY)
		return -EINVAL;

	data = mag3110_pdata;
	if (data->use_irq && !wait_event_interruptible_timeout
	    (data->waitq, data->data_ready != 0,
	     msecs_to_jiffies(INT_TIMEOUT))) {
		dev_dbg(&data->client->dev, "interrupt not received\n");
		return -ETIME;
	}

	do {
		msleep(1);
		result = i2c_smbus_read_byte_data(data->client,
						  MAG3110_DR_STATUS);
		retry--;
	} while (!(result & MAG3110_STATUS_ZYXDR) && retry > 0);
	/* Clear data_ready flag after data is read out */
	if (retry == 0)
		return -EINVAL;

	data->data_ready = 0;

	while (i2c_smbus_read_byte_data(data->client, MAG3110_DR_STATUS)) {
		if (mag3110_read_block_data(data->client,
				    MAG3110_OUT_X_MSB, MAG3110_XYZ_DATA_LEN,
				    tmp_data) < 0)
			return -1;
	}

	*x = ((tmp_data[0] << 8) & 0xff00) | tmp_data[1];
	*y = ((tmp_data[2] << 8) & 0xff00) | tmp_data[3];
	*z = ((tmp_data[4] << 8) & 0xff00) | tmp_data[5];

	return 0;
}

static void report_abs(void)
{
	struct input_dev *idev;
	short x, y, z;

	mutex_lock(&mag3110_lock);
	if (mag3110_read_data(&x, &y, &z) != 0)
		goto out;
	mag3110_adjust_position(&x, &y, &z);
	idev = mag3110_pdata->poll_dev->input;
	input_report_abs(idev, ABS_X, x);
	input_report_abs(idev, ABS_Y, y);
	input_report_abs(idev, ABS_Z, z);
	input_sync(idev);
out:
	mutex_unlock(&mag3110_lock);
}

static void mag3110_dev_poll(struct input_polled_dev *dev)
{
	report_abs();
}

static irqreturn_t mag3110_irq_handler(int irq, void *dev_id)
{
	int result;
	u8 tmp_data[MAG3110_XYZ_DATA_LEN];
	result = i2c_smbus_read_byte_data(mag3110_pdata->client,
						  MAG3110_DR_STATUS);
	if (!(result & MAG3110_STATUS_ZYXDR))
		return IRQ_NONE;

	mag3110_pdata->data_ready = 1;

	if (mag3110_pdata->active == MAG_STANDBY)
		/*
		 * Since the mode will be changed, sometimes irq will
		 * be handled in StandBy mode because of interrupt latency.
		 * So just clear the interrutp flag via reading block data.
		 */
		mag3110_read_block_data(mag3110_pdata->client,
					MAG3110_OUT_X_MSB,
					MAG3110_XYZ_DATA_LEN, tmp_data);
	else
		wake_up_interruptible(&mag3110_pdata->waitq);

	return IRQ_HANDLED;
}

static ssize_t mag3110_enable_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct i2c_client *client;
	int val;
	mutex_lock(&mag3110_lock);
	client = mag3110_pdata->client;
	val = mag3110_read_reg(client, MAG3110_CTRL_REG1) & MAG3110_AC_MASK;

	mutex_unlock(&mag3110_lock);
	return sprintf(buf, "%d\n", val);
}

static ssize_t mag3110_enable_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct i2c_client *client;
	int reg, ret;
	long enable;
	u8 tmp_data[MAG3110_XYZ_DATA_LEN];

	ret = kstrtol(buf, 10, &enable);
	if (ret) {
		dev_err(dev, "string to long error\n");
		return ret;
	}

	mutex_lock(&mag3110_lock);
	client = mag3110_pdata->client;

	reg = mag3110_read_reg(client, MAG3110_CTRL_REG1);
	if (enable && mag3110_pdata->active == MAG_STANDBY) {
		reg |= MAG3110_AC_MASK;
		ret = mag3110_write_reg(client, MAG3110_CTRL_REG1, reg);
		if (!ret)
			mag3110_pdata->active = MAG_ACTIVED;
	} else if (!enable && mag3110_pdata->active == MAG_ACTIVED) {
		reg &= ~MAG3110_AC_MASK;
		ret = mag3110_write_reg(client, MAG3110_CTRL_REG1, reg);
		if (!ret)
			mag3110_pdata->active = MAG_STANDBY;
	}

	/* Read out MSB data to clear interrupt flag */
	msleep(100);
	mag3110_read_block_data(mag3110_pdata->client, MAG3110_OUT_X_MSB,
					MAG3110_XYZ_DATA_LEN, tmp_data);
	mutex_unlock(&mag3110_lock);
	return count;
}

static DEVICE_ATTR(enable, S_IWUSR | S_IRUGO,
		   mag3110_enable_show, mag3110_enable_store);

static ssize_t mag3110_dr_mode_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct i2c_client *client;
	int val;

	client = mag3110_pdata->client;
	val = (mag3110_read_reg(client, MAG3110_CTRL_REG1)
	       & MAG3110_DR_MODE_MASK) >> MAG3110_DR_MODE_OFFSET;

	return sprintf(buf, "%d\n", val);
}

static ssize_t mag3110_dr_mode_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct i2c_client *client;
	int reg, ret;
	unsigned long val;

	/* This must be done when mag3110 is disabled */
	if ((kstrtoul(buf, 10, &val) < 0) || (val > 7))
		return -EINVAL;

	client = mag3110_pdata->client;
	reg = mag3110_read_reg(client, MAG3110_CTRL_REG1) &
	    ~MAG3110_DR_MODE_MASK;
	reg |= (val << MAG3110_DR_MODE_OFFSET);
	/* MAG3110_CTRL_REG1 bit 5-7: data rate mode */
	ret = mag3110_write_reg(client, MAG3110_CTRL_REG1, reg);
	if (ret < 0)
		return ret;

	return count;
}

static DEVICE_ATTR(dr_mode, S_IWUSR | S_IRUGO,
		   mag3110_dr_mode_show, mag3110_dr_mode_store);

static ssize_t mag3110_position_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	int val;
	mutex_lock(&mag3110_lock);
	val = mag3110_pdata->position;
	mutex_unlock(&mag3110_lock);
	return sprintf(buf, "%d\n", val);
}

static ssize_t mag3110_position_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	long position;
	int ret;
	ret = kstrtol(buf, 10, &position);
	if (ret) {
		dev_err(dev, "string to long error\n");
		return ret;
	}

	mutex_lock(&mag3110_lock);
	mag3110_pdata->position = (int)position;
	mutex_unlock(&mag3110_lock);
	return count;
}

static DEVICE_ATTR(position, S_IWUSR | S_IRUGO,
		   mag3110_position_show, mag3110_position_store);

static struct attribute *mag3110_attributes[] = {
	&dev_attr_enable.attr,
	&dev_attr_dr_mode.attr,
	&dev_attr_position.attr,
	NULL
};

static const struct attribute_group mag3110_attr_group = {
	.attrs = mag3110_attributes,
};

static int mag3110_probe(struct i2c_client *client,
				   const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter;
	struct input_dev *idev = NULL;
	struct mag3110_data *data;
	int ret = 0;
	struct regulator *vdd, *vdd_io;
	u32 pos = 0;
	struct device_node *of_node = client->dev.of_node;
	u32 irq_flag;
	struct irq_data *irq_data = NULL;
	bool shared_irq = of_property_read_bool(of_node, "shared-interrupt");

	vdd = NULL;
	vdd_io = NULL;

	vdd = devm_regulator_get(&client->dev, "vdd");
	if (!IS_ERR(vdd)) {
		ret  = regulator_enable(vdd);
		if (ret) {
			dev_err(&client->dev, "vdd set voltage error\n");
			return ret;
		}
	}

	vdd_io = devm_regulator_get(&client->dev, "vddio");
	if (!IS_ERR(vdd_io)) {
		ret = regulator_enable(vdd_io);
		if (ret) {
			dev_err(&client->dev, "vddio set voltage error\n");
			return ret;
		}
	}

	adapter = to_i2c_adapter(client->dev.parent);
	if (!i2c_check_functionality(adapter,
				     I2C_FUNC_SMBUS_BYTE |
				     I2C_FUNC_SMBUS_BYTE_DATA |
				     I2C_FUNC_SMBUS_I2C_BLOCK))
		return -EIO;

	dev_info(&client->dev, "check mag3110 chip ID\n");
	ret = mag3110_read_reg(client, MAG3110_WHO_AM_I);

	if (MAG3110_ID != ret) {
		dev_err(&client->dev,
			"read chip ID 0x%x is not equal to 0x%x!\n", ret,
			MAG3110_ID);
		return -EINVAL;
	}
	data = kzalloc(sizeof(struct mag3110_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	data->client = client;
	i2c_set_clientdata(client, data);
	/* Init queue */
	init_waitqueue_head(&data->waitq);

	data->hwmon_dev = hwmon_device_register(&client->dev);
	if (IS_ERR(data->hwmon_dev)) {
		dev_err(&client->dev, "hwmon register failed!\n");
		ret = PTR_ERR(data->hwmon_dev);
		goto error_rm_dev_sysfs;
	}

	if (client->irq > 0) {
		data->use_irq = 1;
		irq_data = irq_get_irq_data(client->irq);
	}

	/*input poll device register */
	data->poll_dev = input_allocate_polled_device();
	if (!data->poll_dev) {
		dev_err(&client->dev, "alloc poll device failed!\n");
		ret = -ENOMEM;
		goto error_rm_hwmon_dev;
	}
	data->poll_dev->poll = mag3110_dev_poll;
	data->poll_dev->poll_interval = POLL_INTERVAL;
	data->poll_dev->poll_interval_max = POLL_INTERVAL_MAX;
	idev = data->poll_dev->input;
	idev->name = MAG3110_DRV_NAME;
	idev->id.bustype = BUS_I2C;
	idev->evbit[0] = BIT_MASK(EV_ABS);
	input_set_abs_params(idev, ABS_X, -15000, 15000, 0, 0);
	input_set_abs_params(idev, ABS_Y, -15000, 15000, 0, 0);
	input_set_abs_params(idev, ABS_Z, -15000, 15000, 0, 0);
	ret = input_register_polled_device(data->poll_dev);
	if (ret) {
		dev_err(&client->dev, "register poll device failed!\n");
		goto error_free_poll_dev;
	}

	/*create device group in sysfs as user interface */
	ret = sysfs_create_group(&idev->dev.kobj, &mag3110_attr_group);
	if (ret) {
		dev_err(&client->dev, "create device file failed!\n");
		ret = -EINVAL;
		goto error_rm_poll_dev;
	}

	if (data->use_irq) {
		irq_flag = irqd_get_trigger_type(irq_data);
		irq_flag |= IRQF_ONESHOT;
		if (shared_irq)
			irq_flag |= IRQF_SHARED;
		ret = request_threaded_irq(client->irq, NULL, mag3110_irq_handler,
			  		   irq_flag, client->dev.driver->name, idev);
		if (ret < 0) {
			dev_err(&client->dev, "failed to register irq %d!\n",
				client->irq);
			goto error_rm_dev_sysfs;
		}
	}

	/* Initialize mag3110 chip */
	mag3110_init_client(client);
	mag3110_pdata = data;
	mag3110_pdata->active = MAG_STANDBY;
	ret = of_property_read_u32(of_node, "position", &pos);
	if (ret)
		pos = DEFAULT_POSITION;
	mag3110_pdata->position = (int)pos;
	dev_info(&client->dev, "mag3110 is probed\n");
	return 0;
error_rm_dev_sysfs:
	sysfs_remove_group(&client->dev.kobj, &mag3110_attr_group);
error_rm_poll_dev:
	input_unregister_polled_device(data->poll_dev);
error_free_poll_dev:
	input_free_polled_device(data->poll_dev);
error_rm_hwmon_dev:
	hwmon_device_unregister(data->hwmon_dev);

	kfree(data);
	mag3110_pdata = NULL;

	return ret;
}

static int mag3110_remove(struct i2c_client *client)
{
	struct mag3110_data *data;
	int ret;

	data = i2c_get_clientdata(client);

	data->ctl_reg1 = mag3110_read_reg(client, MAG3110_CTRL_REG1);
	ret = mag3110_write_reg(client, MAG3110_CTRL_REG1,
				data->ctl_reg1 & ~MAG3110_AC_MASK);

	free_irq(client->irq, data);
	input_unregister_polled_device(data->poll_dev);
	input_free_polled_device(data->poll_dev);
	hwmon_device_unregister(data->hwmon_dev);
	sysfs_remove_group(&client->dev.kobj, &mag3110_attr_group);
	kfree(data);
	mag3110_pdata = NULL;

	return ret;
}

#ifdef CONFIG_PM
static int mag3110_suspend(struct device *dev)
{
	int ret = 0;
	struct i2c_client *client = to_i2c_client(dev);
	struct mag3110_data *data = i2c_get_clientdata(client);

	if (data->active == MAG_ACTIVED) {
		data->ctl_reg1 = mag3110_read_reg(client, MAG3110_CTRL_REG1);
		ret = mag3110_write_reg(client, MAG3110_CTRL_REG1,
					data->ctl_reg1 & ~MAG3110_AC_MASK);
	}
	return ret;
}

static int mag3110_resume(struct device *dev)
{
	int ret = 0;
	struct i2c_client *client = to_i2c_client(dev);
	struct mag3110_data *data = i2c_get_clientdata(client);
	u8 tmp_data[MAG3110_XYZ_DATA_LEN];

	if (data->active == MAG_ACTIVED) {
		ret = mag3110_write_reg(client, MAG3110_CTRL_REG1,
					data->ctl_reg1);

		if (data->ctl_reg1 & MAG3110_AC_MASK) {
			/* Read out MSB data to clear interrupt
			 flag automatically */
			mag3110_read_block_data(client, MAG3110_OUT_X_MSB,
						MAG3110_XYZ_DATA_LEN, tmp_data);
		}
	}
	return ret;
}

static const struct dev_pm_ops mag3110_dev_pm_ops = {
	.suspend	= mag3110_suspend,
	.resume		= mag3110_resume,
};
#define MAG3110_DEV_PM_OPS (&mag3110_dev_pm_ops)

#else
#define MAG3110_DEV_PM_OPS NULL
#endif /* CONFIG_PM */

static const struct i2c_device_id mag3110_id[] = {
	{MAG3110_DRV_NAME, 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, mag3110_id);
static struct i2c_driver mag3110_driver = {
	.driver = {
		.name	= MAG3110_DRV_NAME,
		.owner	= THIS_MODULE,
		.pm	= MAG3110_DEV_PM_OPS,
	},
	.probe = mag3110_probe,
	.remove = mag3110_remove,
	.id_table = mag3110_id,
};

static int __init mag3110_init(void)
{
	return i2c_add_driver(&mag3110_driver);
}

static void __exit mag3110_exit(void)
{
	i2c_del_driver(&mag3110_driver);
}

module_init(mag3110_init);
module_exit(mag3110_exit);
MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("Freescale mag3110 3-axis magnetometer driver");
MODULE_LICENSE("GPL");
