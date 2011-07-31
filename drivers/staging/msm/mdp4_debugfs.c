/* Copyright (c) 2009, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/hrtimer.h>
#include <linux/clk.h>
#include <mach/hardware.h>
#include <linux/io.h>
#include <linux/debugfs.h>

#include <asm/system.h>
#include <asm/mach-types.h>
#include <linux/semaphore.h>
#include <linux/uaccess.h>

#include "mdp.h"
#include "msm_fb.h"
#include "mdp4.h"


#define MDP4_DEBUG_BUF	128


static char mdp4_debug_buf[MDP4_DEBUG_BUF];
static ulong mdp4_debug_offset;
static ulong mdp4_base_addr;

static int mdp4_offset_set(void *data, u64 val)
{
	mdp4_debug_offset = (int)val;
	return 0;
}

static int mdp4_offset_get(void *data, u64 *val)
{
	*val = (u64)mdp4_debug_offset;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(
			mdp4_offset_fops,
			mdp4_offset_get,
			mdp4_offset_set,
			"%llx\n");


static int mdp4_debugfs_release(struct inode *inode, struct file *file)
{
	return 0;
}

static ssize_t mdp4_debugfs_write(
	struct file *file,
	const char __user *buff,
	size_t count,
	loff_t *ppos)
{
	int cnt;
	unsigned int data;

	printk(KERN_INFO "%s: offset=%d count=%d *ppos=%d\n",
		__func__, (int)mdp4_debug_offset, (int)count, (int)*ppos);

	if (count > sizeof(mdp4_debug_buf))
		return -EFAULT;

	if (copy_from_user(mdp4_debug_buf, buff, count))
		return -EFAULT;


	mdp4_debug_buf[count] = 0;	/* end of string */

	cnt = sscanf(mdp4_debug_buf, "%x", &data);
	if (cnt < 1) {
		printk(KERN_ERR "%s: sscanf failed cnt=%d" , __func__, cnt);
		return -EINVAL;
	}

	writel(&data, mdp4_base_addr + mdp4_debug_offset);

	return 0;
}

static ssize_t mdp4_debugfs_read(
	struct file *file,
	char __user *buff,
	size_t count,
	loff_t *ppos)
{
	int len = 0;
	unsigned int data;

	printk(KERN_INFO "%s: offset=%d count=%d *ppos=%d\n",
		__func__, (int)mdp4_debug_offset, (int)count, (int)*ppos);

	if (*ppos)
		return 0;	/* the end */

	data = readl(mdp4_base_addr + mdp4_debug_offset);

	len = snprintf(mdp4_debug_buf, 4, "%x\n", data);

	if (len > 0) {
		if (len > count)
			len = count;
		if (copy_to_user(buff, mdp4_debug_buf, len))
			return -EFAULT;
	}

	printk(KERN_INFO "%s: len=%d\n", __func__, len);

	if (len < 0)
		return 0;

	*ppos += len;	/* increase offset */

	return len;
}

static const struct file_operations mdp4_debugfs_fops = {
	.open = nonseekable_open,
	.release = mdp4_debugfs_release,
	.read = mdp4_debugfs_read,
	.write = mdp4_debugfs_write,
	.llseek = no_llseek,
};

int mdp4_debugfs_init(void)
{
	struct dentry *dent = debugfs_create_dir("mdp4", NULL);

	if (IS_ERR(dent)) {
		printk(KERN_ERR "%s(%d): debugfs_create_dir fail, error %ld\n",
			__FILE__, __LINE__, PTR_ERR(dent));
		return -1;
	}

	if (debugfs_create_file("offset", 0644, dent, 0, &mdp4_offset_fops)
			== NULL) {
		printk(KERN_ERR "%s(%d): debugfs_create_file: offset fail\n",
			__FILE__, __LINE__);
		return -1;
	}

	if (debugfs_create_file("regs", 0644, dent, 0, &mdp4_debugfs_fops)
			== NULL) {
		printk(KERN_ERR "%s(%d): debugfs_create_file: regs fail\n",
			__FILE__, __LINE__);
		return -1;
	}

	mdp4_debug_offset = 0;
	mdp4_base_addr = (ulong) msm_mdp_base;	/* defined at msm_fb_def.h */

	return 0;
}
