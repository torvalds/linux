/*
 * File...........: linux/drivers/s390/block/dasd_proc.c
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 *		    Horst Hummel <Horst.Hummel@de.ibm.com>
 *		    Carsten Otte <Cotte@de.ibm.com>
 *		    Martin Schwidefsky <schwidefsky@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 1999-2002
 *
 * /proc interface for the dasd driver.
 *
 */

#include <linux/config.h>
#include <linux/ctype.h>
#include <linux/seq_file.h>
#include <linux/vmalloc.h>
#include <linux/proc_fs.h>

#include <asm/debug.h>
#include <asm/uaccess.h>

/* This is ugly... */
#define PRINTK_HEADER "dasd_proc:"

#include "dasd_int.h"

static struct proc_dir_entry *dasd_proc_root_entry = NULL;
static struct proc_dir_entry *dasd_devices_entry = NULL;
static struct proc_dir_entry *dasd_statistics_entry = NULL;

static inline char *
dasd_get_user_string(const char __user *user_buf, size_t user_len)
{
	char *buffer;

	buffer = kmalloc(user_len + 1, GFP_KERNEL);
	if (buffer == NULL)
		return ERR_PTR(-ENOMEM);
	if (copy_from_user(buffer, user_buf, user_len) != 0) {
		kfree(buffer);
		return ERR_PTR(-EFAULT);
	}
	/* got the string, now strip linefeed. */
	if (buffer[user_len - 1] == '\n')
		buffer[user_len - 1] = 0;
	else
		buffer[user_len] = 0;
	return buffer;
}

static int
dasd_devices_show(struct seq_file *m, void *v)
{
	struct dasd_device *device;
	char *substr;

	device = dasd_device_from_devindex((unsigned long) v - 1);
	if (IS_ERR(device))
		return 0;
	/* Print device number. */
	seq_printf(m, "%s", device->cdev->dev.bus_id);
	/* Print discipline string. */
	if (device != NULL && device->discipline != NULL)
		seq_printf(m, "(%s)", device->discipline->name);
	else
		seq_printf(m, "(none)");
	/* Print kdev. */
	if (device->gdp)
		seq_printf(m, " at (%3d:%6d)",
			   device->gdp->major, device->gdp->first_minor);
	else
		seq_printf(m, "  at (???:??????)");
	/* Print device name. */
	if (device->gdp)
		seq_printf(m, " is %-8s", device->gdp->disk_name);
	else
		seq_printf(m, " is ????????");
	/* Print devices features. */
	substr = (device->features & DASD_FEATURE_READONLY) ? "(ro)" : " ";
	seq_printf(m, "%4s: ", substr);
	/* Print device status information. */
	switch ((device != NULL) ? device->state : -1) {
	case -1:
		seq_printf(m, "unknown");
		break;
	case DASD_STATE_NEW:
		seq_printf(m, "new");
		break;
	case DASD_STATE_KNOWN:
		seq_printf(m, "detected");
		break;
	case DASD_STATE_BASIC:
		seq_printf(m, "basic");
		break;
	case DASD_STATE_READY:
	case DASD_STATE_ONLINE:
		seq_printf(m, "active ");
		if (dasd_check_blocksize(device->bp_block))
			seq_printf(m, "n/f	 ");
		else
			seq_printf(m,
				   "at blocksize: %d, %ld blocks, %ld MB",
				   device->bp_block, device->blocks,
				   ((device->bp_block >> 9) *
				    device->blocks) >> 11);
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

static struct seq_operations dasd_devices_seq_ops = {
	.start		= dasd_devices_start,
	.next		= dasd_devices_next,
	.stop		= dasd_devices_stop,
	.show		= dasd_devices_show,
};

static int dasd_devices_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &dasd_devices_seq_ops);
}

static struct file_operations dasd_devices_file_ops = {
	.open		= dasd_devices_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static inline int
dasd_calc_metrics(char *page, char **start, off_t off,
		  int count, int *eof, int len)
{
	len = (len > off) ? len - off : 0;
	if (len > count)
		len = count;
	if (len < count)
		*eof = 1;
	*start = page + off;
	return len;
}

static inline char *
dasd_statistics_array(char *str, int *array, int shift)
{
	int i;

	for (i = 0; i < 32; i++) {
		str += sprintf(str, "%7d ", array[i] >> shift);
		if (i == 15)
			str += sprintf(str, "\n");
	}
	str += sprintf(str,"\n");
	return str;
}

static int
dasd_statistics_read(char *page, char **start, off_t off,
		     int count, int *eof, void *data)
{
	unsigned long len;
#ifdef CONFIG_DASD_PROFILE
	struct dasd_profile_info_t *prof;
	char *str;
	int shift;

	/* check for active profiling */
	if (dasd_profile_level == DASD_PROFILE_OFF) {
		len = sprintf(page, "Statistics are off - they might be "
				    "switched on using 'echo set on > "
				    "/proc/dasd/statistics'\n");
		return dasd_calc_metrics(page, start, off, count, eof, len);
	}

	prof = &dasd_global_profile;
	/* prevent couter 'overflow' on output */
	for (shift = 0; (prof->dasd_io_reqs >> shift) > 9999999; shift++);

	str = page;
	str += sprintf(str, "%d dasd I/O requests\n", prof->dasd_io_reqs);
	str += sprintf(str, "with %d sectors(512B each)\n",
		       prof->dasd_io_sects);
	str += sprintf(str,
		       "   __<4	   ___8	   __16	   __32	   __64	   _128	"
		       "   _256	   _512	   __1k	   __2k	   __4k	   __8k	"
		       "   _16k	   _32k	   _64k	   128k\n");
	str += sprintf(str,
		       "   _256	   _512	   __1M	   __2M	   __4M	   __8M	"
		       "   _16M	   _32M	   _64M	   128M	   256M	   512M	"
		       "   __1G	   __2G	   __4G " "   _>4G\n");

	str += sprintf(str, "Histogram of sizes (512B secs)\n");
	str = dasd_statistics_array(str, prof->dasd_io_secs, shift);
	str += sprintf(str, "Histogram of I/O times (microseconds)\n");
	str = dasd_statistics_array(str, prof->dasd_io_times, shift);
	str += sprintf(str, "Histogram of I/O times per sector\n");
	str = dasd_statistics_array(str, prof->dasd_io_timps, shift);
	str += sprintf(str, "Histogram of I/O time till ssch\n");
	str = dasd_statistics_array(str, prof->dasd_io_time1, shift);
	str += sprintf(str, "Histogram of I/O time between ssch and irq\n");
	str = dasd_statistics_array(str, prof->dasd_io_time2, shift);
	str += sprintf(str, "Histogram of I/O time between ssch "
			    "and irq per sector\n");
	str = dasd_statistics_array(str, prof->dasd_io_time2ps, shift);
	str += sprintf(str, "Histogram of I/O time between irq and end\n");
	str = dasd_statistics_array(str, prof->dasd_io_time3, shift);
	str += sprintf(str, "# of req in chanq at enqueuing (1..32) \n");
	str = dasd_statistics_array(str, prof->dasd_io_nr_req, shift);
	len = str - page;
#else
	len = sprintf(page, "Statistics are not activated in this kernel\n");
#endif
	return dasd_calc_metrics(page, start, off, count, eof, len);
}

static int
dasd_statistics_write(struct file *file, const char __user *user_buf,
		      unsigned long user_len, void *data)
{
#ifdef CONFIG_DASD_PROFILE
	char *buffer, *str;

	if (user_len > 65536)
		user_len = 65536;
	buffer = dasd_get_user_string(user_buf, user_len);
	if (IS_ERR(buffer))
		return PTR_ERR(buffer);
	MESSAGE_LOG(KERN_INFO, "/proc/dasd/statictics: '%s'", buffer);

	/* check for valid verbs */
	for (str = buffer; isspace(*str); str++);
	if (strncmp(str, "set", 3) == 0 && isspace(str[3])) {
		/* 'set xxx' was given */
		for (str = str + 4; isspace(*str); str++);
		if (strcmp(str, "on") == 0) {
			/* switch on statistics profiling */
			dasd_profile_level = DASD_PROFILE_ON;
			MESSAGE(KERN_INFO, "%s", "Statistics switched on");
		} else if (strcmp(str, "off") == 0) {
			/* switch off and reset statistics profiling */
			memset(&dasd_global_profile,
			       0, sizeof (struct dasd_profile_info_t));
			dasd_profile_level = DASD_PROFILE_OFF;
			MESSAGE(KERN_INFO, "%s", "Statistics switched off");
		} else
			goto out_error;
	} else if (strncmp(str, "reset", 5) == 0) {
		/* reset the statistics */
		memset(&dasd_global_profile, 0,
		       sizeof (struct dasd_profile_info_t));
		MESSAGE(KERN_INFO, "%s", "Statistics reset");
	} else
		goto out_error;
	kfree(buffer);
	return user_len;
out_error:
	MESSAGE(KERN_WARNING, "%s",
		"/proc/dasd/statistics: only 'set on', 'set off' "
		"and 'reset' are supported verbs");
	kfree(buffer);
	return -EINVAL;
#else
	MESSAGE(KERN_WARNING, "%s",
		"/proc/dasd/statistics: is not activated in this kernel");
	return user_len;
#endif				/* CONFIG_DASD_PROFILE */
}

int
dasd_proc_init(void)
{
	dasd_proc_root_entry = proc_mkdir("dasd", &proc_root);
	dasd_proc_root_entry->owner = THIS_MODULE;
	dasd_devices_entry = create_proc_entry("devices",
					       S_IFREG | S_IRUGO | S_IWUSR,
					       dasd_proc_root_entry);
	dasd_devices_entry->proc_fops = &dasd_devices_file_ops;
	dasd_devices_entry->owner = THIS_MODULE;
	dasd_statistics_entry = create_proc_entry("statistics",
						  S_IFREG | S_IRUGO | S_IWUSR,
						  dasd_proc_root_entry);
	dasd_statistics_entry->read_proc = dasd_statistics_read;
	dasd_statistics_entry->write_proc = dasd_statistics_write;
	dasd_statistics_entry->owner = THIS_MODULE;
	return 0;
}

void
dasd_proc_exit(void)
{
	remove_proc_entry("devices", dasd_proc_root_entry);
	remove_proc_entry("statistics", dasd_proc_root_entry);
	remove_proc_entry("dasd", &proc_root);
}
