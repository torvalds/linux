/*
 *
 * Touch Screen I2C Driver for EETI Controller
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

// Release Date: 2010/11/08

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/freezer.h>
#include <linux/proc_fs.h>
#include <linux/clk.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <linux/poll.h>
#include <linux/kfifo.h>
#include <linux/version.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/irq.h>
#include <linux/async.h>
#include <mach/board.h>
#ifdef CONFIG_RK_CONFIG
#include <mach/config.h>
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#define TP_MODULE_NAME  "egalax_i2c"
#ifdef CONFIG_RK_CONFIG

enum {
#if defined(RK2926_TB_DEFAULT_CONFIG) || defined(RK2928_TB_DEFAULT_CONFIG)
        DEF_EN = 1,
#else
        DEF_EN = 0,
#endif
#if defined(RK2926_TB_DEFAULT_CONFIG)
        DEF_IRQ = 0x008001b0,
        DEF_RST = 0X000002b0,
#elif defined(RK2928_TB_DEFAULT_CONFIG)
        DEF_IRQ = 0x008003c7,
        DEF_RST = 0X000003c3,
#endif
        DEF_I2C = 2, 
        DEF_ADDR = 0x04,
        DEF_X_MAX = 1087,
        DEF_Y_MAX = 800,
};
static int en = DEF_EN;
module_param(en, int, 0644);

static int irq = DEF_IRQ;
module_param(irq, int, 0644);
static int rst =DEF_RST;
module_param(rst, int, 0644);

static int i2c = DEF_I2C;            // i2c channel
module_param(i2c, int, 0644);
static int addr = DEF_ADDR;           // i2c addr
module_param(addr, int, 0644);
static int x_max = DEF_X_MAX;
module_param(x_max, int, 0644);
static int y_max = DEF_Y_MAX;
module_param(y_max, int, 0644);

static int tp_hw_init(void)
{
        int ret = 0;

        ret = gpio_request(get_port_config(irq).gpio, "tp_irq");
        if(ret < 0){
                printk("%s: gpio_request(irq gpio) failed\n", __func__);
                return ret;
        }

        ret = port_output_init(rst, 0, "tp_rst");
        if(ret < 0){
                printk("%s: port(rst) output init faild\n", __func__);
                return ret;
        }
        port_output_on(rst);

         return 0;
}
#include "rk_tp.c"
#endif

//#define DEBUG
#ifdef CONFIG_EETI_EGALAX_DEBUG
	#define TS_DEBUG(fmt,args...)  printk( KERN_DEBUG "[egalax_i2c]: " fmt, ## args)
	#define DBG() printk("[%s]:%d => \n",__FUNCTION__,__LINE__)
#else
	#define TS_DEBUG(fmt,args...)
	#define DBG()
#endif


//#define _NON_INPUT_DEV // define this to disable register input device	

static int global_major = 0; // dynamic major by default 
static int global_minor = 0;
#define EETI_I2C_RATE   (200*1000)
#define MAX_I2C_LEN		10
#define FIFO_SIZE		PAGE_SIZE
#define MAX_SUPPORT_POINT	2
#define REPORTID_MOUSE		0x01
#define REPORTID_VENDOR		0x03
#define REPORTID_MTOUCH		0x04

/// ioctl command ///
#define EGALAX_IOC_MAGIC	0x72
#define	EGALAX_IOCWAKEUP	_IO(EGALAX_IOC_MAGIC, 1)
#define EGALAX_IOC_MAXNR	1

#define EETI_EARLYSUSPEND_LEVEL	(EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1)

struct point_data {
	short Status;
	short X;
	short Y;
};

struct _egalax_i2c {
	struct workqueue_struct *ktouch_wq;
	struct work_struct work;
	struct mutex mutex_wq;
	struct i2c_client *client;
	char work_state;
	char skip_packet;
	int irq;
};

#ifdef CONFIG_HAS_EARLYSUSPEND 
struct suspend_info {
	struct early_suspend early_suspend;
	struct _egalax_i2c *egalax_i2c;
};
#endif

struct egalax_char_dev
{
	int OpenCnts;
	struct cdev cdev;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,33)
	struct kfifo* pDataKFiFo;
#else
	struct kfifo DataKFiFo;
#endif
	unsigned char *pFiFoBuf;
	spinlock_t FiFoLock;
	struct semaphore sem;
	wait_queue_head_t fifo_inq;
};

static struct _egalax_i2c *p_egalax_i2c_dev = NULL;	// allocated in egalax_i2c_probe
static struct egalax_char_dev *p_char_dev = NULL;	// allocated in init_module
static atomic_t egalax_char_available = ATOMIC_INIT(1);
static struct class *egalax_class;
#ifndef _NON_INPUT_DEV
static struct input_dev *input_dev = NULL;
static struct point_data PointBuf[MAX_SUPPORT_POINT];
#endif //#ifndef _NON_INPUT_DEV

static int egalax_cdev_open(struct inode *inode, struct file *filp)
{
	struct egalax_char_dev *cdev;

	DBG();

	cdev = container_of(inode->i_cdev, struct egalax_char_dev, cdev);
	if( cdev == NULL )
	{
        	TS_DEBUG(" No such char device node \n");
		return -ENODEV;
	}
	
	if( !atomic_dec_and_test(&egalax_char_available) )
	{
		atomic_inc(&egalax_char_available);
		return -EBUSY; /* already open */
	}

	cdev->OpenCnts++;
	filp->private_data = cdev;// Used by the read and write metheds

	TS_DEBUG(" egalax_cdev_open done \n");
	try_module_get(THIS_MODULE);
	return 0;
}

static int egalax_cdev_release(struct inode *inode, struct file *filp)
{
	struct egalax_char_dev *cdev; // device information

	DBG();

	cdev = container_of(inode->i_cdev, struct egalax_char_dev, cdev);
        if( cdev == NULL )
        {
                TS_DEBUG(" No such char device node \n");
                return -ENODEV;
        }

	atomic_inc(&egalax_char_available); /* release the device */

	filp->private_data = NULL;
	cdev->OpenCnts--;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,33)
	kfifo_reset( cdev->pDataKFiFo );
#else
	kfifo_reset( &cdev->DataKFiFo );
#endif

	TS_DEBUG(" egalax_cdev_release done \n");
	module_put(THIS_MODULE);
	return 0;
}

#define MAX_READ_BUF_LEN	50
static char fifo_read_buf[MAX_READ_BUF_LEN];
static ssize_t egalax_cdev_read(struct file *file, char __user *buf, size_t count, loff_t *offset)
{
	int read_cnt, ret, fifoLen;
	struct egalax_char_dev *cdev = file->private_data;

	DBG();
	
	if( down_interruptible(&cdev->sem) )
		return -ERESTARTSYS;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,33)
	fifoLen = kfifo_len(cdev->pDataKFiFo);
#else
	fifoLen = kfifo_len(&cdev->DataKFiFo);
#endif

	while( fifoLen<1 ) /* nothing to read */
	{
		up(&cdev->sem); /* release the lock */
		if( file->f_flags & O_NONBLOCK )
			return -EAGAIN;

	#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,33)
		if( wait_event_interruptible(cdev->fifo_inq, kfifo_len( cdev->pDataKFiFo )>0) )
	#else
		if( wait_event_interruptible(cdev->fifo_inq, kfifo_len( &cdev->DataKFiFo )>0) )
	#endif
		{
			return -ERESTARTSYS; /* signal: tell the fs layer to handle it */
		}

		if( down_interruptible(&cdev->sem) )
			return -ERESTARTSYS;
	}

	if(count > MAX_READ_BUF_LEN)
		count = MAX_READ_BUF_LEN;

	TS_DEBUG("\"%s\" reading: real fifo data\n", current->comm);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,33)
	read_cnt = kfifo_get(cdev->pDataKFiFo, fifo_read_buf, count);
#else
	read_cnt = kfifo_out_locked(&cdev->DataKFiFo, fifo_read_buf, count, &cdev->FiFoLock);
#endif

	ret = copy_to_user(buf, fifo_read_buf, read_cnt)?-EFAULT:read_cnt;

	up(&cdev->sem);
	
	return ret;
}

static ssize_t egalax_cdev_write(struct file *file, const char __user *buf, size_t count, loff_t *offset)
{
	char *tmp;
	struct egalax_char_dev *cdev = file->private_data;
	int ret=0;

	DBG();

	if( down_interruptible(&cdev->sem) )
		return -ERESTARTSYS;

	if (count > MAX_I2C_LEN)
		count = MAX_I2C_LEN;

	tmp = kmalloc(count,GFP_KERNEL);
	if(tmp==NULL)
	{
		up(&cdev->sem);
		return -ENOMEM;
	}

	if(copy_from_user(tmp, buf, count))
	{
		up(&cdev->sem);
		kfree(tmp);
		return -EFAULT;
	}
	
	ret = i2c_master_normal_send(p_egalax_i2c_dev->client, tmp, count,EETI_I2C_RATE);
	TS_DEBUG("I2C writing %zu bytes.\n", count);

	kfree(tmp);

	up(&cdev->sem);

	return ret;
}

static int wakeup_controller(int gpio)
{
	int ret=0, i;

	gpio_free(gpio);

	if( (ret=gpio_request(gpio, "Touch Wakeup GPIO"))!=0 )
	{
		printk(KERN_ERR "[egalax_i2c]: Failed to request GPIO for Touch Wakeup GPIO. Err:%d\n", ret);
		ret = -EFAULT;
	}
	else
	{
		gpio_direction_output(gpio, 0);
		for(i=0; i<100; i++);
		gpio_direction_input(gpio);
		printk(KERN_ERR "[egalax_i2c]: INT wakeup touch controller done\n");
	}
	
	return ret;
}

static long egalax_cdev_ioctl(struct file *filp, unsigned int cmd, unsigned long args)
{	
	//struct egalax_char_dev *cdev = file->private_data;
	int ret=0;

	if(_IOC_TYPE(cmd) != EGALAX_IOC_MAGIC)
		return -ENOTTY;
	if(_IOC_NR(cmd) > EGALAX_IOC_MAXNR)
		return -ENOTTY;

	if(_IOC_DIR(cmd) & _IOC_READ)
		ret = !access_ok(VERIFY_WRITE, (void __user*)args, _IOC_SIZE(cmd));
	else if(_IOC_DIR(cmd) & _IOC_WRITE)
		ret = !access_ok(VERIFY_READ, (void __user*)args, _IOC_SIZE(cmd));

	if(ret)
		return -EFAULT;

	//printk(KERN_ERR "Handle device ioctl command\n");
	switch (cmd)
	{
		case EGALAX_IOCWAKEUP:
			ret = wakeup_controller(irq_to_gpio(p_egalax_i2c_dev->irq));
			break;
		default:
			ret = -ENOTTY;
			break;
	}

	return ret;
}

static unsigned int egalax_cdev_poll(struct file *filp, struct poll_table_struct *wait)
{
	struct egalax_char_dev *cdev = filp->private_data;
	unsigned int mask = 0;
	int fifoLen;
	
	down(&cdev->sem);
	poll_wait(filp, &cdev->fifo_inq,  wait);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,33)
	fifoLen = kfifo_len(cdev->pDataKFiFo);
#else
	fifoLen = kfifo_len(&cdev->DataKFiFo);
#endif

	if( fifoLen > 0 )
		mask |= POLLIN | POLLRDNORM;    /* readable */
	if( (FIFO_SIZE - fifoLen) > MAX_I2C_LEN )
		mask |= POLLOUT | POLLWRNORM;   /* writable */

	up(&cdev->sem);
	return mask;
}

#ifndef _NON_INPUT_DEV
static int LastUpdateID = 0;
static void ProcessReport(unsigned char *buf, int buflen)
{
	int i;
	short X=0, Y=0, ContactID=0, Status=0;
	if(buflen!=MAX_I2C_LEN || buf[0]!=0x04) // check buffer len & header
		return;

	Status = buf[1]&0x01;
	ContactID = (buf[1]&0x7C)>>2;
	X = ((buf[3]<<8) + buf[2])>>4;
	Y = ((buf[5]<<8) + buf[4])>>4;
	
	PointBuf[ContactID].Status = Status;
	PointBuf[ContactID].X = X;
	PointBuf[ContactID].Y = Y;

	TS_DEBUG("Get Point[%d] Update: Status=%d X=%d Y=%d\n", ContactID, Status, X, Y);

	// Send point report
	if( !Status || (ContactID <= LastUpdateID) )
	{
		for(i=0; i<MAX_SUPPORT_POINT;i++)
		{
			if(PointBuf[i].Status > 0)
			{
				input_mt_slot(input_dev, i);
				input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, true);
				input_report_abs(input_dev, ABS_MT_TOUCH_MAJOR, PointBuf[i].Status);
				input_report_abs(input_dev, ABS_MT_POSITION_X, PointBuf[i].X);
				input_report_abs(input_dev, ABS_MT_POSITION_Y, PointBuf[i].Y);
				PointBuf[i].Status = 0;
			}
			else if (PointBuf[i].Status == 0)
			{
				input_mt_slot(input_dev, i);
				input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, false);
				PointBuf[i].Status = -1;
			}
		}
		input_sync(input_dev);
		TS_DEBUG("Input sync point data done!\n");
	}

	LastUpdateID = ContactID;
}

static struct input_dev * allocate_Input_Dev(void)
{
	int ret;
	struct input_dev *pInputDev=NULL;

	pInputDev = input_allocate_device();
	if(pInputDev == NULL)
	{
		TS_DEBUG("Failed to allocate input device\n");
		return NULL;//-ENOMEM;
	}

	pInputDev->name = "eGalax Touch Screen";
	pInputDev->phys = "I2C";
	pInputDev->id.bustype = BUS_I2C;
	pInputDev->id.vendor = 0x0EEF;
	pInputDev->id.product = 0x0020;

	__set_bit(INPUT_PROP_DIRECT, pInputDev->propbit);
	__set_bit(EV_ABS, pInputDev->evbit);

	input_mt_init_slots(pInputDev, MAX_SUPPORT_POINT);
#ifdef CONFIG_RK_CONFIG
	input_set_abs_params(pInputDev, ABS_MT_POSITION_X, 0, x_max, 0, 0);
	input_set_abs_params(pInputDev, ABS_MT_POSITION_Y, 0, y_max, 0, 0);
#else
	input_set_abs_params(pInputDev, ABS_MT_POSITION_X, 0, CONFIG_EETI_EGALAX_MAX_X, 0, 0);
	input_set_abs_params(pInputDev, ABS_MT_POSITION_Y, 0, CONFIG_EETI_EGALAX_MAX_Y, 0, 0);
#endif
	input_set_abs_params(pInputDev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);

	ret = input_register_device(pInputDev);
	if(ret) 
	{
		TS_DEBUG("Unable to register input device.\n");
		input_free_device(pInputDev);
		return NULL;
	}
	
	return pInputDev;
}
#endif //#ifndef _NON_INPUT_DEV

static int egalax_i2c_measure(struct i2c_client *client, char skip_packet)
{
	u8 x_buf[MAX_I2C_LEN];
	int count, loop=3;
	DBG();
	do{
		count = i2c_master_normal_recv(client, x_buf, MAX_I2C_LEN,EETI_I2C_RATE);
	}while(count==EAGAIN && --loop);

	if( count<0 || (x_buf[0]!=REPORTID_VENDOR && x_buf[0]!=REPORTID_MTOUCH) )
	{
		TS_DEBUG("I2C read error data with Len=%d hedaer=%d\n", count, x_buf[0]);
		return -1;
	}

	TS_DEBUG("egalax_i2c read data with Len=%d\n", count);
	if(x_buf[0]==REPORTID_VENDOR)
		TS_DEBUG("egalax_i2c get command packet\n");

	if( skip_packet > 0 )
		return count;

#ifndef _NON_INPUT_DEV
	if( count>0 && x_buf[0]==REPORTID_MTOUCH )
	{
		ProcessReport(x_buf, count);

		return count;
	}
#endif //#ifndef _NON_INPUT_DEV

	if( count>0 && p_char_dev->OpenCnts>0 ) // If someone reading now! put the data into the buffer!
	{
	#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,33)
		kfifo_put(p_char_dev->pDataKFiFo, x_buf, count);
	#else
		kfifo_in_locked(&p_char_dev->DataKFiFo, x_buf, count, &p_char_dev->FiFoLock);
	#endif
	 	wake_up_interruptible( &p_char_dev->fifo_inq );
	}

	return count;
}

static void egalax_i2c_wq(struct work_struct *work)
{
	struct _egalax_i2c *egalax_i2c = container_of(work, struct _egalax_i2c, work);
	struct i2c_client *client = egalax_i2c->client;
	int gpio = client->irq;
	TS_DEBUG("egalax_i2c_wq run\n");
	mutex_lock(&egalax_i2c->mutex_wq);

	/*continue recv data*/
	while( !gpio_get_value(gpio) && egalax_i2c->work_state>0 )
	{
		egalax_i2c_measure(client, egalax_i2c->skip_packet);
		schedule_timeout_interruptible(HZ/100);
	}

#ifndef _NON_INPUT_DEV
	if (gpio_get_value(gpio) && egalax_i2c->work_state > 0 && !egalax_i2c->skip_packet) {
		int i;
		for(i = 0; i < MAX_SUPPORT_POINT; i++) {
			if (PointBuf[i].Status > 0) {
				TS_DEBUG("point %d still down\n", i);
				input_mt_slot(input_dev, i);
				input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, true);
				input_report_abs(input_dev, ABS_MT_TOUCH_MAJOR, 0);
				input_report_abs(input_dev, ABS_MT_POSITION_X, PointBuf[i].X);
				input_report_abs(input_dev, ABS_MT_POSITION_Y, PointBuf[i].Y);
				PointBuf[i].Status = 0;
			}
			else if (PointBuf[i].Status == 0)
			{
				input_mt_slot(input_dev, i);
				input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, false);
				PointBuf[i].Status = -1;
			}
		}
		input_sync(input_dev);
	}
#endif

	if( egalax_i2c->skip_packet > 0 )
		egalax_i2c->skip_packet = 0;

	mutex_unlock(&egalax_i2c->mutex_wq);

	enable_irq(p_egalax_i2c_dev->irq);

	TS_DEBUG("egalax_i2c_wq leave\n");
}

static irqreturn_t egalax_i2c_interrupt(int irq, void *dev_id)
{
	struct _egalax_i2c *egalax_i2c = (struct _egalax_i2c *)dev_id;
	TS_DEBUG("egalax_i2c_interrupt with irq:%d\n", irq);
	disable_irq_nosync(irq);
	queue_work(egalax_i2c->ktouch_wq, &egalax_i2c->work);

	return IRQ_HANDLED;
}


void egalax_i2c_set_standby(struct i2c_client *client, int enable)
{
#ifndef CONFIG_RK_CONFIG
        struct eeti_egalax_platform_data *mach_info = client->dev.platform_data;
	unsigned display_on = mach_info->disp_on_pin;
	unsigned lcd_standby = mach_info->standby_pin;

	int display_on_pol = mach_info->disp_on_value;
	int lcd_standby_pol = mach_info->standby_value;
        printk("%s : %s, enable = %d", __FILE__, __FUNCTION__,enable);
    if(display_on != INVALID_GPIO)
    {
        gpio_direction_output(display_on, 0);
        gpio_set_value(display_on, enable ? display_on_pol : !display_on_pol);				
    }
    if(lcd_standby != INVALID_GPIO)
    {
        gpio_direction_output(lcd_standby, 0);
	gpio_set_value(lcd_standby, enable ? lcd_standby_pol : !lcd_standby_pol);			  
    }
#endif
}

#ifdef CONFIG_PM
static int egalax_i2c_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct _egalax_i2c *egalax_i2c = i2c_get_clientdata(client);
	u8 cmdbuf[MAX_I2C_LEN]={0x03, 0x05, 0x0A, 0x03, 0x36, 0x3F, 0x02, 0, 0, 0};
	
	i2c_master_normal_send(client, cmdbuf, MAX_I2C_LEN, EETI_I2C_RATE);

	disable_irq(p_egalax_i2c_dev->irq);
	egalax_i2c->work_state = 0;
	if (cancel_work_sync(&egalax_i2c->work)) {
		/* if work was pending disable-count is now 2 */
		pr_info("%s: work was pending\n", __func__);
		enable_irq(p_egalax_i2c_dev->irq);
	}

	printk(KERN_DEBUG "[egalax_i2c]: device suspend done\n");	

	if(device_may_wakeup(&client->dev)) 
	{
		enable_irq_wake(p_egalax_i2c_dev->irq);
	}
	else 
	{
		printk(KERN_DEBUG "[egalax_i2c]: device_may_wakeup false\n");
	}
	egalax_i2c_set_standby(client, 0);
	return 0;
}

static int egalax_i2c_resume(struct i2c_client *client)
{
	struct _egalax_i2c *egalax_i2c = i2c_get_clientdata(client);
	egalax_i2c_set_standby(client, 1);	
	if(device_may_wakeup(&client->dev)) 
	{
		disable_irq_wake(p_egalax_i2c_dev->irq);
	}
	else 
	{
		printk(KERN_DEBUG "[egalax_i2c]: device_may_wakeup false\n");
	}

	wakeup_controller(irq_to_gpio(p_egalax_i2c_dev->irq));
	egalax_i2c->work_state = 1;
	enable_irq(p_egalax_i2c_dev->irq);

	printk(KERN_DEBUG "[egalax_i2c]: device wakeup done\n");

	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND 

static void egalax_i2c_early_suspend(struct early_suspend *h)
{
	pm_message_t mesg = {.event = 0};
	struct suspend_info *info = container_of(h,struct suspend_info,early_suspend);
	struct i2c_client *client = info->egalax_i2c->client;
	egalax_i2c_suspend(client,mesg);

}
static void egalax_i2c_early_resume(struct early_suspend *h)
{
   
    struct suspend_info *info = container_of(h,struct suspend_info,early_suspend);
    struct i2c_client *client = info->egalax_i2c->client;

    egalax_i2c_resume(client);

}

#endif



#else
#define egalax_i2c_suspend       NULL
#define egalax_i2c_resume        NULL
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND 
static struct suspend_info suspend_info = {
	.early_suspend.suspend = egalax_i2c_early_suspend,
	.early_suspend.resume = egalax_i2c_early_resume,
	.early_suspend.level = EETI_EARLYSUSPEND_LEVEL,
};
#endif


static int __devinit egalax_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret;
	int gpio;
#ifdef CONFIG_RK_CONFIG
        struct port_config irq_cfg = get_port_config(irq);

        client->irq = irq_cfg.gpio;
        tp_hw_init();
#else
	struct eeti_egalax_platform_data *pdata = pdata = client->dev.platform_data;
        if (pdata->init_platform_hw)
		pdata->init_platform_hw();
#endif
	printk(KERN_DEBUG "[egalax_i2c]: start probe\n");
        gpio = client->irq;

	p_egalax_i2c_dev = (struct _egalax_i2c *)kzalloc(sizeof(struct _egalax_i2c), GFP_KERNEL);
	if (!p_egalax_i2c_dev) 
	{
		printk(KERN_ERR "[egalax_i2c]: request memory failed\n");
		ret = -ENOMEM;
		goto fail1;
	}

#ifndef _NON_INPUT_DEV
	input_dev = allocate_Input_Dev();
	if(input_dev==NULL)
	{
		printk(KERN_ERR "[egalax_i2c]: allocate_Input_Dev failed\n");
		ret = -EINVAL; 
		goto fail2;
	}
	TS_DEBUG("egalax_i2c register input device done\n");
	memset(PointBuf, 0, sizeof(struct point_data)*MAX_SUPPORT_POINT);
#endif //#ifndef _NON_INPUT_DEV


	p_egalax_i2c_dev->client = client;
	mutex_init(&p_egalax_i2c_dev->mutex_wq);

	p_egalax_i2c_dev->ktouch_wq = create_workqueue("egalax_touch_wq"); 
	INIT_WORK(&p_egalax_i2c_dev->work, egalax_i2c_wq);
	
	i2c_set_clientdata(client, p_egalax_i2c_dev);

	if( gpio_get_value(gpio) )
		p_egalax_i2c_dev->skip_packet = 0;
	else
		p_egalax_i2c_dev->skip_packet = 1;

	p_egalax_i2c_dev->work_state = 1;
	
	p_egalax_i2c_dev->irq = gpio_to_irq(client->irq);
#ifdef CONFIG_RK_CONFIG
	ret = request_irq(p_egalax_i2c_dev->irq, egalax_i2c_interrupt, irq_cfg.irq.irq_flags,
		 client->name, p_egalax_i2c_dev);
#else
	ret = request_irq(p_egalax_i2c_dev->irq, egalax_i2c_interrupt, IRQF_TRIGGER_LOW,
		 client->name, p_egalax_i2c_dev);
#endif
	if( ret ) 
	{
		printk(KERN_ERR "[egalax_i2c]: request irq(%d) failed\n", p_egalax_i2c_dev->irq);
		goto fail3;
	}
	TS_DEBUG("egalax_i2c request irq(%d) gpio(%d) with result:%d\n", p_egalax_i2c_dev->irq, gpio, ret);

#ifdef CONFIG_PM
	device_init_wakeup(&client->dev, 0);
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND


 	suspend_info.egalax_i2c = p_egalax_i2c_dev;
	register_early_suspend(&suspend_info.early_suspend);
#endif

	printk(KERN_DEBUG "[egalax_i2c]: probe done\n");
	return 0;

fail3:
	i2c_set_clientdata(client, NULL);
	destroy_workqueue(p_egalax_i2c_dev->ktouch_wq); 
	free_irq(p_egalax_i2c_dev->irq, p_egalax_i2c_dev);
#ifndef _NON_INPUT_DEV
	input_unregister_device(input_dev);
	input_free_device(input_dev);
	input_dev = NULL;
#endif //#ifndef _NON_INPUT_DEV
fail2:
fail1:
	kfree(p_egalax_i2c_dev);
	p_egalax_i2c_dev = NULL;

	printk(KERN_DEBUG "[egalax_i2c]: probe failed\n");
	return ret;
}

static int __devexit egalax_i2c_remove(struct i2c_client *client)
{
	struct _egalax_i2c *egalax_i2c = i2c_get_clientdata(client);

	DBG();

#ifndef _NON_INPUT_DEV
	if(input_dev)
	{
		TS_DEBUG("unregister input device\n");
		input_unregister_device(input_dev);
		input_free_device(input_dev);
		input_dev = NULL;
	}
#endif //#ifndef _NON_INPUT_DEV

	if(p_egalax_i2c_dev->ktouch_wq) 
	{
		destroy_workqueue(p_egalax_i2c_dev->ktouch_wq); 
	}

	if(p_egalax_i2c_dev->irq)
	{
		free_irq(p_egalax_i2c_dev->irq, egalax_i2c);
	}

	i2c_set_clientdata(client, NULL);
	kfree(egalax_i2c);
	p_egalax_i2c_dev = NULL;

	return 0;
}

static struct i2c_device_id egalax_i2c_idtable[] = { 
	{ TP_MODULE_NAME, 0 }, 
	{ } 
}; 

MODULE_DEVICE_TABLE(i2c, egalax_i2c_idtable);

static struct i2c_driver egalax_i2c_driver = {
	.driver = {
		.name 	= TP_MODULE_NAME,
	},
	.id_table	= egalax_i2c_idtable,
	.probe		= egalax_i2c_probe,
	.remove		= __devexit_p(egalax_i2c_remove),
	//.suspend	= egalax_i2c_suspend,
	//.resume		= egalax_i2c_resume,
};

static const struct file_operations egalax_cdev_fops = {
	.owner	= THIS_MODULE,
	.read	= egalax_cdev_read,
	.write	= egalax_cdev_write,
	.unlocked_ioctl	= egalax_cdev_ioctl,
	.poll	= egalax_cdev_poll,
	.open	= egalax_cdev_open,
	.release= egalax_cdev_release,
};

static void egalax_i2c_ts_exit(void)
{
	dev_t devno = MKDEV(global_major, global_minor);
	DBG();

	if(p_char_dev)
	{
		TS_DEBUG("unregister character device\n");
		if( p_char_dev->pFiFoBuf )
			kfree(p_char_dev->pFiFoBuf);
	
		cdev_del(&p_char_dev->cdev);
		kfree(p_char_dev);
		p_char_dev = NULL;
	}

	unregister_chrdev_region( devno, 1);

	if(!IS_ERR(egalax_class))
	{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26)
		class_device_destroy(egalax_class, devno);
#else
		device_destroy(egalax_class, devno);
#endif 
		class_destroy(egalax_class);
	}
	
	i2c_del_driver(&egalax_i2c_driver);

	printk(KERN_DEBUG "[egalax_i2c]: driver exit\n");
}

static struct egalax_char_dev* setup_chardev(dev_t dev)
{
	struct egalax_char_dev *pCharDev;
	int result;

	pCharDev = kmalloc(1*sizeof(struct egalax_char_dev), GFP_KERNEL);
	if(!pCharDev) 
		goto fail_cdev;
	memset(pCharDev, 0, sizeof(struct egalax_char_dev));

	spin_lock_init( &pCharDev->FiFoLock );
	pCharDev->pFiFoBuf = kmalloc(sizeof(unsigned char)*FIFO_SIZE, GFP_KERNEL);
	if(!pCharDev->pFiFoBuf)
		goto fail_fifobuf;
	memset(pCharDev->pFiFoBuf, 0, sizeof(unsigned char)*FIFO_SIZE);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,33)
	pCharDev->pDataKFiFo = kfifo_init(pCharDev->pFiFoBuf, FIFO_SIZE, GFP_KERNEL, &pCharDev->FiFoLock);
	if( pCharDev->pDataKFiFo==NULL )
		goto fail_kfifo;
#else
	kfifo_init(&pCharDev->DataKFiFo, pCharDev->pFiFoBuf, FIFO_SIZE);
	if( !kfifo_initialized(&pCharDev->DataKFiFo) )
		goto fail_kfifo;
#endif
	
	pCharDev->OpenCnts = 0;
	cdev_init(&pCharDev->cdev, &egalax_cdev_fops);
	pCharDev->cdev.owner = THIS_MODULE;
	sema_init(&pCharDev->sem, 1);
	init_waitqueue_head(&pCharDev->fifo_inq);

	result = cdev_add(&pCharDev->cdev, dev, 1);
	if(result)
	{
		TS_DEBUG(KERN_ERR "Error cdev ioctldev added\n");
		goto fail_kfifo;
	}

	return pCharDev; 

fail_kfifo:
	kfree(pCharDev->pFiFoBuf);
fail_fifobuf:
	kfree(pCharDev);
fail_cdev:
	return NULL;
}

static void __init egalax_i2c_ts_init_async(void *unused, async_cookie_t cookie)
{
	int result;
	dev_t devno = 0;

	DBG();

	// Asking for a dynamic major unless directed otherwise at load time.
	if(global_major) 
	{
		devno = MKDEV(global_major, global_minor);
		result = register_chrdev_region(devno, 1, "egalax_i2c");
	} 
	else 
	{
		result = alloc_chrdev_region(&devno, global_minor, 1, "egalax_i2c");
		global_major = MAJOR(devno);
	}

	if (result < 0)
	{
		TS_DEBUG(" egalax_i2c cdev can't get major number\n");
		return;
	}

	// allocate the character device
	p_char_dev = setup_chardev(devno);
	if(!p_char_dev) 
	{
		result = -ENOMEM;
		goto fail;
	}

	egalax_class = class_create(THIS_MODULE, "egalax_i2c");
	if(IS_ERR(egalax_class))
	{
		TS_DEBUG("Err: failed in creating class.\n");
		result = -EFAULT;
		goto fail;
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26)
	class_device_create(egalax_class, NULL, devno, NULL, "egalax_i2c");
#else
	device_create(egalax_class, NULL, devno, NULL, "egalax_i2c");
#endif
	TS_DEBUG("register egalax_i2c cdev, major: %d \n",global_major);

	printk(KERN_DEBUG "[egalax_i2c]: init done\n");
	i2c_add_driver(&egalax_i2c_driver);
	return;

fail:	
	egalax_i2c_ts_exit();
}

static int __init egalax_i2c_ts_init(void)
{
#ifdef CONFIG_RK_CONFIG
        int ret = tp_board_init();

        if(ret < 0)
                return ret;
#endif
	async_schedule(egalax_i2c_ts_init_async, NULL);
	return 0;
}
module_init(egalax_i2c_ts_init);
module_exit(egalax_i2c_ts_exit);

MODULE_DESCRIPTION("egalax touch screen i2c driver");
MODULE_LICENSE("GPL");

