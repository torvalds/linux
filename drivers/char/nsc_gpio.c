/* linux/drivers/char/nsc_gpio.c

   National Semiconductor common GPIO device-file/VFS methods.
   Allows a user space process to control the GPIO pins.

   Copyright (c) 2001,2002 Christer Weinigel <wingel@nano-system.com>
   Copyright (c) 2005      Jim Cromie <jim.cromie@gmail.com>
*/

#include <linux/config.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/nsc_gpio.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#define NAME "nsc_gpio"

MODULE_AUTHOR("Jim Cromie <jim.cromie@gmail.com>");
MODULE_DESCRIPTION("NatSemi GPIO Common Methods");
MODULE_LICENSE("GPL");

static int __init nsc_gpio_init(void)
{
	printk(KERN_DEBUG NAME " initializing\n");
	return 0;
}

static void __exit nsc_gpio_cleanup(void)
{
	printk(KERN_DEBUG NAME " cleanup\n");
}

/* prepare for
   common routines for both scx200_gpio and pc87360_gpio
EXPORT_SYMBOL(scx200_gpio_write);
EXPORT_SYMBOL(scx200_gpio_read);
EXPORT_SYMBOL(scx200_gpio_release);
*/

module_init(nsc_gpio_init);
module_exit(nsc_gpio_cleanup);
