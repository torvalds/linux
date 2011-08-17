/**
 * arch/s390/oprofile/init.c
 *
 * S390 Version
 *   Copyright (C) 2003 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *   Author(s): Thomas Spatzier (tspat@de.ibm.com)
 *   Author(s): Mahesh Salgaonkar (mahesh@linux.vnet.ibm.com)
 *   Author(s): Heinz Graalfs (graalfs@linux.vnet.ibm.com)
 *
 * @remark Copyright 2002-2011 OProfile authors
 */

#include <linux/oprofile.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/fs.h>

#include "../../../drivers/oprofile/oprof.h"

extern void s390_backtrace(struct pt_regs * const regs, unsigned int depth);

#ifdef CONFIG_64BIT

#include "hwsampler.h"

#define DEFAULT_INTERVAL	4127518

#define DEFAULT_SDBT_BLOCKS	1
#define DEFAULT_SDB_BLOCKS	511

static unsigned long oprofile_hw_interval = DEFAULT_INTERVAL;
static unsigned long oprofile_min_interval;
static unsigned long oprofile_max_interval;

static unsigned long oprofile_sdbt_blocks = DEFAULT_SDBT_BLOCKS;
static unsigned long oprofile_sdb_blocks = DEFAULT_SDB_BLOCKS;

static int hwsampler_file;
static int hwsampler_running;	/* start_mutex must be held to change */

static struct oprofile_operations timer_ops;

static int oprofile_hwsampler_start(void)
{
	int retval;

	hwsampler_running = hwsampler_file;

	if (!hwsampler_running)
		return timer_ops.start();

	retval = hwsampler_allocate(oprofile_sdbt_blocks, oprofile_sdb_blocks);
	if (retval)
		return retval;

	retval = hwsampler_start_all(oprofile_hw_interval);
	if (retval)
		hwsampler_deallocate();

	return retval;
}

static void oprofile_hwsampler_stop(void)
{
	if (!hwsampler_running) {
		timer_ops.stop();
		return;
	}

	hwsampler_stop_all();
	hwsampler_deallocate();
	return;
}

static ssize_t hwsampler_read(struct file *file, char __user *buf,
		size_t count, loff_t *offset)
{
	return oprofilefs_ulong_to_user(hwsampler_file, buf, count, offset);
}

static ssize_t hwsampler_write(struct file *file, char const __user *buf,
		size_t count, loff_t *offset)
{
	unsigned long val;
	int retval;

	if (*offset)
		return -EINVAL;

	retval = oprofilefs_ulong_from_user(&val, buf, count);
	if (retval)
		return retval;

	if (oprofile_started)
		/*
		 * save to do without locking as we set
		 * hwsampler_running in start() when start_mutex is
		 * held
		 */
		return -EBUSY;

	hwsampler_file = val;

	return count;
}

static const struct file_operations hwsampler_fops = {
	.read		= hwsampler_read,
	.write		= hwsampler_write,
};

static int oprofile_create_hwsampling_files(struct super_block *sb,
						struct dentry *root)
{
	struct dentry *hw_dir;

	/* reinitialize default values */
	hwsampler_file = 1;

	hw_dir = oprofilefs_mkdir(sb, root, "hwsampling");
	if (!hw_dir)
		return -EINVAL;

	oprofilefs_create_file(sb, hw_dir, "hwsampler", &hwsampler_fops);
	oprofilefs_create_ulong(sb, hw_dir, "hw_interval",
				&oprofile_hw_interval);
	oprofilefs_create_ro_ulong(sb, hw_dir, "hw_min_interval",
				&oprofile_min_interval);
	oprofilefs_create_ro_ulong(sb, hw_dir, "hw_max_interval",
				&oprofile_max_interval);
	oprofilefs_create_ulong(sb, hw_dir, "hw_sdbt_blocks",
				&oprofile_sdbt_blocks);

	return 0;
}

static int oprofile_hwsampler_init(struct oprofile_operations *ops)
{
	if (hwsampler_setup())
		return -ENODEV;

	/*
	 * create hwsampler files only if hwsampler_setup() succeeds.
	 */
	oprofile_min_interval = hwsampler_query_min_interval();
	if (oprofile_min_interval == 0)
		return -ENODEV;
	oprofile_max_interval = hwsampler_query_max_interval();
	if (oprofile_max_interval == 0)
		return -ENODEV;

	/* The initial value should be sane */
	if (oprofile_hw_interval < oprofile_min_interval)
		oprofile_hw_interval = oprofile_min_interval;
	if (oprofile_hw_interval > oprofile_max_interval)
		oprofile_hw_interval = oprofile_max_interval;

	if (oprofile_timer_init(ops))
		return -ENODEV;

	printk(KERN_INFO "oprofile: using hardware sampling\n");

	memcpy(&timer_ops, ops, sizeof(timer_ops));

	ops->start = oprofile_hwsampler_start;
	ops->stop = oprofile_hwsampler_stop;
	ops->create_files = oprofile_create_hwsampling_files;

	return 0;
}

static void oprofile_hwsampler_exit(void)
{
	oprofile_timer_exit();
	hwsampler_shutdown();
}

#endif /* CONFIG_64BIT */

int __init oprofile_arch_init(struct oprofile_operations *ops)
{
	ops->backtrace = s390_backtrace;

#ifdef CONFIG_64BIT
	return oprofile_hwsampler_init(ops);
#else
	return -ENODEV;
#endif
}

void oprofile_arch_exit(void)
{
#ifdef CONFIG_64BIT
	oprofile_hwsampler_exit();
#endif
}
