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
#ifdef CONFIG_ANDROID_POWER
#include <linux/android_power.h>
#endif

//#define RK28_PRINT
//#include <asm/arch/rk28_debug.h>

#define rk28printk(x...) printk(x)

static int  mma7660_probe(struct i2c_client *client, const struct i2c_device_id *id);


#define MMA7660_GPIO_INT     RK2818_PIN_PE3

/* Addresses to scan -- protected by sense_data_mutex */
static char sense_data[RBUFF_SIZE + 1];
static struct i2c_client *this_client;

static DECLARE_WAIT_QUEUE_HEAD(data_ready_wq);
static atomic_t data_ready;
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

static int mma7660_rx_data(char *rxData, int length)
{
#if 0
	struct i2c_msg msgs[] = {
		{
		 .addr = this_client->addr,
		 .flags = 1,
		 .len = length,
		 .buf = rxData,
		 },
	};

	if (i2c_transfer(this_client->adapter, msgs, 1) < 0) {
		rk28printk(KERN_ERR "MMA7660 mma7660_rx_data: transfer error\n");
		return -EIO;
	} else
		return 0;
#else
    int ret;
	struct i2c_adapter *adap = this_client->adapter;
	struct i2c_msg msgs[2];

	msgs[0].addr = this_client->addr;
	msgs[0].buf = (char *)rxData;
	msgs[0].flags = this_client->flags;
	msgs[0].len = 1;
	msgs[0].scl_rate = 200*1000;

	msgs[1].addr = this_client->addr;
	msgs[1].buf = (char *)rxData;
	msgs[1].flags = this_client->flags | I2C_M_RD;
	msgs[1].len = 3;
	msgs[1].scl_rate = 200*1000;

	ret = i2c_transfer(adap, msgs, 2);

	return ret;
#endif
}

static int mma7660_tx_data(char *txData, int length)
{
#if 0
	struct i2c_msg msg[] = {
		{
		 .addr = this_client->addr,
		 .flags = 0,
		 .len = length,
		 .buf = txData,
		 },
	};
	if (i2c_transfer(this_client->adapter, msg, 1) < 0) {
		rk28printk(KERN_ERR "MMA7660 mma7660_tx_data: transfer error\n");
		return -EIO;
	} else
		return 0;
#else
    int ret;
	struct i2c_adapter *adap = this_client->adapter;
	struct i2c_msg msg;

	msg.addr = this_client->addr;
	msg.buf = txData;
	msg.len = length;
	msg.flags = this_client->flags;
	msg.scl_rate = 200*1000;
    
	ret = i2c_transfer(adap, &msg, 1);

	return ret;
#endif
}

static int mma7660_set_rate(char rate)
{
	char buffer[2];
	int ret = 0;
	int i;
	
	if (rate > 128)
        return -EINVAL;

    for (i = 0; i < 7; i++) {
        if (rate & (0x1 << i))
            break;
    }   

	buffer[0] = MMA7660_REG_SR;
	buffer[1] = 0xf8 | (0x07 & (~i));

	ret = mma7660_tx_data(&(buffer[0]), 2);

	return ret;
}

static int mma7660_start_dev(char rate)
{
	char buffer[MMA7660_REG_LEN];
	int ret = 0;

	buffer[0] = MMA7660_REG_INTSU;
	buffer[1] = 0x10;	//0x10; modify by zhao
	ret = mma7660_tx_data(&buffer[0], 2);

	ret = mma7660_set_rate(rate);

	buffer[0] = MMA7660_REG_MODE;
	buffer[1] = 0x81;
	ret = mma7660_tx_data(&buffer[0], 2);

	rk28printk("\n----------------------------mma7660_start------------------------\n");
	
	return ret;
}

static int mma7660_start(char rate)
{ 
    struct mma7660_data *mma = (struct mma7660_data *)i2c_get_clientdata(this_client);
    
    if (mma->status == MMA7660_OPEN) {
        return 0;      
    }
    mma->status = MMA7660_OPEN;
    return mma7660_start_dev(rate);
}

static int mma7660_close_dev(void)
{    	
	char buffer[2];
		
	buffer[0] = MMA7660_REG_MODE;
	buffer[1] = 0x00;
	
	return mma7660_tx_data(buffer, 2);
}

static int mma7660_close(void)
{
    struct mma7660_data *mma = (struct mma7660_data *)i2c_get_clientdata(this_client);
    
    mma->status = MMA7660_CLOSE;
    
    return mma7660_close_dev();
}

static int mma7660_reset_rate(char rate)
{
	int ret = 0;
	
    ret = mma7660_close_dev();
    ret = mma7660_start_dev(rate);
    
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

static void mma7660_report_value(short *rbuf)
{
	struct mma7660_data *data = i2c_get_clientdata(this_client);
    struct mma7660_axis *axis = (struct mma7660_axis *)rbuf;

	/* Report acceleration sensor information */
    input_report_abs(data->input_dev, ABS_X, axis->x);
    input_report_abs(data->input_dev, ABS_Y, axis->y);
    input_report_abs(data->input_dev, ABS_Z, axis->z);
    input_sync(data->input_dev);
    rk28printk("Gsensor x==%d  y==%d z==%d\n",axis->x,axis->y,axis->z);
}

static int mma7660_get_data(void)
{
	char buffer[3];
	int ret;
    struct mma7660_axis axis;

	memset(buffer, 0, 3);
	buffer[0] = MMA7660_REG_X_OUT;
    ret = mma7660_rx_data(&buffer[0], 3);
	//while(mma7660_rx_data(&buffer[0], 3));
	/*
    if (!ret) {
        rk28printk( "%s: -------------------------------------------gsensor device register = [0]:%d  [1]:%d  [2]:%d  [3]:0x%x  [4]:0x%x  [5]:0x%x  [6]:0x%x  [7]:0x%x-----------------------------------------------\n",
               __func__, buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5], buffer[6], buffer[7]); 
    } 
    */
	if (ret < 0)
		return ret;

	axis.x = mma7660_convert_to_int(buffer[MMA7660_REG_X_OUT]);
	axis.y = -mma7660_convert_to_int(buffer[MMA7660_REG_Y_OUT]);
	axis.z = -mma7660_convert_to_int(buffer[MMA7660_REG_Z_OUT]);

    rk28printk( "%s: -------------------------------------------mma7660_GetData axis = %d  %d  %d-----------------------------------------------\n",
           __func__, axis.x, axis.y, axis.z); 
     
    memcpy(sense_data, &axis, sizeof(axis));
    mma7660_report_value(sense_data);
	//atomic_set(&data_ready, 0);
	//wake_up(&data_ready_wq);

	return 0;
}

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

static int mma7660_open(struct inode *inode, struct file *file)
{
	rk28printk("----------------------------mma7660_open------------------------\n");
	return 0;//nonseekable_open(inode, file);
}

static int mma7660_release(struct inode *inode, struct file *file)
{
	return 0;
}

static int mma7660_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
	   unsigned long arg)
{

	void __user *argp = (void __user *)arg;
	char msg[RBUFF_SIZE + 1];
	int ret = -1;
	char rate;

	rk28printk("----------------------------mma7660_ioctl------------------------cmd: %d\n", cmd);

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
		ret = mma7660_start(MMA7660_RATE_32);
		if (ret < 0)
			return ret;
		break;
	case ECS_IOCTL_CLOSE:
		ret = mma7660_close();
		if (ret < 0)
			return ret;
		break;
	case ECS_IOCTL_APP_SET_RATE:
		ret = mma7660_reset_rate(rate);
		if (ret < 0)
			return ret;
		break;
	case ECS_IOCTL_GETDATA:
		ret = mma7660_trans_buff(msg, RBUFF_SIZE);
		if (ret < 0)
			return ret;
		break;
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
	if (mma7660_get_data() < 0) 
		rk28printk(KERN_ERR "MMA7660 mma_work_func: Get data failed\n");
		
    //GPIOClrearInmarkIntr(MMA7660_GPIO_INT);
	
	rk28printk("---------------------------------------mma7660_work_func----------------------------------\n");
}

static void  mma7660_delaywork_func(struct work_struct  *work)
{
	
	if (mma7660_get_data() < 0) 
		rk28printk(KERN_ERR "MMA7660 mma_work_func: Get data failed\n");
		
	//GPIOClrearInmarkIntr(MMA7660_GPIO_INT);
	
	rk28printk("---------------------------------------mma7660_delaywork_func------------------------------\n");

}

static irqreturn_t mma7660_interrupt(int irq, void *dev_id)
{
	struct mma7660_data *data = dev_id;

	///GPIOInmarkIntr(MMA7660_GPIO_INT);
	//schedule_work(&data->work);
	schedule_delayed_work(&data->delaywork,msecs_to_jiffies(30));
	rk28printk("--------------------------------------mma7660_interrupt---------------------------------------\n");
	
	return IRQ_HANDLED;
}

static struct file_operations mma7660_fops = {
	.owner = THIS_MODULE,
	.open = mma7660_open,
	.release = mma7660_release,
	.ioctl = mma7660_ioctl,
};

static struct miscdevice mma7660_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "mma7660_daemon",
	.fops = &mma7660_fops,
};

static int mma7660_remove(struct i2c_client *client)
{
	struct mma7660_data *mma = i2c_get_clientdata(client);
	
    misc_deregister(&mma7660_device);
    input_unregister_device(mma->input_dev);
    input_free_device(mma->input_dev);
    free_irq(client->irq, mma);
    kfree(mma); 
#ifdef CONFIG_ANDROID_POWER
    android_unregister_early_suspend(&mma7660_early_suspend);
#endif      
    this_client = NULL;
	return 0;
}

#ifdef CONFIG_ANDROID_POWER
static int mma7660_suspend(android_early_suspend_t *h)
{
	rk28printk("Gsensor mma7760 enter suspend\n");
	return mma7660_close_dev();
}

static int mma7660_resume(android_early_suspend_t *h)
{
    struct mma7660_data *mma = (struct mma7660_data *)i2c_get_clientdata(this_client);
	rk28printk("Gsensor mma7760 resume!!\n");
	return mma7660_start_dev(mma->curr_tate);
}
/*
static int suspend(struct i2c_client *client, pm_message_t mesg)
{
	rk28printk("Gsensor mma7760 enter 2 level  suspend\n");
	return mma7660_close_dev();
}
static int resume(struct i2c_client *client)
{
	struct mma7660_data *mma = (struct mma7660_data *)i2c_get_clientdata(this_client);
	rk28printk("Gsensor mma7760 2 level resume!!\n");
	return mma7660_start_dev(mma->curr_tate);
}
*/
#else
#define mma7660_suspend NULL
#define mma7660_resume NULL
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
	//.suspend = &suspend,
	//.resume = &resume,
};


static int mma7660_init_client(struct i2c_client *client)
{
	struct mma7660_data *data;
	int ret;
	data = i2c_get_clientdata(client);

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

	client->irq = gpio_to_irq(client->irq);
	ret = request_irq(client->irq, mma7660_interrupt, IRQF_TRIGGER_RISING, client->dev.driver->name, data);
	rk28printk("request irq is %d,ret is  0x%x\n",client->irq,ret);
	if (ret ) {
		rk28printk(KERN_ERR "mma7660_init_client: request irq failed,ret is %d\n",ret);
        return ret;
	}
	init_waitqueue_head(&data_ready_wq);
 
	return 0;
}

static int  mma7660_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct mma7660_data *mma;
	int err;

	mma = kzalloc(sizeof(struct mma7660_data), GFP_KERNEL);
	if (!mma) {
		rk28printk("[mma7660]:alloc data failed.\n");
		err = -ENOMEM;
		goto exit_alloc_data_failed;
	}

	INIT_WORK(&mma->work, mma7660_work_func);
	INIT_DELAYED_WORK(&mma->delaywork, mma7660_delaywork_func);

	mma->client = client;
	i2c_set_clientdata(client, mma);

	this_client = client;

	err = mma7660_init_client(client);
	if (err < 0) {
		rk28printk(KERN_ERR
		       "mma7660_probe: mma7660_init_client failed\n");
		goto exit_request_gpio_irq_failed;
	}
		
	mma->input_dev = input_allocate_device();
	if (!mma->input_dev) {
		err = -ENOMEM;
		rk28printk(KERN_ERR
		       "mma7660_probe: Failed to allocate input device\n");
		goto exit_input_allocate_device_failed;
	}

	set_bit(EV_ABS, mma->input_dev->evbit);

	/* x-axis acceleration */
	input_set_abs_params(mma->input_dev, ABS_X, -1500, 1500, 0, 0);
	/* y-axis acceleration */
	input_set_abs_params(mma->input_dev, ABS_Y, -1500, 1500, 0, 0);
	/* z-axis acceleration */
	input_set_abs_params(mma->input_dev, ABS_Z, -1500, 1500, 0, 0);

	mma->input_dev->name = "compass";

	err = input_register_device(mma->input_dev);
	if (err < 0) {
		rk28printk(KERN_ERR
		       "mma7660_probe: Unable to register input device: %s\n",
		       mma->input_dev->name);
		goto exit_input_register_device_failed;
	}

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

	return 0;

exit_gsensor_sysfs_init_failed:
    misc_deregister(&mma7660_device);
exit_misc_device_register_mma7660_device_failed:
    input_unregister_device(mma->input_dev);
exit_input_register_device_failed:
	input_free_device(mma->input_dev);
exit_input_allocate_device_failed:
    free_irq(client->irq, mma);
exit_request_gpio_irq_failed:
	kfree(mma);	
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



