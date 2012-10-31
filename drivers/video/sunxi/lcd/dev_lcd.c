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

#include "dev_lcd.h"
#include "lcd_panel_cfg.h"

#include "../disp/disp_lcd.h"

static struct cdev *my_cdev;
static dev_t devid;
static struct class *lcd_class;

static int
lcd_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int
lcd_release(struct inode *inode, struct file *file)
{
	return 0;
}

static ssize_t
lcd_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	return -EINVAL;
}

static ssize_t
lcd_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	return -EINVAL;
}

static int
lcd_mmap(struct file *file, struct vm_area_struct *vma)
{
	return 0;
}

static long
lcd_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return 0;
}

static const struct file_operations lcd_fops = {
	.owner = THIS_MODULE,
	.open = lcd_open,
	.release = lcd_release,
	.write = lcd_write,
	.read = lcd_read,
	.unlocked_ioctl = lcd_ioctl,
	.mmap = lcd_mmap,
};

static int
lcd_init(void)
{
	static __lcd_panel_fun_t lcd0_cfg;
	static __lcd_panel_fun_t lcd1_cfg;

	LCD_get_panel_funs_generic(&lcd0_cfg);
	LCD_get_panel_funs_generic(&lcd1_cfg);

	LCD_get_panel_funs_0(&lcd0_cfg);
	LCD_get_panel_funs_1(&lcd1_cfg);
	LCD_set_panel_funs(&lcd0_cfg, &lcd1_cfg);

	DRV_DISP_Init();

	Fb_Init(0);

	return 0;
}

static int
__init lcd_module_init(void)
{
	int ret = 0, err;

	__inf("lcd_module_init\n");

	alloc_chrdev_region(&devid, 0, 1, "lcd");
	my_cdev = cdev_alloc();
	cdev_init(my_cdev, &lcd_fops);
	my_cdev->owner = THIS_MODULE;
	err = cdev_add(my_cdev, devid, 1);
	if (err) {
		__wrn("cdev_add fail.\n");
		return -1;
	}

	lcd_class = class_create(THIS_MODULE, "lcd");
	if (IS_ERR(lcd_class)) {
		__wrn("class_create fail\n");
		return -1;
	}

	device_create(lcd_class, NULL, devid, NULL, "lcd");

	lcd_init();

	return ret;
}

static void __exit lcd_module_exit(void)
{
	__inf("lcd_module_exit\n");

	device_destroy(lcd_class, devid);

	class_destroy(lcd_class);

	cdev_del(my_cdev);
}

late_initcall(lcd_module_init);
module_exit(lcd_module_exit);

MODULE_AUTHOR("danling_xiao");
MODULE_DESCRIPTION("lcd driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:lcd");
