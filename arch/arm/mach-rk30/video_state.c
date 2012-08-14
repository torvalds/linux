/* arch/arm/mach-rk30/video_state.c
 *
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

#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/sched.h>
#include <mach/ddr.h>
#include <linux/cpu.h>
#include <linux/clk.h>


#ifdef CONFIG_DDR_SDRAM_FREQ
#define DDR_FREQ          (CONFIG_DDR_SDRAM_FREQ)
#else
#define DDR_FREQ 400
#endif


int rk_video_state=0;
static struct clk * ddr_clk;


#define VIDEO_STATE_NAME		"video_state"
#define BUFFER_SIZE			16

MODULE_AUTHOR("Mike Lockwood <lockwood@android.com>");
MODULE_DESCRIPTION("Key chord input driver");
MODULE_SUPPORTED_DEVICE("video_state");
MODULE_LICENSE("GPL");

/*
 * video_state_read is used to read video_state events from the driver
 */
static ssize_t video_state_read(struct file *file, char __user *buffer,
		size_t count, loff_t *ppos)
{
	return count;
}

/*
 * video_state_write is used to configure the driver
 */
static ssize_t video_state_write(struct file *file, const char __user *buffer,
		size_t count, loff_t *ppos)
{
	char *parameters= 0;
	int vedeo_state;
	int set_rate=0;
	int set_rate_old=0;

	if(DDR_FREQ<=333)	
		return count;
	//if (count < sizeof(struct input_keychord))
	//	return -EINVAL;
	parameters = kzalloc(count, GFP_KERNEL);
	if (!parameters)
		return -ENOMEM;

	/* read list of keychords from userspace */
	if (copy_from_user(parameters, buffer, count)) {
		kfree(parameters);
		return -EFAULT;
	}
	sscanf(parameters, "%d", &vedeo_state);

	//printk("video_state %d\n",vedeo_state);
	switch(vedeo_state)
	{
		case 0:
			rk_video_state=0;
			set_rate=DDR_FREQ;
		break;
		case 1:
			rk_video_state=1;
			set_rate=300;
		break;
		default:
			rk_video_state=0;
			return -EFAULT;	
	}
	set_rate_old=clk_get_rate(ddr_clk);
	set_rate=clk_round_rate(ddr_clk,set_rate*1000*1000);
	if(set_rate_old!=set_rate)
	{
		clk_set_rate(ddr_clk,set_rate);
		//printk("ddr rate=%d\n",set_rate/(1000*1000));
	}	
	kfree(parameters);
	return count;
}

static unsigned int video_state_poll(struct file *file, poll_table *wait)
{
	return 0;
}

static int video_state_open(struct inode *inode, struct file *file)
{


	return 0;
}

static int video_state_release(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations video_state_fops = {
	.owner		= THIS_MODULE,
	.open		= video_state_open,
	.release	= video_state_release,
	.read		= video_state_read,
	.write		= video_state_write,
	.poll		= video_state_poll,
};

static struct miscdevice video_state = {
	.fops		= &video_state_fops,
	.name		= VIDEO_STATE_NAME,
	.minor		= MISC_DYNAMIC_MINOR,
};


static int __init video_state_init(void)
{
	ddr_clk=clk_get(NULL,"ddr");
	if(IS_ERR(ddr_clk))
		return -1;

	
	return misc_register(&video_state);
}

static void __exit video_state_exit(void)
{
	misc_deregister(&video_state);
}

module_init(video_state_init);
module_exit(video_state_exit);



