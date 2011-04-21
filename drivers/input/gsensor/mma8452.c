/* drivers/i2c/chips/mma8452.c - mma8452 compass driver
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
#include <asm/atomic.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/freezer.h>
#include <linux/mma8452.h>
#include <mach/gpio.h>
#include <mach/board.h> 
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

// #define ENABLE_DEBUG_LOG
#include <mach/custom_log.h>

#if 1
#define mmaprintk(x...) D(x)
#else
#define mmaprintk(x...)
#endif

// #define ENABLE_VERBOSE_LOG
#ifdef ENABLE_VERBOSE_LOG
#define V(x...) D(x)
#else
#define V(x...)
#endif

static int  mma8452_probe(struct i2c_client *client, const struct i2c_device_id *id);

#define MMA8452_SPEED		200 * 1000
#define MMA8451_DEVID		0x1a
#define MMA8452_DEVID		0x2a
/* Addresses to scan -- protected by sense_data_mutex */
//static char sense_data[RBUFF_SIZE + 1];
static struct i2c_client *this_client;
static struct miscdevice mma8452_device;

static DECLARE_WAIT_QUEUE_HEAD(data_ready_wq);

#ifdef CONFIG_HAS_EARLYSUSPEND
static struct early_suspend mma8452_early_suspend;
#endif
static int revision = -1;
static const char* vendor = "Freescale Semiconductor";


typedef char status_t;
/*status*/
#define MMA8452_OPEN           1
#define MMA8452_CLOSE          0


// .! : 设备实例数据类型. 
struct mma8452_data {
    status_t status;
    char  curr_tate;
	struct input_dev *input_dev;
	struct i2c_client *client;
	struct work_struct work;
	struct delayed_work delaywork;	/*report second event*/
    
    /** 缓存 sensor 数据. */
    struct mma8452_axis sense_data;
    /** 对 "sense_data" 的互斥保护. */
    struct mutex sense_data_mutex;

    /** 标识 "sense_data" 中的数据是否有效. */
    atomic_t data_ready;
    /** 用来等待 数据 ready 的等待队列头. */
    wait_queue_head_t data_ready_wq;

    /** 标识 设备被要求 START 采集数据的次数, 对应 MMA_IOCTL_START 可能被调用多次的情况. */
    int start_count;
    /** 对 'start_count' 和相关操作的互斥保护. */
    struct mutex operation_mutex;
};

/* AKM HW info */
static ssize_t gsensor_vendor_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;

	// sprintf(buf, "%#x\n", revision);
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
		mmaprintk(KERN_ERR
		       "MMA8452 gsensor_sysfs_init:"\
		       "subsystem_register failed\n");
		ret = -ENOMEM;
		goto err;
	}

	ret = sysfs_create_file(android_gsensor_kobj, &dev_attr_vendor.attr);   // "vendor"
	if (ret) {
		mmaprintk(KERN_ERR
		       "MMA8452 gsensor_sysfs_init:"\
		       "sysfs_create_group failed\n");
		goto err4;
	}

	return 0 ;
err4:
	kobject_del(android_gsensor_kobj);
err:
	return ret ;
}

static int mma8452_rx_data(struct i2c_client *client, char *rxData, int length)
{
	int ret = 0;
	char reg = rxData[0];
	ret = i2c_master_reg8_recv(client, reg, rxData, length, MMA8452_SPEED);
	return (ret > 0)? 0 : ret;
}

static int mma8452_tx_data(struct i2c_client *client, char *txData, int length)
{
	int ret = 0;
	char reg = txData[0];
	ret = i2c_master_reg8_send(client, reg, &txData[1], length-1, MMA8452_SPEED);
	return (ret > 0)? 0 : ret;
}

static char mma845x_read_reg(struct i2c_client *client,int addr)
{
	char tmp;
	int ret = 0;

	tmp = addr;
//	ret = mma8452_tx_data(client, &tmp, 1);
	ret = mma8452_rx_data(client, &tmp, 1);
	return tmp;
}

static int mma845x_write_reg(struct i2c_client *client,int addr,int value)
{
	char buffer[3];
	int ret = 0;

	buffer[0] = addr;
	buffer[1] = value;
	ret = mma8452_tx_data(client, &buffer[0], 2);
	return ret;
}


static char mma8452_get_devid(struct i2c_client *client)
{
	printk("mma8452 devid:%x\n",mma845x_read_reg(client,MMA8452_REG_WHO_AM_I));
	return mma845x_read_reg(client,MMA8452_REG_WHO_AM_I);
}

static int mma845x_active(struct i2c_client *client,int enable)
{
	int tmp;
	int ret = 0;
	
	tmp = mma845x_read_reg(client,MMA8452_REG_CTRL_REG1);
	if(enable)
		tmp |=ACTIVE_MASK;
	else
		tmp &=~ACTIVE_MASK;
	mmaprintk("mma845x_active %s (0x%x)\n",enable?"active":"standby",tmp);	
	ret = mma845x_write_reg(client,MMA8452_REG_CTRL_REG1,tmp);
	return ret;
}

static int mma8452_start_test(struct i2c_client *client)
{
	int ret = 0;
	int tmp;

	mmaprintk("-------------------------mma8452 start test------------------------\n");	
	
	/* standby */
	mma845x_active(client,0);
	mmaprintk("mma8452 MMA8452_REG_SYSMOD:%x\n",mma845x_read_reg(client,MMA8452_REG_SYSMOD));

	/* disable FIFO  FMODE = 0*/
	ret = mma845x_write_reg(client,MMA8452_REG_F_SETUP,0);
	mmaprintk("mma8452 MMA8452_REG_F_SETUP:%x\n",mma845x_read_reg(client,MMA8452_REG_F_SETUP));

	/* set full scale range to 2g */
	ret = mma845x_write_reg(client,MMA8452_REG_XYZ_DATA_CFG,0);
	mmaprintk("mma8452 MMA8452_REG_XYZ_DATA_CFG:%x\n",mma845x_read_reg(client,MMA8452_REG_XYZ_DATA_CFG));

	/* set bus 8bit/14bit(FREAD = 1,FMODE = 0) ,data rate*/
	tmp = (MMA8452_RATE_12P5<< MMA8452_RATE_SHIFT) | FREAD_MASK;
	ret = mma845x_write_reg(client,MMA8452_REG_CTRL_REG1,tmp);
	mmaprintk("mma8452 MMA8452_REG_CTRL_REG1:%x\n",mma845x_read_reg(client,MMA8452_REG_CTRL_REG1));
	
	mmaprintk("mma8452 MMA8452_REG_SYSMOD:%x\n",mma845x_read_reg(client,MMA8452_REG_SYSMOD));

	ret = mma845x_write_reg(client,MMA8452_REG_CTRL_REG3,5);
	mmaprintk("mma8452 MMA8452_REG_CTRL_REG3:%x\n",mma845x_read_reg(client,MMA8452_REG_CTRL_REG3));
	
	ret = mma845x_write_reg(client,MMA8452_REG_CTRL_REG4,1);
	mmaprintk("mma8452 MMA8452_REG_CTRL_REG4:%x\n",mma845x_read_reg(client,MMA8452_REG_CTRL_REG4));

	ret = mma845x_write_reg(client,MMA8452_REG_CTRL_REG5,1);
	mmaprintk("mma8452 MMA8452_REG_CTRL_REG5:%x\n",mma845x_read_reg(client,MMA8452_REG_CTRL_REG5));	

	mmaprintk("mma8452 MMA8452_REG_SYSMOD:%x\n",mma845x_read_reg(client,MMA8452_REG_SYSMOD));
	mma845x_active(client,1);
	mmaprintk("mma8452 MMA8452_REG_SYSMOD:%x\n",mma845x_read_reg(client,MMA8452_REG_SYSMOD));

	enable_irq(client->irq);
	msleep(50);

	return ret;
}

static int mma8452_start_dev(struct i2c_client *client, char rate)
{
	int ret = 0;
	int tmp;
	struct mma8452_data *mma8452 = (struct mma8452_data *)i2c_get_clientdata(client);   // mma8452_data 定义在 mma8452.h 中. 

	mmaprintk("-------------------------mma8452 start ------------------------\n");	
	/* standby */
	mma845x_active(client,0);
	mmaprintk("mma8452 MMA8452_REG_SYSMOD:%x\n",mma845x_read_reg(client,MMA8452_REG_SYSMOD));

	/* disable FIFO  FMODE = 0*/
	ret = mma845x_write_reg(client,MMA8452_REG_F_SETUP,0);
	mmaprintk("mma8452 MMA8452_REG_F_SETUP:%x\n",mma845x_read_reg(client,MMA8452_REG_F_SETUP));

	/* set full scale range to 2g */
	ret = mma845x_write_reg(client,MMA8452_REG_XYZ_DATA_CFG,0);
	mmaprintk("mma8452 MMA8452_REG_XYZ_DATA_CFG:%x\n",mma845x_read_reg(client,MMA8452_REG_XYZ_DATA_CFG));

	/* set bus 8bit/14bit(FREAD = 1,FMODE = 0) ,data rate*/
	tmp = (rate<< MMA8452_RATE_SHIFT) | FREAD_MASK;
	ret = mma845x_write_reg(client,MMA8452_REG_CTRL_REG1,tmp);
	mma8452->curr_tate = rate;
	mmaprintk("mma8452 MMA8452_REG_CTRL_REG1:%x\n",mma845x_read_reg(client,MMA8452_REG_CTRL_REG1));
	
	mmaprintk("mma8452 MMA8452_REG_SYSMOD:%x\n",mma845x_read_reg(client,MMA8452_REG_SYSMOD));

	ret = mma845x_write_reg(client,MMA8452_REG_CTRL_REG3,5);
	mmaprintk("mma8452 MMA8452_REG_CTRL_REG3:%x\n",mma845x_read_reg(client,MMA8452_REG_CTRL_REG3));
	
	ret = mma845x_write_reg(client,MMA8452_REG_CTRL_REG4,1);
	mmaprintk("mma8452 MMA8452_REG_CTRL_REG4:%x\n",mma845x_read_reg(client,MMA8452_REG_CTRL_REG4));

	ret = mma845x_write_reg(client,MMA8452_REG_CTRL_REG5,1);
	mmaprintk("mma8452 MMA8452_REG_CTRL_REG5:%x\n",mma845x_read_reg(client,MMA8452_REG_CTRL_REG5));	

	mmaprintk("mma8452 MMA8452_REG_SYSMOD:%x\n",mma845x_read_reg(client,MMA8452_REG_SYSMOD));
	mma845x_active(client,1);
	mmaprintk("mma8452 MMA8452_REG_SYSMOD:%x\n",mma845x_read_reg(client,MMA8452_REG_SYSMOD));
	
	enable_irq(client->irq);
	return ret;

}

static int mma8452_start(struct i2c_client *client, char rate)
{ 
    struct mma8452_data *mma8452 = (struct mma8452_data *)i2c_get_clientdata(client);
    
   printk("%s::enter\n",__FUNCTION__); 
    if (mma8452->status == MMA8452_OPEN) {
        return 0;      
    }
    mma8452->status = MMA8452_OPEN;
    return mma8452_start_dev(client, rate);
}

static int mma8452_close_dev(struct i2c_client *client)
{    	
	disable_irq_nosync(client->irq);
	return mma845x_active(client,0);
}

static int mma8452_close(struct i2c_client *client)
{
    struct mma8452_data *mma8452 = (struct mma8452_data *)i2c_get_clientdata(client);
   printk("%s::enter\n",__FUNCTION__); 
    mma8452->status = MMA8452_CLOSE;
    
    return mma8452_close_dev(client);
}

static int mma8452_reset_rate(struct i2c_client *client, char rate)
{
	int ret = 0;
	
	mmaprintk("\n----------------------------mma8452_reset_rate------------------------\n");
	
    ret = mma8452_close_dev(client);
    ret = mma8452_start_dev(client, rate);
  
	return ret ;
}

static inline int mma8452_convert_to_int(char value)
{
    int result;

    if (value < MMA8452_BOUNDARY) {
       result = value * MMA8452_GRAVITY_STEP;
    } else {
       result = ~(((~value & 0x7f) + 1)* MMA8452_GRAVITY_STEP) + 1;
    }

    return result;
}

static void mma8452_report_value(struct i2c_client *client, struct mma8452_axis *axis)
{
	struct mma8452_data *mma8452 = i2c_get_clientdata(client);
    //struct mma8452_axis *axis = (struct mma8452_axis *)rbuf;

	/* Report acceleration sensor information */
    input_report_abs(mma8452->input_dev, ABS_X, axis->x);
    input_report_abs(mma8452->input_dev, ABS_Y, axis->y);
    input_report_abs(mma8452->input_dev, ABS_Z, axis->z);
    input_sync(mma8452->input_dev);
    V("Gsensor x==%d  y==%d z==%d\n",axis->x,axis->y,axis->z);
}

/** 在 底半部执行, 具体获取 g sensor 数据. */
static int mma8452_get_data(struct i2c_client *client)
{
    struct mma8452_data* mma8452 = i2c_get_clientdata(client);
	char buffer[6];
	int ret;
    struct mma8452_axis axis;
    struct mma8452_platform_data *pdata = pdata = client->dev.platform_data;

    do {
        memset(buffer, 0, 3);
        buffer[0] = MMA8452_REG_X_OUT_MSB;
		ret = mma8452_tx_data(client, &buffer[0], 1);
        ret = mma8452_rx_data(client, &buffer[0], 3);
        if (ret < 0)
            return ret;
    } while (0);

	V("0x%02x 0x%02x 0x%02x \n",buffer[0],buffer[1],buffer[2]);

	axis.x = mma8452_convert_to_int(buffer[0]);
	axis.y = mma8452_convert_to_int(buffer[1]);
	axis.z = mma8452_convert_to_int(buffer[2]);

	if(pdata->swap_xy)
	{
		axis.y = -axis.y;
		swap(axis.x,axis.y);		
	}
	
    V( "%s: ------------------mma8452_GetData axis = %d  %d  %d--------------\n",
            __func__, axis.x, axis.y, axis.z); 
     
    //memcpy(sense_data, &axis, sizeof(axis));
    mma8452_report_value(client, &axis);
	//atomic_set(&data_ready, 0);
	//wake_up(&data_ready_wq);

    /* 互斥地缓存数据. */
    mutex_lock(&(mma8452->sense_data_mutex) );
    mma8452->sense_data = axis;
    mutex_unlock(&(mma8452->sense_data_mutex) );

    /* 置位 data_ready */
    atomic_set(&(mma8452->data_ready), 1);
    /* 唤醒 data_ready 等待队列头. */
	wake_up(&(mma8452->data_ready_wq) );

	return 0;
}

/*
static int mma8452_trans_buff(char *rbuf, int size)
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

/**
 * 获取当前已经 cache 了的 sensor 数据.
 * @param sense_data
 *          指向返回用 buffer.
 * @param client
 *          当前 i2c client 设备实例数据的指针. 
 * @return
 *          若成功, 返回 0; 否则, 返回其它.
 */
static int mma8452_get_cached_data(struct i2c_client* client, struct mma8452_axis* sense_data)
{
    struct mma8452_data* this = (struct mma8452_data *)i2c_get_clientdata(client);

    /* 有超时地, 等待数据 ready. */
    wait_event_interruptible_timeout(this->data_ready_wq, 
                                     atomic_read(&(this->data_ready) ),
                                     msecs_to_jiffies(1000) );
    /* 若超时, 且数据没有 ready. */
    if ( 0 == atomic_read(&(this->data_ready) ) ) {
        E("waiting 'data_ready_wq' timed out.");
        /* 返回 error. */
        return -1;
    }
    /* 数据已经 ready, 互斥地返回数据. */
    mutex_lock(&(this->sense_data_mutex) );
    *sense_data = this->sense_data;
    mutex_unlock(&(this->sense_data_mutex) );
    /* 返回. */
    return 0;
}

static int mma8452_open(struct inode *inode, struct file *file)
{
	return 0;//nonseekable_open(inode, file);
}

static int mma8452_release(struct inode *inode, struct file *file)
{
	return 0;
}

static int mma8452_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
	   unsigned long arg)
{

	void __user *argp = (void __user *)arg;
	// char msg[RBUFF_SIZE + 1];
    struct mma8452_axis sense_data = {0};
	int ret = -1;
	char rate;
	struct i2c_client *client = container_of(mma8452_device.parent, struct i2c_client, dev);
    struct mma8452_data* this = (struct mma8452_data *)i2c_get_clientdata(client);  /* 设备数据实例的指针. */

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
        /* 上锁. */
        mutex_lock(&(this->operation_mutex) );
        D("to perform 'MMA_IOCTL_START', former 'start_count' is %d.", this->start_count);
        /* 记数 自加. */
        (this->start_count)++;
        /* 若是初次 start, 则... */
        if ( 1 == this->start_count ) {
            /* 复位 data_ready. */
            atomic_set(&(this->data_ready), 0);
            /* 执行具体的对硬件的启动操作. */
            if ( (ret = mma8452_start(client, MMA8452_RATE_12P5) ) < 0 ) {
                mutex_unlock(&(this->operation_mutex) );
                return ret;
            }
        }
        /* 解锁. */
        mutex_unlock(&(this->operation_mutex) );
        D("finish 'MMA_IOCTL_START', ret = %d.", ret);
        /* 返回. */
        return 0;

	case MMA_IOCTL_CLOSE:
        /* 上锁. */
        mutex_lock(&(this->operation_mutex) );
        D("to perform 'MMA_IOCTL_CLOSE', former 'start_count' is %d, PID : %d", this->start_count, get_current()->pid);
        /* 若记数自减到 0, 则... */
        if ( 0 == (--(this->start_count) ) ) {
            /* 复位 data_ready. */
            atomic_set(&(this->data_ready), 0);
            /* 执行具体的对硬件的 stop 操作. */
            if ( (ret = mma8452_close(client) ) < 0 ) {
                mutex_unlock(&(this->operation_mutex) );
                return ret;
            }
        }
        /* 解锁. */
        mutex_unlock(&(this->operation_mutex) );
        /* 返回. */
        return 0;

	case MMA_IOCTL_APP_SET_RATE:
		ret = mma8452_reset_rate(client, rate);
		if (ret < 0)
			return ret;
		break;
	case MMA_IOCTL_GETDATA:
		// ret = mma8452_trans_buff(msg, RBUFF_SIZE);
		if ( (ret = mma8452_get_cached_data(client, &sense_data) ) < 0 ) {
            E("failed to get cached sense data, ret = %d.", ret);
			return ret;
        }
		break;
	default:
		return -ENOTTY;
	}

	switch (cmd) {
	case MMA_IOCTL_GETDATA:
        /*
		if (copy_to_user(argp, &msg, sizeof(msg)))
			return -EFAULT;
        */
        if ( copy_to_user(argp, &sense_data, sizeof(sense_data) ) ) {
            D("failed to copy sense data to user space.");
			return -EFAULT;
        }
		break;
	default:
		break;
	}

	return 0;
}

static void mma8452_work_func(struct work_struct *work)
{
	struct mma8452_data *mma8452 = container_of(work, struct mma8452_data, work);
	struct i2c_client *client = mma8452->client;
	
	if (mma8452_get_data(client) < 0) 
		mmaprintk(KERN_ERR "MMA8452 mma_work_func: Get data failed\n");
		
	enable_irq(client->irq);		
}

static void  mma8452_delaywork_func(struct work_struct *work)
{
	struct delayed_work *delaywork = container_of(work, struct delayed_work, work);
	struct mma8452_data *mma8452 = container_of(delaywork, struct mma8452_data, delaywork);
	struct i2c_client *client = mma8452->client;

	if (mma8452_get_data(client) < 0) 
		E(KERN_ERR "MMA8452 mma_work_func: Get data failed\n");
	V("%s :int src:0x%02x\n",__FUNCTION__,mma845x_read_reg(mma8452->client,MMA8452_REG_INTSRC));	
	enable_irq(client->irq);		
}

static irqreturn_t mma8452_interrupt(int irq, void *dev_id)
{
	struct mma8452_data *mma8452 = (struct mma8452_data *)dev_id;
	
	disable_irq_nosync(irq);
    /* .! : 延迟地在 底半部 发起后续操作(读取具体数据). */
	schedule_delayed_work(&mma8452->delaywork, msecs_to_jiffies(30));
	V("%s :enter\n",__FUNCTION__);	
	return IRQ_HANDLED;
}

static struct file_operations mma8452_fops = {
	.owner = THIS_MODULE,
	.open = mma8452_open,
	.release = mma8452_release,
	.ioctl = mma8452_ioctl,
};

/** 控制设备. */
static struct miscdevice mma8452_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "mma8452_daemon",//"mma8452_daemon",
	.fops = &mma8452_fops,
};

static int mma8452_remove(struct i2c_client *client)
{
	struct mma8452_data *mma8452 = i2c_get_clientdata(client);
	
    misc_deregister(&mma8452_device);
    input_unregister_device(mma8452->input_dev);
    input_free_device(mma8452->input_dev);
    free_irq(client->irq, mma8452);
    kfree(mma8452); 
#ifdef CONFIG_HAS_EARLYSUSPEND
    unregister_early_suspend(&mma8452_early_suspend);
#endif      
    this_client = NULL;
	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void mma8452_suspend(struct early_suspend *h)
{
	struct i2c_client *client = container_of(mma8452_device.parent, struct i2c_client, dev);
	struct mma8452_data *mma8452 = (struct mma8452_data *)i2c_get_clientdata(client);
	mmaprintk("Gsensor mma7760 enter suspend mma8452->status %d\n",mma8452->status);
//	if(mma8452->status == MMA8452_OPEN)
//	{
		//mma8452->status = MMA8452_SUSPEND;
//		mma8452_close_dev(client);
//	}
}

static void mma8452_resume(struct early_suspend *h)
{
	struct i2c_client *client = container_of(mma8452_device.parent, struct i2c_client, dev);
    struct mma8452_data *mma8452 = (struct mma8452_data *)i2c_get_clientdata(client);
	mmaprintk("Gsensor mma7760 resume!! mma8452->status %d\n",mma8452->status);
	//if((mma8452->status == MMA8452_SUSPEND) && (mma8452->status != MMA8452_OPEN))
//	if (mma8452->status == MMA8452_OPEN)
//		mma8452_start_dev(client,mma8452->curr_tate);
}
#else
static int mma8452_suspend(struct i2c_client *client, pm_message_t mesg)
{
	int ret;
	mmaprintk("Gsensor mma7760 enter 2 level  suspend mma8452->status %d\n",mma8452->status);
	struct mma8452_data *mma8452 = (struct mma8452_data *)i2c_get_clientdata(client);
//	if(mma8452->status == MMA8452_OPEN)
//	{
	//	mma8452->status = MMA8452_SUSPEND;
//		ret = mma8452_close_dev(client);
//	}
	return ret;
}
static int mma8452_resume(struct i2c_client *client)
{
	int ret;
	struct mma8452_data *mma8452 = (struct mma8452_data *)i2c_get_clientdata(client);
	mmaprintk("Gsensor mma7760 2 level resume!! mma8452->status %d\n",mma8452->status);
//	if((mma8452->status == MMA8452_SUSPEND) && (mma8452->status != MMA8452_OPEN))
//if (mma8452->status == MMA8452_OPEN)
//		ret = mma8452_start_dev(client, mma8452->curr_tate);
	return ret;
}
#endif

static const struct i2c_device_id mma8452_id[] = {
		{"gs_mma8452", 0},
		{ }
};

/** 表征驱动的数据类型定义. */
static struct i2c_driver mma8452_driver = {
	.driver = {
		.name = "gs_mma8452",
	    },
	.id_table 	= mma8452_id,
	.probe		= mma8452_probe,            // .!! : 对 probe() 的调用, 仍是由 注册设备的操作触发的,
                                            //      具体参见 board-rk29sdk.c 中的 board_i2c0_devices.
                                            //      在 board_i2c0_devices 中声明了一个可以使用名称是 "gs_mma8452" 驱动程序的 I2C 设备. 
                                            //      同时也体现了 I2C 地址 和 终端号等信息. 
	.remove		= __devexit_p(mma8452_remove),
#ifndef CONFIG_HAS_EARLYSUSPEND	
	.suspend = &mma8452_suspend,
	.resume = &mma8452_resume,
#endif	
};


static int mma8452_init_client(struct i2c_client *client)
{
	struct mma8452_data *mma8452;
	int ret,irq;
	mma8452 = i2c_get_clientdata(client);
	mmaprintk("gpio_to_irq(%d) is %d\n",client->irq,gpio_to_irq(client->irq));
	if ( !gpio_is_valid(client->irq)) {
		mmaprintk("+++++++++++gpio_is_invalid\n");
		return -EINVAL;
	}
	ret = gpio_request(client->irq, "mma8452_int");
	if (ret) {
		mmaprintk( "failed to request mma7990_trig GPIO%d\n",gpio_to_irq(client->irq));
		return ret;
	}
    ret = gpio_direction_input(client->irq);
    if (ret) {
        mmaprintk("failed to set mma7990_trig GPIO gpio input\n");
		gpio_free(client->irq);
		return ret;
    }
	gpio_pull_updown(client->irq, GPIOPullUp);
	irq = gpio_to_irq(client->irq);
	ret = request_irq(irq, mma8452_interrupt, IRQF_TRIGGER_LOW, client->dev.driver->name, mma8452);
	mmaprintk("request irq is %d,ret is  0x%x\n",irq,ret);
	if (ret ) {
		gpio_free(client->irq);
		mmaprintk(KERN_ERR "mma8452_init_client: request irq failed,ret is %d\n",ret);
        return ret;
	}
	client->irq = irq;
	disable_irq(client->irq);
	init_waitqueue_head(&data_ready_wq);
 
	return 0;
}

static int  mma8452_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct mma8452_data *mma8452;
	struct mma8452_platform_data *pdata = pdata = client->dev.platform_data;
	int err;
	char devid;

	mmaprintk("%s enter\n",__FUNCTION__);

	mma8452 = kzalloc(sizeof(struct mma8452_data), GFP_KERNEL);
	if (!mma8452) {
		mmaprintk("[mma8452]:alloc data failed.\n");
		err = -ENOMEM;
		goto exit_alloc_data_failed;
	}
    
	INIT_WORK(&mma8452->work, mma8452_work_func);
	INIT_DELAYED_WORK(&mma8452->delaywork, mma8452_delaywork_func);

	mma8452->client = client;
	i2c_set_clientdata(client, mma8452);

	this_client = client;

	devid = mma8452_get_devid(this_client);
	if ((MMA8452_DEVID != devid) && (MMA8451_DEVID != devid)) {
		pr_info("mma8452: invalid devid\n");
		goto exit_invalid_devid;
	}

	err = mma8452_init_client(client);
	if (err < 0) {
		mmaprintk(KERN_ERR
		       "mma8452_probe: mma8452_init_client failed\n");
		goto exit_request_gpio_irq_failed;
	}

	mma8452->input_dev = input_allocate_device();
	if (!mma8452->input_dev) {
		err = -ENOMEM;
		mmaprintk(KERN_ERR
		       "mma8452_probe: Failed to allocate input device\n");
		goto exit_input_allocate_device_failed;
	}

	set_bit(EV_ABS, mma8452->input_dev->evbit);

	/* x-axis acceleration */
	input_set_abs_params(mma8452->input_dev, ABS_X, -20000, 20000, 0, 0); //2g full scale range
	/* y-axis acceleration */
	input_set_abs_params(mma8452->input_dev, ABS_Y, -20000, 20000, 0, 0); //2g full scale range
	/* z-axis acceleration */
	input_set_abs_params(mma8452->input_dev, ABS_Z, -20000, 20000, 0, 0); //2g full scale range

	// mma8452->input_dev->name = "compass";
	mma8452->input_dev->name = "gsensor";
	mma8452->input_dev->dev.parent = &client->dev;

	err = input_register_device(mma8452->input_dev);
	if (err < 0) {
		mmaprintk(KERN_ERR
		       "mma8452_probe: Unable to register input device: %s\n",
		       mma8452->input_dev->name);
		goto exit_input_register_device_failed;
	}

    mma8452_device.parent = &client->dev;
	err = misc_register(&mma8452_device);
	if (err < 0) {
		mmaprintk(KERN_ERR
		       "mma8452_probe: mmad_device register failed\n");
		goto exit_misc_device_register_mma8452_device_failed;
	}

	err = gsensor_sysfs_init();
	if (err < 0) {
		mmaprintk(KERN_ERR
            "mma8452_probe: gsensor sysfs init failed\n");
		goto exit_gsensor_sysfs_init_failed;
	}
	
#ifdef CONFIG_HAS_EARLYSUSPEND
    mma8452_early_suspend.suspend = mma8452_suspend;
    mma8452_early_suspend.resume = mma8452_resume;
    mma8452_early_suspend.level = 0x2;
    register_early_suspend(&mma8452_early_suspend);
#endif

	printk(KERN_INFO "mma8452 probe ok\n");
    memset(&(mma8452->sense_data), 0, sizeof(struct mma8452_axis) );
    mutex_init(&(mma8452->sense_data_mutex) );
    
	atomic_set(&(mma8452->data_ready), 0);
    init_waitqueue_head(&(mma8452->data_ready_wq) );

    mma8452->start_count = 0;
    mutex_init(&(mma8452->operation_mutex) );
    
	// mma8452->status = -1;
	mma8452->status = MMA8452_CLOSE;
#if  0	
//	mma8452_start_test(this_client);
	mma8452_start(client, MMA8452_RATE_12P5);
#endif
	return 0;

exit_gsensor_sysfs_init_failed:
    misc_deregister(&mma8452_device);
exit_misc_device_register_mma8452_device_failed:
    input_unregister_device(mma8452->input_dev);
exit_input_register_device_failed:
	input_free_device(mma8452->input_dev);
exit_input_allocate_device_failed:
	free_irq(client->irq, mma8452);
exit_request_gpio_irq_failed:
	cancel_delayed_work_sync(&mma8452->delaywork);
	cancel_work_sync(&mma8452->work);
exit_invalid_devid:
	kfree(mma8452);	
exit_alloc_data_failed:
    ;
	mmaprintk("%s error\n",__FUNCTION__);
	return -1;
}


static int __init mma8452_i2c_init(void)
{
	return i2c_add_driver(&mma8452_driver);
}

static void __exit mma8452_i2c_exit(void)
{
	i2c_del_driver(&mma8452_driver);
}

module_init(mma8452_i2c_init);
module_exit(mma8452_i2c_exit);

