// SPDX-License-Identifier: GPL-2.0
/*
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 *		    Horst Hummel <Horst.Hummel@de.ibm.com>
 *		    Carsten Otte <Cotte@de.ibm.com>
 *		    Martin Schwidefsky <schwidefsky@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * Coypright IBM Corp. 1999, 2002
 *
 * /proc interface for the dasd driver.
 *
 */

#define KMSG_COMPONENT "dasd"

#include <linux/ctype.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/seq_file.h>
#include <linux/vmalloc.h>
#include <linux/proc_fs.h>

#include <asm/debug.h>
#include <linux/uaccess.h>

/* This is ugly... */
#define PRINTK_HEADER "dasd_proc:"

#include "dasd_int.h"

static struct proc_dir_entry *dasd_proc_root_entry = NULL;
static struct proc_dir_entry *dasd_devices_entry = NULL;
static struct proc_dir_entry *dasd_statistics_entry = NULL;

static int
dasd_devices_show(struct seq_file *m, void *v)
{
	struct dasd_device *device;
	struct dasd_block *block;
	char *substr;

	device = dasd_device_from_devindex((unsigned long) v - 1);
	if (IS_ERR(device))
		return 0;
	if (device->block)
		block = device->block;
	else {
		dasd_put_device(device);
		return 0;
	}
	/* Print device number. */
	seq_printf(m, "%s", dev_name(&device->cdev->dev));
	/* Print discipline string. */
	if (device->discipline != NULL)
		seq_printf(m, "(%s)", device->discipline->name);
	else
		seq_printf(m, "(none)");
	/* Print kdev. */
	if (block->gdp)
		seq_printf(m, " at (%3d:%6d)",
			   MAJOR(disk_devt(block->gdp)),
			   MINOR(disk_devt(block->gdp)));
	else
		seq_printf(m, "  at (???:??????)");
	/* Print device name. */
	if (block->gdp)
		seq_printf(m, " is %-8s", block->gdp->disk_name);
	else
		seq_printf(m, " is ????????");
	/* Print devices features. */
	substr = (device->features & DASD_FEATURE_READONLY) ? "(ro)" : " ";
	seq_printf(m, "%4s: ", substr);
	/* Print device status information. */
	switch (device->state) {
	case DASD_STATE_NEW:
		seq_printf(m, "new");
		break;
	case DASD_STATE_KNOWN:
		seq_printf(m, "detected");
		break;
	case DASD_STATE_BASIC:
		seq_printf(m, "basic");
		break;
	case DASD_STATE_UNFMT:
		seq_printf(m, "unformatted");
		break;
	case DASD_STATE_READY:
	case DASD_STATE_ONLINE:
		seq_printf(m, "active ");
		if (dasd_check_blocksize(block->bp_block))
			seq_printf(m, "n/f	 ");
		else
			seq_printf(m,
				   "at blocksize: %u, %lu blocks, %lu MB",
				   block->bp_block, block->blocks,
				   ((block->bp_block >> 9) *
				    block->blocks) >> 11);
		break;
	default:
		seq_printf(m, "no stat");
		break;
	}
	dasd_put_device(device);
	if (dasd_probeonly)
		seq_printf(m, "(probeonly)");
	seq_printf(m, "\n");
	return 0;
}

static void *dasd_devices_start(struct seq_file *m, loff_t *pos)
{
	if (*pos >= dasd_max_devindex)
		return NULL;
	return (void *)((unsigned long) *pos + 1);
}

static void *dasd_devices_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return dasd_devices_start(m, pos);
}

static void dasd_devices_stop(struct seq_file *m, void *v)
{
}

static const struct seq_operations dasd_devices_seq_ops = {
	.start		= dasd_devices_start,
	.next		= dasd_devices_next,
	.stop		= dasd_devices_stop,
	.show		= dasd_devices_show,
};

#ifdef CONFIG_DASD_PROFILE
static int dasd_stats_all_block_on(void)
{
	int i, rc;
	struct dasd_device *device;

	rc = 0;
	for (i = 0; i < dasd_max_devindex; ++i) {
		device = dasd_device_from_devindex(i);
		if (IS_ERR(device))
			continue;
		if (device->block)
			rc = dasd_profile_on(&device->block->profile);
		dasd_put_device(device);
		if (rc)
			return rc;
	}
	return 0;
}

static void dasd_stats_all_block_off(void)
{
	int i;
	struct dasd_device *device;

	for (i = 0; i < dasd_max_devindex; ++i) {
		device = dasd_device_from_devindex(i);
		if (IS_ERR(device))
			continue;
		if (device->block)
			dasd_profile_off(&device->block->profile);
		dasd_put_device(device);
	}
}

static void dasd_stats_all_block_reset(void)
{
	int i;
	struct dasd_device *device;

	for (i = 0; i < dasd_max_devindex; ++i) {
		device = dasd_device_from_devindex(i);
		if (IS_ERR(device))
			continue;
		if (device->block)
			dasd_profile_reset(&device->block->profile);
		dasd_put_device(device);
	}
}

static void dasd_statistics_array(struct seq_file *m, unsigned int *array, int factor)
{
	int i;

	for (i = 0; i < 32; i++) {
		seq_printf(m, "%7d ", array[i] / factor);
		if (i == 15)
			seq_putc(m, '\n');
	}
	seq_putc(m, '\n');
}
#endif /* CONFIG_DASD_PROFILE */

static int dasd_stats_proc_show(struct seq_file *m, void *v)
{
#ifdef CONFIG_DASD_PROFILE
	struct dasd_profile_info *prof;
	int factor;

	spin_lock_bh(&dasd_global_profile.lock);
	prof = dasd_global_profile.data;
	if (!prof) {
		spin_unlock_bh(&dasd_global_profile.lock);
		seq_printf(m, "Statistics are off - they might be "
				    "switched on using 'echo set on > "
				    "/proc/dasd/statistics'\n");
		return 0;
	}

	/* prevent counter 'overflow' on output */
	for (factor = 1; (prof->dasd_io_reqs / factor) > 9999999;
	     factor *= 10);

	seq_printf(m, "%d dasd I/O requests\n", prof->dasd_io_reqs);
	seq_printf(m, "with %u sectors(512B each)\n",
		       prof->dasd_io_sects);
	seq_printf(m, "Scale Factor is  %d\n", factor);
	seq_printf(m,
		       "   __<4	   ___8	   __16	   __32	   __64	   _128	"
		       "   _256	   _512	   __1k	   __2k	   __4k	   __8k	"
		       "   _16k	   _32k	   _64k	   128k\n");
	seq_printf(m,
		       "   _256	   _512	   __1M	   __2M	   __4M	   __8M	"
		       "   _16M	   _32M	   _64M	   128M	   256M	   512M	"
		       "   __1G	   __2G	   __4G " "   _>4G\n");

	seq_printf(m, "Histogram of sizes (512B secs)\n");
	dasd_statistics_array(m, prof->dasd_io_secs, factor);
	seq_printf(m, "Histogram of I/O times (microseconds)\n");
	dasd_statistics_array(m, prof->dasd_io_times, factor);
	seq_printf(m, "Histogram of I/O times per sector\n");
	dasd_statistics_array(m, prof->dasd_io_timps, factor);
	seq_printf(m, "Histogram of I/O time till ssch\n");
	dasd_statistics_array(m, prof->dasd_io_time1, factor);
	seq_printf(m, "Histogram of I/O time between ssch and irq\n");
	dasd_statistics_array(m, prof->dasd_io_time2, factor);
	seq_printf(m, "Histogram of I/O time between ssch "
			    "and irq per sector\n");
	dasd_statistics_array(m, prof->dasd_io_time2ps, factor);
	seq_printf(m, "Histogram of I/O time between irq and end\n");
	dasd_statistics_array(m, prof->dasd_io_time3, factor);
	seq_printf(m, "# of req in chanq at enqueuing (1..32) \n");
	dasd_statistics_array(m, prof->dasd_io_nr_req, factor);
	spin_unlock_bh(&dasd_global_profile.lock);
#else
	seq_printf(m, "Statistics are not activated in this kernel\n");
#endif
	return 0;
}

static int dasd_stats_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, dasd_stats_proc_show, NULL);
}

static ssize_t dasd_stats_proc_write(struct file *file,
		const char __user *user_buf, size_t user_len, loff_t *pos)
{
#ifdef CONFIG_DASD_PROFILE
	char *buffer, *str;
	int rc;

	if (user_len > 65536)
		user_len = 65536;
	buffer = dasd_get_user_string(user_buf, user_len);
	if (IS_ERR(buffer))
		return PTR_ERR(buffer);

	/* check for valid verbs */
	str = skip_spaces(buffer);
	if (strncmp(str, "set", 3) == 0 && isspace(str[3])) {
		/* 'set xxx' was given */
		str = skip_spaces(str + 4);
		if (strcmp(str, "on") == 0) {
			/* switch on statistics profiling */
			rc = dasd_stats_all_block_on();
			if (rc) {
				dasd_stats_all_block_off();
				goto out_error;
			}
			rc = dasd_profile_on(&dasd_global_profile);
			if (rc) {
				dasd_stats_all_block_off();
				goto out_error;
			}
			dasd_profile_reset(&dasd_global_profile);
			dasd_global_profile_level = DASD_PROFILE_ON;
			pr_info("The statistics feature has been switched "
				"on\n");
		} else if (strcmp(str, "off") == 0) {
			/* switch off statistics profiling */
			dasd_global_profile_level = DASD_PROFILE_OFF;
			dasd_profile_off(&dasd_global_profile);
			dasd_stats_all_block_off();
			pr_info("The statistics feature has been switched "
				"off\n");
		} else
			goto out_parse_error;
	} else if (strncmp(str, "reset", 5) == 0) {
		/* reset the statistics */
		dasd_profile_reset(&dasd_global_profile);
		dasd_stats_all_block_reset();
		pr_info("The statistics have been reset\n");
	} else
		goto out_parse_error;
	vfree(buffer);
	return user_len;
out_parse_error:
	rc = -EINVAL;
	pr_warn("%s is not a supported value for /proc/dasd/statistics\n", str);
out_error:
	vfree(buffer);
	return rc;
#else
	pr_warn("/proc/dasd/statistics: is not activated in this kernel\n");
	return user_len;
#endif				/* CONFIG_DASD_PROFILE */
}

static const struct file_operations dasd_stats_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= dasd_stats_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= dasd_stats_proc_write,
};

/*
 * Create dasd proc-fs entries.
 * In case creation failed, cleanup and return -ENOENT.
 */
int
dasd_proc_init(void)
{
	dasd_proc_root_entry = proc_mkdir("dasd", NULL);
	if (!dasd_proc_root_entry)
		goto out_nodasd;
	dasd_devices_entry = proc_create_seq("devices",
					 S_IFREG | S_IRUGO | S_IWUSR,
					 dasd_proc_root_entry,
					 &dasd_devices_seq_ops);
	if (!dasd_devices_entry)
		goto out_nodevices;
	dasd_statistics_entry = proc_create("statistics",
					    S_IFREG | S_IRUGO | S_IWUSR,
					    dasd_proc_root_entry,
					    &dasd_stats_proc_fops);
	if (!dasd_statistics_entry)
		goto out_nostatistics;
	return 0;

 out_nostatistics:
	remove_proc_entry("devices", dasd_proc_root_entry);
 out_nodevices:
	remove_proc_entry("dasd", NULL);
 out_nodasd:
	return -ENOENT;
}

void
dasd_proc_exit(void)
{
	remove_proc_entry("devices", dasd_proc_root_entry);
	remove_proc_entry("statistics", dasd_proc_root_entry);
	remove_proc_entry("dasd", NULL);
}
