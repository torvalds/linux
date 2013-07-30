/* drivers/input/sensors/access/akm8963.c
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
#include <linux/sensor-dev.h>

#define SENSOR_DATA_SIZE		8


#define SENSOR_DATA_SIZE	8
#define YPR_DATA_SIZE		12
#define RWBUF_SIZE		16

#define ACC_DATA_FLAG		0
#define MAG_DATA_FLAG		1
#define ORI_DATA_FLAG		2
#define AKM_NUM_SENSORS		3

#define ACC_DATA_READY		(1<<(ACC_DATA_FLAG))
#define MAG_DATA_READY		(1<<(MAG_DATA_FLAG))
#define ORI_DATA_READY		(1<<(ORI_DATA_FLAG))

/*! \name AK8963 constant definition
 \anchor AK8963_Def
 Constant definitions of the AK8963.*/
#define AK8963_MEASUREMENT_TIME_US	10000

/*! \name AK8963 operation mode
 \anchor AK8963_Mode
 Defines an operation mode of the AK8963.*/
/*! @{*/
#define AK8963_MODE_SNG_MEASURE	0x01
#define	AK8963_MODE_SELF_TEST	0x08
#define	AK8963_MODE_FUSE_ACCESS	0x0F
#define	AK8963_MODE_POWERDOWN	0x00

/*! @}*/

/*! \name AK8963 register address
\anchor AK8963_REG
Defines a register address of the AK8963.*/
/*! @{*/
#define AK8963_REG_WIA		0x00
#define AK8963_REG_INFO		0x01
#define AK8963_REG_ST1		0x02
#define AK8963_REG_HXL		0x03
#define AK8963_REG_HXH		0x04
#define AK8963_REG_HYL		0x05
#define AK8963_REG_HYH		0x06
#define AK8963_REG_HZL		0x07
#define AK8963_REG_HZH		0x08
#define AK8963_REG_ST2		0x09
#define AK8963_REG_CNTL1	0x0A
#define AK8963_REG_CNTL2	0x0B
#define AK8963_REG_ASTC		0x0C
#define AK8963_REG_TS1		0x0D
#define AK8963_REG_TS2		0x0E
#define AK8963_REG_I2CDIS	0x0F
/*! @}*/

/*! \name AK8963 fuse-rom address
\anchor AK8963_FUSE
Defines a read-only address of the fuse ROM of the AK8963.*/
/*! @{*/
#define AK8963_FUSE_ASAX	0x10
#define AK8963_FUSE_ASAY	0x11
#define AK8963_FUSE_ASAZ	0x12
/*! @}*/

#define AK8963_INFO_DATA	(0x03<<3)


#define COMPASS_IOCTL_MAGIC                   'c'

/* IOCTLs for AKM library */
#define ECS_IOCTL_WRITE                 _IOW(COMPASS_IOCTL_MAGIC, 0x01, char*)
#define ECS_IOCTL_READ                  _IOWR(COMPASS_IOCTL_MAGIC, 0x02, char*)
#define ECS_IOCTL_RESET      	        _IO(COMPASS_IOCTL_MAGIC, 0x03) /* NOT used in AK8975 */
#define ECS_IOCTL_SET_MODE              _IOW(COMPASS_IOCTL_MAGIC, 0x04, short)
#define ECS_IOCTL_GETDATA               _IOR(COMPASS_IOCTL_MAGIC, 0x05, char[SENSOR_DATA_SIZE])
#define ECS_IOCTL_SET_YPR               _IOW(COMPASS_IOCTL_MAGIC, 0x06, short[12])
#define ECS_IOCTL_GET_OPEN_STATUS       _IOR(COMPASS_IOCTL_MAGIC, 0x07, int)
#define ECS_IOCTL_GET_CLOSE_STATUS      _IOR(COMPASS_IOCTL_MAGIC, 0x08, int)
#define ECS_IOCTL_GET_LAYOUT        	_IOR(COMPASS_IOCTL_MAGIC, 0x09, char)
#define ECS_IOCTL_GET_ACCEL         	_IOR(COMPASS_IOCTL_MAGIC, 0x0A, short[3])
#define ECS_IOCTL_GET_OUTBIT        	_IOR(COMPASS_IOCTL_MAGIC, 0x0B, char)
#define ECS_IOCTL_GET_DELAY             _IOR(COMPASS_IOCTL_MAGIC, 0x30, short)
#define ECS_IOCTL_GET_PROJECT_NAME      _IOR(COMPASS_IOCTL_MAGIC, 0x0D, char[64])
#define ECS_IOCTL_GET_MATRIX            _IOR(COMPASS_IOCTL_MAGIC, 0x0E, short [4][3][3])
#define ECS_IOCTL_GET_PLATFORM_DATA     _IOR(COMPASS_IOCTL_MAGIC, 0x0E, struct akm_platform_data)



#define AK8963_DEVICE_ID		0x48
static struct i2c_client *this_client;
static struct miscdevice compass_dev_device;

static short g_akm_rbuf[12];


/****************operate according to sensor chip:start************/

static int sensor_active(struct i2c_client *client, int enable, int rate)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);	
	int result = 0;
		
	//sensor->ops->ctrl_data = sensor_read_reg(client, sensor->ops->ctrl_reg);
	
	//register setting according to chip datasheet		
	if(enable)
	{	
		sensor->ops->ctrl_data = AK8963_MODE_SNG_MEASURE;	
	}
	else
	{
		sensor->ops->ctrl_data = AK8963_MODE_POWERDOWN;
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
	char info = 0;

	this_client = client;	

	result = sensor->ops->active(client,0,0);
	if(result)
	{
		printk("%s:line=%d,error\n",__func__,__LINE__);
		return result;
	}
	
	sensor->status_cur = SENSOR_OFF;

	info = sensor_read_reg(client, AK8963_REG_INFO);	
	if((info & (0x0f<<3)) != AK8963_INFO_DATA)
	{
		printk("%s:info=0x%x,it is not %s\n",__func__, info, sensor->ops->name);
		return -1;
	}

	result = misc_register(&compass_dev_device);
	if (result < 0) {
		printk("%s:fail to register misc device %s\n", __func__, compass_dev_device.name);
		result = -1;
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
	char value = 0;
	int i;

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
	DBG("%s:",__func__);
	for(i=0; i<sensor->ops->read_len; i++)
		DBG("0x%x,",buffer[i]);
	DBG("\n");

	if((sensor->pdata->irq_enable)&& (sensor->ops->int_status_reg >= 0))	//read sensor intterupt status register
	{
		
		value = sensor_read_reg(client, sensor->ops->int_status_reg);
		DBG("%s:sensor int status :0x%x\n",__func__,value);
	}

	
	//trigger next measurement 
	ret = sensor_write_reg(client, sensor->ops->ctrl_reg, sensor->ops->ctrl_data);
	if(ret)
	{
		printk(KERN_ERR "%s:fail to set ctrl_data:0x%x\n",__func__,sensor->ops->ctrl_data);
		return ret;
	}

	return ret;
}

static void compass_set_YPR(int *rbuf)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(this_client);	

	/* No events are reported */
	if (!rbuf[0]) {
		printk("%s:Don't waste a time.",__func__);
		return;
	}

	DBG("%s:buf[0]=0x%x\n",__func__, rbuf[0]);
	
	/* Report magnetic sensor information */
	if (atomic_read(&sensor->flags.m_flag) && (rbuf[0] & ORI_DATA_READY)) {
		input_report_abs(sensor->input_dev, ABS_RX, rbuf[9]);
		input_report_abs(sensor->input_dev, ABS_RY, rbuf[10]);
		input_report_abs(sensor->input_dev, ABS_RZ, rbuf[11]);
		input_report_abs(sensor->input_dev, ABS_RUDDER, rbuf[4]);
		DBG("%s:m_flag:x=%d,y=%d,z=%d,RUDDER=%d\n",__func__,rbuf[9], rbuf[10], rbuf[11], rbuf[4]);
	}
	
	/* Report acceleration sensor information */
	if (atomic_read(&sensor->flags.a_flag) && (rbuf[0] & ACC_DATA_READY)) {
		input_report_abs(sensor->input_dev, ABS_X, rbuf[1]);
		input_report_abs(sensor->input_dev, ABS_Y, rbuf[2]);
		input_report_abs(sensor->input_dev, ABS_Z, rbuf[3]);
		input_report_abs(sensor->input_dev, ABS_WHEEL, rbuf[4]);
		
		DBG("%s:a_flag:x=%d,y=%d,z=%d,WHEEL=%d\n",__func__,rbuf[1], rbuf[2], rbuf[3], rbuf[4]);
	}
	
	/* Report magnetic vector information */
	if (atomic_read(&sensor->flags.mv_flag) && (rbuf[0] & MAG_DATA_READY)) {
		input_report_abs(sensor->input_dev, ABS_HAT0X, rbuf[5]);
		input_report_abs(sensor->input_dev, ABS_HAT0Y, rbuf[6]);
		input_report_abs(sensor->input_dev, ABS_BRAKE, rbuf[7]);	
		input_report_abs(sensor->input_dev, ABS_HAT1X, rbuf[8]);
	
		DBG("%s:mv_flag:x=%d,y=%d,z=%d,status=%d\n",__func__,rbuf[5], rbuf[6], rbuf[7], rbuf[8]);
	}
	
	input_sync(sensor->input_dev);

	memcpy(g_akm_rbuf, rbuf, 12);	//used for ECS_IOCTL_GET_ACCEL
}



static int compass_dev_open(struct inode *inode, struct file *file)
{
	struct sensor_private_data* sensor = 
		(struct sensor_private_data *)i2c_get_clientdata(this_client); 
	int result = 0;
	DBG("%s\n",__func__);

	return result;
}


static int compass_dev_release(struct inode *inode, struct file *file)
{
	struct sensor_private_data* sensor = 
		(struct sensor_private_data *)i2c_get_clientdata(this_client); 
	int result = 0;	
	DBG("%s\n",__func__);

	return result;
}

static int compass_akm_set_mode(struct i2c_client *client, char mode)
{
	struct sensor_private_data* sensor = (struct sensor_private_data *)i2c_get_clientdata(this_client); 
	int result = 0;	

	switch(mode & 0x0f)
	{
		case AK8963_MODE_SNG_MEASURE:
		case AK8963_MODE_SELF_TEST: 	
		case AK8963_MODE_FUSE_ACCESS:			
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

		case AK8963_MODE_POWERDOWN: 	
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
	
	switch(mode & 0x0f)
	{
		case AK8963_MODE_SNG_MEASURE:		
			result = sensor_write_reg(client, sensor->ops->ctrl_reg, mode);
			if(result)
			printk("%s:i2c error,mode=%d\n",__func__,mode);				
			break;
		case AK8963_MODE_SELF_TEST:			
			result = sensor_write_reg(client, sensor->ops->ctrl_reg, mode);
			if(result)
			printk("%s:i2c error,mode=%d\n",__func__,mode);
			break;
		case AK8963_MODE_FUSE_ACCESS:
			result = sensor_write_reg(client, sensor->ops->ctrl_reg, mode);
			if(result)
			printk("%s:i2c error,mode=%d\n",__func__,mode);
			break;
		case AK8963_MODE_POWERDOWN:
			/* Set powerdown mode */
			result = sensor_write_reg(client, sensor->ops->ctrl_reg, AK8963_MODE_POWERDOWN);
			if(result)
			printk("%s:i2c error,mode=%d\n",__func__,mode);
			udelay(100);
			break;
		default:
			printk("%s: Unknown mode(%d)", __func__, mode);
			result = -EINVAL;
			break;
	}
	DBG("%s:mode=0x%x\n",__func__,mode);
	return result;

}

static int compass_akm_reset(struct i2c_client *client)
{
	struct sensor_private_data* sensor = (struct sensor_private_data *)i2c_get_clientdata(this_client); 
	int result = 0;	
	
	if(sensor->pdata->reset_pin > 0)
	{
		gpio_direction_output(sensor->pdata->reset_pin, GPIO_LOW);
		udelay(10);
		gpio_direction_output(sensor->pdata->reset_pin, GPIO_HIGH);
	}
	else	
	{
		/* Set measure mode */
		result = sensor_write_reg(client, AK8963_REG_CNTL2, AK8963_MODE_SNG_MEASURE);
		if(result)
		printk("%s:fail to Set measure mode\n",__func__);
	}
	
	udelay(100);
	
	return result;

}



static int compass_akm_get_openstatus(void)
{	
	struct sensor_private_data* sensor = (struct sensor_private_data *)i2c_get_clientdata(this_client); 
	wait_event_interruptible(sensor->flags.open_wq, (atomic_read(&sensor->flags.open_flag) != 0));
	return atomic_read(&sensor->flags.open_flag);
}

static int compass_akm_get_closestatus(void)
{	
	struct sensor_private_data* sensor = (struct sensor_private_data *)i2c_get_clientdata(this_client); 
	wait_event_interruptible(sensor->flags.open_wq, (atomic_read(&sensor->flags.open_flag) <= 0));
	return atomic_read(&sensor->flags.open_flag);
}


/* ioctl - I/O control */
static long compass_dev_ioctl(struct file *file,
			  unsigned int cmd, unsigned long arg)
{
    struct sensor_private_data* sensor = (struct sensor_private_data *)i2c_get_clientdata(this_client);
	struct i2c_client *client = this_client;
	void __user *argp = (void __user *)arg;
	int result = 0;
	struct akm_platform_data compass;
		
	/* NOTE: In this function the size of "char" should be 1-byte. */
	char compass_data[SENSOR_DATA_SIZE];	/* for GETDATA */
	char rwbuf[RWBUF_SIZE]; 		/* for READ/WRITE */
	char mode;				/* for SET_MODE*/
	int value[12];			/* for SET_YPR */
	int status;				/* for OPEN/CLOSE_STATUS */
	int ret = -1;				/* Return value. */
	
	//int8_t sensor_buf[SENSOR_DATA_SIZE];	/* for GETDATA */
	//int32_t ypr_buf[YPR_DATA_SIZE]; 	/* for SET_YPR */
	int16_t acc_buf[3];			/* for GET_ACCEL */
	int64_t delay[AKM_NUM_SENSORS]; 	/* for GET_DELAY */

	char layout;		/* for GET_LAYOUT */
	char outbit;		/* for GET_OUTBIT */

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
	case ECS_IOCTL_GETDATA:
	case ECS_IOCTL_GET_OPEN_STATUS:
	case ECS_IOCTL_GET_CLOSE_STATUS:
	case ECS_IOCTL_GET_DELAY:
	case ECS_IOCTL_GET_LAYOUT:
	case ECS_IOCTL_GET_OUTBIT:
	case ECS_IOCTL_GET_ACCEL:
		/* Just check buffer pointer */
		if (argp == NULL) {
			printk("%s:invalid argument\n",__func__);
			return -EINVAL;
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
		if(sensor->ops->ctrl_data != mode)
		{
			ret = compass_akm_set_mode(client, mode);
			if (ret < 0) {
				printk("%s:fait to set mode\n",__func__);		
				mutex_unlock(&sensor->operation_mutex);
				return ret;
			}
			
			sensor->ops->ctrl_data = mode;
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
		DBG("%s:ECS_IOCTL_GET_DELAY start\n",__func__);
		mutex_lock(&sensor->operation_mutex);
		delay[0] = sensor->flags.delay;
		delay[1] = sensor->flags.delay;
		delay[2] = sensor->flags.delay;
		mutex_unlock(&sensor->operation_mutex);
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
	case ECS_IOCTL_GET_LAYOUT:
		DBG("%s:ECS_IOCTL_GET_LAYOUT start\n",__func__);
		layout = sensor->pdata->layout;
		break;
	case ECS_IOCTL_GET_OUTBIT:
		DBG("%s:ECS_IOCTL_GET_OUTBIT start\n",__func__);
		outbit = 1; //sensor->pdata->outbit;
		break;
	case ECS_IOCTL_RESET:
		DBG("%s:ECS_IOCTL_RESET start\n",__func__);
		ret = compass_akm_reset(client);
		if (ret < 0)
			return ret;
		break;
	case ECS_IOCTL_GET_ACCEL:
		DBG("%s:ECS_IOCTL_GET_ACCEL start,no accel data\n",__func__);
		mutex_lock(&sensor->operation_mutex);
		acc_buf[0] = g_akm_rbuf[6];
		acc_buf[1] = g_akm_rbuf[7];
		acc_buf[2] = g_akm_rbuf[8];
		mutex_unlock(&sensor->operation_mutex);
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
	case ECS_IOCTL_GET_LAYOUT:
		if (copy_to_user(argp, &layout, sizeof(layout))) {
			printk("%s:error:%d\n",__FUNCTION__,__LINE__);
			return -EFAULT;
		}
		break;
	case ECS_IOCTL_GET_OUTBIT:
		if (copy_to_user(argp, &outbit, sizeof(outbit))) {
			printk("%s:error:%d\n",__FUNCTION__,__LINE__);
			return -EFAULT;
		}
		break;
	case ECS_IOCTL_GET_ACCEL:
		if (copy_to_user(argp, &acc_buf, sizeof(acc_buf))) {
			printk("%s:error:%d\n",__FUNCTION__,__LINE__);
			return -EFAULT;
		}
		break;
	default:
		break;
	}

	return result;
}

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
	.name = "akm8963_dev",
	.fops = &compass_dev_fops,
};

struct sensor_operate compass_akm8963_ops = {
	.name				= "akm8963",
	.type				= SENSOR_TYPE_COMPASS,	//it is important
	.id_i2c				= COMPASS_ID_AK8963,
	.read_reg			= AK8963_REG_ST1,	//read data
	.read_len			= SENSOR_DATA_SIZE,	//data length
	.id_reg				= AK8963_REG_WIA,	//read id
	.id_data 			= AK8963_DEVICE_ID,
	.precision			= 8,			//12 bits
	.ctrl_reg 			= AK8963_REG_CNTL1,	//enable or disable 
	.int_status_reg			= SENSOR_UNKNOW_DATA,	//not exist
	.range				= {-0xffff,0xffff},
	.trig				= IRQF_TRIGGER_RISING,	//if LEVEL interrupt then IRQF_ONESHOT
	.active				= sensor_active,	
	.init				= sensor_init,
	.report				= sensor_report_value,	
	.misc_dev 			= NULL,			//private misc support
};

/****************operate according to sensor chip:end************/

//function name should not be changed
static struct sensor_operate *compass_get_ops(void)
{
	return &compass_akm8963_ops; 
}


static int __init compass_akm8963_init(void)
{
	struct sensor_operate *ops = compass_get_ops();
	int result = 0;
	int type = ops->type;
	result = sensor_register_slave(type, NULL, NULL, compass_get_ops);
				
	return result;
}

static void __exit compass_akm8963_exit(void)
{
	struct sensor_operate *ops = compass_get_ops();
	int type = ops->type;
	sensor_unregister_slave(type, NULL, NULL, compass_get_ops);
}


module_init(compass_akm8963_init);
module_exit(compass_akm8963_exit);


