/* Lite-On LTR-558ALS Linux Driver 
 * 
 * Copyright (C) 2011 Lite-On Technology Corp (Singapore) 
 *  
 * This program is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. 
 * 
 */ 
 

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/fs.h>
#include <linux/i2c-dev.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/mm.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/sysctl.h>
#include <linux/input.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/earlysuspend.h>
#include <linux/platform_device.h>
#include <asm/uaccess.h>
#include <linux/slab.h>
#include <linux/wakelock.h>
#include <linux/sensor/ltr558_pls.h>

static int LTR558_DEVICE_IC = 0;
static int ltr558_probed = 0;

static int calibrate_high_ltr558 = 0;	//add for calibrate
static int calibrate_low_ltr558 = 0;
static int calibrate_status_ltr558 = 0;	// add for calibrate
//static int ps_opened_ltr558; 
static int als_opened_ltr558; 
//static struct work_struct irq_workqueue; 
static int ps_data_changed_ltr558; 
static int als_data_changed_ltr558; 
static int ps_active_ltr558; 
static int als_active_ltr558; 
 
static int ps_gainrange_ltr558; 
static int als_gainrange_ltr558; 
 
static int final_prox_val_ltr558; 
static int final_lux_val_ltr558 =0; 
 
static int ltr558_irq; 
static unsigned char suspend_flag_ltr558=0; //0: sleep out; 1: sleep in
static DECLARE_WAIT_QUEUE_HEAD(ps_waitqueue_ltr558); 
static DECLARE_WAIT_QUEUE_HEAD(als_waitqueue_ltr558); 
 

static ltr558_pls_struct *the_data_ltr558 = NULL; 
static struct i2c_client *this_client = NULL;
static struct i2c_client *this_client_ltr558 = NULL;
static int ltr558_als_read(int gainrange);
static int ltr558_ps_read(void);
static ssize_t ltr558_show_suspend(struct device* cd,struct device_attribute *attr, char* buf);
static ssize_t ltr558_store_suspend(struct device* cd, struct device_attribute *attr,const char* buf, size_t len);
static ssize_t ltr558_show_version(struct device* cd,struct device_attribute *attr, char* buf);
static void ltr558_early_suspend(struct early_suspend *handler);
static void ltr558_early_resume(struct early_suspend *handler);

#define CMD_CLR_PS_ALS_INT	0xE7 
 
static int pls_rx_data(char *buf, int len) 
{ 

	int ret;

	struct i2c_msg msgs[] = {
		{
			.addr	= this_client->addr,
			.flags	= 0,
			.len	= 1,
			.buf	= buf,
		},
		{
			.addr	= this_client->addr,
			.flags	= I2C_M_RD,
			.len	= len,
			.buf	= buf,
		}
	};

	ret = i2c_transfer(this_client->adapter, msgs, 2);
    	if (ret < 0)
            printk(KERN_ERR "%s i2c read error: %d\n", __func__, ret);


	return ret;
} 
static int i2c_read_reg(unsigned char addr, unsigned char *data)
{
	int ret;
	unsigned char buf;

	buf=addr;
	ret = pls_rx_data(&buf, 1);
	*data = buf;

	return ret;
}
/*  
 * ######### 
 * ## I2C ## 
 * ######### 
 */ 
 
// I2C Read 
static int ltr558_pls_rx_data(char *buf, int len) 
{ 

	int ret;

	struct i2c_msg msgs[] = {
		{
			.addr	= this_client_ltr558->addr,
			.flags	= 0,
			.len	= 1,
			.buf	= buf,
		},
		{
			.addr	= this_client_ltr558->addr,
			.flags	= I2C_M_RD,
			.len	= len,
			.buf	= buf,
		}
	};

	ret = i2c_transfer(this_client_ltr558->adapter, msgs, 2);
    	if (ret < 0)
            printk(KERN_ERR "%s i2c read error: %d\n", __func__, ret);


	return ret;
} 
static int ltr558_i2c_read_reg(unsigned char addr, unsigned char *data)
{
	int ret;
	unsigned char buf;

	buf=addr;
	ret = ltr558_pls_rx_data(&buf, 1);
	*data = buf;

	return ret;
}
// I2C Write 
static int ltr558_pls_tx_data(char *buf, int len) 
{ 
	int ret;

	struct i2c_msg msg[] = {
		{
			.addr	= this_client_ltr558->addr,
			.flags	= 0,
			.len	= len,
			.buf	= buf,
		}
	};	

	//LTR558_DEBUG(KERN_INFO "%s: this_client_ltr558 = 0x%x, this_client_ltr558->addr = 0x%x\n", __func__,this_client_ltr558, this_client_ltr558->addr);

	ret = i2c_transfer(this_client_ltr558->adapter, msg, 1);
	if (ret < 0)
		printk(KERN_ERR "%s i2c write error: %d\n", __func__, ret);

	return ret;
} 

static int ltr558_i2c_write_reg(unsigned char addr, unsigned char data)
{
	unsigned char buf[2];
	buf[0]=addr;
	buf[1]=data;
	return ltr558_pls_tx_data(buf, 2);
}

/*  
 * ############### 
 * ## PS CONFIG ## 
 * ############### 
 */ 
 
static int ltr558_ps_enable(int gainrange) 
{ 
 int error; 
 int setgain;
 unsigned char intval;
 int i = 0;
 int data = 0;
 int avarage = 0;
 int nums = 10;
 int sum = 0;
 
 switch (gainrange) { 
  case PS_RANGE1: 
   setgain = MODE_PS_ON_Gain1; 
   break; 
 
  case PS_RANGE2: 
   setgain = MODE_PS_ON_Gain2; 
   break; 
 
  case PS_RANGE4: 
   setgain = MODE_PS_ON_Gain4; 
   break; 
 
  case PS_RANGE8: 
   setgain = MODE_PS_ON_Gain8; 
   break; 
 
  default: 
   setgain = MODE_PS_ON_Gain1; 
   break; 
 } 
 LTR558_DEBUG("%s: gainrange = %d, setgain = %d\n",__func__,  gainrange, setgain ); 
// ltr558_i2c_write_reg(0x8f, 0x03);
 error = ltr558_i2c_write_reg(LTR558_PS_CONTR, setgain);  
 ltr558_i2c_read_reg(LTR558_PS_CONTR, &intval);
 mdelay(50); 
 //gionee: liudj add for calibrate begin
 if(error < 0 )
 	return error;
 else
 {
 	if(0 == calibrate_status_ltr558)
 	{
		//gionee liudj add 1130 begin
		ltr558_i2c_write_reg(0x80, 0x00);
		//gionee liudj add 1130 end
 		for(i = 0; i < nums; i++)
 		{
			data=ltr558_ps_read();
			mdelay(50);
			sum+= data;
 		}
		avarage = sum/nums;
		if(avarage > 0xff)
		{
			calibrate_high_ltr558 = 0xc8;   //200
			calibrate_low_ltr558 = 0xb9;    //185
			calibrate_status_ltr558 = 0;
		}
		else
		{
			calibrate_high_ltr558 = avarage + 35;
			calibrate_low_ltr558 = avarage + 30;
			calibrate_status_ltr558 = 1;
		}
	ltr558_i2c_write_reg(LTR558_PS_CONTR, MODE_PS_StdBy);
	ltr558_i2c_read_reg(LTR558_PS_CONTR, &intval);

	ltr558_i2c_write_reg(0x90, calibrate_high_ltr558);//upper threshold lower byte
	ltr558_i2c_write_reg(0x91, 0x00);//upper threshold upper byte
	ltr558_i2c_write_reg(0x92, 0x00);//lower threshold lower byte
	ltr558_i2c_write_reg(0x93, 0x00);//lower  threshold upper byte

	ltr558_i2c_write_reg(0x8f, 0x03);
	ltr558_i2c_write_reg(0x80, 0x03);
	ltr558_i2c_read_reg(0x80, &intval);
	ltr558_i2c_write_reg(0x81, 0x03);
	ltr558_i2c_read_reg(0x81, &intval);
 	}
	input_report_abs(the_data_ltr558->input, ABS_DISTANCE, 1);
	input_sync(the_data_ltr558->input);

 }
 //gionee: liudj add for calibrate end
 /* ===============  
  * ** IMPORTANT ** 
  * =============== 
  * Other settings like timing and threshold to be set here, if required. 
   * Not set and kept as device default for now. 
   */ 
 
 LTR558_DEBUG("%s: ltr558_ps_enable over, error = %d avarage =%d calibrate_status_ltr558=%d\n", __func__, error, avarage, calibrate_status_ltr558); 
 return error; 
}

// Put PS into Standby mode 
static int ltr558_ps_disable(void) 
{ 
 int error; 
 error = ltr558_i2c_write_reg(LTR558_PS_CONTR, MODE_PS_StdBy);
 LTR558_DEBUG_LIUDJ("ltr558_ps_disable ++++++++\n");  
 return error; 
} 
 
 
static int ltr558_ps_read(void) 
{ 
	unsigned char psval_lo, psval_hi; //psdata;
	int psdata;
	//unsigned int psval_lo, psval_hi, psdata;
	//int psval_lo, psval_hi, psdata;
	ltr558_i2c_read_reg(LTR558_PS_DATA_0, &psval_lo); 
	if (psval_lo < 0){ 
		psdata = psval_lo; 
		goto out; 
	} 

	ltr558_i2c_read_reg(LTR558_PS_DATA_1, &psval_hi); 
	if (psval_hi < 0){ 
		psdata = psval_hi; 
		goto out; 
	} 
	LTR558_DEBUG(" ltr558_ps_read psval_hi = %x psval_lo = %x\n", psval_hi, psval_lo);
	psdata = ((psval_hi & 7)* 256) + psval_lo; 
	LTR558_DEBUG_LIUDJ(" ltr558_ps_read psdata = %d", psdata);

out: 
	final_prox_val_ltr558 = psdata; 
	return psdata; 
} 
 
static int ltr558_ps_read_status(void) 
{ 
  unsigned char intval; 
 
  ltr558_i2c_read_reg(LTR558_ALS_PS_STATUS, &intval);  
  if (intval < 0) 
  goto out; 
 
 intval = intval & 0x02; 
 
 out: 
 return intval;
}

#if 0 
static int ltr558_ps_open(struct inode *inode, struct file *file) 
{ 
 if (ps_opened_ltr558) { 
  LTR558_DEBUG(KERN_ALERT "%s: already opened\n", __func__); 
  return -EBUSY; 
 } 
 ps_opened_ltr558 = 1; 
 return 0; 
} 
 
static int ltr558_ps_release(struct inode *inode, struct file *file) 
{ 
 LTR558_DEBUG(KERN_ALERT "%s\n", __func__); 
 ps_opened_ltr558 = 0; 
 return ltr558_ps_disable(); 
} 
#endif
//gionee liudj add for debug begin
/*----------------------------------------------------------------------------*/
static ssize_t ltr558_show_ps(struct device* cd, struct device_attribute *attr, char* buf)
{
	//ssize_t res;
	unsigned int  dat=0;
	int dat1 = 0;
	if(!the_data_ltr558)
	{
		printk("ltr558 devices  is null!!\n");
		return 0;
	}
 	dat = ltr558_ps_read();
	ltr558_i2c_read_reg(0x8c, (unsigned char *)&dat1);
	if(0 > dat)
		dat = ltr558_ps_read();
	return snprintf(buf,PAGE_SIZE,"ps_data %d     0x%x    high=0x%x   low=0x%x  0x8c=0x%x\n",dat, dat, calibrate_high_ltr558, calibrate_low_ltr558, dat1);		
}
static ssize_t ltr558_show_als(struct device* cd, struct device_attribute *attr, char* buf)
{
	//ssize_t res;
	unsigned int  dat=0;
	if(!the_data_ltr558)
	{
		printk("ltr558 devices is null!!\n");
		return 0;
	}
 	dat = ltr558_als_read(als_gainrange_ltr558);
	if(0 > dat)
		dat = ltr558_als_read(als_gainrange_ltr558);
	return snprintf(buf,PAGE_SIZE,"als_data %d     0x%x\n",dat , dat);		
}
/*
static ssize_t ltr558_ps_status(struct device* cd, struct device_attribute *attr, char* buf)
{
	unsigned char dat=0;
	if(!the_data_ltr558)
	{
		printk("ltr558 devices is null \n");
	}
	 ltr558_i2c_read_reg( LTR558_PS_CONTR, &dat);
	 if(0 > dat)
	 	ltr558_i2c_read_reg(LTR558_PS_CONTR, &dat);
	 return snprintf(buf,PAGE_SIZE,"ps_status  %d 0x%x\n", dat, dat);
}*/
static ssize_t ltr558_ps_status(struct device* cd, struct device_attribute *attr, char* buf)
{
	int i = 0;
	u8 bufdata;
	int count  = 0;
	if(!the_data_ltr558)
	{
		printk("ltr558 devices is null \n");
	}
	for(i = 0;i < 31; i++)
	{
		ltr558_i2c_read_reg(0x80+i, &bufdata);		
		count+=sprintf(buf+count,"[%x] = (%x)\n",0x80+i,bufdata);
	}

	return count;
/**//*	 ltr558_i2c_read_reg( LTR558_PS_CONTR, &dat);
	 if(0 > dat)
	 	ltr558_i2c_read_reg(LTR558_PS_CONTR, &dat);
	 return snprintf(buf,PAGE_SIZE,"ps_status  %d 0x%x\n", dat, dat);
	
	for(i = 0;i < 31 ;i++)
	{
		if(hwmsen_read_byte_sr(ltr501_obj->client,0x80+i,&bufdata))
		{
			count+= sprintf(buf+count,"[%x] = ERROR\n",0x80+i);
		}
		else
		{
			count+= sprintf(buf+count,"[%x] = (%x)\n",0x80+i,bufdata);
		}
	}

	return count;
*/
}
#define IS_SPACE(CH) (((CH) == ' ') || ((CH) == '\n'))
/*----------------------------------------------------------------------------*/
static int read_int_from_buf(struct device* cd, const char* buf, size_t count,
							 u32 data[], int len)
{
	int idx = 0;
	char *cur = (char*)buf, *end = (char*)(buf+count);

	while(idx < len)
	{
		while((cur < end) && IS_SPACE(*cur))
		{
			cur++;
		}

		if(1 != sscanf(cur, "%d", &data[idx]))
		{
			break;
		}

		idx++;
		while((cur < end) && !IS_SPACE(*cur))
		{
			cur++;
		}
	}
	return idx;
}

static ssize_t ltr558_store_reg(struct device* cd,struct device_attribute *attr,const char *buf,size_t count)
{

	u32 data[2];
	//u32 index;
	if(!the_data_ltr558)
	{
		LTR558_DEBUG("ltr558_obj is null\n");
		return 0;
	}

	if(2 != read_int_from_buf(NULL ,buf,count,data,2))
	{
		LTR558_DEBUG("%sinvalid format:\n",__func__);
		return 0;
	}
	printk("%s,data[0]= %x,%x,count=%d\n",__func__,data[0],data[1],count);
	//hwmsen_write_byte(ltr501_obj->client,data[0],data[1]);
	ltr558_i2c_write_reg(data[0], data[1]);
	printk("%s over!!!1\n",__func__);
	return count;
}
//gionee liudj add for debug end
static ssize_t ltr558_als_status(struct device* cd, struct device_attribute *attr, char* buf)
{
	unsigned char dat=0;
	if(!the_data_ltr558)
	{
		printk("ltr558 devices is null \n");
	}
	 ltr558_i2c_read_reg( LTR558_ALS_CONTR, &dat);
	 if(0 > dat)
	 	ltr558_i2c_read_reg(LTR558_ALS_CONTR, &dat);
	 return snprintf(buf,PAGE_SIZE,"als_status %d 0x%x\n", dat, dat);
}

static DEVICE_ATTR(als_status_ltr558, S_IRUGO | S_IWUSR, ltr558_als_status, NULL);
static DEVICE_ATTR(ps_status_ltr558, S_IRUGO | S_IWUSR, ltr558_ps_status, ltr558_store_reg);
static DEVICE_ATTR(als_data_ltr558,	 S_IRUGO | S_IWUSR, ltr558_show_als,	NULL);
static DEVICE_ATTR(ps_data_ltr558,	 S_IRUGO | S_IWUSR, ltr558_show_ps,	NULL);
//gionee liudj add for debug end
static DEVICE_ATTR(suspend_ltr558, S_IRUGO | S_IWUSR, ltr558_show_suspend, ltr558_store_suspend);
static DEVICE_ATTR(version_ltr558, S_IRUGO | S_IWUSR, ltr558_show_version, NULL);

static ssize_t ltr558_show_suspend(struct device* cd,
				     struct device_attribute *attr, char* buf)
{
	ssize_t ret = 0;

	if(suspend_flag_ltr558==1)
		sprintf(buf, "ltr558 Resume\n");
	else
		sprintf(buf, "ltr558 Suspend\n");
	
	ret = strlen(buf) + 1;

	return ret;
}

static ssize_t ltr558_store_suspend(struct device* cd, struct device_attribute *attr,
		       const char* buf, size_t len)
{
	unsigned long on_off = simple_strtoul(buf, NULL, 10);
	suspend_flag_ltr558 = on_off;
	
	if(on_off==1)
	{
		LTR558_DEBUG("ltr558 Entry Resume\n");
		ltr558_ps_enable(MODE_PS_ON_Gain1);
	}
	else
	{
		LTR558_DEBUG("ltr558 Entry Suspend\n");
		ltr558_ps_disable();
	}
	
	return len;
}

static ssize_t ltr558_show_version(struct device* cd,
				     struct device_attribute *attr, char* buf)
{
	ssize_t ret = 0;

	sprintf(buf, "ltr558");	
	ret = strlen(buf) + 1;

	return ret;
}

 static int ltr558_create_sysfs(struct i2c_client *client)
{
	int err;
	struct device *dev = &(client->dev);

	LTR558_DEBUG("%s", __func__);
	
	err = device_create_file(dev, &dev_attr_suspend_ltr558);
	err = device_create_file(dev, &dev_attr_version_ltr558);
	err = device_create_file(dev, &dev_attr_ps_data_ltr558);
	err = device_create_file(dev, &dev_attr_als_data_ltr558);
	err = device_create_file(dev, &dev_attr_ps_status_ltr558);
	err = device_create_file(dev, &dev_attr_als_status_ltr558);
	if(err < 0)
	LTR558_DEBUG("%s  failed", __func__);
	return err;
}
#if 0
static long ltr558_ps_ioctl(struct file *file, unsigned int cmd, unsigned long arg) 
{ 
	int ret = 0; 
	int buffer = 0; 
	void __user *argp = (void __user *)arg; 

	LTR558_DEBUG("%s: cmd = %u, arg = %lu\n", __func__, cmd, arg);
	switch (cmd)
	{ 
		case LTR_IOCTL_SET_PFLAG: 
			if (copy_from_user(&buffer, argp, 1)) 
			{ 
				ret = -EFAULT; 
				break; 
			} 
			ps_active_ltr558 = buffer; 
			if (ps_active_ltr558) 
			{ 
				ret = ltr558_ps_enable(ps_gainrange_ltr558);
				if(ret < 0) 
					break; 
				//else 
				///  enable_irq_wake(ltr558_irq); 
			}
			break;
		case LTR_IOCTL_GET_PS_DATA: 
			buffer = ltr558_ps_read(); 
			if (buffer < 0) 
			{ 
				ret = -EFAULT; 
				break; 
			} 
			ret = copy_to_user(argp, &buffer, 1); 
			break; 

		case LTR_IOCTL_GET_PFLAG: 
			buffer = ltr558_ps_read_status(); 
			if (&buffer < 0) 
			{ 
				ret = -EFAULT; 
				break; 
			} 
			ret = copy_to_user(argp, &buffer, 1); 
			break;
	}
	return ret;
}
#endif

#if 0 
static struct file_operations ltr558_ps_fops = { 
 .owner  = THIS_MODULE, 
 .open  = ltr558_ps_open, 
 .release = ltr558_ps_release, 
 .unlocked_ioctl  = ltr558_ps_ioctl, 
 //.poll  = ltr558_ps_poll, 
}; 
#endif 
 
/*
* ################ 
 * ## ALS CONFIG ## 
 * ################ 
 */ 
 
static int ltr558_als_enable(int gainrange) 
{ 
 int error; 
 unsigned char intval;
 
printk("%s: enter\n", __func__);
 if (gainrange == 1) 
 {
  error = ltr558_i2c_write_reg(LTR558_ALS_CONTR, MODE_ALS_ON_Range1);
  ltr558_i2c_read_reg(LTR558_ALS_CONTR, &intval);
LTR558_DEBUG("%s: set MODE_ALS_ON_Range1 , error = %d\n", __func__, error);
} 
else if (gainrange == 2) 
{ 
 error = ltr558_i2c_write_reg(LTR558_ALS_CONTR, MODE_ALS_ON_Range2);
 ltr558_i2c_read_reg(LTR558_ALS_CONTR, &intval);
LTR558_DEBUG("%s: set MODE_ALS_ON_Range2 , error = %d\n", __func__, error);
}
 else 
  error = -1; 
LTR558_DEBUG("%s: over, error = %d\n", __func__, error);
 
 mdelay(WAKEUP_DELAY); 
 
 /* ===============  
  * ** IMPORTANT ** 
  * =============== 
  * Other settings like timing and threshold to be set here, if required. 
   * Not set and kept as device default for now. 
   */ 
 
 return error;
}

// Put ALS into Standby mode 
static int ltr558_als_disable(void) 
{ 
 int error; 
 error = ltr558_i2c_write_reg(LTR558_ALS_CONTR, MODE_ALS_StdBy);  
 return error; 
} 
 
 
static int ltr558_als_read(int gainrange) 
{ 
 int alsval_ch0_lo =0;
 int alsval_ch0_hi =0;//, alsval_ch0; 
 int alsval_ch1_lo =0;
 int alsval_ch1_hi =0;//, alsval_ch1; 
 int alsval_ch0 =0;
 int alsval_ch1 =0;
 int luxdata_int=0; 
 int ratio=0;
 int ch0_coeff=0;
 int ch1_coeff=0; 
  #if 0
 ltr558_i2c_read_reg(LTR558_ALS_DATA_CH0_0, &alsval_ch0_lo); 
 ltr558_i2c_read_reg(LTR558_ALS_DATA_CH0_1, &alsval_ch0_hi); 
 alsval_ch0 = (alsval_ch0_hi * 256) + alsval_ch0_lo; 
 
 ltr558_i2c_read_reg(LTR558_ALS_DATA_CH1_0, &alsval_ch1_lo); 
 ltr558_i2c_read_reg(LTR558_ALS_DATA_CH1_1, &alsval_ch1_hi); 
 alsval_ch1 = (alsval_ch1_hi * 256) + alsval_ch1_lo; 
 #else
  ltr558_i2c_read_reg(LTR558_ALS_DATA_CH1_0, (unsigned char *)&alsval_ch1_lo); 
  ltr558_i2c_read_reg(LTR558_ALS_DATA_CH1_1, (unsigned char *)&alsval_ch1_hi); 
 alsval_ch1 = (alsval_ch1_hi * 256) + alsval_ch1_lo; 
 
  ltr558_i2c_read_reg(LTR558_ALS_DATA_CH0_0, (unsigned char *)&alsval_ch0_lo); 
  ltr558_i2c_read_reg(LTR558_ALS_DATA_CH0_1, (unsigned char *)&alsval_ch0_hi); 
 alsval_ch0 = (alsval_ch0_hi * 256) + alsval_ch0_lo;  
 #endif
 LTR558_DEBUG("alsval_ch0[%d],alsval_ch1[%d]\n",alsval_ch0,alsval_ch1);
// LTR558_DEBUG("alsval_ch0 = 0x%x, alsval_ch1 = 0x%x",alsval_ch0, alsval_ch1);

  if(0 == alsval_ch1 + alsval_ch0)
  {
	ratio = 1000;
	//printk("LTR558 als read data error!!\n");
  }
  else
  {
 	ratio = (1000*alsval_ch1) /(alsval_ch1 + alsval_ch0); 
  }
  LTR558_DEBUG("ratio = %d ====================\n", ratio);
  if(ratio<450)
  {
	ch0_coeff = 17743;
	ch1_coeff = -11059;
	LTR558_DEBUG("%s ch1_coeff =%d=================\n",__func__, ch1_coeff);
	luxdata_int  = ((alsval_ch0*ch0_coeff) - (alsval_ch1*ch1_coeff))/10000;
	luxdata_int = luxdata_int + 1;
  }
  else if((ratio >=450)&&(ratio<640))
  {
	ch0_coeff = 37725;
	ch1_coeff = 13663;
	luxdata_int  = ((alsval_ch0*ch0_coeff) - (alsval_ch1*ch1_coeff))/10000;
	luxdata_int = luxdata_int + 1;
  }
  else if((ratio >=640)&&(ratio<850))
  {
	ch0_coeff = 16900;
	ch1_coeff = 1690;
	luxdata_int  = ((alsval_ch0*ch0_coeff) - (alsval_ch1*ch1_coeff))/10000;
	luxdata_int = luxdata_int + 1;
  }
  else if(ratio >= 850)
  {
	ch0_coeff = 0;
	ch1_coeff = 0;	
	luxdata_int  = ((alsval_ch0*ch0_coeff) - (alsval_ch1*ch1_coeff))/10000;
  }
  //luxdata_int  = ((alsval_ch0*ch0_coeff) - (alsval_ch1*ch1_coeff))/10000;
  if(als_gainrange_ltr558 == ALS_RANGE1_320)
  {
	luxdata_int = luxdata_int/15;
	//luxdata_int = luxdata_int;
  }
	luxdata_int = 3 * luxdata_int; 
    return luxdata_int;

}

static int ltr558_als_read_status(void) 
{ 
 unsigned char intval; 
 
 ltr558_i2c_read_reg(LTR558_ALS_PS_STATUS,&intval);  
 if (intval < 0) 
  goto out; 
 
 intval = intval & 0x08; 
 
 out: 
 return intval; 
} 
 
#if 0 
static int ltr558_als_open(struct inode *inode, struct file *file) 
{ 
 if (als_opened_ltr558) { 
//  LTR558_DEBUG(KERN_ALERT "%s: already opened\n", __func__); 
  return -EBUSY; 
 } 
 als_opened_ltr558 = 1; 
 return 0; 
} 
#endif

#if 0 
static int ltr558_als_release(struct inode *inode, struct file *file) 
{ 
 LTR558_DEBUG(KERN_ALERT "%s\n", __func__); 
 als_opened_ltr558 = 0; 
 return ltr558_als_disable(); 
} 
 
 
static long ltr558_als_ioctl(struct file *file, unsigned int cmd, unsigned long arg) 
{ 
 int ret = 0; 
 int buffer = 1; 
 void __user *argp = (void __user *)arg; 
 
 
 switch (cmd){ 
  case LTR_IOCTL_SET_LFLAG: 
   if (copy_from_user(&buffer, argp, 1)) 
   { 
    ret = -EFAULT; 
    break; 
   } 
 
   als_active_ltr558 = buffer; 
   if (als_active_ltr558) 
   { 
    ret = ltr558_als_enable(als_gainrange_ltr558); 
    if (ret < 0) 
     break; 
    //else 
    // enable_irq_wake(pls->client->irq); 
   } 
   break; 
 case LTR_IOCTL_GET_ALS_DATA: 
  buffer = ltr558_als_read(als_gainrange_ltr558); 
  if (buffer < 0) 
  { 
   ret = -EFAULT; 
   break; 
  } 
  ret = copy_to_user(argp, &buffer, 1); 
  break; 
 case LTR_IOCTL_GET_LFLAG: 
  buffer = ltr558_als_read_status(); 
  if (buffer < 0) 
  { 
   ret = -EFAULT; 
   break; 
  } 
  ret = copy_to_user(argp, &buffer, 1); 
  break; 
 } 
 return ret; 
}
#endif 

/*
static unsigned int ltr558_als_poll(struct file *fp, poll_table * wait) 
{ 
 if(als_data_changed_ltr558) 
 { 
  als_data_changed_ltr558 = 0; 
  return POLLIN | POLLRDNORM; 
 }  
 poll_wait(fp, &als_waitqueue_ltr558, wait); 
 
 return 0; 
} 

 */
 #if 0
static struct file_operations ltr558_als_fops = { 
 .owner  = THIS_MODULE, 
 .open  = ltr558_als_open, 
 .release = ltr558_als_release, 
 .unlocked_ioctl  = ltr558_als_ioctl, 
 //.poll        = ltr558_als_poll, 
}; 
#endif 

//=================
static int ltr558_open(struct inode *inode, struct file *file) 
{ 
 if (als_opened_ltr558) { 
  LTR558_DEBUG(KERN_ALERT "%s: already opened\n", __func__); 
  return -EBUSY; 
 } 
 als_opened_ltr558 = 1; 
/*
 if (ps_opened_ltr558) { 
  LTR558_DEBUG(KERN_ALERT "%s: already opened\n", __func__); 
  return -EBUSY; 
 } 
 ps_opened_ltr558 = 1; 
 */
 return 0; 
} 

static int ltr558_release(struct inode *inode, struct file *file) 
{ 
 LTR558_DEBUG(KERN_ALERT "%s\n", __func__); 
 als_opened_ltr558 = 0; 
 //return
 //ltr558_als_disable(); 
  LTR558_DEBUG(KERN_ALERT "%s\n", __func__); 
 //ps_opened_ltr558 = 0; 
 //return
 //ltr558_ps_disable(); 
 return 0;
} 


static long ltr558_ioctl(struct file *file, unsigned int cmd, unsigned long arg) 
{ 
	int ret = 0; 
	int buffer = 0; 
	void __user *argp = (void __user *)arg; 

	LTR558_DEBUG("%s: cmd = 0x%u, arg = %lu\n", __func__, cmd, arg);
	switch (cmd)
	{ 
		case LTR_IOCTL_SET_PFLAG: 
			LTR558_DEBUG_LIUDJ(" SET_PFLAG ===========\n");
			if (copy_from_user(&buffer, argp, sizeof(buffer))) 
			{ 
				return -EFAULT; 
			} 
 
			if (1 == buffer) 
			{ 
				if(ltr558_ps_enable(ps_gainrange_ltr558) < 0)
					return -EIO; 
			}else if(0 == buffer)
			{
				if(ltr558_ps_disable() < 0)
					return -EIO;
			}else
				return -EINVAL;
			ps_active_ltr558 = buffer;			
			break;

		/*case LTR_IOCTL_GET_PS_DATA: 
			buffer = ltr558_ps_read(); 
			if (buffer < 0) 
			{ 
				ret = -EFAULT; 
				break; 
			} 
			ret = copy_to_user(argp, &buffer, 1); 
			break; */

		case LTR_IOCTL_GET_PFLAG:
			LTR558_DEBUG_LIUDJ(" GET_PFLAG ===========\n"); 
			buffer = ltr558_ps_read_status(); 
			if (&buffer < 0) 
			{ 
				return -EFAULT; 
				break; 
			} 
			if(copy_to_user(argp, &buffer, 1))
				return -EFAULT; 
			break;
		case LTR_IOCTL_SET_LFLAG: 
			LTR558_DEBUG_LIUDJ(" SET_LFLAG =====++++++======\n");
			if (copy_from_user(&buffer, argp, 1)) 
			{ 
				return -EFAULT; 
				break; 
			} 

			als_active_ltr558 = buffer; 
			if (1 == als_active_ltr558) 
			{ 
				ret = ltr558_als_enable(als_gainrange_ltr558); 
				if (ret < 0) 
					return ret;
			}else if(0 == als_active_ltr558)
			{ 
				ret = ltr558_als_disable(); 
				if (ret < 0) 
					return ret;
			}else
				return -EINVAL;		 
			break; 
		/*case LTR_IOCTL_GET_ALS_DATA: 
			buffer = ltr558_als_read(als_gainrange_ltr558); 
			if (buffer < 0) 
			{ 
				ret = -EFAULT; 
				break; 
			} 
			ret = copy_to_user(argp, &buffer, 1); 
			break; */
		case LTR_IOCTL_GET_LFLAG: 
			LTR558_DEBUG_LIUDJ(" GET_LFLAG =====+++++++======\n");
			buffer = ltr558_als_read_status(); 
			if (buffer < 0) 
			{ 
				return -EFAULT; 
				break; 
			} 
			if(copy_to_user(argp, &buffer, 1))
				return -EFAULT;
			break;
		default:
			{
				LTR558_DEBUG(" ltr558_ioctrl cmd error \n");
				return -EFAULT;
			}   
	}
	return 0;
}


static struct file_operations ltr558_fops = { 
 .owner  = THIS_MODULE, 
 .open  = ltr558_open, 
 .release = ltr558_release, 
 .unlocked_ioctl  = ltr558_ioctl, 
 //.poll        = ltr558_als_poll, 
}; 


static struct miscdevice ltr558_dev = {
.minor = MISC_DYNAMIC_MINOR,
.name = LTR558_DEVICE,
.fops = &ltr558_fops,
};

//=================
 
 static void ltr558_report_dps(int data, struct input_dev *input)
{
	unsigned char dps_data = (data >= calibrate_high_ltr558 )? 1: 0;
//	LTR558_DEBUG_LIUDJ("%s: ps_data = 0x%x",__func__,data);
	dps_data = (dps_data==1) ? 0: 1;
	LTR558_DEBUG("%s: proximity = %d", __func__, dps_data);

	printk("%s: proximity = %d", __func__, dps_data);
	input_report_abs(input, ABS_DISTANCE, dps_data);
	input_sync(input);
}
 
static void ltr558_report_dls(int data, struct input_dev *input)
{
	LTR558_DEBUG("%s:  lux=%d", __func__, data);
	input_report_abs(input, ABS_MISC, data);
	input_sync(input);
}

static void ltr558_pls_work(struct work_struct *work) 
{ 
	unsigned char als_ps_status; 
	int interrupt, newdata ; 
	ltr558_pls_struct *pls = container_of(work,ltr558_pls_struct, work);

	ltr558_i2c_read_reg(LTR558_ALS_PS_STATUS, &als_ps_status); 
	interrupt = als_ps_status & 10; 
	newdata = als_ps_status & 5;
	LTR558_DEBUG(" interrupt = %d  newdata = %d als_ps_status = 0x%x\n", interrupt, newdata, als_ps_status); 
	if(0x03 == (als_ps_status & 0x03))
	{
		final_prox_val_ltr558 = ltr558_ps_read(); 
		ltr558_report_dps(final_prox_val_ltr558,pls->input);
		if(final_prox_val_ltr558 >= calibrate_high_ltr558)   //0xaa
		{
			//<3cm
			ltr558_i2c_write_reg(0x90, 0xff);//upper threshold lower byte
			ltr558_i2c_write_reg(0x91, 0x07);//upper threshold upper byte
			ltr558_i2c_write_reg(0x92, calibrate_low_ltr558);//lower threshold lower byte
			ltr558_i2c_write_reg(0x93, 0x00);//lower  threshold upper byte

		}
		else if(final_prox_val_ltr558 <=  calibrate_low_ltr558)  //0x96
		{
			//>5cm
			ltr558_i2c_write_reg(0x90, calibrate_high_ltr558);//upper threshold lower byte
			ltr558_i2c_write_reg(0x91, 0x00);//upper threshold upper byte
			ltr558_i2c_write_reg(0x92, 0x00);//lower threshold lower byte
			ltr558_i2c_write_reg(0x93, 0x00);//lower  threshold upper byte		
		}
		ps_data_changed_ltr558 = 1; 		
	}
	if(0x0c == (als_ps_status & 0x0c))
	{
		// ALS interrupt 
		final_lux_val_ltr558 = ltr558_als_read(als_gainrange_ltr558); 
		ltr558_report_dls(final_lux_val_ltr558,pls->input);
		als_data_changed_ltr558 = 1; 
		LTR558_DEBUG("ALS interrupt ++++++++++\n");	
	}
	if((0x0c != (als_ps_status & 0x0c)) && (0x03 != (als_ps_status & 0x03)))
	{
		printk("LTR558 inturrept error !!!\n");
		LTR558_DEBUG(" interrupt = %d  newdata = %d als_ps_status = 0x%x\n", interrupt, newdata, als_ps_status);
	} 
	
	enable_irq(pls->client->irq); 
} 
 

 
static irqreturn_t ltr558_irq_handler(int irq, void *dev_id) 
{ 
	ltr558_pls_struct *pls = (ltr558_pls_struct *)dev_id;

	LTR558_DEBUG("%s: irq happend\n", __func__);

	disable_irq_nosync(pls->client->irq);
	queue_work(pls->ltr_work_queue,&pls->work);
	return IRQ_HANDLED;
} 

 
static int ltr558_devinit(void) 
{ 
	int error;
	int num = 0;
	int ps_data = 1; 
	int init_ps_gain; 

	int init_als_gain; 
	LTR558_DEBUG("%s begin\n",__func__);
	mdelay(PON_DELAY); 
//gionee liudj add soft reset begin
reset:
	ltr558_i2c_write_reg(0x80, 0x04);
//gionee liudj add soft reset end
	// Enable PS to Gain1 at startup 
	init_ps_gain = PS_RANGE1; 
	//init_als_gain = ALS_RANGE2_64K; 
	init_als_gain = ALS_RANGE1_320; 

	als_gainrange_ltr558 = init_als_gain;   
	ps_gainrange_ltr558 = init_ps_gain; 
/*
	error = ltr558_als_disable();
	if(error < 0)
	{
		goto out;
	}

	error = ltr558_ps_disable();
	if(error < 0)
	{
		goto out;
	} 
*/
	ltr558_i2c_write_reg(0x82, 0x6b);
	ltr558_i2c_write_reg(0x83, 0x0f);
	ltr558_i2c_write_reg(0x84, 0x00);
	//ltr558_i2c_write_reg(0x85, 0x13);
	ltr558_i2c_write_reg(0x85, 0x03);//0x02 2_64k 0x13 0_320
	ltr558_i2c_write_reg(0x9e, 0x20);
	///============================ps interrupt threshold 
	ltr558_i2c_write_reg(0x90, 0x83);//upper threshold lower byte
	ltr558_i2c_write_reg(0x91, 0x07);//upper threshold upper byte
	ltr558_i2c_write_reg(0x92, 0x00);//lower threshold lower byte
	ltr558_i2c_write_reg(0x93, 0x00);//lower  threshold upper byte
	//===============================

	//==============================ALS INTERRUPT threshold=============
	ltr558_i2c_write_reg(0x97, 0x01);// upper threshold lower byte 0x00 modify for poll
	ltr558_i2c_write_reg(0x98, 0x00);//upper threshold upper byte 0x01 modify for poll
	ltr558_i2c_write_reg(0x99, 0xc8);//lower threshold lower byte
	ltr558_i2c_write_reg(0x9a, 0x00);//lower  threshold upper byte
	//==========================================
	ltr558_i2c_write_reg(0x8f, 0x02);
	//mdelay(10);
	//gionee liudj add 1130 begin
	ltr558_i2c_write_reg(0x81, 0x03);
	mdelay(50);
	ps_data = ltr558_ps_read();
	if(0 == ps_data && 2 != num)
	{
		num++;
		goto reset;
	}
	ltr558_i2c_write_reg(0x81, 0x00);
	//gionee liudj add 1130 end
	// Enable ALS to Full Range at startup 

//	error = ltr558_als_enable(init_als_gain); 
/*	if (error < 0) 
	{
        return error; 
	}
*/	error = 0; 

	return error; 
} 

static void ltr558_pls_pininit(int irq_pin)
{
	printk(KERN_INFO "%s [irq=%d]\n",__func__,irq_pin);
	gpio_request(irq_pin, LTR558_PLS_IRQ_PIN);
}

static void ltr558_early_suspend(struct early_suspend *handler)
{
	int ret;
	
	LTR558_DEBUG("%s\n", __func__);
 
 	//ret=ltr558_ps_disable(); 
	if(1 == als_active_ltr558)
	{
  		ret = ltr558_als_disable(); 
	}
}


static void ltr558_early_resume(struct early_suspend *handler)
{	
 //int ret; 
 //ret = ltr558_devinit(); 
 LTR558_DEBUG("%s\n", __func__);
 // ret = ltr558_ps_enable(PS_RANGE1); 
 
 // Enable ALS to Full Range at startup 
  
 //ret = ltr558_als_enable(ALS_RANGE1_320);
 if(1 == als_active_ltr558)
 	ltr558_als_enable(als_gainrange_ltr558);
}

static int ltr558_probe(struct i2c_client *client, const struct i2c_device_id *id) 
{ 
	int ret = 0; 
	unsigned char check_id;
	this_client = client;

	//check whether the I2C had insmod.
	if(1 == ltr558_probed)
	{
		ret = -EIO;
		return ret;
	}	
	
      //check whether the i2c address is 0x23	
	if(0x23 == client->addr)
	{
		LTR558_DEVICE_IC = 1;
	}
	else
	{
		ret = -EIO;
	}	
	
	//check whether the i2c communication is ok
	if(!i2c_read_reg(0x87,&check_id))
	{
		LTR558_DEBUG( "%s: i2c error \n",__func__);
		ret = -EIO;
		return ret;
	}
	LTR558_DEBUG( "i2c communication is ok, register 0x87 = %d\n",check_id);

	if(LTR558_DEVICE_IC)
	{
		unsigned char  check_i2c1 = 0; 
		struct input_dev *input_dev;
		struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
		struct ltr558_pls_platform_data *pdata = client->dev.platform_data;

		LTR558_DEBUG("%s:start probe",__func__);
		/* Return 1 if adapter supports everything we need, 0 if not. */ 
		if (!i2c_check_functionality(adapter, I2C_FUNC_I2C)) 
		{ 
			LTR558_DEBUG("%s: LTR-558ALS functionality check failed.\n", __func__); 
			ret = -EIO; 
			goto out; 
		} 
		else
		{
			LTR558_DEBUG("%s: LTR-558ALS functionality check success.\n", __func__);
		}

		/* data memory allocation */ 
		the_data_ltr558 = kzalloc(sizeof(ltr558_pls_struct), GFP_KERNEL); 
		if (the_data_ltr558 == NULL) { 
			LTR558_DEBUG("%s: LTR-558ALS kzalloc failed.\n", __func__); 
			ret = -ENOMEM; 
			goto exit_alloc_data_failed; 
		} 
		else
		{
			LTR558_DEBUG("%s: LTR-558ALS kzalloc success.\n", __func__); 
		}
		
		i2c_set_clientdata(client, the_data_ltr558); 
		the_data_ltr558->client = client; 
		this_client_ltr558 = client;

		//create work queue for interrupt.
		INIT_WORK(&the_data_ltr558->work, ltr558_pls_work);
		the_data_ltr558->ltr_work_queue= create_singlethread_workqueue(LTR558_DEVICE);	
	        if (!the_data_ltr558->ltr_work_queue) {
	                ret = -ESRCH;
	                goto exit_create_singlethread;
	        }

		//===============================================input device======================================
		LTR558_DEBUG("before create the input device.\n"); 
		input_dev = input_allocate_device();
		if (!input_dev) 
		{
			LTR558_DEBUG("%s: input allocate device failed\n", __func__);
			ret = -ENOMEM;

			goto exit_input_device_alloc_failed;
		}
		the_data_ltr558->input = input_dev;
		input_dev->name = LTR558_INPUT_DEV;
		input_dev->phys  = LTR558_INPUT_DEV;
		input_dev->id.bustype = BUS_I2C;
		input_dev->dev.parent = &client->dev;
		input_dev->id.vendor = 0x0001;
		input_dev->id.product = 0x0001;
		input_dev->id.version = 0x0010;

		__set_bit(EV_ABS, input_dev->evbit);	
		//for proximity
		input_set_abs_params(input_dev, ABS_DISTANCE, 0, 1, 0, 0);
		//for lightsensor
		input_set_abs_params(input_dev, ABS_MISC, 0, 100001, 0, 0);

		ret = input_register_device(input_dev);
		if (ret < 0)
		{
			LTR558_DEBUG(KERN_ERR "%s: input device regist failed\n", __func__);
			input_free_device(input_dev);
			goto exit_input_register_device_failed;
		}
		LTR558_DEBUG("%s: input devices register ok=====================\n", __func__);
		//=================input device==============


		//register misc devices
		ret = misc_register(&ltr558_dev); 
		if (ret) { 
			LTR558_DEBUG("%s: LTR-558ALS misc_register failed.\n", __func__); 
			
			goto exit_miscreg_failed; 
		} 
		else
		{
			LTR558_DEBUG("%s: LTR-558ALS misc_register success.\n", __func__); 
		} 
		//the end of register misc devices

		//request the interrupt for ltr558
		/*pin init*/
		ltr558_pls_pininit(pdata->irq_gpio_number);

		/*get irq*/
		client->irq = INT_GPIO_1;//gpio_to_irq(pdata->irq_gpio_number);
		the_data_ltr558->irq = client->irq;	                                             
 
		LTR558_DEBUG("I2C name=%s, addr=0x%x, gpio=%d, irq=%d",client->name,client->addr, pdata->irq_gpio_number, client->irq);

		ltr558_irq = client->irq ;

		LTR558_DEBUG("%s: request irq, irq = %d\n",__func__, ltr558_irq);

		if(client->irq > 0) 
		{  	
			ret =  request_irq(client->irq, ltr558_irq_handler, IRQF_DISABLED/*IRQ_TYPE_LEVEL_LOW*/, client->name,the_data_ltr558);
			//ret =  request_irq(client->irq, ltr558_irq_handler, IRQ_TYPE_EDGE_FALLING, client->name,the_data_ltr558);
			if (ret <0)
			{
				LTR558_DEBUG("%s: IRQ setup failed %d\n", __func__, ret);
				goto exit_irq_request_failed;
			}
			else
			{
				//disable irq 
				disable_irq(client->irq);
			}	
			LTR558_DEBUG("%s: LTR-558ALS request irq over\n", __func__); 
		}
		LTR558_DEBUG("%s: LTR-558ALS request irq successfully\n", __func__); 

             //check whether the i2c communication is ok.
		if(!ltr558_i2c_read_reg(0x86,&check_i2c1))
		{
			LTR558_DEBUG(KERN_ALERT "%s: i2c error \n",__func__);
			ret = -EIO;
			goto exit_devinit_failed;
		}
		LTR558_DEBUG("the i2c communication is ok, register 0x86: %d\n", check_i2c1); 
		
		ret = ltr558_devinit(); 
		if (ret) { 
			LTR558_DEBUG(KERN_ALERT "%s: LTR-558ALS device init failed.\n", __func__); 
			goto exit_devinit_failed; 
		} 
		else
		{
			LTR558_DEBUG(KERN_ALERT "%s: LTR-558ALS device init success.\n", __func__); 
		}
		//register early suspend
	    	the_data_ltr558->ltr_early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB + 25;
		the_data_ltr558->ltr_early_suspend.suspend = ltr558_early_suspend;
		the_data_ltr558->ltr_early_suspend.resume = ltr558_early_resume;
		register_early_suspend(&the_data_ltr558->ltr_early_suspend);


		//create attribute files
		ltr558_create_sysfs(client);
		LTR558_DEBUG("%s: Probe Success!\n",__func__);

		enable_irq(client->irq);
		ltr558_probed = 1;
		ltr558_als_enable(als_gainrange_ltr558);
		ltr558_ps_enable(ps_gainrange_ltr558);
		return 0;


		exit_devinit_failed:
		exit_irq_request_failed:
			free_irq(client->irq, the_data_ltr558);
			gpio_free(pdata->irq_gpio_number);
		exit_miscreg_failed:
			misc_deregister(&ltr558_dev);
		exit_input_device_alloc_failed:
		exit_input_register_device_failed:
			input_unregister_device(input_dev);
			input_free_device(input_dev);
		exit_create_singlethread:
		       cancel_work_sync(&the_data_ltr558->work);
		       destroy_workqueue(the_data_ltr558->ltr_work_queue);
		        i2c_set_clientdata(client, NULL);
		        kfree(the_data_ltr558);
		exit_alloc_data_failed:
		out:
			LTR558_DEBUG("%s: Probe Fail!\n",__func__);
			return ret; 

	}		
	else
		return ret;
} 
 
static int ltr558_remove(struct i2c_client *client) 
{ 
	if(LTR558_DEVICE_IC)
	{
		ltr558_pls_struct *ltr558_pls = i2c_get_clientdata(client);

		/*remove queue*/
		flush_workqueue(ltr558_pls->ltr_work_queue);
		destroy_workqueue(ltr558_pls->ltr_work_queue);

		/*free suspend*/
		unregister_early_suspend(&ltr558_pls->ltr_early_suspend);
		/*free input*/
		input_unregister_device(ltr558_pls->input);
		input_free_device(ltr558_pls->input);

		free_irq(client->irq, ltr558_pls);
	 
		 misc_deregister(&ltr558_dev);	 
		 ltr558_ps_disable(); 
		 ltr558_als_disable(); 
	  
	 	kfree(i2c_get_clientdata(client)); 
	 	
		return 0; 
	}	
	return 0;
} 
 
static const struct i2c_device_id ltr558_id[] = { 
 { LTR558_DEVICE, 0 }, 
 {} 
}; 
 
static struct i2c_driver ltr558_driver = { 
 .driver = { 
  .owner = THIS_MODULE, 
  .name = LTR558_DEVICE, 
 }, 	
 .probe = ltr558_probe, 
 .remove = ltr558_remove, 
 .id_table = ltr558_id, 
}; 

static int __init ltr558_driverinit(void) 
{ 
     LTR558_DEBUG(KERN_ALERT "<<< %s: LTR-558ALS Driver Module LOADED >>>\n", __func__); 
     return i2c_add_driver(&ltr558_driver); 
} 

static void __exit ltr558_driverexit(void) 
{ 
     i2c_del_driver(&ltr558_driver); 
     LTR558_DEBUG(KERN_ALERT ">>> %s: LTR-558ALS Driver Module REMOVED <<<\n", __func__); 
} 

 
module_init(ltr558_driverinit) 
module_exit(ltr558_driverexit) 
 
MODULE_AUTHOR("Lite-On Technology Corp"); 
MODULE_DESCRIPTION("LTR-558ALS Driver"); 
MODULE_LICENSE("Dual BSD/GPL"); 
MODULE_VERSION(DRIVER_VERSION); 


