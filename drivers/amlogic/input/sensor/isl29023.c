/*
 * A iio driver for the light sensor ISL 29023.
 *
 * Hwmon driver for monitoring ambient light intensity in luxi, proximity
 * sensing and infrared sensing.
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA	02110-1301, USA.
 */


/* #define DEBUG 1 */
/* #define VERBOSE_DEBUG 1 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/sensor/isl290xx.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/gpio.h>
#include <linux/kthread.h>
#include <linux/earlysuspend.h>

/**
 * FIXME: This value too short for ADC?
 */

#define  ISL29023_DEVICE 		"isl29023"
#define  ISL29023_INPUT_DEV	"isl29023"

#define CONVERSION_TIME_MS		2

#define ISL29023_REG_ADD_COMMAND1	0x00
#define COMMMAND1_OPMODE_SHIFT		5
#define COMMMAND1_OPMODE_MASK		(7 << COMMMAND1_OPMODE_SHIFT)
#define COMMMAND1_OPMODE_POWER_DOWN	0
#define COMMMAND1_OPMODE_ALS_ONCE	1
#define COMMMAND1_OPMODE_ALS_CONTINUOUS   5 

#define ISL29023_REG_ADD_COMMANDII	0x01
#define COMMANDII_RESOLUTION_SHIFT	2
#define COMMANDII_RESOLUTION_MASK	(0x3 << COMMANDII_RESOLUTION_SHIFT)

#define COMMANDII_RANGE_SHIFT		0
#define COMMANDII_RANGE_MASK		(0x3 << COMMANDII_RANGE_SHIFT)

#define COMMANDII_SCHEME_SHIFT		7
#define COMMANDII_SCHEME_MASK		(0x1 << COMMANDII_SCHEME_SHIFT)

#define ISL29023_REG_ADD_DATA_LSB	0x02
#define ISL29023_REG_ADD_DATA_MSB	0x03

#define ISL29023_REG_INT_LT_LSB		0x04
#define ISL29023_REG_INT_LT_MSB		0x05
#define ISL29023_REG_INT_HT_LSB		0x06
#define ISL29023_REG_INT_HT_MSB		0x07

#define ISL29023_MAX_REGS		ISL29023_REG_INT_HT_MSB

struct isl29023_chip {
	struct i2c_client	*client;
	struct mutex		lock;
	unsigned int		range;
	unsigned int		adc_bit;
	u8			reg_cache[ISL29023_MAX_REGS];
	struct regulator 	*regulator;
	char 			*regulator_name;
	int irq;
	struct input_dev *input_dev;
	#if 0
	struct work_struct	work;
	struct workqueue_struct *isl_work_queue;
	#endif
	uint8_t bThreadRunning;
	int32_t als_lux_last;
	uint32_t als_delay;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend isl_early_suspend;
#endif
};

static struct isl29023_chip *the_data_isl29023 = NULL; 


#define CONFIG_STK_ALS_CHANGE_THRESHOLD	 20

#define STK_LOCK0 mutex_unlock(&isl_io_lock)
#define STK_LOCK1 mutex_lock(&isl_io_lock)


static int polling_function(void* arg);
static struct task_struct *polling_tsk;

static struct mutex isl_io_lock;
static struct completion thread_completion;


static void report_event(struct input_dev* dev,int32_t report_value)
{
	input_report_abs(dev, ABS_MISC, report_value);
	input_sync(dev);
}


static int32_t enable_als(uint32_t enable)
{
	//ret = set_power_state(enable?0:1);
	if (enable)
	{

		if (the_data_isl29023->bThreadRunning == 0)
		{
			the_data_isl29023->als_lux_last = 0;
			the_data_isl29023->bThreadRunning = 1;
			polling_tsk = kthread_run(polling_function,NULL,"als_polling");
		}
		else
		{
		    //WARNING("STK_ALS : thread has running\n");
        }
	}
	else
	{
		if (the_data_isl29023->bThreadRunning)
		{
			the_data_isl29023->bThreadRunning = 0;
			STK_LOCK0;
			wait_for_completion(&thread_completion);
			STK_LOCK1;
			polling_tsk = NULL;
		}
	}
	return 0;
}


static void update_and_check_report_als(int32_t lux)
{
    int32_t lux_last;
    lux_last = the_data_isl29023->als_lux_last;

    if (unlikely(abs(lux - lux_last)>=CONFIG_STK_ALS_CHANGE_THRESHOLD))
    {
        the_data_isl29023->als_lux_last = lux;
		//printk("report_event,lux=%d\n",lux);
        report_event(the_data_isl29023->input_dev,lux);
    }
}
static bool isl29023_read_lux(struct i2c_client *client, int *lux);

static int polling_function(void* arg)
{
	uint32_t lux = 0;
	uint32_t delay;
	init_completion(&thread_completion);

	while (1)
	{
		STK_LOCK1;
		delay = 1000;//the_data_isl29023->als_delay;
		isl29023_read_lux(the_data_isl29023->client,&lux);
        update_and_check_report_als(lux);
		if (the_data_isl29023->bThreadRunning == 0)
			break;
		STK_LOCK0;
		msleep(delay);

	};

    STK_LOCK0;
    complete(&thread_completion);
	return 0;
}


static bool isl29023_write_data(struct i2c_client *client, u8 reg,
	u8 val, u8 mask, u8 shift)
{
	u8 regval;
	int ret = 0;
	struct isl29023_chip *chip = i2c_get_clientdata(client);

	regval = chip->reg_cache[reg];
	regval &= ~mask;
	regval |= val << shift;

	ret = i2c_smbus_write_byte_data(client, reg, regval);
	if (ret) {
		dev_err(&client->dev, "Write to device fails status %x\n", ret);
		return false;
	}
	chip->reg_cache[reg] = regval;
	return true;
}

static void isl29023_disable(void)
{
	printk("isl29023_disable\n");
	isl29023_write_data(the_data_isl29023->client, ISL29023_REG_ADD_COMMAND1,
			0, 0xff, 0);
}

static bool isl29023_set_range(struct i2c_client *client, unsigned long range,
		unsigned int *new_range)
{
	unsigned long supp_ranges[] = {1000, 4000, 16000, 64000};
	int i;

	for (i = 0; i < (ARRAY_SIZE(supp_ranges) -1); ++i) {
		if (range <= supp_ranges[i])
			break;
	}
	*new_range = (unsigned int)supp_ranges[i];

	return isl29023_write_data(client, ISL29023_REG_ADD_COMMANDII,
		i, COMMANDII_RANGE_MASK, COMMANDII_RANGE_SHIFT);
}

static bool isl29023_set_resolution(struct i2c_client *client,
			unsigned long adcbit, unsigned int *conf_adc_bit)
{
	unsigned long supp_adcbit[] = {16, 12, 8, 4};
	int i;

	for (i = 0; i < (ARRAY_SIZE(supp_adcbit)); ++i) {
		if (adcbit == supp_adcbit[i])
			break;
	}
	*conf_adc_bit = (unsigned int)supp_adcbit[i];

	return isl29023_write_data(client, ISL29023_REG_ADD_COMMANDII,
		i, COMMANDII_RESOLUTION_MASK, COMMANDII_RESOLUTION_SHIFT);
}

static int isl29023_read_sensor_input(struct i2c_client *client, int mode)
{
	bool status;
	int lsb;
	int msb;

	/* Set mode */
	status = isl29023_write_data(client, ISL29023_REG_ADD_COMMAND1,
			mode, COMMMAND1_OPMODE_MASK, COMMMAND1_OPMODE_SHIFT);
	if (!status) {
		dev_err(&client->dev, "Error in setting operating mode\n");
		return -EBUSY;
	}

	mdelay(CONVERSION_TIME_MS);
	lsb = i2c_smbus_read_byte_data(client, ISL29023_REG_ADD_DATA_LSB);

	if (lsb < 0) {
		dev_err(&client->dev, "Error in reading LSB DATA\n");
		return lsb;
	}

	msb = i2c_smbus_read_byte_data(client, ISL29023_REG_ADD_DATA_MSB);
	if (msb < 0) {
		dev_err(&client->dev, "Error in reading MSB DATA\n");
		return msb;
	}

	dev_vdbg(&client->dev, "MSB 0x%x and LSB 0x%x\n", msb, lsb);

	return ((msb << 8) | lsb);
}

static bool isl29023_read_lux(struct i2c_client *client, int *lux)
{
	int lux_data;
	struct isl29023_chip *chip = i2c_get_clientdata(client);

	lux_data = isl29023_read_sensor_input(client, COMMMAND1_OPMODE_ALS_CONTINUOUS);
	if (lux_data > 0) {
		*lux = (lux_data * chip->range) >> chip->adc_bit;
		//printk("*lux=%d,lux_data=%d,chip->range=%d,chip->adc_bit=%d\n",*lux,lux_data,chip->range,chip->adc_bit);//edwin
		return true;
	}
	return false;
}
#if 0
static void isl29023_regulator_enable(struct i2c_client *client)
{
	struct isl29023_chip *chip = i2c_get_clientdata(client);

	chip->regulator = regulator_get(NULL, chip->regulator_name);
	if (IS_ERR_OR_NULL(chip->regulator)) {
		dev_err(&client->dev, "Couldn't get regulator %s\n",
				chip->regulator_name);
		chip->regulator = NULL;
	} else {
		regulator_enable(chip->regulator);
		/* Optimal time to get the regulator turned on
		 * before initializing isl29023 chip*/
		mdelay(5);
	}
}
static void isl29023_regulator_disable(struct i2c_client *client)
{
	struct isl29023_chip *chip = i2c_get_clientdata(client);
	struct regulator *isl29023_reg = chip->regulator;
	int ret;

	if (isl29023_reg) {
		ret = regulator_is_enabled(isl29023_reg);
		if (ret > 0)
			regulator_disable(isl29023_reg);
		regulator_put(isl29023_reg);
	}
	chip->regulator = NULL;
}

#endif

static int isl29023_chip_init(struct i2c_client *client)
{
	struct isl29023_chip *chip = i2c_get_clientdata(client);
	bool status;
	int i;
	int new_adc_bit;
	unsigned int new_range;

	//isl29023_regulator_enable(client);

	for (i = 0; i < ARRAY_SIZE(chip->reg_cache); i++) {
		chip->reg_cache[i] = 0;
	}

	/* set defaults */
	status = isl29023_set_range(client, chip->range, &new_range);
	if (status)
		status = isl29023_set_resolution(client, chip->adc_bit,
				&new_adc_bit);
	if (!status) {
		dev_err(&client->dev, "Init of isl29023 fails\n");
		return -ENODEV;
	}
	
	return 0;
}


static void isl29023_early_suspend(struct early_suspend *handler)
{
	/*int ret;
	
	LTR558_DEBUG("%s\n", __func__);
 
 	//ret=ltr558_ps_disable(); 
	if(1 == als_active_ltr558)
	{
  		ret = ltr558_als_disable(); 
	}*/
	//enable_als(0);
	isl29023_disable();
	
}


static void isl29023_early_resume(struct early_suspend *handler)
{	
	
 /*int ret; 
 //ret = ltr558_devinit(); 
 LTR558_DEBUG("%s\n", __func__);
 // ret = ltr558_ps_enable(PS_RANGE1); 
 
 // Enable ALS to Full Range at startup 
  
 //ret = ltr558_als_enable(ALS_RANGE1_320);
 if(1 == als_active_ltr558)
 	ltr558_als_enable(als_gainrange_ltr558);
 */
    //enable_als(1);
}


static int isl29023_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	struct isl290xx_platform_data *pdata;
	struct isl29023_chip *chip;
	int err;
	struct input_dev *input_dev;
	int ret;

	chip = kzalloc(sizeof (struct isl29023_chip), GFP_KERNEL);
	if (!chip) {
		dev_err(&client->dev, "Memory allocation fails\n");
		err = -ENOMEM;
		goto exit;
	}

	i2c_set_clientdata(client, chip);
	chip->client = client;

	mutex_init(&chip->lock);

	pdata = client->dev.platform_data;
	if (pdata) {
		chip->regulator_name = pdata->regulator_name;
		chip->regulator = NULL;
		chip->range = pdata->range;
		chip->adc_bit = pdata->resolution;
	} else {
		chip->regulator_name = NULL;
		chip->regulator = NULL;
		chip->range = 1000;
		chip->adc_bit = 16;
	}

	err = isl29023_chip_init(client);
	if (err)
		goto exit_free;


	input_dev = input_allocate_device();
	if (!input_dev) 
	{
		ret = -ENOMEM;
		goto exit_input_device_alloc_failed;
	}
	chip->input_dev= input_dev;
	input_dev->name = ISL29023_DEVICE;
	input_dev->phys  = ISL29023_DEVICE;
	input_dev->id.bustype = BUS_I2C;
	input_dev->dev.parent = &client->dev;
	input_dev->id.vendor = 0x0001;
	input_dev->id.product = 0x0001;
	input_dev->id.version = 0x0010;
	
	__set_bit(EV_ABS, input_dev->evbit);	

	//for lightsensor
	input_set_abs_params(input_dev, ABS_MISC, 0, 100001, 0, 0);
	
	ret = input_register_device(input_dev);
	if (ret < 0)
	{
		input_free_device(input_dev);
		goto exit_input_register_device_failed;
	}

	mutex_init(&isl_io_lock);

	the_data_isl29023 = chip;

	the_data_isl29023->input_dev = input_dev;

#ifdef CONFIG_HAS_EARLYSUSPEND
	the_data_isl29023->isl_early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB + 25;
	the_data_isl29023->isl_early_suspend.suspend = isl29023_early_suspend;
	the_data_isl29023->isl_early_suspend.resume = isl29023_early_resume;
	register_early_suspend(&the_data_isl29023->isl_early_suspend);
#endif

	enable_als(1);

	return 0;


exit_input_device_alloc_failed:
exit_input_register_device_failed:
    input_unregister_device(input_dev);
	input_free_device(input_dev);

exit_free:
	kfree(chip);
exit:
	return err;
}

static int isl29023_remove(struct i2c_client *client)
{
	struct isl29023_chip *chip = i2c_get_clientdata(client);

	dev_dbg(&client->dev, "%s()\n", __func__);
#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&the_data_isl29023->isl_early_suspend);
#endif

	//isl29023_regulator_disable(client);


	mutex_destroy(&isl_io_lock);
	if (the_data_isl29023)
	{
        input_unregister_device(the_data_isl29023->input_dev);
        input_free_device(the_data_isl29023->input_dev);
		kfree(the_data_isl29023);
		the_data_isl29023 = 0;
	}
	
	kfree(chip);
	return 0;
}

static const struct i2c_device_id isl29023_id[] = {
	{"isl29023", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, isl29023_id);

static struct i2c_driver isl29023_driver = {
	.class	= I2C_CLASS_HWMON,
	.driver  = {
		.name = "isl29023",
		.owner = THIS_MODULE,
	},
	.probe	 = isl29023_probe,
	.remove  = isl29023_remove,
	.id_table = isl29023_id,
};

static int __init isl29023_init(void)
{
	return i2c_add_driver(&isl29023_driver);
	
}

static void __exit isl29023_exit(void)
{
	i2c_del_driver(&isl29023_driver);
}

module_init(isl29023_init);
module_exit(isl29023_exit);
