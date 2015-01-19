/*
 * linux/drivers/input/irremote/virtual_remote.c
 *
 * Virtual Keypad Driver
 *
 * Copyright (C) 2010 Amlogic Corporation
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 * author :   jianfeng_wang
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/errno.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <mach/am_regs.h>
#include <linux/slab.h>
#include <linux/major.h>
#include <asm/uaccess.h>

struct input_struct {
	struct input_dev *input;
	struct timer_list timer;
	unsigned int scancode;
	int config_major;
	char config_name[20];
	struct class *config_class;
	struct device *config_dev;

};

static struct input_struct *gp_input_device = NULL;

static void timer_sr(unsigned long data)
{
	struct input_struct *kp = (struct input_struct *)data;
	input_report_key(kp->input, (kp->scancode) & 0xff, 0);
}

static int virtual_remote_open(struct inode *inode, struct file *file)
{
	file->private_data = gp_input_device;
	return 0;
}

static int
virtual_remote_ioctl(struct inode *inode, struct file *filp,
                     unsigned int cmd, unsigned long args)
{
	unsigned int scan_code;
	struct input_struct *kp = (struct input_struct *)filp->private_data;
	switch (cmd) {
	case 2:
		kp->timer.data = (unsigned long)kp;
		mod_timer(&kp->timer, jiffies + msecs_to_jiffies(120));
	case 1:
		copy_from_user(&scan_code, (unsigned int *)args,
		               sizeof(unsigned int));
		kp->scancode = scan_code;
		input_report_key(kp->input, scan_code & 0xff, 1);
		break;
	case 0:
		copy_from_user(&scan_code, (unsigned int *)args,
		               sizeof(unsigned int));
		input_report_key(kp->input, scan_code & 0xff, 0);
		break;
	default:
		printk("%s : Wrong command code\n", __func__);
		return -EACCES;
	}
	return 0;
}

static int virtual_remote_release(struct inode *inode, struct file *file)
{
	file->private_data = NULL;
	return 0;

}

static const struct file_operations virtual_remote_fops = {
	.owner = THIS_MODULE,
	.open = virtual_remote_open,
	.ioctl = virtual_remote_ioctl,
	.release = virtual_remote_release,
};

static void register_remote_dev(struct input_struct *kp)
{
	int ret = 0;
	strcpy(kp->config_name, "virtualremote");
	ret = register_chrdev(0, kp->config_name, &virtual_remote_fops);
	if (ret <= 0) {
		printk("register char dev (VirtualRemote) error\r\n");
		return;
	}
	kp->config_major = ret;
	kp->config_class = class_create(THIS_MODULE, kp->config_name);
	kp->config_dev =
	    device_create(kp->config_class, NULL, MKDEV(kp->config_major, 0),
	                  NULL, kp->config_name);
	return;
}

static int __init virtual_remote_probe(struct platform_device *pdev)
{
	struct input_struct *apollo_kp;
	struct input_dev *input_dev;
	int i, ret;

	apollo_kp = kzalloc(sizeof(struct input_struct), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!apollo_kp || !input_dev) {
		kfree(apollo_kp);
		input_free_device(input_dev);
		printk("creat device failed.\n");
		return -ENOMEM;
	}
	gp_input_device = apollo_kp;
	platform_set_drvdata(pdev, apollo_kp);
	apollo_kp->input = input_dev;
	setup_timer(&apollo_kp->timer, timer_sr, 0);

	/* setup input device */
	set_bit(EV_KEY, input_dev->evbit);
	set_bit(EV_REP, input_dev->evbit);

	for (i = 0; i < KEY_MAX; i++) {
		set_bit(i, input_dev->keybit);
	}
	input_dev->name = "Virtual-Remote";
	input_dev->phys = "Virtual-Remote/input0";
	input_dev->dev.parent = &pdev->dev;

	input_dev->id.bustype = BUS_ISA;
	input_dev->id.vendor = 0x0001;
	input_dev->id.product = 0x0001;
	input_dev->id.version = 0x0100;

	input_dev->rep[REP_DELAY] = 0xffffffff;
	input_dev->rep[REP_PERIOD] = 0xffffffff;

	input_dev->keycodesize = sizeof(unsigned short);
	input_dev->keycodemax = 0x1ff;

	ret = input_register_device(input_dev);
	if (ret < 0) {
		printk(KERN_ERR
		       "Unable to register Virtual Remote input device\n");
		kfree(apollo_kp);
		input_free_device(input_dev);
		gp_input_device = NULL;
		return -EINVAL;
	}
	printk("input_register_device completed \r\n");

	register_remote_dev(apollo_kp);
	return 0;
}

static int virtual_remote_remove(struct platform_device *pdev)
{
	struct input_struct *apollo_kp = platform_get_drvdata(pdev);
	/* disable keypad interrupt handling */
	printk("remove Virtual Remote. \r\n");

	/* unregister everything */
	input_unregister_device(apollo_kp->input);
	input_free_device(apollo_kp->input);
	unregister_chrdev(apollo_kp->config_major, apollo_kp->config_name);
	if (apollo_kp->config_class) {
		if (apollo_kp->config_dev)
			device_destroy(apollo_kp->config_class,
			               MKDEV(apollo_kp->config_major, 0));
		class_destroy(apollo_kp->config_class);
	}

	kfree(apollo_kp);
	gp_input_device = NULL;
	return 0;
}

static struct platform_driver virtual_remote_driver = {
	.probe = virtual_remote_probe,
	.remove = virtual_remote_remove,
	.suspend = NULL,
	.resume = NULL,
	.driver = {
		.name = "apollo-kp",
	},
};

static int __devinit virtual_remote_init(void)
{
	printk(KERN_INFO "Virtual Remote Driver for QA testing\n");

	return platform_driver_register(&virtual_remote_driver);
}

static void __exit virtual_remote_exit(void)
{
	printk(KERN_INFO "Virtual Remote exit \n");
	platform_driver_unregister(&virtual_remote_driver);
}

module_init(virtual_remote_init);
module_exit(virtual_remote_exit);

MODULE_AUTHOR("geng.li");
MODULE_DESCRIPTION("Virtual Remote Driver");
MODULE_LICENSE("GPL");
