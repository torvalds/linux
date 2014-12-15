/*
 * This software program is licensed subject to the GNU General Public License
 * (GPL).Version 2,June 1991, available at http://www.fsf.org/copyleft/gpl.html

 * (C) Copyright 2011 Thundersoft.LTD 
 * All Rights Reserved
 */

/* file CM36283.c
   brief: This file contains all function implementations for the CM36283 in linux
*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/mutex.h>

#include <linux/sensor/sensor_common.h>

#define VENDOR_NAME     "Capella"
#define SENSOR_NAME 	"CM36283"
#define DRIVER_VERSION  "1.0"
#define CM36283_MAX_DELAY 200

/* register definitation */
#define ALS_CONF 0x00
#define ALS_THDL  0x01
#define ALS_THDH  0x02
#define PS_CONF1_CONF2 0x03
#define PS_CONF3_MS  0x04
#define PS_CANC  0x05
#define PS_THD  0x06
#define PS_DATA 0x08
#define ALS_DATA 0x09
#define INT_FLAG 0x0B
#define DEV_ID 0x0C

#define PS_MAX  255
#define ALS_MAX 3277

#define ALS_CONF_VAL 0x0040
#define ALS_DISABLE 0x0001

#define PS_CONF12_VAL 0x00A2
#define PS_DISABLE 0x0001//0x0001

#define PS_CONF3_VAL 0x0000

struct CM36283_data {
	struct i2c_client *CM36283_client;
	atomic_t delay;
	int als_enable;
	int ps_enable;
	unsigned char als_threshold;
	unsigned char ps_threshold;
	struct input_dev *input;
	struct mutex value_mutex;
	struct delayed_work work;
	struct work_struct irq_work;
};

static int CM36283_smbus_read_word(struct i2c_client *client,
		unsigned char reg_addr)
{
	s32 dummy;
	dummy = i2c_smbus_read_word_data(client, reg_addr);
	if (dummy < 0)
	{
		pr_err("%s read addr=%x read data=%x\n",__func__, reg_addr, dummy);
		return -1;
	}
	return dummy & 0x0000ffff;
}

static int CM36283_smbus_write_word(struct i2c_client *client,
		unsigned char reg_addr, unsigned short data)
{
	s32 dummy;
	dummy = i2c_smbus_write_word_data(client,reg_addr,data);
	if (dummy < 0)
	{
		pr_err("%s write addr=%x write data=%x\n",__func__, reg_addr, data);
		return -1;
	}
	return 0;
}

static int CM36283_read_ps(struct i2c_client *client)
{
	int ret;
	ret = CM36283_smbus_read_word(client, PS_DATA);
	return ret;
}

static int CM36283_read_als(struct i2c_client *client)
{
	int val;
	val = CM36283_smbus_read_word(client, ALS_DATA);
	return val;
}

#define lux_calc(step) (step/20)
static int CM36283_get_als(struct i2c_client *client)
{
	int ret;
	static int als_buf[3];
	static int idx = 0;
	ret = CM36283_read_als(client);
	if(ret > -1)
		ret = lux_calc(ret);
	if(ret > ALS_MAX)
		ret = ALS_MAX;
	if(ret > -1)
	{
		als_buf[idx] = ret;
		idx++;
	}
	if(idx == 3)
	{
		ret = (als_buf[0] + als_buf[1] + als_buf[2])/3;
		idx = 0;
	}
	else
		ret = -5;
	return ret;
}

static int CM36283_get_ps(struct i2c_client *client)
{
	int ret;
	ret = CM36283_read_ps(client);
	if(ret > -1)
		ret &= 0x00ff;
	return ret;
}

static void CM36283_work_func(struct work_struct *work)
{
	int ps;
	int als;
	struct CM36283_data *CM36283 = container_of((struct delayed_work *)work,
			struct CM36283_data, work);
	unsigned long delay = msecs_to_jiffies(atomic_read(&CM36283->delay));
	//printk("CM36283_work_func \n");
	ps  = CM36283_get_ps(CM36283->CM36283_client);
				//printk("ps=%d  ", ps);
	if(CM36283->ps_enable)
	{
		if (ps > -1) {
			input_report_abs(CM36283->input, ABS_DISTANCE, ps);
			input_sync(CM36283->input);
		}
	}
	als = CM36283_get_als(CM36283->CM36283_client);
	if(CM36283->als_enable)
	{
		if (als > -1) {
			input_report_abs(CM36283->input, ABS_MISC, als);
			input_sync(CM36283->input);
		}
	}
	if(CM36283->ps_enable || CM36283->als_enable)
		schedule_delayed_work(&CM36283->work, delay);
}

static int CM36283_als_state(struct CM36283_data *CM36283, int als_on)
{
	int rc; 
	u16 reg;

	if(als_on)
		reg = ALS_CONF_VAL;
	else
		reg = ALS_DISABLE;

	rc = CM36283_smbus_write_word(CM36283->CM36283_client,ALS_THDL, 0x01F4);
	if(rc < 0)
		pr_err("%s CM36283_smbus_write_word rc=%d\n", __func__, rc);

	rc = CM36283_smbus_write_word(CM36283->CM36283_client,ALS_THDH, 0x07D0);
	if(rc < 0)
		pr_err("%s CM36283_smbus_write_word rc=%d\n", __func__, rc);

	rc = CM36283_smbus_write_word(CM36283->CM36283_client,ALS_CONF, reg);
	if(rc < 0)
		pr_err("%s CM36283_smbus_write_word rc=%d\n", __func__, rc);

	return rc; 
}

static int CM36283_ps_state(struct CM36283_data *CM36283, int ps_on)
{
	int rc; 
	u16 reg;

	if(ps_on)
		reg = PS_CONF12_VAL;
	else
		reg = PS_DISABLE;

	rc = CM36283_smbus_write_word(CM36283->CM36283_client,PS_CONF1_CONF2, reg);
	if(rc < 0)
		pr_err("%s CM36283_smbus_write_word rc=%d\n", __func__, rc);

	rc = CM36283_smbus_write_word(CM36283->CM36283_client,PS_THD, 0x0705);
	if(rc < 0)
		pr_err("%s CM36283_smbus_write_word rc=%d\n", __func__, rc);

	return rc; 
}

static int CM36283_state(struct CM36283_data *CM36283,int als_on, int ps_on)
{
	int rc;
	rc = CM36283_als_state(CM36283, als_on);
	if(rc < 0)
		pr_err("%s CM36283 change state error=%d\n", __func__, rc);
	rc = CM36283_ps_state(CM36283, ps_on);
	if(rc < 0)
		pr_err("%s CM36283 change state error=%d\n", __func__, rc);
	return rc;
}

static ssize_t CM36283_als_enable_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int data;
	struct i2c_client *client = to_i2c_client(dev);
	struct CM36283_data *CM36283 = i2c_get_clientdata(client);
	mutex_lock(&CM36283->value_mutex);
	data = CM36283->als_enable;
	mutex_unlock(&CM36283->value_mutex);
	return sprintf(buf, "%d\n", data);
}

static ssize_t CM36283_als_enable_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct CM36283_data *CM36283 = i2c_get_clientdata(client);
	unsigned long delay = msecs_to_jiffies(atomic_read(&CM36283->delay));
	error = strict_strtoul(buf, 10, &data);
	if(error)
		return error;
	printk("%s set als = %ld\n", __func__, data);
	if(data)
	{
		if(!CM36283->als_enable)
		{
			mutex_lock(&CM36283->value_mutex);
			CM36283->als_enable = 1;
			mutex_unlock(&CM36283->value_mutex);
			error = CM36283_als_state(CM36283, CM36283->als_enable);
			if(error < 0)
			{
				pr_err("%s CM36283 change state error = %d\n",__func__, error);
			}else
			{
				schedule_delayed_work(&CM36283->work, delay);
			}
		}
	}
	else
	{
		if(CM36283->als_enable)
			CM36283->als_enable = 0;
		error = CM36283_als_state(CM36283, CM36283->als_enable);
		if(error < 0)
		{
			pr_err("%s CM36283 change state error = %d\n", __func__,error);
		}
	}
	return count;
}

static ssize_t CM36283_als_data_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int data;
	struct i2c_client *client = to_i2c_client(dev);
	struct CM36283_data *CM36283 = i2c_get_clientdata(client);
	data = CM36283_read_als(CM36283->CM36283_client);
	return sprintf(buf, "%d\n", data);
}

static ssize_t CM36283_ps_enable_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int data;
	struct i2c_client *client = to_i2c_client(dev);
	struct CM36283_data *CM36283 = i2c_get_clientdata(client);
	mutex_lock(&CM36283->value_mutex);
	data = CM36283->ps_enable;
	mutex_unlock(&CM36283->value_mutex);
	return sprintf(buf, "%d\n", data);
}

static ssize_t CM36283_ps_enable_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct CM36283_data *CM36283 = i2c_get_clientdata(client);
	unsigned long delay = msecs_to_jiffies(atomic_read(&CM36283->delay));
	error = strict_strtoul(buf, 10, &data);
	if(error)
		return error;
	printk("%s set ps = %ld\n", __func__, data);
	if(data)
	{
		if(!CM36283->ps_enable)
		{
			mutex_lock(&CM36283->value_mutex);
			CM36283->ps_enable = 1;
			mutex_unlock(&CM36283->value_mutex);
			error = CM36283_ps_state(CM36283, CM36283->ps_enable);
			if(error < 0)
			{
				pr_err("%s CM36283 change state error = %d\n",__func__, error);
			}else
			{
				schedule_delayed_work(&CM36283->work, delay);
			}
		}
	}
	else
	{
		mutex_lock(&CM36283->value_mutex);
		if(CM36283->ps_enable)
			CM36283->ps_enable = 0;
		mutex_unlock(&CM36283->value_mutex);
		error = CM36283_ps_state(CM36283, CM36283->ps_enable);
		if(error < 0)
		{
			pr_err("%s CM36283 change state error = %d\n",__func__, error);
		}
	}
	return count;
}

static ssize_t CM36283_ps_data_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int data;
	struct i2c_client *client = to_i2c_client(dev);
	struct CM36283_data *CM36283 = i2c_get_clientdata(client);
	data = CM36283_read_ps(CM36283->CM36283_client);
	return sprintf(buf, "%d\n", data);
}

static ssize_t CM36283_als_threshold_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct CM36283_data *CM36283 = i2c_get_clientdata(client);

	return sprintf(buf, "%d\n", CM36283->als_threshold);
}

static ssize_t CM36283_als_threshold_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct CM36283_data *CM36283 = i2c_get_clientdata(client);
	error = strict_strtoul(buf, 10, &data);
	if(error)
			return error;
	mutex_lock(&CM36283->value_mutex);
	CM36283->als_threshold = data;
	mutex_unlock(&CM36283->value_mutex);

	return count;
}

static ssize_t CM36283_ps_threshold_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct CM36283_data *CM36283 = i2c_get_clientdata(client);

	return sprintf(buf, "%d\n", CM36283->ps_threshold);
}

static ssize_t CM36283_ps_threshold_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct CM36283_data *CM36283 = i2c_get_clientdata(client);
	error = strict_strtoul(buf, 10, &data);
	if(error)
			return error;
	mutex_lock(&CM36283->value_mutex);
	CM36283->ps_threshold = data;
	mutex_unlock(&CM36283->value_mutex);

	return count;
}

static ssize_t CM36283_info_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "Chip: %s %s\nVersion: %s\n",
				   VENDOR_NAME, SENSOR_NAME, DRIVER_VERSION); 
}

static ssize_t CM36283_delay_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct CM36283_data *CM36283 = i2c_get_clientdata(client);

	return sprintf(buf, "%d\n", atomic_read(&CM36283->delay));

}

static ssize_t CM36283_delay_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct CM36283_data *CM36283 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;
	atomic_set(&CM36283->delay, (unsigned int) data);

	return count;
}

static int CM36283_reg = 0x0c;
static ssize_t CM36283_reg_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int data;
	struct i2c_client *client = to_i2c_client(dev);
	struct CM36283_data *CM36283 = i2c_get_clientdata(client);
	data = CM36283_smbus_read_word(CM36283->CM36283_client, CM36283_reg);
	return sprintf(buf, "reg = %d = %x\n", data, data);
}

static ssize_t CM36283_reg_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;
	data &= 0x0f;
	CM36283_reg = data;
	return count;
}

static DEVICE_ATTR(enable_als, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
		CM36283_als_enable_show, CM36283_als_enable_store);
static DEVICE_ATTR(als_data, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
		CM36283_als_data_show, NULL);
static DEVICE_ATTR(enable_ps, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
		CM36283_ps_enable_show, CM36283_ps_enable_store);
static DEVICE_ATTR(ps_data, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
		CM36283_ps_data_show, NULL);
static DEVICE_ATTR(raw_adc, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
		CM36283_ps_data_show, NULL);
static DEVICE_ATTR(als_threshold, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
		CM36283_als_threshold_show, CM36283_als_threshold_store);
static DEVICE_ATTR(ps_threshold, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
		CM36283_ps_threshold_show, CM36283_ps_threshold_store);
static DEVICE_ATTR(info, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
		CM36283_info_show, NULL);
static DEVICE_ATTR(delay, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
		CM36283_delay_show, CM36283_delay_store);
static DEVICE_ATTR(reg, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
		CM36283_reg_show, CM36283_reg_store);

static struct attribute *CM36283_attributes[] = {
	&dev_attr_enable_als.attr,
	&dev_attr_als_data.attr,
	&dev_attr_enable_ps.attr,
	&dev_attr_ps_data.attr,
	&dev_attr_raw_adc.attr,
	&dev_attr_als_threshold.attr,
	&dev_attr_ps_threshold.attr,
	&dev_attr_info.attr,
	&dev_attr_delay.attr,
	&dev_attr_reg.attr,
	NULL
};

static struct attribute_group CM36283_attribute_group = {
	.attrs = CM36283_attributes
};

static int CM36283_input_init(struct CM36283_data *CM36283)
{
	struct input_dev *dev;
	int err;

	dev = input_allocate_device();
	if (!dev)
	{
		pr_err("%s error input_allocate_device\n", __func__);
		return -ENOMEM;
	}
	dev->name = SENSOR_NAME;
	dev->id.bustype = BUS_I2C;
	set_bit(EV_ABS, dev->evbit);
	input_set_capability(dev, EV_ABS, ABS_DISTANCE);
	input_set_capability(dev, EV_ABS, ABS_MISC);
	input_set_drvdata(dev, CM36283);
	input_set_abs_params(dev, ABS_DISTANCE, 0, PS_MAX, 0, 0);
	input_set_abs_params(dev, ABS_MISC, 0, ALS_MAX, 0, 0);

	err = input_register_device(dev);
	if (err < 0) {
		input_free_device(dev);
		return err;
	}
	CM36283->input = dev;
	return 0;
}

static void CM36283_input_delete(struct CM36283_data *CM36283)
{
	struct input_dev *dev = CM36283->input;
	input_unregister_device(dev);
	input_free_device(dev);
}

static int CM36283_config(struct CM36283_data *CM36283)
{
	int rc; 

	rc = CM36283_smbus_read_word(CM36283->CM36283_client,DEV_ID);
	if(rc < 0)
		pr_err("%s CM36283_smbus_read_word rc=%d\n", __func__, rc);
	else
		pr_info("%s DEV_ID=0x%x\n",__func__, rc);

	rc = CM36283_smbus_write_word(CM36283->CM36283_client,ALS_CONF, ALS_CONF_VAL);
	if(rc < 0)
		pr_err("%s CM36283_smbus_write_word rc=%d\n", __func__, rc);

	rc = CM36283_smbus_write_word(CM36283->CM36283_client,ALS_THDL, 0x01F4);
	if(rc < 0)
		pr_err("%s CM36283_smbus_write_word rc=%d\n", __func__, rc);

	rc = CM36283_smbus_write_word(CM36283->CM36283_client,ALS_THDH, 0x07D0);
	if(rc < 0)
		pr_err("%s CM36283_smbus_write_word rc=%d\n", __func__, rc);

	rc = CM36283_smbus_write_word(CM36283->CM36283_client,PS_CONF1_CONF2, PS_CONF12_VAL); //0x04
	if(rc < 0)
		pr_err("%s CM36283_smbus_write_word rc=%d\n", __func__, rc);

	rc = CM36283_smbus_write_word(CM36283->CM36283_client,PS_THD, 0x0705);
	if(rc < 0)
		pr_err("%s CM36283_smbus_write_word rc=%d\n", __func__, rc);

	return rc; 
}

static int CM36283_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	int err = 0;
	struct CM36283_data *data;

	pr_info("%s\n", __func__);
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		printk(KERN_INFO "i2c_check_functionality error\n");
		goto exit;
	}
	data = kzalloc(sizeof(struct CM36283_data), GFP_KERNEL);
	if (!data) {
		err = -ENOMEM;
		goto exit;
	}
	err = CM36283_input_init(data);
	if (err < 0)
		goto kfree_exit;
	i2c_set_clientdata(client, data);
	data->CM36283_client = client;
	mutex_init(&data->value_mutex);
	INIT_DELAYED_WORK(&data->work, CM36283_work_func);
	atomic_set(&data->delay, CM36283_MAX_DELAY);
	err = sysfs_create_group(&data->input->dev.kobj,
						 &CM36283_attribute_group);
	if (err < 0)
		goto error_sysfs;
	data->ps_enable = 1;
	data->als_enable = 1;
	data->ps_threshold = 0;
	data->als_threshold = 0;
	err = CM36283_config(data);
	if(err < 0)
	{
		pr_err("CM36283_config error err=%d\n", err);
		goto error_sysfs;
	}
	if(data->ps_enable || data->als_enable)
		schedule_delayed_work(&data->work, CM36283_MAX_DELAY);
	return 0;
error_sysfs:
	CM36283_input_delete(data);
kfree_exit:
	kfree(data);
exit:
	return err;
}

static int CM36283_remove(struct i2c_client *client)
{
	struct CM36283_data *data = i2c_get_clientdata(client);

	sysfs_remove_group(&data->input->dev.kobj, &CM36283_attribute_group);
	CM36283_input_delete(data);
	kfree(data);
	return 0;
}

static const struct i2c_device_id CM36283_id[] = {
	{ SENSOR_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, CM36283_id);

#ifdef CONFIG_PM
static int CM36283_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct CM36283_data *CM36283 = i2c_get_clientdata(client);
	int err = 0;
	printk("%s\n", __func__);
	err = CM36283_state(CM36283, 0, 0);
	if(err < 0)
			return err;
	if(CM36283->als_enable || CM36283->ps_enable)
	{
		cancel_delayed_work_sync(&CM36283->work);
	}
	return 0;
}

static int CM36283_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct CM36283_data *CM36283 = i2c_get_clientdata(client);
	unsigned long delay = msecs_to_jiffies(atomic_read(&CM36283->delay));
	int err = 0;
	printk("%s\n", __func__);
	err = CM36283_state(CM36283, CM36283->als_enable, CM36283->ps_enable);
	if(err < 0)
			return err;
	if(CM36283->ps_enable || CM36283->als_enable)
		schedule_delayed_work(&CM36283->work, delay);
	return 0;
}

static const struct dev_pm_ops CM36283_pm_ops = { 
		.suspend = CM36283_suspend,
		.resume = CM36283_resume,
};
#endif

static struct i2c_driver CM36283_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= SENSOR_NAME,
#ifdef CONFIG_PM
		.pm = &CM36283_pm_ops,
#endif
	},
	.id_table	= CM36283_id,
	.probe		= CM36283_probe,
	.remove		= CM36283_remove,
};


static int __init CM36283_init(void)
{
	return i2c_add_driver(&CM36283_driver);
}

static void __exit CM36283_exit(void)
{
	i2c_del_driver(&CM36283_driver);
}

MODULE_AUTHOR("Thundersoft");
MODULE_DESCRIPTION("CM36283 driver");
MODULE_LICENSE("GPL");

module_init(CM36283_init);
module_exit(CM36283_exit);
