/* drivers/input/sensors/sensor-dev.c - handle all gsensor in this file
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
#include <linux/proc_fs.h>
#include <mach/gpio.h>
#include <mach/board.h> 
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <linux/l3g4200d.h>
#include <linux/sensor-dev.h>


/*
sensor-dev.c v1.1 add pressure and temperature support 2013-2-27
sensor-dev.c v1.2 add akm8963 support 2013-3-10
sensor-dev.c v1.3 add sensor debug support 2013-3-15
*/

#define SENSOR_VERSION_AND_TIME  "sensor-dev.c v1.3 add sensor debug support 2013-3-15"


struct sensor_private_data *g_sensor[SENSOR_NUM_TYPES];
static struct sensor_operate *sensor_ops[SENSOR_NUM_ID]; 
static struct class *g_sensor_class[SENSOR_NUM_TYPES];

static ssize_t sensor_proc_write(struct file *file, const char __user *buffer,
			   size_t count, loff_t *data)
{
	char c;
	int rc;
	int i = 0, num = 0;
	
	rc = get_user(c, buffer);
	if (rc)
	{
		for(i=SENSOR_TYPE_NULL+1; i<SENSOR_NUM_TYPES; i++)
		atomic_set(&g_sensor[i]->flags.debug_flag, SENSOR_TYPE_NULL);
		return rc; 
	}

	
	num = c - '0';

	printk("%s command list:close:%d, accel:%d, compass:%d, gyro:%d, light:%d, psensor:%d, temp:%d, pressure:%d,total:%d,num=%d\n",__func__,
		
		SENSOR_TYPE_NULL, SENSOR_TYPE_ACCEL,SENSOR_TYPE_COMPASS,SENSOR_TYPE_GYROSCOPE,SENSOR_TYPE_LIGHT,SENSOR_TYPE_PROXIMITY,

		SENSOR_TYPE_TEMPERATURE,SENSOR_TYPE_PRESSURE,SENSOR_NUM_TYPES,num);

	if((num > SENSOR_NUM_TYPES) || (num < SENSOR_TYPE_NULL))
	{
		printk("%s:error! only support %d to %d\n",__func__, SENSOR_TYPE_NULL,SENSOR_NUM_TYPES);
		return -1;
	}

	for(i=SENSOR_TYPE_NULL+1; i<SENSOR_NUM_TYPES; i++)
	{
		if(g_sensor[i])
		atomic_set(&g_sensor[i]->flags.debug_flag, num);
	}
	
	return count; 
}

static const struct file_operations sensor_proc_fops = {
	.owner		= THIS_MODULE, 
	.write		= sensor_proc_write,
};



static int sensor_get_id(struct i2c_client *client, int *value)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);	
	int result = 0;
	char temp = sensor->ops->id_reg;
	int i = 0;

	if(sensor->ops->id_reg >= 0)
	{
		for(i=0; i<3; i++)
		{
			result = sensor_rx_data(client, &temp, 1);
			*value = temp;
			if(!result)
			break;
		}

		if(result)
			return result;

		if(*value != sensor->ops->id_data)
		{
			printk("%s:id=0x%x is not 0x%x\n",__func__,*value, sensor->ops->id_data);
			result = -1;
		}
			
		DBG("%s:devid=0x%x\n",__func__,*value);
	}
	
	return result;
}

static int sensor_initial(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);	
	int result = 0;

	//register setting according to chip datasheet	
	result = sensor->ops->init(client);
	if(result < 0)
	{
		printk("%s:fail to init sensor\n",__func__);
		return result;
	}


	DBG("%s:ctrl_data=0x%x\n",__func__,sensor->ops->ctrl_data);
	
	return result;

}

static int sensor_chip_init(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);	
	struct sensor_operate *ops = sensor_ops[(int)sensor->i2c_id->driver_data];
	int result = 0;
	
	if(ops)
	{
		sensor->ops = ops;
	}
	else
	{
		printk("%s:ops is null,sensor name is %s\n",__func__,sensor->i2c_id->name);
		result = -1;
		goto error;
	}

	if((sensor->type != ops->type) || ((int)sensor->i2c_id->driver_data != ops->id_i2c))
	{
		printk("%s:type or id is different:type=%d,%d,id=%d,%d\n",__func__,sensor->type, ops->type, (int)sensor->i2c_id->driver_data, ops->id_i2c);
		result = -1;
		goto error;
	}
	
	if(!ops->init || !ops->active || !ops->report)
	{
		printk("%s:error:some function is needed\n",__func__);		
		result = -1;
		goto error;
	}

	result = sensor_get_id(sensor->client, &sensor->devid);//get id
	if(result < 0)
	{	
		printk("%s:fail to read %s devid:0x%x\n",__func__, sensor->i2c_id->name, sensor->devid);	
		goto error;
	}
	
	printk("%s:%s:devid=0x%x,ops=0x%p\n",__func__, sensor->i2c_id->name, sensor->devid,sensor->ops);

	result = sensor_initial(sensor->client);	//init sensor
	if(result < 0)
	{	
		printk("%s:fail to init sensor\n",__func__);		
		goto error;
	}

	return 0;

error:
	
	return result;
}

static int sensor_reset_rate(struct i2c_client *client, int rate)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);	
	int result = 0;	
	
	result = sensor->ops->active(client,SENSOR_OFF,rate);
	sensor->ops->init(client);
	result = sensor->ops->active(client,SENSOR_ON,rate);

	return result;
}

static int sensor_get_data(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);	
	int result = 0;
	
	result = sensor->ops->report(client);
	if(result)
		goto error;

	/* set data_ready */
	atomic_set(&sensor->data_ready, 1);
	/*wake up data_ready  work queue*/
	wake_up(&sensor->data_ready_wq);
	
error:		
	return result;
}

#if 0
int sensor_get_cached_data(struct i2c_client* client, char *buffer, int length, struct sensor_axis *axis)
{
    struct sensor_private_data* sensor = (struct sensor_private_data *)i2c_get_clientdata(client);	
    wait_event_interruptible_timeout(sensor->data_ready_wq, 
                                     atomic_read(&(sensor->data_ready) ),
                                     msecs_to_jiffies(1000) );
    if ( 0 == atomic_read(&(sensor->data_ready) ) ) {
        printk("waiting 'data_ready_wq' timed out.");
        goto error;
    }

	
	mutex_lock(&sensor->data_mutex);

	switch(sensor->type)
	{
		case SENSOR_TYPE_ACCEL:
		*axis = sensor->axis;
		break;

		case SENSOR_TYPE_COMPASS:
		memcpy(buffer, sensor->sensor_data, length);
		break;
	}
	
	mutex_unlock(&sensor->data_mutex);
	
    return 0;
	
error:
	return -1;
}
#endif

static void  sensor_delaywork_func(struct work_struct *work)
{
	struct delayed_work *delaywork = container_of(work, struct delayed_work, work);
	struct sensor_private_data *sensor = container_of(delaywork, struct sensor_private_data, delaywork);
	struct i2c_client *client = sensor->client;

	mutex_lock(&sensor->sensor_mutex);	
	if (sensor_get_data(client) < 0) 
		DBG(KERN_ERR "%s: Get data failed\n",__func__);
	
	if(!sensor->pdata->irq_enable)//restart work while polling
	schedule_delayed_work(&sensor->delaywork, msecs_to_jiffies(sensor->pdata->poll_delay_ms));
	//else
	//{
		//if((sensor->ops->trig == IRQF_TRIGGER_LOW) || (sensor->ops->trig == IRQF_TRIGGER_HIGH))
		//enable_irq(sensor->client->irq);
	//}
	mutex_unlock(&sensor->sensor_mutex);
	
	DBG("%s:%s\n",__func__,sensor->i2c_id->name);
}

/*
 * This is a threaded IRQ handler so can access I2C/SPI.  Since all
 * interrupts are clear on read the IRQ line will be reasserted and
 * the physical IRQ will be handled again if another interrupt is
 * asserted while we run - in the normal course of events this is a
 * rare occurrence so we save I2C/SPI reads.  We're also assuming that
 * it's rare to get lots of interrupts firing simultaneously so try to
 * minimise I/O.
 */
static irqreturn_t sensor_interrupt(int irq, void *dev_id)
{
	struct sensor_private_data *sensor = (struct sensor_private_data *)dev_id;

	//use threaded IRQ
	if (sensor_get_data(sensor->client) < 0) 
		DBG(KERN_ERR "%s: Get data failed\n",__func__);
	msleep(sensor->pdata->poll_delay_ms);

	
	//if((sensor->ops->trig == IRQF_TRIGGER_LOW) || (sensor->ops->trig == IRQF_TRIGGER_HIGH))
	//disable_irq_nosync(irq);
	//schedule_delayed_work(&sensor->delaywork, msecs_to_jiffies(sensor->pdata->poll_delay_ms));
	DBG("%s:irq=%d\n",__func__,irq);
	return IRQ_HANDLED;
}


static int sensor_irq_init(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);	
	int result = 0;
	int irq;
	if((sensor->pdata->irq_enable)&&(sensor->ops->trig != SENSOR_UNKNOW_DATA))
	{
		//INIT_DELAYED_WORK(&sensor->delaywork, sensor_delaywork_func);
		if(sensor->pdata->poll_delay_ms < 0)
			sensor->pdata->poll_delay_ms = 30;
		
		result = gpio_request(client->irq, sensor->i2c_id->name);
		if (result)
		{
			printk("%s:fail to request gpio :%d\n",__func__,client->irq);
		}
	
		gpio_pull_updown(client->irq, PullEnable);
		irq = gpio_to_irq(client->irq);
		//result = request_irq(irq, sensor_interrupt, sensor->ops->trig, sensor->ops->name, sensor);
		result = request_threaded_irq(irq, NULL, sensor_interrupt, sensor->ops->trig, sensor->ops->name, sensor);
		if (result) {
			printk(KERN_ERR "%s:fail to request irq = %d, ret = 0x%x\n",__func__, irq, result);	       
			goto error;	       
		}
		client->irq = irq;
		if((sensor->pdata->type == SENSOR_TYPE_GYROSCOPE) || (sensor->pdata->type == SENSOR_TYPE_ACCEL))
		disable_irq_nosync(client->irq);//disable irq
		if(((sensor->pdata->type == SENSOR_TYPE_LIGHT) || (sensor->pdata->type == SENSOR_TYPE_PROXIMITY))&& (!(sensor->ops->trig & IRQF_SHARED)))	
		disable_irq_nosync(client->irq);//disable irq	
		if(((sensor->pdata->type == SENSOR_TYPE_TEMPERATURE) || (sensor->pdata->type == SENSOR_TYPE_PRESSURE))&& (!(sensor->ops->trig & IRQF_SHARED)))		
		disable_irq_nosync(client->irq);//disable irq
		printk("%s:use irq=%d\n",__func__,irq);
	}
	else if(!sensor->pdata->irq_enable)
	{		
		INIT_DELAYED_WORK(&sensor->delaywork, sensor_delaywork_func);
		if(sensor->pdata->poll_delay_ms < 0)
			sensor->pdata->poll_delay_ms = 30;
		
		printk("%s:use polling,delay=%d ms\n",__func__,sensor->pdata->poll_delay_ms);
	}

error:	
	return result;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void sensor_suspend(struct early_suspend *h)
{
	struct sensor_private_data *sensor = 
			container_of(h, struct sensor_private_data, early_suspend);
	
	if(sensor->ops->suspend)
		sensor->ops->suspend(sensor->client);

}

static void sensor_resume(struct early_suspend *h)
{
	struct sensor_private_data *sensor = 
			container_of(h, struct sensor_private_data, early_suspend);

	if(sensor->ops->resume)
		sensor->ops->resume(sensor->client);
}
#endif

static int gsensor_dev_open(struct inode *inode, struct file *file)
{
	//struct sensor_private_data *sensor = g_sensor[SENSOR_TYPE_ACCEL];	
	//struct i2c_client *client = sensor->client;

	int result = 0;


	return result;
}


static int gsensor_dev_release(struct inode *inode, struct file *file)
{
	//struct sensor_private_data *sensor = g_sensor[SENSOR_TYPE_ACCEL];	
	//struct i2c_client *client = sensor->client;

	int result = 0;


	return result;
}

/* ioctl - I/O control */
static long gsensor_dev_ioctl(struct file *file,
			  unsigned int cmd, unsigned long arg)
{
	struct sensor_private_data *sensor = g_sensor[SENSOR_TYPE_ACCEL];
	struct i2c_client *client = sensor->client;
	void __user *argp = (void __user *)arg;
	struct sensor_axis axis = {0};
	char rate;
	int result = 0;

	switch (cmd) {
	case GSENSOR_IOCTL_APP_SET_RATE:
		if (copy_from_user(&rate, argp, sizeof(rate)))
		{
			result = -EFAULT;
			goto error;
		}
		break;
	default:
		break;
	}

	switch (cmd) {
	case GSENSOR_IOCTL_START:	
		DBG("%s:GSENSOR_IOCTL_START start,status=%d\n", __func__,sensor->status_cur);
		mutex_lock(&sensor->operation_mutex);	
		if(++sensor->start_count == 1)
		{
			if(sensor->status_cur == SENSOR_OFF)
			{
				atomic_set(&(sensor->data_ready), 0);
				if ( (result = sensor->ops->active(client, 1, 0) ) < 0 ) {
		        		mutex_unlock(&sensor->operation_mutex);
					printk("%s:fail to active sensor,ret=%d\n",__func__,result);         
					goto error;           
		    		}			
				if(sensor->pdata->irq_enable)
				{
					DBG("%s:enable irq,irq=%d\n",__func__,client->irq);
					enable_irq(client->irq);	//enable irq
				}	
				else
				{
					PREPARE_DELAYED_WORK(&sensor->delaywork, sensor_delaywork_func);
					schedule_delayed_work(&sensor->delaywork, msecs_to_jiffies(sensor->pdata->poll_delay_ms));
				}
				sensor->status_cur = SENSOR_ON;
			}	
		}
	        mutex_unlock(&sensor->operation_mutex);
	        DBG("%s:GSENSOR_IOCTL_START OK\n", __func__);
	        break;

	case GSENSOR_IOCTL_CLOSE:				
	        DBG("%s:GSENSOR_IOCTL_CLOSE start,status=%d\n", __func__,sensor->status_cur);
	        mutex_lock(&sensor->operation_mutex);		
		if(--sensor->start_count == 0)
		{
			if(sensor->status_cur == SENSOR_ON)
			{
				atomic_set(&(sensor->data_ready), 0);
				if ( (result = sensor->ops->active(client, 0, 0) ) < 0 ) {
		                	mutex_unlock(&sensor->operation_mutex);              
					goto error;
		            	}
				
				if(sensor->pdata->irq_enable)
				{				
					DBG("%s:disable irq,irq=%d\n",__func__,client->irq);
					disable_irq_nosync(client->irq);//disable irq
				}
				else
				cancel_delayed_work_sync(&sensor->delaywork);		
				sensor->status_cur = SENSOR_OFF;
		        }
			
			DBG("%s:GSENSOR_IOCTL_CLOSE OK\n", __func__);
		}
		
	        mutex_unlock(&sensor->operation_mutex);	
	        break;

	case GSENSOR_IOCTL_APP_SET_RATE:		
		DBG("%s:GSENSOR_IOCTL_APP_SET_RATE start\n", __func__);		
		mutex_lock(&sensor->operation_mutex);	
		result = sensor_reset_rate(client, rate);
		if (result < 0){
			mutex_unlock(&sensor->operation_mutex);
			goto error;
		}

		sensor->status_cur = SENSOR_ON;
	        mutex_unlock(&sensor->operation_mutex);	
	        DBG("%s:GSENSOR_IOCTL_APP_SET_RATE OK\n", __func__);
		break;
		
	case GSENSOR_IOCTL_GETDATA:
		mutex_lock(&sensor->data_mutex);
		memcpy(&axis, &sensor->axis, sizeof(sensor->axis));	//get data from buffer
		mutex_unlock(&sensor->data_mutex);		
		break;
	default:
		result = -ENOTTY;
	goto error;
	}

	switch (cmd) {
	case GSENSOR_IOCTL_GETDATA:
	        if ( copy_to_user(argp, &axis, sizeof(axis) ) ) {
	            printk("failed to copy sense data to user space.");
				result = -EFAULT;			
				goto error;
	        }		
		DBG("%s:GSENSOR_IOCTL_GETDATA OK\n", __func__);
		break;
	default:
		break;
	}
	
error:
	return result;
}

static ssize_t gsensor_set_orientation_online(struct class *class,
		struct class_attribute *attr, const char *buf, size_t count)
{
	int i=0;
	char orientation[20];
	char *tmp;
	
	struct sensor_private_data *sensor = g_sensor[SENSOR_TYPE_ACCEL];
	struct sensor_platform_data *pdata = sensor->pdata;

	
  	char *p = strstr(buf,"gsensor_class");
	int start = strcspn(p,"{");
	int end = strcspn(p,"}");
	
	strncpy(orientation,p+start,end-start+1);
	tmp = orientation;
	

    	while(strncmp(tmp,"}",1)!=0)
   	 {
    		if((strncmp(tmp,",",1)==0)||(strncmp(tmp,"{",1)==0))
		{
			
			 tmp++;		
			 continue;
		}	
		else if(strncmp(tmp,"-",1)==0)
		{
			pdata->orientation[i++]=-1;
			DBG("i=%d,data=%d\n",i,pdata->orientation[i]);
			 tmp++;
		}		
		else
		{
			pdata->orientation[i++]=tmp[0]-48;		
			DBG("----i=%d,data=%d\n",i,pdata->orientation[i]);	
		}	
		tmp++;
	
						
   	 }

	for(i=0;i<9;i++)
		DBG("i=%d gsensor_info=%d\n",i,pdata->orientation[i]);
	return 0;

}

static CLASS_ATTR(orientation, 0660, NULL, gsensor_set_orientation_online);

static int  gsensor_class_init(void)
{
	int ret ;
	struct sensor_private_data *sensor = g_sensor[SENSOR_TYPE_ACCEL];	
	g_sensor_class[SENSOR_TYPE_ACCEL] = class_create(THIS_MODULE, "gsensor_class");
	ret =  class_create_file(g_sensor_class[SENSOR_TYPE_ACCEL], &class_attr_orientation);
	if (ret)
	{
		printk("%s:Fail to creat class\n",__func__);
		return ret;
	}
	printk("%s:%s\n",__func__,sensor->i2c_id->name);
	return 0;
}



static int compass_dev_open(struct inode *inode, struct file *file)
{
	struct sensor_private_data *sensor = g_sensor[SENSOR_TYPE_COMPASS];
	//struct i2c_client *client = sensor->client;	

	int result = 0;
	int flag = 0;
	flag = atomic_read(&sensor->flags.open_flag);
	if(!flag)
	{	
		atomic_set(&sensor->flags.open_flag, 1);
		wake_up(&sensor->flags.open_wq);
	}

	DBG("%s\n", __func__);
	return result;
}



static int compass_dev_release(struct inode *inode, struct file *file)
{
	struct sensor_private_data *sensor = g_sensor[SENSOR_TYPE_COMPASS];
	//struct i2c_client *client = sensor->client;
	//void __user *argp = (void __user *)arg;
	int result = 0;
	int flag = 0;
	flag = atomic_read(&sensor->flags.open_flag);
	if(flag)
	{
		atomic_set(&sensor->flags.open_flag, 0);
		wake_up(&sensor->flags.open_wq);	
	}
	
	DBG("%s\n", __func__);
	return result;
}



/* ioctl - I/O control */
static long compass_dev_ioctl(struct file *file,
			  unsigned int cmd, unsigned long arg)
{
	struct sensor_private_data *sensor = g_sensor[SENSOR_TYPE_COMPASS];
	//struct i2c_client *client = sensor->client;
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
			atomic_set(&sensor->flags.m_flag, flag);			
			DBG("%s:ECS_IOCTL_APP_SET_MFLAG,flag=%d\n", __func__,flag);
			break;
		case ECS_IOCTL_APP_GET_MFLAG:		
			flag = atomic_read(&sensor->flags.m_flag);
			DBG("%s:ECS_IOCTL_APP_GET_MFLAG,flag=%d\n", __func__,flag);
			break;
		case ECS_IOCTL_APP_SET_AFLAG:	
			atomic_set(&sensor->flags.a_flag, flag);		
			DBG("%s:ECS_IOCTL_APP_SET_AFLAG,flag=%d\n", __func__,flag);
			break;
		case ECS_IOCTL_APP_GET_AFLAG:
			flag = atomic_read(&sensor->flags.a_flag);		
			DBG("%s:ECS_IOCTL_APP_GET_AFLAG,flag=%d\n", __func__,flag);
			break;
		case ECS_IOCTL_APP_SET_MVFLAG:	
			atomic_set(&sensor->flags.mv_flag, flag);		
			DBG("%s:ECS_IOCTL_APP_SET_MVFLAG,flag=%d\n", __func__,flag);
			break;
		case ECS_IOCTL_APP_GET_MVFLAG:		
			flag = atomic_read(&sensor->flags.mv_flag);		
			DBG("%s:ECS_IOCTL_APP_GET_MVFLAG,flag=%d\n", __func__,flag);
			break;
		case ECS_IOCTL_APP_SET_DELAY:
			sensor->flags.delay = flag;
			break;
		case ECS_IOCTL_APP_GET_DELAY:
			flag = sensor->flags.delay;
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

static int gyro_dev_open(struct inode *inode, struct file *file)
{
	//struct sensor_private_data *sensor = g_sensor[SENSOR_TYPE_GYROSCOPE];
	//struct i2c_client *client = sensor->client;

	int result = 0;


	return result;
}


static int gyro_dev_release(struct inode *inode, struct file *file)
{
	//struct sensor_private_data *sensor = g_sensor[SENSOR_TYPE_GYROSCOPE];
	//struct i2c_client *client = sensor->client;

	int result = 0;


	return result;
}


/* ioctl - I/O control */
static long gyro_dev_ioctl(struct file *file,
			  unsigned int cmd, unsigned long arg)
{
	struct sensor_private_data *sensor = g_sensor[SENSOR_TYPE_GYROSCOPE];	
	struct i2c_client *client = sensor->client;	
	void __user *argp = (void __user *)arg;
	int result = 0;
	char rate;
	switch (cmd) {
	case L3G4200D_IOCTL_GET_ENABLE:	
		result = !sensor->status_cur;
		if (copy_to_user(argp, &result, sizeof(result)))
		{
            		printk("%s:failed to copy status to user space.\n",__FUNCTION__);
			return -EFAULT;
		}
		
		DBG("%s :L3G4200D_IOCTL_GET_ENABLE,status=%d\n",__FUNCTION__,result);	
		break;
	case L3G4200D_IOCTL_SET_ENABLE:			
		DBG("%s :L3G4200D_IOCTL_SET_ENABLE,flag=%d\n",__FUNCTION__,*(unsigned int *)argp);
        	mutex_lock(&sensor->operation_mutex);	
		if(*(unsigned int *)argp)
		{
			if(sensor->status_cur == SENSOR_OFF)
			{
				if ( (result = sensor->ops->active(client, 1, ODR100_BW12_5) ) < 0 ) {
	                	mutex_unlock(&sensor->operation_mutex);
				printk("%s:fail to active sensor,ret=%d\n",__func__,result);         
				goto error;           
	            		}			
				if(sensor->pdata->irq_enable)
				{
					DBG("%s:enable irq,irq=%d\n",__func__,client->irq);
					enable_irq(client->irq);	//enable irq
				}	
				else
				{
					PREPARE_DELAYED_WORK(&sensor->delaywork, sensor_delaywork_func);
					schedule_delayed_work(&sensor->delaywork, msecs_to_jiffies(sensor->pdata->poll_delay_ms));
				}
				sensor->status_cur = SENSOR_ON;
			}	
		}
		else
		{
			if(sensor->status_cur == SENSOR_ON)
			{
		            	if ( (result = sensor->ops->active(client, 0, 0) ) < 0 ) {
		                mutex_unlock(&sensor->operation_mutex);              
				goto error;
	            		}
				
				if(sensor->pdata->irq_enable)
				{				
					DBG("%s:disable irq,irq=%d\n",__func__,client->irq);
					disable_irq_nosync(client->irq);//disable irq
				}
				else
				cancel_delayed_work_sync(&sensor->delaywork);		
				sensor->status_cur = SENSOR_OFF;
        		}
		}
	
		result = sensor->status_cur;
		if (copy_to_user(argp, &result, sizeof(result)))
		{
            		printk("%s:failed to copy sense data to user space.\n",__FUNCTION__);
			return -EFAULT;
		}

		mutex_unlock(&sensor->operation_mutex);
        	DBG("%s:L3G4200D_IOCTL_SET_ENABLE OK\n", __func__);
		break;
	case L3G4200D_IOCTL_SET_DELAY:					
		mutex_lock(&sensor->operation_mutex);
		if (copy_from_user(&rate, argp, sizeof(rate)))
		return -EFAULT;
		if(sensor->status_cur == SENSOR_OFF)
		{
			if ( (result = sensor->ops->active(client, 1, rate) ) < 0 ) {
                	mutex_unlock(&sensor->operation_mutex);
			printk("%s:fail to active sensor,ret=%d\n",__func__,result);         
			goto error;           
            		}
			
			if(sensor->pdata->irq_enable)
			{
				DBG("%s:enable irq,irq=%d\n",__func__,client->irq);
				enable_irq(client->irq);	//enable irq
			}	
			else
			{
				PREPARE_DELAYED_WORK(&sensor->delaywork, sensor_delaywork_func);
				schedule_delayed_work(&sensor->delaywork, msecs_to_jiffies(sensor->pdata->poll_delay_ms));
			}
			sensor->status_cur = SENSOR_ON;
		}	
		
		mutex_unlock(&sensor->operation_mutex);
		DBG("%s :L3G4200D_IOCTL_SET_DELAY,rate=%d\n",__FUNCTION__,rate);
		break;

	default:
		printk("%s:error,cmd=0x%x\n",__func__,cmd);
		return -ENOTTY;
	}
	
	DBG("%s:line=%d,cmd=0x%x\n",__func__,__LINE__,cmd);

error:
	return result;
}

static int light_dev_open(struct inode *inode, struct file *file)
{
	//struct sensor_private_data *sensor = g_sensor[SENSOR_TYPE_LIGHT];
	//struct i2c_client *client = sensor->client;	
	int result = 0;	


	return result;
}




static int light_dev_release(struct inode *inode, struct file *file)
{
	//struct sensor_private_data *sensor = g_sensor[SENSOR_TYPE_LIGHT];
	//struct i2c_client *client = sensor->client;	
	int result = 0;


	return result;
}


/* ioctl - I/O control */
static long light_dev_ioctl(struct file *file,
			  unsigned int cmd, unsigned long arg)
{
	struct sensor_private_data *sensor = g_sensor[SENSOR_TYPE_LIGHT];
	struct i2c_client *client = sensor->client;
	unsigned int *argp = (unsigned int *)arg;	
	int result = 0;

	switch(cmd)
	{
		case LIGHTSENSOR_IOCTL_GET_ENABLED:
			*argp = sensor->status_cur;
			break;
		case LIGHTSENSOR_IOCTL_ENABLE:		
			DBG("%s:LIGHTSENSOR_IOCTL_ENABLE start\n", __func__);
			mutex_lock(&sensor->operation_mutex);    
			if(*(unsigned int *)argp)
			{
				if(sensor->status_cur == SENSOR_OFF)
				{
		            		if ( (result = sensor->ops->active(client, SENSOR_ON, 0) ) < 0 ) {
		                	mutex_unlock(&sensor->operation_mutex);
					printk("%s:fail to active sensor,ret=%d\n",__func__,result);         
					goto error;           
		            		}	
					if(sensor->pdata->irq_enable)
					{
						if(!(sensor->ops->trig & IRQF_SHARED))
						{
							DBG("%s:enable irq,irq=%d\n",__func__,client->irq);
							enable_irq(client->irq);	//enable irq
						}
					}	
					else
					{
						PREPARE_DELAYED_WORK(&sensor->delaywork, sensor_delaywork_func);
						schedule_delayed_work(&sensor->delaywork, msecs_to_jiffies(sensor->pdata->poll_delay_ms));
					}
					
					sensor->status_cur = SENSOR_ON;
				}	
			}
			else
			{
				if(sensor->status_cur == SENSOR_ON)
				{
		            		if ( (result = sensor->ops->active(client, SENSOR_OFF, 0) ) < 0 ) {
		                	mutex_unlock(&sensor->operation_mutex);              
					goto error;
		            		}
					
					if(sensor->pdata->irq_enable)
					{				
						if(!(sensor->ops->trig & IRQF_SHARED))
						{
							DBG("%s:disable irq,irq=%d\n",__func__,client->irq);
							disable_irq_nosync(client->irq);//disable irq
						}
					}
					else
					cancel_delayed_work_sync(&sensor->delaywork);	
					
					sensor->status_cur = SENSOR_OFF;
	        		}
			}
			mutex_unlock(&sensor->operation_mutex);
	        	DBG("%s:LIGHTSENSOR_IOCTL_ENABLE OK\n", __func__);
			break;
		
		default:
			break;
	}
	
error:
	return result;
}


static int proximity_dev_open(struct inode *inode, struct file *file)
{
	//struct sensor_private_data *sensor = g_sensor[SENSOR_TYPE_PROXIMITY];
	//struct i2c_client *client = sensor->client;	
	int result = 0;


	return result;
}


static int proximity_dev_release(struct inode *inode, struct file *file)
{
	//struct sensor_private_data *sensor = g_sensor[SENSOR_TYPE_PROXIMITY];
	//struct i2c_client *client = sensor->client;	
	int result = 0;


	return result;
}


/* ioctl - I/O control */
static long proximity_dev_ioctl(struct file *file,
			  unsigned int cmd, unsigned long arg)
{
	struct sensor_private_data *sensor = g_sensor[SENSOR_TYPE_PROXIMITY];
	struct i2c_client *client = sensor->client;	
	unsigned int *argp = (unsigned int *)arg;	
	int result = 0;
	switch(cmd)
	{
		case PSENSOR_IOCTL_GET_ENABLED:
			*argp = sensor->status_cur;
			break;
		case PSENSOR_IOCTL_ENABLE:		
			DBG("%s:PSENSOR_IOCTL_ENABLE start\n", __func__);
			mutex_lock(&sensor->operation_mutex);    
			if(*(unsigned int *)argp)
			{
				if(sensor->status_cur == SENSOR_OFF)
				{
					if ( (result = sensor->ops->active(client, SENSOR_ON, 0) ) < 0 ) {
					mutex_unlock(&sensor->operation_mutex);
					printk("%s:fail to active sensor,ret=%d\n",__func__,result);         
					goto error;           
					}
					
					if(sensor->pdata->irq_enable)
					{
						if(!(sensor->ops->trig & IRQF_SHARED))
						{
							DBG("%s:enable irq,irq=%d\n",__func__,client->irq);
							enable_irq(client->irq);	//enable irq
						}
					}	
					else
					{
						PREPARE_DELAYED_WORK(&sensor->delaywork, sensor_delaywork_func);
						schedule_delayed_work(&sensor->delaywork, msecs_to_jiffies(sensor->pdata->poll_delay_ms));
					}
					
					sensor->status_cur = SENSOR_ON;
				}	
			}
			else
			{
				if(sensor->status_cur == SENSOR_ON)
				{
		            		if ( (result = sensor->ops->active(client, SENSOR_OFF, 0) ) < 0 ) {
		                	mutex_unlock(&sensor->operation_mutex);              
					goto error;
					}
					if(sensor->pdata->irq_enable)
					{				
						if(!(sensor->ops->trig & IRQF_SHARED))
						{
							DBG("%s:disable irq,irq=%d\n",__func__,client->irq);
							disable_irq_nosync(client->irq);//disable irq
						}
					}
					else
					cancel_delayed_work_sync(&sensor->delaywork);		
					sensor->status_cur = SENSOR_OFF;
	        		}
			}
			mutex_unlock(&sensor->operation_mutex);
			DBG("%s:PSENSOR_IOCTL_ENABLE OK\n", __func__);
			break;
		
		default:
			break;
	}
	
error:
	return result;
}

static int temperature_dev_open(struct inode *inode, struct file *file)
{
	//struct sensor_private_data *sensor = g_sensor[SENSOR_TYPE_TEMPERATURE];
	//struct i2c_client *client = sensor->client;

	int result = 0;


	return result;
}


static int temperature_dev_release(struct inode *inode, struct file *file)
{
	//struct sensor_private_data *sensor = g_sensor[SENSOR_TYPE_TEMPERATURE];
	//struct i2c_client *client = sensor->client;

	int result = 0;


	return result;
}


/* ioctl - I/O control */
static long temperature_dev_ioctl(struct file *file,
			  unsigned int cmd, unsigned long arg)
{
	struct sensor_private_data *sensor = g_sensor[SENSOR_TYPE_TEMPERATURE];
	struct i2c_client *client = sensor->client;
	unsigned int *argp = (unsigned int *)arg;	
	int result = 0;

	switch(cmd)
	{
		case TEMPERATURE_IOCTL_GET_ENABLED:
			*argp = sensor->status_cur;
			break;
		case TEMPERATURE_IOCTL_ENABLE:		
			DBG("%s:LIGHTSENSOR_IOCTL_ENABLE start\n", __func__);
			mutex_lock(&sensor->operation_mutex);	 
			if(*(unsigned int *)argp)
			{
				if(sensor->status_cur == SENSOR_OFF)
				{
					if ( (result = sensor->ops->active(client, SENSOR_ON, 0) ) < 0 ) {
					mutex_unlock(&sensor->operation_mutex);
					printk("%s:fail to active sensor,ret=%d\n",__func__,result);	     
					goto error;	      
					}	
					if(sensor->pdata->irq_enable)
					{
						if(!(sensor->ops->trig & IRQF_SHARED))
						{
							DBG("%s:enable irq,irq=%d\n",__func__,client->irq);
							enable_irq(client->irq);	//enable irq
						}
					}	
					else
					{
						PREPARE_DELAYED_WORK(&sensor->delaywork, sensor_delaywork_func);
						schedule_delayed_work(&sensor->delaywork, msecs_to_jiffies(sensor->pdata->poll_delay_ms));
					}
					
					sensor->status_cur = SENSOR_ON;
				}	
			}
			else
			{
				if(sensor->status_cur == SENSOR_ON)
				{
					if ( (result = sensor->ops->active(client, SENSOR_OFF, 0) ) < 0 ) {
					mutex_unlock(&sensor->operation_mutex); 	     
					goto error;
					}
					
					if(sensor->pdata->irq_enable)
					{				
						if(!(sensor->ops->trig & IRQF_SHARED))
						{
							DBG("%s:disable irq,irq=%d\n",__func__,client->irq);
							disable_irq_nosync(client->irq);//disable irq
						}
					}
					else
					cancel_delayed_work_sync(&sensor->delaywork);	
					
					sensor->status_cur = SENSOR_OFF;
				}
			}
			mutex_unlock(&sensor->operation_mutex);
			DBG("%s:LIGHTSENSOR_IOCTL_ENABLE OK\n", __func__);
			break;
		
		default:
			break;
	}
	
error:
	return result;
}


static int pressure_dev_open(struct inode *inode, struct file *file)
{
	//struct sensor_private_data *sensor = g_sensor[SENSOR_TYPE_PRESSURE];
	//struct i2c_client *client = sensor->client;

	int result = 0;


	return result;
}


static int pressure_dev_release(struct inode *inode, struct file *file)
{
	//struct sensor_private_data *sensor = g_sensor[SENSOR_TYPE_PRESSURE];
	//struct i2c_client *client = sensor->client;

	int result = 0;


	return result;
}


/* ioctl - I/O control */
static long pressure_dev_ioctl(struct file *file,
			  unsigned int cmd, unsigned long arg)
{
	struct sensor_private_data *sensor = g_sensor[SENSOR_TYPE_PRESSURE];
	struct i2c_client *client = sensor->client;
	unsigned int *argp = (unsigned int *)arg;	
	int result = 0;

	switch(cmd)
	{
		case PRESSURE_IOCTL_GET_ENABLED:
			*argp = sensor->status_cur;
			break;
		case PRESSURE_IOCTL_ENABLE:		
			DBG("%s:LIGHTSENSOR_IOCTL_ENABLE start\n", __func__);
			mutex_lock(&sensor->operation_mutex);	 
			if(*(unsigned int *)argp)
			{
				if(sensor->status_cur == SENSOR_OFF)
				{
					if ( (result = sensor->ops->active(client, SENSOR_ON, 0) ) < 0 ) {
					mutex_unlock(&sensor->operation_mutex);
					printk("%s:fail to active sensor,ret=%d\n",__func__,result);	     
					goto error;	      
					}	
					if(sensor->pdata->irq_enable)
					{
						if(!(sensor->ops->trig & IRQF_SHARED))
						{
							DBG("%s:enable irq,irq=%d\n",__func__,client->irq);
							enable_irq(client->irq);	//enable irq
						}
					}	
					else
					{
						PREPARE_DELAYED_WORK(&sensor->delaywork, sensor_delaywork_func);
						schedule_delayed_work(&sensor->delaywork, msecs_to_jiffies(sensor->pdata->poll_delay_ms));
					}
					
					sensor->status_cur = SENSOR_ON;
				}	
			}
			else
			{
				if(sensor->status_cur == SENSOR_ON)
				{
					if ( (result = sensor->ops->active(client, SENSOR_OFF, 0) ) < 0 ) {
					mutex_unlock(&sensor->operation_mutex); 	     
					goto error;
					}
					
					if(sensor->pdata->irq_enable)
					{				
						if(!(sensor->ops->trig & IRQF_SHARED))
						{
							DBG("%s:disable irq,irq=%d\n",__func__,client->irq);
							disable_irq_nosync(client->irq);//disable irq
						}
					}
					else
					cancel_delayed_work_sync(&sensor->delaywork);	
					
					sensor->status_cur = SENSOR_OFF;
				}
			}
			mutex_unlock(&sensor->operation_mutex);
			DBG("%s:LIGHTSENSOR_IOCTL_ENABLE OK\n", __func__);
			break;
		
		default:
			break;
	}
	
error:
	return result;
}




static int sensor_misc_device_register(struct sensor_private_data *sensor, int type)
{
	int result = 0;
	
	switch(type)
	{
		case SENSOR_TYPE_ACCEL:
			if(!sensor->ops->misc_dev)
			{
				sensor->fops.owner = THIS_MODULE;
				sensor->fops.unlocked_ioctl = gsensor_dev_ioctl;
				sensor->fops.open = gsensor_dev_open;
				sensor->fops.release = gsensor_dev_release;

				sensor->miscdev.minor = MISC_DYNAMIC_MINOR;
				sensor->miscdev.name = "mma8452_daemon";
				sensor->miscdev.fops = &sensor->fops;
			}
			else
			{
				memcpy(&sensor->miscdev, sensor->ops->misc_dev, sizeof(*sensor->ops->misc_dev));

			}
				
			break;

		case SENSOR_TYPE_COMPASS:			
			if(!sensor->ops->misc_dev)
			{
				sensor->fops.owner = THIS_MODULE;
				sensor->fops.unlocked_ioctl = compass_dev_ioctl;
				sensor->fops.open = compass_dev_open;
				sensor->fops.release = compass_dev_release;

				sensor->miscdev.minor = MISC_DYNAMIC_MINOR;
				sensor->miscdev.name = "compass";
				sensor->miscdev.fops = &sensor->fops;
			}
			else
			{
				memcpy(&sensor->miscdev, sensor->ops->misc_dev, sizeof(*sensor->ops->misc_dev));

			}

			break;

		case SENSOR_TYPE_GYROSCOPE:			
			if(!sensor->ops->misc_dev)
			{
				sensor->fops.owner = THIS_MODULE;
				sensor->fops.unlocked_ioctl = gyro_dev_ioctl;
				sensor->fops.open = gyro_dev_open;
				sensor->fops.release = gyro_dev_release;

				sensor->miscdev.minor = MISC_DYNAMIC_MINOR;
				sensor->miscdev.name = "gyrosensor";
				sensor->miscdev.fops = &sensor->fops;
			}
			else
			{
				memcpy(&sensor->miscdev, sensor->ops->misc_dev, sizeof(*sensor->ops->misc_dev));

			}
			
			break;

		case SENSOR_TYPE_LIGHT:
			if(!sensor->ops->misc_dev)
			{
				sensor->fops.owner = THIS_MODULE;
				sensor->fops.unlocked_ioctl = light_dev_ioctl;
				sensor->fops.open = light_dev_open;
				sensor->fops.release = light_dev_release;

				sensor->miscdev.minor = MISC_DYNAMIC_MINOR;
				sensor->miscdev.name = "lightsensor";
				sensor->miscdev.fops = &sensor->fops;
			}	
			else
			{
				memcpy(&sensor->miscdev, sensor->ops->misc_dev, sizeof(*sensor->ops->misc_dev));

			}
			break;
		
		case SENSOR_TYPE_PROXIMITY:
			if(!sensor->ops->misc_dev)
			{
				sensor->fops.owner = THIS_MODULE;
				sensor->fops.unlocked_ioctl = proximity_dev_ioctl;
				sensor->fops.open = proximity_dev_open;
				sensor->fops.release = proximity_dev_release;

				sensor->miscdev.minor = MISC_DYNAMIC_MINOR;
				sensor->miscdev.name = "psensor";
				sensor->miscdev.fops = &sensor->fops;
			}	
			else
			{
				memcpy(&sensor->miscdev, sensor->ops->misc_dev, sizeof(*sensor->ops->misc_dev));

			}
			break;

		case SENSOR_TYPE_TEMPERATURE:
			if(!sensor->ops->misc_dev)
			{
				sensor->fops.owner = THIS_MODULE;
				sensor->fops.unlocked_ioctl = temperature_dev_ioctl;
				sensor->fops.open = temperature_dev_open;
				sensor->fops.release = temperature_dev_release;

				sensor->miscdev.minor = MISC_DYNAMIC_MINOR;
				sensor->miscdev.name = "temperature";
				sensor->miscdev.fops = &sensor->fops;
			}	
			else
			{
				memcpy(&sensor->miscdev, sensor->ops->misc_dev, sizeof(*sensor->ops->misc_dev));

			}
				
			break;

		case SENSOR_TYPE_PRESSURE:
			if(!sensor->ops->misc_dev)
			{
				sensor->fops.owner = THIS_MODULE;
				sensor->fops.unlocked_ioctl = pressure_dev_ioctl;
				sensor->fops.open = pressure_dev_open;
				sensor->fops.release = pressure_dev_release;

				sensor->miscdev.minor = MISC_DYNAMIC_MINOR;
				sensor->miscdev.name = "pressure";
				sensor->miscdev.fops = &sensor->fops;
			}	
			else
			{
				memcpy(&sensor->miscdev, sensor->ops->misc_dev, sizeof(*sensor->ops->misc_dev));

			}
				
			break;

		default:
			printk("%s:unknow sensor type=%d\n",__func__,type);
			result = -1;
			goto error;
	}
			
	sensor->miscdev.parent = &sensor->client->dev;
	result = misc_register(&sensor->miscdev);
	if (result < 0) {
		dev_err(&sensor->client->dev,
			"fail to register misc device %s\n", sensor->miscdev.name);
		goto error;
	}
	
	printk("%s:miscdevice: %s\n",__func__,sensor->miscdev.name);

error:	
	
	return result;

}

int sensor_register_slave(int type,struct i2c_client *client,
			struct sensor_platform_data *slave_pdata,
			struct sensor_operate *(*get_sensor_ops)(void))
{
	int result = 0;
	struct sensor_operate *ops = get_sensor_ops();
	if((ops->id_i2c >= SENSOR_NUM_ID) || (ops->id_i2c <= ID_INVALID))
	{	
		printk("%s:%s id is error %d\n", __func__, ops->name, ops->id_i2c);
		return -1;	
	}
	sensor_ops[ops->id_i2c] = ops;
	printk("%s:%s,id=%d\n",__func__,sensor_ops[ops->id_i2c]->name, ops->id_i2c);
	return result;
}


int sensor_unregister_slave(int type,struct i2c_client *client,
			struct sensor_platform_data *slave_pdata,
			struct sensor_operate *(*get_sensor_ops)(void))
{
	int result = 0;
	struct sensor_operate *ops = get_sensor_ops();
	if((ops->id_i2c >= SENSOR_NUM_ID) || (ops->id_i2c <= ID_INVALID))
	{	
		printk("%s:%s id is error %d\n", __func__, ops->name, ops->id_i2c);
		return -1;	
	}
	printk("%s:%s,id=%d\n",__func__,sensor_ops[ops->id_i2c]->name, ops->id_i2c);
	sensor_ops[ops->id_i2c] = NULL;	
	return result;
}


int sensor_probe(struct i2c_client *client, const struct i2c_device_id *devid)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);
	struct sensor_platform_data *pdata;
	int result = 0;
	int type = 0;
	
	dev_info(&client->adapter->dev, "%s: %s,0x%x\n", __func__, devid->name,(unsigned int)client);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		result = -ENODEV;
		goto out_no_free;
	}

	pdata = client->dev.platform_data;
	if (!pdata) {
		dev_err(&client->adapter->dev,
			"Missing platform data for slave %s\n", devid->name);
		result = -EFAULT;
		goto out_no_free;
	}

	sensor = kzalloc(sizeof(*sensor), GFP_KERNEL);
	if (!sensor) {
		result = -ENOMEM;
		goto out_no_free;
	}

	type= pdata->type;	
	
	if((type >= SENSOR_NUM_TYPES) || (type <= SENSOR_TYPE_NULL))
	{	
		dev_err(&client->adapter->dev, "sensor type is error %d\n", pdata->type);
		result = -EFAULT;
		goto out_no_free;	
	}

	if(((int)devid->driver_data >= SENSOR_NUM_ID) || ((int)devid->driver_data <= ID_INVALID))
	{	
		dev_err(&client->adapter->dev, "sensor id is error %d\n", (int)devid->driver_data);
		result = -EFAULT;
		goto out_no_free;	
	}
	
	i2c_set_clientdata(client, sensor);
	sensor->client = client;	
	sensor->pdata = pdata;	
	sensor->type = type;
	sensor->i2c_id = (struct i2c_device_id *)devid;

	if (pdata->init_platform_hw) {
		result = pdata->init_platform_hw();
		if (result < 0)
			goto out_free_memory;
	}

	if(pdata->reset_pin)
	gpio_request(pdata->reset_pin,"sensor_reset_pin");

	if(pdata->power_pin)
	gpio_request(pdata->power_pin,"sensor_power_pin");
		
	memset(&(sensor->axis), 0, sizeof(struct sensor_axis) );
	atomic_set(&(sensor->data_ready), 0);
	init_waitqueue_head(&(sensor->data_ready_wq));
	mutex_init(&sensor->data_mutex);	
	mutex_init(&sensor->operation_mutex);	
	mutex_init(&sensor->sensor_mutex);
	mutex_init(&sensor->i2c_mutex);

	/* As default, report all information */
	atomic_set(&sensor->flags.m_flag, 1);
	atomic_set(&sensor->flags.a_flag, 1);
	atomic_set(&sensor->flags.mv_flag, 1);			
	atomic_set(&sensor->flags.open_flag, 0);
	atomic_set(&sensor->flags.debug_flag, 0);
	init_waitqueue_head(&sensor->flags.open_wq);
	sensor->flags.delay = 100;

	sensor->status_cur = SENSOR_OFF;
	sensor->axis.x = 0;
	sensor->axis.y = 0;
	sensor->axis.z = 0;
	
	result = sensor_chip_init(sensor->client);
	if(result < 0)
		goto out_free_memory;
	
	sensor->input_dev = input_allocate_device();
	if (!sensor->input_dev) {
		result = -ENOMEM;
		dev_err(&client->dev,
			"Failed to allocate input device %s\n", sensor->input_dev->name);
		goto out_free_memory;
	}	

	switch(type)
	{
		case SENSOR_TYPE_ACCEL:	
			sensor->input_dev->name = "gsensor";
			set_bit(EV_ABS, sensor->input_dev->evbit);
			/* x-axis acceleration */
			input_set_abs_params(sensor->input_dev, ABS_X, sensor->ops->range[0], sensor->ops->range[1], 0, 0); //2g full scale range
			/* y-axis acceleration */
			input_set_abs_params(sensor->input_dev, ABS_Y, sensor->ops->range[0], sensor->ops->range[1], 0, 0); //2g full scale range
			/* z-axis acceleration */
			input_set_abs_params(sensor->input_dev, ABS_Z, sensor->ops->range[0], sensor->ops->range[1], 0, 0); //2g full scale range
			break;		
		case SENSOR_TYPE_COMPASS:	
			sensor->input_dev->name = "compass";		
			/* Setup input device */
			set_bit(EV_ABS, sensor->input_dev->evbit);
			/* yaw (0, 360) */
			input_set_abs_params(sensor->input_dev, ABS_RX, 0, 23040, 0, 0);
			/* pitch (-180, 180) */
			input_set_abs_params(sensor->input_dev, ABS_RY, -11520, 11520, 0, 0);
			/* roll (-90, 90) */
			input_set_abs_params(sensor->input_dev, ABS_RZ, -5760, 5760, 0, 0);
			/* x-axis acceleration (720 x 8G) */
			input_set_abs_params(sensor->input_dev, ABS_X, -5760, 5760, 0, 0);
			/* y-axis acceleration (720 x 8G) */
			input_set_abs_params(sensor->input_dev, ABS_Y, -5760, 5760, 0, 0);
			/* z-axis acceleration (720 x 8G) */
			input_set_abs_params(sensor->input_dev, ABS_Z, -5760, 5760, 0, 0);
			/* status of magnetic sensor */
			input_set_abs_params(sensor->input_dev, ABS_RUDDER, -32768, 3, 0, 0);
			/* status of acceleration sensor */
			input_set_abs_params(sensor->input_dev, ABS_WHEEL, -32768, 3, 0, 0);
			/* x-axis of raw magnetic vector (-4096, 4095) */
			input_set_abs_params(sensor->input_dev, ABS_HAT0X, -20480, 20479, 0, 0);
			/* y-axis of raw magnetic vector (-4096, 4095) */
			input_set_abs_params(sensor->input_dev, ABS_HAT0Y, -20480, 20479, 0, 0);
			/* z-axis of raw magnetic vector (-4096, 4095) */
			input_set_abs_params(sensor->input_dev, ABS_BRAKE, -20480, 20479, 0, 0);
			break;		
		case SENSOR_TYPE_GYROSCOPE:
			sensor->input_dev->name = "gyro";
			/* x-axis acceleration */
			input_set_capability(sensor->input_dev, EV_REL, REL_RX);
			input_set_abs_params(sensor->input_dev, ABS_RX, sensor->ops->range[0], sensor->ops->range[1], 0, 0); 
			/* y-axis acceleration */	
			input_set_capability(sensor->input_dev, EV_REL, REL_RY);
			input_set_abs_params(sensor->input_dev, ABS_RY, sensor->ops->range[0], sensor->ops->range[1], 0, 0); 
			/* z-axis acceleration */
			input_set_capability(sensor->input_dev, EV_REL, REL_RZ);
			input_set_abs_params(sensor->input_dev, ABS_RZ, sensor->ops->range[0], sensor->ops->range[1], 0, 0); 
			break;
		case SENSOR_TYPE_LIGHT:
			sensor->input_dev->name = "lightsensor-level";
			set_bit(EV_ABS, sensor->input_dev->evbit);
			input_set_abs_params(sensor->input_dev, ABS_MISC, sensor->ops->range[0], sensor->ops->range[1], 0, 0);			
			input_set_abs_params(sensor->input_dev, ABS_TOOL_WIDTH ,  sensor->ops->brightness[0],sensor->ops->brightness[1], 0, 0);
			break;
		case SENSOR_TYPE_PROXIMITY:
			sensor->input_dev->name = "proximity";	
			set_bit(EV_ABS, sensor->input_dev->evbit);
			input_set_abs_params(sensor->input_dev, ABS_DISTANCE, sensor->ops->range[0], sensor->ops->range[1], 0, 0);
			break;
		case SENSOR_TYPE_TEMPERATURE:				
			sensor->input_dev->name = "temperature";
			set_bit(EV_ABS, sensor->input_dev->evbit);		
			input_set_abs_params(sensor->input_dev, ABS_THROTTLE, sensor->ops->range[0], sensor->ops->range[1], 0, 0);
			break;
		case SENSOR_TYPE_PRESSURE:				
			sensor->input_dev->name = "pressure";
			set_bit(EV_ABS, sensor->input_dev->evbit);		
			input_set_abs_params(sensor->input_dev, ABS_PRESSURE, sensor->ops->range[0], sensor->ops->range[1], 0, 0);
			break;
		default:
			printk("%s:unknow sensor type=%d\n",__func__,type);
			break;

	}
	sensor->input_dev->dev.parent = &client->dev;

	result = input_register_device(sensor->input_dev);
	if (result) {
		dev_err(&client->dev,
			"Unable to register input device %s\n", sensor->input_dev->name);
		goto out_input_register_device_failed;
	}

	result = sensor_irq_init(sensor->client);
	if (result) {
		dev_err(&client->dev,
			"fail to init sensor irq,ret=%d\n",result);
		goto out_input_register_device_failed;
	}

	
	sensor->miscdev.parent = &client->dev;
	result = sensor_misc_device_register(sensor, type);
	if (result) {
		dev_err(&client->dev,
			"fail to register misc device %s\n", sensor->miscdev.name);
		goto out_misc_device_register_device_failed;
	}
	
	g_sensor[type] = sensor;

	if((type == SENSOR_TYPE_ACCEL) && (sensor->pdata->factory))	//only support  setting gsensor orientation online now	
	{
		result = gsensor_class_init();
		if (result) {
			dev_err(&client->dev,
				"fail to register misc device %s\n", sensor->i2c_id->name);
			goto out_misc_device_register_device_failed;
		}
	}	
	
#ifdef CONFIG_HAS_EARLYSUSPEND
	if((sensor->ops->suspend) && (sensor->ops->resume))
	{
		sensor->early_suspend.suspend = sensor_suspend;
		sensor->early_suspend.resume = sensor_resume;
		sensor->early_suspend.level = 0x02;
		register_early_suspend(&sensor->early_suspend);
	}
#endif

	printk("%s:initialized ok,sensor name:%s,type:%d,id=%d\n\n",__func__,sensor->ops->name,type,(int)sensor->i2c_id->driver_data);

	return result;
	
out_misc_device_register_device_failed:
	input_unregister_device(sensor->input_dev);	
out_input_register_device_failed:
	input_free_device(sensor->input_dev);	
out_free_memory:
	kfree(sensor);
out_no_free:
	dev_err(&client->adapter->dev, "%s failed %d\n\n", __func__, result);
	return result;

}

static void sensor_shut_down(struct i2c_client *client)
{
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);
	if((sensor->ops->suspend) && (sensor->ops->resume))		
		unregister_early_suspend(&sensor->early_suspend);
	DBG("%s:%s\n",__func__,sensor->i2c_id->name);
#endif
}

static int sensor_remove(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);
	int result = 0;
	
	cancel_delayed_work_sync(&sensor->delaywork);
	misc_deregister(&sensor->miscdev);
	input_unregister_device(sensor->input_dev);	
	input_free_device(sensor->input_dev);	
	kfree(sensor);
#ifdef CONFIG_HAS_EARLYSUSPEND
	if((sensor->ops->suspend) && (sensor->ops->resume))
		unregister_early_suspend(&sensor->early_suspend);
#endif  
	return result;
}

static const struct i2c_device_id sensor_id[] = {
	/*gsensor*/
	{"gsensor", ACCEL_ID_ALL},
	{"gs_mma8452", ACCEL_ID_MMA845X},	
	{"gs_kxtik", ACCEL_ID_KXTIK},	
	{"gs_kxtj9", ACCEL_ID_KXTJ9},
	{"gs_lis3dh", ACCEL_ID_LIS3DH},
	{"gs_mma7660", ACCEL_ID_MMA7660},
	{"gs_mxc6225", ACCEL_ID_MXC6225},
	/*compass*/
	{"compass", COMPASS_ID_ALL},
	{"ak8975", COMPASS_ID_AK8975},	
	{"ak8963", COMPASS_ID_AK8963},
	{"ak09911", COMPASS_ID_AK09911},
	{"mmc314x", COMPASS_ID_MMC314X},
	/*gyroscope*/
	{"gyro", GYRO_ID_ALL},	
	{"l3g4200d_gyro", GYRO_ID_L3G4200D},
	{"l3g20d_gyro", GYRO_ID_L3G20D},
	{"ewtsa_gyro", GYRO_ID_EWTSA},
	{"k3g", GYRO_ID_K3G},
	/*light sensor*/
	{"lightsensor", LIGHT_ID_ALL},	
	{"light_cm3217", LIGHT_ID_CM3217},
	{"light_cm3232", LIGHT_ID_CM3232},
	{"light_al3006", LIGHT_ID_AL3006},
	{"ls_stk3171", LIGHT_ID_STK3171},
	{"ls_isl29023", LIGHT_ID_ISL29023},
	{"ls_ap321xx", LIGHT_ID_AP321XX},
	{"ls_photoresistor", LIGHT_ID_PHOTORESISTOR},
	{"ls_us5152", LIGHT_ID_US5152},
	/*proximity sensor*/
	{"psensor", PROXIMITY_ID_ALL},
	{"proximity_al3006", PROXIMITY_ID_AL3006},	
	{"ps_stk3171", PROXIMITY_ID_STK3171},
	{"ps_ap321xx", PROXIMITY_ID_AP321XX},
	
	/*temperature*/
	{"temperature", TEMPERATURE_ID_ALL},	
	{"tmp_ms5607", TEMPERATURE_ID_MS5607},

	/*pressure*/
	{"pressure", PRESSURE_ID_ALL},
	{"pr_ms5607", PRESSURE_ID_MS5607},
	
	{},
};


static struct i2c_driver sensor_driver = {
	.probe = sensor_probe,
	.remove = sensor_remove,
	.shutdown = sensor_shut_down,
	.id_table = sensor_id,
	.driver = {
		   .owner = THIS_MODULE,
		   .name = "sensors",
		   },
};

static int __init sensor_init(void)
{
	int res = i2c_add_driver(&sensor_driver);	
	struct proc_dir_entry *sensor_proc_entry;	
	pr_info("%s: Probe name %s\n", __func__, sensor_driver.driver.name);
	if (res)
		pr_err("%s failed\n", __func__);
	
	sensor_proc_entry = proc_create("driver/sensor_dbg", 0660, NULL, &sensor_proc_fops); 
	printk("%s\n", SENSOR_VERSION_AND_TIME);
	return res;
}

static void __exit sensor_exit(void)
{
	pr_info("%s\n", __func__);
	i2c_del_driver(&sensor_driver);
}

late_initcall(sensor_init);
module_exit(sensor_exit);

MODULE_AUTHOR("ROCKCHIP Corporation:lw@rock-chips.com");
MODULE_DESCRIPTION("User space character device interface for sensors");
MODULE_LICENSE("GPL");

