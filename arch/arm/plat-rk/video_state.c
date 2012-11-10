/*
 * Copyright (C) 2012 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/clk.h>
#include <linux/uaccess.h>
#include <mach/ddr.h>

#ifdef CONFIG_ARCH_RK2928
#define VIDEO_DDR_RATE	(300*1000*1000)
#else
#define VIDEO_DDR_RATE	(300*1000*1000)
#endif

static struct clk *ddr_clk;
static unsigned long ddr_rate;
static char video_state = '0';

static int video_state_open(struct inode *inode, struct file *file)
{
	ddr_rate = clk_get_rate(ddr_clk);
	return 0;
}

static int video_state_release(struct inode *inode, struct file *file)
{
	clk_set_rate(ddr_clk, ddr_rate);
	return 0;
}

static ssize_t video_state_read(struct file *file, char __user *buffer,
				size_t count, loff_t *ppos)
{
	if (copy_to_user(buffer, &video_state, 1))
		return -EFAULT;
	return 1;
}

static ssize_t video_state_write(struct file *file, const char __user *buffer,
				 size_t count, loff_t *ppos)
{
	unsigned long rate = 0;

	if (ddr_rate <= 333000000)
		return count;
	if (count < 1)
		return count;
	if (copy_from_user(&video_state, buffer, 1)) {
		return -EFAULT;
	}

	switch (video_state) {
	case '0':
		rate = ddr_rate;
		break;
	case '1':
		rate = VIDEO_DDR_RATE;
		break;
	default:
		return -EINVAL;
	}
	clk_set_rate(ddr_clk, rate);
	return count;
}

static const struct file_operations video_state_fops = {
	.owner	= THIS_MODULE,
	.open	= video_state_open,
	.release= video_state_release,
	.read	= video_state_read,
	.write	= video_state_write,
};

static struct miscdevice video_state_dev = {
	.fops	= &video_state_fops,
	.name	= "video_state",
	.minor	= MISC_DYNAMIC_MINOR,
};

static int __init video_state_init(void)
{
	ddr_clk = clk_get(NULL, "ddr");
	if (IS_ERR(ddr_clk))
		return PTR_ERR(ddr_clk);

	return misc_register(&video_state_dev);
}

static void __exit video_state_exit(void)
{
	misc_deregister(&video_state_dev);
	clk_put(ddr_clk);
}

module_init(video_state_init);
module_exit(video_state_exit);
MODULE_LICENSE("GPL");
