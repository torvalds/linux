/* SPDX-License-Identifier: GPL-2.0 */
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
#include <linux/scaler-core.h>

#define SCALER_CORE_VERSION "v1.0.0"
#define SCALER_CORE_NAME "scaler-core"
#define SCALER_DEV_NAME "scaler-ctrl"

static DEFINE_MUTEX(mutex_chips);
static DEFINE_MUTEX(mutex_ports);

static struct scaler_device *sdev = NULL;
static unsigned short chip_ids = 0;       //<id only grow>只增不减
static unsigned int port_ids = 0;
extern int scaler_sysfs_create(struct scaler_device *sdev);
extern int scaler_sysfs_remove(struct scaler_device *sdev);

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
	"HDMI",
	"DP",
	"DVI",
	"YPBPR",
	"YCBCR",
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

static void default_start(void)
{
}

static void default_stop(void)
{
}

static int default_read(unsigned short reg, int bytes, void *desc)
{
	return 0;
}

static int default_write(unsigned short reg, int bytes, void *desc)
{
	return 0;
}

static int default_parse_cmd(unsigned int cmd, unsigned long arg)
{
	return 0;
}

//alloc chip dev memory and init default value
struct scaler_chip_dev *alloc_scaler_chip(void)
{
	struct scaler_chip_dev *p = kzalloc(sizeof(struct scaler_chip_dev), GFP_KERNEL);
	if (p) {
		strcpy(p->name, "unknown");
		//init default operation function
		p->start = default_start;
		p->stop = default_stop;
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

int scaler_init_platform(struct scaler_platform_data *pdata)
{
	printk("%s: init scaler platform\n", SCALER_CORE_NAME);

	if (pdata->init_hw) {
		pdata->init_hw();
		msleep(5);
	}

	//power
	if (pdata->power_gpio > 0) {
		if (!gpio_request(pdata->power_gpio, NULL)) {
			if (pdata->power_level != GPIO_HIGH && 
					pdata->power_level != GPIO_LOW) {
				printk("%s: power pin level use default high\n", SCALER_CORE_NAME);
				pdata->power_level = GPIO_HIGH;
			}
			gpio_direction_output(pdata->power_gpio, pdata->power_level); 
		}else
			printk("%s: request vga power gpio failed\n", SCALER_CORE_NAME);
	}else
		printk("%s: Don't defined power gpio pin\n", SCALER_CORE_NAME);

	//vga 5v en
	if (pdata->vga5v_gpio > 0) {
		if (!gpio_request(pdata->vga5v_gpio, NULL)) {
			if (pdata->vga5v_level != GPIO_HIGH && 
					pdata->vga5v_level != GPIO_LOW) {
				printk("%s: vga5ven pin level use default high\n", SCALER_CORE_NAME);
				pdata->vga5v_level = GPIO_HIGH;
			}
			gpio_direction_output(pdata->vga5v_gpio, pdata->vga5v_level); 
		}else
			printk("%s: request vga5ven  gpio failed\n", SCALER_CORE_NAME);
		msleep(10);
	}else
		printk("%s: Don't defined vga5ven gpio pin\n", SCALER_CORE_NAME);
	
	//ddc select
	if (pdata->ddc_sel_gpio > 0) {
		if (!gpio_request(pdata->ddc_sel_gpio, NULL)) {
			if (pdata->ddc_sel_level != GPIO_HIGH && 
					pdata->ddc_sel_level != GPIO_LOW) {
				printk("%s: ddc select pin level use default high\n", SCALER_CORE_NAME);
				pdata->ddc_sel_level = GPIO_HIGH;
			}
			gpio_direction_output(pdata->ddc_sel_gpio, pdata->ddc_sel_level); 
		}else
			printk("%s: request ddc select gpio failed\n", SCALER_CORE_NAME);
		msleep(10);
	}else
		printk("%s: Don't defined ddc select gpio pin\n", SCALER_CORE_NAME);
}

int init_scaler_chip(struct scaler_chip_dev *chip, struct scaler_platform_data *pdata)
{
	int i;
	struct scaler_output_port *oport = NULL;
	struct scaler_input_port *iport = NULL;

	if (!chip || !pdata) {
		printk("%s: chip or pdata is null.\n", SCALER_CORE_NAME);
		return -1;
	}
	
	//set scaler function type
	if (pdata->func_type > SCALER_FUNC_INVALID && 
			pdata->func_type < SCALER_FUNC_NUMS) {
		chip->func_type = pdata->func_type;
	}else {
		printk("%s: not defined scaer function type\n", SCALER_CORE_NAME);
		chip->func_type = SCALER_FUNC_FULL;
	}
	printk("%s: %s %s\n", chip->name, scaler_func_name[0], scaler_func_name[chip->func_type]);

	//set scaler support input type
	for (i = 0; i < pdata->iport_size; i++) {
		iport = kzalloc(sizeof(struct scaler_input_port), GFP_KERNEL);
		if (!iport) {
		    printk("%s: kzalloc input port memeory failed.\n", SCALER_CORE_NAME);
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
			iport->type = pdata->iports[i].type;
			//the first input port is default 
			if (!chip->cur_inport_id) {
				chip->cur_in_type = iport->type;
				chip->cur_inport_id = iport->id;
				printk("%s:  default %s %s\n", chip->name, scaler_input_name[0], scaler_input_name[iport->type]);
			}

			//init and request input gpio of indicator lamp 
			if (pdata->iports[i].led_gpio > 0) {
				iport->led_gpio = pdata->iports[i].led_gpio;
				if (!gpio_request(iport->led_gpio, NULL)) {
					gpio_direction_output(iport->led_gpio, GPIO_HIGH);	
				}else {
					printk("%s:  request %s<id,%d> gpio failed\n", chip->name, 
							   scaler_input_name[pdata->iports[i].type], iport->id);
				}
			}

			//add input of chip
			list_add_tail(&iport->next, &chip->iports);
			printk("%s:  support %s %s<id,%d> led_gpio %d\n", chip->name, scaler_input_name[0], 
					     scaler_input_name[pdata->iports[i].type], iport->id, iport->led_gpio);
		}//if (!iport)
	}//for()

	//set scaler output type
	for (i = 0; i < pdata->oport_size; i++) {
		oport = kzalloc(sizeof(struct scaler_output_port), GFP_KERNEL);
		if (!oport) {
		    printk("%s: kzalloc output port memeory failed.\n", SCALER_CORE_NAME);
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
			oport->type = pdata->oports[i].type;
			//the first output port is default 
			if (!chip->cur_outport_id) {
				chip->cur_out_type = oport->type;
				chip->cur_outport_id = oport->id;
				printk("%s:  default %s %s\n", chip->name, scaler_output_name[0], scaler_output_name[oport->type]);
			}

			//init and request output gpio of indicator lamp 
			if (pdata->oports[i].led_gpio > 0) {
				oport->led_gpio = pdata->oports[i].led_gpio;
				if (!gpio_request(oport->led_gpio, NULL)) {
					gpio_direction_output(oport->led_gpio, GPIO_HIGH);	
				}else {
					printk("%s:  request %s<id,%d> gpio failed\n", chip->name, scaler_output_name[pdata->oports[i].type], oport->id);
				}
			}

			list_add_tail(&oport->next, &chip->oports);
			printk("%s:  support %s %s<id,%d> led_gpio %d\n", chip->name, scaler_output_name[0], 
					  scaler_output_name[pdata->oports[i].type], oport->id, oport->led_gpio);
		}// if (!oport)
	}//for()


	return 0;
}

//free scaler chip memory
void free_scaler_chip(struct scaler_chip_dev *chip)
{
	struct scaler_output_port *out = NULL;
	struct scaler_input_port *in = NULL;
	if (chip) {
		printk("%s: free %s chip<id:%d> memory resource\n", 
				        SCALER_CORE_NAME, chip->name, chip->id);
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
		chip->start();

		//power on input indicator lamp
		list_for_each_entry(in, &chip->iports, next){
			if (in->id == chip->cur_inport_id)
				scaler_led_on(in->led_gpio);
			else
				scaler_led_off(in->led_gpio);
		}

		//power on output indicator lamp
		list_for_each_entry(out, &chip->oports, next){
			if (out->id == chip->cur_outport_id)
				scaler_led_on(out->led_gpio);
			else
				scaler_led_off(out->led_gpio);
		}
		//add chip to scaler core
		mutex_lock(&mutex_chips);
		chip->id = ++chip_ids;  //chip id only grow
		list_add_tail(&chip->next, &sdev->chips);
		mutex_unlock(&mutex_chips);
		printk("%s: register scaler chip %s<id:%d> success.\n", 
				     SCALER_CORE_NAME, chip->name, chip->id);
	}else {
		printk("%s: register scaler chip %s<id:%d> failed.\n", 
				     SCALER_CORE_NAME, chip->name, chip->id);
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

		chip->stop();

		//power off input indicator lamp
		list_for_each_entry(in, &chip->iports, next){
			if (in->id == chip->cur_inport_id)
				scaler_led_off(in->led_gpio);
		}

		//power off output indicator lamp
		list_for_each_entry(out, &chip->oports, next){
			if (out->id == chip->cur_outport_id)
				scaler_led_off(out->led_gpio);
		}

		//del chip from scaler core
		mutex_lock(&mutex_chips);
		list_del(&chip->next);
		mutex_unlock(&mutex_chips);
		printk("%s: unregister scaler chip %s<id:%d> success.\n", __func__, chip->name, chip->id);
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
	int iport_id;
	struct scaler_chip_dev *chip = NULL;
	struct scaler_input_port *iport = NULL;


	switch(cmd) {
		case 0:
			printk("get cur input cmd %#x\n", SCALER_IOCTL_GET_CUR_INPUT);
			printk("set cur input cmd %#x\n", SCALER_IOCTL_SET_CUR_INPUT);
			break;
		case SCALER_IOCTL_SET_CUR_INPUT:
			//choose input channel;
			copy_from_user(&iport_id, (void *)arg, sizeof(int));

			list_for_each_entry(chip, &sdev->chips, next) {
				if (chip->cur_inport_id != iport_id) {
					list_for_each_entry(iport, &chip->iports, next) {//if iport belong to this chip
						if (iport->id == iport_id) {
							chip->cur_inport_id = iport_id;
							chip->cur_in_type = iport->type;
							chip->parse_cmd(cmd, arg);
							break;
						}
					}//list iports
				}
			}//list chips

			break;
		case SCALER_IOCTL_GET_CUR_INPUT:
			list_for_each_entry(chip, &sdev->chips, next) {
				iport_id = chip->cur_inport_id;
			}//list chips
			copy_to_user((void *)arg, &iport_id, sizeof(int));
			printk("current input port id %d\n", iport_id);
			break;
		default:
			//default_parse_cmd(cmd, arg);
			break;
	}


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
    if(ret != 0){
        printk("%s, can't allocate chrdev devno.\n", __func__);
        return ret;
    }else {
        printk("%s: Scaler chrdev devno(%d,%d).\n", __func__, MAJOR(sdev->devno), MINOR(sdev->devno));
    }   
				    
    // initialize character device driver
	sdev->cdev = cdev_alloc();
	if (!sdev->cdev) {
		printk("%s: cdev alloc failed.\n", __func__);
		goto err1;
	}

    cdev_init(sdev->cdev, &scaler_fops);
    sdev->cdev->owner = THIS_MODULE;
    ret = cdev_add(sdev->cdev, sdev->devno, 1); 
    if(ret < 0){ 
        printk("%s, add character device error, ret %d\n", __func__, ret);
        goto err2;
    }   

    sdev->class = class_create(THIS_MODULE, "scaler");
    if(!sdev->class){
        printk("%s, create class failed\n", __func__);
        goto err3;
    }   

    sdev->dev = device_create(sdev->class, NULL, sdev->devno, sdev, SCALER_DEV_NAME);
	if (!sdev->dev) {
		printk("%s: create device failed\n", __func__);
		goto err4;
	}
	

    return 0;

err4:
	class_destroy(sdev->class);
err3:
	cdev_del(sdev->cdev);
err2:
	kfree(sdev->cdev);
err1:
	unregister_chrdev_region(sdev->devno, 1);

	return -1;
}

static void scaler_unregister_chrdev(void)
{
	cdev_del(sdev->cdev);
	unregister_chrdev_region(sdev->devno, 1);
	kfree(sdev->cdev);
	device_destroy(sdev->class, sdev->devno);
	class_destroy(sdev->class);
}

static int __init rk_scaler_init(void)
{
	printk("%s: SCALER CORE VERSION: %s\n", __func__, SCALER_CORE_VERSION);

	sdev = kzalloc(sizeof(struct scaler_device), GFP_KERNEL);
	if (!sdev) {
		printk("%s: malloc scaler devices failed.\n", __func__);
		return -1;
	}else {
		INIT_LIST_HEAD(&sdev->chips);
	}

	if (scaler_register_chrdev() < 0) {
		printk("%s:  scaler register chrdev failed.\n", __func__);
		goto err1;
	}

	if (scaler_sysfs_create(sdev) < 0) {
		printk("%s: scaler sysfs create faild.\n", __func__);
		goto err2;
	}

	return 0;
err2:
	scaler_unregister_chrdev();
err1:
	kfree(sdev);

	return -1;
}

static void  __exit rk_scaler_exit(void)
{
	printk("%s\n", __func__);
	scaler_sysfs_remove(sdev);
	scaler_unregister_chrdev();
	kfree(sdev);
}

subsys_initcall(rk_scaler_init);
module_exit(rk_scaler_exit);

MODULE_AUTHOR("linjh <linjh@rock-chips.com>");
MODULE_DESCRIPTION("RK Scaler Device Driver");
MODULE_LICENSE("GPL");
