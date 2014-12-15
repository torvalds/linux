/*
 * @file drivers/i2c/dmard06.c
 * @brief DMARD06 g-sensor Linux device driver
 * @author Domintech Technology Co., Ltd (http://www.domintech.com.tw)
 * @version 1.22
 * @date 2011/12/01
 *
 * @section LICENSE
 *
 *	Copyright 2011 Domintech Technology Co., Ltd
 *
 *	This software is licensed under the terms of the GNU General Public
 *	License version 2, as published by the Free Software Foundation, and
 *	may be copied, distributed, and modified under those terms.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *
 */
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <linux/gpio.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/freezer.h>
#include <linux/module.h>
#include <linux/sensor/dmard06.h>
#include <linux/sensor/sensor_common.h>
//#include <mach/gpio.h>
//#include <mach/board.h> 

#ifdef CONFIG_ANDROID_POWER
#include <linux/android_power.h>
#endif




#if 0
#define PRINT_INFO(x...) printk(x)
#else
#define PRINT_INFO(x...)
#endif
static int	dmard06_probe(struct i2c_client *client, const struct i2c_device_id *id);

#define DMARD06_SPEED		200 * 1000


//#define DMARD06_WORK_INTERRUPT  //中断模式工作
#define DMARD06_WORK_POLLING // 轮询模式工作
#define MAX_DELAY	200
#define DEF_DELAY	30
/* Addresses to scan -- protected by sense_data_mutex */
//static char sense_data[RBUFF_SIZE + 1];
static struct i2c_client *this_client;
static struct miscdevice dmard06_device;

static DECLARE_WAIT_QUEUE_HEAD(data_ready_wq);

#ifdef CONFIG_ANDROID_POWER
static android_early_suspend_t dmard06_early_suspend;
#endif
static const char* vendor = "DMARD06 GS-SENSOR";

/* AKM HW info */
static ssize_t gsensor_vendor_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;

	//sprintf(buf, "%#x\n", revision);
	 sprintf(buf, "%s.\n", vendor);
	ret = strlen(buf) + 1;

	return ret;
}

static DEVICE_ATTR(vendor, 0444, gsensor_vendor_show, NULL);

static struct kobject *android_gsensor_kobj;

static int gsensor_sysfs_init(void)
{
	int ret ;

	android_gsensor_kobj = kobject_create_and_add("android_gsensor", NULL);
	if (android_gsensor_kobj == NULL) {
		PRINT_INFO(KERN_ERR
			   "DMARD06 gsensor_sysfs_init:"\
			   "subsystem_register failed\n");
		ret = -ENOMEM;
		goto err;
	}

	ret = sysfs_create_file(android_gsensor_kobj, &dev_attr_vendor.attr);
	if (ret) {
		PRINT_INFO(KERN_ERR
			   "DMARD06 gsensor_sysfs_init:"\
			   "sysfs_create_group failed\n");
		goto err4;
	}

	return 0 ;
err4:
	kobject_del(android_gsensor_kobj);
err:
	return ret ;
}



static int dmard06_rx_data(struct i2c_client *client, char *rxData, int length)
{
	int ret = 0;

    struct i2c_msg msg[] = {
	{
			.addr	= client->addr,
			.flags	= 0,
			.len	= 1,
			.buf	= &rxData[0],
	},
        {
			.addr	= client->addr,
			.flags	= I2C_M_RD,
			.len	= length,
			.buf	= rxData,
        },
	};

	ret = i2c_transfer(client->adapter, msg, 2);
    if (ret < 0) {
        pr_err("error in read dmard06, %d byte(s) should be read,. \n", length);
        return -EIO;
    }
    else
    {
        return 0;
    }

}

static int dmard06_tx_data(struct i2c_client *client, char *txData, int length)
{
	int ret = 0;

    struct i2c_msg msg[] = {
        {
			.addr	= client->addr,
			.flags	= 0,
			.len	= length,
			.buf	= txData,
        },
	};

	ret = i2c_transfer(client->adapter, msg, 1);
    if (ret < 0) {
        pr_err("error in write dmard06, %d byte(s) should be written,. \n", length);
        return -EIO;
    }
    else
    {
        return 0;
    }

}

static int dmard06_set_rate(struct i2c_client *client, char rate)
{
	
	int ret = 0;
	char buffer[2];
	char temp ;
	if (rate > 3)
		return -EINVAL;
	PRINT_INFO("[ZWP]%s,rate = %d\n",__FUNCTION__,rate);
	//因为增加了滤波功能,即每RawDataLength次才上报一次,所以提升gsensor两个档级

	PRINT_INFO("[ZWP]%s,new rate = %d\n",__FUNCTION__,rate);

       temp =  (rate<< DMARD06_RATE_SHIFT);
	buffer[0] = DMARD06_REG_NORMAL;
	buffer[1] = 0x27 | (0x18 & temp);

	ret = dmard06_tx_data(client, &(buffer[0]), 2);
	ret = dmard06_rx_data(client, &(buffer[0]), 1);

	return ret;
}

static int dmard06_start_dev(struct i2c_client *client, char rate)
{

      struct dmard06_data *dmard06;
	
	char buffer[4];
	
	int ret = 0;
	dmard06 = i2c_get_clientdata(client);
       buffer[0] = DMARD06_REG_NORMAL;
	buffer[1] = 0x27;	//0x10; modify by zhao
       ret = dmard06_tx_data(client, &buffer[0], 2);
	 buffer[0] = DMARD06_REG_MODE;
	buffer[1] = 0x24;	//0x10; modify by zhao
       ret = dmard06_tx_data(client, &buffer[0], 2);
	  buffer[0] = DMARD06_REG_FLITER;
	buffer[1] = 0x00;	//0x10; modify by zhao
       ret = dmard06_tx_data(client, &buffer[0], 2);
	/*     buffer[0] = DMARD06_REG_INT;
	buffer[1] = 0x24;	//0x10; modify by zhao
       ret = dmard06_tx_data(client, &buffer[0], 2);*/
	   
	buffer[0] = DMARD06_REG_NA;
	buffer[1] = 0x00;	//0x10; modify by zhao
	ret = dmard06_tx_data(client, &buffer[0], 2);
	buffer[0] = DMARD06_REG_EVENT;
	buffer[1] = 0x2a;	//0x10; modify by zhao
	ret = dmard06_tx_data(client, &buffer[0], 2);
	buffer[0] = DMARD06_REG_Threshold;
	buffer[1] = 0x10;	//0x10; modify by zhao
	ret = dmard06_tx_data(client, &buffer[0], 2);
		buffer[0] = DMARD06_REG_Duration;
	buffer[1] = 0x10;	//0x10; modify by zhao
	ret = dmard06_tx_data(client, &buffer[0], 2);
	//ret = dmard06_rx_data(client, &buffer[0], 1);

	ret = dmard06_set_rate(client, rate);
#ifdef DMARD06_WORK_INTERRUPT
	    buffer[0] = DMARD06_REG_INT;
	buffer[1] = 0x24;	//0x10; modify by zhao
#else
        buffer[0] = DMARD06_REG_INT;
	buffer[1] = 0x0c;	//0x10; modify by zhao
#endif
       ret = dmard06_tx_data(client, &buffer[0], 2);
	ret = dmard06_rx_data(client, &buffer[0], 1);
#ifdef DMARD06_WORK_INTERRUPT
	enable_irq(client->irq);
#else
     schedule_delayed_work(&dmard06->delaywork, msecs_to_jiffies(dmard06->delay));
#endif
	PRINT_INFO("\n----------------------------dmard06_start------------------------\n");
	
	return ret;
}

static int dmard06_start(struct i2c_client *client, char rate)
{ 
	struct dmard06_data *dmard06 = (struct dmard06_data *)i2c_get_clientdata(client);
	
	if (dmard06->status == DMARD06_OPEN) {
		return 0;	   
	}
	dmard06->status = DMARD06_OPEN;
	return dmard06_start_dev(client, rate);
}

static int dmard06_close_dev(struct i2c_client *client)
{		
	char buffer[2];
	struct dmard06_data *dmard06 = (struct dmard06_data *)i2c_get_clientdata(client);
#ifdef DMARD06_WORK_INTERRUPT
		


	disable_irq_nosync(client->irq);
#else
      cancel_delayed_work_sync(&dmard06->delaywork);
#endif


	buffer[0] = DMARD06_REG_NORMAL;
	buffer[1] = 0x07;
	
	return dmard06_tx_data(client, buffer, 2);
}

static int dmard06_close(struct i2c_client *client)
{
	struct dmard06_data *dmard06 = (struct dmard06_data *)i2c_get_clientdata(client);
	
	dmard06->status = DMARD06_CLOSE;
	
	return dmard06_close_dev(client);
}

static int dmard06_reset_rate(struct i2c_client *client, char rate)
{
	int ret = 0;
	
	PRINT_INFO("\n----------------------------dmard06_reset_rate------------------------\n");
	
	ret = dmard06_close_dev(client);
	ret = dmard06_start_dev(client, rate);
	
	return ret ;
}

static inline int dmard06_convert_to_int(char value)
{
	int result;

	if (value < DMARD06_BOUNDARY) {
	   result = value * DMARD06_GRAVITY_STEP;
	} else {
	   result = ~(((~value & 0x3f) + 1)* DMARD06_GRAVITY_STEP) + 1;
	}

	return result;
}

static void dmard06_report_value(struct i2c_client *client, struct dmard06_axis *axis)
{
	struct dmard06_data *dmard06 = i2c_get_clientdata(client);

	/* Report acceleration sensor information */

	aml_sensor_report_acc(client, dmard06->input_dev, axis->x, axis->y, axis->z);
}

#define RawDataLength 4
int RawDataNum;
int Xaverage, Yaverage, Zaverage;

static int dmard06_get_data(struct i2c_client *client)
{
	signed char buffer[3];
	int ret;
	int x,y,z;
	struct dmard06_axis axis;

	do {
	memset(buffer, 0, 3);
		buffer[0] = DMARD06_REG_X_OUT;
		ret = dmard06_rx_data(client, &buffer[0], 3);
		if (ret < 0)
			return ret;
	} while (/*(buffer[0] & 0x40) || (buffer[1] & 0x40) || (buffer[2] & 0x40)*/0);

	x = buffer[0]>>1;
	y = buffer[1]>>1;
	z = buffer[2]>>1;


	axis.x = -y;
	axis.y = -x;
	axis.z = -z;


	dmard06_report_value(client, &axis);

	return 0;
}

/*
static int dmard06_trans_buff(char *rbuf, int size)
{
	//wait_event_interruptible_timeout(data_ready_wq,
	//				 atomic_read(&data_ready), 1000);
	wait_event_interruptible(data_ready_wq,
					 atomic_read(&data_ready));

	atomic_set(&data_ready, 0);
	memcpy(rbuf, &sense_data[0], size);

	return 0;
}
*/

static int dmard06_open(struct inode *inode, struct file *file)
{
	return 0;//nonseekable_open(inode, file);
}

static int dmard06_release(struct inode *inode, struct file *file)
{
	return 0;
}

static long dmard06_ioctl(struct file *file, unsigned int cmd,unsigned long arg)
{

	void __user *argp = (void __user *)arg;
	char msg[RBUFF_SIZE + 1];
	int ret = -1;
	char rate;
	struct i2c_client *client = container_of(dmard06_device.parent, struct i2c_client, dev);

	switch (cmd) {
	case MMA_IOCTL_APP_SET_RATE:
		if (copy_from_user(&rate, argp, sizeof(rate)))
			return -EFAULT;
		break;
	default:
		break;
	}

	switch (cmd) {
	case MMA_IOCTL_START:
	
		ret = dmard06_start(client, DMARD06_RATE_50);
		if (ret < 0)
			return ret;
		break;
	case MMA_IOCTL_CLOSE:
		ret = dmard06_close(client);
		if (ret < 0)
			return ret;
		break;
	case MMA_IOCTL_APP_SET_RATE:
		ret = dmard06_reset_rate(client, rate);
		if (ret < 0)
			return ret;
		break;
	/*
	case ECS_IOCTL_GETDATA:
		ret = dmard06_trans_buff(msg, RBUFF_SIZE);
		if (ret < 0)
			return ret;
		break;
	*/	
	default:
		return -ENOTTY;
	}

	switch (cmd) {
	case MMA_IOCTL_GETDATA:
		if (copy_to_user(argp, &msg, sizeof(msg)))
			return -EFAULT;
		break;
	default:
		break;
	}

	return 0;
}


static char dmard06_get_devid(struct i2c_client *client)
{
        unsigned char buffer[2];
        buffer[0] = SW_RESET;
	dmard06_rx_data(client,&buffer[0],1);

        buffer[0] = DMARD06_REG_WHO_AM_I;
        dmard06_rx_data(client,&buffer[0],1);
	printk("dmard06 devid:%x\n",buffer[0]);

	   return buffer[0];
}

static void dmard06_work_func(struct work_struct *work)
{
	struct dmard06_data *dmard06 = container_of(work, struct dmard06_data, work);
	struct i2c_client *client = dmard06->client;
	
	if (dmard06_get_data(client) < 0) 
		PRINT_INFO(KERN_ERR "dmard06 mma_work_func: Get data failed\n");
		
	
#ifdef DMARD06_WORK_INTERRUPT
		enable_irq(client->irq);
#else
      schedule_work(&dmard06->work);
#endif
	
}

static void  dmard06_delaywork_func(struct work_struct *work)
{
	struct delayed_work *delaywork = container_of(work, struct delayed_work, work);
	struct dmard06_data *dmard06 = container_of(delaywork, struct dmard06_data, delaywork);
	struct i2c_client *client = dmard06->client;

	if (dmard06_get_data(client) < 0) 
		PRINT_INFO(KERN_ERR "DMARD06 mma_work_func: Get data failed\n");
		
	
#ifdef DMARD06_WORK_INTERRUPT
		enable_irq(client->irq);
#else
	schedule_delayed_work(&dmard06->delaywork, msecs_to_jiffies(dmard06->delay));
#endif
	
}

#ifdef DMARD06_WORK_INTERRUPT
static irqreturn_t dmard06_interrupt(int irq, void *dev_id)
{
	struct dmard06_data *dmard06 = (struct dmard06_data *)dev_id;
	
	disable_irq_nosync(irq);
	schedule_delayed_work(&dmard06->delaywork, msecs_to_jiffies(dmard06->delay));
	
	return IRQ_HANDLED;
}
#endif

static struct file_operations dmard06_fops = {
	.owner = THIS_MODULE,
	.open = dmard06_open,
	.release = dmard06_release,
	.unlocked_ioctl = dmard06_ioctl,
};

static struct miscdevice dmard06_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "dmard06",
	.fops = &dmard06_fops,
};

static int dmard06_remove(struct i2c_client *client)
{
	struct dmard06_data *dmard06 = i2c_get_clientdata(client);
	
	misc_deregister(&dmard06_device);
	input_unregister_device(dmard06->input_dev);
	input_free_device(dmard06->input_dev);
	free_irq(client->irq, dmard06);
	kfree(dmard06); 
#ifdef CONFIG_ANDROID_POWER
	android_unregister_early_suspend(&dmard06_early_suspend);
#endif		
	this_client = NULL;
	return 0;
}

#ifdef CONFIG_ANDROID_POWER
static int dmard06_suspend(android_early_suspend_t *h)
{
	struct i2c_client *client = container_of(dmard06_device.parent, struct i2c_client, dev);
	PRINT_INFO("Gsensor mma7760 enter suspend\n");
	return dmard06_close_dev(client);
}

static int dmard06_resume(android_early_suspend_t *h)
{
	struct i2c_client *client = container_of(dmard06_device.parent, struct i2c_client, dev);
	struct dmard06_data *dmard06 = (struct dmard06_data *)i2c_get_clientdata(client);
	PRINT_INFO("Gsensor mma7760 resume!!\n");
	return dmard06_start_dev(dmard06->curr_tate);
}
#else
static int dmard06_suspend(struct i2c_client *client, pm_message_t mesg)
{
	PRINT_INFO("Gsensor mma7760 enter 2 level  suspend\n");
	return dmard06_close_dev(client);
}
static int dmard06_resume(struct i2c_client *client)
{
	struct dmard06_data *dmard06 = (struct dmard06_data *)i2c_get_clientdata(client);
	PRINT_INFO("Gsensor mma7760 2 level resume!!\n");
	return dmard06_start_dev(client, dmard06->curr_tate);
}
#endif

static const struct i2c_device_id dmard06_id[] = {
		{"dmard06", 0},
		{ }
};

static struct i2c_driver dmard06_driver = {
	.driver = {
		.name = "dmard06",
		},
	.id_table	= dmard06_id,
	.probe		= dmard06_probe,
	.remove 	= dmard06_remove,
#ifndef CONFIG_ANDROID_POWER	
	.suspend = &dmard06_suspend,
	.resume = &dmard06_resume,
#endif	
};


static int dmard06_init_client(struct i2c_client *client)
{
	struct dmard06_data *dmard06;
	int ret = 0;
	dmard06 = i2c_get_clientdata(client);
	
#ifdef DMARD06_WORK_INTERRUPT
	PRINT_INFO("gpio_to_irq(%d) is %d\n",client->irq,gpio_to_irq(client->irq));
	if ( !gpio_is_valid(client->irq)) {
		PRINT_INFO("+++++++++++gpio_is_invalid\n");
		return -EINVAL;
	}
	ret = gpio_request(client->irq, "dmard06_int");
	if (ret) {
		PRINT_INFO( "failed to request mma7990_trig GPIO%d\n",gpio_to_irq(client->irq));
		return ret;
	}
	ret = gpio_direction_input(client->irq);
	if (ret) {
		PRINT_INFO("failed to set mma7990_trig GPIO gpio input\n");
		return ret;
	}
	gpio_pull_updown(client->irq, GPIOPullUp);
	client->irq = gpio_to_irq(client->irq);
	ret = request_irq(client->irq, dmard06_interrupt, IRQF_TRIGGER_LOW, client->dev.driver->name, dmard06);
	PRINT_INFO("request irq is %d,ret is  0x%x\n",client->irq,ret);
	if (ret ) {
		PRINT_INFO(KERN_ERR "dmard06_init_client: request irq failed,ret is %d\n",ret);
		return ret;
	}
	disable_irq(client->irq);
	init_waitqueue_head(&data_ready_wq);
 #endif
	return ret;
}

static ssize_t dmard06_delay_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
    struct i2c_client *client = to_i2c_client(dev->parent);

	struct dmard06_data *dmard06 = (struct dmard06_data *)i2c_get_clientdata(client);
    return sprintf(buf, "%d\n", dmard06->delay);
}

static ssize_t dmard06_delay_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long delay;
	int error;

    	struct i2c_client *client = to_i2c_client(dev->parent);
	struct dmard06_data *dmard06 = (struct dmard06_data *)i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &delay);
	if (error)
		return error;
	dmard06->delay = (delay > MAX_DELAY) ? MAX_DELAY : delay;
	return count;
}
static ssize_t dmard06_enable_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
    struct i2c_client *client = to_i2c_client(dev->parent);

	struct dmard06_data *dmard06 = (struct dmard06_data *)i2c_get_clientdata(client);
    return sprintf(buf, "%d\n", dmard06->status);
}

static ssize_t dmard06_enable_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;

    	struct i2c_client *client = to_i2c_client(dev->parent);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;
	if (data == 1)
		dmard06_start(client,0);
	else if(data == 0)
		dmard06_close(client);

	return count;
}

static DEVICE_ATTR(delay, S_IRUGO|S_IWUSR|S_IWGRP,
		dmard06_delay_show, dmard06_delay_store);
static DEVICE_ATTR(enable, S_IRUGO|S_IWUSR|S_IWGRP,
		dmard06_enable_show, dmard06_enable_store);

static struct attribute *dmard06_attributes[] = {
    &dev_attr_delay.attr,
    &dev_attr_enable.attr,
    NULL
};

static struct attribute_group dmard06_attribute_group = {
    .attrs = dmard06_attributes,
};

static int	dmard06_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct dmard06_data *dmard06;
	int err = -1;
	char devid;
	Xaverage = Yaverage = Zaverage = RawDataNum = 0;

	dmard06 = kzalloc(sizeof(struct dmard06_data), GFP_KERNEL);
	if (!dmard06) {
		PRINT_INFO("[dmard06]:alloc data failed.\n");
		err = -ENOMEM;
		goto exit_alloc_data_failed;
	}
	
	INIT_WORK(&dmard06->work, dmard06_work_func);
	INIT_DELAYED_WORK(&dmard06->delaywork, dmard06_delaywork_func);
	dmard06->client = client;
	dmard06->delay = DEF_DELAY;
	i2c_set_clientdata(client, dmard06);

	this_client = client;
       devid = dmard06_get_devid(this_client);
	if (DMARD06_DEVID != devid) {
		pr_info("DMARD06: invalid devid\n");
		goto exit_invalid_devid;
	}
	   
	err = dmard06_init_client(client);
	if (err < 0) {
		PRINT_INFO(KERN_ERR
			   "dmard06_probe: dmard06_init_client failed\n");
		goto exit_request_gpio_irq_failed;
	}
	dmard06->input_dev = input_allocate_device();
	if (!dmard06->input_dev) {
		err = -ENOMEM;
		PRINT_INFO(KERN_ERR
			   "dmard06_probe: Failed to allocate input device\n");
		goto exit_input_allocate_device_failed;
	}

	set_bit(EV_ABS, dmard06->input_dev->evbit);
#define RANGE 64

	/* x-axis acceleration */
	input_set_abs_params(dmard06->input_dev, ABS_X, -RANGE, RANGE, 0, 0);
	/* y-axis acceleration */
	input_set_abs_params(dmard06->input_dev, ABS_Y, -RANGE, RANGE, 0, 0);
	/* z-axis acceleration */
	input_set_abs_params(dmard06->input_dev, ABS_Z, -RANGE, RANGE, 0, 0);

	dmard06->input_dev->name = "dmard06";
	dmard06->input_dev->dev.parent = &client->dev;

	err = input_register_device(dmard06->input_dev);
	if (err < 0) {
		PRINT_INFO(KERN_ERR
			   "dmard06_probe: Unable to register input device: %s\n",
			   dmard06->input_dev->name);
		goto exit_input_register_device_failed;
	}
    err = sysfs_create_group(&dmard06->input_dev->dev.kobj,
        &dmard06_attribute_group);
    if (err < 0)
	{
		PRINT_INFO(KERN_ERR
			   "dmard06_probe: create sysfs group failed\n");
		goto exit_misc_device_register_dmard06_device_failed;
	}

	dmard06_device.parent = &client->dev;
	err = misc_register(&dmard06_device);
	if (err < 0) {
		PRINT_INFO(KERN_ERR
			   "dmard06_probe: mmad_device register failed\n");
		goto exit_misc_device_register_dmard06_device_failed;
	}
	err = gsensor_sysfs_init();
	if (err < 0) {
		PRINT_INFO(KERN_ERR
			"dmard06_probe: gsensor sysfs init failed\n");
		goto exit_gsensor_sysfs_init_failed;
	}
#ifdef CONFIG_ANDROID_POWER
	dmard06_early_suspend.suspend = dmard06_suspend;
	dmard06_early_suspend.resume = dmard06_resume;
	dmard06_early_suspend.level = 0x2;
	android_register_early_suspend(&dmard06_early_suspend);
#endif
	PRINT_INFO(KERN_INFO "dmard06 probe ok\n");
	dmard06->status = -1;
//	dmard06_start(client,0);
#if 0	
	dmard06_start(client, DMARD06_RATE_32);
#endif
	return 0;

exit_gsensor_sysfs_init_failed:
	misc_deregister(&dmard06_device);
exit_misc_device_register_dmard06_device_failed:
	input_unregister_device(dmard06->input_dev);
exit_input_register_device_failed:
	input_free_device(dmard06->input_dev);
exit_input_allocate_device_failed:
	free_irq(client->irq, dmard06);
exit_request_gpio_irq_failed:
	cancel_delayed_work_sync(&dmard06->delaywork);
	cancel_work_sync(&dmard06->work);
exit_invalid_devid:
	kfree(dmard06); 
exit_alloc_data_failed:
	;
	return err;
}


static int __init dmard06_i2c_init(void)
{
	return i2c_add_driver(&dmard06_driver);
}

static void __exit dmard06_i2c_exit(void)
{
	i2c_del_driver(&dmard06_driver);

}

module_init(dmard06_i2c_init);
module_exit(dmard06_i2c_exit);

