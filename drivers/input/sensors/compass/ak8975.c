/* drivers/input/sensors/access/akm8975.c
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
#include <mach/gpio.h>
#include <mach/board.h> 
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <linux/akm8975.h>
#include <linux/sensor-dev.h>

#define SENSOR_DATA_SIZE		8

#if 0
#define SENSOR_DEBUG_TYPE SENSOR_TYPE_COMPASS
#define DBG(x...) if(sensor->pdata->type == SENSOR_DEBUG_TYPE) printk(x)
#else
#define DBG(x...)
#endif



#define AK8975_DEVICE_ID		0x48
static struct i2c_client *this_client;

static atomic_t m_flag;
static atomic_t a_flag;
static atomic_t mv_flag;
static atomic_t open_flag;
static short akmd_delay = 100;
static DECLARE_WAIT_QUEUE_HEAD(open_wq);


/****************operate according to sensor chip:start************/

static int sensor_active(struct i2c_client *client, int enable, int rate)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);	
	int result = 0;
	int status = 0;
		
	sensor->ops->ctrl_data = sensor_read_reg(client, sensor->ops->ctrl_reg);
	
	//register setting according to chip datasheet		
	if(enable)
	{	
		status = AK8975_MODE_SNG_MEASURE;
		sensor->ops->ctrl_data |= status;	
	}
	else
	{
		status = ~AK8975_MODE_SNG_MEASURE;
		sensor->ops->ctrl_data &= status;
	}

	DBG("%s:reg=0x%x,reg_ctrl=0x%x,enable=%d\n",__func__,sensor->ops->ctrl_reg, sensor->ops->ctrl_data, enable);
	result = sensor_write_reg(client, sensor->ops->ctrl_reg, sensor->ops->ctrl_data);
	if(result)
		printk("%s:fail to active sensor\n",__func__);
	
	return result;

}

static int sensor_init(struct i2c_client *client)
{	
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);	
	int result = 0;

	this_client = client;	

	result = sensor->ops->active(client,0,0);
	if(result)
	{
		printk("%s:line=%d,error\n",__func__,__LINE__);
		return result;
	}
	
	sensor->status_cur = SENSOR_OFF;
	
	sensor->ops->ctrl_data = 0;
	result = sensor_write_reg(client, sensor->ops->ctrl_reg, sensor->ops->ctrl_data);
	if(result)
	{
		printk("%s:line=%d,error\n",__func__,__LINE__);
		return result;
	}

	DBG("%s:status_cur=%d\n",__func__, sensor->status_cur);
	return result;
}

static int sensor_report_value(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
	    	(struct sensor_private_data *) i2c_get_clientdata(client);	
	char buffer[8] = {0};	
	unsigned char *stat;
	unsigned char *stat2;	
	int ret = 0;	
#ifdef SENSOR_DEBUG_TYPE	
	int i;
#endif			
	if(sensor->ops->read_len < 8)	//sensor->ops->read_len = 8
	{
		printk("%s:lenth is error,len=%d\n",__func__,sensor->ops->read_len);
		return -1;
	}
	
	memset(buffer, 0, 8);
	
	/* Data bytes from hardware xL, xH, yL, yH, zL, zH */	
	do {
		*buffer = sensor->ops->read_reg;
		ret = sensor_rx_data(client, buffer, sensor->ops->read_len);
		if (ret < 0)
		return ret;
	} while (0);

	stat = &buffer[0];
	stat2 = &buffer[7];
	
	/*
	 * ST : data ready -
	 * Measurement has been completed and data is ready to be read.
	 */
	if ((*stat & 0x01) != 0x01) {
		DBG(KERN_ERR "%s:ST is not set\n",__func__);
		return -1;
	}

	/*
	 * ST2 : data error -
	 * occurs when data read is started outside of a readable period;
	 * data read would not be correct.
	 * Valid in continuous measurement mode only.
	 * In single measurement mode this error should not occour but we
	 * stil account for it and return an error, since the data would be
	 * corrupted.
	 * DERR bit is self-clearing when ST2 register is read.
	 */
	if (*stat2 & 0x04)
	{
		DBG(KERN_ERR "%s:compass data error\n",__func__);
		return -2;
	}
	
	/*
	 * ST2 : overflow -
	 * the sum of the absolute values of all axis |X|+|Y|+|Z| < 2400uT.
	 * This is likely to happen in presence of an external magnetic
	 * disturbance; it indicates, the sensor data is incorrect and should
	 * be ignored.
	 * An error is returned.
	 * HOFL bit clears when a new measurement starts.
	 */
	if (*stat2 & 0x08)
	{	
		DBG(KERN_ERR "%s:compass data overflow\n",__func__);
		return -3;
	}
	
	/* »¥³âµØ»º´æÊý¾Ý. */
	mutex_lock(&sensor->data_mutex);	
	memcpy(sensor->sensor_data, buffer, sensor->ops->read_len);
	mutex_unlock(&sensor->data_mutex);
#ifdef SENSOR_DEBUG_TYPE	
	DBG("%s:",__func__);
	for(i=0; i<sensor->ops->read_len; i++)
		DBG("%d,",buffer[i]);
	DBG("\n");
#endif	

	if((sensor->pdata->irq_enable)&& (sensor->ops->int_status_reg >= 0))	//read sensor intterupt status register
	{
		
		ret= sensor_read_reg(client, sensor->ops->int_status_reg);
		if(ret)
		{
			printk("%s:fail to clear sensor int status,ret=0x%x\n",__func__,ret);
		}
	}
	
	return ret;
}

static void compass_set_YPR(short *rbuf)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(this_client);	
	
	/* Report magnetic sensor information */
	if (atomic_read(&m_flag)) {
		input_report_abs(sensor->input_dev, ABS_RX, rbuf[0]);
		input_report_abs(sensor->input_dev, ABS_RY, rbuf[1]);
		input_report_abs(sensor->input_dev, ABS_RZ, rbuf[2]);
		input_report_abs(sensor->input_dev, ABS_RUDDER, rbuf[4]);
		DBG("%s:m_flag:x=%d,y=%d,z=%d,RUDDER=%d\n",__func__,rbuf[0], rbuf[1], rbuf[2], rbuf[4]);
	}
	
	/* Report acceleration sensor information */
	if (atomic_read(&a_flag)) {
		input_report_abs(sensor->input_dev, ABS_X, rbuf[6]);
		input_report_abs(sensor->input_dev, ABS_Y, rbuf[7]);
		input_report_abs(sensor->input_dev, ABS_Z, rbuf[8]);
		input_report_abs(sensor->input_dev, ABS_WHEEL, rbuf[5]);
		
		DBG("%s:a_flag:x=%d,y=%d,z=%d,WHEEL=%d\n",__func__,rbuf[6], rbuf[7], rbuf[8], rbuf[5]);
	}
	
	/* Report magnetic vector information */
	if (atomic_read(&mv_flag)) {
		input_report_abs(sensor->input_dev, ABS_HAT0X, rbuf[9]);
		input_report_abs(sensor->input_dev, ABS_HAT0Y, rbuf[10]);
		input_report_abs(sensor->input_dev, ABS_BRAKE, rbuf[11]);
	
		DBG("%s:mv_flag:x=%d,y=%d,BRAKE=%d\n",__func__,rbuf[9], rbuf[10], rbuf[11]);
	}
	
	input_sync(sensor->input_dev);
}



static int compass_aot_open(struct inode *inode, struct file *file)
{
#ifdef SENSOR_DEBUG_TYPE
	struct sensor_private_data* sensor =
			(struct sensor_private_data *)i2c_get_clientdata(this_client);
#endif
	int result = 0;
	int flag = 0;
	flag = atomic_read(&open_flag);
	if(!flag)
	{	
		atomic_set(&open_flag, 1);
		wake_up(&open_wq);
	}

	DBG("%s\n", __func__);
	return result;
}


static int compass_aot_release(struct inode *inode, struct file *file)
{	
#ifdef SENSOR_DEBUG_TYPE
	struct sensor_private_data* sensor =
			(struct sensor_private_data *)i2c_get_clientdata(this_client);	
#endif
	//struct i2c_client *client = this_client;
	int result = 0;
	int flag = 0;
	flag = atomic_read(&open_flag);
	if(flag)
	{
		atomic_set(&open_flag, 0);
		wake_up(&open_wq);	
	}
	
	DBG("%s\n", __func__);
	return result;
}


/* ioctl - I/O control */
static long compass_aot_ioctl(struct file *file,
			  unsigned int cmd, unsigned long arg)
{
#ifdef SENSOR_DEBUG_TYPE
	struct sensor_private_data* sensor = 
			(struct sensor_private_data *)i2c_get_clientdata(this_client);	
#endif
	void __user *argp = (void __user *)arg;
	int result = 0;
	short flag;
	
	switch (cmd) {
		case ECS_IOCTL_APP_SET_MFLAG:
		case ECS_IOCTL_APP_SET_AFLAG:
		case ECS_IOCTL_APP_SET_MVFLAG:
			if (copy_from_user(&flag, argp, sizeof(flag))) {
				return -EFAULT;
			}
			if (flag < 0 || flag > 1) {
				return -EINVAL;
			}
			break;
		case ECS_IOCTL_APP_SET_DELAY:
			if (copy_from_user(&flag, argp, sizeof(flag))) {
				return -EFAULT;
			}
			break;
		default:
			break;
	}
	
	switch (cmd) {
		case ECS_IOCTL_APP_SET_MFLAG:	
			atomic_set(&m_flag, flag);			
			DBG("%s:ECS_IOCTL_APP_SET_MFLAG,flag=%d\n", __func__,flag);
			break;
		case ECS_IOCTL_APP_GET_MFLAG:		
			flag = atomic_read(&m_flag);
			DBG("%s:ECS_IOCTL_APP_GET_MFLAG,flag=%d\n", __func__,flag);
			break;
		case ECS_IOCTL_APP_SET_AFLAG:	
			atomic_set(&a_flag, flag);		
			DBG("%s:ECS_IOCTL_APP_SET_AFLAG,flag=%d\n", __func__,flag);
			break;
		case ECS_IOCTL_APP_GET_AFLAG:
			flag = atomic_read(&a_flag);		
			DBG("%s:ECS_IOCTL_APP_GET_AFLAG,flag=%d\n", __func__,flag);
			break;
		case ECS_IOCTL_APP_SET_MVFLAG:	
			atomic_set(&mv_flag, flag);		
			DBG("%s:ECS_IOCTL_APP_SET_MVFLAG,flag=%d\n", __func__,flag);
			break;
		case ECS_IOCTL_APP_GET_MVFLAG:		
			flag = atomic_read(&mv_flag);		
			DBG("%s:ECS_IOCTL_APP_GET_MVFLAG,flag=%d\n", __func__,flag);
			break;
		case ECS_IOCTL_APP_SET_DELAY:
			akmd_delay = flag;
			break;
		case ECS_IOCTL_APP_GET_DELAY:
			flag = akmd_delay;
			break;
		default:
			return -ENOTTY;
	}
	
	switch (cmd) {
		case ECS_IOCTL_APP_GET_MFLAG:
		case ECS_IOCTL_APP_GET_AFLAG:
		case ECS_IOCTL_APP_GET_MVFLAG:
		case ECS_IOCTL_APP_GET_DELAY:
			if (copy_to_user(argp, &flag, sizeof(flag))) {
				return -EFAULT;
			}
			break;
		default:
			break;
	}

	return result;
}

static int compass_dev_open(struct inode *inode, struct file *file)
{
#ifdef SENSOR_DEBUG_TYPE
	struct sensor_private_data* sensor = 
		(struct sensor_private_data *)i2c_get_clientdata(this_client); 
#endif
	int result = 0;
	DBG("%s\n",__func__);

	return result;
}


static int compass_dev_release(struct inode *inode, struct file *file)
{
#ifdef SENSOR_DEBUG_TYPE
	struct sensor_private_data* sensor = 
		(struct sensor_private_data *)i2c_get_clientdata(this_client); 
#endif
	int result = 0;	
	DBG("%s\n",__func__);

	return result;
}

static int compass_akm_set_mode(struct i2c_client *client, char mode)
{
	struct sensor_private_data* sensor = (struct sensor_private_data *)i2c_get_clientdata(this_client); 
	int result = 0;	

	switch(mode)
	{
		case AK8975_MODE_SNG_MEASURE:
		case AK8975_MODE_SELF_TEST: 	
		case AK8975_MODE_FUSE_ACCESS:			
			if(sensor->status_cur == SENSOR_OFF)
			{
				if(sensor->pdata->irq_enable)
				{
					//DBG("%s:enable irq=%d\n",__func__,client->irq);
					//enable_irq(client->irq);
				}	
				else
				{
					schedule_delayed_work(&sensor->delaywork, msecs_to_jiffies(sensor->pdata->poll_delay_ms));
				}
				
				sensor->status_cur = SENSOR_ON;
			}

			break;

		case AK8975_MODE_POWERDOWN: 	
			if(sensor->status_cur == SENSOR_ON)
			{
				if(sensor->pdata->irq_enable)
				{	
					//DBG("%s:disable irq=%d\n",__func__,client->irq);
					//disable_irq_nosync(client->irq);//disable irq
				}
				else
				cancel_delayed_work_sync(&sensor->delaywork);	

				sensor->status_cur = SENSOR_OFF;
			}
			break;

	}
	
	switch(mode)
	{
		case AK8975_MODE_SNG_MEASURE:		
			result = sensor_write_reg(client, sensor->ops->ctrl_reg, AK8975_MODE_SNG_MEASURE);
			if(result)
			printk("%s:i2c error,mode=%d\n",__func__,mode);				
			break;
		case AK8975_MODE_SELF_TEST:			
			result = sensor_write_reg(client, sensor->ops->ctrl_reg, AK8975_MODE_SELF_TEST);
			if(result)
			printk("%s:i2c error,mode=%d\n",__func__,mode);
			break;
		case AK8975_MODE_FUSE_ACCESS:
			result = sensor_write_reg(client, sensor->ops->ctrl_reg, AK8975_MODE_FUSE_ACCESS);
			if(result)
			printk("%s:i2c error,mode=%d\n",__func__,mode);
			break;
		case AK8975_MODE_POWERDOWN:
			/* Set powerdown mode */
			result = sensor_write_reg(client, sensor->ops->ctrl_reg, AK8975_MODE_POWERDOWN);
			if(result)
			printk("%s:i2c error,mode=%d\n",__func__,mode);
			udelay(100);
			break;
		default:
			printk("%s: Unknown mode(%d)", __func__, mode);
			result = -EINVAL;
			break;
	}
	DBG("%s:mode=%d\n",__func__,mode);
	return result;

}


static int compass_akm_get_openstatus(void)
{
	wait_event_interruptible(open_wq, (atomic_read(&open_flag) != 0));
	return atomic_read(&open_flag);
}

static int compass_akm_get_closestatus(void)
{
	wait_event_interruptible(open_wq, (atomic_read(&open_flag) <= 0));
	return atomic_read(&open_flag);
}


/* ioctl - I/O control */
static long compass_dev_ioctl(struct file *file,
			  unsigned int cmd, unsigned long arg)
{
    struct sensor_private_data* sensor = (struct sensor_private_data *)i2c_get_clientdata(this_client);
	struct i2c_client *client = this_client;
	void __user *argp = (void __user *)arg;
	int result = 0;
	struct akm8975_platform_data compass;
		
	/* NOTE: In this function the size of "char" should be 1-byte. */
	char compass_data[SENSOR_DATA_SIZE];/* for GETDATA */
	char rwbuf[RWBUF_SIZE];		/* for READ/WRITE */
	char mode;					/* for SET_MODE*/
	short value[12];			/* for SET_YPR */
	short delay;				/* for GET_DELAY */
	int status;					/* for OPEN/CLOSE_STATUS */
	int ret = -1;				/* Return value. */
	
	switch (cmd) {
		case ECS_IOCTL_WRITE:
		case ECS_IOCTL_READ:
			if (argp == NULL) {
				return -EINVAL;
			}
			if (copy_from_user(&rwbuf, argp, sizeof(rwbuf))) {
				return -EFAULT;
			}
			break;
		case ECS_IOCTL_SET_MODE:
			if (argp == NULL) {
				return -EINVAL;
			}
			if (copy_from_user(&mode, argp, sizeof(mode))) {
				return -EFAULT;
			}
			break;
		case ECS_IOCTL_SET_YPR:
			if (argp == NULL) {
				return -EINVAL;
			}
			if (copy_from_user(&value, argp, sizeof(value))) {
				return -EFAULT;
			}
			break;
		default:
			break;
	}
	
	switch (cmd) {
		case ECS_IOCTL_WRITE:
			DBG("%s:ECS_IOCTL_WRITE start\n",__func__);
			mutex_lock(&sensor->operation_mutex);
			if ((rwbuf[0] < 2) || (rwbuf[0] > (RWBUF_SIZE-1))) {			
				mutex_unlock(&sensor->operation_mutex);
				return -EINVAL;
			}
			ret = sensor_tx_data(client, &rwbuf[1], rwbuf[0]);
			if (ret < 0) {	
				mutex_unlock(&sensor->operation_mutex);		
				printk("%s:fait to tx data\n",__func__);
				return ret;
			}			
			mutex_unlock(&sensor->operation_mutex);
			break;
		case ECS_IOCTL_READ:				
			DBG("%s:ECS_IOCTL_READ start\n",__func__);
			mutex_lock(&sensor->operation_mutex);
			if ((rwbuf[0] < 1) || (rwbuf[0] > (RWBUF_SIZE-1))) {		
				mutex_unlock(&sensor->operation_mutex);			
				printk("%s:data is error\n",__func__);
				return -EINVAL;
			}
			ret = sensor_rx_data(client, &rwbuf[1], rwbuf[0]);
			if (ret < 0) {	
				mutex_unlock(&sensor->operation_mutex);		
				printk("%s:fait to rx data\n",__func__);
				return ret;
			}		
			mutex_unlock(&sensor->operation_mutex);
			break;
		case ECS_IOCTL_SET_MODE:		
			DBG("%s:ECS_IOCTL_SET_MODE start\n",__func__);		
			mutex_lock(&sensor->operation_mutex);
			ret = compass_akm_set_mode(client, mode);
			if (ret < 0) {
				printk("%s:fait to set mode\n",__func__);		
				mutex_unlock(&sensor->operation_mutex);
				return ret;
			}		
			mutex_unlock(&sensor->operation_mutex);
			break;
		case ECS_IOCTL_GETDATA:		
			DBG("%s:ECS_IOCTL_GETDATA start\n",__func__);
			mutex_lock(&sensor->data_mutex);	
			memcpy(compass_data, sensor->sensor_data, SENSOR_DATA_SIZE);	//get data from buffer
			mutex_unlock(&sensor->data_mutex);
			break;
		case ECS_IOCTL_SET_YPR:			
			DBG("%s:ECS_IOCTL_SET_YPR start\n",__func__);
			mutex_lock(&sensor->data_mutex);
			compass_set_YPR(value);		
			mutex_unlock(&sensor->data_mutex);
			break;
		case ECS_IOCTL_GET_OPEN_STATUS:
			status = compass_akm_get_openstatus();	
			DBG("%s:openstatus=%d\n",__func__,status);
			break;
		case ECS_IOCTL_GET_CLOSE_STATUS:
			status = compass_akm_get_closestatus();	
			DBG("%s:closestatus=%d\n",__func__,status);
			break;
		case ECS_IOCTL_GET_DELAY:
			delay = akmd_delay;
			break;
		case ECS_IOCTL_GET_PLATFORM_DATA:			
			DBG("%s:ECS_IOCTL_GET_PLATFORM_DATA start\n",__func__);
			memcpy(compass.m_layout, sensor->pdata->m_layout, sizeof(sensor->pdata->m_layout));
			memcpy(compass.project_name, sensor->pdata->project_name, sizeof(sensor->pdata->project_name));
			ret = copy_to_user(argp, &compass, sizeof(compass));
			if(ret < 0)
			{
				printk("%s:error,ret=%d\n",__FUNCTION__, ret);
				return ret;
			}
			break;
			
		default:
			return -ENOTTY;
	}
	
	switch (cmd) {
		case ECS_IOCTL_READ:
			if (copy_to_user(argp, &rwbuf, rwbuf[0]+1)) {
				return -EFAULT;
			}
			break;
		case ECS_IOCTL_GETDATA:
			if (copy_to_user(argp, &compass_data, sizeof(compass_data))) {
				return -EFAULT;
			}
			break;
		case ECS_IOCTL_GET_OPEN_STATUS:
		case ECS_IOCTL_GET_CLOSE_STATUS:
			if (copy_to_user(argp, &status, sizeof(status))) {
				return -EFAULT;
			}
			break;
		case ECS_IOCTL_GET_DELAY:
			if (copy_to_user(argp, &delay, sizeof(delay))) {
				return -EFAULT;
			}
			break;
		default:
			break;
	}

	return result;
}


static struct file_operations compass_aot_fops =
{
	.owner = THIS_MODULE,
	.unlocked_ioctl = compass_aot_ioctl,
	.open = compass_aot_open,
	.release = compass_aot_release,
};



static struct miscdevice compass_aot_device =
{	
	.minor = MISC_DYNAMIC_MINOR,
	.name = "akm8975_aot",
	.fops = &compass_aot_fops,
};


static struct file_operations compass_dev_fops =
{
	.owner = THIS_MODULE,
	.open = compass_dev_open,
	.release = compass_dev_release,	
	.unlocked_ioctl = compass_dev_ioctl,
};


static struct miscdevice compass_dev_device =
{	
	.minor = MISC_DYNAMIC_MINOR,
	.name = "akm8975_dev",
	.fops = &compass_dev_fops,
};

struct sensor_operate akm8975_ops = {
	.name				= "akm8975",
	.type				= SENSOR_TYPE_COMPASS,	//it is important
	.id_i2c				= COMPASS_ID_AK8975,
	.read_reg			= AK8975_REG_ST1,	//read data
	.read_len			= SENSOR_DATA_SIZE,	//data length
	.id_reg				= AK8975_REG_WIA,	//read id
	.id_data 			= AK8975_DEVICE_ID,
	.precision			= 8,			//12 bits
	.ctrl_reg 			= AK8975_REG_CNTL,	//enable or disable 
	.int_status_reg 		= SENSOR_UNKNOW_DATA,	//not exist
	.range				= {-0xffff,0xffff},
	.trig				= IRQF_TRIGGER_RISING,	//if LEVEL interrupt then IRQF_ONESHOT
	.active				= sensor_active,	
	.init				= sensor_init,
	.report				= sensor_report_value,	
	.misc_dev 			= &compass_dev_device,	//private misc support
};

/****************operate according to sensor chip:end************/

//function name should not be changed
struct sensor_operate *compass_get_ops(void)
{
	return &akm8975_ops;
}

EXPORT_SYMBOL(compass_get_ops);


static int __init compass_init(void)
{
	struct sensor_operate *ops = compass_get_ops();
	int result = 0;
	int type = ops->type;
	result = sensor_register_slave(type, NULL, NULL, compass_get_ops);

	result = misc_register(&compass_aot_device);
	if (result < 0) {
		printk("%s:fail to register misc device %s\n", __func__, compass_aot_device.name);
		goto error;
	}

	/* As default, report all information */
	atomic_set(&m_flag, 1);
	atomic_set(&a_flag, 1);
	atomic_set(&mv_flag, 1);			
	atomic_set(&open_flag, 0);		
	init_waitqueue_head(&open_wq);
			
	printk("%s\n",__func__);
error:
	return result;
}

static void __exit compass_exit(void)
{
	struct sensor_operate *ops = compass_get_ops();
	int type = ops->type;
	sensor_unregister_slave(type, NULL, NULL, compass_get_ops);
}


module_init(compass_init);
module_exit(compass_exit);


