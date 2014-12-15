/*
 *  mc32x9.c - Linux kernel modules for 3-Axis Orientation/Motion
 *  Detection Sensor 
 *
 *  Copyright (C) 2009-2010 MCube Semiconductor Ltd.
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
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/input-polldev.h>

#include <linux/sensor/sensor_common.h>
static struct mutex sensor_lock;

/*
 * Defines
 */

#define DEBUG	0

#if DEBUG
#define assert(expr)\
        if(!(expr)) {\
        printk( "Assertion failed! %s,%d,%s,%s\n",\
        __FILE__,__LINE__,__func__,#expr);\
        }
#else
#define assert(expr) do{} while(0)
#endif


#define MC32X0_DRV_NAME						"mc32x0"
#define MC32X0_XOUT_REG						0x00
#define MC32X0_YOUT_REG						0x01
#define MC32X0_ZOUT_REG						0x02
#define MC32X0_Tilt_Status_REG				0x03
#define MC32X0_Sampling_Rate_Status_REG		0x04
#define MC32X0_Sleep_Count_REG				0x05
#define MC32X0_Interrupt_Enable_REG			0x06
#define MC32X0_Mode_Feature_REG				0x07
#define MC32X0_Sample_Rate_REG				0x08
#define MC32X0_Tap_Detection_Enable_REG		0x09
#define MC32X0_TAP_Dwell_Reject_REG			0x0a
#define MC32X0_DROP_Control_Register_REG	0x0b
#define MC32X0_SHAKE_Debounce_REG			0x0c
#define MC32X0_XOUT_EX_L_REG				0x0d
#define MC32X0_XOUT_EX_H_REG				0x0e
#define MC32X0_YOUT_EX_L_REG				0x0f
#define MC32X0_YOUT_EX_H_REG				0x10
#define MC32X0_ZOUT_EX_L_REG				0x11
#define MC32X0_ZOUT_EX_H_REG				0x12
#define MC32X0_CHIP_ID_REG					0x18
#define MC32X0_RANGE_Control_REG			0x20
#define MC32X0_SHAKE_Threshold_REG			0x2B
#define MC32X0_UD_Z_TH_REG					0x2C
#define MC32X0_UD_X_TH_REG					0x2D
#define MC32X0_RL_Z_TH_REG					0x2E
#define MC32X0_RL_Y_TH_REG					0x2F
#define MC32X0_FB_Z_TH_REG					0x30
#define MC32X0_DROP_Threshold_REG			0x31
#define MC32X0_TAP_Threshold_REG			0x32
#define MC32X0_MODE_SLEEP				0x43
#define MC32X0_MODE_WAKEUP				0x41



#define MODE_CHANGE_DELAY_MS 100

#define MAX_POLL_INTERVAL		200	
#define DEFAULT_POLL_INTERVAL		50

#define MCUBE_1_5G_8BIT 0x20
#define MCUBE_8G_14BIT 0x10

#ifdef CONFIG_PM
static int mc32x0_suspend(struct device *dev);
static int mc32x0_resume(struct device *dev);
#endif

static ssize_t	show_orientation(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t	show_axis_force(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t	show_xyz_force(struct device *dev, struct device_attribute *attr, char *buf);


static struct device *hwmon_dev;
static struct i2c_client *mc32x0_i2c_client;
static struct input_polled_dev *mc32x0_idev;
static u32 is_enabled;
static u32 poll_interval;		//ms
static u8 orientation;
static u8 McubeID = 0;

static SENSOR_DEVICE_ATTR(all_axis_force, S_IRUGO, show_xyz_force, NULL, 0);
static SENSOR_DEVICE_ATTR(x_axis_force, S_IRUGO, show_axis_force, NULL, 0);
static SENSOR_DEVICE_ATTR(y_axis_force, S_IRUGO, show_axis_force, NULL, 1);
static SENSOR_DEVICE_ATTR(z_axis_force, S_IRUGO, show_axis_force, NULL, 2);
static SENSOR_DEVICE_ATTR(orientation, S_IRUGO, show_orientation, NULL, 0);

static struct attribute* mc32x0_attrs[] = 
{
	&sensor_dev_attr_all_axis_force.dev_attr.attr,
	&sensor_dev_attr_x_axis_force.dev_attr.attr,
	&sensor_dev_attr_y_axis_force.dev_attr.attr,
	&sensor_dev_attr_z_axis_force.dev_attr.attr,
	&sensor_dev_attr_orientation.dev_attr.attr,
	NULL
};



static const struct attribute_group mc32x0_group =
{
	.attrs = mc32x0_attrs,
};

static void mc32x0_read_xyz(int idx, s8 *pf)
{
	assert(mc32x0_i2c_client);
	*pf = i2c_smbus_read_byte_data(mc32x0_i2c_client, idx+MC32X0_XOUT_REG);

}


static void mc32x0_read_tilt(u8* pt)
{
	u32 result;
	assert(mc32x0_i2c_client);
	result = i2c_smbus_read_byte_data(mc32x0_i2c_client, MC32X0_Tilt_Status_REG);
	*pt = result;
}

ssize_t	show_orientation(struct device *dev, struct device_attribute *attr, char *buf)
{
	int result;

	switch((orientation>>2)&0x07)
	{
	case 1: 
		result = sprintf(buf, "Left\n");
		break;

	case 2:
		result = sprintf(buf, "Right\n");
		break;
	
	case 5:
		result = sprintf(buf, "Downward\n");
		break;

	case 6:
		result = sprintf(buf, "Upward\n");
		break;

	default:
		switch(orientation & 0x03)
		{
		case 1:
			result = sprintf(buf, "Front\n");
			break;

		case 2:
			result = sprintf(buf, "Back\n");
			break;

		default:
			result = sprintf(buf, "Unknown\n");
		}
	}
	return result;
}

ssize_t show_xyz_force(struct device *dev, struct device_attribute *attr, char *buf)
{
	int i;
	s8 xyz[3]; 

	for(i=0; i<3; i++)
		mc32x0_read_xyz(i, &xyz[i]);
	return sprintf(buf, "(%d,%d,%d)\n", xyz[0], xyz[1], xyz[2]);	
}

ssize_t	show_axis_force(struct device *dev, struct device_attribute *attr, char *buf)
{
	s8 force;
    	int n = ((struct sensor_device_attribute *)to_sensor_dev_attr(attr))->index;

	mc32x0_read_xyz(n, &force);
	return sprintf(buf, "%d\n", force);	
}

ssize_t mc32x0_resolution_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
    if(McubeID == 0x11)
        return sprintf(buf, "%d\n", 14);//mc3210: 14bit
    else
        return sprintf(buf, "%d\n", 8);//mc3230: 8bit
}

ssize_t mc32x0_range_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{

    if(McubeID == 0x11)
        return sprintf(buf, "%d\n", 16);//mc3210: 16G
    else
        return sprintf(buf, "%d\n", 3);//mc3230: 3G
}

ssize_t mc32x0_delay_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", poll_interval);
}

static ssize_t mc32x0_delay_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long delay;
	int error;

	error = strict_strtoul(buf, 10, &delay);
	if (error)
		return error;
	mc32x0_idev->poll_interval = poll_interval = (delay > MAX_POLL_INTERVAL) ? MAX_POLL_INTERVAL: delay;
	return count;
}

static ssize_t mc32x0_enable_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{

	mc32x0_idev->poll_interval = poll_interval;
    return sprintf(buf, "%d\n", is_enabled);
}

static ssize_t mc32x0_enable_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;


	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;
	if (data == 1)
			mc32x0_resume(dev);
	else if(data == 0)
			mc32x0_suspend(dev);
	return count;
}

static DEVICE_ATTR(delay, S_IRUGO|S_IWUSR|S_IWGRP,
		mc32x0_delay_show, mc32x0_delay_store);
static DEVICE_ATTR(enable, S_IRUGO|S_IWUSR|S_IWGRP,
		mc32x0_enable_show, mc32x0_enable_store);
static DEVICE_ATTR(resolution, S_IRUGO,
		mc32x0_resolution_show, NULL);
static DEVICE_ATTR(range, S_IRUGO,
		mc32x0_range_show, NULL);

static struct attribute *mc32x0_attributes[] = {
    &dev_attr_delay.attr,
    &dev_attr_enable.attr,
    &dev_attr_resolution.attr,
    &dev_attr_range.attr,
    NULL
};

static struct attribute_group mc32x0_attribute_group = {
    .attrs = mc32x0_attributes,
};


static void mc32x0_worker(struct work_struct *work)
{
	u8 tilt, new_orientation;

	mc32x0_read_tilt(&tilt);
	new_orientation = tilt & 0x1f;
	if(orientation!=new_orientation)
		orientation = new_orientation;
}

DECLARE_WORK(mc32x0_work, mc32x0_worker);

#if 0
// interrupt handler
static irqreturn_t mmx7660_irq_handler(int irq, void *dev_id)
{
	schedule_work(&mc32x0_work);
	return IRQ_RETVAL(1);
}
#endif

/*
 * Initialization function
 */
 int mc32x0_set_image (struct i2c_client *client) 
{
	int comres = 0;
	unsigned char data;

    if (client == NULL) {
		return -1;
     } 
	  
	data = i2c_smbus_read_byte_data(client, 0x3B);	
	if(data == 0x19 || data == 0x29)
		McubeID = 0x22;
	else if(data == 0x90 || data == 0xa8 || data == 0x88)
		McubeID = 0x11;
	else
	{
		printk("mc32x0: unrecognized product id: %x\n", data);
		return -1;
	}

	if(McubeID &MCUBE_8G_14BIT)
	{

		//#ifdef MCUBE_8G_14BIT
		data = MC32X0_MODE_SLEEP;
		comres += i2c_smbus_write_byte_data(client, MC32X0_Mode_Feature_REG, data );

		data = 0x00;
		comres += i2c_smbus_write_byte_data(client, MC32X0_Sleep_Count_REG, data );	

		data = 0x00;
		comres += i2c_smbus_write_byte_data(client, MC32X0_Sample_Rate_REG, data );

		data = 0x00;
		comres += i2c_smbus_write_byte_data(client, MC32X0_Tap_Detection_Enable_REG, data );

		data = 0x3F;
		comres += i2c_smbus_write_byte_data(client, MC32X0_RANGE_Control_REG, data );

		data = 0x00;
		comres += i2c_smbus_write_byte_data(client, MC32X0_Interrupt_Enable_REG, data );
	//#endif
	}
	else if(McubeID &MCUBE_1_5G_8BIT)
	{		
		data = MC32X0_MODE_SLEEP;
		comres += i2c_smbus_write_byte_data(client, MC32X0_Mode_Feature_REG, data );

		data = 0x00;
		comres += i2c_smbus_write_byte_data(client, MC32X0_Sleep_Count_REG, data );	

		data = 0x00;
		comres += i2c_smbus_write_byte_data(client, MC32X0_Sample_Rate_REG, data );

		data = 0x02;
		comres += i2c_smbus_write_byte_data(client, MC32X0_RANGE_Control_REG, data );

		data = 0x00;
		comres += i2c_smbus_write_byte_data(client, MC32X0_Tap_Detection_Enable_REG, data );

		data = 0x00;
		comres += i2c_smbus_write_byte_data(client, MC32X0_Interrupt_Enable_REG, data );
	}

	data = MC32X0_MODE_WAKEUP;
	comres += i2c_smbus_write_byte_data(client, MC32X0_Mode_Feature_REG, data );

	data = i2c_smbus_read_byte_data(client, MC32X0_Mode_Feature_REG);	

#if DEBUG
	printk("mcube %s reg0x07 = 0x%x  ,comres = %d\n",__FUNCTION__,data,comres);
#endif	
	return comres;
}
static int mc32x0_init_client(struct i2c_client *client)
{
	int result;

	mc32x0_i2c_client = client;
	//plat_data = (struct mxc_mc32x0_platform_data *)client->dev.platform_data;
	//assert(plat_data);


	result = mc32x0_set_image(client);
	if(result < 0)
		return result;

	mdelay(MODE_CHANGE_DELAY_MS);

	{
		u8 tilt;
		mc32x0_read_tilt(&tilt);
		orientation = tilt&0x1f;
	}
	return result;
}

#define MAX_ABS_X	128
#define MAX_ABS_Y	128
#define MAX_ABS_Z	128
#define MAX_ABS_THROTTLE	128

#define INPUT_FUZZ	1//if "0" be better?
#define INPUT_FLAT	1

static void report_abs(void)
{
	int i;
	s8 xyz[3]; 
	s16 x, y, z;

	if(!is_enabled)
		return;

	mutex_lock(&sensor_lock);

	if(!is_enabled)
	{
		mutex_unlock(&sensor_lock);
		return;
	}

	if(McubeID == 0x22)//mc3230
    {
        for(i=0; i<3; i++)
            mc32x0_read_xyz(i, &xyz[i]);

        y = -xyz[0];
        x = xyz[1];
        z = xyz[2];
    }//mc3210
    else
    {
        unsigned char raw_buf[6];
        for(i=0; i<6; i++)
            raw_buf[i] = i2c_smbus_read_byte_data(mc32x0_i2c_client, i+MC32X0_XOUT_EX_L_REG);

		x = (s16)((raw_buf[0])|(raw_buf[1]<<8));
		y = (s16)((raw_buf[2])|(raw_buf[3]<<8));
		z = (s16)((raw_buf[4])|(raw_buf[5]<<8));
    }

    aml_sensor_report_acc(mc32x0_i2c_client, mc32x0_idev->input, x, y, z);

	mutex_unlock(&sensor_lock);

}


static void mc32x0_dev_poll(struct input_polled_dev *dev)
{
	report_abs();
} 
/////////////////////////end//////

/*
 * I2C init/probing/exit functions
 */

static int mc32x0_probe(struct i2c_client *client,
				   const struct i2c_device_id *id)
{
	int result;
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);

	struct input_dev *idev;

#if DEBUG
	printk("probing mc32x0 \n");
#endif
	mutex_init(&sensor_lock);	
	result = i2c_check_functionality(adapter, 
		I2C_FUNC_SMBUS_BYTE|I2C_FUNC_SMBUS_BYTE_DATA);
	assert(result);

	/* Initialize the MC32X0 chip */
	result = mc32x0_init_client(client);

	    if(result)
	       return result;

	result = sysfs_create_group(&client->dev.kobj, &mc32x0_group);
	assert(result==0);

	hwmon_dev = hwmon_device_register(&client->dev);
	assert(!(IS_ERR(hwmon_dev)));

	dev_info(&client->dev, "build time %s %s\n", __DATE__, __TIME__);
  

	/*input poll device register */
	mc32x0_idev = input_allocate_polled_device();
	if (!mc32x0_idev) {
		dev_err(&client->dev, "alloc poll device failed!\n");
		result = -ENOMEM;
		return result;
	}

	mc32x0_idev->poll = mc32x0_dev_poll;
	mc32x0_idev->poll_interval = poll_interval = DEFAULT_POLL_INTERVAL;
	idev = mc32x0_idev->input;
	idev->name = MC32X0_DRV_NAME;
	idev->id.bustype = BUS_I2C;
	idev->evbit[0] = BIT_MASK(EV_ABS);

	//change the param by simon.wang,2012-04-09
	//to enhance the sensititity
	input_set_abs_params(idev, ABS_X, -MAX_ABS_X, MAX_ABS_X, INPUT_FUZZ, INPUT_FLAT);
	input_set_abs_params(idev, ABS_Y, -MAX_ABS_Y, MAX_ABS_Y, INPUT_FUZZ, INPUT_FLAT);
	input_set_abs_params(idev, ABS_Z, -MAX_ABS_Z, MAX_ABS_Z, INPUT_FUZZ, INPUT_FLAT);
	//input_set_abs_params(idev, ABS_THROTTLE, -MAX_ABS_THROTTLE, MAX_ABS_THROTTLE, INPUT_FUZZ, INPUT_FLAT);//if necessary?
#if DEBUG
	printk("***** Sensor MC32X0 param: max_abs_x = %d,max_abs_y = %d,max_abs_z = %d \
		INPUT_FUZZ = %d,INPUT_FLAT = %d\n",MAX_ABS_X,MAX_ABS_Y,MAX_ABS_Y,INPUT_FUZZ,INPUT_FLAT);
#endif
	result = input_register_polled_device(mc32x0_idev);


	result = sysfs_create_group(&mc32x0_idev->input->dev.kobj, &mc32x0_attribute_group);
	assert(result==0);

	if (result) {
		dev_err(&client->dev, "register poll device failed!\n");
		return result;
	}

	return result;
}

static int mc32x0_remove(struct i2c_client *client)
{
	int result;

	mutex_lock(&sensor_lock);
	result = i2c_smbus_write_byte_data(client, MC32X0_Mode_Feature_REG, MC32X0_MODE_SLEEP);
	assert(result==0);

	mutex_unlock(&sensor_lock);
	//free_irq(plat_data->irq, NULL);
	free_irq(client->irq, NULL);
	sysfs_remove_group(&client->dev.kobj, &mc32x0_group);
	hwmon_device_unregister(hwmon_dev);

	return result;
}

#ifdef CONFIG_PM
static int mc32x0_suspend(struct device *dev)
{
	int result;
	
	if(!is_enabled)
		return 0;
	mutex_lock(&sensor_lock);
	result = i2c_smbus_write_byte_data(mc32x0_i2c_client, 
	MC32X0_Mode_Feature_REG, MC32X0_MODE_SLEEP);
	assert(result==0);
	is_enabled = 0;
	mutex_unlock(&sensor_lock);
	return result;
}

static int mc32x0_resume(struct device *dev)
{
	int result;
	if(is_enabled)
		return 0;
	mutex_lock(&sensor_lock);
	result = i2c_smbus_write_byte_data(mc32x0_i2c_client, 
	MC32X0_Mode_Feature_REG, MC32X0_MODE_WAKEUP);
	assert(result==0);
	is_enabled = 1;
	mutex_unlock(&sensor_lock);
	return result;
}
#else

#define mc32x0_suspend		NULL
#define mc32x0_resume		NULL

#endif


static const struct dev_pm_ops mc32x0_dev_pm_ops = {
	.suspend = mc32x0_suspend,
	.resume  = mc32x0_resume,
};


static const struct i2c_device_id mc32x0_id[] = {
	{ MC32X0_DRV_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mc32x0_id);

static struct i2c_driver mc32x0_driver = {
	.driver = {
		.name	= MC32X0_DRV_NAME,
		.owner	= THIS_MODULE,
	#ifdef CONFIG_PM   //add by jf.s, for  sensor resume can not use
		.pm   = &mc32x0_dev_pm_ops,
	#endif
	},
	.probe	= mc32x0_probe,
	.remove	= mc32x0_remove,
	.id_table = mc32x0_id,
};

static int __init mc32x0_init(void)
{
	return i2c_add_driver(&mc32x0_driver);
}

static void __exit mc32x0_exit(void)
{
	i2c_del_driver(&mc32x0_driver);
}

MODULE_DESCRIPTION("MC32X0 3-Axis Orientation/Motion Detection Sensor driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.1");
module_init(mc32x0_init);
module_exit(mc32x0_exit);
