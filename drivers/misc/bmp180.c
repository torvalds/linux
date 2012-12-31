/*
 * Copyright (C) 2011 Samsung Electronics. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input-polldev.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#define BMP180_DRV_NAME		"bmp180"
#define DRIVER_VERSION		"1.0"

/* Register definitions */
#define BMP180_TAKE_MEAS_REG		0xf4
#define BMP180_READ_MEAS_REG_U		0xf6
#define BMP180_READ_MEAS_REG_L		0xf7
#define BMP180_READ_MEAS_REG_XL		0xf8

/*
 * Bytes defined by the spec to take measurements
 * Temperature will take 4.5ms before EOC
 */
#define BMP180_MEAS_TEMP		0x2e
/* 4.5ms wait for measurement */
#define BMP180_MEAS_PRESS_OVERSAMP_0	0x34
/* 7.5ms wait for measurement */
#define BMP180_MEAS_PRESS_OVERSAMP_1	0x74
/* 13.5ms wait for measurement */
#define BMP180_MEAS_PRESS_OVERSAMP_2	0xb4
/* 25.5ms wait for measurement */
#define BMP180_MEAS_PRESS_OVERSAMP_3	0xf4

/*
 * EEPROM registers each is a two byte value so there is
 * an upper byte and a lower byte
 */
#define BMP180_EEPROM_AC1_U	0xaa
#define BMP180_EEPROM_AC1_L	0xab
#define BMP180_EEPROM_AC2_U	0xac
#define BMP180_EEPROM_AC2_L	0xad
#define BMP180_EEPROM_AC3_U	0xae
#define BMP180_EEPROM_AC3_L	0xaf
#define BMP180_EEPROM_AC4_U	0xb0
#define BMP180_EEPROM_AC4_L	0xb1
#define BMP180_EEPROM_AC5_U	0xb2
#define BMP180_EEPROM_AC5_L	0xb3
#define BMP180_EEPROM_AC6_U	0xb4
#define BMP180_EEPROM_AC6_L	0xb5
#define BMP180_EEPROM_B1_U	0xb6
#define BMP180_EEPROM_B1_L	0xb7
#define BMP180_EEPROM_B2_U	0xb8
#define BMP180_EEPROM_B2_L	0xb9
#define BMP180_EEPROM_MB_U	0xba
#define BMP180_EEPROM_MB_L	0xbb
#define BMP180_EEPROM_MC_U	0xbc
#define BMP180_EEPROM_MC_L	0xbd
#define BMP180_EEPROM_MD_U	0xbe
#define BMP180_EEPROM_MD_L	0xbf

#define I2C_TRIES		5
#define AUTO_INCREMENT		0x80

#define DELAY_LOWBOUND		(50 * NSEC_PER_MSEC)
#define DELAY_UPBOUND		(500 * NSEC_PER_MSEC)
#define DELAY_DEFAULT		(200 * NSEC_PER_MSEC)

#define PRESSURE_MAX		125000
#define PRESSURE_MIN		95000
#define PRESSURE_FUZZ		5
#define PRESSURE_FLAT		5

struct bmp180_eeprom_data {
	s16 AC1, AC2, AC3;
	u16 AC4, AC5, AC6;
	s16 B1, B2;
	s16 MB, MC, MD;
};

struct bmp180_data {
	struct i2c_client *client;
	struct mutex lock;
	struct workqueue_struct *wq;
	struct work_struct work_pressure;
	struct input_dev *input_dev;
	struct hrtimer timer;
	ktime_t poll_delay;
	u8 oversampling_rate;
	struct bmp180_eeprom_data bmp180_eeprom_vals;
	bool enabled;
	bool on_before_suspend;
};

static int bmp180_i2c_read(const struct i2c_client *client, u8 cmd,
	u8 *buf, int len)
{
	int err;
	int tries = 0;

	do {
		err = i2c_smbus_read_i2c_block_data(client, cmd, len, buf);
		if (err == len)
			return 0;
	} while (++tries < I2C_TRIES);

	return err;
}

static int bmp180_i2c_write(const struct i2c_client *client, u8 cmd, u8 data)
{
	int err;
	int tries = 0;

	do {
		err = i2c_smbus_write_byte_data(client, cmd, data);
		if (!err)
			return 0;
	} while (++tries < I2C_TRIES);

	return err;
}

static void bmp180_enable(struct bmp180_data *barom)
{
	pr_debug("%s: bmp180_enable\n", __func__);
	if (!barom->enabled) {
		barom->enabled = true;
		pr_debug("%s: start timer\n", __func__);
		hrtimer_start(&barom->timer, barom->poll_delay,
			HRTIMER_MODE_REL);
	}
}

static void bmp180_disable(struct bmp180_data *barom)
{
	pr_debug("%s: bmp180_disable\n", __func__);
	if (barom->enabled) {
		barom->enabled = false;
		pr_debug("%s: stop timer\n", __func__);
		hrtimer_cancel(&barom->timer);
		cancel_work_sync(&barom->work_pressure);
	}
}

static int bmp180_get_raw_temperature(struct bmp180_data *barom,
					u16 *raw_temperature)
{
	int err;
	u16 buf;

	pr_debug("%s: read uncompensated temperature value\n", __func__);
	err = bmp180_i2c_write(barom->client, BMP180_TAKE_MEAS_REG,
		BMP180_MEAS_TEMP);
	if (err) {
		pr_err("%s: can't write BMP180_TAKE_MEAS_REG\n", __func__);
		return err;
	}

	msleep(5);

	err = bmp180_i2c_read(barom->client, BMP180_READ_MEAS_REG_U,
				(u8 *)&buf, 2);
	if (err) {
		pr_err("%s: Fail to read uncompensated temperature\n",
			__func__);
		return err;
	}
	*raw_temperature = be16_to_cpu(buf);
	pr_debug("%s: uncompensated temperature:  %d\n",
		__func__, *raw_temperature);
	return err;
}

static int bmp180_get_raw_pressure(struct bmp180_data *barom,
					u32 *raw_pressure)
{
	int err;
	u32 buf = 0;

	pr_debug("%s: read uncompensated pressure value\n", __func__);

	err = bmp180_i2c_write(barom->client, BMP180_TAKE_MEAS_REG,
		BMP180_MEAS_PRESS_OVERSAMP_0 |
		(barom->oversampling_rate << 6));
	if (err) {
		pr_err("%s: can't write BMP180_TAKE_MEAS_REG\n", __func__);
		return err;
	}

	msleep(2+(3 << barom->oversampling_rate));

	err = bmp180_i2c_read(barom->client, BMP180_READ_MEAS_REG_U,
				((u8 *)&buf)+1, 3);
	if (err) {
		pr_err("%s: Fail to read uncompensated pressure\n", __func__);
		return err;
	}

	*raw_pressure = be32_to_cpu(buf);
	*raw_pressure >>= (8 - barom->oversampling_rate);
	pr_debug("%s: uncompensated pressure:  %d\n",
		__func__, *raw_pressure);

	return err;
}

static void bmp180_get_pressure_data(struct work_struct *work)
{
	u16 raw_temperature;
	u32 raw_pressure;
	long x1, x2, x3, b3, b5, b6;
	unsigned long b4, b7;
	long p;
	int pressure;
	int temperature;
	int err;

	struct bmp180_data *barom =
	    container_of(work, struct bmp180_data, work_pressure);

	if (bmp180_get_raw_temperature(barom, &raw_temperature)) {
		pr_err("%s: can't read uncompensated temperature\n", __func__);
		return;
	}

	if (bmp180_get_raw_pressure(barom, &raw_pressure)) {
		pr_err("%s: Fail to read uncompensated pressure\n", __func__);
		return;
	}

	x1 = ((raw_temperature - barom->bmp180_eeprom_vals.AC6) *
	      barom->bmp180_eeprom_vals.AC5) >> 15;
	x2 = (barom->bmp180_eeprom_vals.MC << 11) /
	    (x1 + barom->bmp180_eeprom_vals.MD);
	b5 = x1 + x2;

	temperature = (x1+x2+8) >> 4;

	b6 = (b5 - 4000);
	x1 = (barom->bmp180_eeprom_vals.B2 * ((b6 * b6) >> 12)) >> 11;
	x2 = (barom->bmp180_eeprom_vals.AC2 * b6) >> 11;
	x3 = x1 + x2;
	b3 = (((((long)barom->bmp180_eeprom_vals.AC1) * 4 +
		x3) << barom->oversampling_rate) + 2) >> 2;
	x1 = (barom->bmp180_eeprom_vals.AC3 * b6) >> 13;
	x2 = (barom->bmp180_eeprom_vals.B1 * (b6 * b6 >> 12)) >> 16;
	x3 = ((x1 + x2) + 2) >> 2;
	b4 = (barom->bmp180_eeprom_vals.AC4 *
	      (unsigned long)(x3 + 32768)) >> 15;
	b7 = ((unsigned long)raw_pressure - b3) *
		(50000 >> barom->oversampling_rate);
	if (b7 < 0x80000000)
		p = (b7 * 2) / b4;
	else
		p = (b7 / b4) * 2;

	x1 = (p >> 8) * (p >> 8);
	x1 = (x1 * 3038) >> 16;
	x2 = (-7357 * p) >> 16;
	pressure = p + ((x1 + x2 + 3791) >> 4);
	pr_debug("%s: calibrated pressure: %d\n",
		__func__, pressure);

	input_report_abs(barom->input_dev, ABS_PRESSURE, pressure);
	input_sync(barom->input_dev);

	input_report_abs(barom->input_dev, ABS_X, temperature);
	input_sync(barom->input_dev);

	return;
}

static int bmp180_input_init(struct bmp180_data *barom)
{
	int err;

	pr_debug("%s: enter\n", __func__);
	barom->input_dev = input_allocate_device();
	if (!barom->input_dev) {
		pr_err("%s: could not allocate input device\n", __func__);
		return -ENOMEM;
	}
	input_set_drvdata(barom->input_dev, barom);
	barom->input_dev->name = "barometer";
	input_set_capability(barom->input_dev, EV_ABS, ABS_PRESSURE);

	/* Need to define the correct min and max */
	input_set_abs_params(barom->input_dev, ABS_PRESSURE,
				PRESSURE_MIN, PRESSURE_MAX,
				PRESSURE_FUZZ, PRESSURE_FLAT);

	input_set_capability(barom->input_dev, EV_ABS, ABS_MISC);

	/* Need to define the correct min and max */
	input_set_abs_params(barom->input_dev, ABS_X,
				600, 0,
				0, 0);

	pr_debug("%s: registering barometer input device\n", __func__);

	err = input_register_device(barom->input_dev);
	if (err) {
		pr_err("%s: unable to register input polled device %s\n",
			__func__, barom->input_dev->name);
		goto err_register_device;
	}

	goto done;

err_register_device:
	input_free_device(barom->input_dev);
done:
	return err;
}

static void bmp180_input_cleanup(struct bmp180_data *barom)
{
	input_unregister_device(barom->input_dev);
	input_free_device(barom->input_dev);
}

static int bmp180_read_store_eeprom_val(struct bmp180_data *barom)
{
	int err;
	u16 buf[11];

	err = bmp180_i2c_read(barom->client, BMP180_EEPROM_AC1_U,
				(u8 *)buf, 22);
	if (err) {
		pr_err("%s: Cannot read EEPROM values\n", __func__);
		return err;
	}
	barom->bmp180_eeprom_vals.AC1 = be16_to_cpu(buf[0]);
	barom->bmp180_eeprom_vals.AC2 = be16_to_cpu(buf[1]);
	barom->bmp180_eeprom_vals.AC3 = be16_to_cpu(buf[2]);
	barom->bmp180_eeprom_vals.AC4 = be16_to_cpu(buf[3]);
	barom->bmp180_eeprom_vals.AC5 = be16_to_cpu(buf[4]);
	barom->bmp180_eeprom_vals.AC6 = be16_to_cpu(buf[5]);
	barom->bmp180_eeprom_vals.B1 = be16_to_cpu(buf[6]);
	barom->bmp180_eeprom_vals.B2 = be16_to_cpu(buf[7]);
	barom->bmp180_eeprom_vals.MB = be16_to_cpu(buf[8]);
	barom->bmp180_eeprom_vals.MC = be16_to_cpu(buf[9]);
	barom->bmp180_eeprom_vals.MD = be16_to_cpu(buf[10]);
	return 0;
}

static enum hrtimer_restart bmp180_timer_func(struct hrtimer *timer)
{
	struct bmp180_data *barom = container_of(timer,
		struct bmp180_data, timer);

	pr_debug("%s: start\n", __func__);
	queue_work(barom->wq, &barom->work_pressure);
	hrtimer_forward_now(&barom->timer, barom->poll_delay);
	return HRTIMER_RESTART;
}

static ssize_t bmp180_poll_delay_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct bmp180_data *barom = dev_get_drvdata(dev);

	return sprintf(buf, "%lld\n",
		ktime_to_ns(barom->poll_delay));
}

static ssize_t bmp180_poll_delay_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	int err;
	int64_t new_delay;
	struct bmp180_data *barom = dev_get_drvdata(dev);

	err = strict_strtoll(buf, 10, &new_delay);
	if (err < 0)
		return err;

	pr_debug("%s: new delay = %lldns, old delay = %lldns\n",
		__func__, new_delay, ktime_to_ns(barom->poll_delay));

	if (new_delay < DELAY_LOWBOUND || new_delay > DELAY_UPBOUND)
		return -EINVAL;

	mutex_lock(&barom->lock);
	if (new_delay != ktime_to_ns(barom->poll_delay))
		barom->poll_delay = ns_to_ktime(new_delay);

	mutex_unlock(&barom->lock);

	return size;
}

static ssize_t bmp180_enable_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct bmp180_data *barom = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", barom->enabled);
}

static ssize_t bmp180_enable_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	bool new_value;
	struct bmp180_data *barom = dev_get_drvdata(dev);

	pr_debug("%s: enable %s\n", __func__, buf);

	if (sysfs_streq(buf, "1")) {
		new_value = true;
	} else if (sysfs_streq(buf, "0")) {
		new_value = false;
	} else {
		pr_err("%s: invalid value %d\n", __func__, *buf);
		return -EINVAL;
	}

	pr_debug("%s: new_value = %d, old state = %d\n",
		__func__, new_value, barom->enabled);

	mutex_lock(&barom->lock);
	if (new_value)
		bmp180_enable(barom);
	else
		bmp180_disable(barom);

	mutex_unlock(&barom->lock);

	return size;
}

static ssize_t bmp180_oversampling_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct bmp180_data *barom = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", barom->oversampling_rate);
}

static ssize_t bmp180_oversampling_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct bmp180_data *barom = dev_get_drvdata(dev);

	unsigned long oversampling;
	int success = strict_strtoul(buf, 10, &oversampling);
	if (success == 0) {
		if (oversampling > 3)
			oversampling = 3;
		barom->oversampling_rate = oversampling;
		return count;
	}
	return success;
}

static DEVICE_ATTR(poll_delay, S_IRUGO | S_IWUSR | S_IWGRP,
		bmp180_poll_delay_show, bmp180_poll_delay_store);

static DEVICE_ATTR(enable, S_IRUGO | S_IWUSR | S_IWGRP,
		bmp180_enable_show, bmp180_enable_store);

static DEVICE_ATTR(oversampling, S_IWUSR | S_IRUGO,
		bmp180_oversampling_show, bmp180_oversampling_store);

static struct attribute *bmp180_sysfs_attrs[] = {
	&dev_attr_enable.attr,
	&dev_attr_poll_delay.attr,
	&dev_attr_oversampling.attr,
	NULL
};

static struct attribute_group bmp180_attribute_group = {
	.attrs = bmp180_sysfs_attrs,
};

static int __devinit bmp180_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int err;
	struct bmp180_data *barom;

	pr_debug("%s: enter\n", __func__);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("%s: client not i2c capable\n", __func__);
		return -EIO;
	}

	barom = kzalloc(sizeof(*barom), GFP_KERNEL);
	if (!barom) {
		pr_err("%s: failed to allocate memory for module\n", __func__);
		return -ENOMEM;
	}

	mutex_init(&barom->lock);
	barom->client = client;

	i2c_set_clientdata(client, barom);

	err = bmp180_read_store_eeprom_val(barom);
	if (err) {
		pr_err("%s: Reading the EEPROM failed\n", __func__);
		err = -ENODEV;
		goto err_read_eeprom;
	}

	hrtimer_init(&barom->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	barom->poll_delay = ns_to_ktime(DELAY_DEFAULT);
	barom->timer.function = bmp180_timer_func;

	barom->wq = alloc_workqueue("bmp180_wq",
		WQ_UNBOUND | WQ_RESCUER, 1);
	if (!barom->wq) {
		err = -ENOMEM;
		pr_err("%s: could not create workqueue\n", __func__);
		goto err_create_workqueue;
	}

	INIT_WORK(&barom->work_pressure, bmp180_get_pressure_data);

	err = bmp180_input_init(barom);
	if (err) {
		pr_err("%s: could not create input device\n", __func__);
		goto err_input_init;
	}
	err = sysfs_create_group(&barom->input_dev->dev.kobj,
		&bmp180_attribute_group);
	if (err) {
		pr_err("%s: could not create sysfs group\n", __func__);
		goto err_sysfs_create_group;
	}

	goto done;

err_sysfs_create_group:
	bmp180_input_cleanup(barom);
err_input_init:
	destroy_workqueue(barom->wq);
err_create_workqueue:
err_read_eeprom:
	mutex_destroy(&barom->lock);
	kfree(barom);
done:
	return err;
}

static int __devexit bmp180_remove(struct i2c_client *client)
{
	/* TO DO: revisit ordering here once _probe order is finalized */
	struct bmp180_data *barom = i2c_get_clientdata(client);

	pr_debug("%s: bmp180_remove +\n", __func__);
	sysfs_remove_group(&barom->input_dev->dev.kobj,
				&bmp180_attribute_group);

	bmp180_disable(barom);

	bmp180_input_cleanup(barom);

	destroy_workqueue(barom->wq);

	mutex_destroy(&barom->lock);
	kfree(barom);

	pr_debug("%s: bmp180_remove -\n", __func__);
	return 0;
}

static int bmp180_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bmp180_data *barom = i2c_get_clientdata(client);
	pr_debug("%s: on_before_suspend %d\n",
		__func__, barom->on_before_suspend);

	if (barom->on_before_suspend)
		bmp180_enable(barom);
	return 0;
}

static int bmp180_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bmp180_data *barom = i2c_get_clientdata(client);

	barom->on_before_suspend = barom->enabled;
	pr_debug("%s: on_before_suspend %d\n",
		__func__, barom->on_before_suspend);
	bmp180_disable(barom);
	return 0;
}

static const struct i2c_device_id bmp180_id[] = {
	{BMP180_DRV_NAME, 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, bmp180_id);
static const struct dev_pm_ops bmp180_pm_ops = {
	.suspend	= bmp180_suspend,
	.resume		= bmp180_resume,
};

static struct i2c_driver bmp180_driver = {
	.driver = {
		.name	= BMP180_DRV_NAME,
		.owner	= THIS_MODULE,
		.pm	= &bmp180_pm_ops,
	},
	.probe		= bmp180_probe,
	.remove		= __devexit_p(bmp180_remove),
	.id_table	= bmp180_id,
};

static int __init bmp180_init(void)
{
	pr_debug("%s: _init\n", __func__);
	return i2c_add_driver(&bmp180_driver);
}

static void __exit bmp180_exit(void)
{
	pr_debug("%s: _exit +\n", __func__);
	i2c_del_driver(&bmp180_driver);
	pr_debug("%s: _exit -\n", __func__);
	return;
}

MODULE_AUTHOR("Hyoung Wook Ham <hwham@sta.samsung.com>");
MODULE_DESCRIPTION("BMP180 Pressure sensor driver");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DRIVER_VERSION);

module_init(bmp180_init);
module_exit(bmp180_exit);
