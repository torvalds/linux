
 /****************************************************************************************
 * File:			driver/input/gsensor/lis3dh.c
 * Copyright:		Copyright (C) 2012-2013 RK Corporation.
 * Author:		LiBing <libing@rock-chips.com>
 * Date:			2012.03.06
 * Description:	This driver use for rk29 chip extern gsensor. Use i2c IF ,the chip is 
 * 				STMicroelectronics lis3dh.
 *****************************************************************************************/
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
#include <mach/gpio.h>
#include <mach/board.h> 
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#include "lis3dh.h"

#if 0
#define stprintk(x...) printk(x)
#else
#define stprintk(x...)
#endif

#if 0
#define stprintkd(x...) printk(x)
#else
#define stprintkd(x...)
#endif

#if 0
#define stprintkf(x...) printk(x)
#else
#define stprintkf(x...)
#endif


static struct i2c_client *this_client;
static struct miscdevice lis3dh_device;
static struct kobject *android_gsensor_kobj;
static const char* vendor = "STMicroelectronics";
static int suspend_flag;
static int  lis3dh_probe(struct i2c_client *client, const struct i2c_device_id *id);
static DECLARE_WAIT_QUEUE_HEAD(data_ready_wq);

/* AKM HW info */
static ssize_t gsensor_vendor_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t liRet = 0;

	sprintf(buf, "%s.\n", vendor);
	liRet = strlen(buf) + 1;

	return liRet;
}

static DEVICE_ATTR(vendor, 0444, gsensor_vendor_show, NULL);

static int gsensor_sysfs_init(void)
{
	int liRet ;

	android_gsensor_kobj = kobject_create_and_add("android_gsensor", NULL);
	if (android_gsensor_kobj == NULL)
	{
		stprintk(KERN_ERR "LIS3DH gsensor_sysfs_init:subsystem_register failed\n");
		liRet = -ENOMEM;
		goto kobject_create_failed;
	}
	else
	{
		//nothing
	}

	liRet = sysfs_create_file(android_gsensor_kobj, &dev_attr_vendor.attr); // "vendor"
	if (liRet) {
		stprintk(KERN_ERR "LIS3DH gsensor_sysfs_init:sysfs_create_group failed\n");
		goto sysfs_create_failed;
	}
	else
	{
		//nothing
	}

	return 0 ;
	
sysfs_create_failed:
	kobject_del(android_gsensor_kobj);
	
kobject_create_failed:
	return liRet ;
	
}

static int lis3dh_rx_data(struct i2c_client *client, char *rxData, int length)
{
	int liRet = 0;
	char lcReg = rxData[0];
	liRet = i2c_master_reg8_recv(client, lcReg, rxData, length, LIS3DH_SPEED);
	
	return (liRet > 0)? 0 : liRet;
}

static int lis3dh_tx_data(struct i2c_client *client, char *txData, int length)
{
	int liRet	= 0;
	char lcReg	= txData[0];
	liRet = i2c_master_reg8_send(client, lcReg, &txData[1], length-1, LIS3DH_SPEED);
	
	return (liRet > 0)? 0 : liRet;
}

static char lis3dh_read_reg(struct i2c_client *client, int addr)
{
	char liTmp;
	int	liRet = 0;

	liTmp = addr;
	liRet = lis3dh_rx_data(client, &liTmp, 1);
	return liTmp;
}

static int lis3dh_write_reg(struct i2c_client *client,int addr,int value)
{
	char buffer[3];
	int liRet = 0;
	buffer[0] = addr;
	buffer[1] = value;
	
	liRet = lis3dh_tx_data(client, &buffer[0], 2);
	return liRet;
}

static int lis3dh_init_device(struct lis3dh_data *lis3dh)
{
	int liRet =-1;

	memset(lis3dh->resume_state, 0, ARRAY_SIZE(lis3dh->resume_state));
	lis3dh->resume_state[RES_CTRL_REG1]		= LIS3DH_ACC_ENABLE_ALL_AXES;
	lis3dh->resume_state[RES_CTRL_REG2]		= 0x00;
	lis3dh->resume_state[RES_CTRL_REG3]		= 0x40; 
	lis3dh->resume_state[RES_CTRL_REG4]		= 0x08;
	lis3dh->resume_state[RES_CTRL_REG5]		= 0x08;
	lis3dh->resume_state[RES_CTRL_REG6]		= 0x40; 
	lis3dh->resume_state[RES_TEMP_CFG_REG]	= 0x00;
	lis3dh->resume_state[RES_FIFO_CTRL_REG] = 0x00;
	lis3dh->resume_state[RES_INT_CFG1]		= 0xFF;
	lis3dh->resume_state[RES_INT_THS1]		= 0x7F; 
	lis3dh->resume_state[RES_INT_DUR1]		= 0x7F; //0x00->ox7f
	lis3dh->resume_state[RES_TT_CFG]		= 0x00;
	lis3dh->resume_state[RES_TT_THS] 		= 0x00;
	lis3dh->resume_state[RES_TT_LIM] 		= 0x00;
	lis3dh->resume_state[RES_TT_TLAT]		= 0x00;
	lis3dh->resume_state[RES_TT_TW]			= 0x00;
	
	liRet =lis3dh_write_reg(lis3dh->client, CTRL_REG1, lis3dh->resume_state[RES_CTRL_REG1]);
	if (liRet < 0)
	{
		printk("RES_CTRL_REG1 err\n");
		return 0;
	}
	else
	{
		//nothing
	}

	liRet =lis3dh_write_reg(lis3dh->client, TEMP_CFG_REG, lis3dh->resume_state[RES_TEMP_CFG_REG]);
	if (liRet < 0)
	{
		printk("TEMP_CFG_REG err\n");
		return 0;
	}
	else
	{
		//nothing
	}

	liRet =lis3dh_write_reg(lis3dh->client, FIFO_CTRL_REG, lis3dh->resume_state[RES_FIFO_CTRL_REG]);
	if(liRet < 0)
	{
		printk("FIFO_CTRL_REG err\n");
		return 0;
	}
	else
	{
		//nothing
	}
	
	liRet =lis3dh_write_reg(lis3dh->client, TT_THS, lis3dh->resume_state[RES_TT_THS]);
	liRet =lis3dh_write_reg(lis3dh->client, TT_LIM, lis3dh->resume_state[RES_TT_LIM]);
	liRet =lis3dh_write_reg(lis3dh->client, TT_TLAT, lis3dh->resume_state[RES_TT_TLAT]);
	liRet =lis3dh_write_reg(lis3dh->client, TT_TW, lis3dh->resume_state[RES_TT_TW]);
	if(liRet < 0)
	{
		printk("I2C_AUTO_INCREMENT err\n");
		return 0;
	}
	else
	{
		//nothing
	}

	liRet =lis3dh_write_reg(lis3dh->client, TT_CFG, lis3dh->resume_state[RES_TT_CFG]);
	if(liRet < 0)
	{
		printk("TT_CFG err\n");
		return 0;
	}
	else
	{
		//nothing
	}
	
	liRet =lis3dh_write_reg(lis3dh->client, INT_THS1, lis3dh->resume_state[RES_INT_THS1]);
	liRet =lis3dh_write_reg(lis3dh->client, INT_DUR1, lis3dh->resume_state[RES_INT_DUR1]);
	if(liRet < 0)
	{
		printk("I2C_AUTO_INCREMENT err\n");
		return 0;
	}
	else
	{
		//nothing
	}

	liRet =lis3dh_write_reg(lis3dh->client, INT_CFG1, lis3dh->resume_state[RES_INT_CFG1]);
	if(liRet < 0)
	{
		printk("INT_CFG1 err\n");
		return 0;
	}
	else
	{
		//nothing
	}
	
	liRet =lis3dh_write_reg(lis3dh->client, CTRL_REG2, lis3dh->resume_state[RES_CTRL_REG2]);
	liRet =lis3dh_write_reg(lis3dh->client, CTRL_REG3, lis3dh->resume_state[RES_CTRL_REG3]);
	liRet =lis3dh_write_reg(lis3dh->client, CTRL_REG4, lis3dh->resume_state[RES_CTRL_REG4]);
	liRet =lis3dh_write_reg(lis3dh->client, CTRL_REG5, lis3dh->resume_state[RES_CTRL_REG5]);
	liRet =lis3dh_write_reg(lis3dh->client, CTRL_REG6, lis3dh->resume_state[RES_CTRL_REG6]);
	if(liRet < 0)
	{
		printk("I2C_AUTO_INCREMENT err\n");
		return 0;
	}
	else
	{
		//nothing
	}

	return liRet;
}

static char lis3dh_get_devid(struct lis3dh_data *lis3dh)
{
	char lcDeviceID;

	lcDeviceID	= lis3dh_read_reg(lis3dh->client,WHO_AM_I);
	if(lcDeviceID < 0)
	{
		printk("devid err\n");
		return 0;
	}
	else
	{
		printk("lis3dh devid:%x\n",lcDeviceID);
	}
	
	return lcDeviceID;
}

static int lis3dh_active(struct i2c_client *client,int enable)
{
	int liTmp = 0;
	int liRet = 0;

	liTmp =lis3dh_read_reg(client, CTRL_REG1);
	if(enable)
	{
		liTmp |= LIS3DH_ACC_ENABLE_ALL_AXES;
	}
	else
	{
		liTmp = 0x08;
	}
	
	liRet =lis3dh_write_reg(client, CTRL_REG1, liTmp);
	
	return liRet;
}

static int lis3dh_start_dev(struct i2c_client *client, char rate)
{
	int liRet	= 0;
	int liRate	= 0;
	char lcTmp	= 0x0;
	struct lis3dh_data *lis3dh = (struct lis3dh_data *)i2c_get_clientdata(client);
	
	if((int)rate == 5)
	{
		liRate = 3;
	}
	else if((int)rate == 6)
	{
		liRate = 2;
	}
	else
	{
		liRate = 4;
	}

	lcTmp = liRate<<4 | LIS3DH_ACC_ENABLE_ALL_AXES;

	liRet =lis3dh_write_reg(lis3dh->client, CTRL_REG1, lcTmp);
	if (liRet < 0)
	{
		printk(KERN_ERR "lis3dh_start_dev err\n");
	}
	else
	{
		stprintkf("lis3dh_start_dev\n");
	}
	lis3dh_active(client,1);
	enable_irq(client->irq);
	return liRet;
}

static int lis3dh_start(struct i2c_client *client, char rate)
{ 
    struct lis3dh_data *lis3dh = (struct lis3dh_data *)i2c_get_clientdata(client);
    
	stprintkf("%s::enter\n",__FUNCTION__); 
    if (lis3dh->status == LIS3DH_OPEN)
	{
        return 0;      
    }
	else
	{
		//nothing
	}
    lis3dh->status = LIS3DH_OPEN;
	
    return lis3dh_start_dev(client, rate);
}

static int lis3dh_close_dev(struct i2c_client *client)
{
	disable_irq_nosync(client->irq);
	return lis3dh_active(client,0);
}

static int lis3dh_close(struct i2c_client *client)
{
    struct lis3dh_data *lis3dh = (struct lis3dh_data *)i2c_get_clientdata(client);
	  stprintkf("%s::enter\n",__FUNCTION__); 
    lis3dh->status = LIS3DH_CLOSE;  
    return lis3dh_close_dev(client);
}

static int lis3dh_reset_rate(struct i2c_client *client, char rate)
{
	int liRet = 0;
	
    liRet = lis3dh_close_dev(client);
    liRet = lis3dh_start_dev(client, rate);
  
	return liRet ;
}

static void lis3dh_report_value(struct i2c_client *client, struct lis3dh_axis *axis)
{
	struct lis3dh_data *lis3dh = i2c_get_clientdata(client);

	/* Report acceleration sensor information */
    input_report_abs(lis3dh->input_dev, ABS_X, axis->x);
    input_report_abs(lis3dh->input_dev, ABS_Y, axis->y);
    input_report_abs(lis3dh->input_dev, ABS_Z, axis->z);
    input_sync(lis3dh->input_dev);
    stprintkd("Gsensor x==%d  y==%d z==%d\n",axis->x,axis->y,axis->z);
}

static inline s64 lis3dh_convert_to_int(const char High_Value, const char Low_Value)
{
	s64 liResult;
	
 	liResult = ((long) High_Value << 8) | Low_Value;
	
    if (liResult < LIS3DH_BOUNDARY)
	{
       liResult = liResult * LIS3DH_GRAVITY_STEP;
    } 
	else
	{
       liResult = ~(((~liResult & 0x7fff) + 1)* LIS3DH_GRAVITY_STEP) + 1;
    }

    return liResult;
}

/**get the gsensor data. */
static int lis3dh_get_data(struct i2c_client *client)
{
	int liResult;
	int x,y,z;
	char acc_data[6];

	struct lis3dh_axis axis;
    struct lis3dh_data* lis3dh = i2c_get_clientdata(client);
    struct gsensor_platform_data *pdata = pdata = client->dev.platform_data;
	
	/* x,y,z hardware data */
	do {
		
        memset(acc_data, 0, 6);
        acc_data[0] = (I2C_AUTO_INCREMENT | AXISDATA_REG);
		
		liResult = lis3dh_rx_data(client, &acc_data[0], 6);
		if (liResult < 0)
		{
            return liResult;
        }
		else
		{
			//nothing
		}
    } while (0);
	
	stprintkd("0x%02x 0x%02x 0x%02x \n",acc_data[1],acc_data[3],acc_data[5]);
	
	z = -lis3dh_convert_to_int(acc_data[1],acc_data[0]);
	x = -lis3dh_convert_to_int(acc_data[3],acc_data[2]);
	y = lis3dh_convert_to_int(acc_data[5],acc_data[4]);

	axis.x = x;
	axis.y = z;
	axis.z = y;

	if (pdata->swap_xyz)
	{
		axis.x = (pdata->orientation[0])*x + (pdata->orientation[1])*y + (pdata->orientation[2])*z;
		axis.y = (pdata->orientation[3])*x + (pdata->orientation[4])*y + (pdata->orientation[5])*z;
		axis.z = (pdata->orientation[6])*x + (pdata->orientation[7])*y + (pdata->orientation[8])*z;
	}
	else 
	{
		axis.x = x;
		axis.y = y;
		axis.z = z;
	}

	if(pdata->swap_xy)
	{
		axis.x = -axis.x;
		swap(axis.x,axis.y);		
	}
	
	stprintkd( "%s: GetData axis = %d  %d  %d-------\n",__func__, axis.x, axis.y, axis.z); 

    lis3dh_report_value(client, &axis);

    /* Caching data mutually exclusive.*/
    mutex_lock(&(lis3dh->sense_data_mutex) );
    lis3dh->sense_data = axis;
    mutex_unlock(&(lis3dh->sense_data_mutex) );

	/* set data_ready */
    atomic_set(&(lis3dh->data_ready), 1);
	/* wakeup the data_ready,the first of wait queue */
	wake_up(&(lis3dh->data_ready_wq) );

	return 0;
}

static int lis3dh_get_cached_data(struct i2c_client* client, struct lis3dh_axis* sense_data)
{
    struct lis3dh_data* lis3dh = (struct lis3dh_data *)i2c_get_clientdata(client);

    wait_event_interruptible_timeout(lis3dh->data_ready_wq, 
                                     atomic_read(&(lis3dh->data_ready) ),msecs_to_jiffies(1000) );
	
    if ( 0 == atomic_read(&(lis3dh->data_ready) ) ) 
	{
        printk("waiting 'data_ready_wq' timed out.");
        return -1;
    }
	else
	{
		//nothing
	}
	
    mutex_lock(&(lis3dh->sense_data_mutex));
    *sense_data = lis3dh->sense_data;
    mutex_unlock(&(lis3dh->sense_data_mutex));
	
    return 0;
}

static int lis3dh_open(struct inode *inode, struct file *file)
{
	return 0;//nonseekable_open(inode, file);
}

static int lis3dh_release(struct inode *inode, struct file *file)
{
	return 0;
}

static long lis3dh_ioctl(struct file *file, unsigned int cmd,unsigned long arg)
{
	int liRet = -1;
	char rate;
	void __user *argp = (void __user *)arg;
	
    struct lis3dh_axis sense_data = {0};
	struct i2c_client *client = container_of(lis3dh_device.parent, struct i2c_client, dev);
    struct lis3dh_data* lis3dh = (struct lis3dh_data *)i2c_get_clientdata(client);  

	switch (cmd)
	{
		case ST_IOCTL_APP_SET_RATE:
			
			if (copy_from_user(&rate, argp, sizeof(rate)))
			{
				return -EFAULT;
			}
			else
			{
				//nothing
			}
			
			break;
			
		default:
			break;
	}

	switch (cmd)
	{
		case ST_IOCTL_START:
			
        	mutex_lock(&(lis3dh->operation_mutex) );
        	stprintkd("to perform 'ST_IOCTL_START', former 'start_count' is %d.", lis3dh->start_count);
        	(lis3dh->start_count)++;
			
        	if ( 1 == lis3dh->start_count )
			{
            	atomic_set(&(lis3dh->data_ready), 0);
            	if ( (liRet = lis3dh_start(client, LIS3DH_RATE_12P5) ) < 0 ) 
				{
                	mutex_unlock(&(lis3dh->operation_mutex) );
                	return liRet;
            	}
				else
				{
					//nothing
				}
        	}
			else
			{
				//nothing
			}
        	mutex_unlock(&(lis3dh->operation_mutex) );
        	stprintkd("finish 'ST_IOCTL_START', ret = %d.", liRet);
        	return 0;

		case ST_IOCTL_CLOSE:
			
        	mutex_lock(&(lis3dh->operation_mutex) );
        	stprintkd("to perform 'ST_IOCTL_CLOSE', former 'start_count' is %d, PID : %d", lis3dh->start_count, get_current()->pid);
        	if ( 0 == (--(lis3dh->start_count) ) )
			{
            	atomic_set(&(lis3dh->data_ready), 0);
            	if ( (liRet = lis3dh_close(client) ) < 0 ) 
				{
                	mutex_unlock(&(lis3dh->operation_mutex) );
                	return liRet;
            	}
				else
				{
					//nothing
				}
        	}
        	mutex_unlock(&(lis3dh->operation_mutex) );
        	return 0;

		case ST_IOCTL_APP_SET_RATE:
			
			liRet = lis3dh_reset_rate(client, rate);
			if (liRet< 0)
			{
				return liRet;
			}
			else
			{
				//nothing
			}
			
			break;
			
		case ST_IOCTL_GETDATA:
			if ( (liRet = lis3dh_get_cached_data(client, &sense_data) ) < 0 )
			{
            	printk("failed to get cached sense data, ret = %d.", liRet);
				return liRet;
			}
			else
			{
				//nothing
			}
			break;
			
		default:
			return -ENOTTY;
	}

	switch (cmd)
	{
		case ST_IOCTL_GETDATA:
        	if ( copy_to_user(argp, &sense_data, sizeof(sense_data) ) )
			{
            	printk("failed to copy sense data to user space.");
				return -EFAULT;
        	}
			else
			{
				//npthing
			}
			break;
			
		default:
			break;
	}
	return 0;
}

static void lis3dh_work_func(struct work_struct *work)
{
	struct lis3dh_data *lis3dh = container_of(work, struct lis3dh_data, work);
	struct i2c_client *client = lis3dh->client;
	
	if (lis3dh_get_data(client) < 0) 
	{
		stprintkd(KERN_ERR "LIS3DH lis3dh_work_func: Get data failed\n");
	}
	else
	{
		//nothing
	}
		
	enable_irq(client->irq);		
}

static void  lis3dh_delaywork_func(struct work_struct *work)
{
	struct delayed_work *delaywork = container_of(work, struct delayed_work, work);
	struct lis3dh_data *lis3dh = container_of(delaywork, struct lis3dh_data, delaywork);
	struct i2c_client *client = lis3dh->client;

	if (lis3dh_get_data(client) < 0) 
	{
		printk(KERN_ERR " lis3dh_work_func: Get data failed\n");
	}
	else
	{
		//nothing
	}
	
	stprintkd("%s :int src:0x%02x\n",__FUNCTION__,lis3dh_read_reg(lis3dh->client,INT_SRC1));
	if(0==suspend_flag){
	   enable_irq(client->irq);		
	}
}

static irqreturn_t lis3dh_interrupt(int irq, void *dev_id)
{
	struct lis3dh_data *lis3dh = (struct lis3dh_data *)dev_id;
	
	disable_irq_nosync(irq);
	schedule_delayed_work(&lis3dh->delaywork, msecs_to_jiffies(30));
	stprintkf("%s :enter\n",__FUNCTION__);	
	return IRQ_HANDLED;
}

static struct file_operations lis3dh_fops = {
	.owner			= THIS_MODULE,
	.open			= lis3dh_open,
	.release		= lis3dh_release,
	.unlocked_ioctl	= lis3dh_ioctl,
};

static struct miscdevice lis3dh_device = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "mma8452_daemon",//"mma8452_daemon",
	.fops	= &lis3dh_fops,
};

static int lis3dh_remove(struct i2c_client *client)
{
	struct lis3dh_data *lis3dh = i2c_get_clientdata(client);
	
    misc_deregister(&lis3dh_device);
    input_unregister_device(lis3dh->input_dev);
    input_free_device(lis3dh->input_dev);
    free_irq(client->irq, lis3dh);
    kfree(lis3dh); 
#ifdef CONFIG_HAS_EARLYSUSPEND
    unregister_early_suspend(&lis3dh_early_suspend);
#endif      
    this_client = NULL;
	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void lis3dh_suspend(struct early_suspend *h)
{
	struct i2c_client *client = container_of(lis3dh_device.parent, struct i2c_client, dev);
	struct lis3dh_data *lis3dh = (struct lis3dh_data *)i2c_get_clientdata(client);
	suspend_flag=1;
  cancel_delayed_work_sync(&(lis3dh->delaywork));	
	lis3dh_close(client);
}

static void lis3dh_resume(struct early_suspend *h)
{
	struct i2c_client *client = container_of(lis3dh_device.parent, struct i2c_client, dev);
  struct lis3dh_data *lis3dh = (struct lis3dh_data *)i2c_get_clientdata(client);
	suspend_flag=0;
	lis3dh_start_dev(client,lis3dh->curr_tate);
	enable_irq(client->irq);
}
#else
static int lis3dh_suspend(struct i2c_client *client, pm_message_t mesg)
{
	int liRet;
	stprintkd("Gsensor lis3dh enter 2 level  suspend lis3dh->status %d\n",lis3dh->status);
	struct lis3dh_data *lis3dh = (struct lis3dh_data *)i2c_get_clientdata(client);

	return liRet;
}
static int lis3dh_resume(struct i2c_client *client)
{
	int liRet;
	struct lis3dh_data *lis3dh = (struct lis3dh_data *)i2c_get_clientdata(client);
	stprintkd("Gsensor lis3dh 2 level resume!! lis3dh->status %d\n",lis3dh->status);
	return liRet;
}
#endif

static const struct i2c_device_id lis3dh_id[] = {
	{"lis3dh", 0},
	{ }
};

static struct i2c_driver lis3dh_driver = {
	.driver = {
		.name = "lis3dh",
	},
	.id_table 	= lis3dh_id,
	.probe		= lis3dh_probe,           
	.remove		= __devexit_p(lis3dh_remove),
#ifndef CONFIG_HAS_EARLYSUSPEND	
	.suspend	= &lis3dh_suspend,
	.resume		= &lis3dh_resume,
#endif	
};

static int lis3dh_init_client(struct i2c_client *client)
{
	int liRet	= 0;
	int irq		= 0;
	struct lis3dh_data *lis3dh;
	
	lis3dh = i2c_get_clientdata(client);
	
	liRet = gpio_request(client->irq, "lis3dh_int");
	if (liRet) {
		stprintk( "failed to request lis3dh_trig GPIO%d\n",gpio_to_irq(client->irq));
		return liRet;
	}
	else
	{
		//nothing
	}

	gpio_direction_output(client->irq, 1);
    liRet = gpio_direction_input(client->irq);
    if (liRet)
	{
        stprintk("failed to set lis3dh_trig GPIO gpio input\n");
		gpio_free(client->irq);
		return liRet;
    }
	else
	{
		//nothing
	}
	
	irq = gpio_to_irq(client->irq);
	liRet = request_irq(irq, lis3dh_interrupt, IRQF_TRIGGER_LOW, client->dev.driver->name, lis3dh);
	if (liRet )
	{
		gpio_free(client->irq);
		stprintk(KERN_ERR "lis3dh_init_client: request irq failed,ret is %d\n",liRet);
        return liRet;
	}
	else
	{
		stprintk("request irq is %d,ret is  0x%x\n",irq,liRet);
	}
	
	client->irq = irq;
	disable_irq(client->irq);
	init_waitqueue_head(&data_ready_wq);
	
	return 0;
} 

static int  lis3dh_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct lis3dh_data *lis3dh;
	struct lis3dh_platform_data *pdata = pdata = client->dev.platform_data;
	int liRet = -1;
	char devid;

	stprintkf("%s enter\n",__FUNCTION__);

	lis3dh = kzalloc(sizeof(struct lis3dh_data), GFP_KERNEL);
	if (!lis3dh)
	{
		stprintk("[lis3dh]:alloc data failed.\n");
		liRet = -ENOMEM;
		goto exit_alloc_data_failed;
	}
	else
	{
		//nothig
	}

	INIT_WORK(&lis3dh->work, lis3dh_work_func);
	INIT_DELAYED_WORK(&lis3dh->delaywork, lis3dh_delaywork_func);

    memset(&(lis3dh->sense_data), 0, sizeof(struct lis3dh_axis) );
    mutex_init(&(lis3dh->sense_data_mutex) );
    
	atomic_set(&(lis3dh->data_ready), 0);
    init_waitqueue_head(&(lis3dh->data_ready_wq) );

    lis3dh->start_count = 0;
    mutex_init(&(lis3dh->operation_mutex) );
    
	lis3dh->status = LIS3DH_CLOSE;
	lis3dh->client = client;
	
	i2c_set_clientdata(client, lis3dh);

	this_client = client;

	devid = lis3dh_get_devid(lis3dh);
	if (devid != WHOAMI_LIS3DH_ACC)
	{
		pr_info("lis3dh: invalid devid\n");
		goto exit_invalid_devid;
	}
	else
	{
		//nothing
	}

	liRet = lis3dh_init_device(lis3dh);
	if (devid < 0)
	{
		pr_info("lis3dh: init err\n");
		goto exit_invalid_devid;
	}
	else
	{
		//nothing
	}
 
	liRet = lis3dh_init_client(client);
	if (liRet < 0)
	{
		stprintk(KERN_ERR "lis3dh_probe: lis3dh_init_client failed\n");
		goto exit_request_irq_failed;
	}
	else
	{
		//nothing
	}

	lis3dh->input_dev = input_allocate_device();
	if (!lis3dh->input_dev)
	{
		liRet = -ENOMEM;
		stprintk(KERN_ERR "lis3dh_probe: Failed to allocate input device\n");
		goto exit_input_allocate_device_failed;
	}
	else
	{
		//nothing
	}

	set_bit(EV_ABS, lis3dh->input_dev->evbit);

	/* x-axis acceleration */
	input_set_abs_params(lis3dh->input_dev, ABS_X, -LIS3DH_RANGE, LIS3DH_RANGE, 0, 0); //2g full scale range
	/* y-axis acceleration */
	input_set_abs_params(lis3dh->input_dev, ABS_Y, -LIS3DH_RANGE, LIS3DH_RANGE, 0, 0); //2g full scale range
	/* z-axis acceleration */
	input_set_abs_params(lis3dh->input_dev, ABS_Z, -LIS3DH_RANGE, LIS3DH_RANGE, 0, 0); //2g full scale range

	lis3dh->input_dev->name = "gsensor";
	lis3dh->input_dev->dev.parent = &client->dev;

	liRet = input_register_device(lis3dh->input_dev);
	if (liRet < 0)
	{
		stprintk(KERN_ERR "lis3dh_probe: Unable to register input device: %s\n",lis3dh->input_dev->name);
		goto exit_input_register_device_failed;
	}
	else
	{
		//nothing
	}

    lis3dh_device.parent = &client->dev;
	liRet = misc_register(&lis3dh_device);
	if (liRet < 0) 
	{
		stprintk(KERN_ERR"lis3dh_probe: mmad_device register failed\n");
		goto exit_misc_device_failed;
	}
	else
	{
		//nothing
	}

	liRet = gsensor_sysfs_init();
	if (liRet < 0)
	{
		stprintk(KERN_ERR "lis3dh_probe: gsensor sysfs init failed\n");
		goto exit_gsensor_sysfs_init_failed;
	}
	else
	{
		//nothing
	}
	
#ifdef CONFIG_HAS_EARLYSUSPEND
    lis3dh_early_suspend.suspend	= lis3dh_suspend;
    lis3dh_early_suspend.resume		= lis3dh_resume;
    lis3dh_early_suspend.level		= 0x2;
    register_early_suspend(&lis3dh_early_suspend);
#endif
  suspend_flag=0;
	printk(KERN_INFO "lis3dh probe ok\n");

	return 0;

exit_gsensor_sysfs_init_failed:
    misc_deregister(&lis3dh_device);
exit_misc_device_failed:
    input_unregister_device(lis3dh->input_dev);
exit_input_register_device_failed:
	input_free_device(lis3dh->input_dev);
exit_input_allocate_device_failed:
	free_irq(client->irq, lis3dh);
exit_request_irq_failed:
	cancel_delayed_work_sync(&lis3dh->delaywork);
	cancel_work_sync(&lis3dh->work);
exit_invalid_devid:
	kfree(lis3dh);	
exit_alloc_data_failed:
	stprintk("%s error\n",__FUNCTION__);
	return -1;
}

static int __init lis3dh_i2c_init(void)
{
	return i2c_add_driver(&lis3dh_driver);
}

static void __exit lis3dh_i2c_exit(void)
{
	i2c_del_driver(&lis3dh_driver);
}

module_init(lis3dh_i2c_init);
module_exit(lis3dh_i2c_exit);

MODULE_DESCRIPTION ("STMicroelectronics gsensor driver");
MODULE_AUTHOR("LB<libing@rock-chips.com>");
MODULE_LICENSE("GPL");
