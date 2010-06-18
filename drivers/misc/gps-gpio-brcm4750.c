/*
 * Copyright (C) 2010 Motorola, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 */

#include <linux/err.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/gps-gpio-brcm4750.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/uaccess.h>

struct gps_gpio_brcm4750_platform_data *gps_gpio_data;

static int gps_brcm4750_ioctl(struct inode *inode,
		struct file *filp, unsigned int cmd, unsigned long arg)
{
	unsigned int gpio_val;

	if (cmd <= 0)
		return -EINVAL;

	if (copy_from_user((void *) &gpio_val, (void *) arg,
				sizeof(int)))
		return -EFAULT;

	if (!(gpio_val == 0 || gpio_val == 1))
		return -EINVAL;

	switch (cmd) {
	case IOC_GPS_GPIO_RESET:
		pr_info("%s: Setting gps gpio reset pin: %d\n",
		 __func__, gpio_val);
		if (gps_gpio_data->set_reset_gpio)
			gps_gpio_data->set_reset_gpio(gpio_val);
		break;
	case IOC_GPS_GPIO_STANDBY:
		pr_info("%s: Setting gps gpio standby pin to: %d\n",
			__func__, gpio_val);
		if (gps_gpio_data->set_standby_gpio)
			gps_gpio_data->set_standby_gpio(gpio_val);
		break;
	default:
		pr_info("%s: Invalid GPS GPIO IOCTL command\n", __func__);
		return -EINVAL;
	}

	return 0;
}

static const struct file_operations gps_brcm4750_fops = {
	.owner		= THIS_MODULE,
	.ioctl		= gps_brcm4750_ioctl,
};

static struct miscdevice gps_gpio_miscdev = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= GPS_GPIO_DRIVER_NAME,
	.fops	= &gps_brcm4750_fops,
};

static int gps_gpio_brcm4750_probe(struct platform_device *pdev)
{
	gps_gpio_data = pdev->dev.platform_data;
	if (misc_register(&gps_gpio_miscdev)) {
		pr_info("%s: gps_brcm4750 misc_register failed\n", __func__);
		return -1;
	}
	return 0;
}

static int gps_gpio_brcm4750_remove(struct platform_device *pdev)
{
	if (gps_gpio_data->free_gpio)
		gps_gpio_data->free_gpio();
	return 0;
}

static struct platform_driver gps_gpio_brcm4750_driver = {
	.probe		= gps_gpio_brcm4750_probe,
	.remove		= gps_gpio_brcm4750_remove,
	.driver		= {
		.name		= GPS_GPIO_DRIVER_NAME,
		.owner		= THIS_MODULE,
	},
};

static int __init gps_gpio_brcm4750_init(void)
{
	return platform_driver_register(&gps_gpio_brcm4750_driver);
}

static void __exit gps_gpio_brcm4750_exit(void)
{
	platform_driver_unregister(&gps_gpio_brcm4750_driver);
}

module_init(gps_gpio_brcm4750_init);
module_exit(gps_gpio_brcm4750_exit);

MODULE_AUTHOR("Motorola");
MODULE_DESCRIPTION("GPS GPIO Controller for BRCM 4750");
MODULE_LICENSE("GPL");
