
/* Lite-On LTR501-ALS Linux Driver
 *
 * Copyright (C) 2011 Lite-On Technology Corp (Singapore)
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 */
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <mach/hardware.h>
#include <linux/delay.h>
#include <linux/earlysuspend.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/sensor/sensor_common.h>

#define DRIVER_VERSION 			"1.0"
#define LTR501_DEVICE_NAME 		"LTR501"

#define LTR501_I2C_SLAVE_ADDR 		0x46
/* LTR501 Registers */
#define LTR501_ALS_CONTR			0x80
#define LTR501_PS_CONTR			0x81
#define LTR501_PS_LED			0x82
#define LTR501_PS_N_PULSES		0x83
#define LTR501_PS_MEAS_RATE		0x84
#define LTR501_ALS_MEAS_RATE		0x85
#define LTR501_MANUFACTURER_ID		0x87

#define LTR501_INTERRUPT			0x8F
#define LTR501_PS_THRES_UP_0		0x90
#define LTR501_PS_THRES_UP_1		0x91
#define LTR501_PS_THRES_LOW_0		0x92
#define LTR501_PS_THRES_LOW_1		0x93

#define LTR501_ALS_THRES_UP_0		0x97
#define LTR501_ALS_THRES_UP_1		0x98
#define LTR501_ALS_THRES_LOW_0		0x99
#define LTR501_ALS_THRES_LOW_1		0x9A

#define LTR501_INTERRUPT_PERSIST	0x9E

/* Read Only Registers */
#define LTR501_ALS_DATA_CH1_0		0x88
#define LTR501_ALS_DATA_CH1_1		0x89
#define LTR501_ALS_DATA_CH0_0		0x8A
#define LTR501_ALS_DATA_CH0_1		0x8B
#define LTR501_ALS_PS_STATUS		0x8C
#define LTR501_PS_DATA_0			0x8D
#define LTR501_PS_DATA_1			0x8E


/* Basic Operating Modes */
#define MODE_ALS_ON_Range1		0x0B
#define MODE_ALS_ON_Range2		0x03
#define MODE_ALS_StdBy			0x00

#define MODE_PS_ON_Gain1			0x03
#define MODE_PS_ON_Gain4			0x07
#define MODE_PS_ON_Gain8			0x0B
#define MODE_PS_ON_Gain16			0x0F
#define MODE_PS_StdBy			0x00

#define PS_RANGE1	1
#define PS_RANGE2	2
#define PS_RANGE4	4
#define PS_RANGE8	8

#define ALS_RANGE1_320	1
#define ALS_RANGE2_64K	2

/*
 * Magic Number
 * ============
 * Refer to file ioctl-number.txt for allocation
 */
#define LIGHT_IOM		 'i'

#define LTR501_IOC_GET_PFLAG		_IOR(LIGHT_IOM, 0x00, short)
#define LTR501_IOC_SET_PFLAG		_IOW(LIGHT_IOM, 0x01, short)

#define LTR501_IOC_GET_LFLAG		_IOR(LIGHT_IOM, 0x10, short)
#define LTR501_IOC_SET_LFLAG		_IOW(LIGHT_IOM, 0x11, short)

/* Power On response time in ms */
#define PON_DELAY				600
#define WAKEUP_DELAY			10
#define LTR501_SCHE_DELAY		500
/* Interrupt vector number to use when probing IRQ number.
 * User changeable depending on sys interrupt.
 * For IRQ numbers used, see /proc/interrupts.
 */

#define LTR501DBG 0
#if LTR501DBG
	#define LTR501_DEBUG(format, ...)	\
		printk(KERN_INFO "LTR501 " format "\n", ## __VA_ARGS__)
#else
	#define LTR501_DEBUG(format, ...)
#endif

static int ps_gainrange;
static int als_gainrange;

static int final_prox_val;
static int final_lux_val;

struct ltr501_data {
	struct i2c_client *client;
	struct delayed_work work;
	struct input_dev *input_dev;
	struct class ltr_cls;
	struct mutex ltr501_mutex;
	atomic_t delay;
    int     als_enabled;
    struct mutex als_mutex;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif
};
#ifdef CONFIG_HAS_EARLYSUSPEND
static void ltr501_early_suspend(struct early_suspend *h);
static void ltr501_late_resume(struct early_suspend *h);
#endif
static struct ltr501_data *the_data = NULL;

#define P_SENSOR_MODE_1	1
#define P_SENSOR_MODE_2	2
// I2C Read
/*
 * i2c_smbus_read_byte_data - SMBus "read byte" protocol
 * @client: Handle to slave device
 * @command: Byte interpreted by slave
 *
 * This executes the SMBus "read byte" protocol, returning negative errno
 * else a data byte received from the device.
 */
static int ltr501_i2c_read_reg(u8 reg)
{
	int ret;
	ret = i2c_smbus_read_byte_data(the_data->client, reg);
	return ret;
}

// I2C Write
/*
 * i2c_smbus_write_byte_data - SMBus "write byte" protocol
 * @client: Handle to slave device
 * @command: Byte interpreted by slave
 * @value: Byte being written
 *
 * This executes the SMBus "write byte" protocol, returning negative errno
 * else zero on success.
 */
static int ltr501_i2c_write_reg(u8 reg, u8 value)
{
	int ret;

	ret = i2c_smbus_write_byte_data(the_data->client, reg, value);

	if (ret < 0)
		return ret;
	else
		return 0;
}


/*
 * ###############
 * ## PS CONFIG ##
 * ###############
 */
#if 0
static int ltr501_ps_enable(int gainrange)
{
	int ret;
	int setgain;

	switch (gainrange) {
		case PS_RANGE1:
			setgain = MODE_PS_ON_Gain1;
			break;

		case PS_RANGE2:
			setgain = MODE_PS_ON_Gain4;
			break;

		case PS_RANGE4:
			setgain = MODE_PS_ON_Gain8;
			break;

		case PS_RANGE8:
			setgain = MODE_PS_ON_Gain16;
			break;

		default:
			setgain = MODE_PS_ON_Gain1;
			break;
	}

	ret = ltr501_i2c_write_reg(LTR501_PS_CONTR, setgain);
	LTR501_DEBUG("0x81 = [%x]\n", ltr501_i2c_read_reg(0x81));
	mdelay(WAKEUP_DELAY);

	/* ===============
	 * ** IMPORTANT **
	 * ===============
	 * Other settings like timing and threshold to be set here, if required.
 	 * Not set and kept as device default for now.
 	 */

	return ret;
}
#endif

// Put PS into Standby mode
static int ltr501_ps_disable(void)
{
	int ret;
	ret = ltr501_i2c_write_reg(LTR501_PS_CONTR, MODE_PS_StdBy);
	LTR501_DEBUG("0x81 = [%x]\n", ltr501_i2c_read_reg(0x81));
	return ret;
}


static int ltr501_ps_read(void)
{
	int psval_lo, psval_hi, psdata;

	psval_lo = ltr501_i2c_read_reg(LTR501_PS_DATA_0);
	if (psval_lo < 0){
		return psval_lo;
	}

	psval_hi = ltr501_i2c_read_reg(LTR501_PS_DATA_1);
	if (psval_hi < 0){
		return psval_hi;
	}

	psdata = ((psval_hi & 7)* 256) + psval_lo;
	final_prox_val = psdata;
	return psdata;
}

/*
 * ################
 * ## ALS CONFIG ##
 * ################
 */

static int ltr501_als_enable(int gainrange)
{
	int ret;

	if (gainrange == ALS_RANGE1_320)
		ret = ltr501_i2c_write_reg(LTR501_ALS_CONTR, MODE_ALS_ON_Range1);
	else if (gainrange == ALS_RANGE2_64K)
		ret = ltr501_i2c_write_reg(LTR501_ALS_CONTR, MODE_ALS_ON_Range2);
	else
		ret = -1;

	LTR501_DEBUG("0x8f  = [%x]\n", ltr501_i2c_read_reg(0x8f));
	LTR501_DEBUG("0x80 = [%x]\n", ltr501_i2c_read_reg(0x80));
	mdelay(WAKEUP_DELAY);

	/* ===============
	 * ** IMPORTANT **
	 * ===============
	 * Other settings like timing and threshold to be set here, if required.
 	 * Not set and kept as device default for now.
 	 */

	return ret;
}


// Put ALS into Standby mode
static int ltr501_als_disable(void)
{
	int ret;
	ret = ltr501_i2c_write_reg(LTR501_ALS_CONTR, MODE_ALS_StdBy);
	LTR501_DEBUG("0x8f  = [%x]\n", ltr501_i2c_read_reg(0x8f));
	LTR501_DEBUG("0x80 = [%x]\n", ltr501_i2c_read_reg(0x80));
	return ret;
}


static int ltr501_als_read(int gainrange)
{
	int alsval_ch0_lo, alsval_ch0_hi;
	int alsval_ch1_lo, alsval_ch1_hi;
	int luxdata_int;
	int ratio;
	int alsval_ch0, alsval_ch1;
	int ch0_coeff, ch1_coeff;

	alsval_ch1_lo = ltr501_i2c_read_reg(LTR501_ALS_DATA_CH1_0);
	alsval_ch1_hi = ltr501_i2c_read_reg(LTR501_ALS_DATA_CH1_1);
	alsval_ch1 = (alsval_ch1_hi <<8)|alsval_ch1_lo ;
        
	alsval_ch0_lo = ltr501_i2c_read_reg(LTR501_ALS_DATA_CH0_0);
	alsval_ch0_hi = ltr501_i2c_read_reg(LTR501_ALS_DATA_CH0_1);
	alsval_ch0 = (alsval_ch0_hi <<8) | alsval_ch0_lo;

	LTR501_DEBUG("alsval_ch0[%d],  alsval_ch1[%d]\n ", alsval_ch0, alsval_ch1);
	// lux formula
	alsval_ch1 = (alsval_ch1<1)?1:alsval_ch1;//susposed 0
	alsval_ch0 = (alsval_ch0<1)?1:alsval_ch0;
	ratio = (100 * alsval_ch1)/(alsval_ch1 + alsval_ch1);

	if (ratio < 45)
	{
		ch0_coeff = 17743;
		ch1_coeff = -11059;
	}
	else if ((ratio >= 45) && (ratio < 64))
	{
		ch0_coeff = 37725;
		ch1_coeff = 13363;
	}
	else if ((ratio >= 64) && (ratio < 85))
	{
		ch0_coeff = 16900;
		ch1_coeff = 1690;
	}
	else if (ratio >= 85)
	{
		ch0_coeff = 0;
		ch1_coeff = 0;
	}

	luxdata_int = ((alsval_ch0 * ch0_coeff) - (alsval_ch1 * ch1_coeff))/10000;

	final_lux_val = luxdata_int;
	return luxdata_int;
}
static int pre_final_lux_val = 0;
static void ltr501_schedwork(struct work_struct *work)
{
	//struct ltr501_data *data = container_of((struct delayed_work *)work,struct ltr501_data, work);
	int als_ps_status;
	int interrupt, newdata;
	unsigned long delay = msecs_to_jiffies(atomic_read(&the_data->delay));
	struct input_dev *input_dev = the_data->input_dev;

	als_ps_status = ltr501_i2c_read_reg(LTR501_ALS_PS_STATUS);
	interrupt = als_ps_status & 10;
	newdata = als_ps_status & 5;

	LTR501_DEBUG("interrupt [%d]  newdata  [%d]\n ", interrupt, newdata);
	switch (interrupt){
	case 2:
		// PS interrupt
		if ((newdata == 1) | (newdata == 5)){
			final_prox_val = ltr501_ps_read();
			//input_report_abs(ltr501_input,ABS_DISTANCE, final_prox_val);
			LTR501_DEBUG("final_prox_val [%d]\n", final_prox_val);
			#if 0
			if (final_prox_val >= 0xC8){
				ltr501_i2c_write_reg(0x90, 0xff);
				ltr501_i2c_write_reg(0x91, 0x07);
				LTR501_DEBUG("0x90 = [%x], 0x91 = [%x]\n", ltr501_i2c_read_reg(0x90), ltr501_i2c_read_reg(0x91));

				ltr501_i2c_write_reg(0x92, 0x80);
				ltr501_i2c_write_reg(0x93, 0x00);
				LTR501_DEBUG("0x92 = [%x], 0x93 = [%x]\n", ltr501_i2c_read_reg(0x92), ltr501_i2c_read_reg(0x93));

			}else if (final_prox_val <= 0x80){
				ltr501_i2c_write_reg(0x90, 0xc8);
				ltr501_i2c_write_reg(0x91, 0x00);
				LTR501_DEBUG("0x90 = [%x], 0x91 = [%x]\n", ltr501_i2c_read_reg(0x90), ltr501_i2c_read_reg(0x91));

				ltr501_i2c_write_reg(0x92, 0x00);
				ltr501_i2c_write_reg(0x93, 0x00);
				LTR501_DEBUG("0x92 = [%x], 0x93 = [%x]\n", ltr501_i2c_read_reg(0x92), ltr501_i2c_read_reg(0x93));

			}
			#endif
		}
		break;

	case 8:
		// ALS interrupt
		if ((newdata == 4) | (newdata == 5)){
			final_lux_val = ltr501_als_read(als_gainrange);
			LTR501_DEBUG("final_lux_val [%d]\n", final_lux_val);
		}
		/******************************************************************************
		NOT SUITABLE TO USE THIS METHOD
		Because there is a bug when the value is not changed,the HAL sensor.amlogic.so 
		will not report the value to framework,so when users switch from manual mode to 
		auto mode and the lux val not changed,the framework not adjust the backlight!
		So I record the pre_final_lux_val and add it one when it is the same as the new 
		final_lux_val.the HAL will continue to report the value the framework
		******************************************************************************/
		if(pre_final_lux_val == final_lux_val){
			final_lux_val++;
		}
		input_report_abs(input_dev, ABS_MISC, final_lux_val);
		input_sync(input_dev);
		pre_final_lux_val = final_lux_val;
		break;

	case 10:
		// Both interrupt
		if ((newdata == 1) | (newdata == 5)){
			final_prox_val = ltr501_ps_read();
			LTR501_DEBUG("final_prox_val [%d]\n", final_prox_val);
		}

		if ((newdata == 4) | (newdata == 5)){
			final_lux_val = ltr501_als_read(als_gainrange);
			LTR501_DEBUG("final_lux_val [%d]\n", final_lux_val);
		}
		break;
	}
	schedule_delayed_work(&the_data->work, delay);

}

static int ltr501_dev_init(void)
{
	int ret=0;

	ps_gainrange = PS_RANGE4;
	als_gainrange = ALS_RANGE2_64K;

	msleep(PON_DELAY);
	LTR501_DEBUG("PART_ID[0x86]     = [%x]\n", ltr501_i2c_read_reg(0x86));
	LTR501_DEBUG("MANUFAC_ID[0x87]  = [%x]\n", ltr501_i2c_read_reg(0x87));
	msleep(PON_DELAY);

	ret = ltr501_als_disable();
	if (ret < 0)
		return ret;

	ret = ltr501_ps_disable();
	if (ret < 0)
		return ret;

	ltr501_i2c_write_reg(0x82, 0x7B);
	ltr501_i2c_write_reg(0x83, 0x0f);
	ltr501_i2c_write_reg(0x84, 0x00);
	ltr501_i2c_write_reg(0x85, 0x03);
	ltr501_i2c_write_reg(0x8f, 0x03);//interprete mode set
	ltr501_i2c_write_reg(0x9e, 0x02);

	ltr501_i2c_write_reg(0x90, 0x01);
	ltr501_i2c_write_reg(0x91, 0x00);
	ltr501_i2c_write_reg(0x92, 0x00);
	ltr501_i2c_write_reg(0x93, 0x00);
#if 1 //interprete mode preset value
	ltr501_i2c_write_reg(0x97, 0x00);
	ltr501_i2c_write_reg(0x98, 0x00);
	ltr501_i2c_write_reg(0x99, 0x01);
	ltr501_i2c_write_reg(0x9a, 0x00);
#endif
	mdelay(WAKEUP_DELAY);

	//ltr501_als_enable(als_gainrange);
	//ltr501_ps_enable(ps_gainrange);

	return ret;
}


static ssize_t als_enable_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	int ret = 0;

	ret = sprintf(buf, "Light sensor Auto Enable = %d\n",
			the_data->als_enabled);

	return ret;
}

static ssize_t als_enable_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	int ret = 0;
	int ls_auto;

	ls_auto = -1;
	sscanf(buf, "%d", &ls_auto);

    mutex_lock(&the_data->als_mutex);
	if (!!ls_auto) {
        
        unsigned long delay = msecs_to_jiffies(atomic_read(&the_data->delay));
        ltr501_als_enable(als_gainrange);
        schedule_delayed_work(&the_data->work, delay);
        the_data->als_enabled = 1;    
                
	} else {
        ret = ltr501_als_disable();
        cancel_delayed_work(&the_data->work);
        the_data->als_enabled = 0;    
	}

    mutex_unlock(&the_data->als_mutex);

	return count;
}


static struct device_attribute dev_attr_als_enable =
__ATTR(enable, S_IRUGO | S_IWUSR | S_IWGRP, als_enable_show, als_enable_store);

static struct attribute *sysfs_attrs[] = {
&dev_attr_als_enable.attr,
NULL
};

static struct attribute_group attribute_group = {
.attrs = sysfs_attrs,
};


static ssize_t ltr_dbg_i2c(struct class *class,
                    struct class_attribute *attr, const char *buf, size_t count)
{
	unsigned int reg, val, ret;
	int n=1,i;
	if (buf[0] == 'a'){
		printk("Get all registers for ltr501 \n");
		for(reg=0x80;reg<0x9f;reg++)
		{
			val = ltr501_i2c_read_reg(reg);
			printk("ltr501 reg 0x%x : 0x%x\n", reg, val);
		}
	}
	else if(buf[0] == 'w'){
		ret = sscanf(buf, "w %x %x", &reg, &val);
		//printk("sscanf w reg = %x, val = %x\n",reg, val);
		printk("write cbus reg 0x%x value %x\n", reg, val);
		ltr501_i2c_write_reg(reg, val);
	}else{
		ret =  sscanf(buf, "%x %d", &reg,&n);
		printk("read %d cbus register from reg: %x \n",n,reg);
		for(i=0;i<n;i++)
		{
			val = ltr501_i2c_read_reg(reg+i);
			printk("reg 0x%x : 0x%x\n", reg+i, val);
		}
	}

	if (ret != 1 || ret !=2)
		return -EINVAL;

	return 0;
}

static struct class_attribute ltr_class_attrs[] = {
    __ATTR(cbus_reg,  S_IRUGO | S_IWUSR, NULL,    ltr_dbg_i2c),
    __ATTR_NULL
};

static int ltr501_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret = 0;
	struct input_dev *idev;
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	printk(" start LTR501 probe !!\n");
	/* Return 1 if adapter supports everything we need, 0 if not. */
	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_WRITE_BYTE | I2C_FUNC_SMBUS_READ_BYTE_DATA))
	{
		printk(KERN_ALERT "%s: LTR501-ALS functionality check failed.\n", __func__);
		ret = -EIO;
		return ret;
	}

	//the_data->client = client;

	/* data memory allocation */
	the_data = kzalloc(sizeof(struct ltr501_data), GFP_KERNEL);
	if (the_data == NULL) {
		printk(KERN_ALERT "%s: LTR501-ALS kzalloc failed.\n", __func__);
		ret = -ENOMEM;
		return ret;
	}
	the_data->client = client;

	i2c_set_clientdata(client, the_data);
	/* setup class for dbg */
	the_data->ltr_cls.name = kzalloc(12, GFP_KERNEL);
	sprintf((char*)the_data->ltr_cls.name, "ltr_dbg_i2c");
	the_data->ltr_cls.class_attrs = ltr_class_attrs;
	ret = class_register(&the_data->ltr_cls);
	if(ret)
		printk(" register ltr_dbg_i2c class fail!\n");

	ret = ltr501_dev_init();
	if (ret) {
		printk(KERN_ALERT "%s: LTR501-ALS device init failed.\n", __func__);
		goto kfree_exit;
	}	

	INIT_DELAYED_WORK(&the_data->work, ltr501_schedwork);
	atomic_set(&the_data->delay, LTR501_SCHE_DELAY);

	idev = input_allocate_device();
	if (!idev){
		printk(KERN_ALERT "%s: LTR501-ALS allocate input device failed.\n", __func__);
		goto kfree_exit;
	}
    
	idev->name = LTR501_DEVICE_NAME;
	idev->id.bustype = BUS_I2C;
	input_set_capability(idev, EV_ABS, ABS_MISC);
	input_set_abs_params(idev, ABS_MISC, 0, 64*1024, 0, 0);
	the_data->input_dev = idev;
	input_set_drvdata(idev, the_data);

	ret = input_register_device(idev);
	if (ret < 0) {
		input_free_device(idev);
		goto kfree_exit;
	}

	mutex_init(&the_data->als_mutex);
	/* register the attributes */
	ret = sysfs_create_group(&idev->dev.kobj, &attribute_group);
	if (ret) {
		goto unregister_exit;
	}


	//schedule_delayed_work(&the_data->work, LTR501_SCHE_DELAY);
#ifdef CONFIG_HAS_EARLYSUSPEND
	the_data->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	the_data->early_suspend.suspend = ltr501_early_suspend;
	the_data->early_suspend.resume = ltr501_late_resume;
	register_early_suspend(&the_data->early_suspend);
#endif
	printk("LTR501- probe ok!!\n");
	ret = 0;
	return ret;
unregister_exit:
    input_unregister_device(idev);
    input_free_device(idev);
kfree_exit:
	kfree(the_data);
	return ret;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void ltr501_early_suspend(struct early_suspend *h)
{
	cancel_delayed_work_sync(&the_data->work);
}

static void ltr501_late_resume(struct early_suspend *h)
{
	int ret;
	unsigned long delay = msecs_to_jiffies(atomic_read(&the_data->delay));
    if(the_data->als_enabled)
        schedule_delayed_work(&the_data->work, delay);
	ret = ltr501_dev_init();
	if (ret) {
		printk(KERN_ALERT "%s: LTR501-ALS device init failed.\n", __func__);
		//return ret;
	}
}
#endif

static int ltr501_remove(struct i2c_client *client)
{
	kfree(i2c_get_clientdata(client));

    if(the_data->als_enabled)
        cancel_delayed_work(&the_data->work);

	ltr501_ps_disable();
	ltr501_als_disable();

	return 0;
}


static int ltr501_suspend(struct i2c_client *client, pm_message_t mesg)
{
	int ret = 0;
	LTR501_DEBUG(">>>>>>>>ltr501_suspend\n");
	//cancel_delayed_work(&the_data->work);
	//ret = ltr501_ps_disable();
	//if (ret == 0)
	//	ret = ltr501_als_disable();
	return ret;
}


static int ltr501_resume(struct i2c_client *client)
{
	int ret = 0;
	LTR501_DEBUG("<<<<<<<<<ltr501_resume\n");
	//ret = ltr501_dev_init();
	//if (ret) {
	//	printk(KERN_ALERT "%s: LTR501-ALS device init failed.\n", __func__);
		//return ret;
	//}
	return ret;
}

static const struct i2c_device_id ltr501_id[] = {
	{ LTR501_DEVICE_NAME, 0 },
	{}
};


static struct i2c_driver ltr501_driver = {
	.probe		= ltr501_probe,
	.remove	= ltr501_remove,
	.id_table	= ltr501_id,
	.driver 	= {
		.owner = THIS_MODULE,
		.name  = LTR501_DEVICE_NAME,
	},
	.suspend	= ltr501_suspend,
	.resume	= ltr501_resume,
};


static int __init ltr501_driver_init(void)
{
	return i2c_add_driver(&ltr501_driver);
}


static void __exit ltr501_driver_exit(void)
{
	i2c_del_driver(&ltr501_driver);
	printk(KERN_ALERT ">>> %s: LTR501-ALS Driver Module REMOVED <<<\n", __func__);
}


module_init(ltr501_driver_init)
module_exit(ltr501_driver_exit)

MODULE_AUTHOR("Amlogic 2012");
MODULE_DESCRIPTION("LTR501-ALS Driver");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION(DRIVER_VERSION);

