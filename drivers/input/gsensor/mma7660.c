/* drivers/i2c/chips/mma7660.c - mma7660 compass driver
 *
 * Copyright (C) 2007-2008 HTC Corporation.
 * Author: Hou-Kun Chen <houkun.chen@gmail.com>
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
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/freezer.h>
#include <linux/mma7660.h>
#include <mach/gpio.h>
#include <mach/board.h> 
#ifdef CONFIG_ANDROID_POWER
#include <linux/android_power.h>
#endif

//#define RK28_PRINT
//#include <asm/arch/rk28_debug.h>
#if 0
#define rk28printk(x...) printk(x)
#else
#define rk28printk(x...)
#endif
static int  mma7660_probe(struct i2c_client *client, const struct i2c_device_id *id);

#define MMA7660_SPEED		200 * 1000

/* Addresses to scan -- protected by sense_data_mutex */
//static char sense_data[RBUFF_SIZE + 1];
static struct i2c_client *this_client;
static struct miscdevice mma7660_device;

static DECLARE_WAIT_QUEUE_HEAD(data_ready_wq);

#ifdef CONFIG_ANDROID_POWER
static android_early_suspend_t mma7660_early_suspend;
#endif
static int revision = -1;
/* AKM HW info */
static ssize_t gsensor_vendor_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;

	sprintf(buf, "%#x\n", revision);
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
		rk28printk(KERN_ERR
		       "MMA7660 gsensor_sysfs_init:"\
		       "subsystem_register failed\n");
		ret = -ENOMEM;
		goto err;
	}

	ret = sysfs_create_file(android_gsensor_kobj, &dev_attr_vendor.attr);
	if (ret) {
		rk28printk(KERN_ERR
		       "MMA7660 gsensor_sysfs_init:"\
		       "sysfs_create_group failed\n");
		goto err4;
	}

	return 0 ;
err4:
	kobject_del(android_gsensor_kobj);
err:
	return ret ;
}

static int mma7660_rx_data(struct i2c_client *client, char *rxData, int length)
{
	int ret = 0;
	char reg = rxData[0];
	ret = i2c_master_reg8_recv(client, reg, rxData, length, MMA7660_SPEED);
	return (ret > 0)? 0 : ret;
}

static int mma7660_tx_data(struct i2c_client *client, char *txData, int length)
{
	int ret = 0;
	char reg = txData[0];
	ret = i2c_master_reg8_send(client, reg, &txData[1], length-1, MMA7660_SPEED);
	return (ret > 0)? 0 : ret;
}

static int mma7660_set_rate(struct i2c_client *client, char rate)
{
	char buffer[2];
	int ret = 0;
	
	if (rate > 128)
        return -EINVAL;
	rk28printk("[ZWP]%s,rate = %d\n",__FUNCTION__,rate);
	//因为增加了滤波功能,即每RawDataLength次才上报一次,所以提升gsensor两个档级
	if(rate > 2)
		rate -= 2;
	rk28printk("[ZWP]%s,new rate = %d\n",__FUNCTION__,rate);

/*	
    for (i = 0; i < 7; i++) {
        if (rate & (0x1 << i))
            break;
    }   
    

	buffer[0] = MMA7660_REG_SR;
	buffer[1] = 0xf8 | (0x07 & (~i));
*/
	buffer[0] = MMA7660_REG_SR;
	buffer[1] = 0xf8 | (0x07 & rate);

	ret = mma7660_tx_data(client, &(buffer[0]), 2);
	ret = mma7660_rx_data(client, &(buffer[0]), 1);

	return ret;
}

static int mma7660_start_dev(struct i2c_client *client, char rate)
{
	char buffer[MMA7660_REG_LEN];
	int ret = 0;

	buffer[0] = MMA7660_REG_INTSU;
	buffer[1] = 0x10;	//0x10; modify by zhao
	ret = mma7660_tx_data(client, &buffer[0], 2);
	ret = mma7660_rx_data(client, &buffer[0], 1);

	ret = mma7660_set_rate(client, rate);

	buffer[0] = MMA7660_REG_MODE;
	buffer[1] = 0x01;
	ret = mma7660_tx_data(client, &buffer[0], 2);
	ret = mma7660_rx_data(client, &buffer[0], 1);

	enable_irq(client->irq);
	rk28printk("\n----------------------------mma7660_start------------------------\n");
	
	return ret;
}

static int mma7660_start(struct i2c_client *client, char rate)
{ 
    struct mma7660_data *mma7660 = (struct mma7660_data *)i2c_get_clientdata(client);
    
    if (mma7660->status == MMA7660_OPEN) {
        return 0;      
    }
    mma7660->status = MMA7660_OPEN;
    return mma7660_start_dev(client, rate);
}

static int mma7660_close_dev(struct i2c_client *client)
{    	
	char buffer[2];

	disable_irq_nosync(client->irq);

	buffer[0] = MMA7660_REG_MODE;
	buffer[1] = 0x00;
	
	return mma7660_tx_data(client, buffer, 2);
}

static int mma7660_close(struct i2c_client *client)
{
    struct mma7660_data *mma7660 = (struct mma7660_data *)i2c_get_clientdata(client);
    
    mma7660->status = MMA7660_CLOSE;
    
    return mma7660_close_dev(client);
}

static int mma7660_reset_rate(struct i2c_client *client, char rate)
{
	int ret = 0;
	
	rk28printk("\n----------------------------mma7660_reset_rate------------------------\n");
	
    ret = mma7660_close_dev(client);
    ret = mma7660_start_dev(client, rate);
    
	return ret ;
}

static inline int mma7660_convert_to_int(char value)
{
    int result;

    if (value < MMA7660_BOUNDARY) {
       result = value * MMA7660_GRAVITY_STEP;
    } else {
       result = ~(((~value & 0x3f) + 1)* MMA7660_GRAVITY_STEP) + 1;
    }

    return result;
}

static void mma7660_report_value(struct i2c_client *client, struct mma7660_axis *axis)
{
	struct mma7660_data *mma7660 = i2c_get_clientdata(client);
    //struct mma7660_axis *axis = (struct mma7660_axis *)rbuf;

	/* Report acceleration sensor information */
    input_report_abs(mma7660->input_dev, ABS_X, axis->x);
    input_report_abs(mma7660->input_dev, ABS_Y, axis->y);
    input_report_abs(mma7660->input_dev, ABS_Z, axis->z);
    input_sync(mma7660->input_dev);
    rk28printk("Gsensor x==%d  y==%d z==%d\n",axis->x,axis->y,axis->z);
}

#define RawDataLength 4
int RawDataNum;
int Xaverage, Yaverage, Zaverage;

static int mma7660_get_data(struct i2c_client *client)
{
	char buffer[3];
	int ret;
    struct mma7660_axis axis;
    //struct rk2818_gs_platform_data *pdata = client->dev.platform_data;
    do {
        memset(buffer, 0, 3);
        buffer[0] = MMA7660_REG_X_OUT;
        ret = mma7660_rx_data(client, &buffer[0], 3);
        if (ret < 0)
            return ret;
    } while ((buffer[0] & 0x40) || (buffer[1] & 0x40) || (buffer[2] & 0x40));

	axis.x = mma7660_convert_to_int(buffer[MMA7660_REG_X_OUT]);
	axis.y = -mma7660_convert_to_int(buffer[MMA7660_REG_Y_OUT]);
	axis.z = -mma7660_convert_to_int(buffer[MMA7660_REG_Z_OUT]);
/*
	if(pdata->swap_xy)
	{
		axis.y = -axis.y;
		swap(axis.x,axis.y);		
	}
*/
	//计算RawDataLength次值的平均值
	Xaverage += axis.x;
	Yaverage += axis.y;
	Zaverage += axis.z;
    rk28printk( "%s: ------------------mma7660_GetData axis = %d  %d  %d,average=%d %d %d--------------\n",
           __func__, axis.x, axis.y, axis.z,Xaverage,Yaverage,Zaverage); 
	
	if((++RawDataNum)>=RawDataLength){
		RawDataNum = 0;
		axis.x = Xaverage/RawDataLength;
		axis.y = Yaverage/RawDataLength;
		axis.z = Zaverage/RawDataLength;
	    mma7660_report_value(client, &axis);
		Xaverage = Yaverage = Zaverage = 0;
	}
#if 0	
  //  rk28printk( "%s: ------------------mma7660_GetData axis = %d  %d  %d--------------\n",
  //         __func__, axis.x, axis.y, axis.z); 
     
    //memcpy(sense_data, &axis, sizeof(axis));
    mma7660_report_value(client, &axis);
	//atomic_set(&data_ready, 0);
	//wake_up(&data_ready_wq);
#endif
	return 0;
}

/*
static int mma7660_trans_buff(char *rbuf, int size)
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

static int mma7660_open(struct inode *inode, struct file *file)
{
	return 0;//nonseekable_open(inode, file);
}

static int mma7660_release(struct inode *inode, struct file *file)
{
	return 0;
}

static long mma7660_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{

	void __user *argp = (void __user *)arg;
	char msg[RBUFF_SIZE + 1];
	int ret = -1;
	char rate;
	struct i2c_client *client = container_of(mma7660_device.parent, struct i2c_client, dev);

	switch (cmd) {
	case ECS_IOCTL_APP_SET_RATE:
		if (copy_from_user(&rate, argp, sizeof(rate)))
			return -EFAULT;
		break;
	default:
		break;
	}

	switch (cmd) {
	case ECS_IOCTL_START:
		ret = mma7660_start(client, MMA7660_RATE_32);
		if (ret < 0)
			return ret;
		break;
	case ECS_IOCTL_CLOSE:
		ret = mma7660_close(client);
		if (ret < 0)
			return ret;
		break;
	case ECS_IOCTL_APP_SET_RATE:
		ret = mma7660_reset_rate(client, rate);
		if (ret < 0)
			return ret;
		break;
    /*
	case ECS_IOCTL_GETDATA:
		ret = mma7660_trans_buff(msg, RBUFF_SIZE);
		if (ret < 0)
			return ret;
		break;
	*/	
	default:
		return -ENOTTY;
	}

	switch (cmd) {
	case ECS_IOCTL_GETDATA:
		if (copy_to_user(argp, &msg, sizeof(msg)))
			return -EFAULT;
		break;
	default:
		break;
	}

	return 0;
}

static void mma7660_work_func(struct work_struct *work)
{
	struct mma7660_data *mma7660 = container_of(work, struct mma7660_data, work);
	struct i2c_client *client = mma7660->client;
	
	if (mma7660_get_data(client) < 0) 
		rk28printk(KERN_ERR "MMA7660 mma_work_func: Get data failed\n");
		
	enable_irq(client->irq);		
}

static void  mma7660_delaywork_func(struct work_struct *work)
{
	struct delayed_work *delaywork = container_of(work, struct delayed_work, work);
	struct mma7660_data *mma7660 = container_of(delaywork, struct mma7660_data, delaywork);
	struct i2c_client *client = mma7660->client;

	if (mma7660_get_data(client) < 0) 
		rk28printk(KERN_ERR "MMA7660 mma_work_func: Get data failed\n");
		
	enable_irq(client->irq);		
}

static irqreturn_t mma7660_interrupt(int irq, void *dev_id)
{
	struct mma7660_data *mma7660 = (struct mma7660_data *)dev_id;
	
	disable_irq_nosync(irq);
	schedule_delayed_work(&mma7660->delaywork, msecs_to_jiffies(30));
	
	return IRQ_HANDLED;
}

static struct file_operations mma7660_fops = {
	.owner = THIS_MODULE,
	.open = mma7660_open,
	.release = mma7660_release,
	.unlocked_ioctl = mma7660_ioctl,
};

static struct miscdevice mma7660_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "mma8452_daemon",//"mma7660_daemon",
	.fops = &mma7660_fops,
};

static int mma7660_remove(struct i2c_client *client)
{
	struct mma7660_data *mma7660 = i2c_get_clientdata(client);
	
    misc_deregister(&mma7660_device);
    input_unregister_device(mma7660->input_dev);
    input_free_device(mma7660->input_dev);
    free_irq(client->irq, mma7660);
    kfree(mma7660); 
#ifdef CONFIG_ANDROID_POWER
    android_unregister_early_suspend(&mma7660_early_suspend);
#endif      
    this_client = NULL;
	return 0;
}

#ifdef CONFIG_ANDROID_POWER
static int mma7660_suspend(android_early_suspend_t *h)
{
	struct i2c_client *client = container_of(mma7660_device.parent, struct i2c_client, dev);
	rk28printk("Gsensor mma7760 enter suspend\n");
	return mma7660_close_dev(client);
}

static int mma7660_resume(android_early_suspend_t *h)
{
	struct i2c_client *client = container_of(mma7660_device.parent, struct i2c_client, dev);
    struct mma7660_data *mma7660 = (struct mma7660_data *)i2c_get_clientdata(client);
	rk28printk("Gsensor mma7760 resume!!\n");
	return mma7660_start_dev(mma7660->curr_tate);
}
#else
static int mma7660_suspend(struct i2c_client *client, pm_message_t mesg)
{
	rk28printk("Gsensor mma7760 enter 2 level  suspend\n");
	return mma7660_close_dev(client);
}
static int mma7660_resume(struct i2c_client *client)
{
	struct mma7660_data *mma7660 = (struct mma7660_data *)i2c_get_clientdata(client);
	rk28printk("Gsensor mma7760 2 level resume!!\n");
	return mma7660_start_dev(client, mma7660->curr_tate);
}
#endif

static const struct i2c_device_id mma7660_id[] = {
		{"gs_mma7660", 0},
		{ }
};

static struct i2c_driver mma7660_driver = {
	.driver = {
		.name = "gs_mma7660",
	    },
	.id_table 	= mma7660_id,
	.probe		= mma7660_probe,
	.remove		= __devexit_p(mma7660_remove),
#ifndef CONFIG_ANDROID_POWER	
	.suspend = &mma7660_suspend,
	.resume = &mma7660_resume,
#endif	
};


static int mma7660_init_client(struct i2c_client *client)
{
	struct mma7660_data *mma7660;
	int ret;
	mma7660 = i2c_get_clientdata(client);
	rk28printk("gpio_to_irq(%d) is %d\n",client->irq,gpio_to_irq(client->irq));
	if ( !gpio_is_valid(client->irq)) {
		rk28printk("+++++++++++gpio_is_invalid\n");
		return -EINVAL;
	}
	ret = gpio_request(client->irq, "mma7660_int");
	if (ret) {
		rk28printk( "failed to request mma7990_trig GPIO%d\n",gpio_to_irq(client->irq));
		return ret;
	}
    ret = gpio_direction_input(client->irq);
    if (ret) {
        rk28printk("failed to set mma7990_trig GPIO gpio input\n");
		return ret;
    }
	gpio_pull_updown(client->irq, GPIOPullUp);
	client->irq = gpio_to_irq(client->irq);
	ret = request_irq(client->irq, mma7660_interrupt, IRQF_TRIGGER_LOW, client->dev.driver->name, mma7660);
	rk28printk("request irq is %d,ret is  0x%x\n",client->irq,ret);
	if (ret ) {
		rk28printk(KERN_ERR "mma7660_init_client: request irq failed,ret is %d\n",ret);
        return ret;
	}
	disable_irq(client->irq);
	init_waitqueue_head(&data_ready_wq);
 
	return 0;
}

static int  mma7660_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct mma7660_data *mma7660;
	int err;
	
	Xaverage = Yaverage = Zaverage = RawDataNum = 0;

	mma7660 = kzalloc(sizeof(struct mma7660_data), GFP_KERNEL);
	if (!mma7660) {
		rk28printk("[mma7660]:alloc data failed.\n");
		err = -ENOMEM;
		goto exit_alloc_data_failed;
	}
    
	INIT_WORK(&mma7660->work, mma7660_work_func);
	INIT_DELAYED_WORK(&mma7660->delaywork, mma7660_delaywork_func);

	mma7660->client = client;
	//mma7660->swap_xy = 
	i2c_set_clientdata(client, mma7660);

	this_client = client;

	err = mma7660_init_client(client);
	if (err < 0) {
		rk28printk(KERN_ERR
		       "mma7660_probe: mma7660_init_client failed\n");
		goto exit_request_gpio_irq_failed;
	}
		
	mma7660->input_dev = input_allocate_device();
	if (!mma7660->input_dev) {
		err = -ENOMEM;
		rk28printk(KERN_ERR
		       "mma7660_probe: Failed to allocate input device\n");
		goto exit_input_allocate_device_failed;
	}

	set_bit(EV_ABS, mma7660->input_dev->evbit);

	/* x-axis acceleration */
	input_set_abs_params(mma7660->input_dev, ABS_X, -MMA7660_RANGE, MMA7660_RANGE, 0, 0);
	/* y-axis acceleration */
	input_set_abs_params(mma7660->input_dev, ABS_Y, -MMA7660_RANGE, MMA7660_RANGE, 0, 0);
	/* z-axis acceleration */
	input_set_abs_params(mma7660->input_dev, ABS_Z, -MMA7660_RANGE, MMA7660_RANGE, 0, 0);

	mma7660->input_dev->name = "gsensor";
	mma7660->input_dev->dev.parent = &client->dev;

	err = input_register_device(mma7660->input_dev);
	if (err < 0) {
		rk28printk(KERN_ERR
		       "mma7660_probe: Unable to register input device: %s\n",
		       mma7660->input_dev->name);
		goto exit_input_register_device_failed;
	}

    mma7660_device.parent = &client->dev;
	err = misc_register(&mma7660_device);
	if (err < 0) {
		rk28printk(KERN_ERR
		       "mma7660_probe: mmad_device register failed\n");
		goto exit_misc_device_register_mma7660_device_failed;
	}

	err = gsensor_sysfs_init();
	if (err < 0) {
		rk28printk(KERN_ERR
            "mma7660_probe: gsensor sysfs init failed\n");
		goto exit_gsensor_sysfs_init_failed;
	}
	
#ifdef CONFIG_ANDROID_POWER
    mma7660_early_suspend.suspend = mma7660_suspend;
    mma7660_early_suspend.resume = mma7660_resume;
    mma7660_early_suspend.level = 0x2;
    android_register_early_suspend(&mma7660_early_suspend);
#endif
	rk28printk(KERN_INFO "mma7660 probe ok\n");
	mma7660->status = -1;
#if 0	
	mma7660_start(client, MMA7660_RATE_32);
#endif
	return 0;

exit_gsensor_sysfs_init_failed:
    misc_deregister(&mma7660_device);
exit_misc_device_register_mma7660_device_failed:
    input_unregister_device(mma7660->input_dev);
exit_input_register_device_failed:
	input_free_device(mma7660->input_dev);
exit_input_allocate_device_failed:
    free_irq(client->irq, mma7660);
exit_request_gpio_irq_failed:
	kfree(mma7660);	
exit_alloc_data_failed:
    ;
	return err;
}


static int __init mma7660_i2c_init(void)
{
	return i2c_add_driver(&mma7660_driver);
}

static void __exit mma7660_i2c_exit(void)
{
	i2c_del_driver(&mma7660_driver);
}

module_init(mma7660_i2c_init);
module_exit(mma7660_i2c_exit);



