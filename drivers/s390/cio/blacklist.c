/*
 *  drivers/s390/cio/blacklist.c
 *   S/390 common I/O routines -- blacklisting of specific devices
 *
 *    Copyright (C) 1999-2002 IBM Deutschland Entwicklung GmbH,
 *			      IBM Corporation
 *    Author(s): Ingo Adlung (adlung@de.ibm.com)
 *		 Cornelia Huck (cornelia.huck@de.ibm.com)
 *		 Arnd Bergmann (arndb@de.ibm.com)
 */

#include <linux/init.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/ctype.h>
#include <linux/device.h>

#include <asm/cio.h>
#include <asm/uaccess.h>

#include "blacklist.h"
#include "cio.h"
#include "cio_debug.h"
#include "css.h"

/*
 * "Blacklisting" of certain devices:
 * Device numbers given in the commandline as cio_ignore=... won't be known
 * to Linux.
 *
 * These can be single devices or ranges of devices
 */

/* 65536 bits for each set to indicate if a devno is blacklisted or not */
#define __BL_DEV_WORDS ((__MAX_SUBCHANNEL + (8*sizeof(long) - 1)) / \
			 (8*sizeof(long)))
static unsigned long bl_dev[__MAX_SSID + 1][__BL_DEV_WORDS];
typedef enum {add, free} range_action;

/*
 * Function: blacklist_range
 * (Un-)blacklist the devices from-to
 */
static void
blacklist_range (range_action action, unsigned int from, unsigned int to,
		 unsigned int ssid)
{
	if (!to)
		to = from;

	if (from > to || to > __MAX_SUBCHANNEL || ssid > __MAX_SSID) {
		printk (KERN_WARNING "cio: Invalid blacklist range "
			"0.%x.%04x to 0.%x.%04x, skipping\n",
			ssid, from, ssid, to);
		return;
	}
	for (; from <= to; from++) {
		if (action == add)
			set_bit (from, bl_dev[ssid]);
		else
			clear_bit (from, bl_dev[ssid]);
	}
}

/*
 * Function: blacklist_busid
 * Get devno/busid from given string.
 * Shamelessly grabbed from dasd_devmap.c.
 */
static int
blacklist_busid(char **str, int *id0, int *ssid, int *devno)
{
	int val, old_style;
	char *sav;

	sav = *str;

	/* check for leading '0x' */
	old_style = 0;
	if ((*str)[0] == '0' && (*str)[1] == 'x') {
		*str += 2;
		old_style = 1;
	}
	if (!isxdigit((*str)[0]))	/* We require at least one hex digit */
		goto confused;
	val = simple_strtoul(*str, str, 16);
	if (old_style || (*str)[0] != '.') {
		*id0 = *ssid = 0;
		if (val < 0 || val > 0xffff)
			goto confused;
		*devno = val;
		if ((*str)[0] != ',' && (*str)[0] != '-' &&
		    (*str)[0] != '\n' && (*str)[0] != '\0')
			goto confused;
		return 0;
	}
	/* New style x.y.z busid */
	if (val < 0 || val > 0xff)
		goto confused;
	*id0 = val;
	(*str)++;
	if (!isxdigit((*str)[0]))	/* We require at least one hex digit */
		goto confused;
	val = simple_strtoul(*str, str, 16);
	if (val < 0 || val > 0xff || (*str)++[0] != '.')
		goto confused;
	*ssid = val;
	if (!isxdigit((*str)[0]))	/* We require at least one hex digit */
		goto confused;
	val = simple_strtoul(*str, str, 16);
	if (val < 0 || val > 0xffff)
		goto confused;
	*devno = val;
	if ((*str)[0] != ',' && (*str)[0] != '-' &&
	    (*str)[0] != '\n' && (*str)[0] != '\0')
		goto confused;
	return 0;
confused:
	strsep(str, ",\n");
	printk(KERN_WARNING "cio: Invalid cio_ignore parameter '%s'\n", sav);
	return 1;
}

static int
blacklist_parse_parameters (char *str, range_action action)
{
	int from, to, from_id0, to_id0, from_ssid, to_ssid;

	while (*str != 0 && *str != '\n') {
		range_action ra = action;
		while(*str == ',')
			str++;
		if (*str == '!') {
			ra = !action;
			++str;
		}

		/*
		 * Since we have to parse the proc commands and the
		 * kernel arguments we have to check four cases
		 */
		if (strncmp(str,"all,",4) == 0 || strcmp(str,"all") == 0 ||
		    strncmp(str,"all\n",4) == 0 || strncmp(str,"all ",4) == 0) {
			int j;

			str += 3;
			for (j=0; j <= __MAX_SSID; j++)
				blacklist_range(ra, 0, __MAX_SUBCHANNEL, j);
		} else {
			int rc;

			rc = blacklist_busid(&str, &from_id0,
					     &from_ssid, &from);
			if (rc)
				continue;
			to = from;
			to_id0 = from_id0;
			to_ssid = from_ssid;
			if (*str == '-') {
				str++;
				rc = blacklist_busid(&str, &to_id0,
						     &to_ssid, &to);
				if (rc)
					continue;
			}
			if (*str == '-') {
				printk(KERN_WARNING "cio: invalid cio_ignore "
					"parameter '%s'\n",
					strsep(&str, ",\n"));
				continue;
			}
			if ((from_id0 != to_id0) ||
			    (from_ssid != to_ssid)) {
				printk(KERN_WARNING "cio: invalid cio_ignore "
				       "range %x.%x.%04x-%x.%x.%04x\n",
				       from_id0, from_ssid, from,
				       to_id0, to_ssid, to);
				continue;
			}
			blacklist_range (ra, from, to, to_ssid);
		}
	}
	return 1;
}

/* Parsing the commandline for blacklist parameters, e.g. to blacklist
 * bus ids 0.0.1234, 0.0.1235 and 0.0.1236, you could use any of:
 * - cio_ignore=1234-1236
 * - cio_ignore=0x1234-0x1235,1236
 * - cio_ignore=0x1234,1235-1236
 * - cio_ignore=1236 cio_ignore=1234-0x1236
 * - cio_ignore=1234 cio_ignore=1236 cio_ignore=0x1235
 * - cio_ignore=0.0.1234-0.0.1236
 * - cio_ignore=0.0.1234,0x1235,1236
 * - ...
 */
static int __init
blacklist_setup (char *str)
{
	CIO_MSG_EVENT(6, "Reading blacklist parameters\n");
	return blacklist_parse_parameters (str, add);
}

__setup ("cio_ignore=", blacklist_setup);

/* Checking if devices are blacklisted */

/*
 * Function: is_blacklisted
 * Returns 1 if the given devicenumber can be found in the blacklist,
 * otherwise 0.
 * Used by validate_subchannel()
 */
int
is_blacklisted (int ssid, int devno)
{
	return test_bit (devno, bl_dev[ssid]);
}

#ifdef CONFIG_PROC_FS
/*
 * Function: blacklist_parse_proc_parameters
 * parse the stuff which is piped to /proc/cio_ignore
 */
static void
blacklist_parse_proc_parameters (char *buf)
{
	if (strncmp (buf, "free ", 5) == 0) {
		blacklist_parse_parameters (buf + 5, free);
	} else if (strncmp (buf, "add ", 4) == 0) {
		/* 
		 * We don't need to check for known devices since
		 * css_probe_device will handle this correctly. 
		 */
		blacklist_parse_parameters (buf + 4, add);
	} else {
		printk (KERN_WARNING "cio: cio_ignore: Parse error; \n"
			KERN_WARNING "try using 'free all|<devno-range>,"
				     "<devno-range>,...'\n"
			KERN_WARNING "or 'add <devno-range>,"
				     "<devno-range>,...'\n");
		return;
	}

	css_schedule_reprobe();
}

/* Iterator struct for all devices. */
struct ccwdev_iter {
	int devno;
	int ssid;
	int in_range;
};

static void *
cio_ignore_proc_seq_start(struct seq_file *s, loff_t *offset)
{
	struct ccwdev_iter *iter;

	if (*offset >= (__MAX_SUBCHANNEL + 1) * (__MAX_SSID + 1))
		return NULL;
	iter = kzalloc(sizeof(struct ccwdev_iter), GFP_KERNEL);
	if (!iter)
		return ERR_PTR(-ENOMEM);
	iter->ssid = *offset / (__MAX_SUBCHANNEL + 1);
	iter->devno = *offset % (__MAX_SUBCHANNEL + 1);
	return iter;
}

static void
cio_ignore_proc_seq_stop(struct seq_file *s, void *it)
{
	if (!IS_ERR(it))
		kfree(it);
}

static void *
cio_ignore_proc_seq_next(struct seq_file *s, void *it, loff_t *offset)
{
	struct ccwdev_iter *iter;

	if (*offset >= (__MAX_SUBCHANNEL + 1) * (__MAX_SSID + 1))
		return NULL;
	iter = it;
	if (iter->devno == __MAX_SUBCHANNEL) {
		iter->devno = 0;
		iter->ssid++;
		if (iter->ssid > __MAX_SSID)
			return NULL;
	} else
		iter->devno++;
	(*offset)++;
	return iter;
}

static int
cio_ignore_proc_seq_show(struct seq_file *s, void *it)
{
	struct ccwdev_iter *iter;

	iter = it;
	if (!is_blacklisted(iter->ssid, iter->devno))
		/* Not blacklisted, nothing to output. */
		return 0;
	if (!iter->in_range) {
		/* First device in range. */
		if ((iter->devno == __MAX_SUBCHANNEL) ||
		    !is_blacklisted(iter->ssid, iter->devno + 1))
			/* Singular device. */
			return seq_printf(s, "0.%x.%04x\n",
					  iter->ssid, iter->devno);
		iter->in_range = 1;
		return seq_printf(s, "0.%x.%04x-", iter->ssid, iter->devno);
	}
	if ((iter->devno == __MAX_SUBCHANNEL) ||
	    !is_blacklisted(iter->ssid, iter->devno + 1)) {
		/* Last device in range. */
		iter->in_range = 0;
		return seq_printf(s, "0.%x.%04x\n", iter->ssid, iter->devno);
	}
	return 0;
}

static ssize_t
cio_ignore_write(struct file *file, const char __user *user_buf,
		 size_t user_len, loff_t *offset)
{
	char *buf;

	if (*offset)
		return -EINVAL;
	if (user_len > 65536)
		user_len = 65536;
	buf = vmalloc (user_len + 1); /* maybe better use the stack? */
	if (buf == NULL)
		return -ENOMEM;
	if (strncpy_from_user (buf, user_buf, user_len) < 0) {
		vfree (buf);
		return -EFAULT;
	}
	buf[user_len] = '\0';

	blacklist_parse_proc_parameters (buf);

	vfree (buf);
	return user_len;
}

static const struct seq_operations cio_ignore_proc_seq_ops = {
	.start = cio_ignore_proc_seq_start,
	.stop  = cio_ignore_proc_seq_stop,
	.next  = cio_ignore_proc_seq_next,
	.show  = cio_ignore_proc_seq_show,
};

static int
cio_ignore_proc_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &cio_ignore_proc_seq_ops);
}

static const struct file_operations cio_ignore_proc_fops = {
	.open    = cio_ignore_proc_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release,
	.write   = cio_ignore_write,
};

static int
cio_ignore_proc_init (void)
{
	struct proc_dir_entry *entry;

	entry = proc_create("cio_ignore", S_IFREG | S_IRUGO | S_IWUSR, NULL,
			    &cio_ignore_proc_fops);
	if (!entry)
		return -ENOENT;
	return 0;
}

__initcall (cio_ignore_proc_init);

#endif /* CONFIG_PROC_FS */
