/* drivers/input/sensors/sensor-dev.c - handle all gsensor in this file
 *
 * Copyright (C) 2012-2015 ROCKCHIP.
 * Author: luowei <lw@rock-chips.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <linux/gpio.h>
#include <asm/uaccess.h>
#include <asm/atomic.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/freezer.h>
#include <linux/proc_fs.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/of.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <linux/l3g4200d.h>
#include <linux/sensor-dev.h>
#include <linux/module.h>
#ifdef CONFIG_COMPAT
#include <linux/compat.h>
#endif
#include <linux/soc/rockchip/rk_vendor_storage.h>

#define SENSOR_CALIBRATION_LEN 64
struct sensor_calibration_data {
	s32 accel_offset[3];
	s32 gyro_offset[3];
	u8 is_accel_calibrated;
	u8 is_gyro_calibrated;
};

static struct sensor_private_data *g_sensor[SENSOR_NUM_TYPES];
static struct sensor_operate *sensor_ops[SENSOR_NUM_ID];
static int sensor_probe_times[SENSOR_NUM_ID];
static struct class *sensor_class;
static struct sensor_calibration_data sensor_cali_data;

static int sensor_calibration_data_write(struct sensor_calibration_data *calibration_data)
{
	int ret;
	u8 data[SENSOR_CALIBRATION_LEN] = {0};

	memcpy(data, (u8 *)calibration_data, sizeof(struct sensor_calibration_data));

	ret = rk_vendor_write(SENSOR_CALIBRATION_ID, (void *)data, SENSOR_CALIBRATION_LEN);
	if (ret < 0) {
		printk(KERN_ERR "%s failed\n", __func__);
		return ret;
	}

	return 0;
}

static int sensor_calibration_data_read(struct sensor_calibration_data *calibration_data)
{
	int ret;
	u8 data[SENSOR_CALIBRATION_LEN] = {0};
	struct sensor_calibration_data *cdata = (struct sensor_calibration_data *)data;

	ret = rk_vendor_read(SENSOR_CALIBRATION_ID, (void *)data, SENSOR_CALIBRATION_LEN);
	if (ret < 0) {
		printk(KERN_ERR "%s failed\n", __func__);
		return ret;
	}
	if (cdata->is_accel_calibrated == 1) {
		calibration_data->accel_offset[0] = cdata->accel_offset[0];
		calibration_data->accel_offset[1] = cdata->accel_offset[1];
		calibration_data->accel_offset[2] = cdata->accel_offset[2];
		calibration_data->is_accel_calibrated = 1;
	}
	if (cdata->is_gyro_calibrated == 1) {
		calibration_data->gyro_offset[0] = cdata->gyro_offset[0];
		calibration_data->gyro_offset[1] = cdata->gyro_offset[1];
		calibration_data->gyro_offset[2] = cdata->gyro_offset[2];
		calibration_data->is_gyro_calibrated = 1;
	}

	return 0;
}

static ssize_t accel_calibration_show(struct class *class,
		struct class_attribute *attr, char *buf)
{
	int ret;
	struct sensor_private_data *sensor = g_sensor[SENSOR_TYPE_ACCEL];

	if (sensor == NULL)
		return sprintf(buf, "no accel sensor find\n");

	if (sensor_cali_data.is_accel_calibrated == 1)
		return sprintf(buf, "accel calibration: %d, %d, %d\n", sensor_cali_data.accel_offset[0],
				sensor_cali_data.accel_offset[1], sensor_cali_data.accel_offset[2]);

	ret = sensor_calibration_data_read(&sensor_cali_data);
	if (ret) {
		dev_err(&sensor->client->dev, "read accel sensor calibration data failed\n");
		return sprintf(buf, "read error\n");
	}

	if (sensor_cali_data.is_accel_calibrated == 1)
		return sprintf(buf, "accel calibration: %d, %d, %d\n", sensor_cali_data.accel_offset[0],
			sensor_cali_data.accel_offset[1], sensor_cali_data.accel_offset[2]);

	return sprintf(buf, "read error\n");
}

#define ACCEL_CAPTURE_TIMES 20
#define ACCEL_SENSITIVE 16384
/* +-1 * 16384 / 9.8 */
#define ACCEL_OFFSET_MAX 1600
static int accel_do_calibration(struct sensor_private_data *sensor)
{
	int i;
	int ret;
	int max_try_times = 20;
	long int sum_accel[3] = {0, 0, 0};

	mutex_lock(&sensor->operation_mutex);
	for (i = 0; i < ACCEL_CAPTURE_TIMES; ) {
		ret = sensor->ops->report(sensor->client);
		if (ret < 0)
			dev_err(&sensor->client->dev, "in %s read accel data error\n", __func__);
		if (abs(sensor->axis.x) > ACCEL_OFFSET_MAX ||
			abs(sensor->axis.y) > ACCEL_OFFSET_MAX ||
			abs(abs(sensor->axis.z) - ACCEL_SENSITIVE) > ACCEL_OFFSET_MAX) {
			sum_accel[0] = 0;
			sum_accel[1] = 0;
			sum_accel[2] = 0;
			i = 0;
			max_try_times--;
		} else {
			sum_accel[0] += sensor->axis.x;
			sum_accel[1] += sensor->axis.y;
			sum_accel[2] += sensor->axis.z;
			i++;
		}
		if (max_try_times == 0) {
			mutex_unlock(&sensor->operation_mutex);
			return -1;
		}
		dev_info(&sensor->client->dev, "%d times, read accel data is %d, %d, %d\n",
			i, sensor->axis.x, sensor->axis.y, sensor->axis.z);
		msleep(sensor->pdata->poll_delay_ms);
	}
	mutex_unlock(&sensor->operation_mutex);

	sensor_cali_data.accel_offset[0] = sum_accel[0] / ACCEL_CAPTURE_TIMES;
	sensor_cali_data.accel_offset[1] = sum_accel[1] / ACCEL_CAPTURE_TIMES;
	sensor_cali_data.accel_offset[2] = sum_accel[2] / ACCEL_CAPTURE_TIMES;

	sensor_cali_data.accel_offset[2] = sensor_cali_data.accel_offset[2] > 0
		? sensor_cali_data.accel_offset[2] - ACCEL_SENSITIVE : sensor_cali_data.accel_offset[2] + ACCEL_SENSITIVE;

	sensor_cali_data.is_accel_calibrated = 1;

	dev_info(&sensor->client->dev, "accel offset is %d, %d, %d\n", sensor_cali_data.accel_offset[0],
		sensor_cali_data.accel_offset[1], sensor_cali_data.accel_offset[2]);

	return 0;
}

static ssize_t accel_calibration_store(struct class *class,
		struct class_attribute *attr, const char *buf, size_t count)
{
	struct sensor_private_data *sensor = g_sensor[SENSOR_TYPE_ACCEL];
	int val, ret;
	int pre_status;

	if (sensor == NULL)
		return -1;

	ret = kstrtoint(buf, 10, &val);
	if (ret) {
		dev_err(&sensor->client->dev, "%s: kstrtoint error return %d\n", __func__, ret);
		return -1;
	}
	if (val != 1) {
		dev_err(&sensor->client->dev, "%s: error value\n", __func__);
		return -1;
	}
	atomic_set(&sensor->is_factory, 1);

	pre_status = sensor->status_cur;
	if (pre_status == SENSOR_OFF) {
		mutex_lock(&sensor->operation_mutex);
		sensor->ops->active(sensor->client, SENSOR_ON, sensor->pdata->poll_delay_ms);
		mutex_unlock(&sensor->operation_mutex);
	} else {
		sensor->stop_work = 1;
		if (sensor->pdata->irq_enable)
			disable_irq_nosync(sensor->client->irq);
		else
			cancel_delayed_work_sync(&sensor->delaywork);
	}

	ret = accel_do_calibration(sensor);
	if (ret < 0) {
		dev_err(&sensor->client->dev, "accel do calibration failed\n");
		goto OUT;
	}
	ret = sensor_calibration_data_write(&sensor_cali_data);
	if (ret)
		dev_err(&sensor->client->dev, "write accel sensor calibration data failed\n");

OUT:
	if (pre_status == SENSOR_ON) {
		sensor->stop_work = 0;
		if (sensor->pdata->irq_enable)
			enable_irq(sensor->client->irq);
		else
			schedule_delayed_work(&sensor->delaywork, msecs_to_jiffies(sensor->pdata->poll_delay_ms));
	} else {
		mutex_lock(&sensor->operation_mutex);
		sensor->ops->active(sensor->client, SENSOR_OFF, sensor->pdata->poll_delay_ms);
		mutex_unlock(&sensor->operation_mutex);
	}

	atomic_set(&sensor->is_factory, 0);
	wake_up(&sensor->is_factory_ok);

	return ret ? ret : count;
}

static CLASS_ATTR(accel_calibration, 0664, accel_calibration_show, accel_calibration_store);

static ssize_t gyro_calibration_show(struct class *class,
		struct class_attribute *attr, char *buf)
{
	int ret;
	struct sensor_private_data *sensor = g_sensor[SENSOR_TYPE_GYROSCOPE];

	if (sensor == NULL)
		return sprintf(buf, "no gyro sensor find\n");

	if (sensor_cali_data.is_gyro_calibrated == 1)
		return sprintf(buf, "gyro calibration: %d, %d, %d\n", sensor_cali_data.gyro_offset[0],
				sensor_cali_data.gyro_offset[1], sensor_cali_data.gyro_offset[2]);

	ret = sensor_calibration_data_read(&sensor_cali_data);
	if (ret) {
		dev_err(&sensor->client->dev, "read gyro sensor calibration data failed\n");
		return sprintf(buf, "read error\n");
	}

	if (sensor_cali_data.is_gyro_calibrated == 1)
		return sprintf(buf, "gyro calibration: %d, %d, %d\n", sensor_cali_data.gyro_offset[0],
				sensor_cali_data.gyro_offset[1], sensor_cali_data.gyro_offset[2]);

	return sprintf(buf, "read error\n");
}

#define GYRO_CAPTURE_TIMES 20
static int gyro_do_calibration(struct sensor_private_data *sensor)
{
	int i;
	int ret;
	long int sum_gyro[3] = {0, 0, 0};

	mutex_lock(&sensor->operation_mutex);
	for (i = 0; i < GYRO_CAPTURE_TIMES; i++) {
		ret = sensor->ops->report(sensor->client);
		if (ret < 0) {
			dev_err(&sensor->client->dev, "in %s read gyro data error\n", __func__);
			mutex_unlock(&sensor->operation_mutex);
			return -1;
		}
		sum_gyro[0] += sensor->axis.x;
		sum_gyro[1] += sensor->axis.y;
		sum_gyro[2] += sensor->axis.z;
		dev_info(&sensor->client->dev, "%d times, read gyro data is %d, %d, %d\n",
			i, sensor->axis.x, sensor->axis.y, sensor->axis.z);
		msleep(sensor->pdata->poll_delay_ms);
	}
	mutex_unlock(&sensor->operation_mutex);

	sensor_cali_data.gyro_offset[0] = sum_gyro[0] / GYRO_CAPTURE_TIMES;
	sensor_cali_data.gyro_offset[1] = sum_gyro[1] / GYRO_CAPTURE_TIMES;
	sensor_cali_data.gyro_offset[2] = sum_gyro[2] / GYRO_CAPTURE_TIMES;
	sensor_cali_data.is_gyro_calibrated = 1;

	dev_info(&sensor->client->dev, "gyro offset is %d, %d, %d\n", sensor_cali_data.gyro_offset[0],
		sensor_cali_data.gyro_offset[1], sensor_cali_data.gyro_offset[2]);

	return 0;
}

static ssize_t gyro_calibration_store(struct class *class,
		struct class_attribute *attr, const char *buf, size_t count)
{
	struct sensor_private_data *sensor = g_sensor[SENSOR_TYPE_GYROSCOPE];
	int val, ret;
	int pre_status;

	if (sensor == NULL)
		return -1;

	ret = kstrtoint(buf, 10, &val);
	if (ret) {
		dev_err(&sensor->client->dev, "%s: kstrtoint error return %d\n", __func__, ret);
		return -1;
	}
	if (val != 1) {
		dev_err(&sensor->client->dev, "%s error value\n", __func__);
		return -1;
	}
	atomic_set(&sensor->is_factory, 1);

	pre_status = sensor->status_cur;
	if (pre_status == SENSOR_OFF) {
		mutex_lock(&sensor->operation_mutex);
		sensor->ops->active(sensor->client, SENSOR_ON, sensor->pdata->poll_delay_ms);
		mutex_unlock(&sensor->operation_mutex);
	} else {
		sensor->stop_work = 1;
		if (sensor->pdata->irq_enable)
			disable_irq_nosync(sensor->client->irq);
		else
			cancel_delayed_work_sync(&sensor->delaywork);
	}

	ret = gyro_do_calibration(sensor);
	if (ret < 0) {
		dev_err(&sensor->client->dev, "gyro do calibration failed\n");
		goto OUT;
	}

	ret = sensor_calibration_data_write(&sensor_cali_data);
	if (ret)
		dev_err(&sensor->client->dev, "write gyro sensor calibration data failed\n");

OUT:
	if (pre_status == SENSOR_ON) {
		sensor->stop_work = 0;
		if (sensor->pdata->irq_enable)
			enable_irq(sensor->client->irq);
		else
			schedule_delayed_work(&sensor->delaywork, msecs_to_jiffies(sensor->pdata->poll_delay_ms));
	} else {
		mutex_lock(&sensor->operation_mutex);
		sensor->ops->active(sensor->client, SENSOR_OFF, sensor->pdata->poll_delay_ms);
		mutex_unlock(&sensor->operation_mutex);
	}

	atomic_set(&sensor->is_factory, 0);
	wake_up(&sensor->is_factory_ok);

	return ret ? ret : count;
}

static CLASS_ATTR(gyro_calibration, 0664, gyro_calibration_show, gyro_calibration_store);

static int sensor_class_init(void)
{
	int ret ;

	sensor_class = class_create(THIS_MODULE, "sensor_class");
	ret = class_create_file(sensor_class, &class_attr_accel_calibration);
	if (ret) {
		printk(KERN_ERR "%s:Fail to creat accel class file\n", __func__);
		return ret;
	}

	ret = class_create_file(sensor_class, &class_attr_gyro_calibration);
	if (ret) {
		printk(KERN_ERR "%s:Fail to creat gyro class file\n", __func__);
		return ret;
	}

	return 0;
}

static int sensor_get_id(struct i2c_client *client, int *value)
{
	struct sensor_private_data *sensor = (struct sensor_private_data *) i2c_get_clientdata(client);
	int result = 0;
	char temp = sensor->ops->id_reg;
	int i = 0;

	if (sensor->ops->id_reg >= 0) {
		for (i = 0; i < 3; i++) {
			result = sensor_rx_data(client, &temp, 1);
			*value = temp;
			if (!result)
				break;
		}

		if (result)
			return result;

		if (*value != sensor->ops->id_data) {
			dev_err(&client->dev, "%s:id=0x%x is not 0x%x\n", __func__, *value, sensor->ops->id_data);
			result = -1;
		}
	}

	return result;
}

static int sensor_initial(struct i2c_client *client)
{
	struct sensor_private_data *sensor = (struct sensor_private_data *) i2c_get_clientdata(client);
	int result = 0;

	/* register setting according to chip datasheet */
	result = sensor->ops->init(client);
	if (result < 0) {
		dev_err(&client->dev, "%s:fail to init sensor\n", __func__);
		return result;
	}

	return result;
}

static int sensor_chip_init(struct i2c_client *client)
{
	struct sensor_private_data *sensor = (struct sensor_private_data *) i2c_get_clientdata(client);
	struct sensor_operate *ops = sensor_ops[(int)sensor->i2c_id->driver_data];
	int result = 0;

	if (ops) {
		sensor->ops = ops;
	} else {
		dev_err(&client->dev, "%s:ops is null,sensor name is %s\n", __func__, sensor->i2c_id->name);
		result = -1;
		goto error;
	}

	if ((sensor->type != ops->type) || ((int)sensor->i2c_id->driver_data != ops->id_i2c)) {
		dev_err(&client->dev, "%s:type or id is different:type=%d,%d,id=%d,%d\n", __func__, sensor->type, ops->type, (int)sensor->i2c_id->driver_data, ops->id_i2c);
		result = -1;
		goto error;
	}

	if (!ops->init || !ops->active || !ops->report) {
		dev_err(&client->dev, "%s:error:some function is needed\n", __func__);
		result = -1;
		goto error;
	}

	result = sensor_get_id(sensor->client, &sensor->devid);
	if (result < 0) {
		dev_err(&client->dev, "%s:fail to read %s devid:0x%x\n", __func__, sensor->i2c_id->name, sensor->devid);
		result = -2;
		goto error;
	}

	dev_info(&client->dev, "%s:%s:devid=0x%x,ops=0x%p\n", __func__, sensor->i2c_id->name, sensor->devid, sensor->ops);

	result = sensor_initial(sensor->client);
	if (result < 0) {
		dev_err(&client->dev, "%s:fail to init sensor\n", __func__);
		result = -2;
		goto error;
	}
	return 0;

error:
	return result;
}

static int sensor_reset_rate(struct i2c_client *client, int rate)
{
	struct sensor_private_data *sensor = (struct sensor_private_data *) i2c_get_clientdata(client);
	int result = 0;

	if (rate < 5)
		rate = 5;
	else if (rate > 200)
		rate = 200;

	dev_info(&client->dev, "set sensor poll time to %dms\n", rate);

	/* work queue is always slow, we need more quickly to match hal rate */
	if (sensor->pdata->poll_delay_ms == (rate - 2))
		return 0;

	sensor->pdata->poll_delay_ms = rate - 2;

	if (sensor->status_cur == SENSOR_ON) {
		if (!sensor->pdata->irq_enable) {
			sensor->stop_work = 1;
			cancel_delayed_work_sync(&sensor->delaywork);
		}
		result = sensor->ops->active(client, SENSOR_OFF, rate);
		result = sensor->ops->active(client, SENSOR_ON, rate);
		if (!sensor->pdata->irq_enable) {
			sensor->stop_work = 0;
			schedule_delayed_work(&sensor->delaywork, msecs_to_jiffies(sensor->pdata->poll_delay_ms));
		}
	}

	return result;
}

static void  sensor_delaywork_func(struct work_struct *work)
{
	struct delayed_work *delaywork = container_of(work, struct delayed_work, work);
	struct sensor_private_data *sensor = container_of(delaywork, struct sensor_private_data, delaywork);
	struct i2c_client *client = sensor->client;
	int result;

	mutex_lock(&sensor->sensor_mutex);
	result = sensor->ops->report(client);
	if (result < 0)
		dev_err(&client->dev, "%s: Get data failed\n", __func__);
	mutex_unlock(&sensor->sensor_mutex);

	if ((!sensor->pdata->irq_enable) && (sensor->stop_work == 0))
		schedule_delayed_work(&sensor->delaywork, msecs_to_jiffies(sensor->pdata->poll_delay_ms));
}

/*
 * This is a threaded IRQ handler so can access I2C/SPI.  Since all
 * interrupts are clear on read the IRQ line will be reasserted and
 * the physical IRQ will be handled again if another interrupt is
 * asserted while we run - in the normal course of events this is a
 * rare occurrence so we save I2C/SPI reads.  We're also assuming that
 * it's rare to get lots of interrupts firing simultaneously so try to
 * minimise I/O.
 */
static irqreturn_t sensor_interrupt(int irq, void *dev_id)
{
	struct sensor_private_data *sensor =
			(struct sensor_private_data *)dev_id;
	struct i2c_client *client = sensor->client;

	mutex_lock(&sensor->sensor_mutex);
	if (sensor->ops->report(client) < 0)
		dev_err(&client->dev, "%s: Get data failed\n", __func__);
	mutex_unlock(&sensor->sensor_mutex);

	return IRQ_HANDLED;
}

static int sensor_irq_init(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
			(struct sensor_private_data *) i2c_get_clientdata(client);
	int result = 0;
	int irq;

	if ((sensor->pdata->irq_enable) && (sensor->pdata->irq_flags != SENSOR_UNKNOW_DATA)) {
		if (sensor->pdata->poll_delay_ms <= 0)
			sensor->pdata->poll_delay_ms = 30;
		result = gpio_request(client->irq, sensor->i2c_id->name);
		if (result)
			dev_err(&client->dev, "%s:fail to request gpio :%d\n", __func__, client->irq);

		irq = gpio_to_irq(client->irq);
		result = devm_request_threaded_irq(&client->dev, irq, NULL, sensor_interrupt, sensor->pdata->irq_flags | IRQF_ONESHOT, sensor->ops->name, sensor);
		if (result) {
			dev_err(&client->dev, "%s:fail to request irq = %d, ret = 0x%x\n", __func__, irq, result);
			goto error;
		}

		client->irq = irq;
		disable_irq_nosync(client->irq);

		dev_info(&client->dev, "%s:use irq=%d\n", __func__, irq);
	} else if (!sensor->pdata->irq_enable) {
		INIT_DELAYED_WORK(&sensor->delaywork, sensor_delaywork_func);
		sensor->stop_work = 1;
		if (sensor->pdata->poll_delay_ms <= 0)
			sensor->pdata->poll_delay_ms = 30;

		dev_info(&client->dev, "%s:use polling,delay=%d ms\n", __func__, sensor->pdata->poll_delay_ms);
	}

error:
	return result;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void sensor_suspend(struct early_suspend *h)
{
	struct sensor_private_data *sensor =
			container_of(h, struct sensor_private_data, early_suspend);

	if (sensor->ops->suspend)
		sensor->ops->suspend(sensor->client);
}

static void sensor_resume(struct early_suspend *h)
{
	struct sensor_private_data *sensor =
			container_of(h, struct sensor_private_data, early_suspend);

	if (sensor->ops->resume)
		sensor->ops->resume(sensor->client);
}
#endif

#ifdef CONFIG_PM
static int __maybe_unused sensor_of_suspend(struct device *dev)
{
	struct sensor_private_data *sensor = dev_get_drvdata(dev);

	if (sensor->ops->suspend)
		sensor->ops->suspend(sensor->client);

	return 0;
}

static int __maybe_unused sensor_of_resume(struct device *dev)
{
	struct sensor_private_data *sensor = dev_get_drvdata(dev);

	if (sensor->ops->resume)
		sensor->ops->resume(sensor->client);
	if (sensor->pdata->power_off_in_suspend)
		sensor_initial(sensor->client);

	return 0;
}

static const struct dev_pm_ops sensor_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(sensor_of_suspend, sensor_of_resume)
};

#define SENSOR_PM_OPS (&sensor_pm_ops)
#else
#define SENSOR_PM_OPS NULL
#endif

static int angle_dev_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int angle_dev_release(struct inode *inode, struct file *file)
{
	return 0;
}

static int sensor_enable(struct sensor_private_data *sensor, int enable)
{
	int result = 0;
	struct i2c_client *client = sensor->client;

	if (enable == SENSOR_ON) {
		result = sensor->ops->active(client, 1, sensor->pdata->poll_delay_ms);
		if (result < 0) {
			dev_err(&client->dev, "%s:fail to active sensor,ret=%d\n", __func__, result);
			return result;
		}
		sensor->status_cur = SENSOR_ON;
		sensor->stop_work = 0;
		if (sensor->pdata->irq_enable)
			enable_irq(client->irq);
		else
			schedule_delayed_work(&sensor->delaywork, msecs_to_jiffies(sensor->pdata->poll_delay_ms));
		dev_info(&client->dev, "sensor on: starting poll sensor data %dms\n", sensor->pdata->poll_delay_ms);
	} else {
		sensor->stop_work = 1;
		if (sensor->pdata->irq_enable)
			disable_irq_nosync(client->irq);
		else
			cancel_delayed_work_sync(&sensor->delaywork);
		result = sensor->ops->active(client, 0, sensor->pdata->poll_delay_ms);
		if (result < 0) {
			dev_err(&client->dev, "%s:fail to disable sensor,ret=%d\n", __func__, result);
			return result;
		}
		sensor->status_cur = SENSOR_OFF;
	}

	return result;
}

/* ioctl - I/O control */
static long angle_dev_ioctl(struct file *file,
			  unsigned int cmd, unsigned long arg)
{
	struct sensor_private_data *sensor = g_sensor[SENSOR_TYPE_ANGLE];
	struct i2c_client *client = sensor->client;
	void __user *argp = (void __user *)arg;
	struct sensor_axis axis = {0};
	short rate;
	int result = 0;

	switch (cmd) {
	case GSENSOR_IOCTL_APP_SET_RATE:
		if (copy_from_user(&rate, argp, sizeof(rate))) {
			result = -EFAULT;
			goto error;
		}
		break;
	default:
		break;
	}

	switch (cmd) {
	case GSENSOR_IOCTL_START:
		mutex_lock(&sensor->operation_mutex);
		if (++sensor->start_count == 1)	{
			if (sensor->status_cur == SENSOR_OFF) {
				sensor_enable(sensor, SENSOR_ON);
			}
		}
		mutex_unlock(&sensor->operation_mutex);
		break;

	case GSENSOR_IOCTL_CLOSE:
		mutex_lock(&sensor->operation_mutex);
		if (--sensor->start_count == 0) {
			if (sensor->status_cur == SENSOR_ON) {
				sensor_enable(sensor, SENSOR_OFF);
			}
		}
		mutex_unlock(&sensor->operation_mutex);
		break;

	case GSENSOR_IOCTL_APP_SET_RATE:
		mutex_lock(&sensor->operation_mutex);
		result = sensor_reset_rate(client, rate);
		if (result < 0) {
			mutex_unlock(&sensor->operation_mutex);
			goto error;
		}
		mutex_unlock(&sensor->operation_mutex);
		break;

	case GSENSOR_IOCTL_GETDATA:
		mutex_lock(&sensor->data_mutex);
		memcpy(&axis, &sensor->axis, sizeof(sensor->axis));
		mutex_unlock(&sensor->data_mutex);
		break;

	default:
		result = -ENOTTY;

	goto error;
	}

	switch (cmd) {
	case GSENSOR_IOCTL_GETDATA:
		if (copy_to_user(argp, &axis, sizeof(axis))) {
			dev_err(&client->dev, "failed to copy sense data to user space.\n");
			result = -EFAULT;
			goto error;
		}
		break;
	default:
		break;
	}

error:
	return result;
}


static int gsensor_dev_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int gsensor_dev_release(struct inode *inode, struct file *file)
{
	return 0;
}

/* ioctl - I/O control */
static long gsensor_dev_ioctl(struct file *file,
			  unsigned int cmd, unsigned long arg)
{
	struct sensor_private_data *sensor = g_sensor[SENSOR_TYPE_ACCEL];
	struct i2c_client *client = sensor->client;
	void __user *argp = (void __user *)arg;
	struct sensor_axis axis = {0};
	short rate;
	int result = 0;

	wait_event_interruptible(sensor->is_factory_ok, (atomic_read(&sensor->is_factory) == 0));

	switch (cmd) {
	case GSENSOR_IOCTL_APP_SET_RATE:
		if (copy_from_user(&rate, argp, sizeof(rate))) {
			result = -EFAULT;
			goto error;
		}
		break;
	default:
		break;
	}

	switch (cmd) {
	case GSENSOR_IOCTL_START:
		mutex_lock(&sensor->operation_mutex);
		if (++sensor->start_count == 1) {
			if (sensor->status_cur == SENSOR_OFF) {
				sensor_enable(sensor, SENSOR_ON);
			}
		}
		mutex_unlock(&sensor->operation_mutex);
		break;

	case GSENSOR_IOCTL_CLOSE:
		mutex_lock(&sensor->operation_mutex);
		if (--sensor->start_count == 0) {
			if (sensor->status_cur == SENSOR_ON) {
				sensor_enable(sensor, SENSOR_OFF);
			}
		}
		mutex_unlock(&sensor->operation_mutex);
		break;

	case GSENSOR_IOCTL_APP_SET_RATE:
		mutex_lock(&sensor->operation_mutex);
		result = sensor_reset_rate(client, rate);
		if (result < 0) {
			mutex_unlock(&sensor->operation_mutex);
			goto error;
		}
		mutex_unlock(&sensor->operation_mutex);
		break;

	case GSENSOR_IOCTL_GETDATA:
		mutex_lock(&sensor->data_mutex);
		memcpy(&axis, &sensor->axis, sizeof(sensor->axis));
		mutex_unlock(&sensor->data_mutex);
		break;

	case GSENSOR_IOCTL_GET_CALIBRATION:
		if (sensor_cali_data.is_accel_calibrated != 1) {
			if (sensor_calibration_data_read(&sensor_cali_data)) {
				dev_err(&client->dev, "failed to read accel offset data from storage\n");
				result = -EFAULT;
				goto error;
			}
		}
		if (sensor_cali_data.is_accel_calibrated == 1) {
			if (copy_to_user(argp, sensor_cali_data.accel_offset, sizeof(sensor_cali_data.accel_offset))) {
				dev_err(&client->dev, "failed to copy accel offset data to user\n");
				result = -EFAULT;
				goto error;
			}
		}
		break;

	default:
		result = -ENOTTY;
	goto error;
	}

	switch (cmd) {
	case GSENSOR_IOCTL_GETDATA:
		if (copy_to_user(argp, &axis, sizeof(axis))) {
			dev_err(&client->dev, "failed to copy sense data to user space.\n");
			result = -EFAULT;
			goto error;
		}
		break;
	default:
		break;
	}

error:
	return result;
}

static int compass_dev_open(struct inode *inode, struct file *file)
{
	struct sensor_private_data *sensor = g_sensor[SENSOR_TYPE_COMPASS];
	int result = 0;
	int flag = 0;

	flag = atomic_read(&sensor->flags.open_flag);
	if (!flag) {
		atomic_set(&sensor->flags.open_flag, 1);
		wake_up(&sensor->flags.open_wq);
	}

	return result;
}

static int compass_dev_release(struct inode *inode, struct file *file)
{
	struct sensor_private_data *sensor = g_sensor[SENSOR_TYPE_COMPASS];
	int result = 0;
	int flag = 0;

	flag = atomic_read(&sensor->flags.open_flag);
	if (flag) {
		atomic_set(&sensor->flags.open_flag, 0);
		wake_up(&sensor->flags.open_wq);
	}

	return result;
}

#ifdef CONFIG_COMPAT
/* ioctl - I/O control */
static long compass_dev_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *arg64 = compat_ptr(arg);
	int result = 0;

	if (!file->f_op || !file->f_op->unlocked_ioctl) {
		printk(KERN_ERR "file->f_op or file->f_op->unlocked_ioctl is null\n");
		return -ENOTTY;
	}

	switch (cmd) {
	case COMPAT_ECS_IOCTL_APP_SET_MFLAG:
		if (file->f_op->unlocked_ioctl)
			result = file->f_op->unlocked_ioctl(file, ECS_IOCTL_APP_SET_MFLAG, (unsigned long)arg64);
		break;
	case COMPAT_ECS_IOCTL_APP_GET_MFLAG:
		if (file->f_op->unlocked_ioctl)
			result = file->f_op->unlocked_ioctl(file, ECS_IOCTL_APP_GET_MFLAG, (unsigned long)arg64);
		break;
	case COMPAT_ECS_IOCTL_APP_SET_AFLAG:
		if (file->f_op->unlocked_ioctl)
			result = file->f_op->unlocked_ioctl(file, ECS_IOCTL_APP_SET_AFLAG, (unsigned long)arg64);
		break;
	case COMPAT_ECS_IOCTL_APP_GET_AFLAG:
		if (file->f_op->unlocked_ioctl)
			result = file->f_op->unlocked_ioctl(file, ECS_IOCTL_APP_GET_AFLAG, (unsigned long)arg64);
		break;
	case COMPAT_ECS_IOCTL_APP_SET_MVFLAG:
		if (file->f_op->unlocked_ioctl)
			result = file->f_op->unlocked_ioctl(file, ECS_IOCTL_APP_SET_MVFLAG, (unsigned long)arg64);
		break;
	case COMPAT_ECS_IOCTL_APP_GET_MVFLAG:
		if (file->f_op->unlocked_ioctl)
			result = file->f_op->unlocked_ioctl(file, ECS_IOCTL_APP_GET_MVFLAG, (unsigned long)arg64);
		break;
	case COMPAT_ECS_IOCTL_APP_SET_DELAY:
		if (file->f_op->unlocked_ioctl)
			result = file->f_op->unlocked_ioctl(file, ECS_IOCTL_APP_SET_DELAY, (unsigned long)arg64);
		break;
	case COMPAT_ECS_IOCTL_APP_GET_DELAY:
		if (file->f_op->unlocked_ioctl)
			result = file->f_op->unlocked_ioctl(file, ECS_IOCTL_APP_GET_DELAY, (unsigned long)arg64);
		break;
	default:
		break;
	}

	return result;
}
#endif

/* ioctl - I/O control */
static long compass_dev_ioctl(struct file *file,
			  unsigned int cmd, unsigned long arg)
{
	struct sensor_private_data *sensor = g_sensor[SENSOR_TYPE_COMPASS];
	void __user *argp = (void __user *)arg;
	int result = 0;
	short flag;

	switch (cmd) {
	case ECS_IOCTL_APP_SET_MFLAG:
	case ECS_IOCTL_APP_SET_AFLAG:
	case ECS_IOCTL_APP_SET_MVFLAG:
		if (copy_from_user(&flag, argp, sizeof(flag)))
			return -EFAULT;
		if (flag < 0 || flag > 1)
			return -EINVAL;
		break;
	case ECS_IOCTL_APP_SET_DELAY:
		if (copy_from_user(&flag, argp, sizeof(flag)))
			return -EFAULT;
		break;
	default:
		break;
	}

	switch (cmd) {
	case ECS_IOCTL_APP_SET_MFLAG:
		atomic_set(&sensor->flags.m_flag, flag);
		break;
	case ECS_IOCTL_APP_GET_MFLAG:
		flag = atomic_read(&sensor->flags.m_flag);
		break;
	case ECS_IOCTL_APP_SET_AFLAG:
		atomic_set(&sensor->flags.a_flag, flag);
		break;
	case ECS_IOCTL_APP_GET_AFLAG:
		flag = atomic_read(&sensor->flags.a_flag);
		break;
	case ECS_IOCTL_APP_SET_MVFLAG:
		atomic_set(&sensor->flags.mv_flag, flag);
		break;
	case ECS_IOCTL_APP_GET_MVFLAG:
		flag = atomic_read(&sensor->flags.mv_flag);
		break;
	case ECS_IOCTL_APP_SET_DELAY:
		sensor->flags.delay = flag;
		break;
	case ECS_IOCTL_APP_GET_DELAY:
		flag = sensor->flags.delay;
		break;
	default:
		return -ENOTTY;
	}

	switch (cmd) {
	case ECS_IOCTL_APP_GET_MFLAG:
	case ECS_IOCTL_APP_GET_AFLAG:
	case ECS_IOCTL_APP_GET_MVFLAG:
	case ECS_IOCTL_APP_GET_DELAY:
		if (copy_to_user(argp, &flag, sizeof(flag)))
			return -EFAULT;
		break;
	default:
		break;
	}

	return result;
}

static int gyro_dev_open(struct inode *inode, struct file *file)
{
	return 0;
}


static int gyro_dev_release(struct inode *inode, struct file *file)
{
	return 0;
}

/* ioctl - I/O control */
static long gyro_dev_ioctl(struct file *file,
			  unsigned int cmd, unsigned long arg)
{
	struct sensor_private_data *sensor = g_sensor[SENSOR_TYPE_GYROSCOPE];
	struct i2c_client *client = sensor->client;
	void __user *argp = (void __user *)arg;
	int result = 0;
	int rate;

	wait_event_interruptible(sensor->is_factory_ok, (atomic_read(&sensor->is_factory) == 0));

	switch (cmd) {
	case L3G4200D_IOCTL_GET_ENABLE:
		result = !sensor->status_cur;
		if (copy_to_user(argp, &result, sizeof(result))) {
			dev_err(&client->dev, "%s:failed to copy status to user space.\n", __func__);
			return -EFAULT;
		}
		break;
	case L3G4200D_IOCTL_SET_ENABLE:
		if (copy_from_user(&result, argp, sizeof(result))) {
			dev_err(&client->dev, "%s:failed to copy gyro sensor status from user space.\n", __func__);
			return -EFAULT;
		}
		mutex_lock(&sensor->operation_mutex);
		if (result) {
			if (sensor->status_cur == SENSOR_OFF)
				sensor_enable(sensor, SENSOR_ON);
		} else {
			if (sensor->status_cur == SENSOR_ON)
				sensor_enable(sensor, SENSOR_OFF);
		}
		result = sensor->status_cur;
		if (copy_to_user(argp, &result, sizeof(result))) {
			mutex_unlock(&sensor->operation_mutex);
			dev_err(&client->dev, "%s:failed to copy sense data to user space.\n", __func__);
			return -EFAULT;
		}
		mutex_unlock(&sensor->operation_mutex);
		break;
	case L3G4200D_IOCTL_SET_DELAY:
		if (copy_from_user(&rate, argp, sizeof(rate))) {
			dev_err(&client->dev, "L3G4200D_IOCTL_SET_DELAY: copy form user failed\n");
			return -EFAULT;
		}
		mutex_lock(&sensor->operation_mutex);
		result = sensor_reset_rate(client, rate);
		if (result < 0) {
			dev_err(&client->dev, "gyro reset rate failed\n");
			mutex_unlock(&sensor->operation_mutex);
			goto error;
		}
		mutex_unlock(&sensor->operation_mutex);
		break;
	case L3G4200D_IOCTL_GET_CALIBRATION:
		if (sensor_cali_data.is_gyro_calibrated != 1) {
			if (sensor_calibration_data_read(&sensor_cali_data)) {
				dev_err(&client->dev, "failed to read gyro offset data from storage\n");
				result = -EFAULT;
				goto error;
			}
		}
		if (sensor_cali_data.is_gyro_calibrated == 1) {
			if (copy_to_user(argp, sensor_cali_data.gyro_offset, sizeof(sensor_cali_data.gyro_offset))) {
				dev_err(&client->dev, "failed to copy gyro offset data to user\n");
				result = -EFAULT;
				goto error;
			}
		}
		break;
	default:
		return -ENOTTY;
	}

error:
	return result;
}

static int light_dev_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int light_dev_release(struct inode *inode, struct file *file)
{
	return 0;
}

#ifdef CONFIG_COMPAT
static long light_dev_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long ret = 0;
	void __user *arg64 = compat_ptr(arg);

	if (!file->f_op || !file->f_op->unlocked_ioctl) {
		printk(KERN_ERR "[DEBUG] file->f_op or file->f_op->unlocked_ioctl is null\n");
		return -ENOTTY;
	}

	switch (cmd) {
	case COMPAT_LIGHTSENSOR_IOCTL_GET_ENABLED:
		if (file->f_op->unlocked_ioctl)
			ret = file->f_op->unlocked_ioctl(file, LIGHTSENSOR_IOCTL_GET_ENABLED, (unsigned long)arg64);
		break;
	case COMPAT_LIGHTSENSOR_IOCTL_ENABLE:
		if (file->f_op->unlocked_ioctl)
			ret = file->f_op->unlocked_ioctl(file, LIGHTSENSOR_IOCTL_ENABLE, (unsigned long)arg64);
		break;
	case COMPAT_LIGHTSENSOR_IOCTL_SET_RATE:
		if (file->f_op->unlocked_ioctl)
			ret = file->f_op->unlocked_ioctl(file, LIGHTSENSOR_IOCTL_SET_RATE, (unsigned long)arg64);
		break;
	default:
		break;
	}

	return ret;
}
#endif

/* ioctl - I/O control */
static long light_dev_ioctl(struct file *file,
			  unsigned int cmd, unsigned long arg)
{
	struct sensor_private_data *sensor = g_sensor[SENSOR_TYPE_LIGHT];
	struct i2c_client *client = sensor->client;
	void __user *argp = (void __user *)arg;
	int result = 0;
	short rate;

	switch (cmd) {
	case LIGHTSENSOR_IOCTL_SET_RATE:
		if (copy_from_user(&rate, argp, sizeof(rate))) {
			dev_err(&client->dev, "%s:failed to copy light sensor rate from user space.\n", __func__);
			return -EFAULT;
		}
		mutex_lock(&sensor->operation_mutex);
		result = sensor_reset_rate(client, rate);
		if (result < 0) {
			mutex_unlock(&sensor->operation_mutex);
			goto error;
		}
		mutex_unlock(&sensor->operation_mutex);
		break;
	case LIGHTSENSOR_IOCTL_GET_ENABLED:
		result = sensor->status_cur;
		if (copy_to_user(argp, &result, sizeof(result))) {
			dev_err(&client->dev, "%s:failed to copy light sensor status to user space.\n", __func__);
			return -EFAULT;
		}
		break;
	case LIGHTSENSOR_IOCTL_ENABLE:
		if (copy_from_user(&result, argp, sizeof(result))) {
			dev_err(&client->dev, "%s:failed to copy light sensor status from user space.\n", __func__);
			return -EFAULT;
		}

		mutex_lock(&sensor->operation_mutex);
		if (result) {
			if (sensor->status_cur == SENSOR_OFF)
				sensor_enable(sensor, SENSOR_ON);
		} else {
			if (sensor->status_cur == SENSOR_ON)
				sensor_enable(sensor, SENSOR_OFF);
		}
		mutex_unlock(&sensor->operation_mutex);
		break;

	default:
		break;
	}

error:
	return result;
}

static int proximity_dev_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int proximity_dev_release(struct inode *inode, struct file *file)
{
	return 0;
}

#ifdef CONFIG_COMPAT
static long proximity_dev_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long ret = 0;
	void __user *arg64 = compat_ptr(arg);

	if (!file->f_op || !file->f_op->unlocked_ioctl) {
		printk(KERN_ERR "file->f_op or file->f_op->unlocked_ioctl is null\n");
		return -ENOTTY;
	}

	switch (cmd) {
	case COMPAT_PSENSOR_IOCTL_GET_ENABLED:
		if (file->f_op->unlocked_ioctl)
			ret = file->f_op->unlocked_ioctl(file, PSENSOR_IOCTL_GET_ENABLED, (unsigned long)arg64);
		break;
	case COMPAT_PSENSOR_IOCTL_ENABLE:
		if (file->f_op->unlocked_ioctl)
			ret = file->f_op->unlocked_ioctl(file, PSENSOR_IOCTL_ENABLE, (unsigned long)arg64);
		break;
	default:
		break;
	}

	return ret;
}
#endif

/* ioctl - I/O control */
static long proximity_dev_ioctl(struct file *file,
			  unsigned int cmd, unsigned long arg)
{
	struct sensor_private_data *sensor = g_sensor[SENSOR_TYPE_PROXIMITY];
	void __user *argp = (void __user *)arg;
	int result = 0;

	switch (cmd) {
	case PSENSOR_IOCTL_GET_ENABLED:
		result = sensor->status_cur;
		if (copy_to_user(argp, &result, sizeof(result))) {
			dev_err(&sensor->client->dev, "%s:failed to copy psensor status to user space.\n", __func__);
			return -EFAULT;
		}
		break;
	case PSENSOR_IOCTL_ENABLE:
		if (copy_from_user(&result, argp, sizeof(result))) {
			dev_err(&sensor->client->dev, "%s:failed to copy psensor status from user space.\n", __func__);
			return -EFAULT;
		}
		mutex_lock(&sensor->operation_mutex);
		if (result) {
			if (sensor->status_cur == SENSOR_OFF)
				sensor_enable(sensor, SENSOR_ON);
		} else {
			if (sensor->status_cur == SENSOR_ON)
				sensor_enable(sensor, SENSOR_OFF);
		}
		mutex_unlock(&sensor->operation_mutex);
		break;

	default:
		break;
	}

	return result;
}

static int temperature_dev_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int temperature_dev_release(struct inode *inode, struct file *file)
{
	return 0;
}

/* ioctl - I/O control */
static long temperature_dev_ioctl(struct file *file,
			  unsigned int cmd, unsigned long arg)
{
	struct sensor_private_data *sensor = g_sensor[SENSOR_TYPE_TEMPERATURE];
	void __user *argp = (void __user *)arg;
	int result = 0;

	switch (cmd) {
	case TEMPERATURE_IOCTL_GET_ENABLED:
		result = sensor->status_cur;
		if (copy_to_user(argp, &result, sizeof(result))) {
			dev_err(&sensor->client->dev, "%s:failed to copy temperature sensor status to user space.\n", __func__);
			return -EFAULT;
		}
		break;
	case TEMPERATURE_IOCTL_ENABLE:
		if (copy_from_user(&result, argp, sizeof(result))) {
			dev_err(&sensor->client->dev, "%s:failed to copy temperature sensor status from user space.\n", __func__);
			return -EFAULT;
		}
		mutex_lock(&sensor->operation_mutex);
		if (result) {
			if (sensor->status_cur == SENSOR_OFF)
				sensor_enable(sensor, SENSOR_ON);
		} else {
			if (sensor->status_cur == SENSOR_ON)
				sensor_enable(sensor, SENSOR_OFF);
		}
		mutex_unlock(&sensor->operation_mutex);
		break;

	default:
		break;
	}

	return result;
}


static int pressure_dev_open(struct inode *inode, struct file *file)
{
	return 0;
}


static int pressure_dev_release(struct inode *inode, struct file *file)
{
	return 0;
}


/* ioctl - I/O control */
static long pressure_dev_ioctl(struct file *file,
			  unsigned int cmd, unsigned long arg)
{
	struct sensor_private_data *sensor = g_sensor[SENSOR_TYPE_PRESSURE];
	void __user *argp = (void __user *)arg;
	int result = 0;

	switch (cmd) {
	case PRESSURE_IOCTL_GET_ENABLED:
		result = sensor->status_cur;
		if (copy_to_user(argp, &result, sizeof(result))) {
			dev_err(&sensor->client->dev, "%s:failed to copy pressure sensor status to user space.\n", __func__);
			return -EFAULT;
		}
		break;
	case PRESSURE_IOCTL_ENABLE:
		if (copy_from_user(&result, argp, sizeof(result))) {
			dev_err(&sensor->client->dev, "%s:failed to copy pressure sensor status from user space.\n", __func__);
			return -EFAULT;
		}
		mutex_lock(&sensor->operation_mutex);
		if (result) {
			if (sensor->status_cur == SENSOR_OFF)
				sensor_enable(sensor, SENSOR_ON);
		} else {
			if (sensor->status_cur == SENSOR_ON)
				sensor_enable(sensor, SENSOR_OFF);
		}
		mutex_unlock(&sensor->operation_mutex);
		break;

	default:
		break;
	}

	return result;
}

static int sensor_misc_device_register(struct sensor_private_data *sensor, int type)
{
	int result = 0;

	switch (type) {
	case SENSOR_TYPE_ANGLE:
		if (!sensor->ops->misc_dev) {
			sensor->fops.owner = THIS_MODULE;
			sensor->fops.unlocked_ioctl = angle_dev_ioctl;
			sensor->fops.open = angle_dev_open;
			sensor->fops.release = angle_dev_release;

			sensor->miscdev.minor = MISC_DYNAMIC_MINOR;
			sensor->miscdev.name = "angle";
			sensor->miscdev.fops = &sensor->fops;
		} else {
			memcpy(&sensor->miscdev, sensor->ops->misc_dev, sizeof(*sensor->ops->misc_dev));
		}
		break;

	case SENSOR_TYPE_ACCEL:
		if (!sensor->ops->misc_dev) {
			sensor->fops.owner = THIS_MODULE;
			sensor->fops.unlocked_ioctl = gsensor_dev_ioctl;
			#ifdef CONFIG_COMPAT
			sensor->fops.compat_ioctl = gsensor_dev_ioctl;
			#endif
			sensor->fops.open = gsensor_dev_open;
			sensor->fops.release = gsensor_dev_release;

			sensor->miscdev.minor = MISC_DYNAMIC_MINOR;
			sensor->miscdev.name = "mma8452_daemon";
			sensor->miscdev.fops = &sensor->fops;
		} else {
			memcpy(&sensor->miscdev, sensor->ops->misc_dev, sizeof(*sensor->ops->misc_dev));
		}
		break;

	case SENSOR_TYPE_COMPASS:
		if (!sensor->ops->misc_dev) {
			sensor->fops.owner = THIS_MODULE;
			sensor->fops.unlocked_ioctl = compass_dev_ioctl;
			#ifdef CONFIG_COMPAT
			sensor->fops.compat_ioctl = compass_dev_compat_ioctl;
			#endif
			sensor->fops.open = compass_dev_open;
			sensor->fops.release = compass_dev_release;

			sensor->miscdev.minor = MISC_DYNAMIC_MINOR;
			sensor->miscdev.name = "compass";
			sensor->miscdev.fops = &sensor->fops;
		} else {
			memcpy(&sensor->miscdev, sensor->ops->misc_dev, sizeof(*sensor->ops->misc_dev));
		}
		break;

	case SENSOR_TYPE_GYROSCOPE:
		if (!sensor->ops->misc_dev) {
			sensor->fops.owner = THIS_MODULE;
			sensor->fops.unlocked_ioctl = gyro_dev_ioctl;
			sensor->fops.open = gyro_dev_open;
			sensor->fops.release = gyro_dev_release;

			sensor->miscdev.minor = MISC_DYNAMIC_MINOR;
			sensor->miscdev.name = "gyrosensor";
			sensor->miscdev.fops = &sensor->fops;
		} else {
			memcpy(&sensor->miscdev, sensor->ops->misc_dev, sizeof(*sensor->ops->misc_dev));
		}
		break;

	case SENSOR_TYPE_LIGHT:
		if (!sensor->ops->misc_dev) {
			sensor->fops.owner = THIS_MODULE;
			sensor->fops.unlocked_ioctl = light_dev_ioctl;
			#ifdef CONFIG_COMPAT
			sensor->fops.compat_ioctl = light_dev_compat_ioctl;
			#endif
			sensor->fops.open = light_dev_open;
			sensor->fops.release = light_dev_release;

			sensor->miscdev.minor = MISC_DYNAMIC_MINOR;
			sensor->miscdev.name = "lightsensor";
			sensor->miscdev.fops = &sensor->fops;
		} else {
			memcpy(&sensor->miscdev, sensor->ops->misc_dev, sizeof(*sensor->ops->misc_dev));
		}
		break;

	case SENSOR_TYPE_PROXIMITY:
		if (!sensor->ops->misc_dev) {
			sensor->fops.owner = THIS_MODULE;
			sensor->fops.unlocked_ioctl = proximity_dev_ioctl;
			#ifdef CONFIG_COMPAT
			sensor->fops.compat_ioctl = proximity_dev_compat_ioctl;
			#endif
			sensor->fops.open = proximity_dev_open;
			sensor->fops.release = proximity_dev_release;

			sensor->miscdev.minor = MISC_DYNAMIC_MINOR;
			sensor->miscdev.name = "psensor";
			sensor->miscdev.fops = &sensor->fops;
		} else {
			memcpy(&sensor->miscdev, sensor->ops->misc_dev, sizeof(*sensor->ops->misc_dev));
		}
		break;

	case SENSOR_TYPE_TEMPERATURE:
		if (!sensor->ops->misc_dev) {
			sensor->fops.owner = THIS_MODULE;
			sensor->fops.unlocked_ioctl = temperature_dev_ioctl;
			sensor->fops.open = temperature_dev_open;
			sensor->fops.release = temperature_dev_release;

			sensor->miscdev.minor = MISC_DYNAMIC_MINOR;
			sensor->miscdev.name = "temperature";
			sensor->miscdev.fops = &sensor->fops;
		} else {
			memcpy(&sensor->miscdev, sensor->ops->misc_dev, sizeof(*sensor->ops->misc_dev));
		}
		break;

	case SENSOR_TYPE_PRESSURE:
		if (!sensor->ops->misc_dev) {
			sensor->fops.owner = THIS_MODULE;
			sensor->fops.unlocked_ioctl = pressure_dev_ioctl;
			sensor->fops.open = pressure_dev_open;
			sensor->fops.release = pressure_dev_release;

			sensor->miscdev.minor = MISC_DYNAMIC_MINOR;
			sensor->miscdev.name = "pressure";
			sensor->miscdev.fops = &sensor->fops;
		} else {
			memcpy(&sensor->miscdev, sensor->ops->misc_dev, sizeof(*sensor->ops->misc_dev));
		}
		break;

	default:
		dev_err(&sensor->client->dev, "%s:unknow sensor type=%d\n", __func__, type);
		result = -1;
		goto error;
	}

	sensor->miscdev.parent = &sensor->client->dev;
	result = misc_register(&sensor->miscdev);
	if (result < 0) {
		dev_err(&sensor->client->dev,
			"fail to register misc device %s\n", sensor->miscdev.name);
		goto error;
	}
	dev_info(&sensor->client->dev, "%s:miscdevice: %s\n", __func__, sensor->miscdev.name);

error:
	return result;
}

int sensor_register_slave(int type, struct i2c_client *client,
			struct sensor_platform_data *slave_pdata,
			struct sensor_operate *(*get_sensor_ops)(void))
{
	int result = 0;
	struct sensor_operate *ops = get_sensor_ops();

	if ((ops->id_i2c >= SENSOR_NUM_ID) || (ops->id_i2c <= ID_INVALID)) {
		printk(KERN_ERR "%s:%s id is error %d\n", __func__, ops->name, ops->id_i2c);
		return -1;
	}
	sensor_ops[ops->id_i2c] = ops;
	sensor_probe_times[ops->id_i2c] = 0;

	printk(KERN_INFO "%s:%s,id=%d\n", __func__, sensor_ops[ops->id_i2c]->name, ops->id_i2c);

	return result;
}

int sensor_unregister_slave(int type, struct i2c_client *client,
			struct sensor_platform_data *slave_pdata,
			struct sensor_operate *(*get_sensor_ops)(void))
{
	int result = 0;
	struct sensor_operate *ops = get_sensor_ops();

	if ((ops->id_i2c >= SENSOR_NUM_ID) || (ops->id_i2c <= ID_INVALID)) {
		printk(KERN_ERR "%s:%s id is error %d\n", __func__, ops->name, ops->id_i2c);
		return -1;
	}
	printk(KERN_INFO "%s:%s,id=%d\n", __func__, sensor_ops[ops->id_i2c]->name, ops->id_i2c);
	sensor_ops[ops->id_i2c] = NULL;

	return result;
}

int sensor_probe(struct i2c_client *client, const struct i2c_device_id *devid)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);
	struct sensor_platform_data *pdata;
	struct device_node *np = client->dev.of_node;
	enum of_gpio_flags rst_flags, pwr_flags;
	unsigned long irq_flags;
	int result = 0;
	int type = 0;
	int reprobe_en = 0;

	dev_info(&client->adapter->dev, "%s: %s,%p\n", __func__, devid->name, client);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		result = -ENODEV;
		goto out_no_free;
	}
	if (!np) {
		dev_err(&client->dev, "no device tree\n");
		return -EINVAL;
	}
	pdata = devm_kzalloc(&client->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		result = -ENOMEM;
		goto out_no_free;
	}
	sensor = devm_kzalloc(&client->dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor) {
		result = -ENOMEM;
		goto out_no_free;
	}

	of_property_read_u32(np, "type", &(pdata->type));

	pdata->irq_pin = of_get_named_gpio_flags(np, "irq-gpio", 0, (enum of_gpio_flags *)&irq_flags);
	pdata->reset_pin = of_get_named_gpio_flags(np, "reset-gpio", 0, &rst_flags);
	pdata->power_pin = of_get_named_gpio_flags(np, "power-gpio", 0, &pwr_flags);

	of_property_read_u32(np, "irq_enable", &(pdata->irq_enable));
	of_property_read_u32(np, "poll_delay_ms", &(pdata->poll_delay_ms));

	of_property_read_u32(np, "x_min", &(pdata->x_min));
	of_property_read_u32(np, "y_min", &(pdata->y_min));
	of_property_read_u32(np, "z_min", &(pdata->z_min));
	of_property_read_u32(np, "factory", &(pdata->factory));
	of_property_read_u32(np, "layout", &(pdata->layout));
	of_property_read_u32(np, "reprobe_en", &reprobe_en);

	of_property_read_u8(np, "address", &(pdata->address));
	of_get_property(np, "project_name", pdata->project_name);

	of_property_read_u32(np, "power-off-in-suspend",
			     &pdata->power_off_in_suspend);

	switch (pdata->layout) {
	case 1:
		pdata->orientation[0] = 1;
		pdata->orientation[1] = 0;
		pdata->orientation[2] = 0;

		pdata->orientation[3] = 0;
		pdata->orientation[4] = 1;
		pdata->orientation[5] = 0;

		pdata->orientation[6] = 0;
		pdata->orientation[7] = 0;
		pdata->orientation[8] = 1;
		break;

	case 2:
		pdata->orientation[0] = 0;
		pdata->orientation[1] = -1;
		pdata->orientation[2] = 0;

		pdata->orientation[3] = 1;
		pdata->orientation[4] = 0;
		pdata->orientation[5] = 0;

		pdata->orientation[6] = 0;
		pdata->orientation[7] = 0;
		pdata->orientation[8] = 1;
		break;

	case 3:
		pdata->orientation[0] = -1;
		pdata->orientation[1] = 0;
		pdata->orientation[2] = 0;

		pdata->orientation[3] = 0;
		pdata->orientation[4] = -1;
		pdata->orientation[5] = 0;

		pdata->orientation[6] = 0;
		pdata->orientation[7] = 0;
		pdata->orientation[8] = 1;
		break;

	case 4:
		pdata->orientation[0] = 0;
		pdata->orientation[1] = 1;
		pdata->orientation[2] = 0;

		pdata->orientation[3] = -1;
		pdata->orientation[4] = 0;
		pdata->orientation[5] = 0;

		pdata->orientation[6] = 0;
		pdata->orientation[7] = 0;
		pdata->orientation[8] = 1;
		break;

	case 5:
		pdata->orientation[0] = 1;
		pdata->orientation[1] = 0;
		pdata->orientation[2] = 0;

		pdata->orientation[3] = 0;
		pdata->orientation[4] = -1;
		pdata->orientation[5] = 0;

		pdata->orientation[6] = 0;
		pdata->orientation[7] = 0;
		pdata->orientation[8] = -1;
		break;

	case 6:
		pdata->orientation[0] = 0;
		pdata->orientation[1] = -1;
		pdata->orientation[2] = 0;

		pdata->orientation[3] = -1;
		pdata->orientation[4] = 0;
		pdata->orientation[5] = 0;

		pdata->orientation[6] = 0;
		pdata->orientation[7] = 0;
		pdata->orientation[8] = -1;
		break;

	case 7:
		pdata->orientation[0] = -1;
		pdata->orientation[1] = 0;
		pdata->orientation[2] = 0;

		pdata->orientation[3] = 0;
		pdata->orientation[4] = 1;
		pdata->orientation[5] = 0;

		pdata->orientation[6] = 0;
		pdata->orientation[7] = 0;
		pdata->orientation[8] = -1;
		break;

	case 8:
		pdata->orientation[0] = 0;
		pdata->orientation[1] = 1;
		pdata->orientation[2] = 0;

		pdata->orientation[3] = 1;
		pdata->orientation[4] = 0;
		pdata->orientation[5] = 0;

		pdata->orientation[6] = 0;
		pdata->orientation[7] = 0;
		pdata->orientation[8] = -1;
		break;

	default:
		pdata->orientation[0] = 1;
		pdata->orientation[1] = 0;
		pdata->orientation[2] = 0;

		pdata->orientation[3] = 0;
		pdata->orientation[4] = 1;
		pdata->orientation[5] = 0;

		pdata->orientation[6] = 0;
		pdata->orientation[7] = 0;
		pdata->orientation[8] = 1;
		break;
	}

	client->irq = pdata->irq_pin;
	type = pdata->type;
	pdata->irq_flags = irq_flags;
	pdata->poll_delay_ms = 30;

	if ((type >= SENSOR_NUM_TYPES) || (type <= SENSOR_TYPE_NULL)) {
		dev_err(&client->adapter->dev, "sensor type is error %d\n", type);
		result = -EFAULT;
		goto out_no_free;
	}
	if (((int)devid->driver_data >= SENSOR_NUM_ID) || ((int)devid->driver_data <= ID_INVALID)) {
		dev_err(&client->adapter->dev, "sensor id is error %d\n", (int)devid->driver_data);
		result = -EFAULT;
		goto out_no_free;
	}
	i2c_set_clientdata(client, sensor);
	sensor->client = client;
	sensor->pdata = pdata;
	sensor->type = type;
	sensor->i2c_id = (struct i2c_device_id *)devid;

	memset(&(sensor->axis), 0, sizeof(struct sensor_axis));
	mutex_init(&sensor->data_mutex);
	mutex_init(&sensor->operation_mutex);
	mutex_init(&sensor->sensor_mutex);
	mutex_init(&sensor->i2c_mutex);

	atomic_set(&sensor->is_factory, 0);
	init_waitqueue_head(&sensor->is_factory_ok);

	/* As default, report all information */
	atomic_set(&sensor->flags.m_flag, 1);
	atomic_set(&sensor->flags.a_flag, 1);
	atomic_set(&sensor->flags.mv_flag, 1);
	atomic_set(&sensor->flags.open_flag, 0);
	atomic_set(&sensor->flags.debug_flag, 1);
	init_waitqueue_head(&sensor->flags.open_wq);
	sensor->flags.delay = 100;

	sensor->status_cur = SENSOR_OFF;
	sensor->axis.x = 0;
	sensor->axis.y = 0;
	sensor->axis.z = 0;

	result = sensor_chip_init(sensor->client);
	if (result < 0) {
		if (reprobe_en && (result == -2)) {
			sensor_probe_times[sensor->ops->id_i2c]++;
			if (sensor_probe_times[sensor->ops->id_i2c] < 3)
				result = -EPROBE_DEFER;
		}
		goto out_free_memory;
	}

	sensor->input_dev = devm_input_allocate_device(&client->dev);
	if (!sensor->input_dev) {
		result = -ENOMEM;
		dev_err(&client->dev,
			"Failed to allocate input device\n");
		goto out_free_memory;
	}

	switch (type) {
	case SENSOR_TYPE_ANGLE:
		sensor->input_dev->name = "angle";
		set_bit(EV_ABS, sensor->input_dev->evbit);
		/* x-axis acceleration */
		input_set_abs_params(sensor->input_dev, ABS_X, sensor->ops->range[0], sensor->ops->range[1], 0, 0);
		/* y-axis acceleration */
		input_set_abs_params(sensor->input_dev, ABS_Y, sensor->ops->range[0], sensor->ops->range[1], 0, 0);
		/* z-axis acceleration */
		input_set_abs_params(sensor->input_dev, ABS_Z, sensor->ops->range[0], sensor->ops->range[1], 0, 0);

	case SENSOR_TYPE_ACCEL:
		sensor->input_dev->name = "gsensor";
		set_bit(EV_ABS, sensor->input_dev->evbit);
		/* x-axis acceleration */
		input_set_abs_params(sensor->input_dev, ABS_X, sensor->ops->range[0], sensor->ops->range[1], 0, 0);
		/* y-axis acceleration */
		input_set_abs_params(sensor->input_dev, ABS_Y, sensor->ops->range[0], sensor->ops->range[1], 0, 0);
		/* z-axis acceleration */
		input_set_abs_params(sensor->input_dev, ABS_Z, sensor->ops->range[0], sensor->ops->range[1], 0, 0);
		break;
	case SENSOR_TYPE_COMPASS:
		sensor->input_dev->name = "compass";
		/* Setup input device */
		set_bit(EV_ABS, sensor->input_dev->evbit);
		/* yaw (0, 360) */
		input_set_abs_params(sensor->input_dev, ABS_RX, 0, 23040, 0, 0);
		/* pitch (-180, 180) */
		input_set_abs_params(sensor->input_dev, ABS_RY, -11520, 11520, 0, 0);
		/* roll (-90, 90) */
		input_set_abs_params(sensor->input_dev, ABS_RZ, -5760, 5760, 0, 0);
		/* x-axis acceleration (720 x 8G) */
		input_set_abs_params(sensor->input_dev, ABS_X, -5760, 5760, 0, 0);
		/* y-axis acceleration (720 x 8G) */
		input_set_abs_params(sensor->input_dev, ABS_Y, -5760, 5760, 0, 0);
		/* z-axis acceleration (720 x 8G) */
		input_set_abs_params(sensor->input_dev, ABS_Z, -5760, 5760, 0, 0);
		/* status of magnetic sensor */
		input_set_abs_params(sensor->input_dev, ABS_RUDDER, -32768, 3, 0, 0);
		/* status of acceleration sensor */
		input_set_abs_params(sensor->input_dev, ABS_WHEEL, -32768, 3, 0, 0);
		/* x-axis of raw magnetic vector (-4096, 4095) */
		input_set_abs_params(sensor->input_dev, ABS_HAT0X, -20480, 20479, 0, 0);
		/* y-axis of raw magnetic vector (-4096, 4095) */
		input_set_abs_params(sensor->input_dev, ABS_HAT0Y, -20480, 20479, 0, 0);
		/* z-axis of raw magnetic vector (-4096, 4095) */
		input_set_abs_params(sensor->input_dev, ABS_BRAKE, -20480, 20479, 0, 0);
		break;
	case SENSOR_TYPE_GYROSCOPE:
		sensor->input_dev->name = "gyro";
		/* x-axis acceleration */
		input_set_capability(sensor->input_dev, EV_REL, REL_RX);
		input_set_abs_params(sensor->input_dev, ABS_RX, sensor->ops->range[0], sensor->ops->range[1], 0, 0);
		/* y-axis acceleration */
		input_set_capability(sensor->input_dev, EV_REL, REL_RY);
		input_set_abs_params(sensor->input_dev, ABS_RY, sensor->ops->range[0], sensor->ops->range[1], 0, 0);
		/* z-axis acceleration */
		input_set_capability(sensor->input_dev, EV_REL, REL_RZ);
		input_set_abs_params(sensor->input_dev, ABS_RZ, sensor->ops->range[0], sensor->ops->range[1], 0, 0);
		break;
	case SENSOR_TYPE_LIGHT:
		sensor->input_dev->name = "lightsensor-level";
		set_bit(EV_ABS, sensor->input_dev->evbit);
		input_set_abs_params(sensor->input_dev, ABS_MISC, sensor->ops->range[0], sensor->ops->range[1], 0, 0);
		input_set_abs_params(sensor->input_dev, ABS_TOOL_WIDTH,  sensor->ops->brightness[0], sensor->ops->brightness[1], 0, 0);
		break;
	case SENSOR_TYPE_PROXIMITY:
		sensor->input_dev->name = "proximity";
		set_bit(EV_ABS, sensor->input_dev->evbit);
		input_set_abs_params(sensor->input_dev, ABS_DISTANCE, sensor->ops->range[0], sensor->ops->range[1], 0, 0);
		break;
	case SENSOR_TYPE_TEMPERATURE:
		sensor->input_dev->name = "temperature";
		set_bit(EV_ABS, sensor->input_dev->evbit);
		input_set_abs_params(sensor->input_dev, ABS_THROTTLE, sensor->ops->range[0], sensor->ops->range[1], 0, 0);
		break;
	case SENSOR_TYPE_PRESSURE:
		sensor->input_dev->name = "pressure";
		set_bit(EV_ABS, sensor->input_dev->evbit);
		input_set_abs_params(sensor->input_dev, ABS_PRESSURE, sensor->ops->range[0], sensor->ops->range[1], 0, 0);
		break;
	default:
		dev_err(&client->dev, "%s:unknow sensor type=%d\n", __func__, type);
		break;
	}
	sensor->input_dev->dev.parent = &client->dev;

	result = input_register_device(sensor->input_dev);
	if (result) {
		dev_err(&client->dev,
			"Unable to register input device %s\n", sensor->input_dev->name);
		goto out_input_register_device_failed;
	}

	result = sensor_irq_init(sensor->client);
	if (result) {
		dev_err(&client->dev,
			"fail to init sensor irq,ret=%d\n", result);
		goto out_input_register_device_failed;
	}

	sensor->miscdev.parent = &client->dev;
	result = sensor_misc_device_register(sensor, type);
	if (result) {
		dev_err(&client->dev,
			"fail to register misc device %s\n", sensor->miscdev.name);
		goto out_misc_device_register_device_failed;
	}

	g_sensor[type] = sensor;

#ifdef CONFIG_HAS_EARLYSUSPEND
	if ((sensor->ops->suspend) && (sensor->ops->resume)) {
		sensor->early_suspend.suspend = sensor_suspend;
		sensor->early_suspend.resume = sensor_resume;
		sensor->early_suspend.level = 0x02;
		register_early_suspend(&sensor->early_suspend);
	}
#endif

	dev_info(&client->dev, "%s:initialized ok,sensor name:%s,type:%d,id=%d\n\n", __func__, sensor->ops->name, type, (int)sensor->i2c_id->driver_data);

	return result;

out_misc_device_register_device_failed:
out_input_register_device_failed:
out_free_memory:
out_no_free:
	dev_err(&client->adapter->dev, "%s failed %d\n\n", __func__, result);
	return result;
}

static void sensor_shut_down(struct i2c_client *client)
{
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);

	if ((sensor->ops->suspend) && (sensor->ops->resume))
		unregister_early_suspend(&sensor->early_suspend);
#endif
}

static int sensor_remove(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);

	sensor->stop_work = 1;
	cancel_delayed_work_sync(&sensor->delaywork);
	misc_deregister(&sensor->miscdev);
#ifdef CONFIG_HAS_EARLYSUSPEND
	if ((sensor->ops->suspend) && (sensor->ops->resume))
		unregister_early_suspend(&sensor->early_suspend);
#endif

	return 0;
}

static const struct i2c_device_id sensor_id[] = {
	/*angle*/
	{"angle_kxtik", ANGLE_ID_KXTIK},
	{"angle_lis3dh", ANGLE_ID_LIS3DH},
	/*gsensor*/
	{"gsensor", ACCEL_ID_ALL},
	{"gs_mma8452", ACCEL_ID_MMA845X},
	{"gs_kxtik", ACCEL_ID_KXTIK},
	{"gs_kxtj9", ACCEL_ID_KXTJ9},
	{"gs_lis3dh", ACCEL_ID_LIS3DH},
	{"gs_mma7660", ACCEL_ID_MMA7660},
	{"gs_mxc6225", ACCEL_ID_MXC6225},
	{"gs_dmard10", ACCEL_ID_DMARD10},
	{"gs_lsm303d", ACCEL_ID_LSM303D},
	{"gs_mc3230", ACCEL_ID_MC3230},
	{"mpu6880_acc", ACCEL_ID_MPU6880},
	{"mpu6500_acc", ACCEL_ID_MPU6500},
	{"lsm330_acc", ACCEL_ID_LSM330},
	{"bma2xx_acc", ACCEL_ID_BMA2XX},
	{"gs_stk8baxx", ACCEL_ID_STK8BAXX},
	/*compass*/
	{"compass", COMPASS_ID_ALL},
	{"ak8975", COMPASS_ID_AK8975},
	{"ak8963", COMPASS_ID_AK8963},
	{"ak09911", COMPASS_ID_AK09911},
	{"mmc314x", COMPASS_ID_MMC314X},
	/*gyroscope*/
	{"gyro", GYRO_ID_ALL},
	{"l3g4200d_gyro", GYRO_ID_L3G4200D},
	{"l3g20d_gyro", GYRO_ID_L3G20D},
	{"ewtsa_gyro", GYRO_ID_EWTSA},
	{"k3g", GYRO_ID_K3G},
	{"mpu6500_gyro", GYRO_ID_MPU6500},
	{"mpu6880_gyro", GYRO_ID_MPU6880},
	{"lsm330_gyro", GYRO_ID_LSM330},
	/*light sensor*/
	{"lightsensor", LIGHT_ID_ALL},
	{"light_cm3217", LIGHT_ID_CM3217},
	{"light_cm3218", LIGHT_ID_CM3218},
	{"light_cm3232", LIGHT_ID_CM3232},
	{"light_al3006", LIGHT_ID_AL3006},
	{"ls_stk3171", LIGHT_ID_STK3171},
	{"ls_isl29023", LIGHT_ID_ISL29023},
	{"ls_ap321xx", LIGHT_ID_AP321XX},
	{"ls_photoresistor", LIGHT_ID_PHOTORESISTOR},
	{"ls_us5152", LIGHT_ID_US5152},
	{"ls_stk3410", LIGHT_ID_STK3410},
	/*proximity sensor*/
	{"psensor", PROXIMITY_ID_ALL},
	{"proximity_al3006", PROXIMITY_ID_AL3006},
	{"ps_stk3171", PROXIMITY_ID_STK3171},
	{"ps_ap321xx", PROXIMITY_ID_AP321XX},
	{"ps_stk3410", PROXIMITY_ID_STK3410},
	/*temperature*/
	{"temperature", TEMPERATURE_ID_ALL},
	{"tmp_ms5607", TEMPERATURE_ID_MS5607},

	/*pressure*/
	{"pressure", PRESSURE_ID_ALL},
	{"pr_ms5607", PRESSURE_ID_MS5607},

	{},
};

static struct of_device_id sensor_dt_ids[] = {
	/*gsensor*/
	{ .compatible = "gs_mma8452" },
	{ .compatible = "gs_lis3dh" },
	{ .compatible = "gs_lsm303d" },
	{ .compatible = "gs_mma7660" },
	{ .compatible = "gs_mxc6225" },
	{ .compatible = "gs_mc3230" },
	{ .compatible = "lsm330_acc" },
	{ .compatible = "bma2xx_acc" },
	{ .compatible = "gs_stk8baxx" },
	/*compass*/
	{ .compatible = "ak8975" },
	{ .compatible = "ak8963" },
	{ .compatible = "ak09911" },
	{ .compatible = "mmc314x" },

	/* gyroscop*/
	{ .compatible = "l3g4200d_gyro" },
	{ .compatible = "l3g20d_gyro" },
	{ .compatible = "ewtsa_gyro" },
	{ .compatible = "k3g" },
	{ .compatible = "lsm330_gyro" },

	/*light sensor*/
	{ .compatible = "light_cm3217" },
	{ .compatible = "light_cm3232" },
	{ .compatible = "light_al3006" },
	{ .compatible = "ls_stk3171" },
	{ .compatible = "ls_ap321xx" },

	{ .compatible = "ls_photoresistor" },
	{ .compatible = "ls_us5152" },
	{ .compatible = "ls_stk3410" },
	{ .compatible = "ps_stk3410" },

	/*temperature sensor*/
	{ .compatible = "tmp_ms5607" },

	/*pressure sensor*/
	{ .compatible = "pr_ms5607" },

	/*hall sensor*/
	{ .compatible = "hall_och165t" },
	{ }
};

static struct i2c_driver sensor_driver = {
	.probe = sensor_probe,
	.remove = sensor_remove,
	.shutdown = sensor_shut_down,
	.id_table = sensor_id,
	.driver = {
		.name = "sensors",
		.of_match_table = of_match_ptr(sensor_dt_ids),
		.pm = SENSOR_PM_OPS,
	},
};

static int __init sensor_init(void)
{
	sensor_class_init();
	return i2c_add_driver(&sensor_driver);
}

static void __exit sensor_exit(void)
{
	i2c_del_driver(&sensor_driver);
}

late_initcall(sensor_init);
module_exit(sensor_exit);

MODULE_AUTHOR("ROCKCHIP Corporation:lw@rock-chips.com");
MODULE_DESCRIPTION("User space character device interface for sensors");
MODULE_LICENSE("GPL");
