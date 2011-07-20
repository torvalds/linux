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

#if DEBUG
#define DBG(X...)	printk(KERN_NOTICE X)
#else
#define DBG(X...)
#endif

//#define CM3202_SD_IOPIN		LIGHT_INT_IOPIN//light sensor int Pin  <level=Low "ON"> <level=Hight "OFF">
//#define DATA_ADC_CHN	2 	//SARADC_AIN[3]
#define SENSOR_ON	1
#define SENSOR_OFF	0
#define LIGHTSENSOR_IOCTL_MAGIC 'l'
#define LIGHTSENSOR_IOCTL_GET_ENABLED	 _IOR(LIGHTSENSOR_IOCTL_MAGIC, 1, int *) 
#define LIGHTSENSOR_IOCTL_ENABLE	 _IOW(LIGHTSENSOR_IOCTL_MAGIC, 2, int *) 
#define LIGHTSENSOR_IOCTL_DISABLE        _IOW(LIGHTSENSOR_IOCTL_MAGIC, 3, int *)

struct cm3202_data {
	struct adc_client	*client;
	struct timer_list	timer;
	struct work_struct	timer_work;
	struct input_dev	*input;
	int CM3202_SD;
	int statue;
};
#ifdef CONFIG_HAS_EARLYSUSPEND
static struct early_suspend cm3202_early_suspend;
#endif
static struct cm3202_data *light;

static int cm3202_start(struct cm3202_data *data)
{
	struct cm3202_data *cm3202 = data;
	if(cm3202->statue)
		return 0;
	gpio_direction_output(cm3202->CM3202_SD,0);//level = 0 Sensor ON
	cm3202->statue = SENSOR_ON;
	cm3202->timer.expires  = jiffies + 1*HZ;
	add_timer(&cm3202->timer);
	printk("========== cm3202 light sensor start ==========\n");
	return 0;
}

static int cm3202_stop(struct cm3202_data *data)
{
	struct cm3202_data *cm3202 = data;
	if(cm3202->statue == 0)
		return 0;
	gpio_direction_output(cm3202->CM3202_SD,1);//level = 1 Sensor OFF
	cm3202->statue = SENSOR_OFF;
	del_timer(&cm3202->timer);
	printk("========== cm3202 light sensor stop ==========\n");
	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void cm3202_suspend(struct early_suspend *h)
{
	struct cm3202_data *cm3202 = light;
	cm3202_stop(cm3202);
	printk("Light Sensor cm3202 enter suspend cm3202->status %d\n",cm3202->statue);
}

static void cm3202_resume(struct early_suspend *h)
{
	struct cm3202_data *cm3202 = light;
	cm3202_start(cm3202);
	printk("Light Sensor cm3202 enter resume cm3202->status %d\n",cm3202->statue);
}
#endif
static int cm3202_open(struct inode *indoe, struct file *file)
{
	return 0;
}

static int cm3202_release(struct inode *inode, struct file *file)
{
	return 0;
}

static int cm3202_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	unsigned long *argp = (unsigned char *)arg;
	switch(cmd){
		case LIGHTSENSOR_IOCTL_GET_ENABLED:
			*argp = light->statue;
			break;
		case LIGHTSENSOR_IOCTL_ENABLE:
			if(*argp)
				cm3202_start(light);
			else
				cm3202_stop(light);
			break;
		default:break;
	}
	return 0;
}

static void cm3202_value_report(struct input_dev *input, int data)
{
	unsigned char index = 0;
	if(data <= 10){
		index = 0;goto report;
	}
	else if(data <= 160){
		index = 1;goto report;
	}
	else if(data <= 225){
		index = 2;goto report;
	}
	else if(data <= 320){
		index = 3;goto report;
	}
	else if(data <= 640){
		index = 4;goto report;
	}
	else if(data <= 1280){
		index = 5;goto report;
	}
	else if(data <= 2600){
		index = 6;goto report;
	}
	else{
		index = 7;goto report;
	}
report:
	DBG("cm3202 report index = %d\n",index);
	input_report_abs(input, ABS_MISC, index);
	input_sync(input);
	return;
}
static void callback(struct adc_client *client, void *param, int result)
{
	DBG("[chn%d] cm3202 report value: %d\n", client->chn, result);
	return;
}
static void adc_timer(unsigned long data)
{
	struct cm3202_data *cm3202=(struct cm3202_data *)data;
	schedule_work(&cm3202->timer_work);
}
static void adc_timer_work(struct work_struct *work)
{
	int sync_read = 0;
	struct cm3202_data *cm3202 = container_of(work, struct cm3202_data,timer_work);
	adc_async_read(cm3202->client);
	sync_read = adc_sync_read(cm3202->client);
	cm3202_value_report(cm3202->input, sync_read);
	if(cm3202->statue){
		cm3202->timer.expires  = jiffies + 3*HZ;
		add_timer(&cm3202->timer);
	}
}

static struct file_operations cm3202_fops = {
	.owner = THIS_MODULE,
	.ioctl = cm3202_ioctl,
	.open = cm3202_open,
	.release = cm3202_release,
};

static struct miscdevice cm3202_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "lightsensor",
	.fops = &cm3202_fops,
};

static int cm3202_probe(struct platform_device *pdev)
{
	struct cm3202_data *cm3202;
	struct cm3202_platform_data *pdata = pdata = pdev->dev.platform_data;
	int err;
	DBG("============= cm3202 probe enter ==============\n");
	cm3202 = kmalloc(sizeof(struct cm3202_data), GFP_KERNEL);
	if(!cm3202){
		printk("cm3202 alloc memory err !!!\n");
		err = -ENOMEM;
		goto alloc_memory_fail;
	}	
		if(pdata->init_platform_hw)
		pdata->init_platform_hw();

	cm3202->CM3202_SD = pdata->CM3202_SD_IOPIN;
	DBG("===============================cm3202==========================\ncm3202_ADC_CHN = %d",pdata->DATA_ADC_CHN);
	light = cm3202;
	cm3202->client = adc_register(pdata->DATA_ADC_CHN, callback, NULL);
	cm3202->statue = SENSOR_OFF;
	cm3202->input = input_allocate_device();
	if (!cm3202->input) {
		err = -ENOMEM;
		printk(KERN_ERR"cm3202: Failed to allocate input device\n");
		goto exit_input_allocate_device_failed;
	}
	set_bit(EV_ABS, cm3202->input->evbit);
	/* light sensor data */
	input_set_abs_params(cm3202->input, ABS_MISC, 0, 0x3ff, 0, 0);
	cm3202->input->name = "lightsensor";

	err = input_register_device(cm3202->input);
	if (err < 0) {
		printk(KERN_ERR"cm3202: Unable to register input device: %s\n",cm3202->input->name);						
		goto exit_input_register_device_failed;
	}
	/*
	ret = gpio_request(CM3202_SD_IOPIN, "cm3202_sd");
	if (ret) {
		printk( "failed to request cm3202 SD GPIO%d\n",CM3202_SD_IOPIN);
		goto exit_gpio_request_fail;
	}
	DBG("cm3202 request INT inpin ok !!!");
	gpio_pull_updown(CM3202_SD_IOPIN,GPIOPullDown); */
	INIT_WORK(&cm3202->timer_work, adc_timer_work);
	setup_timer(&cm3202->timer, adc_timer, (unsigned long)cm3202);
	err = misc_register(&cm3202_device);
	if (err < 0) {
		printk(KERN_ERR"cm3202_probe: lightsensor_device register failed\n");
		goto exit_misc_register_fail;
	}
	printk("lightsensor cm3202 driver created !\n");
	//cm3202_start(light);
#ifdef CONFIG_HAS_EARLYSUSPEND
	cm3202_early_suspend.suspend = cm3202_suspend;
	cm3202_early_suspend.resume = cm3202_resume;
	cm3202_early_suspend.level = 0x2;
	register_early_suspend(&cm3202_early_suspend);
#endif
	return 0;
exit_misc_register_fail:
	gpio_free(pdata->CM3202_SD_IOPIN);
	input_unregister_device(cm3202->input);
exit_input_register_device_failed:
	input_free_device(cm3202->input);
exit_input_allocate_device_failed:
	kfree(cm3202);
alloc_memory_fail:
	printk("%s error\n",__FUNCTION__);
	return err;
}

static int cm3202_remove(struct platform_device *pdev)
{
	struct cm3202_data *cm3202 = light;
	kfree(cm3202);
	input_free_device(cm3202->input);
	input_unregister_device(cm3202->input);
	misc_deregister(&cm3202_device);
	return 0;
}

static struct platform_driver cm3202_driver = {
	.probe = cm3202_probe,
	.remove = cm3202_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = "lightsensor",
	}
};

static int __init cm3202_init(void)
{
	return platform_driver_register(&cm3202_driver);
}

static void __exit cm3202_exit(void)
{
	platform_driver_unregister(&cm3202_driver);
}

module_init(cm3202_init);
module_exit(cm3202_exit);
