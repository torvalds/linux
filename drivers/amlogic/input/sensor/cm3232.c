/* drivers/input/misc/cm3232.c - cm3232 optical sensors driver
 *
 * Copyright (C) 2011 Capella Microsystems Inc.
 * Author: Frank Hsieh <pengyueh@gmail.com>   
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

#include <linux/delay.h>
#include <linux/earlysuspend.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/miscdevice.h>
//#include <linux/lightsensor.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <asm/mach-types.h>
#include <linux/sensor/cm3232.h>
#include <asm/setup.h>
#include <linux/jiffies.h>

#include <linux/sensor/sensor_common.h>

#define D(x...) pr_info(x)

#define I2C_RETRY_COUNT 10

#define LS_POLLING_DELAY 500

static void report_do_work(struct work_struct *w);
static DECLARE_DELAYED_WORK(report_work, report_do_work);

struct cm3232_info {
	struct class *cm3232_class;
	struct device *ls_dev;
	struct input_dev *ls_input_dev;
	atomic_t delay;
	atomic_t enable;
#ifdef CONFIG_HAS_EARLYSUSPEND	
	struct early_suspend early_suspend;
#endif
	struct i2c_client *i2c_client;
	struct workqueue_struct *lp_wq;
	struct delayed_work work;
	int als_enable;
	uint16_t *adc_table;
	uint16_t cali_table[10];
	int ls_calibrate;
	int (*power)(int, uint8_t); /* power to the chip */

	uint32_t als_kadc;
	uint32_t als_gadc;
	uint16_t golden_adc;

	int lightsensor_opened;
	int current_level;
	uint16_t current_adc;
  int polling_delay;
};
struct cm3232_info *lp_info;
int enable_log = 0;
int fLevel=-1;
static struct mutex als_enable_mutex, als_disable_mutex, als_get_adc_mutex;
static int lightsensor_enable(struct cm3232_info *lpi);
static int lightsensor_disable(struct cm3232_info *lpi);

int32_t als_kadc;

static int I2C_RxData(uint16_t slaveAddr, uint8_t *rxData, int length)
{
	uint8_t loop_i;
  uint8_t subaddr[1];
  
  
	struct i2c_msg msg[] = {
		{
		 .addr = slaveAddr,
		 .flags = 0,
		 .len = 1,
		 .buf = subaddr,
		 },
		{
		 .addr = slaveAddr,
		 .flags = I2C_M_RD,
		 .len = length,
		 .buf = rxData,
		 },
	};

    subaddr[0] = CM3232_ALS_READ_COMMAND_CODE;

	for (loop_i = 0; loop_i < I2C_RETRY_COUNT; loop_i++) {

		if (i2c_transfer(lp_info->i2c_client->adapter, msg, 2) > 0)
			break;

		msleep(10);
	}
	if (loop_i >= I2C_RETRY_COUNT) {
		printk(KERN_ERR "[ERR][CM3232 error] %s retry over %d\n",
			__func__, I2C_RETRY_COUNT);
		return -EIO;
	}

	return 0; 
}

static int I2C_TxData(uint16_t slaveAddr, uint8_t *txData, int length)
{
	uint8_t loop_i;

	struct i2c_msg msg[] = {
		{
		 .addr = slaveAddr,
		 .flags = 0,
		 .len = length,
		 .buf = txData,
		 },
	};

	for (loop_i = 0; loop_i < I2C_RETRY_COUNT; loop_i++) {
		if (i2c_transfer(lp_info->i2c_client->adapter, msg, 1) > 0)
			break;

		msleep(10);
	}

	if (loop_i >= I2C_RETRY_COUNT) {
		printk(KERN_ERR "[ERR][CM3232 error] %s retry over %d\n",
			__func__, I2C_RETRY_COUNT);
		return -EIO;
	}

	return 0;
}

static int _cm3232_I2C_Read_Word(uint16_t slaveAddr, uint16_t* pdata)
{
	int ret = 0;
	uint8_t buffer[2];

	if (pdata == NULL)
		return -EFAULT;

	ret = I2C_RxData(slaveAddr, buffer, 2);
	if (ret < 0) {
		pr_err(
			"[ERR][CM3232 error]%s: I2C_RxData fail, slave addr: 0x%x\n",
			__func__, slaveAddr);
		return ret;
	}
	*pdata = (buffer[1]<<8) | buffer[0];

#if 0
	/* Debug use */
	printk(KERN_DEBUG "[CM3232] %s: I2C_RxData[0x%x] = 0x%x\n",
		__func__, slaveAddr, *pdata);
#endif
	return ret;
}

static int _cm3232_I2C_Write_Byte(uint16_t SlaveAddress,
				uint8_t data)
{
	char buffer[2];
	int ret = 0;

	buffer[0] = CM3232_ALS_COMMAND_CODE; 
	buffer[1] = data;
#if 0
	/* Debug use */
	printk(KERN_DEBUG
	"[CM3232] %s: _cm3232_I2C_Write_Byte[0x%x, 0x%x, 0x%x]\n",
		__func__, SlaveAddress, buffer[0], buffer[1]);
#endif

	ret = I2C_TxData(SlaveAddress, buffer, 2);
	if (ret < 0) {
		pr_err("[ERR][CM3232 error]%s: I2C_TxData fail\n", __func__);
		return -EIO;
	}

	return ret;
}

static int get_ls_adc_value(uint16_t *als_step, bool resume)
{
	uint32_t tmpResult;	
	int ret = 0;
	struct cm3232_info *lpi = lp_info;	

	if (als_step == NULL)
		return -EFAULT;

	/* Read ALS data: */
	ret = _cm3232_I2C_Read_Word(CM3232_SLAVE_addr, als_step);
	if (ret < 0) {
		pr_err(
			"[LS][CM3232 error]%s: _cm3232_I2C_Read_Word fail\n",
			__func__);
		return -EIO;
	}

  if (!lpi->ls_calibrate) {
		tmpResult = (uint32_t)(*als_step) * lpi->als_gadc / lpi->als_kadc;
		if (tmpResult > 0xFFFF)
			*als_step = 0xFFFF;
		else
		  *als_step = tmpResult;  			
	}

	D("[LS][CM3232] %s: raw adc = 0x%X\n",
		__func__, *als_step);

	return ret;
}

static void report_lsensor_input_event(struct cm3232_info *lpi, bool resume)
{/*when resume need report a data, so the paramerter need to quick reponse*/
	uint16_t adc_value = 0;
	int level = 0, i, ret = 0;
	int lux[] = {10, 320, 640,1280,2560,5120,7000,8000,9000,10000};
	mutex_lock(&als_get_adc_mutex);

	ret = get_ls_adc_value(&adc_value, resume);

  if( lpi->ls_calibrate ) {
  	for (i = 0; i < 10; i++) {
  		if (adc_value <= (*(lpi->cali_table + i))) {
  			level = i;
  			if (*(lpi->cali_table + i))
  				break;
  		}
  		if ( i == 9) {/*avoid  i = 10, because 'cali_table' of size is 10 */
  			level = i;
  			break;
  		}
  	}
  } else {
  	for (i = 0; i < 10; i++) {
  		if (adc_value <= (*(lpi->adc_table + i))) {
  			level = i;
  			if (*(lpi->adc_table + i))
  				break;
  		}
  		if ( i == 9) {/*avoid  i = 10, because 'cali_table' of size is 10 */
  			level = i;
  			break;
  		}
  	}
	}

	if ((i == 0) || (adc_value == 0))
		D("[LS][CM3232] %s: ADC=0x%03X, Level=%d, l_thd equal 0, h_thd = 0x%x \n",
			__func__, adc_value, level, *(lpi->cali_table + i));
	else
		D("[LS][CM3232] %s: ADC=0x%03X, Level=%d, l_thd = 0x%x, h_thd = 0x%x \n",
			__func__, adc_value, level, *(lpi->cali_table + (i - 1)) + 1, *(lpi->cali_table + i));

	lpi->current_level = level;
	lpi->current_adc = adc_value;
	/*D("[CM3232] %s: *(lpi->cali_table + (i - 1)) + 1 = 0x%X, *(lpi->cali_table + i) = 0x%x \n", __func__, *(lpi->cali_table + (i - 1)) + 1, *(lpi->cali_table + i));*/
	if(fLevel>=0){
		D("[LS][CM3232] L-sensor force level enable level=%d fLevel=%d\n",level,fLevel);
		level=fLevel;
	}

	input_report_abs(lpi->ls_input_dev, ABS_MISC, lux[level]);
	input_sync(lpi->ls_input_dev);

	mutex_unlock(&als_get_adc_mutex);
}

static void report_do_work(struct work_struct *work)
{
	struct cm3232_info *lpi = lp_info;
	
 	if (enable_log)
		D("[CM3232] %s\n", __func__);

	report_lsensor_input_event(lpi, 0);

  queue_delayed_work(lpi->lp_wq, &report_work, lpi->polling_delay);
}

static int als_power(int enable)
{
	struct cm3232_info *lpi = lp_info;

	if (lpi->power)
		lpi->power(LS_PWR_ON, 1);

	return 0;
}

void lightsensor_set_kvalue(struct cm3232_info *lpi)
{
	if (!lpi) {
		pr_err("[LS][CM3232 error]%s: ls_info is empty\n", __func__);
		return;
	}

	D("[LS][CM3232] %s: ALS calibrated als_kadc=0x%x\n",
			__func__, als_kadc);

	if (als_kadc >> 16 == ALS_CALIBRATED)
		lpi->als_kadc = als_kadc & 0xFFFF;
	else {
		lpi->als_kadc = 0;
		D("[LS][CM3232] %s: no ALS calibrated\n", __func__);
	}

	if (lpi->als_kadc && lpi->golden_adc > 0) {
		lpi->als_kadc = (lpi->als_kadc > 0 && lpi->als_kadc < 0x1000) ?
				lpi->als_kadc : lpi->golden_adc;
		lpi->als_gadc = lpi->golden_adc;
	} else {
		lpi->als_kadc = 1;
		lpi->als_gadc = 1;
	}
	D("[LS][CM3232] %s: als_kadc=0x%x, als_gadc=0x%x\n",
		__func__, lpi->als_kadc, lpi->als_gadc);
}

static int lightsensor_update_table(struct cm3232_info *lpi)
{
	uint32_t tmpData[10];
	int i;
	for (i = 0; i < 10; i++) {
		tmpData[i] = (uint32_t)(*(lpi->adc_table + i))
				* lpi->als_kadc / lpi->als_gadc ;
		if( tmpData[i] <= 0xFFFF ){
      lpi->cali_table[i] = (uint16_t) tmpData[i];		
    } else {
      lpi->cali_table[i] = 0xFFFF;    
    }         
		D("[LS][CM3232] %s: Calibrated adc_table: data[%d], %x\n",
			__func__, i, lpi->cali_table[i]);
	}

	return 0;
}

static int lightsensor_enable(struct cm3232_info *lpi)
{
	int ret = 0;
	uint8_t cmd = 0;

	mutex_lock(&als_enable_mutex);
	D("[LS][CM3232] %s\n", __func__);
  
	cmd = CM3232_ALS_IT_200ms | CM3232_ALS_HS_HIGH ;
	ret = _cm3232_I2C_Write_Byte(CM3232_SLAVE_addr, cmd);
	if (ret < 0)
		pr_err(
		"[LS][CM3232 error]%s: set auto light sensor fail\n",
		__func__);
	else {
		msleep(50);/*wait for 50 ms for the first report adc*/
		/* report an invalid value first to ensure we
		* trigger an event when adc_level is zero.
		*/

		input_report_abs(lpi->ls_input_dev, ABS_MISC, -1);
		input_sync(lpi->ls_input_dev);
		report_lsensor_input_event(lpi, 1);/*resume, IOCTL and DEVICE_ATTR*/
		lpi->als_enable = 1;		
	}
       queue_delayed_work(lpi->lp_wq, &report_work, lpi->polling_delay);
       lpi->als_enable=1;
	mutex_unlock(&als_enable_mutex);
	
	return ret;
}

static int lightsensor_disable(struct cm3232_info *lpi)
{
	int ret = 0;
	char cmd = 0;

	mutex_lock(&als_disable_mutex);

	D("[LS][CM3232] %s\n", __func__);

	cmd = CM3232_ALS_IT_200ms | CM3232_ALS_HS_HIGH | CM3232_ALS_SD ;
	ret = _cm3232_I2C_Write_Byte(CM3232_SLAVE_addr, cmd);
	if (ret < 0)
		pr_err("[LS][CM3232 error]%s: disable auto light sensor fail\n",
			__func__);
	else {
		lpi->als_enable = 0;
	}

 	cancel_delayed_work_sync(&report_work);
 	 	
  lpi->als_enable=0;
	mutex_unlock(&als_disable_mutex);
	
	return ret;
}

static int lightsensor_open(struct inode *inode, struct file *file)
{
	struct cm3232_info *lpi = lp_info;
	int rc = 0;

	D("[LS][CM3232] %s\n", __func__);
	if (lpi->lightsensor_opened) {
		pr_err("[LS][CM3232 error]%s: already opened\n", __func__);
		rc = -EBUSY;
	}
	lpi->lightsensor_opened = 1;
	return rc;
}

static int lightsensor_release(struct inode *inode, struct file *file)
{
	struct cm3232_info *lpi = lp_info;

	D("[LS][CM3232] %s\n", __func__);
	lpi->lightsensor_opened = 0;
	return 0;
}

static long lightsensor_ioctl(struct file *file, unsigned int cmd,
		unsigned long arg)
{
	int rc, val;
	struct cm3232_info *lpi = lp_info;

	/*D("[CM3232] %s cmd %d\n", __func__, _IOC_NR(cmd));*/

	switch (cmd) {
	case LIGHTSENSOR_IOCTL_ENABLE:
		if (get_user(val, (unsigned long __user *)arg)) {
			rc = -EFAULT;
			break;
		}
		D("[LS][CM3232] %s LIGHTSENSOR_IOCTL_ENABLE, value = %d\n",
			__func__, val);
		rc = val ? lightsensor_enable(lpi) : lightsensor_disable(lpi);
		break;
	case LIGHTSENSOR_IOCTL_GET_ENABLED:
		val = lpi->als_enable;
		D("[LS][CM3232] %s LIGHTSENSOR_IOCTL_GET_ENABLED, enabled %d\n",
			__func__, val);
		rc = put_user(val, (unsigned long __user *)arg);
		break;
	default:
		pr_err("[LS][CM3232 error]%s: invalid cmd %d\n",
			__func__, _IOC_NR(cmd));
		rc = -EINVAL;
	}

	return rc;
}

static const struct file_operations lightsensor_fops = {
	.owner = THIS_MODULE,
	.open = lightsensor_open,
	.release = lightsensor_release,
	.unlocked_ioctl = lightsensor_ioctl
};

static struct miscdevice lightsensor_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "lightsensor",
	.fops = &lightsensor_fops
};

static ssize_t ls_adc_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	int ret;
	struct cm3232_info *lpi = lp_info;

	D("[LS][CM3232] %s: ADC = 0x%04X, Level = %d \n",
		__func__, lpi->current_adc, lpi->current_level);
	ret = sprintf(buf, "ADC[0x%04X] => level %d\n",
		lpi->current_adc, lpi->current_level);

	return ret;
}

static ssize_t ls_enable_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	int ret = 0;
	struct cm3232_info *lpi = lp_info;

	ret = sprintf(buf, "Light sensor Auto Enable = %d\n",
			lpi->als_enable);

	return ret;
}

static ssize_t ls_enable_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	int ret = 0;
	int ls_auto;
	struct cm3232_info *lpi = lp_info;

	ls_auto = -1;
	sscanf(buf, "%d", &ls_auto);

	if (ls_auto != 0 && ls_auto != 1 )
		return -EINVAL;

	if (ls_auto) {
		ret = lightsensor_enable(lpi);
	} else {
		ret = lightsensor_disable(lpi);
	}

	D("[LS][CM3232] %s: lpi->als_enable = %d, lpi->ls_calibrate = %d, ls_auto=%d\n",
		__func__, lpi->als_enable, lpi->ls_calibrate, ls_auto);

	if (ret < 0)
		pr_err(
		"[LS][CM3232 error]%s: set auto light sensor fail\n",
		__func__);

	return count;
}

static ssize_t ls_poll_delay_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	int ret = 0;
	struct cm3232_info *lpi = lp_info;

	ret = sprintf(buf, "Light sensor Poll Delay = %d ms\n",
			jiffies_to_msecs(lpi->polling_delay));

	return ret;
}

static ssize_t ls_poll_delay_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	int new_delay;
	struct cm3232_info *lpi = lp_info;

	sscanf(buf, "%d", &new_delay);
  
	D("new delay = %d ms, old delay = %d ms \n", 
  new_delay, jiffies_to_msecs(lpi->polling_delay));

  lpi->polling_delay = msecs_to_jiffies(new_delay);

  if( lpi->als_enable ){
		lightsensor_disable(lpi); 
		lightsensor_enable(lpi);
  }

	return count;
}

static ssize_t ls_kadc_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct cm3232_info *lpi = lp_info;
	int ret;

	ret = sprintf(buf, "kadc = 0x%x",
			lpi->als_kadc);

	return ret;
}

static ssize_t ls_kadc_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct cm3232_info *lpi = lp_info;
	int kadc_temp = 0;

	sscanf(buf, "%d", &kadc_temp);
	/*
	if (kadc_temp <= 0 || lpi->golden_adc <= 0) {
		printk(KERN_ERR "[LS][CM3232 error] %s: kadc_temp=0x%x, als_gadc=0x%x\n",
			__func__, kadc_temp, lpi->golden_adc);
		return -EINVAL;
	}*/
	mutex_lock(&als_get_adc_mutex);
  if(kadc_temp != 0) {
		lpi->als_kadc = kadc_temp;
		if(  lpi->als_gadc != 0){
  		if (lightsensor_update_table(lpi) < 0)
				printk(KERN_ERR "[LS][CM3232 error] %s: update ls table fail\n", __func__);
  	} else {
			printk(KERN_INFO "[LS]%s: als_gadc =0x%x wait to be set\n",
					__func__, lpi->als_gadc);
  	}		
	} else {
		printk(KERN_INFO "[LS]%s: als_kadc can't be set to zero\n",
				__func__);
	}
				
	mutex_unlock(&als_get_adc_mutex);
	return count;
}

static ssize_t ls_gadc_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct cm3232_info *lpi = lp_info;
	int ret;

	ret = sprintf(buf, "gadc = 0x%x\n", lpi->als_gadc);

	return ret;
}

static ssize_t ls_gadc_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct cm3232_info *lpi = lp_info;
	int gadc_temp = 0;

	sscanf(buf, "%d", &gadc_temp);
	/*if (gadc_temp <= 0 || lpi->golden_adc <= 0) {
		printk(KERN_ERR "[LS][CM3232 error] %s: kadc_temp=0x%x, als_gadc=0x%x\n",
			__func__, kadc_temp, lpi->golden_adc);
		return -EINVAL;
	}*/
	
	mutex_lock(&als_get_adc_mutex);
  if(gadc_temp != 0) {
		lpi->als_gadc = gadc_temp;
		if(  lpi->als_kadc != 0){
  		if (lightsensor_update_table(lpi) < 0)
				printk(KERN_ERR "[LS][CM3232 error] %s: update ls table fail\n", __func__);
  	} else {
			printk(KERN_INFO "[LS]%s: als_kadc =0x%x wait to be set\n",
					__func__, lpi->als_kadc);
  	}		
	} else {
		printk(KERN_INFO "[LS]%s: als_gadc can't be set to zero\n",
				__func__);
	}
	mutex_unlock(&als_get_adc_mutex);
	return count;
}

static ssize_t ls_adc_table_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	unsigned length = 0;
	int i;

	for (i = 0; i < 10; i++) {
		length += sprintf(buf + length,
			"[CM3232]Get adc_table[%d] =  0x%x ; %d, Get cali_table[%d] =  0x%x ; %d, \n",
			i, *(lp_info->adc_table + i),
			*(lp_info->adc_table + i),
			i, *(lp_info->cali_table + i),
			*(lp_info->cali_table + i));
	}
	return length;
}

static ssize_t ls_adc_table_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{

	struct cm3232_info *lpi = lp_info;
	char *token[10];
	uint16_t tempdata[10];
	int i;

	printk(KERN_INFO "[LS][CM3232]%s\n", buf);
	for (i = 0; i < 10; i++) {
		token[i] = strsep((char **)&buf, " ");
		tempdata[i] = simple_strtoul(token[i], NULL, 16);
		if (tempdata[i] < 1 || tempdata[i] > 0xffff) {
			printk(KERN_ERR
			"[LS][CM3232 error] adc_table[%d] =  0x%x Err\n",
			i, tempdata[i]);
			return count;
		}
	}
	mutex_lock(&als_get_adc_mutex);
	for (i = 0; i < 10; i++) {
		lpi->adc_table[i] = tempdata[i];
		printk(KERN_INFO
		"[LS][CM3232]Set lpi->adc_table[%d] =  0x%x\n",
		i, *(lp_info->adc_table + i));
	}
	if (lightsensor_update_table(lpi) < 0)
		printk(KERN_ERR "[LS][CM3232 error] %s: update ls table fail\n",
		__func__);
	mutex_unlock(&als_get_adc_mutex);
	D("[LS][CM3232] %s\n", __func__);

	return count;
}

static uint8_t ALS_CONF = 0;
static ssize_t ls_conf_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "ALS_CONF = %x\n", ALS_CONF);
}
static ssize_t ls_conf_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	int value = 0;
	sscanf(buf, "0x%x", &value);

	ALS_CONF = value;
	printk(KERN_INFO "[LS]set ALS_CONF = %x\n", ALS_CONF);
	_cm3232_I2C_Write_Byte(CM3232_SLAVE_addr, ALS_CONF);
	return count;
}

static ssize_t ls_fLevel_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "fLevel = %d\n", fLevel);
}
static ssize_t ls_fLevel_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct cm3232_info *lpi = lp_info;
	int value=0;
	sscanf(buf, "%d", &value);
	(value>=0)?(value=min(value,10)):(value=max(value,-1));
	fLevel=value;
	input_report_abs(lpi->ls_input_dev, ABS_MISC, fLevel);
	input_sync(lpi->ls_input_dev);
	printk(KERN_INFO "[LS]set fLevel = %d\n", fLevel);

	msleep(1000);
	fLevel=-1;
	return count;
}

static struct device_attribute dev_attr_light_enable =
__ATTR(enable, S_IRUGO | S_IWUSR | S_IWGRP, ls_enable_show, ls_enable_store);

static struct device_attribute dev_attr_light_poll_delay =
__ATTR(delay, S_IRUGO | S_IWUSR | S_IWGRP, ls_poll_delay_show, ls_poll_delay_store);

static struct device_attribute dev_attr_light_kadc =
__ATTR(kadc, S_IRUGO | S_IWUSR | S_IWGRP, ls_kadc_show, ls_kadc_store);

static struct device_attribute dev_attr_light_gadc =
__ATTR(gadc, S_IRUGO | S_IWUSR | S_IWGRP, ls_gadc_show, ls_gadc_store);

static struct device_attribute dev_attr_light_adc_table =
__ATTR(adc_table, S_IRUGO | S_IWUSR | S_IWGRP, ls_adc_table_show, ls_adc_table_store);

static struct device_attribute dev_attr_light_conf =
__ATTR(conf, S_IRUGO | S_IWUSR | S_IWGRP, ls_conf_show, ls_conf_store);

static struct device_attribute dev_attr_light_fLevel =
__ATTR(fLevel, S_IRUGO | S_IWUSR | S_IWGRP, ls_fLevel_show, ls_fLevel_store);

static struct device_attribute dev_attr_light_adc =
__ATTR(adc, S_IRUGO | S_IWUSR | S_IWGRP, ls_adc_show, NULL);

static struct attribute *light_sysfs_attrs[] = {
&dev_attr_light_enable.attr,
&dev_attr_light_poll_delay.attr,
&dev_attr_light_kadc.attr,
&dev_attr_light_gadc.attr,
&dev_attr_light_adc_table.attr,
&dev_attr_light_conf.attr,
&dev_attr_light_fLevel.attr,
&dev_attr_light_adc.attr,
NULL
};

static struct attribute_group light_attribute_group = {
.attrs = light_sysfs_attrs,
};
static void cm3232_do_work(struct work_struct *work)
{
	struct cm3232_info *lpi = lp_info;
	unsigned long delay = msecs_to_jiffies(atomic_read(&lpi->delay));
	printk("cm3232_do_work----->1111\n");
 	if (enable_log)
		D("[CM3232] %s\n", __func__);

	report_lsensor_input_event(lpi, 0);
	schedule_delayed_work(&lpi->work, delay);

}
static int lightsensor_setup(struct cm3232_info *lpi)
{
	int ret;

	lpi->ls_input_dev = input_allocate_device();
	if (!lpi->ls_input_dev) {
		pr_err(
			"[LS][CM3232 error]%s: could not allocate ls input device\n",
			__func__);
		return -ENOMEM;
	}
	lpi->ls_input_dev->name = "cm3232-ls";
	set_bit(EV_ABS, lpi->ls_input_dev->evbit);
	input_set_capability(lpi->ls_input_dev,EV_ABS,ABS_MISC);
	input_set_abs_params(lpi->ls_input_dev, ABS_MISC, 0, 10240, 0, 0);

	ret = input_register_device(lpi->ls_input_dev);
	if (ret < 0) {
		pr_err("[LS][CM3232 error]%s: can not register ls input device\n",
				__func__);
		goto err_free_ls_input_device;
	}
	
	ret = misc_register(&lightsensor_misc);
	if (ret < 0) {
		pr_err("[LS][CM3232 error]%s: can not register ls misc device\n",
				__func__);
		goto err_unregister_ls_input_device;
	}

	return ret;

err_unregister_ls_input_device:
	input_unregister_device(lpi->ls_input_dev);
err_free_ls_input_device:
	input_free_device(lpi->ls_input_dev);
	return ret;
}

static int cm3232_setup(struct cm3232_info *lpi)
{
	int ret = 0;

	als_power(1);
	msleep(5);
	printk("cm3232_setup-------->1111\n");
  ret = _cm3232_I2C_Write_Byte(CM3232_SLAVE_addr, CM3232_ALS_RESET);
  if(ret<0){
  	printk("i2c write failed!\n");
    return ret;  
  	}
  msleep(10);
      printk("cm3232_setup-------->2222\n");
  ret = _cm3232_I2C_Write_Byte(CM3232_SLAVE_addr, CM3232_ALS_IT_200ms | CM3232_ALS_HS_HIGH );
    if(ret<0){
  	printk("i2c write failed!\n");
	return ret;  
  	}
	msleep(10);

	return ret;
}

#ifdef CONFIG_HAS_EARLYSUSPEND	
static void cm3232_early_suspend(struct early_suspend *h)
{
	struct cm3232_info *lpi = lp_info;

	D("[LS][CM3232] %s\n", __func__);

	if (lpi->als_enable)
		lightsensor_disable(lpi);
}

static void cm3232_late_resume(struct early_suspend *h)
{
	struct cm3232_info *lpi = lp_info;

	D("[LS][CM3232] %s\n", __func__);

	if (!lpi->als_enable)
		lightsensor_enable(lpi);
}
#endif
static int cm3232_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	int ret = 0;
	struct cm3232_info *lpi;
	struct cm3232_platform_data *pdata;

	D("[CM3232] %s\n", __func__);


	lpi = kzalloc(sizeof(struct cm3232_info), GFP_KERNEL);
	if (!lpi)
		return -ENOMEM;

	lpi->i2c_client = client;
	pdata = client->dev.platform_data;
	if (!pdata) {
		pr_err("[CM3232 error]%s: Assign platform_data error!!\n",
			__func__);
		ret = -EBUSY;
		goto err_platform_data_null;
	}

	i2c_set_clientdata(client, lpi);
	lpi->adc_table = pdata->levels;
	lpi->golden_adc = pdata->golden_adc;
	lpi->power = pdata->power;
	lpi->polling_delay = msecs_to_jiffies(LS_POLLING_DELAY);
	INIT_DELAYED_WORK(&lpi->work, cm3232_do_work);
	atomic_set(&lpi->delay, LS_POLLING_DELAY);
	atomic_set(&lpi->enable, 0);
	
	lp_info = lpi;

	mutex_init(&als_enable_mutex);
	mutex_init(&als_disable_mutex);
	mutex_init(&als_get_adc_mutex);

	ret = lightsensor_setup(lpi);
	if (ret < 0) {
		pr_err("[LS][CM3232 error]%s: lightsensor_setup error!!\n",
			__func__);
		goto err_lightsensor_setup;
	}
 
  //SET LUX STEP FACTOR HERE
  // if 1 lux equals to adc value 28
  // the factor will be 1/28  
  // and lpi->golden_adc = 1;  
  // set als_kadc = (ALS_CALIBRATED <<16) | 28;

  als_kadc = (ALS_CALIBRATED <<16) | 28;
  lpi->golden_adc = 1;

  //ls calibrate always set to 1 
  lpi->ls_calibrate = 1;
	lightsensor_set_kvalue(lpi);
	ret = lightsensor_update_table(lpi);
	if (ret < 0) {
		pr_err("[LS][CM3232 error]%s: update ls table fail\n",
			__func__);
		goto err_lightsensor_update_table;
	}

	lpi->lp_wq = create_singlethread_workqueue("cm3232_wq");
	if (!lpi->lp_wq) {
		pr_err("[CM3232 error]%s: can't create workqueue\n", __func__);
		ret = -ENOMEM;
		goto err_create_singlethread_workqueue;
	}
	ret = cm3232_setup(lpi);
	if (ret < 0) {
		pr_err("[ERR][CM3232 error]%s: cm3232_setup error!\n", __func__);
		goto err_cm3232_setup;
	}
	lpi->cm3232_class = class_create(THIS_MODULE, "optical_sensors");
	if (IS_ERR(lpi->cm3232_class)) {
		ret = PTR_ERR(lpi->cm3232_class);
		lpi->cm3232_class = NULL;
		goto err_create_class;
	}

	lpi->ls_dev = device_create(lpi->cm3232_class,
				NULL, 0, "%s", "lightsensor");
	if (unlikely(IS_ERR(lpi->ls_dev))) {
		ret = PTR_ERR(lpi->ls_dev);
		lpi->ls_dev = NULL;
		goto err_create_ls_device;
	}
	/* register the attributes */
	ret = sysfs_create_group(&lpi->ls_input_dev->dev.kobj,
	&light_attribute_group);
	if (ret) {
		pr_err("[LS][CM3232 error]%s: could not create sysfs group\n", __func__);
		goto err_sysfs_create_group_light;
	}
#ifdef CONFIG_HAS_EARLYSUSPEND
	lpi->early_suspend.level =
			EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	lpi->early_suspend.suspend = cm3232_early_suspend;
	lpi->early_suspend.resume = cm3232_late_resume;
	register_early_suspend(&lpi->early_suspend);
#endif
       lpi->als_enable=0;
	D("[CM3232] %s: Probe success!\n", __func__);
	
	//schedule_delayed_work(&lpi->work, 0);
	return ret;

err_sysfs_create_group_light:
//err_create_ls_device_file:
	device_unregister(lpi->ls_dev);
err_create_ls_device:
	class_destroy(lpi->cm3232_class);
err_create_class:
err_cm3232_setup:
	destroy_workqueue(lpi->lp_wq);
	mutex_destroy(&als_enable_mutex);
	mutex_destroy(&als_disable_mutex);
	mutex_destroy(&als_get_adc_mutex);
	input_unregister_device(lpi->ls_input_dev);
	input_free_device(lpi->ls_input_dev);
err_create_singlethread_workqueue:
err_lightsensor_update_table:
  misc_deregister(&lightsensor_misc);
err_lightsensor_setup:
err_platform_data_null:
	kfree(lpi);
	return ret;
}

static const struct i2c_device_id cm3232_i2c_id[] = {
	{CM3232_I2C_NAME, 0},
	{}
};

static struct i2c_driver cm3232_driver = {
	.id_table = cm3232_i2c_id,
	.probe = cm3232_probe,
	.driver = {
		.name = CM3232_I2C_NAME,
		.owner = THIS_MODULE,
	},
};


static int __init cm3232_init(void)
{

	return i2c_add_driver(&cm3232_driver);
}

static void __exit cm3232_exit(void)
{
	i2c_del_driver(&cm3232_driver);
}

module_init(cm3232_init);
module_exit(cm3232_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CM3232 Driver");
MODULE_AUTHOR("Frank Hsieh <pengyueh@gmail.com>");


