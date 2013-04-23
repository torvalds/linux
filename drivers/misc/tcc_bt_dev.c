/*
 *  Copyright (C) 2010-2011  RDA Micro <anli@rdamicro.com>
 *  This file belong to RDA micro
 * File:        drivers/char/tcc_bt_dev.c
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/ioctl.h>
#include <linux/device.h>

//#include <mach/bsp.h>
#include <asm/io.h>
#include <linux/delay.h>

//#include <linux/tcc_bt_dev.h>
//#include <mach/tcc_pca953x.h>

#include <linux/gpio.h>
#include <mach/gpio.h>
#include <asm/mach-types.h>

#include <mach/iomux.h>
#include <linux/interrupt.h>
#include <asm/irq.h>
#include <linux/wakelock.h>

#ifndef ON
#define ON 		1
#endif

#ifndef OFF
#define OFF 	0
#endif

#define LDO_ON_PIN	       RK2928_PIN3_PC0
#define RDA_BT_HOST_WAKE_PIN   RK2928_PIN0_PC5

#define BT_DEV_ON					1
#define BT_DEV_OFF					0

#define BT_DEV_MAJOR_NUM			234
#define BT_DEV_MINOR_NUM			0

//#define IOCTL_BT_DEV_POWER			_IO(BT_DEV_MAJOR_NUM, 100)
//#define IOCTL_BT_DEV_SPECIFIC		_IO(BT_DEV_MAJOR_NUM, 101)
//#define IOCTL_BT_DEV_IS_POWER			_IO(BT_DEV_MAJOR_NUM, 102)


#define IOCTL_BT_DEV_POWER   		_IO(BT_DEV_MAJOR_NUM, 100)
#define IOCTL_BT_DEV_CTRL	        _IO(BT_DEV_MAJOR_NUM, 101)
#define IOCTL_BT_SET_EINT               _IO(BT_DEV_MAJOR_NUM, 102)
#define IOCTL_BT_DEV_SPECIFIC		_IO(BT_DEV_MAJOR_NUM, 103)


static int bt_is_power = 0;
static int rda_bt_irq = 0;
static int irq_mask = 0;
extern void export_bt_hci_wakeup_chip(void);

#define DEV_NAME "tcc_bt_dev"
static struct class *bt_dev_class;

typedef struct {
	int module;  // 0x12:CSR, 0x34:Broadcom
	int TMP1;
	int TMP2;
} tcc_bt_info_t;

static int tcc_bt_dev_open(struct inode *inode, struct file *file)
{
	printk("[## BT ##] tcc_bt_dev_open\n");
    	return 0;
}

static int tcc_bt_dev_release(struct inode *inode, struct file *file)
{
	printk("[## BT ##] tcc_bt_dev_release\n");
    	return 0;
}

int tcc_bt_power_control(int on_off)
{
//	volatile PGPIO pGPIO = (volatile PGPIO)tcc_p2v(HwGPIO_BASE);
    
	printk("[## BT ##] tcc_bt_power_control input[%d]\n", on_off);
	
	if(on_off == BT_DEV_ON)
	{	    
 		gpio_direction_output(LDO_ON_PIN, GPIO_HIGH);
		bt_is_power = on_off;
		msleep(500);
	}
	else if(on_off == BT_DEV_OFF)
	{
  		gpio_direction_output(LDO_ON_PIN, GPIO_LOW);
		bt_is_power = BT_DEV_OFF;
		msleep(500);
	}
	else
	{
		printk("[## BT_ERR ##] input_error On[%d] Off[%d]\n", BT_DEV_ON, BT_DEV_OFF);
	}

	return 0;
}


static int tcc_bt_get_info(tcc_bt_info_t* arg)
{
	tcc_bt_info_t *info_t;
	int module_t;
	
	info_t = (tcc_bt_info_t *)arg;
	copy_from_user(info_t, (tcc_bt_info_t *)arg, sizeof(tcc_bt_info_t));

	module_t = 0x56;	

	printk("[## BT ##] module[0x%x]\n", module_t);

	info_t->module = module_t;

	copy_to_user((tcc_bt_info_t *)arg, info_t, sizeof(tcc_bt_info_t));

	return 0;
}

static long  tcc_bt_dev_ioctl(struct file *file, unsigned int cmd,unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	char msg[14];
	int ret = -1;
	char rate;
	//printk("[## BT ##] tcc_bt_dev_ioctl cmd[%d] arg[%d]\n", cmd, arg);
	switch(cmd)
	{
		case IOCTL_BT_DEV_POWER:			
			if (copy_from_user(&rate, argp, sizeof(rate)))
			return -EFAULT;
			printk("[## BT ##] IOCTL_BT_DEV_POWER cmd[%d] parm1[%d]\n", cmd, rate);
			tcc_bt_power_control(rate);
			// GPIO Control
			break;
			
		case IOCTL_BT_DEV_SPECIFIC:
			printk("[## BT ##] IOCTL_BT_DEV_SPECIFIC cmd[%d]\n", cmd);
			tcc_bt_get_info((tcc_bt_info_t*)arg);
			break;

		//case IOCTL_BT_DEV_IS_POWER:
			//if (copy_to_user(argp, &bt_is_power, sizeof(bt_is_power)))
			//return -EFAULT;
                case IOCTL_BT_SET_EINT:
                     printk("[## BT ##] IOCTL_BT_SET_EINT cmd[%d]\n", cmd);
                     if (irq_mask)
                     {
                         irq_mask = 0;
                         enable_irq(rda_bt_irq);
                     }
                     break;
		default :
			printk("[## BT ##] tcc_bt_dev_ioctl cmd[%d]\n", cmd);
			break;
	}

	return 0;
}


struct file_operations tcc_bt_dev_ops = {
    .owner      = THIS_MODULE,
    .unlocked_ioctl      = tcc_bt_dev_ioctl,
    .open       = tcc_bt_dev_open,
    .release    = tcc_bt_dev_release,
};

struct wake_lock rda_bt_wakelock;
static irqreturn_t rda_5876_host_wake_irq(int irq, void *dev)
{
	printk("rda_5876_host_wake_irq\n");
	//export_bt_hci_wakeup_chip();
        wake_lock_timeout(&rda_bt_wakelock, 3 * HZ); 
        disable_irq(rda_bt_irq);
        irq_mask = 1;       
	return IRQ_HANDLED;
}

int init_module(void)
{
    int ret;

        wake_lock_init(&rda_bt_wakelock, WAKE_LOCK_SUSPEND, "rda_bt_wake");
	ret=gpio_request(LDO_ON_PIN, "ldoonpin");
	if(ret < 0) {
		printk("%s:fail to request gpio %d\n",__func__,LDO_ON_PIN);
		return ret;
	}
	gpio_set_value(LDO_ON_PIN, GPIO_LOW);//GPIO_LOW
	gpio_direction_output(LDO_ON_PIN, GPIO_LOW);//GPIO_LOW

        if(rda_bt_irq == 0){
		if(gpio_request(RDA_BT_HOST_WAKE_PIN, "bt_wake") != 0){
			printk("RDA_BT_HOST_WAKE_PIN request fail!\n");
			return -EIO;
		}
		gpio_direction_input(RDA_BT_HOST_WAKE_PIN);

		rda_bt_irq = gpio_to_irq(RDA_BT_HOST_WAKE_PIN);
		ret = request_irq(rda_bt_irq, rda_5876_host_wake_irq, IRQF_TRIGGER_RISING, "bt_host_wake",NULL);
		if(ret){
			printk("bt_host_wake irq request fail\n");
			rda_bt_irq = 0;
			gpio_free(RDA_BT_HOST_WAKE_PIN);
			return -EIO;
		}
                enable_irq_wake(rda_bt_irq); //bt irq can wakeup host when host sleep
                disable_irq(rda_bt_irq);
                irq_mask = 1;
            printk("request_irq bt_host_wake\n");
	}

	
	printk("[## BT ##] init_module\n");
    	ret = register_chrdev(BT_DEV_MAJOR_NUM, DEV_NAME, &tcc_bt_dev_ops);

	bt_dev_class = class_create(THIS_MODULE, DEV_NAME);
	device_create(bt_dev_class, NULL, MKDEV(BT_DEV_MAJOR_NUM, BT_DEV_MINOR_NUM), NULL, DEV_NAME);
#if 0
	if(machine_is_tcc8900()){
	    gpio_request(TCC_GPB(25), "bt_power");
	    gpio_request(TCC_GPEXT2(9), "bt_reset");
		gpio_direction_output(TCC_GPB(25), 0); // output
		gpio_direction_output(TCC_GPEXT2(9), 0);
	}else if(machine_is_tcc9300() || machine_is_tcc8800()) {      // #elif defined (CONFIG_MACH_TCC9300)
			//gpio_set_value(TCC_GPEXT1(7), 0);   /* BT-ON Disable */
		gpio_request(TCC_GPEXT3(2), "bt_wake");
	    gpio_request(TCC_GPEXT2(4), "bt_reset");
		gpio_direction_output(TCC_GPEXT3(2), 0); // output
		gpio_direction_output(TCC_GPEXT2(4), 0);
	}
	else if(machine_is_m801_88())
	{
		gpio_request(TCC_GPA(13), "bt_reset");
		gpio_request(TCC_GPB(22), "BT WAKE");
		gpio_direction_output(TCC_GPA(13), 0); // output
		gpio_direction_output(TCC_GPB(22), 0); // output
	}
#endif	
    if(ret < 0){
        printk("[## BT ##] [%d]fail to register the character device\n", ret);
        return ret;
    }

    return 0;
}

void cleanup_module(void)
{
	printk("[## BT ##] cleanup_module\n");
    unregister_chrdev(BT_DEV_MAJOR_NUM, DEV_NAME);
    wake_lock_destroy(&rda_bt_wakelock);
}


late_initcall(init_module);
module_exit(cleanup_module);


MODULE_AUTHOR("Telechips Inc. linux@telechips.com");
MODULE_DESCRIPTION("TCC_BT_DEV");
MODULE_LICENSE("GPL");


