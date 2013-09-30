/*
 *
 * Copyright (C) 2012 Rockchip
 *
 *---------------------------------
 * version 1.0 2012-9-13
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/sysfs.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/version.h>
#include <linux/pm_runtime.h>
#include <asm/uaccess.h>
#include "scaler-core.h"

#define SCALER_CORE_VERSION "v1.0.0"
#define SCALER_DEV_NAME "scaler-ctrl"

static DEFINE_MUTEX(mutex_chips);
static DEFINE_MUTEX(mutex_ports);

static struct scaler_device *sdev = NULL;
static unsigned short chip_ids = 0;       //<id only grow>只增不减
static unsigned int port_ids = 0;

static const char const *scaler_output_name[] = {
	"output type",
	"LVDS",
	"VGA",
	"RGB",
	"HDMI",
	"DP",
};

const char const *scaler_input_name[] = {
	"input tpye",
	"VGA",
	"RGB",
	"DVI",
	"YPBPR",
	"YCBCR",
	"DP",
	"HDMI",
	"MYDP",
	"IDP",
};

static const char const *scaler_func_name[] ={
	"This Scaler Function Type Is:",
	"convertor",
	"switch",
	"full function",
};

static void scaler_led_on(int gpio)
{
	gpio_set_value(gpio, GPIO_HIGH);	
}

static void scaler_led_off(int gpio)
{
	gpio_set_value(gpio, GPIO_LOW);	
}

static int default_read(unsigned short reg, int bytes, void *desc)
{
	printk("Don't init read register func\n");
	return 0;
}

static int default_write(unsigned short reg, int bytes, void *desc)
{
	printk("Don't init write register func\n");
	return 0;
}

static int default_parse_cmd(unsigned int cmd, void *arg)
{
	struct scaler_chip_dev *chip = NULL;
	printk("Don't init parse_scaler_cmd funct\n");

	list_for_each_entry(chip, &sdev->chips, next) {
		chip->parse_cmd(cmd, arg);
	}

	return 0;
}

//alloc chip dev memory and init default value
struct scaler_chip_dev *alloc_scaler_chip(void)
{
	struct scaler_chip_dev *p = kzalloc(sizeof(struct scaler_chip_dev), GFP_KERNEL);
	if (p) {
		strcpy(p->name, "unknown");
		//init default operation function
		p->read = default_read;
		p->write = default_write;
		p->parse_cmd = default_parse_cmd;
		//init list head
		INIT_LIST_HEAD(&p->next);
		INIT_LIST_HEAD(&p->oports);
		INIT_LIST_HEAD(&p->iports);
	}
	return p;
}

int init_scaler_chip(struct scaler_chip_dev *chip, struct scaler_platform_data *pdata)
{
	int i;
	struct scaler_output_port *oport = NULL;
	struct scaler_input_port *iport = NULL;

	if (!chip || !pdata) {
		printk("%s: chip or pdata is null.\n", __func__);
		return -1;
	}
	
	//set scaler function type
	if (pdata->func_type > SCALER_FUNC_INVALID && 
			pdata->func_type < SCALER_FUNC_NUMS) {
		chip->func_type = pdata->func_type;
	}else {
		printk("%s: not defined scaer function type\n", __func__);
		chip->func_type = SCALER_FUNC_FULL;
	}
	printk("%s: %s %s\n", chip->name, scaler_func_name[0], scaler_func_name[chip->func_type]);

	//set scaler support input type
	for (i = 0; i < pdata->iport_size; i++) {
		iport = kzalloc(sizeof(struct scaler_input_port), GFP_KERNEL);
		if (!iport) {
		    printk("%s: kzalloc input port memeory failed.\n", __func__);
		    return -1;
		}else {
			iport->max_hres = 1920;
			iport->max_vres = 1080;
			iport->freq = 60;
			//input port id
			mutex_lock(&mutex_ports);
			iport->id = ++port_ids;
			mutex_unlock(&mutex_ports);
			//input port type
			iport->type = pdata->iports[i];

			//init and request input gpio of indicator lamp 
			if (pdata->in_leds_gpio[pdata->iports[i]] > 0) {
				iport->led_gpio = pdata->in_leds_gpio[pdata->iports[i]];
				if (gpio_request(iport->led_gpio, scaler_input_name[pdata->iports[i]])) {
					gpio_direction_output(iport->led_gpio, GPIO_HIGH);	
				}
			}

			//add input of chip
			list_add(&iport->next, &chip->iports);
			printk("%s:  support %s %s<id,%d>\n", chip->name, scaler_input_name[0], scaler_input_name[pdata->iports[i]], iport->id);
		}
	}

	//default input type
	if (pdata->default_in > SCALER_IN_INVALID && 
			pdata->default_in < SCALER_IN_NUMS) {
		chip->cur_in_type = pdata->default_in;
	}else {
		chip->cur_in_type = SCALER_IN_VGA;
	}
	printk("%s:  default %s %s\n", chip->name, scaler_input_name[0], scaler_input_name[pdata->default_in]);

	//set scaler output type
	for (i = 0; i < pdata->oport_size; i++) {
		oport = kzalloc(sizeof(struct scaler_output_port), GFP_KERNEL);
		if (!oport) {
		    printk("%s: kzalloc output port memeory failed.\n", __func__);
		    return -1;
		}else {
			oport->max_hres = 1920;
			oport->max_vres = 1080;
			oport->freq = 60;
			//output port id
			mutex_lock(&mutex_ports);
			oport->id = ++port_ids;
			mutex_unlock(&mutex_ports);
			//output port type
			oport->type = pdata->oports[i];

			//init and request output gpio of indicator lamp 
			if (pdata->out_leds_gpio[pdata->oports[i]] > 0) {
				oport->led_gpio = pdata->out_leds_gpio[pdata->iports[i]];
				if (gpio_request(oport->led_gpio, scaler_output_name[pdata->oports[i]])) {
					gpio_direction_output(oport->led_gpio, GPIO_HIGH);	
				}
			}

			list_add(&oport->next, &chip->oports);
			printk("%s:  support %s %s<id,%d>\n", chip->name, scaler_output_name[0], scaler_output_name[pdata->oports[i]], oport->id);
		}
	}

	//default output type
	if (pdata->default_out > SCALER_OUT_INVALID && 
			pdata->default_out < SCALER_OUT_NUMS) {
		chip->cur_out_type = pdata->default_out;
	}else {
		chip->cur_out_type = SCALER_OUT_VGA;
	}
	printk("%s:  default %s %s\n", chip->name, scaler_output_name[0], scaler_output_name[pdata->default_out]);

	return 0;
}

//free scaler chip memory
void free_scaler_chip(struct scaler_chip_dev *chip)
{
	struct scaler_output_port *out = NULL;
	struct scaler_input_port *in = NULL;
	if (chip) {

		list_for_each_entry(out, &chip->oports, next) {
			kfree(out);
		}

		list_for_each_entry(in, &chip->iports, next) {
			if (in->led_gpio > 0)
				gpio_free(in->led_gpio);
			kfree(in);
		}

		kfree(chip);
	}
}

//register chip to scaler core
int register_scaler_chip(struct scaler_chip_dev *chip)
{
	int res = -1;
	struct scaler_input_port *in;
	struct scaler_output_port *out;
	
	if (chip && sdev) {
		res = 0;

		//start chip to process
		if (chip->start)
			chip->start();

		//power on input indicator lamp
		list_for_each_entry(in, &chip->iports, next){
			if (in->type == chip->cur_in_type)
				scaler_led_on(in->led_gpio);
			scaler_led_off(in->led_gpio);
		}

		//power on output indicator lamp
		list_for_each_entry(out, &chip->oports, next){
			if (out->type == chip->cur_out_type)
				scaler_led_on(out->led_gpio);
			scaler_led_off(out->led_gpio);
		}

		//add chip to scaler core
		mutex_lock(&mutex_chips);
		chip->id = ++chip_ids;  //chip id only grow
		list_add(&chip->next, &sdev->chips);
		mutex_unlock(&mutex_chips);
		printk("%s: register scaler chip %s success.\n", __func__, chip->name);
	}

	return res;
}

//unregister chip to scaler core
int unregister_scaler_chip(struct scaler_chip_dev *chip)
{
	int res = -1;
	struct scaler_input_port *in;
	struct scaler_output_port *out;
	
	if (chip && sdev) {
		res = 0;

		if (chip->stop)
			chip->stop();

		//power off input indicator lamp
		list_for_each_entry(in, &chip->iports, next){
			if (in->type == chip->cur_in_type)
				scaler_led_off(in->led_gpio);
		}

		//power off output indicator lamp
		list_for_each_entry(out, &chip->oports, next){
			if (out->type == chip->cur_out_type)
				scaler_led_off(out->led_gpio);
		}

		//del chip from scaler core
		mutex_lock(&mutex_chips);
		list_del(&chip->next);
		mutex_unlock(&mutex_chips);
		printk("%s: unregister scaler chip %s success.\n", __func__, chip->name);
	}

	return res;
}

/***        cdev operate ***/
static int scaler_file_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static int scaler_file_close(struct inode *inode, struct file *filp)
{
	return 0;
}

static ssize_t scaler_file_read(struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
	return 0;
}

static ssize_t scaler_file_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos)
{
	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36)
static long scaler_file_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
#else
static int  scaler_file_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg)
#endif
{
	switch(cmd) {
		case 0:
			break;
		case 1:
			//choose input channel;
			break;
		default:
			printk("%s: Don't register scaler chip!\n", __func__);
			break;
	}

	default_parse_cmd(cmd, NULL);

	return 0;
}

struct file_operations scaler_fops = {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36)
		.unlocked_ioctl =scaler_file_ioctl,
#else
		.ioctl =scaler_file_ioctl,          //定义所有对scaler操作的cmd
#endif
		.read =scaler_file_read,
		.write = scaler_file_write,
		.open = scaler_file_open,
		.release = scaler_file_close,
};

//注册一个scaler的字符设备 供别的设备操作scaler
static int scaler_register_chrdev(void)
{
    int ret = 0;

    ret = alloc_chrdev_region(&sdev->devno, 0, 1, SCALER_DEV_NAME);
    if(ret){
        printk("%s, can't allocate chrdev devno.\n", __func__);
        return ret;
    }else {
        printk("%s: Scaler chrdev devno(%d,%d).\n", __func__, MAJOR(sdev->devno), MINOR(sdev->devno));
    }   
				    
    // initialize character device driver
	sdev->cdev = cdev_alloc();
	if (!sdev->cdev) {
		printk("%s: cdev alloc failed.\n", __func__);
		return -1;
	}
    cdev_init(sdev->cdev, &scaler_fops);
    sdev->cdev->owner = THIS_MODULE;
    ret = cdev_add(sdev->cdev, sdev->devno, 1); 
    if(ret < 0){ 
         printk("%s, add character device error, ret %d\n", __func__, ret);
         return ret;
     }   

     sdev->class = class_create(THIS_MODULE, SCALER_DEV_NAME);
     if(IS_ERR(sdev->class)){
         printk("%s, create class, error\n", __func__);
         return ret;
     }   
     device_create(sdev->class, NULL, sdev->devno, NULL, SCALER_DEV_NAME);

     return 0;
}
#if 0
static int scaler_register_i2c(struct scaler_platform_data *pdata)
{
	int res = 0;
	struct i2c_board_info scaler_i2c_info;
	printk("%s\n", __func__);

	if (pdata->i2c_channel < 0 || pdata->i2c_channel > 4) {
		printk("%s: i2c channel %d bound [0,3]", __func__, pdata->i2c_channel);
		return 1;
	}

	strcpy(scaler_i2c_info.type, pdata->name);
	scaler_i2c_info.addr = pdata->i2c_addr;

	res = i2c_register_board_info(pdata->i2c_channel, &scaler_i2c_info, 1);
	if (res < 0) {
		printk("%s: i2c register board info failed.\n", __func__);
		return -1;
	}


	return res;
}

static int  rk_scaler_probe(struct platform_device *pdev)
{
	int res = 0;
	struct scaler_platform_data *pdata;

	printk("%s\n", __func__);
	pdata = pdev->dev.platform_data;
	if (!pdata) {
		printk("%s: Don't Defined Platform Data.\n", __func__);
		return -1;
	}

	sdev = kzalloc(sizeof(struct scaler_device), GFP_KERNEL);
	if (!sdev) {
		printk("%s: malloc scaler devices failed.\n", __func__);
		return -1;
	}else {
		//sdev->pdata = pdata;
		INIT_LIST_HEAD(&sdev->chips);
	}

	scaler_register_chrdev();

	//if (pdata->init_hw)
	//	pdata->init_hw();
	//else
	//	scaler_init_hw();

	//res = scaler_register_i2c(pdata);
	if (res < 0) 
		return -1;

	return res;
}

static int rk_scaler_remove(struct platform_device *pdev)
{
	printk("%s\n", __func__);
	kfree(sdev);
	return 0;
}

static struct platform_driver rk_scaler_driver = {
	.probe		= rk_scaler_probe,
	.remove		= rk_scaler_remove,
	.driver = {
		.name = "rk-scaler",
		.owner	= THIS_MODULE,
	}
};
#endif

static int __init rk_scaler_init(void)
{
	//printk("%s\n", __func__);
	//return platform_driver_register(&rk_scaler_driver);
	int res = 0;

	printk("%s: SCALER CORE VERSION: %s\n", __func__, SCALER_CORE_VERSION);

	sdev = kzalloc(sizeof(struct scaler_device), GFP_KERNEL);
	if (!sdev) {
		printk("%s: malloc scaler devices failed.\n", __func__);
		return -1;
	}else {
		INIT_LIST_HEAD(&sdev->chips);
	}

	res = scaler_register_chrdev();

	return res;
}

static void  __exit rk_scaler_exit(void)
{
	//platform_driver_unregister(&rk_scaler_driver);
	kfree(sdev);
}

subsys_initcall(rk_scaler_init);
module_exit(rk_scaler_exit);

MODULE_AUTHOR("linjh <linjh@rock-chips.com>");
MODULE_DESCRIPTION("RK Scaler Device Driver");
MODULE_LICENSE("GPL");
