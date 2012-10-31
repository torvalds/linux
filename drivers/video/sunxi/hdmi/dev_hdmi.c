/*
 * Copyright (C) 2007-2012 Allwinner Technology Co., Ltd.
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

#include "dev_hdmi.h"
#include "drv_hdmi_i.h"

static struct cdev *my_cdev;
static dev_t devid;
static struct class *hdmi_class;

hdmi_info_t ghdmi;

static struct resource hdmi_resource[1] = {
	[0] = {
	       .start = 0x01c16000,
	       .end = 0x01c165ff,
	       .flags = IORESOURCE_MEM,
	       },
};

static void
hdmi_device_release(struct device *dev)
{
	/* FILL ME! */
}

static struct platform_device hdmi_device = {
	.name = "hdmi",
	.id = -1,
	.num_resources = ARRAY_SIZE(hdmi_resource),
	.resource = hdmi_resource,
	.dev = {
		.release = hdmi_device_release,
	}
};

static int __devinit hdmi_probe(struct platform_device *pdev)
{
	__inf("hdmi_probe call\n");

	memset(&ghdmi, 0, sizeof(hdmi_info_t));

	ghdmi.base_hdmi = 0xf1c16000;

	Hdmi_init();
	Fb_Init(1);

	return 0;
}

static int hdmi_remove(struct platform_device *pdev)
{
	__inf("hdmi_remove call\n");

	Hdmi_exit();

	return 0;
}

static int
hdmi_suspend(struct platform_device *pdev, pm_message_t state)
{
	return 0;
}

static int
hdmi_resume(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver hdmi_driver = {
	.probe = hdmi_probe,
	.remove = hdmi_remove,
	.suspend = hdmi_suspend,
	.resume = hdmi_resume,
	.driver = {
		   .name = "hdmi",
		   .owner = THIS_MODULE,
		   },
};

int hdmi_open(struct inode *inode, struct file *file)
{
	return 0;
}

int hdmi_release(struct inode *inode, struct file *file)
{
	return 0;
}

ssize_t hdmi_read(struct file *file, char __user *buf, size_t count,
		  loff_t *ppos)
{
	return -EINVAL;
}

ssize_t hdmi_write(struct file *file, const char __user *buf, size_t count,
		   loff_t *ppos)
{
	return -EINVAL;
}

int hdmi_mmap(struct file *file, struct vm_area_struct *vma)
{
	return 0;
}

long hdmi_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return 0;
}

static const struct file_operations hdmi_fops = {
	.owner = THIS_MODULE,
	.open = hdmi_open,
	.release = hdmi_release,
	.write = hdmi_write,
	.read = hdmi_read,
	.unlocked_ioctl = hdmi_ioctl,
	.mmap = hdmi_mmap,
};

static int __init
hdmi_module_init(void)
{
	int ret = 0, err;

	__inf("hdmi_module_init\n");

	alloc_chrdev_region(&devid, 0, 1, "hdmi");
	my_cdev = cdev_alloc();
	cdev_init(my_cdev, &hdmi_fops);
	my_cdev->owner = THIS_MODULE;
	err = cdev_add(my_cdev, devid, 1);
	if (err) {
		__wrn("cdev_add fail.\n");
		return -1;
	}

	hdmi_class = class_create(THIS_MODULE, "hdmi");
	if (IS_ERR(hdmi_class)) {
		__wrn("class_create fail\n");
		return -1;
	}

	ret = platform_device_register(&hdmi_device);

	if (ret == 0)
		ret = platform_driver_register(&hdmi_driver);

	return ret;
}

static void __exit hdmi_module_exit(void)
{
	__inf("hdmi_module_exit\n");

	platform_driver_unregister(&hdmi_driver);
	platform_device_unregister(&hdmi_device);

	class_destroy(hdmi_class);

	cdev_del(my_cdev);
}

late_initcall(hdmi_module_init);
module_exit(hdmi_module_exit);

MODULE_AUTHOR("danling_xiao");
MODULE_DESCRIPTION("hdmi driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:hdmi");
