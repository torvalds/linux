/*
 * File:         arch/blackfin/oprofile/common.c
 * Based on:     arch/alpha/oprofile/common.c
 * Author:       Anton Blanchard <anton@au.ibm.com>
 *
 * Created:
 * Description:
 *
 * Modified:
 *               Copyright (C) 2004 Anton Blanchard <anton@au.ibm.com>, IBM
 *               Copyright 2004-2006 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/oprofile.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/ptrace.h>
#include <linux/irq.h>
#include <linux/io.h>

#include <asm/system.h>
#include <asm/blackfin.h>

#include "op_blackfin.h"

#define BFIN_533_ID  0xE5040003
#define BFIN_537_ID  0xE5040002

static int pfmon_enabled;
static struct mutex pfmon_lock;

struct op_bfin533_model *model;

struct op_counter_config ctr[OP_MAX_COUNTER];

static int op_bfin_setup(void)
{
	int ret;

	/* Pre-compute the values to stuff in the hardware registers.  */
	spin_lock(&oprofilefs_lock);
	ret = model->reg_setup(ctr);
	spin_unlock(&oprofilefs_lock);

	return ret;
}

static void op_bfin_shutdown(void)
{
#if 0
	/* what is the difference between shutdown and stop? */
#endif
}

static int op_bfin_start(void)
{
	int ret = -EBUSY;

	printk(KERN_INFO "KSDBG:in %s\n", __FUNCTION__);
	mutex_lock(&pfmon_lock);
	if (!pfmon_enabled) {
		ret = model->start(ctr);
		pfmon_enabled = !ret;
	}
	mutex_unlock(&pfmon_lock);

	return ret;
}

static void op_bfin_stop(void)
{
	mutex_lock(&pfmon_lock);
	if (pfmon_enabled) {
		model->stop();
		pfmon_enabled = 0;
	}
	mutex_unlock(&pfmon_lock);
}

static int op_bfin_create_files(struct super_block *sb, struct dentry *root)
{
	int i;

	for (i = 0; i < model->num_counters; ++i) {
		struct dentry *dir;
		char buf[3];
		printk(KERN_INFO "Oprofile: creating files... \n");

		snprintf(buf, sizeof buf, "%d", i);
		dir = oprofilefs_mkdir(sb, root, buf);

		oprofilefs_create_ulong(sb, dir, "enabled", &ctr[i].enabled);
		oprofilefs_create_ulong(sb, dir, "event", &ctr[i].event);
		oprofilefs_create_ulong(sb, dir, "count", &ctr[i].count);
		/*
		 * We dont support per counter user/kernel selection, but
		 * we leave the entries because userspace expects them
		 */
		oprofilefs_create_ulong(sb, dir, "kernel", &ctr[i].kernel);
		oprofilefs_create_ulong(sb, dir, "user", &ctr[i].user);
		oprofilefs_create_ulong(sb, dir, "unit_mask",
					&ctr[i].unit_mask);
	}

	return 0;
}
int __init oprofile_arch_init(struct oprofile_operations *ops)
{
#ifdef CONFIG_HARDWARE_PM
	unsigned int dspid;

	mutex_init(&pfmon_lock);

	dspid = bfin_read_DSPID();

	printk(KERN_INFO "Oprofile got the cpu id is 0x%x. \n", dspid);

	switch (dspid) {
	case BFIN_533_ID:
		model = &op_model_bfin533;
		model->num_counters = 2;
		break;
	case BFIN_537_ID:
		model = &op_model_bfin533;
		model->num_counters = 2;
		break;
	default:
		return -ENODEV;
	}

	ops->cpu_type = model->name;
	ops->create_files = op_bfin_create_files;
	ops->setup = op_bfin_setup;
	ops->shutdown = op_bfin_shutdown;
	ops->start = op_bfin_start;
	ops->stop = op_bfin_stop;

	printk(KERN_INFO "oprofile: using %s performance monitoring.\n",
	       ops->cpu_type);

	return 0;
#else
	return -1;
#endif
}

void oprofile_arch_exit(void)
{
}
