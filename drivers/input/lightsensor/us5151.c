/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/adc.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/pm.h>
#include <linux/sysctl.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <asm/gpio.h>
#include <asm/uaccess.h>
#include <linux/timer.h>
#include <linux/input.h>
#include <linux/ioctl.h>
//#include <mach/rk29_sdk_io.h>
#include <mach/board.h> 
#include <linux/platform_device.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#define DEBUG 	0

#define  POWER_PIN  NULL//RK29_PIN6_PA0

#if DEBUG
#define DBG(X...)	printk(KERN_NOTICE X)
#else
#define DBG(X...)
#endif

//#define DATA_ADC_CHN	2 	//SARADC_AIN[3]
#define SENSOR_ON	1
#define SENSOR_OFF	0
#define LIGHTSENSOR_IOCTL_MAGIC 'l'
#define LIGHTSENSOR_IOCTL_GET_ENABLED	 _IOR(LIGHTSENSOR_IOCTL_MAGIC, 1, int *) 
#define LIGHTSENSOR_IOCTL_ENABLE	 _IOW(LIGHTSENSOR_IOCTL_MAGIC, 2, int *) 
#define LIGHTSENSOR_IOCTL_DISABLE        _IOW(LIGHTSENSOR_IOCTL_MAGIC, 3, int *)

struct us5151_data {
	struct timer_list	timer;
	struct work_struct	timer_work;
	struct input_dev	*input;
	struct i2c_client *client;
	int statue;
};
#ifdef CONFIG_HAS_EARLYSUSPEND
static struct early_suspend us5151_early_suspend;
#endif
static struct us5151_data *light;

static int us5151_rx_data(struct i2c_client *client, char *rxData, int length)
{
	int ret = 0;
	char reg = rxData[0];
	DBG("%s.....%d...\n",__FUNCTION__,__LINE__);
	ret = i2c_master_reg8_recv(client, reg, rxData, length, 250*1000);
	return (ret > 0)? 0 : ret;
}

static int us5151_tx_data(struct i2c_client *client, char *txData, int length)
{
	int ret = 0;
	char reg = txData[0];
	char i = 0;
	DBG("%s.....%d...\n",__FUNCTION__,__LINE__);

	ret =i2c_master_normal_send(client,txData,length,250*1000);
	//ret = i2c_master_reg8_send(client, reg, &txData[1], length-1, 250*1000);
	return (ret > 0)? 0 : ret;
}

static char start_flag = 0;
static int us5151_start(struct us5151_data *data)
{
	struct us5151_data *us5151 = data;
	int ret = 0;
	char txData[2] = {0x01,0x80}; 
	
	DBG("%s.....%d...\n",__FUNCTION__,__LINE__);
	if(us5151->statue)
		return 0;
	ret = us5151_tx_data(us5151->client,txData,sizeof(txData));
	if (ret == 0)
	{
		us5151->statue = SENSOR_ON;
		us5151->timer.expires  = jiffies + 3*HZ;
		add_timer(&us5151->timer);
	}
	else
	{
		us5151->statue = 0;
		printk("%s......%d ret=%d\n",__FUNCTION__,__LINE__,ret);
	}
	printk("========== us5151 light sensor start ==========\n");
	return 0;
}

static int us5151_stop(struct us5151_data *data)
{
	struct us5151_data *us5151 = data;
	int ret = 0;
	char txData[2] = {0x01,0x00}; 

	DBG("%s.....%d...\n",__FUNCTION__,__LINE__);
	if(us5151->statue == 0)
		return 0;
	
	ret = us5151_tx_data(us5151->client,txData,sizeof(txData));
	if (ret == 0)
	{
		us5151->statue = SENSOR_OFF;
		del_timer(&us5151->timer);
	}
	else
	{
		us5151->statue = 0;
		printk("%s......%d ret=%d\n",__FUNCTION__,__LINE__,ret);
	}
	printk("========== us5151 light sensor stop ==========\n");
	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void us5151_suspend(struct early_suspend *h)
{
	struct us5151_data *us5151 = light;
	
	if (start_flag == 1)
		us5151_stop(us5151);
	
	printk("Light Sensor us5151 enter suspend us5151->status %d\n",us5151->statue);
}

static void us5151_resume(struct early_suspend *h)
{
	struct us5151_data *us5151 = light;
	
	if (start_flag == 1)
		us5151_start(us5151);
	
	printk("Light Sensor us5151 enter resume us5151->status %d\n",us5151->statue);
}
#endif
static int us5151_open(struct inode *indoe, struct file *file)
{
	DBG("%s.....%d\n",__FUNCTION__,__LINE__);
	return 0;
}

static int us5151_release(struct inode *inode, struct file *file)
{
	DBG("%s.....%d\n",__FUNCTION__,__LINE__);
	return 0;
}

static long us5151_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	unsigned long *argp = (unsigned long *)arg;

	DBG("%s.....%d\n",__FUNCTION__,__LINE__);
	switch(cmd){
		case LIGHTSENSOR_IOCTL_GET_ENABLED:
			*argp = light->statue;
			break;
		case LIGHTSENSOR_IOCTL_ENABLE:
			if(*argp)
			{
				start_flag = 1;
				us5151_start(light);
			}
			else
			{
				start_flag = 0;
				us5151_stop(light);
			}
			break;
		default:break;
	}
	return 0;
}

static void us5151_value_report(struct input_dev *input, int data)
{
	unsigned char index = 0;
	if(data <= 30){
		index = 0;goto report;
	}
	else if(data <= 100){
		index = 1;goto report;
	}
	else if(data <= 150){
		index = 2;goto report;
	}
	else if(data <= 220){
		index = 3;goto report;
	}
	else if(data <= 280){
		index = 4;goto report;
	}
	else if(data <= 350){
		index = 5;goto report;
	}
	else if(data <= 420){
		index = 6;goto report;
	}
	else{
		index = 7;goto report;
	}
report:
	DBG("us5151 report index = %d data=%d\n",index,data);
	input_report_abs(input, ABS_MISC, index);
	input_sync(input);
	return;
}

static void us5151_read(struct work_struct *work)
{

	int ret = 0;
	int adc_value = 35541;
	char value[2] = {0x02,0x03};
	char enable[1] = {0x01};

	struct us5151_data *us5151 = container_of(work, struct us5151_data,timer_work);

	ret = us5151_rx_data(us5151->client,value,sizeof(value));
	ret = us5151_rx_data(us5151->client,enable,sizeof(enable));
	if (ret == 0)
	{
		adc_value = value[1]>>7 | value[0]<<1;
		DBG("%s......%d ret=%d value[0]=%d value[1]=%d adc_value=%d enable=%d\n",__FUNCTION__,__LINE__,ret,value[0],value[1],adc_value,enable[0]);		
		us5151_value_report(us5151->input,adc_value);
	}
	else
	{
		//us5151->statue = 0;
		printk("%s......%d ret=%d value[0]=%d value[1]=%d adc_value=%d\n",__FUNCTION__,__LINE__,ret,value[0],value[1],adc_value);
	}

	if(us5151->statue){
		us5151->timer.expires  = jiffies + 3*HZ;
		add_timer(&us5151->timer);
	}
};

static void us5151_read_value(unsigned long data)
{
	struct us5151_data *us5151=(struct us5151_data *)data;
	schedule_work(&us5151->timer_work);
};
static struct file_operations us5151_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = us5151_ioctl,
	.open = us5151_open,
	.release = us5151_release,
};

static struct miscdevice us5151_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "lightsensor",
	.fops = &us5151_fops,
};

static ssize_t us5151_start_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{

	DBG("%s......\n",__FUNCTION__);
	if (*buf == 'A')
		us5151_start(light);
	else
	{
		us5151_stop(light);
		printk("%s.........%s\n",__FUNCTION__,buf);
	}

	return len;
};
static DEVICE_ATTR(start,0666,NULL,us5151_start_store);

static int us5151_probe(struct i2c_client *pdev,const struct i2c_device_id *id)
{
	struct us5151_data *us5151;
	struct us5151_platform_data *pdata = pdata = pdev->dev.platform_data;
	int err;
	DBG("============= us5151 probe enter ==============\n");
	us5151 = kmalloc(sizeof(struct us5151_data), GFP_KERNEL);
	if(!us5151){
		printk("us5151 alloc memory err !!!\n");
		err = -ENOMEM;
		goto alloc_memory_fail;
	}
	light = us5151;
	us5151->statue = SENSOR_OFF;
	us5151->client = pdev;
	us5151->input = input_allocate_device();
	if (!us5151->input) {
		err = -ENOMEM;
		printk(KERN_ERR"us5151: Failed to allocate input device\n");
		goto exit_input_allocate_device_failed;
	}
	set_bit(EV_ABS, us5151->input->evbit);
	/* light sensor data */
	input_set_abs_params(us5151->input, ABS_MISC, 0, 0x1ff, 0, 0);
	us5151->input->name = "lightsensor-level";

	err = input_register_device(us5151->input);
	if (err < 0) {
		printk(KERN_ERR"us5151: Unable to register input device: %s\n",us5151->input->name);						
		goto exit_input_register_device_failed;
	}

	INIT_WORK(&us5151->timer_work,us5151_read);
	setup_timer(&us5151->timer, us5151_read_value, (unsigned long)us5151);
	
	err = misc_register(&us5151_device);
	if (err < 0) {
		printk(KERN_ERR"us5151_probe: lightsensor_device register failed\n");
		goto exit_misc_register_fail;
	}
	
     err = device_create_file(&pdev->dev, &dev_attr_start);
     if (err)
     {
	
		printk("%s....make a mistake in creating devices  attr file\n\n ",__FUNCTION__);
     }

#ifdef CONFIG_HAS_EARLYSUSPEND
	us5151_early_suspend.suspend = us5151_suspend;
	us5151_early_suspend.resume = us5151_resume;
	us5151_early_suspend.level = 0x2;
	register_early_suspend(&us5151_early_suspend);
#endif

	printk("lightsensor us5151 driver created !\n");
	//us5151_start(light);
	return 0;
exit_misc_register_fail:
	input_unregister_device(us5151->input);
exit_input_register_device_failed:
	input_free_device(us5151->input);
exit_input_allocate_device_failed:
	kfree(us5151);
alloc_memory_fail:
	printk("%s error\n",__FUNCTION__);
	return err;
}

static __devexit int  us5151_remove(struct i2c_client *pdev)
{
	struct us5151_data *us5151 = light;
	kfree(us5151);
	input_free_device(us5151->input);
	input_unregister_device(us5151->input);
	misc_deregister(&us5151_device);
	return 0;
}

static const struct i2c_device_id us5151_i2c_id[] = {
	{ "us5151", 0 },
	{ }
};

static struct i2c_driver us5151_driver = {
	.probe = us5151_probe,
	.remove = __devexit_p(us5151_remove),
	.driver = {
		.owner = THIS_MODULE,
		.name = "lightsensor",
	},
	.id_table = us5151_i2c_id,
};

static int __init us5151_init(void)
{
	return i2c_add_driver(&us5151_driver);
}

static void __exit us5151_exit(void)
{
	i2c_del_driver(&us5151_driver);
}

module_init(us5151_init);
module_exit(us5151_exit);
