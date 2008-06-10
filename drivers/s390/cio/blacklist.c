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
static int blacklist_range(range_action action, unsigned int from_ssid,
			   unsigned int to_ssid, unsigned int from,
			   unsigned int to, int msgtrigger)
{
	if ((from_ssid > to_ssid) || ((from_ssid == to_ssid) && (from > to))) {
		if (msgtrigger)
			printk(KERN_WARNING "cio: Invalid cio_ignore range "
			       "0.%x.%04x-0.%x.%04x\n", from_ssid, from,
			       to_ssid, to);
		return 1;
	}

	while ((from_ssid < to_ssid) || ((from_ssid == to_ssid) &&
	       (from <= to))) {
		if (action == add)
			set_bit(from, bl_dev[from_ssid]);
		else
			clear_bit(from, bl_dev[from_ssid]);
		from++;
		if (from > __MAX_SUBCHANNEL) {
			from_ssid++;
			from = 0;
		}
	}

	return 0;
}

static int pure_hex(char **cp, unsigned int *val, int min_digit,
		    int max_digit, int max_val)
{
	int diff;
	unsigned int value;

	diff = 0;
	*val = 0;

	while (isxdigit(**cp) && (diff <= max_digit)) {

		if (isdigit(**cp))
			value = **cp - '0';
		else
			value = tolower(**cp) - 'a' + 10;
		*val = *val * 16 + value;
		(*cp)++;
		diff++;
	}

	if ((diff < min_digit) || (diff > max_digit) || (*val > max_val))
		return 1;

	return 0;
}

static int parse_busid(char *str, int *cssid, int *ssid, int *devno,
		       int msgtrigger)
{
	char *str_work;
	int val, rc, ret;

	rc = 1;

	if (*str == '\0')
		goto out;

	/* old style */
	str_work = str;
	val = simple_strtoul(str, &str_work, 16);

	if (*str_work == '\0') {
		if (val <= __MAX_SUBCHANNEL) {
			*devno = val;
			*ssid = 0;
			*cssid = 0;
			rc = 0;
		}
		goto out;
	}

	/* new style */
	str_work = str;
	ret = pure_hex(&str_work, cssid, 1, 2, __MAX_CSSID);
	if (ret || (str_work[0] != '.'))
		goto out;
	str_work++;
	ret = pure_hex(&str_work, ssid, 1, 1, __MAX_SSID);
	if (ret || (str_work[0] != '.'))
		goto out;
	str_work++;
	ret = pure_hex(&str_work, devno, 4, 4, __MAX_SUBCHANNEL);
	if (ret || (str_work[0] != '\0'))
		goto out;

	rc = 0;
out:
	if (rc && msgtrigger)
		printk(KERN_WARNING "cio: Invalid cio_ignore device '%s'\n",
		       str);

	return rc;
}

static int blacklist_parse_parameters(char *str, range_action action,
				      int msgtrigger)
{
	int from_cssid, to_cssid, from_ssid, to_ssid, from, to;
	int rc, totalrc;
	char *parm;
	range_action ra;

	totalrc = 0;

	while ((parm = strsep(&str, ","))) {
		rc = 0;
		ra = action;
		if (*parm == '!') {
			if (ra == add)
				ra = free;
			else
				ra = add;
			parm++;
		}
		if (strcmp(parm, "all") == 0) {
			from_cssid = 0;
			from_ssid = 0;
			from = 0;
			to_cssid = __MAX_CSSID;
			to_ssid = __MAX_SSID;
			to = __MAX_SUBCHANNEL;
		} else {
			rc = parse_busid(strsep(&parm, "-"), &from_cssid,
					 &from_ssid, &from, msgtrigger);
			if (!rc) {
				if (parm != NULL)
					rc = parse_busid(parm, &to_cssid,
							 &to_ssid, &to,
							 msgtrigger);
				else {
					to_cssid = from_cssid;
					to_ssid = from_ssid;
					to = from;
				}
			}
		}
		if (!rc) {
			rc = blacklist_range(ra, from_ssid, to_ssid, from, to,
					     msgtrigger);
			if (rc)
				totalrc = 1;
		} else
			totalrc = 1;
	}

	return totalrc;
}

static int __init
blacklist_setup (char *str)
{
	CIO_MSG_EVENT(6, "Reading blacklist parameters\n");
	if (blacklist_parse_parameters(str, add, 1))
		return 0;
	return 1;
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
static int blacklist_parse_proc_parameters(char *buf)
{
	int rc;
	char *parm;

	parm = strsep(&buf, " ");

	if (strcmp("free", parm) == 0)
		rc = blacklist_parse_parameters(buf, free, 0);
	else if (strcmp("add", parm) == 0)
		rc = blacklist_parse_parameters(buf, add, 0);
	else
		return 1;

	css_schedule_reprobe();

	return rc;
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
	size_t i;
	ssize_t rc, ret;

	if (*offset)
		return -EINVAL;
	if (user_len > 65536)
		user_len = 65536;
	buf = vmalloc (user_len + 1); /* maybe better use the stack? */
	if (buf == NULL)
		return -ENOMEM;
	memset(buf, 0, user_len + 1);

	if (strncpy_from_user (buf, user_buf, user_len) < 0) {
		rc = -EFAULT;
		goto out_free;
	}

	i = user_len - 1;
	while ((i >= 0) && (isspace(buf[i]) || (buf[i] == 0))) {
		buf[i] = '\0';
		i--;
	}
	ret = blacklist_parse_proc_parameters(buf);
	if (ret)
		rc = -EINVAL;
	else
		rc = user_len;

out_free:
	vfree (buf);
	return rc;
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
