#include <linux/sched.h>
#include <linux/time.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <mach/hardware.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/sensor/sensor_common.h>


#define DRV_VERSION		"1.0.0.1"
#define CM3217_I2C_ADDRESS	0x10
#define CM3217_DEV_NAME		"cm3217"
#define CM3217_SCHE_DELAY		500

struct cm3217_data {
        struct delayed_work work;
	struct input_dev *input_dev;
        struct i2c_client *i2c_client;
	int  delay;
    	int  enable;
    	struct mutex mutex;
};
struct cm3217_data *cm3217_data;
static struct i2c_driver cm3217_driver;

static int cm3217_reset(struct i2c_client *client)
{
	i2c_smbus_write_byte(client, 0x20);
	cm3217_data->i2c_client->addr = 0x11;
	i2c_smbus_write_byte(client, 0xa0);
	return 0;
}

static void cm3217_schedwork(struct work_struct *work)
{
	unsigned long delay = msecs_to_jiffies(cm3217_data->delay);
	struct input_dev *input_dev = cm3217_data->input_dev;
	u32 msb = 0, lsb = 0;
	u32 val;

	cm3217_data->i2c_client->addr=0x11;
	lsb = i2c_smbus_read_byte(cm3217_data->i2c_client);
	cm3217_data->i2c_client->addr=0x10;
	msb = i2c_smbus_read_byte(cm3217_data->i2c_client);
	val = msb;
	val = val<<8;
	val = val | lsb;
	input_report_abs(input_dev, ABS_MISC, val);
	input_sync(input_dev);
	schedule_delayed_work(&cm3217_data->work, delay);
}

static ssize_t cm3217_enable_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	int ret = 0;

	ret = sprintf(buf, "Light sensor Auto Enable = %d\n",
			cm3217_data->enable);

	return ret;
}

static int lightsensor_enable(void)
{
	int ret = 0;

	unsigned long delay = msecs_to_jiffies(cm3217_data->delay);

	if (cm3217_data->enable == 1)
		return ret;

	mutex_lock(&cm3217_data->mutex);

	schedule_delayed_work(&cm3217_data->work, delay);
	cm3217_data->enable = 1;

	mutex_unlock(&cm3217_data->mutex);

	return ret;
}

static int lightsensor_disable(void)
{
	int ret = 0;
	
	if(cm3217_data->enable == 0)
		return ret;
    	
	mutex_lock(&cm3217_data->mutex);
        
	cancel_delayed_work(&cm3217_data->work);
  	cm3217_data->enable=0;
	
	mutex_unlock(&cm3217_data->mutex);
	
	return ret;
}
static ssize_t cm3217_enable_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	int ls_auto;

	ls_auto = -1;
	sscanf(buf, "%d", &ls_auto);

	if (ls_auto != 0 && ls_auto != 1 )
		return -EINVAL;

	if (ls_auto) {
		lightsensor_enable();
	} else {
		lightsensor_disable();
	}
	
	return count;
}

static ssize_t cm3217_poll_delay_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	int ret = 0;

	ret = sprintf(buf, "Light sensor Poll Delay = %d ms\n",	cm3217_data->delay);

	return ret;
}

static ssize_t cm3217_poll_delay_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	int new_delay;

	sscanf(buf, "%d", &new_delay);

	printk("new delay = %d ms, old delay = %d ms \n",  new_delay, cm3217_data->delay);

	cm3217_data->delay = new_delay;

	if( cm3217_data->enable ){
		lightsensor_disable(); 
		lightsensor_enable();
	}

	return count;
}

static struct device_attribute dev_attr_light_enable =
__ATTR(enable, S_IRUGO | S_IWUSR | S_IWGRP, cm3217_enable_show, cm3217_enable_store);

static struct device_attribute dev_attr_light_delay =
__ATTR(delay, S_IRUGO | S_IWUSR | S_IWGRP, cm3217_poll_delay_show, cm3217_poll_delay_store);

static struct attribute *light_sysfs_attrs[] = {
&dev_attr_light_enable.attr,
&dev_attr_light_delay.attr,
NULL
};

static struct attribute_group light_attribute_group = {
.attrs = light_sysfs_attrs,
};

static int cm3217_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret = 0;
	struct input_dev *idev;
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	printk(" start cm3217 probe !!\n");
	
	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_WRITE_BYTE | I2C_FUNC_SMBUS_READ_BYTE_DATA))
	{
		ret = -EIO;
		return ret;
	}

	/* data memory allocation */
	cm3217_data = kzalloc(sizeof(struct cm3217_data), GFP_KERNEL);
	if (cm3217_data == NULL) {
		printk(KERN_ALERT "%s: CM3217 kzalloc failed.\n", __func__);
		ret = -ENOMEM;
		return ret;
	}
	cm3217_data->i2c_client = client;

	i2c_set_clientdata(client, cm3217_data);

	cm3217_reset(cm3217_data->i2c_client);

	INIT_DELAYED_WORK(&cm3217_data->work, cm3217_schedwork);
	cm3217_data->delay = CM3217_SCHE_DELAY;

	idev = input_allocate_device();
	if (!idev){
		printk(KERN_ALERT "%s: cm3217 allocate input device failed.\n", __func__);
		goto kfree_exit;
	}
    
	idev->name = CM3217_DEV_NAME;
	idev->id.bustype = BUS_I2C;
	input_set_capability(idev, EV_ABS, ABS_MISC);
	input_set_abs_params(idev, ABS_MISC, 0, 8192, 0, 0);
	cm3217_data->input_dev = idev;
	input_set_drvdata(idev, cm3217_data);

	ret = input_register_device(idev);
	if (ret < 0) {
		input_free_device(idev);
		goto kfree_exit;
	}
	
	mutex_init(&cm3217_data->mutex);
	/* register the attributes */
	ret = sysfs_create_group(&idev->dev.kobj, &light_attribute_group);
	if (ret) {
		goto unregister_exit;
	}

//	schedule_delayed_work(&cm3217_data->work, CM3217_SCHE_DELAY);
	cm3217_data->enable = 0;
	return ret;

unregister_exit:
    input_unregister_device(idev);
    input_free_device(idev);

kfree_exit:
	kfree(cm3217_data);
	return ret;
}

static int cm3217_remove(struct i2c_client *client)
{
	i2c_unregister_device(cm3217_data->i2c_client);
        cancel_delayed_work(&cm3217_data->work);
	kfree(cm3217_data);
	return 0;
}
static const struct i2c_device_id cm3217_id[] = {
	{ CM3217_DEV_NAME, 0 },
	{ }
};

static struct i2c_driver cm3217_driver = {
	.driver = {
		.name	= CM3217_DEV_NAME,
		.owner  = THIS_MODULE,
	},
	.probe = cm3217_probe,
	.remove = cm3217_remove,
	.id_table	= cm3217_id,
};

static int __init cm3217_init(void)
{
	i2c_add_driver(&cm3217_driver);
	printk("cm3217 v.%s\n",DRV_VERSION);
	
	return 0;
}

static void __exit cm3217_exit(void)
{
	i2c_del_driver(&cm3217_driver);
}


module_init(cm3217_init);
module_exit(cm3217_exit);

MODULE_AUTHOR("Amlogic 2014");
MODULE_DESCRIPTION("Capella CM3217 driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);
