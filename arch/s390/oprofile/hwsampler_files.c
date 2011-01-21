/**
 * arch/s390/oprofile/hwsampler_files.c
 *
 * Copyright IBM Corp. 2010
 * Author: Mahesh Salgaonkar (mahesh@linux.vnet.ibm.com)
 */
#include <linux/oprofile.h>
#include <linux/errno.h>
#include <linux/fs.h>

#include "hwsampler.h"

#define DEFAULT_INTERVAL	4096

#define DEFAULT_SDBT_BLOCKS	1
#define DEFAULT_SDB_BLOCKS	511

static unsigned long oprofile_hw_interval = DEFAULT_INTERVAL;
static unsigned long oprofile_min_interval;
static unsigned long oprofile_max_interval;

static unsigned long oprofile_sdbt_blocks = DEFAULT_SDBT_BLOCKS;
static unsigned long oprofile_sdb_blocks = DEFAULT_SDB_BLOCKS;

static unsigned long oprofile_hwsampler;

static int oprofile_hwsampler_start(void)
{
	int retval;

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
	hwsampler_stop_all();
	hwsampler_deallocate();
	return;
}

int oprofile_arch_set_hwsampler(struct oprofile_operations *ops)
{
	printk(KERN_INFO "oprofile: using hardware sampling\n");
	ops->start = oprofile_hwsampler_start;
	ops->stop = oprofile_hwsampler_stop;
	ops->cpu_type = "timer";

	return 0;
}

static ssize_t hwsampler_read(struct file *file, char __user *buf,
		size_t count, loff_t *offset)
{
	return oprofilefs_ulong_to_user(oprofile_hwsampler, buf, count, offset);
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

	if (oprofile_hwsampler == val)
		return -EINVAL;

	retval = oprofile_set_hwsampler(val);

	if (retval)
		return retval;

	oprofile_hwsampler = val;
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
	oprofile_hwsampler = 1;

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

int oprofile_hwsampler_init(struct oprofile_operations* ops)
{
	if (hwsampler_setup())
		return -ENODEV;

	/*
	 * create hwsampler files only if hwsampler_setup() succeeds.
	 */
	ops->create_files = oprofile_create_hwsampling_files;
	oprofile_min_interval = hwsampler_query_min_interval();
	if (oprofile_min_interval < 0) {
		oprofile_min_interval = 0;
		return -ENODEV;
	}
	oprofile_max_interval = hwsampler_query_max_interval();
	if (oprofile_max_interval < 0) {
		oprofile_max_interval = 0;
		return -ENODEV;
	}
	oprofile_arch_set_hwsampler(ops);
	return 0;
}

void oprofile_hwsampler_exit(void)
{
	hwsampler_shutdown();
}
