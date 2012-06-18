/*
 * drivers/media/pa/sun4i_pa.c
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/preempt.h>
#include <linux/cdev.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <mach/hardware.h>
#include <asm/system.h>
#include <linux/rmap.h>
#include <linux/string.h>
#ifdef CONFIG_PM
#include <linux/pm.h>
#endif
#include <plat/sys_config.h>
#include <mach/system.h>

static int gpio_pa_shutdown = 0;
static struct class *pa_dev_class;
static struct cdev *pa_dev;
static dev_t dev_num ;

//#define PA_DEBUG
typedef enum PA_OPT
{
	PA_OPEN = 200,
	PA_CLOSE,
	PA_DEV_
}__ace_ops_e;


static int pa_dev_open(struct inode *inode, struct file *filp){
	#ifdef PA_DEBUG
	 printk("%s,%d\n", __func__, __LINE__);
	 #endif
    return 0;
}

static int pa_dev_release(struct inode *inode, struct file *filp){
	#ifdef PA_DEBUG
	 printk("%s,%d\n", __func__, __LINE__);
	 #endif
    return 0;
}

static long
pa_dev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg){

	switch (cmd) {
		case PA_OPEN:
			#ifdef PA_DEBUG
			printk("%s,%d\n", __func__, __LINE__);
			#endif
			gpio_write_one_pin_value(gpio_pa_shutdown, 1, "audio_pa_ctrl");
			break;
		case PA_CLOSE:
			#ifdef PA_DEBUG
			printk("%s,%d\n", __func__, __LINE__);
			#endif
			gpio_write_one_pin_value(gpio_pa_shutdown, 0, "audio_pa_ctrl");
			break;
		default:
			break;
	}
	return 0;
}

static int snd_sun4i_pa_suspend(struct platform_device *pdev,pm_message_t state)
{
	return 0;
}

static int snd_sun4i_pa_resume(struct platform_device *pdev)
{
	return 0;
}

static struct file_operations pa_dev_fops = {
    .owner 			= THIS_MODULE,
    .unlocked_ioctl = pa_dev_ioctl,
    .open           = pa_dev_open,
    .release        = pa_dev_release,
};

/*data relating*/
static struct platform_device sun4i_device_pa = {
	.name = "sun4i-pa",
};

/*method relating*/
static struct platform_driver sun4i_pa_driver = {
#ifdef CONFIG_PM
	.suspend	= snd_sun4i_pa_suspend,
	.resume		= snd_sun4i_pa_resume,
#endif
	.driver		= {
		.name	= "sun4i-pa",
	},
};

static int __init pa_dev_init(void)
{
    int err = 0;
	printk("[pa_drv] start!!!\n");

	if((platform_device_register(&sun4i_device_pa))<0)
		return err;

	if ((err = platform_driver_register(&sun4i_pa_driver)) < 0)
		return err;

    alloc_chrdev_region(&dev_num, 0, 1, "pa_chrdev");
    pa_dev = cdev_alloc();
    cdev_init(pa_dev, &pa_dev_fops);
    pa_dev->owner = THIS_MODULE;
    err = cdev_add(pa_dev, dev_num, 1);
    if (err) {
    	printk(KERN_NOTICE"Error %d adding pa_dev!\n", err);
        return -1;
    }
    pa_dev_class = class_create(THIS_MODULE, "pa_cls");
    device_create(pa_dev_class, NULL,
                  dev_num, NULL, "pa_dev");
    printk("[pa_drv] init end!!!\n");
    return 0;
}
module_init(pa_dev_init);

static void __exit pa_dev_exit(void)
{
    device_destroy(pa_dev_class,  dev_num);
    class_destroy(pa_dev_class);
    platform_driver_unregister(&sun4i_pa_driver);
}
module_exit(pa_dev_exit);

MODULE_AUTHOR("young");
MODULE_DESCRIPTION("User mode encrypt device interface");
MODULE_LICENSE("GPL");

