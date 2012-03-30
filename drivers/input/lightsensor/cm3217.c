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

#define CM3217_I2C_RATE		(200*1000)
#define CM3217_ADDR_COM1	0x10
#define CM3217_ADDR_COM2	0x11
#define CM3217_ADDR_DATA_MSB	0x10
#define CM3217_ADDR_DATA_LSB	0x11

#define CM3217_COM1_VALUE	0xA7	// (GAIN1:GAIN0)=10, (IT_T1:IT_TO)=01,WMD=1,SD=1,
#define CM3217_COM2_VALUE	0xA0	//100ms


#define SENSOR_ON	1
#define SENSOR_OFF	0
#define LIGHTSENSOR_IOCTL_MAGIC 'l'
#define LIGHTSENSOR_IOCTL_GET_ENABLED	 _IOR(LIGHTSENSOR_IOCTL_MAGIC, 1, int *) 
#define LIGHTSENSOR_IOCTL_ENABLE	 _IOW(LIGHTSENSOR_IOCTL_MAGIC, 2, int *) 
#define LIGHTSENSOR_IOCTL_DISABLE        _IOW(LIGHTSENSOR_IOCTL_MAGIC, 3, int *)

struct cm3217_data {
	struct i2c_client	*client;
	struct timer_list	timer;
	struct work_struct	timer_work;
	struct input_dev	*input;
	int power_pin;
	int irq_pin;
	int status;
};
#ifdef CONFIG_HAS_EARLYSUSPEND
static struct early_suspend cm3217_early_suspend;
#endif
static struct cm3217_data *glight;

static int cm3217_command_set(struct i2c_client *client, char *buf, int num)
{
	int ret = 0;
	ret = i2c_master_normal_send(client, buf, num, CM3217_I2C_RATE);
	
	return (ret == num) ? 0 : ret;
}

static int cm3217_command_get(struct i2c_client *client, char *buf, int num)
{
	int ret = 0;
	ret = i2c_master_normal_recv(client, buf, num, CM3217_I2C_RATE);
	
	return (ret == num) ? 0 : ret;
}


static int cm3217_start(struct cm3217_data *data)
{
	struct cm3217_data *cm3217 = data;
	char buf = 0;
	if(cm3217->status)
		return 0;
	
	//if(cm3217->power_pin != INVALID_GPIO)
	//gpio_direction_output(cm3217->power_pin,0);//level = 0 Sensor ON
	
	buf = CM3217_COM1_VALUE & 0xfe ;	//SD=0
	cm3217->client->addr = CM3217_ADDR_COM1;
	cm3217_command_set(cm3217->client, &buf, 1);
	
	cm3217->status = SENSOR_ON;
	cm3217->timer.expires  = jiffies + 1*HZ;
	add_timer(&cm3217->timer);
	DBG("cm3217 light sensor start\n");
	return 0;
}

static int cm3217_stop(struct cm3217_data *data)
{
	struct cm3217_data *cm3217 = data;
	char buf = 0;
	if(cm3217->status == 0)
		return 0;
	
	//if(cm3217->power_pin != INVALID_GPIO)
	//gpio_direction_output(cm3217->power_pin,1);//level = 1 Sensor OFF
	
	buf = CM3217_COM1_VALUE | 0x01 ;	//SD=1
	cm3217->client->addr = CM3217_ADDR_COM1;
	cm3217_command_set(cm3217->client, &buf, 1);
	
	cm3217->status = SENSOR_OFF;
	del_timer(&cm3217->timer);
	DBG("cm3217 light sensor stop\n");
	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void cm3217_suspend(struct early_suspend *h)
{
	struct cm3217_data *cm3217 = glight;
	cm3217_stop(cm3217);
	DBG("Light Sensor cm3217 enter suspend cm3217->status %d\n",cm3217->status);
}

static void cm3217_resume(struct early_suspend *h)
{
	struct cm3217_data *cm3217 = glight;
	cm3217_start(cm3217);
	DBG("Light Sensor cm3217 enter resume cm3217->status %d\n",cm3217->status);
}
#endif
static int cm3217_open(struct inode *indoe, struct file *file)
{
	return 0;
}

static int cm3217_release(struct inode *inode, struct file *file)
{
	return 0;
}


static long cm3217_ioctl( struct file *file, unsigned int cmd, unsigned long arg)
{
	unsigned int *argp = (unsigned int *)arg;	
	struct cm3217_data *cm3217 = glight;
	switch(cmd){
		case LIGHTSENSOR_IOCTL_GET_ENABLED:
			*argp = cm3217->status;
			break;
		case LIGHTSENSOR_IOCTL_ENABLE:
			if(*argp)
				cm3217_start(cm3217);
			else
				cm3217_stop(cm3217);
			break;
		default:break;
	}
	return 0;
}

static void cm3217_value_report(struct input_dev *input, int data)
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
	DBG("cm3217 report data=%d,index = %d\n",data,index);
	input_report_abs(input, ABS_MISC, index);
	input_sync(input);
	return;
}

static void adc_timer(unsigned long data)
{
	struct cm3217_data *cm3217=(struct cm3217_data *)data;
	schedule_work(&cm3217->timer_work);
}

static void adc_timer_work(struct work_struct *work)
{
	int result = 0;
	struct cm3217_data *cm3217 = container_of(work, struct cm3217_data,timer_work);
	char msb = 0, lsb = 0;

	cm3217->client->addr = CM3217_ADDR_DATA_LSB;
	cm3217_command_get(cm3217->client, &lsb, 1);
	cm3217->client->addr = CM3217_ADDR_DATA_MSB;
	cm3217_command_get(cm3217->client, &msb, 1);
	result = ((msb << 8) | lsb) & 0xffff;
	cm3217_value_report(cm3217->input, result);
	DBG("%s:result=%d\n",__func__,result);
	
	if(cm3217->status){
		cm3217->timer.expires  = jiffies + 3*HZ;
		add_timer(&cm3217->timer);
	}
}

static struct file_operations cm3217_fops = {
	.owner = THIS_MODULE,
	.open = cm3217_open,
	.release = cm3217_release,
	.unlocked_ioctl = cm3217_ioctl,
};

static struct miscdevice cm3217_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "lightsensor",
	.fops = &cm3217_fops,
};

static int cm3217_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct cm3217_data *cm3217;	
	struct cm3217_platform_data *pdata = pdata = client->dev.platform_data;	
	char com1 = CM3217_COM1_VALUE, com2 = CM3217_COM2_VALUE;
	int err;
	
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) 
	{
		dev_err(&client->dev, "Must have I2C_FUNC_I2C.\n");
		err = -ENODEV;
		goto alloc_memory_fail;
	}
		
	cm3217 = kmalloc(sizeof(struct cm3217_data), GFP_KERNEL);
	if(!cm3217){
		printk("cm3217 alloc memory err !!!\n");
		err = -ENOMEM;
		goto alloc_memory_fail;
	}	
	
	if(pdata->init_platform_hw)
	pdata->init_platform_hw();

	cm3217->client = client;
	i2c_set_clientdata(client, cm3217);	
	cm3217->power_pin = pdata->power_pin;
	cm3217->irq_pin = pdata->irq_pin;
	cm3217->status = SENSOR_OFF;	
	glight = cm3217;

	//init cm3217
	client->addr = CM3217_ADDR_COM1;
	cm3217_command_set(client, &com1, 1);	
	client->addr = CM3217_ADDR_COM2;
	cm3217_command_set(client, &com2, 1);	
	
	cm3217->input = input_allocate_device();
	if (!cm3217->input) {
		err = -ENOMEM;
		printk(KERN_ERR"cm3217: Failed to allocate input device\n");
		goto exit_input_allocate_device_failed;
	}
	set_bit(EV_ABS, cm3217->input->evbit);
	/* light sensor data */
	input_set_abs_params(cm3217->input, ABS_MISC, 0, 10, 0, 0);
	cm3217->input->name = "lightsensor-level";

	err = input_register_device(cm3217->input);
	if (err < 0) {
		printk(KERN_ERR"cm3217: Unable to register input device: %s\n",cm3217->input->name);						
		goto exit_input_register_device_failed;
	}
/*	
	if(cm3217->power_pin != INVALID_GPIO)
	{
		gpio_request(cm3217->power_pin, "cm3217_power_pin");
		gpio_pull_updown(cm3217->power_pin,PullDisable); 
	}
*/	
	INIT_WORK(&cm3217->timer_work, adc_timer_work);
	setup_timer(&cm3217->timer, adc_timer, (unsigned long)cm3217);
	err = misc_register(&cm3217_device);
	if (err < 0) {
		printk(KERN_ERR"cm3217_probe: lightsensor_device register failed\n");
		goto exit_misc_register_fail;
	}
	printk("lightsensor cm3217 driver created !\n");
	//cm3217_start(cm3217);
#ifdef CONFIG_HAS_EARLYSUSPEND
	cm3217_early_suspend.suspend = cm3217_suspend;
	cm3217_early_suspend.resume = cm3217_resume;
	cm3217_early_suspend.level = 0x2;
	register_early_suspend(&cm3217_early_suspend);
#endif
	return 0;
exit_misc_register_fail:
	//gpio_free(pdata->power_pin);
	input_unregister_device(cm3217->input);
exit_input_register_device_failed:
	input_free_device(cm3217->input);
exit_input_allocate_device_failed:
	kfree(cm3217);
alloc_memory_fail:
	printk("%s error\n",__FUNCTION__);
	return err;
}

static int cm3217_remove(struct i2c_client *client)
{	
	struct cm3217_data *cm3217 = i2c_get_clientdata(client);
	kfree(cm3217);
	input_free_device(cm3217->input);
	input_unregister_device(cm3217->input);
	misc_deregister(&cm3217_device);
	return 0;
}

static const struct i2c_device_id cm3217_id[] = {
	{ "lightsensor", 0 },
};


static struct i2c_driver cm3217_driver = {
	.probe = cm3217_probe,
	.remove = __devexit_p(cm3217_remove),
	.id_table = cm3217_id,
	.driver = {
		.owner = THIS_MODULE,
		.name = "lightsensor",
	}
};


static int __init cm3217_init(void)
{
	return i2c_add_driver(&cm3217_driver);
}

static void __exit cm3217_exit(void)
{
	i2c_del_driver(&cm3217_driver);
}

module_init(cm3217_init);
module_exit(cm3217_exit);
